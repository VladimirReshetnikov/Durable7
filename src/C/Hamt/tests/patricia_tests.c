/*
 * Tests for the persistent Patricia maps and sets, including signed key ordering.
 */

#include <durable7/hamt/patricia.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail_at(const char *file, int line, const char *expression)
{
    fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    exit(1);
}

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fail_at(__FILE__, __LINE__, #expression); \
        } \
    } while (0)

#define CHECK_STATUS(expression) \
    do { \
        const d7_hamt_status status_value = (expression); \
        if (status_value != D7_HAMT_OK) { \
            fprintf(stderr, "%s:%d: %s returned status %d\n", \
                __FILE__, __LINE__, #expression, (int)status_value); \
            exit(1); \
        } \
    } while (0)

typedef struct int_visit_state {
    int32_t keys[32];
    const void *values[32];
    size_t count;
} int_visit_state;

typedef struct long_visit_state {
    int64_t keys[32];
    size_t count;
} long_visit_state;

static void collect_int_map(int32_t key, const void *value, void *context)
{
    int_visit_state *state = (int_visit_state *)context;
    CHECK(state->count < sizeof(state->keys) / sizeof(state->keys[0]));
    state->keys[state->count] = key;
    state->values[state->count] = value;
    ++state->count;
}

static void collect_int_set(int32_t value, void *context)
{
    int_visit_state *state = (int_visit_state *)context;
    CHECK(state->count < sizeof(state->keys) / sizeof(state->keys[0]));
    state->keys[state->count++] = value;
}

static void collect_long_map(int64_t key, const void *value, void *context)
{
    long_visit_state *state = (long_visit_state *)context;
    (void)value;
    CHECK(state->count < sizeof(state->keys) / sizeof(state->keys[0]));
    state->keys[state->count++] = key;
}

static void collect_long_set(int64_t value, void *context)
{
    long_visit_state *state = (long_visit_state *)context;
    CHECK(state->count < sizeof(state->keys) / sizeof(state->keys[0]));
    state->keys[state->count++] = value;
}

static void test_signed_order_and_persistence(void)
{
    static const int values[] = { 11, 12, 13, 14, 15 };
    static const int32_t keys[] = { INT32_MAX, 0, INT32_MIN, -1, 1 };
    static const int32_t expected[] = { INT32_MIN, -1, 0, 1, INT32_MAX };
    d7_int_map map = d7_int_map_create(NULL);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        CHECK_STATUS(d7_int_map_set(&map, keys[i], &values[i], &map));
    }
    CHECK(d7_int_map_count(&map) == 5);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const void *found = NULL;
        CHECK(d7_int_map_try_get(&map, keys[i], &found));
        CHECK(found == &values[i]);
    }

    int_visit_state visit = { { 0 }, { NULL }, 0 };
    d7_int_map_visit(&map, collect_int_map, &visit);
    CHECK(visit.count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == expected[i]);

    d7_int_map no_change;
    CHECK_STATUS(d7_int_map_set(&map, 0, &values[1], &no_change));
    CHECK(d7_int_map_shares_root(&map, &no_change));
    d7_int_map_destroy(&no_change);

    CHECK_STATUS(d7_int_map_remove(&map, 42, &no_change));
    CHECK(d7_int_map_shares_root(&map, &no_change));
    d7_int_map_destroy(&no_change);

    d7_int_map snapshot = d7_int_map_clone(&map);
    CHECK_STATUS(d7_int_map_remove(&map, INT32_MIN, &map));
    CHECK(!d7_int_map_try_get(&map, INT32_MIN, NULL));
    CHECK(d7_int_map_try_get(&snapshot, INT32_MIN, NULL));
    CHECK(d7_int_map_count(&map) == 4);
    CHECK(d7_int_map_count(&snapshot) == 5);
    d7_int_map_destroy(&snapshot);
    d7_int_map_destroy(&map);

    static const int64_t long_keys[] = { INT64_MAX, 0, INT64_MIN, -1, 1 };
    static const int64_t long_expected[] = { INT64_MIN, -1, 0, 1, INT64_MAX };
    d7_long_map long_map = d7_long_map_create(NULL);
    for (size_t i = 0; i < sizeof(long_keys) / sizeof(long_keys[0]); ++i) {
        CHECK_STATUS(d7_long_map_set(&long_map, long_keys[i], &values[i], &long_map));
    }
    long_visit_state long_visit = { { 0 }, 0 };
    d7_long_map_visit(&long_map, collect_long_map, &long_visit);
    CHECK(long_visit.count == sizeof(long_expected) / sizeof(long_expected[0]));
    for (size_t i = 0; i < long_visit.count; ++i) CHECK(long_visit.keys[i] == long_expected[i]);
    d7_long_map_destroy(&long_map);
}

typedef struct combine_state {
    int values[32];
    size_t calls;
} combine_state;

static const void *sum_int_values(
    int32_t key, const void *left, const void *right, void *context)
{
    combine_state *state = (combine_state *)context;
    const size_t index = (size_t)(key + 16);
    CHECK(index < sizeof(state->values) / sizeof(state->values[0]));
    state->values[index] = *(const int *)left + *(const int *)right;
    ++state->calls;
    return &state->values[index];
}

static const void *choose_left_value(
    int32_t key, const void *left, const void *right, void *context)
{
    (void)key;
    (void)right;
    (void)context;
    return left;
}

static const void *choose_right_value(
    int32_t key, const void *left, const void *right, void *context)
{
    (void)key;
    (void)left;
    (void)context;
    return right;
}

static void test_map_algebra(void)
{
    static const int left_minus = 10;
    static const int left_zero = 20;
    static const int left_plus = 30;
    static const int right_zero = 200;
    static const int right_seven = 70;
    d7_int_map left = d7_int_map_create(NULL);
    d7_int_map right = d7_int_map_create(NULL);
    CHECK_STATUS(d7_int_map_set(&left, -5, &left_minus, &left));
    CHECK_STATUS(d7_int_map_set(&left, 0, &left_zero, &left));
    CHECK_STATUS(d7_int_map_set(&left, 5, &left_plus, &left));
    CHECK_STATUS(d7_int_map_set(&right, 0, &right_zero, &right));
    CHECK_STATUS(d7_int_map_set(&right, 7, &right_seven, &right));

    d7_int_map united;
    d7_int_map common;
    d7_int_map difference;
    CHECK_STATUS(d7_int_map_union(&left, &right, &united));
    CHECK_STATUS(d7_int_map_intersect(&left, &right, &common));
    CHECK_STATUS(d7_int_map_except(&left, &right, &difference));
    CHECK(d7_int_map_count(&united) == 4);
    CHECK(d7_int_map_count(&common) == 1);
    CHECK(d7_int_map_count(&difference) == 2);

    const void *found = NULL;
    CHECK(d7_int_map_try_get(&united, 0, &found));
    CHECK(found == &right_zero);
    CHECK(d7_int_map_try_get(&common, 0, &found));
    CHECK(found == &left_zero);
    CHECK(d7_int_map_try_get(&difference, -5, &found) && found == &left_minus);
    CHECK(d7_int_map_try_get(&difference, 5, &found) && found == &left_plus);

    combine_state combined = { { 0 }, 0 };
    d7_int_map summed_union;
    d7_int_map summed_intersection;
    CHECK_STATUS(d7_int_map_union_with(
        &left, &right, sum_int_values, &combined, &summed_union));
    CHECK(combined.calls == 1);
    CHECK(d7_int_map_try_get(&summed_union, 0, &found));
    CHECK(*(const int *)found == left_zero + right_zero);
    combined.calls = 0;
    CHECK_STATUS(d7_int_map_intersect_with(
        &left, &right, sum_int_values, &combined, &summed_intersection));
    CHECK(combined.calls == 1);
    CHECK(d7_int_map_count(&summed_intersection) == 1);
    CHECK(d7_int_map_try_get(&summed_intersection, 0, &found));
    CHECK(*(const int *)found == left_zero + right_zero);

    d7_int_map no_change;
    CHECK_STATUS(d7_int_map_union_with(
        &left, &left, choose_left_value, NULL, &no_change));
    CHECK(d7_int_map_shares_root(&left, &no_change));
    d7_int_map_destroy(&no_change);
    CHECK_STATUS(d7_int_map_intersect_with(
        &left, &left, choose_left_value, NULL, &no_change));
    CHECK(d7_int_map_shares_root(&left, &no_change));
    d7_int_map_destroy(&no_change);

    d7_int_map one_left = d7_int_map_create(NULL);
    d7_int_map one_right = d7_int_map_create(NULL);
    CHECK_STATUS(d7_int_map_set(&one_left, 0, &left_zero, &one_left));
    CHECK_STATUS(d7_int_map_set(&one_right, 0, &right_zero, &one_right));
    CHECK_STATUS(d7_int_map_intersect_with(
        &one_left, &one_right, choose_right_value, NULL, &no_change));
    CHECK(d7_int_map_shares_root(&one_right, &no_change));
    d7_int_map_destroy(&no_change);
    d7_int_map_destroy(&one_right);
    d7_int_map_destroy(&one_left);

    d7_int_map alias_right = d7_int_map_clone(&right);
    CHECK_STATUS(d7_int_map_union(&left, &alias_right, &alias_right));
    CHECK(d7_int_map_count(&alias_right) == 4);
    CHECK(d7_int_map_try_get(&alias_right, 0, &found) && found == &right_zero);

    d7_int_map_destroy(&alias_right);
    d7_int_map_destroy(&summed_intersection);
    d7_int_map_destroy(&summed_union);
    d7_int_map_destroy(&difference);
    d7_int_map_destroy(&common);
    d7_int_map_destroy(&united);
    d7_int_map_destroy(&right);
    d7_int_map_destroy(&left);
}

static void test_set_algebra(void)
{
    static const int32_t left_values[] = { INT32_MIN, -3, 5, INT32_MAX };
    static const int32_t right_values[] = { -3, 0, INT32_MAX };
    static const int32_t union_expected[] = { INT32_MIN, -3, 0, 5, INT32_MAX };
    static const int32_t intersect_expected[] = { -3, INT32_MAX };
    static const int32_t except_expected[] = { INT32_MIN, 5 };
    d7_int_set left = d7_int_set_create();
    d7_int_set right = d7_int_set_create();
    for (size_t i = 0; i < sizeof(left_values) / sizeof(left_values[0]); ++i)
        CHECK_STATUS(d7_int_set_add(&left, left_values[i], &left));
    for (size_t i = 0; i < sizeof(right_values) / sizeof(right_values[0]); ++i)
        CHECK_STATUS(d7_int_set_add(&right, right_values[i], &right));

    d7_int_set united;
    d7_int_set common;
    d7_int_set difference;
    CHECK_STATUS(d7_int_set_union(&left, &right, &united));
    CHECK_STATUS(d7_int_set_intersect(&left, &right, &common));
    CHECK_STATUS(d7_int_set_except(&left, &right, &difference));
    CHECK(d7_int_set_count(&united) == sizeof(union_expected) / sizeof(union_expected[0]));
    CHECK(d7_int_set_count(&common) == sizeof(intersect_expected) / sizeof(intersect_expected[0]));
    CHECK(d7_int_set_count(&difference) == sizeof(except_expected) / sizeof(except_expected[0]));

    int_visit_state visit = { { 0 }, { NULL }, 0 };
    d7_int_set_visit(&united, collect_int_set, &visit);
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == union_expected[i]);
    visit.count = 0;
    d7_int_set_visit(&common, collect_int_set, &visit);
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == intersect_expected[i]);
    visit.count = 0;
    d7_int_set_visit(&difference, collect_int_set, &visit);
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == except_expected[i]);

    d7_int_set_destroy(&difference);
    d7_int_set_destroy(&common);
    d7_int_set_destroy(&united);
    d7_int_set_destroy(&right);
    d7_int_set_destroy(&left);

    d7_long_set longs = d7_long_set_create();
    CHECK_STATUS(d7_long_set_add(&longs, INT64_MAX, &longs));
    CHECK_STATUS(d7_long_set_add(&longs, INT64_MIN, &longs));
    CHECK_STATUS(d7_long_set_add(&longs, 0, &longs));
    long_visit_state long_visit = { { 0 }, 0 };
    d7_long_set_visit(&longs, collect_long_set, &long_visit);
    CHECK(long_visit.count == 3);
    CHECK(long_visit.keys[0] == INT64_MIN);
    CHECK(long_visit.keys[1] == 0);
    CHECK(long_visit.keys[2] == INT64_MAX);
    d7_long_set_destroy(&longs);
}

static void test_persistent_cursors(void)
{
    static const int minimum_value = 10;
    static const int negative_value = 20;
    static const int positive_value = 40;
    static const int maximum_value = 50;
    static const int inserted_value = 30;
    static const int replacement_value = 25;
    static const int put_value = 26;
    d7_int_map map = d7_int_map_create(NULL);
    CHECK_STATUS(d7_int_map_set(&map, INT32_MAX, &maximum_value, &map));
    CHECK_STATUS(d7_int_map_set(&map, 7, &positive_value, &map));
    CHECK_STATUS(d7_int_map_set(&map, 0, NULL, &map));
    CHECK_STATUS(d7_int_map_set(&map, -5, &negative_value, &map));
    CHECK_STATUS(d7_int_map_set(&map, INT32_MIN, &minimum_value, &map));

    d7_int_map_cursor start;
    CHECK_STATUS(d7_int_map_cursor_at_start(&map, &start));
    CHECK(d7_int_map_cursor_count(&start) == 5);
    CHECK(d7_int_map_cursor_position(&start) == 0);
    CHECK(d7_int_map_cursor_is_at_start(&start));
    int32_t key = 0;
    const void *value = NULL;
    CHECK(!d7_int_map_cursor_try_peek_previous(&start, &key, &value));
    CHECK(d7_int_map_cursor_try_peek_next(&start, &key, &value));
    CHECK(key == INT32_MIN && value == &minimum_value);
    CHECK(d7_int_map_cursor_move_previous(&start, &start)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_int_map_cursor_create(&map, 6, &start)
        == D7_HAMT_INVALID_ARGUMENT);

    bool found = false;
    d7_int_map_cursor exact;
    CHECK_STATUS(d7_int_map_cursor_at_key(&map, 0, &found, &exact));
    CHECK(found);
    CHECK(d7_int_map_cursor_position(&exact) == 2);
    value = &minimum_value;
    CHECK(d7_int_map_cursor_try_peek_next(&exact, &key, &value));
    CHECK(key == 0 && value == NULL);
    CHECK(d7_int_map_cursor_try_peek_previous(&exact, &key, &value));
    CHECK(key == -5 && value == &negative_value);

    d7_int_map_cursor miss;
    CHECK_STATUS(d7_int_map_cursor_at_key(&map, 3, &found, &miss));
    CHECK(!found);
    CHECK(d7_int_map_cursor_position(&miss) == 3);
    CHECK(d7_int_map_cursor_try_peek_next(&miss, &key, &value));
    CHECK(key == 7);

    d7_int_map_cursor bound;
    CHECK_STATUS(d7_int_map_cursor_lower_bound(&map, 0, &bound));
    CHECK(d7_int_map_cursor_position(&bound) == 2);
    d7_int_map_cursor_destroy(&bound);
    CHECK_STATUS(d7_int_map_cursor_upper_bound(&map, 0, &bound));
    CHECK(d7_int_map_cursor_position(&bound) == 3);
    d7_int_map_cursor_destroy(&bound);
    CHECK_STATUS(d7_int_map_cursor_lower_bound(&map, INT32_MAX, &bound));
    CHECK(d7_int_map_cursor_position(&bound) == 4);
    d7_int_map_cursor_destroy(&bound);
    CHECK_STATUS(d7_int_map_cursor_upper_bound(&map, INT32_MAX, &bound));
    CHECK(d7_int_map_cursor_position(&bound) == 5);
    d7_int_map_cursor_destroy(&bound);

    d7_int_map_cursor inserted;
    CHECK_STATUS(d7_int_map_cursor_insert(
        &miss, 3, &inserted_value, &inserted));
    CHECK(d7_int_map_cursor_position(&inserted) == 4);
    CHECK(d7_int_map_cursor_try_peek_previous(&inserted, &key, &value));
    CHECK(key == 3 && value == &inserted_value);
    CHECK(d7_int_map_cursor_try_peek_next(&inserted, &key, &value));
    CHECK(key == 7);
    CHECK(!d7_int_map_try_get(&map, 3, NULL));

    d7_int_map inserted_snapshot;
    CHECK_STATUS(d7_int_map_cursor_snapshot(&inserted, &inserted_snapshot));
    CHECK(d7_int_map_try_get(&inserted_snapshot, 3, &value));
    CHECK(value == &inserted_value);
    d7_int_map_destroy(&inserted_snapshot);
    d7_int_map clean_snapshot;
    CHECK_STATUS(d7_int_map_cursor_snapshot(&start, &clean_snapshot));
    CHECK(d7_int_map_shares_root(&map, &clean_snapshot));
    d7_int_map_destroy(&clean_snapshot);

    CHECK(d7_int_map_cursor_insert(&miss, 7, &positive_value, &bound)
        == D7_HAMT_DUPLICATE_KEY);
    CHECK(d7_int_map_cursor_insert(&start, 3, &inserted_value, &start)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_int_map_cursor_position(&start) == 0);

    d7_int_map_cursor updated;
    CHECK_STATUS(d7_int_map_cursor_set_next_value(
        &exact, &replacement_value, &updated));
    CHECK(d7_int_map_cursor_position(&updated) == 2);
    CHECK(d7_int_map_cursor_try_peek_next(&updated, &key, &value));
    CHECK(key == 0 && value == &replacement_value);
    value = &minimum_value;
    CHECK(d7_int_map_cursor_try_peek_next(&exact, &key, &value));
    CHECK(key == 0 && value == NULL);

    d7_int_map_cursor put;
    CHECK_STATUS(d7_int_map_cursor_put(&exact, 0, &put_value, &put));
    CHECK(d7_int_map_cursor_position(&put) == 2);
    CHECK(d7_int_map_cursor_try_peek_next(&put, &key, &value));
    CHECK(value == &put_value);
    d7_int_map_cursor_destroy(&put);
    CHECK_STATUS(d7_int_map_cursor_put(&miss, 3, &inserted_value, &put));
    CHECK(d7_int_map_cursor_position(&put) == 4);
    d7_int_map_cursor_destroy(&put);

    d7_int_map_cursor deleted;
    CHECK_STATUS(d7_int_map_cursor_delete_next(&exact, &deleted));
    CHECK(d7_int_map_cursor_position(&deleted) == 2);
    CHECK(d7_int_map_cursor_try_peek_next(&deleted, &key, &value));
    CHECK(key == 7);
    d7_int_map_cursor_destroy(&deleted);
    CHECK_STATUS(d7_int_map_cursor_delete_previous(&exact, &deleted));
    CHECK(d7_int_map_cursor_position(&deleted) == 1);
    CHECK(d7_int_map_cursor_try_peek_previous(&deleted, &key, &value));
    CHECK(key == INT32_MIN);
    CHECK(d7_int_map_cursor_try_peek_next(&deleted, &key, &value));
    CHECK(key == 0);
    d7_int_map_cursor_destroy(&deleted);

    d7_int_map_cursor end;
    CHECK_STATUS(d7_int_map_cursor_at_end(&map, &end));
    CHECK(d7_int_map_cursor_is_at_end(&end));
    CHECK(d7_int_map_cursor_try_peek_previous(&end, &key, &value));
    CHECK(key == INT32_MAX);
    CHECK(!d7_int_map_cursor_try_peek_next(&end, &key, &value));
    CHECK(d7_int_map_cursor_move_next(&end, &end)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_int_map_cursor_delete_next(&end, &end)
        == D7_HAMT_INVALID_ARGUMENT);

    for (int32_t probe = -10; probe <= 10; ++probe) {
        size_t expected = 0;
        static const int32_t ordered[] = { INT32_MIN, -5, 0, 7, INT32_MAX };
        while (expected < sizeof(ordered) / sizeof(ordered[0])
            && ordered[expected] < probe) ++expected;
        CHECK_STATUS(d7_int_map_cursor_lower_bound(&map, probe, &bound));
        CHECK(d7_int_map_cursor_position(&bound) == expected);
        d7_int_map_cursor_destroy(&bound);
    }

    d7_long_set set = d7_long_set_create();
    CHECK_STATUS(d7_long_set_add(&set, INT64_MAX, &set));
    CHECK_STATUS(d7_long_set_add(&set, 0, &set));
    CHECK_STATUS(d7_long_set_add(&set, INT64_MIN, &set));
    d7_long_set_cursor set_exact;
    CHECK_STATUS(d7_long_set_cursor_at_item(&set, 0, &found, &set_exact));
    CHECK(found && d7_long_set_cursor_position(&set_exact) == 1);
    int64_t long_value = -1;
    CHECK(d7_long_set_cursor_try_peek_next(&set_exact, &long_value));
    CHECK(long_value == 0);
    d7_long_set_cursor duplicate;
    CHECK_STATUS(d7_long_set_cursor_insert(&set_exact, 0, &duplicate));
    d7_long_set duplicate_snapshot;
    CHECK_STATUS(d7_long_set_cursor_snapshot(&duplicate, &duplicate_snapshot));
    CHECK(duplicate_snapshot.map.root == set.map.root);
    d7_long_set_destroy(&duplicate_snapshot);
    d7_long_set_cursor_destroy(&duplicate);
    d7_long_set_cursor set_miss;
    CHECK_STATUS(d7_long_set_cursor_lower_bound(&set, -1, &set_miss));
    CHECK_STATUS(d7_long_set_cursor_insert(&set_miss, -1, &duplicate));
    CHECK(d7_long_set_cursor_try_peek_previous(&duplicate, &long_value));
    CHECK(long_value == -1);
    CHECK(d7_long_set_cursor_try_peek_next(&duplicate, &long_value));
    CHECK(long_value == 0);
    d7_long_set_cursor_destroy(&duplicate);
    d7_long_set_cursor_destroy(&set_miss);
    d7_long_set_cursor_destroy(&set_exact);
    d7_long_set_destroy(&set);

    d7_int_map_cursor_destroy(&end);
    d7_int_map_cursor_destroy(&updated);
    d7_int_map_cursor_destroy(&inserted);
    d7_int_map_cursor_destroy(&miss);
    d7_int_map_cursor_destroy(&exact);
    d7_int_map_destroy(&map);
    CHECK(d7_int_map_cursor_count(&start) == 5);
    CHECK(d7_int_map_cursor_try_peek_next(&start, &key, &value));
    CHECK(key == INT32_MIN);
    d7_int_map_cursor_destroy(&start);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void verify_random_model(
    const d7_int_map *map, const bool present[257], const void *const expected[257], size_t expected_count)
{
    CHECK(d7_int_map_count(map) == expected_count);
    for (size_t index = 0; index < 257; ++index) {
        const int32_t key = (int32_t)index - 128;
        const void *found = NULL;
        CHECK(d7_int_map_try_get(map, key, &found) == present[index]);
        if (present[index]) CHECK(found == expected[index]);
    }
}

static void test_random_model_and_structural_algebra(void)
{
    static int first_values[257];
    static int second_values[257];
    bool present[257] = { false };
    const void *expected[257] = { NULL };
    size_t expected_count = 0;
    uint32_t random = UINT32_C(0x9e3779b9);
    d7_int_map map = d7_int_map_create(NULL);

    for (size_t operation = 0; operation < 10000; ++operation) {
        const uint32_t sample = next_random(&random);
        const size_t index = sample % 257u;
        const int32_t key = (int32_t)index - 128;
        if ((sample & 3u) != 0) {
            const void *value = (sample & 4u) == 0 ? (const void *)&first_values[index] : (const void *)&second_values[index];
            CHECK_STATUS(d7_int_map_set(&map, key, value, &map));
            if (!present[index]) ++expected_count;
            present[index] = true;
            expected[index] = value;
        } else {
            CHECK_STATUS(d7_int_map_remove(&map, key, &map));
            if (present[index]) --expected_count;
            present[index] = false;
            expected[index] = NULL;
        }
        if (operation % 97u == 0) verify_random_model(&map, present, expected, expected_count);
    }
    verify_random_model(&map, present, expected, expected_count);
    d7_int_map_destroy(&map);

    bool in_left[257] = { false };
    bool in_right[257] = { false };
    d7_int_set left = d7_int_set_create();
    d7_int_set right = d7_int_set_create();
    for (size_t index = 0; index < 257; ++index) {
        const int32_t key = (int32_t)index - 128;
        in_left[index] = (next_random(&random) & 1u) != 0;
        in_right[index] = (next_random(&random) & 1u) != 0;
        if (in_left[index]) CHECK_STATUS(d7_int_set_add(&left, key, &left));
        if (in_right[index]) CHECK_STATUS(d7_int_set_add(&right, key, &right));
    }
    d7_int_set united;
    d7_int_set common;
    d7_int_set difference;
    CHECK_STATUS(d7_int_set_union(&left, &right, &united));
    CHECK_STATUS(d7_int_set_intersect(&left, &right, &common));
    CHECK_STATUS(d7_int_set_except(&left, &right, &difference));
    for (size_t index = 0; index < 257; ++index) {
        const int32_t key = (int32_t)index - 128;
        CHECK(d7_int_set_contains(&united, key) == (in_left[index] || in_right[index]));
        CHECK(d7_int_set_contains(&common, key) == (in_left[index] && in_right[index]));
        CHECK(d7_int_set_contains(&difference, key) == (in_left[index] && !in_right[index]));
    }
    d7_int_set_destroy(&difference);
    d7_int_set_destroy(&common);
    d7_int_set_destroy(&united);
    d7_int_set_destroy(&right);
    d7_int_set_destroy(&left);
}

typedef struct lifetime_tracker {
    size_t retains;
    size_t releases;
} lifetime_tracker;

static void *tracking_retain(const void *value, void *context)
{
    lifetime_tracker *tracker = (lifetime_tracker *)context;
    ++tracker->retains;
    return (void *)value;
}

static void tracking_release(void *value, void *context)
{
    lifetime_tracker *tracker = (lifetime_tracker *)context;
    (void)value;
    ++tracker->releases;
}

static void test_value_lifetime(void)
{
    static const int first = 1;
    static const int replacement = 2;
    static const int second = 3;
    lifetime_tracker tracker = { 0, 0 };
    d7_patricia_value_policy policy = d7_patricia_value_policy_default();
    policy.retain = tracking_retain;
    policy.release = tracking_release;
    policy.context = &tracker;

    d7_int_map original = d7_int_map_create(&policy);
    CHECK_STATUS(d7_int_map_set(&original, 10, &first, &original));
    d7_int_map changed = d7_int_map_clone(&original);
    CHECK_STATUS(d7_int_map_set(&changed, 20, &second, &changed));
    CHECK_STATUS(d7_int_map_set(&changed, 10, &replacement, &changed));
    CHECK(tracker.retains == 3);
    d7_int_map_destroy(&original);
    CHECK(tracker.releases == 1);
    d7_int_map_destroy(&changed);
    CHECK(tracker.releases == tracker.retains);
}

int main(void)
{
    test_signed_order_and_persistence();
    test_map_algebra();
    test_set_algebra();
    test_persistent_cursors();
    test_random_model_and_structural_algebra();
    test_value_lifetime();
    puts("All C Patricia tests passed.");
    return 0;
}
