#include <Tools/DataStructures/Hamt/persistent_hash_multimap.h>
#include <Tools/DataStructures/Hamt/persistent_relation.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define CHECK_STATUS(expression) CHECK((expression) == TDS_HAMT_OK)

typedef struct test_value {
    int identity;
    int representative;
} test_value;

static uint32_t value_hash(const void* raw, void* context)
{
    (void)context;
    return (uint32_t)((const test_value*)raw)->identity * UINT32_C(2654435761);
}

static bool value_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return ((const test_value*)left)->identity
        == ((const test_value*)right)->identity;
}

static void* value_retain(const void* raw, void* context)
{
    (void)context;
    test_value* copy = (test_value*)malloc(sizeof(*copy));
    if (copy != NULL) {
        *copy = *(const test_value*)raw;
    }
    return copy;
}

static void value_release(void* raw, void* context)
{
    (void)context;
    free(raw);
}

static tds_hamt_set_policy value_policy(void)
{
    tds_hamt_set_policy policy;
    policy.hash = value_hash;
    policy.equal = value_equal;
    policy.retain_item = value_retain;
    policy.release_item = value_release;
    policy.context = NULL;
    return policy;
}

static void test_multimap(void)
{
    const tds_hamt_set_policy policy = value_policy();
    const test_value key_a = { 1, 10 };
    const test_value key_equal = { 1, 99 };
    const test_value value_a = { 2, 20 };
    const test_value value_equal = { 2, 88 };
    const test_value value_b = { 3, 30 };

    tds_hamt_multimap map;
    CHECK_STATUS(tds_hamt_multimap_init(&map, &policy, &policy));
    CHECK_STATUS(tds_hamt_multimap_add(&map, &key_a, &value_a, &map));
    CHECK_STATUS(tds_hamt_multimap_add(&map, &key_equal, &value_equal, &map));
    CHECK_STATUS(tds_hamt_multimap_add(&map, &key_equal, &value_b, &map));
    CHECK(tds_hamt_multimap_key_count(&map) == 1u);
    CHECK(tds_hamt_multimap_pair_count(&map) == 2);
    CHECK(tds_hamt_multimap_debug_validate(&map));

    const void* actual_key = NULL;
    CHECK(tds_hamt_multimap_try_get_key(&map, &key_equal, &actual_key));
    CHECK(((const test_value*)actual_key)->representative == 10);
    const tds_hamt_set* values = NULL;
    CHECK(tds_hamt_multimap_try_get_values(&map, &key_a, &values));
    const void* actual_value = NULL;
    CHECK(tds_hamt_set_try_get_value(values, &value_equal, &actual_value));
    CHECK(((const test_value*)actual_value)->representative == 20);

    const void* root = tds_hamt_map_debug_root_identity(&map.groups);
    CHECK_STATUS(tds_hamt_multimap_add(&map, &key_equal, &value_equal, &map));
    CHECK(tds_hamt_map_debug_root_identity(&map.groups) == root);
    CHECK_STATUS(tds_hamt_multimap_remove(&map, &key_a, &value_a, &map));
    CHECK(tds_hamt_multimap_contains_key(&map, &key_a));
    CHECK_STATUS(tds_hamt_multimap_remove(&map, &key_a, &value_b, &map));
    CHECK(tds_hamt_multimap_empty(&map));
    tds_hamt_multimap_destroy(&map);
}

static void test_relation(void)
{
    const tds_hamt_set_policy policy = value_policy();
    const test_value left_a = { 1, 10 };
    const test_value left_equal = { 1, 90 };
    const test_value left_b = { 2, 20 };
    const test_value right_a = { 3, 30 };
    const test_value right_equal = { 3, 80 };
    const test_value right_b = { 4, 40 };

    tds_hamt_relation relation;
    CHECK_STATUS(tds_hamt_relation_init(&relation, &policy, &policy));
    CHECK_STATUS(tds_hamt_relation_add(&relation, &left_a, &right_a, &relation));
    CHECK_STATUS(tds_hamt_relation_add(&relation, &left_equal, &right_b, &relation));
    CHECK_STATUS(tds_hamt_relation_add(&relation, &left_b, &right_equal, &relation));
    CHECK(tds_hamt_relation_pair_count(&relation) == 3);
    CHECK(tds_hamt_relation_left_count(&relation) == 2u);
    CHECK(tds_hamt_relation_right_count(&relation) == 2u);
    CHECK(tds_hamt_relation_debug_validate(&relation));

    tds_hamt_relation inverse;
    CHECK_STATUS(tds_hamt_relation_inverse(&relation, &inverse));
    CHECK(tds_hamt_relation_contains(&inverse, &right_a, &left_a));
    CHECK(tds_hamt_map_shares_root(
        &inverse.forward.groups, &relation.reverse.groups));
    CHECK(tds_hamt_map_shares_root(
        &inverse.reverse.groups, &relation.forward.groups));

    tds_hamt_relation branch;
    CHECK_STATUS(tds_hamt_relation_remove_left(&relation, &left_equal, &branch));
    CHECK(tds_hamt_relation_pair_count(&branch) == 1);
    CHECK(tds_hamt_relation_pair_count(&relation) == 3);
    CHECK(tds_hamt_relation_debug_validate(&branch));
    CHECK_STATUS(tds_hamt_relation_remove_right(&branch, &right_equal, &branch));
    CHECK(tds_hamt_relation_empty(&branch));

    tds_hamt_relation_destroy(&branch);
    tds_hamt_relation_destroy(&inverse);
    tds_hamt_relation_destroy(&relation);
}

int main(void)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    test_multimap();
    puts("[PASS] persistent hash multimap");
    test_relation();
    puts("[PASS] persistent relation");
    puts("2 test(s) passed");
    return EXIT_SUCCESS;
}
