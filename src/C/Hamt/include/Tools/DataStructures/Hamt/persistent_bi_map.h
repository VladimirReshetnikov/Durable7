#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_BI_MAP_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_BI_MAP_H

#include <Tools/DataStructures/Hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tds_hamt_bi_map_conflict {
    TDS_HAMT_BI_MAP_NO_CONFLICT = 0,
    TDS_HAMT_BI_MAP_KEY_CONFLICT = 1,
    TDS_HAMT_BI_MAP_VALUE_CONFLICT = 2
} tds_hamt_bi_map_conflict;

struct tds_hamt_bi_map_policy_state;

typedef struct tds_hamt_bi_map {
    tds_hamt_map forward;
    tds_hamt_map inverse;
    struct tds_hamt_bi_map_policy_state *policy_state;
    bool policy_inverted;
} tds_hamt_bi_map;

typedef struct tds_hamt_bi_map_iterator {
    tds_hamt_map_iterator inner;
} tds_hamt_bi_map_iterator;

/* Output handles must be uninitialized unless they alias the input handle. Every initialized
 * handle must be destroyed. Key and value policies, including their contexts and ownership
 * callbacks, are independent and must remain valid until the last related bimap is destroyed. */
tds_hamt_status tds_hamt_bi_map_create(
    const tds_hamt_set_policy *key_policy,
    const tds_hamt_set_policy *value_policy,
    tds_hamt_bi_map *result);
tds_hamt_bi_map tds_hamt_bi_map_clone(const tds_hamt_bi_map *map);
void tds_hamt_bi_map_destroy(tds_hamt_bi_map *map);

size_t tds_hamt_bi_map_count(const tds_hamt_bi_map *map);
bool tds_hamt_bi_map_is_empty(const tds_hamt_bi_map *map);
bool tds_hamt_bi_map_contains_key(const tds_hamt_bi_map *map, const void *key);
bool tds_hamt_bi_map_contains_value(const tds_hamt_bi_map *map, const void *value);
bool tds_hamt_bi_map_try_get(const tds_hamt_bi_map *map, const void *key, const void **value);
bool tds_hamt_bi_map_try_get_key(const tds_hamt_bi_map *map, const void *value, const void **key);

tds_hamt_status tds_hamt_bi_map_add(
    const tds_hamt_bi_map *map,
    const void *key,
    const void *value,
    tds_hamt_bi_map *result);
tds_hamt_status tds_hamt_bi_map_try_add(
    const tds_hamt_bi_map *map,
    const void *key,
    const void *value,
    tds_hamt_bi_map *result,
    bool *added,
    tds_hamt_bi_map_conflict *conflict);
tds_hamt_status tds_hamt_bi_map_set(
    const tds_hamt_bi_map *map,
    const void *key,
    const void *value,
    tds_hamt_bi_map *result);

/* Opposite representatives are borrowed from the source snapshot. `removed` distinguishes a
 * stored NULL from a miss. As with tds_hamt_map_try_remove, an aliased result reports NULL rather
 * than a potentially dangling non-NULL pointer; retain an owned representative before aliasing. */
tds_hamt_status tds_hamt_bi_map_try_remove_key(
    const tds_hamt_bi_map *map,
    const void *key,
    tds_hamt_bi_map *result,
    bool *removed,
    const void **opposite_value);
tds_hamt_status tds_hamt_bi_map_remove_key(
    const tds_hamt_bi_map *map,
    const void *key,
    tds_hamt_bi_map *result);
tds_hamt_status tds_hamt_bi_map_try_remove_value(
    const tds_hamt_bi_map *map,
    const void *value,
    tds_hamt_bi_map *result,
    bool *removed,
    const void **opposite_key);
tds_hamt_status tds_hamt_bi_map_remove_value(
    const tds_hamt_bi_map *map,
    const void *value,
    tds_hamt_bi_map *result);
tds_hamt_status tds_hamt_bi_map_clear(
    const tds_hamt_bi_map *map,
    tds_hamt_bi_map *result);

/* O(1) in pair count: clones and swaps two reference-counted CHAMP handles. */
tds_hamt_status tds_hamt_bi_map_inverse(
    const tds_hamt_bi_map *map,
    tds_hamt_bi_map *result);

void tds_hamt_bi_map_iterator_init(
    const tds_hamt_bi_map *map,
    tds_hamt_bi_map_iterator *iterator);
bool tds_hamt_bi_map_iterator_next(
    tds_hamt_bi_map_iterator *iterator,
    const void **key,
    const void **value);
bool tds_hamt_bi_map_shares_roots(
    const tds_hamt_bi_map *left,
    const tds_hamt_bi_map *right);
bool tds_hamt_bi_map_debug_validate(const tds_hamt_bi_map *map);

#ifdef __cplusplus
}
#endif

#endif
