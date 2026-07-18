#include <tools/data_structures/finger_tree/persistent_interval_map.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define CHECK_STATUS(expression) CHECK((expression) == FT_STATUS_OK)

static int compare_int(const void* left, const void* right, void* context)
{
    (void)context;
    const int l = *(const int*)left;
    const int r = *(const int*)right;
    return (l > r) - (l < r);
}

static bool equal_int(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

typedef struct overlap_state {
    size_t count;
    int sum;
} overlap_state;

static void visit_overlap(
    const void* low,
    const void* high,
    const void* value,
    void* raw_context)
{
    (void)low;
    (void)high;
    overlap_state* state = (overlap_state*)raw_context;
    ++state->count;
    state->sum += *(const int*)value;
}

int main(void)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_persistent_interval_map map;
    CHECK_STATUS(ft_persistent_interval_map_init(
        &map, &int_type, &int_type, compare_int, NULL, equal_int, NULL));

    const int zero = 0;
    const int one = 1;
    const int two = 2;
    const int three = 3;
    const int four = 4;
    const int five = 5;
    const int six = 6;
    const int nine = 9;
    const int eleven = 11;
    const int twelve = 12;
    const int value_a = 10;
    const int value_b = 20;
    const int value_c = 30;
    const int value_d = 40;

    CHECK_STATUS(ft_persistent_interval_map_add(&map, &one, &five, &value_a, &map));
    CHECK_STATUS(ft_persistent_interval_map_add(&map, &one, &three, &value_b, &map));
    CHECK_STATUS(ft_persistent_interval_map_add(&map, &zero, &two, &value_c, &map));
    CHECK_STATUS(ft_persistent_interval_map_add(&map, &four, &nine, &value_d, &map));
    CHECK(ft_persistent_interval_map_size(&map) == 4u);
    CHECK(ft_persistent_interval_map_debug_validate(&map));

    int low = -1;
    int high = -1;
    int value = -1;
    CHECK_STATUS(ft_persistent_interval_map_entry_at(&map, 1u, &low, &high, &value));
    CHECK(low == 1 && high == 3 && value == 20);
    CHECK(ft_persistent_interval_map_add(&map, &one, &three, &value_a, &map)
        == FT_STATUS_ALREADY_EXISTS);

    ft_persistent_interval_map snapshot;
    CHECK_STATUS(ft_persistent_interval_map_copy(&map, &snapshot));
    CHECK_STATUS(ft_persistent_interval_map_set(&map, &one, &three, &value_a, &map));
    bool found = false;
    CHECK_STATUS(ft_persistent_interval_map_try_get(
        &map, &one, &three, &found, &value));
    CHECK(found && value == 10);
    CHECK_STATUS(ft_persistent_interval_map_try_get(
        &snapshot, &one, &three, &found, &value));
    CHECK(found && value == 20);

    CHECK_STATUS(ft_persistent_interval_map_try_find_overlap(
        &map, &six, &eleven, &found, &low, &high, &value));
    CHECK(found && low == 4 && high == 9 && value == 40);
    CHECK(ft_persistent_interval_map_count_overlaps(&map, &two, &six) == 4u);
    overlap_state overlaps = { 0u, 0 };
    CHECK_STATUS(ft_persistent_interval_map_visit_overlaps(
        &map, &two, &six, visit_overlap, &overlaps));
    CHECK(overlaps.count == 4u && overlaps.sum == 90);

    CHECK_STATUS(ft_persistent_interval_map_remove(&map, &one, &five, &map));
    CHECK(!ft_persistent_interval_map_contains_key(&map, &one, &five));
    CHECK(ft_persistent_interval_map_contains_key(&snapshot, &one, &five));
    CHECK(ft_persistent_interval_map_add(&map, &twelve, &eleven, &value_a, &map)
        == FT_STATUS_INVALID_ARGUMENT);
    CHECK(ft_persistent_interval_map_debug_validate(&map));

    ft_persistent_interval_map_cursor cursor;
    ft_persistent_interval_map_cursor retained;
    ft_persistent_interval_map_cursor overlap_cursor;
    ft_persistent_interval_map cursor_snapshot;
    const int cursor_value = 50;
    const int replacement_value = 41;
    CHECK_STATUS(ft_persistent_interval_map_get_cursor_at_key(
        &snapshot, &one, &five, &found, &cursor));
    CHECK(found);
    CHECK(ft_persistent_interval_map_cursor_position(&cursor) == 2u);
    CHECK_STATUS(ft_persistent_interval_map_cursor_try_peek_next(
        &cursor, &found, &low, &high, &value));
    CHECK(found && low == 1 && high == 5 && value == 10);
    CHECK_STATUS(ft_persistent_interval_map_cursor_copy(&cursor, &retained));
    ft_persistent_interval_map_dispose(&snapshot);

    CHECK_STATUS(ft_persistent_interval_map_cursor_insert(
        &cursor, &two, &six, &cursor_value, &cursor));
    CHECK(ft_persistent_interval_map_cursor_position(&cursor) == 4u);
    CHECK(ft_persistent_interval_map_cursor_size(&cursor) == 5u);
    CHECK_STATUS(ft_persistent_interval_map_cursor_try_peek_previous(
        &cursor, &found, &low, &high, &value));
    CHECK(found && low == 2 && high == 6 && value == 50);
    CHECK_STATUS(ft_persistent_interval_map_cursor_set_next(
        &cursor, &replacement_value, &cursor));
    CHECK_STATUS(ft_persistent_interval_map_cursor_try_peek_next(
        &cursor, &found, &low, &high, &value));
    CHECK(found && low == 4 && high == 9 && value == 41);
    CHECK_STATUS(ft_persistent_interval_map_cursor_snapshot(
        &cursor, &cursor_snapshot));

    CHECK_STATUS(ft_persistent_interval_map_find_containing_cursor(
        &cursor_snapshot, &three, &found, &overlap_cursor));
    CHECK(found);
    CHECK(ft_persistent_interval_map_cursor_position(&overlap_cursor) == 1u);
    CHECK_STATUS(ft_persistent_interval_map_cursor_seek_next_overlap(
        &overlap_cursor, &three, &three, &found, &overlap_cursor));
    CHECK(found);
    CHECK(ft_persistent_interval_map_cursor_position(&overlap_cursor) == 2u);
    CHECK_STATUS(ft_persistent_interval_map_cursor_try_peek_next(
        &overlap_cursor, &found, &low, &high, &value));
    CHECK(found && low == 1 && high == 5);

    CHECK_STATUS(ft_persistent_interval_map_cursor_delete_previous(
        &cursor, &cursor));
    CHECK(ft_persistent_interval_map_cursor_position(&cursor) == 3u);
    CHECK(ft_persistent_interval_map_cursor_size(&cursor) == 4u);
    CHECK(ft_persistent_interval_map_cursor_insert(
        &cursor, &one, &five, &value_a, &cursor) == FT_STATUS_ALREADY_EXISTS);
    CHECK(ft_persistent_interval_map_cursor_position(&cursor) == 3u);
    CHECK(ft_persistent_interval_map_cursor_size(&retained) == 4u);
    CHECK(ft_persistent_interval_map_cursor_seek_rank(
        &cursor, 5u, &cursor) == FT_STATUS_OUT_OF_RANGE);

    ft_persistent_interval_map_cursor_dispose(&overlap_cursor);
    ft_persistent_interval_map_cursor_dispose(&retained);
    ft_persistent_interval_map_cursor_dispose(&cursor);
    ft_persistent_interval_map_dispose(&cursor_snapshot);

    ft_persistent_interval_map_dispose(&map);
    puts("[PASS] persistent interval map");
    return EXIT_SUCCESS;
}
