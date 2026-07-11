#include <Tools/DataStructures/Hamt/patricia.h>

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
        const tds_hamt_status status_value = (expression); \
        if (status_value != TDS_HAMT_OK) { \
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
    tds_int_map map = tds_int_map_create(NULL);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        CHECK_STATUS(tds_int_map_set(&map, keys[i], &values[i], &map));
    }
    CHECK(tds_int_map_count(&map) == 5);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const void *found = NULL;
        CHECK(tds_int_map_try_get(&map, keys[i], &found));
        CHECK(found == &values[i]);
    }

    int_visit_state visit = { { 0 }, { NULL }, 0 };
    tds_int_map_visit(&map, collect_int_map, &visit);
    CHECK(visit.count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == expected[i]);

    tds_int_map no_change;
    CHECK_STATUS(tds_int_map_set(&map, 0, &values[1], &no_change));
    CHECK(tds_int_map_shares_root(&map, &no_change));
    tds_int_map_destroy(&no_change);

    CHECK_STATUS(tds_int_map_remove(&map, 42, &no_change));
    CHECK(tds_int_map_shares_root(&map, &no_change));
    tds_int_map_destroy(&no_change);

    tds_int_map snapshot = tds_int_map_clone(&map);
    CHECK_STATUS(tds_int_map_remove(&map, INT32_MIN, &map));
    CHECK(!tds_int_map_try_get(&map, INT32_MIN, NULL));
    CHECK(tds_int_map_try_get(&snapshot, INT32_MIN, NULL));
    CHECK(tds_int_map_count(&map) == 4);
    CHECK(tds_int_map_count(&snapshot) == 5);
    tds_int_map_destroy(&snapshot);
    tds_int_map_destroy(&map);

    static const int64_t long_keys[] = { INT64_MAX, 0, INT64_MIN, -1, 1 };
    static const int64_t long_expected[] = { INT64_MIN, -1, 0, 1, INT64_MAX };
    tds_long_map long_map = tds_long_map_create(NULL);
    for (size_t i = 0; i < sizeof(long_keys) / sizeof(long_keys[0]); ++i) {
        CHECK_STATUS(tds_long_map_set(&long_map, long_keys[i], &values[i], &long_map));
    }
    long_visit_state long_visit = { { 0 }, 0 };
    tds_long_map_visit(&long_map, collect_long_map, &long_visit);
    CHECK(long_visit.count == sizeof(long_expected) / sizeof(long_expected[0]));
    for (size_t i = 0; i < long_visit.count; ++i) CHECK(long_visit.keys[i] == long_expected[i]);
    tds_long_map_destroy(&long_map);
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

static void test_map_algebra(void)
{
    static const int left_minus = 10;
    static const int left_zero = 20;
    static const int left_plus = 30;
    static const int right_zero = 200;
    static const int right_seven = 70;
    tds_int_map left = tds_int_map_create(NULL);
    tds_int_map right = tds_int_map_create(NULL);
    CHECK_STATUS(tds_int_map_set(&left, -5, &left_minus, &left));
    CHECK_STATUS(tds_int_map_set(&left, 0, &left_zero, &left));
    CHECK_STATUS(tds_int_map_set(&left, 5, &left_plus, &left));
    CHECK_STATUS(tds_int_map_set(&right, 0, &right_zero, &right));
    CHECK_STATUS(tds_int_map_set(&right, 7, &right_seven, &right));

    tds_int_map united;
    tds_int_map common;
    tds_int_map difference;
    CHECK_STATUS(tds_int_map_union(&left, &right, &united));
    CHECK_STATUS(tds_int_map_intersect(&left, &right, &common));
    CHECK_STATUS(tds_int_map_except(&left, &right, &difference));
    CHECK(tds_int_map_count(&united) == 4);
    CHECK(tds_int_map_count(&common) == 1);
    CHECK(tds_int_map_count(&difference) == 2);

    const void *found = NULL;
    CHECK(tds_int_map_try_get(&united, 0, &found));
    CHECK(found == &right_zero);
    CHECK(tds_int_map_try_get(&common, 0, &found));
    CHECK(found == &left_zero);
    CHECK(tds_int_map_try_get(&difference, -5, &found) && found == &left_minus);
    CHECK(tds_int_map_try_get(&difference, 5, &found) && found == &left_plus);

    combine_state combined = { { 0 }, 0 };
    tds_int_map summed_union;
    tds_int_map summed_intersection;
    CHECK_STATUS(tds_int_map_union_with(
        &left, &right, sum_int_values, &combined, &summed_union));
    CHECK(combined.calls == 1);
    CHECK(tds_int_map_try_get(&summed_union, 0, &found));
    CHECK(*(const int *)found == left_zero + right_zero);
    combined.calls = 0;
    CHECK_STATUS(tds_int_map_intersect_with(
        &left, &right, sum_int_values, &combined, &summed_intersection));
    CHECK(combined.calls == 1);
    CHECK(tds_int_map_count(&summed_intersection) == 1);
    CHECK(tds_int_map_try_get(&summed_intersection, 0, &found));
    CHECK(*(const int *)found == left_zero + right_zero);

    tds_int_map no_change;
    CHECK_STATUS(tds_int_map_union_with(
        &left, &left, choose_left_value, NULL, &no_change));
    CHECK(tds_int_map_shares_root(&left, &no_change));
    tds_int_map_destroy(&no_change);
    CHECK_STATUS(tds_int_map_intersect_with(
        &left, &left, choose_left_value, NULL, &no_change));
    CHECK(tds_int_map_shares_root(&left, &no_change));
    tds_int_map_destroy(&no_change);

    tds_int_map alias_right = tds_int_map_clone(&right);
    CHECK_STATUS(tds_int_map_union(&left, &alias_right, &alias_right));
    CHECK(tds_int_map_count(&alias_right) == 4);
    CHECK(tds_int_map_try_get(&alias_right, 0, &found) && found == &right_zero);

    tds_int_map_destroy(&alias_right);
    tds_int_map_destroy(&summed_intersection);
    tds_int_map_destroy(&summed_union);
    tds_int_map_destroy(&difference);
    tds_int_map_destroy(&common);
    tds_int_map_destroy(&united);
    tds_int_map_destroy(&right);
    tds_int_map_destroy(&left);
}

static void test_set_algebra(void)
{
    static const int32_t left_values[] = { INT32_MIN, -3, 5, INT32_MAX };
    static const int32_t right_values[] = { -3, 0, INT32_MAX };
    static const int32_t union_expected[] = { INT32_MIN, -3, 0, 5, INT32_MAX };
    static const int32_t intersect_expected[] = { -3, INT32_MAX };
    static const int32_t except_expected[] = { INT32_MIN, 5 };
    tds_int_set left = tds_int_set_create();
    tds_int_set right = tds_int_set_create();
    for (size_t i = 0; i < sizeof(left_values) / sizeof(left_values[0]); ++i)
        CHECK_STATUS(tds_int_set_add(&left, left_values[i], &left));
    for (size_t i = 0; i < sizeof(right_values) / sizeof(right_values[0]); ++i)
        CHECK_STATUS(tds_int_set_add(&right, right_values[i], &right));

    tds_int_set united;
    tds_int_set common;
    tds_int_set difference;
    CHECK_STATUS(tds_int_set_union(&left, &right, &united));
    CHECK_STATUS(tds_int_set_intersect(&left, &right, &common));
    CHECK_STATUS(tds_int_set_except(&left, &right, &difference));
    CHECK(tds_int_set_count(&united) == sizeof(union_expected) / sizeof(union_expected[0]));
    CHECK(tds_int_set_count(&common) == sizeof(intersect_expected) / sizeof(intersect_expected[0]));
    CHECK(tds_int_set_count(&difference) == sizeof(except_expected) / sizeof(except_expected[0]));

    int_visit_state visit = { { 0 }, { NULL }, 0 };
    tds_int_set_visit(&united, collect_int_set, &visit);
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == union_expected[i]);
    visit.count = 0;
    tds_int_set_visit(&common, collect_int_set, &visit);
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == intersect_expected[i]);
    visit.count = 0;
    tds_int_set_visit(&difference, collect_int_set, &visit);
    for (size_t i = 0; i < visit.count; ++i) CHECK(visit.keys[i] == except_expected[i]);

    tds_int_set_destroy(&difference);
    tds_int_set_destroy(&common);
    tds_int_set_destroy(&united);
    tds_int_set_destroy(&right);
    tds_int_set_destroy(&left);

    tds_long_set longs = tds_long_set_create();
    CHECK_STATUS(tds_long_set_add(&longs, INT64_MAX, &longs));
    CHECK_STATUS(tds_long_set_add(&longs, INT64_MIN, &longs));
    CHECK_STATUS(tds_long_set_add(&longs, 0, &longs));
    long_visit_state long_visit = { { 0 }, 0 };
    tds_long_set_visit(&longs, collect_long_set, &long_visit);
    CHECK(long_visit.count == 3);
    CHECK(long_visit.keys[0] == INT64_MIN);
    CHECK(long_visit.keys[1] == 0);
    CHECK(long_visit.keys[2] == INT64_MAX);
    tds_long_set_destroy(&longs);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void verify_random_model(
    const tds_int_map *map, const bool present[257], const void *const expected[257], size_t expected_count)
{
    CHECK(tds_int_map_count(map) == expected_count);
    for (size_t index = 0; index < 257; ++index) {
        const int32_t key = (int32_t)index - 128;
        const void *found = NULL;
        CHECK(tds_int_map_try_get(map, key, &found) == present[index]);
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
    tds_int_map map = tds_int_map_create(NULL);

    for (size_t operation = 0; operation < 10000; ++operation) {
        const uint32_t sample = next_random(&random);
        const size_t index = sample % 257u;
        const int32_t key = (int32_t)index - 128;
        if ((sample & 3u) != 0) {
            const void *value = (sample & 4u) == 0 ? (const void *)&first_values[index] : (const void *)&second_values[index];
            CHECK_STATUS(tds_int_map_set(&map, key, value, &map));
            if (!present[index]) ++expected_count;
            present[index] = true;
            expected[index] = value;
        } else {
            CHECK_STATUS(tds_int_map_remove(&map, key, &map));
            if (present[index]) --expected_count;
            present[index] = false;
            expected[index] = NULL;
        }
        if (operation % 97u == 0) verify_random_model(&map, present, expected, expected_count);
    }
    verify_random_model(&map, present, expected, expected_count);
    tds_int_map_destroy(&map);

    bool in_left[257] = { false };
    bool in_right[257] = { false };
    tds_int_set left = tds_int_set_create();
    tds_int_set right = tds_int_set_create();
    for (size_t index = 0; index < 257; ++index) {
        const int32_t key = (int32_t)index - 128;
        in_left[index] = (next_random(&random) & 1u) != 0;
        in_right[index] = (next_random(&random) & 1u) != 0;
        if (in_left[index]) CHECK_STATUS(tds_int_set_add(&left, key, &left));
        if (in_right[index]) CHECK_STATUS(tds_int_set_add(&right, key, &right));
    }
    tds_int_set united;
    tds_int_set common;
    tds_int_set difference;
    CHECK_STATUS(tds_int_set_union(&left, &right, &united));
    CHECK_STATUS(tds_int_set_intersect(&left, &right, &common));
    CHECK_STATUS(tds_int_set_except(&left, &right, &difference));
    for (size_t index = 0; index < 257; ++index) {
        const int32_t key = (int32_t)index - 128;
        CHECK(tds_int_set_contains(&united, key) == (in_left[index] || in_right[index]));
        CHECK(tds_int_set_contains(&common, key) == (in_left[index] && in_right[index]));
        CHECK(tds_int_set_contains(&difference, key) == (in_left[index] && !in_right[index]));
    }
    tds_int_set_destroy(&difference);
    tds_int_set_destroy(&common);
    tds_int_set_destroy(&united);
    tds_int_set_destroy(&right);
    tds_int_set_destroy(&left);
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
    tds_patricia_value_policy policy = tds_patricia_value_policy_default();
    policy.retain = tracking_retain;
    policy.release = tracking_release;
    policy.context = &tracker;

    tds_int_map original = tds_int_map_create(&policy);
    CHECK_STATUS(tds_int_map_set(&original, 10, &first, &original));
    tds_int_map changed = tds_int_map_clone(&original);
    CHECK_STATUS(tds_int_map_set(&changed, 20, &second, &changed));
    CHECK_STATUS(tds_int_map_set(&changed, 10, &replacement, &changed));
    CHECK(tracker.retains == 3);
    tds_int_map_destroy(&original);
    CHECK(tracker.releases == 1);
    tds_int_map_destroy(&changed);
    CHECK(tracker.releases == tracker.retains);
}

int main(void)
{
    test_signed_order_and_persistence();
    test_map_algebra();
    test_set_algebra();
    test_random_model_and_structural_algebra();
    test_value_lifetime();
    puts("All C Patricia tests passed.");
    return 0;
}
