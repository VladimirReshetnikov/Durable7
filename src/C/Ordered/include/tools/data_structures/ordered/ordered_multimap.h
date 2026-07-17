#ifndef TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_MULTIMAP_H
#define TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_MULTIMAP_H

#include <tools/data_structures/ordered/ordered_map.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tds_ordered_multimap_context;

/* Persistent key-grouped multimap. Key groups and distinct values within each
 * group retain first-insertion order. Empty groups are never stored. */
typedef struct tds_ordered_multimap {
    tds_ordered_map groups;
    int64_t pair_count;
    struct tds_ordered_multimap_context* context;
} tds_ordered_multimap;

typedef void (*tds_ordered_multimap_visit_fn)(
    const void* key,
    const void* value,
    void* context);

tds_ordered_status tds_ordered_multimap_init(
    tds_ordered_multimap* map,
    const tds_ordered_policy* key_policy,
    const tds_ordered_policy* value_policy);
tds_ordered_status tds_ordered_multimap_clone(
    const tds_ordered_multimap* source,
    tds_ordered_multimap* destination);
void tds_ordered_multimap_move(
    tds_ordered_multimap* destination,
    tds_ordered_multimap* source);
void tds_ordered_multimap_destroy(tds_ordered_multimap* map);

size_t tds_ordered_multimap_key_count(const tds_ordered_multimap* map);
int64_t tds_ordered_multimap_pair_count(const tds_ordered_multimap* map);
bool tds_ordered_multimap_empty(const tds_ordered_multimap* map);
bool tds_ordered_multimap_contains_key(
    const tds_ordered_multimap* map,
    const void* key);
bool tds_ordered_multimap_contains(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value);
bool tds_ordered_multimap_try_get_key(
    const tds_ordered_multimap* map,
    const void* equal_key,
    const void** actual_key);
bool tds_ordered_multimap_try_get_values(
    const tds_ordered_multimap* map,
    const void* key,
    const tds_ordered_set** values);

tds_ordered_status tds_ordered_multimap_add(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value,
    tds_ordered_multimap* result);
tds_ordered_status tds_ordered_multimap_remove(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value,
    tds_ordered_multimap* result);
tds_ordered_status tds_ordered_multimap_remove_key(
    const tds_ordered_multimap* map,
    const void* key,
    tds_ordered_multimap* result);
tds_ordered_status tds_ordered_multimap_clear(
    const tds_ordered_multimap* map,
    tds_ordered_multimap* result);
tds_ordered_status tds_ordered_multimap_visit(
    const tds_ordered_multimap* map,
    tds_ordered_multimap_visit_fn visitor,
    void* context);

bool tds_ordered_multimap_debug_validate(const tds_ordered_multimap* map);
bool tds_ordered_multimap_debug_shares_groups(
    const tds_ordered_multimap* left,
    const tds_ordered_multimap* right);

#ifdef __cplusplus
}
#endif

#endif
