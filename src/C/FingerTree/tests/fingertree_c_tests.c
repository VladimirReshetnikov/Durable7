#include <tools/data_structures/finger_tree/fingertree.h>
#include <tools/data_structures/test_support/headless_test_process.h>

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
typedef volatile LONG test_atomic_long;

static void test_atomic_long_init(test_atomic_long* value, long initial_value)
{
    *value = initial_value;
}

static void test_atomic_long_increment(test_atomic_long* value)
{
    (void)InterlockedIncrement(value);
}

static long test_atomic_long_read(test_atomic_long* value)
{
    return InterlockedCompareExchange(value, 0, 0);
}
#else
#include <stdatomic.h>
typedef atomic_long test_atomic_long;

static void test_atomic_long_init(test_atomic_long* value, long initial_value)
{
    atomic_init(value, initial_value);
}

static void test_atomic_long_increment(test_atomic_long* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static long test_atomic_long_read(test_atomic_long* value)
{
    return atomic_load_explicit(value, memory_order_relaxed);
}

/* Non-Windows builds race the snapshot workers with C11 threads when the
 * implementation provides them; without threads.h the concurrency test
 * degrades to a sequential functional check. */
#if !defined(__STDC_NO_THREADS__)
#define TEST_HAS_C11_THREADS 1
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
            (void)fprintf(stderr, "%s:%d: %s returned %d, expected %d\n", __FILE__, __LINE__, #expression, actual_status__, (expected)); \
            ++g_failures; \
            return; \
        } \
    } while (0)

typedef struct int_buffer {
    int values[128];
    size_t count;
} int_buffer;

typedef struct char_buffer {
    char values[128];
    size_t count;
} char_buffer;

typedef struct char_span_buffer {
    char* values;
    size_t count;
    size_t capacity;
} char_span_buffer;

typedef struct map_buffer {
    int keys[128];
    int values[128];
    size_t count;
} map_buffer;

typedef struct int_summary {
    long long sum;
    size_t count;
} int_summary;

typedef struct structural_costs {
    size_t value_copies;
    size_t measure_combines;
} structural_costs;

static void counted_int_copy(void* destination, const void* source, void* context)
{
    structural_costs* costs = (structural_costs*)context;
    ++costs->value_copies;
    *(int*)destination = *(const int*)source;
}

static void counted_size_identity(void* destination, void* context)
{
    (void)context;
    *(size_t*)destination = 0;
}

static void counted_size_measure(void* destination, const void* value, void* context)
{
    (void)value;
    (void)context;
    *(size_t*)destination = 1;
}

static void counted_size_combine(void* destination, const void* left, const void* right, void* context)
{
    structural_costs* costs = (structural_costs*)context;
    ++costs->measure_combines;
    *(size_t*)destination = *(const size_t*)left + *(const size_t*)right;
}

static void init_counted_int_policy(ft_tree_policy* policy, structural_costs* costs)
{
    ft_value_type_init(&policy->value, sizeof(int));
    policy->value.copy = counted_int_copy;
    policy->value.context = costs;
    policy->measure.size = sizeof(size_t);
    policy->measure.identity = counted_size_identity;
    policy->measure.measure = counted_size_measure;
    policy->measure.combine = counted_size_combine;
    policy->measure.context = costs;
}

static void init_int_policy(ft_tree_policy* policy)
{
    ft_value_type value_type;
    ft_value_type_init(&value_type, sizeof(int));
    ft_tree_policy_init_size(policy, &value_type);
}

static int compare_ints(const void* left, const void* right, void* context)
{
    (void)context;
    const int left_value = *(const int*)left;
    const int right_value = *(const int*)right;
    return (left_value > right_value) - (left_value < right_value);
}

typedef struct tagged_int {
    int key;
    char representative;
} tagged_int;

static int compare_tagged_ints(const void* left, const void* right, void* context)
{
    (void)context;
    const tagged_int* left_value = (const tagged_int*)left;
    const tagged_int* right_value = (const tagged_int*)right;
    return (left_value->key > right_value->key) - (left_value->key < right_value->key);
}

typedef struct comparison_counter {
    size_t comparisons;
} comparison_counter;

static int compare_ints_counted(const void* left, const void* right, void* context)
{
    comparison_counter* counter = (comparison_counter*)context;
    ++counter->comparisons;
    return compare_ints(left, right, NULL);
}

static void int_sum_identity(void* destination, void* context)
{
    (void)context;
    *(int*)destination = 0;
}

static void int_sum_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(int*)destination = *(const int*)value;
}

static void int_sum_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    *(int*)destination = *(const int*)left + *(const int*)right;
}

static void init_int_sum_measure(ft_measure_policy* policy)
{
    policy->size = sizeof(int);
    policy->identity = int_sum_identity;
    policy->measure = int_sum_measure;
    policy->combine = int_sum_combine;
    policy->context = NULL;
}

typedef struct ordered_measure {
    uint64_t hash;
    size_t count;
} ordered_measure;

static uint64_t ordered_measure_power(size_t exponent)
{
    uint64_t result = 1;
    for (size_t index = 0; index != exponent; ++index) {
        result *= UINT64_C(1099511628211);
    }
    return result;
}

static void ordered_measure_identity(void* destination, void* context)
{
    (void)context;
    ordered_measure* result = (ordered_measure*)destination;
    result->hash = 0;
    result->count = 0;
}

static void ordered_measure_value(void* destination, const void* value, void* context)
{
    (void)context;
    ordered_measure* result = (ordered_measure*)destination;
    result->hash = (uint64_t)(uint32_t)*(const int*)value + 1u;
    result->count = 1;
}

static void ordered_measure_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    const ordered_measure* left_measure = (const ordered_measure*)left;
    const ordered_measure* right_measure = (const ordered_measure*)right;
    ordered_measure* result = (ordered_measure*)destination;
    result->hash = left_measure->hash * ordered_measure_power(right_measure->count) + right_measure->hash;
    result->count = left_measure->count + right_measure->count;
}

static void init_ordered_measure(ft_measure_policy* policy)
{
    policy->size = sizeof(ordered_measure);
    policy->identity = ordered_measure_identity;
    policy->measure = ordered_measure_value;
    policy->combine = ordered_measure_combine;
    policy->context = NULL;
}

static ordered_measure ordered_measure_for_values(const int* values, size_t count)
{
    ordered_measure result = {0, 0};
    for (size_t index = 0; index != count; ++index) {
        const ordered_measure next = {(uint64_t)(uint32_t)values[index] + 1u, 1};
        ordered_measure combined;
        ordered_measure_combine(&combined, &result, &next, NULL);
        result = combined;
    }
    return result;
}

static void collect_int(const void* value, void* context)
{
    int_buffer* buffer = (int_buffer*)context;
    buffer->values[buffer->count] = *(const int*)value;
    ++buffer->count;
}

static ft_status reversible_deque_from_range(
    const ft_tree_policy* policy,
    int start,
    int count,
    ft_reversible_deque* result)
{
    ft_status status = ft_reversible_deque_init(result, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }

    for (int offset = 0; offset != count; ++offset) {
        const int value = start + offset;
        ft_reversible_deque next;
        status = ft_reversible_deque_push_back(result, &value, &next);
        if (status != FT_STATUS_OK) {
            ft_reversible_deque_dispose(result);
            return status;
        }

        ft_reversible_deque_dispose(result);
        *result = next;
    }

    return FT_STATUS_OK;
}

static bool reversible_deque_matches(const ft_reversible_deque* deque, const int* expected, size_t count)
{
    if (ft_reversible_deque_size(deque) != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        int actual = 0;
        if (ft_reversible_deque_at(deque, index, &actual) != FT_STATUS_OK || actual != expected[index]) {
            return false;
        }
    }

    return true;
}

static bool int_buffer_matches(const int_buffer* buffer, const int* expected, size_t count)
{
    if (buffer->count != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        if (buffer->values[index] != expected[index]) {
            return false;
        }
    }

    return true;
}

static bool rope_matches(const ft_rope* rope, const int* expected, size_t count)
{
    size_t actual_count = 0;
    if (ft_rope_try_size(rope, &actual_count) != FT_STATUS_OK || actual_count != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        int actual = 0;
        if (ft_rope_at(rope, index, &actual) != FT_STATUS_OK || actual != expected[index]) {
            return false;
        }
    }

    return true;
}

static bool measured_rope_matches(const ft_measured_rope* rope, const int* expected, size_t count)
{
    size_t actual_count = 0;
    if (ft_measured_rope_try_size(rope, &actual_count) != FT_STATUS_OK || actual_count != count) {
        return false;
    }

    int expected_sum = 0;
    for (size_t index = 0; index != count; ++index) {
        int actual = 0;
        if (ft_measured_rope_at(rope, index, &actual) != FT_STATUS_OK || actual != expected[index]) {
            return false;
        }
        expected_sum += expected[index];
    }

    int actual_sum = 0;
    return ft_measured_rope_measure(rope, &actual_sum) == FT_STATUS_OK && actual_sum == expected_sum;
}

static void collect_char(const void* value, void* context)
{
    char_buffer* buffer = (char_buffer*)context;
    buffer->values[buffer->count] = *(const char*)value;
    ++buffer->count;
}

static void collect_char_span(const void* value, void* context)
{
    char_span_buffer* buffer = (char_span_buffer*)context;
    if (buffer->count < buffer->capacity) {
        buffer->values[buffer->count] = *(const char*)value;
    }
    ++buffer->count;
}

static void collect_map_entry(const void* key, const void* value, void* context)
{
    map_buffer* buffer = (map_buffer*)context;
    buffer->keys[buffer->count] = *(const int*)key;
    buffer->values[buffer->count] = *(const int*)value;
    ++buffer->count;
}

static void summarize_int(const void* value, void* context)
{
    int_summary* summary = (int_summary*)context;
    summary->sum += *(const int*)value;
    ++summary->count;
}

static bool size_reaches(const void* measure, void* context)
{
    const size_t value = *(const size_t*)measure;
    const size_t threshold = *(const size_t*)context;
    return value >= threshold;
}

static bool int_sum_reaches(const void* measure, void* context)
{
    return *(const int*)measure >= *(const int*)context;
}

static size_t model_line_count(const char* text, size_t length)
{
    size_t count = 1;
    for (size_t index = 0; index != length; ++index) {
        if (text[index] == '\n') {
            ++count;
        }
    }

    return count;
}

static ft_line_column model_line_column_of(const char* text, size_t offset)
{
    ft_line_column result;
    result.line = 0;
    result.column = 0;
    for (size_t index = 0; index != offset; ++index) {
        if (text[index] == '\n') {
            ++result.line;
            result.column = 0;
        } else {
            ++result.column;
        }
    }

    return result;
}

static void model_insert_char(char* text, size_t* length, size_t index, char value)
{
    memmove(text + index + 1, text + index, *length - index + 1);
    text[index] = value;
    *length += 1;
}

static void model_remove_at(char* text, size_t* length, size_t index)
{
    memmove(text + index, text + index + 1, *length - index);
    *length -= 1;
}

static bool text_rope_matches_model(const ft_text_rope* rope, const char* model, size_t length)
{
    if (ft_text_rope_size(rope) != length || ft_text_rope_line_count(rope) != model_line_count(model, length)) {
        return false;
    }

    size_t try_size = 0;
    size_t try_lines = 0;
    if (ft_text_rope_try_size(rope, &try_size) != FT_STATUS_OK || try_size != length ||
        ft_text_rope_try_line_count(rope, &try_lines) != FT_STATUS_OK ||
        try_lines != model_line_count(model, length)) {
        return false;
    }

    for (size_t index = 0; index != length; ++index) {
        char actual = '\0';
        if (ft_text_rope_at(rope, index, &actual) != FT_STATUS_OK || actual != model[index]) {
            return false;
        }
    }

    char visited[8192];
    char_span_buffer buffer;
    buffer.values = visited;
    buffer.count = 0;
    buffer.capacity = sizeof(visited);
    if (ft_text_rope_visit(rope, collect_char_span, &buffer) != FT_STATUS_OK ||
        buffer.count != length ||
        memcmp(visited, model, length) != 0) {
        return false;
    }

    for (size_t offset = 0; offset <= length; offset += 37) {
        ft_line_column actual;
        if (ft_text_rope_line_column_of(rope, offset, &actual) != FT_STATUS_OK) {
            return false;
        }

        const ft_line_column expected = model_line_column_of(model, offset);
        if (actual.line != expected.line || actual.column != expected.column) {
            return false;
        }
    }

    if (length % 37 != 0) {
        ft_line_column actual;
        if (ft_text_rope_line_column_of(rope, length, &actual) != FT_STATUS_OK) {
            return false;
        }

        const ft_line_column expected = model_line_column_of(model, length);
        if (actual.line != expected.line || actual.column != expected.column) {
            return false;
        }
    }

    return true;
}

typedef struct concurrent_tree_context {
    const ft_tree* tree;
    int iterations;
    test_atomic_long failures;
} concurrent_tree_context;

static void concurrent_tree_worker(concurrent_tree_context* context)
{
    for (int iteration = 0; iteration != context->iterations; ++iteration) {
        ft_tree snapshot;
        if (ft_tree_copy(context->tree, &snapshot) != FT_STATUS_OK) {
            test_atomic_long_increment(&context->failures);
            return;
        }

        int front = -1;
        int back = -1;
        int middle = -1;
        if (ft_tree_front(&snapshot, &front) != FT_STATUS_OK ||
            ft_tree_back(&snapshot, &back) != FT_STATUS_OK ||
            ft_tree_at(&snapshot, 500, &middle) != FT_STATUS_OK ||
            front != 0 ||
            back != 999 ||
            middle != 500) {
            test_atomic_long_increment(&context->failures);
            ft_tree_dispose(&snapshot);
            return;
        }

        const int extra = -iteration - 1;
        ft_tree updated;
        if (ft_tree_push_front(&snapshot, &extra, &updated) != FT_STATUS_OK) {
            test_atomic_long_increment(&context->failures);
            ft_tree_dispose(&snapshot);
            return;
        }

        int updated_front = 0;
        if (ft_tree_front(&updated, &updated_front) != FT_STATUS_OK || updated_front != extra) {
            test_atomic_long_increment(&context->failures);
            ft_tree_dispose(&updated);
            ft_tree_dispose(&snapshot);
            return;
        }

        ft_tree_dispose(&updated);
        ft_tree_dispose(&snapshot);
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_tree_thread_proc(void* parameter)
{
    concurrent_tree_worker((concurrent_tree_context*)parameter);
    return 0;
}
#elif defined(TEST_HAS_C11_THREADS)
static int concurrent_tree_thread_main(void* parameter)
{
    concurrent_tree_worker((concurrent_tree_context*)parameter);
    return 0;
}
#endif

static void test_concurrent_snapshot_refcounts(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_tree tree;
    REQUIRE_STATUS(ft_tree_init(&tree, &policy), FT_STATUS_OK);
    for (int value = 0; value != 1000; ++value) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&tree, &value, &next), FT_STATUS_OK);
        ft_tree_dispose(&tree);
        tree = next;
    }

    concurrent_tree_context context;
    context.tree = &tree;
    context.iterations = 400;
    test_atomic_long_init(&context.failures, 0);

#ifdef _WIN32
    enum { thread_count = 8 };
    HANDLE threads[thread_count];
    for (DWORD index = 0; index != thread_count; ++index) {
        threads[index] = CreateThread(NULL, 0, concurrent_tree_thread_proc, &context, 0, NULL);
        REQUIRE(threads[index] != NULL);
    }

    const DWORD wait_result = WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE);
    REQUIRE(wait_result == WAIT_OBJECT_0);
    for (DWORD index = 0; index != thread_count; ++index) {
        CloseHandle(threads[index]);
    }
#elif defined(TEST_HAS_C11_THREADS)
    enum { thread_count = 8 };
    thrd_t threads[thread_count];
    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(thrd_create(&threads[index], concurrent_tree_thread_main, &context) == thrd_success);
    }

    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(thrd_join(threads[index], NULL) == thrd_success);
    }
#else
    for (int index = 0; index != 8; ++index) {
        concurrent_tree_worker(&context);
    }
#endif

    REQUIRE(test_atomic_long_read(&context.failures) == 0);
    int front = -1;
    int back = -1;
    REQUIRE_STATUS(ft_tree_front(&tree, &front), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_back(&tree, &back), FT_STATUS_OK);
    REQUIRE(front == 0);
    REQUIRE(back == 999);
    ft_tree_dispose(&tree);
}

static void test_reversible_deque(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_reversible_deque deque;
    REQUIRE_STATUS(reversible_deque_from_range(&policy, 0, 5, &deque), FT_STATUS_OK);

    ft_reversible_deque reversed;
    REQUIRE_STATUS(ft_reversible_deque_reverse(&deque, &reversed), FT_STATUS_OK);
    const int reversed_expected[] = {4, 3, 2, 1, 0};
    REQUIRE(reversible_deque_matches(&reversed, reversed_expected, 5));

    int pushed = 9;
    ft_reversible_deque extended;
    REQUIRE_STATUS(ft_reversible_deque_push_front(&reversed, &pushed, &extended), FT_STATUS_OK);
    int front = -1;
    int back = -1;
    REQUIRE_STATUS(ft_reversible_deque_front(&extended, &front), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_back(&extended, &back), FT_STATUS_OK);
    REQUIRE(front == 9);
    REQUIRE(back == 0);

    int removed = -1;
    ft_reversible_deque trimmed;
    REQUIRE_STATUS(ft_reversible_deque_pop_back(&extended, &removed, &trimmed), FT_STATUS_OK);
    REQUIRE(removed == 0);
    REQUIRE(ft_reversible_deque_size(&trimmed) == 5);
    REQUIRE(ft_reversible_deque_size(&reversed) == 5);

    ft_reversible_deque left;
    ft_reversible_deque right;
    ft_reversible_deque reversed_left;
    ft_reversible_deque reversed_right;
    REQUIRE_STATUS(reversible_deque_from_range(&policy, 0, 4, &left), FT_STATUS_OK);
    REQUIRE_STATUS(reversible_deque_from_range(&policy, 10, 3, &right), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_reverse(&left, &reversed_left), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_reverse(&right, &reversed_right), FT_STATUS_OK);

    ft_reversible_deque ff;
    ft_reversible_deque rf;
    ft_reversible_deque fr;
    ft_reversible_deque rr;
    REQUIRE_STATUS(ft_reversible_deque_concat(&left, &right, &ff), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_concat(&reversed_left, &right, &rf), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_concat(&left, &reversed_right, &fr), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_concat(&reversed_left, &reversed_right, &rr), FT_STATUS_OK);

    const int ff_expected[] = {0, 1, 2, 3, 10, 11, 12};
    const int rf_expected[] = {3, 2, 1, 0, 10, 11, 12};
    const int fr_expected[] = {0, 1, 2, 3, 12, 11, 10};
    const int rr_expected[] = {3, 2, 1, 0, 12, 11, 10};
    REQUIRE(reversible_deque_matches(&ff, ff_expected, 7));
    REQUIRE(reversible_deque_matches(&rf, rf_expected, 7));
    REQUIRE(reversible_deque_matches(&fr, fr_expected, 7));
    REQUIRE(reversible_deque_matches(&rr, rr_expected, 7));

    int_buffer visit_buffer = {0};
    REQUIRE_STATUS(ft_reversible_deque_visit(&rr, collect_int, &visit_buffer), FT_STATUS_OK);
    REQUIRE(int_buffer_matches(&visit_buffer, rr_expected, 7));

    ft_reversible_deque_split_result split;
    REQUIRE_STATUS(ft_reversible_deque_split_at(&rr, 4, &split), FT_STATUS_OK);
    const int split_left_expected[] = {3, 2, 1, 0};
    const int split_right_expected[] = {12, 11, 10};
    REQUIRE(reversible_deque_matches(&split.left, split_left_expected, 4));
    REQUIRE(reversible_deque_matches(&split.right, split_right_expected, 3));

    ft_reversible_deque rejoined;
    REQUIRE_STATUS(ft_reversible_deque_concat(&split.left, &split.right, &rejoined), FT_STATUS_OK);
    REQUIRE(reversible_deque_matches(&rejoined, rr_expected, 7));

    ft_reversible_deque large_left;
    ft_reversible_deque large_right;
    ft_reversible_deque large_reversed_left;
    ft_reversible_deque large_reversed_right;
    ft_reversible_deque large_joined;
    ft_reversible_deque_split_result large_split;
    REQUIRE_STATUS(reversible_deque_from_range(&policy, 0, 40, &large_left), FT_STATUS_OK);
    REQUIRE_STATUS(reversible_deque_from_range(&policy, 100, 35, &large_right), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_reverse(&large_left, &large_reversed_left), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_reverse(&large_right, &large_reversed_right), FT_STATUS_OK);
    REQUIRE_STATUS(ft_reversible_deque_concat(&large_reversed_left, &large_reversed_right, &large_joined), FT_STATUS_OK);
    REQUIRE(ft_reversible_deque_size(&large_joined) == 75);
    for (size_t index = 0; index != 40; ++index) {
        int actual = 0;
        REQUIRE_STATUS(ft_reversible_deque_at(&large_joined, index, &actual), FT_STATUS_OK);
        REQUIRE(actual == 39 - (int)index);
    }

    for (size_t index = 0; index != 35; ++index) {
        int actual = 0;
        REQUIRE_STATUS(ft_reversible_deque_at(&large_joined, 40 + index, &actual), FT_STATUS_OK);
        REQUIRE(actual == 134 - (int)index);
    }

    REQUIRE_STATUS(ft_reversible_deque_split_at(&large_joined, 40, &large_split), FT_STATUS_OK);
    REQUIRE(ft_reversible_deque_size(&large_split.left) == 40);
    REQUIRE(ft_reversible_deque_size(&large_split.right) == 35);
    for (size_t index = 0; index != 40; ++index) {
        int actual = 0;
        REQUIRE_STATUS(ft_reversible_deque_at(&large_split.left, index, &actual), FT_STATUS_OK);
        REQUIRE(actual == 39 - (int)index);
    }

    for (size_t index = 0; index != 35; ++index) {
        int actual = 0;
        REQUIRE_STATUS(ft_reversible_deque_at(&large_split.right, index, &actual), FT_STATUS_OK);
        REQUIRE(actual == 134 - (int)index);
    }

    int ninety_nine = 99;
    ft_reversible_deque set;
    REQUIRE_STATUS(ft_reversible_deque_set_at(&reversed, 1, &ninety_nine, &set), FT_STATUS_OK);
    const int set_expected[] = {4, 99, 2, 1, 0};
    REQUIRE(reversible_deque_matches(&set, set_expected, 5));

    int seventy_seven = 77;
    ft_reversible_deque inserted;
    REQUIRE_STATUS(ft_reversible_deque_insert_at(&reversed, 2, &seventy_seven, &inserted), FT_STATUS_OK);
    const int inserted_expected[] = {4, 3, 77, 2, 1, 0};
    REQUIRE(reversible_deque_matches(&inserted, inserted_expected, 6));

    ft_reversible_deque removed_at;
    REQUIRE_STATUS(ft_reversible_deque_remove_at(&reversed, 3, &removed_at), FT_STATUS_OK);
    const int removed_at_expected[] = {4, 3, 2, 0};
    REQUIRE(reversible_deque_matches(&removed_at, removed_at_expected, 4));

    ft_reversible_deque_dispose(&removed_at);
    ft_reversible_deque_dispose(&inserted);
    ft_reversible_deque_dispose(&set);
    ft_reversible_deque_dispose(&large_split.left);
    ft_reversible_deque_dispose(&large_split.right);
    ft_reversible_deque_dispose(&large_joined);
    ft_reversible_deque_dispose(&large_reversed_right);
    ft_reversible_deque_dispose(&large_reversed_left);
    ft_reversible_deque_dispose(&large_right);
    ft_reversible_deque_dispose(&large_left);
    ft_reversible_deque_dispose(&rejoined);
    ft_reversible_deque_dispose(&split.left);
    ft_reversible_deque_dispose(&split.right);
    ft_reversible_deque_dispose(&rr);
    ft_reversible_deque_dispose(&fr);
    ft_reversible_deque_dispose(&rf);
    ft_reversible_deque_dispose(&ff);
    ft_reversible_deque_dispose(&reversed_right);
    ft_reversible_deque_dispose(&reversed_left);
    ft_reversible_deque_dispose(&right);
    ft_reversible_deque_dispose(&left);
    ft_reversible_deque_dispose(&trimmed);
    ft_reversible_deque_dispose(&extended);
    ft_reversible_deque_dispose(&reversed);
    ft_reversible_deque_dispose(&deque);
}

static void test_tree_endpoint_index_split_and_concat(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_tree tree;
    REQUIRE_STATUS(ft_tree_init(&tree, &policy), FT_STATUS_OK);

    ft_tree snapshot;
    bool has_snapshot = false;
    for (int value = 0; value != 24; ++value) {
        if (value == 6) {
            REQUIRE_STATUS(ft_tree_copy(&tree, &snapshot), FT_STATUS_OK);
            has_snapshot = true;
        }

        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&tree, &value, &next), FT_STATUS_OK);
        ft_tree_dispose(&tree);
        tree = next;
    }

    REQUIRE(has_snapshot);
    REQUIRE(ft_tree_size(&tree) == 24);
    REQUIRE(ft_tree_size(&snapshot) == 6);

    int front = -1;
    int back = -1;
    REQUIRE_STATUS(ft_tree_front(&tree, &front), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_back(&tree, &back), FT_STATUS_OK);
    REQUIRE(front == 0);
    REQUIRE(back == 23);

    for (int expected = 0; expected != 24; ++expected) {
        int actual = -1;
        REQUIRE_STATUS(ft_tree_at(&tree, (size_t)expected, &actual), FT_STATUS_OK);
        REQUIRE(actual == expected);
    }

    size_t measure = 0;
    REQUIRE_STATUS(ft_tree_measure(&tree, &measure), FT_STATUS_OK);
    REQUIRE(measure == 24);

    ft_tree_split_result split;
    REQUIRE_STATUS(ft_tree_split_at(&tree, 10, &split), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&split.left) == 10);
    REQUIRE(ft_tree_size(&split.right) == 14);

    ft_tree joined;
    REQUIRE_STATUS(ft_tree_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&joined) == 24);
    for (int expected = 0; expected != 24; ++expected) {
        int actual = -1;
        REQUIRE_STATUS(ft_tree_at(&joined, (size_t)expected, &actual), FT_STATUS_OK);
        REQUIRE(actual == expected);
    }

    int replacement = 111;
    ft_tree replaced;
    REQUIRE_STATUS(ft_tree_set_at(&joined, 11, &replacement, &replaced), FT_STATUS_OK);
    int replaced_value = -1;
    int original_value = -1;
    REQUIRE_STATUS(ft_tree_at(&replaced, 11, &replaced_value), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_at(&joined, 11, &original_value), FT_STATUS_OK);
    REQUIRE(replaced_value == 111);
    REQUIRE(original_value == 11);
    measure = 0;
    REQUIRE_STATUS(ft_tree_measure(&replaced, &measure), FT_STATUS_OK);
    REQUIRE(measure == 24);

    int removed_front = -1;
    ft_tree without_front;
    REQUIRE_STATUS(ft_tree_pop_front(&joined, &removed_front, &without_front), FT_STATUS_OK);
    REQUIRE(removed_front == 0);
    REQUIRE(ft_tree_size(&without_front) == 23);

    int removed_back = -1;
    ft_tree without_both;
    REQUIRE_STATUS(ft_tree_pop_back(&without_front, &removed_back, &without_both), FT_STATUS_OK);
    REQUIRE(removed_back == 23);
    REQUIRE(ft_tree_size(&without_both) == 22);
    int first_after = -1;
    int last_after = -1;
    REQUIRE_STATUS(ft_tree_front(&without_both, &first_after), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_back(&without_both, &last_after), FT_STATUS_OK);
    REQUIRE(first_after == 1);
    REQUIRE(last_after == 22);

    ft_tree_dispose(&without_both);
    ft_tree_dispose(&without_front);
    ft_tree_dispose(&replaced);
    ft_tree_dispose(&joined);
    ft_tree_dispose(&split.left);
    ft_tree_dispose(&split.right);
    ft_tree_dispose(&snapshot);
    ft_tree_dispose(&tree);
}

static void test_lazy_middle_force_paths(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_tree tree;
    REQUIRE_STATUS(ft_tree_init(&tree, &policy), FT_STATUS_OK);
    for (int value = 0; value != 96; ++value) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&tree, &value, &next), FT_STATUS_OK);
        ft_tree_dispose(&tree);
        tree = next;
    }

    size_t measure = 0;
    REQUIRE_STATUS(ft_tree_measure(&tree, &measure), FT_STATUS_OK);
    REQUIRE(measure == 96);

    int removed_front = -1;
    ft_tree popped_front;
    REQUIRE_STATUS(ft_tree_pop_front(&tree, &removed_front, &popped_front), FT_STATUS_OK);
    REQUIRE(removed_front == 0);
    REQUIRE(ft_tree_size(&popped_front) == 95);

    measure = 0;
    REQUIRE_STATUS(ft_tree_measure(&popped_front, &measure), FT_STATUS_OK);
    REQUIRE(measure == 95);

    int front = -1;
    int middle = -1;
    int back = -1;
    REQUIRE_STATUS(ft_tree_front(&popped_front, &front), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_at(&popped_front, 47, &middle), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_back(&popped_front, &back), FT_STATUS_OK);
    REQUIRE(front == 1);
    REQUIRE(middle == 48);
    REQUIRE(back == 95);

    int_summary summary = { 0, 0 };
    REQUIRE_STATUS(ft_tree_visit(&popped_front, summarize_int, &summary), FT_STATUS_OK);
    REQUIRE(summary.count == 95);
    REQUIRE(summary.sum == 4560);

    ft_tree_split_result split;
    REQUIRE_STATUS(ft_tree_split_at(&popped_front, 40, &split), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&split.left) == 40);
    REQUIRE(ft_tree_size(&split.right) == 55);

    ft_tree joined;
    REQUIRE_STATUS(ft_tree_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
    for (int expected = 1; expected != 96; ++expected) {
        int actual = -1;
        REQUIRE_STATUS(ft_tree_at(&joined, (size_t)(expected - 1), &actual), FT_STATUS_OK);
        REQUIRE(actual == expected);
    }

    int removed_back = -1;
    ft_tree popped_both;
    REQUIRE_STATUS(ft_tree_pop_back(&joined, &removed_back, &popped_both), FT_STATUS_OK);
    REQUIRE(removed_back == 95);
    measure = 0;
    REQUIRE_STATUS(ft_tree_measure(&popped_both, &measure), FT_STATUS_OK);
    REQUIRE(measure == 94);

    ft_tree_dispose(&popped_both);
    ft_tree_dispose(&joined);
    ft_tree_dispose(&split.left);
    ft_tree_dispose(&split.right);
    ft_tree_dispose(&popped_front);
    ft_tree_dispose(&tree);
}

static void test_measure_locate_and_split(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_tree tree;
    REQUIRE_STATUS(ft_tree_init(&tree, &policy), FT_STATUS_OK);
    for (int value = 0; value != 8; ++value) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&tree, &value, &next), FT_STATUS_OK);
        ft_tree_dispose(&tree);
        tree = next;
    }

    size_t threshold = 6;
    size_t measure_before = 999;
    int found_value = -1;
    bool found = false;
    REQUIRE_STATUS(ft_tree_locate(&tree, size_reaches, &threshold, &found, &measure_before, &found_value), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(measure_before == 5);
    REQUIRE(found_value == 5);

    threshold = 4;
    ft_tree left;
    ft_tree right;
    int split_value = -1;
    REQUIRE_STATUS(ft_tree_split(&tree, size_reaches, &threshold, &found, &left, &split_value, &right), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(split_value == 3);
    REQUIRE(ft_tree_size(&left) == 3);
    REQUIRE(ft_tree_size(&right) == 4);
    int first_right = -1;
    REQUIRE_STATUS(ft_tree_front(&right, &first_right), FT_STATUS_OK);
    REQUIRE(first_right == 4);

    threshold = 100;
    measure_before = 0;
    found_value = -1;
    REQUIRE_STATUS(ft_tree_locate(&tree, size_reaches, &threshold, &found, &measure_before, &found_value), FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE(measure_before == 8);
    REQUIRE(found_value == -1);

    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);
    ft_tree_policy sum_policy;
    sum_policy.value = int_type;
    sum_policy.measure = sum_measure;

    ft_tree sum_tree;
    REQUIRE_STATUS(ft_tree_init(&sum_tree, &sum_policy), FT_STATUS_OK);
    for (int value = 1; value != 6; ++value) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&sum_tree, &value, &next), FT_STATUS_OK);
        ft_tree_dispose(&sum_tree);
        sum_tree = next;
    }

    int sum = 0;
    REQUIRE_STATUS(ft_tree_measure(&sum_tree, &sum), FT_STATUS_OK);
    REQUIRE(sum == 15);
    int large = 30;
    ft_tree changed_sum;
    REQUIRE_STATUS(ft_tree_set_at(&sum_tree, 2, &large, &changed_sum), FT_STATUS_OK);
    sum = 0;
    REQUIRE_STATUS(ft_tree_measure(&changed_sum, &sum), FT_STATUS_OK);
    REQUIRE(sum == 42);
    int changed_value = -1;
    REQUIRE_STATUS(ft_tree_at(&changed_sum, 2, &changed_value), FT_STATUS_OK);
    REQUIRE(changed_value == 30);

    ft_tree_dispose(&changed_sum);
    ft_tree_dispose(&sum_tree);
    ft_tree_dispose(&left);
    ft_tree_dispose(&right);
    ft_tree_dispose(&tree);
}

static void test_structural_split_and_locate_costs(void)
{
    structural_costs costs = { 0, 0 };
    ft_tree_policy policy;
    init_counted_int_policy(&policy, &costs);

    ft_tree tree;
    REQUIRE_STATUS(ft_tree_init(&tree, &policy), FT_STATUS_OK);
    for (int value = 0; value != 4096; ++value) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&tree, &value, &next), FT_STATUS_OK);
        ft_tree_dispose(&tree);
        tree = next;
    }

    for (size_t index = 0; index <= 4096; index += 37) {
        ft_tree_split_result checked;
        REQUIRE_STATUS(ft_tree_split_at(&tree, index, &checked), FT_STATUS_OK);
        REQUIRE(ft_tree_size(&checked.left) == index);
        REQUIRE(ft_tree_size(&checked.right) == 4096 - index);
        if (index != 0) {
            int last_left = -1;
            REQUIRE_STATUS(ft_tree_back(&checked.left, &last_left), FT_STATUS_OK);
            REQUIRE(last_left == (int)index - 1);
        }

        if (index != 4096) {
            int first_right = -1;
            REQUIRE_STATUS(ft_tree_front(&checked.right, &first_right), FT_STATUS_OK);
            REQUIRE(first_right == (int)index);
        }

        ft_tree_dispose(&checked.left);
        ft_tree_dispose(&checked.right);
    }

    ft_tree_split_result at_end;
    REQUIRE_STATUS(ft_tree_split_at(&tree, 4096, &at_end), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&at_end.left) == 4096);
    REQUIRE(ft_tree_empty(&at_end.right));
    ft_tree_dispose(&at_end.left);
    ft_tree_dispose(&at_end.right);

    for (size_t expected = 1; expected <= 4096; expected += 31) {
        size_t threshold = expected;
        size_t before = SIZE_MAX;
        int actual = -1;
        bool found = false;
        REQUIRE_STATUS(ft_tree_locate(&tree, size_reaches, &threshold, &found, &before, &actual), FT_STATUS_OK);
        REQUIRE(found);
        REQUIRE(before == expected - 1);
        REQUIRE(actual == (int)expected - 1);
    }

    costs.value_copies = 0;
    costs.measure_combines = 0;
    ft_tree_split_result split;
    REQUIRE_STATUS(ft_tree_split_at(&tree, 3072, &split), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&split.left) == 3072);
    REQUIRE(ft_tree_size(&split.right) == 1024);
    REQUIRE(costs.value_copies < 64);
    REQUIRE(costs.measure_combines < 256);

    int boundary = -1;
    REQUIRE_STATUS(ft_tree_front(&split.right, &boundary), FT_STATUS_OK);
    REQUIRE(boundary == 3072);
    ft_tree_dispose(&split.left);
    ft_tree_dispose(&split.right);

    costs.value_copies = 0;
    costs.measure_combines = 0;
    size_t threshold = 3001;
    size_t measure_before = 0;
    int located = -1;
    bool found = false;
    REQUIRE_STATUS(
        ft_tree_locate(&tree, size_reaches, &threshold, &found, &measure_before, &located),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(measure_before == 3000);
    REQUIRE(located == 3000);
    REQUIRE(costs.value_copies == 1);
    REQUIRE(costs.measure_combines < 64);

    costs.value_copies = 0;
    costs.measure_combines = 0;
    ft_tree left;
    ft_tree right;
    int hit = -1;
    REQUIRE_STATUS(
        ft_tree_split(&tree, size_reaches, &threshold, &found, &left, &hit, &right),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(hit == 3000);
    REQUIRE(ft_tree_size(&left) == 3000);
    REQUIRE(ft_tree_size(&right) == 1095);
    REQUIRE(costs.value_copies < 64);
    REQUIRE(costs.measure_combines < 320);

    ft_tree_dispose(&left);
    ft_tree_dispose(&right);
    ft_tree_dispose(&tree);
}

static void test_sorted_set_and_multiset(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_sorted_set set;
    REQUIRE_STATUS(ft_sorted_set_init(&set, &policy, compare_ints, NULL), FT_STATUS_OK);

    const int inputs[] = {3, 1, 2, 2, 4};
    for (size_t index = 0; index != sizeof(inputs) / sizeof(inputs[0]); ++index) {
        ft_sorted_set next;
        REQUIRE_STATUS(ft_sorted_set_add(&set, &inputs[index], &next), FT_STATUS_OK);
        ft_sorted_set_dispose(&set);
        set = next;
    }

    REQUIRE(ft_sorted_set_size(&set) == 4);
    for (int expected = 1; expected != 5; ++expected) {
        int actual = -1;
        REQUIRE_STATUS(ft_sorted_set_at(&set, (size_t)(expected - 1), &actual), FT_STATUS_OK);
        REQUIRE(actual == expected);
        REQUIRE(ft_sorted_set_contains(&set, &expected));
    }

    int removed = 2;
    ft_sorted_set without_two;
    REQUIRE_STATUS(ft_sorted_set_remove(&set, &removed, &without_two), FT_STATUS_OK);
    REQUIRE(ft_sorted_set_size(&without_two) == 3);
    REQUIRE(!ft_sorted_set_contains(&without_two, &removed));
    REQUIRE(ft_sorted_set_contains(&set, &removed));

    ft_sorted_multiset bag;
    REQUIRE_STATUS(ft_sorted_multiset_init(&bag, &policy, compare_ints, NULL), FT_STATUS_OK);
    for (size_t index = 0; index != sizeof(inputs) / sizeof(inputs[0]); ++index) {
        ft_sorted_multiset next;
        REQUIRE_STATUS(ft_sorted_multiset_add(&bag, &inputs[index], &next), FT_STATUS_OK);
        ft_sorted_multiset_dispose(&bag);
        bag = next;
    }

    REQUIRE(ft_sorted_multiset_size(&bag) == 5);
    REQUIRE(ft_sorted_multiset_count_of(&bag, &removed) == 2);

    int_buffer buffer;
    buffer.count = 0;
    REQUIRE_STATUS(ft_sorted_multiset_visit(&bag, collect_int, &buffer), FT_STATUS_OK);
    REQUIRE(buffer.count == 5);
    REQUIRE(buffer.values[0] == 1);
    REQUIRE(buffer.values[1] == 2);
    REQUIRE(buffer.values[2] == 2);
    REQUIRE(buffer.values[3] == 3);
    REQUIRE(buffer.values[4] == 4);

    ft_sorted_multiset one_removed;
    REQUIRE_STATUS(ft_sorted_multiset_remove_one(&bag, &removed, &one_removed), FT_STATUS_OK);
    REQUIRE(ft_sorted_multiset_count_of(&one_removed, &removed) == 1);

    ft_sorted_multiset_dispose(&one_removed);
    ft_sorted_multiset_dispose(&bag);
    ft_sorted_set_dispose(&without_two);
    ft_sorted_set_dispose(&set);
}

static void test_sorted_facade_structural_bounds(void)
{
    structural_costs costs;
    costs.value_copies = 0;
    costs.measure_combines = 0;
    ft_tree_policy policy;
    init_counted_int_policy(&policy, &costs);

    comparison_counter counter;
    counter.comparisons = 0;
    ft_sorted_multiset bag;
    REQUIRE_STATUS(ft_sorted_multiset_init(&bag, &policy, compare_ints_counted, &counter), FT_STATUS_OK);
    for (int value = 0; value != 4096; ++value) {
        ft_sorted_multiset next;
        REQUIRE_STATUS(ft_sorted_multiset_add(&bag, &value, &next), FT_STATUS_OK);
        ft_sorted_multiset_dispose(&bag);
        bag = next;
    }

    costs.value_copies = 0;
    costs.measure_combines = 0;
    counter.comparisons = 0;
    int probe = 3072;
    REQUIRE(ft_sorted_multiset_contains(&bag, &probe));
    REQUIRE(ft_sorted_multiset_count_of(&bag, &probe) == 1);
    REQUIRE(costs.value_copies == 0);
    REQUIRE(counter.comparisons < 128);

    costs.value_copies = 0;
    counter.comparisons = 0;
    int missing = 5000;
    REQUIRE(!ft_sorted_multiset_contains(&bag, &missing));
    REQUIRE(costs.value_copies == 0);
    REQUIRE(counter.comparisons < 32);

    ft_sorted_multiset_dispose(&bag);
}

static void test_priority_queue(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    ft_priority_queue queue;
    REQUIRE_STATUS(ft_priority_queue_init(&queue, &int_type, &int_type, compare_ints, NULL), FT_STATUS_OK);

    const int values[] = {10, 20, 30, 40};
    const int priorities[] = {5, 1, 1, 3};
    for (size_t index = 0; index != sizeof(values) / sizeof(values[0]); ++index) {
        ft_priority_queue next;
        REQUIRE_STATUS(ft_priority_queue_push(&queue, &values[index], &priorities[index], &next), FT_STATUS_OK);
        ft_priority_queue_dispose(&queue);
        ft_priority_queue_move(&queue, &next);
    }

    REQUIRE(ft_priority_queue_size(&queue) == 4);
    bool found = false;
    int value = -1;
    int priority = -1;
    REQUIRE_STATUS(ft_priority_queue_try_peek(&queue, &found, &value, &priority), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(value == 20);
    REQUIRE(priority == 1);

    const int expected_values[] = {20, 30, 40, 10};
    const int expected_priorities[] = {1, 1, 3, 5};
    for (size_t index = 0; index != sizeof(expected_values) / sizeof(expected_values[0]); ++index) {
        ft_priority_queue rest;
        value = -1;
        priority = -1;
        REQUIRE_STATUS(ft_priority_queue_try_pop(&queue, &found, &value, &priority, &rest), FT_STATUS_OK);
        REQUIRE(found);
        REQUIRE(value == expected_values[index]);
        REQUIRE(priority == expected_priorities[index]);
        ft_priority_queue_dispose(&queue);
        ft_priority_queue_move(&queue, &rest);
    }

    REQUIRE(ft_priority_queue_empty(&queue));
    ft_priority_queue_dispose(&queue);

    REQUIRE_STATUS(ft_priority_queue_init(&queue, &int_type, &int_type, compare_ints, NULL), FT_STATUS_OK);
    const int same_priority = 7;
    for (int ordinal = 0; ordinal != 128; ++ordinal) {
        ft_priority_queue next;
        REQUIRE_STATUS(ft_priority_queue_push(&queue, &ordinal, &same_priority, &next), FT_STATUS_OK);
        ft_priority_queue_dispose(&queue);
        ft_priority_queue_move(&queue, &next);
    }

    for (int ordinal = 0; ordinal != 128; ++ordinal) {
        ft_priority_queue rest;
        value = -1;
        priority = -1;
        REQUIRE_STATUS(ft_priority_queue_try_pop(&queue, &found, &value, &priority, &rest), FT_STATUS_OK);
        REQUIRE(found);
        REQUIRE(value == ordinal);
        REQUIRE(priority == same_priority);
        ft_priority_queue_dispose(&queue);
        ft_priority_queue_move(&queue, &rest);
    }

    REQUIRE(ft_priority_queue_empty(&queue));
    ft_priority_queue_dispose(&queue);
}

static void test_sorted_map(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    ft_sorted_map map;
    REQUIRE_STATUS(ft_sorted_map_init(&map, &int_type, &int_type, compare_ints, NULL), FT_STATUS_OK);

    const int keys[] = {3, 1, 2};
    const int values[] = {30, 10, 20};
    for (size_t index = 0; index != sizeof(keys) / sizeof(keys[0]); ++index) {
        ft_sorted_map next;
        REQUIRE_STATUS(ft_sorted_map_insert(&map, &keys[index], &values[index], &next), FT_STATUS_OK);
        ft_sorted_map_dispose(&map);
        ft_sorted_map_move(&map, &next);
    }

    REQUIRE(ft_sorted_map_size(&map) == 3);
    for (int expected_key = 1; expected_key != 4; ++expected_key) {
        int actual_key = -1;
        int actual_value = -1;
        REQUIRE_STATUS(ft_sorted_map_entry_at(&map, (size_t)(expected_key - 1), &actual_key, &actual_value), FT_STATUS_OK);
        REQUIRE(actual_key == expected_key);
        REQUIRE(actual_value == expected_key * 10);
    }

    bool found = false;
    size_t index = 99;
    int key = 2;
    int value = -1;
    REQUIRE_STATUS(ft_sorted_map_index_of_key(&map, &key, &found, &index), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(index == 1);
    REQUIRE_STATUS(ft_sorted_map_try_get(&map, &key, &found, &value), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(value == 20);

    ft_sorted_map duplicate;
    REQUIRE_STATUS(ft_sorted_map_insert(&map, &key, &value, &duplicate), FT_STATUS_ALREADY_EXISTS);

    int replacement = 200;
    ft_sorted_map replaced;
    REQUIRE_STATUS(ft_sorted_map_set(&map, &key, &replacement, &replaced), FT_STATUS_OK);
    REQUIRE_STATUS(ft_sorted_map_try_get(&replaced, &key, &found, &value), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(value == 200);
    REQUIRE_STATUS(ft_sorted_map_try_get(&map, &key, &found, &value), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(value == 20);

    int new_key = 4;
    int new_value = 40;
    ft_sorted_map extended;
    REQUIRE_STATUS(ft_sorted_map_set(&replaced, &new_key, &new_value, &extended), FT_STATUS_OK);
    REQUIRE(ft_sorted_map_size(&extended) == 4);
    REQUIRE(ft_sorted_map_contains_key(&extended, &new_key));

    ft_sorted_map removed;
    REQUIRE_STATUS(ft_sorted_map_remove(&extended, &key, &removed), FT_STATUS_OK);
    REQUIRE(!ft_sorted_map_contains_key(&removed, &key));
    REQUIRE(ft_sorted_map_contains_key(&extended, &key));

    map_buffer buffer;
    buffer.count = 0;
    REQUIRE_STATUS(ft_sorted_map_visit(&removed, collect_map_entry, &buffer), FT_STATUS_OK);
    REQUIRE(buffer.count == 3);
    REQUIRE(buffer.keys[0] == 1);
    REQUIRE(buffer.values[0] == 10);
    REQUIRE(buffer.keys[1] == 3);
    REQUIRE(buffer.values[1] == 30);
    REQUIRE(buffer.keys[2] == 4);
    REQUIRE(buffer.values[2] == 40);

    ft_sorted_map_dispose(&removed);
    ft_sorted_map_dispose(&extended);
    ft_sorted_map_dispose(&replaced);
    ft_sorted_map_dispose(&map);
}

static void test_ordered_search_cursors(void)
{
    ft_value_type tagged_type;
    ft_value_type_init(&tagged_type, sizeof(tagged_int));
    ft_tree_policy tagged_policy;
    ft_tree_policy_init_size(&tagged_policy, &tagged_type);
    ft_sorted_multiset bag;
    REQUIRE_STATUS(
        ft_sorted_multiset_init(&bag, &tagged_policy, compare_tagged_ints, NULL),
        FT_STATUS_OK);
    const tagged_int values[] = {{1, 'a'}, {1, 'b'}, {1, 'c'}, {2, 'd'}};
    for (size_t index = 0; index != 4; ++index) {
        ft_sorted_multiset next;
        REQUIRE_STATUS(ft_sorted_multiset_add(&bag, &values[index], &next), FT_STATUS_OK);
        ft_sorted_multiset_dispose(&bag);
        bag = next;
    }

    const tagged_int probe = {1, 'x'};
    ft_sorted_multiset_cursor lower = {0};
    ft_sorted_multiset_cursor upper = {0};
    REQUIRE_STATUS(ft_sorted_multiset_get_cursor_lower_bound(&bag, &probe, &lower), FT_STATUS_OK);
    REQUIRE_STATUS(ft_sorted_multiset_get_cursor_upper_bound(&bag, &probe, &upper), FT_STATUS_OK);
    REQUIRE(ft_sorted_multiset_cursor_position(&lower) == 0);
    REQUIRE(ft_sorted_multiset_cursor_position(&upper) == 3);

    ft_sorted_multiset_cursor occurrence = {0};
    ft_sorted_multiset_cursor deleted = {0};
    REQUIRE_STATUS(ft_sorted_multiset_get_cursor(&bag, 1, &occurrence), FT_STATUS_OK);
    REQUIRE_STATUS(ft_sorted_multiset_cursor_delete_next(&occurrence, &deleted), FT_STATUS_OK);
    tagged_int actual = {0};
    REQUIRE_STATUS(ft_sorted_multiset_at(&deleted.set, 0, &actual), FT_STATUS_OK);
    REQUIRE(actual.representative == 'a');
    REQUIRE_STATUS(ft_sorted_multiset_at(&deleted.set, 1, &actual), FT_STATUS_OK);
    REQUIRE(actual.representative == 'c');
    REQUIRE_STATUS(ft_sorted_multiset_at(&bag, 1, &actual), FT_STATUS_OK);
    REQUIRE(actual.representative == 'b');

    ft_tree_policy int_policy;
    init_int_policy(&int_policy);
    ft_sorted_set set;
    REQUIRE_STATUS(ft_sorted_set_init(&set, &int_policy, compare_ints, NULL), FT_STATUS_OK);
    const int one = 1;
    const int two = 2;
    const int three = 3;
    ft_sorted_set set_next;
    REQUIRE_STATUS(ft_sorted_set_add(&set, &one, &set_next), FT_STATUS_OK);
    ft_sorted_set_dispose(&set);
    set = set_next;
    REQUIRE_STATUS(ft_sorted_set_add(&set, &three, &set_next), FT_STATUS_OK);
    ft_sorted_set_dispose(&set);
    set = set_next;
    ft_sorted_set_cursor set_cursor = {0};
    ft_sorted_set_cursor set_added = {0};
    REQUIRE_STATUS(ft_sorted_set_get_cursor_lower_bound(&set, &two, &set_cursor), FT_STATUS_OK);
    REQUIRE_STATUS(ft_sorted_set_cursor_add(&set_cursor, &two, &set_added), FT_STATUS_OK);
    REQUIRE(ft_sorted_set_cursor_position(&set_added) == 2);
    REQUIRE(ft_sorted_set_size(&set_added.set) == 3);
    REQUIRE(ft_sorted_set_size(&set) == 2);

    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_sorted_map map;
    REQUIRE_STATUS(ft_sorted_map_init(&map, &int_type, &int_type, compare_ints, NULL), FT_STATUS_OK);
    const int ten = 10;
    const int thirty = 30;
    ft_sorted_map map_next;
    REQUIRE_STATUS(ft_sorted_map_insert(&map, &one, &ten, &map_next), FT_STATUS_OK);
    ft_sorted_map_dispose(&map);
    ft_sorted_map_move(&map, &map_next);
    REQUIRE_STATUS(ft_sorted_map_insert(&map, &three, &thirty, &map_next), FT_STATUS_OK);
    ft_sorted_map_dispose(&map);
    ft_sorted_map_move(&map, &map_next);
    bool found = true;
    ft_sorted_map_cursor map_cursor = {0};
    REQUIRE_STATUS(ft_sorted_map_get_cursor_at_key(&map, &two, &found, &map_cursor), FT_STATUS_OK);
    REQUIRE(!found && ft_sorted_map_cursor_position(&map_cursor) == 1);
    const int twenty = 20;
    bool inserted = false;
    ft_sorted_map_cursor map_inserted = {0};
    REQUIRE_STATUS(
        ft_sorted_map_cursor_try_insert(&map_cursor, &two, &twenty, &inserted, &map_inserted),
        FT_STATUS_OK);
    REQUIRE(inserted && ft_sorted_map_cursor_position(&map_inserted) == 2);
    const int thirty_three = 33;
    ft_sorted_map_cursor map_updated = {0};
    REQUIRE_STATUS(
        ft_sorted_map_cursor_set_next_value(&map_inserted, &thirty_three, &map_updated),
        FT_STATUS_OK);
    int stored = 0;
    REQUIRE_STATUS(ft_sorted_map_try_get(&map_updated.map, &three, &found, &stored), FT_STATUS_OK);
    REQUIRE(found && stored == 33);
    const size_t retained_position = ft_sorted_map_cursor_position(&map_updated);
    REQUIRE_STATUS(
        ft_sorted_map_cursor_insert(&map_updated, &three, &thirty, &map_updated),
        FT_STATUS_ALREADY_EXISTS);
    REQUIRE(ft_sorted_map_cursor_valid(&map_updated));
    REQUIRE(ft_sorted_map_cursor_position(&map_updated) == retained_position);

    ft_interval_tree_i64 intervals;
    REQUIRE_STATUS(ft_interval_tree_i64_init(&intervals), FT_STATUS_OK);
    const ft_interval_i64 interval_values[] = {{1, 2}, {1, 3}, {4, 8}, {9, 10}};
    for (size_t index = 0; index != 4; ++index) {
        ft_interval_tree_i64 next;
        REQUIRE_STATUS(
            ft_interval_tree_i64_insert(&intervals, interval_values[index], &next),
            FT_STATUS_OK);
        ft_interval_tree_i64_dispose(&intervals);
        ft_interval_tree_i64_move(&intervals, &next);
    }
    ft_interval_tree_i64_cursor interval_cursor = {0};
    REQUIRE_STATUS(
        ft_interval_tree_i64_get_cursor_at_interval(
            &intervals,
            (ft_interval_i64){1, 2},
            &found,
            &interval_cursor),
        FT_STATUS_OK);
    REQUIRE(found);
    ft_interval_tree_i64_cursor interval_deleted = {0};
    REQUIRE_STATUS(
        ft_interval_tree_i64_cursor_delete_next(&interval_cursor, &interval_deleted),
        FT_STATUS_OK);
    REQUIRE(!ft_interval_tree_i64_contains(&interval_deleted.tree, (ft_interval_i64){1, 2}));
    REQUIRE(ft_interval_tree_i64_contains(&interval_deleted.tree, (ft_interval_i64){1, 3}));
    ft_interval_tree_i64_cursor overlap_cursor = {0};
    REQUIRE_STATUS(
        ft_interval_tree_i64_find_overlap_cursor(
            &intervals,
            (ft_interval_i64){6, 7},
            &found,
            &overlap_cursor),
        FT_STATUS_OK);
    REQUIRE(found);
    ft_interval_i64 overlap = {0};
    REQUIRE_STATUS(
        ft_interval_tree_i64_cursor_try_peek_next(&overlap_cursor, &found, &overlap),
        FT_STATUS_OK);
    REQUIRE(found && overlap.low == 4 && overlap.high == 8);

    ft_interval_tree generic;
    REQUIRE_STATUS(ft_interval_tree_init(&generic, &int_type, compare_ints, NULL), FT_STATUS_OK);
    const int four = 4;
    const int eight = 8;
    ft_interval_tree generic_next;
    REQUIRE_STATUS(ft_interval_tree_insert(&generic, &one, &two, &generic_next), FT_STATUS_OK);
    ft_interval_tree_dispose(&generic);
    ft_interval_tree_move(&generic, &generic_next);
    REQUIRE_STATUS(ft_interval_tree_insert(&generic, &four, &eight, &generic_next), FT_STATUS_OK);
    ft_interval_tree_dispose(&generic);
    ft_interval_tree_move(&generic, &generic_next);
    ft_interval_tree_cursor generic_cursor = {0};
    const int six = 6;
    REQUIRE_STATUS(
        ft_interval_tree_find_containing_cursor(&generic, &six, &found, &generic_cursor),
        FT_STATUS_OK);
    REQUIRE(found && ft_interval_tree_cursor_position(&generic_cursor) == 1);

    ft_interval_tree_cursor_dispose(&generic_cursor);
    ft_interval_tree_dispose(&generic);
    ft_interval_tree_i64_cursor_dispose(&overlap_cursor);
    ft_interval_tree_i64_cursor_dispose(&interval_deleted);
    ft_interval_tree_i64_cursor_dispose(&interval_cursor);
    ft_interval_tree_i64_dispose(&intervals);
    ft_sorted_map_cursor_dispose(&map_updated);
    ft_sorted_map_cursor_dispose(&map_inserted);
    ft_sorted_map_cursor_dispose(&map_cursor);
    ft_sorted_map_dispose(&map);
    ft_sorted_set_cursor_dispose(&set_added);
    ft_sorted_set_cursor_dispose(&set_cursor);
    ft_sorted_set_dispose(&set);
    ft_sorted_multiset_cursor_dispose(&deleted);
    ft_sorted_multiset_cursor_dispose(&occurrence);
    ft_sorted_multiset_cursor_dispose(&upper);
    ft_sorted_multiset_cursor_dispose(&lower);
    ft_sorted_multiset_dispose(&bag);
}

static void test_rope(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    int values[3000];
    long long expected_sum = 0;
    for (int index = 0; index != 3000; ++index) {
        values[index] = index;
        expected_sum += index;
    }

    ft_rope rope;
    REQUIRE_STATUS(ft_rope_from_array(&rope, &int_type, values, 3000), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&rope) == 3000);
    REQUIRE(ft_tree_size(&rope.tree) == 2);

    size_t reported_size = 0;
    REQUIRE_STATUS(ft_rope_try_size(&rope, &reported_size), FT_STATUS_OK);
    REQUIRE(reported_size == 3000);
    REQUIRE_STATUS(ft_rope_try_size(NULL, &reported_size), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_rope_try_size(&rope, NULL), FT_STATUS_INVALID_ARGUMENT);

    const int probes[] = {0, 2047, 2048, 2999};
    for (size_t index = 0; index != sizeof(probes) / sizeof(probes[0]); ++index) {
        int actual = -1;
        REQUIRE_STATUS(ft_rope_at(&rope, (size_t)probes[index], &actual), FT_STATUS_OK);
        REQUIRE(actual == probes[index]);
    }

    int_summary summary;
    summary.sum = 0;
    summary.count = 0;
    REQUIRE_STATUS(ft_rope_visit(&rope, summarize_int, &summary), FT_STATUS_OK);
    REQUIRE(summary.count == 3000);
    REQUIRE(summary.sum == expected_sum);

    ft_rope_split_result split;
    REQUIRE_STATUS(ft_rope_split_at(&rope, 2050, &split), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&split.left) == 2050);
    REQUIRE(ft_rope_size(&split.right) == 950);
    int boundary_left = -1;
    int boundary_right = -1;
    REQUIRE_STATUS(ft_rope_at(&split.left, 2049, &boundary_left), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rope_at(&split.right, 0, &boundary_right), FT_STATUS_OK);
    REQUIRE(boundary_left == 2049);
    REQUIRE(boundary_right == 2050);

    ft_rope joined;
    REQUIRE_STATUS(ft_rope_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&joined) == 3000);
    REQUIRE(ft_tree_size(&joined.tree) == 2);
    for (size_t index = 0; index < 3000; index += 257) {
        int actual = -1;
        REQUIRE_STATUS(ft_rope_at(&joined, index, &actual), FT_STATUS_OK);
        REQUIRE(actual == (int)index);
    }

    int inserted_value = 7777;
    ft_rope inserted;
    REQUIRE_STATUS(ft_rope_insert_at(&joined, 3, &inserted_value, &inserted), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&inserted) == 3001);
    REQUIRE(ft_tree_size(&inserted.tree) == 2);
    int actual = -1;
    REQUIRE_STATUS(ft_rope_at(&inserted, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == inserted_value);
    REQUIRE_STATUS(ft_rope_at(&joined, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == 3);

    ft_rope removed;
    REQUIRE_STATUS(ft_rope_remove_at(&inserted, 3, &removed), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&removed) == 3000);
    REQUIRE(ft_tree_size(&removed.tree) == 2);
    REQUIRE_STATUS(ft_rope_at(&removed, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == 3);

    int pushed = 9001;
    ft_rope pushed_rope;
    REQUIRE_STATUS(ft_rope_push_back(&removed, &pushed, &pushed_rope), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&pushed_rope) == 3001);
    REQUIRE_STATUS(ft_rope_at(&pushed_rope, 3000, &actual), FT_STATUS_OK);
    REQUIRE(actual == pushed);
    ft_rope_dispose(&removed);
    ft_rope_move(&removed, &pushed_rope);
    REQUIRE(ft_rope_size(&removed) == 3001);
    REQUIRE_STATUS(ft_rope_at(&removed, 3000, &actual), FT_STATUS_OK);
    REQUIRE(actual == pushed);

    ft_rope_dispose(&removed);
    ft_rope_dispose(&inserted);
    ft_rope_dispose(&joined);
    ft_rope_dispose(&split.left);
    ft_rope_dispose(&split.right);
    ft_rope_dispose(&rope);
}

static void test_rope_cursor(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    int values[3000];
    for (int index = 0; index != 3000; ++index) {
        values[index] = index;
    }

    ft_rope rope;
    REQUIRE_STATUS(ft_rope_from_array(&rope, &int_type, values, 3000), FT_STATUS_OK);

    ft_rope_cursor start;
    REQUIRE_STATUS(ft_rope_get_cursor(&rope, 0, &start), FT_STATUS_OK);
    REQUIRE(ft_rope_cursor_valid(&start));
    REQUIRE(!ft_rope_cursor_empty(&start));
    REQUIRE(ft_rope_cursor_size(&start) == 3000);
    REQUIRE(ft_rope_cursor_position(&start) == 0);

    size_t reported_size = 0;
    REQUIRE_STATUS(ft_rope_cursor_try_size(&start, &reported_size), FT_STATUS_OK);
    REQUIRE(reported_size == 3000);
    bool boundary = false;
    REQUIRE_STATUS(ft_rope_cursor_is_at_start(&start, &boundary), FT_STATUS_OK);
    REQUIRE(boundary);
    REQUIRE_STATUS(ft_rope_cursor_is_at_end(&start, &boundary), FT_STATUS_OK);
    REQUIRE(!boundary);

    bool found = true;
    int peek = -1;
    REQUIRE_STATUS(ft_rope_cursor_try_peek_previous(&start, &found, &peek), FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE(peek == -1);
    REQUIRE_STATUS(ft_rope_cursor_try_peek_next(&start, &found, &peek), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(peek == 0);

    ft_rope_cursor seam;
    REQUIRE_STATUS(ft_rope_cursor_seek(&start, 2048, &seam), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rope_cursor_try_peek_previous(&seam, &found, &peek), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(peek == 2047);
    REQUIRE_STATUS(ft_rope_cursor_try_peek_next(&seam, &found, &peek), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(peek == 2048);

    ft_rope_cursor end;
    REQUIRE_STATUS(ft_rope_get_cursor(&rope, 3000, &end), FT_STATUS_OK);
    REQUIRE_STATUS(ft_rope_cursor_is_at_end(&end, &boundary), FT_STATUS_OK);
    REQUIRE(boundary);
    peek = -1;
    REQUIRE_STATUS(ft_rope_cursor_try_peek_next(&end, &found, &peek), FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE(peek == -1);
    REQUIRE_STATUS(ft_rope_cursor_try_peek_previous(&end, &found, &peek), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(peek == 2999);

    ft_rope_cursor moved;
    REQUIRE_STATUS(ft_rope_cursor_copy(&seam, &moved), FT_STATUS_OK);
    ft_rope_cursor moved_again = {0};
    ft_rope_cursor_move(&moved_again, &moved);
    REQUIRE(!ft_rope_cursor_valid(&moved));
    REQUIRE(ft_rope_cursor_valid(&moved_again));
    REQUIRE(ft_rope_cursor_position(&moved_again) == 2048);
    REQUIRE_STATUS(ft_rope_cursor_move_previous(&moved_again, &moved_again), FT_STATUS_OK);
    REQUIRE(ft_rope_cursor_position(&moved_again) == 2047);
    REQUIRE_STATUS(ft_rope_cursor_move_next(&moved_again, &moved_again), FT_STATUS_OK);
    REQUIRE(ft_rope_cursor_position(&moved_again) == 2048);

    ft_rope_cursor sentinel;
    REQUIRE_STATUS(ft_rope_get_cursor(&rope, 17, &sentinel), FT_STATUS_OK);
    ft_tree_rep* const sentinel_rep = sentinel.rope.tree.rep;
    REQUIRE_STATUS(ft_rope_cursor_seek(&sentinel, 3001, &sentinel), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(sentinel.rope.tree.rep == sentinel_rep);
    REQUIRE(ft_rope_cursor_position(&sentinel) == 17);
    REQUIRE_STATUS(ft_rope_cursor_move_previous(&start, &sentinel), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(sentinel.rope.tree.rep == sentinel_rep);
    REQUIRE(ft_rope_cursor_position(&sentinel) == 17);
    REQUIRE_STATUS(ft_rope_get_cursor(&rope, 3001, &sentinel), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(sentinel.rope.tree.rep == sentinel_rep);
    REQUIRE(ft_rope_cursor_position(&sentinel) == 17);

    ft_rope empty_rope;
    REQUIRE_STATUS(ft_rope_init(&empty_rope, &int_type), FT_STATUS_OK);
    ft_rope_cursor empty_cursor;
    REQUIRE_STATUS(ft_rope_get_cursor(&empty_rope, 0, &empty_cursor), FT_STATUS_OK);
    REQUIRE(ft_rope_cursor_empty(&empty_cursor));
    REQUIRE_STATUS(ft_rope_cursor_is_at_start(&empty_cursor, &boundary), FT_STATUS_OK);
    REQUIRE(boundary);
    REQUIRE_STATUS(ft_rope_cursor_is_at_end(&empty_cursor, &boundary), FT_STATUS_OK);
    REQUIRE(boundary);
    peek = -1;
    REQUIRE_STATUS(ft_rope_cursor_try_peek_previous(&empty_cursor, &found, &peek), FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE_STATUS(ft_rope_cursor_try_peek_next(&empty_cursor, &found, &peek), FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE_STATUS(ft_rope_cursor_move_previous(&empty_cursor, &sentinel), FT_STATUS_EMPTY);
    REQUIRE_STATUS(ft_rope_cursor_move_next(&empty_cursor, &sentinel), FT_STATUS_EMPTY);
    REQUIRE_STATUS(ft_rope_cursor_delete_previous(&empty_cursor, &sentinel), FT_STATUS_EMPTY);
    REQUIRE_STATUS(ft_rope_cursor_delete_next(&empty_cursor, &sentinel), FT_STATUS_EMPTY);
    REQUIRE_STATUS(ft_rope_cursor_replace_next(&empty_cursor, &peek, &sentinel), FT_STATUS_EMPTY);
    REQUIRE(sentinel.rope.tree.rep == sentinel_rep);
    REQUIRE(ft_rope_cursor_position(&sentinel) == 17);

    const int small_values[] = {10, 20, 30, 40};
    ft_rope small;
    REQUIRE_STATUS(ft_rope_from_array(&small, &int_type, small_values, 4), FT_STATUS_OK);
    ft_rope_cursor base;
    REQUIRE_STATUS(ft_rope_get_cursor(&small, 2, &base), FT_STATUS_OK);

    const int inserted_value = 99;
    ft_rope_cursor inserted;
    REQUIRE_STATUS(ft_rope_cursor_insert(&base, &inserted_value, &inserted), FT_STATUS_OK);
    REQUIRE(ft_rope_cursor_position(&inserted) == 3);
    const int inserted_expected[] = {10, 20, 99, 30, 40};
    ft_rope inserted_snapshot;
    REQUIRE_STATUS(ft_rope_cursor_snapshot(&inserted, &inserted_snapshot), FT_STATUS_OK);
    REQUIRE(rope_matches(&inserted_snapshot, inserted_expected, 5));
    REQUIRE(rope_matches(&small, small_values, 4));

    ft_rope_cursor restored;
    REQUIRE_STATUS(ft_rope_cursor_delete_previous(&inserted, &restored), FT_STATUS_OK);
    ft_rope restored_snapshot;
    REQUIRE_STATUS(ft_rope_cursor_snapshot(&restored, &restored_snapshot), FT_STATUS_OK);
    REQUIRE(rope_matches(&restored_snapshot, small_values, 4));
    REQUIRE(ft_rope_cursor_position(&restored) == 2);

    const int range_values[] = {7, 8, 9};
    ft_rope_cursor with_range;
    REQUIRE_STATUS(
        ft_rope_cursor_insert_array(&base, range_values, 3, &with_range),
        FT_STATUS_OK);
    const int range_expected[] = {10, 20, 7, 8, 9, 30, 40};
    ft_rope range_snapshot;
    REQUIRE_STATUS(ft_rope_cursor_snapshot(&with_range, &range_snapshot), FT_STATUS_OK);
    REQUIRE(rope_matches(&range_snapshot, range_expected, 7));
    REQUIRE(ft_rope_cursor_position(&with_range) == 5);

    ft_rope range_rope;
    REQUIRE_STATUS(ft_rope_from_array(&range_rope, &int_type, range_values, 3), FT_STATUS_OK);
    ft_rope_cursor with_rope;
    REQUIRE_STATUS(ft_rope_cursor_insert_rope(&base, &range_rope, &with_rope), FT_STATUS_OK);
    ft_rope range_rope_snapshot;
    REQUIRE_STATUS(ft_rope_cursor_snapshot(&with_rope, &range_rope_snapshot), FT_STATUS_OK);
    REQUIRE(rope_matches(&range_rope_snapshot, range_expected, 7));

    ft_rope_cursor deleted_next;
    REQUIRE_STATUS(ft_rope_cursor_delete_next(&base, &deleted_next), FT_STATUS_OK);
    const int deleted_next_expected[] = {10, 20, 40};
    ft_rope deleted_next_snapshot;
    REQUIRE_STATUS(ft_rope_cursor_snapshot(&deleted_next, &deleted_next_snapshot), FT_STATUS_OK);
    REQUIRE(rope_matches(&deleted_next_snapshot, deleted_next_expected, 3));
    REQUIRE(ft_rope_cursor_position(&deleted_next) == 2);

    const int equal_replacement = 30;
    ft_rope_cursor replaced;
    REQUIRE_STATUS(ft_rope_cursor_replace_next(&base, &equal_replacement, &replaced), FT_STATUS_OK);
    ft_rope replaced_snapshot;
    REQUIRE_STATUS(ft_rope_cursor_snapshot(&replaced, &replaced_snapshot), FT_STATUS_OK);
    REQUIRE(rope_matches(&replaced_snapshot, small_values, 4));
    REQUIRE(replaced_snapshot.tree.rep != small.tree.rep);
    REQUIRE(ft_rope_cursor_position(&replaced) == 2);

    ft_rope_cursor no_op;
    REQUIRE_STATUS(ft_rope_cursor_insert_array(&base, NULL, 0, &no_op), FT_STATUS_OK);
    REQUIRE(no_op.rope.tree.rep == base.rope.tree.rep);
    REQUIRE(ft_rope_cursor_position(&no_op) == 2);

    ft_value_type short_type;
    ft_value_type_init(&short_type, sizeof(short));
    const short short_value = 1;
    ft_rope incompatible;
    REQUIRE_STATUS(ft_rope_from_array(&incompatible, &short_type, &short_value, 1), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_rope_cursor_insert_rope(&base, &incompatible, &sentinel),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(sentinel.rope.tree.rep == sentinel_rep);
    REQUIRE(ft_rope_cursor_position(&sentinel) == 17);

    ft_rope_cursor_dispose(&no_op);
    ft_rope_dispose(&incompatible);
    ft_rope_dispose(&replaced_snapshot);
    ft_rope_cursor_dispose(&replaced);
    ft_rope_dispose(&deleted_next_snapshot);
    ft_rope_cursor_dispose(&deleted_next);
    ft_rope_dispose(&range_rope_snapshot);
    ft_rope_cursor_dispose(&with_rope);
    ft_rope_dispose(&range_rope);
    ft_rope_dispose(&range_snapshot);
    ft_rope_cursor_dispose(&with_range);
    ft_rope_dispose(&restored_snapshot);
    ft_rope_cursor_dispose(&restored);
    ft_rope_dispose(&inserted_snapshot);
    ft_rope_cursor_dispose(&inserted);
    ft_rope_cursor_dispose(&base);
    ft_rope_dispose(&small);
    ft_rope_cursor_dispose(&empty_cursor);
    ft_rope_dispose(&empty_rope);
    ft_rope_cursor_dispose(&sentinel);
    ft_rope_cursor_dispose(&moved_again);
    ft_rope_cursor_dispose(&moved);
    ft_rope_cursor_dispose(&end);
    ft_rope_cursor_dispose(&seam);
    ft_rope_cursor_dispose(&start);
    ft_rope_dispose(&rope);
}

static uint32_t rope_cursor_next_random(uint32_t* state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_rope_cursor_model(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    ft_rope rope;
    REQUIRE_STATUS(ft_rope_init(&rope, &int_type), FT_STATUS_OK);
    ft_rope_cursor cursor;
    REQUIRE_STATUS(ft_rope_get_cursor(&rope, 0, &cursor), FT_STATUS_OK);
    ft_rope_dispose(&rope);

    int model[96] = {0};
    size_t count = 0;
    size_t position = 0;
    size_t reported_size = 0;
    uint32_t random = UINT32_C(0x6d2b79f5);

    for (int step = 0; step != 750; ++step) {
        const uint32_t bits = rope_cursor_next_random(&random);
        unsigned operation = bits % 7u;
        if (count == 96 && operation == 0) {
            operation = position == 0 ? 2u : 1u;
        }

        if (operation == 0 || (count == 0 && operation != 6)) {
            const int value = step * 17 + (int)(bits >> 24);
            REQUIRE_STATUS(ft_rope_cursor_insert(&cursor, &value, &cursor), FT_STATUS_OK);
            (void)memmove(
                model + position + 1u,
                model + position,
                (count - position) * sizeof(model[0]));
            model[position] = value;
            ++position;
            ++count;
        } else if (operation == 1 && position != 0) {
            REQUIRE_STATUS(ft_rope_cursor_delete_previous(&cursor, &cursor), FT_STATUS_OK);
            (void)memmove(
                model + position - 1u,
                model + position,
                (count - position) * sizeof(model[0]));
            --position;
            --count;
        } else if (operation == 2 && position != count) {
            REQUIRE_STATUS(ft_rope_cursor_delete_next(&cursor, &cursor), FT_STATUS_OK);
            (void)memmove(
                model + position,
                model + position + 1u,
                (count - position - 1u) * sizeof(model[0]));
            --count;
        } else if (operation == 3 && position != 0) {
            REQUIRE_STATUS(ft_rope_cursor_move_previous(&cursor, &cursor), FT_STATUS_OK);
            --position;
        } else if (operation == 4 && position != count) {
            REQUIRE_STATUS(ft_rope_cursor_move_next(&cursor, &cursor), FT_STATUS_OK);
            ++position;
        } else if (operation == 5 && position != count) {
            const int value = -step - 1;
            REQUIRE_STATUS(ft_rope_cursor_replace_next(&cursor, &value, &cursor), FT_STATUS_OK);
            model[position] = value;
        } else {
            const size_t next_position = count == 0 ? 0 : (size_t)(bits >> 8) % (count + 1u);
            REQUIRE_STATUS(ft_rope_cursor_seek(&cursor, next_position, &cursor), FT_STATUS_OK);
            position = next_position;
        }

        REQUIRE(ft_rope_cursor_position(&cursor) == position);
        REQUIRE_STATUS(ft_rope_cursor_try_size(&cursor, &reported_size), FT_STATUS_OK);
        REQUIRE(reported_size == count);

        bool found = false;
        int value = 0;
        REQUIRE_STATUS(ft_rope_cursor_try_peek_previous(&cursor, &found, &value), FT_STATUS_OK);
        REQUIRE(found == (position != 0));
        if (found) {
            REQUIRE(value == model[position - 1u]);
        }

        REQUIRE_STATUS(ft_rope_cursor_try_peek_next(&cursor, &found, &value), FT_STATUS_OK);
        REQUIRE(found == (position != count));
        if (found) {
            REQUIRE(value == model[position]);
        }

        if (step % 25 == 0 || step == 749) {
            ft_rope snapshot;
            REQUIRE_STATUS(ft_rope_cursor_snapshot(&cursor, &snapshot), FT_STATUS_OK);
            REQUIRE(rope_matches(&snapshot, model, count));
            ft_rope_dispose(&snapshot);
        }
    }

    ft_rope_cursor_dispose(&cursor);
}

typedef struct concurrent_rope_cursor_context {
    const ft_rope_cursor* cursor;
    int iterations;
    test_atomic_long failures;
} concurrent_rope_cursor_context;

static void concurrent_rope_cursor_worker(concurrent_rope_cursor_context* context)
{
    for (int iteration = 0; iteration != context->iterations; ++iteration) {
        ft_rope_cursor cursor;
        if (ft_rope_cursor_copy(context->cursor, &cursor) != FT_STATUS_OK) {
            test_atomic_long_increment(&context->failures);
            return;
        }

        bool found = false;
        int previous = -1;
        int next = -1;
        if (ft_rope_cursor_try_peek_previous(&cursor, &found, &previous) != FT_STATUS_OK ||
            !found || previous != 2047 ||
            ft_rope_cursor_try_peek_next(&cursor, &found, &next) != FT_STATUS_OK ||
            !found || next != 2048) {
            test_atomic_long_increment(&context->failures);
            ft_rope_cursor_dispose(&cursor);
            return;
        }

        const int inserted = -iteration - 1;
        ft_rope_cursor branch = {0};
        if (ft_rope_cursor_insert(&cursor, &inserted, &branch) != FT_STATUS_OK ||
            ft_rope_cursor_position(&branch) != 2049 ||
            ft_rope_cursor_size(&branch) != 3001 ||
            ft_rope_cursor_position(&cursor) != 2048 ||
            ft_rope_cursor_size(&cursor) != 3000) {
            test_atomic_long_increment(&context->failures);
            ft_rope_cursor_dispose(&branch);
            ft_rope_cursor_dispose(&cursor);
            return;
        }

        ft_rope_cursor_dispose(&branch);
        ft_rope_cursor_dispose(&cursor);
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_rope_cursor_thread_proc(void* parameter)
{
    concurrent_rope_cursor_worker((concurrent_rope_cursor_context*)parameter);
    return 0;
}
#elif defined(TEST_HAS_C11_THREADS)
static int concurrent_rope_cursor_thread_main(void* parameter)
{
    concurrent_rope_cursor_worker((concurrent_rope_cursor_context*)parameter);
    return 0;
}
#endif

static void test_rope_cursor_concurrent_readers(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    int values[3000];
    for (int index = 0; index != 3000; ++index) {
        values[index] = index;
    }

    ft_rope rope;
    REQUIRE_STATUS(ft_rope_from_array(&rope, &int_type, values, 3000), FT_STATUS_OK);
    ft_rope_cursor cursor;
    REQUIRE_STATUS(ft_rope_get_cursor(&rope, 2048, &cursor), FT_STATUS_OK);

    concurrent_rope_cursor_context context;
    context.cursor = &cursor;
    context.iterations = 48;
    test_atomic_long_init(&context.failures, 0);

#ifdef _WIN32
    enum { thread_count = 4 };
    HANDLE threads[thread_count];
    for (DWORD index = 0; index != thread_count; ++index) {
        threads[index] = CreateThread(NULL, 0, concurrent_rope_cursor_thread_proc, &context, 0, NULL);
        REQUIRE(threads[index] != NULL);
    }

    REQUIRE(WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (DWORD index = 0; index != thread_count; ++index) {
        CloseHandle(threads[index]);
    }
#elif defined(TEST_HAS_C11_THREADS)
    enum { thread_count = 4 };
    thrd_t threads[thread_count];
    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(thrd_create(&threads[index], concurrent_rope_cursor_thread_main, &context) == thrd_success);
    }

    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(thrd_join(threads[index], NULL) == thrd_success);
    }
#else
    for (int index = 0; index != 4; ++index) {
        concurrent_rope_cursor_worker(&context);
    }
#endif

    REQUIRE(test_atomic_long_read(&context.failures) == 0);
    REQUIRE(ft_rope_cursor_position(&cursor) == 2048);
    REQUIRE(ft_rope_cursor_size(&cursor) == 3000);
    ft_rope_cursor_dispose(&cursor);
    ft_rope_dispose(&rope);
}

static void test_rope_chunk_boundaries(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    int values[2048];
    for (int index = 0; index != 2048; ++index) {
        values[index] = index;
    }

    ft_rope rope;
    REQUIRE_STATUS(ft_rope_from_array(&rope, &int_type, values, 2048), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&rope.tree) == 1);

    const int inserted_value = -1;
    ft_rope inserted;
    REQUIRE_STATUS(ft_rope_insert_at(&rope, 1024, &inserted_value, &inserted), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&inserted) == 2049);
    REQUIRE(ft_tree_size(&inserted.tree) == 2);

    ft_rope removed;
    REQUIRE_STATUS(ft_rope_remove_at(&inserted, 1024, &removed), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&removed) == 2048);
    REQUIRE(ft_tree_size(&removed.tree) == 1);

    ft_rope_split_result split;
    REQUIRE_STATUS(ft_rope_split_at(&removed, 1024, &split), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&split.left.tree) == 1);
    REQUIRE(ft_tree_size(&split.right.tree) == 1);

    ft_rope joined;
    REQUIRE_STATUS(ft_rope_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&joined) == 2048);
    REQUIRE(ft_tree_size(&joined.tree) == 1);

    ft_rope empty;
    REQUIRE_STATUS(ft_rope_init(&empty, &int_type), FT_STATUS_OK);
    ft_rope singleton;
    REQUIRE_STATUS(ft_rope_insert_at(&empty, 0, &inserted_value, &singleton), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&singleton.tree) == 1);
    ft_rope empty_again;
    REQUIRE_STATUS(ft_rope_remove_at(&singleton, 0, &empty_again), FT_STATUS_OK);
    REQUIRE(ft_rope_empty(&empty_again));
    REQUIRE(ft_tree_size(&empty_again.tree) == 0);

    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);
    ft_measured_rope measured;
    REQUIRE_STATUS(
        ft_measured_rope_from_array(&measured, &int_type, &sum_measure, values, 2048),
        FT_STATUS_OK);
    REQUIRE(ft_tree_size(&measured.tree) == 1);
    ft_measured_rope measured_inserted;
    REQUIRE_STATUS(
        ft_measured_rope_insert_at(&measured, 1024, &inserted_value, &measured_inserted),
        FT_STATUS_OK);
    REQUIRE(ft_tree_size(&measured_inserted.tree) == 2);
    ft_measured_rope measured_removed;
    REQUIRE_STATUS(
        ft_measured_rope_remove_at(&measured_inserted, 1024, &measured_removed),
        FT_STATUS_OK);
    REQUIRE(ft_tree_size(&measured_removed.tree) == 1);

    ft_measured_rope_dispose(&measured_removed);
    ft_measured_rope_dispose(&measured_inserted);
    ft_measured_rope_dispose(&measured);
    ft_rope_dispose(&empty_again);
    ft_rope_dispose(&singleton);
    ft_rope_dispose(&empty);
    ft_rope_dispose(&joined);
    ft_rope_dispose(&split.left);
    ft_rope_dispose(&split.right);
    ft_rope_dispose(&removed);
    ft_rope_dispose(&inserted);
    ft_rope_dispose(&rope);
}

static void test_measured_rope(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);

    int values[3000];
    int expected_sum = 0;
    for (int index = 0; index != 3000; ++index) {
        values[index] = 1;
        expected_sum += values[index];
    }
    values[2048] = 10;
    expected_sum += 9;

    ft_measured_rope rope;
    REQUIRE_STATUS(ft_measured_rope_from_array(&rope, &int_type, &sum_measure, values, 3000), FT_STATUS_OK);
    REQUIRE(ft_measured_rope_size(&rope) == 3000);
    REQUIRE(ft_tree_size(&rope.tree) == 2);

    int measure = -1;
    REQUIRE_STATUS(ft_measured_rope_measure(&rope, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum);

    int prefix = -1;
    REQUIRE_STATUS(ft_measured_rope_prefix_measure(&rope, 2050, &prefix), FT_STATUS_OK);
    REQUIRE(prefix == 2059);

    int actual = -1;
    REQUIRE_STATUS(ft_measured_rope_at(&rope, 2048, &actual), FT_STATUS_OK);
    REQUIRE(actual == 10);

    int threshold = 2054;
    bool found = false;
    size_t index = 0;
    int before = -1;
    int value = -1;
    REQUIRE_STATUS(
        ft_measured_rope_locate_by_measure(
            &rope,
            int_sum_reaches,
            &threshold,
            &found,
            &index,
            &before,
            &value),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(index == 2048);
    REQUIRE(before == 2048);
    REQUIRE(value == 10);

    ft_measured_rope_split_result split;
    REQUIRE_STATUS(ft_measured_rope_split_by_measure(&rope, int_sum_reaches, &threshold, &split), FT_STATUS_OK);
    REQUIRE(ft_measured_rope_size(&split.left) == 2048);
    REQUIRE(ft_measured_rope_size(&split.right) == 952);
    REQUIRE_STATUS(ft_measured_rope_measure(&split.left, &measure), FT_STATUS_OK);
    REQUIRE(measure == 2048);
    REQUIRE_STATUS(ft_measured_rope_at(&split.right, 0, &actual), FT_STATUS_OK);
    REQUIRE(actual == 10);

    ft_measured_rope joined;
    REQUIRE_STATUS(ft_measured_rope_concat(&split.left, &split.right, &joined), FT_STATUS_OK);
    REQUIRE(ft_measured_rope_size(&joined) == 3000);
    REQUIRE(ft_tree_size(&joined.tree) == 2);
    REQUIRE_STATUS(ft_measured_rope_measure(&joined, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum);

    int inserted_value = 7;
    ft_measured_rope inserted;
    REQUIRE_STATUS(ft_measured_rope_insert_at(&joined, 3, &inserted_value, &inserted), FT_STATUS_OK);
    REQUIRE(ft_measured_rope_size(&inserted) == 3001);
    REQUIRE(ft_tree_size(&inserted.tree) == 2);
    REQUIRE_STATUS(ft_measured_rope_measure(&inserted, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum + inserted_value);
    REQUIRE_STATUS(ft_measured_rope_at(&joined, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == 1);

    ft_measured_rope removed;
    REQUIRE_STATUS(ft_measured_rope_remove_at(&inserted, 3, &removed), FT_STATUS_OK);
    REQUIRE(ft_tree_size(&removed.tree) == 2);
    REQUIRE_STATUS(ft_measured_rope_measure(&removed, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum);

    int pushed = 11;
    ft_measured_rope pushed_rope;
    REQUIRE_STATUS(ft_measured_rope_push_back(&removed, &pushed, &pushed_rope), FT_STATUS_OK);
    REQUIRE_STATUS(ft_measured_rope_measure(&pushed_rope, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum + pushed);
    ft_measured_rope_dispose(&removed);
    ft_measured_rope_move(&removed, &pushed_rope);
    REQUIRE_STATUS(ft_measured_rope_measure(&removed, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum + pushed);

    ft_measured_rope_dispose(&removed);
    ft_measured_rope_dispose(&inserted);
    ft_measured_rope_dispose(&joined);
    ft_measured_rope_dispose(&split.left);
    ft_measured_rope_dispose(&split.right);
    ft_measured_rope_dispose(&rope);
}

static void test_measured_rope_cursor(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);

    int values[3000];
    for (size_t index = 0; index != 3000; ++index) {
        values[index] = 1;
    }
    values[2048] = 10;

    ft_measured_rope rope;
    REQUIRE_STATUS(
        ft_measured_rope_from_array(&rope, &int_type, &sum_measure, values, 3000),
        FT_STATUS_OK);

    ft_measured_rope_cursor cursor;
    REQUIRE_STATUS(ft_measured_rope_get_cursor(&rope, 2048, &cursor), FT_STATUS_OK);
    REQUIRE(ft_measured_rope_cursor_valid(&cursor));
    REQUIRE(ft_measured_rope_cursor_size(&cursor) == 3000);
    REQUIRE(ft_measured_rope_cursor_position(&cursor) == 2048);

    int before = -1;
    int after = -1;
    REQUIRE_STATUS(ft_measured_rope_cursor_measure_before(&cursor, &before), FT_STATUS_OK);
    REQUIRE_STATUS(ft_measured_rope_cursor_measure_after(&cursor, &after), FT_STATUS_OK);
    REQUIRE(before == 2048);
    REQUIRE(after == 961);

    bool found = false;
    int value = 0;
    REQUIRE_STATUS(ft_measured_rope_cursor_try_peek_previous(&cursor, &found, &value), FT_STATUS_OK);
    REQUIRE(found && value == 1);
    REQUIRE_STATUS(ft_measured_rope_cursor_try_peek_next(&cursor, &found, &value), FT_STATUS_OK);
    REQUIRE(found && value == 10);

    int threshold = 2054;
    ft_measured_rope_cursor located;
    REQUIRE_STATUS(
        ft_measured_rope_get_cursor_by_measure(&rope, int_sum_reaches, &threshold, &found, &located),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(ft_measured_rope_cursor_position(&located) == 2048);

    threshold = 10000;
    REQUIRE_STATUS(
        ft_measured_rope_cursor_seek_by_measure(&located, int_sum_reaches, &threshold, &found, &located),
        FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE(ft_measured_rope_cursor_position(&located) == 3000);

    threshold = 0;
    REQUIRE_STATUS(
        ft_measured_rope_cursor_seek_by_measure(&located, int_sum_reaches, &threshold, &found, &located),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(ft_measured_rope_cursor_position(&located) == 0);

    const int base_values[] = {1, 2, 3, 4};
    ft_measured_rope base_rope;
    REQUIRE_STATUS(
        ft_measured_rope_from_array(&base_rope, &int_type, &sum_measure, base_values, 4),
        FT_STATUS_OK);
    ft_measured_rope_cursor base;
    REQUIRE_STATUS(ft_measured_rope_get_cursor(&base_rope, 2, &base), FT_STATUS_OK);

    const int range_values[] = {7, 8};
    ft_measured_rope_cursor edited;
    REQUIRE_STATUS(
        ft_measured_rope_cursor_insert_array(&base, range_values, 2, &edited),
        FT_STATUS_OK);
    REQUIRE(ft_measured_rope_cursor_position(&edited) == 4);
    const int inserted_expected[] = {1, 2, 7, 8, 3, 4};
    ft_measured_rope snapshot;
    REQUIRE_STATUS(ft_measured_rope_cursor_snapshot(&edited, &snapshot), FT_STATUS_OK);
    REQUIRE(measured_rope_matches(&snapshot, inserted_expected, 6));

    REQUIRE_STATUS(ft_measured_rope_cursor_delete_previous(&edited, &edited), FT_STATUS_OK);
    REQUIRE_STATUS(ft_measured_rope_cursor_delete_next(&edited, &edited), FT_STATUS_OK);
    value = 9;
    REQUIRE_STATUS(ft_measured_rope_cursor_replace_next(&edited, &value, &edited), FT_STATUS_OK);
    const int edited_expected[] = {1, 2, 7, 9};
    ft_measured_rope edited_snapshot;
    REQUIRE_STATUS(ft_measured_rope_cursor_snapshot(&edited, &edited_snapshot), FT_STATUS_OK);
    REQUIRE(measured_rope_matches(&edited_snapshot, edited_expected, 4));
    REQUIRE(measured_rope_matches(&base_rope, base_values, 4));

    ft_measured_rope_cursor sentinel;
    REQUIRE_STATUS(ft_measured_rope_get_cursor(&base_rope, 1, &sentinel), FT_STATUS_OK);
    REQUIRE_STATUS(ft_measured_rope_cursor_seek(&sentinel, 5, &sentinel), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(ft_measured_rope_cursor_position(&sentinel) == 1);

    ft_measure_policy incompatible_measure = sum_measure;
    incompatible_measure.context = &sentinel;
    ft_measured_rope incompatible;
    REQUIRE_STATUS(
        ft_measured_rope_from_array(&incompatible, &int_type, &incompatible_measure, base_values, 4),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_measured_rope_cursor_insert_rope(&base, &incompatible, &sentinel),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_measured_rope_cursor_position(&sentinel) == 1);

    ft_measured_rope empty_rope;
    REQUIRE_STATUS(ft_measured_rope_init(&empty_rope, &int_type, &sum_measure), FT_STATUS_OK);
    ft_measured_rope_cursor empty_cursor;
    REQUIRE_STATUS(
        ft_measured_rope_get_cursor_by_measure(
            &empty_rope, int_sum_reaches, &threshold, &found, &empty_cursor),
        FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE(ft_measured_rope_cursor_empty(&empty_cursor));
    REQUIRE(ft_measured_rope_cursor_position(&empty_cursor) == 0);

    ft_measured_rope_cursor moved = {0};
    ft_measured_rope_cursor_move(&moved, &sentinel);
    REQUIRE(!ft_measured_rope_cursor_valid(&sentinel));
    REQUIRE(ft_measured_rope_cursor_valid(&moved));

    ft_measured_rope_cursor_dispose(&moved);
    ft_measured_rope_cursor_dispose(&sentinel);
    ft_measured_rope_cursor_dispose(&empty_cursor);
    ft_measured_rope_dispose(&empty_rope);
    ft_measured_rope_dispose(&incompatible);
    ft_measured_rope_dispose(&edited_snapshot);
    ft_measured_rope_dispose(&snapshot);
    ft_measured_rope_cursor_dispose(&edited);
    ft_measured_rope_cursor_dispose(&base);
    ft_measured_rope_dispose(&base_rope);
    ft_measured_rope_cursor_dispose(&located);
    ft_measured_rope_cursor_dispose(&cursor);
    ft_measured_rope_dispose(&rope);
}

static void test_measured_rope_cursor_ordered_measure(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy measure_policy;
    init_ordered_measure(&measure_policy);
    const int values[] = {3, 1, 4, 1, 5, 9, 2};

    ft_measured_rope rope;
    REQUIRE_STATUS(
        ft_measured_rope_from_array(&rope, &int_type, &measure_policy, values, 7),
        FT_STATUS_OK);
    ft_measured_rope_cursor cursor;
    REQUIRE_STATUS(ft_measured_rope_get_cursor(&rope, 3, &cursor), FT_STATUS_OK);

    ordered_measure before;
    ordered_measure after;
    REQUIRE_STATUS(ft_measured_rope_cursor_measure_before(&cursor, &before), FT_STATUS_OK);
    REQUIRE_STATUS(ft_measured_rope_cursor_measure_after(&cursor, &after), FT_STATUS_OK);
    const ordered_measure expected_before = ordered_measure_for_values(values, 3);
    const ordered_measure expected_after = ordered_measure_for_values(values + 3, 4);
    REQUIRE(before.hash == expected_before.hash && before.count == expected_before.count);
    REQUIRE(after.hash == expected_after.hash && after.count == expected_after.count);

    ordered_measure combined;
    ordered_measure_combine(&combined, &before, &after, NULL);
    const ordered_measure expected_whole = ordered_measure_for_values(values, 7);
    REQUIRE(combined.hash == expected_whole.hash && combined.count == expected_whole.count);

    ft_measured_rope_cursor_dispose(&cursor);
    ft_measured_rope_dispose(&rope);
}

static void test_measured_rope_cursor_model(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);
    int model[128] = {1, 2, 3};
    size_t count = 3;
    size_t position = 1;
    uint32_t random = UINT32_C(0x5a17c9e3);

    ft_measured_rope rope;
    REQUIRE_STATUS(ft_measured_rope_from_array(&rope, &int_type, &sum_measure, model, count), FT_STATUS_OK);
    ft_measured_rope_cursor cursor;
    REQUIRE_STATUS(ft_measured_rope_get_cursor(&rope, position, &cursor), FT_STATUS_OK);
    ft_measured_rope_dispose(&rope);

    for (size_t step = 0; step != 750; ++step) {
        const uint32_t bits = rope_cursor_next_random(&random);
        const unsigned operation = bits % 8u;
        if (operation == 0 && count < 96) {
            const int value = (int)((bits >> 8) % 9u) + 1;
            memmove(model + position + 1, model + position, (count - position) * sizeof(int));
            model[position] = value;
            ++position;
            ++count;
            REQUIRE_STATUS(ft_measured_rope_cursor_insert(&cursor, &value, &cursor), FT_STATUS_OK);
        } else if (operation == 1 && position != 0) {
            memmove(model + position - 1, model + position, (count - position) * sizeof(int));
            --position;
            --count;
            REQUIRE_STATUS(ft_measured_rope_cursor_delete_previous(&cursor, &cursor), FT_STATUS_OK);
        } else if (operation == 2 && position != count) {
            memmove(model + position, model + position + 1, (count - position - 1) * sizeof(int));
            --count;
            REQUIRE_STATUS(ft_measured_rope_cursor_delete_next(&cursor, &cursor), FT_STATUS_OK);
        } else if (operation == 3 && position != 0) {
            --position;
            REQUIRE_STATUS(ft_measured_rope_cursor_move_previous(&cursor, &cursor), FT_STATUS_OK);
        } else if (operation == 4 && position != count) {
            ++position;
            REQUIRE_STATUS(ft_measured_rope_cursor_move_next(&cursor, &cursor), FT_STATUS_OK);
        } else if (operation == 5 && position != count) {
            const int value = (int)((bits >> 12) % 9u) + 1;
            model[position] = value;
            REQUIRE_STATUS(ft_measured_rope_cursor_replace_next(&cursor, &value, &cursor), FT_STATUS_OK);
        } else if (operation == 6) {
            position = count == 0 ? 0 : (bits >> 16) % (count + 1u);
            REQUIRE_STATUS(ft_measured_rope_cursor_seek(&cursor, position, &cursor), FT_STATUS_OK);
        } else {
            int total = 0;
            for (size_t index = 0; index != count; ++index) {
                total += model[index];
            }
            int threshold = total == 0 ? 1 : (int)((bits >> 16) % (unsigned)(total + 2)) + 1;
            bool found = false;
            REQUIRE_STATUS(
                ft_measured_rope_cursor_seek_by_measure(
                    &cursor, int_sum_reaches, &threshold, &found, &cursor),
                FT_STATUS_OK);
            int prefix = 0;
            position = 0;
            while (position != count && prefix + model[position] < threshold) {
                prefix += model[position];
                ++position;
            }
            const bool expected_found = position != count;
            REQUIRE(found == expected_found);
            if (!expected_found) {
                position = count;
            }
        }

        REQUIRE(ft_measured_rope_cursor_position(&cursor) == position);
        REQUIRE(ft_measured_rope_cursor_size(&cursor) == count);
        int expected_before = 0;
        int expected_after = 0;
        for (size_t index = 0; index != position; ++index) {
            expected_before += model[index];
        }
        for (size_t index = position; index != count; ++index) {
            expected_after += model[index];
        }
        int actual_before = -1;
        int actual_after = -1;
        REQUIRE_STATUS(ft_measured_rope_cursor_measure_before(&cursor, &actual_before), FT_STATUS_OK);
        REQUIRE_STATUS(ft_measured_rope_cursor_measure_after(&cursor, &actual_after), FT_STATUS_OK);
        REQUIRE(actual_before == expected_before && actual_after == expected_after);

        if (step % 47u == 0) {
            ft_measured_rope snapshot;
            REQUIRE_STATUS(ft_measured_rope_cursor_snapshot(&cursor, &snapshot), FT_STATUS_OK);
            REQUIRE(measured_rope_matches(&snapshot, model, count));
            ft_measured_rope_dispose(&snapshot);
        }
    }

    ft_measured_rope_cursor_dispose(&cursor);
}

static void test_text_rope_cursor(void)
{
    const char* initial = "alpha\nbeta\n";
    ft_text_rope rope;
    REQUIRE_STATUS(ft_text_rope_from_cstr(initial, &rope), FT_STATUS_OK);
    ft_text_rope_cursor cursor;
    REQUIRE_STATUS(ft_text_rope_get_cursor(&rope, 6, &cursor), FT_STATUS_OK);
    REQUIRE(ft_text_rope_cursor_valid(&cursor));
    REQUIRE(ft_text_rope_cursor_position(&cursor) == 6);

    ft_line_column line_column;
    REQUIRE_STATUS(ft_text_rope_cursor_line_column(&cursor, &line_column), FT_STATUS_OK);
    REQUIRE(line_column.line == 1 && line_column.column == 0);
    size_t before = SIZE_MAX;
    size_t after = SIZE_MAX;
    REQUIRE_STATUS(ft_text_rope_cursor_measure_before(&cursor, &before), FT_STATUS_OK);
    REQUIRE_STATUS(ft_text_rope_cursor_measure_after(&cursor, &after), FT_STATUS_OK);
    REQUIRE(before == 1 && after == 1);

    size_t threshold = 1;
    bool found = false;
    ft_text_rope_cursor newline;
    REQUIRE_STATUS(
        ft_text_rope_get_cursor_by_measure(&rope, size_reaches, &threshold, &found, &newline),
        FT_STATUS_OK);
    REQUIRE(found && ft_text_rope_cursor_position(&newline) == 5);

    ft_text_rope_cursor edited;
    REQUIRE_STATUS(ft_text_rope_cursor_insert_cstr(&newline, "X\n", &edited), FT_STATUS_OK);
    REQUIRE(ft_text_rope_cursor_position(&edited) == 7);
    ft_text_rope snapshot;
    REQUIRE_STATUS(ft_text_rope_cursor_snapshot(&edited, &snapshot), FT_STATUS_OK);
    REQUIRE(text_rope_matches_model(&snapshot, "alphaX\n\nbeta\n", 13));

    REQUIRE_STATUS(ft_text_rope_cursor_delete_previous(&edited, &edited), FT_STATUS_OK);
    REQUIRE_STATUS(ft_text_rope_cursor_replace_next(&edited, '!', &edited), FT_STATUS_OK);
    ft_text_rope edited_snapshot;
    REQUIRE_STATUS(ft_text_rope_cursor_snapshot(&edited, &edited_snapshot), FT_STATUS_OK);
    REQUIRE(text_rope_matches_model(&edited_snapshot, "alphaX!beta\n", 12));
    REQUIRE(text_rope_matches_model(&rope, initial, strlen(initial)));

    threshold = 99;
    REQUIRE_STATUS(
        ft_text_rope_cursor_seek_by_measure(&edited, size_reaches, &threshold, &found, &edited),
        FT_STATUS_OK);
    REQUIRE(!found && ft_text_rope_cursor_position(&edited) == 12);

    ft_text_rope_cursor sentinel;
    REQUIRE_STATUS(ft_text_rope_get_cursor(&rope, 2, &sentinel), FT_STATUS_OK);
    REQUIRE_STATUS(ft_text_rope_cursor_seek(&sentinel, 99, &sentinel), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(ft_text_rope_cursor_position(&sentinel) == 2);

    ft_text_rope empty;
    REQUIRE_STATUS(ft_text_rope_init(&empty), FT_STATUS_OK);
    ft_text_rope_cursor empty_cursor;
    threshold = 0;
    REQUIRE_STATUS(
        ft_text_rope_get_cursor_by_measure(&empty, size_reaches, &threshold, &found, &empty_cursor),
        FT_STATUS_OK);
    REQUIRE(!found && ft_text_rope_cursor_empty(&empty_cursor));

    ft_text_rope_cursor_dispose(&empty_cursor);
    ft_text_rope_dispose(&empty);
    ft_text_rope_cursor_dispose(&sentinel);
    ft_text_rope_dispose(&edited_snapshot);
    ft_text_rope_dispose(&snapshot);
    ft_text_rope_cursor_dispose(&edited);
    ft_text_rope_cursor_dispose(&newline);
    ft_text_rope_cursor_dispose(&cursor);
    ft_text_rope_dispose(&rope);
}

typedef struct concurrent_measured_cursor_context {
    const ft_measured_rope_cursor* cursor;
    int iterations;
    test_atomic_long failures;
} concurrent_measured_cursor_context;

static void concurrent_measured_cursor_worker(concurrent_measured_cursor_context* context)
{
    for (int iteration = 0; iteration != context->iterations; ++iteration) {
        ft_measured_rope_cursor cursor = {0};
        int before = 0;
        int after = 0;
        if (ft_measured_rope_cursor_copy(context->cursor, &cursor) != FT_STATUS_OK ||
            ft_measured_rope_cursor_measure_before(&cursor, &before) != FT_STATUS_OK ||
            ft_measured_rope_cursor_measure_after(&cursor, &after) != FT_STATUS_OK ||
            before != 2048 || after != 961) {
            test_atomic_long_increment(&context->failures);
            ft_measured_rope_cursor_dispose(&cursor);
            return;
        }
        const int inserted = 7;
        ft_measured_rope_cursor branch = {0};
        if (ft_measured_rope_cursor_insert(&cursor, &inserted, &branch) != FT_STATUS_OK ||
            ft_measured_rope_cursor_size(&branch) != 3001 ||
            ft_measured_rope_cursor_size(&cursor) != 3000) {
            test_atomic_long_increment(&context->failures);
            ft_measured_rope_cursor_dispose(&branch);
            ft_measured_rope_cursor_dispose(&cursor);
            return;
        }
        ft_measured_rope_cursor_dispose(&branch);
        ft_measured_rope_cursor_dispose(&cursor);
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_measured_cursor_thread_proc(void* parameter)
{
    concurrent_measured_cursor_worker((concurrent_measured_cursor_context*)parameter);
    return 0;
}
#elif defined(TEST_HAS_C11_THREADS)
static int concurrent_measured_cursor_thread_main(void* parameter)
{
    concurrent_measured_cursor_worker((concurrent_measured_cursor_context*)parameter);
    return 0;
}
#endif

static void test_measured_rope_cursor_concurrent_readers(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);
    int values[3000];
    for (size_t index = 0; index != 3000; ++index) {
        values[index] = 1;
    }
    values[2048] = 10;
    ft_measured_rope rope;
    REQUIRE_STATUS(
        ft_measured_rope_from_array(&rope, &int_type, &sum_measure, values, 3000),
        FT_STATUS_OK);
    ft_measured_rope_cursor cursor;
    REQUIRE_STATUS(ft_measured_rope_get_cursor(&rope, 2048, &cursor), FT_STATUS_OK);
    ft_measured_rope_dispose(&rope);

    concurrent_measured_cursor_context context;
    context.cursor = &cursor;
    context.iterations = 32;
    test_atomic_long_init(&context.failures, 0);

#ifdef _WIN32
    HANDLE threads[4];
    for (size_t index = 0; index != 4; ++index) {
        threads[index] = CreateThread(NULL, 0, concurrent_measured_cursor_thread_proc, &context, 0, NULL);
        REQUIRE(threads[index] != NULL);
    }
    REQUIRE(WaitForMultipleObjects(4, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (size_t index = 0; index != 4; ++index) {
        REQUIRE(CloseHandle(threads[index]) != 0);
    }
#elif defined(TEST_HAS_C11_THREADS)
    thrd_t threads[4];
    for (size_t index = 0; index != 4; ++index) {
        REQUIRE(thrd_create(&threads[index], concurrent_measured_cursor_thread_main, &context) == thrd_success);
    }
    for (size_t index = 0; index != 4; ++index) {
        int result = 0;
        REQUIRE(thrd_join(threads[index], &result) == thrd_success && result == 0);
    }
#else
    for (size_t index = 0; index != 4; ++index) {
        concurrent_measured_cursor_worker(&context);
    }
#endif

    REQUIRE(test_atomic_long_read(&context.failures) == 0);
    REQUIRE(ft_measured_rope_cursor_position(&cursor) == 2048);
    ft_measured_rope_cursor_dispose(&cursor);
}

/* Every handle-producing operation documents exact source/result aliasing. These paths used to
 * re-initialize or memset the destination before reading the source, so an aliased call leaked a
 * retained version or destroyed the operand it was still reading. */
static void test_exact_source_result_aliasing(void)
{
    ft_interval_tree_i64 tree;
    REQUIRE_STATUS(ft_interval_tree_i64_init(&tree), FT_STATUS_OK);

    const ft_interval_i64 seeds[] = {{5, 8}, {1, 3}, {4, 6}};
    for (size_t index = 0; index != sizeof(seeds) / sizeof(seeds[0]); ++index) {
        REQUIRE_STATUS(ft_interval_tree_i64_insert(&tree, seeds[index], &tree), FT_STATUS_OK);
    }
    REQUIRE(ft_interval_tree_i64_size(&tree) == 3);

    REQUIRE_STATUS(ft_interval_tree_i64_copy(&tree, &tree), FT_STATUS_OK);
    REQUIRE(ft_interval_tree_i64_size(&tree) == 3);

    REQUIRE_STATUS(ft_interval_tree_i64_remove_one(&tree, seeds[1], &tree), FT_STATUS_OK);
    REQUIRE(ft_interval_tree_i64_size(&tree) == 2);
    REQUIRE(!ft_interval_tree_i64_contains(&tree, seeds[1]));
    ft_interval_tree_i64_dispose(&tree);

    ft_value_type tagged_type;
    ft_value_type_init(&tagged_type, sizeof(tagged_int));
    ft_tree_policy tagged_policy;
    ft_tree_policy_init_size(&tagged_policy, &tagged_type);
    ft_sorted_multiset bag;
    REQUIRE_STATUS(
        ft_sorted_multiset_init(&bag, &tagged_policy, compare_tagged_ints, NULL), FT_STATUS_OK);
    const tagged_int items[] = {{1, 'a'}, {1, 'b'}, {2, 'c'}, {3, 'd'}};
    for (size_t index = 0; index != 4; ++index) {
        REQUIRE_STATUS(ft_sorted_multiset_add(&bag, &items[index], &bag), FT_STATUS_OK);
    }

    ft_sorted_multiset_cursor bag_cursor;
    REQUIRE_STATUS(ft_sorted_multiset_get_cursor(&bag, 2, &bag_cursor), FT_STATUS_OK);
    /* Snapshotting onto the cursor's own retained set must neither leak nor invalidate it. */
    REQUIRE_STATUS(
        ft_sorted_multiset_cursor_snapshot(&bag_cursor, &bag_cursor.set), FT_STATUS_OK);
    REQUIRE(ft_sorted_multiset_cursor_valid(&bag_cursor));
    REQUIRE(ft_sorted_multiset_cursor_size(&bag_cursor) == 4);
    REQUIRE(ft_sorted_multiset_cursor_position(&bag_cursor) == 2);
    ft_sorted_multiset_cursor_dispose(&bag_cursor);
    ft_sorted_multiset_dispose(&bag);
}

static void test_interval_tree(void)
{
    ft_interval_tree_i64 tree;
    REQUIRE_STATUS(ft_interval_tree_i64_init(&tree), FT_STATUS_OK);

    const ft_interval_i64 intervals[] = {
        {5, 8},
        {1, 3},
        {4, 6}
    };

    for (size_t index = 0; index != sizeof(intervals) / sizeof(intervals[0]); ++index) {
        ft_interval_tree_i64 next;
        REQUIRE_STATUS(ft_interval_tree_i64_insert(&tree, intervals[index], &next), FT_STATUS_OK);
        ft_interval_tree_i64_dispose(&tree);
        ft_interval_tree_i64_move(&tree, &next);
    }

    REQUIRE(ft_interval_tree_i64_size(&tree) == 3);
    REQUIRE(ft_interval_tree_i64_contains(&tree, intervals[0]));

    ft_interval_i64 first;
    REQUIRE_STATUS(ft_interval_tree_i64_at(&tree, 0, &first), FT_STATUS_OK);
    REQUIRE(first.low == 1);
    REQUIRE(first.high == 3);

    bool found = false;
    ft_interval_i64 overlap;
    REQUIRE_STATUS(ft_interval_tree_i64_try_find_overlap(&tree, (ft_interval_i64){2, 2}, &found, &overlap), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(overlap.low == 1);
    REQUIRE(overlap.high == 3);
    REQUIRE(ft_interval_tree_i64_count_overlaps(&tree, (ft_interval_i64){6, 7}) == 2);

    ft_interval_tree_i64 removed;
    REQUIRE_STATUS(ft_interval_tree_i64_remove_one(&tree, intervals[0], &removed), FT_STATUS_OK);
    REQUIRE(!ft_interval_tree_i64_contains(&removed, intervals[0]));
    REQUIRE(ft_interval_tree_i64_contains(&tree, intervals[0]));

    ft_interval_tree_i64_dispose(&removed);
    ft_interval_tree_i64_dispose(&tree);
}

static void test_interval_tree_equal_low_tie_order(void)
{
    /* Matches the C# reference: insert splits at the first stored interval
     * whose low >= the new low, so newer equal-low intervals come first. */
    ft_interval_tree_i64 tree;
    REQUIRE_STATUS(ft_interval_tree_i64_init(&tree), FT_STATUS_OK);

    const ft_interval_i64 script[] = {
        {1, 3},
        {1, 5},
        {1, 4},
        {0, 9}
    };
    for (size_t index = 0; index != sizeof(script) / sizeof(script[0]); ++index) {
        ft_interval_tree_i64 next;
        REQUIRE_STATUS(ft_interval_tree_i64_insert(&tree, script[index], &next), FT_STATUS_OK);
        ft_interval_tree_i64_dispose(&tree);
        ft_interval_tree_i64_move(&tree, &next);
    }

    const ft_interval_i64 expected[] = {
        {0, 9},
        {1, 4},
        {1, 5},
        {1, 3}
    };
    for (size_t index = 0; index != sizeof(expected) / sizeof(expected[0]); ++index) {
        ft_interval_i64 current;
        REQUIRE_STATUS(ft_interval_tree_i64_at(&tree, index, &current), FT_STATUS_OK);
        REQUIRE(current.low == expected[index].low);
        REQUIRE(current.high == expected[index].high);
    }

    /* Membership and removal must search the equal-low run, not rely on a
     * (low, high)-sorted order. */
    REQUIRE(ft_interval_tree_i64_contains(&tree, (ft_interval_i64){1, 5}));
    REQUIRE(!ft_interval_tree_i64_contains(&tree, (ft_interval_i64){1, 6}));

    ft_interval_tree_i64 removed;
    REQUIRE_STATUS(ft_interval_tree_i64_remove_one(&tree, (ft_interval_i64){1, 5}, &removed), FT_STATUS_OK);
    REQUIRE(ft_interval_tree_i64_size(&removed) == 3);
    REQUIRE(!ft_interval_tree_i64_contains(&removed, (ft_interval_i64){1, 5}));
    REQUIRE(ft_interval_tree_i64_contains(&removed, (ft_interval_i64){1, 4}));
    REQUIRE(ft_interval_tree_i64_contains(&removed, (ft_interval_i64){1, 3}));

    ft_interval_tree_i64_dispose(&removed);
    ft_interval_tree_i64_dispose(&tree);
}

static void test_generic_interval_tree(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    ft_interval_tree tree;
    REQUIRE_STATUS(ft_interval_tree_init(&tree, &int_type, compare_ints, NULL), FT_STATUS_OK);

    const int lows[] = {5, 1, 4};
    const int highs[] = {8, 3, 6};
    for (size_t index = 0; index != sizeof(lows) / sizeof(lows[0]); ++index) {
        ft_interval_tree next;
        REQUIRE_STATUS(ft_interval_tree_insert(&tree, &lows[index], &highs[index], &next), FT_STATUS_OK);
        ft_interval_tree_dispose(&tree);
        ft_interval_tree_move(&tree, &next);
    }

    REQUIRE(ft_interval_tree_size(&tree) == 3);

    int low = 0;
    int high = 0;
    REQUIRE_STATUS(ft_interval_tree_at(&tree, 0, &low, &high), FT_STATUS_OK);
    REQUIRE(low == 1);
    REQUIRE(high == 3);

    int query_low = 6;
    int query_high = 7;
    REQUIRE(ft_interval_tree_count_overlaps(&tree, &query_low, &query_high) == 2);

    bool found = false;
    low = -1;
    high = -1;
    query_low = 2;
    query_high = 2;
    REQUIRE_STATUS(ft_interval_tree_try_find_overlap(&tree, &query_low, &query_high, &found, &low, &high), FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(low == 1);
    REQUIRE(high == 3);

    int remove_low = 5;
    int remove_high = 8;
    REQUIRE(ft_interval_tree_contains(&tree, &remove_low, &remove_high));
    ft_interval_tree removed;
    REQUIRE_STATUS(ft_interval_tree_remove_one(&tree, &remove_low, &remove_high, &removed), FT_STATUS_OK);
    REQUIRE(!ft_interval_tree_contains(&removed, &remove_low, &remove_high));
    REQUIRE(ft_interval_tree_contains(&tree, &remove_low, &remove_high));

    int invalid_low = 9;
    int invalid_high = 1;
    ft_interval_tree invalid;
    REQUIRE_STATUS(ft_interval_tree_insert(&tree, &invalid_low, &invalid_high, &invalid), FT_STATUS_INVALID_ARGUMENT);

    ft_interval_tree_dispose(&removed);
    ft_interval_tree_dispose(&tree);
}

static void test_interval_tree_max_high_descent(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    comparison_counter counter = { 0 };

    ft_interval_tree tree;
    REQUIRE_STATUS(
        ft_interval_tree_init(&tree, &int_type, compare_ints_counted, &counter),
        FT_STATUS_OK);

    for (int index = 0; index != 2048; ++index) {
        const int low = index * 2;
        const int high = index == 7 ? 200000 : low;
        ft_interval_tree next;
        REQUIRE_STATUS(ft_interval_tree_insert(&tree, &low, &high, &next), FT_STATUS_OK);
        ft_interval_tree_dispose(&tree);
        ft_interval_tree_move(&tree, &next);
    }

    /* Keep only a copied handle before querying. The max-high annotations may
     * borrow endpoint storage, so this also proves they remain valid through
     * structural sharing after the original facade/context is released. */
    ft_interval_tree snapshot;
    REQUIRE_STATUS(ft_interval_tree_copy(&tree, &snapshot), FT_STATUS_OK);
    ft_interval_tree_dispose(&tree);

    int query_low = 150000;
    int query_high = 150000;
    bool found = false;
    int overlap_low = -1;
    int overlap_high = -1;
    counter.comparisons = 0;
    REQUIRE_STATUS(
        ft_interval_tree_try_find_overlap(
            &snapshot,
            &query_low,
            &query_high,
            &found,
            &overlap_low,
            &overlap_high),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(overlap_low == 14);
    REQUIRE(overlap_high == 200000);
    REQUIRE(counter.comparisons < 128);

    counter.comparisons = 0;
    REQUIRE(ft_interval_tree_count_overlaps(&snapshot, &query_low, &query_high) == 1);
    REQUIRE(counter.comparisons < 256);

    query_low = 300000;
    query_high = 300000;
    counter.comparisons = 0;
    REQUIRE_STATUS(
        ft_interval_tree_try_find_overlap(
            &snapshot,
            &query_low,
            &query_high,
            &found,
            &overlap_low,
            &overlap_high),
        FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE(counter.comparisons < 16);

    ft_interval_tree_dispose(&snapshot);
}

static void test_text_rope(void)
{
    ft_text_rope rope;
    REQUIRE_STATUS(ft_text_rope_from_cstr("ab\ncd\n", &rope), FT_STATUS_OK);
    REQUIRE(ft_text_rope_size(&rope) == 6);
    REQUIRE(ft_text_rope_line_count(&rope) == 3);

    ft_line_column lc;
    REQUIRE_STATUS(ft_text_rope_line_column_of(&rope, 4, &lc), FT_STATUS_OK);
    REQUIRE(lc.line == 1);
    REQUIRE(lc.column == 1);

    for (size_t offset = 0; offset <= ft_text_rope_size(&rope); ++offset) {
        REQUIRE_STATUS(ft_text_rope_line_column_of(&rope, offset, &lc), FT_STATUS_OK);
        size_t round_trip = SIZE_MAX;
        REQUIRE_STATUS(ft_text_rope_offset_of(&rope, lc.line, lc.column, &round_trip), FT_STATUS_OK);
        REQUIRE(round_trip == offset);
    }

    size_t line = SIZE_MAX;
    REQUIRE_STATUS(ft_text_rope_line_of_offset(&rope, 4, &line), FT_STATUS_OK);
    REQUIRE(line == 1);
    size_t line_start = SIZE_MAX;
    REQUIRE_STATUS(ft_text_rope_line_start_offset(&rope, 1, &line_start), FT_STATUS_OK);
    REQUIRE(line_start == 3);
    size_t offset = SIZE_MAX;
    REQUIRE_STATUS(ft_text_rope_offset_of(&rope, 0, 2, &offset), FT_STATUS_OK);
    REQUIRE(offset == 2);
    REQUIRE_STATUS(ft_text_rope_offset_of(&rope, 0, 3, &offset), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_text_rope_offset_of(&rope, 2, 0, &offset), FT_STATUS_OK);
    REQUIRE(offset == 6);
    REQUIRE_STATUS(ft_text_rope_offset_of(&rope, 2, 1, &offset), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_text_rope_line_start_offset(&rope, 3, &line_start), FT_STATUS_OUT_OF_RANGE);

    ft_text_rope inserted;
    REQUIRE_STATUS(ft_text_rope_insert_char(&rope, 2, 'X', &inserted), FT_STATUS_OK);
    REQUIRE(ft_text_rope_size(&inserted) == 7);
    REQUIRE(ft_tree_size(&inserted.rope.tree) == 1);
    char value = '\0';
    REQUIRE_STATUS(ft_text_rope_at(&inserted, 2, &value), FT_STATUS_OK);
    REQUIRE(value == 'X');

    ft_text_rope removed;
    REQUIRE_STATUS(ft_text_rope_remove_at(&inserted, 2, &removed), FT_STATUS_OK);
    REQUIRE(ft_text_rope_size(&removed) == 6);
    ft_text_rope_dispose(&inserted);
    ft_text_rope_move(&inserted, &removed);
    REQUIRE(ft_text_rope_size(&inserted) == 6);

    char_buffer chars;
    chars.count = 0;
    REQUIRE_STATUS(ft_text_rope_visit(&inserted, collect_char, &chars), FT_STATUS_OK);
    REQUIRE(chars.count == 6);
    chars.values[chars.count] = '\0';
    REQUIRE(strcmp(chars.values, "ab\ncd\n") == 0);

    ft_text_rope_dispose(&inserted);
    ft_text_rope_dispose(&rope);
}

static void test_text_rope_long_edit_script(void)
{
    enum { capacity = 8192 };
    char model[capacity];
    char snapshot_model[capacity];
    size_t length = 0;

    for (int line = 0; line != 240; ++line) {
        const int written = snprintf(
            model + length,
            (size_t)capacity - length,
            "line-%03d:%c%c%c\n",
            line,
            (char)('a' + (line % 26)),
            (char)('A' + ((line * 7) % 26)),
            (char)('0' + (line % 10)));
        REQUIRE(written > 0);
        REQUIRE(length + (size_t)written < (size_t)capacity);
        length += (size_t)written;
    }

    memcpy(snapshot_model, model, length + 1);
    const size_t snapshot_length = length;

    ft_text_rope rope;
    REQUIRE_STATUS(ft_text_rope_from_cstr(model, &rope), FT_STATUS_OK);
    ft_text_rope snapshot;
    REQUIRE_STATUS(ft_text_rope_copy(&rope, &snapshot), FT_STATUS_OK);
    REQUIRE(text_rope_matches_model(&rope, model, length));

    for (int step = 0; step != 180; ++step) {
        if (step % 5 == 1 && length > 0) {
            const size_t index = ((size_t)step * 53u + 17u) % length;
            ft_text_rope next;
            REQUIRE_STATUS(ft_text_rope_remove_at(&rope, index, &next), FT_STATUS_OK);
            model_remove_at(model, &length, index);
            ft_text_rope_dispose(&rope);
            ft_text_rope_move(&rope, &next);
        } else {
            const size_t index = ((size_t)step * 97u + 11u) % (length + 1u);
            const char value = step % 5 == 0 ? '\n' : (char)('!' + (step % 57));
            ft_text_rope next;
            REQUIRE_STATUS(ft_text_rope_insert_char(&rope, index, value, &next), FT_STATUS_OK);
            model_insert_char(model, &length, index, value);
            ft_text_rope_dispose(&rope);
            ft_text_rope_move(&rope, &next);
        }

        if (step % 17 == 0) {
            REQUIRE(text_rope_matches_model(&rope, model, length));
            REQUIRE(text_rope_matches_model(&snapshot, snapshot_model, snapshot_length));
        }
    }

    REQUIRE(text_rope_matches_model(&rope, model, length));
    REQUIRE(text_rope_matches_model(&snapshot, snapshot_model, snapshot_length));
    const size_t ideal_chunks = (length + rope.rope.max_chunk_length - 1u) / rope.rope.max_chunk_length;
    REQUIRE(ft_tree_size(&rope.rope.tree) <= ideal_chunks + 1u);
    for (size_t probe = 0; probe <= length; probe += 113u) {
        ft_line_column lc;
        REQUIRE_STATUS(ft_text_rope_line_column_of(&rope, probe, &lc), FT_STATUS_OK);
        size_t round_trip = SIZE_MAX;
        REQUIRE_STATUS(ft_text_rope_offset_of(&rope, lc.line, lc.column, &round_trip), FT_STATUS_OK);
        REQUIRE(round_trip == probe);
    }

    ft_text_rope_dispose(&snapshot);
    ft_text_rope_dispose(&rope);
}

static void test_sequence_cursors(void)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_measure_policy sum_measure;
    init_int_sum_measure(&sum_measure);
    ft_tree_policy sum_policy = {int_type, sum_measure};

    ft_tree tree;
    REQUIRE_STATUS(ft_tree_init(&tree, &sum_policy), FT_STATUS_OK);
    const int measured_values[] = {2, 3, 5, 7};
    for (size_t index = 0; index != 4; ++index) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&tree, &measured_values[index], &next), FT_STATUS_OK);
        ft_tree_dispose(&tree);
        tree = next;
    }

    int threshold = 6;
    bool found = false;
    ft_tree_cursor measured = {0};
    REQUIRE_STATUS(
        ft_tree_get_cursor_by_measure(
            &tree,
            int_sum_reaches,
            &threshold,
            &found,
            &measured),
        FT_STATUS_OK);
    REQUIRE(found);
    int before = 0;
    int after = 0;
    int value = 0;
    REQUIRE_STATUS(ft_tree_cursor_measure_before(&measured, &before), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_cursor_measure_after(&measured, &after), FT_STATUS_OK);
    REQUIRE(before == 5 && after == 12);
    REQUIRE_STATUS(ft_tree_cursor_try_peek_next(&measured, &found, &value), FT_STATUS_OK);
    REQUIRE(found && value == 5);

    const int eleven = 11;
    const int thirteen = 13;
    ft_tree_cursor inserted = {0};
    ft_tree_cursor deleted = {0};
    ft_tree_cursor edited = {0};
    REQUIRE_STATUS(ft_tree_cursor_insert(&measured, &eleven, &inserted), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_cursor_delete_next(&inserted, &deleted), FT_STATUS_OK);
    REQUIRE_STATUS(ft_tree_cursor_replace_next(&deleted, &thirteen, &edited), FT_STATUS_OK);
    const int measured_expected[] = {2, 3, 11, 13};
    for (size_t index = 0; index != 4; ++index) {
        value = 0;
        REQUIRE_STATUS(ft_tree_at(&edited.tree, index, &value), FT_STATUS_OK);
        REQUIRE(value == measured_expected[index]);
        value = 0;
        REQUIRE_STATUS(ft_tree_at(&tree, index, &value), FT_STATUS_OK);
        REQUIRE(value == measured_values[index]);
    }

    ft_tree_policy deque_policy;
    init_int_policy(&deque_policy);
    ft_persistent_deque deque;
    REQUIRE_STATUS(ft_persistent_deque_init(&deque, &deque_policy), FT_STATUS_OK);
    const int deque_values[] = {1, 0, 3};
    for (size_t index = 0; index != 3; ++index) {
        ft_tree next;
        REQUIRE_STATUS(ft_tree_push_back(&deque, &deque_values[index], &next), FT_STATUS_OK);
        ft_tree_dispose(&deque);
        deque = next;
    }
    ft_persistent_deque_cursor deque_cursor = {0};
    REQUIRE_STATUS(ft_persistent_deque_get_cursor(&deque, 2, &deque_cursor), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_persistent_deque_cursor_try_peek_previous(&deque_cursor, &found, &value),
        FT_STATUS_OK);
    REQUIRE(found && value == 0);
    const int range[] = {7, 8};
    ft_persistent_deque_cursor deque_inserted = {0};
    ft_persistent_deque_cursor deque_deleted = {0};
    ft_persistent_deque_cursor deque_edited = {0};
    REQUIRE_STATUS(
        ft_persistent_deque_cursor_insert_array(&deque_cursor, range, 2, &deque_inserted),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_persistent_deque_cursor_delete_previous(&deque_inserted, &deque_deleted),
        FT_STATUS_OK);
    const int nine = 9;
    REQUIRE_STATUS(
        ft_persistent_deque_cursor_replace_next(&deque_deleted, &nine, &deque_edited),
        FT_STATUS_OK);
    const int deque_expected[] = {1, 0, 7, 9};
    REQUIRE(ft_persistent_deque_cursor_position(&deque_edited) == 3);
    REQUIRE(ft_persistent_deque_cursor_size(&deque_edited) == 4);
    for (size_t index = 0; index != 4; ++index) {
        value = 0;
        REQUIRE_STATUS(ft_tree_at(&deque_edited.tree, index, &value), FT_STATUS_OK);
        REQUIRE(value == deque_expected[index]);
    }
    REQUIRE(ft_tree_size(&deque) == 3);

    ft_reversible_deque reversible;
    REQUIRE_STATUS(reversible_deque_from_range(&deque_policy, 1, 4, &reversible), FT_STATUS_OK);
    ft_reversible_deque reversed;
    REQUIRE_STATUS(ft_reversible_deque_reverse(&reversible, &reversed), FT_STATUS_OK);
    ft_reversible_deque_cursor reversible_cursor = {0};
    REQUIRE_STATUS(ft_reversible_deque_get_cursor(&reversed, 1, &reversible_cursor), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_reversible_deque_cursor_try_peek_previous(&reversible_cursor, &found, &value),
        FT_STATUS_OK);
    REQUIRE(found && value == 4);
    REQUIRE_STATUS(
        ft_reversible_deque_cursor_try_peek_next(&reversible_cursor, &found, &value),
        FT_STATUS_OK);
    REQUIRE(found && value == 3);
    ft_reversible_deque_cursor reversible_inserted = {0};
    ft_reversible_deque_cursor reversible_deleted = {0};
    ft_reversible_deque_cursor reversible_rereversed = {0};
    REQUIRE_STATUS(
        ft_reversible_deque_cursor_insert(&reversible_cursor, &nine, &reversible_inserted),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_reversible_deque_cursor_delete_next(&reversible_inserted, &reversible_deleted),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_reversible_deque_cursor_reverse(&reversible_deleted, &reversible_rereversed),
        FT_STATUS_OK);
    const int reverse_expected[] = {1, 2, 9, 4};
    REQUIRE(ft_reversible_deque_cursor_position(&reversible_rereversed) == 2);
    REQUIRE(reversible_deque_matches(
        &reversible_rereversed.deque,
        reverse_expected,
        4));

    ft_reversible_deque_cursor_dispose(&reversible_rereversed);
    ft_reversible_deque_cursor_dispose(&reversible_deleted);
    ft_reversible_deque_cursor_dispose(&reversible_inserted);
    ft_reversible_deque_cursor_dispose(&reversible_cursor);
    ft_reversible_deque_dispose(&reversed);
    ft_reversible_deque_dispose(&reversible);
    ft_persistent_deque_cursor_dispose(&deque_edited);
    ft_persistent_deque_cursor_dispose(&deque_deleted);
    ft_persistent_deque_cursor_dispose(&deque_inserted);
    ft_persistent_deque_cursor_dispose(&deque_cursor);
    ft_persistent_deque_dispose(&deque);
    ft_tree_cursor_dispose(&edited);
    ft_tree_cursor_dispose(&deleted);
    ft_tree_cursor_dispose(&inserted);
    ft_tree_cursor_dispose(&measured);
    ft_tree_dispose(&tree);
}

static void run_test(const char* name, void (*test)(void))
{
    const int before = g_failures;
    test();
    if (g_failures == before) {
        (void)printf("[pass] %s\n", name);
        (void)fflush(stdout);
    } else {
        (void)fprintf(stderr, "[fail] %s\n", name);
        (void)fflush(stderr);
    }
}

int main(void)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    run_test("concurrent snapshot refcounts", test_concurrent_snapshot_refcounts);
    run_test("reversible deque", test_reversible_deque);
    run_test("tree endpoint/index/split/concat", test_tree_endpoint_index_split_and_concat);
    run_test("lazy middle force paths", test_lazy_middle_force_paths);
    run_test("measure locate and split", test_measure_locate_and_split);
    run_test("structural split and locate costs", test_structural_split_and_locate_costs);
    run_test("sorted set and multiset", test_sorted_set_and_multiset);
    run_test("sorted facade structural bounds", test_sorted_facade_structural_bounds);
    run_test("sorted map", test_sorted_map);
    run_test("ordered-search cursors", test_ordered_search_cursors);
    run_test("rope", test_rope);
    run_test("rope cursor", test_rope_cursor);
    run_test("rope cursor model", test_rope_cursor_model);
    run_test("rope cursor concurrent readers", test_rope_cursor_concurrent_readers);
    run_test("rope chunk boundaries", test_rope_chunk_boundaries);
    run_test("measured rope", test_measured_rope);
    run_test("measured rope cursor", test_measured_rope_cursor);
    run_test("measured rope cursor ordered measure", test_measured_rope_cursor_ordered_measure);
    run_test("measured rope cursor model", test_measured_rope_cursor_model);
    run_test("measured rope cursor concurrent readers", test_measured_rope_cursor_concurrent_readers);
    run_test("priority queue", test_priority_queue);
    run_test("interval tree", test_interval_tree);
    run_test("interval tree equal-low tie order", test_interval_tree_equal_low_tie_order);
    run_test("generic interval tree", test_generic_interval_tree);
    run_test("interval tree max-high descent", test_interval_tree_max_high_descent);
    run_test("text rope", test_text_rope);
    run_test("text rope cursor", test_text_rope_cursor);
    run_test("text rope long edit script", test_text_rope_long_edit_script);
    run_test("persistent sequence cursors", test_sequence_cursors);
    run_test("exact source/result aliasing", test_exact_source_result_aliasing);

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    (void)printf("all C FingerTree tests passed\n");
    return 0;
}
