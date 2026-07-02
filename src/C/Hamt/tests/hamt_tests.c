#include <Tools/DataStructures/Hamt/hamt.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    CHECK(tds_hamt_map_debug_root_child_identities(&map, before_children, 4) == 2);
    CHECK(tds_hamt_map_debug_root_child_identities(&updated, after_children, 4) == 2);
    CHECK(before_children[0] != after_children[0]);
    CHECK(before_children[1] == after_children[1]);

    tds_hamt_map no_op;
    CHECK_STATUS(tds_hamt_map_set(&map, &a, int_value(1), &no_op));
    CHECK(tds_hamt_map_shares_root(&map, &no_op));
    tds_hamt_map_destroy(&no_op);

    tds_hamt_map_destroy(&updated);
    tds_hamt_map_destroy(&map);
    tds_hamt_map_destroy(&single);
    tds_hamt_map_destroy(&empty);
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

        CHECK(tds_hamt_set_is_subset_of_many(&set, right_items, right_count) == subset);
        CHECK(tds_hamt_set_is_proper_subset_of_many(&set, right_items, right_count) ==
              (subset && left_distinct < right_distinct));
        CHECK(tds_hamt_set_is_superset_of_many(&set, right_items, right_count) == superset);
        CHECK(tds_hamt_set_is_proper_superset_of_many(&set, right_items, right_count) ==
              (superset && left_distinct > right_distinct));
        CHECK(tds_hamt_set_overlaps_many(&set, right_items, right_count) == overlaps);
        CHECK(tds_hamt_set_equals_many(&set, right_items, right_count) ==
              (left_distinct == right_distinct && subset));

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

static const test_case tests[] = {
    { "empty map has no entries", test_empty_map_has_no_entries },
    { "set item adds replaces and preserves old versions", test_set_adds_replaces_and_preserves_old_versions },
    { "add and try_add reject duplicates", test_add_and_try_add_reject_duplicates },
    { "remove and try_remove delete present keys", test_remove_and_try_remove_delete_present_keys },
    { "set_many and clear preserve contracts", test_set_many_and_clear_preserve_contracts },
    { "create_range last wins and retains first equivalent key", test_create_range_last_wins_and_retains_first_equivalent_key },
    { "equal hash collision bucket preserves every key", test_equal_hash_collision_bucket_preserves_every_key },
    { "deep shared hash prefixes lookup and remove correctly", test_deep_shared_hash_prefixes_lookup_and_remove_correctly },
    { "collision bucket splits and hash mismatch probes miss", test_collision_bucket_splits_and_hash_mismatch_probes_miss },
    { "collision bucket equal value keeps root and key object", test_collision_bucket_equal_value_keeps_root_and_key_object },
    { "structure root shape and sharing", test_structure_root_shape_and_sharing },
    { "iterator copy advances independently", test_iterator_copy_advances_independently },
    { "random history matches model and preserves snapshots", test_random_history_matches_model_and_preserves_snapshots },
    { "random history with colliding hashes matches model", test_random_history_with_colliding_hashes_matches_model },
    { "set add remove contains and persistence", test_set_add_remove_contains_and_persistence },
    { "set custom comparer retains first item", test_set_custom_comparer_retains_first_item },
    { "set algebra matches model", test_set_algebra_matches_model },
    { "set symmetric_except treats duplicates as one item", test_set_symmetric_except_treats_duplicates_as_one_item }
};

int main(void) {
    init_pools();

    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < test_count; ++i) {
        tests[i].run();
        printf("[PASS] %s\n", tests[i].name);
    }

    printf("%zu test(s) passed\n", test_count);
    return 0;
}
