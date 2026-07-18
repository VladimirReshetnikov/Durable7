#ifndef TOOLS_DATA_STRUCTURES_HAMT_PATRICIA_H
#define TOOLS_DATA_STRUCTURES_HAMT_PATRICIA_H

#include <Tools/DataStructures/Hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tds_patricia_value_policy {
    tds_hamt_equal_fn equal;
    tds_hamt_retain_fn retain;
    tds_hamt_release_fn release;
    void *context;
} tds_patricia_value_policy;

typedef struct tds_patricia_map {
    const struct tds_patricia_node *root;
    size_t count;
    tds_patricia_value_policy policy;
} tds_patricia_map;
typedef tds_patricia_map tds_int_map;
typedef tds_patricia_map tds_long_map;
typedef struct tds_int_set { tds_int_map map; } tds_int_set;
typedef struct tds_long_set { tds_long_map map; } tds_long_set;

/* Immutable cursors own one exact ref-counted collection version. Their position is an
 * ordered gap in 0..count. Initialize them only through a cursor factory or clone, and
 * destroy every initialized cursor. Related versions use the Patricia nodes' non-atomic
 * reference counts and therefore require the same external synchronization as maps/sets. */
typedef struct tds_patricia_map_cursor {
    tds_patricia_map map;
    size_t position;
} tds_patricia_map_cursor;
typedef tds_patricia_map_cursor tds_int_map_cursor;
typedef tds_patricia_map_cursor tds_long_map_cursor;
typedef struct tds_int_set_cursor { tds_int_map_cursor inner; } tds_int_set_cursor;
typedef struct tds_long_set_cursor { tds_long_map_cursor inner; } tds_long_set_cursor;

typedef void (*tds_int_map_visitor)(int32_t key, const void *value, void *context);
typedef void (*tds_long_map_visitor)(int64_t key, const void *value, void *context);
typedef const void *(*tds_int_map_combine_fn)(
    int32_t key, const void *left, const void *right, void *context);
typedef const void *(*tds_long_map_combine_fn)(
    int64_t key, const void *left, const void *right, void *context);
typedef void (*tds_int_set_visitor)(int32_t value, void *context);
typedef void (*tds_long_set_visitor)(int64_t value, void *context);

tds_patricia_value_policy tds_patricia_value_policy_default(void);

#define TDS_DECLARE_PATRICIA_MAP(prefix, map_type, key_type, visitor_type, combine_type) \
    map_type prefix##_create(const tds_patricia_value_policy *policy); \
    map_type prefix##_clone(const map_type *map); \
    void prefix##_destroy(map_type *map); \
    size_t prefix##_count(const map_type *map); \
    bool prefix##_try_get(const map_type *map, key_type key, const void **value); \
    tds_hamt_status prefix##_set(const map_type *map, key_type key, const void *value, map_type *result); \
    tds_hamt_status prefix##_remove(const map_type *map, key_type key, map_type *result); \
    tds_hamt_status prefix##_union(const map_type *left, const map_type *right, map_type *result); \
    tds_hamt_status prefix##_union_with(const map_type *left, const map_type *right, combine_type combine, void *context, map_type *result); \
    tds_hamt_status prefix##_intersect(const map_type *left, const map_type *right, map_type *result); \
    tds_hamt_status prefix##_intersect_with(const map_type *left, const map_type *right, combine_type combine, void *context, map_type *result); \
    tds_hamt_status prefix##_except(const map_type *left, const map_type *right, map_type *result); \
    void prefix##_visit(const map_type *map, visitor_type visitor, void *context); \
    bool prefix##_shares_root(const map_type *left, const map_type *right)

TDS_DECLARE_PATRICIA_MAP(tds_int_map, tds_int_map, int32_t, tds_int_map_visitor, tds_int_map_combine_fn);
TDS_DECLARE_PATRICIA_MAP(tds_long_map, tds_long_map, int64_t, tds_long_map_visitor, tds_long_map_combine_fn);

#define TDS_DECLARE_PATRICIA_MAP_CURSOR(prefix, map_type, cursor_type, key_type) \
    tds_hamt_status prefix##_cursor_create(const map_type *map, size_t position, cursor_type *result); \
    tds_hamt_status prefix##_cursor_at_start(const map_type *map, cursor_type *result); \
    tds_hamt_status prefix##_cursor_at_end(const map_type *map, cursor_type *result); \
    tds_hamt_status prefix##_cursor_lower_bound(const map_type *map, key_type key, cursor_type *result); \
    tds_hamt_status prefix##_cursor_upper_bound(const map_type *map, key_type key, cursor_type *result); \
    tds_hamt_status prefix##_cursor_at_key(const map_type *map, key_type key, bool *found, cursor_type *result); \
    tds_hamt_status prefix##_cursor_clone(const cursor_type *cursor, cursor_type *result); \
    void prefix##_cursor_destroy(cursor_type *cursor); \
    size_t prefix##_cursor_count(const cursor_type *cursor); \
    size_t prefix##_cursor_position(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_start(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_end(const cursor_type *cursor); \
    bool prefix##_cursor_try_peek_previous(const cursor_type *cursor, key_type *key, const void **value); \
    bool prefix##_cursor_try_peek_next(const cursor_type *cursor, key_type *key, const void **value); \
    tds_hamt_status prefix##_cursor_move_previous(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_move_next(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_seek(const cursor_type *cursor, size_t position, cursor_type *result); \
    tds_hamt_status prefix##_cursor_insert(const cursor_type *cursor, key_type key, const void *value, cursor_type *result); \
    tds_hamt_status prefix##_cursor_put(const cursor_type *cursor, key_type key, const void *value, cursor_type *result); \
    tds_hamt_status prefix##_cursor_set_next_value(const cursor_type *cursor, const void *value, cursor_type *result); \
    tds_hamt_status prefix##_cursor_delete_previous(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_delete_next(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_snapshot(const cursor_type *cursor, map_type *result)

TDS_DECLARE_PATRICIA_MAP_CURSOR(tds_int_map, tds_int_map, tds_int_map_cursor, int32_t);
TDS_DECLARE_PATRICIA_MAP_CURSOR(tds_long_map, tds_long_map, tds_long_map_cursor, int64_t);

#define TDS_DECLARE_PATRICIA_SET(prefix, set_type, key_type, visitor_type) \
    set_type prefix##_create(void); \
    set_type prefix##_clone(const set_type *set); \
    void prefix##_destroy(set_type *set); \
    size_t prefix##_count(const set_type *set); \
    bool prefix##_contains(const set_type *set, key_type value); \
    tds_hamt_status prefix##_add(const set_type *set, key_type value, set_type *result); \
    tds_hamt_status prefix##_remove(const set_type *set, key_type value, set_type *result); \
    tds_hamt_status prefix##_union(const set_type *left, const set_type *right, set_type *result); \
    tds_hamt_status prefix##_intersect(const set_type *left, const set_type *right, set_type *result); \
    tds_hamt_status prefix##_except(const set_type *left, const set_type *right, set_type *result); \
    void prefix##_visit(const set_type *set, visitor_type visitor, void *context)

TDS_DECLARE_PATRICIA_SET(tds_int_set, tds_int_set, int32_t, tds_int_set_visitor);
TDS_DECLARE_PATRICIA_SET(tds_long_set, tds_long_set, int64_t, tds_long_set_visitor);

#define TDS_DECLARE_PATRICIA_SET_CURSOR(prefix, set_type, cursor_type, key_type) \
    tds_hamt_status prefix##_cursor_create(const set_type *set, size_t position, cursor_type *result); \
    tds_hamt_status prefix##_cursor_at_start(const set_type *set, cursor_type *result); \
    tds_hamt_status prefix##_cursor_at_end(const set_type *set, cursor_type *result); \
    tds_hamt_status prefix##_cursor_lower_bound(const set_type *set, key_type value, cursor_type *result); \
    tds_hamt_status prefix##_cursor_upper_bound(const set_type *set, key_type value, cursor_type *result); \
    tds_hamt_status prefix##_cursor_at_item(const set_type *set, key_type value, bool *found, cursor_type *result); \
    tds_hamt_status prefix##_cursor_clone(const cursor_type *cursor, cursor_type *result); \
    void prefix##_cursor_destroy(cursor_type *cursor); \
    size_t prefix##_cursor_count(const cursor_type *cursor); \
    size_t prefix##_cursor_position(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_start(const cursor_type *cursor); \
    bool prefix##_cursor_is_at_end(const cursor_type *cursor); \
    bool prefix##_cursor_try_peek_previous(const cursor_type *cursor, key_type *value); \
    bool prefix##_cursor_try_peek_next(const cursor_type *cursor, key_type *value); \
    tds_hamt_status prefix##_cursor_move_previous(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_move_next(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_seek(const cursor_type *cursor, size_t position, cursor_type *result); \
    tds_hamt_status prefix##_cursor_insert(const cursor_type *cursor, key_type value, cursor_type *result); \
    tds_hamt_status prefix##_cursor_delete_previous(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_delete_next(const cursor_type *cursor, cursor_type *result); \
    tds_hamt_status prefix##_cursor_snapshot(const cursor_type *cursor, set_type *result)

TDS_DECLARE_PATRICIA_SET_CURSOR(tds_int_set, tds_int_set, tds_int_set_cursor, int32_t);
TDS_DECLARE_PATRICIA_SET_CURSOR(tds_long_set, tds_long_set, tds_long_set_cursor, int64_t);

#undef TDS_DECLARE_PATRICIA_MAP
#undef TDS_DECLARE_PATRICIA_MAP_CURSOR
#undef TDS_DECLARE_PATRICIA_SET
#undef TDS_DECLARE_PATRICIA_SET_CURSOR

#ifdef __cplusplus
}
#endif
#endif
