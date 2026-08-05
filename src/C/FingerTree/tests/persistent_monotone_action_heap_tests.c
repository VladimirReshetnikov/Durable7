/*
 * Tests for the persistent monotone-action heap.
 */

#include <durable7/finger_tree/persistent_monotone_action_heap.h>
#include <durable7/test_support/headless_test_process.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static const unsigned char g_element_type_identity = 0;
static const unsigned char g_priority_type_identity = 0;
static const unsigned char g_representative_type_identity = 0;

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

/* ---------------------------------------------------------------------------------------------
 * Instrumented policy hooks
 * ------------------------------------------------------------------------------------------- */

typedef struct test_context {
    size_t allocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t destroy_calls;
    size_t fail_copy_at;
    size_t compare_calls;
    size_t fail_compare_at;
} test_context;

typedef struct counting_algebra {
    ft_monotone_action_is_identity_fn is_identity;
    ft_monotone_action_compose_fn compose;
    ft_monotone_action_apply_fn apply;
    void* context;
    size_t identity_tests;
    size_t compositions;
    size_t applications;
    size_t fail_compose_at;
} counting_algebra;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* allocation = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 &&
        state->allocation_calls == state->fail_allocation_at) {
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

static ft_status tracked_int_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->copy_calls;
    if (state->fail_copy_at != 0 && state->copy_calls == state->fail_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(int*)destination = *(const int*)source;
    return FT_STATUS_OK;
}

static void tracked_int_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->destroy_calls;
}

static ft_status tracked_int_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    test_context* state = (test_context*)context;
    const int left_value = *(const int*)left;
    const int right_value = *(const int*)right;
    ++state->compare_calls;
    if (state->fail_compare_at != 0 && state->compare_calls == state->fail_compare_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *comparison = (left_value > right_value) - (left_value < right_value);
    return FT_STATUS_OK;
}

static ft_status counting_is_identity(const void* action, bool* is_identity, void* context)
{
    counting_algebra* algebra = (counting_algebra*)context;
    ++algebra->identity_tests;
    return algebra->is_identity(action, is_identity, algebra->context);
}

static ft_status counting_compose(
    void* destination,
    const void* outer,
    const void* inner,
    void* context)
{
    counting_algebra* algebra = (counting_algebra*)context;
    ++algebra->compositions;
    if (algebra->fail_compose_at != 0 && algebra->compositions == algebra->fail_compose_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    return algebra->compose(destination, outer, inner, algebra->context);
}

static ft_status counting_apply(
    void* destination,
    const void* action,
    const void* priority,
    void* context)
{
    counting_algebra* algebra = (counting_algebra*)context;
    ++algebra->applications;
    return algebra->apply(destination, action, priority, algebra->context);
}

/* ---------------------------------------------------------------------------------------------
 * Fixture
 * ------------------------------------------------------------------------------------------- */

typedef struct fixture {
    test_context context;
    counting_algebra algebra;
    ft_monotone_action_clamp_family family;
    ft_monotone_action_policy policy;
} fixture;

static void fixture_reset_counts(fixture* subject)
{
    subject->context.allocation_calls = 0;
    subject->context.copy_calls = 0;
    subject->context.destroy_calls = 0;
    subject->context.compare_calls = 0;
    subject->algebra.identity_tests = 0;
    subject->algebra.compositions = 0;
    subject->algebra.applications = 0;
}

static ft_status fixture_init(fixture* subject)
{
    ft_monotone_action_type_policy element;
    ft_monotone_action_type_policy priority;
    ft_monotone_action_allocator allocator;
    ft_monotone_action_policy_config config;
    ft_status status = FT_STATUS_OK;
    (void)memset(subject, 0, sizeof(*subject));
    ft_monotone_action_type_policy_init(
        &element, sizeof(int), &g_element_type_identity, &subject->context);
    element.copy = tracked_int_copy;
    element.destroy = tracked_int_destroy;
    ft_monotone_action_type_policy_init(
        &priority, sizeof(int), &g_priority_type_identity, &subject->context);
    priority.copy = tracked_int_copy;
    priority.destroy = tracked_int_destroy;
    allocator.allocate = tracked_allocate;
    allocator.deallocate = tracked_deallocate;
    allocator.context = &subject->context;
    status = ft_monotone_action_clamp_family_create(
        &subject->family, &priority, tracked_int_compare, &allocator);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_monotone_action_clamp_family_config_init(&subject->family, &config, &element);
    subject->algebra.is_identity = config.is_identity;
    subject->algebra.compose = config.compose;
    subject->algebra.apply = config.apply;
    subject->algebra.context = config.algebra_context;
    config.is_identity = counting_is_identity;
    config.compose = counting_compose;
    config.apply = counting_apply;
    config.algebra_context = &subject->algebra;
    status = ft_monotone_action_policy_create(&subject->policy, &config);
    if (status != FT_STATUS_OK) {
        ft_monotone_action_clamp_family_dispose(&subject->family);
    }
    return status;
}

static void fixture_dispose(fixture* subject)
{
    ft_monotone_action_policy_dispose(&subject->policy);
    ft_monotone_action_clamp_family_dispose(&subject->family);
}

/* ---------------------------------------------------------------------------------------------
 * Entry collection helpers
 * ------------------------------------------------------------------------------------------- */

typedef struct entry_pair {
    int element;
    int priority;
} entry_pair;

typedef struct collector {
    entry_pair* items;
    size_t count;
    size_t capacity;
    bool overflowed;
} collector;

static ft_status collect_visit(const void* element, const void* priority, void* context)
{
    collector* target = (collector*)context;
    if (target->count == target->capacity) {
        target->overflowed = true;
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    target->items[target->count].element = *(const int*)element;
    target->items[target->count].priority = *(const int*)priority;
    ++target->count;
    return FT_STATUS_OK;
}

static int compare_entries(const void* left, const void* right)
{
    const entry_pair* first = (const entry_pair*)left;
    const entry_pair* second = (const entry_pair*)right;
    if (first->element != second->element) {
        return first->element < second->element ? -1 : 1;
    }
    if (first->priority != second->priority) {
        return first->priority < second->priority ? -1 : 1;
    }
    return 0;
}

static int compare_int_values(const void* left, const void* right)
{
    const int first = *(const int*)left;
    const int second = *(const int*)right;
    return (first > second) - (first < second);
}

static bool ordered_entries(
    const ft_monotone_action_heap* heap,
    entry_pair* buffer,
    size_t capacity,
    size_t* count)
{
    collector target;
    target.items = buffer;
    target.count = 0;
    target.capacity = capacity;
    target.overflowed = false;
    if (ft_monotone_action_heap_visit(heap, collect_visit, &target) != FT_STATUS_OK) {
        return false;
    }
    qsort(buffer, target.count, sizeof(*buffer), compare_entries);
    *count = target.count;
    return true;
}

static bool entries_match(
    const ft_monotone_action_heap* heap,
    const entry_pair* expected,
    size_t expected_count)
{
    entry_pair observed[512];
    entry_pair sorted[512];
    size_t count = 0;
    if (expected_count > 512) {
        return false;
    }
    if (!ordered_entries(heap, observed, 512, &count) || count != expected_count) {
        return false;
    }
    if (ft_monotone_action_heap_size(heap) != expected_count) {
        return false;
    }
    if (expected_count != 0) {
        (void)memcpy(sorted, expected, expected_count * sizeof(*sorted));
        qsort(sorted, expected_count, sizeof(*sorted), compare_entries);
    }
    return memcmp(sorted, observed, expected_count * sizeof(*sorted)) == 0;
}

static bool heap_valid(const ft_monotone_action_heap* heap)
{
    bool valid = false;
    ft_monotone_action_heap_statistics statistics;
    return ft_monotone_action_heap_validate(heap, &valid, &statistics) == FT_STATUS_OK &&
        valid && statistics.count == ft_monotone_action_heap_size(heap);
}

/* Removes every entry, checking that priorities leave in nondecreasing order. */
static bool drain_sorted(
    const ft_monotone_action_heap* heap,
    entry_pair* buffer,
    size_t capacity,
    size_t* count)
{
    ft_monotone_action_heap current;
    size_t index = 0;
    bool ok = true;
    if (ft_monotone_action_heap_copy(heap, &current) != FT_STATUS_OK) {
        return false;
    }
    for (;;) {
        bool removed = false;
        int element = 0;
        int priority = 0;
        if (ft_monotone_action_heap_try_delete_minimum(
                &current, &removed, &element, &priority, &current) != FT_STATUS_OK) {
            ok = false;
            break;
        }
        if (!removed) {
            break;
        }
        if (index == capacity) {
            ok = false;
            break;
        }
        if (index != 0 && buffer[index - 1].priority > priority) {
            ok = false;
            break;
        }
        buffer[index].element = element;
        buffer[index].priority = priority;
        ++index;
        if ((index & 255) == 0 && !heap_valid(&current)) {
            ok = false;
            break;
        }
    }
    if (ok) {
        ok = ft_monotone_action_heap_empty(&current) &&
            index == ft_monotone_action_heap_size(heap);
    }
    ft_monotone_action_heap_dispose(&current);
    *count = index;
    return ok;
}

/* ---------------------------------------------------------------------------------------------
 * Clamp action models
 * ------------------------------------------------------------------------------------------- */

typedef enum model_kind {
    MODEL_IDENTITY,
    MODEL_AT_LEAST,
    MODEL_AT_MOST,
    MODEL_BETWEEN,
    MODEL_CONSTANT
} model_kind;

typedef struct model_action {
    model_kind kind;
    int lower;
    int upper;
} model_action;

static int model_apply(const model_action* action, int priority)
{
    switch (action->kind) {
    case MODEL_AT_LEAST:
        return priority < action->lower ? action->lower : priority;
    case MODEL_AT_MOST:
        return priority > action->upper ? action->upper : priority;
    case MODEL_BETWEEN:
        if (priority < action->lower) {
            return action->lower;
        }
        return priority > action->upper ? action->upper : priority;
    case MODEL_CONSTANT:
        return action->lower;
    case MODEL_IDENTITY:
    default:
        return priority;
    }
}

static ft_status make_action(
    const fixture* subject,
    const model_action* model,
    ft_monotone_action_clamp* action)
{
    switch (model->kind) {
    case MODEL_AT_LEAST:
        return ft_monotone_action_clamp_at_least(&subject->family, &model->lower, action);
    case MODEL_AT_MOST:
        return ft_monotone_action_clamp_at_most(&subject->family, &model->upper, action);
    case MODEL_BETWEEN:
        return ft_monotone_action_clamp_between(
            &subject->family, &model->lower, &model->upper, action);
    case MODEL_CONSTANT:
        return ft_monotone_action_clamp_constant(&subject->family, &model->lower, action);
    case MODEL_IDENTITY:
    default:
        ft_monotone_action_clamp_init_identity(action);
        return FT_STATUS_OK;
    }
}

static ft_status transform_by_model(
    const fixture* subject,
    const ft_monotone_action_heap* heap,
    const model_action* model,
    ft_monotone_action_heap* result)
{
    ft_monotone_action_clamp action;
    ft_status status = make_action(subject, model, &action);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_monotone_action_heap_transform_all(heap, &action, result);
    ft_monotone_action_clamp_dispose(&subject->family, &action);
    return status;
}

typedef struct rng {
    uint64_t state;
} rng;

static uint64_t rng_next(rng* generator)
{
    uint64_t state = generator->state;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    generator->state = state;
    return state;
}

static size_t rng_below(rng* generator, size_t bound)
{
    return (size_t)(rng_next(generator) % (uint64_t)bound);
}

static int rng_between(rng* generator, int low, int high)
{
    return low + (int)rng_below(generator, (size_t)(high - low + 1));
}

static ft_status insert_entry(
    const ft_monotone_action_heap* heap,
    int element,
    int priority,
    ft_monotone_action_heap* result)
{
    return ft_monotone_action_heap_insert(heap, &element, &priority, result);
}

/* ---------------------------------------------------------------------------------------------
 * Clamp algebra
 * ------------------------------------------------------------------------------------------- */

typedef struct algebra_view {
    ft_monotone_action_is_identity_fn is_identity;
    ft_monotone_action_compose_fn compose;
    ft_monotone_action_apply_fn apply;
    void* context;
} algebra_view;

static int applied(const algebra_view* view, const ft_monotone_action_clamp* action, int value)
{
    int result = 0;
    if (view->apply(&result, action, &value, view->context) != FT_STATUS_OK) {
        return INT_MIN;
    }
    return result;
}

static void test_clamp_algebra(void)
{
    enum { action_count = 8, sample_count = 43 };
    fixture subject;
    algebra_view view;
    ft_monotone_action_clamp actions[action_count];
    ft_monotone_action_clamp identity;
    ft_monotone_action_clamp composed;
    ft_monotone_action_clamp floor_then_cap;
    ft_monotone_action_clamp cap_then_floor;
    ft_monotone_action_clamp rejected;
    int samples[sample_count];
    int bounds[2];
    bool flag = false;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    view.is_identity = subject.algebra.is_identity;
    view.compose = subject.algebra.compose;
    view.apply = subject.algebra.apply;
    view.context = subject.algebra.context;
    for (int index = 0; index != 41; ++index) {
        samples[index] = index - 20;
    }
    samples[41] = INT_MIN;
    samples[42] = INT_MAX;

    ft_monotone_action_clamp_init_identity(&identity);
    ft_monotone_action_clamp_init_identity(&actions[0]);
    bounds[0] = -3;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&subject.family, &bounds[0], &actions[1]),
        FT_STATUS_OK);
    bounds[0] = 8;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&subject.family, &bounds[0], &actions[2]),
        FT_STATUS_OK);
    bounds[0] = -4;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_most(&subject.family, &bounds[0], &actions[3]),
        FT_STATUS_OK);
    bounds[0] = 11;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_most(&subject.family, &bounds[0], &actions[4]),
        FT_STATUS_OK);
    bounds[0] = -6;
    bounds[1] = 9;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_between(&subject.family, &bounds[0], &bounds[1], &actions[5]),
        FT_STATUS_OK);
    bounds[0] = 5;
    bounds[1] = 5;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_between(&subject.family, &bounds[0], &bounds[1], &actions[6]),
        FT_STATUS_OK);
    bounds[0] = -9;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_constant(&subject.family, &bounds[0], &actions[7]),
        FT_STATUS_OK);
    bounds[0] = 2;
    bounds[1] = 1;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_between(&subject.family, &bounds[0], &bounds[1], &rejected),
        FT_STATUS_INVALID_ARGUMENT);

    REQUIRE(ft_monotone_action_clamp_is_identity(&identity));
    REQUIRE_STATUS(view.is_identity(&identity, &flag, view.context), FT_STATUS_OK);
    REQUIRE(flag);
    REQUIRE(!ft_monotone_action_clamp_is_identity(&actions[1]));
    REQUIRE(ft_monotone_action_clamp_is_constant(&actions[7]));
    REQUIRE(*(const int*)ft_monotone_action_clamp_constant_ref(&actions[7]) == -9);
    REQUIRE(ft_monotone_action_clamp_constant_ref(&actions[1]) == NULL);
    REQUIRE(*(const int*)ft_monotone_action_clamp_lower_bound_ref(&actions[1]) == -3);
    REQUIRE(ft_monotone_action_clamp_upper_bound_ref(&actions[1]) == NULL);
    REQUIRE(*(const int*)ft_monotone_action_clamp_upper_bound_ref(&actions[4]) == 11);
    REQUIRE(ft_monotone_action_clamp_lower_bound_ref(&actions[4]) == NULL);

    /* Identity is a two-sided unit. */
    for (int index = 0; index != action_count; ++index) {
        REQUIRE_STATUS(
            view.compose(&composed, &identity, &actions[index], view.context), FT_STATUS_OK);
        for (int sample = 0; sample != sample_count; ++sample) {
            REQUIRE(applied(&view, &composed, samples[sample]) ==
                applied(&view, &actions[index], samples[sample]));
        }
        ft_monotone_action_clamp_dispose(&subject.family, &composed);
        REQUIRE_STATUS(
            view.compose(&composed, &actions[index], &identity, view.context), FT_STATUS_OK);
        for (int sample = 0; sample != sample_count; ++sample) {
            REQUIRE(applied(&view, &composed, samples[sample]) ==
                applied(&view, &actions[index], samples[sample]));
        }
        ft_monotone_action_clamp_dispose(&subject.family, &composed);
    }

    /* Disjoint bounds collapse to an explicit constant carrying the newer boundary. */
    REQUIRE_STATUS(
        view.compose(&floor_then_cap, &actions[3], &actions[2], view.context), FT_STATUS_OK);
    REQUIRE_STATUS(
        view.compose(&cap_then_floor, &actions[2], &actions[3], view.context), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_clamp_is_constant(&floor_then_cap));
    REQUIRE(ft_monotone_action_clamp_is_constant(&cap_then_floor));
    REQUIRE(*(const int*)ft_monotone_action_clamp_constant_ref(&floor_then_cap) == -4);
    REQUIRE(*(const int*)ft_monotone_action_clamp_constant_ref(&cap_then_floor) == 8);
    for (int sample = 0; sample != sample_count; ++sample) {
        REQUIRE(applied(&view, &floor_then_cap, samples[sample]) == -4);
        REQUIRE(applied(&view, &cap_then_floor, samples[sample]) == 8);
    }
    ft_monotone_action_clamp_dispose(&subject.family, &floor_then_cap);
    ft_monotone_action_clamp_dispose(&subject.family, &cap_then_floor);

    /* compose(outer, inner) denotes outer(inner(priority)) pointwise. */
    for (int inner = 0; inner != action_count; ++inner) {
        for (int outer = 0; outer != action_count; ++outer) {
            REQUIRE_STATUS(
                view.compose(&composed, &actions[outer], &actions[inner], view.context),
                FT_STATUS_OK);
            for (int sample = 0; sample != sample_count; ++sample) {
                const int sequential = applied(
                    &view, &actions[outer], applied(&view, &actions[inner], samples[sample]));
                REQUIRE(applied(&view, &composed, samples[sample]) == sequential);
            }
            ft_monotone_action_clamp_dispose(&subject.family, &composed);
        }
    }

    /* Composition is associative. */
    for (int first = 0; first != action_count; ++first) {
        for (int second = 0; second != action_count; ++second) {
            for (int third = 0; third != action_count; ++third) {
                ft_monotone_action_clamp inner_first;
                ft_monotone_action_clamp outer_first;
                ft_monotone_action_clamp left;
                ft_monotone_action_clamp right;
                REQUIRE_STATUS(
                    view.compose(&inner_first, &actions[second], &actions[first], view.context),
                    FT_STATUS_OK);
                REQUIRE_STATUS(
                    view.compose(&left, &actions[third], &inner_first, view.context),
                    FT_STATUS_OK);
                REQUIRE_STATUS(
                    view.compose(&outer_first, &actions[third], &actions[second], view.context),
                    FT_STATUS_OK);
                REQUIRE_STATUS(
                    view.compose(&right, &outer_first, &actions[first], view.context),
                    FT_STATUS_OK);
                for (int sample = 0; sample != sample_count; ++sample) {
                    REQUIRE(applied(&view, &left, samples[sample]) ==
                        applied(&view, &right, samples[sample]));
                }
                ft_monotone_action_clamp_dispose(&subject.family, &right);
                ft_monotone_action_clamp_dispose(&subject.family, &outer_first);
                ft_monotone_action_clamp_dispose(&subject.family, &left);
                ft_monotone_action_clamp_dispose(&subject.family, &inner_first);
            }
        }
    }

    /* Every member is monotone for the retained comparer. */
    for (int index = 0; index != action_count; ++index) {
        for (int low = -12; low <= 12; ++low) {
            for (int high = low; high <= 12; ++high) {
                REQUIRE(applied(&view, &actions[index], low) <=
                    applied(&view, &actions[index], high));
            }
        }
    }

    for (int index = 0; index != action_count; ++index) {
        ft_monotone_action_clamp_dispose(&subject.family, &actions[index]);
    }
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
}

/* ---------------------------------------------------------------------------------------------
 * Exact representatives under a coarse comparer
 * ------------------------------------------------------------------------------------------- */

typedef struct representative {
    int key;
    int label;
} representative;

static ft_status representative_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    const int left_key = ((const representative*)left)->key;
    const int right_key = ((const representative*)right)->key;
    (void)context;
    *comparison = (left_key > right_key) - (left_key < right_key);
    return FT_STATUS_OK;
}

static int applied_label(
    const algebra_view* view,
    const ft_monotone_action_clamp* action,
    const representative* value)
{
    representative result;
    result.key = 0;
    result.label = 0;
    if (view->apply(&result, action, value, view->context) != FT_STATUS_OK) {
        return -1;
    }
    return result.label;
}

static void test_clamp_preserves_exact_representatives(void)
{
    ft_monotone_action_type_policy priority;
    ft_monotone_action_clamp_family family;
    ft_monotone_action_policy_config config;
    algebra_view view;
    ft_monotone_action_clamp older_lower;
    ft_monotone_action_clamp newer_upper;
    ft_monotone_action_clamp older_upper;
    ft_monotone_action_clamp newer_lower;
    ft_monotone_action_clamp floor_then_cap;
    ft_monotone_action_clamp cap_then_floor;
    ft_monotone_action_clamp touching;
    ft_monotone_action_clamp equal_cap;
    ft_monotone_action_clamp equal_floor;
    representative values[6];
    representative below;
    representative above;
    representative older_cap_value;
    representative newer_floor_value;
    representative inside;
    ft_monotone_action_type_policy_init(
        &priority, sizeof(representative), &g_representative_type_identity, NULL);
    REQUIRE_STATUS(
        ft_monotone_action_clamp_family_create(
            &family, &priority, representative_compare, NULL),
        FT_STATUS_OK);
    ft_monotone_action_clamp_family_config_init(&family, &config, &priority);
    view.is_identity = config.is_identity;
    view.compose = config.compose;
    view.apply = config.apply;
    view.context = config.algebra_context;

    values[0].key = 10;
    values[0].label = 1;
    values[1].key = 5;
    values[1].label = 2;
    values[2].key = 5;
    values[2].label = 3;
    values[3].key = 5;
    values[3].label = 4;
    values[4].key = 10;
    values[4].label = 5;
    values[5].key = 10;
    values[5].label = 6;
    below.key = -100;
    below.label = 90;
    above.key = 100;
    above.label = 91;

    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&family, &values[0], &older_lower), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_most(&family, &values[1], &newer_upper), FT_STATUS_OK);
    REQUIRE_STATUS(
        view.compose(&floor_then_cap, &newer_upper, &older_lower, view.context), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_clamp_is_constant(&floor_then_cap));
    REQUIRE(((const representative*)
        ft_monotone_action_clamp_constant_ref(&floor_then_cap))->label == 2);
    REQUIRE(applied_label(&view, &floor_then_cap, &values[2]) == 2);
    REQUIRE(applied_label(&view, &floor_then_cap, &below) == 2);
    REQUIRE(applied_label(&view, &floor_then_cap, &above) == 2);

    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_most(&family, &values[3], &older_upper), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&family, &values[4], &newer_lower), FT_STATUS_OK);
    REQUIRE_STATUS(
        view.compose(&cap_then_floor, &newer_lower, &older_upper, view.context), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_clamp_is_constant(&cap_then_floor));
    REQUIRE(((const representative*)
        ft_monotone_action_clamp_constant_ref(&cap_then_floor))->label == 5);
    REQUIRE(applied_label(&view, &cap_then_floor, &values[5]) == 5);
    REQUIRE(applied_label(&view, &cap_then_floor, &below) == 5);
    REQUIRE(applied_label(&view, &cap_then_floor, &above) == 5);

    /* Equal order classes overlap rather than collapse: each side keeps its own exact
     * representative and an input in the shared class passes through unchanged. */
    older_cap_value.key = 7;
    older_cap_value.label = 11;
    newer_floor_value.key = 7;
    newer_floor_value.label = 12;
    inside.key = 7;
    inside.label = 13;
    below.key = 6;
    below.label = 14;
    above.key = 8;
    above.label = 15;
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_most(&family, &older_cap_value, &equal_cap), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&family, &newer_floor_value, &equal_floor),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        view.compose(&touching, &equal_floor, &equal_cap, view.context), FT_STATUS_OK);
    REQUIRE(!ft_monotone_action_clamp_is_constant(&touching));
    REQUIRE(((const representative*)
        ft_monotone_action_clamp_lower_bound_ref(&touching))->label == 12);
    REQUIRE(((const representative*)
        ft_monotone_action_clamp_upper_bound_ref(&touching))->label == 11);
    REQUIRE(applied_label(&view, &touching, &below) == 12);
    REQUIRE(applied_label(&view, &touching, &inside) == 13);
    REQUIRE(applied_label(&view, &touching, &above) == 11);

    ft_monotone_action_clamp_dispose(&family, &touching);
    ft_monotone_action_clamp_dispose(&family, &equal_floor);
    ft_monotone_action_clamp_dispose(&family, &equal_cap);
    ft_monotone_action_clamp_dispose(&family, &cap_then_floor);
    ft_monotone_action_clamp_dispose(&family, &floor_then_cap);
    ft_monotone_action_clamp_dispose(&family, &newer_lower);
    ft_monotone_action_clamp_dispose(&family, &older_upper);
    ft_monotone_action_clamp_dispose(&family, &newer_upper);
    ft_monotone_action_clamp_dispose(&family, &older_lower);
    ft_monotone_action_clamp_family_dispose(&family);
}

/* ---------------------------------------------------------------------------------------------
 * Temporal semantics
 * ------------------------------------------------------------------------------------------- */

static void test_temporal_insert_after_noninvertible_transform(void)
{
    fixture subject;
    ft_monotone_action_heap source;
    ft_monotone_action_heap transformed;
    ft_monotone_action_heap inserted;
    ft_monotone_action_heap deleted;
    ft_monotone_action_heap bounded;
    ft_monotone_action_heap later_high;
    ft_monotone_action_heap transformed_again;
    ft_monotone_action_heap newest;
    model_action floor_ten = {MODEL_AT_LEAST, 10, 0};
    model_action zero_to_ten = {MODEL_BETWEEN, 0, 10};
    model_action floor_five = {MODEL_AT_LEAST, 5, 0};
    const entry_pair source_entries[3] = {{1, -10}, {2, 5}, {3, 20}};
    const entry_pair transformed_entries[3] = {{1, 10}, {2, 10}, {3, 20}};
    const entry_pair inserted_entries[4] = {{1, 10}, {2, 10}, {3, 20}, {4, 0}};
    const entry_pair bounded_entries[2] = {{1, 0}, {2, 10}};
    const entry_pair later_entries[3] = {{1, 0}, {2, 10}, {3, 50}};
    const entry_pair again_entries[3] = {{1, 5}, {2, 10}, {3, 50}};
    const entry_pair newest_entries[4] = {{1, 5}, {2, 10}, {3, 50}, {4, -20}};
    ft_monotone_action_entry_ref minimum;
    bool found = false;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&source, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&source, 1, -10, &source), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&source, 2, 5, &source), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&source, 3, 20, &source), FT_STATUS_OK);
    REQUIRE_STATUS(
        transform_by_model(&subject, &source, &floor_ten, &transformed), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&transformed, 4, 0, &inserted), FT_STATUS_OK);

    REQUIRE(entries_match(&source, source_entries, 3));
    REQUIRE(entries_match(&transformed, transformed_entries, 3));
    REQUIRE(entries_match(&inserted, inserted_entries, 4));
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_ref(&inserted, &found, &minimum), FT_STATUS_OK);
    REQUIRE(found && *(const int*)minimum.element == 4 && *(const int*)minimum.priority == 0);

    REQUIRE_STATUS(ft_monotone_action_heap_delete_minimum(&inserted, &deleted), FT_STATUS_OK);
    REQUIRE(entries_match(&deleted, transformed_entries, 3));
    REQUIRE(entries_match(&transformed, transformed_entries, 3));
    REQUIRE(heap_valid(&source));
    REQUIRE(heap_valid(&transformed));
    REQUIRE(heap_valid(&inserted));
    REQUIRE(heap_valid(&deleted));

    /* The opposite root-selection case: the transformed root wins, and exposing it is what keeps
     * the old bound off the newly inserted child. */
    REQUIRE_STATUS(ft_monotone_action_heap_init(&bounded, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&bounded, 1, -10, &bounded), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&bounded, 2, 100, &bounded), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &bounded, &zero_to_ten, &bounded), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&bounded, 3, 50, &later_high), FT_STATUS_OK);
    REQUIRE(entries_match(&bounded, bounded_entries, 2));
    REQUIRE(entries_match(&later_high, later_entries, 3));
    REQUIRE_STATUS(
        transform_by_model(&subject, &later_high, &floor_five, &transformed_again),
        FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&transformed_again, 4, -20, &newest), FT_STATUS_OK);
    REQUIRE(entries_match(&transformed_again, again_entries, 3));
    REQUIRE(entries_match(&newest, newest_entries, 4));
    REQUIRE(heap_valid(&bounded));
    REQUIRE(heap_valid(&later_high));
    REQUIRE(heap_valid(&transformed_again));
    REQUIRE(heap_valid(&newest));

    ft_monotone_action_heap_dispose(&newest);
    ft_monotone_action_heap_dispose(&transformed_again);
    ft_monotone_action_heap_dispose(&later_high);
    ft_monotone_action_heap_dispose(&bounded);
    ft_monotone_action_heap_dispose(&deleted);
    ft_monotone_action_heap_dispose(&inserted);
    ft_monotone_action_heap_dispose(&transformed);
    ft_monotone_action_heap_dispose(&source);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
}

static void test_meld_does_not_cross_apply_actions(void)
{
    enum { count = 257 };
    fixture subject;
    ft_monotone_action_heap raw_left;
    ft_monotone_action_heap raw_right;
    ft_monotone_action_heap left;
    ft_monotone_action_heap right;
    ft_monotone_action_heap melded;
    ft_monotone_action_heap many_left;
    ft_monotone_action_heap many_right;
    ft_monotone_action_heap many_melded;
    model_action floor_ten = {MODEL_AT_LEAST, 10, 0};
    model_action cap_minus_ten = {MODEL_AT_MOST, 0, -10};
    model_action left_window = {MODEL_BETWEEN, -30, 20};
    model_action right_window = {MODEL_BETWEEN, 40, 70};
    const entry_pair raw_left_entries[2] = {{1, -20}, {2, 3}};
    const entry_pair raw_right_entries[2] = {{3, -4}, {4, 30}};
    const entry_pair left_entries[2] = {{1, 10}, {2, 10}};
    const entry_pair right_entries[2] = {{3, -10}, {4, -10}};
    const entry_pair melded_entries[4] = {{1, 10}, {2, 10}, {3, -10}, {4, -10}};
    int* expected = (int*)malloc(2 * (size_t)count * sizeof(int));
    entry_pair* drained = (entry_pair*)malloc(2 * (size_t)count * sizeof(entry_pair));
    size_t drained_count = 0;
    ft_monotone_action_entry_ref minimum;
    bool found = false;
    REQUIRE(expected != NULL && drained != NULL);
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&raw_left, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&raw_right, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&raw_left, 1, -20, &raw_left), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&raw_left, 2, 3, &raw_left), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&raw_right, 3, -4, &raw_right), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&raw_right, 4, 30, &raw_right), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &raw_left, &floor_ten, &left), FT_STATUS_OK);
    REQUIRE_STATUS(
        transform_by_model(&subject, &raw_right, &cap_minus_ten, &right), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_meld(&left, &right, &melded), FT_STATUS_OK);

    REQUIRE(entries_match(&raw_left, raw_left_entries, 2));
    REQUIRE(entries_match(&raw_right, raw_right_entries, 2));
    REQUIRE(entries_match(&left, left_entries, 2));
    REQUIRE(entries_match(&right, right_entries, 2));
    REQUIRE(entries_match(&melded, melded_entries, 4));
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_ref(&melded, &found, &minimum), FT_STATUS_OK);
    REQUIRE(found && *(const int*)minimum.priority == -10);
    REQUIRE(heap_valid(&left));
    REQUIRE(heap_valid(&right));
    REQUIRE(heap_valid(&melded));

    REQUIRE_STATUS(ft_monotone_action_heap_init(&many_left, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&many_right, &subject.policy), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        const int left_priority = ((index * 37) % 401) - 200;
        const int right_priority = ((index * 53) % 401) - 200;
        REQUIRE_STATUS(insert_entry(&many_left, index, left_priority, &many_left), FT_STATUS_OK);
        REQUIRE_STATUS(
            insert_entry(&many_right, index + count, right_priority, &many_right),
            FT_STATUS_OK);
        expected[2 * index] = model_apply(&left_window, left_priority);
        expected[2 * index + 1] = model_apply(&right_window, right_priority);
    }
    REQUIRE_STATUS(
        transform_by_model(&subject, &many_left, &left_window, &many_left), FT_STATUS_OK);
    REQUIRE_STATUS(
        transform_by_model(&subject, &many_right, &right_window, &many_right), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_heap_meld(&many_left, &many_right, &many_melded), FT_STATUS_OK);
    REQUIRE(heap_valid(&many_left));
    REQUIRE(heap_valid(&many_right));
    REQUIRE(heap_valid(&many_melded));
    REQUIRE(drain_sorted(&many_melded, drained, 2 * (size_t)count, &drained_count));
    REQUIRE(drained_count == 2 * (size_t)count);
    qsort(expected, 2 * (size_t)count, sizeof(*expected), compare_int_values);
    for (size_t index = 0; index != drained_count; ++index) {
        REQUIRE(drained[index].priority == expected[index]);
    }

    ft_monotone_action_heap_dispose(&many_melded);
    ft_monotone_action_heap_dispose(&many_right);
    ft_monotone_action_heap_dispose(&many_left);
    ft_monotone_action_heap_dispose(&melded);
    ft_monotone_action_heap_dispose(&right);
    ft_monotone_action_heap_dispose(&left);
    ft_monotone_action_heap_dispose(&raw_right);
    ft_monotone_action_heap_dispose(&raw_left);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
    free(drained);
    free(expected);
}

/* ---------------------------------------------------------------------------------------------
 * Persistence across divergent branches
 * ------------------------------------------------------------------------------------------- */

static void test_updates_are_persistent_across_branches(void)
{
    fixture subject;
    ft_monotone_action_heap original;
    ft_monotone_action_heap floored;
    ft_monotone_action_heap with_future;
    ft_monotone_action_heap deleted;
    ft_monotone_action_heap capped;
    ft_monotone_action_heap melded;
    ft_monotone_action_heap unchanged;
    model_action floor_five = {MODEL_AT_LEAST, 5, 0};
    model_action cap_three = {MODEL_AT_MOST, 0, 3};
    model_action identity = {MODEL_IDENTITY, 0, 0};
    const entry_pair original_entries[3] = {{1, 1}, {2, 4}, {3, 9}};
    const entry_pair floored_entries[3] = {{1, 5}, {2, 5}, {3, 9}};
    const entry_pair future_entries[4] = {{1, 5}, {2, 5}, {3, 9}, {4, 0}};
    const entry_pair capped_entries[3] = {{1, 1}, {2, 3}, {3, 3}};
    const entry_pair melded_entries[6] = {{1, 1}, {1, 5}, {2, 3}, {2, 5}, {3, 3}, {3, 9}};
    const void* original_root = NULL;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&original, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&original, 1, 1, &original), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&original, 2, 4, &original), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&original, 3, 9, &original), FT_STATUS_OK);
    original_root = ft_monotone_action_heap_root_identity(&original);
    REQUIRE_STATUS(transform_by_model(&subject, &original, &floor_five, &floored), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&floored, 4, 0, &with_future), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_heap_delete_minimum(&with_future, &deleted), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &original, &cap_three, &capped), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_meld(&deleted, &capped, &melded), FT_STATUS_OK);
    REQUIRE_STATUS(
        transform_by_model(&subject, &original, &identity, &unchanged), FT_STATUS_OK);

    REQUIRE(ft_monotone_action_heap_root_identity(&original) == original_root);
    REQUIRE(ft_monotone_action_heap_root_identity(&floored) != original_root);
    REQUIRE(ft_monotone_action_heap_root_identity(&unchanged) == original_root);
    REQUIRE(entries_match(&original, original_entries, 3));
    REQUIRE(entries_match(&floored, floored_entries, 3));
    REQUIRE(entries_match(&with_future, future_entries, 4));
    REQUIRE(entries_match(&deleted, floored_entries, 3));
    REQUIRE(entries_match(&capped, capped_entries, 3));
    REQUIRE(entries_match(&melded, melded_entries, 6));
    REQUIRE(heap_valid(&original));
    REQUIRE(heap_valid(&floored));
    REQUIRE(heap_valid(&with_future));
    REQUIRE(heap_valid(&deleted));
    REQUIRE(heap_valid(&capped));
    REQUIRE(heap_valid(&melded));

    ft_monotone_action_heap_dispose(&unchanged);
    ft_monotone_action_heap_dispose(&melded);
    ft_monotone_action_heap_dispose(&capped);
    ft_monotone_action_heap_dispose(&deleted);
    ft_monotone_action_heap_dispose(&with_future);
    ft_monotone_action_heap_dispose(&floored);
    ft_monotone_action_heap_dispose(&original);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
}

/* ---------------------------------------------------------------------------------------------
 * Randomized retained history against an immutable model
 * ------------------------------------------------------------------------------------------- */

#define MODEL_LIMIT 192
#define VERSION_LIMIT 1024

typedef struct model_version {
    ft_monotone_action_heap heap;
    entry_pair entries[MODEL_LIMIT];
    size_t count;
    const void* root;
} model_version;

static bool version_matches_model(const model_version* version)
{
    entry_pair observed[MODEL_LIMIT];
    entry_pair expected[MODEL_LIMIT];
    size_t count = 0;
    bool found = false;
    ft_monotone_action_entry_ref minimum;
    if (ft_monotone_action_heap_root_identity(&version->heap) != version->root) {
        return false;
    }
    if (!heap_valid(&version->heap) ||
        ft_monotone_action_heap_size(&version->heap) != version->count) {
        return false;
    }
    if (!ordered_entries(&version->heap, observed, MODEL_LIMIT, &count) ||
        count != version->count) {
        return false;
    }
    if (count != 0) {
        (void)memcpy(expected, version->entries, count * sizeof(*expected));
        qsort(expected, count, sizeof(*expected), compare_entries);
        if (memcmp(expected, observed, count * sizeof(*expected)) != 0) {
            return false;
        }
    }
    if (ft_monotone_action_heap_try_get_minimum_ref(&version->heap, &found, &minimum) !=
        FT_STATUS_OK) {
        return false;
    }
    if (count == 0) {
        return !found && ft_monotone_action_heap_empty(&version->heap);
    }
    if (!found || ft_monotone_action_heap_empty(&version->heap)) {
        return false;
    }
    {
        int smallest = version->entries[0].priority;
        for (size_t index = 1; index != count; ++index) {
            if (version->entries[index].priority < smallest) {
                smallest = version->entries[index].priority;
            }
        }
        return *(const int*)minimum.priority == smallest;
    }
}

static void model_remove_one(model_version* version, int element, int priority)
{
    for (size_t index = 0; index != version->count; ++index) {
        if (version->entries[index].element == element &&
            version->entries[index].priority == priority) {
            version->entries[index] = version->entries[version->count - 1];
            --version->count;
            return;
        }
    }
    version->count = MODEL_LIMIT + 1;
}

static model_action random_action(rng* generator)
{
    model_action action;
    const int first = rng_between(generator, -100, 100);
    const int second = rng_between(generator, -100, 100);
    action.kind = MODEL_IDENTITY;
    action.lower = first;
    action.upper = first;
    switch (rng_below(generator, 6)) {
    case 0:
        action.kind = MODEL_IDENTITY;
        break;
    case 1:
        action.kind = MODEL_AT_LEAST;
        break;
    case 2:
        action.kind = MODEL_AT_MOST;
        action.upper = first;
        break;
    case 3:
        action.kind = MODEL_BETWEEN;
        action.lower = first < second ? first : second;
        action.upper = first < second ? second : first;
        break;
    case 4:
        action.kind = MODEL_BETWEEN;
        action.lower = first;
        action.upper = first;
        break;
    default:
        action.kind = MODEL_CONSTANT;
        break;
    }
    return action;
}

static void test_randomized_retained_history(void)
{
    enum { steps = 1200 };
    fixture subject;
    rng generator;
    model_version* versions = (model_version*)malloc(VERSION_LIMIT * sizeof(*versions));
    size_t version_count = 0;
    int next_payload = 0;
    size_t inserts = 0;
    size_t deletes = 0;
    size_t melds = 0;
    size_t transforms = 0;
    REQUIRE(versions != NULL);
    generator.state = 0x5A172026u;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_heap_init(&versions[0].heap, &subject.policy), FT_STATUS_OK);
    versions[0].count = 0;
    versions[0].root = ft_monotone_action_heap_root_identity(&versions[0].heap);
    version_count = 1;

    for (int step = 0; step != steps && version_count != VERSION_LIMIT; ++step) {
        const model_version* source = &versions[rng_below(&generator, version_count)];
        model_version* result = &versions[version_count];
        const size_t operation = rng_below(&generator, 100);
        const void* source_root = source->root;
        result->count = 0;
        if (source->count == 0 || (source->count < MODEL_LIMIT / 2 && operation < 30)) {
            const int priority = rng_between(&generator, -100, 100);
            const int element = next_payload++;
            REQUIRE_STATUS(
                insert_entry(&source->heap, element, priority, &result->heap), FT_STATUS_OK);
            (void)memcpy(
                result->entries, source->entries, source->count * sizeof(*result->entries));
            result->count = source->count;
            result->entries[result->count].element = element;
            result->entries[result->count].priority = priority;
            ++result->count;
            ++inserts;
        } else if (operation < 50) {
            bool removed = false;
            int element = 0;
            int priority = 0;
            REQUIRE_STATUS(
                ft_monotone_action_heap_try_delete_minimum(
                    &source->heap, &removed, &element, &priority, &result->heap),
                FT_STATUS_OK);
            REQUIRE(removed);
            (void)memcpy(
                result->entries, source->entries, source->count * sizeof(*result->entries));
            result->count = source->count;
            model_remove_one(result, element, priority);
            REQUIRE(result->count == source->count - 1);
            ++deletes;
        } else if (operation < 75) {
            const model_action action = random_action(&generator);
            REQUIRE_STATUS(
                transform_by_model(&subject, &source->heap, &action, &result->heap),
                FT_STATUS_OK);
            result->count = source->count;
            for (size_t index = 0; index != source->count; ++index) {
                result->entries[index].element = source->entries[index].element;
                result->entries[index].priority =
                    model_apply(&action, source->entries[index].priority);
            }
            ++transforms;
        } else {
            const model_version* partner = NULL;
            for (int attempt = 0; attempt != 24; ++attempt) {
                const model_version* candidate =
                    &versions[rng_below(&generator, version_count)];
                if (source->count + candidate->count <= MODEL_LIMIT) {
                    partner = candidate;
                    break;
                }
            }
            if (partner == NULL) {
                const model_action action = random_action(&generator);
                REQUIRE_STATUS(
                    transform_by_model(&subject, &source->heap, &action, &result->heap),
                    FT_STATUS_OK);
                result->count = source->count;
                for (size_t index = 0; index != source->count; ++index) {
                    result->entries[index].element = source->entries[index].element;
                    result->entries[index].priority =
                        model_apply(&action, source->entries[index].priority);
                }
                ++transforms;
            } else {
                const void* partner_root = partner->root;
                REQUIRE_STATUS(
                    ft_monotone_action_heap_meld(&source->heap, &partner->heap, &result->heap),
                    FT_STATUS_OK);
                (void)memcpy(
                    result->entries, source->entries, source->count * sizeof(*result->entries));
                (void)memcpy(
                    result->entries + source->count,
                    partner->entries,
                    partner->count * sizeof(*result->entries));
                result->count = source->count + partner->count;
                REQUIRE(
                    ft_monotone_action_heap_root_identity(&partner->heap) == partner_root);
                ++melds;
            }
        }
        REQUIRE(ft_monotone_action_heap_root_identity(&source->heap) == source_root);
        result->root = ft_monotone_action_heap_root_identity(&result->heap);
        ++version_count;
        if ((step & 63) == 0) {
            REQUIRE(version_matches_model(result));
        }
    }

    REQUIRE(inserts > 150);
    REQUIRE(deletes > 75);
    REQUIRE(melds > 50);
    REQUIRE(transforms > 150);
    for (size_t index = 0; index < version_count; index += 37) {
        REQUIRE(version_matches_model(&versions[index]));
    }
    REQUIRE(version_matches_model(&versions[version_count - 1]));
    for (size_t index = 0; index != version_count; ++index) {
        ft_monotone_action_heap_dispose(&versions[index].heap);
    }
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
    free(versions);
}

/* ---------------------------------------------------------------------------------------------
 * Transform-all cost witness
 * ------------------------------------------------------------------------------------------- */

static void test_transform_all_has_constant_cost(void)
{
    enum { size_count = 3 };
    const int sizes[size_count] = {2, 1024, 16384};
    fixture subject;
    ft_monotone_action_heap heaps[size_count];
    ft_monotone_action_heap singleton;
    ft_monotone_action_heap empty;
    ft_monotone_action_heap result;
    ft_monotone_action_clamp action;
    ft_monotone_action_clamp identity;
    size_t allocations[size_count];
    size_t retained[size_count];
    size_t comparisons[size_count];
    int bound = 17;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&subject.family, &bound, &action), FT_STATUS_OK);
    ft_monotone_action_clamp_init_identity(&identity);
    for (int index = 0; index != size_count; ++index) {
        REQUIRE_STATUS(
            ft_monotone_action_heap_init(&heaps[index], &subject.policy), FT_STATUS_OK);
        for (int entry = 0; entry != sizes[index]; ++entry) {
            REQUIRE_STATUS(
                insert_entry(&heaps[index], entry, entry, &heaps[index]), FT_STATUS_OK);
        }
    }

    for (int index = 0; index != size_count; ++index) {
        size_t outstanding_before = 0;
        fixture_reset_counts(&subject);
        outstanding_before = subject.context.outstanding_allocations;
        REQUIRE_STATUS(
            ft_monotone_action_heap_transform_all(&heaps[index], &action, &result),
            FT_STATUS_OK);
        allocations[index] = subject.context.allocation_calls;
        retained[index] = subject.context.outstanding_allocations - outstanding_before;
        /* One identity test on the request, one inside the root tag, one inside the exposure and
         * one inside the forest tag; one composition each for the tag and the forest tag; one
         * application for the root priority. */
        REQUIRE(subject.algebra.identity_tests == 4);
        REQUIRE(subject.algebra.compositions == 2);
        REQUIRE(subject.algebra.applications == 1);
        /* The only comparisons are the ones the clamp itself performs inside that one
         * application; heap order is never re-established. */
        comparisons[index] = subject.context.compare_calls;
        REQUIRE(ft_monotone_action_heap_size(&result) ==
            ft_monotone_action_heap_size(&heaps[index]));
        REQUIRE(heap_valid(&result));
        ft_monotone_action_heap_dispose(&result);
        REQUIRE(allocations[index] == allocations[0]);
        REQUIRE(retained[index] == retained[0]);
        REQUIRE(comparisons[index] == comparisons[0]);
        REQUIRE(comparisons[index] <= 2);
    }

    /* A singleton has no child forest, so it skips exactly the forest tag. */
    REQUIRE_STATUS(ft_monotone_action_heap_init(&singleton, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&singleton, 1, 1, &singleton), FT_STATUS_OK);
    fixture_reset_counts(&subject);
    REQUIRE_STATUS(
        ft_monotone_action_heap_transform_all(&singleton, &action, &result), FT_STATUS_OK);
    REQUIRE(subject.algebra.identity_tests == 3);
    REQUIRE(subject.algebra.compositions == 1);
    REQUIRE(subject.algebra.applications == 1);
    ft_monotone_action_heap_dispose(&result);

    /* An identity action shares the receiver's root and costs one identity test. */
    fixture_reset_counts(&subject);
    REQUIRE_STATUS(
        ft_monotone_action_heap_transform_all(&heaps[1], &identity, &result), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_root_identity(&result) ==
        ft_monotone_action_heap_root_identity(&heaps[1]));
    REQUIRE(subject.algebra.identity_tests == 1);
    REQUIRE(subject.algebra.compositions == 0);
    REQUIRE(subject.algebra.applications == 0);
    REQUIRE(subject.context.allocation_calls == 0);
    ft_monotone_action_heap_dispose(&result);

    /* An empty heap makes no policy call at all. */
    REQUIRE_STATUS(ft_monotone_action_heap_init(&empty, &subject.policy), FT_STATUS_OK);
    fixture_reset_counts(&subject);
    REQUIRE_STATUS(
        ft_monotone_action_heap_transform_all(&empty, &action, &result), FT_STATUS_OK);
    REQUIRE(subject.algebra.identity_tests == 0);
    REQUIRE(subject.algebra.compositions == 0);
    REQUIRE(subject.algebra.applications == 0);
    REQUIRE(subject.context.allocation_calls == 0);
    REQUIRE(ft_monotone_action_heap_empty(&result));
    ft_monotone_action_heap_dispose(&result);

    ft_monotone_action_heap_dispose(&empty);
    ft_monotone_action_heap_dispose(&singleton);
    for (int index = 0; index != size_count; ++index) {
        ft_monotone_action_heap_dispose(&heaps[index]);
    }
    ft_monotone_action_clamp_dispose(&subject.family, &action);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
}

/* ---------------------------------------------------------------------------------------------
 * Boundaries and payload preservation
 * ------------------------------------------------------------------------------------------- */

static void test_empty_singleton_and_boundaries(void)
{
    fixture subject;
    ft_monotone_action_heap empty;
    ft_monotone_action_heap result;
    ft_monotone_action_heap singleton;
    ft_monotone_action_heap removed_heap;
    ft_monotone_action_heap extremes;
    ft_monotone_action_heap bounded;
    ft_monotone_action_heap self_melded;
    ft_monotone_action_heap_statistics statistics;
    ft_monotone_action_entry_ref minimum;
    model_action floor_three = {MODEL_AT_LEAST, 3, 0};
    model_action window = {MODEL_BETWEEN, -10, 10};
    const entry_pair bounded_entries[2] = {{1, -10}, {2, 10}};
    const void* extremes_root = NULL;
    bool valid = false;
    bool found = false;
    bool removed = false;
    int element = 0;
    int priority = 0;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&empty, &subject.policy), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_empty(&empty));
    REQUIRE(ft_monotone_action_heap_size(&empty) == 0);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_ref(&empty, &found, &minimum), FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_copy(&empty, &found, &element, &priority),
        FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE_STATUS(
        ft_monotone_action_heap_delete_minimum(&empty, &result), FT_STATUS_EMPTY);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_delete_minimum(
            &empty, &removed, &element, &priority, &result),
        FT_STATUS_OK);
    REQUIRE(!removed && ft_monotone_action_heap_empty(&result));
    ft_monotone_action_heap_dispose(&result);
    REQUIRE_STATUS(transform_by_model(&subject, &empty, &floor_three, &result), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_empty(&result));
    ft_monotone_action_heap_dispose(&result);
    REQUIRE_STATUS(ft_monotone_action_heap_meld(&empty, &empty, &result), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_empty(&result));
    ft_monotone_action_heap_dispose(&result);
    REQUIRE_STATUS(
        ft_monotone_action_heap_validate(&empty, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == 0 && statistics.root_forest_length == 0 &&
        statistics.maximum_rank == 0 && statistics.maximum_depth == 0 &&
        statistics.tagged_component_count == 0);

    REQUIRE_STATUS(insert_entry(&empty, 7, INT_MAX, &singleton), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_ref(&singleton, &found, &minimum),
        FT_STATUS_OK);
    REQUIRE(found && *(const int*)minimum.element == 7 &&
        *(const int*)minimum.priority == INT_MAX);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_delete_minimum(
            &singleton, &removed, &element, &priority, &removed_heap),
        FT_STATUS_OK);
    REQUIRE(removed && element == 7 && priority == INT_MAX);
    REQUIRE(ft_monotone_action_heap_empty(&removed_heap));
    REQUIRE(ft_monotone_action_heap_size(&singleton) == 1);
    REQUIRE(heap_valid(&singleton));
    REQUIRE(heap_valid(&removed_heap));

    REQUIRE_STATUS(ft_monotone_action_heap_init(&extremes, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&extremes, 1, INT_MIN, &extremes), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&extremes, 2, INT_MAX, &extremes), FT_STATUS_OK);
    extremes_root = ft_monotone_action_heap_root_identity(&extremes);
    REQUIRE_STATUS(transform_by_model(&subject, &extremes, &window, &bounded), FT_STATUS_OK);
    REQUIRE(entries_match(&bounded, bounded_entries, 2));
    REQUIRE_STATUS(ft_monotone_action_heap_meld(&extremes, &empty, &result), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_root_identity(&result) == extremes_root);
    ft_monotone_action_heap_dispose(&result);
    REQUIRE_STATUS(ft_monotone_action_heap_meld(&empty, &extremes, &result), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_root_identity(&result) == extremes_root);
    ft_monotone_action_heap_dispose(&result);

    /* Self-meld keeps both logical occurrences. */
    REQUIRE_STATUS(
        ft_monotone_action_heap_meld(&extremes, &extremes, &self_melded), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_size(&self_melded) == 4);
    REQUIRE(heap_valid(&self_melded));
    REQUIRE(heap_valid(&extremes));
    REQUIRE(heap_valid(&bounded));

    ft_monotone_action_heap_dispose(&self_melded);
    ft_monotone_action_heap_dispose(&bounded);
    ft_monotone_action_heap_dispose(&extremes);
    ft_monotone_action_heap_dispose(&removed_heap);
    ft_monotone_action_heap_dispose(&singleton);
    ft_monotone_action_heap_dispose(&empty);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
}

static void test_collapsed_priorities_preserve_every_payload(void)
{
    enum { count = 2048 };
    fixture subject;
    ft_monotone_action_heap source;
    ft_monotone_action_heap collapsed;
    model_action constant_seven = {MODEL_CONSTANT, 7, 7};
    entry_pair* drained = (entry_pair*)malloc((size_t)count * sizeof(*drained));
    size_t drained_count = 0;
    bool distinct = true;
    REQUIRE(drained != NULL);
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&source, &subject.policy), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        REQUIRE_STATUS(
            insert_entry(&source, index, (index % 257) - 128, &source), FT_STATUS_OK);
    }
    REQUIRE_STATUS(
        transform_by_model(&subject, &source, &constant_seven, &collapsed), FT_STATUS_OK);
    REQUIRE(drain_sorted(&collapsed, drained, (size_t)count, &drained_count));
    REQUIRE(drained_count == (size_t)count);
    qsort(drained, drained_count, sizeof(*drained), compare_entries);
    for (size_t index = 0; index != drained_count; ++index) {
        REQUIRE(drained[index].priority == 7);
        REQUIRE(drained[index].element == (int)index);
    }
    REQUIRE(ft_monotone_action_heap_size(&source) == (size_t)count);
    {
        entry_pair* observed = (entry_pair*)malloc((size_t)count * sizeof(*observed));
        size_t observed_count = 0;
        REQUIRE(observed != NULL);
        REQUIRE(ordered_entries(&source, observed, (size_t)count, &observed_count));
        distinct = false;
        for (size_t index = 0; index != observed_count; ++index) {
            if (observed[index].priority != 7) {
                distinct = true;
            }
        }
        free(observed);
    }
    REQUIRE(distinct);
    REQUIRE(heap_valid(&source));
    REQUIRE(heap_valid(&collapsed));
    ft_monotone_action_heap_dispose(&collapsed);
    ft_monotone_action_heap_dispose(&source);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
    free(drained);
}

/* ---------------------------------------------------------------------------------------------
 * Adversarially tagged shapes
 * ------------------------------------------------------------------------------------------- */

static void test_validate_accepts_deeply_tagged_shapes(void)
{
    enum { count = 2048 };
    fixture subject;
    ft_monotone_action_heap left;
    ft_monotone_action_heap right;
    ft_monotone_action_heap heap;
    ft_monotone_action_heap_statistics statistics;
    ft_monotone_action_heap_statistics left_statistics;
    ft_monotone_action_heap_statistics right_statistics;
    model_action left_floor = {MODEL_AT_LEAST, -20, 0};
    model_action left_cap = {MODEL_AT_MOST, 0, 30};
    model_action right_cap = {MODEL_AT_MOST, 0, 25};
    model_action right_floor = {MODEL_AT_LEAST, -35, 0};
    model_action window = {MODEL_BETWEEN, -12, 18};
    model_action final_window = {MODEL_BETWEEN, -9, 11};
    entry_pair* observed = NULL;
    size_t observed_count = 0;
    bool valid = false;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&left, &subject.policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&right, &subject.policy), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        REQUIRE_STATUS(
            insert_entry(&left, index, (index % 137) - 68, &left), FT_STATUS_OK);
        REQUIRE_STATUS(
            insert_entry(&right, index + count, 90 - (index % 181), &right), FT_STATUS_OK);
    }
    REQUIRE_STATUS(transform_by_model(&subject, &left, &left_floor, &left), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &left, &left_cap, &left), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &right, &right_cap, &right), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &right, &right_floor, &right), FT_STATUS_OK);
    REQUIRE_STATUS(ft_monotone_action_heap_meld(&left, &right, &heap), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &heap, &window, &heap), FT_STATUS_OK);
    for (int index = 0; index != 96; ++index) {
        REQUIRE_STATUS(ft_monotone_action_heap_delete_minimum(&heap, &heap), FT_STATUS_OK);
        if ((index & 7) == 0) {
            model_action step = {MODEL_AT_LEAST, 0, 0};
            step.lower = -10 + (index / 8);
            REQUIRE_STATUS(transform_by_model(&subject, &heap, &step, &heap), FT_STATUS_OK);
        }
    }
    REQUIRE_STATUS(insert_entry(&heap, -1, -100, &heap), FT_STATUS_OK);
    REQUIRE_STATUS(transform_by_model(&subject, &heap, &final_window, &heap), FT_STATUS_OK);

    REQUIRE_STATUS(
        ft_monotone_action_heap_validate(&heap, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == ft_monotone_action_heap_size(&heap));
    REQUIRE(statistics.count == 4001);
    REQUIRE(statistics.root_forest_length > 0);
    REQUIRE(statistics.maximum_rank > 0);
    REQUIRE(statistics.maximum_depth > 1);
    REQUIRE(statistics.tagged_component_count > 0);
    observed = (entry_pair*)malloc(statistics.count * sizeof(*observed));
    REQUIRE(observed != NULL);
    REQUIRE(ordered_entries(&heap, observed, statistics.count, &observed_count));
    REQUIRE(observed_count == statistics.count);
    for (size_t index = 0; index != observed_count; ++index) {
        REQUIRE(observed[index].priority >= -9 && observed[index].priority <= 11);
    }
    free(observed);

    REQUIRE_STATUS(
        ft_monotone_action_heap_validate(&left, &valid, &left_statistics), FT_STATUS_OK);
    REQUIRE(valid && left_statistics.count == (size_t)count);
    REQUIRE_STATUS(
        ft_monotone_action_heap_validate(&right, &valid, &right_statistics), FT_STATUS_OK);
    REQUIRE(valid && right_statistics.count == (size_t)count);

    ft_monotone_action_heap_dispose(&heap);
    ft_monotone_action_heap_dispose(&right);
    ft_monotone_action_heap_dispose(&left);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
}

/* ---------------------------------------------------------------------------------------------
 * Failure atomicity
 * ------------------------------------------------------------------------------------------- */

/* Runs one fallible update whose successor is published into `result`, releasing that successor
 * when the injected failure happened to land past the operation's last fallible step. */
#define ATTEMPT(expression) \
    do { \
        if ((expression) == FT_STATUS_OK) { \
            ft_monotone_action_heap_dispose(&result); \
        } \
    } while (0)

static void test_failure_atomicity_and_lifetimes(void)
{
    fixture subject;
    fixture other;
    ft_monotone_action_heap heap;
    ft_monotone_action_heap foreign;
    ft_monotone_action_heap result;
    ft_monotone_action_clamp action;
    ft_monotone_action_policy policy_copy;
    const entry_pair expected[4] = {{1, 5}, {2, -1}, {3, 12}, {4, 3}};
    model_action floor_four = {MODEL_AT_LEAST, 4, 0};
    const void* root = NULL;
    int bound = 4;
    bool removed = false;
    int element = 0;
    int priority = 0;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    REQUIRE_STATUS(fixture_init(&other), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_policy_copy(&subject.policy, &policy_copy), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_policy_same(&subject.policy, &policy_copy));
    REQUIRE(!ft_monotone_action_policy_same(&subject.policy, &other.policy));
    REQUIRE_STATUS(ft_monotone_action_heap_init(&heap, &policy_copy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&heap, 1, 5, &heap), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&heap, 2, -1, &heap), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&heap, 3, 12, &heap), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&heap, 4, 3, &heap), FT_STATUS_OK);
    ft_monotone_action_policy_dispose(&policy_copy);
    root = ft_monotone_action_heap_root_identity(&heap);
    REQUIRE_STATUS(ft_monotone_action_heap_init(&foreign, &other.policy), FT_STATUS_OK);
    REQUIRE_STATUS(insert_entry(&foreign, 9, 0, &foreign), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_monotone_action_clamp_at_least(&subject.family, &bound, &action), FT_STATUS_OK);

    /* A policy mismatch is rejected and publishes nothing. */
    REQUIRE_STATUS(
        ft_monotone_action_heap_meld(&heap, &foreign, &result),
        FT_STATUS_INCOMPATIBLE_POLICY);
    REQUIRE(ft_monotone_action_heap_root_identity(&heap) == root);
    REQUIRE(entries_match(&heap, expected, 4));

    /* A failing comparer aborts insert, meld, and delete-minimum without publishing. */
    for (size_t failure = 1; failure != 6; ++failure) {
        subject.context.fail_compare_at = failure;
        subject.context.compare_calls = 0;
        ATTEMPT(insert_entry(&heap, 5, 2, &result));
        subject.context.compare_calls = 0;
        ATTEMPT(ft_monotone_action_heap_meld(&heap, &heap, &result));
        subject.context.compare_calls = 0;
        ATTEMPT(ft_monotone_action_heap_delete_minimum(&heap, &result));
        subject.context.compare_calls = 0;
        ATTEMPT(ft_monotone_action_heap_try_delete_minimum(
            &heap, &removed, &element, &priority, &result));
        subject.context.fail_compare_at = 0;
        subject.context.compare_calls = 0;
        REQUIRE(ft_monotone_action_heap_root_identity(&heap) == root);
        REQUIRE(entries_match(&heap, expected, 4));
        REQUIRE(heap_valid(&heap));
    }

    /* A failing composition aborts transform-all without publishing. */
    subject.algebra.fail_compose_at = 1;
    subject.algebra.compositions = 0;
    REQUIRE_STATUS(
        ft_monotone_action_heap_transform_all(&heap, &action, &result),
        FT_STATUS_CALLBACK_FAILURE);
    subject.algebra.fail_compose_at = 0;
    REQUIRE(ft_monotone_action_heap_root_identity(&heap) == root);
    REQUIRE(entries_match(&heap, expected, 4));
    REQUIRE(heap_valid(&heap));

    /* A failing value copy aborts insertion without publishing. */
    for (size_t failure = 1; failure != 3; ++failure) {
        subject.context.copy_calls = 0;
        subject.context.fail_copy_at = failure;
        REQUIRE_STATUS(insert_entry(&heap, 6, 6, &result), FT_STATUS_CALLBACK_FAILURE);
        subject.context.fail_copy_at = 0;
        REQUIRE(ft_monotone_action_heap_root_identity(&heap) == root);
        REQUIRE(entries_match(&heap, expected, 4));
    }

    /* Every allocation failure point leaves the source untouched and leaks nothing. */
    for (size_t failure = 1; failure != 40; ++failure) {
        const size_t outstanding = subject.context.outstanding_allocations;
        ft_monotone_action_heap_statistics ignored;
        subject.context.allocation_calls = 0;
        subject.context.fail_allocation_at = failure;
        ATTEMPT(insert_entry(&heap, 7, -7, &result));
        subject.context.allocation_calls = 0;
        ATTEMPT(ft_monotone_action_heap_transform_all(&heap, &action, &result));
        subject.context.allocation_calls = 0;
        ATTEMPT(ft_monotone_action_heap_meld(&heap, &heap, &result));
        subject.context.allocation_calls = 0;
        ATTEMPT(ft_monotone_action_heap_delete_minimum(&heap, &result));
        subject.context.allocation_calls = 0;
        (void)ft_monotone_action_heap_validate(&heap, &removed, &ignored);
        subject.context.fail_allocation_at = 0;
        subject.context.allocation_calls = 0;
        REQUIRE(subject.context.outstanding_allocations == outstanding);
        REQUIRE(ft_monotone_action_heap_root_identity(&heap) == root);
        REQUIRE(entries_match(&heap, expected, 4));
        REQUIRE(heap_valid(&heap));
    }

    /* The same sweep over a ranked heap, where delete-minimum walks the fused decomposition,
     * the ranked meld buckets and the zero reinsertion pass. */
    {
        ft_monotone_action_heap ranked;
        entry_pair ranked_entries[64];
        const void* ranked_root = NULL;
        REQUIRE_STATUS(ft_monotone_action_heap_init(&ranked, &subject.policy), FT_STATUS_OK);
        for (int index = 0; index != 64; ++index) {
            const int value = ((index * 29) % 61) - 30;
            REQUIRE_STATUS(insert_entry(&ranked, index, value, &ranked), FT_STATUS_OK);
            ranked_entries[index].element = index;
            ranked_entries[index].priority = value;
        }
        REQUIRE_STATUS(transform_by_model(&subject, &ranked, &floor_four, &ranked), FT_STATUS_OK);
        for (int index = 0; index != 64; ++index) {
            ranked_entries[index].priority =
                model_apply(&floor_four, ranked_entries[index].priority);
        }
        ranked_root = ft_monotone_action_heap_root_identity(&ranked);
        for (size_t failure = 1; failure != 140; ++failure) {
            const size_t outstanding = subject.context.outstanding_allocations;
            subject.context.allocation_calls = 0;
            subject.context.fail_allocation_at = failure;
            ATTEMPT(ft_monotone_action_heap_delete_minimum(&ranked, &result));
            subject.context.allocation_calls = 0;
            ATTEMPT(ft_monotone_action_heap_meld(&ranked, &ranked, &result));
            subject.context.fail_allocation_at = 0;
            subject.context.allocation_calls = 0;
            subject.context.compare_calls = 0;
            subject.context.fail_compare_at = failure;
            ATTEMPT(ft_monotone_action_heap_delete_minimum(&ranked, &result));
            subject.context.fail_compare_at = 0;
            subject.context.compare_calls = 0;
            REQUIRE(subject.context.outstanding_allocations == outstanding);
            REQUIRE(ft_monotone_action_heap_root_identity(&ranked) == ranked_root);
            REQUIRE(entries_match(&ranked, ranked_entries, 64));
            REQUIRE(heap_valid(&ranked));
        }
        ft_monotone_action_heap_dispose(&ranked);
    }

    /* Invalid arguments are rejected before anything is touched. */
    REQUIRE_STATUS(
        ft_monotone_action_heap_insert(&heap, NULL, &bound, &result),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_monotone_action_heap_transform_all(&heap, NULL, &result),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_monotone_action_heap_visit(&heap, NULL, NULL), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_monotone_action_heap_root_identity(&heap) == root);

    ft_monotone_action_clamp_dispose(&subject.family, &action);
    ft_monotone_action_heap_dispose(&foreign);
    ft_monotone_action_heap_dispose(&heap);
    fixture_dispose(&other);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
    REQUIRE(other.context.outstanding_allocations == 0);
}

/* ---------------------------------------------------------------------------------------------
 * Handle lifecycle
 * ------------------------------------------------------------------------------------------- */

static void test_handle_lifecycle(void)
{
    fixture subject;
    ft_monotone_action_heap heap;
    ft_monotone_action_heap shared;
    ft_monotone_action_heap moved;
    ft_monotone_action_clamp_family family_copy;
    ft_monotone_action_clamp_family family_moved;
    ft_monotone_action_policy policy_copy;
    ft_monotone_action_policy policy_moved;
    ft_monotone_action_entry_ref minimum;
    ft_monotone_action_entry_input entries[4];
    int elements[4] = {1, 2, 3, 4};
    int priorities[4] = {40, 10, 30, 20};
    const entry_pair expected[4] = {{1, 40}, {2, 10}, {3, 30}, {4, 20}};
    const void* root = NULL;
    int element = 0;
    int priority = 0;
    bool found = false;
    REQUIRE_STATUS(fixture_init(&subject), FT_STATUS_OK);
    for (int index = 0; index != 4; ++index) {
        entries[index].element = &elements[index];
        entries[index].priority = &priorities[index];
    }
    REQUIRE_STATUS(
        ft_monotone_action_heap_from_array(&heap, &subject.policy, entries, 4), FT_STATUS_OK);
    REQUIRE(entries_match(&heap, expected, 4));
    REQUIRE(heap_valid(&heap));
    root = ft_monotone_action_heap_root_identity(&heap);
    REQUIRE_STATUS(ft_monotone_action_heap_copy(&heap, &shared), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_heap_root_identity(&shared) == root);
    ft_monotone_action_heap_move(&moved, &shared);
    REQUIRE(ft_monotone_action_heap_root_identity(&moved) == root);
    REQUIRE(ft_monotone_action_heap_root_identity(&shared) == NULL);
    ft_monotone_action_heap_dispose(&moved);
    REQUIRE(entries_match(&heap, expected, 4));

    /* The borrowed minimum costs nothing, and the owned pair reads the same representatives. */
    fixture_reset_counts(&subject);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_ref(&heap, &found, &minimum), FT_STATUS_OK);
    REQUIRE(found && *(const int*)minimum.element == 2 && *(const int*)minimum.priority == 10);
    REQUIRE(subject.context.compare_calls == 0);
    REQUIRE(subject.context.allocation_calls == 0);
    REQUIRE_STATUS(
        ft_monotone_action_heap_try_get_minimum_copy(&heap, &found, &element, &priority),
        FT_STATUS_OK);
    REQUIRE(found && element == 2 && priority == 10);
    REQUIRE(subject.context.copy_calls == 2);
    REQUIRE(subject.context.allocation_calls == 0);

    REQUIRE_STATUS(
        ft_monotone_action_clamp_family_copy(&subject.family, &family_copy), FT_STATUS_OK);
    REQUIRE(ft_monotone_action_clamp_family_same(&subject.family, &family_copy));
    ft_monotone_action_clamp_family_move(&family_moved, &family_copy);
    REQUIRE(ft_monotone_action_clamp_family_same(&subject.family, &family_moved));
    REQUIRE(!ft_monotone_action_clamp_family_same(&subject.family, &family_copy));
    ft_monotone_action_clamp_family_dispose(&family_moved);

    REQUIRE_STATUS(
        ft_monotone_action_policy_copy(&subject.policy, &policy_copy), FT_STATUS_OK);
    ft_monotone_action_policy_move(&policy_moved, &policy_copy);
    REQUIRE(ft_monotone_action_policy_same(&subject.policy, &policy_moved));
    REQUIRE(!ft_monotone_action_policy_same(&subject.policy, &policy_copy));
    ft_monotone_action_policy_dispose(&policy_moved);

    ft_monotone_action_heap_dispose(&heap);
    fixture_dispose(&subject);
    REQUIRE(subject.context.outstanding_allocations == 0);
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
    run_test("Monotone-action clamp algebra", test_clamp_algebra);
    run_test(
        "Monotone-action clamp exact representatives",
        test_clamp_preserves_exact_representatives);
    run_test(
        "Monotone-action temporal insertion",
        test_temporal_insert_after_noninvertible_transform);
    run_test("Monotone-action independent melds", test_meld_does_not_cross_apply_actions);
    run_test("Monotone-action persistent branches", test_updates_are_persistent_across_branches);
    run_test("Monotone-action randomized retained history", test_randomized_retained_history);
    run_test("Monotone-action transform-all cost", test_transform_all_has_constant_cost);
    run_test("Monotone-action boundaries", test_empty_singleton_and_boundaries);
    run_test(
        "Monotone-action collapsed priorities",
        test_collapsed_priorities_preserve_every_payload);
    run_test("Monotone-action tagged shapes", test_validate_accepts_deeply_tagged_shapes);
    run_test("Monotone-action failure atomicity", test_failure_atomicity_and_lifetimes);
    run_test("Monotone-action handle lifecycle", test_handle_lifecycle);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C monotone-action heap tests passed\n");
    return EXIT_SUCCESS;
}
