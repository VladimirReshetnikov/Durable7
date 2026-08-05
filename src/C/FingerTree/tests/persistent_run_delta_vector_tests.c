/*
 * Tests for the persistent run-delta vector.
 */

#include <durable7/finger_tree/persistent_run_delta_vector.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static const unsigned char g_item_type_identity = 0;

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

/* The payload carries a representative tag the equality callback deliberately ignores, so a test
 * can tell "an equal value" from "the exact same representative". */
typedef struct test_item {
    int key;
    int representative;
} test_item;

typedef struct test_context {
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t destroy_calls;
    size_t fail_copy_at;
    size_t equal_calls;
    size_t fail_equal_at;
} test_context;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* const state = (test_context*)context;
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
    test_context* const state = (test_context*)context;
    if (allocation != NULL) {
        ++state->deallocation_calls;
        --state->outstanding_allocations;
        free(allocation);
    }
}

static ft_status tracked_copy(void* destination, const void* source, void* context)
{
    test_context* const state = (test_context*)context;
    ++state->copy_calls;
    if (state->fail_copy_at != 0 && state->copy_calls == state->fail_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_item*)destination = *(const test_item*)source;
    return FT_STATUS_OK;
}

static void tracked_destroy(void* value, void* context)
{
    test_context* const state = (test_context*)context;
    (void)value;
    ++state->destroy_calls;
}

static ft_status tracked_equal(const void* left, const void* right, bool* equal, void* context)
{
    test_context* const state = (test_context*)context;
    ++state->equal_calls;
    if (state->fail_equal_at != 0 && state->equal_calls == state->fail_equal_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *equal = ((const test_item*)left)->key == ((const test_item*)right)->key;
    return FT_STATUS_OK;
}

static void init_config(ft_run_delta_policy_config* config, test_context* context)
{
    ft_run_delta_policy_config_init(
        config,
        sizeof(test_item),
        &g_item_type_identity,
        tracked_equal,
        context);
    config->copy = tracked_copy;
    config->destroy = tracked_destroy;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static void reset_context(test_context* context)
{
    (void)memset(context, 0, sizeof(*context));
}

static test_item make_item(int key, int representative)
{
    test_item item;
    item.key = key;
    item.representative = representative;
    return item;
}

static bool same_item(test_item left, test_item right)
{
    return left.key == right.key && left.representative == right.representative;
}

static bool same_run(ft_run_delta_run left, ft_run_delta_run right)
{
    return left.start == right.start && left.length == right.length;
}

/* -------------------------------------------------------------------------------------------
 * Collectors and the independent model.
 * ------------------------------------------------------------------------------------------- */

typedef struct run_collector {
    ft_run_delta_run* runs;
    size_t capacity;
    size_t count;
    bool overflowed;
} run_collector;

static ft_status collect_run(const ft_run_delta_run* run, void* context)
{
    run_collector* const collector = (run_collector*)context;
    if (collector->count == collector->capacity) {
        collector->overflowed = true;
        return FT_STATUS_OVERFLOW;
    }
    collector->runs[collector->count++] = *run;
    return FT_STATUS_OK;
}

typedef struct value_collector {
    test_item* values;
    size_t capacity;
    size_t count;
    bool overflowed;
} value_collector;

static ft_status collect_value(const void* value, void* context)
{
    value_collector* const collector = (value_collector*)context;
    if (collector->count == collector->capacity) {
        collector->overflowed = true;
        return FT_STATUS_OVERFLOW;
    }
    collector->values[collector->count++] = *(const test_item*)value;
    return FT_STATUS_OK;
}

typedef struct stop_visitor_state {
    size_t seen;
    size_t stop_after;
} stop_visitor_state;

static ft_status stopping_value_visit(const void* value, void* context)
{
    stop_visitor_state* const state = (stop_visitor_state*)context;
    (void)value;
    ++state->seen;
    return state->seen == state->stop_after ? FT_STATUS_NOT_FOUND : FT_STATUS_OK;
}

static ft_status stopping_run_visit(const ft_run_delta_run* run, void* context)
{
    stop_visitor_state* const state = (stop_visitor_state*)context;
    (void)run;
    ++state->seen;
    return state->seen == state->stop_after ? FT_STATUS_NOT_FOUND : FT_STATUS_OK;
}

/* The runs the model says a version must publish: maximal, ordered, non-adjacent. */
static size_t expected_runs(
    const test_item* current,
    const test_item* checkpoint,
    size_t length,
    ft_run_delta_run* runs,
    size_t capacity)
{
    size_t count = 0;
    size_t index = 0;
    while (index != length) {
        size_t start = 0;
        if (current[index].key == checkpoint[index].key) {
            ++index;
            continue;
        }
        start = index;
        ++index;
        while (index != length && current[index].key != checkpoint[index].key) {
            ++index;
        }
        if (count == capacity) {
            return capacity + 1;
        }
        runs[count].start = start;
        runs[count].length = index - start;
        ++count;
    }
    return count;
}

typedef struct model_buffers {
    ft_run_delta_run expected[160];
    ft_run_delta_run actual[160];
    test_item values[160];
} model_buffers;

static void assert_matches_model(
    const ft_run_delta_vector* vector,
    const test_item* current,
    const test_item* checkpoint,
    size_t length,
    model_buffers* buffers)
{
    run_collector runs;
    value_collector values;
    ft_run_delta_statistics statistics;
    size_t expected_count = 0;
    size_t dirty_total = 0;
    size_t index = 0;
    size_t rank = 0;
    bool valid = false;

    REQUIRE(ft_run_delta_vector_size(vector) == length);
    values.values = buffers->values;
    values.capacity = sizeof(buffers->values) / sizeof(buffers->values[0]);
    values.count = 0;
    values.overflowed = false;
    REQUIRE_STATUS(ft_run_delta_vector_visit(vector, collect_value, &values), FT_STATUS_OK);
    REQUIRE(!values.overflowed);
    REQUIRE(values.count == length);
    for (index = 0; index != length; ++index) {
        REQUIRE(same_item(values.values[index], current[index]));
    }

    expected_count = expected_runs(
        current,
        checkpoint,
        length,
        buffers->expected,
        sizeof(buffers->expected) / sizeof(buffers->expected[0]));
    REQUIRE(expected_count <= sizeof(buffers->expected) / sizeof(buffers->expected[0]));

    runs.runs = buffers->actual;
    runs.capacity = sizeof(buffers->actual) / sizeof(buffers->actual[0]);
    runs.count = 0;
    runs.overflowed = false;
    REQUIRE_STATUS(
        ft_run_delta_vector_visit_dirty_runs(vector, collect_run, &runs),
        FT_STATUS_OK);
    REQUIRE(!runs.overflowed);
    REQUIRE(runs.count == expected_count);
    REQUIRE(ft_run_delta_vector_dirty_run_count(vector) == expected_count);
    for (rank = 0; rank != expected_count; ++rank) {
        ft_run_delta_run selected;
        REQUIRE(same_run(runs.runs[rank], buffers->expected[rank]));
        REQUIRE_STATUS(
            ft_run_delta_vector_dirty_run_at(vector, rank, &selected),
            FT_STATUS_OK);
        REQUIRE(same_run(selected, buffers->expected[rank]));
        dirty_total += buffers->expected[rank].length;
    }
    {
        ft_run_delta_run selected;
        REQUIRE_STATUS(
            ft_run_delta_vector_dirty_run_at(vector, expected_count, &selected),
            FT_STATUS_OUT_OF_RANGE);
    }
    REQUIRE(ft_run_delta_vector_dirty_count(vector) == dirty_total);
    REQUIRE(ft_run_delta_vector_has_changes(vector) == (dirty_total != 0));

    for (index = 0; index != length; ++index) {
        test_item observed;
        bool dirty = false;
        bool found = false;
        ft_run_delta_run containing;
        const ft_run_delta_run* expected_run = NULL;
        for (rank = 0; rank != expected_count; ++rank) {
            if (ft_run_delta_run_contains(&buffers->expected[rank], index)) {
                expected_run = &buffers->expected[rank];
                break;
            }
        }
        REQUIRE_STATUS(ft_run_delta_vector_at_copy(vector, index, &observed), FT_STATUS_OK);
        REQUIRE(same_item(observed, current[index]));
        REQUIRE_STATUS(
            ft_run_delta_vector_checkpoint_at_copy(vector, index, &observed),
            FT_STATUS_OK);
        REQUIRE(same_item(observed, checkpoint[index]));
        REQUIRE_STATUS(ft_run_delta_vector_is_dirty(vector, index, &dirty), FT_STATUS_OK);
        REQUIRE(dirty == (expected_run != NULL));
        REQUIRE_STATUS(
            ft_run_delta_vector_try_get_dirty_run_containing(vector, index, &found, &containing),
            FT_STATUS_OK);
        REQUIRE(found == (expected_run != NULL));
        if (expected_run != NULL) {
            REQUIRE(same_run(containing, *expected_run));
        }
    }

    REQUIRE_STATUS(ft_run_delta_vector_validate(vector, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == length);
    REQUIRE(statistics.dirty_count == dirty_total);
    REQUIRE(statistics.dirty_run_count == expected_count);
}

static void assert_runs(
    const ft_run_delta_vector* vector,
    const ft_run_delta_run* expected,
    size_t expected_count,
    model_buffers* buffers)
{
    run_collector runs;
    ft_run_delta_statistics statistics;
    size_t rank = 0;
    size_t dirty_total = 0;
    bool valid = false;

    runs.runs = buffers->actual;
    runs.capacity = sizeof(buffers->actual) / sizeof(buffers->actual[0]);
    runs.count = 0;
    runs.overflowed = false;
    REQUIRE_STATUS(
        ft_run_delta_vector_visit_dirty_runs(vector, collect_run, &runs),
        FT_STATUS_OK);
    REQUIRE(runs.count == expected_count);
    for (rank = 0; rank != expected_count; ++rank) {
        size_t index = 0;
        REQUIRE(same_run(runs.runs[rank], expected[rank]));
        dirty_total += expected[rank].length;
        for (index = expected[rank].start;
             index != expected[rank].start + expected[rank].length;
             ++index) {
            bool dirty = false;
            bool found = false;
            ft_run_delta_run containing;
            REQUIRE_STATUS(ft_run_delta_vector_is_dirty(vector, index, &dirty), FT_STATUS_OK);
            REQUIRE(dirty);
            REQUIRE_STATUS(
                ft_run_delta_vector_try_get_dirty_run_containing(
                    vector,
                    index,
                    &found,
                    &containing),
                FT_STATUS_OK);
            REQUIRE(found);
            REQUIRE(same_run(containing, expected[rank]));
        }
    }
    REQUIRE(ft_run_delta_vector_dirty_count(vector) == dirty_total);
    REQUIRE(ft_run_delta_vector_dirty_run_count(vector) == expected_count);
    REQUIRE_STATUS(ft_run_delta_vector_validate(vector, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
}

/* -------------------------------------------------------------------------------------------
 * Deterministic randomness.
 * ------------------------------------------------------------------------------------------- */

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

/* Applies one point edit in place, replacing the handle with its successor. */
static ft_status advance_set(ft_run_delta_vector* vector, size_t index, test_item value)
{
    return ft_run_delta_vector_set(vector, index, &value, vector);
}

/* -------------------------------------------------------------------------------------------
 * Tests.
 * ------------------------------------------------------------------------------------------- */

static void test_empty_and_boundary_contracts(void)
{
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_policy policy_copy;
    ft_run_delta_policy moved_policy;
    ft_run_delta_policy other_policy;
    ft_run_delta_vector empty;
    ft_run_delta_vector produced;
    ft_run_delta_vector alias;
    ft_run_delta_statistics statistics;
    ft_run_delta_run run;
    test_item observed;
    const test_item probe = { 1, 1 };
    bool valid = false;
    bool dirty = false;
    bool found = false;

    reset_context(&context);
    init_config(&config, &context);

    REQUIRE_STATUS(ft_run_delta_policy_create(NULL, &config), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, NULL), FT_STATUS_INVALID_ARGUMENT);
    {
        ft_run_delta_policy_config invalid = config;
        invalid.equal = NULL;
        REQUIRE_STATUS(
            ft_run_delta_policy_create(&policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
        invalid = config;
        invalid.value_size = 0;
        REQUIRE_STATUS(
            ft_run_delta_policy_create(&policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
        invalid = config;
        invalid.value_type_identity = NULL;
        REQUIRE_STATUS(
            ft_run_delta_policy_create(&policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
        invalid = config;
        invalid.copy = NULL;
        REQUIRE_STATUS(
            ft_run_delta_policy_create(&policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
    }

    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_run_delta_policy_copy(&policy, &policy_copy), FT_STATUS_OK);
    REQUIRE(ft_run_delta_policy_same(&policy, &policy_copy));
    ft_run_delta_policy_move(&moved_policy, &policy_copy);
    REQUIRE(policy_copy.rep == NULL);
    REQUIRE(ft_run_delta_policy_same(&policy, &moved_policy));
    ft_run_delta_policy_dispose(&moved_policy);
    REQUIRE_STATUS(ft_run_delta_policy_create(&other_policy, &config), FT_STATUS_OK);
    REQUIRE(!ft_run_delta_policy_same(&policy, &other_policy));
    ft_run_delta_policy_dispose(&other_policy);

    REQUIRE_STATUS(ft_run_delta_vector_init(&empty, &policy), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_empty(&empty));
    REQUIRE(ft_run_delta_vector_size(&empty) == 0);
    REQUIRE(!ft_run_delta_vector_has_changes(&empty));
    REQUIRE(ft_run_delta_vector_dirty_count(&empty) == 0);
    REQUIRE(ft_run_delta_vector_dirty_run_count(&empty) == 0);
    REQUIRE_STATUS(ft_run_delta_vector_at_copy(&empty, 0, &observed), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_checkpoint_at_copy(&empty, 0, &observed),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_run_delta_vector_is_dirty(&empty, 0, &dirty), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_try_get_dirty_run_containing(&empty, 0, &found, &run),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_run_delta_vector_dirty_run_at(&empty, 0, &run), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_set(&empty, 0, &probe, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_run_delta_vector_reset(&empty, 0, &produced), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_accept_dirty_run_at(&empty, 0, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_revert_dirty_run_at(&empty, 0, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_accept_dirty_run_containing(&empty, 0, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_revert_dirty_run_containing(&empty, 0, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_run_delta_vector_validate(&empty, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == 0);

    /* Whole-version operations on a clean version produce the receiver's contents. */
    REQUIRE_STATUS(ft_run_delta_vector_checkpoint(&empty, &produced), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_empty(&produced));
    ft_run_delta_vector_dispose(&produced);
    REQUIRE_STATUS(ft_run_delta_vector_rollback(&empty, &produced), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_empty(&produced));
    ft_run_delta_vector_dispose(&produced);

    /* An empty build shares the empty root and stays canonical. */
    REQUIRE_STATUS(ft_run_delta_vector_from_array(&produced, &policy, NULL, 0), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_shares_current_root(&produced, &empty));
    REQUIRE(ft_run_delta_vector_shares_checkpoint_root(&produced, &empty));
    ft_run_delta_vector_dispose(&produced);
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&produced, &policy, NULL, 3),
        FT_STATUS_INVALID_ARGUMENT);

    /* Copy takes a reference, an aliased result is supported, and move relocates. */
    REQUIRE_STATUS(ft_run_delta_vector_copy(&empty, &alias), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_shares_current_root(&alias, &empty));
    REQUIRE_STATUS(ft_run_delta_vector_checkpoint(&alias, &alias), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_empty(&alias));
    ft_run_delta_vector_move(&produced, &alias);
    REQUIRE(alias.policy == NULL);
    ft_run_delta_vector_dispose(&produced);

    /* Run descriptor arithmetic. */
    run.start = 3;
    run.length = 2;
    REQUIRE(ft_run_delta_run_end_exclusive(&run) == 5);
    REQUIRE(!ft_run_delta_run_contains(&run, 2));
    REQUIRE(ft_run_delta_run_contains(&run, 3));
    REQUIRE(ft_run_delta_run_contains(&run, 4));
    REQUIRE(!ft_run_delta_run_contains(&run, 5));

    ft_run_delta_vector_dispose(&empty);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_equality_no_ops_and_exact_representatives(void)
{
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector source;
    ft_run_delta_vector no_op;
    ft_run_delta_vector dirty;
    ft_run_delta_vector cancelled;
    ft_run_delta_vector reset;
    test_item initial[2];
    test_item observed;
    const void* current_ref = NULL;
    const void* checkpoint_ref = NULL;
    ft_run_delta_run run;
    bool found = false;

    reset_context(&context);
    init_config(&config, &context);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    initial[0] = make_item(1, 100);
    initial[1] = make_item(2, 200);
    REQUIRE_STATUS(ft_run_delta_vector_from_array(&source, &policy, initial, 2), FT_STATUS_OK);
    REQUIRE(
        ft_run_delta_vector_current_root_identity(&source) ==
        ft_run_delta_vector_checkpoint_root_identity(&source));

    /* An equality-equal write is a semantic no-op that keeps the current representative. */
    {
        const test_item replacement = make_item(1, 999);
        REQUIRE_STATUS(ft_run_delta_vector_set(&source, 0, &replacement, &no_op), FT_STATUS_OK);
    }
    REQUIRE(!ft_run_delta_vector_has_changes(&no_op));
    REQUIRE(ft_run_delta_vector_shares_current_root(&no_op, &source));
    REQUIRE_STATUS(ft_run_delta_vector_at_copy(&no_op, 0, &observed), FT_STATUS_OK);
    REQUIRE(same_item(observed, make_item(1, 100)));

    /* A genuine change becomes one singleton run. */
    {
        const test_item replacement = make_item(7, 700);
        REQUIRE_STATUS(ft_run_delta_vector_set(&source, 0, &replacement, &dirty), FT_STATUS_OK);
    }
    REQUIRE(ft_run_delta_vector_dirty_count(&dirty) == 1);
    REQUIRE(ft_run_delta_vector_dirty_run_count(&dirty) == 1);
    REQUIRE_STATUS(ft_run_delta_vector_dirty_run_at(&dirty, 0, &run), FT_STATUS_OK);
    REQUIRE(run.start == 0 && run.length == 1);
    REQUIRE_STATUS(ft_run_delta_vector_at_copy(&dirty, 0, &observed), FT_STATUS_OK);
    REQUIRE(same_item(observed, make_item(7, 700)));
    REQUIRE_STATUS(ft_run_delta_vector_checkpoint_at_copy(&dirty, 0, &observed), FT_STATUS_OK);
    REQUIRE(same_item(observed, make_item(1, 100)));
    REQUIRE(ft_run_delta_vector_shares_checkpoint_root(&dirty, &source));

    /* Returning to the checkpoint's equality class restores the EXACT checkpoint representative,
     * not the equal value that was written, and snaps the current root back. */
    {
        const test_item replacement = make_item(1, 555);
        REQUIRE_STATUS(
            ft_run_delta_vector_set(&dirty, 0, &replacement, &cancelled),
            FT_STATUS_OK);
    }
    REQUIRE(!ft_run_delta_vector_has_changes(&cancelled));
    REQUIRE(ft_run_delta_vector_dirty_run_count(&cancelled) == 0);
    REQUIRE_STATUS(ft_run_delta_vector_at_copy(&cancelled, 0, &observed), FT_STATUS_OK);
    REQUIRE(same_item(observed, make_item(1, 100)));
    REQUIRE(ft_run_delta_vector_shares_current_root(&cancelled, &source));
    REQUIRE_STATUS(ft_run_delta_vector_at_ref(&cancelled, 0, &current_ref), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_run_delta_vector_checkpoint_at_ref(&cancelled, 0, &checkpoint_ref),
        FT_STATUS_OK);
    REQUIRE(current_ref == checkpoint_ref);

    /* Reset restores the same exact representative, and is a no-op on a clean position. */
    REQUIRE_STATUS(ft_run_delta_vector_reset(&dirty, 0, &reset), FT_STATUS_OK);
    REQUIRE(!ft_run_delta_vector_has_changes(&reset));
    REQUIRE_STATUS(ft_run_delta_vector_at_ref(&reset, 0, &current_ref), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_run_delta_vector_checkpoint_at_ref(&reset, 0, &checkpoint_ref),
        FT_STATUS_OK);
    REQUIRE(current_ref == checkpoint_ref);
    ft_run_delta_vector_dispose(&reset);
    REQUIRE_STATUS(ft_run_delta_vector_reset(&cancelled, 1, &reset), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_shares_current_root(&reset, &cancelled));
    ft_run_delta_vector_dispose(&reset);

    /* The retained intermediate version is untouched. */
    REQUIRE_STATUS(ft_run_delta_vector_at_copy(&dirty, 0, &observed), FT_STATUS_OK);
    REQUIRE(same_item(observed, make_item(7, 700)));
    REQUIRE_STATUS(
        ft_run_delta_vector_try_get_dirty_run_containing(&dirty, 0, &found, &run),
        FT_STATUS_OK);
    REQUIRE(found && run.start == 0 && run.length == 1);
    REQUIRE(!ft_run_delta_vector_has_changes(&source));

    ft_run_delta_vector_dispose(&cancelled);
    ft_run_delta_vector_dispose(&dirty);
    ft_run_delta_vector_dispose(&no_op);
    ft_run_delta_vector_dispose(&source);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_point_edits_maintain_exact_maximal_runs(void)
{
    enum { length = 12 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector vector;
    ft_run_delta_vector unchanged;
    test_item initial[length];
    model_buffers buffers;
    ft_run_delta_run expected[2];
    size_t index = 0;

    reset_context(&context);
    init_config(&config, &context);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    for (index = 0; index != length; ++index) {
        initial[index] = make_item(0, (int)index);
    }
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&vector, &policy, initial, length),
        FT_STATUS_OK);

    REQUIRE_STATUS(advance_set(&vector, 2, make_item(20, 20)), FT_STATUS_OK);
    REQUIRE_STATUS(advance_set(&vector, 4, make_item(40, 40)), FT_STATUS_OK);
    expected[0].start = 2;
    expected[0].length = 1;
    expected[1].start = 4;
    expected[1].length = 1;
    assert_runs(&vector, expected, 2, &buffers);

    /* A new position between two singleton runs merges both. */
    REQUIRE_STATUS(advance_set(&vector, 3, make_item(30, 30)), FT_STATUS_OK);
    expected[0].start = 2;
    expected[0].length = 3;
    assert_runs(&vector, expected, 1, &buffers);

    /* A detached position, then the gap that joins it to the run on its left. */
    REQUIRE_STATUS(advance_set(&vector, 6, make_item(60, 60)), FT_STATUS_OK);
    REQUIRE_STATUS(advance_set(&vector, 5, make_item(50, 50)), FT_STATUS_OK);
    expected[0].start = 2;
    expected[0].length = 5;
    assert_runs(&vector, expected, 1, &buffers);

    /* Interior clearing splits one run in two. */
    REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, 4, &vector), FT_STATUS_OK);
    expected[0].start = 2;
    expected[0].length = 2;
    expected[1].start = 5;
    expected[1].length = 2;
    assert_runs(&vector, expected, 2, &buffers);

    /* Left-edge clearing shrinks from the start. */
    REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, 2, &vector), FT_STATUS_OK);
    expected[0].start = 3;
    expected[0].length = 1;
    assert_runs(&vector, expected, 2, &buffers);

    /* Right-edge clearing shrinks from the end. */
    REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, 6, &vector), FT_STATUS_OK);
    expected[1].start = 5;
    expected[1].length = 1;
    assert_runs(&vector, expected, 2, &buffers);

    /* Clearing the last dirty position snaps the current root back to the checkpoint root. */
    REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, 3, &vector), FT_STATUS_OK);
    REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, 5, &vector), FT_STATUS_OK);
    REQUIRE(!ft_run_delta_vector_has_changes(&vector));
    assert_runs(&vector, expected, 0, &buffers);
    REQUIRE(
        ft_run_delta_vector_current_root_identity(&vector) ==
        ft_run_delta_vector_checkpoint_root_identity(&vector));
    assert_matches_model(&vector, initial, initial, length, &buffers);

    /* Resetting an already clean position produces the receiver's contents. */
    REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, 3, &unchanged), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_shares_current_root(&unchanged, &vector));
    ft_run_delta_vector_dispose(&unchanged);

    ft_run_delta_vector_dispose(&vector);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_selected_runs_change_only_their_own_hunk(void)
{
    enum { length = 20 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector original;
    ft_run_delta_vector edited;
    ft_run_delta_vector accepted_middle;
    ft_run_delta_vector reverted_first;
    ft_run_delta_vector clean;
    ft_run_delta_vector spare;
    test_item initial[length];
    test_item current_model[length];
    test_item checkpoint_model[length];
    model_buffers buffers;
    ft_run_delta_run expected[3];
    size_t equal_calls_before = 0;
    size_t index = 0;

    reset_context(&context);
    init_config(&config, &context);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    for (index = 0; index != length; ++index) {
        initial[index] = make_item((int)index, (int)index);
    }
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&original, &policy, initial, length),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_run_delta_vector_copy(&original, &edited), FT_STATUS_OK);
    (void)memcpy(current_model, initial, sizeof(initial));
    (void)memcpy(checkpoint_model, initial, sizeof(initial));
    for (index = 0; index != length; ++index) {
        if ((index >= 2 && index < 6) || (index >= 9 && index < 14) || index == 17) {
            const test_item replacement = make_item(1000 + (int)index, 1000 + (int)index);
            REQUIRE_STATUS(advance_set(&edited, index, replacement), FT_STATUS_OK);
            current_model[index] = replacement;
        }
    }
    expected[0].start = 2;
    expected[0].length = 4;
    expected[1].start = 9;
    expected[1].length = 5;
    expected[2].start = 17;
    expected[2].length = 1;
    assert_runs(&edited, expected, 3, &buffers);
    REQUIRE(ft_run_delta_vector_dirty_count(&edited) == 10);

    /* Splicing a whole run must not consult the equality callback at all. */
    equal_calls_before = context.equal_calls;
    REQUIRE_STATUS(
        ft_run_delta_vector_accept_dirty_run_at(&edited, 1, &accepted_middle),
        FT_STATUS_OK);
    REQUIRE(context.equal_calls == equal_calls_before);
    for (index = 9; index != 14; ++index) {
        checkpoint_model[index] = current_model[index];
    }
    assert_matches_model(&accepted_middle, current_model, checkpoint_model, length, &buffers);

    equal_calls_before = context.equal_calls;
    REQUIRE_STATUS(
        ft_run_delta_vector_revert_dirty_run_at(&accepted_middle, 0, &reverted_first),
        FT_STATUS_OK);
    REQUIRE(context.equal_calls == equal_calls_before);
    for (index = 2; index != 6; ++index) {
        current_model[index] = checkpoint_model[index];
    }
    assert_matches_model(&reverted_first, current_model, checkpoint_model, length, &buffers);

    /* Accepting the last remaining run is the whole-checkpoint root swap. */
    equal_calls_before = context.equal_calls;
    REQUIRE_STATUS(
        ft_run_delta_vector_accept_dirty_run_at(&reverted_first, 0, &clean),
        FT_STATUS_OK);
    REQUIRE(context.equal_calls == equal_calls_before);
    REQUIRE(!ft_run_delta_vector_has_changes(&clean));
    checkpoint_model[17] = current_model[17];
    assert_matches_model(&clean, current_model, checkpoint_model, length, &buffers);
    REQUIRE(
        ft_run_delta_vector_current_root_identity(&clean) ==
        ft_run_delta_vector_checkpoint_root_identity(&clean));
    REQUIRE(ft_run_delta_vector_shares_current_root(&clean, &reverted_first));

    /* Every retained source version is unchanged, and out-of-range ranks fail. */
    assert_runs(&edited, expected, 3, &buffers);
    REQUIRE(!ft_run_delta_vector_has_changes(&original));
    REQUIRE_STATUS(
        ft_run_delta_vector_accept_dirty_run_at(&edited, 3, &spare),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_revert_dirty_run_at(&edited, 3, &spare),
        FT_STATUS_OUT_OF_RANGE);

    ft_run_delta_vector_dispose(&clean);
    ft_run_delta_vector_dispose(&reverted_first);
    ft_run_delta_vector_dispose(&accepted_middle);
    ft_run_delta_vector_dispose(&edited);
    ft_run_delta_vector_dispose(&original);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_position_addressed_runs_agree_with_rank_addressed_ones(void)
{
    enum { length = 16 };
    static const size_t clean_positions[] = { 0, 1, 6, 8, 12, 15 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector original;
    ft_run_delta_vector edited;
    ft_run_delta_vector by_rank;
    ft_run_delta_vector by_position;
    ft_run_delta_vector spare;
    test_item initial[length];
    test_item current_model[length];
    test_item checkpoint_model[length];
    test_item accepted_model[length];
    test_item reverted_model[length];
    model_buffers buffers;
    ft_run_delta_run expected[2];
    size_t equal_calls_before = 0;
    size_t index = 0;
    size_t probe = 0;

    reset_context(&context);
    init_config(&config, &context);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    for (index = 0; index != length; ++index) {
        initial[index] = make_item((int)index, (int)index);
    }
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&original, &policy, initial, length),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_run_delta_vector_copy(&original, &edited), FT_STATUS_OK);
    (void)memcpy(current_model, initial, sizeof(initial));
    (void)memcpy(checkpoint_model, initial, sizeof(initial));
    for (index = 0; index != length; ++index) {
        if ((index >= 2 && index < 6) || (index >= 9 && index < 12)) {
            const test_item replacement = make_item(1000 + (int)index, 1000 + (int)index);
            REQUIRE_STATUS(advance_set(&edited, index, replacement), FT_STATUS_OK);
            current_model[index] = replacement;
        }
    }
    expected[0].start = 2;
    expected[0].length = 4;
    expected[1].start = 9;
    expected[1].length = 3;
    assert_runs(&edited, expected, 2, &buffers);

    /* A position outside the vector is out of range, like every other position accessor. */
    REQUIRE_STATUS(
        ft_run_delta_vector_accept_dirty_run_containing(&edited, length, &spare),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_run_delta_vector_revert_dirty_run_containing(&edited, length, &spare),
        FT_STATUS_OUT_OF_RANGE);

    /* A clean position is vacuous rather than an error: every root comes back shared and no
     * equality call is made. */
    for (probe = 0; probe != sizeof(clean_positions) / sizeof(clean_positions[0]); ++probe) {
        const size_t position = clean_positions[probe];
        bool dirty = true;
        REQUIRE_STATUS(ft_run_delta_vector_is_dirty(&edited, position, &dirty), FT_STATUS_OK);
        REQUIRE(!dirty);
        equal_calls_before = context.equal_calls;
        REQUIRE_STATUS(
            ft_run_delta_vector_accept_dirty_run_containing(&edited, position, &spare),
            FT_STATUS_OK);
        REQUIRE(ft_run_delta_vector_shares_current_root(&spare, &edited));
        REQUIRE(ft_run_delta_vector_shares_checkpoint_root(&spare, &edited));
        REQUIRE(
            ft_run_delta_vector_dirty_count(&spare) ==
            ft_run_delta_vector_dirty_count(&edited));
        ft_run_delta_vector_dispose(&spare);
        REQUIRE_STATUS(
            ft_run_delta_vector_revert_dirty_run_containing(&edited, position, &spare),
            FT_STATUS_OK);
        REQUIRE(ft_run_delta_vector_shares_current_root(&spare, &edited));
        REQUIRE(ft_run_delta_vector_shares_checkpoint_root(&spare, &edited));
        ft_run_delta_vector_dispose(&spare);
        REQUIRE(context.equal_calls == equal_calls_before);
    }

    /* Every position inside a run acts exactly as the rank-addressed operation on that run. */
    (void)memcpy(accepted_model, checkpoint_model, sizeof(checkpoint_model));
    for (index = 9; index != 12; ++index) {
        accepted_model[index] = current_model[index];
    }
    REQUIRE_STATUS(ft_run_delta_vector_accept_dirty_run_at(&edited, 1, &by_rank), FT_STATUS_OK);
    assert_matches_model(&by_rank, current_model, accepted_model, length, &buffers);
    ft_run_delta_vector_dispose(&by_rank);
    equal_calls_before = context.equal_calls;
    for (index = 9; index != 12; ++index) {
        REQUIRE_STATUS(
            ft_run_delta_vector_accept_dirty_run_containing(&edited, index, &by_position),
            FT_STATUS_OK);
        assert_matches_model(&by_position, current_model, accepted_model, length, &buffers);
        ft_run_delta_vector_dispose(&by_position);
    }

    (void)memcpy(reverted_model, current_model, sizeof(current_model));
    for (index = 9; index != 12; ++index) {
        reverted_model[index] = checkpoint_model[index];
    }
    REQUIRE_STATUS(ft_run_delta_vector_revert_dirty_run_at(&edited, 1, &by_rank), FT_STATUS_OK);
    assert_matches_model(&by_rank, reverted_model, checkpoint_model, length, &buffers);
    ft_run_delta_vector_dispose(&by_rank);
    for (index = 9; index != 12; ++index) {
        REQUIRE_STATUS(
            ft_run_delta_vector_revert_dirty_run_containing(&edited, index, &by_position),
            FT_STATUS_OK);
        assert_matches_model(&by_position, reverted_model, checkpoint_model, length, &buffers);
        ft_run_delta_vector_dispose(&by_position);
    }

    /* The version every branch came from is still exactly what it was. */
    assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);

    ft_run_delta_vector_dispose(&edited);
    ft_run_delta_vector_dispose(&original);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_clustered_delta_uses_two_descriptors(void)
{
    enum { length = 8192 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector source;
    ft_run_delta_vector edited;
    ft_run_delta_vector accepted;
    ft_run_delta_statistics statistics;
    ft_run_delta_run runs[4];
    run_collector collector;
    test_item* initial = NULL;
    test_item observed;
    size_t index = 0;
    bool valid = false;

    reset_context(&context);
    init_config(&config, &context);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    initial = (test_item*)malloc(sizeof(test_item) * (size_t)length);
    REQUIRE(initial != NULL);
    for (index = 0; index != (size_t)length; ++index) {
        initial[index] = make_item(0, (int)index);
    }
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&source, &policy, initial, (size_t)length),
        FT_STATUS_OK);
    free(initial);

    REQUIRE_STATUS(ft_run_delta_vector_copy(&source, &edited), FT_STATUS_OK);
    for (index = 0; index + 2 < (size_t)length; ++index) {
        REQUIRE_STATUS(advance_set(&edited, index, make_item(1, 1)), FT_STATUS_OK);
    }
    REQUIRE_STATUS(advance_set(&edited, (size_t)length - 1, make_item(1, 1)), FT_STATUS_OK);

    /* Thousands of dirty positions, exactly two descriptors. */
    REQUIRE(ft_run_delta_vector_dirty_count(&edited) == (size_t)length - 1);
    collector.runs = runs;
    collector.capacity = sizeof(runs) / sizeof(runs[0]);
    collector.count = 0;
    collector.overflowed = false;
    REQUIRE_STATUS(
        ft_run_delta_vector_visit_dirty_runs(&edited, collect_run, &collector),
        FT_STATUS_OK);
    REQUIRE(collector.count == 2);
    REQUIRE(runs[0].start == 0 && runs[0].length == (size_t)length - 2);
    REQUIRE(runs[1].start == (size_t)length - 1 && runs[1].length == 1);
    REQUIRE_STATUS(ft_run_delta_vector_validate(&edited, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.dirty_run_count == 2);

    /* One splice retires thousands of dirty positions without touching the other run. */
    REQUIRE_STATUS(ft_run_delta_vector_accept_dirty_run_at(&edited, 0, &accepted), FT_STATUS_OK);
    REQUIRE(ft_run_delta_vector_dirty_run_count(&accepted) == 1);
    REQUIRE(ft_run_delta_vector_dirty_count(&accepted) == 1);
    REQUIRE_STATUS(ft_run_delta_vector_dirty_run_at(&accepted, 0, &runs[2]), FT_STATUS_OK);
    REQUIRE(runs[2].start == (size_t)length - 1 && runs[2].length == 1);
    REQUIRE_STATUS(ft_run_delta_vector_checkpoint_at_copy(&accepted, 0, &observed), FT_STATUS_OK);
    REQUIRE(observed.key == 1);
    REQUIRE_STATUS(
        ft_run_delta_vector_checkpoint_at_copy(&accepted, (size_t)length - 1, &observed),
        FT_STATUS_OK);
    REQUIRE(observed.key == 0);
    REQUIRE_STATUS(ft_run_delta_vector_validate(&accepted, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);

    /* The original version still reads as it was built. */
    REQUIRE(!ft_run_delta_vector_has_changes(&source));
    for (index = 0; index != (size_t)length; index += 512) {
        REQUIRE_STATUS(ft_run_delta_vector_at_copy(&source, index, &observed), FT_STATUS_OK);
        REQUIRE(same_item(observed, make_item(0, (int)index)));
    }

    ft_run_delta_vector_dispose(&accepted);
    ft_run_delta_vector_dispose(&edited);
    ft_run_delta_vector_dispose(&source);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_run_index_scales_to_many_runs(void)
{
    enum { length = 128, operations = 400 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector vector;
    test_item initial[length];
    test_item current_model[length];
    test_item checkpoint_model[length];
    model_buffers buffers;
    rng generator;
    int representative = 5000;
    size_t operation = 0;
    size_t index = 0;

    reset_context(&context);
    init_config(&config, &context);
    generator.state = 0xD17E20260099ull;
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    for (index = 0; index != length; ++index) {
        initial[index] = make_item((int)index, (int)index);
    }
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&vector, &policy, initial, length),
        FT_STATUS_OK);
    (void)memcpy(current_model, initial, sizeof(initial));
    (void)memcpy(checkpoint_model, initial, sizeof(initial));

    /* Every other position dirty is the maximum number of maximal runs a vector can publish, so
     * the run index is at its widest. */
    for (index = 0; index < (size_t)length; index += 2) {
        test_item value;
        ++representative;
        value = make_item(representative, representative);
        REQUIRE_STATUS(advance_set(&vector, index, value), FT_STATUS_OK);
        current_model[index] = value;
    }
    REQUIRE(ft_run_delta_vector_dirty_run_count(&vector) == (size_t)length / 2);
    assert_matches_model(&vector, current_model, checkpoint_model, length, &buffers);

    /* Toggling arbitrary positions drives every insert, merge, shrink, split, and delete case
     * through a wide index. */
    for (operation = 0; operation != operations; ++operation) {
        const size_t position = rng_below(&generator, length);
        if (rng_below(&generator, 2) == 0) {
            const test_item value = make_item((int)rng_below(&generator, 12), ++representative);
            REQUIRE_STATUS(advance_set(&vector, position, value), FT_STATUS_OK);
            if (current_model[position].key != value.key) {
                current_model[position] = checkpoint_model[position].key == value.key
                    ? checkpoint_model[position]
                    : value;
            }
        } else {
            REQUIRE_STATUS(ft_run_delta_vector_reset(&vector, position, &vector), FT_STATUS_OK);
            current_model[position] = checkpoint_model[position];
        }
        if (operation % 8 == 0) {
            assert_matches_model(&vector, current_model, checkpoint_model, length, &buffers);
        }
    }
    assert_matches_model(&vector, current_model, checkpoint_model, length, &buffers);

    /* Retiring the index one run at a time, in place, exercises deletion from every shape. */
    while (ft_run_delta_vector_dirty_run_count(&vector) != 0) {
        ft_run_delta_run run;
        const size_t rank =
            rng_below(&generator, ft_run_delta_vector_dirty_run_count(&vector));
        const bool accept = rng_below(&generator, 2) == 0;
        REQUIRE_STATUS(ft_run_delta_vector_dirty_run_at(&vector, rank, &run), FT_STATUS_OK);
        if (accept) {
            REQUIRE_STATUS(
                ft_run_delta_vector_accept_dirty_run_at(&vector, rank, &vector),
                FT_STATUS_OK);
            for (index = 0; index != run.length; ++index) {
                checkpoint_model[run.start + index] = current_model[run.start + index];
            }
        } else {
            REQUIRE_STATUS(
                ft_run_delta_vector_revert_dirty_run_containing(&vector, run.start, &vector),
                FT_STATUS_OK);
            for (index = 0; index != run.length; ++index) {
                current_model[run.start + index] = checkpoint_model[run.start + index];
            }
        }
        assert_matches_model(&vector, current_model, checkpoint_model, length, &buffers);
    }
    REQUIRE(!ft_run_delta_vector_has_changes(&vector));
    REQUIRE(
        ft_run_delta_vector_current_root_identity(&vector) ==
        ft_run_delta_vector_checkpoint_root_identity(&vector));

    ft_run_delta_vector_dispose(&vector);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef struct model_version {
    ft_run_delta_vector value;
    test_item current[32];
    test_item checkpoint[32];
} model_version;

static void test_randomized_retained_versions_match_the_model(void)
{
    enum { length = 32, pool_limit = 10, operations = 600 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    model_version* pool = NULL;
    model_buffers buffers;
    rng generator;
    test_item initial[length];
    int representative = 1000;
    size_t pool_count = 0;
    size_t operation = 0;
    size_t index = 0;

    reset_context(&context);
    init_config(&config, &context);
    generator.state = 0x5A1720261234ull;
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    pool = (model_version*)malloc(sizeof(model_version) * (size_t)(pool_limit + 1));
    REQUIRE(pool != NULL);
    for (index = 0; index != length; ++index) {
        initial[index] = make_item((int)(index % 5), (int)index);
    }
    if (ft_run_delta_vector_from_array(&pool[0].value, &policy, initial, length) !=
        FT_STATUS_OK) {
        free(pool);
        REQUIRE(false);
    }
    (void)memcpy(pool[0].current, initial, sizeof(initial));
    (void)memcpy(pool[0].checkpoint, initial, sizeof(initial));
    pool_count = 1;

    for (operation = 0; operation != operations; ++operation) {
        const size_t parent = rng_below(&generator, pool_count);
        model_version* const next = &pool[pool_count];
        ft_status status = FT_STATUS_OK;
        size_t choice = 0;
        (void)memcpy(next->current, pool[parent].current, sizeof(next->current));
        (void)memcpy(next->checkpoint, pool[parent].checkpoint, sizeof(next->checkpoint));

        choice = rng_below(&generator, 8);
        if (choice < 3) {
            const size_t position = rng_below(&generator, length);
            const test_item replacement =
                make_item((int)rng_below(&generator, 7), ++representative);
            status = ft_run_delta_vector_set(
                &pool[parent].value,
                position,
                &replacement,
                &next->value);
            if (status == FT_STATUS_OK && next->current[position].key != replacement.key) {
                next->current[position] = next->checkpoint[position].key == replacement.key
                    ? next->checkpoint[position]
                    : replacement;
            }
        } else if (choice == 3) {
            const size_t position = rng_below(&generator, length);
            status = ft_run_delta_vector_reset(&pool[parent].value, position, &next->value);
            if (status == FT_STATUS_OK) {
                next->current[position] = next->checkpoint[position];
            }
        } else if (choice == 4) {
            status = ft_run_delta_vector_checkpoint(&pool[parent].value, &next->value);
            if (status == FT_STATUS_OK) {
                (void)memcpy(next->checkpoint, next->current, sizeof(next->checkpoint));
            }
        } else if (choice == 5) {
            status = ft_run_delta_vector_rollback(&pool[parent].value, &next->value);
            if (status == FT_STATUS_OK) {
                (void)memcpy(next->current, next->checkpoint, sizeof(next->current));
            }
        } else {
            ft_run_delta_run model_runs[length];
            const size_t run_count = expected_runs(
                next->current,
                next->checkpoint,
                length,
                model_runs,
                length);
            if (run_count == 0) {
                status = ft_run_delta_vector_copy(&pool[parent].value, &next->value);
            } else {
                const size_t rank = rng_below(&generator, run_count);
                const ft_run_delta_run run = model_runs[rank];
                const size_t position = run.start + rng_below(&generator, run.length);
                const bool by_position = rng_below(&generator, 2) == 0;
                size_t inside = 0;
                if (choice == 6) {
                    status = by_position
                        ? ft_run_delta_vector_accept_dirty_run_containing(
                              &pool[parent].value,
                              position,
                              &next->value)
                        : ft_run_delta_vector_accept_dirty_run_at(
                              &pool[parent].value,
                              rank,
                              &next->value);
                    if (status == FT_STATUS_OK) {
                        for (inside = 0; inside != run.length; ++inside) {
                            next->checkpoint[run.start + inside] =
                                next->current[run.start + inside];
                        }
                    }
                } else {
                    status = by_position
                        ? ft_run_delta_vector_revert_dirty_run_containing(
                              &pool[parent].value,
                              position,
                              &next->value)
                        : ft_run_delta_vector_revert_dirty_run_at(
                              &pool[parent].value,
                              rank,
                              &next->value);
                    if (status == FT_STATUS_OK) {
                        for (inside = 0; inside != run.length; ++inside) {
                            next->current[run.start + inside] =
                                next->checkpoint[run.start + inside];
                        }
                    }
                }
            }
        }

        if (status != FT_STATUS_OK) {
            (void)fprintf(stderr, "randomized operation failed with %d\n", (int)status);
            ++g_failures;
            break;
        }
        ++pool_count;

        assert_matches_model(&next->value, next->current, next->checkpoint, length, &buffers);
        assert_matches_model(
            &pool[parent].value,
            pool[parent].current,
            pool[parent].checkpoint,
            length,
            &buffers);
        if (g_failures != 0) {
            break;
        }
        if (pool_count > (size_t)pool_limit) {
            const size_t victim = 1 + rng_below(&generator, pool_count - 1);
            ft_run_delta_vector_dispose(&pool[victim].value);
            pool[victim] = pool[pool_count - 1];
            --pool_count;
        }
    }

    for (index = 0; index != pool_count; ++index) {
        ft_run_delta_vector_dispose(&pool[index].value);
    }
    free(pool);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_failure_atomicity_and_lifetimes(void)
{
    enum { length = 24 };
    test_context context;
    ft_run_delta_policy_config config;
    ft_run_delta_policy policy;
    ft_run_delta_vector source;
    ft_run_delta_vector edited;
    ft_run_delta_vector probe;
    ft_run_delta_vector sentinel;
    test_item initial[length];
    test_item current_model[length];
    test_item checkpoint_model[length];
    model_buffers buffers;
    stop_visitor_state stop_state;
    const test_item replacement = { 4242, 4242 };
    size_t index = 0;
    size_t attempt = 0;

    reset_context(&context);
    init_config(&config, &context);
    REQUIRE_STATUS(ft_run_delta_policy_create(&policy, &config), FT_STATUS_OK);
    for (index = 0; index != length; ++index) {
        initial[index] = make_item((int)index, (int)index);
    }
    REQUIRE_STATUS(
        ft_run_delta_vector_from_array(&source, &policy, initial, length),
        FT_STATUS_OK);
    (void)memcpy(current_model, initial, sizeof(initial));
    (void)memcpy(checkpoint_model, initial, sizeof(initial));
    REQUIRE_STATUS(ft_run_delta_vector_copy(&source, &edited), FT_STATUS_OK);
    for (index = 3; index != 9; ++index) {
        const test_item value = make_item(500 + (int)index, 500 + (int)index);
        REQUIRE_STATUS(advance_set(&edited, index, value), FT_STATUS_OK);
        current_model[index] = value;
    }
    for (index = 15; index != 17; ++index) {
        const test_item value = make_item(600 + (int)index, 600 + (int)index);
        REQUIRE_STATUS(advance_set(&edited, index, value), FT_STATUS_OK);
        current_model[index] = value;
    }
    assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);

    (void)memset(&sentinel, 0xAB, sizeof(sentinel));

    /* A failing equality callback publishes no partial version. */
    context.equal_calls = 0;
    context.fail_equal_at = 1;
    probe = sentinel;
    REQUIRE_STATUS(
        ft_run_delta_vector_set(&edited, 10, &replacement, &probe),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(memcmp(&probe, &sentinel, sizeof(probe)) == 0);
    context.equal_calls = 0;
    context.fail_equal_at = 2;
    probe = sentinel;
    REQUIRE_STATUS(
        ft_run_delta_vector_set(&edited, 10, &replacement, &probe),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(memcmp(&probe, &sentinel, sizeof(probe)) == 0);
    context.fail_equal_at = 0;
    assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);

    /* A failing copy callback publishes no partial version either. */
    context.copy_calls = 0;
    context.fail_copy_at = 1;
    probe = sentinel;
    REQUIRE_STATUS(
        ft_run_delta_vector_set(&edited, 10, &replacement, &probe),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(memcmp(&probe, &sentinel, sizeof(probe)) == 0);
    context.fail_copy_at = 0;
    assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);

    /* Every allocation site is exercised: each failure leaves the operand valid and leaks
     * nothing. */
    for (attempt = 1; attempt != 40; ++attempt) {
        ft_status status = FT_STATUS_OK;
        context.allocation_calls = 0;
        context.fail_allocation_at = attempt;
        probe = sentinel;
        status = ft_run_delta_vector_set(&edited, 10, &replacement, &probe);
        context.fail_allocation_at = 0;
        if (status == FT_STATUS_OK) {
            ft_run_delta_vector_dispose(&probe);
        } else {
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(memcmp(&probe, &sentinel, sizeof(probe)) == 0);
        }
        assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);

        context.allocation_calls = 0;
        context.fail_allocation_at = attempt;
        probe = sentinel;
        status = ft_run_delta_vector_accept_dirty_run_at(&edited, 0, &probe);
        context.fail_allocation_at = 0;
        if (status == FT_STATUS_OK) {
            ft_run_delta_vector_dispose(&probe);
        } else {
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(memcmp(&probe, &sentinel, sizeof(probe)) == 0);
        }
        assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);

        context.allocation_calls = 0;
        context.fail_allocation_at = attempt;
        probe = sentinel;
        status = ft_run_delta_vector_revert_dirty_run_containing(&edited, 16, &probe);
        context.fail_allocation_at = 0;
        if (status == FT_STATUS_OK) {
            ft_run_delta_vector_dispose(&probe);
        } else {
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(memcmp(&probe, &sentinel, sizeof(probe)) == 0);
        }
        assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);
    }

    /* After every injected failure a successful edit still behaves, and the operand it branched
     * from is untouched. */
    probe = sentinel;
    REQUIRE_STATUS(ft_run_delta_vector_set(&edited, 10, &replacement, &probe), FT_STATUS_OK);
    current_model[10] = replacement;
    assert_matches_model(&probe, current_model, checkpoint_model, length, &buffers);
    current_model[10] = checkpoint_model[10];
    assert_matches_model(&edited, current_model, checkpoint_model, length, &buffers);
    ft_run_delta_vector_dispose(&probe);

    /* An aborting visitor ends the traversal and propagates its status. */
    stop_state.seen = 0;
    stop_state.stop_after = 4;
    REQUIRE_STATUS(
        ft_run_delta_vector_visit(&edited, stopping_value_visit, &stop_state),
        FT_STATUS_NOT_FOUND);
    REQUIRE(stop_state.seen == 4);
    stop_state.seen = 0;
    stop_state.stop_after = 1;
    REQUIRE_STATUS(
        ft_run_delta_vector_visit_dirty_runs(&edited, stopping_run_visit, &stop_state),
        FT_STATUS_NOT_FOUND);
    REQUIRE(stop_state.seen == 1);

    ft_run_delta_vector_dispose(&edited);
    ft_run_delta_vector_dispose(&source);
    ft_run_delta_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls != 0);
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
    run_test("Run-delta empty and boundary contracts", test_empty_and_boundary_contracts);
    run_test(
        "Run-delta equality no-ops and exact representatives",
        test_equality_no_ops_and_exact_representatives);
    run_test(
        "Run-delta point edits maintain exact maximal runs",
        test_point_edits_maintain_exact_maximal_runs);
    run_test(
        "Run-delta selected runs change only their own hunk",
        test_selected_runs_change_only_their_own_hunk);
    run_test(
        "Run-delta position-addressed runs agree with rank-addressed ones",
        test_position_addressed_runs_agree_with_rank_addressed_ones);
    run_test(
        "Run-delta clustered delta uses two descriptors",
        test_clustered_delta_uses_two_descriptors);
    run_test("Run-delta run index scales to many runs", test_run_index_scales_to_many_runs);
    run_test(
        "Run-delta randomized retained versions match the model",
        test_randomized_retained_versions_match_the_model);
    run_test("Run-delta failure atomicity and lifetimes", test_failure_atomicity_and_lifetimes);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C persistent run-delta vector tests passed\n");
    return EXIT_SUCCESS;
}
