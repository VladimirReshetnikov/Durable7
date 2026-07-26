/*
 * Tests for the persistent hash map and set.
 *
 * Covers policy retention, representative choice, structural sharing on no-op edits, and the
 * transient session lifecycle.
 */

#include <durable7/hamt/hamt.h>
#include <durable7/test_support/headless_test_process.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef D7_HAMT_TESTING
#error The native HAMT test executable requires D7_HAMT_TESTING allocation hooks.
#endif

void d7_hamt_test_fail_allocations_after(size_t successful_allocations);
void d7_hamt_test_reset_allocator(void);

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
        d7_hamt_status status_value = (expression); \
        if (status_value != D7_HAMT_OK) { \
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

static d7_hamt_policy int_map_policy(uint32_t (*hash)(const void *, void *)) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = hash;
    policy.key_equal = int_equal;
    policy.value_equal = int_equal;
    return policy;
}

static d7_hamt_set_policy int_set_policy(uint32_t (*hash)(const void *, void *)) {
    d7_hamt_set_policy policy = d7_hamt_set_policy_default();
    policy.hash = hash;
    policy.equal = int_equal;
    return policy;
}

static void assert_int_value(const d7_hamt_map *map, int key, int expected) {
    const void *actual = NULL;
    CHECK(d7_hamt_map_try_get(map, int_key(key), &actual));
    CHECK(actual != NULL);
    CHECK(*(const int *)actual == expected);
}

static void assert_int_model_matches(
    const bool present[81],
    const int values[81],
    const d7_hamt_map *map) {
    size_t expected_count = 0;
    for (int key = -40; key <= 40; ++key) {
        if (present[key + 40]) {
            ++expected_count;
            assert_int_value(map, key, values[key + 40]);
        } else {
            CHECK(!d7_hamt_map_contains_key(map, int_key(key)));
        }
    }

    CHECK(d7_hamt_map_count(map) == expected_count);

    size_t enumerated = 0;
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(map, &iterator);
    const void *key_ptr = NULL;
    const void *value_ptr = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key_ptr, &value_ptr)) {
        const int key = *(const int *)key_ptr;
        CHECK(key >= -40 && key <= 40);
        CHECK(present[key + 40]);
        CHECK(*(const int *)value_ptr == values[key + 40]);
        ++enumerated;
    }

    CHECK(enumerated == expected_count);
}

static void assert_int_transient_model_matches(
    const bool present[81],
    const int values[81],
    const d7_hamt_map_transient *transient) {
    size_t expected_count = 0;
    for (int key = -40; key <= 40; ++key) {
        bool found = false;
        const void *actual = NULL;
        CHECK_STATUS(d7_hamt_map_transient_try_get(
            transient,
            int_key(key),
            &found,
            &actual));
        if (present[key + 40]) {
            ++expected_count;
            CHECK(found);
            CHECK(actual != NULL);
            CHECK(*(const int *)actual == values[key + 40]);
        } else {
            CHECK(!found);
            CHECK(actual == NULL);
        }
    }

    size_t actual_count = SIZE_MAX;
    CHECK_STATUS(d7_hamt_map_transient_count(transient, &actual_count));
    CHECK(actual_count == expected_count);

    bool seen[81] = { false };
    size_t enumerated = 0;
    d7_hamt_map_transient_iterator iterator;
    CHECK_STATUS(d7_hamt_map_transient_iterator_init(transient, &iterator));
    for (;;) {
        bool has_value = false;
        const void *key_ptr = NULL;
        const void *value_ptr = NULL;
        CHECK_STATUS(d7_hamt_map_transient_iterator_next(
            &iterator,
            &has_value,
            &key_ptr,
            &value_ptr));
        if (!has_value) {
            break;
        }

        const int key = *(const int *)key_ptr;
        CHECK(key >= -40 && key <= 40);
        CHECK(present[key + 40]);
        CHECK(!seen[key + 40]);
        CHECK(*(const int *)value_ptr == values[key + 40]);
        seen[key + 40] = true;
        ++enumerated;
    }
    CHECK(enumerated == expected_count);
}

static void assert_set_model_matches(const bool present[61], const d7_hamt_set *set) {
    size_t expected_count = 0;
    for (int value = -30; value <= 30; ++value) {
        if (present[value + 30]) {
            ++expected_count;
            CHECK(d7_hamt_set_contains(set, int_key(value)));
        } else {
            CHECK(!d7_hamt_set_contains(set, int_key(value)));
        }
    }

    CHECK(d7_hamt_set_count(set) == expected_count);

    d7_hamt_set_iterator iterator;
    d7_hamt_set_iterator_init(set, &iterator);
    const void *item = NULL;
    size_t enumerated = 0;
    while (d7_hamt_set_iterator_next(&iterator, &item)) {
        const int value = *(const int *)item;
        CHECK(value >= -30 && value <= 30);
        CHECK(present[value + 30]);
        ++enumerated;
    }

    CHECK(enumerated == expected_count);
}

typedef struct concurrent_retained_context {
    const d7_hamt_map *map;
    const d7_hamt_set *set;
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
        if (d7_hamt_map_count(context->map) != 128 || d7_hamt_set_count(context->set) != 128) {
            record_concurrent_failure(context);
            return;
        }

        for (size_t index = 0; index != context->probe_count; ++index) {
            const int key = context->probe_values[index];
            const void *actual = NULL;
            if (!d7_hamt_map_try_get(context->map, context->probe_keys[index], &actual) ||
                actual == NULL ||
                *(const int *)actual != key * 3 - 200 ||
                !d7_hamt_set_contains(context->set, context->probe_keys[index])) {
                record_concurrent_failure(context);
                return;
            }
        }

        d7_hamt_map_iterator map_iterator;
        d7_hamt_map_iterator_init(context->map, &map_iterator);
        const void *key_ptr = NULL;
        const void *value_ptr = NULL;
        size_t map_count = 0;
        while (d7_hamt_map_iterator_next(&map_iterator, &key_ptr, &value_ptr)) {
            const int key = *(const int *)key_ptr;
            if (key < 0 || key >= 128 || *(const int *)value_ptr != key * 3 - 200) {
                record_concurrent_failure(context);
                return;
            }

            ++map_count;
        }

        d7_hamt_set_iterator set_iterator;
        d7_hamt_set_iterator_init(context->set, &set_iterator);
        const void *item = NULL;
        size_t set_count = 0;
        while (d7_hamt_set_iterator_next(&set_iterator, &item)) {
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

static d7_hamt_policy explicit_map_policy(void) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = explicit_hash_key_hash;
    policy.key_equal = explicit_hash_key_equal;
    policy.value_equal = int_equal;
    return policy;
}

static void assert_explicit_value(
    const d7_hamt_map *map,
    const explicit_hash_key *key,
    int expected) {
    const void *actual = NULL;
    CHECK(d7_hamt_map_try_get(map, key, &actual));
    CHECK(actual != NULL);
    CHECK(*(const int *)actual == expected);
}

typedef struct factory_test_state {
    size_t hash_calls;
    size_t key_equal_calls;
    size_t value_equal_calls;
    size_t add_calls;
    size_t update_calls;
    bool constant_hash;
    d7_hamt_status add_status;
    d7_hamt_status update_status;
    const void *add_candidate;
    const void *update_candidate;
    const void *observed_key;
    const void *observed_stored_value;
} factory_test_state;

static uint32_t factory_test_hash(const void *item, void *context) {
    factory_test_state *state = (factory_test_state *)context;
    ++state->hash_calls;
    return state->constant_hash ? 0u : ((const explicit_hash_key *)item)->hash;
}

static bool factory_test_key_equal(const void *left, const void *right, void *context) {
    factory_test_state *state = (factory_test_state *)context;
    ++state->key_equal_calls;
    return ((const explicit_hash_key *)left)->id == ((const explicit_hash_key *)right)->id;
}

static bool factory_test_value_equal(const void *left, const void *right, void *context) {
    factory_test_state *state = (factory_test_state *)context;
    ++state->value_equal_calls;
    return *(const int *)left == *(const int *)right;
}

static d7_hamt_status factory_test_add(
    const void *key,
    void *context,
    const void **value) {
    factory_test_state *state = (factory_test_state *)context;
    ++state->add_calls;
    state->observed_key = key;
    if (state->add_status == D7_HAMT_OK) {
        *value = state->add_candidate;
    }
    return state->add_status;
}

static d7_hamt_status factory_test_update(
    const void *key,
    const void *stored_value,
    void *context,
    const void **value) {
    factory_test_state *state = (factory_test_state *)context;
    ++state->update_calls;
    state->observed_key = key;
    state->observed_stored_value = stored_value;
    if (state->update_status == D7_HAMT_OK) {
        *value = state->update_candidate;
    }
    return state->update_status;
}

static d7_hamt_policy factory_test_policy(factory_test_state *state) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = factory_test_hash;
    policy.key_equal = factory_test_key_equal;
    policy.value_equal = factory_test_value_equal;
    policy.context = state;
    return policy;
}

static void factory_test_reset_counts(factory_test_state *state) {
    state->hash_calls = 0;
    state->key_equal_calls = 0;
    state->value_equal_calls = 0;
    state->add_calls = 0;
    state->update_calls = 0;
    state->observed_key = NULL;
    state->observed_stored_value = NULL;
}

static void *clone_int_value(const void *value, void *context) {
    (void)context;
    if (value == NULL) {
        return NULL;
    }
    int *clone = (int *)malloc(sizeof(*clone));
    if (clone != NULL) {
        *clone = *(const int *)value;
    }
    return clone;
}

static void release_cloned_int_value(void *value, void *context) {
    (void)context;
    free(value);
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
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map empty = d7_hamt_map_create(&policy);

    CHECK(d7_hamt_map_is_empty(&empty));
    CHECK(d7_hamt_map_count(&empty) == 0);
    CHECK(!d7_hamt_map_contains_key(&empty, int_key(1)));

    d7_hamt_map removed;
    CHECK_STATUS(d7_hamt_map_remove(&empty, int_key(1), &removed));
    CHECK(d7_hamt_map_shares_root(&empty, &removed));
    d7_hamt_map_destroy(&removed);

    d7_hamt_map cleared;
    CHECK_STATUS(d7_hamt_map_clear(&empty, &cleared));
    CHECK(d7_hamt_map_shares_root(&empty, &cleared));
    d7_hamt_map_destroy(&cleared);
    d7_hamt_map_destroy(&empty);
}

static void test_set_adds_replaces_and_preserves_old_versions(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map empty = d7_hamt_map_create(&policy);
    d7_hamt_map one;
    d7_hamt_map two;
    d7_hamt_map replaced;

    CHECK_STATUS(d7_hamt_map_set(&empty, int_key(1), int_value(10), &one));
    CHECK_STATUS(d7_hamt_map_set(&one, int_key(2), int_value(20), &two));
    CHECK_STATUS(d7_hamt_map_set(&two, int_key(1), int_value(11), &replaced));

    CHECK(d7_hamt_map_is_empty(&empty));
    assert_int_value(&one, 1, 10);
    assert_int_value(&two, 1, 10);
    assert_int_value(&two, 2, 20);
    assert_int_value(&replaced, 1, 11);
    assert_int_value(&replaced, 2, 20);

    d7_hamt_map no_op;
    CHECK_STATUS(d7_hamt_map_set(&replaced, int_key(1), int_value(11), &no_op));
    CHECK(d7_hamt_map_shares_root(&replaced, &no_op));

    d7_hamt_map_destroy(&no_op);
    d7_hamt_map_destroy(&replaced);
    d7_hamt_map_destroy(&two);
    d7_hamt_map_destroy(&one);
    d7_hamt_map_destroy(&empty);
}

static void test_add_and_try_add_reject_duplicates(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map empty = d7_hamt_map_create(&policy);
    d7_hamt_map map;
    CHECK_STATUS(d7_hamt_map_add(&empty, int_key(1), int_value(10), &map));

    d7_hamt_map duplicate;
    CHECK(d7_hamt_map_add(&map, int_key(1), int_value(99), &duplicate) == D7_HAMT_DUPLICATE_KEY);

    bool added = true;
    d7_hamt_map same;
    CHECK_STATUS(d7_hamt_map_try_add(&map, int_key(1), int_value(99), &same, &added));
    CHECK(!added);
    CHECK(d7_hamt_map_shares_root(&map, &same));
    d7_hamt_map_destroy(&same);

    d7_hamt_map with_two;
    CHECK_STATUS(d7_hamt_map_try_add(&map, int_key(2), int_value(20), &with_two, &added));
    CHECK(added);
    assert_int_value(&with_two, 2, 20);
    CHECK(!d7_hamt_map_contains_key(&map, int_key(2)));

    d7_hamt_map_destroy(&with_two);
    d7_hamt_map_destroy(&map);
    d7_hamt_map_destroy(&empty);
}

static void test_factories_select_once_and_preserve_representatives(void) {
    factory_test_state state = { 0 };
    d7_hamt_policy policy = factory_test_policy(&state);
    explicit_hash_key stored_key = { 1, 0u };
    explicit_hash_key lookup_key = { 1, 0u };
    explicit_hash_key missing_key = { 2, 32u };
    int stored_value = 10;
    int equal_candidate = 10;
    int changed_candidate = 11;
    int added_candidate = 20;

    d7_hamt_map empty = d7_hamt_map_create(&policy);
    d7_hamt_map source;
    CHECK_STATUS(d7_hamt_map_set(&empty, &stored_key, &stored_value, &source));

    factory_test_reset_counts(&state);
    d7_hamt_map untouched = d7_hamt_map_clone(&source);
    const void *untouched_root = d7_hamt_map_debug_root_identity(&untouched);
    const void *selected = &added_candidate;
    CHECK(d7_hamt_map_get_or_add(
        &source, &lookup_key, NULL, &state, &untouched, &selected)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(state.hash_calls == 0 && state.add_calls == 0 && state.update_calls == 0);
    CHECK(d7_hamt_map_debug_root_identity(&untouched) == untouched_root);
    CHECK(selected == &added_candidate);
    CHECK(d7_hamt_map_add_or_update(
        &source,
        &lookup_key,
        factory_test_add,
        &state,
        NULL,
        &state,
        &untouched,
        &selected) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(state.hash_calls == 0);
    d7_hamt_map_destroy(&untouched);

    state.add_candidate = &added_candidate;
    factory_test_reset_counts(&state);
    d7_hamt_map hit;
    selected = NULL;
    CHECK_STATUS(d7_hamt_map_get_or_add(
        &source, &lookup_key, factory_test_add, &state, &hit, &selected));
    CHECK(state.hash_calls == 1);
    CHECK(state.add_calls == 0 && state.update_calls == 0);
    CHECK(state.value_equal_calls == 0);
    CHECK(d7_hamt_map_shares_root(&source, &hit));
    CHECK(selected == &stored_value);
    d7_hamt_map_destroy(&hit);

    state.update_candidate = &equal_candidate;
    factory_test_reset_counts(&state);
    d7_hamt_map equal;
    CHECK_STATUS(d7_hamt_map_add_or_update(
        &source,
        &lookup_key,
        factory_test_add,
        &state,
        factory_test_update,
        &state,
        &equal,
        &selected));
    CHECK(state.hash_calls == 1);
    CHECK(state.add_calls == 0 && state.update_calls == 1);
    CHECK(state.value_equal_calls == 1);
    CHECK(state.observed_key == &lookup_key);
    CHECK(state.observed_stored_value == &stored_value);
    CHECK(d7_hamt_map_shares_root(&source, &equal));
    CHECK(selected == &stored_value);
    d7_hamt_map_destroy(&equal);

    state.update_candidate = &changed_candidate;
    factory_test_reset_counts(&state);
    d7_hamt_map changed;
    CHECK_STATUS(d7_hamt_map_add_or_update(
        &source,
        &lookup_key,
        factory_test_add,
        &state,
        factory_test_update,
        &state,
        &changed,
        &selected));
    CHECK(state.hash_calls == 1);
    CHECK(state.add_calls == 0 && state.update_calls == 1);
    CHECK(state.observed_key == &lookup_key);
    CHECK(!d7_hamt_map_shares_root(&source, &changed));
    CHECK(selected == &changed_candidate);
    const void *actual_key = NULL;
    CHECK(d7_hamt_map_try_get_key(&changed, &lookup_key, &actual_key));
    CHECK(actual_key == &stored_key);
    assert_explicit_value(&source, &stored_key, stored_value);

    factory_test_reset_counts(&state);
    d7_hamt_map added;
    selected = NULL;
    CHECK_STATUS(d7_hamt_map_get_or_add(
        &source, &missing_key, factory_test_add, &state, &added, &selected));
    CHECK(state.hash_calls == 1);
    CHECK(state.add_calls == 1 && state.update_calls == 0);
    CHECK(state.observed_key == &missing_key);
    CHECK(selected == &added_candidate);
    CHECK(d7_hamt_map_count(&added) == 2);
    CHECK(!d7_hamt_map_contains_key(&source, &missing_key));

    d7_hamt_map_destroy(&added);
    d7_hamt_map_destroy(&changed);
    d7_hamt_map_destroy(&source);
    d7_hamt_map_destroy(&empty);
}

static void test_factories_cover_collision_bitmap_and_retained_outputs(void) {
    factory_test_state collision_state = { 0 };
    collision_state.constant_hash = true;
    d7_hamt_policy collision_policy = factory_test_policy(&collision_state);
    explicit_hash_key first = { 1, 0u };
    explicit_hash_key second = { 2, 0u };
    explicit_hash_key third = { 3, 0u };
    int one = 1;
    int two = 2;
    int twenty = 20;
    int three = 3;
    d7_hamt_map collisions = d7_hamt_map_create(&collision_policy);
    CHECK_STATUS(d7_hamt_map_set(&collisions, &first, &one, &collisions));
    CHECK_STATUS(d7_hamt_map_set(&collisions, &second, &two, &collisions));
    CHECK(d7_hamt_map_debug_root_kind(&collisions) == D7_HAMT_NODE_COLLISION);

    collision_state.add_candidate = &three;
    collision_state.update_candidate = &twenty;
    factory_test_reset_counts(&collision_state);
    const void *selected = NULL;
    d7_hamt_map replaced;
    CHECK_STATUS(d7_hamt_map_add_or_update(
        &collisions,
        &second,
        factory_test_add,
        &collision_state,
        factory_test_update,
        &collision_state,
        &replaced,
        &selected));
    CHECK(collision_state.hash_calls == 1 && collision_state.update_calls == 1);
    CHECK(selected == &twenty);
    assert_explicit_value(&replaced, &second, twenty);

    factory_test_reset_counts(&collision_state);
    d7_hamt_map expanded;
    CHECK_STATUS(d7_hamt_map_get_or_add(
        &collisions,
        &third,
        factory_test_add,
        &collision_state,
        &expanded,
        &selected));
    CHECK(collision_state.hash_calls == 1 && collision_state.add_calls == 1);
    CHECK(selected == &three);
    CHECK(d7_hamt_map_count(&expanded) == 3);
    CHECK(d7_hamt_map_debug_root_kind(&expanded) == D7_HAMT_NODE_COLLISION);

    factory_test_state bitmap_state = { 0 };
    d7_hamt_policy bitmap_policy = factory_test_policy(&bitmap_state);
    explicit_hash_key zero = { 10, 0u };
    explicit_hash_key deep = { 11, 32u };
    explicit_hash_key side = { 12, 1u };
    int ten = 10;
    int eleven = 11;
    int twelve = 12;
    int changed_eleven = 111;
    int changed_twelve = 120;
    int thirteen = 13;
    explicit_hash_key new_side = { 13, 2u };
    d7_hamt_map bitmap = d7_hamt_map_create(&bitmap_policy);
    CHECK_STATUS(d7_hamt_map_set(&bitmap, &zero, &ten, &bitmap));
    CHECK_STATUS(d7_hamt_map_set(&bitmap, &deep, &eleven, &bitmap));
    CHECK_STATUS(d7_hamt_map_set(&bitmap, &side, &twelve, &bitmap));
    CHECK(d7_hamt_map_debug_root_kind(&bitmap) == D7_HAMT_NODE_BITMAP_INDEXED);
    bitmap_state.add_candidate = &twelve;
    bitmap_state.update_candidate = &changed_eleven;
    factory_test_reset_counts(&bitmap_state);
    d7_hamt_map bitmap_changed;
    CHECK_STATUS(d7_hamt_map_add_or_update(
        &bitmap,
        &deep,
        factory_test_add,
        &bitmap_state,
        factory_test_update,
        &bitmap_state,
        &bitmap_changed,
        &selected));
    CHECK(bitmap_state.hash_calls == 1 && bitmap_state.update_calls == 1);
    CHECK(selected == &changed_eleven);
    assert_explicit_value(&bitmap_changed, &deep, changed_eleven);

    bitmap_state.update_candidate = &changed_twelve;
    factory_test_reset_counts(&bitmap_state);
    d7_hamt_map bitmap_inline_changed;
    CHECK_STATUS(d7_hamt_map_add_or_update(
        &bitmap,
        &side,
        factory_test_add,
        &bitmap_state,
        factory_test_update,
        &bitmap_state,
        &bitmap_inline_changed,
        &selected));
    CHECK(bitmap_state.hash_calls == 1 && bitmap_state.update_calls == 1);
    CHECK(selected == &changed_twelve);
    assert_explicit_value(&bitmap_inline_changed, &side, changed_twelve);

    bitmap_state.add_candidate = &thirteen;
    factory_test_reset_counts(&bitmap_state);
    d7_hamt_map bitmap_added;
    CHECK_STATUS(d7_hamt_map_get_or_add(
        &bitmap,
        &new_side,
        factory_test_add,
        &bitmap_state,
        &bitmap_added,
        &selected));
    CHECK(bitmap_state.hash_calls == 1 && bitmap_state.add_calls == 1);
    CHECK(selected == &thirteen);
    assert_explicit_value(&bitmap_added, &new_side, thirteen);

    factory_test_state retained_state = { 0 };
    retained_state.add_candidate = &three;
    d7_hamt_policy retained_policy = factory_test_policy(&retained_state);
    retained_policy.retain_value = clone_int_value;
    retained_policy.release_value = release_cloned_int_value;
    d7_hamt_map retained_empty = d7_hamt_map_create(&retained_policy);
    d7_hamt_map retained;
    selected = NULL;
    CHECK_STATUS(d7_hamt_map_get_or_add(
        &retained_empty,
        &third,
        factory_test_add,
        &retained_state,
        &retained,
        &selected));
    CHECK(selected != &three);
    CHECK(*(const int *)selected == three);
    const void *looked_up = NULL;
    CHECK(d7_hamt_map_try_get(&retained, &third, &looked_up));
    CHECK(looked_up == selected);

    d7_hamt_map_destroy(&retained);
    d7_hamt_map_destroy(&retained_empty);
    d7_hamt_map_destroy(&bitmap_added);
    d7_hamt_map_destroy(&bitmap_inline_changed);
    d7_hamt_map_destroy(&bitmap_changed);
    d7_hamt_map_destroy(&bitmap);
    d7_hamt_map_destroy(&expanded);
    d7_hamt_map_destroy(&replaced);
    d7_hamt_map_destroy(&collisions);
}

static void test_factory_failures_leave_sources_and_outputs_unchanged(void) {
    factory_test_state state = { 0 };
    state.constant_hash = true;
    d7_hamt_policy policy = factory_test_policy(&state);
    explicit_hash_key keys[] = { { 1, 0u }, { 2, 0u }, { 3, 0u } };
    int values[] = { 10, 20, 30 };
    d7_hamt_map source = d7_hamt_map_create(&policy);
    CHECK_STATUS(d7_hamt_map_set(&source, &keys[0], &values[0], &source));
    CHECK_STATUS(d7_hamt_map_set(&source, &keys[1], &values[1], &source));
    const void *source_root = d7_hamt_map_debug_root_identity(&source);

    state.update_status = D7_HAMT_INVALID_ARGUMENT;
    state.update_candidate = &values[2];
    factory_test_reset_counts(&state);
    d7_hamt_map failed_result;
    memset(&failed_result, 0xa5, sizeof(failed_result));
    d7_hamt_map failed_before = failed_result;
    const void *selected = &values[0];
    CHECK(d7_hamt_map_add_or_update(
        &source,
        &keys[1],
        factory_test_add,
        &state,
        factory_test_update,
        &state,
        &failed_result,
        &selected) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(state.hash_calls == 1 && state.update_calls == 1 && state.add_calls == 0);
    CHECK(memcmp(&failed_result, &failed_before, sizeof(failed_result)) == 0);
    CHECK(selected == &values[0]);
    CHECK(d7_hamt_map_debug_root_identity(&source) == source_root);
    assert_explicit_value(&source, &keys[1], values[1]);

    state.update_status = D7_HAMT_OK;
    state.add_candidate = &values[2];
    bool saw_failure = false;
    bool completed = false;
    for (size_t fail_after = 0; fail_after < 128 && !completed; ++fail_after) {
        memset(&failed_result, 0xa5, sizeof(failed_result));
        failed_before = failed_result;
        selected = &values[0];
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_map_get_or_add(
            &source,
            &keys[2],
            factory_test_add,
            &state,
            &failed_result,
            &selected);
        d7_hamt_test_reset_allocator();
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(memcmp(&failed_result, &failed_before, sizeof(failed_result)) == 0);
            CHECK(selected == &values[0]);
            CHECK(d7_hamt_map_debug_root_identity(&source) == source_root);
            assert_explicit_value(&source, &keys[0], values[0]);
            assert_explicit_value(&source, &keys[1], values[1]);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(*(const int *)selected == values[2]);
            CHECK(d7_hamt_map_count(&failed_result) == 3);
            d7_hamt_map_destroy(&failed_result);
            completed = true;
        }
    }
    CHECK(saw_failure && completed);

    d7_hamt_map_destroy(&source);
}

static void test_remove_and_try_remove_delete_present_keys(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_map next;
    CHECK_STATUS(d7_hamt_map_set(&map, int_key(1), int_value(10), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, int_key(2), int_value(20), &next));
    d7_hamt_map_destroy(&map);
    map = next;

    bool removed = false;
    const void *removed_value = NULL;
    d7_hamt_map without_one;
    CHECK_STATUS(d7_hamt_map_try_remove(&map, int_key(1), &without_one, &removed, &removed_value));
    CHECK(removed);
    CHECK(*(const int *)removed_value == 10);
    CHECK(!d7_hamt_map_contains_key(&without_one, int_key(1)));
    CHECK(d7_hamt_map_contains_key(&map, int_key(1)));

    d7_hamt_map same;
    CHECK_STATUS(d7_hamt_map_try_remove(&without_one, int_key(9), &same, &removed, &removed_value));
    CHECK(!removed);
    CHECK(removed_value == NULL);
    CHECK(d7_hamt_map_shares_root(&without_one, &same));

    d7_hamt_map_destroy(&same);
    d7_hamt_map_destroy(&without_one);
    d7_hamt_map_destroy(&map);
}

static void test_set_many_and_clear_preserve_contracts(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_entry entries[] = {
        { int_key(1), int_value(10) },
        { int_key(2), int_value(20) },
        { int_key(1), int_value(11) }
    };
    d7_hamt_map updated;
    CHECK_STATUS(d7_hamt_map_set_many(&map, entries, 3, &updated));
    CHECK(d7_hamt_map_is_empty(&map));
    CHECK(d7_hamt_map_count(&updated) == 2);
    assert_int_value(&updated, 1, 11);
    assert_int_value(&updated, 2, 20);

    d7_hamt_map cleared;
    CHECK_STATUS(d7_hamt_map_clear(&updated, &cleared));
    CHECK(d7_hamt_map_is_empty(&cleared));
    CHECK(!d7_hamt_map_shares_root(&updated, &cleared));

    d7_hamt_map_destroy(&cleared);
    d7_hamt_map_destroy(&updated);
    d7_hamt_map_destroy(&map);
}

static void test_create_range_last_wins_and_retains_first_equivalent_key(void) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = ci_hash;
    policy.key_equal = ci_equal;
    policy.value_equal = int_equal;

    char stored_alpha[] = "Alpha";
    d7_hamt_entry entries[] = {
        { stored_alpha, int_value(1) },
        { "beta", int_value(2) },
        { "ALPHA", int_value(3) }
    };
    d7_hamt_map map;
    CHECK_STATUS(d7_hamt_map_create_range(&policy, entries, 3, &map));

    CHECK(d7_hamt_map_count(&map) == 2);
    const void *actual = NULL;
    CHECK(d7_hamt_map_try_get(&map, "alpha", &actual));
    CHECK(*(const int *)actual == 3);
    CHECK(d7_hamt_map_contains_key(&map, "BETA"));

    const void *actual_key = NULL;
    CHECK(d7_hamt_map_try_get_key(&map, "ALPHA", &actual_key));
    CHECK(actual_key == stored_alpha);

    d7_hamt_map_destroy(&map);
}

static void test_equal_hash_collision_bucket_preserves_every_key(void) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = collision_key_hash;
    policy.key_equal = collision_key_equal;
    policy.value_equal = int_equal;

    collision_key keys[100];
    bool present[100] = { false };
    int values[100] = { 0 };
    d7_hamt_map map = d7_hamt_map_create(&policy);

    for (int i = 0; i < 100; ++i) {
        keys[i].id = i;
        d7_hamt_map next;
        CHECK_STATUS(d7_hamt_map_set(&map, &keys[i], int_value(i * 10 - 500), &next));
        d7_hamt_map_destroy(&map);
        map = next;
        present[i] = true;
        values[i] = i * 10 - 500;
    }

    for (int i = 0; i < 100; i += 3) {
        d7_hamt_map next;
        CHECK_STATUS(d7_hamt_map_remove(&map, &keys[i], &next));
        d7_hamt_map_destroy(&map);
        map = next;
        present[i] = false;
    }

    for (int i = 1; i < 100; i += 4) {
        d7_hamt_map next;
        CHECK_STATUS(d7_hamt_map_set(&map, &keys[i], int_value(-i), &next));
        d7_hamt_map_destroy(&map);
        map = next;
        present[i] = true;
        values[i] = -i;
    }

    size_t expected_count = 0;
    for (int i = 0; i < 100; ++i) {
        const void *actual = NULL;
        if (present[i]) {
            ++expected_count;
            CHECK(d7_hamt_map_try_get(&map, &keys[i], &actual));
            CHECK(*(const int *)actual == values[i]);
        } else {
            CHECK(!d7_hamt_map_contains_key(&map, &keys[i]));
        }
    }
    CHECK(d7_hamt_map_count(&map) == expected_count);

    size_t enumerated = 0;
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&map, &iterator);
    const void *key = NULL;
    const void *value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        const int id = ((const collision_key *)key)->id;
        CHECK(id >= 0 && id < 100);
        CHECK(present[id]);
        CHECK(*(const int *)value == values[id]);
        ++enumerated;
    }
    CHECK(enumerated == expected_count);

    d7_hamt_map_destroy(&map);
}

static void test_topology_comparator_rejects_different_collision_keys(void) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = collision_key_hash;
    policy.key_equal = collision_key_equal;
    policy.value_equal = int_equal;

    collision_key left_keys[] = { { 1 }, { 2 } };
    collision_key reversed_keys[] = { { 2 }, { 1 } };
    collision_key different_keys[] = { { 1 }, { 3 } };
    d7_hamt_map left = d7_hamt_map_create(&policy);
    d7_hamt_map reversed = d7_hamt_map_create(&policy);
    d7_hamt_map different = d7_hamt_map_create(&policy);
    for (size_t index = 0; index != 2; ++index) {
        CHECK_STATUS(d7_hamt_map_set(
            &left, &left_keys[index], int_value((int)index), &left));
        CHECK_STATUS(d7_hamt_map_set(
            &reversed, &reversed_keys[index], int_value((int)index), &reversed));
        CHECK_STATUS(d7_hamt_map_set(
            &different, &different_keys[index], int_value((int)index), &different));
    }

    CHECK(d7_hamt_map_debug_topology_equal(&left, &reversed));
    CHECK(!d7_hamt_map_debug_topology_equal(&left, &different));

    d7_hamt_map_destroy(&different);
    d7_hamt_map_destroy(&reversed);
    d7_hamt_map_destroy(&left);
}

static void test_deep_shared_hash_prefixes_lookup_and_remove_correctly(void) {
    d7_hamt_policy policy = explicit_map_policy();
    explicit_hash_key a = { 1, 0u };
    explicit_hash_key b = { 2, 1u << 30 };
    explicit_hash_key c = { 3, 0x80000000u };
    explicit_hash_key d = { 4, 0xC0000000u };

    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_map next;
    CHECK_STATUS(d7_hamt_map_set(&map, &a, int_value(1), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, &b, int_value(2), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, &c, int_value(3), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, &d, int_value(4), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK(d7_hamt_map_debug_validate_canonical(&map));

    const void *actual = NULL;
    CHECK(d7_hamt_map_try_get(&map, &a, &actual) && *(const int *)actual == 1);
    CHECK(d7_hamt_map_try_get(&map, &b, &actual) && *(const int *)actual == 2);
    CHECK(d7_hamt_map_try_get(&map, &c, &actual) && *(const int *)actual == 3);
    CHECK(d7_hamt_map_try_get(&map, &d, &actual) && *(const int *)actual == 4);

    d7_hamt_map reduced;
    CHECK_STATUS(d7_hamt_map_remove(&map, &b, &reduced));
    CHECK_STATUS(d7_hamt_map_remove(&reduced, &c, &next));
    d7_hamt_map_destroy(&reduced);
    reduced = next;
    CHECK_STATUS(d7_hamt_map_remove(&reduced, &d, &next));
    d7_hamt_map_destroy(&reduced);
    reduced = next;

    CHECK(d7_hamt_map_count(&reduced) == 1);
    CHECK(d7_hamt_map_debug_validate_canonical(&reduced));
    CHECK(d7_hamt_map_debug_root_kind(&reduced) == D7_HAMT_NODE_LEAF);
    CHECK(d7_hamt_map_try_get(&reduced, &a, &actual) && *(const int *)actual == 1);
    CHECK(!d7_hamt_map_contains_key(&reduced, &b));
    CHECK(d7_hamt_map_try_get(&map, &b, &actual) && *(const int *)actual == 2);

    d7_hamt_map_destroy(&reduced);
    d7_hamt_map_destroy(&map);
}

static void test_depth_seven_iterator_traversal(void) {
    d7_hamt_policy policy = explicit_map_policy();
    explicit_hash_key first = { 1, 0u };
    explicit_hash_key second = { 2, 1u << 30 };

    d7_hamt_map empty = d7_hamt_map_create(&policy);
    d7_hamt_map one;
    CHECK_STATUS(d7_hamt_map_set(&empty, &first, int_value(1), &one));
    d7_hamt_map map;
    CHECK_STATUS(d7_hamt_map_set(&one, &second, int_value(2), &map));

    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&map, &iterator);
    const void *key = NULL;
    const void *value = NULL;
    CHECK(d7_hamt_map_iterator_next(&iterator, &key, &value));
    CHECK(iterator.depth == 7);
    CHECK(((const explicit_hash_key *)key)->id == 1);
    CHECK(*(const int *)value == 1);
    CHECK(d7_hamt_map_iterator_next(&iterator, &key, &value));
    CHECK(((const explicit_hash_key *)key)->id == 2);
    CHECK(*(const int *)value == 2);
    CHECK(!d7_hamt_map_iterator_next(&iterator, &key, &value));

    d7_hamt_map_destroy(&map);
    d7_hamt_map_destroy(&one);
    d7_hamt_map_destroy(&empty);
}

static void test_allocation_failures_unwind_node_set_and_merge(void) {
    d7_hamt_policy policy = explicit_map_policy();
    explicit_hash_key deep_left = { 1, 0u };
    explicit_hash_key deep_right = { 2, 1u << 30 };

    d7_hamt_map empty = d7_hamt_map_create(&policy);
    d7_hamt_map deep_base;
    CHECK_STATUS(d7_hamt_map_set(&empty, &deep_left, int_value(1), &deep_base));

    bool merge_succeeded = false;
    size_t merge_failure_count = 0;
    for (size_t fail_after = 0; fail_after != 32; ++fail_after) {
        d7_hamt_map result = { 0 };
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status =
            d7_hamt_map_set(&deep_base, &deep_right, int_value(2), &result);
        d7_hamt_test_reset_allocator();

        const void *actual = NULL;
        CHECK(d7_hamt_map_count(&deep_base) == 1);
        CHECK(d7_hamt_map_try_get(&deep_base, &deep_left, &actual));
        CHECK(*(const int *)actual == 1);
        CHECK(!d7_hamt_map_contains_key(&deep_base, &deep_right));
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            CHECK(result.root == NULL);
            ++merge_failure_count;
            continue;
        }

        CHECK(status == D7_HAMT_OK);
        CHECK(d7_hamt_map_count(&result) == 2);
        CHECK(d7_hamt_map_try_get(&result, &deep_right, &actual));
        CHECK(*(const int *)actual == 2);
        d7_hamt_map_destroy(&result);
        merge_succeeded = true;
        break;
    }

    CHECK(merge_succeeded);
    CHECK(merge_failure_count >= 8);

    explicit_hash_key branch_first = { 3, 0u };
    explicit_hash_key branch_second = { 4, 1u };
    explicit_hash_key branch_added = { 5, 2u };
    d7_hamt_map branch_one;
    CHECK_STATUS(d7_hamt_map_set(&empty, &branch_first, int_value(3), &branch_one));
    d7_hamt_map branch_base;
    CHECK_STATUS(d7_hamt_map_set(&branch_one, &branch_second, int_value(4), &branch_base));

    bool node_set_succeeded = false;
    size_t node_set_failure_count = 0;
    for (size_t fail_after = 0; fail_after != 16; ++fail_after) {
        d7_hamt_map result = { 0 };
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status =
            d7_hamt_map_set(&branch_base, &branch_added, int_value(5), &result);
        d7_hamt_test_reset_allocator();

        const void *actual = NULL;
        CHECK(d7_hamt_map_count(&branch_base) == 2);
        CHECK(d7_hamt_map_try_get(&branch_base, &branch_first, &actual));
        CHECK(*(const int *)actual == 3);
        CHECK(d7_hamt_map_try_get(&branch_base, &branch_second, &actual));
        CHECK(*(const int *)actual == 4);
        CHECK(!d7_hamt_map_contains_key(&branch_base, &branch_added));
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            CHECK(result.root == NULL);
            ++node_set_failure_count;
            continue;
        }

        CHECK(status == D7_HAMT_OK);
        CHECK(d7_hamt_map_count(&result) == 3);
        CHECK(d7_hamt_map_try_get(&result, &branch_added, &actual));
        CHECK(*(const int *)actual == 5);
        d7_hamt_map_destroy(&result);
        node_set_succeeded = true;
        break;
    }

    CHECK(node_set_succeeded);
    /* CHAMP inserts an unused bitmap position directly into the inline
     * payload run, so only the replacement node allocation is mandatory. */
    CHECK(node_set_failure_count >= 1);

    d7_hamt_map_destroy(&branch_base);
    d7_hamt_map_destroy(&branch_one);
    d7_hamt_map_destroy(&deep_base);
    d7_hamt_map_destroy(&empty);
}

static void test_collision_bucket_splits_and_hash_mismatch_probes_miss(void) {
    d7_hamt_policy policy = explicit_map_policy();
    explicit_hash_key a = { 1, 0x10u };
    explicit_hash_key b = { 2, 0x10u };
    explicit_hash_key probe = { 9, 0x410u };

    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_map next;
    CHECK_STATUS(d7_hamt_map_set(&map, &a, int_value(1), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, &b, int_value(2), &next));
    d7_hamt_map_destroy(&map);
    map = next;

    CHECK(d7_hamt_map_debug_root_kind(&map) == D7_HAMT_NODE_COLLISION);
    CHECK(!d7_hamt_map_contains_key(&map, &probe));
    CHECK_STATUS(d7_hamt_map_remove(&map, &probe, &next));
    CHECK(d7_hamt_map_shares_root(&map, &next));
    d7_hamt_map_destroy(&next);

    d7_hamt_map expanded;
    CHECK_STATUS(d7_hamt_map_set(&map, &probe, int_value(9), &expanded));
    CHECK(d7_hamt_map_count(&expanded) == 3);
    CHECK(d7_hamt_map_debug_root_kind(&expanded) == D7_HAMT_NODE_BITMAP_INDEXED);
    const void *actual = NULL;
    CHECK(d7_hamt_map_try_get(&expanded, &probe, &actual) && *(const int *)actual == 9);
    CHECK(d7_hamt_map_try_get(&expanded, &a, &actual) && *(const int *)actual == 1);
    CHECK(d7_hamt_map_try_get(&expanded, &b, &actual) && *(const int *)actual == 2);

    d7_hamt_map_destroy(&expanded);
    d7_hamt_map_destroy(&map);
}

static void test_collision_bucket_equal_value_keeps_root_and_key_object(void) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = constant_string_hash;
    policy.key_equal = ci_equal;
    policy.value_equal = int_equal;

    char stored_alpha[] = "Alpha";
    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_map next;
    CHECK_STATUS(d7_hamt_map_set(&map, stored_alpha, int_value(1), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, "beta", int_value(2), &next));
    d7_hamt_map_destroy(&map);
    map = next;

    d7_hamt_map no_op;
    CHECK_STATUS(d7_hamt_map_set(&map, "ALPHA", int_value(1), &no_op));
    CHECK(d7_hamt_map_shares_root(&map, &no_op));
    d7_hamt_map_destroy(&no_op);

    d7_hamt_map replaced;
    CHECK_STATUS(d7_hamt_map_set(&map, "ALPHA", int_value(3), &replaced));
    const void *actual = NULL;
    CHECK(d7_hamt_map_try_get(&replaced, "alpha", &actual) && *(const int *)actual == 3);
    const void *actual_key = NULL;
    CHECK(d7_hamt_map_try_get_key(&replaced, "alpha", &actual_key));
    CHECK(actual_key == stored_alpha);

    d7_hamt_map_destroy(&replaced);
    d7_hamt_map_destroy(&map);
}

static void test_structure_root_shape_and_sharing(void) {
    d7_hamt_policy policy = explicit_map_policy();
    explicit_hash_key a = { 1, 0x00u };
    explicit_hash_key b = { 2, 0x01u };
    explicit_hash_key c = { 3, 0x21u };

    d7_hamt_map empty = d7_hamt_map_create(&policy);
    CHECK(d7_hamt_map_debug_root_kind(&empty) == D7_HAMT_NODE_EMPTY);

    d7_hamt_map single;
    CHECK_STATUS(d7_hamt_map_set(&empty, &a, int_value(1), &single));
    CHECK(d7_hamt_map_debug_root_kind(&single) == D7_HAMT_NODE_LEAF);

    d7_hamt_map map;
    CHECK_STATUS(d7_hamt_map_set(&single, &b, int_value(2), &map));
    d7_hamt_map next;
    CHECK_STATUS(d7_hamt_map_set(&map, &c, int_value(3), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK(d7_hamt_map_debug_root_kind(&map) == D7_HAMT_NODE_BITMAP_INDEXED);

    d7_hamt_map updated;
    CHECK_STATUS(d7_hamt_map_set(&map, &a, int_value(10), &updated));
    const void *before_children[4] = { NULL };
    const void *after_children[4] = { NULL };
    CHECK(d7_hamt_map_debug_root_child_identities(&map, before_children, 4) == 1);
    CHECK(d7_hamt_map_debug_root_child_identities(&updated, after_children, 4) == 1);
    CHECK(before_children[0] == after_children[0]);

    d7_hamt_map no_op;
    CHECK_STATUS(d7_hamt_map_set(&map, &a, int_value(1), &no_op));
    CHECK(d7_hamt_map_shares_root(&map, &no_op));
    d7_hamt_map_destroy(&no_op);

    d7_hamt_map_destroy(&updated);
    d7_hamt_map_destroy(&map);
    d7_hamt_map_destroy(&single);
    d7_hamt_map_destroy(&empty);
}

typedef struct diff_counts {
    size_t added;
    size_t removed;
    size_t changed;
} diff_counts;

static void count_difference(const d7_hamt_difference *difference, void *context) {
    diff_counts *counts = (diff_counts *)context;
    if (difference->kind == D7_HAMT_DIFFERENCE_ADDED) {
        ++counts->added;
    } else if (difference->kind == D7_HAMT_DIFFERENCE_REMOVED) {
        ++counts->removed;
    } else {
        ++counts->changed;
    }
}

typedef struct captured_differences {
    d7_hamt_difference items[8];
    size_t count;
} captured_differences;

static void capture_difference(const d7_hamt_difference *difference, void *context) {
    captured_differences *captured = (captured_differences *)context;
    CHECK(captured->count < sizeof(captured->items) / sizeof(captured->items[0]));
    captured->items[captured->count++] = *difference;
}

typedef struct champ_pruning_counts {
    size_t hash_calls;
    size_t key_equal_calls;
    size_t value_equal_calls;
} champ_pruning_counts;

static uint32_t champ_pruning_hash(const void *item, void *context) {
    champ_pruning_counts *counts = (champ_pruning_counts *)context;
    ++counts->hash_calls;
    return int_hash(item, NULL);
}

static bool champ_pruning_key_equal(const void *left, const void *right, void *context) {
    champ_pruning_counts *counts = (champ_pruning_counts *)context;
    ++counts->key_equal_calls;
    return *(const int *)left == *(const int *)right;
}

static bool champ_pruning_value_equal(const void *left, const void *right, void *context) {
    champ_pruning_counts *counts = (champ_pruning_counts *)context;
    ++counts->value_equal_calls;
    return *(const int *)left == *(const int *)right;
}

static void test_champ_independent_histories_and_typed_diff(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map ascending = d7_hamt_map_create(&policy);
    d7_hamt_map descending = d7_hamt_map_create(&policy);
    for (int key = -100; key <= 100; ++key) {
        CHECK_STATUS(d7_hamt_map_set(&ascending, int_key(key), int_value(key), &ascending));
        CHECK_STATUS(d7_hamt_map_set(&descending, int_key(-key), int_value(-key), &descending));
    }
    CHECK(d7_hamt_map_equals(&ascending, &descending));
    CHECK(d7_hamt_map_debug_validate_canonical(&ascending));
    CHECK(d7_hamt_map_debug_validate_canonical(&descending));
    CHECK(d7_hamt_map_debug_topology_equal(&ascending, &descending));
    diff_counts empty_counts = { 0 };
    CHECK_STATUS(d7_hamt_map_diff(&ascending, &descending, count_difference, &empty_counts));
    CHECK(empty_counts.added == 0 && empty_counts.removed == 0 && empty_counts.changed == 0);

    d7_hamt_map changed = d7_hamt_map_clone(&descending);
    CHECK_STATUS(d7_hamt_map_remove(&changed, int_key(7), &changed));
    CHECK_STATUS(d7_hamt_map_set(&changed, int_key(9), int_value(-9), &changed));
    CHECK_STATUS(d7_hamt_map_set(&changed, int_key(101), int_value(101), &changed));
    diff_counts counts = { 0 };
    CHECK_STATUS(d7_hamt_map_diff(&ascending, &changed, count_difference, &counts));
    CHECK(counts.added == 1 && counts.removed == 1 && counts.changed == 1);

    d7_hamt_map churned = d7_hamt_map_clone(&ascending);
    for (int key = -99; key <= 99; key += 3) {
        CHECK_STATUS(d7_hamt_map_remove(&churned, int_key(key), &churned));
    }
    for (int key = 99; key >= -99; key -= 3) {
        CHECK_STATUS(d7_hamt_map_set(&churned, int_key(key), int_value(key), &churned));
    }
    CHECK(d7_hamt_map_debug_validate_canonical(&churned));
    CHECK(d7_hamt_map_debug_topology_equal(&ascending, &churned));
    d7_hamt_map_destroy(&churned);
    d7_hamt_map_destroy(&changed);
    d7_hamt_map_destroy(&descending);
    d7_hamt_map_destroy(&ascending);
}

static void test_champ_collision_runs_compare_and_diff_semantically(void) {
    d7_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    d7_hamt_map ascending = d7_hamt_map_create(&policy);
    d7_hamt_map descending = d7_hamt_map_create(&policy);
    for (int key = 0; key != 8; ++key) {
        CHECK_STATUS(d7_hamt_map_set(&ascending, int_key(key), int_value(key * 10), &ascending));
        CHECK_STATUS(d7_hamt_map_set(
            &descending, int_key(7 - key), int_value((7 - key) * 10), &descending));
    }
    CHECK(d7_hamt_map_equals(&ascending, &descending));
    diff_counts equal_counts = { 0 };
    CHECK_STATUS(d7_hamt_map_diff(&ascending, &descending, count_difference, &equal_counts));
    CHECK(equal_counts.added == 0 && equal_counts.removed == 0 && equal_counts.changed == 0);

    d7_hamt_map changed = d7_hamt_map_clone(&descending);
    CHECK_STATUS(d7_hamt_map_remove(&changed, int_key(1), &changed));
    CHECK_STATUS(d7_hamt_map_set(&changed, int_key(2), int_value(-20), &changed));
    CHECK_STATUS(d7_hamt_map_set(&changed, int_key(8), int_value(80), &changed));
    captured_differences captured = { 0 };
    CHECK_STATUS(d7_hamt_map_diff(&ascending, &changed, capture_difference, &captured));
    CHECK(captured.count == 3);
    bool saw_removed = false;
    bool saw_changed = false;
    bool saw_added = false;
    for (size_t index = 0; index != captured.count; ++index) {
        const d7_hamt_difference *item = &captured.items[index];
        const int key = *(const int *)item->key;
        saw_removed = saw_removed
            || (item->kind == D7_HAMT_DIFFERENCE_REMOVED && key == 1
                && *(const int *)item->before == 10 && item->after == NULL);
        saw_changed = saw_changed
            || (item->kind == D7_HAMT_DIFFERENCE_CHANGED && key == 2
                && *(const int *)item->before == 20 && *(const int *)item->after == -20);
        saw_added = saw_added
            || (item->kind == D7_HAMT_DIFFERENCE_ADDED && key == 8
                && item->before == NULL && *(const int *)item->after == 80);
    }
    CHECK(saw_removed && saw_changed && saw_added);

    d7_hamt_map_destroy(&changed);
    d7_hamt_map_destroy(&descending);
    d7_hamt_map_destroy(&ascending);
}

static void test_champ_equality_and_diff_prune_shared_descendants(void) {
    champ_pruning_counts callback_counts = { 0 };
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = champ_pruning_hash;
    policy.key_equal = champ_pruning_key_equal;
    policy.value_equal = champ_pruning_value_equal;
    policy.context = &callback_counts;

    int stored_values[100];
    d7_hamt_map basis = d7_hamt_map_create(&policy);
    for (int key = 0; key != 100; ++key) {
        stored_values[key] = key;
        CHECK_STATUS(d7_hamt_map_set(&basis, int_key(key), &stored_values[key], &basis));
    }

    int changed_value = -42;
    int restored_value = 42;
    d7_hamt_map changed;
    d7_hamt_map restored;
    CHECK_STATUS(d7_hamt_map_set(&basis, int_key(42), &changed_value, &changed));
    CHECK_STATUS(d7_hamt_map_set(&changed, int_key(42), &restored_value, &restored));
    CHECK(!d7_hamt_map_shares_root(&basis, &restored));

    callback_counts = (champ_pruning_counts){ 0 };
    CHECK(d7_hamt_map_equals(&basis, &restored));
    CHECK(callback_counts.hash_calls == 0);
    CHECK(callback_counts.key_equal_calls > 0 && callback_counts.key_equal_calls < 100);
    CHECK(callback_counts.value_equal_calls == 1);

    callback_counts = (champ_pruning_counts){ 0 };
    diff_counts empty = { 0 };
    CHECK_STATUS(d7_hamt_map_diff(&basis, &restored, count_difference, &empty));
    CHECK(empty.added == 0 && empty.removed == 0 && empty.changed == 0);
    CHECK(callback_counts.hash_calls == 0);
    CHECK(callback_counts.key_equal_calls > 0 && callback_counts.key_equal_calls < 100);
    CHECK(callback_counts.value_equal_calls == 1);

    callback_counts = (champ_pruning_counts){ 0 };
    diff_counts changed_counts = { 0 };
    CHECK_STATUS(d7_hamt_map_diff(&basis, &changed, count_difference, &changed_counts));
    CHECK(changed_counts.added == 0 && changed_counts.removed == 0 && changed_counts.changed == 1);
    CHECK(callback_counts.hash_calls == 0);
    CHECK(callback_counts.key_equal_calls > 0 && callback_counts.key_equal_calls < 100);
    CHECK(callback_counts.value_equal_calls == 1);

    d7_hamt_map_destroy(&restored);
    d7_hamt_map_destroy(&changed);
    d7_hamt_map_destroy(&basis);
}

static void test_iterator_copy_advances_independently(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_map next;
    CHECK_STATUS(d7_hamt_map_set(&map, int_key(0), int_value(0), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, int_key(1), int_value(1), &next));
    d7_hamt_map_destroy(&map);
    map = next;
    CHECK_STATUS(d7_hamt_map_set(&map, int_key(33), int_value(33), &next));
    d7_hamt_map_destroy(&map);
    map = next;

    d7_hamt_map_iterator left;
    d7_hamt_map_iterator_init(&map, &left);
    d7_hamt_map_iterator right = left;

    size_t left_count = 0;
    size_t right_count = 0;
    const void *key = NULL;
    const void *value = NULL;
    while (d7_hamt_map_iterator_next(&left, &key, &value)) {
        ++left_count;
    }
    while (d7_hamt_map_iterator_next(&right, &key, &value)) {
        ++right_count;
    }

    CHECK(left_count == 3);
    CHECK(right_count == 3);
    d7_hamt_map_destroy(&map);
}

typedef struct map_snapshot {
    bool active;
    d7_hamt_map map;
    bool present[81];
    int values[81];
} map_snapshot;

typedef struct explicit_map_snapshot {
    bool active;
    d7_hamt_map map;
    bool present[96];
    int values[96];
} explicit_map_snapshot;

static void assert_explicit_model_matches(
    const explicit_hash_key keys[96],
    const bool present[96],
    const int values[96],
    const d7_hamt_map *map) {
    size_t expected_count = 0;
    for (int id = 0; id != 96; ++id) {
        const void *actual = NULL;
        if (present[id]) {
            ++expected_count;
            CHECK(d7_hamt_map_try_get(map, &keys[id], &actual));
            CHECK(actual != NULL);
            CHECK(*(const int *)actual == values[id]);
        } else {
            CHECK(!d7_hamt_map_contains_key(map, &keys[id]));
        }
    }

    CHECK(d7_hamt_map_count(map) == expected_count);

    size_t enumerated = 0;
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(map, &iterator);
    const void *key = NULL;
    const void *value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
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
    const d7_hamt_map *map,
    const bool present[96],
    const int values[96]) {
    if (snapshot->active) {
        d7_hamt_map_destroy(&snapshot->map);
    }

    snapshot->active = true;
    snapshot->map = d7_hamt_map_clone(map);
    memcpy(snapshot->present, present, 96 * sizeof(bool));
    memcpy(snapshot->values, values, 96 * sizeof(int));
}

static void test_random_history_matches_model_and_preserves_snapshots(void) {
    d7_hamt_policy policy = int_map_policy(int_hash);
    uint32_t rng = 0xC0FFEEu;

    for (int iteration = 0; iteration < 200; ++iteration) {
        d7_hamt_map map = d7_hamt_map_create(&policy);
        bool present[81] = { false };
        int values[81] = { 0 };
        map_snapshot snapshots[5];
        memset(snapshots, 0, sizeof(snapshots));

        for (int step = 0; step < 160; ++step) {
            const int op = rng_range(&rng, 0, 4);
            const int key = rng_range(&rng, -40, 40);
            const int value = rng_range(&rng, -1000, 1000);
            d7_hamt_map next;

            switch (op) {
            case 0:
                CHECK_STATUS(d7_hamt_map_set(&map, int_key(key), int_value(value), &next));
                d7_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = true;
                values[key + 40] = value;
                break;

            case 1:
                CHECK_STATUS(d7_hamt_map_remove(&map, int_key(key), &next));
                d7_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = false;
                break;

            case 2: {
                bool added = false;
                CHECK_STATUS(d7_hamt_map_try_add(&map, int_key(key), int_value(value), &next, &added));
                CHECK(added == !present[key + 40]);
                d7_hamt_map_destroy(&map);
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
                CHECK_STATUS(d7_hamt_map_try_remove(&map, int_key(key), &next, &removed, &removed_value));
                CHECK(removed == expected_removed);
                if (removed) {
                    CHECK(*(const int *)removed_value == expected_value);
                    present[key + 40] = false;
                }
                d7_hamt_map_destroy(&map);
                map = next;
                break;
            }

            default: {
                const size_t slot = (size_t)step % 5u;
                if (snapshots[slot].active) {
                    d7_hamt_map_destroy(&snapshots[slot].map);
                }
                snapshots[slot].active = true;
                snapshots[slot].map = d7_hamt_map_clone(&map);
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
                d7_hamt_map_destroy(&snapshots[i].map);
            }
        }
        d7_hamt_map_destroy(&map);
    }
}

static void test_scripted_collision_snapshot_story(void) {
    d7_hamt_policy policy = explicit_map_policy();
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

    d7_hamt_map map = d7_hamt_map_create(&policy);

    for (int step = 0; step != 96; ++step) {
        const int id = (step * 37) % 96;
        const int value = id - 500;
        d7_hamt_map next;
        CHECK_STATUS(d7_hamt_map_set(&map, &keys[id], int_value(value), &next));
        d7_hamt_map_destroy(&map);
        map = next;
        present[id] = true;
        values[id] = value;

        if (step == 23 || step == 47 || step == 71) {
            capture_explicit_snapshot(&snapshots[(size_t)step / 24u], &map, present, values);
        }
    }

    assert_explicit_model_matches(keys, present, values, &map);

    for (int id = 5; id < 96; id += 11) {
        d7_hamt_map same;
        CHECK_STATUS(d7_hamt_map_set(&map, &keys[id], int_value(values[id]), &same));
        CHECK(d7_hamt_map_shares_root(&map, &same));
        d7_hamt_map_destroy(&same);
    }

    for (int id = 2; id < 96; id += 5) {
        const int value = 400 - id;
        d7_hamt_map next;
        CHECK_STATUS(d7_hamt_map_set(&map, &keys[id], int_value(value), &next));
        d7_hamt_map_destroy(&map);
        map = next;
        values[id] = value;
    }

    capture_explicit_snapshot(&snapshots[3], &map, present, values);

    for (int id = 0; id < 96; ++id) {
        if (id % 7 == 0 || id % 13 == 0) {
            d7_hamt_map next;
            CHECK_STATUS(d7_hamt_map_remove(&map, &keys[id], &next));
            d7_hamt_map_destroy(&map);
            map = next;
            present[id] = false;
        }
    }

    for (int id = 0; id < 96; id += 9) {
        bool added = true;
        d7_hamt_map next;
        const int value = 700 - id;
        CHECK_STATUS(d7_hamt_map_try_add(&map, &keys[id], int_value(value), &next, &added));
        CHECK(added == !present[id]);
        d7_hamt_map_destroy(&map);
        map = next;
        if (added) {
            present[id] = true;
            values[id] = value;
        }
    }

    for (int id = 1; id < 96; id += 10) {
        if (present[id]) {
            bool added = true;
            d7_hamt_map same;
            CHECK_STATUS(d7_hamt_map_try_add(&map, &keys[id], int_value(-900), &same, &added));
            CHECK(!added);
            CHECK(d7_hamt_map_shares_root(&map, &same));
            d7_hamt_map_destroy(&same);
        }
    }

    assert_explicit_model_matches(keys, present, values, &map);
    for (size_t slot = 0; slot != 4; ++slot) {
        CHECK(snapshots[slot].active);
        assert_explicit_model_matches(keys, snapshots[slot].present, snapshots[slot].values, &snapshots[slot].map);
    }

    d7_hamt_map cleared;
    CHECK_STATUS(d7_hamt_map_clear(&map, &cleared));
    CHECK(d7_hamt_map_is_empty(&cleared));
    CHECK(!d7_hamt_map_is_empty(&map));
    d7_hamt_map_destroy(&cleared);

    for (size_t slot = 0; slot != 4; ++slot) {
        d7_hamt_map_destroy(&snapshots[slot].map);
    }
    d7_hamt_map_destroy(&map);
}

static void test_random_history_with_colliding_hashes_matches_model(void) {
    d7_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    uint32_t rng = 0xBAD5EEDu;

    for (int iteration = 0; iteration < 200; ++iteration) {
        d7_hamt_map map = d7_hamt_map_create(&policy);
        bool present[81] = { false };
        int values[81] = { 0 };

        for (int step = 0; step < 160; ++step) {
            const int op = rng_range(&rng, 0, 4);
            const int key = rng_range(&rng, -40, 40);
            const int value = rng_range(&rng, -1000, 1000);
            d7_hamt_map next;

            if (op == 0 || op == 4) {
                CHECK_STATUS(d7_hamt_map_set(&map, int_key(key), int_value(value), &next));
                d7_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = true;
                values[key + 40] = value;
            } else if (op == 1) {
                CHECK_STATUS(d7_hamt_map_remove(&map, int_key(key), &next));
                d7_hamt_map_destroy(&map);
                map = next;
                present[key + 40] = false;
            } else if (op == 2) {
                bool added = false;
                CHECK_STATUS(d7_hamt_map_try_add(&map, int_key(key), int_value(value), &next, &added));
                CHECK(added == !present[key + 40]);
                d7_hamt_map_destroy(&map);
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
                CHECK_STATUS(d7_hamt_map_try_remove(&map, int_key(key), &next, &removed, &removed_value));
                CHECK(removed == expected_removed);
                if (removed) {
                    CHECK(*(const int *)removed_value == expected_value);
                    present[key + 40] = false;
                }
                d7_hamt_map_destroy(&map);
                map = next;
            }
        }

        assert_int_model_matches(present, values, &map);
        d7_hamt_map_destroy(&map);
    }
}

static void test_set_add_remove_contains_and_persistence(void) {
    d7_hamt_set_policy policy = int_set_policy(int_hash);
    d7_hamt_set empty = d7_hamt_set_create(&policy);
    d7_hamt_set one;
    d7_hamt_set two;
    d7_hamt_set removed;
    CHECK_STATUS(d7_hamt_set_add(&empty, int_key(1), &one));
    CHECK_STATUS(d7_hamt_set_add(&one, int_key(2), &two));
    CHECK_STATUS(d7_hamt_set_remove(&two, int_key(1), &removed));

    CHECK(d7_hamt_set_is_empty(&empty));
    CHECK(d7_hamt_set_contains(&one, int_key(1)));
    CHECK(!d7_hamt_set_contains(&one, int_key(2)));
    CHECK(d7_hamt_set_contains(&two, int_key(1)));
    CHECK(d7_hamt_set_contains(&two, int_key(2)));
    CHECK(!d7_hamt_set_contains(&removed, int_key(1)));
    CHECK(d7_hamt_set_contains(&removed, int_key(2)));

    d7_hamt_set no_op;
    CHECK_STATUS(d7_hamt_set_add(&one, int_key(1), &no_op));
    CHECK(d7_hamt_set_shares_root(&one, &no_op));
    d7_hamt_set_destroy(&no_op);

    CHECK_STATUS(d7_hamt_set_remove(&one, int_key(9), &no_op));
    CHECK(d7_hamt_set_shares_root(&one, &no_op));
    d7_hamt_set_destroy(&no_op);

    bool added = true;
    CHECK_STATUS(d7_hamt_set_try_add(&one, int_key(1), &no_op, &added));
    CHECK(!added);
    CHECK(d7_hamt_set_shares_root(&one, &no_op));
    d7_hamt_set_destroy(&no_op);

    d7_hamt_set with_three;
    CHECK_STATUS(d7_hamt_set_try_add(&one, int_key(3), &with_three, &added));
    CHECK(added);
    CHECK(d7_hamt_set_contains(&with_three, int_key(3)));

    bool removed_flag = false;
    d7_hamt_set without_three;
    CHECK_STATUS(d7_hamt_set_try_remove(&with_three, int_key(3), &without_three, &removed_flag));
    CHECK(removed_flag);
    CHECK(!d7_hamt_set_contains(&without_three, int_key(3)));

    d7_hamt_set_destroy(&without_three);
    d7_hamt_set_destroy(&with_three);

    d7_hamt_set cleared;
    CHECK_STATUS(d7_hamt_set_clear(&two, &cleared));
    CHECK(d7_hamt_set_is_empty(&cleared));
    d7_hamt_set_destroy(&cleared);

    d7_hamt_set_destroy(&removed);
    d7_hamt_set_destroy(&two);
    d7_hamt_set_destroy(&one);
    d7_hamt_set_destroy(&empty);
}

static void test_set_custom_comparer_retains_first_item(void) {
    d7_hamt_set_policy policy = d7_hamt_set_policy_default();
    policy.hash = ci_hash;
    policy.equal = ci_equal;

    char stored_alpha[] = "Alpha";
    const void *items[] = { stored_alpha, "ALPHA", "beta" };
    d7_hamt_set set;
    CHECK_STATUS(d7_hamt_set_create_range(&policy, items, 3, &set));

    CHECK(d7_hamt_set_count(&set) == 2);
    CHECK(d7_hamt_set_contains(&set, "alpha"));
    const void *actual = NULL;
    CHECK(d7_hamt_set_try_get_value(&set, "ALPHA", &actual));
    CHECK(actual == stored_alpha);

    d7_hamt_set_destroy(&set);
}

static void test_champ_map_algebra_preserves_representatives_and_bias(void) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = ci_hash;
    policy.key_equal = ci_equal;
    policy.value_equal = int_equal;
    char alpha[] = "Alpha";
    const d7_hamt_entry left_entries[] = {
        { alpha, int_value(1) },
        { "left", int_value(10) }
    };
    const d7_hamt_entry right_entries[] = {
        { "ALPHA", int_value(2) },
        { "right", int_value(20) }
    };
    d7_hamt_map left;
    d7_hamt_map right;
    CHECK_STATUS(d7_hamt_map_create_range(&policy, left_entries, 2, &left));
    CHECK_STATUS(d7_hamt_map_create_range(&policy, right_entries, 2, &right));

    d7_hamt_map actual;
    CHECK_STATUS(d7_hamt_map_union(&left, &right, &actual));
    CHECK(d7_hamt_map_count(&actual) == 3);
    const void *stored_key = NULL;
    CHECK(d7_hamt_map_try_get_key(&actual, "alpha", &stored_key));
    CHECK(stored_key == alpha);
    const void *stored_value = NULL;
    CHECK(d7_hamt_map_try_get(&actual, "alpha", &stored_value));
    CHECK(*(const int *)stored_value == 2);
    d7_hamt_map_destroy(&actual);

    CHECK_STATUS(d7_hamt_map_intersect(&left, &right, &actual));
    CHECK(d7_hamt_map_count(&actual) == 1);
    d7_hamt_map_destroy(&actual);
    CHECK_STATUS(d7_hamt_map_except(&left, &right, &actual));
    CHECK(d7_hamt_map_count(&actual) == 1);
    d7_hamt_map_destroy(&actual);
    CHECK_STATUS(d7_hamt_map_symmetric_except(&left, &right, &actual));
    CHECK(d7_hamt_map_count(&actual) == 2);
    d7_hamt_map_destroy(&actual);
    CHECK_STATUS(d7_hamt_map_union(&left, &left, &actual));
    CHECK(d7_hamt_map_shares_root(&left, &actual));
    d7_hamt_map_destroy(&actual);

    d7_hamt_map_destroy(&right);
    d7_hamt_map_destroy(&left);
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
    d7_hamt_set_policy policy = int_set_policy(int_hash);
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

        d7_hamt_set set;
        CHECK_STATUS(d7_hamt_set_create_range(&policy, left_items, left_count, &set));
        d7_hamt_set right_set;
        CHECK_STATUS(d7_hamt_set_create_range(
            &policy, right_items, right_count, &right_set));

        bool expected[61];
        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] || right_model[i];
        }
        d7_hamt_set actual;
        CHECK_STATUS(d7_hamt_set_union_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);
        CHECK_STATUS(d7_hamt_set_union(&set, &right_set, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);

        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] && right_model[i];
        }
        CHECK_STATUS(d7_hamt_set_intersect_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);
        CHECK_STATUS(d7_hamt_set_intersect(&set, &right_set, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);

        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] && !right_model[i];
        }
        CHECK_STATUS(d7_hamt_set_except_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);
        CHECK_STATUS(d7_hamt_set_except(&set, &right_set, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);

        for (size_t i = 0; i < 61; ++i) {
            expected[i] = left_model[i] != right_model[i];
        }
        CHECK_STATUS(d7_hamt_set_symmetric_except_many(&set, right_items, right_count, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);
        CHECK_STATUS(d7_hamt_set_symmetric_except(&set, &right_set, &actual));
        assert_set_model_matches(expected, &actual);
        d7_hamt_set_destroy(&actual);

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
        CHECK_STATUS(d7_hamt_set_is_subset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == subset);
        CHECK_STATUS(d7_hamt_set_is_proper_subset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == (subset && left_distinct < right_distinct));
        CHECK_STATUS(d7_hamt_set_is_superset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == superset);
        CHECK_STATUS(d7_hamt_set_is_proper_superset_of_many(&set, right_items, right_count, &relation));
        CHECK(relation == (superset && left_distinct > right_distinct));
        CHECK_STATUS(d7_hamt_set_overlaps_many(&set, right_items, right_count, &relation));
        CHECK(relation == overlaps);
        CHECK_STATUS(d7_hamt_set_equals_many(&set, right_items, right_count, &relation));
        CHECK(relation == (left_distinct == right_distinct && subset));
        CHECK_STATUS(d7_hamt_set_is_subset_of(&set, &right_set, &relation));
        CHECK(relation == subset);
        CHECK_STATUS(d7_hamt_set_is_proper_subset_of(&set, &right_set, &relation));
        CHECK(relation == (subset && left_distinct < right_distinct));
        CHECK_STATUS(d7_hamt_set_is_superset_of(&set, &right_set, &relation));
        CHECK(relation == superset);
        CHECK_STATUS(d7_hamt_set_is_proper_superset_of(&set, &right_set, &relation));
        CHECK(relation == (superset && left_distinct > right_distinct));
        CHECK_STATUS(d7_hamt_set_overlaps(&set, &right_set, &relation));
        CHECK(relation == overlaps);
        CHECK_STATUS(d7_hamt_set_equals(&set, &right_set, &relation));
        CHECK(relation == (left_distinct == right_distinct && subset));

        d7_hamt_set_destroy(&right_set);
        d7_hamt_set_destroy(&set);
    }
}

static void test_set_symmetric_except_treats_duplicates_as_one_item(void) {
    d7_hamt_set_policy policy = int_set_policy(int_hash);
    const void *initial[] = { int_key(1), int_key(2) };
    const void *right[] = { int_key(1), int_key(1), int_key(3), int_key(3) };
    d7_hamt_set set;
    CHECK_STATUS(d7_hamt_set_create_range(&policy, initial, 2, &set));

    d7_hamt_set actual;
    CHECK_STATUS(d7_hamt_set_symmetric_except_many(&set, right, 4, &actual));
    bool expected[61] = { false };
    expected[2 + 30] = true;
    expected[3 + 30] = true;
    assert_set_model_matches(expected, &actual);

    d7_hamt_set_destroy(&actual);
    d7_hamt_set_destroy(&set);
}

static void test_concurrent_retained_snapshot_reads(void) {
    d7_hamt_policy map_policy = int_map_policy(int_hash);
    d7_hamt_set_policy set_policy = int_set_policy(int_hash);
    d7_hamt_map map = d7_hamt_map_create(&map_policy);
    d7_hamt_set set = d7_hamt_set_create(&set_policy);

    for (int key = 0; key != 128; ++key) {
        d7_hamt_map next_map;
        CHECK_STATUS(d7_hamt_map_set(&map, int_key(key), int_value(key * 3 - 200), &next_map));
        d7_hamt_map_destroy(&map);
        map = next_map;

        d7_hamt_set next_set;
        CHECK_STATUS(d7_hamt_set_add(&set, int_key(key), &next_set));
        d7_hamt_set_destroy(&set);
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
    d7_hamt_set_destroy(&set);
    d7_hamt_map_destroy(&map);
}

typedef struct counting_policy_state {
    long hash_calls;
    long key_retains;
    long key_releases;
    long value_retains;
    long value_releases;
} counting_policy_state;

static uint32_t counting_int_hash(const void *item, void *context) {
    counting_policy_state *state = (counting_policy_state *)context;
    ++state->hash_calls;
    return int_hash(item, NULL);
}

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
    counting_policy_state state = { 0, 0, 0, 0, 0 };
    d7_hamt_policy policy = int_map_policy(int_hash);
    policy.retain_key = counting_retain_key;
    policy.release_key = counting_release_key;
    policy.retain_value = counting_retain_value;
    policy.release_value = counting_release_value;
    policy.context = &state;

    d7_hamt_map map = d7_hamt_map_create(&policy);
    d7_hamt_map snapshot;

    /* In-place (aliased result) updates: mixed history including collisions,
     * replaces, no-op replaces, and removals. */
    for (int i = 0; i < 64; ++i) {
        CHECK_STATUS(d7_hamt_map_set(&map, int_key(i % 32), int_value(i), &map));
    }
    CHECK(d7_hamt_map_count(&map) == 32);

    /* A retained snapshot, then more aliased edits on the main line. */
    snapshot = d7_hamt_map_clone(&map);
    for (int i = 0; i < 16; ++i) {
        CHECK_STATUS(d7_hamt_map_remove(&map, int_key(i), &map));
    }
    CHECK(d7_hamt_map_count(&map) == 16);
    CHECK(d7_hamt_map_count(&snapshot) == 32);

    /* No-op replace through the aliased path must not unbalance refcounts. */
    CHECK_STATUS(d7_hamt_map_set(&map, int_key(20), int_value(52), &map));

    d7_hamt_map_destroy(&snapshot);
    d7_hamt_map_destroy(&map);

    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
    CHECK(state.key_retains > 0);
}

static void test_structural_set_algebra_prunes_shared_nodes_without_rehashing(void) {
    counting_policy_state state = { 0, 0, 0, 0, 0 };
    d7_hamt_set_policy policy = int_set_policy(counting_int_hash);
    policy.retain_item = counting_retain_key;
    policy.release_item = counting_release_key;
    policy.context = &state;

    d7_hamt_set basis = d7_hamt_set_create(&policy);
    for (int value = 0; value < 100; ++value) {
        CHECK_STATUS(d7_hamt_set_add(&basis, int_key(value), &basis));
    }
    d7_hamt_set left;
    d7_hamt_set right;
    CHECK_STATUS(d7_hamt_set_add(&basis, int_key(120), &left));
    CHECK_STATUS(d7_hamt_set_add(&basis, int_key(121), &right));

    state.hash_calls = 0;
    d7_hamt_set actual;
    CHECK_STATUS(d7_hamt_set_union(&left, &right, &actual));
    CHECK(d7_hamt_set_count(&actual) == 102);
    d7_hamt_set_destroy(&actual);
    CHECK_STATUS(d7_hamt_set_intersect(&left, &right, &actual));
    CHECK(d7_hamt_set_count(&actual) == 100);
    d7_hamt_set_destroy(&actual);
    CHECK_STATUS(d7_hamt_set_except(&left, &right, &actual));
    CHECK(d7_hamt_set_count(&actual) == 1);
    d7_hamt_set_destroy(&actual);
    CHECK_STATUS(d7_hamt_set_symmetric_except(&left, &right, &actual));
    CHECK(d7_hamt_set_count(&actual) == 2);
    d7_hamt_set_destroy(&actual);
    bool relation = false;
    CHECK_STATUS(d7_hamt_set_is_subset_of(&basis, &left, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_overlaps(&left, &right, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_equals(&left, &left, &relation));
    CHECK(relation);
    CHECK(state.hash_calls == 0);

    CHECK_STATUS(d7_hamt_set_union(&left, &left, &actual));
    CHECK(d7_hamt_set_shares_root(&left, &actual));
    d7_hamt_set_destroy(&actual);
    CHECK_STATUS(d7_hamt_set_intersect(&left, &left, &actual));
    CHECK(d7_hamt_set_shares_root(&left, &actual));
    d7_hamt_set_destroy(&actual);

    d7_hamt_set_destroy(&right);
    d7_hamt_set_destroy(&left);
    d7_hamt_set_destroy(&basis);
    CHECK(state.key_retains == state.key_releases);
}

static void test_structural_set_algebra_allocation_failures_are_atomic(void) {
    counting_policy_state state = { 0, 0, 0, 0, 0 };
    d7_hamt_set_policy policy = int_set_policy(few_buckets_int_hash);
    policy.retain_item = counting_retain_key;
    policy.release_item = counting_release_key;
    policy.context = &state;
    const void *left_items[24];
    const void *right_items[24];
    for (int index = 0; index < 24; ++index) {
        left_items[index] = int_key(index);
        right_items[index] = int_key(index + 12);
    }
    d7_hamt_set left;
    d7_hamt_set right;
    CHECK_STATUS(d7_hamt_set_create_range(&policy, left_items, 24, &left));
    CHECK_STATUS(d7_hamt_set_create_range(&policy, right_items, 24, &right));

    bool completed = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        d7_hamt_set result = { 0 };
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_set_union(&left, &right, &result);
        d7_hamt_test_reset_allocator();
        CHECK(d7_hamt_map_debug_validate_canonical(&left.map));
        CHECK(d7_hamt_map_debug_validate_canonical(&right.map));
        if (status == D7_HAMT_OK) {
            CHECK(d7_hamt_set_count(&result) == 36);
            d7_hamt_set_destroy(&result);
            completed = true;
        } else {
            CHECK(status == D7_HAMT_OUT_OF_MEMORY);
            CHECK(result.map.root == NULL);
        }
    }
    CHECK(completed);
    d7_hamt_set_destroy(&right);
    d7_hamt_set_destroy(&left);
    CHECK(state.key_retains == state.key_releases);
}

static void test_aliased_set_add_keeps_item_refcounts_balanced(void) {
    counting_policy_state state = { 0, 0, 0, 0, 0 };
    d7_hamt_set_policy policy = int_set_policy(int_hash);
    policy.retain_item = counting_retain_key;
    policy.release_item = counting_release_key;
    policy.context = &state;

    d7_hamt_set set = d7_hamt_set_create(&policy);

    /* In-place (aliased result) adds, including duplicate items that reuse
     * the existing root through the unit-value no-op replace path. */
    for (int i = 0; i < 48; ++i) {
        CHECK_STATUS(d7_hamt_set_add(&set, int_key(i % 24), &set));
    }
    CHECK(d7_hamt_set_count(&set) == 24);

    /* A retained snapshot must survive further aliased adds. */
    d7_hamt_set snapshot = d7_hamt_set_clone(&set);
    for (int i = 24; i < 32; ++i) {
        CHECK_STATUS(d7_hamt_set_add(&set, int_key(i), &set));
    }
    CHECK(d7_hamt_set_count(&set) == 32);
    CHECK(d7_hamt_set_count(&snapshot) == 24);
    CHECK(d7_hamt_set_contains(&set, int_key(31)));
    CHECK(!d7_hamt_set_contains(&snapshot, int_key(31)));

    d7_hamt_set_destroy(&snapshot);
    d7_hamt_set_destroy(&set);

    /* Every retained item must be released: an aliased add that leaked the
     * overwritten root would leave its items retained forever. */
    CHECK(state.key_retains == state.key_releases);
    CHECK(state.key_retains > 0);
}

static void test_aliased_map_add_duplicate_preserves_source(void) {
    counting_policy_state state = { 0, 0, 0, 0, 0 };
    d7_hamt_policy policy = int_map_policy(int_hash);
    policy.retain_key = counting_retain_key;
    policy.release_key = counting_release_key;
    policy.retain_value = counting_retain_value;
    policy.release_value = counting_release_value;
    policy.context = &state;

    d7_hamt_map map = d7_hamt_map_create(&policy);
    for (int i = 0; i < 16; ++i) {
        CHECK_STATUS(d7_hamt_map_add(&map, int_key(i), int_value(i * 2), &map));
    }
    CHECK(d7_hamt_map_count(&map) == 16);

    /* An aliased duplicate rejection must leave the caller's map intact. */
    CHECK(d7_hamt_map_add(&map, int_key(5), int_value(999), &map) == D7_HAMT_DUPLICATE_KEY);
    CHECK(d7_hamt_map_count(&map) == 16);
    for (int i = 0; i < 16; ++i) {
        assert_int_value(&map, i, i * 2);
    }

    /* The rejected map must remain fully usable afterwards. */
    CHECK_STATUS(d7_hamt_map_add(&map, int_key(16), int_value(32), &map));
    CHECK(d7_hamt_map_count(&map) == 17);
    assert_int_value(&map, 16, 32);

    /* A distinct result still releases the rejected version, leaving an
     * empty result value and an untouched source. */
    d7_hamt_map rejected;
    CHECK(d7_hamt_map_add(&map, int_key(5), int_value(999), &rejected) == D7_HAMT_DUPLICATE_KEY);
    CHECK(d7_hamt_map_count(&rejected) == 0);
    CHECK(d7_hamt_map_count(&map) == 17);
    assert_int_value(&map, 5, 10);

    d7_hamt_map_destroy(&map);

    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
    CHECK(state.key_retains > 0);
}

static void test_aliased_try_remove_reports_null_removed_value(void) {
    counting_policy_state state = { 0, 0, 0, 0, 0 };
    d7_hamt_policy policy = int_map_policy(int_hash);
    policy.retain_key = counting_retain_key;
    policy.release_key = counting_release_key;
    policy.retain_value = counting_retain_value;
    policy.release_value = counting_release_value;
    policy.context = &state;

    d7_hamt_map map = d7_hamt_map_create(&policy);
    for (int i = 0; i < 8; ++i) {
        CHECK_STATUS(d7_hamt_map_set(&map, int_key(i), int_value(i + 100), &map));
    }

    /* A distinct result keeps the removed value pointer valid through the
     * still-live source version. */
    bool removed = false;
    const void *removed_value = NULL;
    d7_hamt_map without_three;
    CHECK_STATUS(d7_hamt_map_try_remove(&map, int_key(3), &without_three, &removed, &removed_value));
    CHECK(removed);
    CHECK(removed_value != NULL);
    CHECK(*(const int *)removed_value == 103);
    d7_hamt_map_destroy(&without_three);

    /* An aliased removal releases the previous version inside the call, so
     * the removed value pointer is reported as NULL while the removed flag
     * stays accurate. */
    removed = false;
    removed_value = int_value(0);
    CHECK_STATUS(d7_hamt_map_try_remove(&map, int_key(5), &map, &removed, &removed_value));
    CHECK(removed);
    CHECK(removed_value == NULL);
    CHECK(d7_hamt_map_count(&map) == 7);
    CHECK(!d7_hamt_map_contains_key(&map, int_key(5)));

    /* An aliased miss also reports no removed value. */
    removed = true;
    removed_value = int_value(0);
    CHECK_STATUS(d7_hamt_map_try_remove(&map, int_key(5), &map, &removed, &removed_value));
    CHECK(!removed);
    CHECK(removed_value == NULL);
    CHECK(d7_hamt_map_count(&map) == 7);

    d7_hamt_map_destroy(&map);

    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
}

static void test_map_transient_lifecycle_reads_and_snapshot_isolation(void) {
    int policy_context = 17;
    int stored_key = 5;
    int equivalent_key = 5;
    int stored_value = 50;
    int equal_value = 50;
    d7_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    policy.context = &policy_context;

    d7_hamt_map source = d7_hamt_map_create(&policy);
    CHECK_STATUS(d7_hamt_map_set(&source, &stored_key, &stored_value, &source));
    const void *source_root = d7_hamt_map_debug_root_identity(&source);

    d7_hamt_map_transient clean;
    CHECK_STATUS(d7_hamt_map_to_transient(&source, &clean));
    CHECK(d7_hamt_map_transient_is_active(&clean));
    CHECK(d7_hamt_map_transient_debug_root_identity(&clean) == source_root);

    d7_hamt_policy actual_policy;
    CHECK_STATUS(d7_hamt_map_transient_get_policy(&clean, &actual_policy));
    CHECK(actual_policy.hash == policy.hash);
    CHECK(actual_policy.key_equal == policy.key_equal);
    CHECK(actual_policy.value_equal == policy.value_equal);
    CHECK(actual_policy.context == &policy_context);

    d7_hamt_map_transient alias;
    CHECK_STATUS(d7_hamt_map_transient_clone(&clean, &alias));
    d7_hamt_map_transient_iterator clean_iterator;
    CHECK_STATUS(d7_hamt_map_transient_iterator_init(&clean, &clean_iterator));

    CHECK_STATUS(d7_hamt_map_transient_set(
        &clean,
        &equivalent_key,
        &equal_value));
    CHECK(d7_hamt_map_transient_debug_root_identity(&clean) == source_root);

    bool added = true;
    CHECK_STATUS(d7_hamt_map_transient_try_add(
        &clean,
        &equivalent_key,
        int_value(999),
        &added));
    CHECK(!added);
    CHECK(d7_hamt_map_transient_add(
        &clean,
        &equivalent_key,
        int_value(999)) == D7_HAMT_DUPLICATE_KEY);
    CHECK_STATUS(d7_hamt_map_transient_remove(&clean, int_key(99)));
    CHECK(d7_hamt_map_transient_debug_root_identity(&clean) == source_root);

    bool found = false;
    const void *actual_key = NULL;
    const void *actual_value = NULL;
    CHECK_STATUS(d7_hamt_map_transient_try_get_key(
        &clean,
        &equivalent_key,
        &found,
        &actual_key));
    CHECK(found);
    CHECK(actual_key == &stored_key);
    CHECK_STATUS(d7_hamt_map_transient_try_get(
        &clean,
        &equivalent_key,
        &found,
        &actual_value));
    CHECK(found);
    CHECK(actual_value == &stored_value);

    bool has_value = false;
    const void *iterator_key = NULL;
    const void *iterator_value = NULL;
    CHECK_STATUS(d7_hamt_map_transient_iterator_next(
        &clean_iterator,
        &has_value,
        &iterator_key,
        &iterator_value));
    CHECK(has_value);
    CHECK(iterator_key == &stored_key);
    CHECK(iterator_value == &stored_value);

    d7_hamt_map clean_published;
    CHECK_STATUS(d7_hamt_map_transient_persist(&alias, &clean_published));
    CHECK(d7_hamt_map_shares_root(&source, &clean_published));
    CHECK(!d7_hamt_map_transient_is_active(&clean));
    CHECK(!d7_hamt_map_transient_is_active(&alias));

    size_t consumed_count = 12345;
    CHECK(d7_hamt_map_transient_count(&clean, &consumed_count) ==
        D7_HAMT_TRANSIENT_CONSUMED);
    CHECK(consumed_count == 12345);
    has_value = true;
    iterator_key = int_key(1);
    iterator_value = int_value(1);
    CHECK(d7_hamt_map_transient_iterator_next(
        &clean_iterator,
        &has_value,
        &iterator_key,
        &iterator_value) == D7_HAMT_TRANSIENT_CONSUMED);
    CHECK(has_value);
    CHECK(iterator_key == int_key(1));
    CHECK(iterator_value == int_value(1));

    d7_hamt_map untouched = d7_hamt_map_create(&policy);
    CHECK_STATUS(d7_hamt_map_set(&untouched, int_key(42), int_value(420), &untouched));
    const void *untouched_root = d7_hamt_map_debug_root_identity(&untouched);
    CHECK(d7_hamt_map_transient_persist(&clean, &untouched) ==
        D7_HAMT_TRANSIENT_CONSUMED);
    CHECK(d7_hamt_map_debug_root_identity(&untouched) == untouched_root);

    d7_hamt_map_transient_destroy(&alias);
    d7_hamt_map_transient_destroy(&clean);

    d7_hamt_map_transient edited;
    CHECK_STATUS(d7_hamt_map_to_transient(&source, &edited));
    d7_hamt_map_transient_iterator invalidated;
    CHECK_STATUS(d7_hamt_map_transient_iterator_init(&edited, &invalidated));
    CHECK_STATUS(d7_hamt_map_transient_set(&edited, int_key(8), int_value(80)));
    CHECK(!d7_hamt_map_contains_key(&source, int_key(8)));
    bool contains = false;
    CHECK_STATUS(d7_hamt_map_transient_contains_key(&edited, int_key(8), &contains));
    CHECK(contains);

    has_value = false;
    iterator_key = NULL;
    iterator_value = NULL;
    CHECK(d7_hamt_map_transient_iterator_next(
        &invalidated,
        &has_value,
        &iterator_key,
        &iterator_value) == D7_HAMT_TRANSIENT_MODIFIED);
    CHECK(!has_value);
    CHECK(iterator_key == NULL);
    CHECK(iterator_value == NULL);

    CHECK(d7_hamt_map_transient_persist(&edited, NULL) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_map_transient_is_active(&edited));
    d7_hamt_map changed_published;
    CHECK_STATUS(d7_hamt_map_transient_persist(&edited, &changed_published));
    CHECK(d7_hamt_map_count(&source) == 1);
    CHECK(d7_hamt_map_count(&changed_published) == 2);
    assert_int_value(&changed_published, 8, 80);
    d7_hamt_map_transient_destroy(&edited);

    d7_hamt_map_transient cleared;
    CHECK_STATUS(d7_hamt_map_to_transient(&changed_published, &cleared));
    CHECK_STATUS(d7_hamt_map_transient_clear(&cleared));
    size_t count = SIZE_MAX;
    CHECK_STATUS(d7_hamt_map_transient_count(&cleared, &count));
    CHECK(count == 0);
    d7_hamt_map_transient_iterator empty_iterator;
    CHECK_STATUS(d7_hamt_map_transient_iterator_init(&cleared, &empty_iterator));
    CHECK_STATUS(d7_hamt_map_transient_clear(&cleared));
    has_value = true;
    CHECK_STATUS(d7_hamt_map_transient_iterator_next(
        &empty_iterator,
        &has_value,
        NULL,
        NULL));
    CHECK(!has_value);
    d7_hamt_map empty_published;
    CHECK_STATUS(d7_hamt_map_transient_persist(&cleared, &empty_published));
    CHECK(d7_hamt_map_count(&empty_published) == 0);
    CHECK(empty_published.policy.context == &policy_context);
    d7_hamt_map_transient_destroy(&cleared);

    d7_hamt_map_destroy(&empty_published);
    d7_hamt_map_destroy(&changed_published);
    d7_hamt_map_destroy(&untouched);
    d7_hamt_map_destroy(&clean_published);
    d7_hamt_map_destroy(&source);
}

static void test_set_transient_lifecycle_representatives_and_clear(void) {
    int policy_context = 23;
    int stored_item = 7;
    int equivalent_item = 7;
    d7_hamt_set_policy policy = int_set_policy(few_buckets_int_hash);
    policy.context = &policy_context;

    d7_hamt_set source = d7_hamt_set_create(&policy);
    CHECK_STATUS(d7_hamt_set_add(&source, &stored_item, &source));
    const void *source_root = d7_hamt_set_debug_root_identity(&source);

    d7_hamt_set_transient transient;
    CHECK_STATUS(d7_hamt_set_to_transient(&source, &transient));
    CHECK(d7_hamt_set_transient_debug_root_identity(&transient) == source_root);
    d7_hamt_set_transient alias;
    CHECK_STATUS(d7_hamt_set_transient_clone(&transient, &alias));

    d7_hamt_set_policy actual_policy;
    CHECK_STATUS(d7_hamt_set_transient_get_policy(&transient, &actual_policy));
    CHECK(actual_policy.hash == policy.hash);
    CHECK(actual_policy.equal == policy.equal);
    CHECK(actual_policy.context == &policy_context);

    d7_hamt_set_transient_iterator stable_iterator;
    CHECK_STATUS(d7_hamt_set_transient_iterator_init(&transient, &stable_iterator));
    CHECK_STATUS(d7_hamt_set_transient_add(&transient, &equivalent_item));
    bool added = true;
    CHECK_STATUS(d7_hamt_set_transient_try_add(
        &transient,
        &equivalent_item,
        &added));
    CHECK(!added);
    bool removed = true;
    CHECK_STATUS(d7_hamt_set_transient_try_remove(
        &transient,
        int_key(99),
        &removed));
    CHECK(!removed);
    CHECK(d7_hamt_set_transient_debug_root_identity(&transient) == source_root);

    bool found = false;
    const void *actual_item = NULL;
    CHECK_STATUS(d7_hamt_set_transient_try_get_value(
        &transient,
        &equivalent_item,
        &found,
        &actual_item));
    CHECK(found);
    CHECK(actual_item == &stored_item);

    bool has_value = false;
    const void *item = NULL;
    CHECK_STATUS(d7_hamt_set_transient_iterator_next(
        &stable_iterator,
        &has_value,
        &item));
    CHECK(has_value);
    CHECK(item == &stored_item);

    d7_hamt_set_transient_iterator invalidated;
    CHECK_STATUS(d7_hamt_set_transient_iterator_init(&transient, &invalidated));
    CHECK_STATUS(d7_hamt_set_transient_add(&transient, int_key(8)));
    CHECK(!d7_hamt_set_contains(&source, int_key(8)));
    has_value = true;
    item = int_key(1);
    CHECK(d7_hamt_set_transient_iterator_next(
        &invalidated,
        &has_value,
        &item) == D7_HAMT_TRANSIENT_MODIFIED);
    CHECK(has_value);
    CHECK(item == int_key(1));

    CHECK_STATUS(d7_hamt_set_transient_clear(&transient));
    size_t count = SIZE_MAX;
    CHECK_STATUS(d7_hamt_set_transient_count(&transient, &count));
    CHECK(count == 0);
    d7_hamt_set_transient_iterator empty_iterator;
    CHECK_STATUS(d7_hamt_set_transient_iterator_init(&transient, &empty_iterator));
    CHECK_STATUS(d7_hamt_set_transient_clear(&transient));
    has_value = true;
    CHECK_STATUS(d7_hamt_set_transient_iterator_next(
        &empty_iterator,
        &has_value,
        NULL));
    CHECK(!has_value);
    CHECK_STATUS(d7_hamt_set_transient_add(&transient, int_key(9)));

    d7_hamt_set published;
    CHECK_STATUS(d7_hamt_set_transient_persist(&alias, &published));
    CHECK(d7_hamt_set_contains(&published, int_key(9)));
    CHECK(d7_hamt_set_count(&published) == 1);
    CHECK(!d7_hamt_set_transient_is_active(&transient));
    CHECK(!d7_hamt_set_transient_is_active(&alias));
    bool contains = true;
    CHECK(d7_hamt_set_transient_contains(&transient, int_key(9), &contains) ==
        D7_HAMT_TRANSIENT_CONSUMED);
    CHECK(contains);

    d7_hamt_set_transient_destroy(&alias);
    d7_hamt_set_transient_destroy(&transient);
    d7_hamt_set_destroy(&published);
    d7_hamt_set_destroy(&source);

    d7_hamt_set_transient empty;
    CHECK_STATUS(d7_hamt_set_transient_create(&policy, &empty));
    d7_hamt_set empty_published;
    CHECK_STATUS(d7_hamt_set_transient_persist(&empty, &empty_published));
    CHECK(d7_hamt_set_count(&empty_published) == 0);
    CHECK(empty_published.map.policy.context == &policy_context);
    d7_hamt_set_transient_destroy(&empty);
    d7_hamt_set_destroy(&empty_published);
}

static void test_map_transient_deterministic_model_history(void) {
    d7_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    d7_hamt_map_transient transient;
    CHECK_STATUS(d7_hamt_map_transient_create(&policy, &transient));

    bool present[81] = { false };
    int values[81] = { 0 };
    uint32_t random = 0xa511e9b3u;
    for (int step = 0; step < 4096; ++step) {
        random = random * 1664525u + 1013904223u;
        const int key = (int)(random % 81u) - 40;
        random = random * 1664525u + 1013904223u;
        const int value = (int)(random % 2001u) - 1000;
        const unsigned operation = (random >> 27) % 7u;
        const size_t index = (size_t)(key + 40);

        if (operation == 0) {
            CHECK_STATUS(d7_hamt_map_transient_set(
                &transient,
                int_key(key),
                int_value(value)));
            present[index] = true;
            values[index] = value;
        } else if (operation == 1) {
            bool added = false;
            CHECK_STATUS(d7_hamt_map_transient_try_add(
                &transient,
                int_key(key),
                int_value(value),
                &added));
            CHECK(added == !present[index]);
            if (added) {
                present[index] = true;
                values[index] = value;
            }
        } else if (operation == 2) {
            const d7_hamt_status status = d7_hamt_map_transient_add(
                &transient,
                int_key(key),
                int_value(value));
            if (present[index]) {
                CHECK(status == D7_HAMT_DUPLICATE_KEY);
            } else {
                CHECK(status == D7_HAMT_OK);
                present[index] = true;
                values[index] = value;
            }
        } else if (operation == 3) {
            bool removed = false;
            CHECK_STATUS(d7_hamt_map_transient_try_remove(
                &transient,
                int_key(key),
                &removed));
            CHECK(removed == present[index]);
            present[index] = false;
        } else if (operation == 4) {
            CHECK_STATUS(d7_hamt_map_transient_remove(&transient, int_key(key)));
            present[index] = false;
        } else if (operation == 5 && (random & 31u) == 0) {
            CHECK_STATUS(d7_hamt_map_transient_clear(&transient));
            memset(present, 0, sizeof(present));
        } else {
            bool contains = false;
            CHECK_STATUS(d7_hamt_map_transient_contains_key(
                &transient,
                int_key(key),
                &contains));
            CHECK(contains == present[index]);
        }

        if ((step & 127) == 0) {
            assert_int_transient_model_matches(present, values, &transient);
        }
    }

    assert_int_transient_model_matches(present, values, &transient);
    d7_hamt_map published;
    CHECK_STATUS(d7_hamt_map_transient_persist(&transient, &published));
    assert_int_model_matches(present, values, &published);
    d7_hamt_map_transient_destroy(&transient);
    d7_hamt_map_destroy(&published);
}

static void test_transient_allocation_failures_are_atomic(void) {
    d7_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    d7_hamt_map source = d7_hamt_map_create(&policy);
    for (int index = 0; index < 24; ++index) {
        CHECK_STATUS(d7_hamt_map_set(
            &source,
            int_key(index),
            int_value(index * 3),
            &source));
    }

    d7_hamt_map_transient failed_adoption;
    failed_adoption.state = (struct d7_hamt_map_transient_state *)(void *)&source;
    d7_hamt_test_fail_allocations_after(0);
    CHECK(d7_hamt_map_to_transient(&source, &failed_adoption) ==
        D7_HAMT_OUT_OF_MEMORY);
    d7_hamt_test_reset_allocator();
    CHECK(failed_adoption.state ==
        (struct d7_hamt_map_transient_state *)(void *)&source);

    bool completed = false;
    bool saw_failure = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        d7_hamt_map_transient transient;
        CHECK_STATUS(d7_hamt_map_to_transient(&source, &transient));
        const void *root = d7_hamt_map_transient_debug_root_identity(&transient);
        d7_hamt_map_transient_iterator iterator;
        CHECK_STATUS(d7_hamt_map_transient_iterator_init(&transient, &iterator));
        bool added = true;

        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_map_transient_try_add(
            &transient,
            int_key(24),
            int_value(72),
            &added);
        d7_hamt_test_reset_allocator();

        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(added);
            CHECK(d7_hamt_map_transient_debug_root_identity(&transient) == root);
            size_t count = SIZE_MAX;
            CHECK_STATUS(d7_hamt_map_transient_count(&transient, &count));
            CHECK(count == 24);
            bool contains = true;
            CHECK_STATUS(d7_hamt_map_transient_contains_key(
                &transient,
                int_key(24),
                &contains));
            CHECK(!contains);
            bool has_value = false;
            CHECK_STATUS(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL));
            CHECK(has_value);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(added);
            size_t count = 0;
            CHECK_STATUS(d7_hamt_map_transient_count(&transient, &count));
            CHECK(count == 25);
            bool has_value = false;
            CHECK(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL) == D7_HAMT_TRANSIENT_MODIFIED);
            completed = true;
        }
        d7_hamt_map_transient_destroy(&transient);
    }
    CHECK(saw_failure);
    CHECK(completed);

    completed = false;
    saw_failure = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        d7_hamt_map_transient transient;
        CHECK_STATUS(d7_hamt_map_to_transient(&source, &transient));
        const void *root = d7_hamt_map_transient_debug_root_identity(&transient);
        d7_hamt_map_transient_iterator iterator;
        CHECK_STATUS(d7_hamt_map_transient_iterator_init(&transient, &iterator));

        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_map_transient_set(
            &transient,
            int_key(5),
            int_value(777));
        d7_hamt_test_reset_allocator();

        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(d7_hamt_map_transient_debug_root_identity(&transient) == root);
            bool found = false;
            const void *value = NULL;
            CHECK_STATUS(d7_hamt_map_transient_try_get(
                &transient,
                int_key(5),
                &found,
                &value));
            CHECK(found);
            CHECK(*(const int *)value == 15);
            bool has_value = false;
            CHECK_STATUS(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL));
            CHECK(has_value);
        } else {
            CHECK(status == D7_HAMT_OK);
            bool found = false;
            const void *value = NULL;
            CHECK_STATUS(d7_hamt_map_transient_try_get(
                &transient,
                int_key(5),
                &found,
                &value));
            CHECK(found);
            CHECK(*(const int *)value == 777);
            bool has_value = false;
            CHECK(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL) == D7_HAMT_TRANSIENT_MODIFIED);
            completed = true;
        }
        d7_hamt_map_transient_destroy(&transient);
    }
    CHECK(saw_failure);
    CHECK(completed);

    completed = false;
    saw_failure = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        d7_hamt_map_transient transient;
        CHECK_STATUS(d7_hamt_map_to_transient(&source, &transient));
        const void *root = d7_hamt_map_transient_debug_root_identity(&transient);
        d7_hamt_map_transient_iterator iterator;
        CHECK_STATUS(d7_hamt_map_transient_iterator_init(&transient, &iterator));
        bool removed = true;

        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_map_transient_try_remove(
            &transient,
            int_key(7),
            &removed);
        d7_hamt_test_reset_allocator();

        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(removed);
            CHECK(d7_hamt_map_transient_debug_root_identity(&transient) == root);
            bool contains = false;
            CHECK_STATUS(d7_hamt_map_transient_contains_key(
                &transient,
                int_key(7),
                &contains));
            CHECK(contains);
            bool has_value = false;
            CHECK_STATUS(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL));
            CHECK(has_value);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(removed);
            bool contains = true;
            CHECK_STATUS(d7_hamt_map_transient_contains_key(
                &transient,
                int_key(7),
                &contains));
            CHECK(!contains);
            bool has_value = false;
            CHECK(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL) == D7_HAMT_TRANSIENT_MODIFIED);
            completed = true;
        }
        d7_hamt_map_transient_destroy(&transient);
    }
    CHECK(saw_failure);
    CHECK(completed);

    d7_hamt_set_policy set_policy = int_set_policy(few_buckets_int_hash);
    d7_hamt_set set_source = d7_hamt_set_create(&set_policy);
    for (int index = 0; index < 24; ++index) {
        CHECK_STATUS(d7_hamt_set_add(&set_source, int_key(index), &set_source));
    }

    d7_hamt_set_transient failed_set_adoption;
    failed_set_adoption.inner.state =
        (struct d7_hamt_map_transient_state *)(void *)&set_source;
    d7_hamt_test_fail_allocations_after(0);
    CHECK(d7_hamt_set_to_transient(&set_source, &failed_set_adoption) ==
        D7_HAMT_OUT_OF_MEMORY);
    d7_hamt_test_reset_allocator();
    CHECK(failed_set_adoption.inner.state ==
        (struct d7_hamt_map_transient_state *)(void *)&set_source);

    completed = false;
    saw_failure = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        d7_hamt_set_transient transient;
        CHECK_STATUS(d7_hamt_set_to_transient(&set_source, &transient));
        const void *root = d7_hamt_set_transient_debug_root_identity(&transient);
        d7_hamt_set_transient_iterator iterator;
        CHECK_STATUS(d7_hamt_set_transient_iterator_init(&transient, &iterator));
        bool added = true;

        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_set_transient_try_add(
            &transient,
            int_key(24),
            &added);
        d7_hamt_test_reset_allocator();

        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(added);
            CHECK(d7_hamt_set_transient_debug_root_identity(&transient) == root);
            bool contains = true;
            CHECK_STATUS(d7_hamt_set_transient_contains(
                &transient,
                int_key(24),
                &contains));
            CHECK(!contains);
            bool has_value = false;
            CHECK_STATUS(d7_hamt_set_transient_iterator_next(
                &iterator,
                &has_value,
                NULL));
            CHECK(has_value);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(added);
            bool contains = false;
            CHECK_STATUS(d7_hamt_set_transient_contains(
                &transient,
                int_key(24),
                &contains));
            CHECK(contains);
            bool has_value = false;
            CHECK(d7_hamt_set_transient_iterator_next(
                &iterator,
                &has_value,
                NULL) == D7_HAMT_TRANSIENT_MODIFIED);
            completed = true;
        }
        d7_hamt_set_transient_destroy(&transient);
    }
    CHECK(saw_failure);
    CHECK(completed);

    d7_hamt_set_destroy(&set_source);
    d7_hamt_map_destroy(&source);
}

typedef struct failing_transient_retain_state {
    size_t call_count;
    size_t fail_after;
    bool failure_enabled;
    long key_retains;
    long key_releases;
    long value_retains;
    long value_releases;
} failing_transient_retain_state;

static void *failing_transient_retain_key(const void *key, void *context) {
    failing_transient_retain_state *state =
        (failing_transient_retain_state *)context;
    if (key == NULL) {
        return NULL;
    }
    if (state->failure_enabled && state->call_count++ == state->fail_after) {
        return NULL;
    }
    ++state->key_retains;
    return (void *)key;
}

static void failing_transient_release_key(void *key, void *context) {
    failing_transient_retain_state *state =
        (failing_transient_retain_state *)context;
    if (key != NULL) {
        ++state->key_releases;
    }
}

static void *failing_transient_retain_value(const void *value, void *context) {
    failing_transient_retain_state *state =
        (failing_transient_retain_state *)context;
    if (value == NULL) {
        return NULL;
    }
    if (state->failure_enabled && state->call_count++ == state->fail_after) {
        return NULL;
    }
    ++state->value_retains;
    return (void *)value;
}

static void failing_transient_release_value(void *value, void *context) {
    failing_transient_retain_state *state =
        (failing_transient_retain_state *)context;
    if (value != NULL) {
        ++state->value_releases;
    }
}

static void test_transient_retain_failures_are_atomic_and_retryable(void) {
    failing_transient_retain_state state = {
        0, 0, false, 0, 0, 0, 0
    };
    d7_hamt_policy policy = int_map_policy(few_buckets_int_hash);
    policy.retain_key = failing_transient_retain_key;
    policy.release_key = failing_transient_release_key;
    policy.retain_value = failing_transient_retain_value;
    policy.release_value = failing_transient_release_value;
    policy.context = &state;

    d7_hamt_map source = d7_hamt_map_create(&policy);
    for (int index = 0; index < 12; ++index) {
        CHECK_STATUS(d7_hamt_map_set(
            &source,
            int_key(index),
            int_value(index * 5),
            &source));
    }

    bool completed = false;
    bool saw_failure = false;
    for (size_t fail_after = 0; fail_after < 256 && !completed; ++fail_after) {
        d7_hamt_map_transient transient;
        CHECK_STATUS(d7_hamt_map_to_transient(&source, &transient));
        const void *root = d7_hamt_map_transient_debug_root_identity(&transient);
        d7_hamt_map_transient_iterator iterator;
        CHECK_STATUS(d7_hamt_map_transient_iterator_init(&transient, &iterator));
        bool added = true;

        state.call_count = 0;
        state.fail_after = fail_after;
        state.failure_enabled = true;
        const d7_hamt_status status = d7_hamt_map_transient_try_add(
            &transient,
            int_key(12),
            int_value(60),
            &added);
        state.failure_enabled = false;

        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(added);
            CHECK(d7_hamt_map_transient_debug_root_identity(&transient) == root);
            size_t count = SIZE_MAX;
            CHECK_STATUS(d7_hamt_map_transient_count(&transient, &count));
            CHECK(count == 12);
            bool contains = true;
            CHECK_STATUS(d7_hamt_map_transient_contains_key(
                &transient,
                int_key(12),
                &contains));
            CHECK(!contains);
            bool has_value = false;
            CHECK_STATUS(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL));
            CHECK(has_value);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(added);
            bool contains = false;
            CHECK_STATUS(d7_hamt_map_transient_contains_key(
                &transient,
                int_key(12),
                &contains));
            CHECK(contains);
            bool has_value = false;
            CHECK(d7_hamt_map_transient_iterator_next(
                &iterator,
                &has_value,
                NULL,
                NULL) == D7_HAMT_TRANSIENT_MODIFIED);
            completed = true;
        }
        d7_hamt_map_transient_destroy(&transient);
    }
    CHECK(saw_failure);
    CHECK(completed);

    d7_hamt_map_destroy(&source);
    CHECK(state.key_retains == state.key_releases);
    CHECK(state.value_retains == state.value_releases);
    CHECK(state.key_retains > 0);
    CHECK(state.value_retains > 0);
}

static void test_set_transient_relations_preserve_policy_and_lifecycle(void) {
    d7_hamt_set_policy policy = int_set_policy(few_buckets_int_hash);
    const void *source_items[] = { int_key(1), int_key(2), int_key(3) };
    d7_hamt_set source;
    CHECK_STATUS(d7_hamt_set_create_range(&policy, source_items, 3, &source));
    d7_hamt_set_transient transient;
    CHECK_STATUS(d7_hamt_set_to_transient(&source, &transient));
    const void *root = d7_hamt_set_transient_debug_root_identity(&transient);
    d7_hamt_set_transient_iterator iterator;
    CHECK_STATUS(d7_hamt_set_transient_iterator_init(&transient, &iterator));

    const void *equal_with_duplicates[] = {
        int_key(3), int_key(2), int_key(1), int_key(1)
    };
    bool relation = false;
    CHECK_STATUS(d7_hamt_set_transient_is_subset_of_many(
        &transient, equal_with_duplicates, 4, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_transient_is_proper_subset_of_many(
        &transient, equal_with_duplicates, 4, &relation));
    CHECK(!relation);
    CHECK_STATUS(d7_hamt_set_transient_is_superset_of_many(
        &transient, equal_with_duplicates, 4, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_transient_is_proper_superset_of_many(
        &transient, equal_with_duplicates, 4, &relation));
    CHECK(!relation);
    CHECK_STATUS(d7_hamt_set_transient_overlaps_many(
        &transient, equal_with_duplicates, 4, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_transient_equals_many(
        &transient, equal_with_duplicates, 4, &relation));
    CHECK(relation);

    d7_hamt_set_policy other_policy = int_set_policy(int_hash);
    const void *larger_items[] = {
        int_key(4), int_key(3), int_key(2), int_key(1)
    };
    d7_hamt_set larger;
    CHECK_STATUS(d7_hamt_set_create_range(
        &other_policy, larger_items, 4, &larger));
    CHECK_STATUS(d7_hamt_set_transient_is_subset_of(
        &transient, &larger, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_transient_is_proper_subset_of(
        &transient, &larger, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_transient_is_superset_of(
        &transient, &larger, &relation));
    CHECK(!relation);
    CHECK_STATUS(d7_hamt_set_transient_is_proper_superset_of(
        &transient, &larger, &relation));
    CHECK(!relation);
    CHECK_STATUS(d7_hamt_set_transient_overlaps(
        &transient, &larger, &relation));
    CHECK(relation);
    CHECK_STATUS(d7_hamt_set_transient_equals(
        &transient, &larger, &relation));
    CHECK(!relation);
    CHECK(d7_hamt_set_transient_debug_root_identity(&transient) == root);

    bool has_value = false;
    CHECK_STATUS(d7_hamt_set_transient_iterator_next(
        &iterator, &has_value, NULL));
    CHECK(has_value);

    d7_hamt_set published;
    CHECK_STATUS(d7_hamt_set_transient_persist(&transient, &published));
    relation = true;
    CHECK(d7_hamt_set_transient_equals_many(
        &transient, equal_with_duplicates, 4, &relation) ==
        D7_HAMT_TRANSIENT_CONSUMED);
    CHECK(relation);

    d7_hamt_set_transient_destroy(&transient);
    d7_hamt_set_destroy(&published);
    d7_hamt_set_destroy(&larger);
    d7_hamt_set_destroy(&source);
}

static void test_set_transient_relation_failures_preserve_output(void) {
    d7_hamt_set_policy policy = int_set_policy(few_buckets_int_hash);
    const void *source_items[16];
    const void *larger_items[17];
    for (int index = 0; index < 16; ++index) {
        source_items[index] = int_key(index);
        larger_items[index] = int_key(index);
    }
    larger_items[16] = int_key(16);

    d7_hamt_set source;
    CHECK_STATUS(d7_hamt_set_create_range(&policy, source_items, 16, &source));
    d7_hamt_set_transient transient;
    CHECK_STATUS(d7_hamt_set_to_transient(&source, &transient));
    const void *root = d7_hamt_set_transient_debug_root_identity(&transient);

    bool completed = false;
    bool saw_failure = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        bool relation = true;
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_set_transient_equals_many(
            &transient, larger_items, 17, &relation);
        d7_hamt_test_reset_allocator();
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(relation);
            CHECK(d7_hamt_set_transient_debug_root_identity(&transient) == root);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(!relation);
            completed = true;
        }
    }
    CHECK(saw_failure);
    CHECK(completed);

    d7_hamt_set_policy other_policy = int_set_policy(int_hash);
    d7_hamt_set larger;
    CHECK_STATUS(d7_hamt_set_create_range(
        &other_policy, larger_items, 17, &larger));
    completed = false;
    saw_failure = false;
    for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
        bool relation = true;
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_set_transient_equals(
            &transient, &larger, &relation);
        d7_hamt_test_reset_allocator();
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_failure = true;
            CHECK(relation);
            CHECK(d7_hamt_set_transient_debug_root_identity(&transient) == root);
        } else {
            CHECK(status == D7_HAMT_OK);
            CHECK(!relation);
            completed = true;
        }
    }
    CHECK(saw_failure);
    CHECK(completed);

    d7_hamt_set_destroy(&larger);
    d7_hamt_set_transient_destroy(&transient);
    d7_hamt_set_destroy(&source);
}

static const test_case tests[] = {
    { "empty map has no entries", test_empty_map_has_no_entries },
    { "set item adds replaces and preserves old versions", test_set_adds_replaces_and_preserves_old_versions },
    { "add and try_add reject duplicates", test_add_and_try_add_reject_duplicates },
    { "factories select once and preserve representatives",
      test_factories_select_once_and_preserve_representatives },
    { "factories cover collision bitmap and retained outputs",
      test_factories_cover_collision_bitmap_and_retained_outputs },
    { "factory failures leave sources and outputs unchanged",
      test_factory_failures_leave_sources_and_outputs_unchanged },
    { "remove and try_remove delete present keys", test_remove_and_try_remove_delete_present_keys },
    { "set_many and clear preserve contracts", test_set_many_and_clear_preserve_contracts },
    { "create_range last wins and retains first equivalent key", test_create_range_last_wins_and_retains_first_equivalent_key },
    { "equal hash collision bucket preserves every key", test_equal_hash_collision_bucket_preserves_every_key },
    { "topology comparator rejects different collision keys",
      test_topology_comparator_rejects_different_collision_keys },
    { "deep shared hash prefixes lookup and remove correctly", test_deep_shared_hash_prefixes_lookup_and_remove_correctly },
    { "depth seven iterator traversal", test_depth_seven_iterator_traversal },
    { "allocation failures unwind node_set and merge", test_allocation_failures_unwind_node_set_and_merge },
    { "collision bucket splits and hash mismatch probes miss", test_collision_bucket_splits_and_hash_mismatch_probes_miss },
    { "collision bucket equal value keeps root and key object", test_collision_bucket_equal_value_keeps_root_and_key_object },
    { "structure root shape and sharing", test_structure_root_shape_and_sharing },
    { "CHAMP independent histories and typed diff", test_champ_independent_histories_and_typed_diff },
    { "CHAMP collision runs compare and diff semantically",
      test_champ_collision_runs_compare_and_diff_semantically },
    { "CHAMP equality and diff prune shared descendants",
      test_champ_equality_and_diff_prune_shared_descendants },
    { "iterator copy advances independently", test_iterator_copy_advances_independently },
    { "random history matches model and preserves snapshots", test_random_history_matches_model_and_preserves_snapshots },
    { "scripted collision snapshot story", test_scripted_collision_snapshot_story },
    { "random history with colliding hashes matches model", test_random_history_with_colliding_hashes_matches_model },
    { "set add remove contains and persistence", test_set_add_remove_contains_and_persistence },
    { "set custom comparer retains first item", test_set_custom_comparer_retains_first_item },
    { "CHAMP map algebra preserves representatives and bias",
      test_champ_map_algebra_preserves_representatives_and_bias },
    { "set algebra matches model", test_set_algebra_matches_model },
    { "set symmetric_except treats duplicates as one item", test_set_symmetric_except_treats_duplicates_as_one_item },
    { "concurrent retained snapshot reads", test_concurrent_retained_snapshot_reads },
    { "counting policy stays balanced and aliasing updates are safe",
      test_counting_policy_stays_balanced_and_aliasing_updates_are_safe },
    { "structural set algebra prunes shared nodes without rehashing",
      test_structural_set_algebra_prunes_shared_nodes_without_rehashing },
    { "structural set algebra allocation failures are atomic",
      test_structural_set_algebra_allocation_failures_are_atomic },
    { "aliased set_add keeps item refcounts balanced",
      test_aliased_set_add_keeps_item_refcounts_balanced },
    { "aliased map_add duplicate preserves source",
      test_aliased_map_add_duplicate_preserves_source },
    { "aliased try_remove reports null removed value",
      test_aliased_try_remove_reports_null_removed_value },
    { "map transient lifecycle reads and snapshot isolation",
      test_map_transient_lifecycle_reads_and_snapshot_isolation },
    { "set transient lifecycle representatives and clear",
      test_set_transient_lifecycle_representatives_and_clear },
    { "map transient deterministic model history",
      test_map_transient_deterministic_model_history },
    { "transient allocation failures are atomic",
      test_transient_allocation_failures_are_atomic },
    { "transient retain failures are atomic and retryable",
      test_transient_retain_failures_are_atomic_and_retryable },
    { "set transient relations preserve policy and lifecycle",
      test_set_transient_relations_preserve_policy_and_lifecycle },
    { "set transient relation failures preserve output",
      test_set_transient_relation_failures_preserve_output }
};

int main(void) {
    if (!d7_enter_headless_test_process()) {
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
