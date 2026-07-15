#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_HASH_BAG_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_HASH_BAG_H

#include <Tools/DataStructures/Hamt/hamt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable unordered multiset over tds_hamt_map. One retained item
 * representative and one positive int32_t multiplicity are stored per item
 * equivalence class. The expanded total is a checked nonnegative int64_t. */
typedef struct tds_hamt_bag {
    tds_hamt_map counts;
    int64_t total_count;
} tds_hamt_bag;

typedef struct tds_hamt_bag_entry {
    const void *item;
    int32_t count;
} tds_hamt_bag_entry;

/* Iterators borrow their source bag. Keep the source alive and unchanged until
 * iteration ends. Value copies of an iterator advance independently. */
typedef struct tds_hamt_bag_iterator {
    tds_hamt_map_iterator inner;
    const void *current_item;
    int32_t remaining;
} tds_hamt_bag_iterator;

typedef struct tds_hamt_bag_distinct_iterator {
    tds_hamt_map_iterator inner;
} tds_hamt_bag_distinct_iterator;

typedef struct tds_hamt_bag_entry_iterator {
    tds_hamt_map_iterator inner;
} tds_hamt_bag_entry_iterator;

/* Empty creation cannot fail. Range construction eagerly consumes items in
 * order and retains the first representative of each policy class. On range
 * failure, result is unchanged. */
tds_hamt_bag tds_hamt_bag_create(const tds_hamt_set_policy *policy);
tds_hamt_status tds_hamt_bag_create_range(
    const tds_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    tds_hamt_bag *result);
tds_hamt_bag tds_hamt_bag_clone(const tds_hamt_bag *bag);
void tds_hamt_bag_destroy(tds_hamt_bag *bag);

size_t tds_hamt_bag_distinct_count(const tds_hamt_bag *bag);
int64_t tds_hamt_bag_total_count(const tds_hamt_bag *bag);
bool tds_hamt_bag_is_empty(const tds_hamt_bag *bag);
tds_hamt_status tds_hamt_bag_get_policy(
    const tds_hamt_bag *bag,
    tds_hamt_set_policy *policy);

bool tds_hamt_bag_contains(const tds_hamt_bag *bag, const void *item);
int32_t tds_hamt_bag_count_of(const tds_hamt_bag *bag, const void *item);
/* On a miss, actual_item echoes equal_item, matching map and set lookup. */
bool tds_hamt_bag_try_get_value(
    const tds_hamt_bag *bag,
    const void *equal_item,
    const void **actual_item);
bool tds_hamt_bag_try_get_entry(
    const tds_hamt_bag *bag,
    const void *equal_item,
    tds_hamt_bag_entry *entry);

/* copies must be in [0, INT32_MAX]. Validation and total-overflow checks occur
 * before hashing or equality callbacks. Zero is a root-sharing no-op. Every
 * positive addition selects its new count through tds_hamt_map_add_or_update,
 * so it performs one hash-trie update descent. */
tds_hamt_status tds_hamt_bag_add(
    const tds_hamt_bag *bag,
    const void *item,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_add_copies(
    const tds_hamt_bag *bag,
    const void *item,
    int64_t copies,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_remove(
    const tds_hamt_bag *bag,
    const void *item,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_remove_copies(
    const tds_hamt_bag *bag,
    const void *item,
    int64_t copies,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_remove_all(
    const tds_hamt_bag *bag,
    const void *item,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_clear(
    const tds_hamt_bag *bag,
    tds_hamt_bag *result);

/* Algebra eagerly normalizes a policy-incompatible argument under the
 * receiver policy before applying shortcuts. Receiver representatives win
 * surviving classes. Union takes maxima, intersection minima, except uses
 * saturated subtraction, and sum uses checked addition. */
tds_hamt_status tds_hamt_bag_union(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_intersect(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_except(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result);
tds_hamt_status tds_hamt_bag_sum(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result);

void tds_hamt_bag_iterator_init(
    const tds_hamt_bag *bag,
    tds_hamt_bag_iterator *iterator);
bool tds_hamt_bag_iterator_next(
    tds_hamt_bag_iterator *iterator,
    const void **item);
void tds_hamt_bag_distinct_iterator_init(
    const tds_hamt_bag *bag,
    tds_hamt_bag_distinct_iterator *iterator);
bool tds_hamt_bag_distinct_iterator_next(
    tds_hamt_bag_distinct_iterator *iterator,
    const void **item);
void tds_hamt_bag_entry_iterator_init(
    const tds_hamt_bag *bag,
    tds_hamt_bag_entry_iterator *iterator);
bool tds_hamt_bag_entry_iterator_next(
    tds_hamt_bag_entry_iterator *iterator,
    tds_hamt_bag_entry *entry);

bool tds_hamt_bag_shares_root(
    const tds_hamt_bag *left,
    const tds_hamt_bag *right);
const void *tds_hamt_bag_debug_root_identity(const tds_hamt_bag *bag);
bool tds_hamt_bag_debug_validate_canonical(const tds_hamt_bag *bag);

/* For operations with a tds_hamt_bag *result, success initializes that handle.
 * It may alias an input; otherwise it must not already own a live bag. Every
 * error leaves result and all inputs unchanged. Destroy every successfully
 * initialized handle exactly once. */

#ifdef __cplusplus
}
#endif

#endif
