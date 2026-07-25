#include <durable7/hamt/persistent_bi_map.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void d7_hamt_test_fail_allocations_after(size_t successful_allocations);
void d7_hamt_test_reset_allocator(void);

static void fail_at(const char *file, int line, const char *expression) {
    fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail_at(__FILE__, __LINE__, #expression); } while (0)
#define CHECK_STATUS(expression) CHECK((expression) == D7_HAMT_OK)

static int keys[32];
static int values[32];

static uint32_t int_hash(const void *item, void *context) {
    (void)context;
    return (uint32_t)*(const int *)item * 0x9e3779b9u;
}
static uint32_t few_hash(const void *item, void *context) {
    (void)context;
    return (uint32_t)*(const int *)item & 3u;
}
static bool int_equal(const void *left, const void *right, void *context) {
    (void)context;
    return *(const int *)left == *(const int *)right;
}
static uint32_t modulo_hash(const void *item, void *context) {
    const int modulus = *(const int *)context;
    return (uint32_t)(*(const int *)item % modulus);
}
static bool modulo_equal(const void *left, const void *right, void *context) {
    const int modulus = *(const int *)context;
    return *(const int *)left % modulus == *(const int *)right % modulus;
}
static d7_hamt_set_policy int_policy(d7_hamt_hash_fn hash) {
    d7_hamt_set_policy policy = d7_hamt_set_policy_default();
    policy.hash = hash;
    policy.equal = int_equal;
    return policy;
}

static void strict_policies_representatives_and_inverse(void) {
    int key_modulus = 10;
    int value_modulus = 100;
    d7_hamt_set_policy key_policy = d7_hamt_set_policy_default();
    key_policy.hash = modulo_hash;
    key_policy.equal = modulo_equal;
    key_policy.context = &key_modulus;
    d7_hamt_set_policy value_policy = d7_hamt_set_policy_default();
    value_policy.hash = modulo_hash;
    value_policy.equal = modulo_equal;
    value_policy.context = &value_modulus;

    int key1 = 1, equal_key1 = 11, key2 = 2;
    int value1 = 101, equal_value1 = 201, value2 = 102, value3 = 103;
    d7_hamt_bi_map empty;
    CHECK_STATUS(d7_hamt_bi_map_create(&key_policy, &value_policy, &empty));
    d7_hamt_bi_map one;
    CHECK_STATUS(d7_hamt_bi_map_add(&empty, &key1, &value1, &one));
    d7_hamt_bi_map two;
    CHECK_STATUS(d7_hamt_bi_map_add(&one, &key2, &value2, &two));

    d7_hamt_bi_map same;
    bool added = true;
    d7_hamt_bi_map_conflict conflict = D7_HAMT_BI_MAP_NO_CONFLICT;
    CHECK_STATUS(d7_hamt_bi_map_try_add(&two, &equal_key1, &value3, &same, &added, &conflict));
    CHECK(!added && conflict == D7_HAMT_BI_MAP_KEY_CONFLICT);
    CHECK(d7_hamt_bi_map_shares_roots(&two, &same));
    d7_hamt_bi_map_destroy(&same);
    CHECK_STATUS(d7_hamt_bi_map_try_add(&two, &value3, &equal_value1, &same, &added, &conflict));
    CHECK(!added && conflict == D7_HAMT_BI_MAP_VALUE_CONFLICT);
    d7_hamt_bi_map_destroy(&same);

    CHECK(d7_hamt_bi_map_set(&two, &equal_key1, &value2, &same) == D7_HAMT_DUPLICATE_VALUE);
    CHECK_STATUS(d7_hamt_bi_map_set(&two, &equal_key1, &equal_value1, &same));
    CHECK(d7_hamt_bi_map_shares_roots(&two, &same));
    d7_hamt_bi_map_destroy(&same);
    CHECK_STATUS(d7_hamt_bi_map_set(&two, &equal_key1, &value3, &same));
    const void *stored_key = NULL;
    CHECK(d7_hamt_bi_map_try_get_key(&same, &value3, &stored_key));
    CHECK(stored_key == &key1);
    CHECK(!d7_hamt_bi_map_contains_value(&same, &value1));
    CHECK(d7_hamt_bi_map_debug_validate(&same));

    d7_hamt_bi_map inverse;
    d7_hamt_bi_map round_trip;
    CHECK_STATUS(d7_hamt_bi_map_inverse(&same, &inverse));
    const void *stored_value = NULL;
    CHECK(d7_hamt_bi_map_try_get(&inverse, &value3, &stored_value));
    CHECK(stored_value == &key1);
    d7_hamt_bi_map inverse_same;
    CHECK_STATUS(d7_hamt_bi_map_set(&inverse, &value3, &equal_key1, &inverse_same));
    CHECK(d7_hamt_bi_map_shares_roots(&inverse, &inverse_same));
    CHECK(d7_hamt_bi_map_debug_validate(&inverse_same));
    CHECK_STATUS(d7_hamt_bi_map_inverse(&inverse, &round_trip));
    CHECK(d7_hamt_bi_map_shares_roots(&same, &round_trip));

    bool removed = false;
    const void *opposite = NULL;
    d7_hamt_bi_map removed_map;
    CHECK_STATUS(d7_hamt_bi_map_try_remove_value(&same, &value3, &removed_map, &removed, &opposite));
    CHECK(removed && opposite == &key1);
    CHECK(!d7_hamt_bi_map_contains_key(&removed_map, &key1));
    CHECK(d7_hamt_bi_map_debug_validate(&removed_map));

    d7_hamt_bi_map_destroy(&removed_map);
    d7_hamt_bi_map_destroy(&round_trip);
    d7_hamt_bi_map_destroy(&inverse_same);
    d7_hamt_bi_map_destroy(&inverse);
    d7_hamt_bi_map_destroy(&same);
    d7_hamt_bi_map_destroy(&two);
    d7_hamt_bi_map_destroy(&one);
    d7_hamt_bi_map_destroy(&empty);
}

static void nullable_presence_and_clear(void) {
    d7_hamt_set_policy key_policy = int_policy(int_hash);
    d7_hamt_set_policy value_policy = d7_hamt_set_policy_default();
    int key = 7;
    d7_hamt_bi_map map;
    CHECK_STATUS(d7_hamt_bi_map_create(&key_policy, &value_policy, &map));
    d7_hamt_bi_map with_null;
    CHECK_STATUS(d7_hamt_bi_map_add(&map, &key, NULL, &with_null));
    const void *value = (const void *)(uintptr_t)1;
    CHECK(d7_hamt_bi_map_try_get(&with_null, &key, &value));
    CHECK(value == NULL);
    bool removed = false;
    const void *opposite = (const void *)(uintptr_t)1;
    d7_hamt_bi_map empty;
    CHECK_STATUS(d7_hamt_bi_map_try_remove_key(&with_null, &key, &empty, &removed, &opposite));
    CHECK(removed && opposite == NULL);
    CHECK(d7_hamt_bi_map_is_empty(&empty));
    d7_hamt_bi_map same_empty;
    CHECK_STATUS(d7_hamt_bi_map_clear(&empty, &same_empty));
    CHECK(d7_hamt_bi_map_shares_roots(&empty, &same_empty));
    d7_hamt_bi_map_destroy(&same_empty);
    d7_hamt_bi_map_destroy(&empty);
    d7_hamt_bi_map_destroy(&with_null);
    d7_hamt_bi_map_destroy(&map);
}

static void deterministic_model_and_failure_atomicity(void) {
    for (int i = 0; i < 32; ++i) { keys[i] = i; values[i] = 100 + i; }
    d7_hamt_set_policy key_policy = int_policy(few_hash);
    d7_hamt_set_policy value_policy = int_policy(few_hash);
    d7_hamt_bi_map map;
    CHECK_STATUS(d7_hamt_bi_map_create(&key_policy, &value_policy, &map));
    int forward[32];
    int backward[32];
    for (int i = 0; i < 32; ++i) { forward[i] = -1; backward[i] = -1; }
    uint32_t state = 0xb1a4d00du;
    for (int step = 0; step < 2000; ++step) {
        state = state * 1664525u + 1013904223u;
        const int operation = (int)(state & 3u);
        state = state * 1664525u + 1013904223u;
        const int key = (int)(state & 31u);
        state = state * 1664525u + 1013904223u;
        const int value_index = (int)(state & 31u);
        d7_hamt_bi_map next;
        if (operation == 0) {
            bool added = false;
            d7_hamt_bi_map_conflict conflict;
            CHECK_STATUS(d7_hamt_bi_map_try_add(
                &map, &keys[key], &values[value_index], &next, &added, &conflict));
            const bool expected = forward[key] < 0 && backward[value_index] < 0;
            CHECK(added == expected);
            if (expected) { forward[key] = value_index; backward[value_index] = key; }
            d7_hamt_bi_map_destroy(&map); map = next;
        } else if (operation == 1) {
            if (backward[value_index] >= 0 && backward[value_index] != key) {
                CHECK(d7_hamt_bi_map_set(&map, &keys[key], &values[value_index], &next)
                    == D7_HAMT_DUPLICATE_VALUE);
            } else {
                CHECK_STATUS(d7_hamt_bi_map_set(&map, &keys[key], &values[value_index], &next));
                if (forward[key] >= 0) backward[forward[key]] = -1;
                forward[key] = value_index;
                backward[value_index] = key;
                d7_hamt_bi_map_destroy(&map); map = next;
            }
        } else if (operation == 2) {
            bool removed = false;
            CHECK_STATUS(d7_hamt_bi_map_try_remove_key(&map, &keys[key], &next, &removed, NULL));
            CHECK(removed == (forward[key] >= 0));
            if (forward[key] >= 0) { backward[forward[key]] = -1; forward[key] = -1; }
            d7_hamt_bi_map_destroy(&map); map = next;
        } else {
            bool removed = false;
            CHECK_STATUS(d7_hamt_bi_map_try_remove_value(
                &map, &values[value_index], &next, &removed, NULL));
            CHECK(removed == (backward[value_index] >= 0));
            if (backward[value_index] >= 0) { forward[backward[value_index]] = -1; backward[value_index] = -1; }
            d7_hamt_bi_map_destroy(&map); map = next;
        }
        CHECK(d7_hamt_bi_map_debug_validate(&map));
        size_t expected_count = 0;
        for (int i = 0; i < 32; ++i) {
            if (forward[i] >= 0) {
                ++expected_count;
                const void *actual = NULL;
                CHECK(d7_hamt_bi_map_try_get(&map, &keys[i], &actual));
                CHECK(actual == &values[forward[i]]);
            }
        }
        CHECK(d7_hamt_bi_map_count(&map) == expected_count);
    }

    d7_hamt_bi_map before = d7_hamt_bi_map_clone(&map);
    d7_hamt_bi_map output;
    memset(&output, 0, sizeof(output));
    d7_hamt_test_fail_allocations_after(0);
    const int extra_key = 99;
    const int extra_value = 999;
    CHECK(d7_hamt_bi_map_add(&map, &extra_key, &extra_value, &output) == D7_HAMT_OUT_OF_MEMORY);
    d7_hamt_test_reset_allocator();
    CHECK(output.policy_state == NULL);
    CHECK(d7_hamt_bi_map_shares_roots(&map, &before));
    CHECK(d7_hamt_bi_map_debug_validate(&map));
    d7_hamt_bi_map_destroy(&before);
    d7_hamt_bi_map_destroy(&map);
}

int main(void) {
    if (!d7_enter_headless_test_process()) return 1;
    strict_policies_representatives_and_inverse();
    nullable_presence_and_clear();
    deterministic_model_and_failure_atomicity();
    puts("persistent bimap tests passed");
    return 0;
}
