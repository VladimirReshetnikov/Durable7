/*
 * A persistent many-to-many relation, indexed from both sides.
 *
 * Forward and reverse are maintained as exact inverses down to the representative chosen for each
 * equivalence class, so inverting the relation reuses both existing indexes rather than rebuilding.
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_HAMT_PERSISTENT_RELATION_H
#define DURABLE7_HAMT_PERSISTENT_RELATION_H

#include <durable7/hamt/persistent_hash_multimap.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bidirectional many-to-many relation. forward and reverse are maintained as
 * exact inverses, including one global representative per equality class. */
typedef struct d7_hamt_relation {
    d7_hamt_multimap forward;
    d7_hamt_multimap reverse;
} d7_hamt_relation;

/* Initializes the relation in place. */
d7_hamt_status d7_hamt_relation_init(
    d7_hamt_relation* relation,
    const d7_hamt_set_policy* left_policy,
    const d7_hamt_set_policy* right_policy);
/* Initializes a second handle on the same relation version, taking a reference rather than
 * copying. */
d7_hamt_status d7_hamt_relation_clone(
    const d7_hamt_relation* source,
    d7_hamt_relation* destination);
/* Relocates an initialized relation into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_hamt_relation_move(
    d7_hamt_relation* destination,
    d7_hamt_relation* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_relation_destroy(d7_hamt_relation* relation);

/* How many distinct left values are present. */
size_t d7_hamt_relation_left_count(const d7_hamt_relation* relation);
/* How many distinct right values are present. */
size_t d7_hamt_relation_right_count(const d7_hamt_relation* relation);
/* How many pairs are present in total. */
int64_t d7_hamt_relation_pair_count(const d7_hamt_relation* relation);
/* Whether the relation holds no pairs. */
bool d7_hamt_relation_empty(const d7_hamt_relation* relation);
/* Whether the pair is present. */
bool d7_hamt_relation_contains(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right);
/* Reads the right values related to the given left value, reporting whether it was present. */
bool d7_hamt_relation_try_get_rights(
    const d7_hamt_relation* relation,
    const void* left,
    const d7_hamt_set** rights);
/* Reads the left values related to the given right value, reporting whether it was present. Both
 * directions are indexed, so neither is a scan. */
bool d7_hamt_relation_try_get_lefts(
    const d7_hamt_relation* relation,
    const void* right,
    const d7_hamt_set** lefts);

/* Produces a relation containing the given pair. */
d7_hamt_status d7_hamt_relation_add(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right,
    d7_hamt_relation* result);
/* Produces a relation without that pair. */
d7_hamt_status d7_hamt_relation_remove(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right,
    d7_hamt_relation* result);
/* Produces a relation without any pair holding that left value. */
d7_hamt_status d7_hamt_relation_remove_left(
    const d7_hamt_relation* relation,
    const void* left,
    d7_hamt_relation* result);
/* Produces a relation without any pair holding that right value. */
d7_hamt_status d7_hamt_relation_remove_right(
    const d7_hamt_relation* relation,
    const void* right,
    d7_hamt_relation* result);
/* Produces an empty relation retaining the same policies. */
d7_hamt_status d7_hamt_relation_clear(
    const d7_hamt_relation* relation,
    d7_hamt_relation* result);
/* Produces the relation with the two sides exchanged, reusing both existing indexes. */
d7_hamt_status d7_hamt_relation_inverse(
    const d7_hamt_relation* relation,
    d7_hamt_relation* result);

/* Checks the relation's structural invariants. For tests and diagnostics. */
bool d7_hamt_relation_debug_validate(const d7_hamt_relation* relation);
/* Whether both handles reference the same roots. */
bool d7_hamt_relation_debug_shares_roots(
    const d7_hamt_relation* left,
    const d7_hamt_relation* right);

#ifdef __cplusplus
}
#endif

#endif
