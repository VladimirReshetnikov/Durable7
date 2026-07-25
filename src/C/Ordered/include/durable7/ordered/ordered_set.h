#ifndef DURABLE7_ORDERED_C_ORDERED_SET_H
#define DURABLE7_ORDERED_C_ORDERED_SET_H

#include <durable7/hamt/hamt.h>
#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum d7_ordered_status {
    D7_ORDERED_OK = 0,
    D7_ORDERED_INVALID_ARGUMENT = 1,
    D7_ORDERED_OUT_OF_RANGE = 2,
    D7_ORDERED_EMPTY = 3,
    D7_ORDERED_NOT_FOUND = 4,
    D7_ORDERED_OUT_OF_MEMORY = 5,
    D7_ORDERED_OVERFLOW = 6,
    D7_ORDERED_INVARIANT_VIOLATION = 7
} d7_ordered_status;

typedef uint32_t (*d7_ordered_hash_fn)(const void* item, void* context);
typedef bool (*d7_ordered_equal_fn)(const void* left, const void* right, void* context);
typedef void (*d7_ordered_visit_fn)(const void* item, void* context);

/* The callbacks and their context must remain usable until every set that was
 * initialized from this policy has been destroyed. item_type.copy constructs
 * an owned value in uninitialized storage; item_type.destroy releases it. */
typedef struct d7_ordered_policy {
    ft_value_type item_type;
    d7_ordered_hash_fn hash;
    d7_ordered_equal_fn equal;
    void* context;
} d7_ordered_policy;

struct d7_ordered_context;

/* Persistent value handle. Copy with d7_ordered_set_clone rather than by
 * assignment, move with d7_ordered_set_move, and destroy every initialized
 * handle. Changed operations require an uninitialized, non-aliasing result. */
typedef struct d7_ordered_set {
    struct d7_ordered_context* context;
    ft_persistent_deque order;
    d7_hamt_map stamps;
} d7_ordered_set;

void d7_ordered_policy_init(
    d7_ordered_policy* policy,
    const ft_value_type* item_type,
    d7_ordered_hash_fn hash,
    d7_ordered_equal_fn equal,
    void* context);

d7_ordered_status d7_ordered_set_init(
    d7_ordered_set* set,
    const d7_ordered_policy* policy);
d7_ordered_status d7_ordered_set_from_array(
    d7_ordered_set* set,
    const d7_ordered_policy* policy,
    const void* items,
    size_t item_count);
d7_ordered_status d7_ordered_set_from_items(
    d7_ordered_set* set,
    const d7_ordered_policy* policy,
    const void* const* items,
    size_t item_count);
d7_ordered_status d7_ordered_set_clone(
    const d7_ordered_set* source,
    d7_ordered_set* destination);
void d7_ordered_set_move(d7_ordered_set* destination, d7_ordered_set* source);
void d7_ordered_set_destroy(d7_ordered_set* set);

const d7_ordered_policy* d7_ordered_set_policy(const d7_ordered_set* set);
bool d7_ordered_set_empty(const d7_ordered_set* set);
size_t d7_ordered_set_size(const d7_ordered_set* set);

/* Returned item pointers are borrowed from set and remain valid until that
 * handle is destroyed. On a try_get miss, actual_item echoes equal_item. */
bool d7_ordered_set_contains(const d7_ordered_set* set, const void* item);
bool d7_ordered_set_try_get_value(
    const d7_ordered_set* set,
    const void* equal_item,
    const void** actual_item);
d7_ordered_status d7_ordered_set_front(const d7_ordered_set* set, const void** item);
d7_ordered_status d7_ordered_set_back(const d7_ordered_set* set, const void** item);
d7_ordered_status d7_ordered_set_at(
    const d7_ordered_set* set,
    size_t index,
    const void** item);
bool d7_ordered_set_index_of(
    const d7_ordered_set* set,
    const void* equal_item,
    size_t* index);

d7_ordered_status d7_ordered_set_add(
    const d7_ordered_set* set,
    const void* item,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_add_first(
    const d7_ordered_set* set,
    const void* item,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_insert(
    const d7_ordered_set* set,
    size_t index,
    const void* item,
    d7_ordered_set* result);

d7_ordered_status d7_ordered_set_move_to_first(
    const d7_ordered_set* set,
    const void* equal_item,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_move_to_last(
    const d7_ordered_set* set,
    const void* equal_item,
    d7_ordered_set* result);
/* index is the class's final index after movement. */
d7_ordered_status d7_ordered_set_move_to(
    const d7_ordered_set* set,
    size_t index,
    const void* equal_item,
    d7_ordered_set* result);

d7_ordered_status d7_ordered_set_remove(
    const d7_ordered_set* set,
    const void* equal_item,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_try_remove(
    const d7_ordered_set* set,
    const void* equal_item,
    bool* removed,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_remove_at(
    const d7_ordered_set* set,
    size_t index,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_remove_first(
    const d7_ordered_set* set,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_remove_last(
    const d7_ordered_set* set,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_clear(
    const d7_ordered_set* set,
    d7_ordered_set* result);

d7_ordered_status d7_ordered_set_get_range(
    const d7_ordered_set* set,
    size_t index,
    size_t count,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_take(
    const d7_ordered_set* set,
    size_t count,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_drop(
    const d7_ordered_set* set,
    size_t count,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_reverse(
    const d7_ordered_set* set,
    d7_ordered_set* result);
/* Stable one-shot reordering. It does not install a maintained sort policy. */
d7_ordered_status d7_ordered_set_sort(
    const d7_ordered_set* set,
    ft_compare_fn compare,
    void* compare_context,
    d7_ordered_set* result);

/* Array operands are eagerly normalized under the receiver's policy. The
 * first representative of each receiver-equivalent argument class wins. */
d7_ordered_status d7_ordered_set_union_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_intersect_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_except_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_symmetric_except_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result);

/* Same-sized set operands are also fully re-normalized under the receiver's
 * policy; the right set's policy is never used to decide receiver membership. */
d7_ordered_status d7_ordered_set_union(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_intersect(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_except(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result);
d7_ordered_status d7_ordered_set_symmetric_except(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result);

d7_ordered_status d7_ordered_set_is_subset_of_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
d7_ordered_status d7_ordered_set_is_proper_subset_of_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
d7_ordered_status d7_ordered_set_is_superset_of_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
d7_ordered_status d7_ordered_set_is_proper_superset_of_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
d7_ordered_status d7_ordered_set_overlaps_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);
d7_ordered_status d7_ordered_set_equals_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    bool* answer);

d7_ordered_status d7_ordered_set_is_subset_of(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    bool* answer);
d7_ordered_status d7_ordered_set_is_proper_subset_of(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    bool* answer);
d7_ordered_status d7_ordered_set_is_superset_of(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    bool* answer);
d7_ordered_status d7_ordered_set_is_proper_superset_of(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    bool* answer);
d7_ordered_status d7_ordered_set_overlaps(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    bool* answer);
d7_ordered_status d7_ordered_set_equals(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    bool* answer);

d7_ordered_status d7_ordered_set_visit(
    const d7_ordered_set* set,
    d7_ordered_visit_fn visitor,
    void* context);

bool d7_ordered_set_debug_validate(const d7_ordered_set* set);
bool d7_ordered_set_debug_shares_order(
    const d7_ordered_set* left,
    const d7_ordered_set* right);
bool d7_ordered_set_debug_shares_index(
    const d7_ordered_set* left,
    const d7_ordered_set* right);

#ifdef __cplusplus
}
#endif

#endif
