#ifndef TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_SET_H
#define TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_SET_H

#include <Tools/DataStructures/Hamt/hamt.h>
#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tds_ordered_status {
    TDS_ORDERED_OK = 0,
    TDS_ORDERED_INVALID_ARGUMENT = 1,
    TDS_ORDERED_OUT_OF_RANGE = 2,
    TDS_ORDERED_EMPTY = 3,
    TDS_ORDERED_NOT_FOUND = 4,
    TDS_ORDERED_OUT_OF_MEMORY = 5,
    TDS_ORDERED_OVERFLOW = 6,
    TDS_ORDERED_INVARIANT_VIOLATION = 7
} tds_ordered_status;

typedef uint32_t (*tds_ordered_hash_fn)(const void* item, void* context);
typedef bool (*tds_ordered_equal_fn)(const void* left, const void* right, void* context);
typedef void (*tds_ordered_visit_fn)(const void* item, void* context);

/* The callbacks and their context must remain usable until every set that was
 * initialized from this policy has been destroyed. item_type.copy constructs
 * an owned value in uninitialized storage; item_type.destroy releases it. */
typedef struct tds_ordered_policy {
    ft_value_type item_type;
    tds_ordered_hash_fn hash;
    tds_ordered_equal_fn equal;
    void* context;
} tds_ordered_policy;

struct tds_ordered_context;

/* Persistent value handle. Copy with tds_ordered_set_clone rather than by
 * assignment, move with tds_ordered_set_move, and destroy every initialized
 * handle. Changed operations require an uninitialized, non-aliasing result. */
typedef struct tds_ordered_set {
    struct tds_ordered_context* context;
    ft_persistent_deque order;
    tds_hamt_map stamps;
} tds_ordered_set;

void tds_ordered_policy_init(
    tds_ordered_policy* policy,
    const ft_value_type* item_type,
    tds_ordered_hash_fn hash,
    tds_ordered_equal_fn equal,
    void* context);

tds_ordered_status tds_ordered_set_init(
    tds_ordered_set* set,
    const tds_ordered_policy* policy);
tds_ordered_status tds_ordered_set_from_array(
    tds_ordered_set* set,
    const tds_ordered_policy* policy,
    const void* items,
    size_t item_count);
tds_ordered_status tds_ordered_set_from_items(
    tds_ordered_set* set,
    const tds_ordered_policy* policy,
    const void* const* items,
    size_t item_count);
tds_ordered_status tds_ordered_set_clone(
    const tds_ordered_set* source,
    tds_ordered_set* destination);
void tds_ordered_set_move(tds_ordered_set* destination, tds_ordered_set* source);
void tds_ordered_set_destroy(tds_ordered_set* set);

const tds_ordered_policy* tds_ordered_set_policy(const tds_ordered_set* set);
bool tds_ordered_set_empty(const tds_ordered_set* set);
size_t tds_ordered_set_size(const tds_ordered_set* set);

/* Returned item pointers are borrowed from set and remain valid until that
 * handle is destroyed. On a try_get miss, actual_item echoes equal_item. */
bool tds_ordered_set_contains(const tds_ordered_set* set, const void* item);
bool tds_ordered_set_try_get_value(
    const tds_ordered_set* set,
    const void* equal_item,
    const void** actual_item);
tds_ordered_status tds_ordered_set_front(const tds_ordered_set* set, const void** item);
tds_ordered_status tds_ordered_set_back(const tds_ordered_set* set, const void** item);
tds_ordered_status tds_ordered_set_at(
    const tds_ordered_set* set,
    size_t index,
    const void** item);
bool tds_ordered_set_index_of(
    const tds_ordered_set* set,
    const void* equal_item,
    size_t* index);

tds_ordered_status tds_ordered_set_add(
    const tds_ordered_set* set,
    const void* item,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_add_first(
    const tds_ordered_set* set,
    const void* item,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_insert(
    const tds_ordered_set* set,
    size_t index,
    const void* item,
    tds_ordered_set* result);

tds_ordered_status tds_ordered_set_move_to_first(
    const tds_ordered_set* set,
    const void* equal_item,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_move_to_last(
    const tds_ordered_set* set,
    const void* equal_item,
    tds_ordered_set* result);
/* index is the class's final index after movement. */
tds_ordered_status tds_ordered_set_move_to(
    const tds_ordered_set* set,
    size_t index,
    const void* equal_item,
    tds_ordered_set* result);

tds_ordered_status tds_ordered_set_remove(
    const tds_ordered_set* set,
    const void* equal_item,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_try_remove(
    const tds_ordered_set* set,
    const void* equal_item,
    bool* removed,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_remove_at(
    const tds_ordered_set* set,
    size_t index,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_remove_first(
    const tds_ordered_set* set,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_remove_last(
    const tds_ordered_set* set,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_clear(
    const tds_ordered_set* set,
    tds_ordered_set* result);

tds_ordered_status tds_ordered_set_get_range(
    const tds_ordered_set* set,
    size_t index,
    size_t count,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_take(
    const tds_ordered_set* set,
    size_t count,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_drop(
    const tds_ordered_set* set,
    size_t count,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_reverse(
    const tds_ordered_set* set,
    tds_ordered_set* result);
/* Stable one-shot reordering. It does not install a maintained sort policy. */
tds_ordered_status tds_ordered_set_sort(
    const tds_ordered_set* set,
    ft_compare_fn compare,
    void* compare_context,
    tds_ordered_set* result);

/* Array operands are eagerly normalized under the receiver's policy. The
 * first representative of each receiver-equivalent argument class wins. */
tds_ordered_status tds_ordered_set_union_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_intersect_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_except_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_symmetric_except_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result);

/* Same-sized set operands are also fully re-normalized under the receiver's
 * policy; the right set's policy is never used to decide receiver membership. */
tds_ordered_status tds_ordered_set_union(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_intersect(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_except(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result);
tds_ordered_status tds_ordered_set_symmetric_except(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result);

tds_ordered_status tds_ordered_set_is_subset_of_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
tds_ordered_status tds_ordered_set_is_proper_subset_of_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
tds_ordered_status tds_ordered_set_is_superset_of_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
tds_ordered_status tds_ordered_set_is_proper_superset_of_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
tds_ordered_status tds_ordered_set_overlaps_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
tds_ordered_status tds_ordered_set_equals_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);

tds_ordered_status tds_ordered_set_is_subset_of(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    bool* answer);
tds_ordered_status tds_ordered_set_is_proper_subset_of(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    bool* answer);
tds_ordered_status tds_ordered_set_is_superset_of(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    bool* answer);
tds_ordered_status tds_ordered_set_is_proper_superset_of(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    bool* answer);
tds_ordered_status tds_ordered_set_overlaps(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    bool* answer);
tds_ordered_status tds_ordered_set_equals(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    bool* answer);

tds_ordered_status tds_ordered_set_visit(
    const tds_ordered_set* set,
    tds_ordered_visit_fn visitor,
    void* context);

bool tds_ordered_set_debug_validate(const tds_ordered_set* set);
bool tds_ordered_set_debug_shares_order(
    const tds_ordered_set* left,
    const tds_ordered_set* right);
bool tds_ordered_set_debug_shares_index(
    const tds_ordered_set* left,
    const tds_ordered_set* right);

#ifdef __cplusplus
}
#endif

#endif
