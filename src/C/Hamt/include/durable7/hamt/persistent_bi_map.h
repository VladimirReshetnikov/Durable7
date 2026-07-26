/*
 * A persistent bidirectional map: a one-to-one correspondence indexed from both sides.
 *
 * Both directions are maintained as exact inverses, so a lookup by value is a lookup rather than a
 * scan, and an insertion that would break the correspondence is rejected instead of silently
 * displacing a pair. Every operation returns a new version and leaves its inputs valid, sharing
 * unchanged structure, so an edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_HAMT_PERSISTENT_BI_MAP_H
#define DURABLE7_HAMT_PERSISTENT_BI_MAP_H

#include <durable7/hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum d7_hamt_bi_map_conflict {
    D7_HAMT_BI_MAP_NO_CONFLICT = 0,
    D7_HAMT_BI_MAP_KEY_CONFLICT = 1,
    D7_HAMT_BI_MAP_VALUE_CONFLICT = 2
} d7_hamt_bi_map_conflict;

struct d7_hamt_bi_map_policy_state;

typedef struct d7_hamt_bi_map {
    d7_hamt_map forward;
    d7_hamt_map inverse;
    struct d7_hamt_bi_map_policy_state *policy_state;
    bool policy_inverted;
} d7_hamt_bi_map;

typedef struct d7_hamt_bi_map_iterator {
    d7_hamt_map_iterator inner;
} d7_hamt_bi_map_iterator;

/* Output handles must be uninitialized unless they alias the input handle. Every initialized
 * handle must be destroyed. Key and value policies, including their contexts and ownership
 * callbacks, are independent and must remain valid until the last related bimap is destroyed. */
d7_hamt_status d7_hamt_bi_map_create(
    const d7_hamt_set_policy *key_policy,
    const d7_hamt_set_policy *value_policy,
    d7_hamt_bi_map *result);
/* Initializes a second handle on the same bimap version, taking a reference rather than copying. */
d7_hamt_bi_map d7_hamt_bi_map_clone(const d7_hamt_bi_map *map);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_bi_map_destroy(d7_hamt_bi_map *map);

/* Number of pairs in the bimap. */
size_t d7_hamt_bi_map_count(const d7_hamt_bi_map *map);
/* Whether the bimap holds no pairs. */
bool d7_hamt_bi_map_is_empty(const d7_hamt_bi_map *map);
/* Whether the key is present. */
bool d7_hamt_bi_map_contains_key(const d7_hamt_bi_map *map, const void *key);
/* Whether any entry holds the value. */
bool d7_hamt_bi_map_contains_value(const d7_hamt_bi_map *map, const void *value);
/* Reads the value stored for the key, reporting whether it was present. */
bool d7_hamt_bi_map_try_get(const d7_hamt_bi_map *map, const void *key, const void **value);
/* Reads the stored key representative, reporting whether the key was present. */
bool d7_hamt_bi_map_try_get_key(const d7_hamt_bi_map *map, const void *value, const void **key);

/* Produces a bimap containing the given pair. */
d7_hamt_status d7_hamt_bi_map_add(
    const d7_hamt_bi_map *map,
    const void *key,
    const void *value,
    d7_hamt_bi_map *result);
/* Adds the pair unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_bi_map_try_add(
    const d7_hamt_bi_map *map,
    const void *key,
    const void *value,
    d7_hamt_bi_map *result,
    bool *added,
    d7_hamt_bi_map_conflict *conflict);
/* Produces a bimap with the key bound to the value, adding or replacing as needed. */
d7_hamt_status d7_hamt_bi_map_set(
    const d7_hamt_bi_map *map,
    const void *key,
    const void *value,
    d7_hamt_bi_map *result);

/* Opposite representatives are borrowed from the source snapshot. `removed` distinguishes a
 * stored NULL from a miss. As with d7_hamt_map_try_remove, an aliased result reports NULL rather
 * than a potentially dangling non-NULL pointer; retain an owned representative before aliasing. */
d7_hamt_status d7_hamt_bi_map_try_remove_key(
    const d7_hamt_bi_map *map,
    const void *key,
    d7_hamt_bi_map *result,
    bool *removed,
    const void **opposite_value);
/* Produces a bimap without that key. */
d7_hamt_status d7_hamt_bi_map_remove_key(
    const d7_hamt_bi_map *map,
    const void *key,
    d7_hamt_bi_map *result);
/* Removes the pair holding that value, reporting whether it was present. */
d7_hamt_status d7_hamt_bi_map_try_remove_value(
    const d7_hamt_bi_map *map,
    const void *value,
    d7_hamt_bi_map *result,
    bool *removed,
    const void **opposite_key);
/* Produces a bimap without the pair holding that value. */
d7_hamt_status d7_hamt_bi_map_remove_value(
    const d7_hamt_bi_map *map,
    const void *value,
    d7_hamt_bi_map *result);
/* Produces an empty bimap retaining the same policies. */
d7_hamt_status d7_hamt_bi_map_clear(
    const d7_hamt_bi_map *map,
    d7_hamt_bi_map *result);

/* O(1) in pair count: clones and swaps two reference-counted CHAMP handles. */
d7_hamt_status d7_hamt_bi_map_inverse(
    const d7_hamt_bi_map *map,
    d7_hamt_bi_map *result);

/* Initializes an iterator over the pairs. */
void d7_hamt_bi_map_iterator_init(
    const d7_hamt_bi_map *map,
    d7_hamt_bi_map_iterator *iterator);
/* Advances to the next pair, reporting whether there was one. */
bool d7_hamt_bi_map_iterator_next(
    d7_hamt_bi_map_iterator *iterator,
    const void **key,
    const void **value);
/* Whether both handles reference the same root node. */
bool d7_hamt_bi_map_shares_roots(
    const d7_hamt_bi_map *left,
    const d7_hamt_bi_map *right);
/* Checks the bimap's structural invariants. For tests and diagnostics. */
bool d7_hamt_bi_map_debug_validate(const d7_hamt_bi_map *map);

#ifdef __cplusplus
}
#endif

#endif
