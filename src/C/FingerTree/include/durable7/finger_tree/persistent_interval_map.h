#ifndef DURABLE7_FINGER_TREE_PERSISTENT_INTERVAL_MAP_H
#define DURABLE7_FINGER_TREE_PERSISTENT_INTERVAL_MAP_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ft_interval_map_value_equal_fn)(
    const void* left,
    const void* right,
    void* context);
typedef void (*ft_interval_map_visit_fn)(
    const void* low,
    const void* high,
    const void* value,
    void* context);

struct ft_interval_map_context;

/* Unique payload-bearing closed intervals. The interval tree provides pruned
 * overlap navigation; the sorted map provides complete (low, high) keys and
 * payload lookup. Copy/move/dispose handles explicitly. */
typedef struct ft_persistent_interval_map {
    ft_interval_tree intervals;
    ft_sorted_map values;
    struct ft_interval_map_context* context;
} ft_persistent_interval_map;

/* Immutable lexicographic (low, high)-order root-plus-rank gap cursor. */
typedef struct ft_persistent_interval_map_cursor {
    ft_persistent_interval_map map;
    size_t position;
} ft_persistent_interval_map_cursor;

ft_status ft_persistent_interval_map_init(
    ft_persistent_interval_map* map,
    const ft_value_type* endpoint_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_endpoint,
    void* compare_context,
    ft_interval_map_value_equal_fn value_equal,
    void* value_equal_context);
ft_status ft_persistent_interval_map_copy(
    const ft_persistent_interval_map* source,
    ft_persistent_interval_map* destination);
void ft_persistent_interval_map_move(
    ft_persistent_interval_map* destination,
    ft_persistent_interval_map* source);
void ft_persistent_interval_map_dispose(ft_persistent_interval_map* map);

bool ft_persistent_interval_map_empty(const ft_persistent_interval_map* map);
size_t ft_persistent_interval_map_size(const ft_persistent_interval_map* map);
bool ft_persistent_interval_map_contains_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high);
ft_status ft_persistent_interval_map_try_get(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    void* value);
ft_status ft_persistent_interval_map_entry_at(
    const ft_persistent_interval_map* map,
    size_t index,
    void* low,
    void* high,
    void* value);

ft_status ft_persistent_interval_map_add(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result);
ft_status ft_persistent_interval_map_set(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result);
ft_status ft_persistent_interval_map_remove(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map* result);
ft_status ft_persistent_interval_map_clear(
    const ft_persistent_interval_map* map,
    ft_persistent_interval_map* result);

ft_status ft_persistent_interval_map_try_find_overlap(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high,
    void* value);
size_t ft_persistent_interval_map_count_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high);
ft_status ft_persistent_interval_map_visit(
    const ft_persistent_interval_map* map,
    ft_interval_map_visit_fn visitor,
    void* context);
ft_status ft_persistent_interval_map_visit_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    ft_interval_map_visit_fn visitor,
    void* context);

bool ft_persistent_interval_map_debug_validate(
    const ft_persistent_interval_map* map);

ft_status ft_persistent_interval_map_get_cursor(
    const ft_persistent_interval_map* map,
    size_t position,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_get_cursor_lower_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_get_cursor_upper_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_get_cursor_at_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_find_overlap_cursor(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_find_containing_cursor(
    const ft_persistent_interval_map* map,
    const void* point,
    bool* found,
    ft_persistent_interval_map_cursor* result);

ft_status ft_persistent_interval_map_cursor_copy(
    const ft_persistent_interval_map_cursor* source,
    ft_persistent_interval_map_cursor* destination);
void ft_persistent_interval_map_cursor_move(
    ft_persistent_interval_map_cursor* destination,
    ft_persistent_interval_map_cursor* source);
void ft_persistent_interval_map_cursor_dispose(ft_persistent_interval_map_cursor* cursor);
bool ft_persistent_interval_map_cursor_valid(const ft_persistent_interval_map_cursor* cursor);
bool ft_persistent_interval_map_cursor_empty(const ft_persistent_interval_map_cursor* cursor);
size_t ft_persistent_interval_map_cursor_size(const ft_persistent_interval_map_cursor* cursor);
size_t ft_persistent_interval_map_cursor_position(const ft_persistent_interval_map_cursor* cursor);
ft_status ft_persistent_interval_map_cursor_is_at_start(
    const ft_persistent_interval_map_cursor* cursor,
    bool* result);
ft_status ft_persistent_interval_map_cursor_is_at_end(
    const ft_persistent_interval_map_cursor* cursor,
    bool* result);
/* Present entries are copied into any non-null component outputs. */
ft_status ft_persistent_interval_map_cursor_try_peek_previous(
    const ft_persistent_interval_map_cursor* cursor,
    bool* found,
    void* low,
    void* high,
    void* value);
ft_status ft_persistent_interval_map_cursor_try_peek_next(
    const ft_persistent_interval_map_cursor* cursor,
    bool* found,
    void* low,
    void* high,
    void* value);
ft_status ft_persistent_interval_map_cursor_move_previous(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_move_next(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_seek_rank(
    const ft_persistent_interval_map_cursor* cursor,
    size_t position,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_seek_next_overlap(
    const ft_persistent_interval_map_cursor* cursor,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_insert(
    const ft_persistent_interval_map_cursor* cursor,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_set_item(
    const ft_persistent_interval_map_cursor* cursor,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_set_next(
    const ft_persistent_interval_map_cursor* cursor,
    const void* value,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_delete_previous(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_delete_next(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
ft_status ft_persistent_interval_map_cursor_snapshot(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map* result);

#ifdef __cplusplus
}
#endif

#endif
