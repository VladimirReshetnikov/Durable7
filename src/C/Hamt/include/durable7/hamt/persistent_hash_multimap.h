#ifndef DURABLE7_HAMT_PERSISTENT_HASH_MULTIMAP_H
#define DURABLE7_HAMT_PERSISTENT_HASH_MULTIMAP_H

#include <durable7/hamt/hamt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct d7_hamt_multimap_context;

/* Immutable set-valued multimap. The outer CHAMP owns only nonempty set
 * groups; key and value domains retain independent hash/equality/ownership
 * policies. Copy with clone rather than assignment and destroy every handle. */
typedef struct d7_hamt_multimap {
    d7_hamt_map groups;
    int64_t pair_count;
    struct d7_hamt_multimap_context* context;
} d7_hamt_multimap;

typedef void (*d7_hamt_multimap_visit_fn)(
    const void* key,
    const void* value,
    void* context);

d7_hamt_status d7_hamt_multimap_init(
    d7_hamt_multimap* map,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy);
d7_hamt_status d7_hamt_multimap_clone(
    const d7_hamt_multimap* source,
    d7_hamt_multimap* destination);
void d7_hamt_multimap_move(
    d7_hamt_multimap* destination,
    d7_hamt_multimap* source);
void d7_hamt_multimap_destroy(d7_hamt_multimap* map);

size_t d7_hamt_multimap_key_count(const d7_hamt_multimap* map);
int64_t d7_hamt_multimap_pair_count(const d7_hamt_multimap* map);
bool d7_hamt_multimap_empty(const d7_hamt_multimap* map);
bool d7_hamt_multimap_contains_key(
    const d7_hamt_multimap* map,
    const void* key);
bool d7_hamt_multimap_contains(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value);
bool d7_hamt_multimap_try_get_key(
    const d7_hamt_multimap* map,
    const void* equal_key,
    const void** actual_key);
bool d7_hamt_multimap_try_get_values(
    const d7_hamt_multimap* map,
    const void* key,
    const d7_hamt_set** values);

d7_hamt_status d7_hamt_multimap_add(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value,
    d7_hamt_multimap* result);
d7_hamt_status d7_hamt_multimap_remove(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value,
    d7_hamt_multimap* result);
d7_hamt_status d7_hamt_multimap_remove_key(
    const d7_hamt_multimap* map,
    const void* key,
    d7_hamt_multimap* result);
d7_hamt_status d7_hamt_multimap_clear(
    const d7_hamt_multimap* map,
    d7_hamt_multimap* result);

d7_hamt_status d7_hamt_multimap_visit(
    const d7_hamt_multimap* map,
    d7_hamt_multimap_visit_fn visitor,
    void* context);
bool d7_hamt_multimap_debug_validate(const d7_hamt_multimap* map);
bool d7_hamt_multimap_debug_shares_root(
    const d7_hamt_multimap* left,
    const d7_hamt_multimap* right);

#ifdef __cplusplus
}
#endif

#endif
