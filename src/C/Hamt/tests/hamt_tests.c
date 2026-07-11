#include <Tools/DataStructures/Hamt/hamt.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TDS_HAMT_TESTING
#error The native HAMT test executable requires TDS_HAMT_TESTING allocation hooks.
#endif

void tds_hamt_test_fail_allocations_after(size_t successful_allocations);
void tds_hamt_test_reset_allocator(void);

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef volatile LONG test_atomic_long;

static void test_atomic_long_init(test_atomic_long *value, long initial_value) {
    *value = initial_value;
}

static void test_atomic_long_increment(test_atomic_long *value) {
    (void)InterlockedIncrement(value);
}

static long test_atomic_long_read(test_atomic_long *value) {
    return InterlockedCompareExchange(value, 0, 0);
}
#else
#include <stdatomic.h>
typedef atomic_long test_atomic_long;

static void test_atomic_long_init(test_atomic_long *value, long initial_value) {
    atomic_init(value, initial_value);
}

static void test_atomic_long_increment(test_atomic_long *value) {
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static long test_atomic_long_read(test_atomic_long *value) {
    return atomic_load_explicit(value, memory_order_relaxed);
}
#endif

typedef void (*test_fn)(void);

typedef struct test_case {
    const char *name;
    test_fn run;
} test_case;

static void fail_at(const char *file, int line, const char *expression) {
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
        tds_hamt_status status_value = (expression); \
        if (status_value != TDS_HAMT_OK) { \
            fprintf(stderr, "%s:%d: %s returned status %d\n", __FILE__, __LINE__, #expression, (int)status_value); \
            exit(1); \
        } \
    } while (0)

static int key_pool[257];
static int value_pool[2001];

static void init_pools(void) {
    for (int i = -128; i <= 128; ++i) {
        key_pool[i + 128] = i;
    }
    for (int i = -1000; i <= 1000; ++i) {
        value_pool[i + 1000] = i;
    }
}

static const int *int_key(int value) {
    CHECK(value >= -128 && value <= 128);
    return &key_pool[value + 128];
}

static const int *int_value(int value) {
    CHECK(value >= -1000 && value <= 1000);
    return &value_pool[value + 1000];
}

static uint32_t int_hash(const void *item, void *context) {
    (void)context;
    uint32_t value = (uint32_t)*(const int *)item;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}

static uint32_t few_buckets_int_hash(const void *item, void *context) {
    (void)context;
    return (uint32_t)*(const int *)item & 3u;
}

static bool int_equal(const void *left, const void *right, void *context) {
    (void)context;
    return *(const int *)left == *(const int *)right;
}

static tds_hamt_policy int_map_policy(uint32_t (*hash)(const void *, void *)) {
    tds_hamt_policy policy = tds_hamt_policy_default();
    policy.hash = hash;
    policy.key_equal = int_equal;
    policy.value_equal = int_equal;
    return policy;
}

static tds_hamt_set_policy int_set_policy(uint32_t (*hash)(const void *, void *)) {
    tds_hamt_set_policy policy = tds_hamt_set_policy_default();
    policy.hash = hash;
    policy.equal = int_equal;
    return policy;
}

static void assert_int_value(const tds_hamt_map *map, int key, int expected) {
    const void *actual = NULL;
    CHECK(tds_hamt_map_try_get(map, int_key(key), &actual));
    CHECK(actual != NULL);
    CHECK(*(const int *)actual == expected);
}

static void assert_int_model_matches(
    const bool present[81],
    const int values[81],
    const tds_hamt_map *map) {
    size_t expected_count = 0;
    for (int key = -40; key <= 40; ++key) {
        if (present[key + 40]) {
            ++expected_count;
            assert_int_value(map, key, values[key + 40]);
        } else {
            CHECK(!tds_hamt_map_contains_key(map, int_key(key)));
        }
    }

    CHECK(tds_hamt_map_count(map) == expected_count);

    size_t enumerated = 0;
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(map, &iterator);
    const void *key_ptr = NULL;
    const void *value_ptr = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key_ptr, &value_ptr)) {
        const int key = *(const int *)key_ptr;
        CHECK(key >= -40 && key <= 40);
        CHECK(present[key + 40]);
        CHECK(*(const int *)value_ptr == values[key + 40]);
        ++enumerated;
    }

    CHECK(enumerated == expected_count);
}

static void assert_set_model_matches(const bool present[61], const tds_hamt_set *set) {
    size_t expected_count = 0;
    for (int value = -30; value <= 30; ++value) {
        if (present[value + 30]) {
            ++expected_count;
            CHECK(tds_hamt_set_contains(set, int_key(value)));
        } else {
            CHECK(!tds_hamt_set_contains(set, int_key(value)));
        }
    }

    CHECK(tds_hamt_set_count(set) == expected_count);

    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(set, &iterator);
    const void *item = NULL;
    size_t enumerated = 0;
    while (tds_hamt_set_iterator_next(&iterator, &item)) {
        const int value = *(const int *)item;
        CHECK(value >= -30 && value <= 30);
        CHECK(present[value + 30]);
        ++enumerated;
    }

    CHECK(enumerated == expected_count);
}

typedef struct concurrent_retained_context {
    const tds_hamt_map *map;
    const tds_hamt_set *set;
    const void *const *probe_keys;
    const int *probe_values;
    size_t probe_count;
    test_atomic_long failures;
} concurrent_retained_context;

static void record_concurrent_failure(concurrent_retained_context *context) {
    test_atomic_long_increment(&context->failures);
}

static void concurrent_retained_worker(concurrent_retained_context *context) {
    for (int pass = 0; pass != 256; ++pass) {
        if (tds_hamt_map_count(context->map) != 128 || tds_hamt_set_count(context->set) != 128) {
            record_concurrent_failure(context);
            return;
        }

        for (size_t index = 0; index != context->probe_count; ++index) {
            const int key = context->probe_values[index];
            const void *actual = NULL;
            if (!tds_hamt_map_try_get(context->map, context->probe_keys[index], &actual) ||
                actual == NULL ||
                *(const int *)actual != key * 3 - 200 ||
                !tds_hamt_set_contains(context->set, context->probe_keys[index])) {
                record_concurrent_failure(context);
                return;
            }
        }

        tds_hamt_map_iterator map_iterator;
        tds_hamt_map_iterator_init(context->map, &map_iterator);
        const void *key_ptr = NULL;
        const void *value_ptr = NULL;
        size_t map_count = 0;
        while (tds_hamt_map_iterator_next(&map_iterator, &key_ptr, &value_ptr)) {
            const int key = *(const int *)key_ptr;
            if (key < 0 || key >= 128 || *(const int *)value_ptr != key * 3 - 200) {
                record_concurrent_failure(context);
                return;
            }

            ++map_count;
        }

        tds_hamt_set_iterator set_iterator;
        tds_hamt_set_iterator_init(context->set, &set_iterator);
        const void *item = NULL;
        size_t set_count = 0;
        while (tds_hamt_set_iterator_next(&set_iterator, &item)) {
            const int value = *(const int *)item;
            if (value < 0 || value >= 128) {
                record_concurrent_failure(context);
                return;
            }

            ++set_count;
        }

        if (map_count != 128 || set_count != 128) {
            record_concurrent_failure(context);
            return;
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_retained_thread_proc(void *parameter) {
    concurrent_retained_worker((concurrent_retained_context *)parameter);
    return 0;
}
#endif

static uint32_t ci_hash(const void *item, void *context) {
    (void)context;
    const unsigned char *text = (const unsigned char *)item;
    uint32_t hash = 2166136261u;
    while (*text != 0) {
        hash ^= (uint32_t)tolower(*text);
        hash *= 16777619u;
        ++text;
    }
    return hash;
}

static bool ci_equal(const void *left, const void *right, void *context) {
    (void)context;
    const unsigned char *l = (const unsigned char *)left;
    const unsigned char *r = (const unsigned char *)right;
    while (*l != 0 && *r != 0) {
        if (tolower(*l) != tolower(*r)) {
            return false;
        }
        ++l;
        ++r;
    }
    return *l == *r;
}

static uint32_t constant_string_hash(const void *item, void *context) {
    (void)item;
    (void)context;
    return 0x77u;
}

typedef struct collision_key {
    int id;
} collision_key;

static uint32_t collision_key_hash(const void *item, void *context) {
    (void)item;
    (void)context;
    return 0x12345u;
}

static bool collision_key_equal(const void *left, const void *right, void *context) {
    (void)context;
    return ((const collision_key *)left)->id == ((const collision_key *)right)->id;
}

typedef struct explicit_hash_key {
    int id;
    uint32_t hash;
} explicit_hash_key;

static uint32_t explicit_hash_key_hash(const void *item, void *context) {
    (void)context;
    return ((const explicit_hash_key *)item)->hash;
}

static bool explicit_hash_key_equal(const void *left, const void *right, void *context) {
    (void)context;
    return ((const explicit_hash_key *)left)->id == ((const explicit_hash_key *)right)->id;
}

static tds_hamt_policy explicit_map_policy(void) {
    tds_hamt_policy policy = tds_hamt_policy_default();
    policy.hash = explicit_hash_key_hash;
    policy.key_equal = explicit_hash_key_equal;
    policy.value_equal = int_equal;
    return policy;
}

static uint32_t rng_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static int rng_range(uint32_t *state, int min, int max) {
    const uint32_t span = (uint32_t)(max - min + 1);
    return min + (int)(rng_next(state) % span);
}

static void test_empty_map_has_no_entries(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map empty = tds_hamt_map_create(&policy);

    CHECK(tds_hamt_map_is_empty(&empty));
    CHECK(tds_hamt_map_count(&empty) == 0);
    CHECK(!tds_hamt_map_contains_key(&empty, int_key(1)));

    tds_hamt_map removed;
    CHECK_STATUS(tds_hamt_map_remove(&empty, int_key(1), &removed));
    CHECK(tds_hamt_map_shares_root(&empty, &removed));
    tds_hamt_map_destroy(&removed);

    tds_hamt_map cleared;
    CHECK_STATUS(tds_hamt_map_clear(&empty, &cleared));
    CHECK(tds_hamt_map_shares_root(&empty, &cleared));
    tds_hamt_map_destroy(&cleared);
    tds_hamt_map_destroy(&empty);
}

static void test_set_adds_replaces_and_preserves_old_versions(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map empty = tds_hamt_map_create(&policy);
    tds_hamt_map one;
    tds_hamt_map two;
    tds_hamt_map replaced;

    CHECK_STATUS(tds_hamt_map_set(&empty, int_key(1), int_value(10), &one));
    CHECK_STATUS(tds_hamt_map_set(&one, int_key(2), int_value(20), &two));
    CHECK_STATUS(tds_hamt_map_set(&two, int_key(1), int_value(11), &replaced));

    CHECK(tds_hamt_map_is_empty(&empty));
    assert_int_value(&one, 1, 10);
    assert_int_value(&two, 1, 10);
    assert_int_value(&two, 2, 20);
    assert_int_value(&replaced, 1, 11);
    assert_int_value(&replaced, 2, 20);

    tds_hamt_map no_op;
    CHECK_STATUS(tds_hamt_map_set(&replaced, int_key(1), int_value(11), &no_op));
    CHECK(tds_hamt_map_shares_root(&replaced, &no_op));

    tds_hamt_map_destroy(&no_op);
    tds_hamt_map_destroy(&replaced);
    tds_hamt_map_destroy(&two);
    tds_hamt_map_destroy(&one);
    tds_hamt_map_destroy(&empty);
}

static void test_add_and_try_add_reject_duplicates(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map empty = tds_hamt_map_create(&policy);
    tds_hamt_map map;
    CHECK_STATUS(tds_hamt_map_add(&empty, int_key(1), int_value(10), &map));

    tds_hamt_map duplicate;
    CHECK(tds_hamt_map_add(&map, int_key(1), int_value(99), &duplicate) == TDS_HAMT_DUPLICATE_KEY);

    bool added = true;
    tds_hamt_map same;
    CHECK_STATUS(tds_hamt_map_try_add(&map, int_key(1), int_value(99), &same, &added));
    CHECK(!added);
    CHECK(tds_hamt_map_shares_root(&map, &same));
    tds_hamt_map_destroy(&same);

    tds_hamt_map with_two;
    CHECK_STATUS(tds_hamt_map_try_add(&map, int_key(2), int_value(20), &with_two, &added));
    CHECK(added);
    assert_int_value(&with_two, 2, 20);
    CHECK(!tds_hamt_map_contains_key(&map, int_key(2)));

    tds_hamt_map_destroy(&with_two);
    tds_hamt_map_destroy(&map);
    tds_hamt_map_destroy(&empty);
}

static void test_remove_and_try_remove_delete_present_keys(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(&map, int_key(1), int_value(10), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, int_key(2), int_value(20), &next));
    tds_hamt_map_destroy(&map);
    map = next;

    bool removed = false;
    const void *removed_value = NULL;
    tds_hamt_map without_one;
    CHECK_STATUS(tds_hamt_map_try_remove(&map, int_key(1), &without_one, &removed, &removed_value));
    CHECK(removed);
    CHECK(*(const int *)removed_value == 10);
    CHECK(!tds_hamt_map_contains_key(&without_one, int_key(1)));
    CHECK(tds_hamt_map_contains_key(&map, int_key(1)));

    tds_hamt_map same;
    CHECK_STATUS(tds_hamt_map_try_remove(&without_one, int_key(9), &same, &removed, &removed_value));
    CHECK(!removed);
    CHECK(removed_value == NULL);
    CHECK(tds_hamt_map_shares_root(&without_one, &same));

    tds_hamt_map_destroy(&same);
    tds_hamt_map_destroy(&without_one);
    tds_hamt_map_destroy(&map);
}

static void test_set_many_and_clear_preserve_contracts(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_entry entries[] = {
        { int_key(1), int_value(10) },
        { int_key(2), int_value(20) },
        { int_key(1), int_value(11) }
    };
    tds_hamt_map updated;
    CHECK_STATUS(tds_hamt_map_set_many(&map, entries, 3, &updated));
    CHECK(tds_hamt_map_is_empty(&map));
    CHECK(tds_hamt_map_count(&updated) == 2);
    assert_int_value(&updated, 1, 11);
    assert_int_value(&updated, 2, 20);

    tds_hamt_map cleared;
    CHECK_STATUS(tds_hamt_map_clear(&updated, &cleared));
    CHECK(tds_hamt_map_is_empty(&cleared));
    CHECK(!tds_hamt_map_shares_root(&updated, &cleared));

    tds_hamt_map_destroy(&cleared);
    tds_hamt_map_destroy(&updated);
    tds_hamt_map_destroy(&map);
}

static void test_create_range_last_wins_and_retains_first_equivalent_key(void) {
    tds_hamt_policy policy = tds_hamt_policy_default();
    policy.hash = ci_hash;
    policy.key_equal = ci_equal;
    policy.value_equal = int_equal;

    char stored_alpha[] = "Alpha";
    tds_hamt_entry entries[] = {
        { stored_alpha, int_value(1) },
        { "beta", int_value(2) },
        { "ALPHA", int_value(3) }
    };
    tds_hamt_map map;
    CHECK_STATUS(tds_hamt_map_create_range(&policy, entries, 3, &map));

    CHECK(tds_hamt_map_count(&map) == 2);
    const void *actual = NULL;
    CHECK(tds_hamt_map_try_get(&map, "alpha", &actual));
    CHECK(*(const int *)actual == 3);
    CHECK(tds_hamt_map_contains_key(&map, "BETA"));

    const void *actual_key = NULL;
    CHECK(tds_hamt_map_try_get_key(&map, "ALPHA", &actual_key));
    CHECK(actual_key == stored_alpha);

    tds_hamt_map_destroy(&map);
}

static void test_equal_hash_collision_bucket_preserves_every_key(void) {
    tds_hamt_policy policy = tds_hamt_policy_default();
    policy.hash = collision_key_hash;
    policy.key_equal = collision_key_equal;
    policy.value_equal = int_equal;

    collision_key keys[100];
    bool present[100] = { false };
    int values[100] = { 0 };
    tds_hamt_map map = tds_hamt_map_create(&policy);

    for (int i = 0; i < 100; ++i) {
        keys[i].id = i;
        tds_hamt_map next;
        CHECK_STATUS(tds_hamt_map_set(&map, &keys[i], int_value(i * 10 - 500), &next));
        tds_hamt_map_destroy(&map);
        map = next;
        present[i] = true;
        values[i] = i * 10 - 500;
    }

    for (int i = 0; i < 100; i += 3) {
        tds_hamt_map next;
        CHECK_STATUS(tds_hamt_map_remove(&map, &keys[i], &next));
        tds_hamt_map_destroy(&map);
        map = next;
        present[i] = false;
    }

    for (int i = 1; i < 100; i += 4) {
        tds_hamt_map next;
        CHECK_STATUS(tds_hamt_map_set(&map, &keys[i], int_value(-i), &next));
        tds_hamt_map_destroy(&map);
        map = next;
        present[i] = true;
        values[i] = -i;
    }

    size_t expected_count = 0;
    for (int i = 0; i < 100; ++i) {
        const void *actual = NULL;
        if (present[i]) {
            ++expected_count;
            CHECK(tds_hamt_map_try_get(&map, &keys[i], &actual));
            CHECK(*(const int *)actual == values[i]);
        } else {
            CHECK(!tds_hamt_map_contains_key(&map, &keys[i]));
        }
    }
    CHECK(tds_hamt_map_count(&map) == expected_count);

    size_t enumerated = 0;
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&map, &iterator);
    const void *key = NULL;
    const void *value = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
        const int id = ((const collision_key *)key)->id;
        CHECK(id >= 0 && id < 100);
        CHECK(present[id]);
        CHECK(*(const int *)value == values[id]);
        ++enumerated;
    }
    CHECK(enumerated == expected_count);

    tds_hamt_map_destroy(&map);
}

static void test_deep_shared_hash_prefixes_lookup_and_remove_correctly(void) {
    tds_hamt_policy policy = explicit_map_policy();
    explicit_hash_key a = { 1, 0u };
    explicit_hash_key b = { 2, 1u << 30 };
    explicit_hash_key c = { 3, 0x80000000u };
    explicit_hash_key d = { 4, 0xC0000000u };

    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(&map, &a, int_value(1), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, &b, int_value(2), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, &c, int_value(3), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, &d, int_value(4), &next));
    tds_hamt_map_destroy(&map);
    map = next;

    const void *actual = NULL;
    CHECK(tds_hamt_map_try_get(&map, &a, &actual) && *(const int *)actual == 1);
    CHECK(tds_hamt_map_try_get(&map, &b, &actual) && *(const int *)actual == 2);
    CHECK(tds_hamt_map_try_get(&map, &c, &actual) && *(const int *)actual == 3);
    CHECK(tds_hamt_map_try_get(&map, &d, &actual) && *(const int *)actual == 4);

    tds_hamt_map reduced;
    CHECK_STATUS(tds_hamt_map_remove(&map, &b, &reduced));
    CHECK_STATUS(tds_hamt_map_remove(&reduced, &c, &next));
    tds_hamt_map_destroy(&reduced);
    reduced = next;
    CHECK_STATUS(tds_hamt_map_remove(&reduced, &d, &next));
    tds_hamt_map_destroy(&reduced);
    reduced = next;

    CHECK(tds_hamt_map_count(&reduced) == 1);
    CHECK(tds_hamt_map_debug_root_kind(&reduced) == TDS_HAMT_NODE_LEAF);
    CHECK(tds_hamt_map_try_get(&reduced, &a, &actual) && *(const int *)actual == 1);
    CHECK(!tds_hamt_map_contains_key(&reduced, &b));
    CHECK(tds_hamt_map_try_get(&map, &b, &actual) && *(const int *)actual == 2);

    tds_hamt_map_destroy(&reduced);
    tds_hamt_map_destroy(&map);
}

static void test_depth_seven_iterator_traversal(void) {
    tds_hamt_policy policy = explicit_map_policy();
    explicit_hash_key first = { 1, 0u };
    explicit_hash_key second = { 2, 1u << 30 };

    tds_hamt_map empty = tds_hamt_map_create(&policy);
    tds_hamt_map one;
    CHECK_STATUS(tds_hamt_map_set(&empty, &first, int_value(1), &one));
    tds_hamt_map map;
    CHECK_STATUS(tds_hamt_map_set(&one, &second, int_value(2), &map));

    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&map, &iterator);
    const void *key = NULL;
    const void *value = NULL;
    CHECK(tds_hamt_map_iterator_next(&iterator, &key, &value));
    CHECK(iterator.depth == 7);
    CHECK(((const explicit_hash_key *)key)->id == 1);
    CHECK(*(const int *)value == 1);
    CHECK(tds_hamt_map_iterator_next(&iterator, &key, &value));
    CHECK(((const explicit_hash_key *)key)->id == 2);
    CHECK(*(const int *)value == 2);
    CHECK(!tds_hamt_map_iterator_next(&iterator, &key, &value));

    tds_hamt_map_destroy(&map);
    tds_hamt_map_destroy(&one);
    tds_hamt_map_destroy(&empty);
}

static void test_allocation_failures_unwind_node_set_and_merge(void) {
    tds_hamt_policy policy = explicit_map_policy();
    explicit_hash_key deep_left = { 1, 0u };
    explicit_hash_key deep_right = { 2, 1u << 30 };

    tds_hamt_map empty = tds_hamt_map_create(&policy);
    tds_hamt_map deep_base;
    CHECK_STATUS(tds_hamt_map_set(&empty, &deep_left, int_value(1), &deep_base));

    bool merge_succeeded = false;
    size_t merge_failure_count = 0;
    for (size_t fail_after = 0; fail_after != 32; ++fail_after) {
        tds_hamt_map result = { 0 };
        tds_hamt_test_fail_allocations_after(fail_after);
        const tds_hamt_status status =
            tds_hamt_map_set(&deep_base, &deep_right, int_value(2), &result);
        tds_hamt_test_reset_allocator();

        const void *actual = NULL;
        CHECK(tds_hamt_map_count(&deep_base) == 1);
        CHECK(tds_hamt_map_try_get(&deep_base, &deep_left, &actual));
        CHECK(*(const int *)actual == 1);
        CHECK(!tds_hamt_map_contains_key(&deep_base, &deep_right));
        if (status == TDS_HAMT_OUT_OF_MEMORY) {
            CHECK(result.root == NULL);
            ++merge_failure_count;
            continue;
        }

        CHECK(status == TDS_HAMT_OK);
        CHECK(tds_hamt_map_count(&result) == 2);
        CHECK(tds_hamt_map_try_get(&result, &deep_right, &actual));
        CHECK(*(const int *)actual == 2);
        tds_hamt_map_destroy(&result);
        merge_succeeded = true;
        break;
    }

    CHECK(merge_succeeded);
    CHECK(merge_failure_count >= 8);

    explicit_hash_key branch_first = { 3, 0u };
    explicit_hash_key branch_second = { 4, 1u };
    explicit_hash_key branch_added = { 5, 2u };
    tds_hamt_map branch_one;
    CHECK_STATUS(tds_hamt_map_set(&empty, &branch_first, int_value(3), &branch_one));
    tds_hamt_map branch_base;
    CHECK_STATUS(tds_hamt_map_set(&branch_one, &branch_second, int_value(4), &branch_base));

    bool node_set_succeeded = false;
    size_t node_set_failure_count = 0;
    for (size_t fail_after = 0; fail_after != 16; ++fail_after) {
        tds_hamt_map result = { 0 };
        tds_hamt_test_fail_allocations_after(fail_after);
        const tds_hamt_status status =
            tds_hamt_map_set(&branch_base, &branch_added, int_value(5), &result);
        tds_hamt_test_reset_allocator();

        const void *actual = NULL;
        CHECK(tds_hamt_map_count(&branch_base) == 2);
        CHECK(tds_hamt_map_try_get(&branch_base, &branch_first, &actual));
        CHECK(*(const int *)actual == 3);
        CHECK(tds_hamt_map_try_get(&branch_base, &branch_second, &actual));
        CHECK(*(const int *)actual == 4);
        CHECK(!tds_hamt_map_contains_key(&branch_base, &branch_added));
        if (status == TDS_HAMT_OUT_OF_MEMORY) {
            CHECK(result.root == NULL);
            ++node_set_failure_count;
            continue;
        }

        CHECK(status == TDS_HAMT_OK);
        CHECK(tds_hamt_map_count(&result) == 3);
        CHECK(tds_hamt_map_try_get(&result, &branch_added, &actual));
        CHECK(*(const int *)actual == 5);
        tds_hamt_map_destroy(&result);
        node_set_succeeded = true;
        break;
    }

    CHECK(node_set_succeeded);
    /* CHAMP inserts an unused bitmap position directly into the inline
     * payload run, so only the replacement node allocation is mandatory. */
    CHECK(node_set_failure_count >= 1);

    tds_hamt_map_destroy(&branch_base);
    tds_hamt_map_destroy(&branch_one);
    tds_hamt_map_destroy(&deep_base);
    tds_hamt_map_destroy(&empty);
}

static void test_collision_bucket_splits_and_hash_mismatch_probes_miss(void) {
    tds_hamt_policy policy = explicit_map_policy();
    explicit_hash_key a = { 1, 0x10u };
    explicit_hash_key b = { 2, 0x10u };
    explicit_hash_key probe = { 9, 0x410u };

    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(&map, &a, int_value(1), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, &b, int_value(2), &next));
    tds_hamt_map_destroy(&map);
    map = next;

    CHECK(tds_hamt_map_debug_root_kind(&map) == TDS_HAMT_NODE_COLLISION);
    CHECK(!tds_hamt_map_contains_key(&map, &probe));
    CHECK_STATUS(tds_hamt_map_remove(&map, &probe, &next));
    CHECK(tds_hamt_map_shares_root(&map, &next));
    tds_hamt_map_destroy(&next);

    tds_hamt_map expanded;
    CHECK_STATUS(tds_hamt_map_set(&map, &probe, int_value(9), &expanded));
    CHECK(tds_hamt_map_count(&expanded) == 3);
    CHECK(tds_hamt_map_debug_root_kind(&expanded) == TDS_HAMT_NODE_BITMAP_INDEXED);
    const void *actual = NULL;
    CHECK(tds_hamt_map_try_get(&expanded, &probe, &actual) && *(const int *)actual == 9);
    CHECK(tds_hamt_map_try_get(&expanded, &a, &actual) && *(const int *)actual == 1);
    CHECK(tds_hamt_map_try_get(&expanded, &b, &actual) && *(const int *)actual == 2);

    tds_hamt_map_destroy(&expanded);
    tds_hamt_map_destroy(&map);
}

static void test_collision_bucket_equal_value_keeps_root_and_key_object(void) {
    tds_hamt_policy policy = tds_hamt_policy_default();
    policy.hash = constant_string_hash;
    policy.key_equal = ci_equal;
    policy.value_equal = int_equal;

    char stored_alpha[] = "Alpha";
    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(&map, stored_alpha, int_value(1), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, "beta", int_value(2), &next));
    tds_hamt_map_destroy(&map);
    map = next;

    tds_hamt_map no_op;
    CHECK_STATUS(tds_hamt_map_set(&map, "ALPHA", int_value(1), &no_op));
    CHECK(tds_hamt_map_shares_root(&map, &no_op));
    tds_hamt_map_destroy(&no_op);

    tds_hamt_map replaced;
    CHECK_STATUS(tds_hamt_map_set(&map, "ALPHA", int_value(3), &replaced));
    const void *actual = NULL;
    CHECK(tds_hamt_map_try_get(&replaced, "alpha", &actual) && *(const int *)actual == 3);
    const void *actual_key = NULL;
    CHECK(tds_hamt_map_try_get_key(&replaced, "alpha", &actual_key));
    CHECK(actual_key == stored_alpha);

    tds_hamt_map_destroy(&replaced);
    tds_hamt_map_destroy(&map);
}

static void test_structure_root_shape_and_sharing(void) {
    tds_hamt_policy policy = explicit_map_policy();
    explicit_hash_key a = { 1, 0x00u };
    explicit_hash_key b = { 2, 0x01u };
    explicit_hash_key c = { 3, 0x21u };

    tds_hamt_map empty = tds_hamt_map_create(&policy);
    CHECK(tds_hamt_map_debug_root_kind(&empty) == TDS_HAMT_NODE_EMPTY);

    tds_hamt_map single;
    CHECK_STATUS(tds_hamt_map_set(&empty, &a, int_value(1), &single));
    CHECK(tds_hamt_map_debug_root_kind(&single) == TDS_HAMT_NODE_LEAF);

    tds_hamt_map map;
    CHECK_STATUS(tds_hamt_map_set(&single, &b, int_value(2), &map));
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(&map, &c, int_value(3), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK(tds_hamt_map_debug_root_kind(&map) == TDS_HAMT_NODE_BITMAP_INDEXED);

    tds_hamt_map updated;
    CHECK_STATUS(tds_hamt_map_set(&map, &a, int_value(10), &updated));
    const void *before_children[4] = { NULL };
    const void *after_children[4] = { NULL };
    CHECK(tds_hamt_map_debug_root_child_identities(&map, before_children, 4) == 1);
    CHECK(tds_hamt_map_debug_root_child_identities(&updated, after_children, 4) == 1);
    CHECK(before_children[0] == after_children[0]);

    tds_hamt_map no_op;
    CHECK_STATUS(tds_hamt_map_set(&map, &a, int_value(1), &no_op));
    CHECK(tds_hamt_map_shares_root(&map, &no_op));
    tds_hamt_map_destroy(&no_op);

    tds_hamt_map_destroy(&updated);
    tds_hamt_map_destroy(&map);
    tds_hamt_map_destroy(&single);
    tds_hamt_map_destroy(&empty);
}

typedef struct diff_counts {
    size_t added;
    size_t removed;
    size_t changed;
} diff_counts;

static void count_difference(const tds_hamt_difference *difference, void *context) {
    diff_counts *counts = (diff_counts *)context;
    if (difference->kind == TDS_HAMT_DIFFERENCE_ADDED) {
        ++counts->added;
    } else if (difference->kind == TDS_HAMT_DIFFERENCE_REMOVED) {
        ++counts->removed;
    } else {
        ++counts->changed;
    }
}

static void test_champ_independent_histories_and_typed_diff(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map ascending = tds_hamt_map_create(&policy);
    tds_hamt_map descending = tds_hamt_map_create(&policy);
    for (int key = -100; key <= 100; ++key) {
        CHECK_STATUS(tds_hamt_map_set(&ascending, int_key(key), int_value(key), &ascending));
        CHECK_STATUS(tds_hamt_map_set(&descending, int_key(-key), int_value(-key), &descending));
    }
    CHECK(tds_hamt_map_equals(&ascending, &descending));
    diff_counts empty_counts = { 0 };
    CHECK_STATUS(tds_hamt_map_diff(&ascending, &descending, count_difference, &empty_counts));
    CHECK(empty_counts.added == 0 && empty_counts.removed == 0 && empty_counts.changed == 0);

    tds_hamt_map changed = tds_hamt_map_clone(&descending);
    CHECK_STATUS(tds_hamt_map_remove(&changed, int_key(7), &changed));
    CHECK_STATUS(tds_hamt_map_set(&changed, int_key(9), int_value(-9), &changed));
    CHECK_STATUS(tds_hamt_map_set(&changed, int_key(101), int_value(101), &changed));
    diff_counts counts = { 0 };
    CHECK_STATUS(tds_hamt_map_diff(&ascending, &changed, count_difference, &counts));
    CHECK(counts.added == 1 && counts.removed == 1 && counts.changed == 1);
    tds_hamt_map_destroy(&changed);
    tds_hamt_map_destroy(&descending);
    tds_hamt_map_destroy(&ascending);
}

static void test_iterator_copy_advances_independently(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(&map, int_key(0), int_value(0), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, int_key(1), int_value(1), &next));
    tds_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(tds_hamt_map_set(&map, int_key(33), int_value(33), &next));
    tds_hamt_map_destroy(&map);
    map = next;

    tds_hamt_map_iterator left;
    tds_hamt_map_iterator_init(&map, &left);
    tds_hamt_map_iterator right = left;

    size_t left_count = 0;
    size_t right_count = 0;
    const void *key = NULL;
    const void *value = NULL;
    while (tds_hamt_map_iterator_next(&left, &key, &value)) {
        ++left_count;
    }
    while (tds_hamt_map_iterator_next(&right, &key, &value)) {
        ++right_count;
    }

    CHECK(left_count == 3);
    CHECK(right_count == 3);
    tds_hamt_map_destroy(&map);
}

typedef struct map_snapshot {
    bool active;
    tds_hamt_map map;
    bool present[81];
    int values[81];
} map_snapshot;

typedef struct explicit_map_snapshot {
    bool active;
    tds_hamt_map map;
    bool present[96];
    int values[96];
} explicit_map_snapshot;

static void assert_explicit_model_matches(
    const explicit_hash_key keys[96],
    const bool present[96],
    const int values[96],
    const tds_hamt_map *map) {
    size_t expected_count = 0;
    for (int id = 0; id != 96; ++id) {
        const void *actual = NULL;
        if (present[id]) {
            ++expected_count;
            CHECK(tds_hamt_map_try_get(map, &keys[id], &actual));
            CHECK(actual != NULL);
            CHECK(*(const int *)actual == values[id]);
        } else {
            CHECK(!tds_hamt_map_contains_key(map, &keys[id]));
        }
    }

    CHECK(tds_hamt_map_count(map) == expected_count);

    size_t enumerated = 0;
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(map, &iterator);
    const void *key = NULL;
    const void *value = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
        const int id = ((const explicit_hash_key *)key)->id;
        CHECK(id >= 0 && id < 96);
        CHECK(present[id]);
        CHECK(*(const int *)value == values[id]);
        ++enumerated;
    }

    CHECK(enumerated == expected_count);
}

static void capture_explicit_snapshot(
    explicit_map_snapshot *snapshot,
    const tds_hamt_map *map,
    const bool present[96],
    const int values[96]) {
    if (snapshot->active) {
        tds_hamt_map_destroy(&snapshot->map);
    }

    snapshot->active = true;
    snapshot->map = tds_hamt_map_clone(map);
    memcpy(snapshot->present, present, 96 * sizeof(bool));
    memcpy(snapshot->values, values, 96 * sizeof(int));
}

static void test_random_history_matches_model_and_preserves_snapshots(void) {
    tds_hamt_policy policy = int_map_policy(int_hash);
    uint32_t rng = 0xC0FFEEu;

    for (int iteration = 0; iteration < 200; ++iteration) {
        tds_hamt_map map = tds_hamt_map_create(&policy);
        bool present[81] = { false };
        int values[81] = { 0 };
        map_snapshot snapshots[5];
        memset(snapshots, 0, sizeof(snapshots));

        for (int step = 0; step < 160; ++step) {
            const int op = rng_range(&rng, 0, 4);
            const int key = rng_range(&rng, -40, 40);
            const int value = rng_range(&rng, -1000, 1000);
            tds_hamt_map next;

            switch (op) {
            case 0:
                CHECK_STATUS(tds_hamt_map_set(&map, int_key(key), int_value(value), &next));
                tds_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = true;
                values[key + 40] = value;
                break;

            case 1:
                CHECK_STATUS(tds_hamt_map_remove(&map, int_key(key), &next));
                tds_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = false;
                break;

            case 2: {
                bool added = false;
                CHECK_STATUS(tds_hamt_map_try_add(&map, int_key(key), int_value(value), &next, &added));
                CHECK(added == !present[key + 40]);
                tds_hamt_map_destroy(&map);
                map = next;
                if (added) {
                    present[key + 40] = true;
                    values[key + 40] = value;
                }
                break;
            }

            case 3: {
                bool removed = false;
                const void *removed_value = NULL;
                const bool expected_removed = present[key + 40];
                const int expected_value = values[key + 40];
                CHECK_STATUS(tds_hamt_map_try_remove(&map, int_key(key), &next, &removed, &removed_value));
                CHECK(removed == expected_removed);
                if (removed) {
                    CHECK(*(const int *)removed_value == expected_value);
                    present[key + 40] = false;
                }
                tds_hamt_map_destroy(&map);
                map = next;
                break;
            }

            default: {
                const size_t slot = (size_t)step % 5u;
                if (snapshots[slot].active) {
                    tds_hamt_map_destroy(&snapshots[slot].map);
                }
                snapshots[slot].active = true;
                snapshots[slot].map = tds_hamt_map_clone(&map);
                memcpy(snapshots[slot].present, present, sizeof(present));
                memcpy(snapshots[slot].values, values, sizeof(values));
                break;
            }
            }

            assert_int_model_matches(present, values, &map);
            for (size_t i = 0; i < 5; ++i) {
                if (snapshots[i].active) {
                    assert_int_model_matches(snapshots[i].present, snapshots[i].values, &snapshots[i].map);
                }
            }
        }

        for (size_t i = 0; i < 5; ++i) {
            if (snapshots[i].active) {
                tds_hamt_map_destroy(&snapshots[i].map);
            }
        }
        tds_hamt_map_destroy(&map);
    }
}

static void test_scripted_collision_snapshot_story(void) {
    tds_hamt_policy policy = explicit_map_policy();
    explicit_hash_key keys[96];
    bool present[96] = { false };
    int values[96] = { 0 };
    explicit_map_snapshot snapshots[4];
    memset(snapshots, 0, sizeof(snapshots));

    for (int id = 0; id != 96; ++id) {
        keys[id].id = id;
        if (id < 32) {
            keys[id].hash = 0x00ABCDEFu;
        } else if (id < 64) {
            keys[id].hash = (uint32_t)((id - 32) << 25);
        } else {
            keys[id].hash = 0x00000410u | (uint32_t)((id & 7) << 15) | (uint32_t)((id & 3) << 5);
        }
    }

    tds_hamt_map map = tds_hamt_map_create(&policy);

    for (int step = 0; step != 96; ++step) {
        const int id = (step * 37) % 96;
        const int value = id - 500;
        tds_hamt_map next;
        CHECK_STATUS(tds_hamt_map_set(&map, &keys[id], int_value(value), &next));
        tds_hamt_map_destroy(&map);
        map = next;
        present[id] = true;
        values[id] = value;

        if (step == 23 || step == 47 || step == 71) {
            capture_explicit_snapshot(&snapshots[(size_t)step / 24u], &map, present, values);
        }
    }

    assert_explicit_model_matches(keys, present, values, &map);

    for (int id = 5; id < 96; id += 11) {
        tds_hamt_map same;
        CHECK_STATUS(tds_hamt_map_set(&map, &keys[id], int_value(values[id]), &same));
        CHECK(tds_hamt_map_shares_root(&map, &same));
        tds_hamt_map_destroy(&same);
    }

    for (int id = 2; id < 96; id += 5) {
        const int value = 400 - id;
        tds_hamt_map next;
        CHECK_STATUS(tds_hamt_map_set(&map, &keys[id], int_value(value), &next));
        tds_hamt_map_destroy(&map);
        map = next;
        values[id] = value;
    }

    capture_explicit_snapshot(&snapshots[3], &map, present, values);

    for (int id = 0; id < 96; ++id) {
        if (id % 7 == 0 || id % 13 == 0) {
            tds_hamt_map next;
            CHECK_STATUS(tds_hamt_map_remove(&map, &keys[id], &next));
            tds_hamt_map_destroy(&map);
            map = next;
            present[id] = false;
        }
    }

    for (int id = 0; id < 96; id += 9) {
        bool added = true;
        tds_hamt_map next;
        const int value = 700 - id;
        CHECK_STATUS(tds_hamt_map_try_add(&map, &keys[id], int_value(value), &next, &added));
        CHECK(added == !present[id]);
        tds_hamt_map_destroy(&map);
        map = next;
        if (added) {
            present[id] = true;
            values[id] = value;
        }
    }

    for (int id = 1; id < 96; id += 10) {
        if (present[id]) {
            bool added = true;
            tds_hamt_map same;
            CHECK_STATUS(tds_hamt_map_try_add(&map, &keys[id], int_value(-900), &same, &added));
            CHECK(!added);
            CHECK(tds_hamt_map_shares_root(&map, &same));
            tds_hamt_map_destroy(&same);
        }
    }

    assert_explicit_model_matches(keys, present, values, &map);
    for (size_t slot = 0; slot != 4; ++slot) {
        CHECK(snapshots[slot].active);
        assert_explicit_model_matches(keys, snapshots[slot].present, snapshots[slot].values, &snapshots[slot].map);
    }

    tds_hamt_map cleared;
    CHECK_STATUS(tds_hamt_map_clear(&map, &cleared));
    CHECK(tds_hamt_map_is_empty(&cleared));
    CHECK(!tds_hamt_map_is_empty(&map));
    tds_hamt_map_destroy(&cleared);

    for (size_t slot = 0; slot != 4; ++slot) {
        tds_hamt_map_destroy(&snapshots[slot].map);
    }
    tds_hamt_map_destroy(&map);
}

static void test_random_history_with_colliding_hashes_matches_model(void) {
    tds_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    uint32_t rng = 0xBAD5EEDu;

    for (int iteration = 0; iteration < 200; ++iteration) {
        tds_hamt_map map = tds_hamt_map_create(&policy);
        bool present[81] = { false };
        int values[81] = { 0 };

        for (int step = 0; step < 160; ++step) {
            const int op = rng_range(&rng, 0, 4);
            const int key = rng_range(&rng, -40, 40);
            const int value = rng_range(&rng, -1000, 1000);
            tds_hamt_map next;

            if (op == 0 || op == 4) {
                CHECK_STATUS(tds_hamt_map_set(&map, int_key(key), int_value(value), &next));
                tds_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = true;
                values[key + 40] = value;
            } else if (op == 1) {
                CHECK_STATUS(tds_hamt_map_remove(&map, int_key(key), &next));
                tds_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = false;
            } else if (op == 2) {
                bool added = false;
                CHECK_STATUS(tds_hamt_map_try_add(&map, int_key(key), int_value(value), &next, &added));
                CHECK(added == !present[key + 40]);
                tds_hamt_map_destroy(&map);
                map = next;
                if (added) {
                    present[key + 40] = true;
                    values[key + 40] = value;
                }
            } else {
                bool removed = false;
                const void *removed_value = NULL;
                const bool expected_removed = present[key + 40];
                const int expected_value = values[key + 40];
                CHECK_STATUS(tds_hamt_map_try_remove(&map, int_key(key), &next, &removed, &removed_value));
                CHECK(removed == expected_removed);
                if (removed) {
                    CHECK(*(const int *)removed_value == expected_value);
                    present[key + 40] = false;
                }
                tds_hamt_map_destroy(&map);
                map = next;
            }
        }

        assert_int_model_matches(present, values, &map);
        tds_hamt_map_destroy(&map);
    }
}

static void test_set_add_remove_contains_and_persistence(void) {
    tds_hamt_set_policy policy = int_set_policy(int_hash);
    tds_hamt_set empty = tds_hamt_set_create(&policy);
    tds_hamt_set one;
    tds_hamt_set two;
    tds_hamt_set removed;
    CHECK_STATUS(tds_hamt_set_add(&empty, int_key(1), &one));
    CHECK_STATUS(tds_hamt_set_add(&one, int_key(2), &two));
    CHECK_STATUS(tds_hamt_set_remove(&two, int_key(1), &removed));

    CHECK(tds_hamt_set_is_empty(&empty));
    CHECK(tds_hamt_set_contains(&one, int_key(1)));
    CHECK(!tds_hamt_set_contains(&one, int_key(2)));
    CHECK(tds_hamt_set_contains(&two, int_key(1)));
    CHECK(tds_hamt_set_contains(&two, int_key(2)));
    CHECK(!tds_hamt_set_contains(&removed, int_key(1)));
    CHECK(tds_hamt_set_contains(&removed, int_key(2)));

    tds_hamt_set no_op;
    CHECK_STATUS(tds_hamt_set_add(&one, int_key(1), &no_op));
    CHECK(tds_hamt_set_shares_root(&one, &no_op));
    tds_hamt_set_destroy(&no_op);

    CHECK_STATUS(tds_hamt_set_remove(&one, int_key(9), &no_op));
    CHECK(tds_hamt_set_shares_root(&one, &no_op));
    tds_hamt_set_destroy(&no_op);

    bool added = true;
    CHECK_STATUS(tds_hamt_set_try_add(&one, int_key(1), &no_op, &added));
    CHECK(!added);
    CHECK(tds_hamt_set_shares_root(&one, &no_op));
    tds_hamt_set_destroy(&no_op);

    tds_hamt_set with_three;
    CHECK_STATUS(tds_hamt_set_try_add(&one, int_key(3), &with_three, &added));
    CHECK(added);
    CHECK(tds_hamt_set_contains(&with_three, int_key(3)));

    bool removed_flag = false;
    tds_hamt_set without_three;
    CHECK_STATUS(tds_hamt_set_try_remove(&with_three, int_key(3), &without_three, &removed_flag));
    CHECK(removed_flag);
    CHECK(!tds_hamt_set_contains(&without_three, int_key(3)));

    tds_hamt_set_destroy(&without_three);
    tds_hamt_set_destroy(&with_three);

    tds_hamt_set cleared;
    CHECK_STATUS(tds_hamt_set_clear(&two, &cleared));
    CHECK(tds_hamt_set_is_empty(&cleared));
    tds_hamt_set_destroy(&cleared);

    tds_hamt_set_destroy(&removed);
    tds_hamt_set_destroy(&two);
    tds_hamt_set_destroy(&one);
    tds_hamt_set_destroy(&empty);
}

static void test_set_custom_comparer_retains_first_item(void) {
    tds_hamt_set_policy policy = tds_hamt_set_policy_default();
    policy.hash = ci_hash;
    policy.equal = ci_equal;

    char stored_alpha[] = "Alpha";
    const void *items[] = { stored_alpha, "ALPHA", "beta" };
    tds_hamt_set set;
    CHECK_STATUS(tds_hamt_set_create_range(&policy, items, 3, &set));

    CHECK(tds_hamt_set_count(&set) == 2);
    CHECK(tds_hamt_set_contains(&set, "alpha"));
    const void *actual = NULL;
    CHECK(tds_hamt_set_try_get_value(&set, "ALPHA", &actual));
    CHECK(actual == stored_alpha);

    tds_hamt_set_destroy(&set);
}

static void model_from_items(const void *const *items, size_t item_count, bool model[61]) {
    memset(model, 0, 61 * sizeof(bool));
    for (size_t i = 0; i < item_count; ++i) {
        const int value = *(const int *)items[i];
        CHECK(value >= -30 && value <= 30);
        model[value + 30] = true;
    }
}

static void test_set_algebra_matches_model(void) {
    tds_hamt_set_policy policy = int_set_policy(int_hash);
    uint32_t rng = 0x51A7E5u;

    for (int iteration = 0; iteration < 200; ++iteration) {
        const size_t left_count = (size_t)rng_range(&rng, 0, 80);
        const size_t right_count = (size_t)rng_range(&rng, 0, 80);
        const void *left_items[80];
        const void *right_items[80];
        for (size_t i = 0; i < left_count; ++i) {
            left_items[i] = int_key(rng_range(&rng, -30, 30));
        }
        for (size_t i = 0; i < right_count; ++i) {
            right_items[i] = int_key(rng_range(&rng, -30, 30));
        }

        bool left_model[61];
        bool right_model[61];
        model_from_items(left_items, left_count, left_model);
        model_from_items(right_items, right_count, right_model);

        tds_hamt_set set;
        CHECK_STATUS(tds_hamt_set_create_range(&policy, left_items, left_count, &set));

        bool expected[61];
        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] || right_model[i];
        }
        tds_hamt_set actual;
        CHECK_STATUS(tds_hamt_set_union_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        tds_hamt_set_destroy(&actual);

        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] && right_model[i];
        }
        CHECK_STATUS(tds_hamt_set_intersect_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        tds_hamt_set_destroy(&actual);

        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] && !right_model[i];
        }
        CHECK_STATUS(tds_hamt_set_except_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        tds_hamt_set_destroy(&actual);

        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] != right_model[i];
        }
        CHECK_STATUS(tds_hamt_set_symmetric_except_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        tds_hamt_set_destroy(&actual);

        bool subset = true;
        bool superset = true;
        bool overlaps = false;
        size_t left_distinct = 0;
        size_t right_distinct = 0;
        for (size_t i = 0; i < 61; ++i) {
            if (left_model[i]) {
                ++left_distinct;
                subset = subset && right_model[i];
            }
            if (right_model[i]) {
                ++right_distinct;
                superset = superset && left_model[i];
            }
            overlaps = overlaps || (left_model[i] && right_model[i]);
        }

        bool relation = false;
        CHECK_STATUS(tds_hamt_set_is_subset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == subset);
        CHECK_STATUS(tds_hamt_set_is_proper_subset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == (subset && left_distinct < right_distinct));
        CHECK_STATUS(tds_hamt_set_is_superset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == superset);
        CHECK_STATUS(tds_hamt_set_is_proper_superset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == (superset && left_distinct > right_distinct));
        CHECK_STATUS(tds_hamt_set_overlaps_many(&set, right_items, right_count, &relation));
        CHECK(relation == overlaps);
        CHECK_STATUS(tds_hamt_set_equals_many(&set, right_items, right_count, &relation));
        CHECK(relation == (left_distinct == right_distinct && subset));

        tds_hamt_set_destroy(&set);
    }
}

static void test_set_symmetric_except_treats_duplicates_as_one_item(void) {
    tds_hamt_set_policy policy = int_set_policy(int_hash);
    const void *initial[] = { int_key(1), int_key(2) };
    const void *right[] = { int_key(1), int_key(1), int_key(3), int_key(3) };
    tds_hamt_set set;
    CHECK_STATUS(tds_hamt_set_create_range(&policy, initial, 2, &set));

    tds_hamt_set actual;
    CHECK_STATUS(tds_hamt_set_symmetric_except_many(&set, right, 4, &actual));
    bool expected[61] = { false };
    expected[2 + 30] = true;
    expected[3 + 30] = true;
    assert_set_model_matches(expected, &actual);

    tds_hamt_set_destroy(&actual);
    tds_hamt_set_destroy(&set);
}

static void test_concurrent_retained_snapshot_reads(void) {
    tds_hamt_policy map_policy = int_map_policy(int_hash);
    tds_hamt_set_policy set_policy = int_set_policy(int_hash);
    tds_hamt_map map = tds_hamt_map_create(&map_policy);
    tds_hamt_set set = tds_hamt_set_create(&set_policy);

    for (int key = 0; key != 128; ++key) {
        tds_hamt_map next_map;
        CHECK_STATUS(tds_hamt_map_set(&map, int_key(key), int_value(key * 3 - 200), &next_map));
        tds_hamt_map_destroy(&map);
        map = next_map;

        tds_hamt_set next_set;
        CHECK_STATUS(tds_hamt_set_add(&set, int_key(key), &next_set));
        tds_hamt_set_destroy(&set);
        set = next_set;
    }

    concurrent_retained_context context;
    const void *probe_keys[19];
    int probe_values[19];
    size_t probe_count = 0;
    for (int key = 0; key < 128; key += 7) {
        probe_keys[probe_count] = int_key(key);
        probe_values[probe_count] = key;
        ++probe_count;
    }

    context.map = &map;
    context.set = &set;
    context.probe_keys = probe_keys;
    context.probe_values = probe_values;
    context.probe_count = probe_count;
    test_atomic_long_init(&context.failures, 0);

#ifdef _WIN32
    enum { thread_count = 8 };
    HANDLE threads[thread_count];
    for (DWORD index = 0; index != thread_count; ++index) {
        threads[index] = CreateThread(NULL, 0, concurrent_retained_thread_proc, &context, 0, NULL);
        CHECK(threads[index] != NULL);
    }

    const DWORD wait_result = WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE);
    CHECK(wait_result == WAIT_OBJECT_0);
    for (DWORD index = 0; index != thread_count; ++index) {
        CloseHandle(threads[index]);
    }
#else
    for (int index = 0; index != 8; ++index) {
        concurrent_retained_worker(&context);
    }
#endif

    CHECK(test_atomic_long_read(&context.failures) == 0);
    tds_hamt_set_destroy(&set);
    tds_hamt_map_destroy(&map);
}

typedef struct counting_policy_state {
    long key_retains;
    long key_releases;
    long value_retains;
    long value_releases;
} counting_policy_state;

static void *counting_retain_key(const void *key, void *context) {
    counting_policy_state *state = (counting_policy_state *)context;
    if (key != NULL) {
        ++state->key_retains;
    }
    return (void *)key;
}

static void counting_release_key(void *key, void *context) {
    counting_policy_state *state = (counting_policy_state *)context;
    if (key != NULL) {
        ++state->key_releases;
    }
}

static void *counting_retain_value(const void *value, void *context) {
    counting_policy_state *state = (counting_policy_state *)context;
    if (value != NULL) {
        ++state->value_retains;
    }
    return (void *)value;
}

static void counting_release_value(void *value, void *context) {
    counting_policy_state *state = (counting_policy_state *)context;
    if (value != NULL) {
        ++state->value_releases;
    }
}

static void test_counting_policy_stays_balanced_and_aliasing_updates_are_safe(void) {
    counting_policy_state state = { 0, 0, 0, 0 };
    tds_hamt_policy policy = int_map_policy(int_hash);
    policy.retain_key = counting_retain_key;
    policy.release_key = counting_release_key;
    policy.retain_value = counting_retain_value;
    policy.release_value = counting_release_value;
    policy.context = &state;

    tds_hamt_map map = tds_hamt_map_create(&policy);
    tds_hamt_map snapshot;

    /* In-place (aliased result) updates: mixed history including collisions,
     * replaces, no-op replaces, and removals. */
    for (int i = 0; i < 64; ++i) {
        CHECK_STATUS(tds_hamt_map_set(&map, int_key(i % 32), int_value(i), &map));
    }
    CHECK(tds_hamt_map_count(&map) == 32);

    /* A retained snapshot, then more aliased edits on the main line. */
    snapshot = tds_hamt_map_clone(&map);
    for (int i = 0; i < 16; ++i) {
        CHECK_STATUS(tds_hamt_map_remove(&map, int_key(i), &map));
    }
    CHECK(tds_hamt_map_count(&map) == 16);
    CHECK(tds_hamt_map_count(&snapshot) == 32);

    /* No-op replace through the aliased path must not unbalance refcounts. */
    CHECK_STATUS(tds_hamt_map_set(&map, int_key(20), int_value(52), &map));

    tds_hamt_map_destroy(&snapshot);
    tds_hamt_map_destroy(&map);

    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
    CHECK(state.key_retains > 0);
}

static void test_aliased_set_add_keeps_item_refcounts_balanced(void) {
    counting_policy_state state = { 0, 0, 0, 0 };
    tds_hamt_set_policy policy = int_set_policy(int_hash);
    policy.retain_item = counting_retain_key;
    policy.release_item = counting_release_key;
    policy.context = &state;

    tds_hamt_set set = tds_hamt_set_create(&policy);

    /* In-place (aliased result) adds, including duplicate items that reuse
     * the existing root through the unit-value no-op replace path. */
    for (int i = 0; i < 48; ++i) {
        CHECK_STATUS(tds_hamt_set_add(&set, int_key(i % 24), &set));
    }
    CHECK(tds_hamt_set_count(&set) == 24);

    /* A retained snapshot must survive further aliased adds. */
    tds_hamt_set snapshot = tds_hamt_set_clone(&set);
    for (int i = 24; i < 32; ++i) {
        CHECK_STATUS(tds_hamt_set_add(&set, int_key(i), &set));
    }
    CHECK(tds_hamt_set_count(&set) == 32);
    CHECK(tds_hamt_set_count(&snapshot) == 24);
    CHECK(tds_hamt_set_contains(&set, int_key(31)));
    CHECK(!tds_hamt_set_contains(&snapshot, int_key(31)));

    tds_hamt_set_destroy(&snapshot);
    tds_hamt_set_destroy(&set);

    /* Every retained item must be released: an aliased add that leaked the
     * overwritten root would leave its items retained forever. */
    CHECK(state.key_retains == state.key_releases);
    CHECK(state.key_retains > 0);
}

static void test_aliased_map_add_duplicate_preserves_source(void) {
    counting_policy_state state = { 0, 0, 0, 0 };
    tds_hamt_policy policy = int_map_policy(int_hash);
    policy.retain_key = counting_retain_key;
    policy.release_key = counting_release_key;
    policy.retain_value = counting_retain_value;
    policy.release_value = counting_release_value;
    policy.context = &state;

    tds_hamt_map map = tds_hamt_map_create(&policy);
    for (int i = 0; i < 16; ++i) {
        CHECK_STATUS(tds_hamt_map_add(&map, int_key(i), int_value(i * 2), &map));
    }
    CHECK(tds_hamt_map_count(&map) == 16);

    /* An aliased duplicate rejection must leave the caller's map intact. */
    CHECK(tds_hamt_map_add(&map, int_key(5), int_value(999), &map) == TDS_HAMT_DUPLICATE_KEY);
    CHECK(tds_hamt_map_count(&map) == 16);
    for (int i = 0; i < 16; ++i) {
        assert_int_value(&map, i, i * 2);
    }

    /* The rejected map must remain fully usable afterwards. */
    CHECK_STATUS(tds_hamt_map_add(&map, int_key(16), int_value(32), &map));
    CHECK(tds_hamt_map_count(&map) == 17);
    assert_int_value(&map, 16, 32);

    /* A distinct result still releases the rejected version, leaving an
     * empty result value and an untouched source. */
    tds_hamt_map rejected;
    CHECK(tds_hamt_map_add(&map, int_key(5), int_value(999), &rejected) == TDS_HAMT_DUPLICATE_KEY);
    CHECK(tds_hamt_map_count(&rejected) == 0);
    CHECK(tds_hamt_map_count(&map) == 17);
    assert_int_value(&map, 5, 10);

    tds_hamt_map_destroy(&map);

    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
    CHECK(state.key_retains > 0);
}

static void test_aliased_try_remove_reports_null_removed_value(void) {
    counting_policy_state state = { 0, 0, 0, 0 };
    tds_hamt_policy policy = int_map_policy(int_hash);
    policy.retain_key = counting_retain_key;
    policy.release_key = counting_release_key;
    policy.retain_value = counting_retain_value;
    policy.release_value = counting_release_value;
    policy.context = &state;

    tds_hamt_map map = tds_hamt_map_create(&policy);
    for (int i = 0; i < 8; ++i) {
        CHECK_STATUS(tds_hamt_map_set(&map, int_key(i), int_value(i + 100), &map));
    }

    /* A distinct result keeps the removed value pointer valid through the
     * still-live source version. */
    bool removed = false;
    const void *removed_value = NULL;
    tds_hamt_map without_three;
    CHECK_STATUS(tds_hamt_map_try_remove(&map, int_key(3), &without_three, &removed, &removed_value));
    CHECK(removed);
    CHECK(removed_value != NULL);
    CHECK(*(const int *)removed_value == 103);
    tds_hamt_map_destroy(&without_three);

    /* An aliased removal releases the previous version inside the call, so
     * the removed value pointer is reported as NULL while the removed flag
     * stays accurate. */
    removed = false;
    removed_value = int_value(0);
    CHECK_STATUS(tds_hamt_map_try_remove(&map, int_key(5), &map, &removed, &removed_value));
    CHECK(removed);
    CHECK(removed_value == NULL);
    CHECK(tds_hamt_map_count(&map) == 7);
    CHECK(!tds_hamt_map_contains_key(&map, int_key(5)));

    /* An aliased miss also reports no removed value. */
    removed = true;
    removed_value = int_value(0);
    CHECK_STATUS(tds_hamt_map_try_remove(&map, int_key(5), &map, &removed, &removed_value));
    CHECK(!removed);
    CHECK(removed_value == NULL);
    CHECK(tds_hamt_map_count(&map) == 7);

    tds_hamt_map_destroy(&map);

    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
}

static const test_case tests[] = {
    { "empty map has no entries", test_empty_map_has_no_entries },
    { "set item adds replaces and preserves old versions", test_set_adds_replaces_and_preserves_old_versions },
    { "add and try_add reject duplicates", test_add_and_try_add_reject_duplicates },
    { "remove and try_remove delete present keys", test_remove_and_try_remove_delete_present_keys },
    { "set_many and clear preserve contracts", test_set_many_and_clear_preserve_contracts },
    { "create_range last wins and retains first equivalent key", test_create_range_last_wins_and_retains_first_equivalent_key },
    { "equal hash collision bucket preserves every key", test_equal_hash_collision_bucket_preserves_every_key },
    { "deep shared hash prefixes lookup and remove correctly", test_deep_shared_hash_prefixes_lookup_and_remove_correctly },
    { "depth seven iterator traversal", test_depth_seven_iterator_traversal },
    { "allocation failures unwind node_set and merge", test_allocation_failures_unwind_node_set_and_merge },
    { "collision bucket splits and hash mismatch probes miss", test_collision_bucket_splits_and_hash_mismatch_probes_miss },
    { "collision bucket equal value keeps root and key object", test_collision_bucket_equal_value_keeps_root_and_key_object },
    { "structure root shape and sharing", test_structure_root_shape_and_sharing },
    { "CHAMP independent histories and typed diff", test_champ_independent_histories_and_typed_diff },
    { "iterator copy advances independently", test_iterator_copy_advances_independently },
    { "random history matches model and preserves snapshots", test_random_history_matches_model_and_preserves_snapshots },
    { "scripted collision snapshot story", test_scripted_collision_snapshot_story },
    { "random history with colliding hashes matches model", test_random_history_with_colliding_hashes_matches_model },
    { "set add remove contains and persistence", test_set_add_remove_contains_and_persistence },
    { "set custom comparer retains first item", test_set_custom_comparer_retains_first_item },
    { "set algebra matches model", test_set_algebra_matches_model },
    { "set symmetric_except treats duplicates as one item", test_set_symmetric_except_treats_duplicates_as_one_item },
    { "concurrent retained snapshot reads", test_concurrent_retained_snapshot_reads },
    { "counting policy stays balanced and aliasing updates are safe",
      test_counting_policy_stays_balanced_and_aliasing_updates_are_safe },
    { "aliased set_add keeps item refcounts balanced",
      test_aliased_set_add_keeps_item_refcounts_balanced },
    { "aliased map_add duplicate preserves source",
      test_aliased_map_add_duplicate_preserves_source },
    { "aliased try_remove reports null removed value",
      test_aliased_try_remove_reports_null_removed_value }
};

int main(void) {
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    init_pools();

    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < test_count; ++i) {
        tests[i].run();
        printf("[PASS] %s\n", tests[i].name);
    }

    printf("%zu test(s) passed\n", test_count);
    return 0;
}
