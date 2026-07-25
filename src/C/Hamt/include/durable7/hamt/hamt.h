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

d7_hamt_policy d7_hamt_policy_default(void);
d7_hamt_set_policy d7_hamt_set_policy_default(void);

d7_hamt_map d7_hamt_map_create(const d7_hamt_policy *policy);
d7_hamt_status d7_hamt_map_create_range(
    const d7_hamt_policy *policy,
    const d7_hamt_entry *entries,
    size_t entry_count,
    d7_hamt_map *result);
d7_hamt_map d7_hamt_map_clone(const d7_hamt_map *map);
void d7_hamt_map_destroy(d7_hamt_map *map);

size_t d7_hamt_map_count(const d7_hamt_map *map);
bool d7_hamt_map_is_empty(const d7_hamt_map *map);
bool d7_hamt_map_contains_key(const d7_hamt_map *map, const void *key);
bool d7_hamt_map_try_get(const d7_hamt_map *map, const void *key, const void **value);
bool d7_hamt_map_try_get_key(const d7_hamt_map *map, const void *equal_key, const void **actual_key);

d7_hamt_status d7_hamt_map_set(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result);
d7_hamt_status d7_hamt_map_set_many(
    const d7_hamt_map *map,
    const d7_hamt_entry *entries,
    size_t entry_count,
    d7_hamt_map *result);
d7_hamt_status d7_hamt_map_add(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result);
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
d7_hamt_status d7_hamt_map_add_or_update(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map_add_factory_fn add_factory,
    void *add_context,
    d7_hamt_map_update_factory_fn update_factory,
    void *update_context,
    d7_hamt_map *result,
    const void **selected_value);
d7_hamt_status d7_hamt_map_remove(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map *result);
d7_hamt_status d7_hamt_map_try_remove(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map *result,
    bool *removed,
    const void **removed_value);
d7_hamt_status d7_hamt_map_clear(const d7_hamt_map *map, d7_hamt_map *result);
d7_hamt_status d7_hamt_map_union(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);
d7_hamt_status d7_hamt_map_intersect(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);
d7_hamt_status d7_hamt_map_except(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);
d7_hamt_status d7_hamt_map_symmetric_except(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result);

void d7_hamt_map_iterator_init(const d7_hamt_map *map, d7_hamt_map_iterator *iterator);
bool d7_hamt_map_iterator_next(
    d7_hamt_map_iterator *iterator,
    const void **key,
    const void **value);

bool d7_hamt_map_shares_root(const d7_hamt_map *left, const d7_hamt_map *right);
bool d7_hamt_map_equals(const d7_hamt_map *left, const d7_hamt_map *right);
d7_hamt_status d7_hamt_map_diff(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_difference_visitor visitor,
    void *context);
const void *d7_hamt_map_debug_root_identity(const d7_hamt_map *map);
d7_hamt_node_kind d7_hamt_map_debug_root_kind(const d7_hamt_map *map);
size_t d7_hamt_map_debug_root_child_identities(
    const d7_hamt_map *map,
    const void **children,
    size_t child_capacity);
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
d7_hamt_status d7_hamt_map_to_transient(
    const d7_hamt_map *map,
    d7_hamt_map_transient *result);
d7_hamt_status d7_hamt_map_transient_clone(
    const d7_hamt_map_transient *transient,
    d7_hamt_map_transient *result);
void d7_hamt_map_transient_destroy(d7_hamt_map_transient *transient);
bool d7_hamt_map_transient_is_active(const d7_hamt_map_transient *transient);
d7_hamt_status d7_hamt_map_transient_get_policy(
    const d7_hamt_map_transient *transient,
    d7_hamt_policy *policy);
d7_hamt_status d7_hamt_map_transient_count(
    const d7_hamt_map_transient *transient,
    size_t *count);
d7_hamt_status d7_hamt_map_transient_contains_key(
    const d7_hamt_map_transient *transient,
    const void *key,
    bool *contains);
d7_hamt_status d7_hamt_map_transient_try_get(
    const d7_hamt_map_transient *transient,
    const void *key,
    bool *found,
    const void **value);
d7_hamt_status d7_hamt_map_transient_try_get_key(
    const d7_hamt_map_transient *transient,
    const void *equal_key,
    bool *found,
    const void **actual_key);
d7_hamt_status d7_hamt_map_transient_set(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value);
d7_hamt_status d7_hamt_map_transient_add(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value);
d7_hamt_status d7_hamt_map_transient_try_add(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value,
    bool *added);
d7_hamt_status d7_hamt_map_transient_remove(
    d7_hamt_map_transient *transient,
    const void *key);
d7_hamt_status d7_hamt_map_transient_try_remove(
    d7_hamt_map_transient *transient,
    const void *key,
    bool *removed);
d7_hamt_status d7_hamt_map_transient_clear(d7_hamt_map_transient *transient);
d7_hamt_status d7_hamt_map_transient_iterator_init(
    const d7_hamt_map_transient *transient,
    d7_hamt_map_transient_iterator *iterator);
d7_hamt_status d7_hamt_map_transient_iterator_next(
    d7_hamt_map_transient_iterator *iterator,
    bool *has_value,
    const void **key,
    const void **value);
d7_hamt_status d7_hamt_map_transient_persist(
    d7_hamt_map_transient *transient,
    d7_hamt_map *result);
const void *d7_hamt_map_transient_debug_root_identity(
    const d7_hamt_map_transient *transient);

d7_hamt_set d7_hamt_set_create(const d7_hamt_set_policy *policy);
d7_hamt_status d7_hamt_set_create_range(
    const d7_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
d7_hamt_set d7_hamt_set_clone(const d7_hamt_set *set);
void d7_hamt_set_destroy(d7_hamt_set *set);

size_t d7_hamt_set_count(const d7_hamt_set *set);
bool d7_hamt_set_is_empty(const d7_hamt_set *set);
bool d7_hamt_set_contains(const d7_hamt_set *set, const void *item);
bool d7_hamt_set_try_get_value(const d7_hamt_set *set, const void *equal_value, const void **actual_value);

d7_hamt_status d7_hamt_set_add(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_try_add(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result,
    bool *added);
d7_hamt_status d7_hamt_set_remove(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_try_remove(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result,
    bool *removed);
d7_hamt_status d7_hamt_set_clear(const d7_hamt_set *set, d7_hamt_set *result);
d7_hamt_status d7_hamt_set_union(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_intersect(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_except(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_symmetric_except(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result);

d7_hamt_status d7_hamt_set_union_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_intersect_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
d7_hamt_status d7_hamt_set_except_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result);
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
d7_hamt_status d7_hamt_set_is_proper_subset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_is_superset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_is_proper_superset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_overlaps_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_equals_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_is_subset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
d7_hamt_status d7_hamt_set_is_proper_subset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
d7_hamt_status d7_hamt_set_is_superset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
d7_hamt_status d7_hamt_set_is_proper_superset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
d7_hamt_status d7_hamt_set_overlaps(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);
d7_hamt_status d7_hamt_set_equals(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result);

void d7_hamt_set_iterator_init(const d7_hamt_set *set, d7_hamt_set_iterator *iterator);
bool d7_hamt_set_iterator_next(d7_hamt_set_iterator *iterator, const void **item);

bool d7_hamt_set_shares_root(const d7_hamt_set *left, const d7_hamt_set *right);
const void *d7_hamt_set_debug_root_identity(const d7_hamt_set *set);
d7_hamt_node_kind d7_hamt_set_debug_root_kind(const d7_hamt_set *set);

/* Set edit sessions share the map session lifecycle and failure contract.
 * They preserve the set hash/equality/ownership policy and first stored item
 * representative, while changed edits use persistent path copying. Relation
 * APIs use receiver-policy semantics and write their boolean only on success. */
d7_hamt_status d7_hamt_set_transient_create(
    const d7_hamt_set_policy *policy,
    d7_hamt_set_transient *result);
d7_hamt_status d7_hamt_set_to_transient(
    const d7_hamt_set *set,
    d7_hamt_set_transient *result);
d7_hamt_status d7_hamt_set_transient_clone(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_transient *result);
void d7_hamt_set_transient_destroy(d7_hamt_set_transient *transient);
bool d7_hamt_set_transient_is_active(const d7_hamt_set_transient *transient);
d7_hamt_status d7_hamt_set_transient_get_policy(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_policy *policy);
d7_hamt_status d7_hamt_set_transient_count(
    const d7_hamt_set_transient *transient,
    size_t *count);
d7_hamt_status d7_hamt_set_transient_contains(
    const d7_hamt_set_transient *transient,
    const void *item,
    bool *contains);
d7_hamt_status d7_hamt_set_transient_try_get_value(
    const d7_hamt_set_transient *transient,
    const void *equal_value,
    bool *found,
    const void **actual_value);
d7_hamt_status d7_hamt_set_transient_add(
    d7_hamt_set_transient *transient,
    const void *item);
d7_hamt_status d7_hamt_set_transient_try_add(
    d7_hamt_set_transient *transient,
    const void *item,
    bool *added);
d7_hamt_status d7_hamt_set_transient_remove(
    d7_hamt_set_transient *transient,
    const void *item);
d7_hamt_status d7_hamt_set_transient_try_remove(
    d7_hamt_set_transient *transient,
    const void *item,
    bool *removed);
d7_hamt_status d7_hamt_set_transient_clear(d7_hamt_set_transient *transient);
d7_hamt_status d7_hamt_set_transient_is_subset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_proper_subset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_superset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_proper_superset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_transient_overlaps_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_transient_equals_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_subset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_proper_subset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_superset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
d7_hamt_status d7_hamt_set_transient_is_proper_superset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
d7_hamt_status d7_hamt_set_transient_overlaps(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
d7_hamt_status d7_hamt_set_transient_equals(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result);
d7_hamt_status d7_hamt_set_transient_iterator_init(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_transient_iterator *iterator);
d7_hamt_status d7_hamt_set_transient_iterator_next(
    d7_hamt_set_transient_iterator *iterator,
    bool *has_value,
    const void **item);
d7_hamt_status d7_hamt_set_transient_persist(
    d7_hamt_set_transient *transient,
    d7_hamt_set *result);
const void *d7_hamt_set_transient_debug_root_identity(
    const d7_hamt_set_transient *transient);

#ifdef __cplusplus
}
#endif

#endif
