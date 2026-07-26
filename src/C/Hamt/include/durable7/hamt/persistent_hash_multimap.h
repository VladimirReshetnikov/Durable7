/*
 * A persistent multimap: each key bound to a set of values.
 *
 * Value sets are themselves persistent, so keys whose values did not change are shared outright
 * between versions. Every operation returns a new version and leaves its inputs valid, sharing
 * unchanged structure, so an edit copies a path rather than the whole collection.
 */

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

/* Initializes the multimap in place. */
d7_hamt_status d7_hamt_multimap_init(
    d7_hamt_multimap* map,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy);
/* Initializes a second handle on the same multimap version, taking a reference rather than
 * copying. */
d7_hamt_status d7_hamt_multimap_clone(
    const d7_hamt_multimap* source,
    d7_hamt_multimap* destination);
/* Relocates an initialized multimap into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_hamt_multimap_move(
    d7_hamt_multimap* destination,
    d7_hamt_multimap* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_multimap_destroy(d7_hamt_multimap* map);

/* How many distinct keys are present. */
size_t d7_hamt_multimap_key_count(const d7_hamt_multimap* map);
/* How many key-value pairs are present in total. */
int64_t d7_hamt_multimap_pair_count(const d7_hamt_multimap* map);
/* Whether the multimap holds no pairs. */
bool d7_hamt_multimap_empty(const d7_hamt_multimap* map);
/* Whether the key is present. */
bool d7_hamt_multimap_contains_key(
    const d7_hamt_multimap* map,
    const void* key);
/* Whether the pair is present. */
bool d7_hamt_multimap_contains(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value);
/* Reads the stored key representative, reporting whether the key was present. */
bool d7_hamt_multimap_try_get_key(
    const d7_hamt_multimap* map,
    const void* equal_key,
    const void** actual_key);
/* Reads the set of values bound to the key, reporting whether the key was present. */
bool d7_hamt_multimap_try_get_values(
    const d7_hamt_multimap* map,
    const void* key,
    const d7_hamt_set** values);

/* Produces a multimap containing the given pair. */
d7_hamt_status d7_hamt_multimap_add(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value,
    d7_hamt_multimap* result);
/* Produces a multimap without that pair. */
d7_hamt_status d7_hamt_multimap_remove(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value,
    d7_hamt_multimap* result);
/* Produces a multimap without that key. */
d7_hamt_status d7_hamt_multimap_remove_key(
    const d7_hamt_multimap* map,
    const void* key,
    d7_hamt_multimap* result);
/* Produces an empty multimap retaining the same policies. */
d7_hamt_status d7_hamt_multimap_clear(
    const d7_hamt_multimap* map,
    d7_hamt_multimap* result);

/* Calls the visitor once per pair, in the multimap's own order. */
d7_hamt_status d7_hamt_multimap_visit(
    const d7_hamt_multimap* map,
    d7_hamt_multimap_visit_fn visitor,
    void* context);
/* Checks the multimap's structural invariants. For tests and diagnostics. */
bool d7_hamt_multimap_debug_validate(const d7_hamt_multimap* map);
/* Whether both handles reference the same root node. */
bool d7_hamt_multimap_debug_shares_root(
    const d7_hamt_multimap* left,
    const d7_hamt_multimap* right);

#ifdef __cplusplus
}
#endif

#endif
