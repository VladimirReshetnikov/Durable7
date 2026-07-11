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

#undef TDS_DECLARE_PATRICIA_MAP
#undef TDS_DECLARE_PATRICIA_SET

#ifdef __cplusplus
}
#endif
#endif
