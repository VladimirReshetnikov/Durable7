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

d7_hamt_status d7_hamt_relation_init(
    d7_hamt_relation* relation,
    const d7_hamt_set_policy* left_policy,
    const d7_hamt_set_policy* right_policy);
d7_hamt_status d7_hamt_relation_clone(
    const d7_hamt_relation* source,
    d7_hamt_relation* destination);
void d7_hamt_relation_move(
    d7_hamt_relation* destination,
    d7_hamt_relation* source);
void d7_hamt_relation_destroy(d7_hamt_relation* relation);

size_t d7_hamt_relation_left_count(const d7_hamt_relation* relation);
size_t d7_hamt_relation_right_count(const d7_hamt_relation* relation);
int64_t d7_hamt_relation_pair_count(const d7_hamt_relation* relation);
bool d7_hamt_relation_empty(const d7_hamt_relation* relation);
bool d7_hamt_relation_contains(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right);
bool d7_hamt_relation_try_get_rights(
    const d7_hamt_relation* relation,
    const void* left,
    const d7_hamt_set** rights);
bool d7_hamt_relation_try_get_lefts(
    const d7_hamt_relation* relation,
    const void* right,
    const d7_hamt_set** lefts);

d7_hamt_status d7_hamt_relation_add(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right,
    d7_hamt_relation* result);
d7_hamt_status d7_hamt_relation_remove(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right,
    d7_hamt_relation* result);
d7_hamt_status d7_hamt_relation_remove_left(
    const d7_hamt_relation* relation,
    const void* left,
    d7_hamt_relation* result);
d7_hamt_status d7_hamt_relation_remove_right(
    const d7_hamt_relation* relation,
    const void* right,
    d7_hamt_relation* result);
d7_hamt_status d7_hamt_relation_clear(
    const d7_hamt_relation* relation,
    d7_hamt_relation* result);
d7_hamt_status d7_hamt_relation_inverse(
    const d7_hamt_relation* relation,
    d7_hamt_relation* result);

bool d7_hamt_relation_debug_validate(const d7_hamt_relation* relation);
bool d7_hamt_relation_debug_shares_roots(
    const d7_hamt_relation* left,
    const d7_hamt_relation* right);

#ifdef __cplusplus
}
#endif

#endif
