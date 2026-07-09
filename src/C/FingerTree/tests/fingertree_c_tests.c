#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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
    for (size_t index = 0; index < 3000; index += 257) {
        int actual = -1;
        REQUIRE_STATUS(ft_rope_at(&joined, index, &actual), FT_STATUS_OK);
        REQUIRE(actual == (int)index);
    }

    int inserted_value = 7777;
    ft_rope inserted;
    REQUIRE_STATUS(ft_rope_insert_at(&joined, 3, &inserted_value, &inserted), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&inserted) == 3001);
    int actual = -1;
    REQUIRE_STATUS(ft_rope_at(&inserted, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == inserted_value);
    REQUIRE_STATUS(ft_rope_at(&joined, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == 3);

    ft_rope removed;
    REQUIRE_STATUS(ft_rope_remove_at(&inserted, 3, &removed), FT_STATUS_OK);
    REQUIRE(ft_rope_size(&removed) == 3000);
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
    REQUIRE_STATUS(ft_measured_rope_measure(&joined, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum);

    int inserted_value = 7;
    ft_measured_rope inserted;
    REQUIRE_STATUS(ft_measured_rope_insert_at(&joined, 3, &inserted_value, &inserted), FT_STATUS_OK);
    REQUIRE(ft_measured_rope_size(&inserted) == 3001);
    REQUIRE_STATUS(ft_measured_rope_measure(&inserted, &measure), FT_STATUS_OK);
    REQUIRE(measure == expected_sum + inserted_value);
    REQUIRE_STATUS(ft_measured_rope_at(&joined, 3, &actual), FT_STATUS_OK);
    REQUIRE(actual == 1);

    ft_measured_rope removed;
    REQUIRE_STATUS(ft_measured_rope_remove_at(&inserted, 3, &removed), FT_STATUS_OK);
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

    ft_text_rope inserted;
    REQUIRE_STATUS(ft_text_rope_insert_char(&rope, 2, 'X', &inserted), FT_STATUS_OK);
    REQUIRE(ft_text_rope_size(&inserted) == 7);
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

    ft_text_rope_dispose(&snapshot);
    ft_text_rope_dispose(&rope);
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
    run_test("concurrent snapshot refcounts", test_concurrent_snapshot_refcounts);
    run_test("reversible deque", test_reversible_deque);
    run_test("tree endpoint/index/split/concat", test_tree_endpoint_index_split_and_concat);
    run_test("lazy middle force paths", test_lazy_middle_force_paths);
    run_test("measure locate and split", test_measure_locate_and_split);
    run_test("sorted set and multiset", test_sorted_set_and_multiset);
    run_test("sorted map", test_sorted_map);
    run_test("rope", test_rope);
    run_test("measured rope", test_measured_rope);
    run_test("priority queue", test_priority_queue);
    run_test("interval tree", test_interval_tree);
    run_test("interval tree equal-low tie order", test_interval_tree_equal_low_tie_order);
    run_test("generic interval tree", test_generic_interval_tree);
    run_test("text rope", test_text_rope);
    run_test("text rope long edit script", test_text_rope_long_edit_script);

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    (void)printf("all C FingerTree tests passed\n");
    return 0;
}
