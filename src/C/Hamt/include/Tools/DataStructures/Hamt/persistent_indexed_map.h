#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_INDEXED_MAP_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_INDEXED_MAP_H

#include <Tools/DataStructures/Hamt/persistent_hash_multimap.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef tds_hamt_status (*tds_hamt_index_selector_fn)(
    const void* key,
    const void* value,
    void* context,
    const void** index_key);

struct tds_hamt_indexed_map_context;

typedef struct tds_hamt_indexed_map {
    tds_hamt_map primary;
    tds_hamt_multimap index;
    struct tds_hamt_indexed_map_context* context;
} tds_hamt_indexed_map;

typedef void (*tds_hamt_indexed_map_visit_fn)(
    const void* key,
    const void* value,
    void* context);

tds_hamt_status tds_hamt_indexed_map_init(
    tds_hamt_indexed_map* map,
    const tds_hamt_set_policy* key_policy,
    const tds_hamt_set_policy* value_policy,
    const tds_hamt_set_policy* index_policy,
    tds_hamt_index_selector_fn selector,
    void* selector_context);
tds_hamt_status tds_hamt_indexed_map_clone(
    const tds_hamt_indexed_map* source,
    tds_hamt_indexed_map* destination);
void tds_hamt_indexed_map_move(
    tds_hamt_indexed_map* destination,
    tds_hamt_indexed_map* source);
void tds_hamt_indexed_map_destroy(tds_hamt_indexed_map* map);

size_t tds_hamt_indexed_map_count(const tds_hamt_indexed_map* map);
bool tds_hamt_indexed_map_empty(const tds_hamt_indexed_map* map);
size_t tds_hamt_indexed_map_index_key_count(const tds_hamt_indexed_map* map);
bool tds_hamt_indexed_map_contains_key(
    const tds_hamt_indexed_map* map,
    const void* key);
bool tds_hamt_indexed_map_try_get(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void** value);
bool tds_hamt_indexed_map_try_get_key(
    const tds_hamt_indexed_map* map,
    const void* equal_key,
    const void** actual_key);
bool tds_hamt_indexed_map_try_get_index_key(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void** index_key);
bool tds_hamt_indexed_map_contains_index_key(
    const tds_hamt_indexed_map* map,
    const void* index_key);
size_t tds_hamt_indexed_map_count_by_index(
    const tds_hamt_indexed_map* map,
    const void* index_key);
bool tds_hamt_indexed_map_try_get_keys_by_index(
    const tds_hamt_indexed_map* map,
    const void* index_key,
    const tds_hamt_set** keys);

tds_hamt_status tds_hamt_indexed_map_add(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void* value,
    tds_hamt_indexed_map* result);
tds_hamt_status tds_hamt_indexed_map_try_add(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void* value,
    bool* added,
    tds_hamt_indexed_map* result);
tds_hamt_status tds_hamt_indexed_map_set(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void* value,
    tds_hamt_indexed_map* result);
tds_hamt_status tds_hamt_indexed_map_remove(
    const tds_hamt_indexed_map* map,
    const void* key,
    tds_hamt_indexed_map* result);
tds_hamt_status tds_hamt_indexed_map_clear(
    const tds_hamt_indexed_map* map,
    tds_hamt_indexed_map* result);
tds_hamt_status tds_hamt_indexed_map_visit(
    const tds_hamt_indexed_map* map,
    tds_hamt_indexed_map_visit_fn visitor,
    void* context);

bool tds_hamt_indexed_map_debug_validate(const tds_hamt_indexed_map* map);
bool tds_hamt_indexed_map_debug_shares_roots(
    const tds_hamt_indexed_map* left,
    const tds_hamt_indexed_map* right);

#ifdef __cplusplus
}
#endif

#endif
