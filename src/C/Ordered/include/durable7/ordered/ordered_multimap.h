/*
 * A persistent multimap that remembers insertion order, both across keys and within a key.
 *
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection.
 */

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

/* Initializes the multimap in place. */
d7_ordered_status d7_ordered_multimap_init(
    d7_ordered_multimap* map,
    const d7_ordered_policy* key_policy,
    const d7_ordered_policy* value_policy);
/* Initializes a second handle on the same multimap version, taking a reference rather than
 * copying. */
d7_ordered_status d7_ordered_multimap_clone(
    const d7_ordered_multimap* source,
    d7_ordered_multimap* destination);
/* Relocates an initialized multimap into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_ordered_multimap_move(
    d7_ordered_multimap* destination,
    d7_ordered_multimap* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_ordered_multimap_destroy(d7_ordered_multimap* map);

/* How many distinct keys are present. */
size_t d7_ordered_multimap_key_count(const d7_ordered_multimap* map);
/* How many key-value pairs are present in total. */
int64_t d7_ordered_multimap_pair_count(const d7_ordered_multimap* map);
/* Whether the multimap holds no pairs. */
bool d7_ordered_multimap_empty(const d7_ordered_multimap* map);
/* Whether the key is present. */
bool d7_ordered_multimap_contains_key(
    const d7_ordered_multimap* map,
    const void* key);
/* Whether the pair is present. */
bool d7_ordered_multimap_contains(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value);
/* Reads the stored key representative, reporting whether the key was present. */
bool d7_ordered_multimap_try_get_key(
    const d7_ordered_multimap* map,
    const void* equal_key,
    const void** actual_key);
/* Reads the values bound to the key in insertion order, reporting whether the key was present. */
bool d7_ordered_multimap_try_get_values(
    const d7_ordered_multimap* map,
    const void* key,
    const d7_ordered_set** values);

/* Produces a multimap containing the given pair. */
d7_ordered_status d7_ordered_multimap_add(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    d7_ordered_multimap* result);
/* Produces a multimap without that pair. */
d7_ordered_status d7_ordered_multimap_remove(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    d7_ordered_multimap* result);
/* Produces a multimap without that key. */
d7_ordered_status d7_ordered_multimap_remove_key(
    const d7_ordered_multimap* map,
    const void* key,
    d7_ordered_multimap* result);
/* Produces an empty multimap retaining the same policies. */
d7_ordered_status d7_ordered_multimap_clear(
    const d7_ordered_multimap* map,
    d7_ordered_multimap* result);
/* Calls the visitor once per pair, in the multimap's own order. */
d7_ordered_status d7_ordered_multimap_visit(
    const d7_ordered_multimap* map,
    d7_ordered_multimap_visit_fn visitor,
    void* context);

/* Checks the multimap's structural invariants. For tests and diagnostics. */
bool d7_ordered_multimap_debug_validate(const d7_ordered_multimap* map);
/* Whether both handles share their group index. */
bool d7_ordered_multimap_debug_shares_groups(
    const d7_ordered_multimap* left,
    const d7_ordered_multimap* right);

#ifdef __cplusplus
}
#endif

#endif
