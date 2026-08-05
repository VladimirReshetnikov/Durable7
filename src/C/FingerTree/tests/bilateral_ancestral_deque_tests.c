/*
 * Tests for the bilateral ancestral deque.
 *
 * The obligations mirror the C# suite: model equivalence against an immutable array oracle over
 * exhaustive construction words and randomized retained branches, retained-version independence
 * after branching, reversal and slice-closure laws, boundary and empty cases, failure atomicity
 * under failing callbacks and allocations, and the level-ancestor query ceilings the arena's
 * statistics counters exist to pin.
 */

#include <durable7/finger_tree/bilateral_ancestral_deque.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static const unsigned char g_value_type_identity = 0;

static void fail_at(const char* file, int line, const char* expression)
{
    (void)fprintf(stderr, "%s:%d: requirement failed: %s\n", file, line, expression);
    ++g_failures;
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail_at(__FILE__, __LINE__, #expression); \
            return; \
        } \
    } while (0)

#define REQUIRE_STATUS(expression, expected) \
    do { \
        const ft_status actual_status__ = (expression); \
        if (actual_status__ != (expected)) { \
            (void)fprintf(stderr, "%s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #expression, (int)actual_status__, (int)(expected)); \
            ++g_failures; \
            return; \
        } \
    } while (0)

/* The widest model any test compares against; the randomized histories cannot exceed it. */
#define MODEL_CAPACITY 1100

typedef struct test_value {
    int value;
} test_value;

typedef struct test_context {
    size_t allocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t live_values;
    size_t fail_copy_at;
} test_context;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* allocation = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 && state->allocation_calls == state->fail_allocation_at) {
        return NULL;
    }
    allocation = malloc(size);
    if (allocation != NULL) {
        ++state->outstanding_allocations;
    }
    return allocation;
}

static void tracked_deallocate(void* allocation, void* context)
{
    test_context* state = (test_context*)context;
    if (allocation != NULL) {
        --state->outstanding_allocations;
        free(allocation);
    }
}

static ft_status tracked_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->copy_calls;
    if (state->fail_copy_at != 0 && state->copy_calls == state->fail_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_value*)destination = *(const test_value*)source;
    ++state->live_values;
    return FT_STATUS_OK;
}

static void tracked_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    --state->live_values;
}

static void init_context(test_context* context)
{
    (void)memset(context, 0, sizeof(*context));
}

static ft_status create_policy(ft_incremental_ancestor_policy* policy, test_context* context)
{
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy_config_init(
        &config,
        sizeof(test_value),
        &g_value_type_identity,
        context);
    config.copy = tracked_copy;
    config.destroy = tracked_destroy;
    config.allocator.allocate = tracked_allocate;
    config.allocator.deallocate = tracked_deallocate;
    config.allocator.context = context;
    return ft_incremental_ancestor_policy_create(policy, &config);
}

/* A deterministic xorshift generator; the tests must not depend on the platform's rand(). */
typedef struct xorshift {
    uint64_t state;
} xorshift;

static void xorshift_init(xorshift* random, uint64_t seed)
{
    random->state = seed | 1u;
}

static size_t xorshift_below(xorshift* random, size_t bound)
{
    random->state ^= random->state << 13;
    random->state ^= random->state >> 7;
    random->state ^= random->state << 17;
    return (size_t)(random->state % (uint64_t)bound);
}

static test_value make_value(int value)
{
    test_value result;
    result.value = value;
    return result;
}

/* Reads the policy configuration back out of a deque, which is how a consumer holding only a handle
 * destroys the owned values the copy accessors hand it. */
static bool deque_config(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_policy_config* config)
{
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_policy policy;
    bool ok = false;
    if (ft_bilateral_deque_get_arena(deque, &arena) != FT_STATUS_OK) {
        return false;
    }
    if (ft_incremental_ancestor_arena_get_policy(&arena, &policy) == FT_STATUS_OK) {
        ok = ft_incremental_ancestor_policy_get_config(&policy, config) == FT_STATUS_OK;
        ft_incremental_ancestor_policy_dispose(&policy);
    }
    ft_incremental_ancestor_arena_dispose(&arena);
    return ok;
}

static void destroy_owned(
    const ft_incremental_ancestor_policy_config* config,
    test_value* values,
    size_t count)
{
    size_t index = 0;
    if (config->destroy == NULL) {
        return;
    }
    for (index = 0; index != count; ++index) {
        config->destroy(&values[index], config->callback_context);
    }
}

/* Test-only representation identity: the same arena and the same cached intervals. The C# tests
 * assert reference identity for the operations that return their receiver; a C handle is a value, so
 * the equivalent observation is that the produced handle has exactly the same representation. */
static bool same_version(const ft_bilateral_deque* left, const ft_bilateral_deque* right)
{
    return ft_bilateral_deque_root_identity(left) == ft_bilateral_deque_root_identity(right) &&
        ft_bilateral_deque_root_identity(left) != NULL &&
        left->left.anchor == right->left.anchor && left->left.base == right->left.base &&
        left->left.tail == right->left.tail && left->left.count == right->left.count &&
        left->right.anchor == right->right.anchor && left->right.base == right->right.base &&
        left->right.tail == right->right.tail && left->right.count == right->right.count &&
        left->count == right->count;
}

typedef struct collect_context {
    int* values;
    size_t capacity;
    size_t count;
    size_t fail_at;
} collect_context;

static ft_status collect_visitor(const void* value, void* context)
{
    collect_context* state = (collect_context*)context;
    if (state->fail_at != 0 && state->count + 1 == state->fail_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (state->count == state->capacity) {
        return FT_STATUS_OVERFLOW;
    }
    state->values[state->count] = ((const test_value*)value)->value;
    ++state->count;
    return FT_STATUS_OK;
}

/* Asserts the complete observable contract of one handle against a sequence oracle: size, the two
 * traversals, every index, both endpoints, the out-of-range boundary, and the structural audit. */
static bool check_deque(const int* expected, size_t length, const ft_bilateral_deque* deque)
{
    ft_incremental_ancestor_policy_config config;
    ft_bilateral_deque_statistics statistics;
    collect_context collected;
    static int visited[MODEL_CAPACITY];
    static test_value copies[MODEL_CAPACITY];
    const void* borrowed = NULL;
    test_value owned;
    bool valid = false;
    bool ok = true;
    size_t index = 0;

    if (length > MODEL_CAPACITY || !deque_config(deque, &config)) {
        return false;
    }
    if (ft_bilateral_deque_size(deque) != length ||
        ft_bilateral_deque_empty(deque) != (length == 0)) {
        return false;
    }

    collected.values = visited;
    collected.capacity = MODEL_CAPACITY;
    collected.count = 0;
    collected.fail_at = 0;
    if (ft_bilateral_deque_visit(deque, collect_visitor, &collected) != FT_STATUS_OK ||
        collected.count != length) {
        return false;
    }
    for (index = 0; index != length; ++index) {
        if (visited[index] != expected[index]) {
            return false;
        }
    }

    if (ft_bilateral_deque_copy_to_array(deque, copies, MODEL_CAPACITY) != FT_STATUS_OK) {
        return false;
    }
    for (index = 0; index != length; ++index) {
        if (copies[index].value != expected[index]) {
            ok = false;
        }
    }
    destroy_owned(&config, copies, length);
    if (!ok) {
        return false;
    }

    for (index = 0; index != length; ++index) {
        if (ft_bilateral_deque_at_ref(deque, index, &borrowed) != FT_STATUS_OK ||
            ((const test_value*)borrowed)->value != expected[index]) {
            return false;
        }
        if (ft_bilateral_deque_at_copy(deque, index, &owned) != FT_STATUS_OK) {
            return false;
        }
        ok = owned.value == expected[index];
        destroy_owned(&config, &owned, 1);
        if (!ok) {
            return false;
        }
    }
    if (ft_bilateral_deque_at_ref(deque, length, &borrowed) != FT_STATUS_OUT_OF_RANGE) {
        return false;
    }

    if (length == 0) {
        if (ft_bilateral_deque_first_ref(deque, &borrowed) != FT_STATUS_EMPTY ||
            ft_bilateral_deque_last_ref(deque, &borrowed) != FT_STATUS_EMPTY ||
            ft_bilateral_deque_first_copy(deque, &owned) != FT_STATUS_EMPTY ||
            ft_bilateral_deque_last_copy(deque, &owned) != FT_STATUS_EMPTY) {
            return false;
        }
    } else {
        if (ft_bilateral_deque_first_ref(deque, &borrowed) != FT_STATUS_OK ||
            ((const test_value*)borrowed)->value != expected[0]) {
            return false;
        }
        if (ft_bilateral_deque_last_ref(deque, &borrowed) != FT_STATUS_OK ||
            ((const test_value*)borrowed)->value != expected[length - 1]) {
            return false;
        }
        if (ft_bilateral_deque_first_copy(deque, &owned) != FT_STATUS_OK) {
            return false;
        }
        ok = owned.value == expected[0];
        destroy_owned(&config, &owned, 1);
        if (!ok) {
            return false;
        }
        if (ft_bilateral_deque_last_copy(deque, &owned) != FT_STATUS_OK) {
            return false;
        }
        ok = owned.value == expected[length - 1];
        destroy_owned(&config, &owned, 1);
        if (!ok) {
            return false;
        }
    }

    if (ft_bilateral_deque_validate(deque, &valid, &statistics) != FT_STATUS_OK || !valid) {
        return false;
    }
    return statistics.count == length &&
        statistics.left_count + statistics.right_count == length;
}

static bool push_front_value(ft_bilateral_deque* deque, int value)
{
    const test_value stored = make_value(value);
    return ft_bilateral_deque_push_front(deque, &stored, deque) == FT_STATUS_OK;
}

static bool push_back_value(ft_bilateral_deque* deque, int value)
{
    const test_value stored = make_value(value);
    return ft_bilateral_deque_push_back(deque, &stored, deque) == FT_STATUS_OK;
}

/* Reads the shipped arena's deterministic level-ancestor query counter. */
static bool arena_query_count(const ft_incremental_ancestor_arena* arena, uint64_t* count)
{
    ft_incremental_ancestor_myers_statistics statistics;
    if (ft_incremental_ancestor_myers_arena_get_statistics(arena, &statistics) != FT_STATUS_OK) {
        return false;
    }
    *count = statistics.ancestor_query_count;
    return true;
}

static void test_an_empty_deque_has_stable_reads_failures_and_identity(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque empty;
    ft_bilateral_deque produced;
    ft_bilateral_deque_split_result split;
    ft_bilateral_deque built;
    const test_value stored = make_value(7);
    const int expected[3] = {1, 2, 3};
    test_value values[3];
    bool removed = true;
    test_value popped;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&empty, &policy), FT_STATUS_OK);

    REQUIRE(check_deque(NULL, 0, &empty));
    REQUIRE(ft_bilateral_deque_empty(&empty));
    REQUIRE(ft_bilateral_deque_size(&empty) == 0);

    /* The operations C# answers with its receiver reproduce the receiver's exact representation. */
    REQUIRE_STATUS(ft_bilateral_deque_clear(&empty, &produced), FT_STATUS_OK);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_take(&empty, 0, &produced), FT_STATUS_OK);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_drop(&empty, 0, &produced), FT_STATUS_OK);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&empty, 0, 0, &produced), FT_STATUS_OK);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&empty, &produced), FT_STATUS_OK);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_split_at(&empty, 0, &split), FT_STATUS_OK);
    REQUIRE(same_version(&split.left, &empty));
    REQUIRE(same_version(&split.right, &empty));
    ft_bilateral_deque_dispose(&split.left);
    ft_bilateral_deque_dispose(&split.right);

    /* Removals from an empty deque fail without touching anything. */
    REQUIRE_STATUS(ft_bilateral_deque_remove_first(&empty, &produced), FT_STATUS_EMPTY);
    REQUIRE_STATUS(ft_bilateral_deque_remove_last(&empty, &produced), FT_STATUS_EMPTY);
    popped = make_value(-1);
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_front(&empty, &removed, &popped, &produced),
        FT_STATUS_OK);
    REQUIRE(!removed);
    REQUIRE(popped.value == -1);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);
    removed = true;
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_back(&empty, &removed, &popped, &produced),
        FT_STATUS_OK);
    REQUIRE(!removed);
    REQUIRE(popped.value == -1);
    REQUIRE(same_version(&produced, &empty));
    ft_bilateral_deque_dispose(&produced);

    /* Boundaries beyond an empty deque are rejected. */
    REQUIRE_STATUS(ft_bilateral_deque_take(&empty, 1, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_drop(&empty, 1, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_split_at(&empty, 1, &split), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&empty, 0, 1, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&empty, 1, 0, &produced), FT_STATUS_OUT_OF_RANGE);

    /* An empty handle stays appendable, and the extension leaves it empty. */
    REQUIRE_STATUS(ft_bilateral_deque_push_front(&empty, &stored, &produced), FT_STATUS_OK);
    REQUIRE(check_deque(&stored.value, 1, &produced));
    REQUIRE(check_deque(NULL, 0, &empty));
    ft_bilateral_deque_dispose(&produced);

    values[0] = make_value(1);
    values[1] = make_value(2);
    values[2] = make_value(3);
    REQUIRE_STATUS(ft_bilateral_deque_from_array(&built, &policy, values, 3), FT_STATUS_OK);
    REQUIRE(check_deque(expected, 3, &built));
    ft_bilateral_deque_dispose(&built);
    REQUIRE_STATUS(ft_bilateral_deque_from_array(&built, &policy, NULL, 0), FT_STATUS_OK);
    REQUIRE(check_deque(NULL, 0, &built));
    ft_bilateral_deque_dispose(&built);

    ft_bilateral_deque_dispose(&empty);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_endpoint_operations_retain_every_prior_branch(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque root;
    ft_bilateral_deque one;
    ft_bilateral_deque two;
    ft_bilateral_deque three;
    ft_bilateral_deque four;
    ft_bilateral_deque five;
    ft_bilateral_deque produced;
    ft_bilateral_deque branch;
    const test_value ten = make_value(10);
    const test_value twenty = make_value(20);
    const test_value five_value = make_value(5);
    const test_value one_value = make_value(1);
    const test_value thirty = make_value(30);
    const int model_one[1] = {10};
    const int model_two[2] = {10, 20};
    const int model_three[3] = {5, 10, 20};
    const int model_four[4] = {1, 5, 10, 20};
    const int model_five[5] = {1, 5, 10, 20, 30};
    const int without_first[4] = {5, 10, 20, 30};
    const int left_branch[3] = {-1, 10, 20};
    const int right_branch[3] = {10, 20, 99};
    const int cleared_model[2] = {-7, 8};
    test_value popped;
    bool removed = false;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&root, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_push_back(&root, &ten, &one), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_push_back(&one, &twenty, &two), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_push_front(&two, &five_value, &three), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_push_front(&three, &one_value, &four), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_push_back(&four, &thirty, &five), FT_STATUS_OK);

    REQUIRE(check_deque(NULL, 0, &root));
    REQUIRE(check_deque(model_one, 1, &one));
    REQUIRE(check_deque(model_two, 2, &two));
    REQUIRE(check_deque(model_three, 3, &three));
    REQUIRE(check_deque(model_four, 4, &four));
    REQUIRE(check_deque(model_five, 5, &five));

    REQUIRE_STATUS(ft_bilateral_deque_remove_first(&five, &produced), FT_STATUS_OK);
    REQUIRE(check_deque(without_first, 4, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_remove_last(&five, &produced), FT_STATUS_OK);
    REQUIRE(check_deque(model_four, 4, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE(check_deque(model_five, 5, &five));

    /* Removals that walk one arm empty and cross the centre. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &produced), FT_STATUS_OK);
    REQUIRE(push_back_value(&produced, 1));
    REQUIRE(push_back_value(&produced, 2));
    REQUIRE(push_back_value(&produced, 3));
    {
        const int right_only[3] = {1, 2, 3};
        const int after_one[2] = {2, 3};
        const int after_two[1] = {3};
        REQUIRE(check_deque(right_only, 3, &produced));
        REQUIRE_STATUS(ft_bilateral_deque_remove_first(&produced, &branch), FT_STATUS_OK);
        REQUIRE(check_deque(after_one, 2, &branch));
        REQUIRE_STATUS(ft_bilateral_deque_remove_first(&branch, &branch), FT_STATUS_OK);
        REQUIRE(check_deque(after_two, 1, &branch));
        REQUIRE_STATUS(ft_bilateral_deque_remove_first(&branch, &branch), FT_STATUS_OK);
        REQUIRE(check_deque(NULL, 0, &branch));
        REQUIRE(check_deque(right_only, 3, &produced));
        ft_bilateral_deque_dispose(&branch);
    }
    ft_bilateral_deque_dispose(&produced);

    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &produced), FT_STATUS_OK);
    REQUIRE(push_front_value(&produced, 3));
    REQUIRE(push_front_value(&produced, 2));
    REQUIRE(push_front_value(&produced, 1));
    {
        const int left_only[3] = {1, 2, 3};
        const int without_last[2] = {1, 2};
        const int without_two[1] = {1};
        REQUIRE(check_deque(left_only, 3, &produced));
        REQUIRE_STATUS(ft_bilateral_deque_remove_last(&produced, &branch), FT_STATUS_OK);
        REQUIRE(check_deque(without_last, 2, &branch));
        REQUIRE_STATUS(ft_bilateral_deque_remove_last(&branch, &branch), FT_STATUS_OK);
        REQUIRE(check_deque(without_two, 1, &branch));
        REQUIRE_STATUS(ft_bilateral_deque_remove_last(&branch, &branch), FT_STATUS_OK);
        REQUIRE(check_deque(NULL, 0, &branch));
        ft_bilateral_deque_dispose(&branch);
    }
    ft_bilateral_deque_dispose(&produced);

    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_front(&five, &removed, &popped, &produced),
        FT_STATUS_OK);
    REQUIRE(removed);
    REQUIRE(popped.value == 1);
    tracked_destroy(&popped, &context);
    REQUIRE(check_deque(without_first, 4, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_back(&five, &removed, &popped, &produced),
        FT_STATUS_OK);
    REQUIRE(removed);
    REQUIRE(popped.value == 30);
    tracked_destroy(&popped, &context);
    REQUIRE(check_deque(model_four, 4, &produced));
    ft_bilateral_deque_dispose(&produced);

    /* Exact result/operand aliasing is supported, including a split whose own result member is the
     * operand. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&five, &produced), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&produced, &produced), FT_STATUS_OK);
    {
        const int reversed[5] = {30, 20, 10, 5, 1};
        REQUIRE(check_deque(reversed, 5, &produced));
    }
    ft_bilateral_deque_dispose(&produced);
    {
        ft_bilateral_deque_split_result inplace;
        REQUIRE_STATUS(ft_bilateral_deque_copy(&five, &inplace.left), FT_STATUS_OK);
        REQUIRE_STATUS(ft_bilateral_deque_split_at(&inplace.left, 2, &inplace), FT_STATUS_OK);
        REQUIRE(check_deque(model_five, 2, &inplace.left));
        REQUIRE(check_deque(model_five + 2, 3, &inplace.right));
        ft_bilateral_deque_dispose(&inplace.left);
        ft_bilateral_deque_dispose(&inplace.right);
    }
    REQUIRE(check_deque(model_five, 5, &five));

    /* Two independent branches grow from one retained version. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&two, &produced), FT_STATUS_OK);
    REQUIRE(push_front_value(&produced, -1));
    REQUIRE(check_deque(left_branch, 3, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&two, &produced), FT_STATUS_OK);
    REQUIRE(push_back_value(&produced, 99));
    REQUIRE(check_deque(right_branch, 3, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE(check_deque(model_two, 2, &two));

    REQUIRE_STATUS(ft_bilateral_deque_clear(&five, &produced), FT_STATUS_OK);
    REQUIRE(check_deque(NULL, 0, &produced));
    REQUIRE(push_front_value(&produced, -7));
    REQUIRE(push_back_value(&produced, 8));
    REQUIRE(check_deque(cleared_model, 2, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE(check_deque(model_five, 5, &five));

    ft_bilateral_deque_dispose(&root);
    ft_bilateral_deque_dispose(&one);
    ft_bilateral_deque_dispose(&two);
    ft_bilateral_deque_dispose(&three);
    ft_bilateral_deque_dispose(&four);
    ft_bilateral_deque_dispose(&five);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_reverse_swaps_orientation_without_changing_retained_versions(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque deque;
    ft_bilateral_deque reversed;
    ft_bilateral_deque restored;
    ft_bilateral_deque produced;
    ft_bilateral_deque singleton;
    const int forward[7] = {1, 2, 3, 4, 5, 6, 7};
    const int backward[7] = {7, 6, 5, 4, 3, 2, 1};
    const int grown[9] = {0, 7, 6, 5, 4, 3, 2, 1, 8};
    const int trimmed[5] = {6, 5, 4, 3, 2};
    const int single[1] = {42};

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&deque, &policy), FT_STATUS_OK);
    REQUIRE(push_front_value(&deque, 3));
    REQUIRE(push_front_value(&deque, 2));
    REQUIRE(push_front_value(&deque, 1));
    REQUIRE(push_back_value(&deque, 4));
    REQUIRE(push_back_value(&deque, 5));
    REQUIRE(push_back_value(&deque, 6));
    REQUIRE(push_back_value(&deque, 7));

    REQUIRE_STATUS(ft_bilateral_deque_reverse(&deque, &reversed), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&reversed, &restored), FT_STATUS_OK);
    REQUIRE(check_deque(forward, 7, &deque));
    REQUIRE(check_deque(backward, 7, &reversed));
    REQUIRE(check_deque(forward, 7, &restored));

    REQUIRE_STATUS(ft_bilateral_deque_copy(&reversed, &produced), FT_STATUS_OK);
    REQUIRE(push_front_value(&produced, 0));
    REQUIRE(push_back_value(&produced, 8));
    REQUIRE(check_deque(grown, 9, &produced));
    ft_bilateral_deque_dispose(&produced);

    REQUIRE_STATUS(ft_bilateral_deque_remove_first(&reversed, &produced), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_remove_last(&produced, &produced), FT_STATUS_OK);
    REQUIRE(check_deque(trimmed, 5, &produced));
    ft_bilateral_deque_dispose(&produced);
    REQUIRE(check_deque(forward, 7, &deque));

    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&singleton, &policy), FT_STATUS_OK);
    REQUIRE(push_back_value(&singleton, 42));
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&singleton, &produced), FT_STATUS_OK);
    REQUIRE(check_deque(single, 1, &produced));
    REQUIRE(same_version(&produced, &singleton));
    ft_bilateral_deque_dispose(&produced);
    ft_bilateral_deque_dispose(&singleton);

    ft_bilateral_deque_dispose(&deque);
    ft_bilateral_deque_dispose(&reversed);
    ft_bilateral_deque_dispose(&restored);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_every_slice_and_split_boundary_remains_growable(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque original;
    ft_bilateral_deque slice;
    ft_bilateral_deque grown;
    ft_bilateral_deque_split_result split;
    const int expected[7] = {1, 2, 3, 4, 5, 6, 7};
    int model[9];
    size_t start = 0;
    size_t count = 0;
    size_t index = 0;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&original, &policy), FT_STATUS_OK);
    REQUIRE(push_front_value(&original, 3));
    REQUIRE(push_front_value(&original, 2));
    REQUIRE(push_front_value(&original, 1));
    REQUIRE(push_back_value(&original, 4));
    REQUIRE(push_back_value(&original, 5));
    REQUIRE(push_back_value(&original, 6));
    REQUIRE(push_back_value(&original, 7));

    for (start = 0; start <= 7; ++start) {
        for (count = 0; count + start <= 7; ++count) {
            REQUIRE_STATUS(ft_bilateral_deque_slice(&original, start, count, &slice),
                FT_STATUS_OK);
            REQUIRE(check_deque(expected + start, count, &slice));

            /* Both sliced endpoints must stay appendable: the slice is a bilateral handle. */
            model[0] = -100 - (int)start;
            for (index = 0; index != count; ++index) {
                model[index + 1] = expected[start + index];
            }
            model[count + 1] = 100 + (int)count;
            REQUIRE_STATUS(ft_bilateral_deque_copy(&slice, &grown), FT_STATUS_OK);
            REQUIRE(push_front_value(&grown, model[0]));
            REQUIRE(push_back_value(&grown, model[count + 1]));
            REQUIRE(check_deque(model, count + 2, &grown));
            ft_bilateral_deque_dispose(&grown);
            REQUIRE(check_deque(expected + start, count, &slice));

            model[0] = -1;
            for (index = 0; index != count; ++index) {
                model[index + 1] = expected[start + count - 1 - index];
            }
            model[count + 1] = -2;
            REQUIRE_STATUS(ft_bilateral_deque_reverse(&slice, &grown), FT_STATUS_OK);
            REQUIRE(push_front_value(&grown, -1));
            REQUIRE(push_back_value(&grown, -2));
            REQUIRE(check_deque(model, count + 2, &grown));
            ft_bilateral_deque_dispose(&grown);

            ft_bilateral_deque_dispose(&slice);
        }
    }

    for (start = 0; start <= 7; ++start) {
        REQUIRE_STATUS(ft_bilateral_deque_split_at(&original, start, &split), FT_STATUS_OK);
        REQUIRE(check_deque(expected, start, &split.left));
        REQUIRE(check_deque(expected + start, 7 - start, &split.right));

        for (index = 0; index != start; ++index) {
            model[index] = expected[index];
        }
        model[start] = 88;
        REQUIRE_STATUS(ft_bilateral_deque_copy(&split.left, &grown), FT_STATUS_OK);
        REQUIRE(push_back_value(&grown, 88));
        REQUIRE(check_deque(model, start + 1, &grown));
        ft_bilateral_deque_dispose(&grown);

        model[0] = 77;
        for (index = start; index != 7; ++index) {
            model[index - start + 1] = expected[index];
        }
        REQUIRE_STATUS(ft_bilateral_deque_copy(&split.right, &grown), FT_STATUS_OK);
        REQUIRE(push_front_value(&grown, 77));
        REQUIRE(check_deque(model, 7 - start + 1, &grown));
        ft_bilateral_deque_dispose(&grown);

        ft_bilateral_deque_dispose(&split.left);
        ft_bilateral_deque_dispose(&split.right);
    }

    REQUIRE(check_deque(expected, 7, &original));
    ft_bilateral_deque_dispose(&original);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_small_endpoint_construction_words_match_the_sequence_oracle(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque deque;
    ft_bilateral_deque slice;
    ft_bilateral_deque grown;
    ft_bilateral_deque_split_result split;
    int model[10];
    int expected[10];
    size_t length = 0;
    unsigned word = 0;
    size_t value = 0;
    size_t start = 0;
    size_t count = 0;
    size_t index = 0;
    size_t model_length = 0;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);

    for (length = 0; length <= 8; ++length) {
        for (word = 0; word != (1u << length); ++word) {
            REQUIRE_STATUS(ft_bilateral_deque_init_myers(&deque, &policy), FT_STATUS_OK);
            model_length = 0;
            for (value = 0; value != length; ++value) {
                if ((word & (1u << value)) == 0) {
                    REQUIRE(push_front_value(&deque, (int)value));
                    for (index = model_length; index != 0; --index) {
                        model[index] = model[index - 1];
                    }
                    model[0] = (int)value;
                } else {
                    REQUIRE(push_back_value(&deque, (int)value));
                    model[model_length] = (int)value;
                }
                ++model_length;
            }
            REQUIRE(check_deque(model, model_length, &deque));

            for (start = 0; start <= length; ++start) {
                for (count = 0; count + start <= length; ++count) {
                    REQUIRE_STATUS(
                        ft_bilateral_deque_slice(&deque, start, count, &slice),
                        FT_STATUS_OK);
                    REQUIRE(check_deque(model + start, count, &slice));

                    expected[0] = -1;
                    for (index = 0; index != count; ++index) {
                        expected[index + 1] = model[start + index];
                    }
                    expected[count + 1] = -2;
                    REQUIRE_STATUS(ft_bilateral_deque_copy(&slice, &grown), FT_STATUS_OK);
                    REQUIRE(push_front_value(&grown, -1));
                    REQUIRE(push_back_value(&grown, -2));
                    REQUIRE(check_deque(expected, count + 2, &grown));
                    ft_bilateral_deque_dispose(&grown);
                    ft_bilateral_deque_dispose(&slice);
                }
            }

            for (start = 0; start <= length; ++start) {
                REQUIRE_STATUS(ft_bilateral_deque_split_at(&deque, start, &split), FT_STATUS_OK);
                REQUIRE(check_deque(model, start, &split.left));
                REQUIRE(check_deque(model + start, length - start, &split.right));
                ft_bilateral_deque_dispose(&split.left);
                ft_bilateral_deque_dispose(&split.right);

                REQUIRE_STATUS(ft_bilateral_deque_take(&deque, start, &slice), FT_STATUS_OK);
                REQUIRE(check_deque(model, start, &slice));
                ft_bilateral_deque_dispose(&slice);
                REQUIRE_STATUS(ft_bilateral_deque_drop(&deque, start, &slice), FT_STATUS_OK);
                REQUIRE(check_deque(model + start, length - start, &slice));
                ft_bilateral_deque_dispose(&slice);
            }

            ft_bilateral_deque_dispose(&deque);
        }
    }

    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

typedef struct version {
    ft_bilateral_deque deque;
    int model[MODEL_CAPACITY];
    size_t length;
} version;

#define RANDOM_STEPS 500

static version* g_versions = NULL;

static void run_randomized_history(
    const ft_incremental_ancestor_policy* policy,
    uint64_t seed,
    test_context* context)
{
    xorshift random;
    version* versions = g_versions;
    ft_bilateral_deque produced;
    ft_bilateral_deque_split_result split;
    size_t count = 1;
    size_t step = 0;
    size_t index = 0;
    size_t source_index = 0;
    size_t choice = 0;
    size_t start = 0;
    size_t length = 0;
    int value = 0;
    version* source = NULL;
    version* target = NULL;

    xorshift_init(&random, seed);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&versions[0].deque, policy), FT_STATUS_OK);
    versions[0].length = 0;

    for (step = 0; step != RANDOM_STEPS; ++step) {
        source_index = xorshift_below(&random, count);
        source = &versions[source_index];
        REQUIRE(check_deque(source->model, source->length, &source->deque));
        target = &versions[count];
        value = (int)seed + (int)step * 31;
        choice = xorshift_below(&random, 11);
        if ((choice == 2 || choice == 3) && source->length == 0) {
            choice = 10;
        }
        if ((choice == 0 || choice == 1 || choice == 10) &&
            source->length + 2 > MODEL_CAPACITY) {
            choice = 9;
        }

        switch (choice) {
        case 0: {
            const test_value stored = make_value(value);
            REQUIRE_STATUS(
                ft_bilateral_deque_push_front(&source->deque, &stored, &produced),
                FT_STATUS_OK);
            target->model[0] = value;
            for (index = 0; index != source->length; ++index) {
                target->model[index + 1] = source->model[index];
            }
            target->length = source->length + 1;
            break;
        }
        case 1: {
            const test_value stored = make_value(value);
            REQUIRE_STATUS(
                ft_bilateral_deque_push_back(&source->deque, &stored, &produced),
                FT_STATUS_OK);
            for (index = 0; index != source->length; ++index) {
                target->model[index] = source->model[index];
            }
            target->model[source->length] = value;
            target->length = source->length + 1;
            break;
        }
        case 2:
            REQUIRE_STATUS(
                ft_bilateral_deque_remove_first(&source->deque, &produced),
                FT_STATUS_OK);
            for (index = 1; index != source->length; ++index) {
                target->model[index - 1] = source->model[index];
            }
            target->length = source->length - 1;
            break;
        case 3:
            REQUIRE_STATUS(
                ft_bilateral_deque_remove_last(&source->deque, &produced),
                FT_STATUS_OK);
            for (index = 0; index + 1 != source->length; ++index) {
                target->model[index] = source->model[index];
            }
            target->length = source->length - 1;
            break;
        case 4:
            REQUIRE_STATUS(ft_bilateral_deque_reverse(&source->deque, &produced), FT_STATUS_OK);
            for (index = 0; index != source->length; ++index) {
                target->model[index] = source->model[source->length - 1 - index];
            }
            target->length = source->length;
            break;
        case 5:
            start = xorshift_below(&random, source->length + 1);
            length = xorshift_below(&random, source->length - start + 1);
            REQUIRE_STATUS(
                ft_bilateral_deque_slice(&source->deque, start, length, &produced),
                FT_STATUS_OK);
            for (index = 0; index != length; ++index) {
                target->model[index] = source->model[start + index];
            }
            target->length = length;
            break;
        case 6:
            start = xorshift_below(&random, source->length + 1);
            REQUIRE_STATUS(
                ft_bilateral_deque_split_at(&source->deque, start, &split),
                FT_STATUS_OK);
            if (xorshift_below(&random, 2) == 0) {
                produced = split.left;
                ft_bilateral_deque_dispose(&split.right);
                for (index = 0; index != start; ++index) {
                    target->model[index] = source->model[index];
                }
                target->length = start;
            } else {
                produced = split.right;
                ft_bilateral_deque_dispose(&split.left);
                for (index = start; index != source->length; ++index) {
                    target->model[index - start] = source->model[index];
                }
                target->length = source->length - start;
            }
            break;
        case 7:
            length = xorshift_below(&random, source->length + 1);
            REQUIRE_STATUS(
                ft_bilateral_deque_take(&source->deque, length, &produced),
                FT_STATUS_OK);
            for (index = 0; index != length; ++index) {
                target->model[index] = source->model[index];
            }
            target->length = length;
            break;
        case 8:
            length = xorshift_below(&random, source->length + 1);
            REQUIRE_STATUS(
                ft_bilateral_deque_drop(&source->deque, length, &produced),
                FT_STATUS_OK);
            for (index = length; index != source->length; ++index) {
                target->model[index - length] = source->model[index];
            }
            target->length = source->length - length;
            break;
        case 9:
            REQUIRE_STATUS(ft_bilateral_deque_clear(&source->deque, &produced), FT_STATUS_OK);
            target->length = 0;
            break;
        default: {
            const test_value first = make_value(value);
            const test_value last = make_value(~value);
            REQUIRE_STATUS(
                ft_bilateral_deque_push_front(&source->deque, &first, &produced),
                FT_STATUS_OK);
            REQUIRE_STATUS(ft_bilateral_deque_reverse(&produced, &produced), FT_STATUS_OK);
            REQUIRE_STATUS(
                ft_bilateral_deque_push_back(&produced, &last, &produced),
                FT_STATUS_OK);
            for (index = 0; index != source->length; ++index) {
                target->model[index] = source->model[source->length - 1 - index];
            }
            target->model[source->length] = value;
            target->model[source->length + 1] = ~value;
            target->length = source->length + 2;
            break;
        }
        }

        target->deque = produced;
        REQUIRE(check_deque(target->model, target->length, &target->deque));
        REQUIRE(check_deque(source->model, source->length, &source->deque));
        ++count;

        if (step % 29 == 0) {
            for (index = 0; index != (count < 12 ? count : 12); ++index) {
                const version* retained = &versions[xorshift_below(&random, count)];
                REQUIRE(check_deque(retained->model, retained->length, &retained->deque));
            }
        }
    }

    REQUIRE(check_deque(versions[0].model, 0, &versions[0].deque));
    for (index = 0; index != count; ++index) {
        ft_bilateral_deque_dispose(&versions[index].deque);
    }
    REQUIRE(context->live_values == 0);
}

static void test_randomized_retained_branches_match_an_immutable_sequence_model(void)
{
    static const uint64_t seeds[5] = {0u, 1u, 17u, 91u, 8675309u};
    test_context context;
    ft_incremental_ancestor_policy policy;
    size_t index = 0;

    g_versions = (version*)calloc((size_t)RANDOM_STEPS + 1, sizeof(version));
    REQUIRE(g_versions != NULL);
    for (index = 0; index != 5; ++index) {
        init_context(&context);
        if (create_policy(&policy, &context) != FT_STATUS_OK) {
            free(g_versions);
            g_versions = NULL;
            REQUIRE(false);
        }
        run_randomized_history(&policy, seeds[index], &context);
        ft_incremental_ancestor_policy_dispose(&policy);
        if (context.outstanding_allocations != 0 || context.live_values != 0) {
            free(g_versions);
            g_versions = NULL;
            REQUIRE(false);
        }
    }
    free(g_versions);
    g_versions = NULL;
}

static void test_public_operations_respect_the_level_ancestor_query_ceilings(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    ft_bilateral_deque root;
    ft_bilateral_deque deque;
    ft_bilateral_deque produced;
    ft_bilateral_deque_split_result split;
    ft_bilateral_deque_statistics audit;
    const void* borrowed = NULL;
    const test_value zero = make_value(0);
    const test_value nine = make_value(9);
    static int collected[16];
    collect_context collector;
    test_value copies[16];
    uint64_t start_queries = 0;
    uint64_t end_queries = 0;
    size_t index = 0;
    size_t count = 0;
    bool valid = false;
    int expected[8];

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init(&root, &arena), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &deque), FT_STATUS_OK);
    REQUIRE(push_front_value(&deque, 4));
    REQUIRE(push_front_value(&deque, 3));
    REQUIRE(push_front_value(&deque, 2));
    REQUIRE(push_front_value(&deque, 1));
    REQUIRE(push_back_value(&deque, 5));
    REQUIRE(push_back_value(&deque, 6));
    REQUIRE(push_back_value(&deque, 7));
    REQUIRE(push_back_value(&deque, 8));
    for (index = 0; index != 8; ++index) {
        expected[index] = (int)index + 1;
    }
    REQUIRE(check_deque(expected, 8, &deque));

    /* Endpoint reads, size, reverse, clear, both insertions, and both traversals: zero queries. */
    REQUIRE(arena_query_count(&arena, &start_queries));
    REQUIRE_STATUS(ft_bilateral_deque_first_ref(&deque, &borrowed), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_last_ref(&deque, &borrowed), FT_STATUS_OK);
    REQUIRE(ft_bilateral_deque_size(&deque) == 8);
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&deque, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_clear(&deque, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_push_front(&deque, &zero, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_push_back(&deque, &nine, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    collector.values = collected;
    collector.capacity = 16;
    collector.count = 0;
    collector.fail_at = 0;
    REQUIRE_STATUS(ft_bilateral_deque_visit(&deque, collect_visitor, &collector), FT_STATUS_OK);
    REQUIRE(collector.count == 8);
    REQUIRE_STATUS(ft_bilateral_deque_copy_to_array(&deque, copies, 16), FT_STATUS_OK);
    for (index = 0; index != 8; ++index) {
        tracked_destroy(&copies[index], &context);
    }
    REQUIRE(arena_query_count(&arena, &end_queries));
    REQUIRE(end_queries == start_queries);

    /* Indexing: at most one query. */
    for (index = 0; index != 8; ++index) {
        REQUIRE(arena_query_count(&arena, &start_queries));
        REQUIRE_STATUS(ft_bilateral_deque_at_ref(&deque, index, &borrowed), FT_STATUS_OK);
        REQUIRE(((const test_value*)borrowed)->value == (int)index + 1);
        REQUIRE(arena_query_count(&arena, &end_queries));
        REQUIRE(end_queries - start_queries <= 1);
    }

    /* Slicing and splitting: at most two queries each, regardless of how much they select. */
    for (index = 0; index <= 8; ++index) {
        for (count = 0; count + index <= 8; ++count) {
            REQUIRE(arena_query_count(&arena, &start_queries));
            REQUIRE_STATUS(
                ft_bilateral_deque_slice(&deque, index, count, &produced),
                FT_STATUS_OK);
            REQUIRE(arena_query_count(&arena, &end_queries));
            REQUIRE(end_queries - start_queries <= 2);
            REQUIRE(check_deque(expected + index, count, &produced));
            ft_bilateral_deque_dispose(&produced);
        }
    }
    for (index = 0; index <= 8; ++index) {
        REQUIRE(arena_query_count(&arena, &start_queries));
        REQUIRE_STATUS(ft_bilateral_deque_split_at(&deque, index, &split), FT_STATUS_OK);
        REQUIRE(arena_query_count(&arena, &end_queries));
        REQUIRE(end_queries - start_queries <= 2);
        REQUIRE(check_deque(expected, index, &split.left));
        REQUIRE(check_deque(expected + index, 8 - index, &split.right));
        ft_bilateral_deque_dispose(&split.left);
        ft_bilateral_deque_dispose(&split.right);
    }

    /* Removal on its own nonempty arm makes no query at all. */
    REQUIRE(arena_query_count(&arena, &start_queries));
    REQUIRE_STATUS(ft_bilateral_deque_remove_first(&deque, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_remove_last(&deque, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE(arena_query_count(&arena, &end_queries));
    REQUIRE(end_queries == start_queries);

    /* Removal that crosses the centre makes at most one. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &produced), FT_STATUS_OK);
    REQUIRE(push_back_value(&produced, 10));
    REQUIRE(push_back_value(&produced, 11));
    REQUIRE(push_back_value(&produced, 12));
    REQUIRE(push_back_value(&produced, 13));
    while (!ft_bilateral_deque_empty(&produced)) {
        REQUIRE(arena_query_count(&arena, &start_queries));
        REQUIRE_STATUS(ft_bilateral_deque_remove_first(&produced, &produced), FT_STATUS_OK);
        REQUIRE(arena_query_count(&arena, &end_queries));
        REQUIRE(end_queries - start_queries <= 1);
    }
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &produced), FT_STATUS_OK);
    REQUIRE(push_front_value(&produced, 13));
    REQUIRE(push_front_value(&produced, 12));
    REQUIRE(push_front_value(&produced, 11));
    REQUIRE(push_front_value(&produced, 10));
    while (!ft_bilateral_deque_empty(&produced)) {
        REQUIRE(arena_query_count(&arena, &start_queries));
        REQUIRE_STATUS(ft_bilateral_deque_remove_last(&produced, &produced), FT_STATUS_OK);
        REQUIRE(arena_query_count(&arena, &end_queries));
        REQUIRE(end_queries - start_queries <= 1);
    }
    ft_bilateral_deque_dispose(&produced);

    /* The audit spends two queries per nonempty interval and none on an empty handle. */
    REQUIRE(arena_query_count(&arena, &start_queries));
    REQUIRE_STATUS(ft_bilateral_deque_validate(&root, &valid, &audit), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(arena_query_count(&arena, &end_queries));
    REQUIRE(end_queries == start_queries);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &produced), FT_STATUS_OK);
    REQUIRE(push_back_value(&produced, 1));
    REQUIRE(push_back_value(&produced, 2));
    REQUIRE(arena_query_count(&arena, &start_queries));
    REQUIRE_STATUS(ft_bilateral_deque_validate(&produced, &valid, &audit), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(arena_query_count(&arena, &end_queries));
    REQUIRE(end_queries - start_queries <= 2);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE(arena_query_count(&arena, &start_queries));
    REQUIRE_STATUS(ft_bilateral_deque_validate(&deque, &valid, &audit), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(audit.count == 8);
    REQUIRE(audit.left_count == 4);
    REQUIRE(audit.right_count == 4);
    REQUIRE(arena_query_count(&arena, &end_queries));
    REQUIRE(end_queries - start_queries <= 4);

    /* Rejected boundaries publish no node and spend no query. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &before),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_at_ref(&deque, 8, &borrowed), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_at_ref(&deque, SIZE_MAX, &borrowed), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_take(&deque, 9, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_drop(&deque, 9, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_split_at(&deque, 9, &split), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&deque, 9, 0, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&deque, 7, 2, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_bilateral_deque_slice(&deque, SIZE_MAX, 1, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_bilateral_deque_slice(&deque, 1, SIZE_MAX, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after),
        FT_STATUS_OK);
    REQUIRE(after.published_node_count == before.published_node_count);
    REQUIRE(after.add_leaf_count == before.add_leaf_count);
    REQUIRE(after.ancestor_query_count == before.ancestor_query_count);
    REQUIRE(check_deque(expected, 8, &deque));

    ft_bilateral_deque_dispose(&deque);
    ft_bilateral_deque_dispose(&root);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_the_audit_rejects_a_corrupted_representation(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque deque;
    ft_bilateral_deque corrupted;
    ft_bilateral_deque empty;
    ft_bilateral_deque_statistics statistics;
    bool valid = false;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&deque, &policy), FT_STATUS_OK);
    REQUIRE(push_front_value(&deque, 2));
    REQUIRE(push_front_value(&deque, 1));
    REQUIRE(push_back_value(&deque, 3));
    REQUIRE_STATUS(ft_bilateral_deque_validate(&deque, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == 3);
    REQUIRE(statistics.left_count == 2);
    REQUIRE(statistics.right_count == 1);

    /* The total must agree with the interval counts. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&deque, &corrupted), FT_STATUS_OK);
    corrupted.count += 1;
    REQUIRE_STATUS(ft_bilateral_deque_validate(&corrupted, &valid, NULL), FT_STATUS_OK);
    REQUIRE(!valid);
    ft_bilateral_deque_dispose(&corrupted);

    /* An empty interval must have one shared endpoint. */
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&empty, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&empty, &corrupted), FT_STATUS_OK);
    corrupted.left.tail += 1;
    REQUIRE_STATUS(ft_bilateral_deque_validate(&corrupted, &valid, NULL), FT_STATUS_OK);
    REQUIRE(!valid);
    ft_bilateral_deque_dispose(&corrupted);
    ft_bilateral_deque_dispose(&empty);

    /* An interval count must equal the depth span between its endpoints. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&deque, &corrupted), FT_STATUS_OK);
    corrupted.left.count -= 1;
    corrupted.count -= 1;
    REQUIRE_STATUS(ft_bilateral_deque_validate(&corrupted, &valid, NULL), FT_STATUS_OK);
    REQUIRE(!valid);
    ft_bilateral_deque_dispose(&corrupted);

    /* The cached base must be the first node after the anchor. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&deque, &corrupted), FT_STATUS_OK);
    corrupted.left.base = corrupted.left.tail;
    REQUIRE_STATUS(ft_bilateral_deque_validate(&corrupted, &valid, NULL), FT_STATUS_OK);
    REQUIRE(!valid);
    ft_bilateral_deque_dispose(&corrupted);

    /* A node handle no arena ever published is structural invalidity, not an argument failure. */
    REQUIRE_STATUS(ft_bilateral_deque_copy(&deque, &corrupted), FT_STATUS_OK);
    corrupted.right.tail = SIZE_MAX;
    REQUIRE_STATUS(ft_bilateral_deque_validate(&corrupted, &valid, NULL), FT_STATUS_OK);
    REQUIRE(!valid);
    ft_bilateral_deque_dispose(&corrupted);

    ft_bilateral_deque_dispose(&deque);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_failure_atomicity_under_failing_callbacks_and_allocations(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    ft_bilateral_deque deque;
    ft_bilateral_deque produced;
    const test_value stored = make_value(99);
    const int expected[4] = {2, 1, 3, 4};
    static int collected[8];
    collect_context collector;
    test_value copies[8];
    test_value popped;
    bool removed = false;
    size_t live_before = 0;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init(&deque, &arena), FT_STATUS_OK);
    REQUIRE(push_front_value(&deque, 1));
    REQUIRE(push_front_value(&deque, 2));
    REQUIRE(push_back_value(&deque, 3));
    REQUIRE(push_back_value(&deque, 4));
    REQUIRE(check_deque(expected, 4, &deque));

    /* A failing value copy leaves the arena, the deque, and every live value untouched. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &before),
        FT_STATUS_OK);
    live_before = context.live_values;
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_bilateral_deque_push_front(&deque, &stored, &produced),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_bilateral_deque_push_back(&deque, &stored, &produced),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after),
        FT_STATUS_OK);
    REQUIRE(after.published_node_count == before.published_node_count);
    REQUIRE(after.add_leaf_count == before.add_leaf_count);
    REQUIRE(context.live_values == live_before);
    REQUIRE(check_deque(expected, 4, &deque));

    /* A failing allocation is reported as such and is equally atomic. */
    context.fail_allocation_at = context.allocation_calls + 1;
    REQUIRE_STATUS(ft_bilateral_deque_push_back(&deque, &stored, &produced), FT_STATUS_NO_MEMORY);
    context.fail_allocation_at = 0;
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after),
        FT_STATUS_OK);
    REQUIRE(after.published_node_count == before.published_node_count);
    REQUIRE(context.live_values == live_before);
    REQUIRE(check_deque(expected, 4, &deque));

    /* A partially filled array is left ownership-free when a later copy fails. */
    context.fail_copy_at = context.copy_calls + 3;
    REQUIRE_STATUS(
        ft_bilateral_deque_copy_to_array(&deque, copies, 8),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    REQUIRE(context.live_values == live_before);

    /* A withheld pop leaves neither an owned copy nor a remainder behind. */
    popped = make_value(-5);
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_front(&deque, &removed, &popped, &produced),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_back(&deque, &removed, &popped, &produced),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    REQUIRE(popped.value == -5);
    REQUIRE(context.live_values == live_before);
    REQUIRE(check_deque(expected, 4, &deque));

    /* A visitor that fails aborts the traversal and propagates its own status. */
    collector.values = collected;
    collector.capacity = 8;
    collector.count = 0;
    collector.fail_at = 3;
    REQUIRE_STATUS(
        ft_bilateral_deque_visit(&deque, collect_visitor, &collector),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(collector.count == 2);

    /* The right arm's temporary buffer is the only allocation a traversal makes. */
    context.fail_allocation_at = context.allocation_calls + 1;
    collector.count = 0;
    collector.fail_at = 0;
    REQUIRE_STATUS(
        ft_bilateral_deque_visit(&deque, collect_visitor, &collector),
        FT_STATUS_NO_MEMORY);
    REQUIRE(collector.count == 2);
    context.fail_allocation_at = 0;
    REQUIRE(check_deque(expected, 4, &deque));

    /* An array too small for the visible values is rejected before anything is written. */
    REQUIRE_STATUS(ft_bilateral_deque_copy_to_array(&deque, copies, 3), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(context.live_values == live_before);

    ft_bilateral_deque_dispose(&deque);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_a_shared_arena_serves_independent_families(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    ft_bilateral_deque root;
    ft_bilateral_deque first;
    ft_bilateral_deque second;
    ft_bilateral_deque produced;
    const void* borrowed = NULL;
    const int first_model[2] = {1, 2};
    const int second_model[2] = {8, 9};

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init(&root, &arena), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &first), FT_STATUS_OK);
    REQUIRE(push_back_value(&first, 1));
    REQUIRE(push_back_value(&first, 2));
    REQUIRE_STATUS(ft_bilateral_deque_copy(&root, &second), FT_STATUS_OK);
    REQUIRE(push_front_value(&second, 9));
    REQUIRE(push_front_value(&second, 8));

    REQUIRE(check_deque(first_model, 2, &first));
    REQUIRE(check_deque(second_model, 2, &second));
    REQUIRE(check_deque(NULL, 0, &root));
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &before),
        FT_STATUS_OK);
    REQUIRE(before.published_node_count == 4);
    REQUIRE(ft_bilateral_deque_root_identity(&first) ==
        ft_incremental_ancestor_arena_root_identity(&arena));
    REQUIRE(ft_bilateral_deque_root_identity(&first) == ft_bilateral_deque_root_identity(&second));

    /* Reading a version publishes nothing. */
    REQUIRE_STATUS(ft_bilateral_deque_first_ref(&first, &borrowed), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_last_ref(&second, &borrowed), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&first, 1, 1, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&second, &produced), FT_STATUS_OK);
    ft_bilateral_deque_dispose(&produced);
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after),
        FT_STATUS_OK);
    REQUIRE(after.published_node_count == before.published_node_count);
    REQUIRE(after.add_leaf_count == before.add_leaf_count);

    ft_bilateral_deque_dispose(&first);
    ft_bilateral_deque_dispose(&second);
    ft_bilateral_deque_dispose(&root);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_handle_lifecycle_shares_rather_than_copies(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_bilateral_deque deque;
    ft_bilateral_deque shared;
    ft_bilateral_deque moved;
    ft_incremental_ancestor_arena arena;
    const int model[2] = {1, 2};

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&deque, &policy), FT_STATUS_OK);
    REQUIRE(push_back_value(&deque, 1));
    REQUIRE(push_back_value(&deque, 2));

    REQUIRE_STATUS(ft_bilateral_deque_copy(&deque, &shared), FT_STATUS_OK);
    REQUIRE(same_version(&shared, &deque));
    REQUIRE(check_deque(model, 2, &shared));

    ft_bilateral_deque_move(&moved, &shared);
    REQUIRE(shared.arena.rep == NULL);
    REQUIRE(same_version(&moved, &deque));
    REQUIRE(check_deque(model, 2, &moved));

    /* Releasing one handle leaves every other version and the arena itself usable. */
    ft_bilateral_deque_dispose(&moved);
    REQUIRE(moved.arena.rep == NULL);
    REQUIRE(check_deque(model, 2, &deque));

    REQUIRE_STATUS(ft_bilateral_deque_get_arena(&deque, &arena), FT_STATUS_OK);
    REQUIRE(ft_incremental_ancestor_arena_root_identity(&arena) ==
        ft_bilateral_deque_root_identity(&deque));
    ft_incremental_ancestor_arena_dispose(&arena);
    REQUIRE(check_deque(model, 2, &deque));

    ft_bilateral_deque_dispose(&deque);
    ft_bilateral_deque_dispose(&deque);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* A backend whose bottom node reports the wrong depth, used to prove eager rejection. */
typedef struct misplaced_bottom_backend {
    size_t touched;
} misplaced_bottom_backend;

static ft_status misplaced_bottom(void* backend, ft_incremental_ancestor_node* bottom)
{
    (void)backend;
    *bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    return FT_STATUS_OK;
}

static ft_status misplaced_published_node_count(void* backend, size_t* count)
{
    (void)backend;
    *count = 0;
    return FT_STATUS_OK;
}

static ft_status misplaced_add_leaf(
    void* backend,
    ft_incremental_ancestor_node parent,
    const void* value,
    ft_incremental_ancestor_node* leaf)
{
    misplaced_bottom_backend* state = (misplaced_bottom_backend*)backend;
    (void)parent;
    (void)value;
    (void)leaf;
    ++state->touched;
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status misplaced_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth)
{
    (void)backend;
    (void)node;
    *depth = 0;
    return FT_STATUS_OK;
}

static ft_status misplaced_parent(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent)
{
    misplaced_bottom_backend* state = (misplaced_bottom_backend*)backend;
    (void)node;
    (void)parent;
    ++state->touched;
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status misplaced_ancestor_at_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor)
{
    misplaced_bottom_backend* state = (misplaced_bottom_backend*)backend;
    (void)node;
    (void)depth;
    (void)ancestor;
    ++state->touched;
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status misplaced_value_ref(
    void* backend,
    ft_incremental_ancestor_node node,
    const void** value_ref)
{
    misplaced_bottom_backend* state = (misplaced_bottom_backend*)backend;
    (void)node;
    (void)value_ref;
    ++state->touched;
    return FT_STATUS_OUT_OF_RANGE;
}

static const ft_incremental_ancestor_arena_vtable g_misplaced_vtable = {
    misplaced_bottom,
    misplaced_published_node_count,
    misplaced_add_leaf,
    misplaced_depth,
    misplaced_parent,
    misplaced_ancestor_at_depth,
    misplaced_value_ref,
    NULL
};

static void test_invalid_arguments_are_rejected(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_arena_config arena_config;
    misplaced_bottom_backend backend;
    ft_bilateral_deque deque;
    ft_bilateral_deque produced;
    ft_bilateral_deque_split_result split;
    ft_bilateral_deque_statistics statistics;
    const test_value stored = make_value(1);
    const void* borrowed = NULL;
    test_value owned;
    bool removed = false;
    bool valid = false;

    init_context(&context);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);

    /* An arena whose bottom node is not at depth -1 is rejected before anything is published. */
    backend.touched = 0;
    arena_config.vtable = &g_misplaced_vtable;
    arena_config.backend = &backend;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_create(&arena, &policy, &arena_config),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_init(&deque, &arena), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(backend.touched == 0);
    ft_incremental_ancestor_arena_dispose(&arena);

    REQUIRE_STATUS(ft_bilateral_deque_init(NULL, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_bilateral_deque_from_array(&deque, &policy, NULL, 2),
        FT_STATUS_INVALID_ARGUMENT);

    REQUIRE_STATUS(ft_bilateral_deque_init_myers(&deque, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_bilateral_deque_push_back(&deque, &stored, &deque), FT_STATUS_OK);

    REQUIRE_STATUS(ft_bilateral_deque_copy(NULL, &produced), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_copy(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_get_arena(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_first_ref(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_first_copy(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_last_ref(NULL, &borrowed), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_last_copy(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_at_ref(&deque, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_at_copy(&deque, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_bilateral_deque_push_front(&deque, NULL, &produced),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_bilateral_deque_push_back(&deque, &stored, NULL),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_remove_first(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_remove_last(NULL, &produced), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_front(&deque, NULL, &owned, &produced),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_bilateral_deque_try_pop_back(&deque, &removed, NULL, &produced),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_take(&deque, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_drop(&deque, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_slice(&deque, 0, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_split_at(&deque, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_reverse(&deque, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_clear(NULL, &produced), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_visit(&deque, NULL, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_copy_to_array(&deque, NULL, 4), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_bilateral_deque_validate(&deque, NULL, &statistics),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_validate(NULL, &valid, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_bilateral_deque_split_at(&deque, 5, &split), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(ft_bilateral_deque_root_identity(NULL) == NULL);
    REQUIRE(!ft_bilateral_deque_empty(NULL));
    REQUIRE(ft_bilateral_deque_size(NULL) == 0);

    ft_bilateral_deque_dispose(&deque);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

typedef void (*test_fn)(void);

static void run_test(const char* name, test_fn test)
{
    const int before = g_failures;
    test();
    if (before == g_failures) {
        (void)printf("[pass] %s\n", name);
        (void)fflush(stdout);
    } else {
        (void)fprintf(stderr, "[fail] %s\n", name);
        (void)fflush(stderr);
    }
}

int main(void)
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    run_test(
        "Bilateral deque empty reads and identity",
        test_an_empty_deque_has_stable_reads_failures_and_identity);
    run_test(
        "Bilateral deque endpoint branches",
        test_endpoint_operations_retain_every_prior_branch);
    run_test(
        "Bilateral deque reversal laws",
        test_reverse_swaps_orientation_without_changing_retained_versions);
    run_test(
        "Bilateral deque slice and split closure",
        test_every_slice_and_split_boundary_remains_growable);
    run_test(
        "Bilateral deque exhaustive construction words",
        test_small_endpoint_construction_words_match_the_sequence_oracle);
    run_test(
        "Bilateral deque randomized retained branches",
        test_randomized_retained_branches_match_an_immutable_sequence_model);
    run_test(
        "Bilateral deque level-ancestor query ceilings",
        test_public_operations_respect_the_level_ancestor_query_ceilings);
    run_test(
        "Bilateral deque structural audit",
        test_the_audit_rejects_a_corrupted_representation);
    run_test(
        "Bilateral deque failure atomicity",
        test_failure_atomicity_under_failing_callbacks_and_allocations);
    run_test(
        "Bilateral deque shared arena",
        test_a_shared_arena_serves_independent_families);
    run_test(
        "Bilateral deque handle lifecycle",
        test_handle_lifecycle_shares_rather_than_copies);
    run_test(
        "Bilateral deque invalid arguments",
        test_invalid_arguments_are_rejected);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C bilateral ancestral deque tests passed\n");
    return EXIT_SUCCESS;
}
