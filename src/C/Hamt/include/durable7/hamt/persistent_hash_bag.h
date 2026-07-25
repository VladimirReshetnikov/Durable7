#ifndef DURABLE7_HAMT_PERSISTENT_HASH_BAG_H
#define DURABLE7_HAMT_PERSISTENT_HASH_BAG_H

#include <durable7/hamt/hamt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable unordered multiset over d7_hamt_map. One retained item
 * representative and one positive int32_t multiplicity are stored per item
 * equivalence class. The expanded total is a checked nonnegative int64_t. */
typedef struct d7_hamt_bag {
    d7_hamt_map counts;
    int64_t total_count;
} d7_hamt_bag;

typedef struct d7_hamt_bag_entry {
    const void *item;
    int32_t count;
} d7_hamt_bag_entry;

/* Iterators borrow their source bag. Keep the source alive and unchanged until
 * iteration ends. Value copies of an iterator advance independently. */
typedef struct d7_hamt_bag_iterator {
    d7_hamt_map_iterator inner;
    const void *current_item;
    int32_t remaining;
} d7_hamt_bag_iterator;

typedef struct d7_hamt_bag_distinct_iterator {
    d7_hamt_map_iterator inner;
} d7_hamt_bag_distinct_iterator;

typedef struct d7_hamt_bag_entry_iterator {
    d7_hamt_map_iterator inner;
} d7_hamt_bag_entry_iterator;

/* Empty creation cannot fail. Range construction eagerly consumes items in
 * order and retains the first representative of each policy class. On range
 * failure, result is unchanged. */
d7_hamt_bag d7_hamt_bag_create(const d7_hamt_set_policy *policy);
d7_hamt_status d7_hamt_bag_create_range(
    const d7_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    d7_hamt_bag *result);
d7_hamt_bag d7_hamt_bag_clone(const d7_hamt_bag *bag);
void d7_hamt_bag_destroy(d7_hamt_bag *bag);

size_t d7_hamt_bag_distinct_count(const d7_hamt_bag *bag);
int64_t d7_hamt_bag_total_count(const d7_hamt_bag *bag);
bool d7_hamt_bag_is_empty(const d7_hamt_bag *bag);
d7_hamt_status d7_hamt_bag_get_policy(
    const d7_hamt_bag *bag,
    d7_hamt_set_policy *policy);

bool d7_hamt_bag_contains(const d7_hamt_bag *bag, const void *item);
int32_t d7_hamt_bag_count_of(const d7_hamt_bag *bag, const void *item);
/* On a miss, actual_item echoes equal_item, matching map and set lookup. */
bool d7_hamt_bag_try_get_value(
    const d7_hamt_bag *bag,
    const void *equal_item,
    const void **actual_item);
bool d7_hamt_bag_try_get_entry(
    const d7_hamt_bag *bag,
    const void *equal_item,
    d7_hamt_bag_entry *entry);

/* copies must be in [0, INT32_MAX]. Validation and total-overflow checks occur
 * before hashing or equality callbacks. Zero is a root-sharing no-op. Every
 * positive addition selects its new count through d7_hamt_map_add_or_update,
 * so it performs one hash-trie update descent. */
d7_hamt_status d7_hamt_bag_add(
    const d7_hamt_bag *bag,
    const void *item,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_add_copies(
    const d7_hamt_bag *bag,
    const void *item,
    int64_t copies,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_remove(
    const d7_hamt_bag *bag,
    const void *item,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_remove_copies(
    const d7_hamt_bag *bag,
    const void *item,
    int64_t copies,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_remove_all(
    const d7_hamt_bag *bag,
    const void *item,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_clear(
    const d7_hamt_bag *bag,
    d7_hamt_bag *result);

/* Algebra eagerly normalizes a policy-incompatible argument under the
 * receiver policy before applying shortcuts. Receiver representatives win
 * surviving classes. Union takes maxima, intersection minima, except uses
 * saturated subtraction, and sum uses checked addition. */
d7_hamt_status d7_hamt_bag_union(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_intersect(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_except(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);
d7_hamt_status d7_hamt_bag_sum(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);

void d7_hamt_bag_iterator_init(
    const d7_hamt_bag *bag,
    d7_hamt_bag_iterator *iterator);
bool d7_hamt_bag_iterator_next(
    d7_hamt_bag_iterator *iterator,
    const void **item);
void d7_hamt_bag_distinct_iterator_init(
    const d7_hamt_bag *bag,
    d7_hamt_bag_distinct_iterator *iterator);
bool d7_hamt_bag_distinct_iterator_next(
    d7_hamt_bag_distinct_iterator *iterator,
    const void **item);
void d7_hamt_bag_entry_iterator_init(
    const d7_hamt_bag *bag,
    d7_hamt_bag_entry_iterator *iterator);
bool d7_hamt_bag_entry_iterator_next(
    d7_hamt_bag_entry_iterator *iterator,
    d7_hamt_bag_entry *entry);

bool d7_hamt_bag_shares_root(
    const d7_hamt_bag *left,
    const d7_hamt_bag *right);
const void *d7_hamt_bag_debug_root_identity(const d7_hamt_bag *bag);
bool d7_hamt_bag_debug_validate_canonical(const d7_hamt_bag *bag);

/* For operations with a d7_hamt_bag *result, success initializes that handle.
 * It may alias an input; otherwise it must not already own a live bag. Every
 * error leaves result and all inputs unchanged. Destroy every successfully
 * initialized handle exactly once. */

#ifdef __cplusplus
}
#endif

#endif
