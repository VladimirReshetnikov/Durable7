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

d7_hamt_status d7_hamt_indexed_map_init(
    d7_hamt_indexed_map* map,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy,
    const d7_hamt_set_policy* index_policy,
    d7_hamt_index_selector_fn selector,
    void* selector_context);
d7_hamt_status d7_hamt_indexed_map_clone(
    const d7_hamt_indexed_map* source,
    d7_hamt_indexed_map* destination);
void d7_hamt_indexed_map_move(
    d7_hamt_indexed_map* destination,
    d7_hamt_indexed_map* source);
void d7_hamt_indexed_map_destroy(d7_hamt_indexed_map* map);

size_t d7_hamt_indexed_map_count(const d7_hamt_indexed_map* map);
bool d7_hamt_indexed_map_empty(const d7_hamt_indexed_map* map);
size_t d7_hamt_indexed_map_index_key_count(const d7_hamt_indexed_map* map);
bool d7_hamt_indexed_map_contains_key(
    const d7_hamt_indexed_map* map,
    const void* key);
bool d7_hamt_indexed_map_try_get(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void** value);
bool d7_hamt_indexed_map_try_get_key(
    const d7_hamt_indexed_map* map,
    const void* equal_key,
    const void** actual_key);
bool d7_hamt_indexed_map_try_get_index_key(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void** index_key);
bool d7_hamt_indexed_map_contains_index_key(
    const d7_hamt_indexed_map* map,
    const void* index_key);
size_t d7_hamt_indexed_map_count_by_index(
    const d7_hamt_indexed_map* map,
    const void* index_key);
bool d7_hamt_indexed_map_try_get_keys_by_index(
    const d7_hamt_indexed_map* map,
    const void* index_key,
    const d7_hamt_set** keys);

d7_hamt_status d7_hamt_indexed_map_add(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void* value,
    d7_hamt_indexed_map* result);
d7_hamt_status d7_hamt_indexed_map_try_add(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void* value,
    bool* added,
    d7_hamt_indexed_map* result);
d7_hamt_status d7_hamt_indexed_map_set(
    const d7_hamt_indexed_map* map,
    const void* key,
    const void* value,
    d7_hamt_indexed_map* result);
d7_hamt_status d7_hamt_indexed_map_remove(
    const d7_hamt_indexed_map* map,
    const void* key,
    d7_hamt_indexed_map* result);
d7_hamt_status d7_hamt_indexed_map_clear(
    const d7_hamt_indexed_map* map,
    d7_hamt_indexed_map* result);
d7_hamt_status d7_hamt_indexed_map_visit(
    const d7_hamt_indexed_map* map,
    d7_hamt_indexed_map_visit_fn visitor,
    void* context);

bool d7_hamt_indexed_map_debug_validate(const d7_hamt_indexed_map* map);
bool d7_hamt_indexed_map_debug_shares_roots(
    const d7_hamt_indexed_map* left,
    const d7_hamt_indexed_map* right);

#ifdef __cplusplus
}
#endif

#endif
