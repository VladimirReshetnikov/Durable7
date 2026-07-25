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

d7_ordered_status d7_ordered_set_get_cursor(
    const d7_ordered_set* set,
    size_t position,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_get_cursor_at_item(
    const d7_ordered_set* set,
    const void* equal_item,
    bool* found,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_clone(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
void d7_ordered_set_cursor_move(
    d7_ordered_set_cursor* destination,
    d7_ordered_set_cursor* source);
void d7_ordered_set_cursor_destroy(d7_ordered_set_cursor* cursor);
bool d7_ordered_set_cursor_valid(const d7_ordered_set_cursor* cursor);
size_t d7_ordered_set_cursor_count(const d7_ordered_set_cursor* cursor);
size_t d7_ordered_set_cursor_position(const d7_ordered_set_cursor* cursor);
bool d7_ordered_set_cursor_is_at_start(const d7_ordered_set_cursor* cursor);
bool d7_ordered_set_cursor_is_at_end(const d7_ordered_set_cursor* cursor);

/* Successful peeks borrow representatives from the cursor snapshot. */
d7_ordered_status d7_ordered_set_cursor_try_peek_previous(
    const d7_ordered_set_cursor* cursor,
    bool* found,
    const void** item);
d7_ordered_status d7_ordered_set_cursor_try_peek_next(
    const d7_ordered_set_cursor* cursor,
    bool* found,
    const void** item);
d7_ordered_status d7_ordered_set_cursor_move_previous(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_move_next(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_seek(
    const d7_ordered_set_cursor* cursor,
    size_t position,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_insert(
    const d7_ordered_set_cursor* cursor,
    const void* item,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_try_insert(
    const d7_ordered_set_cursor* cursor,
    const void* item,
    bool* inserted,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_delete_previous(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_delete_next(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result);
d7_ordered_status d7_ordered_set_cursor_snapshot(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set* result);

d7_ordered_status d7_ordered_map_get_cursor(
    const d7_ordered_map* map,
    size_t position,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_get_cursor_at_key(
    const d7_ordered_map* map,
    const void* equal_key,
    bool* found,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_clone(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
void d7_ordered_map_cursor_move(
    d7_ordered_map_cursor* destination,
    d7_ordered_map_cursor* source);
void d7_ordered_map_cursor_destroy(d7_ordered_map_cursor* cursor);
bool d7_ordered_map_cursor_valid(const d7_ordered_map_cursor* cursor);
size_t d7_ordered_map_cursor_count(const d7_ordered_map_cursor* cursor);
size_t d7_ordered_map_cursor_position(const d7_ordered_map_cursor* cursor);
bool d7_ordered_map_cursor_is_at_start(const d7_ordered_map_cursor* cursor);
bool d7_ordered_map_cursor_is_at_end(const d7_ordered_map_cursor* cursor);
d7_ordered_status d7_ordered_map_cursor_try_peek_previous(
    const d7_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
d7_ordered_status d7_ordered_map_cursor_try_peek_next(
    const d7_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
d7_ordered_status d7_ordered_map_cursor_move_previous(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_move_next(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_seek(
    const d7_ordered_map_cursor* cursor,
    size_t position,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_insert(
    const d7_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_try_insert(
    const d7_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_set_next_value(
    const d7_ordered_map_cursor* cursor,
    const void* value,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_delete_previous(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_delete_next(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result);
d7_ordered_status d7_ordered_map_cursor_snapshot(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map* result);

d7_ordered_status d7_ordered_multimap_get_cursor(
    const d7_ordered_multimap* map,
    int64_t position,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_get_cursor_at_pair(
    const d7_ordered_multimap* map,
    const void* equal_key,
    const void* equal_value,
    bool* found,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_get_cursor_at_group(
    const d7_ordered_multimap* map,
    const void* equal_key,
    bool* found,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_clone(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
void d7_ordered_multimap_cursor_move(
    d7_ordered_multimap_cursor* destination,
    d7_ordered_multimap_cursor* source);
void d7_ordered_multimap_cursor_destroy(d7_ordered_multimap_cursor* cursor);
bool d7_ordered_multimap_cursor_valid(
    const d7_ordered_multimap_cursor* cursor);
int64_t d7_ordered_multimap_cursor_count(
    const d7_ordered_multimap_cursor* cursor);
int64_t d7_ordered_multimap_cursor_position(
    const d7_ordered_multimap_cursor* cursor);
bool d7_ordered_multimap_cursor_is_at_start(
    const d7_ordered_multimap_cursor* cursor);
bool d7_ordered_multimap_cursor_is_at_end(
    const d7_ordered_multimap_cursor* cursor);
d7_ordered_status d7_ordered_multimap_cursor_try_peek_previous(
    const d7_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
d7_ordered_status d7_ordered_multimap_cursor_try_peek_next(
    const d7_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
d7_ordered_status d7_ordered_multimap_cursor_move_previous(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_move_next(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_seek(
    const d7_ordered_multimap_cursor* cursor,
    int64_t position,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_add(
    const d7_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_try_add(
    const d7_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_delete_previous(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_delete_next(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result);
d7_ordered_status d7_ordered_multimap_cursor_snapshot(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap* result);

#ifdef __cplusplus
}
#endif

#endif
