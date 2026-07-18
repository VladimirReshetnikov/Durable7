#ifndef TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_CURSOR_H
#define TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_CURSOR_H

#include <tools/data_structures/ordered/ordered_multimap.h>

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
typedef struct tds_ordered_set_cursor {
    tds_ordered_set set;
    size_t position;
} tds_ordered_set_cursor;

typedef struct tds_ordered_map_cursor {
    tds_ordered_map map;
    size_t position;
} tds_ordered_map_cursor;

typedef struct tds_ordered_multimap_cursor {
    tds_ordered_multimap map;
    int64_t position;
} tds_ordered_multimap_cursor;

tds_ordered_status tds_ordered_set_get_cursor(
    const tds_ordered_set* set,
    size_t position,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_get_cursor_at_item(
    const tds_ordered_set* set,
    const void* equal_item,
    bool* found,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_clone(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result);
void tds_ordered_set_cursor_move(
    tds_ordered_set_cursor* destination,
    tds_ordered_set_cursor* source);
void tds_ordered_set_cursor_destroy(tds_ordered_set_cursor* cursor);
bool tds_ordered_set_cursor_valid(const tds_ordered_set_cursor* cursor);
size_t tds_ordered_set_cursor_count(const tds_ordered_set_cursor* cursor);
size_t tds_ordered_set_cursor_position(const tds_ordered_set_cursor* cursor);
bool tds_ordered_set_cursor_is_at_start(const tds_ordered_set_cursor* cursor);
bool tds_ordered_set_cursor_is_at_end(const tds_ordered_set_cursor* cursor);

/* Successful peeks borrow representatives from the cursor snapshot. */
tds_ordered_status tds_ordered_set_cursor_try_peek_previous(
    const tds_ordered_set_cursor* cursor,
    bool* found,
    const void** item);
tds_ordered_status tds_ordered_set_cursor_try_peek_next(
    const tds_ordered_set_cursor* cursor,
    bool* found,
    const void** item);
tds_ordered_status tds_ordered_set_cursor_move_previous(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_move_next(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_seek(
    const tds_ordered_set_cursor* cursor,
    size_t position,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_insert(
    const tds_ordered_set_cursor* cursor,
    const void* item,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_try_insert(
    const tds_ordered_set_cursor* cursor,
    const void* item,
    bool* inserted,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_delete_previous(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_delete_next(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result);
tds_ordered_status tds_ordered_set_cursor_snapshot(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set* result);

tds_ordered_status tds_ordered_map_get_cursor(
    const tds_ordered_map* map,
    size_t position,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_get_cursor_at_key(
    const tds_ordered_map* map,
    const void* equal_key,
    bool* found,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_clone(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result);
void tds_ordered_map_cursor_move(
    tds_ordered_map_cursor* destination,
    tds_ordered_map_cursor* source);
void tds_ordered_map_cursor_destroy(tds_ordered_map_cursor* cursor);
bool tds_ordered_map_cursor_valid(const tds_ordered_map_cursor* cursor);
size_t tds_ordered_map_cursor_count(const tds_ordered_map_cursor* cursor);
size_t tds_ordered_map_cursor_position(const tds_ordered_map_cursor* cursor);
bool tds_ordered_map_cursor_is_at_start(const tds_ordered_map_cursor* cursor);
bool tds_ordered_map_cursor_is_at_end(const tds_ordered_map_cursor* cursor);
tds_ordered_status tds_ordered_map_cursor_try_peek_previous(
    const tds_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
tds_ordered_status tds_ordered_map_cursor_try_peek_next(
    const tds_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
tds_ordered_status tds_ordered_map_cursor_move_previous(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_move_next(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_seek(
    const tds_ordered_map_cursor* cursor,
    size_t position,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_insert(
    const tds_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_try_insert(
    const tds_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_set_next_value(
    const tds_ordered_map_cursor* cursor,
    const void* value,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_delete_previous(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_delete_next(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result);
tds_ordered_status tds_ordered_map_cursor_snapshot(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map* result);

tds_ordered_status tds_ordered_multimap_get_cursor(
    const tds_ordered_multimap* map,
    int64_t position,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_get_cursor_at_pair(
    const tds_ordered_multimap* map,
    const void* equal_key,
    const void* equal_value,
    bool* found,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_get_cursor_at_group(
    const tds_ordered_multimap* map,
    const void* equal_key,
    bool* found,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_clone(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result);
void tds_ordered_multimap_cursor_move(
    tds_ordered_multimap_cursor* destination,
    tds_ordered_multimap_cursor* source);
void tds_ordered_multimap_cursor_destroy(tds_ordered_multimap_cursor* cursor);
bool tds_ordered_multimap_cursor_valid(
    const tds_ordered_multimap_cursor* cursor);
int64_t tds_ordered_multimap_cursor_count(
    const tds_ordered_multimap_cursor* cursor);
int64_t tds_ordered_multimap_cursor_position(
    const tds_ordered_multimap_cursor* cursor);
bool tds_ordered_multimap_cursor_is_at_start(
    const tds_ordered_multimap_cursor* cursor);
bool tds_ordered_multimap_cursor_is_at_end(
    const tds_ordered_multimap_cursor* cursor);
tds_ordered_status tds_ordered_multimap_cursor_try_peek_previous(
    const tds_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
tds_ordered_status tds_ordered_multimap_cursor_try_peek_next(
    const tds_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value);
tds_ordered_status tds_ordered_multimap_cursor_move_previous(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_move_next(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_seek(
    const tds_ordered_multimap_cursor* cursor,
    int64_t position,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_add(
    const tds_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_try_add(
    const tds_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_delete_previous(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_delete_next(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result);
tds_ordered_status tds_ordered_multimap_cursor_snapshot(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap* result);

#ifdef __cplusplus
}
#endif

#endif
