#include <durable7/ordered/ordered_map.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expression); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define CHECK_STATUS(expression) CHECK((expression) == D7_ORDERED_OK)

static uint32_t hash_int(const void* item, void* context)
{
    (void)context;
    return (uint32_t)*(const int*)item * UINT32_C(2654435761);
}

static bool equal_int(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

static int compare_value_descending(
    const void* left_key,
    const void* left_value,
    const void* right_key,
    const void* right_value,
    void* context)
{
    (void)left_key;
    (void)right_key;
    (void)context;
    const int left = *(const int*)left_value;
    const int right = *(const int*)right_value;
    return (right > left) - (right < left);
}

static void check_entry(
    const d7_ordered_map* map,
    size_t index,
    int expected_key,
    int expected_value)
{
    const void* key = NULL;
    const void* value = NULL;
    CHECK_STATUS(d7_ordered_map_entry_at(map, index, &key, &value));
    CHECK(*(const int*)key == expected_key);
    CHECK(*(const int*)value == expected_value);
}

int main(void)
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    d7_ordered_map_policy policy;
    d7_ordered_map_policy_init(
        &policy,
        &int_type,
        &int_type,
        hash_int,
        equal_int,
        equal_int,
        NULL);
    d7_ordered_map map;
    CHECK_STATUS(d7_ordered_map_init(&map, &policy));

    const int one = 1;
    const int two = 2;
    const int three = 3;
    const int four = 4;
    const int ten = 10;
    const int twenty = 20;
    const int thirty = 30;
    const int forty = 40;
    const int twenty_five = 25;

    d7_ordered_map next;
    CHECK_STATUS(d7_ordered_map_add(&map, &one, &ten, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    CHECK_STATUS(d7_ordered_map_add(&map, &two, &twenty, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    CHECK_STATUS(d7_ordered_map_add_first(&map, &three, &thirty, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    CHECK_STATUS(d7_ordered_map_insert(&map, 1u, &four, &forty, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    CHECK(d7_ordered_map_size(&map) == 4u);
    check_entry(&map, 0u, 3, 30);
    check_entry(&map, 1u, 4, 40);
    check_entry(&map, 2u, 1, 10);
    check_entry(&map, 3u, 2, 20);
    CHECK(d7_ordered_map_debug_validate(&map));

    d7_ordered_map snapshot;
    CHECK_STATUS(d7_ordered_map_clone(&map, &snapshot));
    CHECK_STATUS(d7_ordered_map_set(&map, &two, &twenty_five, &next));
    CHECK(d7_ordered_map_debug_shares_order(&map, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    const void* actual_key = NULL;
    const void* value = NULL;
    CHECK(d7_ordered_map_try_get(
        &map, &two, &actual_key, &value));
    CHECK(*(const int*)actual_key == 2 && *(const int*)value == 25);
    CHECK(d7_ordered_map_try_get(
        &snapshot, &two, &actual_key, &value));
    CHECK(*(const int*)value == 20);

    CHECK_STATUS(d7_ordered_map_move_to_first(&map, &one, &next));
    CHECK(d7_ordered_map_debug_shares_values(&map, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    check_entry(&map, 0u, 1, 10);

    CHECK_STATUS(d7_ordered_map_sort(
        &map, compare_value_descending, NULL, &next));
    d7_ordered_map_destroy(&map);
    d7_ordered_map_move(&map, &next);
    check_entry(&map, 0u, 4, 40);
    check_entry(&map, 1u, 3, 30);
    check_entry(&map, 2u, 2, 25);
    check_entry(&map, 3u, 1, 10);

    CHECK_STATUS(d7_ordered_map_get_range(&map, 1u, 2u, &next));
    CHECK(d7_ordered_map_size(&next) == 2u);
    check_entry(&next, 0u, 3, 30);
    check_entry(&next, 1u, 2, 25);
    CHECK(d7_ordered_map_debug_validate(&next));
    d7_ordered_map_destroy(&next);

    bool added = true;
    CHECK_STATUS(d7_ordered_map_try_add(
        &map, &two, &twenty, &added, &next));
    CHECK(!added);
    CHECK(d7_ordered_map_debug_shares_order(&map, &next));
    d7_ordered_map_destroy(&next);

    bool removed = false;
    CHECK_STATUS(d7_ordered_map_try_remove(&map, &four, &removed, &next));
    CHECK(removed && !d7_ordered_map_contains_key(&next, &four));
    CHECK(d7_ordered_map_debug_validate(&next));
    d7_ordered_map_destroy(&next);
    CHECK(d7_ordered_map_contains_key(&snapshot, &four));

    d7_ordered_map_destroy(&snapshot);
    d7_ordered_map_destroy(&map);
    puts("[PASS] persistent ordered map");
    return EXIT_SUCCESS;
}
