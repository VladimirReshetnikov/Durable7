#ifndef DURABLE7_ORDERED_C_ORDERED_MULTIMAP_H
#define DURABLE7_ORDERED_C_ORDERED_MULTIMAP_H

#include <durable7/ordered/ordered_map.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct d7_ordered_multimap_context;

/* Persistent key-grouped multimap. Key groups and distinct values within each
 * group retain first-insertion order. Empty groups are never stored. */
typedef struct d7_ordered_multimap {
    d7_ordered_map groups;
    int64_t pair_count;
    struct d7_ordered_multimap_context* context;
} d7_ordered_multimap;

typedef void (*d7_ordered_multimap_visit_fn)(
    const void* key,
    const void* value,
    void* context);

d7_ordered_status d7_ordered_multimap_init(
    d7_ordered_multimap* map,
    const d7_ordered_policy* key_policy,
    const d7_ordered_policy* value_policy);
d7_ordered_status d7_ordered_multimap_clone(
    const d7_ordered_multimap* source,
    d7_ordered_multimap* destination);
void d7_ordered_multimap_move(
    d7_ordered_multimap* destination,
    d7_ordered_multimap* source);
void d7_ordered_multimap_destroy(d7_ordered_multimap* map);

size_t d7_ordered_multimap_key_count(const d7_ordered_multimap* map);
int64_t d7_ordered_multimap_pair_count(const d7_ordered_multimap* map);
bool d7_ordered_multimap_empty(const d7_ordered_multimap* map);
bool d7_ordered_multimap_contains_key(
    const d7_ordered_multimap* map,
    const void* key);
bool d7_ordered_multimap_contains(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value);
bool d7_ordered_multimap_try_get_key(
    const d7_ordered_multimap* map,
    const void* equal_key,
    const void** actual_key);
bool d7_ordered_multimap_try_get_values(
    const d7_ordered_multimap* map,
    const void* key,
    const d7_ordered_set** values);

d7_ordered_status d7_ordered_multimap_add(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    d7_ordered_multimap* result);
d7_ordered_status d7_ordered_multimap_remove(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    d7_ordered_multimap* result);
d7_ordered_status d7_ordered_multimap_remove_key(
    const d7_ordered_multimap* map,
    const void* key,
    d7_ordered_multimap* result);
d7_ordered_status d7_ordered_multimap_clear(
    const d7_ordered_multimap* map,
    d7_ordered_multimap* result);
d7_ordered_status d7_ordered_multimap_visit(
    const d7_ordered_multimap* map,
    d7_ordered_multimap_visit_fn visitor,
    void* context);

bool d7_ordered_multimap_debug_validate(const d7_ordered_multimap* map);
bool d7_ordered_multimap_debug_shares_groups(
    const d7_ordered_multimap* left,
    const d7_ordered_multimap* right);

#ifdef __cplusplus
}
#endif

#endif
