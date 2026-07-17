#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_MAP_PATCH_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_MAP_PATCH_H

#include <Tools/DataStructures/Hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tds_hamt_map_patch_value {
    bool present;
    const void* value;
} tds_hamt_map_patch_value;

typedef struct tds_hamt_map_patch_entry {
    const void* key;
    tds_hamt_map_patch_value before;
    tds_hamt_map_patch_value after;
} tds_hamt_map_patch_entry;

struct tds_hamt_map_patch_context;

typedef struct tds_hamt_map_patch {
    tds_hamt_map changes;
    struct tds_hamt_map_patch_context* context;
} tds_hamt_map_patch;

typedef void (*tds_hamt_map_patch_visit_fn)(
    const tds_hamt_map_patch_entry* entry,
    void* context);

tds_hamt_status tds_hamt_map_patch_init(
    tds_hamt_map_patch* patch,
    const tds_hamt_set_policy* key_policy,
    const tds_hamt_set_policy* value_policy);
tds_hamt_status tds_hamt_map_patch_between(
    const tds_hamt_map* source,
    const tds_hamt_map* target,
    tds_hamt_map_patch* result);
tds_hamt_status tds_hamt_map_patch_clone(
    const tds_hamt_map_patch* source,
    tds_hamt_map_patch* destination);
void tds_hamt_map_patch_move(
    tds_hamt_map_patch* destination,
    tds_hamt_map_patch* source);
void tds_hamt_map_patch_destroy(tds_hamt_map_patch* patch);

size_t tds_hamt_map_patch_count(const tds_hamt_map_patch* patch);
bool tds_hamt_map_patch_empty(const tds_hamt_map_patch* patch);
bool tds_hamt_map_patch_contains_key(
    const tds_hamt_map_patch* patch,
    const void* key);
bool tds_hamt_map_patch_try_get_entry(
    const tds_hamt_map_patch* patch,
    const void* key,
    tds_hamt_map_patch_entry* entry);

tds_hamt_status tds_hamt_map_patch_add(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map_patch_entry* entry,
    tds_hamt_map_patch* result);
tds_hamt_status tds_hamt_map_patch_try_add(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map_patch_entry* entry,
    bool* added,
    tds_hamt_map_patch* result);
tds_hamt_status tds_hamt_map_patch_remove(
    const tds_hamt_map_patch* patch,
    const void* key,
    tds_hamt_map_patch* result);
tds_hamt_status tds_hamt_map_patch_clear(
    const tds_hamt_map_patch* patch,
    tds_hamt_map_patch* result);

/* Every expectation is checked before any edit. On conflict, applied is false,
 * conflicting_key borrows the patch key, and result is a clone of source. */
tds_hamt_status tds_hamt_map_patch_try_apply(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map* source,
    bool* applied,
    const void** conflicting_key,
    tds_hamt_map* result);
tds_hamt_status tds_hamt_map_patch_apply(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map* source,
    tds_hamt_map* result);
tds_hamt_status tds_hamt_map_patch_invert(
    const tds_hamt_map_patch* patch,
    tds_hamt_map_patch* result);
tds_hamt_status tds_hamt_map_patch_compose(
    const tds_hamt_map_patch* first,
    const tds_hamt_map_patch* next,
    tds_hamt_map_patch* result);
tds_hamt_status tds_hamt_map_patch_visit(
    const tds_hamt_map_patch* patch,
    tds_hamt_map_patch_visit_fn visitor,
    void* context);

bool tds_hamt_map_patch_debug_validate(const tds_hamt_map_patch* patch);
bool tds_hamt_map_patch_debug_shares_root(
    const tds_hamt_map_patch* left,
    const tds_hamt_map_patch* right);

#ifdef __cplusplus
}
#endif

#endif
