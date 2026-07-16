#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_RELATION_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_RELATION_H

#include <Tools/DataStructures/Hamt/persistent_hash_multimap.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bidirectional many-to-many relation. forward and reverse are maintained as
 * exact inverses, including one global representative per equality class. */
typedef struct tds_hamt_relation {
    tds_hamt_multimap forward;
    tds_hamt_multimap reverse;
} tds_hamt_relation;

tds_hamt_status tds_hamt_relation_init(
    tds_hamt_relation* relation,
    const tds_hamt_set_policy* left_policy,
    const tds_hamt_set_policy* right_policy);
tds_hamt_status tds_hamt_relation_clone(
    const tds_hamt_relation* source,
    tds_hamt_relation* destination);
void tds_hamt_relation_move(
    tds_hamt_relation* destination,
    tds_hamt_relation* source);
void tds_hamt_relation_destroy(tds_hamt_relation* relation);

size_t tds_hamt_relation_left_count(const tds_hamt_relation* relation);
size_t tds_hamt_relation_right_count(const tds_hamt_relation* relation);
int64_t tds_hamt_relation_pair_count(const tds_hamt_relation* relation);
bool tds_hamt_relation_empty(const tds_hamt_relation* relation);
bool tds_hamt_relation_contains(
    const tds_hamt_relation* relation,
    const void* left,
    const void* right);
bool tds_hamt_relation_try_get_rights(
    const tds_hamt_relation* relation,
    const void* left,
    const tds_hamt_set** rights);
bool tds_hamt_relation_try_get_lefts(
    const tds_hamt_relation* relation,
    const void* right,
    const tds_hamt_set** lefts);

tds_hamt_status tds_hamt_relation_add(
    const tds_hamt_relation* relation,
    const void* left,
    const void* right,
    tds_hamt_relation* result);
tds_hamt_status tds_hamt_relation_remove(
    const tds_hamt_relation* relation,
    const void* left,
    const void* right,
    tds_hamt_relation* result);
tds_hamt_status tds_hamt_relation_remove_left(
    const tds_hamt_relation* relation,
    const void* left,
    tds_hamt_relation* result);
tds_hamt_status tds_hamt_relation_remove_right(
    const tds_hamt_relation* relation,
    const void* right,
    tds_hamt_relation* result);
tds_hamt_status tds_hamt_relation_clear(
    const tds_hamt_relation* relation,
    tds_hamt_relation* result);
tds_hamt_status tds_hamt_relation_inverse(
    const tds_hamt_relation* relation,
    tds_hamt_relation* result);

bool tds_hamt_relation_debug_validate(const tds_hamt_relation* relation);
bool tds_hamt_relation_debug_shares_roots(
    const tds_hamt_relation* left,
    const tds_hamt_relation* right);

#ifdef __cplusplus
}
#endif

#endif
