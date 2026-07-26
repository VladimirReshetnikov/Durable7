/*
 * Implementation of the insertion-ordered collections' cursors.
 */

#include <durable7/ordered/ordered_cursor.h>

#include <string.h>

static bool d7_ordered_set_cursor_is_valid(
    const d7_ordered_set_cursor* cursor)
{
    return cursor != NULL && cursor->set.context != NULL
        && cursor->position <= d7_ordered_set_size(&cursor->set);
}

static void d7_ordered_set_cursor_publish(
    const d7_ordered_set_cursor* source,
    d7_ordered_set_cursor* result,
    d7_ordered_set_cursor* candidate)
{
    if (result == source) {
        d7_ordered_set_cursor_destroy(result);
    }
    d7_ordered_set_cursor_move(result, candidate);
}

static d7_ordered_status d7_ordered_set_cursor_publish_set(
    const d7_ordered_set_cursor* source,
    d7_ordered_set* set,
    size_t position,
    d7_ordered_set_cursor* result)
{
    d7_ordered_set_cursor candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    d7_ordered_set_move(&candidate.set, set);
    candidate.position = position;
    d7_ordered_set_cursor_publish(source, result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_get_cursor(
    const d7_ordered_set* set,
    size_t position,
    d7_ordered_set_cursor* result)
{
    if (set == NULL || set->context == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position > d7_ordered_set_size(set)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    d7_ordered_set_cursor candidate;
    const d7_ordered_status status =
        d7_ordered_set_clone(set, &candidate.set);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    candidate.position = position;
    d7_ordered_set_cursor_move(result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_get_cursor_at_item(
    const d7_ordered_set* set,
    const void* equal_item,
    bool* found,
    d7_ordered_set_cursor* result)
{
    if (set == NULL || equal_item == NULL || found == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    size_t position = 0u;
    const bool hit = d7_ordered_set_index_of(set, equal_item, &position);
    if (!hit) {
        position = d7_ordered_set_size(set);
    }
    const d7_ordered_status status =
        d7_ordered_set_get_cursor(set, position, result);
    if (status == D7_ORDERED_OK) {
        *found = hit;
    }
    return status;
}

d7_ordered_status d7_ordered_set_cursor_clone(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (result == cursor) {
        return D7_ORDERED_OK;
    }
    return d7_ordered_set_get_cursor(&cursor->set, cursor->position, result);
}

void d7_ordered_set_cursor_move(
    d7_ordered_set_cursor* destination,
    d7_ordered_set_cursor* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        (void)memset(destination, 0, sizeof(*destination));
        d7_ordered_set_move(&destination->set, &source->set);
        destination->position = source->position;
        source->position = 0u;
    }
}

void d7_ordered_set_cursor_destroy(d7_ordered_set_cursor* cursor)
{
    if (cursor != NULL) {
        d7_ordered_set_destroy(&cursor->set);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool d7_ordered_set_cursor_valid(const d7_ordered_set_cursor* cursor)
{
    return d7_ordered_set_cursor_is_valid(cursor);
}

size_t d7_ordered_set_cursor_count(const d7_ordered_set_cursor* cursor)
{
    return d7_ordered_set_cursor_is_valid(cursor)
        ? d7_ordered_set_size(&cursor->set) : 0u;
}

size_t d7_ordered_set_cursor_position(const d7_ordered_set_cursor* cursor)
{
    return d7_ordered_set_cursor_is_valid(cursor) ? cursor->position : 0u;
}

bool d7_ordered_set_cursor_is_at_start(const d7_ordered_set_cursor* cursor)
{
    return d7_ordered_set_cursor_is_valid(cursor) && cursor->position == 0u;
}

bool d7_ordered_set_cursor_is_at_end(const d7_ordered_set_cursor* cursor)
{
    return d7_ordered_set_cursor_is_valid(cursor)
        && cursor->position == d7_ordered_set_size(&cursor->set);
}

static d7_ordered_status d7_ordered_set_cursor_peek(
    const d7_ordered_set_cursor* cursor,
    size_t position,
    bool* found,
    const void** item)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || found == NULL || item == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position >= d7_ordered_set_size(&cursor->set)) {
        *found = false;
        return D7_ORDERED_OK;
    }
    const d7_ordered_status status =
        d7_ordered_set_at(&cursor->set, position, item);
    if (status == D7_ORDERED_OK) {
        *found = true;
    }
    return status;
}

d7_ordered_status d7_ordered_set_cursor_try_peek_previous(
    const d7_ordered_set_cursor* cursor,
    bool* found,
    const void** item)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || found == NULL || item == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        *found = false;
        return D7_ORDERED_OK;
    }
    return d7_ordered_set_cursor_peek(cursor, cursor->position - 1u, found, item);
}

d7_ordered_status d7_ordered_set_cursor_try_peek_next(
    const d7_ordered_set_cursor* cursor,
    bool* found,
    const void** item)
{
    return d7_ordered_set_cursor_peek(
        cursor, cursor == NULL ? 0u : cursor->position, found, item);
}

d7_ordered_status d7_ordered_set_cursor_seek(
    const d7_ordered_set_cursor* cursor,
    size_t position,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position > d7_ordered_set_size(&cursor->set)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return D7_ORDERED_OK;
    }
    d7_ordered_set_cursor candidate;
    const d7_ordered_status status =
        d7_ordered_set_get_cursor(&cursor->set, position, &candidate);
    if (status == D7_ORDERED_OK) {
        d7_ordered_set_cursor_publish(cursor, result, &candidate);
    }
    return status;
}

d7_ordered_status d7_ordered_set_cursor_move_previous(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return d7_ordered_set_empty(&cursor->set)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_set_cursor_seek(cursor, cursor->position - 1u, result);
}

d7_ordered_status d7_ordered_set_cursor_move_next(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == d7_ordered_set_size(&cursor->set)) {
        return d7_ordered_set_empty(&cursor->set)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_set_cursor_seek(cursor, cursor->position + 1u, result);
}

d7_ordered_status d7_ordered_set_cursor_try_insert(
    const d7_ordered_set_cursor* cursor,
    const void* item,
    bool* inserted,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || item == NULL
        || inserted == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const bool changed = !d7_ordered_set_contains(&cursor->set, item);
    if (!changed) {
        d7_ordered_set_cursor candidate;
        const d7_ordered_status status =
            d7_ordered_set_cursor_clone(cursor, &candidate);
        if (status == D7_ORDERED_OK) {
            d7_ordered_set_cursor_publish(cursor, result, &candidate);
            *inserted = false;
        }
        return status;
    }
    if (cursor->position == SIZE_MAX) {
        return D7_ORDERED_OVERFLOW;
    }
    d7_ordered_set set;
    const d7_ordered_status status = d7_ordered_set_insert(
        &cursor->set, cursor->position, item, &set);
    if (status == D7_ORDERED_OK) {
        (void)d7_ordered_set_cursor_publish_set(
            cursor, &set, cursor->position + 1u, result);
        *inserted = true;
    }
    return status;
}

d7_ordered_status d7_ordered_set_cursor_insert(
    const d7_ordered_set_cursor* cursor,
    const void* item,
    d7_ordered_set_cursor* result)
{
    bool inserted = false;
    return d7_ordered_set_cursor_try_insert(cursor, item, &inserted, result);
}

static d7_ordered_status d7_ordered_set_cursor_delete_at(
    const d7_ordered_set_cursor* cursor,
    size_t index,
    size_t position,
    d7_ordered_set_cursor* result)
{
    d7_ordered_set set;
    const d7_ordered_status status =
        d7_ordered_set_remove_at(&cursor->set, index, &set);
    return status == D7_ORDERED_OK
        ? d7_ordered_set_cursor_publish_set(cursor, &set, position, result)
        : status;
}

d7_ordered_status d7_ordered_set_cursor_delete_previous(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return d7_ordered_set_empty(&cursor->set)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_set_cursor_delete_at(
        cursor, cursor->position - 1u, cursor->position - 1u, result);
}

d7_ordered_status d7_ordered_set_cursor_delete_next(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set_cursor* result)
{
    if (!d7_ordered_set_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == d7_ordered_set_size(&cursor->set)) {
        return d7_ordered_set_empty(&cursor->set)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_set_cursor_delete_at(
        cursor, cursor->position, cursor->position, result);
}

d7_ordered_status d7_ordered_set_cursor_snapshot(
    const d7_ordered_set_cursor* cursor,
    d7_ordered_set* result)
{
    return !d7_ordered_set_cursor_is_valid(cursor) || result == NULL
        ? D7_ORDERED_INVALID_ARGUMENT
        : d7_ordered_set_clone(&cursor->set, result);
}

static bool d7_ordered_map_cursor_is_valid(
    const d7_ordered_map_cursor* cursor)
{
    return cursor != NULL && cursor->map.context != NULL
        && cursor->position <= d7_ordered_map_size(&cursor->map);
}

static void d7_ordered_map_cursor_publish(
    const d7_ordered_map_cursor* source,
    d7_ordered_map_cursor* result,
    d7_ordered_map_cursor* candidate)
{
    if (result == source) {
        d7_ordered_map_cursor_destroy(result);
    }
    d7_ordered_map_cursor_move(result, candidate);
}

static d7_ordered_status d7_ordered_map_cursor_publish_map(
    const d7_ordered_map_cursor* source,
    d7_ordered_map* map,
    size_t position,
    d7_ordered_map_cursor* result)
{
    d7_ordered_map_cursor candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    d7_ordered_map_move(&candidate.map, map);
    candidate.position = position;
    d7_ordered_map_cursor_publish(source, result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_get_cursor(
    const d7_ordered_map* map,
    size_t position,
    d7_ordered_map_cursor* result)
{
    if (map == NULL || map->context == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position > d7_ordered_map_size(map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    d7_ordered_map_cursor candidate;
    const d7_ordered_status status =
        d7_ordered_map_clone(map, &candidate.map);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    candidate.position = position;
    d7_ordered_map_cursor_move(result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_get_cursor_at_key(
    const d7_ordered_map* map,
    const void* equal_key,
    bool* found,
    d7_ordered_map_cursor* result)
{
    if (map == NULL || equal_key == NULL || found == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    size_t position = 0u;
    const bool hit = d7_ordered_map_index_of_key(map, equal_key, &position);
    if (!hit) {
        position = d7_ordered_map_size(map);
    }
    const d7_ordered_status status =
        d7_ordered_map_get_cursor(map, position, result);
    if (status == D7_ORDERED_OK) {
        *found = hit;
    }
    return status;
}

d7_ordered_status d7_ordered_map_cursor_clone(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (result == cursor) {
        return D7_ORDERED_OK;
    }
    return d7_ordered_map_get_cursor(&cursor->map, cursor->position, result);
}

void d7_ordered_map_cursor_move(
    d7_ordered_map_cursor* destination,
    d7_ordered_map_cursor* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        (void)memset(destination, 0, sizeof(*destination));
        d7_ordered_map_move(&destination->map, &source->map);
        destination->position = source->position;
        source->position = 0u;
    }
}

void d7_ordered_map_cursor_destroy(d7_ordered_map_cursor* cursor)
{
    if (cursor != NULL) {
        d7_ordered_map_destroy(&cursor->map);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool d7_ordered_map_cursor_valid(const d7_ordered_map_cursor* cursor)
{
    return d7_ordered_map_cursor_is_valid(cursor);
}

size_t d7_ordered_map_cursor_count(const d7_ordered_map_cursor* cursor)
{
    return d7_ordered_map_cursor_is_valid(cursor)
        ? d7_ordered_map_size(&cursor->map) : 0u;
}

size_t d7_ordered_map_cursor_position(const d7_ordered_map_cursor* cursor)
{
    return d7_ordered_map_cursor_is_valid(cursor) ? cursor->position : 0u;
}

bool d7_ordered_map_cursor_is_at_start(const d7_ordered_map_cursor* cursor)
{
    return d7_ordered_map_cursor_is_valid(cursor) && cursor->position == 0u;
}

bool d7_ordered_map_cursor_is_at_end(const d7_ordered_map_cursor* cursor)
{
    return d7_ordered_map_cursor_is_valid(cursor)
        && cursor->position == d7_ordered_map_size(&cursor->map);
}

static d7_ordered_status d7_ordered_map_cursor_peek(
    const d7_ordered_map_cursor* cursor,
    size_t position,
    bool* found,
    const void** key,
    const void** value)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || found == NULL
        || key == NULL || value == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position >= d7_ordered_map_size(&cursor->map)) {
        *found = false;
        return D7_ORDERED_OK;
    }
    const d7_ordered_status status =
        d7_ordered_map_entry_at(&cursor->map, position, key, value);
    if (status == D7_ORDERED_OK) {
        *found = true;
    }
    return status;
}

d7_ordered_status d7_ordered_map_cursor_try_peek_previous(
    const d7_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || found == NULL
        || key == NULL || value == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        *found = false;
        return D7_ORDERED_OK;
    }
    return d7_ordered_map_cursor_peek(
        cursor, cursor->position - 1u, found, key, value);
}

d7_ordered_status d7_ordered_map_cursor_try_peek_next(
    const d7_ordered_map_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    return d7_ordered_map_cursor_peek(
        cursor, cursor == NULL ? 0u : cursor->position, found, key, value);
}

d7_ordered_status d7_ordered_map_cursor_seek(
    const d7_ordered_map_cursor* cursor,
    size_t position,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position > d7_ordered_map_size(&cursor->map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return D7_ORDERED_OK;
    }
    d7_ordered_map_cursor candidate;
    const d7_ordered_status status =
        d7_ordered_map_get_cursor(&cursor->map, position, &candidate);
    if (status == D7_ORDERED_OK) {
        d7_ordered_map_cursor_publish(cursor, result, &candidate);
    }
    return status;
}

d7_ordered_status d7_ordered_map_cursor_move_previous(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return d7_ordered_map_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_map_cursor_seek(cursor, cursor->position - 1u, result);
}

d7_ordered_status d7_ordered_map_cursor_move_next(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == d7_ordered_map_size(&cursor->map)) {
        return d7_ordered_map_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_map_cursor_seek(cursor, cursor->position + 1u, result);
}

d7_ordered_status d7_ordered_map_cursor_insert(
    const d7_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || key == NULL
        || value == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == SIZE_MAX) {
        return D7_ORDERED_OVERFLOW;
    }
    d7_ordered_map map;
    const d7_ordered_status status = d7_ordered_map_insert(
        &cursor->map, cursor->position, key, value, &map);
    return status == D7_ORDERED_OK
        ? d7_ordered_map_cursor_publish_map(
            cursor, &map, cursor->position + 1u, result)
        : status;
}

d7_ordered_status d7_ordered_map_cursor_try_insert(
    const d7_ordered_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || key == NULL
        || value == NULL || inserted == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    size_t position = 0u;
    if (d7_ordered_map_index_of_key(&cursor->map, key, &position)) {
        d7_ordered_map_cursor candidate;
        const d7_ordered_status status =
            d7_ordered_map_get_cursor(&cursor->map, position, &candidate);
        if (status == D7_ORDERED_OK) {
            d7_ordered_map_cursor_publish(cursor, result, &candidate);
            *inserted = false;
        }
        return status;
    }
    const d7_ordered_status status =
        d7_ordered_map_cursor_insert(cursor, key, value, result);
    if (status == D7_ORDERED_OK) {
        *inserted = true;
    }
    return status;
}

d7_ordered_status d7_ordered_map_cursor_set_next_value(
    const d7_ordered_map_cursor* cursor,
    const void* value,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const void* key = NULL;
    const void* old_value = NULL;
    bool found = false;
    d7_ordered_status status = d7_ordered_map_cursor_try_peek_next(
        cursor, &found, &key, &old_value);
    (void)old_value;
    if (status != D7_ORDERED_OK) {
        return status;
    }
    if (!found) {
        return d7_ordered_map_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    d7_ordered_map map;
    status = d7_ordered_map_set(&cursor->map, key, value, &map);
    return status == D7_ORDERED_OK
        ? d7_ordered_map_cursor_publish_map(
            cursor, &map, cursor->position, result)
        : status;
}

static d7_ordered_status d7_ordered_map_cursor_delete_at(
    const d7_ordered_map_cursor* cursor,
    size_t index,
    size_t position,
    d7_ordered_map_cursor* result)
{
    d7_ordered_map map;
    const d7_ordered_status status =
        d7_ordered_map_remove_at(&cursor->map, index, &map);
    return status == D7_ORDERED_OK
        ? d7_ordered_map_cursor_publish_map(cursor, &map, position, result)
        : status;
}

d7_ordered_status d7_ordered_map_cursor_delete_previous(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0u) {
        return d7_ordered_map_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_map_cursor_delete_at(
        cursor, cursor->position - 1u, cursor->position - 1u, result);
}

d7_ordered_status d7_ordered_map_cursor_delete_next(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map_cursor* result)
{
    if (!d7_ordered_map_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == d7_ordered_map_size(&cursor->map)) {
        return d7_ordered_map_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_map_cursor_delete_at(
        cursor, cursor->position, cursor->position, result);
}

d7_ordered_status d7_ordered_map_cursor_snapshot(
    const d7_ordered_map_cursor* cursor,
    d7_ordered_map* result)
{
    return !d7_ordered_map_cursor_is_valid(cursor) || result == NULL
        ? D7_ORDERED_INVALID_ARGUMENT
        : d7_ordered_map_clone(&cursor->map, result);
}

static bool d7_ordered_multimap_cursor_is_valid(
    const d7_ordered_multimap_cursor* cursor)
{
    return cursor != NULL && cursor->map.context != NULL
        && cursor->position >= 0
        && cursor->position <= d7_ordered_multimap_pair_count(&cursor->map);
}

static void d7_ordered_multimap_cursor_publish(
    const d7_ordered_multimap_cursor* source,
    d7_ordered_multimap_cursor* result,
    d7_ordered_multimap_cursor* candidate)
{
    if (result == source) {
        d7_ordered_multimap_cursor_destroy(result);
    }
    d7_ordered_multimap_cursor_move(result, candidate);
}

static d7_ordered_status d7_ordered_multimap_cursor_publish_map(
    const d7_ordered_multimap_cursor* source,
    d7_ordered_multimap* map,
    int64_t position,
    d7_ordered_multimap_cursor* result)
{
    d7_ordered_multimap_cursor candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    d7_ordered_multimap_move(&candidate.map, map);
    candidate.position = position;
    d7_ordered_multimap_cursor_publish(source, result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_get_cursor(
    const d7_ordered_multimap* map,
    int64_t position,
    d7_ordered_multimap_cursor* result)
{
    if (map == NULL || map->context == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position < 0 || position > d7_ordered_multimap_pair_count(map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    d7_ordered_multimap_cursor candidate;
    const d7_ordered_status status =
        d7_ordered_multimap_clone(map, &candidate.map);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    candidate.position = position;
    d7_ordered_multimap_cursor_move(result, &candidate);
    return D7_ORDERED_OK;
}

typedef struct d7_ordered_multimap_rank_context {
    int64_t target;
    int64_t position;
    const void* key;
    const void* value;
    bool found;
} d7_ordered_multimap_rank_context;

static void d7_ordered_multimap_rank_visit(
    const void* key,
    const void* value,
    void* raw_context)
{
    d7_ordered_multimap_rank_context* context =
        (d7_ordered_multimap_rank_context*)raw_context;
    if (!context->found && context->position == context->target) {
        context->key = key;
        context->value = value;
        context->found = true;
    }
    if (context->position < INT64_MAX) {
        ++context->position;
    }
}

static d7_ordered_status d7_ordered_multimap_entry_at(
    const d7_ordered_multimap* map,
    int64_t position,
    bool* found,
    const void** key,
    const void** value)
{
    if (map == NULL || map->context == NULL || found == NULL
        || key == NULL || value == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position < 0 || position >= d7_ordered_multimap_pair_count(map)) {
        *found = false;
        return D7_ORDERED_OK;
    }
    d7_ordered_multimap_rank_context context = {
        position, 0, NULL, NULL, false };
    const d7_ordered_status status =
        d7_ordered_multimap_visit(map, d7_ordered_multimap_rank_visit, &context);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    if (!context.found) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    *found = true;
    *key = context.key;
    *value = context.value;
    return D7_ORDERED_OK;
}

static bool d7_ordered_map_policy_keys_equal(
    const d7_ordered_map_policy* policy,
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

static bool d7_ordered_policy_items_equal(
    const d7_ordered_policy* policy,
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

typedef struct d7_ordered_multimap_search_context {
    const d7_ordered_map_policy* key_policy;
    const d7_ordered_policy* value_policy;
    const void* key;
    const void* value;
    int64_t position;
    int64_t result;
    bool compare_value;
} d7_ordered_multimap_search_context;

static void d7_ordered_multimap_search_visit(
    const void* key,
    const void* value,
    void* raw_context)
{
    d7_ordered_multimap_search_context* context =
        (d7_ordered_multimap_search_context*)raw_context;
    if (context->result < 0
        && d7_ordered_map_policy_keys_equal(
            context->key_policy, key, context->key)
        && (!context->compare_value
            || d7_ordered_policy_items_equal(
                context->value_policy, value, context->value))) {
        context->result = context->position;
    }
    if (context->position < INT64_MAX) {
        ++context->position;
    }
}

static d7_ordered_status d7_ordered_multimap_find_position(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    bool compare_value,
    int64_t* position)
{
    if (map == NULL || map->context == NULL || key == NULL || position == NULL
        || (compare_value && value == NULL)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const d7_ordered_set* values = NULL;
    const bool key_found =
        d7_ordered_multimap_try_get_values(map, key, &values);
    if (!key_found || (compare_value && !d7_ordered_set_contains(values, value))) {
        *position = -1;
        return D7_ORDERED_OK;
    }
    d7_ordered_multimap_search_context context = {
        d7_ordered_map_policy_of(&map->groups),
        d7_ordered_set_policy(values),
        key,
        value,
        0,
        -1,
        compare_value };
    const d7_ordered_status status = d7_ordered_multimap_visit(
        map, d7_ordered_multimap_search_visit, &context);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    if (context.result < 0) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    *position = context.result;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_multimap_get_cursor_at(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    bool compare_value,
    bool* found,
    d7_ordered_multimap_cursor* result)
{
    if (found == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    int64_t position = -1;
    d7_ordered_status status = d7_ordered_multimap_find_position(
        map, key, value, compare_value, &position);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    const bool hit = position >= 0;
    if (!hit) {
        position = d7_ordered_multimap_pair_count(map);
    }
    status = d7_ordered_multimap_get_cursor(map, position, result);
    if (status == D7_ORDERED_OK) {
        *found = hit;
    }
    return status;
}

d7_ordered_status d7_ordered_multimap_get_cursor_at_pair(
    const d7_ordered_multimap* map,
    const void* equal_key,
    const void* equal_value,
    bool* found,
    d7_ordered_multimap_cursor* result)
{
    return d7_ordered_multimap_get_cursor_at(
        map, equal_key, equal_value, true, found, result);
}

d7_ordered_status d7_ordered_multimap_get_cursor_at_group(
    const d7_ordered_multimap* map,
    const void* equal_key,
    bool* found,
    d7_ordered_multimap_cursor* result)
{
    return d7_ordered_multimap_get_cursor_at(
        map, equal_key, NULL, false, found, result);
}

d7_ordered_status d7_ordered_multimap_cursor_clone(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (result == cursor) {
        return D7_ORDERED_OK;
    }
    return d7_ordered_multimap_get_cursor(
        &cursor->map, cursor->position, result);
}

void d7_ordered_multimap_cursor_move(
    d7_ordered_multimap_cursor* destination,
    d7_ordered_multimap_cursor* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        (void)memset(destination, 0, sizeof(*destination));
        d7_ordered_multimap_move(&destination->map, &source->map);
        destination->position = source->position;
        source->position = 0;
    }
}

void d7_ordered_multimap_cursor_destroy(d7_ordered_multimap_cursor* cursor)
{
    if (cursor != NULL) {
        d7_ordered_multimap_destroy(&cursor->map);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool d7_ordered_multimap_cursor_valid(
    const d7_ordered_multimap_cursor* cursor)
{
    return d7_ordered_multimap_cursor_is_valid(cursor);
}

int64_t d7_ordered_multimap_cursor_count(
    const d7_ordered_multimap_cursor* cursor)
{
    return d7_ordered_multimap_cursor_is_valid(cursor)
        ? d7_ordered_multimap_pair_count(&cursor->map) : 0;
}

int64_t d7_ordered_multimap_cursor_position(
    const d7_ordered_multimap_cursor* cursor)
{
    return d7_ordered_multimap_cursor_is_valid(cursor)
        ? cursor->position : 0;
}

bool d7_ordered_multimap_cursor_is_at_start(
    const d7_ordered_multimap_cursor* cursor)
{
    return d7_ordered_multimap_cursor_is_valid(cursor)
        && cursor->position == 0;
}

bool d7_ordered_multimap_cursor_is_at_end(
    const d7_ordered_multimap_cursor* cursor)
{
    return d7_ordered_multimap_cursor_is_valid(cursor)
        && cursor->position == d7_ordered_multimap_pair_count(&cursor->map);
}

d7_ordered_status d7_ordered_multimap_cursor_try_peek_previous(
    const d7_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || found == NULL
        || key == NULL || value == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        *found = false;
        return D7_ORDERED_OK;
    }
    return d7_ordered_multimap_entry_at(
        &cursor->map, cursor->position - 1, found, key, value);
}

d7_ordered_status d7_ordered_multimap_cursor_try_peek_next(
    const d7_ordered_multimap_cursor* cursor,
    bool* found,
    const void** key,
    const void** value)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_multimap_entry_at(
        &cursor->map, cursor->position, found, key, value);
}

d7_ordered_status d7_ordered_multimap_cursor_seek(
    const d7_ordered_multimap_cursor* cursor,
    int64_t position,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (position < 0 || position > d7_ordered_multimap_pair_count(&cursor->map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return D7_ORDERED_OK;
    }
    d7_ordered_multimap_cursor candidate;
    const d7_ordered_status status =
        d7_ordered_multimap_get_cursor(&cursor->map, position, &candidate);
    if (status == D7_ORDERED_OK) {
        d7_ordered_multimap_cursor_publish(cursor, result, &candidate);
    }
    return status;
}

d7_ordered_status d7_ordered_multimap_cursor_move_previous(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return d7_ordered_multimap_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_multimap_cursor_seek(cursor, cursor->position - 1, result);
}

d7_ordered_status d7_ordered_multimap_cursor_move_next(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == d7_ordered_multimap_pair_count(&cursor->map)) {
        return d7_ordered_multimap_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_multimap_cursor_seek(cursor, cursor->position + 1, result);
}

typedef struct d7_ordered_multimap_group_end_context {
    const d7_ordered_map_policy* key_policy;
    const void* key;
    int64_t position;
    int64_t group_end;
    bool found;
} d7_ordered_multimap_group_end_context;

static void d7_ordered_multimap_group_end_visit(
    const void* key,
    const void* value,
    void* raw_context)
{
    d7_ordered_multimap_group_end_context* context =
        (d7_ordered_multimap_group_end_context*)raw_context;
    (void)value;
    if (d7_ordered_map_policy_keys_equal(context->key_policy, key, context->key)) {
        context->found = true;
        context->group_end =
            context->position < INT64_MAX ? context->position + 1 : INT64_MAX;
    }
    if (context->position < INT64_MAX) {
        ++context->position;
    }
}

/* Pair rank of the gap immediately after the last pair of an equivalent key
 * group, or the end gap when no such group is present. Key groups are contiguous
 * in the flattened enumeration, so this walks the pairs once and consults only
 * the key equality. Deriving the post-insert gap this way rather than re-scanning
 * for the inserted pair by value keeps insertion total for a value that is not
 * reflexive under the value policy, such as a NaN. */
static d7_ordered_status d7_ordered_multimap_group_end(
    const d7_ordered_multimap* map,
    const void* key,
    int64_t* group_end)
{
    if (map == NULL || map->context == NULL || key == NULL || group_end == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_multimap_group_end_context context = {
        d7_ordered_map_policy_of(&map->groups),
        key,
        0,
        0,
        false };
    const d7_ordered_status status = d7_ordered_multimap_visit(
        map, d7_ordered_multimap_group_end_visit, &context);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    *group_end = context.found
        ? context.group_end
        : d7_ordered_multimap_pair_count(map);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_cursor_try_add(
    const d7_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || key == NULL
        || value == NULL || inserted == NULL || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (d7_ordered_multimap_contains(&cursor->map, key, value)) {
        d7_ordered_multimap_cursor candidate;
        const d7_ordered_status status =
            d7_ordered_multimap_cursor_clone(cursor, &candidate);
        if (status == D7_ORDERED_OK) {
            d7_ordered_multimap_cursor_publish(cursor, result, &candidate);
            *inserted = false;
        }
        return status;
    }
    d7_ordered_multimap map;
    d7_ordered_status status =
        d7_ordered_multimap_add(&cursor->map, key, value, &map);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    int64_t position = 0;
    status = d7_ordered_multimap_group_end(&map, key, &position);
    if (status != D7_ORDERED_OK) {
        d7_ordered_multimap_destroy(&map);
        return status;
    }
    if (position < 0 || position > d7_ordered_multimap_pair_count(&map)) {
        d7_ordered_multimap_destroy(&map);
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    (void)d7_ordered_multimap_cursor_publish_map(
        cursor, &map, position, result);
    *inserted = true;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_cursor_add(
    const d7_ordered_multimap_cursor* cursor,
    const void* key,
    const void* value,
    d7_ordered_multimap_cursor* result)
{
    bool inserted = false;
    return d7_ordered_multimap_cursor_try_add(
        cursor, key, value, &inserted, result);
}

static d7_ordered_status d7_ordered_multimap_cursor_delete(
    const d7_ordered_multimap_cursor* cursor,
    int64_t rank,
    int64_t position,
    d7_ordered_multimap_cursor* result)
{
    bool found = false;
    const void* key = NULL;
    const void* value = NULL;
    d7_ordered_status status = d7_ordered_multimap_entry_at(
        &cursor->map, rank, &found, &key, &value);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    if (!found) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    d7_ordered_multimap map;
    status = d7_ordered_multimap_remove(&cursor->map, key, value, &map);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    /* The pair was located by rank, but a value that is not reflexive under the
     * value policy (such as a NaN) is one that content-based removal cannot find.
     * Publishing the unchanged version at the shifted gap would report a deletion
     * that removed nothing, so a no-op is a failure, not a false success. The
     * staged version is discarded and *result is left untouched. */
    if (d7_ordered_multimap_pair_count(&map)
        == d7_ordered_multimap_pair_count(&cursor->map)) {
        d7_ordered_multimap_destroy(&map);
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    return d7_ordered_multimap_cursor_publish_map(cursor, &map, position, result);
}

d7_ordered_status d7_ordered_multimap_cursor_delete_previous(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return d7_ordered_multimap_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_multimap_cursor_delete(
        cursor, cursor->position - 1, cursor->position - 1, result);
}

d7_ordered_status d7_ordered_multimap_cursor_delete_next(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap_cursor* result)
{
    if (!d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (cursor->position == d7_ordered_multimap_pair_count(&cursor->map)) {
        return d7_ordered_multimap_empty(&cursor->map)
            ? D7_ORDERED_EMPTY : D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_multimap_cursor_delete(
        cursor, cursor->position, cursor->position, result);
}

d7_ordered_status d7_ordered_multimap_cursor_snapshot(
    const d7_ordered_multimap_cursor* cursor,
    d7_ordered_multimap* result)
{
    return !d7_ordered_multimap_cursor_is_valid(cursor) || result == NULL
        ? D7_ORDERED_INVALID_ARGUMENT
        : d7_ordered_multimap_clone(&cursor->map, result);
}
