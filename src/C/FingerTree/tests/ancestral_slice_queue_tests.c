/*
 * Tests for the ancestral slice queue.
 *
 * The obligations mirror the C# suite: model equivalence over randomized branching histories,
 * retained-version independence after branching, every slice/take/drop/split boundary including the
 * anchored-empty cases, the square seams induced by the shipped arena's odd blocks, failure
 * atomicity under failing callbacks and allocations, and the operation-count guardrail the C# tests
 * assert -- namely that the boundary-specialized calls able to reuse an existing tail make no
 * ancestor query at all. The guardrail is expressed through the Myers arena's statistics, which is
 * the C analogue of the reference suite's AssertArenaDelta helper.
 */

#include <durable7/finger_tree/ancestral_slice_queue.h>
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

static ft_status create_policy(ft_incremental_ancestor_policy* policy, test_context* context)
{
    ft_incremental_ancestor_policy_config config;
    (void)memset(context, 0, sizeof(*context));
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

/* Destroys a value obtained from an owning read, using the very policy the queue's arena carries. */
static void destroy_value(const ft_ancestral_slice_queue* queue, void* value)
{
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_policy_config config;

    if (ft_ancestral_slice_queue_get_arena(queue, &arena) != FT_STATUS_OK) {
        return;
    }
    if (ft_incremental_ancestor_arena_get_policy(&arena, &policy) == FT_STATUS_OK) {
        if (ft_incremental_ancestor_policy_get_config(&policy, &config) == FT_STATUS_OK &&
            config.destroy != NULL) {
            config.destroy(value, config.callback_context);
        }
        ft_incremental_ancestor_policy_dispose(&policy);
    }
    ft_incremental_ancestor_arena_dispose(&arena);
}

typedef struct collect_state {
    int* values;
    size_t capacity;
    size_t count;
    size_t stop_after;
} collect_state;

static ft_status collect_visit(const void* value, void* context)
{
    collect_state* state = (collect_state*)context;
    if (state->stop_after != 0 && state->count == state->stop_after) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (state->count == state->capacity) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    state->values[state->count] = ((const test_value*)value)->value;
    ++state->count;
    return FT_STATUS_OK;
}

/* Checks every read the reference AssertState helper checks against one expected model. The owning
 * index read is sampled rather than run at every position: it repeats the node lookup the borrowing
 * read already performs at every position, and this suite exercises very long queues. */
static void assert_state(const ft_ancestral_slice_queue* queue, const int* expected, size_t count)
{
    collect_state state;
    const void* value_ref = NULL;
    test_value copied;
    size_t index = 0;

    REQUIRE(ft_ancestral_slice_queue_size(queue) == count);
    REQUIRE(ft_ancestral_slice_queue_empty(queue) == (count == 0));

    state.values = NULL;
    state.capacity = count;
    state.count = 0;
    state.stop_after = 0;
    if (count != 0) {
        state.values = (int*)malloc(count * sizeof(int));
        REQUIRE(state.values != NULL);
    }
    REQUIRE_STATUS(ft_ancestral_slice_queue_visit(queue, collect_visit, &state), FT_STATUS_OK);
    REQUIRE(state.count == count);
    for (index = 0; index != count; ++index) {
        const bool matched = state.values[index] == expected[index];
        if (!matched) {
            free(state.values);
            REQUIRE(matched);
        }
    }
    free(state.values);

    for (index = 0; index != count; ++index) {
        REQUIRE_STATUS(ft_ancestral_slice_queue_at_ref(queue, index, &value_ref), FT_STATUS_OK);
        REQUIRE(((const test_value*)value_ref)->value == expected[index]);
        if (index < (size_t)4 || index + (size_t)4 >= count || index % (size_t)64 == 0) {
            REQUIRE_STATUS(ft_ancestral_slice_queue_at_copy(queue, index, &copied), FT_STATUS_OK);
            REQUIRE(copied.value == expected[index]);
            destroy_value(queue, &copied);
        }
    }
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_at_ref(queue, count, &value_ref),
        FT_STATUS_OUT_OF_RANGE);

    if (count == 0) {
        ft_ancestral_slice_queue rest;
        bool removed = true;

        REQUIRE_STATUS(ft_ancestral_slice_queue_first_ref(queue, &value_ref), FT_STATUS_EMPTY);
        REQUIRE_STATUS(ft_ancestral_slice_queue_last_ref(queue, &value_ref), FT_STATUS_EMPTY);
        REQUIRE_STATUS(ft_ancestral_slice_queue_first_copy(queue, &copied), FT_STATUS_EMPTY);
        REQUIRE_STATUS(ft_ancestral_slice_queue_last_copy(queue, &copied), FT_STATUS_EMPTY);
        REQUIRE_STATUS(ft_ancestral_slice_queue_remove_first(queue, &rest), FT_STATUS_EMPTY);
        REQUIRE_STATUS(ft_ancestral_slice_queue_remove_last(queue, &rest), FT_STATUS_EMPTY);

        REQUIRE_STATUS(
            ft_ancestral_slice_queue_try_remove_first(queue, &removed, &copied, &rest),
            FT_STATUS_OK);
        REQUIRE(!removed);
        REQUIRE(ft_ancestral_slice_queue_size(&rest) == 0);
        ft_ancestral_slice_queue_dispose(&rest);

        removed = true;
        REQUIRE_STATUS(
            ft_ancestral_slice_queue_try_remove_last(queue, &removed, &copied, &rest),
            FT_STATUS_OK);
        REQUIRE(!removed);
        REQUIRE(ft_ancestral_slice_queue_size(&rest) == 0);
        ft_ancestral_slice_queue_dispose(&rest);
    } else {
        REQUIRE_STATUS(ft_ancestral_slice_queue_first_ref(queue, &value_ref), FT_STATUS_OK);
        REQUIRE(((const test_value*)value_ref)->value == expected[0]);
        REQUIRE_STATUS(ft_ancestral_slice_queue_last_ref(queue, &value_ref), FT_STATUS_OK);
        REQUIRE(((const test_value*)value_ref)->value == expected[count - 1]);

        REQUIRE_STATUS(ft_ancestral_slice_queue_first_copy(queue, &copied), FT_STATUS_OK);
        REQUIRE(copied.value == expected[0]);
        destroy_value(queue, &copied);
        REQUIRE_STATUS(ft_ancestral_slice_queue_last_copy(queue, &copied), FT_STATUS_OK);
        REQUIRE(copied.value == expected[count - 1]);
        destroy_value(queue, &copied);
    }
}

static int* build_model(int start, size_t count)
{
    int* model = (int*)malloc((count == 0 ? (size_t)1 : count) * sizeof(int));
    size_t index = 0;
    if (model == NULL) {
        return NULL;
    }
    for (index = 0; index != count; ++index) {
        model[index] = start + (int)index;
    }
    return model;
}

static ft_status build_queue(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_policy* policy,
    int start,
    size_t count)
{
    test_value* values = NULL;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;

    if (count == 0) {
        return ft_ancestral_slice_queue_init_myers(queue, policy);
    }
    values = (test_value*)malloc(count * sizeof(test_value));
    if (values == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    for (index = 0; index != count; ++index) {
        values[index].value = start + (int)index;
    }
    status = ft_ancestral_slice_queue_from_array(queue, policy, values, count);
    free(values);
    return status;
}

static test_value make_value(int value)
{
    test_value item;
    item.value = value;
    return item;
}

/* Appends one marker to a handle and checks the result equals the model followed by that marker,
 * which is how every "this anchor is still appendable" obligation is expressed. */
static void assert_append(
    const ft_ancestral_slice_queue* queue,
    const int* expected,
    size_t count,
    int marker)
{
    ft_ancestral_slice_queue extended;
    test_value appended = make_value(marker);
    int* model = (int*)malloc((count + 1) * sizeof(int));

    REQUIRE(model != NULL);
    if (count != 0) {
        (void)memcpy(model, expected, count * sizeof(int));
    }
    model[count] = marker;
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(queue, &appended, &extended), FT_STATUS_OK);
    assert_state(&extended, model, count + 1);
    ft_ancestral_slice_queue_dispose(&extended);
    free(model);
}

/* ------------------------------------------------------------------------------------------- */

static void test_empty_singleton_and_endpoint_contracts(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue empty;
    ft_ancestral_slice_queue singleton;
    ft_ancestral_slice_queue pair;
    ft_ancestral_slice_queue rest;
    test_value first_value = make_value(11);
    test_value second_value = make_value(22);
    test_value removed_value = make_value(0);
    bool removed = false;
    int model_one[1];
    int model_two[2];

    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init_myers(&empty, &policy), FT_STATUS_OK);
    assert_state(&empty, NULL, 0);

    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&empty, &first_value, &singleton),
        FT_STATUS_OK);
    model_one[0] = 11;
    assert_state(&singleton, model_one, 1);
    assert_state(&empty, NULL, 0);

    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&singleton, &second_value, &pair),
        FT_STATUS_OK);
    model_two[0] = 11;
    model_two[1] = 22;
    assert_state(&pair, model_two, 2);
    assert_state(&singleton, model_one, 1);

    REQUIRE_STATUS(
        ft_ancestral_slice_queue_try_remove_first(&pair, &removed, &removed_value, &rest),
        FT_STATUS_OK);
    REQUIRE(removed);
    REQUIRE(removed_value.value == 11);
    destroy_value(&pair, &removed_value);
    model_one[0] = 22;
    assert_state(&rest, model_one, 1);
    ft_ancestral_slice_queue_dispose(&rest);

    REQUIRE_STATUS(
        ft_ancestral_slice_queue_try_remove_last(&pair, &removed, &removed_value, &rest),
        FT_STATUS_OK);
    REQUIRE(removed);
    REQUIRE(removed_value.value == 22);
    destroy_value(&pair, &removed_value);
    model_one[0] = 11;
    assert_state(&rest, model_one, 1);
    ft_ancestral_slice_queue_dispose(&rest);
    assert_state(&pair, model_two, 2);

    /* A singleton emptied from either end appends to exactly one visible value, and the two empty
     * results keep different anchors. */
    REQUIRE_STATUS(ft_ancestral_slice_queue_remove_first(&singleton, &rest), FT_STATUS_OK);
    assert_state(&rest, NULL, 0);
    assert_append(&rest, NULL, 0, 77);
    ft_ancestral_slice_queue_dispose(&rest);

    REQUIRE_STATUS(ft_ancestral_slice_queue_remove_last(&singleton, &rest), FT_STATUS_OK);
    assert_state(&rest, NULL, 0);
    assert_append(&rest, NULL, 0, 88);
    ft_ancestral_slice_queue_dispose(&rest);
    model_one[0] = 11;
    assert_state(&singleton, model_one, 1);

    /* Every boundary read of the empty queue's own empty derivations stays anchored. */
    REQUIRE_STATUS(ft_ancestral_slice_queue_take(&empty, 0, &rest), FT_STATUS_OK);
    assert_state(&rest, NULL, 0);
    assert_append(&rest, NULL, 0, 1);
    ft_ancestral_slice_queue_dispose(&rest);
    REQUIRE_STATUS(ft_ancestral_slice_queue_drop(&empty, 0, &rest), FT_STATUS_OK);
    assert_state(&rest, NULL, 0);
    ft_ancestral_slice_queue_dispose(&rest);
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&empty, 0, 0, &rest), FT_STATUS_OK);
    assert_state(&rest, NULL, 0);
    ft_ancestral_slice_queue_dispose(&rest);

    ft_ancestral_slice_queue_dispose(&pair);
    ft_ancestral_slice_queue_dispose(&singleton);
    ft_ancestral_slice_queue_dispose(&empty);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_every_slice_boundary_matches_the_model(void)
{
    enum { source_length = 65 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue slice;
    int* model = build_model(0, length);
    size_t start = 0;
    size_t count = 0;

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);

    for (start = 0; start <= length; ++start) {
        for (count = 0; count <= length - start; ++count) {
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_slice(&source, start, count, &slice),
                FT_STATUS_OK);
            assert_state(&slice, model + start, count);
            if (count == 0) {
                /* The anchored-empty rule: appending to any empty slice yields exactly the new
                 * values and never resurrects the discarded prefix. */
                ft_ancestral_slice_queue continued;
                test_value marker = make_value(-(int)start - 1);
                int expected[2];
                REQUIRE_STATUS(
                    ft_ancestral_slice_queue_add_last(&slice, &marker, &continued),
                    FT_STATUS_OK);
                marker = make_value(10000 + (int)start);
                REQUIRE_STATUS(
                    ft_ancestral_slice_queue_add_last(&continued, &marker, &continued),
                    FT_STATUS_OK);
                expected[0] = -(int)start - 1;
                expected[1] = 10000 + (int)start;
                assert_state(&continued, expected, 2);
                assert_state(&slice, NULL, 0);
                ft_ancestral_slice_queue_dispose(&continued);
            }
            ft_ancestral_slice_queue_dispose(&slice);
        }
    }

    /* The boundary-specialized calls denote the same interval on the same retained tail. */
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&source, 0, length, &slice), FT_STATUS_OK);
    REQUIRE(slice.tail == source.tail && slice.low_depth == source.low_depth);
    ft_ancestral_slice_queue_dispose(&slice);
    REQUIRE_STATUS(ft_ancestral_slice_queue_take(&source, length, &slice), FT_STATUS_OK);
    REQUIRE(slice.tail == source.tail && slice.low_depth == source.low_depth);
    ft_ancestral_slice_queue_dispose(&slice);
    REQUIRE_STATUS(ft_ancestral_slice_queue_drop(&source, 0, &slice), FT_STATUS_OK);
    REQUIRE(slice.tail == source.tail && slice.low_depth == source.low_depth);
    ft_ancestral_slice_queue_dispose(&slice);
    assert_state(&source, model, length);

    ft_ancestral_slice_queue_dispose(&source);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_take_drop_and_split_partition_at_every_boundary(void)
{
    enum { source_length = 257 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue prefix;
    ft_ancestral_slice_queue suffix;
    ft_ancestral_slice_queue_split_result split;
    int* model = build_model(0, length);
    size_t position = 0;

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);

    for (position = 0; position <= length; ++position) {
        REQUIRE_STATUS(ft_ancestral_slice_queue_take(&source, position, &prefix), FT_STATUS_OK);
        REQUIRE_STATUS(ft_ancestral_slice_queue_drop(&source, position, &suffix), FT_STATUS_OK);
        assert_state(&prefix, model, position);
        assert_state(&suffix, model + position, length - position);

        REQUIRE_STATUS(ft_ancestral_slice_queue_split_at(&source, position, &split), FT_STATUS_OK);
        assert_state(&split.left, model, position);
        assert_state(&split.right, model + position, length - position);
        REQUIRE(split.left.tail == prefix.tail && split.left.low_depth == prefix.low_depth);
        REQUIRE(split.right.tail == suffix.tail && split.right.low_depth == suffix.low_depth);
        ft_ancestral_slice_queue_dispose(&split.left);
        ft_ancestral_slice_queue_dispose(&split.right);

        /* Both results remain real ancestry slices and start independent branches. */
        assert_append(&prefix, model, position, -(int)position - 1);
        assert_append(&suffix, model + position, length - position, -(int)position - 1);

        ft_ancestral_slice_queue_dispose(&prefix);
        ft_ancestral_slice_queue_dispose(&suffix);
    }

    assert_state(&source, model, length);
    ft_ancestral_slice_queue_dispose(&source);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_square_and_odd_block_boundaries_support_index_slice_and_branching(void)
{
    enum { maximum_root = 128, source_length = maximum_root * maximum_root + 1 };
    const size_t length = (size_t)source_length;
    const size_t root_limit = (size_t)maximum_root;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue selected;
    const void* value_ref = NULL;
    int* model = build_model(0, length);
    size_t root = 0;

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);

    for (root = 0; root <= root_limit; ++root) {
        const size_t square = root * root;
        const size_t start = square == 0 ? (size_t)0 : square - 1;
        size_t count = length - start < (size_t)3 ? length - start : (size_t)3;

        REQUIRE_STATUS(ft_ancestral_slice_queue_at_ref(&source, square, &value_ref), FT_STATUS_OK);
        REQUIRE(((const test_value*)value_ref)->value == (int)square);

        REQUIRE_STATUS(
            ft_ancestral_slice_queue_slice(&source, start, count, &selected),
            FT_STATUS_OK);
        assert_state(&selected, model + start, count);
        ft_ancestral_slice_queue_dispose(&selected);

        REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&source, square, 0, &selected), FT_STATUS_OK);
        assert_state(&selected, NULL, 0);
        assert_append(&selected, NULL, 0, -(int)square - 1);
        ft_ancestral_slice_queue_dispose(&selected);

        if (square > 0) {
            /* The queue length equals the tail handle here, so the versions of length square - 1
             * and square end immediately before and at the odd-block seam. */
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_take(&source, square - 1, &selected),
                FT_STATUS_OK);
            assert_append(&selected, model, square - 1, -(int)square - 2);
            ft_ancestral_slice_queue_dispose(&selected);

            REQUIRE_STATUS(ft_ancestral_slice_queue_take(&source, square, &selected), FT_STATUS_OK);
            assert_append(&selected, model, square, -(int)square - 3);
            ft_ancestral_slice_queue_dispose(&selected);
        }

        if (root == root_limit) {
            continue;
        }
        count = (root + 1) * (root + 1) - square;
        REQUIRE(count == 2 * root + 1);
        REQUIRE_STATUS(
            ft_ancestral_slice_queue_slice(&source, square, count, &selected),
            FT_STATUS_OK);
        assert_state(&selected, model + square, count);
        ft_ancestral_slice_queue_dispose(&selected);
    }

    assert_state(&source, model, length);
    ft_ancestral_slice_queue_dispose(&source);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

typedef struct branching_collect_state {
    collect_state collect;
    const ft_ancestral_slice_queue* source;
    ft_ancestral_slice_queue branch;
    bool branched;
} branching_collect_state;

static ft_status branching_visit(const void* value, void* context)
{
    branching_collect_state* state = (branching_collect_state*)context;
    const test_value injected = {-999};
    ft_status status = collect_visit(value, &state->collect);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (!state->branched) {
        state->branched = true;
        status = ft_ancestral_slice_queue_add_last(state->source, &injected, &state->branch);
    }
    return status;
}

static void test_visitation_is_repeatable_and_does_not_observe_later_branches(void)
{
    enum { source_length = 100, window = 40 };
    const size_t length = (size_t)source_length;
    const size_t visible = (size_t)window;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue slice;
    branching_collect_state state;
    int collected[source_length];
    int expected_branch[window + 1];
    int* model = build_model(0, length);
    size_t index = 0;

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&source, 20, visible, &slice), FT_STATUS_OK);

    /* The path is captured before the first callback, so a branch published from inside the
     * visitor cannot enter the traversal. */
    state.collect.values = collected;
    state.collect.capacity = length;
    state.collect.count = 0;
    state.collect.stop_after = 0;
    state.source = &slice;
    state.branched = false;
    REQUIRE_STATUS(ft_ancestral_slice_queue_visit(&slice, branching_visit, &state), FT_STATUS_OK);
    REQUIRE(state.branched);
    REQUIRE(state.collect.count == visible);
    for (index = 0; index != visible; ++index) {
        REQUIRE(collected[index] == model[20 + index]);
        expected_branch[index] = model[20 + index];
    }
    expected_branch[visible] = -999;
    assert_state(&state.branch, expected_branch, visible + 1);
    ft_ancestral_slice_queue_dispose(&state.branch);

    /* Repeated traversals of the retained handle still yield the original path. */
    assert_state(&slice, model + 20, visible);
    assert_state(&slice, model + 20, visible);
    assert_state(&source, model, length);

    /* A visitor that stops aborts the traversal and propagates its own status. */
    state.collect.count = 0;
    state.collect.stop_after = 7;
    state.branched = true;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_visit(&slice, branching_visit, &state),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(state.collect.count == 7);

    ft_ancestral_slice_queue_dispose(&slice);
    ft_ancestral_slice_queue_dispose(&source);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_endpoint_removal_histories_retain_every_version(void)
{
    enum { source_length = 512 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue* front = NULL;
    ft_ancestral_slice_queue* back = NULL;
    int* model = build_model(0, length);
    size_t index = 0;

    REQUIRE(model != NULL);
    front = (ft_ancestral_slice_queue*)malloc((length + 1) * sizeof(*front));
    back = (ft_ancestral_slice_queue*)malloc((length + 1) * sizeof(*back));
    REQUIRE(front != NULL && back != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_copy(&source, &front[0]), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_copy(&source, &back[0]), FT_STATUS_OK);

    for (index = 0; index != length; ++index) {
        REQUIRE_STATUS(
            ft_ancestral_slice_queue_remove_first(&front[index], &front[index + 1]),
            FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_ancestral_slice_queue_remove_last(&back[index], &back[index + 1]),
            FT_STATUS_OK);
    }

    for (index = 0; index <= length; ++index) {
        assert_state(&front[index], model + index, length - index);
        assert_state(&back[index], model, length - index);
    }

    /* Both fully drained versions keep different anchors but behave as empty queues. */
    REQUIRE(front[length].tail != back[length].tail);
    assert_append(&front[length], NULL, 0, -1);
    assert_append(&back[length], NULL, 0, -2);
    assert_state(&source, model, length);

    for (index = 0; index <= length; ++index) {
        ft_ancestral_slice_queue_dispose(&front[index]);
        ft_ancestral_slice_queue_dispose(&back[index]);
    }
    free(front);
    free(back);
    ft_ancestral_slice_queue_dispose(&source);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_retained_branches_stay_independent(void)
{
    enum { source_length = 200, branch_count = 7 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue middle;
    ft_ancestral_slice_queue working;
    ft_ancestral_slice_queue branches[branch_count];
    int* model = build_model(0, length);
    int expected[102];
    test_value marker = make_value(0);
    size_t index = 0;

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&source, 50, 100, &middle), FT_STATUS_OK);

    marker = make_value(-1);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&middle, &marker, &branches[0]), FT_STATUS_OK);

    marker = make_value(-2);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&middle, &marker, &branches[1]), FT_STATUS_OK);
    marker = make_value(-3);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&branches[1], &marker, &branches[1]),
        FT_STATUS_OK);

    marker = make_value(-4);
    REQUIRE_STATUS(ft_ancestral_slice_queue_remove_first(&middle, &working), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&working, &marker, &branches[2]), FT_STATUS_OK);
    ft_ancestral_slice_queue_dispose(&working);

    marker = make_value(-5);
    REQUIRE_STATUS(ft_ancestral_slice_queue_remove_last(&middle, &working), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&working, &marker, &branches[3]), FT_STATUS_OK);
    ft_ancestral_slice_queue_dispose(&working);

    marker = make_value(-6);
    REQUIRE_STATUS(ft_ancestral_slice_queue_take(&middle, 40, &working), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&working, &marker, &branches[4]), FT_STATUS_OK);
    ft_ancestral_slice_queue_dispose(&working);

    marker = make_value(-7);
    REQUIRE_STATUS(ft_ancestral_slice_queue_drop(&middle, 60, &working), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&working, &marker, &branches[5]), FT_STATUS_OK);
    ft_ancestral_slice_queue_dispose(&working);

    marker = make_value(-8);
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&middle, 30, 0, &working), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&working, &marker, &branches[6]), FT_STATUS_OK);
    ft_ancestral_slice_queue_dispose(&working);

    for (index = 0; index != 100; ++index) {
        expected[index] = model[50 + index];
    }
    expected[100] = -1;
    assert_state(&branches[0], expected, 101);
    expected[100] = -2;
    expected[101] = -3;
    assert_state(&branches[1], expected, 102);

    for (index = 0; index != 99; ++index) {
        expected[index] = model[51 + index];
    }
    expected[99] = -4;
    assert_state(&branches[2], expected, 100);

    for (index = 0; index != 99; ++index) {
        expected[index] = model[50 + index];
    }
    expected[99] = -5;
    assert_state(&branches[3], expected, 100);

    for (index = 0; index != 40; ++index) {
        expected[index] = model[50 + index];
    }
    expected[40] = -6;
    assert_state(&branches[4], expected, 41);

    for (index = 0; index != 40; ++index) {
        expected[index] = model[110 + index];
    }
    expected[40] = -7;
    assert_state(&branches[5], expected, 41);

    expected[0] = -8;
    assert_state(&branches[6], expected, 1);

    assert_state(&middle, model + 50, 100);
    assert_state(&source, model, length);

    for (index = 0; index != (size_t)branch_count; ++index) {
        ft_ancestral_slice_queue_dispose(&branches[index]);
    }
    ft_ancestral_slice_queue_dispose(&middle);
    ft_ancestral_slice_queue_dispose(&source);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* A deterministic xorshift generator; the tests must not depend on the platform's rand. */
typedef struct xorshift {
    uint64_t state;
} xorshift;

static size_t xorshift_below(xorshift* random, size_t bound)
{
    random->state ^= random->state << 13;
    random->state ^= random->state >> 7;
    random->state ^= random->state << 17;
    return (size_t)(random->state % (uint64_t)bound);
}

typedef struct history_entry {
    ft_ancestral_slice_queue queue;
    int* model;
    size_t length;
} history_entry;

static void test_randomized_branching_history_matches_array_models(void)
{
    enum { steps = 2000 };
    test_context context;
    ft_incremental_ancestor_policy policy;
    history_entry* histories = NULL;
    size_t history_count = 0;
    xorshift random;
    size_t step = 0;
    size_t index = 0;

    random.state = 0x51CEu | 1u;
    histories = (history_entry*)malloc(((size_t)steps + 1) * sizeof(*histories));
    REQUIRE(histories != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init_myers(&histories[0].queue, &policy), FT_STATUS_OK);
    histories[0].model = build_model(0, 0);
    histories[0].length = 0;
    REQUIRE(histories[0].model != NULL);
    history_count = 1;

    for (step = 0; step != (size_t)steps; ++step) {
        const size_t selected = xorshift_below(&random, history_count);
        const ft_ancestral_slice_queue* source = &histories[selected].queue;
        const int* model = histories[selected].model;
        const size_t length = histories[selected].length;
        ft_ancestral_slice_queue result;
        int* expected = NULL;
        size_t expected_length = 0;
        size_t choice = xorshift_below(&random, 9);
        test_value appended = make_value((int)step);
        test_value taken = make_value(0);
        bool removed = false;

        if (length == 0 && (choice == 1 || choice == 2 || choice == 7 || choice == 8)) {
            choice = 0;
        }
        switch (choice) {
        case 1:
            REQUIRE_STATUS(ft_ancestral_slice_queue_remove_first(source, &result), FT_STATUS_OK);
            expected_length = length - 1;
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model + 1, expected_length * sizeof(int));
            break;
        case 2:
            REQUIRE_STATUS(ft_ancestral_slice_queue_remove_last(source, &result), FT_STATUS_OK);
            expected_length = length - 1;
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model, expected_length * sizeof(int));
            break;
        case 3:
            expected_length = xorshift_below(&random, length + 1);
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_take(source, expected_length, &result),
                FT_STATUS_OK);
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model, expected_length * sizeof(int));
            break;
        case 4: {
            const size_t dropped = xorshift_below(&random, length + 1);
            REQUIRE_STATUS(ft_ancestral_slice_queue_drop(source, dropped, &result), FT_STATUS_OK);
            expected_length = length - dropped;
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model + dropped, expected_length * sizeof(int));
            break;
        }
        case 5: {
            const size_t start = xorshift_below(&random, length + 1);
            expected_length = xorshift_below(&random, length - start + 1);
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_slice(source, start, expected_length, &result),
                FT_STATUS_OK);
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model + start, expected_length * sizeof(int));
            break;
        }
        case 6: {
            const size_t position = xorshift_below(&random, length + 1);
            ft_ancestral_slice_queue_split_result split;
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_split_at(source, position, &split),
                FT_STATUS_OK);
            if (xorshift_below(&random, 2) == 0) {
                result = split.left;
                ft_ancestral_slice_queue_dispose(&split.right);
                expected_length = position;
                expected = build_model(0, expected_length);
                REQUIRE(expected != NULL);
                (void)memcpy(expected, model, expected_length * sizeof(int));
            } else {
                result = split.right;
                ft_ancestral_slice_queue_dispose(&split.left);
                expected_length = length - position;
                expected = build_model(0, expected_length);
                REQUIRE(expected != NULL);
                (void)memcpy(expected, model + position, expected_length * sizeof(int));
            }
            break;
        }
        case 7:
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_try_remove_first(source, &removed, &taken, &result),
                FT_STATUS_OK);
            REQUIRE(removed);
            REQUIRE(taken.value == model[0]);
            destroy_value(source, &taken);
            expected_length = length - 1;
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model + 1, expected_length * sizeof(int));
            break;
        case 8:
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_try_remove_last(source, &removed, &taken, &result),
                FT_STATUS_OK);
            REQUIRE(removed);
            REQUIRE(taken.value == model[length - 1]);
            destroy_value(source, &taken);
            expected_length = length - 1;
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model, expected_length * sizeof(int));
            break;
        default:
            REQUIRE_STATUS(
                ft_ancestral_slice_queue_add_last(source, &appended, &result),
                FT_STATUS_OK);
            expected_length = length + 1;
            expected = build_model(0, expected_length);
            REQUIRE(expected != NULL);
            (void)memcpy(expected, model, length * sizeof(int));
            expected[length] = (int)step;
            break;
        }

        assert_state(source, model, length);
        assert_state(&result, expected, expected_length);
        histories[history_count].queue = result;
        histories[history_count].model = expected;
        histories[history_count].length = expected_length;
        ++history_count;

        if (step % 211 == 0) {
            const size_t samples = history_count < (size_t)16 ? history_count : (size_t)16;
            for (index = 0; index != samples; ++index) {
                const size_t sample = xorshift_below(&random, history_count);
                assert_state(
                    &histories[sample].queue,
                    histories[sample].model,
                    histories[sample].length);
            }
        }
    }

    for (index = 0; index < history_count; index += 97) {
        assert_state(&histories[index].queue, histories[index].model, histories[index].length);
    }
    for (index = 0; index != history_count; ++index) {
        ft_ancestral_slice_queue_dispose(&histories[index].queue);
        free(histories[index].model);
    }
    free(histories);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

#define BEGIN_DELTA() \
    REQUIRE_STATUS( \
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &before), \
        FT_STATUS_OK)

#define END_DELTA(adds, queries) \
    do { \
        REQUIRE_STATUS( \
            ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after), \
            FT_STATUS_OK); \
        REQUIRE(after.add_leaf_count == before.add_leaf_count + (uint64_t)(adds)); \
        REQUIRE(after.published_node_count == before.published_node_count + (size_t)(adds)); \
        REQUIRE(after.ancestor_query_count == before.ancestor_query_count + (uint64_t)(queries)); \
    } while (0)

static void test_operations_use_the_parameterized_backend_complexity_boundary(void)
{
    enum { source_length = 16 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    ft_ancestral_slice_queue queue;
    ft_ancestral_slice_queue result;
    ft_ancestral_slice_queue_split_result split;
    collect_state collected;
    const void* value_ref = NULL;
    int buffer[source_length];
    test_value value = make_value(0);
    test_value taken = make_value(0);
    bool removed = false;
    int step = 0;

    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init(&queue, &arena), FT_STATUS_OK);
    for (step = 0; step != source_length; ++step) {
        value = make_value(step);
        REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&queue, &value, &queue), FT_STATUS_OK);
    }

    /* The count, the emptiness flag, the back, and the last index are all O(1) reads that never
     * consult the level-ancestor structure. */
    BEGIN_DELTA();
    REQUIRE(ft_ancestral_slice_queue_size(&queue) == length);
    REQUIRE(!ft_ancestral_slice_queue_empty(&queue));
    REQUIRE_STATUS(ft_ancestral_slice_queue_last_ref(&queue, &value_ref), FT_STATUS_OK);
    REQUIRE(((const test_value*)value_ref)->value == 15);
    REQUIRE_STATUS(ft_ancestral_slice_queue_at_ref(&queue, 15, &value_ref), FT_STATUS_OK);
    REQUIRE(((const test_value*)value_ref)->value == 15);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_first_ref(&queue, &value_ref), FT_STATUS_OK);
    REQUIRE(((const test_value*)value_ref)->value == 0);
    END_DELTA(0, 1);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_at_ref(&queue, 7, &value_ref), FT_STATUS_OK);
    REQUIRE(((const test_value*)value_ref)->value == 7);
    END_DELTA(0, 1);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_remove_first(&queue, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == length - 1);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_remove_last(&queue, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == length - 1);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_drop(&queue, 7, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == 9);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_take(&queue, 7, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == 7);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 1);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&queue, 3, 5, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == 5);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 1);

    /* A suffix range keeps the old tail, so it makes no ancestor query. */
    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&queue, 3, 13, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == 13);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_split_at(&queue, 7, &split), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&split.left) == 7);
    REQUIRE(ft_ancestral_slice_queue_size(&split.right) == 9);
    ft_ancestral_slice_queue_dispose(&split.left);
    ft_ancestral_slice_queue_dispose(&split.right);
    END_DELTA(0, 1);

    /* A split at zero is not query-free: the anchored-empty rule makes the empty prefix retain the
     * node at low_depth - 1, which only an ancestor query can name from the tail. */
    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_split_at(&queue, 0, &split), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&split.left) == 0);
    REQUIRE(ft_ancestral_slice_queue_size(&split.right) == length);
    ft_ancestral_slice_queue_dispose(&split.left);
    ft_ancestral_slice_queue_dispose(&split.right);
    END_DELTA(0, 1);

    /* A split at the count reuses the source tail for the prefix and only moves the low boundary
     * for the empty suffix, so it makes no query at all. */
    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_split_at(&queue, length, &split), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&split.left) == length);
    REQUIRE(ft_ancestral_slice_queue_size(&split.right) == 0);
    REQUIRE(split.left.tail == queue.tail);
    REQUIRE(split.right.tail == queue.tail);
    ft_ancestral_slice_queue_dispose(&split.left);
    ft_ancestral_slice_queue_dispose(&split.right);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_try_remove_first(&queue, &removed, &taken, &result),
        FT_STATUS_OK);
    REQUIRE(removed && taken.value == 0);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == length - 1);
    destroy_value(&queue, &taken);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 1);

    BEGIN_DELTA();
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_try_remove_last(&queue, &removed, &taken, &result),
        FT_STATUS_OK);
    REQUIRE(removed && taken.value == 15);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == length - 1);
    destroy_value(&queue, &taken);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(0, 0);

    /* Traversal follows parent links, so it publishes nothing and queries nothing. */
    collected.values = buffer;
    collected.capacity = length;
    collected.count = 0;
    collected.stop_after = 0;
    BEGIN_DELTA();
    REQUIRE_STATUS(ft_ancestral_slice_queue_visit(&queue, collect_visit, &collected), FT_STATUS_OK);
    REQUIRE(collected.count == length);
    END_DELTA(0, 0);

    BEGIN_DELTA();
    value = make_value(source_length);
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&queue, &value, &result), FT_STATUS_OK);
    REQUIRE(ft_ancestral_slice_queue_size(&result) == length + 1);
    ft_ancestral_slice_queue_dispose(&result);
    END_DELTA(1, 0);

    ft_ancestral_slice_queue_dispose(&queue);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static int g_flat_backend = 0;

static ft_status flat_bottom(void* backend, ft_incremental_ancestor_node* bottom)
{
    (void)backend;
    *bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    return FT_STATUS_OK;
}

static ft_status flat_published_node_count(void* backend, size_t* count)
{
    (void)backend;
    *count = 0;
    return FT_STATUS_OK;
}

static ft_status flat_add_leaf(
    void* backend,
    ft_incremental_ancestor_node parent,
    const void* value,
    ft_incremental_ancestor_node* leaf)
{
    (void)backend;
    (void)parent;
    (void)value;
    (void)leaf;
    return FT_STATUS_OUT_OF_RANGE;
}

/* The whole point of this backend: a bottom node that is not at depth -1, which cannot satisfy the
 * anchored-empty rule and must be refused before anything else is called. */
static ft_status flat_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth)
{
    (void)backend;
    (void)node;
    *depth = 0;
    return FT_STATUS_OK;
}

static ft_status flat_parent(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent)
{
    (void)backend;
    (void)node;
    (void)parent;
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status flat_ancestor_at_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor)
{
    (void)backend;
    (void)node;
    (void)depth;
    (void)ancestor;
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status flat_value_ref(
    void* backend,
    ft_incremental_ancestor_node node,
    const void** value_ref)
{
    (void)backend;
    (void)node;
    (void)value_ref;
    return FT_STATUS_OUT_OF_RANGE;
}

static const ft_incremental_ancestor_arena_vtable g_flat_vtable = {
    flat_bottom,
    flat_published_node_count,
    flat_add_leaf,
    flat_depth,
    flat_parent,
    flat_ancestor_at_depth,
    flat_value_ref,
    NULL
};

static void test_factories_create_independent_queues_and_reject_an_unanchored_arena(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_arena flat;
    ft_incremental_ancestor_arena_config flat_config;
    ft_ancestral_slice_queue explicit_queue;
    ft_ancestral_slice_queue ranged;
    ft_ancestral_slice_queue empty_range;
    ft_ancestral_slice_queue first;
    ft_ancestral_slice_queue second;
    ft_ancestral_slice_queue rejected;
    test_value values[3];
    test_value marker = make_value(0);
    size_t published = 0;
    int model[3];

    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init(&explicit_queue, &arena), FT_STATUS_OK);
    marker = make_value(10);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&explicit_queue, &marker, &explicit_queue),
        FT_STATUS_OK);
    marker = make_value(20);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&explicit_queue, &marker, &explicit_queue),
        FT_STATUS_OK);
    model[0] = 10;
    model[1] = 20;
    assert_state(&explicit_queue, model, 2);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &published),
        FT_STATUS_OK);
    REQUIRE(published == 2);
    REQUIRE(
        ft_ancestral_slice_queue_root_identity(&explicit_queue) ==
        ft_incremental_ancestor_arena_root_identity(&arena));

    values[0] = make_value(1);
    values[1] = make_value(2);
    values[2] = make_value(3);
    REQUIRE_STATUS(ft_ancestral_slice_queue_from_array(&ranged, &policy, values, 3), FT_STATUS_OK);
    model[0] = 1;
    model[1] = 2;
    model[2] = 3;
    assert_state(&ranged, model, 3);
    assert_append(&ranged, model, 3, 4);
    assert_state(&ranged, model, 3);

    /* An empty array is exactly an empty Myers-backed queue, and it is still appendable. */
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_from_array(&empty_range, &policy, NULL, 0),
        FT_STATUS_OK);
    assert_state(&empty_range, NULL, 0);
    assert_append(&empty_range, NULL, 0, 5);

    /* Each convenience initializer owns a private arena, so unrelated queues share no history. */
    REQUIRE_STATUS(ft_ancestral_slice_queue_init_myers(&first, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init_myers(&second, &policy), FT_STATUS_OK);
    REQUIRE(
        ft_ancestral_slice_queue_root_identity(&first) !=
        ft_ancestral_slice_queue_root_identity(&second));
    REQUIRE(
        ft_ancestral_slice_queue_root_identity(&ranged) !=
        ft_ancestral_slice_queue_root_identity(&empty_range));
    assert_append(&first, NULL, 0, 1);
    assert_append(&second, NULL, 0, 2);

    flat_config.vtable = &g_flat_vtable;
    flat_config.backend = &g_flat_backend;
    REQUIRE_STATUS(ft_incremental_ancestor_arena_create(&flat, &policy, &flat_config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init(&rejected, &flat), FT_STATUS_INVALID_ARGUMENT);
    ft_incremental_ancestor_arena_dispose(&flat);

    ft_ancestral_slice_queue_dispose(&second);
    ft_ancestral_slice_queue_dispose(&first);
    ft_ancestral_slice_queue_dispose(&empty_range);
    ft_ancestral_slice_queue_dispose(&ranged);
    ft_ancestral_slice_queue_dispose(&explicit_queue);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_failure_atomicity_and_lifetimes(void)
{
    enum { source_length = 8 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    ft_ancestral_slice_queue queue;
    ft_ancestral_slice_queue result;
    collect_state collected;
    int buffer[source_length];
    int* model = build_model(0, length);
    test_value value = make_value(99);
    test_value taken = make_value(0);
    bool removed = false;
    int step = 0;

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init(&queue, &arena), FT_STATUS_OK);
    for (step = 0; step != source_length; ++step) {
        const test_value item = make_value(step);
        REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&queue, &item, &queue), FT_STATUS_OK);
    }
    assert_state(&queue, model, length);

    /* A failing value copy leaves the source valid, publishes nothing, and does not disturb the
     * arena's counters. */
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_get_statistics(&arena, &before), FT_STATUS_OK);
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&queue, &value, &result),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after), FT_STATUS_OK);
    REQUIRE(after.add_leaf_count == before.add_leaf_count);
    REQUIRE(after.published_node_count == before.published_node_count);
    assert_state(&queue, model, length);

    /* The same holds when the result would alias the source. */
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&queue, &value, &queue),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    assert_state(&queue, model, length);

    /* A failing allocation inside the arena's value creation is reported without publishing. */
    context.fail_allocation_at = context.allocation_calls + 1;
    REQUIRE_STATUS(ft_ancestral_slice_queue_add_last(&queue, &value, &result), FT_STATUS_NO_MEMORY);
    context.fail_allocation_at = 0;
    assert_state(&queue, model, length);

    /* Owning reads and the removing reads withhold both the copy and the remainder on failure. */
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(ft_ancestral_slice_queue_first_copy(&queue, &taken), FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;

    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(ft_ancestral_slice_queue_last_copy(&queue, &taken), FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;

    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_try_remove_first(&queue, &removed, &taken, &result),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    assert_state(&queue, model, length);

    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_try_remove_last(&queue, &removed, &taken, &result),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;
    assert_state(&queue, model, length);

    /* A failing traversal buffer allocation is reported, and an aborting visitor propagates. */
    collected.values = buffer;
    collected.capacity = length;
    collected.count = 0;
    collected.stop_after = 0;
    context.fail_allocation_at = context.allocation_calls + 1;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_visit(&queue, collect_visit, &collected),
        FT_STATUS_NO_MEMORY);
    context.fail_allocation_at = 0;
    REQUIRE(collected.count == 0);

    collected.stop_after = 3;
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_visit(&queue, collect_visit, &collected),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(collected.count == 3);

    assert_state(&queue, model, length);
    ft_ancestral_slice_queue_dispose(&queue);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_handle_lifecycle_shares_rather_than_copies(void)
{
    enum { source_length = 6 };
    const size_t length = (size_t)source_length;
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue shared;
    ft_ancestral_slice_queue moved;
    ft_incremental_ancestor_arena arena;
    size_t published = 0;
    int* model = build_model(0, length);

    REQUIRE(model != NULL);
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 0, length), FT_STATUS_OK);

    REQUIRE_STATUS(ft_ancestral_slice_queue_copy(&source, &shared), FT_STATUS_OK);
    REQUIRE(
        ft_ancestral_slice_queue_root_identity(&shared) ==
        ft_ancestral_slice_queue_root_identity(&source));
    REQUIRE(shared.tail == source.tail);
    REQUIRE(shared.low_depth == source.low_depth);
    assert_state(&shared, model, length);

    ft_ancestral_slice_queue_move(&moved, &shared);
    REQUIRE(ft_ancestral_slice_queue_root_identity(&shared) == NULL);
    assert_state(&moved, model, length);
    ft_ancestral_slice_queue_dispose(&moved);
    assert_state(&source, model, length);

    /* The arena outlives every queue handle that reached it. */
    REQUIRE_STATUS(ft_ancestral_slice_queue_get_arena(&source, &arena), FT_STATUS_OK);
    REQUIRE(
        ft_incremental_ancestor_arena_root_identity(&arena) ==
        ft_ancestral_slice_queue_root_identity(&source));
    ft_ancestral_slice_queue_dispose(&source);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &published),
        FT_STATUS_OK);
    REQUIRE(published == length);
    ft_incremental_ancestor_arena_dispose(&arena);

    ft_incremental_ancestor_policy_dispose(&policy);
    free(model);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_invalid_arguments_are_rejected(void)
{
    test_context context;
    ft_incremental_ancestor_policy policy;
    ft_ancestral_slice_queue source;
    ft_ancestral_slice_queue result;
    ft_ancestral_slice_queue_split_result split;
    ft_ancestral_slice_queue uninitialized;
    const void* value_ref = NULL;
    test_value value = make_value(0);
    int model[3];

    (void)memset(&uninitialized, 0, sizeof(uninitialized));
    model[0] = 10;
    model[1] = 20;
    model[2] = 30;
    REQUIRE_STATUS(create_policy(&policy, &context), FT_STATUS_OK);
    REQUIRE_STATUS(build_queue(&source, &policy, 10, 3), FT_STATUS_OK);
    model[1] = 11;
    model[2] = 12;

    REQUIRE_STATUS(ft_ancestral_slice_queue_init(NULL, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_ancestral_slice_queue_init_myers(&result, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_from_array(&result, &policy, NULL, 3),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_copy(&uninitialized, &result),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_ancestral_slice_queue_copy(&source, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_ancestral_slice_queue_at_ref(&source, 0, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&source, NULL, &result),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&source, &value, NULL),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_ancestral_slice_queue_visit(&source, NULL, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_add_last(&uninitialized, &value, &result),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_get_arena(&uninitialized, NULL),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_ancestral_slice_queue_root_identity(&uninitialized) == NULL);
    REQUIRE(ft_ancestral_slice_queue_root_identity(NULL) == NULL);

    REQUIRE_STATUS(ft_ancestral_slice_queue_at_ref(&source, 3, &value_ref), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_at_ref(&source, SIZE_MAX, &value_ref),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_ancestral_slice_queue_at_copy(&source, 3, &value), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&source, 4, 0, &result), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_ancestral_slice_queue_slice(&source, 2, 2, &result), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_ancestral_slice_queue_slice(&source, 2, SIZE_MAX, &result),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_ancestral_slice_queue_take(&source, 4, &result), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_ancestral_slice_queue_drop(&source, 4, &result), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_ancestral_slice_queue_split_at(&source, 4, &split), FT_STATUS_OUT_OF_RANGE);
    assert_state(&source, model, 3);

    ft_ancestral_slice_queue_dispose(&source);
    ft_ancestral_slice_queue_dispose(&uninitialized);
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
        "Slice queue empty and endpoint contracts",
        test_empty_singleton_and_endpoint_contracts);
    run_test("Slice queue every slice boundary", test_every_slice_boundary_matches_the_model);
    run_test(
        "Slice queue take/drop/split boundaries",
        test_take_drop_and_split_partition_at_every_boundary);
    run_test(
        "Slice queue square and odd-block seams",
        test_square_and_odd_block_boundaries_support_index_slice_and_branching);
    run_test(
        "Slice queue traversal isolation",
        test_visitation_is_repeatable_and_does_not_observe_later_branches);
    run_test(
        "Slice queue endpoint-removal histories",
        test_endpoint_removal_histories_retain_every_version);
    run_test("Slice queue retained-branch independence", test_retained_branches_stay_independent);
    run_test(
        "Slice queue randomized branching history",
        test_randomized_branching_history_matches_array_models);
    run_test(
        "Slice queue backend complexity boundary",
        test_operations_use_the_parameterized_backend_complexity_boundary);
    run_test(
        "Slice queue factories and arena contract",
        test_factories_create_independent_queues_and_reject_an_unanchored_arena);
    run_test("Slice queue failure atomicity and lifetimes", test_failure_atomicity_and_lifetimes);
    run_test("Slice queue handle lifecycle", test_handle_lifecycle_shares_rather_than_copies);
    run_test("Slice queue invalid arguments", test_invalid_arguments_are_rejected);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C ancestral slice queue tests passed\n");
    return EXIT_SUCCESS;
}
