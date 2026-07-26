/*
 * A persistent, invertible record of changes between two map versions.
 *
 * Each change records both the state it expects and the state it produces. That is what lets a
 * patch be inverted, composed with another, and applied against a target that may have drifted,
 * rather than only replayed forward onto its original source.
 */

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

/* Initializes the patch in place. */
d7_hamt_status d7_hamt_map_patch_init(
    d7_hamt_map_patch* patch,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy);
/* Records the changes carrying the source map to the target, so the patch can later be applied or
 * inverted. */
d7_hamt_status d7_hamt_map_patch_between(
    const d7_hamt_map* source,
    const d7_hamt_map* target,
    d7_hamt_map_patch* result);
/* Initializes a second handle on the same patch version, taking a reference rather than copying. */
d7_hamt_status d7_hamt_map_patch_clone(
    const d7_hamt_map_patch* source,
    d7_hamt_map_patch* destination);
/* Relocates an initialized patch into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_hamt_map_patch_move(
    d7_hamt_map_patch* destination,
    d7_hamt_map_patch* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_map_patch_destroy(d7_hamt_map_patch* patch);

/* Number of changes in the patch. */
size_t d7_hamt_map_patch_count(const d7_hamt_map_patch* patch);
/* Whether the patch holds no changes. */
bool d7_hamt_map_patch_empty(const d7_hamt_map_patch* patch);
/* Whether the key is present. */
bool d7_hamt_map_patch_contains_key(
    const d7_hamt_map_patch* patch,
    const void* key);
/* Reads the stored key representative and value, reporting whether the key was present. */
bool d7_hamt_map_patch_try_get_entry(
    const d7_hamt_map_patch* patch,
    const void* key,
    d7_hamt_map_patch_entry* entry);

/* Produces a patch containing the given change. */
d7_hamt_status d7_hamt_map_patch_add(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map_patch_entry* entry,
    d7_hamt_map_patch* result);
/* Adds the change unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_map_patch_try_add(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map_patch_entry* entry,
    bool* added,
    d7_hamt_map_patch* result);
/* Produces a patch without that change. */
d7_hamt_status d7_hamt_map_patch_remove(
    const d7_hamt_map_patch* patch,
    const void* key,
    d7_hamt_map_patch* result);
/* Produces an empty patch retaining the same policies. */
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
/* Applies the tag to a measure or element, as the tag algebra defines. */
d7_hamt_status d7_hamt_map_patch_apply(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map* source,
    d7_hamt_map* result);
/* Produces the patch undoing this one. Each change records both its prior and its resulting state,
 * which is what makes inversion possible. */
d7_hamt_status d7_hamt_map_patch_invert(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch* result);
/* Produces the patch equivalent to applying the first and then the second. */
d7_hamt_status d7_hamt_map_patch_compose(
    const d7_hamt_map_patch* first,
    const d7_hamt_map_patch* next,
    d7_hamt_map_patch* result);
/* Calls the visitor once per change, in the patch's own order. */
d7_hamt_status d7_hamt_map_patch_visit(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch_visit_fn visitor,
    void* context);

/* Checks the patch's structural invariants. For tests and diagnostics. */
bool d7_hamt_map_patch_debug_validate(const d7_hamt_map_patch* patch);
/* Whether both handles reference the same root node. */
bool d7_hamt_map_patch_debug_shares_root(
    const d7_hamt_map_patch* left,
    const d7_hamt_map_patch* right);

#ifdef __cplusplus
}
#endif

#endif
