/*
 * A persistent map carrying a secondary index derived from its entries.
 *
 * The index is maintained as entries change, so looking up by the derived key is a lookup rather
 * than a scan of the whole map. Every operation returns a new version and leaves its inputs valid,
 * sharing unchanged structure, so an edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_HAMT_PERSISTENT_INDEXED_MAP_H
#define DURABLE7_HAMT_PERSISTENT_INDEXED_MAP_H

#include <durable7/hamt/persistent_hash_multimap.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef d7_hamt_status (*d7_hamt_index_selector_fn)(
    const void* key,
    const void* value,
    void* context,
    const void** index_key);

struct d7_hamt_indexed_map_context;

typedef struct d7_hamt_indexed_map {
    d7_hamt_map primary;
    d7_hamt_multimap index;
    struct d7_hamt_indexed_map_context* context;
} d7_hamt_indexed_map;

typedef void (*d7_hamt_indexed_map_visit_fn)(
    const void* key,
    const void* value,
    void* context);

/* Initializes the map in place. */
d7_hamt_status d7_hamt_indexed_map_init(
    d7_hamt_indexed_map* map,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy,
    const d7_hamt_set_policy* index_policy,
    d7_hamt_index_selector_fn selector,
    void* selector_context);
/* Initializes a second handle on the same map version, taking a reference rather than copying. */
d7_hamt_status d7_hamt_indexed_map_clone(
    const d7_hamt_indexed_map* source,
    d7_hamt_indexed_map* destination);
/* Relocates an initialized map into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_hamt_indexed_map_move(
    d7_hamt_indexed_map* destination,
    d7_hamt_indexed_map* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_indexed_map_destroy(d7_hamt_indexed_map* map);

/* Number of entries in the map. */
size_t d7_hamt_indexed_map_count(const d7_hamt_indexed_map* map);
/* Whether the map holds no entries. */
bool d7_hamt_indexed_map_empty(const d7_hamt_indexed_map* map);
/* How many distinct secondary index keys are present. */
size_t d7_hamt_indexed_map_index_key_count(const d7_hamt_indexed_map* map);
/* Whether the key is present. */
bool d7_hamt_indexed_map_contains_key(
    const d7_hamt_indexed_map* map,
    const void* key);
/* Reads the value stored for the key, reporting whether it was present. */
bool d7_hamt_indexed_map_try_get(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void** value);
/* Reads the stored key representative, reporting whether the key was present. */
bool d7_hamt_indexed_map_try_get_key(
    const d7_hamt_indexed_map* map,
    const void* equal_key,
    const void** actual_key);
/* Reads the secondary index key derived for a primary key, reporting whether the primary key was
 * present. */
bool d7_hamt_indexed_map_try_get_index_key(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void** index_key);
/* Whether the secondary index key is present. */
bool d7_hamt_indexed_map_contains_index_key(
    const d7_hamt_indexed_map* map,
    const void* index_key);
/* How many entries carry the given secondary index key. */
size_t d7_hamt_indexed_map_count_by_index(
    const d7_hamt_indexed_map* map,
    const void* index_key);
/* Reads the primary keys carrying the given secondary index key, reporting whether any do. */
bool d7_hamt_indexed_map_try_get_keys_by_index(
    const d7_hamt_indexed_map* map,
    const void* index_key,
    const d7_hamt_set** keys);

/* Produces a map containing the given entry. */
d7_hamt_status d7_hamt_indexed_map_add(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void* value,
    d7_hamt_indexed_map* result);
/* Adds the entry unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_indexed_map_try_add(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void* value,
    bool* added,
    d7_hamt_indexed_map* result);
/* Produces a map with the key bound to the value, adding or replacing as needed. */
d7_hamt_status d7_hamt_indexed_map_set(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void* value,
    d7_hamt_indexed_map* result);
/* Produces a map without that entry. */
d7_hamt_status d7_hamt_indexed_map_remove(
    const d7_hamt_indexed_map* map,
    const void* key,
    d7_hamt_indexed_map* result);
/* Produces an empty map retaining the same policies. */
d7_hamt_status d7_hamt_indexed_map_clear(
    const d7_hamt_indexed_map* map,
    d7_hamt_indexed_map* result);
/* Calls the visitor once per entry, in the map's own order. */
d7_hamt_status d7_hamt_indexed_map_visit(
    const d7_hamt_indexed_map* map,
    d7_hamt_indexed_map_visit_fn visitor,
    void* context);

/* Checks the map's structural invariants. For tests and diagnostics. */
bool d7_hamt_indexed_map_debug_validate(const d7_hamt_indexed_map* map);
/* Whether both handles reference the same roots. */
bool d7_hamt_indexed_map_debug_shares_roots(
    const d7_hamt_indexed_map* left,
    const d7_hamt_indexed_map* right);

#ifdef __cplusplus
}
#endif

#endif
