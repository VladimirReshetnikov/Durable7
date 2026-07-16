#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_HASH_MULTIMAP_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_HASH_MULTIMAP_H

#include <Tools/DataStructures/Hamt/hamt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tds_hamt_multimap_context;

/* Immutable set-valued multimap. The outer CHAMP owns only nonempty set
 * groups; key and value domains retain independent hash/equality/ownership
 * policies. Copy with clone rather than assignment and destroy every handle. */
typedef struct tds_hamt_multimap {
    tds_hamt_map groups;
    int64_t pair_count;
    struct tds_hamt_multimap_context* context;
} tds_hamt_multimap;

typedef void (*tds_hamt_multimap_visit_fn)(
    const void* key,
    const void* value,
    void* context);

tds_hamt_status tds_hamt_multimap_init(
    tds_hamt_multimap* map,
    const tds_hamt_set_policy* key_policy,
    const tds_hamt_set_policy* value_policy);
tds_hamt_status tds_hamt_multimap_clone(
    const tds_hamt_multimap* source,
    tds_hamt_multimap* destination);
void tds_hamt_multimap_move(
    tds_hamt_multimap* destination,
    tds_hamt_multimap* source);
void tds_hamt_multimap_destroy(tds_hamt_multimap* map);

size_t tds_hamt_multimap_key_count(const tds_hamt_multimap* map);
int64_t tds_hamt_multimap_pair_count(const tds_hamt_multimap* map);
bool tds_hamt_multimap_empty(const tds_hamt_multimap* map);
bool tds_hamt_multimap_contains_key(
    const tds_hamt_multimap* map,
    const void* key);
bool tds_hamt_multimap_contains(
    const tds_hamt_multimap* map,
    const void* key,
    const void* value);
bool tds_hamt_multimap_try_get_key(
    const tds_hamt_multimap* map,
    const void* equal_key,
    const void** actual_key);
bool tds_hamt_multimap_try_get_values(
    const tds_hamt_multimap* map,
    const void* key,
    const tds_hamt_set** values);

tds_hamt_status tds_hamt_multimap_add(
    const tds_hamt_multimap* map,
    const void* key,
    const void* value,
    tds_hamt_multimap* result);
tds_hamt_status tds_hamt_multimap_remove(
    const tds_hamt_multimap* map,
    const void* key,
    const void* value,
    tds_hamt_multimap* result);
tds_hamt_status tds_hamt_multimap_remove_key(
    const tds_hamt_multimap* map,
    const void* key,
    tds_hamt_multimap* result);
tds_hamt_status tds_hamt_multimap_clear(
    const tds_hamt_multimap* map,
    tds_hamt_multimap* result);

tds_hamt_status tds_hamt_multimap_visit(
    const tds_hamt_multimap* map,
    tds_hamt_multimap_visit_fn visitor,
    void* context);
bool tds_hamt_multimap_debug_validate(const tds_hamt_multimap* map);
bool tds_hamt_multimap_debug_shares_root(
    const tds_hamt_multimap* left,
    const tds_hamt_multimap* right);

#ifdef __cplusplus
}
#endif

#endif
