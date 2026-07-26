/*
 * Gap cursors over the insertion-ordered collections.
 *
 * A cursor holds one exact version and a gap position in 0..count. Moving it produces a cursor
 * rather than mutating one, and an edit through a cursor produces a new version the returned cursor
 * is positioned in, so a cursor can never observe a version it did not name.
 */

#ifndef DURABLE7_ORDERED_C_ORDERED_CURSOR_H
#define DURABLE7_ORDERED_C_ORDERED_CURSOR_H

#include <durable7/ordered/ordered_multimap.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cursors own an exact persistent snapshot and a gap position. Initialize only
 * through a factory or clone, move to transfer ownership, and destroy every
 * initialized cursor. Distinct result cursors must be uninitialized; exact
 * source/result aliasing is supported and publishes only after success. */
typedef struct d7_ordered_set_cursor {
    d7_ordered_set set;
    size_t position;
} d7_ordered_set_cursor;

typedef struct d7_ordered_map_cursor {
    d7_ordered_map map;
    size_t position;
} d7_ordered_map_cursor;

typedef struct d7_ordered_multimap_cursor {
    d7_ordered_multimap map;
    int64_t position;
} d7_ordered_multimap_cursor;

/* Initializes a cursor at the given gap of the set. */
d7_ordered_status d7_ordered_set_get_cursor(
    const d7_ordered_set* set,
    size_t position,
    d7_ordered_set_cursor* result);
/* Initializes a cursor at the element, reporting whether it was actually present; on a miss the
 * cursor sits at the insertion point. */
d7_ordered_status d7_ordered_set_get_cursor_at_item(
    const d7_ordered_set* set,
    const void* equal_item,
    bool* found,
    d7_ordered_set_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
d7_ordered_status d7_ordered_set_cursor_clone(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_ordered_set_cursor_move(
    d7_ordered_set_cursor* destination,
    d7_ordered_set_cursor* source);
/* Releases the cursor's reference on its version. */
void d7_ordered_set_cursor_destroy(d7_ordered_set_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool d7_ordered_set_cursor_valid(const d7_ordered_set_cursor* cursor);
/* Number of elements in the set version the cursor is positioned in. */
size_t d7_ordered_set_cursor_count(const d7_ordered_set_cursor* cursor);
/* The cursor's gap position. */
size_t d7_ordered_set_cursor_position(const d7_ordered_set_cursor* cursor);
/* Whether the gap precedes the first element. */
bool d7_ordered_set_cursor_is_at_start(const d7_ordered_set_cursor* cursor);
/* Whether the gap follows the last element. */
bool d7_ordered_set_cursor_is_at_end(const d7_ordered_set_cursor* cursor);

/* Successful peeks borrow representatives from the cursor snapshot. */
d7_ordered_status d7_ordered_set_cursor_try_peek_previous(
    const d7_ordered_set_cursor* cursor,
    bool* found,
    const void** item);
/* Reads the element immediately after the gap, reporting whether one exists. */
d7_ordered_status d7_ordered_set_cursor_try_peek_next(
    const d7_ordered_set_cursor* cursor,
    bool* found,
    const void** item);
/* Moves the cursor one position earlier. */
d7_ordered_status d7_ordered_set_cursor_move_previous(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
/* Moves the cursor one position later. */
d7_ordered_status d7_ordered_set_cursor_move_next(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
/* Moves the cursor to the given position within the same set version. */
d7_ordered_status d7_ordered_set_cursor_seek(
    const d7_ordered_set_cursor* cursor,
    size_t position,
    d7_ordered_set_cursor* result);
/* Inserts an element at the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_set_cursor_insert(
    const d7_ordered_set_cursor* cursor,
    const void* item,
    d7_ordered_set_cursor* result);
/* Inserts the element unless an equivalent one is present, reporting which happened. */
d7_ordered_status d7_ordered_set_cursor_try_insert(
    const d7_ordered_set_cursor* cursor,
    const void* item,
    bool* inserted,
    d7_ordered_set_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_set_cursor_delete_previous(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_set_cursor_delete_next(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
/* Initializes a handle on the set version this cursor is positioned in. */
d7_ordered_status d7_ordered_set_cursor_snapshot(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set* result);

/* Initializes a cursor at the given gap of the map. */
d7_ordered_status d7_ordered_map_get_cursor(
    const d7_ordered_map* map,
    size_t position,
    d7_ordered_map_cursor* result);
/* Initializes a cursor at the key, reporting whether it was actually present; on a miss the cursor
 * sits at the insertion point. */
d7_ordered_status d7_ordered_map_get_cursor_at_key(
    const d7_ordered_map* map,
    const void* equal_key,
    bool* found,
    d7_ordered_map_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
d7_ordered_status d7_ordered_map_cursor_clone(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_ordered_map_cursor_move(
    d7_ordered_map_cursor* destination,
    d7_ordered_map_cursor* source);
/* Releases the cursor's reference on its version. */
void d7_ordered_map_cursor_destroy(d7_ordered_map_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool d7_ordered_map_cursor_valid(const d7_ordered_map_cursor* cursor);
/* Number of entries in the map version the cursor is positioned in. */
size_t d7_ordered_map_cursor_count(const d7_ordered_map_cursor* cursor);
/* The cursor's gap position. */
size_t d7_ordered_map_cursor_position(const d7_ordered_map_cursor* cursor);
/* Whether the gap precedes the first entry. */
bool d7_ordered_map_cursor_is_at_start(const d7_ordered_map_cursor* cursor);
/* Whether the gap follows the last entry. */
bool d7_ordered_map_cursor_is_at_end(const d7_ordered_map_cursor* cursor);
/* Reads the entry immediately before the gap, reporting whether one exists. */
d7_ordered_status d7_ordered_map_cursor_try_peek_previous(
    const d7_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
/* Reads the entry immediately after the gap, reporting whether one exists. */
d7_ordered_status d7_ordered_map_cursor_try_peek_next(
    const d7_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
/* Moves the cursor one position earlier. */
d7_ordered_status d7_ordered_map_cursor_move_previous(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
/* Moves the cursor one position later. */
d7_ordered_status d7_ordered_map_cursor_move_next(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
/* Moves the cursor to the given position within the same map version. */
d7_ordered_status d7_ordered_map_cursor_seek(
    const d7_ordered_map_cursor* cursor,
    size_t position,
    d7_ordered_map_cursor* result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_map_cursor_insert(
    const d7_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    d7_ordered_map_cursor* result);
/* Inserts the entry unless an equivalent one is present, reporting which happened. */
d7_ordered_status d7_ordered_map_cursor_try_insert(
    const d7_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    d7_ordered_map_cursor* result);
/* Replaces the value of the entry after the gap, producing a new version the cursor is then
 * positioned in. */
d7_ordered_status d7_ordered_map_cursor_set_next_value(
    const d7_ordered_map_cursor* cursor,
    const void* value,
    d7_ordered_map_cursor* result);
/* Removes the entry before the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_map_cursor_delete_previous(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
/* Removes the entry after the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_map_cursor_delete_next(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
/* Initializes a handle on the map version this cursor is positioned in. */
d7_ordered_status d7_ordered_map_cursor_snapshot(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map* result);

/* Initializes a cursor at the given gap of the multimap. */
d7_ordered_status d7_ordered_multimap_get_cursor(
    const d7_ordered_multimap* map,
    int64_t position,
    d7_ordered_multimap_cursor* result);
/* Initializes a cursor at the given pair. */
d7_ordered_status d7_ordered_multimap_get_cursor_at_pair(
    const d7_ordered_multimap* map,
    const void* equal_key,
    const void* equal_value,
    bool* found,
    d7_ordered_multimap_cursor* result);
/* Initializes a cursor at the start of the key's group of values. */
d7_ordered_status d7_ordered_multimap_get_cursor_at_group(
    const d7_ordered_multimap* map,
    const void* equal_key,
    bool* found,
    d7_ordered_multimap_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
d7_ordered_status d7_ordered_multimap_cursor_clone(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_ordered_multimap_cursor_move(
    d7_ordered_multimap_cursor* destination,
    d7_ordered_multimap_cursor* source);
/* Releases the cursor's reference on its version. */
void d7_ordered_multimap_cursor_destroy(d7_ordered_multimap_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool d7_ordered_multimap_cursor_valid(
    const d7_ordered_multimap_cursor* cursor);
/* Number of pairs in the multimap version the cursor is positioned in. */
int64_t d7_ordered_multimap_cursor_count(
    const d7_ordered_multimap_cursor* cursor);
/* The cursor's gap position. */
int64_t d7_ordered_multimap_cursor_position(
    const d7_ordered_multimap_cursor* cursor);
/* Whether the gap precedes the first pair. */
bool d7_ordered_multimap_cursor_is_at_start(
    const d7_ordered_multimap_cursor* cursor);
/* Whether the gap follows the last pair. */
bool d7_ordered_multimap_cursor_is_at_end(
    const d7_ordered_multimap_cursor* cursor);
/* Reads the pair immediately before the gap, reporting whether one exists. */
d7_ordered_status d7_ordered_multimap_cursor_try_peek_previous(
    const d7_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
/* Reads the pair immediately after the gap, reporting whether one exists. */
d7_ordered_status d7_ordered_multimap_cursor_try_peek_next(
    const d7_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
/* Moves the cursor one position earlier. */
d7_ordered_status d7_ordered_multimap_cursor_move_previous(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
/* Moves the cursor one position later. */
d7_ordered_status d7_ordered_multimap_cursor_move_next(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
/* Moves the cursor to the given position within the same multimap version. */
d7_ordered_status d7_ordered_multimap_cursor_seek(
    const d7_ordered_multimap_cursor* cursor,
    int64_t position,
    d7_ordered_multimap_cursor* result);
/* Produces a multimap containing the given pair. */
d7_ordered_status d7_ordered_multimap_cursor_add(
    const d7_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    d7_ordered_multimap_cursor* result);
/* Adds the pair unless an equivalent one is present, reporting which happened. */
d7_ordered_status d7_ordered_multimap_cursor_try_add(
    const d7_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    d7_ordered_multimap_cursor* result);
/* Removes the pair before the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_multimap_cursor_delete_previous(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
/* Removes the pair after the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_multimap_cursor_delete_next(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
/* Initializes a handle on the multimap version this cursor is positioned in. */
d7_ordered_status d7_ordered_multimap_cursor_snapshot(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap* result);

#ifdef __cplusplus
}
#endif

#endif
