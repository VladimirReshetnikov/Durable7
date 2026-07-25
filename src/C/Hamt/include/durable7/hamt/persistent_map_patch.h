#ifndef DURABLE7_HAMT_PERSISTENT_MAP_PATCH_H
#define DURABLE7_HAMT_PERSISTENT_MAP_PATCH_H

#include <durable7/hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct d7_hamt_map_patch_value {
    bool present;
    const void* value;
} d7_hamt_map_patch_value;

typedef struct d7_hamt_map_patch_entry {
    const void* key;
    d7_hamt_map_patch_value before;
    d7_hamt_map_patch_value after;
} d7_hamt_map_patch_entry;

struct d7_hamt_map_patch_context;

typedef struct d7_hamt_map_patch {
    d7_hamt_map changes;
    struct d7_hamt_map_patch_context* context;
} d7_hamt_map_patch;

typedef void (*d7_hamt_map_patch_visit_fn)(
    const d7_hamt_map_patch_entry* entry,
    void* context);

d7_hamt_status d7_hamt_map_patch_init(
    d7_hamt_map_patch* patch,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy);
d7_hamt_status d7_hamt_map_patch_between(
    const d7_hamt_map* source,
    const d7_hamt_map* target,
    d7_hamt_map_patch* result);
d7_hamt_status d7_hamt_map_patch_clone(
    const d7_hamt_map_patch* source,
    d7_hamt_map_patch* destination);
void d7_hamt_map_patch_move(
    d7_hamt_map_patch* destination,
    d7_hamt_map_patch* source);
void d7_hamt_map_patch_destroy(d7_hamt_map_patch* patch);

size_t d7_hamt_map_patch_count(const d7_hamt_map_patch* patch);
bool d7_hamt_map_patch_empty(const d7_hamt_map_patch* patch);
bool d7_hamt_map_patch_contains_key(
    const d7_hamt_map_patch* patch,
    const void* key);
bool d7_hamt_map_patch_try_get_entry(
    const d7_hamt_map_patch* patch,
    const void* key,
    d7_hamt_map_patch_entry* entry);

d7_hamt_status d7_hamt_map_patch_add(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map_patch_entry* entry,
    d7_hamt_map_patch* result);
d7_hamt_status d7_hamt_map_patch_try_add(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map_patch_entry* entry,
    bool* added,
    d7_hamt_map_patch* result);
d7_hamt_status d7_hamt_map_patch_remove(
    const d7_hamt_map_patch* patch,
    const void* key,
    d7_hamt_map_patch* result);
d7_hamt_status d7_hamt_map_patch_clear(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch* result);

/* Every expectation is checked before any edit. On conflict, applied is false,
 * conflicting_key borrows the patch key, and result is a clone of source. */
d7_hamt_status d7_hamt_map_patch_try_apply(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map* source,
    bool* applied,
    const void** conflicting_key,
    d7_hamt_map* result);
d7_hamt_status d7_hamt_map_patch_apply(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map* source,
    d7_hamt_map* result);
d7_hamt_status d7_hamt_map_patch_invert(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch* result);
d7_hamt_status d7_hamt_map_patch_compose(
    const d7_hamt_map_patch* first,
    const d7_hamt_map_patch* next,
    d7_hamt_map_patch* result);
d7_hamt_status d7_hamt_map_patch_visit(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch_visit_fn visitor,
    void* context);

bool d7_hamt_map_patch_debug_validate(const d7_hamt_map_patch* patch);
bool d7_hamt_map_patch_debug_shares_root(
    const d7_hamt_map_patch* left,
    const d7_hamt_map_patch* right);

#ifdef __cplusplus
}
#endif

#endif
