/*
 * Persistent Patricia (big-endian radix) maps and sets over 32- and 64-bit integer keys.
 *
 * Branch nodes store a common prefix and a discriminating bit, so a lookup is bounded by the key
 * width rather than by the entry count, and keys are visited in signed numeric order. The four
 * collections are declared through macros because their operations differ only in key type. Every
 * operation returns a new version and leaves its inputs valid, sharing unchanged structure, so an
 * edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_HAMT_PATRICIA_H
#define DURABLE7_HAMT_PATRICIA_H

#include <durable7/hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct d7_patricia_value_policy {
    d7_hamt_equal_fn equal;
    d7_hamt_retain_fn retain;
    d7_hamt_release_fn release;
    void *context;
} d7_patricia_value_policy;

typedef struct d7_patricia_map {
    const struct d7_patricia_node *root;
    size_t count;
    d7_patricia_value_policy policy;
} d7_patricia_map;
/* A persistent Patricia map over signed 32-bit keys. */
typedef d7_patricia_map d7_int_map;
/* A persistent Patricia map over signed 64-bit keys. */
typedef d7_patricia_map d7_long_map;
/* A persistent Patricia set over signed 32-bit keys. */
typedef struct d7_int_set { d7_int_map map; } d7_int_set;
/* A persistent Patricia set over signed 64-bit keys. */
typedef struct d7_long_set { d7_long_map map; } d7_long_set;

/* Immutable cursors own one exact ref-counted collection version. Their position is an
 * ordered gap in 0..count. Initialize them only through a cursor factory or clone, and
 * destroy every initialized cursor. Related versions use the Patricia nodes' non-atomic
 * reference counts and therefore require the same external synchronization as maps/sets. */
typedef struct d7_patricia_map_cursor {
    d7_patricia_map map;
    size_t position;
} d7_patricia_map_cursor;
/* A gap cursor over one 32-bit Patricia map version. */
typedef d7_patricia_map_cursor d7_int_map_cursor;
/* A gap cursor over one 64-bit Patricia map version. */
typedef d7_patricia_map_cursor d7_long_map_cursor;
/* A gap cursor over one 32-bit Patricia set version. */
typedef struct d7_int_set_cursor { d7_int_map_cursor inner; } d7_int_set_cursor;
/* A gap cursor over one 64-bit Patricia set version. */
typedef struct d7_long_set_cursor { d7_long_map_cursor inner; } d7_long_set_cursor;

typedef void (*d7_int_map_visitor)(int32_t key, const void *value, void *context);
typedef void (*d7_long_map_visitor)(int64_t key, const void *value, void *context);
typedef const void *(*d7_int_map_combine_fn)(
    int32_t key, const void *left, const void *right, void *context);
typedef const void *(*d7_long_map_combine_fn)(
    int64_t key, const void *left, const void *right, void *context);
typedef void (*d7_int_set_visitor)(int32_t value, void *context);
typedef void (*d7_long_set_visitor)(int64_t value, void *context);

/* The default value policy: values are compared by pointer and neither retained nor released. */
d7_patricia_value_policy d7_patricia_value_policy_default(void);

/* Declares one Patricia map's whole API. The four collections differ only in key type, so their
 * prototypes are written once here and instantiated per key width rather than repeated.
 *
 * `prefix` names the resulting functions, `map_type` the handle, `key_type` the integer key, and
 * `visitor_type` and `combine_type` the callbacks its iteration and merging operations take. */
#define D7_DECLARE_PATRICIA_MAP(prefix, map_type, key_type, visitor_type, combine_type) \
    map_type prefix##_create(const d7_patricia_value_policy *policy); \
    map_type prefix##_clone(const map_type *map); \
    void prefix##_destroy(map_type *map); \
    size_t prefix##_count(const map_type *map); \
    bool prefix##_try_get(const map_type *map, key_type key, const void **value); \
    d7_hamt_status prefix##_set(const map_type *map, key_type key, const void *value, map_type *result); \
    d7_hamt_status prefix##_remove(const map_type *map, key_type key, map_type *result); \
    d7_hamt_status prefix##_union(const map_type *left, const map_type *right, map_type *result); \
    d7_hamt_status prefix##_union_with(const map_type *left, const map_type *right, combine_type combine, void *context, map_type *result); \
    d7_hamt_status prefix##_intersect(const map_type *left, const map_type *right, map_type *result); \
    d7_hamt_status prefix##_intersect_with(const map_type *left, const map_type *right, combine_type combine, void *context, map_type *result); \
    d7_hamt_status prefix##_except(const map_type *left, const map_type *right, map_type *result); \
    void prefix##_visit(const map_type *map, visitor_type visitor, void *context); \
    bool prefix##_shares_root(const map_type *left, const map_type *right)

D7_DECLARE_PATRICIA_MAP(d7_int_map, d7_int_map, int32_t, d7_int_map_visitor, d7_int_map_combine_fn);
D7_DECLARE_PATRICIA_MAP(d7_long_map, d7_long_map, int64_t, d7_long_map_visitor, d7_long_map_combine_fn);

/* Declares one Patricia map cursor's whole API: gap positioning, bound searches, movement, and the
 * reads at either side of the gap. */
#define D7_DECLARE_PATRICIA_MAP_CURSOR(prefix, map_type, cursor_type, key_type) \
    d7_hamt_status prefix##_cursor_create(const map_type *map, size_t position, cursor_type *result); \
    d7_hamt_status prefix##_cursor_at_start(const map_type *map, cursor_type *result); \
    d7_hamt_status prefix##_cursor_at_end(const map_type *map, cursor_type *result); \
    d7_hamt_status prefix##_cursor_lower_bound(const map_type *map, key_type key, cursor_type *result); \
    d7_hamt_status prefix##_cursor_upper_bound(const map_type *map, key_type key, cursor_type *result); \
    d7_hamt_status prefix##_cursor_at_key(const map_type *map, key_type key, bool *found, cursor_type *result); \
    d7_hamt_status prefix##_cursor_clone(const cursor_type *cursor, cursor_type *result); \
    void prefix##_cursor_destroy(cursor_type *cursor); \
    size_t prefix##_cursor_count(const cursor_type *cursor); \
    size_t prefix##_cursor_position(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_start(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_end(const cursor_type *cursor); \
    bool prefix##_cursor_try_peek_previous(const cursor_type *cursor, key_type *key, const void **value); \
    bool prefix##_cursor_try_peek_next(const cursor_type *cursor, key_type *key, const void **value); \
    d7_hamt_status prefix##_cursor_move_previous(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_move_next(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_seek(const cursor_type *cursor, size_t position, cursor_type *result); \
    d7_hamt_status prefix##_cursor_insert(const cursor_type *cursor, key_type key, const void *value, cursor_type *result); \
    d7_hamt_status prefix##_cursor_put(const cursor_type *cursor, key_type key, const void *value, cursor_type *result); \
    d7_hamt_status prefix##_cursor_set_next_value(const cursor_type *cursor, const void *value, cursor_type *result); \
    d7_hamt_status prefix##_cursor_delete_previous(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_delete_next(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_snapshot(const cursor_type *cursor, map_type *result)

D7_DECLARE_PATRICIA_MAP_CURSOR(d7_int_map, d7_int_map, d7_int_map_cursor, int32_t);
D7_DECLARE_PATRICIA_MAP_CURSOR(d7_long_map, d7_long_map, d7_long_map_cursor, int64_t);

/* Declares one Patricia set's whole API, including its set algebra. */
#define D7_DECLARE_PATRICIA_SET(prefix, set_type, key_type, visitor_type) \
    set_type prefix##_create(void); \
    set_type prefix##_clone(const set_type *set); \
    void prefix##_destroy(set_type *set); \
    size_t prefix##_count(const set_type *set); \
    bool prefix##_contains(const set_type *set, key_type value); \
    d7_hamt_status prefix##_add(const set_type *set, key_type value, set_type *result); \
    d7_hamt_status prefix##_remove(const set_type *set, key_type value, set_type *result); \
    d7_hamt_status prefix##_union(const set_type *left, const set_type *right, set_type *result); \
    d7_hamt_status prefix##_intersect(const set_type *left, const set_type *right, set_type *result); \
    d7_hamt_status prefix##_except(const set_type *left, const set_type *right, set_type *result); \
    void prefix##_visit(const set_type *set, visitor_type visitor, void *context)

D7_DECLARE_PATRICIA_SET(d7_int_set, d7_int_set, int32_t, d7_int_set_visitor);
D7_DECLARE_PATRICIA_SET(d7_long_set, d7_long_set, int64_t, d7_long_set_visitor);

/* Declares one Patricia set cursor's whole API. */
#define D7_DECLARE_PATRICIA_SET_CURSOR(prefix, set_type, cursor_type, key_type) \
    d7_hamt_status prefix##_cursor_create(const set_type *set, size_t position, cursor_type *result); \
    d7_hamt_status prefix##_cursor_at_start(const set_type *set, cursor_type *result); \
    d7_hamt_status prefix##_cursor_at_end(const set_type *set, cursor_type *result); \
    d7_hamt_status prefix##_cursor_lower_bound(const set_type *set, key_type value, cursor_type *result); \
    d7_hamt_status prefix##_cursor_upper_bound(const set_type *set, key_type value, cursor_type *result); \
    d7_hamt_status prefix##_cursor_at_item(const set_type *set, key_type value, bool *found, cursor_type *result); \
    d7_hamt_status prefix##_cursor_clone(const cursor_type *cursor, cursor_type *result); \
    void prefix##_cursor_destroy(cursor_type *cursor); \
    size_t prefix##_cursor_count(const cursor_type *cursor); \
    size_t prefix##_cursor_position(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_start(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_end(const cursor_type *cursor); \
    bool prefix##_cursor_try_peek_previous(const cursor_type *cursor, key_type *value); \
    bool prefix##_cursor_try_peek_next(const cursor_type *cursor, key_type *value); \
    d7_hamt_status prefix##_cursor_move_previous(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_move_next(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_seek(const cursor_type *cursor, size_t position, cursor_type *result); \
    d7_hamt_status prefix##_cursor_insert(const cursor_type *cursor, key_type value, cursor_type *result); \
    d7_hamt_status prefix##_cursor_delete_previous(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_delete_next(const cursor_type *cursor, cursor_type *result); \
    d7_hamt_status prefix##_cursor_snapshot(const cursor_type *cursor, set_type *result)

D7_DECLARE_PATRICIA_SET_CURSOR(d7_int_set, d7_int_set, d7_int_set_cursor, int32_t);
D7_DECLARE_PATRICIA_SET_CURSOR(d7_long_set, d7_long_set, d7_long_set_cursor, int64_t);

#undef D7_DECLARE_PATRICIA_MAP
#undef D7_DECLARE_PATRICIA_MAP_CURSOR
#undef D7_DECLARE_PATRICIA_SET
#undef D7_DECLARE_PATRICIA_SET_CURSOR

#ifdef __cplusplus
}
#endif
#endif
