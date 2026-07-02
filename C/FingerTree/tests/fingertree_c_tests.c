#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct map_buffer {
    int keys[128];
    int values[128];
    size_t count;
} map_buffer;

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

static void collect_int(const void* value, void* context)
{
    int_buffer* buffer = (int_buffer*)context;
    buffer->values[buffer->count] = *(const int*)value;
    ++buffer->count;
}

static void collect_char(const void* value, void* context)
{
    char_buffer* buffer = (char_buffer*)context;
    buffer->values[buffer->count] = *(const char*)value;
    ++buffer->count;
}

static void collect_map_entry(const void* key, const void* value, void* context)
{
    map_buffer* buffer = (map_buffer*)context;
    buffer->keys[buffer->count] = *(const int*)key;
    buffer->values[buffer->count] = *(const int*)value;
    ++buffer->count;
}

static bool size_reaches(const void* measure, void* context)
{
    const size_t value = *(const size_t*)measure;
    const size_t threshold = *(const size_t*)context;
    return value >= threshold;
}

static void test_reversible_deque(void)
{
    ft_tree_policy policy;
    init_int_policy(&policy);

    ft_reversible_deque deque;
    REQUIRE_STATUS(ft_reversible_deque_init(&deque, &policy), FT_STATUS_OK);
    for (int value = 0; value != 5; ++value) {
        ft_reversible_deque next;
        REQUIRE_STATUS(ft_reversible_deque_push_back(&deque, &value, &next), FT_STATUS_OK);
        ft_reversible_deque_dispose(&deque);
        deque = next;
    }

    ft_reversible_deque reversed;
    REQUIRE_STATUS(ft_reversible_deque_reverse(&deque, &reversed), FT_STATUS_OK);
    REQUIRE(ft_reversible_deque_size(&reversed) == 5);
    for (int index = 0; index != 5; ++index) {
        int actual = -1;
        REQUIRE_STATUS(ft_reversible_deque_at(&reversed, (size_t)index, &actual), FT_STATUS_OK);
        REQUIRE(actual == 4 - index);
    }

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
    ft_tree_dispose(&joined);
    ft_tree_dispose(&split.left);
    ft_tree_dispose(&split.right);
    ft_tree_dispose(&snapshot);
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
        queue = next;
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
        queue = rest;
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
        map = next;
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
        tree = next;
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

    char_buffer chars;
    chars.count = 0;
    REQUIRE_STATUS(ft_text_rope_visit(&removed, collect_char, &chars), FT_STATUS_OK);
    REQUIRE(chars.count == 6);
    chars.values[chars.count] = '\0';
    REQUIRE(strcmp(chars.values, "ab\ncd\n") == 0);

    ft_text_rope_dispose(&removed);
    ft_text_rope_dispose(&inserted);
    ft_text_rope_dispose(&rope);
}

static void run_test(const char* name, void (*test)(void))
{
    const int before = g_failures;
    test();
    if (g_failures == before) {
        (void)printf("[pass] %s\n", name);
    } else {
        (void)fprintf(stderr, "[fail] %s\n", name);
    }
}

int main(void)
{
    run_test("reversible deque", test_reversible_deque);
    run_test("tree endpoint/index/split/concat", test_tree_endpoint_index_split_and_concat);
    run_test("measure locate and split", test_measure_locate_and_split);
    run_test("sorted set and multiset", test_sorted_set_and_multiset);
    run_test("sorted map", test_sorted_map);
    run_test("priority queue", test_priority_queue);
    run_test("interval tree", test_interval_tree);
    run_test("text rope", test_text_rope);

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    (void)printf("all C FingerTree tests passed\n");
    return 0;
}
