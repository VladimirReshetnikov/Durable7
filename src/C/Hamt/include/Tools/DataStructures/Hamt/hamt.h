#ifndef TOOLS_DATA_STRUCTURES_HAMT_HAMT_H
#define TOOLS_DATA_STRUCTURES_HAMT_HAMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tds_hamt_status {
    TDS_HAMT_OK = 0,
    TDS_HAMT_OUT_OF_MEMORY = 1,
    TDS_HAMT_DUPLICATE_KEY = 2,
    TDS_HAMT_INVALID_ARGUMENT = 3
} tds_hamt_status;

typedef enum tds_hamt_node_kind {
    TDS_HAMT_NODE_EMPTY = 0,
    TDS_HAMT_NODE_LEAF = 1,
    TDS_HAMT_NODE_COLLISION = 2,
    TDS_HAMT_NODE_BITMAP_INDEXED = 3
} tds_hamt_node_kind;

typedef uint32_t (*tds_hamt_hash_fn)(const void *item, void *context);
typedef bool (*tds_hamt_equal_fn)(const void *left, const void *right, void *context);
typedef void *(*tds_hamt_retain_fn)(const void *item, void *context);
typedef void (*tds_hamt_release_fn)(void *item, void *context);

typedef struct tds_hamt_policy {
    tds_hamt_hash_fn hash;
    tds_hamt_equal_fn key_equal;
    tds_hamt_equal_fn value_equal;
    tds_hamt_retain_fn retain_key;
    tds_hamt_retain_fn retain_value;
    tds_hamt_release_fn release_key;
    tds_hamt_release_fn release_value;
    void *context;
} tds_hamt_policy;

typedef struct tds_hamt_set_policy {
    tds_hamt_hash_fn hash;
    tds_hamt_equal_fn equal;
    tds_hamt_retain_fn retain_item;
    tds_hamt_release_fn release_item;
    void *context;
} tds_hamt_set_policy;

typedef struct tds_hamt_entry {
    const void *key;
    const void *value;
} tds_hamt_entry;

typedef enum tds_hamt_difference_kind {
    TDS_HAMT_DIFFERENCE_ADDED = 0,
    TDS_HAMT_DIFFERENCE_REMOVED = 1,
    TDS_HAMT_DIFFERENCE_CHANGED = 2
} tds_hamt_difference_kind;

typedef struct tds_hamt_difference {
    tds_hamt_difference_kind kind;
    const void *key;
    const void *before;
    const void *after;
} tds_hamt_difference;

typedef void (*tds_hamt_difference_visitor)(
    const tds_hamt_difference *difference,
    void *context);

struct tds_hamt_node;

typedef struct tds_hamt_map {
    const struct tds_hamt_node *root;
    size_t count;
    tds_hamt_policy policy;
} tds_hamt_map;

typedef struct tds_hamt_set {
    tds_hamt_map map;
} tds_hamt_set;

typedef struct tds_hamt_map_iterator_frame {
    const void *data;
    size_t data_count;
    size_t data_index;
    const struct tds_hamt_node *const *children;
    size_t child_count;
    size_t child_index;
} tds_hamt_map_iterator_frame;

typedef struct tds_hamt_map_iterator {
    const struct tds_hamt_node *next;
    tds_hamt_map_iterator_frame frames[7];
    size_t depth;
    const tds_hamt_entry *collision_entries;
    size_t collision_count;
    size_t collision_index;
} tds_hamt_map_iterator;

typedef struct tds_hamt_set_iterator {
    tds_hamt_map_iterator inner;
} tds_hamt_set_iterator;

tds_hamt_policy tds_hamt_policy_default(void);
tds_hamt_set_policy tds_hamt_set_policy_default(void);

tds_hamt_map tds_hamt_map_create(const tds_hamt_policy *policy);
tds_hamt_status tds_hamt_map_create_range(
    const tds_hamt_policy *policy,
    const tds_hamt_entry *entries,
    size_t entry_count,
    tds_hamt_map *result);
tds_hamt_map tds_hamt_map_clone(const tds_hamt_map *map);
void tds_hamt_map_destroy(tds_hamt_map *map);

size_t tds_hamt_map_count(const tds_hamt_map *map);
bool tds_hamt_map_is_empty(const tds_hamt_map *map);
bool tds_hamt_map_contains_key(const tds_hamt_map *map, const void *key);
bool tds_hamt_map_try_get(const tds_hamt_map *map, const void *key, const void **value);
bool tds_hamt_map_try_get_key(const tds_hamt_map *map, const void *equal_key, const void **actual_key);

tds_hamt_status tds_hamt_map_set(
    const tds_hamt_map *map,
    const void *key,
    const void *value,
    tds_hamt_map *result);
tds_hamt_status tds_hamt_map_set_many(
    const tds_hamt_map *map,
    const tds_hamt_entry *entries,
    size_t entry_count,
    tds_hamt_map *result);
tds_hamt_status tds_hamt_map_add(
    const tds_hamt_map *map,
    const void *key,
    const void *value,
    tds_hamt_map *result);
tds_hamt_status tds_hamt_map_try_add(
    const tds_hamt_map *map,
    const void *key,
    const void *value,
    tds_hamt_map *result,
    bool *added);
tds_hamt_status tds_hamt_map_remove(
    const tds_hamt_map *map,
    const void *key,
    tds_hamt_map *result);
tds_hamt_status tds_hamt_map_try_remove(
    const tds_hamt_map *map,
    const void *key,
    tds_hamt_map *result,
    bool *removed,
    const void **removed_value);
tds_hamt_status tds_hamt_map_clear(const tds_hamt_map *map, tds_hamt_map *result);

void tds_hamt_map_iterator_init(const tds_hamt_map *map, tds_hamt_map_iterator *iterator);
bool tds_hamt_map_iterator_next(
    tds_hamt_map_iterator *iterator,
    const void **key,
    const void **value);

bool tds_hamt_map_shares_root(const tds_hamt_map *left, const tds_hamt_map *right);
bool tds_hamt_map_equals(const tds_hamt_map *left, const tds_hamt_map *right);
tds_hamt_status tds_hamt_map_diff(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_difference_visitor visitor,
    void *context);
const void *tds_hamt_map_debug_root_identity(const tds_hamt_map *map);
tds_hamt_node_kind tds_hamt_map_debug_root_kind(const tds_hamt_map *map);
size_t tds_hamt_map_debug_root_child_identities(
    const tds_hamt_map *map,
    const void **children,
    size_t child_capacity);
bool tds_hamt_map_debug_validate_canonical(const tds_hamt_map *map);
bool tds_hamt_map_debug_topology_equal(
    const tds_hamt_map *left,
    const tds_hamt_map *right);

tds_hamt_set tds_hamt_set_create(const tds_hamt_set_policy *policy);
tds_hamt_status tds_hamt_set_create_range(
    const tds_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result);
tds_hamt_set tds_hamt_set_clone(const tds_hamt_set *set);
void tds_hamt_set_destroy(tds_hamt_set *set);

size_t tds_hamt_set_count(const tds_hamt_set *set);
bool tds_hamt_set_is_empty(const tds_hamt_set *set);
bool tds_hamt_set_contains(const tds_hamt_set *set, const void *item);
bool tds_hamt_set_try_get_value(const tds_hamt_set *set, const void *equal_value, const void **actual_value);

tds_hamt_status tds_hamt_set_add(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result);
tds_hamt_status tds_hamt_set_try_add(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result,
    bool *added);
tds_hamt_status tds_hamt_set_remove(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result);
tds_hamt_status tds_hamt_set_try_remove(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result,
    bool *removed);
tds_hamt_status tds_hamt_set_clear(const tds_hamt_set *set, tds_hamt_set *result);

tds_hamt_status tds_hamt_set_union_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result);
tds_hamt_status tds_hamt_set_intersect_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result);
tds_hamt_status tds_hamt_set_except_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result);
tds_hamt_status tds_hamt_set_symmetric_except_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result);

/* The relation predicates report their answer through *result and return a
 * status so an allocation failure while building the internal probe set is
 * distinguishable from a genuine negative answer. *result is written false
 * before any fallible work. */
tds_hamt_status tds_hamt_set_is_subset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
tds_hamt_status tds_hamt_set_is_proper_subset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
tds_hamt_status tds_hamt_set_is_superset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
tds_hamt_status tds_hamt_set_is_proper_superset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
tds_hamt_status tds_hamt_set_overlaps_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);
tds_hamt_status tds_hamt_set_equals_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result);

void tds_hamt_set_iterator_init(const tds_hamt_set *set, tds_hamt_set_iterator *iterator);
bool tds_hamt_set_iterator_next(tds_hamt_set_iterator *iterator, const void **item);

bool tds_hamt_set_shares_root(const tds_hamt_set *left, const tds_hamt_set *right);
const void *tds_hamt_set_debug_root_identity(const tds_hamt_set *set);
tds_hamt_node_kind tds_hamt_set_debug_root_kind(const tds_hamt_set *set);

#ifdef __cplusplus
}
#endif

#endif
