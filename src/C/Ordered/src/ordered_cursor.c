#include <tools/data_structures/ordered/ordered_cursor.h>

#include <string.h>

static bool tds_ordered_set_cursor_is_valid(
    const tds_ordered_set_cursor* cursor)
{
    return cursor != NULL && cursor->set.context != NULL
        && cursor->position <= tds_ordered_set_size(&cursor->set);
}

static void tds_ordered_set_cursor_publish(
    const tds_ordered_set_cursor* source,
    tds_ordered_set_cursor* result,
    tds_ordered_set_cursor* candidate)
{
    if (result == source) {
        tds_ordered_set_cursor_destroy(result);
    }
    tds_ordered_set_cursor_move(result, candidate);
}

static tds_ordered_status tds_ordered_set_cursor_publish_set(
    const tds_ordered_set_cursor* source,
    tds_ordered_set* set,
    size_t position,
    tds_ordered_set_cursor* result)
{
    tds_ordered_set_cursor candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    tds_ordered_set_move(&candidate.set, set);
    candidate.position = position;
    tds_ordered_set_cursor_publish(source, result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_get_cursor(
    const tds_ordered_set* set,
    size_t position,
    tds_ordered_set_cursor* result)
{
    if (set == NULL || set->context == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position > tds_ordered_set_size(set)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    tds_ordered_set_cursor candidate;
    const tds_ordered_status status =
        tds_ordered_set_clone(set, &candidate.set);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    candidate.position = position;
    tds_ordered_set_cursor_move(result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_get_cursor_at_item(
    const tds_ordered_set* set,
    const void* equal_item,
    bool* found,
    tds_ordered_set_cursor* result)
{
    if (set == NULL || equal_item == NULL || found == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    size_t position = 0u;
    const bool hit = tds_ordered_set_index_of(set, equal_item, &position);
    if (!hit) {
        position = tds_ordered_set_size(set);
    }
    const tds_ordered_status status =
        tds_ordered_set_get_cursor(set, position, result);
    if (status == TDS_ORDERED_OK) {
        *found = hit;
    }
    return status;
}

tds_ordered_status tds_ordered_set_cursor_clone(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (result == cursor) {
        return TDS_ORDERED_OK;
    }
    return tds_ordered_set_get_cursor(&cursor->set, cursor->position, result);
}

void tds_ordered_set_cursor_move(
    tds_ordered_set_cursor* destination,
    tds_ordered_set_cursor* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        (void)memset(destination, 0, sizeof(*destination));
        tds_ordered_set_move(&destination->set, &source->set);
        destination->position = source->position;
        source->position = 0u;
    }
}

void tds_ordered_set_cursor_destroy(tds_ordered_set_cursor* cursor)
{
    if (cursor != NULL) {
        tds_ordered_set_destroy(&cursor->set);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool tds_ordered_set_cursor_valid(const tds_ordered_set_cursor* cursor)
{
    return tds_ordered_set_cursor_is_valid(cursor);
}

size_t tds_ordered_set_cursor_count(const tds_ordered_set_cursor* cursor)
{
    return tds_ordered_set_cursor_is_valid(cursor)
        ? tds_ordered_set_size(&cursor->set) : 0u;
}

size_t tds_ordered_set_cursor_position(const tds_ordered_set_cursor* cursor)
{
    return tds_ordered_set_cursor_is_valid(cursor) ? cursor->position : 0u;
}

bool tds_ordered_set_cursor_is_at_start(const tds_ordered_set_cursor* cursor)
{
    return tds_ordered_set_cursor_is_valid(cursor) && cursor->position == 0u;
}

bool tds_ordered_set_cursor_is_at_end(const tds_ordered_set_cursor* cursor)
{
    return tds_ordered_set_cursor_is_valid(cursor)
        && cursor->position == tds_ordered_set_size(&cursor->set);
}

static tds_ordered_status tds_ordered_set_cursor_peek(
    const tds_ordered_set_cursor* cursor,
    size_t position,
    bool* found,
    const void** item)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || found == NULL || item == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position >= tds_ordered_set_size(&cursor->set)) {
        *found = false;
        return TDS_ORDERED_OK;
    }
    const tds_ordered_status status =
        tds_ordered_set_at(&cursor->set, position, item);
    if (status == TDS_ORDERED_OK) {
        *found = true;
    }
    return status;
}

tds_ordered_status tds_ordered_set_cursor_try_peek_previous(
    const tds_ordered_set_cursor* cursor,
    bool* found,
    const void** item)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || found == NULL || item == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        *found = false;
        return TDS_ORDERED_OK;
    }
    return tds_ordered_set_cursor_peek(cursor, cursor->position - 1u, found, item);
}

tds_ordered_status tds_ordered_set_cursor_try_peek_next(
    const tds_ordered_set_cursor* cursor,
    bool* found,
    const void** item)
{
    return tds_ordered_set_cursor_peek(
        cursor, cursor == NULL ? 0u : cursor->position, found, item);
}

tds_ordered_status tds_ordered_set_cursor_seek(
    const tds_ordered_set_cursor* cursor,
    size_t position,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position > tds_ordered_set_size(&cursor->set)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return TDS_ORDERED_OK;
    }
    tds_ordered_set_cursor candidate;
    const tds_ordered_status status =
        tds_ordered_set_get_cursor(&cursor->set, position, &candidate);
    if (status == TDS_ORDERED_OK) {
        tds_ordered_set_cursor_publish(cursor, result, &candidate);
    }
    return status;
}

tds_ordered_status tds_ordered_set_cursor_move_previous(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return tds_ordered_set_empty(&cursor->set)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_set_cursor_seek(cursor, cursor->position - 1u, result);
}

tds_ordered_status tds_ordered_set_cursor_move_next(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == tds_ordered_set_size(&cursor->set)) {
        return tds_ordered_set_empty(&cursor->set)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_set_cursor_seek(cursor, cursor->position + 1u, result);
}

tds_ordered_status tds_ordered_set_cursor_try_insert(
    const tds_ordered_set_cursor* cursor,
    const void* item,
    bool* inserted,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || item == NULL
        || inserted == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const bool changed = !tds_ordered_set_contains(&cursor->set, item);
    if (!changed) {
        tds_ordered_set_cursor candidate;
        const tds_ordered_status status =
            tds_ordered_set_cursor_clone(cursor, &candidate);
        if (status == TDS_ORDERED_OK) {
            tds_ordered_set_cursor_publish(cursor, result, &candidate);
            *inserted = false;
        }
        return status;
    }
    if (cursor->position == SIZE_MAX) {
        return TDS_ORDERED_OVERFLOW;
    }
    tds_ordered_set set;
    const tds_ordered_status status = tds_ordered_set_insert(
        &cursor->set, cursor->position, item, &set);
    if (status == TDS_ORDERED_OK) {
        (void)tds_ordered_set_cursor_publish_set(
            cursor, &set, cursor->position + 1u, result);
        *inserted = true;
    }
    return status;
}

tds_ordered_status tds_ordered_set_cursor_insert(
    const tds_ordered_set_cursor* cursor,
    const void* item,
    tds_ordered_set_cursor* result)
{
    bool inserted = false;
    return tds_ordered_set_cursor_try_insert(cursor, item, &inserted, result);
}

static tds_ordered_status tds_ordered_set_cursor_delete_at(
    const tds_ordered_set_cursor* cursor,
    size_t index,
    size_t position,
    tds_ordered_set_cursor* result)
{
    tds_ordered_set set;
    const tds_ordered_status status =
        tds_ordered_set_remove_at(&cursor->set, index, &set);
    return status == TDS_ORDERED_OK
        ? tds_ordered_set_cursor_publish_set(cursor, &set, position, result)
        : status;
}

tds_ordered_status tds_ordered_set_cursor_delete_previous(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return tds_ordered_set_empty(&cursor->set)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_set_cursor_delete_at(
        cursor, cursor->position - 1u, cursor->position - 1u, result);
}

tds_ordered_status tds_ordered_set_cursor_delete_next(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set_cursor* result)
{
    if (!tds_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == tds_ordered_set_size(&cursor->set)) {
        return tds_ordered_set_empty(&cursor->set)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_set_cursor_delete_at(
        cursor, cursor->position, cursor->position, result);
}

tds_ordered_status tds_ordered_set_cursor_snapshot(
    const tds_ordered_set_cursor* cursor,
    tds_ordered_set* result)
{
    return !tds_ordered_set_cursor_is_valid(cursor) || result == NULL
        ? TDS_ORDERED_INVALID_ARGUMENT
        : tds_ordered_set_clone(&cursor->set, result);
}

static bool tds_ordered_map_cursor_is_valid(
    const tds_ordered_map_cursor* cursor)
{
    return cursor != NULL && cursor->map.context != NULL
        && cursor->position <= tds_ordered_map_size(&cursor->map);
}

static void tds_ordered_map_cursor_publish(
    const tds_ordered_map_cursor* source,
    tds_ordered_map_cursor* result,
    tds_ordered_map_cursor* candidate)
{
    if (result == source) {
        tds_ordered_map_cursor_destroy(result);
    }
    tds_ordered_map_cursor_move(result, candidate);
}

static tds_ordered_status tds_ordered_map_cursor_publish_map(
    const tds_ordered_map_cursor* source,
    tds_ordered_map* map,
    size_t position,
    tds_ordered_map_cursor* result)
{
    tds_ordered_map_cursor candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    tds_ordered_map_move(&candidate.map, map);
    candidate.position = position;
    tds_ordered_map_cursor_publish(source, result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_get_cursor(
    const tds_ordered_map* map,
    size_t position,
    tds_ordered_map_cursor* result)
{
    if (map == NULL || map->context == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position > tds_ordered_map_size(map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    tds_ordered_map_cursor candidate;
    const tds_ordered_status status =
        tds_ordered_map_clone(map, &candidate.map);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    candidate.position = position;
    tds_ordered_map_cursor_move(result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_get_cursor_at_key(
    const tds_ordered_map* map,
    const void* equal_key,
    bool* found,
    tds_ordered_map_cursor* result)
{
    if (map == NULL || equal_key == NULL || found == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    size_t position = 0u;
    const bool hit = tds_ordered_map_index_of_key(map, equal_key, &position);
    if (!hit) {
        position = tds_ordered_map_size(map);
    }
    const tds_ordered_status status =
        tds_ordered_map_get_cursor(map, position, result);
    if (status == TDS_ORDERED_OK) {
        *found = hit;
    }
    return status;
}

tds_ordered_status tds_ordered_map_cursor_clone(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (result == cursor) {
        return TDS_ORDERED_OK;
    }
    return tds_ordered_map_get_cursor(&cursor->map, cursor->position, result);
}

void tds_ordered_map_cursor_move(
    tds_ordered_map_cursor* destination,
    tds_ordered_map_cursor* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        (void)memset(destination, 0, sizeof(*destination));
        tds_ordered_map_move(&destination->map, &source->map);
        destination->position = source->position;
        source->position = 0u;
    }
}

void tds_ordered_map_cursor_destroy(tds_ordered_map_cursor* cursor)
{
    if (cursor != NULL) {
        tds_ordered_map_destroy(&cursor->map);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool tds_ordered_map_cursor_valid(const tds_ordered_map_cursor* cursor)
{
    return tds_ordered_map_cursor_is_valid(cursor);
}

size_t tds_ordered_map_cursor_count(const tds_ordered_map_cursor* cursor)
{
    return tds_ordered_map_cursor_is_valid(cursor)
        ? tds_ordered_map_size(&cursor->map) : 0u;
}

size_t tds_ordered_map_cursor_position(const tds_ordered_map_cursor* cursor)
{
    return tds_ordered_map_cursor_is_valid(cursor) ? cursor->position : 0u;
}

bool tds_ordered_map_cursor_is_at_start(const tds_ordered_map_cursor* cursor)
{
    return tds_ordered_map_cursor_is_valid(cursor) && cursor->position == 0u;
}

bool tds_ordered_map_cursor_is_at_end(const tds_ordered_map_cursor* cursor)
{
    return tds_ordered_map_cursor_is_valid(cursor)
        && cursor->position == tds_ordered_map_size(&cursor->map);
}

static tds_ordered_status tds_ordered_map_cursor_peek(
    const tds_ordered_map_cursor* cursor,
    size_t position,
    bool* found,
    const void** key,
    const void** value)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || found == NULL
        || key == NULL || value == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position >= tds_ordered_map_size(&cursor->map)) {
        *found = false;
        return TDS_ORDERED_OK;
    }
    const tds_ordered_status status =
        tds_ordered_map_entry_at(&cursor->map, position, key, value);
    if (status == TDS_ORDERED_OK) {
        *found = true;
    }
    return status;
}

tds_ordered_status tds_ordered_map_cursor_try_peek_previous(
    const tds_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || found == NULL
        || key == NULL || value == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        *found = false;
        return TDS_ORDERED_OK;
    }
    return tds_ordered_map_cursor_peek(
        cursor, cursor->position - 1u, found, key, value);
}

tds_ordered_status tds_ordered_map_cursor_try_peek_next(
    const tds_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    return tds_ordered_map_cursor_peek(
        cursor, cursor == NULL ? 0u : cursor->position, found, key, value);
}

tds_ordered_status tds_ordered_map_cursor_seek(
    const tds_ordered_map_cursor* cursor,
    size_t position,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position > tds_ordered_map_size(&cursor->map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return TDS_ORDERED_OK;
    }
    tds_ordered_map_cursor candidate;
    const tds_ordered_status status =
        tds_ordered_map_get_cursor(&cursor->map, position, &candidate);
    if (status == TDS_ORDERED_OK) {
        tds_ordered_map_cursor_publish(cursor, result, &candidate);
    }
    return status;
}

tds_ordered_status tds_ordered_map_cursor_move_previous(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return tds_ordered_map_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_map_cursor_seek(cursor, cursor->position - 1u, result);
}

tds_ordered_status tds_ordered_map_cursor_move_next(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == tds_ordered_map_size(&cursor->map)) {
        return tds_ordered_map_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_map_cursor_seek(cursor, cursor->position + 1u, result);
}

tds_ordered_status tds_ordered_map_cursor_insert(
    const tds_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || key == NULL
        || value == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == SIZE_MAX) {
        return TDS_ORDERED_OVERFLOW;
    }
    tds_ordered_map map;
    const tds_ordered_status status = tds_ordered_map_insert(
        &cursor->map, cursor->position, key, value, &map);
    return status == TDS_ORDERED_OK
        ? tds_ordered_map_cursor_publish_map(
            cursor, &map, cursor->position + 1u, result)
        : status;
}

tds_ordered_status tds_ordered_map_cursor_try_insert(
    const tds_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || key == NULL
        || value == NULL || inserted == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    size_t position = 0u;
    if (tds_ordered_map_index_of_key(&cursor->map, key, &position)) {
        tds_ordered_map_cursor candidate;
        const tds_ordered_status status =
            tds_ordered_map_get_cursor(&cursor->map, position, &candidate);
        if (status == TDS_ORDERED_OK) {
            tds_ordered_map_cursor_publish(cursor, result, &candidate);
            *inserted = false;
        }
        return status;
    }
    const tds_ordered_status status =
        tds_ordered_map_cursor_insert(cursor, key, value, result);
    if (status == TDS_ORDERED_OK) {
        *inserted = true;
    }
    return status;
}

tds_ordered_status tds_ordered_map_cursor_set_next_value(
    const tds_ordered_map_cursor* cursor,
    const void* value,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const void* key = NULL;
    const void* old_value = NULL;
    bool found = false;
    tds_ordered_status status = tds_ordered_map_cursor_try_peek_next(
        cursor, &found, &key, &old_value);
    (void)old_value;
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    if (!found) {
        return tds_ordered_map_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    tds_ordered_map map;
    status = tds_ordered_map_set(&cursor->map, key, value, &map);
    return status == TDS_ORDERED_OK
        ? tds_ordered_map_cursor_publish_map(
            cursor, &map, cursor->position, result)
        : status;
}

static tds_ordered_status tds_ordered_map_cursor_delete_at(
    const tds_ordered_map_cursor* cursor,
    size_t index,
    size_t position,
    tds_ordered_map_cursor* result)
{
    tds_ordered_map map;
    const tds_ordered_status status =
        tds_ordered_map_remove_at(&cursor->map, index, &map);
    return status == TDS_ORDERED_OK
        ? tds_ordered_map_cursor_publish_map(cursor, &map, position, result)
        : status;
}

tds_ordered_status tds_ordered_map_cursor_delete_previous(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return tds_ordered_map_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_map_cursor_delete_at(
        cursor, cursor->position - 1u, cursor->position - 1u, result);
}

tds_ordered_status tds_ordered_map_cursor_delete_next(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map_cursor* result)
{
    if (!tds_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == tds_ordered_map_size(&cursor->map)) {
        return tds_ordered_map_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_map_cursor_delete_at(
        cursor, cursor->position, cursor->position, result);
}

tds_ordered_status tds_ordered_map_cursor_snapshot(
    const tds_ordered_map_cursor* cursor,
    tds_ordered_map* result)
{
    return !tds_ordered_map_cursor_is_valid(cursor) || result == NULL
        ? TDS_ORDERED_INVALID_ARGUMENT
        : tds_ordered_map_clone(&cursor->map, result);
}

static bool tds_ordered_multimap_cursor_is_valid(
    const tds_ordered_multimap_cursor* cursor)
{
    return cursor != NULL && cursor->map.context != NULL
        && cursor->position >= 0
        && cursor->position <= tds_ordered_multimap_pair_count(&cursor->map);
}

static void tds_ordered_multimap_cursor_publish(
    const tds_ordered_multimap_cursor* source,
    tds_ordered_multimap_cursor* result,
    tds_ordered_multimap_cursor* candidate)
{
    if (result == source) {
        tds_ordered_multimap_cursor_destroy(result);
    }
    tds_ordered_multimap_cursor_move(result, candidate);
}

static tds_ordered_status tds_ordered_multimap_cursor_publish_map(
    const tds_ordered_multimap_cursor* source,
    tds_ordered_multimap* map,
    int64_t position,
    tds_ordered_multimap_cursor* result)
{
    tds_ordered_multimap_cursor candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    tds_ordered_multimap_move(&candidate.map, map);
    candidate.position = position;
    tds_ordered_multimap_cursor_publish(source, result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_multimap_get_cursor(
    const tds_ordered_multimap* map,
    int64_t position,
    tds_ordered_multimap_cursor* result)
{
    if (map == NULL || map->context == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position < 0 || position > tds_ordered_multimap_pair_count(map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    tds_ordered_multimap_cursor candidate;
    const tds_ordered_status status =
        tds_ordered_multimap_clone(map, &candidate.map);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    candidate.position = position;
    tds_ordered_multimap_cursor_move(result, &candidate);
    return TDS_ORDERED_OK;
}

typedef struct tds_ordered_multimap_rank_context {
    int64_t target;
    int64_t position;
    const void* key;
    const void* value;
    bool found;
} tds_ordered_multimap_rank_context;

static void tds_ordered_multimap_rank_visit(
    const void* key,
    const void* value,
    void* raw_context)
{
    tds_ordered_multimap_rank_context* context =
        (tds_ordered_multimap_rank_context*)raw_context;
    if (!context->found && context->position == context->target) {
        context->key = key;
        context->value = value;
        context->found = true;
    }
    if (context->position < INT64_MAX) {
        ++context->position;
    }
}

static tds_ordered_status tds_ordered_multimap_entry_at(
    const tds_ordered_multimap* map,
    int64_t position,
    bool* found,
    const void** key,
    const void** value)
{
    if (map == NULL || map->context == NULL || found == NULL
        || key == NULL || value == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position < 0 || position >= tds_ordered_multimap_pair_count(map)) {
        *found = false;
        return TDS_ORDERED_OK;
    }
    tds_ordered_multimap_rank_context context = {
        position, 0, NULL, NULL, false };
    const tds_ordered_status status =
        tds_ordered_multimap_visit(map, tds_ordered_multimap_rank_visit, &context);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    if (!context.found) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    *found = true;
    *key = context.key;
    *value = context.value;
    return TDS_ORDERED_OK;
}

static bool tds_ordered_map_policy_keys_equal(
    const tds_ordered_map_policy* policy,
    const void* left,
    const void* right)
{
    if (left == right) {
        return true;
    }
    return policy->key_equal != NULL
        ? policy->key_equal(left, right, policy->context)
        : memcmp(left, right, policy->key_type.size) == 0;
}

static bool tds_ordered_policy_items_equal(
    const tds_ordered_policy* policy,
    const void* left,
    const void* right)
{
    if (left == right) {
        return true;
    }
    return policy->equal != NULL
        ? policy->equal(left, right, policy->context)
        : memcmp(left, right, policy->item_type.size) == 0;
}

typedef struct tds_ordered_multimap_search_context {
    const tds_ordered_map_policy* key_policy;
    const tds_ordered_policy* value_policy;
    const void* key;
    const void* value;
    int64_t position;
    int64_t result;
    bool compare_value;
} tds_ordered_multimap_search_context;

static void tds_ordered_multimap_search_visit(
    const void* key,
    const void* value,
    void* raw_context)
{
    tds_ordered_multimap_search_context* context =
        (tds_ordered_multimap_search_context*)raw_context;
    if (context->result < 0
        && tds_ordered_map_policy_keys_equal(
            context->key_policy, key, context->key)
        && (!context->compare_value
            || tds_ordered_policy_items_equal(
                context->value_policy, value, context->value))) {
        context->result = context->position;
    }
    if (context->position < INT64_MAX) {
        ++context->position;
    }
}

static tds_ordered_status tds_ordered_multimap_find_position(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value,
    bool compare_value,
    int64_t* position)
{
    if (map == NULL || map->context == NULL || key == NULL || position == NULL
        || (compare_value && value == NULL)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const tds_ordered_set* values = NULL;
    const bool key_found =
        tds_ordered_multimap_try_get_values(map, key, &values);
    if (!key_found || (compare_value && !tds_ordered_set_contains(values, value))) {
        *position = -1;
        return TDS_ORDERED_OK;
    }
    tds_ordered_multimap_search_context context = {
        tds_ordered_map_policy_of(&map->groups),
        tds_ordered_set_policy(values),
        key,
        value,
        0,
        -1,
        compare_value };
    const tds_ordered_status status = tds_ordered_multimap_visit(
        map, tds_ordered_multimap_search_visit, &context);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    if (context.result < 0) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    *position = context.result;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_multimap_get_cursor_at(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value,
    bool compare_value,
    bool* found,
    tds_ordered_multimap_cursor* result)
{
    if (found == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    int64_t position = -1;
    tds_ordered_status status = tds_ordered_multimap_find_position(
        map, key, value, compare_value, &position);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    const bool hit = position >= 0;
    if (!hit) {
        position = tds_ordered_multimap_pair_count(map);
    }
    status = tds_ordered_multimap_get_cursor(map, position, result);
    if (status == TDS_ORDERED_OK) {
        *found = hit;
    }
    return status;
}

tds_ordered_status tds_ordered_multimap_get_cursor_at_pair(
    const tds_ordered_multimap* map,
    const void* equal_key,
    const void* equal_value,
    bool* found,
    tds_ordered_multimap_cursor* result)
{
    return tds_ordered_multimap_get_cursor_at(
        map, equal_key, equal_value, true, found, result);
}

tds_ordered_status tds_ordered_multimap_get_cursor_at_group(
    const tds_ordered_multimap* map,
    const void* equal_key,
    bool* found,
    tds_ordered_multimap_cursor* result)
{
    return tds_ordered_multimap_get_cursor_at(
        map, equal_key, NULL, false, found, result);
}

tds_ordered_status tds_ordered_multimap_cursor_clone(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (result == cursor) {
        return TDS_ORDERED_OK;
    }
    return tds_ordered_multimap_get_cursor(
        &cursor->map, cursor->position, result);
}

void tds_ordered_multimap_cursor_move(
    tds_ordered_multimap_cursor* destination,
    tds_ordered_multimap_cursor* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        (void)memset(destination, 0, sizeof(*destination));
        tds_ordered_multimap_move(&destination->map, &source->map);
        destination->position = source->position;
        source->position = 0;
    }
}

void tds_ordered_multimap_cursor_destroy(tds_ordered_multimap_cursor* cursor)
{
    if (cursor != NULL) {
        tds_ordered_multimap_destroy(&cursor->map);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool tds_ordered_multimap_cursor_valid(
    const tds_ordered_multimap_cursor* cursor)
{
    return tds_ordered_multimap_cursor_is_valid(cursor);
}

int64_t tds_ordered_multimap_cursor_count(
    const tds_ordered_multimap_cursor* cursor)
{
    return tds_ordered_multimap_cursor_is_valid(cursor)
        ? tds_ordered_multimap_pair_count(&cursor->map) : 0;
}

int64_t tds_ordered_multimap_cursor_position(
    const tds_ordered_multimap_cursor* cursor)
{
    return tds_ordered_multimap_cursor_is_valid(cursor)
        ? cursor->position : 0;
}

bool tds_ordered_multimap_cursor_is_at_start(
    const tds_ordered_multimap_cursor* cursor)
{
    return tds_ordered_multimap_cursor_is_valid(cursor)
        && cursor->position == 0;
}

bool tds_ordered_multimap_cursor_is_at_end(
    const tds_ordered_multimap_cursor* cursor)
{
    return tds_ordered_multimap_cursor_is_valid(cursor)
        && cursor->position == tds_ordered_multimap_pair_count(&cursor->map);
}

tds_ordered_status tds_ordered_multimap_cursor_try_peek_previous(
    const tds_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || found == NULL
        || key == NULL || value == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        *found = false;
        return TDS_ORDERED_OK;
    }
    return tds_ordered_multimap_entry_at(
        &cursor->map, cursor->position - 1, found, key, value);
}

tds_ordered_status tds_ordered_multimap_cursor_try_peek_next(
    const tds_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_multimap_entry_at(
        &cursor->map, cursor->position, found, key, value);
}

tds_ordered_status tds_ordered_multimap_cursor_seek(
    const tds_ordered_multimap_cursor* cursor,
    int64_t position,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (position < 0 || position > tds_ordered_multimap_pair_count(&cursor->map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return TDS_ORDERED_OK;
    }
    tds_ordered_multimap_cursor candidate;
    const tds_ordered_status status =
        tds_ordered_multimap_get_cursor(&cursor->map, position, &candidate);
    if (status == TDS_ORDERED_OK) {
        tds_ordered_multimap_cursor_publish(cursor, result, &candidate);
    }
    return status;
}

tds_ordered_status tds_ordered_multimap_cursor_move_previous(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return tds_ordered_multimap_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_multimap_cursor_seek(cursor, cursor->position - 1, result);
}

tds_ordered_status tds_ordered_multimap_cursor_move_next(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == tds_ordered_multimap_pair_count(&cursor->map)) {
        return tds_ordered_multimap_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_multimap_cursor_seek(cursor, cursor->position + 1, result);
}

tds_ordered_status tds_ordered_multimap_cursor_try_add(
    const tds_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || key == NULL
        || value == NULL || inserted == NULL || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (tds_ordered_multimap_contains(&cursor->map, key, value)) {
        tds_ordered_multimap_cursor candidate;
        const tds_ordered_status status =
            tds_ordered_multimap_cursor_clone(cursor, &candidate);
        if (status == TDS_ORDERED_OK) {
            tds_ordered_multimap_cursor_publish(cursor, result, &candidate);
            *inserted = false;
        }
        return status;
    }
    tds_ordered_multimap map;
    tds_ordered_status status =
        tds_ordered_multimap_add(&cursor->map, key, value, &map);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    int64_t position = -1;
    status = tds_ordered_multimap_find_position(
        &map, key, value, true, &position);
    if (status != TDS_ORDERED_OK || position < 0) {
        tds_ordered_multimap_destroy(&map);
        return status == TDS_ORDERED_OK
            ? TDS_ORDERED_INVARIANT_VIOLATION : status;
    }
    if (position == INT64_MAX) {
        tds_ordered_multimap_destroy(&map);
        return TDS_ORDERED_OVERFLOW;
    }
    (void)tds_ordered_multimap_cursor_publish_map(
        cursor, &map, position + 1, result);
    *inserted = true;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_multimap_cursor_add(
    const tds_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    tds_ordered_multimap_cursor* result)
{
    bool inserted = false;
    return tds_ordered_multimap_cursor_try_add(
        cursor, key, value, &inserted, result);
}

static tds_ordered_status tds_ordered_multimap_cursor_delete(
    const tds_ordered_multimap_cursor* cursor,
    int64_t rank,
    int64_t position,
    tds_ordered_multimap_cursor* result)
{
    bool found = false;
    const void* key = NULL;
    const void* value = NULL;
    tds_ordered_status status = tds_ordered_multimap_entry_at(
        &cursor->map, rank, &found, &key, &value);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    if (!found) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    tds_ordered_multimap map;
    status = tds_ordered_multimap_remove(&cursor->map, key, value, &map);
    return status == TDS_ORDERED_OK
        ? tds_ordered_multimap_cursor_publish_map(cursor, &map, position, result)
        : status;
}

tds_ordered_status tds_ordered_multimap_cursor_delete_previous(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return tds_ordered_multimap_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_multimap_cursor_delete(
        cursor, cursor->position - 1, cursor->position - 1, result);
}

tds_ordered_status tds_ordered_multimap_cursor_delete_next(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap_cursor* result)
{
    if (!tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == tds_ordered_multimap_pair_count(&cursor->map)) {
        return tds_ordered_multimap_empty(&cursor->map)
            ? TDS_ORDERED_EMPTY : TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_multimap_cursor_delete(
        cursor, cursor->position, cursor->position, result);
}

tds_ordered_status tds_ordered_multimap_cursor_snapshot(
    const tds_ordered_multimap_cursor* cursor,
    tds_ordered_multimap* result)
{
    return !tds_ordered_multimap_cursor_is_valid(cursor) || result == NULL
        ? TDS_ORDERED_INVALID_ARGUMENT
        : tds_ordered_multimap_clone(&cursor->map, result);
}
