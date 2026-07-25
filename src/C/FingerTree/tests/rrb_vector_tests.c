#include <durable7/finger_tree/rrb_vector.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#if !defined(__STDC_NO_THREADS__)
#define RRB_TEST_HAS_THREADS 1
#include <threads.h>
#endif
#endif

static int g_failures = 0;

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

static bool int_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

static void init_int_policy(ft_rrb_policy* policy)
{
    ft_value_type type;
    ft_value_type_init(&type, sizeof(int));
    ft_rrb_policy_init(policy, &type, int_equal, NULL);
}

typedef struct int_collector {
    int* values;
    size_t count;
    size_t capacity;
} int_collector;

static void collect_int(const void* value, void* context)
{
    int_collector* collector = (int_collector*)context;
    if (collector->count < collector->capacity) {
        collector->values[collector->count] = *(const int*)value;
    }
    ++collector->count;
}

static bool vector_matches(const ft_rrb_vector* vector, const int* values, size_t count)
{
    if (ft_rrb_vector_size(vector) != count) {
        return false;
    }
    for (size_t index = 0; index != count; ++index) {
        int actual = 0;
        if (ft_rrb_vector_at(vector, index, &actual) != FT_STATUS_OK || actual != values[index]) {
            return false;
        }
    }
    int* visited = count == 0 ? NULL : (int*)malloc(count * sizeof(int));
    if (count != 0 && visited == NULL) {
        return false;
    }
    int_collector collector = { visited, 0, count };
    const bool valid_visit = ft_rrb_vector_visit(vector, collect_int, &collector) == FT_STATUS_OK &&
        collector.count == count && (count == 0 || memcmp(visited, values, count * sizeof(int)) == 0);
    free(visited);
    return valid_visit;
}

typedef struct leaf_collector {
    const void** identities;
    size_t* lengths;
    size_t count;
    size_t capacity;
} leaf_collector;

static void collect_leaf(const void* identity, size_t count, void* context)
{
    leaf_collector* collector = (leaf_collector*)context;
    if (collector->count < collector->capacity) {
        collector->identities[collector->count] = identity;
        collector->lengths[collector->count] = count;
    }
    ++collector->count;
}

static size_t collect_leaf_identities(
    const ft_rrb_vector* vector,
    const void** identities,
    size_t* lengths,
    size_t capacity)
{
    leaf_collector collector = { identities, lengths, 0, capacity };
    if (ft_rrb_vector_visit_leaves(vector, collect_leaf, &collector) != FT_STATUS_OK) {
        return SIZE_MAX;
    }
    return collector.count;
}

static int* make_range(size_t start, size_t count)
{
    int* values = count == 0 ? NULL : (int*)malloc(count * sizeof(int));
    if (count != 0 && values == NULL) {
        return NULL;
    }
    for (size_t index = 0; index != count; ++index) {
        values[index] = (int)(start + index);
    }
    return values;
}

static void test_boundaries_and_unequal_concat(void)
{
    static const size_t counts[] = { 0, 1, 31, 32, 33, 1023, 1024, 1025, 100000 };
    ft_rrb_policy policy;
    init_int_policy(&policy);
    for (size_t case_index = 0; case_index != sizeof(counts) / sizeof(counts[0]); ++case_index) {
        const size_t count = counts[case_index];
        int* values = make_range(0, count);
        REQUIRE(count == 0 || values != NULL);
        ft_rrb_vector vector;
        REQUIRE_STATUS(ft_rrb_vector_from_array(&vector, &policy, values, count), FT_STATUS_OK);
        REQUIRE(vector_matches(&vector, values, count));
        ft_rrb_statistics statistics;
        REQUIRE(ft_rrb_vector_validate(&vector, &statistics));
        REQUIRE(statistics.count == count);
        REQUIRE(statistics.relaxed_branch_count == 0);
        ft_rrb_vector_dispose(&vector);
        free(values);
    }

    static const size_t cases[][2] = {
        { 1, 100000 }, { 100000, 1 }, { 31, 33 }, { 1023, 1025 }, { 50000, 50000 }
    };
    for (size_t case_index = 0; case_index != sizeof(cases) / sizeof(cases[0]); ++case_index) {
        const size_t left_count = cases[case_index][0];
        const size_t right_count = cases[case_index][1];
        int* left_values = make_range(0, left_count);
        int* right_values = make_range(left_count, right_count);
        int* expected = make_range(0, left_count + right_count);
        REQUIRE(left_values != NULL && right_values != NULL && expected != NULL);
        ft_rrb_vector left;
        ft_rrb_vector right;
        ft_rrb_vector combined;
        REQUIRE_STATUS(ft_rrb_vector_from_array(&left, &policy, left_values, left_count), FT_STATUS_OK);
        REQUIRE_STATUS(ft_rrb_vector_from_array(&right, &policy, right_values, right_count), FT_STATUS_OK);
        REQUIRE_STATUS(ft_rrb_vector_concat(&left, &right, &combined), FT_STATUS_OK);
        REQUIRE(vector_matches(&combined, expected, left_count + right_count));
        ft_rrb_statistics statistics;
        REQUIRE(ft_rrb_vector_validate(&combined, &statistics));

        ft_rrb_vector empty;
        REQUIRE_STATUS(ft_rrb_vector_init(&empty, &policy), FT_STATUS_OK);
        ft_rrb_vector same;
        REQUIRE_STATUS(ft_rrb_vector_concat(&left, &empty, &same), FT_STATUS_OK);
        REQUIRE(ft_rrb_vector_shares_root(&left, &same));
        ft_rrb_vector_dispose(&same);
        REQUIRE_STATUS(ft_rrb_vector_concat(&empty, &right, &same), FT_STATUS_OK);
        REQUIRE(ft_rrb_vector_shares_root(&right, &same));

        ft_rrb_vector_dispose(&same);
        ft_rrb_vector_dispose(&empty);
        ft_rrb_vector_dispose(&combined);
        ft_rrb_vector_dispose(&right);
        ft_rrb_vector_dispose(&left);
        free(expected);
        free(right_values);
        free(left_values);
    }
}

static void test_exact_sharing_and_relaxed_layout(void)
{
    enum { leaf_count = 128, count = 32 * leaf_count };
    ft_rrb_policy policy;
    init_int_policy(&policy);
    int* values = make_range(0, count);
    REQUIRE(values != NULL);
    ft_rrb_vector vector;
    REQUIRE_STATUS(ft_rrb_vector_from_array(&vector, &policy, values, count), FT_STATUS_OK);

    const void* original[leaf_count];
    size_t lengths[leaf_count];
    REQUIRE(collect_leaf_identities(&vector, original, lengths, leaf_count) == leaf_count);
    static const size_t boundaries[] = { 32, 64, 32 * 31, 32 * 32, 32 * 33, 32 * 96, 32 * 127 };
    for (size_t case_index = 0; case_index != sizeof(boundaries) / sizeof(boundaries[0]); ++case_index) {
        ft_rrb_split_result split;
        REQUIRE_STATUS(ft_rrb_vector_split_at(&vector, boundaries[case_index], &split), FT_STATUS_OK);
        const void* retained[leaf_count];
        size_t retained_lengths[leaf_count];
        const size_t left_leaves = collect_leaf_identities(&split.left, retained, retained_lengths, leaf_count);
        const size_t right_leaves = collect_leaf_identities(
            &split.right,
            retained + left_leaves,
            retained_lengths + left_leaves,
            leaf_count - left_leaves);
        REQUIRE(left_leaves + right_leaves == leaf_count);
        for (size_t index = 0; index != leaf_count; ++index) {
            REQUIRE(retained[index] == original[index]);
            REQUIRE(retained_lengths[index] == lengths[index]);
        }
        ft_rrb_statistics stats;
        REQUIRE(ft_rrb_vector_validate(&split.left, &stats));
        REQUIRE(ft_rrb_vector_validate(&split.right, &stats));
        ft_rrb_vector_dispose(&split.left);
        ft_rrb_vector_dispose(&split.right);
    }

    ft_rrb_split_result relaxed_split;
    REQUIRE_STATUS(ft_rrb_vector_split_at(&vector, 1, &relaxed_split), FT_STATUS_OK);
    ft_rrb_statistics relaxed_stats;
    REQUIRE(ft_rrb_vector_validate(&relaxed_split.right, &relaxed_stats));
    REQUIRE(relaxed_stats.relaxed_branch_count > 0);
    ft_rrb_vector_dispose(&relaxed_split.left);
    ft_rrb_vector_dispose(&relaxed_split.right);

    int same_value = values[32 * 73 + 11];
    ft_rrb_vector unchanged;
    REQUIRE_STATUS(ft_rrb_vector_set(&vector, 32 * 73 + 11, &same_value, &unchanged), FT_STATUS_OK);
    REQUIRE(ft_rrb_vector_shares_root(&vector, &unchanged));
    ft_rrb_vector_dispose(&unchanged);

    int changed_value = -1;
    ft_rrb_vector changed;
    REQUIRE_STATUS(ft_rrb_vector_set(&vector, 32 * 73 + 11, &changed_value, &changed), FT_STATUS_OK);
    const void* changed_ids[leaf_count];
    size_t changed_lengths[leaf_count];
    REQUIRE(collect_leaf_identities(&changed, changed_ids, changed_lengths, leaf_count) == leaf_count);
    for (size_t index = 0; index != leaf_count; ++index) {
        REQUIRE((index == 73) == (changed_ids[index] != original[index]));
    }

    ft_rrb_vector_dispose(&changed);
    ft_rrb_vector_dispose(&vector);
    free(values);
}

typedef struct retained_snapshot {
    ft_rrb_vector vector;
    int* values;
    size_t count;
} retained_snapshot;

static uint32_t next_random(uint32_t* state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static bool ensure_model_capacity(int** values, size_t* capacity, size_t required)
{
    if (required <= *capacity) {
        return true;
    }
    size_t next = *capacity == 0 ? 64 : *capacity;
    while (next < required) {
        next *= 2;
    }
    int* resized = (int*)realloc(*values, next * sizeof(int));
    if (resized == NULL) {
        return false;
    }
    *values = resized;
    *capacity = next;
    return true;
}

static void test_randomized_model_and_aliasing(void)
{
    ft_rrb_policy policy;
    init_int_policy(&policy);
    ft_rrb_vector vector;
    REQUIRE_STATUS(ft_rrb_vector_init(&vector, &policy), FT_STATUS_OK);
    int* model = NULL;
    size_t count = 0;
    size_t capacity = 0;
    retained_snapshot snapshots[16];
    size_t snapshot_count = 0;
    uint32_t state = UINT32_C(20260714);

    for (int step = 0; step != 10000; ++step) {
        const unsigned operation = next_random(&state) % 7u;
        if (operation == 0) {
            REQUIRE(ensure_model_capacity(&model, &capacity, count + 1));
            int value = step;
            REQUIRE_STATUS(ft_rrb_vector_push_back(&vector, &value, &vector), FT_STATUS_OK);
            model[count++] = value;
        } else if (operation == 1) {
            REQUIRE(ensure_model_capacity(&model, &capacity, count + 1));
            int value = step;
            REQUIRE_STATUS(ft_rrb_vector_push_front(&vector, &value, &vector), FT_STATUS_OK);
            memmove(model + 1, model, count * sizeof(int));
            model[0] = value;
            ++count;
        } else if (operation == 2 && count != 0) {
            const size_t index = next_random(&state) % count;
            int value = -step;
            REQUIRE_STATUS(ft_rrb_vector_set(&vector, index, &value, &vector), FT_STATUS_OK);
            model[index] = value;
        } else if (operation == 3) {
            const size_t index = count == 0 ? 0 : next_random(&state) % (count + 1);
            int inserted[3] = { step, step + 1, step + 2 };
            REQUIRE(ensure_model_capacity(&model, &capacity, count + 3));
            REQUIRE_STATUS(ft_rrb_vector_insert_range(&vector, index, inserted, 3, &vector), FT_STATUS_OK);
            memmove(model + index + 3, model + index, (count - index) * sizeof(int));
            memcpy(model + index, inserted, sizeof(inserted));
            count += 3;
        } else if (operation == 4 && count != 0) {
            const size_t index = next_random(&state) % count;
            const size_t removed = next_random(&state) % (count - index + 1);
            REQUIRE_STATUS(ft_rrb_vector_remove_range(&vector, index, removed, &vector), FT_STATUS_OK);
            memmove(model + index, model + index + removed, (count - index - removed) * sizeof(int));
            count -= removed;
        } else if (operation == 5) {
            const size_t boundary = count == 0 ? 0 : next_random(&state) % (count + 1);
            ft_rrb_split_result split;
            REQUIRE_STATUS(ft_rrb_vector_split_at(&vector, boundary, &split), FT_STATUS_OK);
            ft_rrb_vector joined;
            REQUIRE_STATUS(ft_rrb_vector_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
            ft_rrb_vector_dispose(&vector);
            ft_rrb_vector_move(&vector, &joined);
            ft_rrb_vector_dispose(&split.left);
            ft_rrb_vector_dispose(&split.right);
        } else if (operation == 6 && count != 0) {
            int popped = 0;
            REQUIRE_STATUS(ft_rrb_vector_pop_back(&vector, &popped, &vector), FT_STATUS_OK);
            REQUIRE(popped == model[count - 1]);
            --count;
        }

        REQUIRE(ft_rrb_vector_size(&vector) == count);
        if (step % 701 == 0 && snapshot_count < sizeof(snapshots) / sizeof(snapshots[0])) {
            retained_snapshot* snapshot = &snapshots[snapshot_count++];
            REQUIRE_STATUS(ft_rrb_vector_copy(&vector, &snapshot->vector), FT_STATUS_OK);
            snapshot->values = count == 0 ? NULL : (int*)malloc(count * sizeof(int));
            REQUIRE(count == 0 || snapshot->values != NULL);
            if (count != 0) {
                memcpy(snapshot->values, model, count * sizeof(int));
            }
            snapshot->count = count;
        }
        if (step % 997 == 0) {
            ft_rrb_statistics statistics;
            REQUIRE(ft_rrb_vector_validate(&vector, &statistics));
        }
    }

    REQUIRE(vector_matches(&vector, model, count));
    for (size_t index = 0; index != snapshot_count; ++index) {
        REQUIRE(vector_matches(&snapshots[index].vector, snapshots[index].values, snapshots[index].count));
        ft_rrb_vector_dispose(&snapshots[index].vector);
        free(snapshots[index].values);
    }
    ft_rrb_vector_dispose(&vector);
    free(model);
}

static size_t minimum_height(size_t count)
{
    size_t capacity = 32;
    size_t height = 0;
    while (capacity < count) {
        capacity *= 32;
        ++height;
    }
    return height;
}

static void test_adversarial_density_and_height(void)
{
    enum { count = 32 * 1024 };
    ft_rrb_policy policy;
    init_int_policy(&policy);
    int* values = make_range(0, count);
    REQUIRE(values != NULL);
    ft_rrb_vector vector;
    REQUIRE_STATUS(ft_rrb_vector_from_array(&vector, &policy, values, count), FT_STATUS_OK);
    uint32_t state = UINT32_C(20260724);

    for (size_t operation = 0; operation != 2000; ++operation) {
        size_t boundary = 0;
        switch (operation % 7) {
        case 0: boundary = 1; break;
        case 1: boundary = 31; break;
        case 2: boundary = 32; break;
        case 3: boundary = 1023; break;
        case 4: boundary = 1024; break;
        case 5: boundary = count - 1; break;
        default: boundary = 1 + next_random(&state) % (count - 1); break;
        }
        ft_rrb_split_result split;
        REQUIRE_STATUS(ft_rrb_vector_split_at(&vector, boundary, &split), FT_STATUS_OK);
        ft_rrb_vector joined;
        REQUIRE_STATUS(ft_rrb_vector_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
        ft_rrb_vector_dispose(&vector);
        ft_rrb_vector_move(&vector, &joined);
        ft_rrb_vector_dispose(&split.left);
        ft_rrb_vector_dispose(&split.right);
        if (operation % 127 == 0) {
            ft_rrb_statistics interim;
            REQUIRE(ft_rrb_vector_validate(&vector, &interim));
        }
    }

    REQUIRE(vector_matches(&vector, values, count));
    ft_rrb_statistics statistics;
    REQUIRE(ft_rrb_vector_validate(&vector, &statistics));
    REQUIRE(statistics.height <= minimum_height(count) + 1);
    REQUIRE(statistics.leaf_count <= (count + 15) / 16);
    REQUIRE(statistics.branch_count < statistics.leaf_count);
    ft_rrb_vector_dispose(&vector);
    free(values);
}

static void test_builder_snapshots(void)
{
    ft_rrb_policy policy;
    init_int_policy(&policy);
    int* values = make_range(0, 2001);
    REQUIRE(values != NULL);

    ft_rrb_builder builder;
    REQUIRE_STATUS(ft_rrb_builder_init(&builder, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rrb_builder_append_range(&builder, values, 1000), FT_STATUS_OK);
    ft_rrb_vector first;
    ft_rrb_vector cached;
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&builder, &first), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&builder, &cached), FT_STATUS_OK);
    REQUIRE(ft_rrb_vector_shares_root(&first, &cached));
    REQUIRE(vector_matches(&first, values, 1000));
    ft_rrb_statistics statistics;
    REQUIRE(ft_rrb_vector_validate(&first, &statistics));
    REQUIRE(statistics.relaxed_branch_count == 0);

    REQUIRE_STATUS(ft_rrb_builder_append(&builder, values + 1000), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rrb_builder_append_range(&builder, values + 1001, 999), FT_STATUS_OK);
    ft_rrb_vector second;
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&builder, &second), FT_STATUS_OK);
    REQUIRE(vector_matches(&first, values, 1000));
    REQUIRE(vector_matches(&second, values, 2000));
    REQUIRE(ft_rrb_vector_validate(&second, &statistics));

    ft_rrb_builder adopted;
    REQUIRE_STATUS(ft_rrb_builder_init_from_vector(&adopted, &second), FT_STATUS_OK);
    ft_rrb_vector adopted_cached;
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&adopted, &adopted_cached), FT_STATUS_OK);
    REQUIRE(ft_rrb_vector_shares_root(&second, &adopted_cached));
    REQUIRE_STATUS(ft_rrb_builder_append(&adopted, values + 2000), FT_STATUS_OK);
    ft_rrb_vector third;
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&adopted, &third), FT_STATUS_OK);
    REQUIRE(vector_matches(&second, values, 2000));
    REQUIRE(vector_matches(&third, values, 2001));
    ft_rrb_builder_clear(&adopted);
    REQUIRE(ft_rrb_builder_size(&adopted) == 0);
    ft_rrb_vector empty;
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&adopted, &empty), FT_STATUS_OK);
    REQUIRE(ft_rrb_vector_empty(&empty));

    ft_rrb_vector_dispose(&empty);
    ft_rrb_vector_dispose(&third);
    ft_rrb_vector_dispose(&adopted_cached);
    ft_rrb_builder_dispose(&adopted);
    ft_rrb_vector_dispose(&second);
    ft_rrb_vector_dispose(&cached);
    ft_rrb_vector_dispose(&first);
    ft_rrb_builder_dispose(&builder);
    free(values);
}

typedef struct tracked_value_context {
    size_t copies;
    size_t destroys;
    size_t live;
} tracked_value_context;

typedef struct tracked_value {
    int value;
} tracked_value;

static void tracked_copy(void* destination, const void* source, void* context)
{
    tracked_value_context* tracker = (tracked_value_context*)context;
    *(tracked_value*)destination = *(const tracked_value*)source;
    ++tracker->copies;
    ++tracker->live;
}

static void tracked_destroy(void* value, void* context)
{
    tracked_value_context* tracker = (tracked_value_context*)context;
    (void)value;
    ++tracker->destroys;
    --tracker->live;
}

static bool tracked_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return ((const tracked_value*)left)->value == ((const tracked_value*)right)->value;
}

static void test_policy_lifetimes_and_compatibility(void)
{
    tracked_value_context tracker = { 0, 0, 0 };
    ft_value_type type;
    ft_value_type_init(&type, sizeof(tracked_value));
    type.copy = tracked_copy;
    type.destroy = tracked_destroy;
    type.context = &tracker;
    ft_rrb_policy policy;
    ft_rrb_policy_init(&policy, &type, tracked_equal, NULL);

    tracked_value values[100];
    for (int index = 0; index != 100; ++index) {
        values[index].value = index;
    }
    ft_rrb_vector vector;
    REQUIRE_STATUS(ft_rrb_vector_from_array(&vector, &policy, values, 100), FT_STATUS_OK);
    ft_rrb_vector snapshot;
    REQUIRE_STATUS(ft_rrb_vector_copy(&vector, &snapshot), FT_STATUS_OK);
    ft_rrb_split_result split;
    REQUIRE_STATUS(ft_rrb_vector_split_at(&vector, 37, &split), FT_STATUS_OK);
    ft_rrb_vector joined;
    REQUIRE_STATUS(ft_rrb_vector_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
    tracked_value replacement = { -1 };
    ft_rrb_vector changed;
    REQUIRE_STATUS(ft_rrb_vector_set(&joined, 50, &replacement, &changed), FT_STATUS_OK);
    REQUIRE(tracker.live > 0);

    ft_rrb_policy other_policy;
    ft_rrb_policy_init(&other_policy, &type, tracked_equal, NULL);
    ft_rrb_vector other;
    REQUIRE_STATUS(ft_rrb_vector_init(&other, &other_policy), FT_STATUS_OK);
    ft_rrb_vector invalid;
    REQUIRE_STATUS(ft_rrb_vector_concat(&vector, &other, &invalid), FT_STATUS_INVALID_ARGUMENT);

    ft_rrb_vector_dispose(&other);
    ft_rrb_vector_dispose(&changed);
    ft_rrb_vector_dispose(&joined);
    ft_rrb_vector_dispose(&split.left);
    ft_rrb_vector_dispose(&split.right);
    ft_rrb_vector_dispose(&snapshot);
    ft_rrb_vector_dispose(&vector);
    REQUIRE(tracker.live == 0);
    REQUIRE(tracker.copies == tracker.destroys);
}

typedef struct failing_allocator {
    size_t attempts;
    size_t fail_after;
    size_t live;
} failing_allocator;

static void* failing_allocate(size_t size, void* context)
{
    failing_allocator* allocator = (failing_allocator*)context;
    if (allocator->attempts++ == allocator->fail_after) {
        return NULL;
    }
    void* allocation = malloc(size == 0 ? 1 : size);
    if (allocation != NULL) {
        ++allocator->live;
    }
    return allocation;
}

static void failing_deallocate(void* allocation, void* context)
{
    failing_allocator* allocator = (failing_allocator*)context;
    if (allocation != NULL) {
        --allocator->live;
        free(allocation);
    }
}

static void configure_failing_allocator(ft_rrb_policy* policy, failing_allocator* allocator)
{
    policy->allocator.allocate = failing_allocate;
    policy->allocator.deallocate = failing_deallocate;
    policy->allocator.context = allocator;
}

static void test_allocation_failure_rollback(void)
{
    int values[256];
    for (int index = 0; index != 256; ++index) {
        values[index] = index;
    }
    failing_allocator allocator = { 0, SIZE_MAX, 0 };
    ft_rrb_policy policy;
    init_int_policy(&policy);
    configure_failing_allocator(&policy, &allocator);

    bool succeeded = false;
    for (size_t failure = 0; failure != 128 && !succeeded; ++failure) {
        allocator.attempts = 0;
        allocator.fail_after = failure;
        ft_rrb_vector candidate;
        const ft_status status = ft_rrb_vector_from_array(&candidate, &policy, values, 256);
        if (status == FT_STATUS_OK) {
            succeeded = true;
            ft_rrb_vector_dispose(&candidate);
        } else {
            REQUIRE(status == FT_STATUS_NO_MEMORY);
        }
        REQUIRE(allocator.live == 0);
    }
    REQUIRE(succeeded);

    allocator.fail_after = SIZE_MAX;
    allocator.attempts = 0;
    ft_rrb_vector baseline;
    REQUIRE_STATUS(ft_rrb_vector_from_array(&baseline, &policy, values, 256), FT_STATUS_OK);
    const size_t baseline_live = allocator.live;
    succeeded = false;
    for (size_t failure = 0; failure != 64 && !succeeded; ++failure) {
        allocator.attempts = 0;
        allocator.fail_after = failure;
        int replacement = -1;
        ft_rrb_vector candidate = { NULL, NULL };
        const ft_status status = ft_rrb_vector_set(&baseline, 127, &replacement, &candidate);
        if (status == FT_STATUS_OK) {
            succeeded = true;
            ft_rrb_vector_dispose(&candidate);
        } else {
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(candidate.root == NULL);
        }
        REQUIRE(vector_matches(&baseline, values, 256));
        REQUIRE(allocator.live == baseline_live);
    }
    REQUIRE(succeeded);

    allocator.fail_after = SIZE_MAX;
    allocator.attempts = 0;
    ft_rrb_builder builder;
    REQUIRE_STATUS(ft_rrb_builder_init(&builder, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rrb_builder_append_range(&builder, values, 10), FT_STATUS_OK);
    const size_t builder_live = allocator.live;
    succeeded = false;
    for (size_t failure = 0; failure != 128 && !succeeded; ++failure) {
        allocator.attempts = 0;
        allocator.fail_after = failure;
        const ft_status status = ft_rrb_builder_append_range(&builder, values + 10, 100);
        if (status == FT_STATUS_OK) {
            succeeded = true;
        } else {
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(ft_rrb_builder_size(&builder) == 10);
            REQUIRE(allocator.live == builder_live);
        }
    }
    REQUIRE(succeeded);

    allocator.fail_after = SIZE_MAX;
    ft_rrb_vector built;
    REQUIRE_STATUS(ft_rrb_builder_to_vector(&builder, &built), FT_STATUS_OK);
    REQUIRE(vector_matches(&built, values, 110));
    ft_rrb_vector_dispose(&built);
    ft_rrb_builder_dispose(&builder);
    ft_rrb_vector_dispose(&baseline);
    REQUIRE(allocator.live == 0);
}

typedef struct concurrent_context {
    const ft_rrb_vector* vector;
    int failures;
} concurrent_context;

static void concurrent_failure(concurrent_context* context)
{
#ifdef _WIN32
    (void)InterlockedIncrement((volatile LONG*)&context->failures);
#elif defined(RRB_TEST_HAS_THREADS)
    /* The test only needs a race-free failure flag; the cast keeps the public
     * context layout independent of whether threads.h is available. */
    (void)__atomic_fetch_add(&context->failures, 1, __ATOMIC_RELAXED);
#else
    ++context->failures;
#endif
}

static void concurrent_worker(concurrent_context* context)
{
    for (size_t iteration = 0; iteration != 300; ++iteration) {
        ft_rrb_vector snapshot;
        if (ft_rrb_vector_copy(context->vector, &snapshot) != FT_STATUS_OK) {
            concurrent_failure(context);
            return;
        }
        const size_t index = iteration * 37 % 10000;
        int value = -1;
        ft_rrb_statistics statistics;
        if (ft_rrb_vector_at(&snapshot, index, &value) != FT_STATUS_OK ||
            value != (int)index ||
            !ft_rrb_vector_validate(&snapshot, &statistics) ||
            statistics.count != 10000) {
            concurrent_failure(context);
        }
        ft_rrb_vector_dispose(&snapshot);
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#elif defined(RRB_TEST_HAS_THREADS)
static int concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#endif

static void test_concurrent_readers(void)
{
    ft_rrb_policy policy;
    init_int_policy(&policy);
    int* values = make_range(0, 10000);
    REQUIRE(values != NULL);
    ft_rrb_vector vector;
    REQUIRE_STATUS(ft_rrb_vector_from_array(&vector, &policy, values, 10000), FT_STATUS_OK);
    concurrent_context context = { &vector, 0 };

#ifdef _WIN32
    enum { thread_count = 8 };
    HANDLE threads[thread_count];
    for (DWORD index = 0; index != thread_count; ++index) {
        threads[index] = CreateThread(NULL, 0, concurrent_thread, &context, 0, NULL);
        REQUIRE(threads[index] != NULL);
    }
    REQUIRE(WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (DWORD index = 0; index != thread_count; ++index) {
        CloseHandle(threads[index]);
    }
#elif defined(RRB_TEST_HAS_THREADS)
    enum { thread_count = 8 };
    thrd_t threads[thread_count];
    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(thrd_create(&threads[index], concurrent_thread, &context) == thrd_success);
    }
    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(thrd_join(threads[index], NULL) == thrd_success);
    }
#else
    for (int index = 0; index != 8; ++index) {
        concurrent_worker(&context);
    }
#endif

    REQUIRE(context.failures == 0);
    REQUIRE(vector_matches(&vector, values, 10000));
    ft_rrb_vector_dispose(&vector);
    free(values);
}

static void test_persistent_cursor(void)
{
    ft_rrb_policy policy;
    init_int_policy(&policy);
    int* values = make_range(0, 96);
    REQUIRE(values != NULL);
    const int inserted_values[] = {500, 501, 502};
    ft_rrb_vector basis = {0};
    ft_rrb_vector inserted = {0};
    REQUIRE_STATUS(ft_rrb_vector_from_array(&basis, &policy, values, 96), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_rrb_vector_from_array(&inserted, &policy, inserted_values, 3),
        FT_STATUS_OK);

    ft_rrb_vector_cursor cursor = {0};
    ft_rrb_vector_cursor edited = {0};
    REQUIRE_STATUS(ft_rrb_vector_get_cursor(&basis, 32, &cursor), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rrb_vector_cursor_insert_vector(&cursor, &inserted, &edited), FT_STATUS_OK);
    REQUIRE(ft_rrb_vector_cursor_position(&edited) == 35);
    REQUIRE(ft_rrb_vector_cursor_size(&edited) == 99);
    const int seam[] = {30, 31, 500, 501, 502, 32, 33};
    for (size_t offset = 0; offset != 7; ++offset) {
        int actual = 0;
        REQUIRE_STATUS(
            ft_rrb_vector_at(&edited.vector, 30 + offset, &actual),
            FT_STATUS_OK);
        REQUIRE(actual == seam[offset]);
    }
    REQUIRE(vector_matches(&basis, values, 96));

    bool found = false;
    int peeked = -1;
    REQUIRE_STATUS(
        ft_rrb_vector_cursor_try_peek_previous(&edited, &found, &peeked),
        FT_STATUS_OK);
    REQUIRE(found && peeked == 502);
    REQUIRE_STATUS(
        ft_rrb_vector_cursor_try_peek_next(&edited, &found, &peeked),
        FT_STATUS_OK);
    REQUIRE(found && peeked == 32);

    ft_rrb_vector_cursor removed = {0};
    ft_rrb_vector_cursor replaced = {0};
    REQUIRE_STATUS(ft_rrb_vector_cursor_delete_previous(&edited, &removed), FT_STATUS_OK);
    const int replacement = 900;
    REQUIRE_STATUS(
        ft_rrb_vector_cursor_replace_next(&removed, &replacement, &replaced),
        FT_STATUS_OK);
    REQUIRE(ft_rrb_vector_cursor_position(&replaced) == 34);
    REQUIRE_STATUS(
        ft_rrb_vector_at(&replaced.vector, 34, &peeked),
        FT_STATUS_OK);
    REQUIRE(peeked == replacement);
    REQUIRE_STATUS(
        ft_rrb_vector_get_cursor(&basis, 97, &removed),
        FT_STATUS_OUT_OF_RANGE);

    ft_rrb_vector_cursor_dispose(&replaced);
    ft_rrb_vector_cursor_dispose(&removed);
    ft_rrb_vector_cursor_dispose(&edited);
    ft_rrb_vector_cursor_dispose(&cursor);
    ft_rrb_vector_dispose(&inserted);
    ft_rrb_vector_dispose(&basis);
    free(values);
}

static void run_test(const char* name, void (*test)(void))
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

    run_test("RRB boundaries and unequal concat", test_boundaries_and_unequal_concat);
    run_test("RRB exact sharing and relaxed layout", test_exact_sharing_and_relaxed_layout);
    run_test("RRB 10k randomized model and aliasing", test_randomized_model_and_aliasing);
    run_test("RRB adversarial density and height", test_adversarial_density_and_height);
    run_test("RRB builder snapshots", test_builder_snapshots);
    run_test("RRB policy lifetimes and compatibility", test_policy_lifetimes_and_compatibility);
    run_test("RRB allocation rollback", test_allocation_failure_rollback);
    run_test("RRB concurrent readers", test_concurrent_readers);
    run_test("RRB persistent cursor", test_persistent_cursor);

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C RRB vector tests passed\n");
    return EXIT_SUCCESS;
}
