/*
 * A persistent multiset: hashed elements with multiplicities.
 *
 * Multiplicity is stored per distinct element, so a large count costs no more than a small one.
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection.
 */

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
/* Initializes a bag holding the occurrences in the range. */
d7_hamt_status d7_hamt_bag_create_range(
    const d7_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    d7_hamt_bag *result);
/* Initializes a second handle on the same bag version, taking a reference rather than copying. */
d7_hamt_bag d7_hamt_bag_clone(const d7_hamt_bag *bag);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_bag_destroy(d7_hamt_bag *bag);

/* How many distinct elements are present, ignoring multiplicity. */
size_t d7_hamt_bag_distinct_count(const d7_hamt_bag *bag);
/* The total multiplicity across all elements. */
int64_t d7_hamt_bag_total_count(const d7_hamt_bag *bag);
/* Whether the bag holds no occurrences. */
bool d7_hamt_bag_is_empty(const d7_hamt_bag *bag);
/* The policy the bag retains. */
d7_hamt_status d7_hamt_bag_get_policy(
    const d7_hamt_bag *bag,
    d7_hamt_set_policy *policy);

/* Whether the occurrence is present. */
bool d7_hamt_bag_contains(const d7_hamt_bag *bag, const void *item);
/* How many times the occurrence occurs. */
int32_t d7_hamt_bag_count_of(const d7_hamt_bag *bag, const void *item);
/* On a miss, actual_item echoes equal_item, matching map and set lookup. */
bool d7_hamt_bag_try_get_value(
    const d7_hamt_bag *bag,
    const void *equal_item,
    const void **actual_item);
/* Reads the stored key representative and value, reporting whether the key was present. */
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
/* Produces a bag with the element's multiplicity raised by the given count. */
d7_hamt_status d7_hamt_bag_add_copies(
    const d7_hamt_bag *bag,
    const void *item,
    int64_t copies,
    d7_hamt_bag *result);
/* Produces a bag without that occurrence. */
d7_hamt_status d7_hamt_bag_remove(
    const d7_hamt_bag *bag,
    const void *item,
    d7_hamt_bag *result);
/* Produces a bag with the element's multiplicity lowered by the given count, stopping at zero. */
d7_hamt_status d7_hamt_bag_remove_copies(
    const d7_hamt_bag *bag,
    const void *item,
    int64_t copies,
    d7_hamt_bag *result);
/* Produces a bag without any occurrence of the element. */
d7_hamt_status d7_hamt_bag_remove_all(
    const d7_hamt_bag *bag,
    const void *item,
    d7_hamt_bag *result);
/* Produces an empty bag retaining the same policies. */
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
/* Produces the occurrences present in both bags. */
d7_hamt_status d7_hamt_bag_intersect(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);
/* Produces this bag's occurrences that are absent from the other. */
d7_hamt_status d7_hamt_bag_except(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);
/* Produces the bag whose multiplicity for each element is the sum of the two operands'. */
d7_hamt_status d7_hamt_bag_sum(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);

/* Initializes an iterator over the occurrences. */
void d7_hamt_bag_iterator_init(
    const d7_hamt_bag *bag,
    d7_hamt_bag_iterator *iterator);
/* Advances to the next occurrence, reporting whether there was one. */
bool d7_hamt_bag_iterator_next(
    d7_hamt_bag_iterator *iterator,
    const void **item);
/* Initializes an iterator over the distinct elements, ignoring multiplicity. */
void d7_hamt_bag_distinct_iterator_init(
    const d7_hamt_bag *bag,
    d7_hamt_bag_distinct_iterator *iterator);
/* Advances to the next distinct element, reporting whether there was one. */
bool d7_hamt_bag_distinct_iterator_next(
    d7_hamt_bag_distinct_iterator *iterator,
    const void **item);
/* Initializes an iterator over the entries. */
void d7_hamt_bag_entry_iterator_init(
    const d7_hamt_bag *bag,
    d7_hamt_bag_entry_iterator *iterator);
/* Advances to the next entry, reporting whether there was one. */
bool d7_hamt_bag_entry_iterator_next(
    d7_hamt_bag_entry_iterator *iterator,
    d7_hamt_bag_entry *entry);

/* Whether both handles reference the same root node. A representation check used to confirm a no-op
 * avoided copying, not an equality test. */
bool d7_hamt_bag_shares_root(
    const d7_hamt_bag *left,
    const d7_hamt_bag *right);
/* The root node's address, for tests that a no-op shared rather than copied. */
const void *d7_hamt_bag_debug_root_identity(const d7_hamt_bag *bag);
/* Checks that the structure is in its canonical shape, which must depend only on contents and not
 * on edit history. */
bool d7_hamt_bag_debug_validate_canonical(const d7_hamt_bag *bag);

/* For operations with a d7_hamt_bag *result, success initializes that handle.
 * It may alias an input; otherwise it must not already own a live bag. Every
 * error leaves result and all inputs unchanged. Destroy every successfully
 * initialized handle exactly once. */

#ifdef __cplusplus
}
#endif

#endif
