/*
 * Tests for the insertion-ordered collections' cursors.
 */

#include <durable7/ordered/ordered_cursor.h>
#include <durable7/test_support/headless_test_process.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            (void)fprintf(stderr, "%s:%d: requirement failed: %s\n", \
                __FILE__, __LINE__, #condition); \
            ++failures; \
            return; \
        } \
    } while (0)

#define REQUIRE_STATUS(expression) REQUIRE((expression) == D7_ORDERED_OK)

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

static void init_int_policy(d7_ordered_policy* policy)
{
    ft_value_type type;
    ft_value_type_init(&type, sizeof(int));
    d7_ordered_policy_init(policy, &type, hash_int, equal_int, NULL);
}

static void init_int_map_policy(d7_ordered_map_policy* policy)
{
    ft_value_type type;
    ft_value_type_init(&type, sizeof(int));
    d7_ordered_map_policy_init(
        policy, &type, &type, hash_int, equal_int, equal_int, NULL);
}

static uint32_t hash_double(const void* item, void* context)
{
    (void)context;
    double value = *(const double*)item;
    uint64_t bits = 0u;
    (void)memcpy(&bits, &value, sizeof(bits));
    return (uint32_t)(bits ^ (bits >> 32));
}

static bool equal_double(const void* left, const void* right, void* context)
{
    (void)context;
    /* IEEE == semantics: a NaN compares unequal to itself, an idiomatic and
     * legitimate value policy. */
    return *(const double*)left == *(const double*)right;
}

static void init_double_policy(d7_ordered_policy* policy)
{
    ft_value_type type;
    ft_value_type_init(&type, sizeof(double));
    d7_ordered_policy_init(policy, &type, hash_double, equal_double, NULL);
}

typedef struct double_pair_buffer {
    int keys[16];
    double values[16];
    size_t count;
} double_pair_buffer;

static void collect_double_pair(const void* key, const void* value, void* raw_buffer)
{
    double_pair_buffer* buffer = (double_pair_buffer*)raw_buffer;
    if (buffer->count < 16u) {
        buffer->keys[buffer->count] = *(const int*)key;
        buffer->values[buffer->count] = *(const double*)value;
        ++buffer->count;
    }
}

static bool set_equals(const d7_ordered_set* set, const int* expected, size_t count)
{
    if (d7_ordered_set_size(set) != count) {
        return false;
    }
    for (size_t index = 0u; index != count; ++index) {
        const void* item = NULL;
        if (d7_ordered_set_at(set, index, &item) != D7_ORDERED_OK
            || *(const int*)item != expected[index]) {
            return false;
        }
    }
    return true;
}

static bool map_equals(
    const d7_ordered_map* map,
    const int* keys,
    const int* values,
    size_t count)
{
    if (d7_ordered_map_size(map) != count) {
        return false;
    }
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        const void* value = NULL;
        if (d7_ordered_map_entry_at(map, index, &key, &value) != D7_ORDERED_OK
            || *(const int*)key != keys[index]
            || *(const int*)value != values[index]) {
            return false;
        }
    }
    return true;
}

typedef struct pair_buffer {
    int keys[16];
    int values[16];
    size_t count;
} pair_buffer;

static void collect_pair(const void* key, const void* value, void* raw_buffer)
{
    pair_buffer* buffer = (pair_buffer*)raw_buffer;
    if (buffer->count < 16u) {
        buffer->keys[buffer->count] = *(const int*)key;
        buffer->values[buffer->count] = *(const int*)value;
        ++buffer->count;
    }
}

static bool multimap_equals(
    const d7_ordered_multimap* map,
    const int* keys,
    const int* values,
    size_t count)
{
    pair_buffer buffer;
    (void)memset(&buffer, 0, sizeof(buffer));
    return d7_ordered_multimap_visit(map, collect_pair, &buffer) == D7_ORDERED_OK
        && buffer.count == count
        && memcmp(buffer.keys, keys, count * sizeof(*keys)) == 0
        && memcmp(buffer.values, values, count * sizeof(*values)) == 0;
}

static void test_ordered_set_cursor(void)
{
    d7_ordered_policy policy;
    init_int_policy(&policy);
    const int items[] = { 1, 2, 3 };
    d7_ordered_set source;
    REQUIRE_STATUS(d7_ordered_set_from_array(&source, &policy, items, 3u));

    d7_ordered_set_cursor cursor;
    REQUIRE_STATUS(d7_ordered_set_get_cursor(&source, 1u, &cursor));
    bool found = false;
    const void* item = NULL;
    REQUIRE_STATUS(d7_ordered_set_cursor_try_peek_previous(&cursor, &found, &item));
    REQUIRE(found && *(const int*)item == 1);
    REQUIRE_STATUS(d7_ordered_set_cursor_try_peek_next(&cursor, &found, &item));
    REQUIRE(found && *(const int*)item == 2);

    const int inserted_item = 9;
    d7_ordered_set_cursor inserted;
    REQUIRE_STATUS(d7_ordered_set_cursor_insert(&cursor, &inserted_item, &inserted));
    REQUIRE(d7_ordered_set_cursor_position(&inserted) == 2u);
    const int inserted_expected[] = { 1, 9, 2, 3 };
    d7_ordered_set snapshot;
    REQUIRE_STATUS(d7_ordered_set_cursor_snapshot(&inserted, &snapshot));
    REQUIRE(set_equals(&snapshot, inserted_expected, 4u));
    d7_ordered_set_destroy(&snapshot);

    const int duplicate_item = 2;
    bool was_inserted = true;
    d7_ordered_set_cursor duplicate;
    REQUIRE_STATUS(d7_ordered_set_cursor_try_insert(
        &inserted, &duplicate_item, &was_inserted, &duplicate));
    REQUIRE(!was_inserted && d7_ordered_set_cursor_position(&duplicate) == 2u);
    REQUIRE(d7_ordered_set_debug_shares_index(&inserted.set, &duplicate.set));

    d7_ordered_set_cursor deleted;
    REQUIRE_STATUS(d7_ordered_set_cursor_delete_previous(&inserted, &deleted));
    REQUIRE(d7_ordered_set_cursor_position(&deleted) == 1u);
    REQUIRE_STATUS(d7_ordered_set_cursor_snapshot(&deleted, &snapshot));
    REQUIRE(set_equals(&snapshot, items, 3u));
    d7_ordered_set_destroy(&snapshot);

    d7_ordered_set_cursor_destroy(&deleted);
    REQUIRE_STATUS(d7_ordered_set_get_cursor_at_item(
        &source, &duplicate_item, &found, &deleted));
    REQUIRE(found && d7_ordered_set_cursor_position(&deleted) == 1u);
    REQUIRE_STATUS(d7_ordered_set_cursor_seek(&deleted, 2u, &deleted));
    REQUIRE(d7_ordered_set_cursor_position(&deleted) == 2u);

    d7_ordered_set_cursor_destroy(&deleted);
    d7_ordered_set_cursor_destroy(&duplicate);
    d7_ordered_set_cursor_destroy(&inserted);
    d7_ordered_set_cursor_destroy(&cursor);
    d7_ordered_set_destroy(&source);
}

static void replace_map(d7_ordered_map* current, d7_ordered_map* next)
{
    d7_ordered_map_destroy(current);
    d7_ordered_map_move(current, next);
}

static void test_ordered_map_cursor(void)
{
    d7_ordered_map_policy policy;
    init_int_map_policy(&policy);
    d7_ordered_map source;
    REQUIRE_STATUS(d7_ordered_map_init(&source, &policy));
    for (int key = 1; key <= 3; ++key) {
        const int value = key;
        d7_ordered_map next;
        REQUIRE_STATUS(d7_ordered_map_add(&source, &key, &value, &next));
        replace_map(&source, &next);
    }

    d7_ordered_map_cursor cursor;
    REQUIRE_STATUS(d7_ordered_map_get_cursor(&source, 1u, &cursor));
    const int key = 9;
    const int value = 90;
    d7_ordered_map_cursor inserted;
    REQUIRE_STATUS(d7_ordered_map_cursor_insert(&cursor, &key, &value, &inserted));
    const int updated_value = 20;
    d7_ordered_map_cursor updated;
    REQUIRE_STATUS(d7_ordered_map_cursor_set_next_value(
        &inserted, &updated_value, &updated));
    const int edit_keys[] = { 1, 9, 2, 3 };
    const int edit_values[] = { 1, 90, 20, 3 };
    d7_ordered_map snapshot;
    REQUIRE_STATUS(d7_ordered_map_cursor_snapshot(&updated, &snapshot));
    REQUIRE(map_equals(&snapshot, edit_keys, edit_values, 4u));
    d7_ordered_map_destroy(&snapshot);

    const int duplicate_key = 2;
    const int ignored_value = 200;
    bool was_inserted = true;
    d7_ordered_map_cursor duplicate;
    REQUIRE_STATUS(d7_ordered_map_cursor_try_insert(
        &updated, &duplicate_key, &ignored_value, &was_inserted, &duplicate));
    REQUIRE(!was_inserted && d7_ordered_map_cursor_position(&duplicate) == 2u);

    d7_ordered_map_cursor without_previous;
    REQUIRE_STATUS(d7_ordered_map_cursor_delete_previous(
        &updated, &without_previous));
    d7_ordered_map_cursor deleted;
    REQUIRE_STATUS(d7_ordered_map_cursor_delete_next(
        &without_previous, &deleted));
    const int deleted_keys[] = { 1, 3 };
    const int deleted_values[] = { 1, 3 };
    REQUIRE_STATUS(d7_ordered_map_cursor_snapshot(&deleted, &snapshot));
    REQUIRE(map_equals(&snapshot, deleted_keys, deleted_values, 2u));
    d7_ordered_map_destroy(&snapshot);
    const int source_keys[] = { 1, 2, 3 };
    const int source_values[] = { 1, 2, 3 };
    REQUIRE(map_equals(&cursor.map, source_keys, source_values, 3u));

    d7_ordered_map_cursor_destroy(&deleted);
    d7_ordered_map_cursor_destroy(&without_previous);
    d7_ordered_map_cursor_destroy(&duplicate);
    d7_ordered_map_cursor_destroy(&updated);
    d7_ordered_map_cursor_destroy(&inserted);
    d7_ordered_map_cursor_destroy(&cursor);
    d7_ordered_map_destroy(&source);
}

static void replace_multimap(
    d7_ordered_multimap* current,
    d7_ordered_multimap* next)
{
    d7_ordered_multimap_destroy(current);
    d7_ordered_multimap_move(current, next);
}

static void test_ordered_multimap_cursor(void)
{
    d7_ordered_policy policy;
    init_int_policy(&policy);
    d7_ordered_multimap source;
    REQUIRE_STATUS(d7_ordered_multimap_init(&source, &policy, &policy));
    const int keys[] = { 2, 1, 2, 3 };
    const int values[] = { 2, 9, 1, 7 };
    for (size_t index = 0u; index != 4u; ++index) {
        d7_ordered_multimap next;
        REQUIRE_STATUS(d7_ordered_multimap_add(
            &source, &keys[index], &values[index], &next));
        replace_multimap(&source, &next);
    }
    const int grouped_keys[] = { 2, 2, 1, 3 };
    const int grouped_values[] = { 2, 1, 9, 7 };
    REQUIRE(multimap_equals(&source, grouped_keys, grouped_values, 4u));

    const int find_key = 2;
    const int find_value = 1;
    bool found = false;
    d7_ordered_multimap_cursor cursor;
    REQUIRE_STATUS(d7_ordered_multimap_get_cursor_at_pair(
        &source, &find_key, &find_value, &found, &cursor));
    REQUIRE(found && d7_ordered_multimap_cursor_position(&cursor) == 1);
    const int added_value = 3;
    d7_ordered_multimap_cursor added;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_add(
        &cursor, &find_key, &added_value, &added));
    REQUIRE(d7_ordered_multimap_cursor_position(&added) == 3);

    bool was_inserted = true;
    d7_ordered_multimap_cursor duplicate;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_try_add(
        &added, &find_key, &added_value, &was_inserted, &duplicate));
    REQUIRE(!was_inserted && d7_ordered_multimap_cursor_position(&duplicate) == 3);
    REQUIRE(d7_ordered_multimap_debug_shares_groups(&added.map, &duplicate.map));

    d7_ordered_multimap_cursor without_previous;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_delete_previous(
        &added, &without_previous));
    d7_ordered_multimap_cursor deleted;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_delete_next(
        &without_previous, &deleted));
    const int deleted_keys[] = { 2, 2, 3 };
    const int deleted_values[] = { 2, 1, 7 };
    d7_ordered_multimap snapshot;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_snapshot(&deleted, &snapshot));
    REQUIRE(multimap_equals(&snapshot, deleted_keys, deleted_values, 3u));
    d7_ordered_multimap_destroy(&snapshot);
    REQUIRE(d7_ordered_multimap_cursor_position(&deleted) == 2);

    const int group_key = 1;
    d7_ordered_multimap_cursor group;
    REQUIRE_STATUS(d7_ordered_multimap_get_cursor_at_group(
        &source, &group_key, &found, &group));
    REQUIRE(found && d7_ordered_multimap_cursor_position(&group) == 2);
    const void* next_key = NULL;
    const void* next_value = NULL;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_try_peek_next(
        &deleted, &found, &next_key, &next_value));
    REQUIRE(found && *(const int*)next_key == 3 && *(const int*)next_value == 7);
    REQUIRE(multimap_equals(&cursor.map, grouped_keys, grouped_values, 4u));

    d7_ordered_multimap_cursor_destroy(&group);
    d7_ordered_multimap_cursor_destroy(&deleted);
    d7_ordered_multimap_cursor_destroy(&without_previous);
    d7_ordered_multimap_cursor_destroy(&duplicate);
    d7_ordered_multimap_cursor_destroy(&added);
    d7_ordered_multimap_cursor_destroy(&cursor);
    d7_ordered_multimap_destroy(&source);
}

static void test_ordered_multimap_cursor_non_reflexive_value(void)
{
    d7_ordered_policy key_policy;
    init_int_policy(&key_policy);
    d7_ordered_policy value_policy;
    init_double_policy(&value_policy);
    d7_ordered_multimap source;
    REQUIRE_STATUS(d7_ordered_multimap_init(&source, &key_policy, &value_policy));

    const int build_keys[] = { 1, 1, 2 };
    const double build_values[] = { 1.0, 2.0, 3.0 };
    for (size_t index = 0u; index != 3u; ++index) {
        d7_ordered_multimap next;
        REQUIRE_STATUS(d7_ordered_multimap_add(
            &source, &build_keys[index], &build_values[index], &next));
        replace_multimap(&source, &next);
    }
    REQUIRE(d7_ordered_multimap_pair_count(&source) == 3);

    d7_ordered_multimap_cursor cursor;
    REQUIRE_STATUS(d7_ordered_multimap_get_cursor(&source, 0, &cursor));

    /* Cursor-add a non-reflexive NaN into key group 1; must not spuriously fail,
     * and the post-insert gap is derived from the group end, not a value scan. */
    const int nan_key = 1;
    const double nan_value = NAN;
    d7_ordered_multimap_cursor added;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_add(
        &cursor, &nan_key, &nan_value, &added));
    /* Group 1 now spans ranks 0,1,2; the post-insert gap is the group end = 3. */
    REQUIRE(d7_ordered_multimap_cursor_position(&added) == 3);
    REQUIRE(d7_ordered_multimap_cursor_count(&added) == 4);

    /* The NaN pair is stored and reachable by rank. */
    d7_ordered_multimap_cursor at_gap;
    REQUIRE_STATUS(d7_ordered_multimap_get_cursor(&added.map, 3, &at_gap));
    bool found = false;
    const void* peek_key = NULL;
    const void* peek_value = NULL;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_try_peek_previous(
        &at_gap, &found, &peek_key, &peek_value));
    REQUIRE(found && *(const int*)peek_key == 1 && isnan(*(const double*)peek_value));

    /* Deleting the peeked NaN pair (rank 2) must be a real success, never a false
     * one. The C multimap locates the peeked value by its stored pointer, so the
     * removal genuinely happens: the pair count drops by one and the NaN is gone.
     * The delete guard rejects any remove that leaves the pair count unchanged, so
     * a passing status here is proof that a pair was actually removed rather than a
     * no-op published at a shifted gap. */
    d7_ordered_multimap_cursor deleted;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_delete_previous(&at_gap, &deleted));
    REQUIRE(d7_ordered_multimap_cursor_position(&deleted) == 2);
    REQUIRE(d7_ordered_multimap_cursor_count(&deleted) == 3);

    d7_ordered_multimap deleted_snapshot;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_snapshot(&deleted, &deleted_snapshot));
    double_pair_buffer deleted_buffer;
    (void)memset(&deleted_buffer, 0, sizeof(deleted_buffer));
    REQUIRE_STATUS(d7_ordered_multimap_visit(
        &deleted_snapshot, collect_double_pair, &deleted_buffer));
    REQUIRE(deleted_buffer.count == 3u);
    for (size_t index = 0u; index != deleted_buffer.count; ++index) {
        REQUIRE(!isnan(deleted_buffer.values[index]));
    }
    const int post_delete_keys[] = { 1, 1, 2 };
    const double post_delete_values[] = { 1.0, 2.0, 3.0 };
    REQUIRE(memcmp(
        deleted_buffer.keys, post_delete_keys, sizeof(post_delete_keys)) == 0);
    for (size_t index = 0u; index != 3u; ++index) {
        REQUIRE(deleted_buffer.values[index] == post_delete_values[index]);
    }
    d7_ordered_multimap_destroy(&deleted_snapshot);

    /* A reflexive value still inserts at the group end and deletes with the right gap. */
    const int reflexive_key = 2;
    const double reflexive_value = 4.0;
    d7_ordered_multimap_cursor reflexive_added;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_add(
        &cursor, &reflexive_key, &reflexive_value, &reflexive_added));
    REQUIRE(d7_ordered_multimap_cursor_position(&reflexive_added) == 4);
    REQUIRE(d7_ordered_multimap_cursor_count(&reflexive_added) == 4);

    d7_ordered_multimap_cursor reflexive_deleted;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_delete_previous(
        &reflexive_added, &reflexive_deleted));
    REQUIRE(d7_ordered_multimap_cursor_position(&reflexive_deleted) == 3);
    REQUIRE(d7_ordered_multimap_cursor_count(&reflexive_deleted) == 3);

    /* Contents after the reflexive delete match the original build order. */
    d7_ordered_multimap snapshot;
    REQUIRE_STATUS(d7_ordered_multimap_cursor_snapshot(&reflexive_deleted, &snapshot));
    double_pair_buffer buffer;
    (void)memset(&buffer, 0, sizeof(buffer));
    REQUIRE_STATUS(d7_ordered_multimap_visit(&snapshot, collect_double_pair, &buffer));
    REQUIRE(buffer.count == 3u);
    const int expected_keys[] = { 1, 1, 2 };
    const double expected_values[] = { 1.0, 2.0, 3.0 };
    REQUIRE(memcmp(buffer.keys, expected_keys, sizeof(expected_keys)) == 0);
    for (size_t index = 0u; index != 3u; ++index) {
        REQUIRE(buffer.values[index] == expected_values[index]);
    }
    d7_ordered_multimap_destroy(&snapshot);

    d7_ordered_multimap_cursor_destroy(&reflexive_deleted);
    d7_ordered_multimap_cursor_destroy(&reflexive_added);
    d7_ordered_multimap_cursor_destroy(&deleted);
    d7_ordered_multimap_cursor_destroy(&at_gap);
    d7_ordered_multimap_cursor_destroy(&added);
    d7_ordered_multimap_cursor_destroy(&cursor);
    d7_ordered_multimap_destroy(&source);
}

static void run_test(const char* name, void (*test)(void))
{
    const int before = failures;
    test();
    if (failures == before) {
        (void)printf("[pass] %s\n", name);
    }
}

int main(void)
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    run_test("ordered set cursor", test_ordered_set_cursor);
    run_test("ordered map cursor", test_ordered_map_cursor);
    run_test("ordered multimap cursor", test_ordered_multimap_cursor);
    run_test("ordered multimap cursor non-reflexive value",
        test_ordered_multimap_cursor_non_reflexive_value);
    if (failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C Ordered cursor tests passed\n");
    return EXIT_SUCCESS;
}
