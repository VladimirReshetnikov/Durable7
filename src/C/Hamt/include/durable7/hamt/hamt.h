/*
 * The Hamt workspace's public C API: persistent hash maps and sets, and the shared status type.
 *
 * Backed by a CHAMP trie: branching is 32-way and bitmap-indexed, with immutable collision buckets,
 * so operations are constant time in expectation while versions share every unchanged node. A
 * collection retains its hashing and equivalence policy, so equivalence classes are defined by the
 * policy rather than by the platform. Every operation returns a new version and leaves its inputs
 * valid, sharing unchanged structure, so an edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_HAMT_HAMT_H
#define DURABLE7_HAMT_HAMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum d7_hamt_status {
    D7_HAMT_OK = 0,
    D7_HAMT_OUT_OF_MEMORY = 1,
    D7_HAMT_DUPLICATE_KEY = 2,
    D7_HAMT_INVALID_ARGUMENT = 3,
    D7_HAMT_TRANSIENT_CONSUMED = 4,
    D7_HAMT_TRANSIENT_MODIFIED = 5,
    D7_HAMT_OVERFLOW = 6,
    D7_HAMT_DUPLICATE_VALUE = 7
} d7_hamt_status;

typedef enum d7_hamt_node_kind {
    D7_HAMT_NODE_EMPTY = 0,
    D7_HAMT_NODE_LEAF = 1,
    D7_HAMT_NODE_COLLISION = 2,
    D7_HAMT_NODE_BITMAP_INDEXED = 3
} d7_hamt_node_kind;

typedef uint32_t (*d7_hamt_hash_fn)(const void *item, void *context);
typedef bool (*d7_hamt_equal_fn)(const void *left, const void *right, void *context);
typedef void *(*d7_hamt_retain_fn)(const void *item, void *context);
typedef void (*d7_hamt_release_fn)(void *item, void *context);
typedef d7_hamt_status (*d7_hamt_map_add_factory_fn)(
    const void *lookup_key,
    void *context,
    const void **value);
typedef d7_hamt_status (*d7_hamt_map_update_factory_fn)(
    const void *lookup_key,
    const void *stored_value,
    void *context,
    const void **value);

typedef struct d7_hamt_policy {
    d7_hamt_hash_fn hash;
    d7_hamt_equal_fn key_equal;
    d7_hamt_equal_fn value_equal;
    d7_hamt_retain_fn retain_key;
    d7_hamt_retain_fn retain_value;
    d7_hamt_release_fn release_key;
    d7_hamt_release_fn release_value;
    void *context;
} d7_hamt_policy;

typedef struct d7_hamt_set_policy {
    d7_hamt_hash_fn hash;
    d7_hamt_equal_fn equal;
    d7_hamt_retain_fn retain_item;
    d7_hamt_release_fn release_item;
    void *context;
} d7_hamt_set_policy;

typedef struct d7_hamt_entry {
    const void *key;
    const void *value;
} d7_hamt_entry;

typedef enum d7_hamt_difference_kind {
    D7_HAMT_DIFFERENCE_ADDED = 0,
    D7_HAMT_DIFFERENCE_REMOVED = 1,
    D7_HAMT_DIFFERENCE_CHANGED = 2
} d7_hamt_difference_kind;

typedef struct d7_hamt_difference {
    d7_hamt_difference_kind kind;
    const void *key;
    const void *before;
    const void *after;
} d7_hamt_difference;

typedef void (*d7_hamt_difference_visitor)(
    const d7_hamt_difference *difference,
    void *context);

struct d7_hamt_node;

typedef struct d7_hamt_map {
    const struct d7_hamt_node *root;
    size_t count;
    d7_hamt_policy policy;
} d7_hamt_map;

typedef struct d7_hamt_set {
    d7_hamt_map map;
} d7_hamt_set;

typedef struct d7_hamt_map_iterator_frame {
    const void *data;
    size_t data_count;
    size_t data_index;
    const struct d7_hamt_node *const *children;
    size_t child_count;
    size_t child_index;
} d7_hamt_map_iterator_frame;

typedef struct d7_hamt_map_iterator {
    const struct d7_hamt_node *next;
    d7_hamt_map_iterator_frame frames[7];
    size_t depth;
    const d7_hamt_entry *collision_entries;
    size_t collision_count;
    size_t collision_index;
} d7_hamt_map_iterator;

typedef struct d7_hamt_set_iterator {
    d7_hamt_map_iterator inner;
} d7_hamt_set_iterator;

struct d7_hamt_map_transient_state;

/* A one-way edit-session handle. The state is opaque and reference-counted;
 * use d7_hamt_map_transient_clone rather than copying a live handle by
 * assignment, and destroy every initialized handle. Cloned handles alias one
 * logical session, so publishing through one consumes all of them. */
typedef struct d7_hamt_map_transient {
    struct d7_hamt_map_transient_state *state;
} d7_hamt_map_transient;

typedef struct d7_hamt_set_transient {
    d7_hamt_map_transient inner;
} d7_hamt_set_transient;

/* Transient iterators borrow their session state. Keep at least one owning
 * session handle alive until iteration ends. A changed edit invalidates an
 * iterator with D7_HAMT_TRANSIENT_MODIFIED; publication invalidates it with
 * D7_HAMT_TRANSIENT_CONSUMED. */
typedef struct d7_hamt_map_transient_iterator {
    const struct d7_hamt_map_transient_state *state;
    size_t version;
    d7_hamt_map_iterator inner;
} d7_hamt_map_transient_iterator;

typedef struct d7_hamt_set_transient_iterator {
    d7_hamt_map_transient_iterator inner;
} d7_hamt_set_transient_iterator;

/* The default hashing and equivalence policy, using the platform's own hashing and equality. */
d7_hamt_policy d7_hamt_policy_default(void);
/* The default policy. */
d7_hamt_set_policy d7_hamt_set_policy_default(void);

/* Initializes an empty map using the supplied policies, which it retains. */
d7_hamt_map d7_hamt_map_create(const d7_hamt_policy *policy);
/* Initializes a map holding the entries in the range. */
d7_hamt_status d7_hamt_map_create_range(
    const d7_hamt_policy *policy,
    const d7_hamt_entry *entries,
    size_t entry_count,
    d7_hamt_map *result);
/* Initializes a second handle on the same map version, taking a reference rather than copying. */
d7_hamt_map d7_hamt_map_clone(const d7_hamt_map *map);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_map_destroy(d7_hamt_map *map);

/* Number of entries in the map. */
size_t d7_hamt_map_count(const d7_hamt_map *map);
/* Whether the map holds no entries. */
bool d7_hamt_map_is_empty(const d7_hamt_map *map);
/* Whether the key is present. */
bool d7_hamt_map_contains_key(const d7_hamt_map *map, const void *key);
/* Reads the value stored for the key, reporting whether it was present. */
bool d7_hamt_map_try_get(const d7_hamt_map *map, const void *key, const void **value);
/* Reads the stored key representative, reporting whether the key was present. */
bool d7_hamt_map_try_get_key(const d7_hamt_map *map, const void *equal_key, const void **actual_key);

/* Produces a map with the key bound to the value, adding or replacing as needed. */
d7_hamt_status d7_hamt_map_set(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result);
/* Produces a map with every given key bound to its value. */
d7_hamt_status d7_hamt_map_set_many(
    const d7_hamt_map *map,
    const d7_hamt_entry *entries,
    size_t entry_count,
    d7_hamt_map *result);
/* Produces a map containing the given entry. */
d7_hamt_status d7_hamt_map_add(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result);
/* Adds the entry unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_map_try_add(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result,
    bool *added);
/* Selects and publishes one value through a single hash-trie descent. Factory
 * outputs are borrowed candidates and are retained under the map policy before
 * publication. `selected_value`, when non-NULL, receives the concrete value
 * representative stored in `result`; it remains borrowed from that result.
 * Every factory argument is validated before hashing, even when its branch is
 * not selected. On failure, `result` and `selected_value` are unchanged. */
d7_hamt_status d7_hamt_map_get_or_add(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map_add_factory_fn add_factory,
    void *add_context,
    d7_hamt_map *result,
    const void **selected_value);
/* Produces a map with the key bound to the value, adding or replacing as needed. */
d7_hamt_status d7_hamt_map_add_or_update(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map_add_factory_fn add_factory,
    void *add_context,
    d7_hamt_map_update_factory_fn update_factory,
    void *update_context,
    d7_hamt_map *result,
    const void **selected_value);
/* Produces a map without that entry. */
d7_hamt_status d7_hamt_map_remove(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map *result);
/* Removes the entry, reporting whether it was present. */
d7_hamt_status d7_hamt_map_try_remove(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map *result,
    bool *removed,
    const void **removed_value);
/* Produces an empty map retaining the same policies. */
d7_hamt_status d7_hamt_map_clear(const d7_hamt_map *map, d7_hamt_map *result);
/* Produces the entries of both maps. Subtrees the operands already share are adopted whole rather
 * than re-entered. */
d7_hamt_status d7_hamt_map_union(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);
/* Produces the entries present in both maps. */
d7_hamt_status d7_hamt_map_intersect(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);
/* Produces this map's entries that are absent from the other. */
d7_hamt_status d7_hamt_map_except(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);
/* Produces the entries present in exactly one of the two maps. */
d7_hamt_status d7_hamt_map_symmetric_except(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);

/* Initializes an iterator over the entries. */
void d7_hamt_map_iterator_init(const d7_hamt_map *map, d7_hamt_map_iterator *iterator);
/* Advances to the next entry, reporting whether there was one. */
bool d7_hamt_map_iterator_next(
    d7_hamt_map_iterator *iterator,
    const void **key,
    const void **value);

/* Whether both handles reference the same root node. A representation check used to confirm a no-op
 * avoided copying, not an equality test. */
bool d7_hamt_map_shares_root(const d7_hamt_map *left, const d7_hamt_map *right);
/* Whether both maps hold the same entries. */
bool d7_hamt_map_equals(const d7_hamt_map *left, const d7_hamt_map *right);
/* Produces the entry-level differences between two maps. Subtrees the two maps share are skipped
 * whole, so the cost tracks what actually changed. */
d7_hamt_status d7_hamt_map_diff(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_difference_visitor visitor,
    void *context);
/* The root node's address, for tests that a no-op shared rather than copied. */
const void *d7_hamt_map_debug_root_identity(const d7_hamt_map *map);
/* The root node's kind, for shape assertions. */
d7_hamt_node_kind d7_hamt_map_debug_root_kind(const d7_hamt_map *map);
/* The root's child addresses, for tests about which subtrees were shared. */
size_t d7_hamt_map_debug_root_child_identities(
    const d7_hamt_map *map,
    const void **children,
    size_t child_capacity);
/* Checks that the structure is in its canonical shape, which must depend only on contents and not
 * on edit history. */
bool d7_hamt_map_debug_validate_canonical(const d7_hamt_map *map);
/* Returns false unless both maps have compatible hash/equality callbacks and context.
 * Equal-hash collision contents are matched without regard to insertion order by
 * invoking the receiver map's key-equality callback. */
bool d7_hamt_map_debug_topology_equal(
    const d7_hamt_map *left,
    const d7_hamt_map *right);

/* One-way map edit sessions. Creation/adoption and publication are O(1) in
 * trie size. Point edits currently use the persistent path-copy operations;
 * this surface makes no in-place-edit or throughput claim. Output handles
 * must not already own a live value. On failure, outputs and session state
 * are unchanged. */
d7_hamt_status d7_hamt_map_transient_create(
    const d7_hamt_policy *policy,
    d7_hamt_map_transient *result);
/* Opens a mutable session seeded from this map. The map itself is unaffected; the session's edits
 * are visible only through it. */
d7_hamt_status d7_hamt_map_to_transient(
    const d7_hamt_map *map,
    d7_hamt_map_transient *result);
/* Initializes a second handle on the same session version, taking a reference rather than
 * copying. */
d7_hamt_status d7_hamt_map_transient_clone(
    const d7_hamt_map_transient *transient,
    d7_hamt_map_transient *result);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_map_transient_destroy(d7_hamt_map_transient *transient);
/* Whether the session is still open. A persisted session is spent and rejects further edits. */
bool d7_hamt_map_transient_is_active(const d7_hamt_map_transient *transient);
/* The policy the session retains. */
d7_hamt_status d7_hamt_map_transient_get_policy(
    const d7_hamt_map_transient *transient,
    d7_hamt_policy *policy);
/* Number of entries in the session. */
d7_hamt_status d7_hamt_map_transient_count(
    const d7_hamt_map_transient *transient,
    size_t *count);
/* Whether the key is present. */
d7_hamt_status d7_hamt_map_transient_contains_key(
    const d7_hamt_map_transient *transient,
    const void *key,
    bool *contains);
/* Reads the value stored for the key, reporting whether it was present. */
d7_hamt_status d7_hamt_map_transient_try_get(
    const d7_hamt_map_transient *transient,
    const void *key,
    bool *found,
    const void **value);
/* Reads the stored key representative, reporting whether the key was present. */
d7_hamt_status d7_hamt_map_transient_try_get_key(
    const d7_hamt_map_transient *transient,
    const void *equal_key,
    bool *found,
    const void **actual_key);
/* Produces a session with the key bound to the value, adding or replacing as needed. */
d7_hamt_status d7_hamt_map_transient_set(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value);
/* Produces a session containing the given entry. */
d7_hamt_status d7_hamt_map_transient_add(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value);
/* Adds the entry unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_map_transient_try_add(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value,
    bool *added);
/* Produces a session without that entry. */
d7_hamt_status d7_hamt_map_transient_remove(
    d7_hamt_map_transient *transient,
    const void *key);
/* Removes the entry, reporting whether it was present. */
d7_hamt_status d7_hamt_map_transient_try_remove(
    d7_hamt_map_transient *transient,
    const void *key,
    bool *removed);
/* Produces an empty session retaining the same policies. */
d7_hamt_status d7_hamt_map_transient_clear(d7_hamt_map_transient *transient);
/* Initializes an iterator over the entries. */
d7_hamt_status d7_hamt_map_transient_iterator_init(
    const d7_hamt_map_transient *transient,
    d7_hamt_map_transient_iterator *iterator);
/* Advances to the next entry, reporting whether there was one. */
d7_hamt_status d7_hamt_map_transient_iterator_next(
    d7_hamt_map_transient_iterator *iterator,
    bool *has_value,
    const void **key,
    const void **value);
/* Closes the session and produces the map holding its accumulated edits. */
d7_hamt_status d7_hamt_map_transient_persist(
    d7_hamt_map_transient *transient,
    d7_hamt_map *result);
/* The root node's address, for tests that a no-op shared rather than copied. */
const void *d7_hamt_map_transient_debug_root_identity(
    const d7_hamt_map_transient *transient);

/* Initializes an empty set using the supplied policies, which it retains. */
d7_hamt_set d7_hamt_set_create(const d7_hamt_set_policy *policy);
/* Initializes a set holding the elements in the range. */
d7_hamt_status d7_hamt_set_create_range(
    const d7_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
/* Initializes a second handle on the same set version, taking a reference rather than copying. */
d7_hamt_set d7_hamt_set_clone(const d7_hamt_set *set);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_set_destroy(d7_hamt_set *set);

/* Number of elements in the set. */
size_t d7_hamt_set_count(const d7_hamt_set *set);
/* Whether the set holds no elements. */
bool d7_hamt_set_is_empty(const d7_hamt_set *set);
/* Whether the element is present. */
bool d7_hamt_set_contains(const d7_hamt_set *set, const void *item);
/* Reads the stored value, reporting whether the key was present. */
bool d7_hamt_set_try_get_value(const d7_hamt_set *set, const void *equal_value, const void **actual_value);

/* Produces a set containing the given element. */
d7_hamt_status d7_hamt_set_add(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result);
/* Adds the element unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_set_try_add(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result,
    bool *added);
/* Produces a set without that element. */
d7_hamt_status d7_hamt_set_remove(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result);
/* Removes the element, reporting whether it was present. */
d7_hamt_status d7_hamt_set_try_remove(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result,
    bool *removed);
/* Produces an empty set retaining the same policies. */
d7_hamt_status d7_hamt_set_clear(const d7_hamt_set *set, d7_hamt_set *result);
/* Produces the elements of both sets. Subtrees the operands already share are adopted whole rather
 * than re-entered. */
d7_hamt_status d7_hamt_set_union(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);
/* Produces the elements present in both sets. */
d7_hamt_status d7_hamt_set_intersect(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);
/* Produces this set's elements that are absent from the other. */
d7_hamt_status d7_hamt_set_except(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);
/* Produces the elements present in exactly one of the two sets. */
d7_hamt_status d7_hamt_set_symmetric_except(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);

/* Produces the union with the elements of a plain array. */
d7_hamt_status d7_hamt_set_union_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
/* Produces the elements also present in a plain array. */
d7_hamt_status d7_hamt_set_intersect_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
/* Produces this set's elements that are absent from a plain array. */
d7_hamt_status d7_hamt_set_except_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
/* Produces the elements present in exactly one of this set and a plain array. */
d7_hamt_status d7_hamt_set_symmetric_except_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);

/* The relation predicates report their answer through *result and return a
 * status so an allocation failure while building the internal probe set is
 * distinguishable from a genuine negative answer. *result is written false
 * before any fallible work. */
d7_hamt_status d7_hamt_set_is_subset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this set is a proper subset of a plain array's elements. */
d7_hamt_status d7_hamt_set_is_proper_subset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether every element of a plain array occurs in this set. */
d7_hamt_status d7_hamt_set_is_superset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this set is a proper superset of a plain array's elements. */
d7_hamt_status d7_hamt_set_is_proper_superset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this set shares an element with a plain array. */
d7_hamt_status d7_hamt_set_overlaps_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this set holds exactly a plain array's elements. */
d7_hamt_status d7_hamt_set_equals_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether every element of this set also occurs in the other. */
d7_hamt_status d7_hamt_set_is_subset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
/* Whether this set is a subset of the other and the other holds an element it lacks. */
d7_hamt_status d7_hamt_set_is_proper_subset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
/* Whether every element of the other occurs in this set. */
d7_hamt_status d7_hamt_set_is_superset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
/* Whether this set is a superset of the other and holds an element the other lacks. */
d7_hamt_status d7_hamt_set_is_proper_superset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
/* Whether the two sets share at least one element. */
d7_hamt_status d7_hamt_set_overlaps(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
/* Whether both sets hold the same elements. */
d7_hamt_status d7_hamt_set_equals(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);

/* Initializes an iterator over the elements. */
void d7_hamt_set_iterator_init(const d7_hamt_set *set, d7_hamt_set_iterator *iterator);
/* Advances to the next element, reporting whether there was one. */
bool d7_hamt_set_iterator_next(d7_hamt_set_iterator *iterator, const void **item);

/* Whether both handles reference the same root node. A representation check used to confirm a no-op
 * avoided copying, not an equality test. */
bool d7_hamt_set_shares_root(const d7_hamt_set *left, const d7_hamt_set *right);
/* The root node's address, for tests that a no-op shared rather than copied. */
const void *d7_hamt_set_debug_root_identity(const d7_hamt_set *set);
/* The root node's kind, for shape assertions. */
d7_hamt_node_kind d7_hamt_set_debug_root_kind(const d7_hamt_set *set);

/* Set edit sessions share the map session lifecycle and failure contract.
 * They preserve the set hash/equality/ownership policy and first stored item
 * representative, while changed edits use persistent path copying. Relation
 * APIs use receiver-policy semantics and write their boolean only on success. */
d7_hamt_status d7_hamt_set_transient_create(
    const d7_hamt_set_policy *policy,
    d7_hamt_set_transient *result);
/* Opens a mutable session seeded from this set. The set itself is unaffected. */
d7_hamt_status d7_hamt_set_to_transient(
    const d7_hamt_set *set,
    d7_hamt_set_transient *result);
/* Initializes a second handle on the same session version, taking a reference rather than
 * copying. */
d7_hamt_status d7_hamt_set_transient_clone(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_transient *result);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_set_transient_destroy(d7_hamt_set_transient *transient);
/* Whether the session is still open. A persisted session is spent and rejects further edits. */
bool d7_hamt_set_transient_is_active(const d7_hamt_set_transient *transient);
/* The policy the session retains. */
d7_hamt_status d7_hamt_set_transient_get_policy(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_policy *policy);
/* Number of elements in the session. */
d7_hamt_status d7_hamt_set_transient_count(
    const d7_hamt_set_transient *transient,
    size_t *count);
/* Whether the element is present. */
d7_hamt_status d7_hamt_set_transient_contains(
    const d7_hamt_set_transient *transient,
    const void *item,
    bool *contains);
/* Reads the stored value, reporting whether the key was present. */
d7_hamt_status d7_hamt_set_transient_try_get_value(
    const d7_hamt_set_transient *transient,
    const void *equal_value,
    bool *found,
    const void **actual_value);
/* Produces a session containing the given element. */
d7_hamt_status d7_hamt_set_transient_add(
    d7_hamt_set_transient *transient,
    const void *item);
/* Adds the element unless an equivalent one is present, reporting which happened. */
d7_hamt_status d7_hamt_set_transient_try_add(
    d7_hamt_set_transient *transient,
    const void *item,
    bool *added);
/* Produces a session without that element. */
d7_hamt_status d7_hamt_set_transient_remove(
    d7_hamt_set_transient *transient,
    const void *item);
/* Removes the element, reporting whether it was present. */
d7_hamt_status d7_hamt_set_transient_try_remove(
    d7_hamt_set_transient *transient,
    const void *item,
    bool *removed);
/* Produces an empty session retaining the same policies. */
d7_hamt_status d7_hamt_set_transient_clear(d7_hamt_set_transient *transient);
/* Whether every element of this session occurs in a plain array. */
d7_hamt_status d7_hamt_set_transient_is_subset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this session is a proper subset of a plain array's elements. */
d7_hamt_status d7_hamt_set_transient_is_proper_subset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether every element of a plain array occurs in this session. */
d7_hamt_status d7_hamt_set_transient_is_superset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this session is a proper superset of a plain array's elements. */
d7_hamt_status d7_hamt_set_transient_is_proper_superset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this session shares an element with a plain array. */
d7_hamt_status d7_hamt_set_transient_overlaps_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether this session holds exactly a plain array's elements. */
d7_hamt_status d7_hamt_set_transient_equals_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
/* Whether every element of this session also occurs in the other. */
d7_hamt_status d7_hamt_set_transient_is_subset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
/* Whether this session is a subset of the other and the other holds an element it lacks. */
d7_hamt_status d7_hamt_set_transient_is_proper_subset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
/* Whether every element of the other occurs in this session. */
d7_hamt_status d7_hamt_set_transient_is_superset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
/* Whether this session is a superset of the other and holds an element the other lacks. */
d7_hamt_status d7_hamt_set_transient_is_proper_superset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
/* Whether the two sessions share at least one element. */
d7_hamt_status d7_hamt_set_transient_overlaps(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
/* Whether both sessions hold the same elements. */
d7_hamt_status d7_hamt_set_transient_equals(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
/* Initializes an iterator over the elements. */
d7_hamt_status d7_hamt_set_transient_iterator_init(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_transient_iterator *iterator);
/* Advances to the next element, reporting whether there was one. */
d7_hamt_status d7_hamt_set_transient_iterator_next(
    d7_hamt_set_transient_iterator *iterator,
    bool *has_value,
    const void **item);
/* Closes the session and produces the set holding its accumulated edits. */
d7_hamt_status d7_hamt_set_transient_persist(
    d7_hamt_set_transient *transient,
    d7_hamt_set *result);
/* The root node's address, for tests that a no-op shared rather than copied. */
const void *d7_hamt_set_transient_debug_root_identity(
    const d7_hamt_set_transient *transient);

#ifdef __cplusplus
}
#endif

#endif
