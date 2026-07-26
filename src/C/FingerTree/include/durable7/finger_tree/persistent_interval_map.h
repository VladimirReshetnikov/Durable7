/*
 * A persistent map keyed by closed intervals, with overlap and containment queries.
 *
 * Each subtree caches the maximum high endpoint below it, so a query visits only the subtrees that
 * can still hold a match. Every operation returns a new version and leaves its inputs valid,
 * sharing unchanged structure, so an edit copies a path rather than the whole collection.
 */

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

/* Initializes the map in place. */
ft_status ft_persistent_interval_map_init(
    ft_persistent_interval_map* map,
    const ft_value_type* endpoint_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_endpoint,
    void* compare_context,
    ft_interval_map_value_equal_fn value_equal,
    void* value_equal_context);
/* Initializes a second handle on the same map version, taking a reference rather than copying. */
ft_status ft_persistent_interval_map_copy(
    const ft_persistent_interval_map* source,
    ft_persistent_interval_map* destination);
/* Relocates an initialized map into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_persistent_interval_map_move(
    ft_persistent_interval_map* destination,
    ft_persistent_interval_map* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_persistent_interval_map_dispose(ft_persistent_interval_map* map);

/* Whether the map holds no entries. */
bool ft_persistent_interval_map_empty(const ft_persistent_interval_map* map);
/* Number of entries in the map. */
size_t ft_persistent_interval_map_size(const ft_persistent_interval_map* map);
/* Whether the key is present. */
bool ft_persistent_interval_map_contains_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high);
/* Reads the value stored for the key, reporting whether it was present. */
ft_status ft_persistent_interval_map_try_get(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    void* value);
/* Reads the entry at the given rank. */
ft_status ft_persistent_interval_map_entry_at(
    const ft_persistent_interval_map* map,
    size_t index,
    void* low,
    void* high,
    void* value);

/* Produces a map containing the given entry. */
ft_status ft_persistent_interval_map_add(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result);
/* Produces a map with the key bound to the value, adding or replacing as needed. */
ft_status ft_persistent_interval_map_set(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result);
/* Produces a map without that entry. */
ft_status ft_persistent_interval_map_remove(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map* result);
/* Produces an empty map retaining the same policies. */
ft_status ft_persistent_interval_map_clear(
    const ft_persistent_interval_map* map,
    ft_persistent_interval_map* result);

/* Finds an entry whose interval overlaps the probe, reporting whether one exists. */
ft_status ft_persistent_interval_map_try_find_overlap(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high,
    void* value);
/* How many entries' intervals overlap the probe. */
size_t ft_persistent_interval_map_count_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high);
/* Calls the visitor once per entry, in the map's own order. */
ft_status ft_persistent_interval_map_visit(
    const ft_persistent_interval_map* map,
    ft_interval_map_visit_fn visitor,
    void* context);
/* Calls the visitor once per interval overlapping the probe. */
ft_status ft_persistent_interval_map_visit_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    ft_interval_map_visit_fn visitor,
    void* context);

/* Checks the map's structural invariants. For tests and diagnostics. */
bool ft_persistent_interval_map_debug_validate(
    const ft_persistent_interval_map* map);

/* Initializes a cursor at the given gap of the map. */
ft_status ft_persistent_interval_map_get_cursor(
    const ft_persistent_interval_map* map,
    size_t position,
    ft_persistent_interval_map_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_persistent_interval_map_get_cursor_lower_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_persistent_interval_map_get_cursor_upper_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map_cursor* result);
/* Initializes a cursor at the key, reporting whether it was actually present; on a miss the cursor
 * sits at the insertion point. */
ft_status ft_persistent_interval_map_get_cursor_at_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    ft_persistent_interval_map_cursor* result);
/* Initializes a cursor at the first entry overlapping the probe. */
ft_status ft_persistent_interval_map_find_overlap_cursor(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result);
/* Initializes a cursor at the first entry containing the point. */
ft_status ft_persistent_interval_map_find_containing_cursor(
    const ft_persistent_interval_map* map,
    const void* point,
    bool* found,
    ft_persistent_interval_map_cursor* result);

/* Initializes a second cursor at the same position on the same version. */
ft_status ft_persistent_interval_map_cursor_copy(
    const ft_persistent_interval_map_cursor* source,
    ft_persistent_interval_map_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_persistent_interval_map_cursor_move(
    ft_persistent_interval_map_cursor* destination,
    ft_persistent_interval_map_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_persistent_interval_map_cursor_dispose(ft_persistent_interval_map_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_persistent_interval_map_cursor_valid(const ft_persistent_interval_map_cursor* cursor);
/* Whether the map version the cursor is positioned in holds no entries. */
bool ft_persistent_interval_map_cursor_empty(const ft_persistent_interval_map_cursor* cursor);
/* Number of entries in the map version the cursor is positioned in. */
size_t ft_persistent_interval_map_cursor_size(const ft_persistent_interval_map_cursor* cursor);
/* The cursor's gap position. */
size_t ft_persistent_interval_map_cursor_position(const ft_persistent_interval_map_cursor* cursor);
/* Whether the gap precedes the first entry. */
ft_status ft_persistent_interval_map_cursor_is_at_start(
    const ft_persistent_interval_map_cursor* cursor,
    bool* result);
/* Whether the gap follows the last entry. */
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
/* Reads the entry immediately after the gap, reporting whether one exists. */
ft_status ft_persistent_interval_map_cursor_try_peek_next(
    const ft_persistent_interval_map_cursor* cursor,
    bool* found,
    void* low,
    void* high,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_persistent_interval_map_cursor_move_previous(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_persistent_interval_map_cursor_move_next(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_persistent_interval_map_cursor_seek_rank(
    const ft_persistent_interval_map_cursor* cursor,
    size_t position,
    ft_persistent_interval_map_cursor* result);
/* Moves the cursor to the next entry overlapping the probe. */
ft_status ft_persistent_interval_map_cursor_seek_next_overlap(
    const ft_persistent_interval_map_cursor* cursor,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_persistent_interval_map_cursor_insert(
    const ft_persistent_interval_map_cursor* cursor,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map_cursor* result);
/* Produces a map with the key bound to the value. */
ft_status ft_persistent_interval_map_cursor_set_item(
    const ft_persistent_interval_map_cursor* cursor,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map_cursor* result);
/* Replaces the entry after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_persistent_interval_map_cursor_set_next(
    const ft_persistent_interval_map_cursor* cursor,
    const void* value,
    ft_persistent_interval_map_cursor* result);
/* Removes the entry before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_persistent_interval_map_cursor_delete_previous(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
/* Removes the entry after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_persistent_interval_map_cursor_delete_next(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result);
/* Initializes a handle on the map version this cursor is positioned in. */
ft_status ft_persistent_interval_map_cursor_snapshot(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map* result);

#ifdef __cplusplus
}
#endif

#endif
