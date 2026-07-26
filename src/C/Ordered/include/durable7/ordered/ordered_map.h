/*
 * A persistent map that remembers insertion order.
 *
 * Replacing an entry's value leaves its position alone, which is what distinguishes this from a map
 * rebuilt in iteration order. Every operation returns a new version and leaves its inputs valid,
 * sharing unchanged structure, so an edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_ORDERED_C_ORDERED_MAP_H
#define DURABLE7_ORDERED_C_ORDERED_MAP_H

#include <durable7/ordered/ordered_set.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*d7_ordered_map_value_equal_fn)(
    const void* left,
    const void* right,
    void* context);
typedef void (*d7_ordered_map_visit_fn)(
    const void* key,
    const void* value,
    void* context);
typedef int (*d7_ordered_map_compare_fn)(
    const void* left_key,
    const void* left_value,
    const void* right_key,
    const void* right_value,
    void* context);

/* Callback contexts, and the contexts embedded in both value types, must
 * remain usable until every map initialized from this policy is destroyed. */
typedef struct d7_ordered_map_policy {
    ft_value_type key_type;
    ft_value_type value_type;
    d7_ordered_hash_fn hash;
    d7_ordered_equal_fn key_equal;
    d7_ordered_map_value_equal_fn value_equal;
    void* context;
} d7_ordered_map_policy;

struct d7_ordered_map_context;

/* Persistent value handle. Clone rather than assigning, move to transfer
 * ownership, and destroy each initialized handle once. Result parameters must
 * be uninitialized and must not alias the source map. */
typedef struct d7_ordered_map {
    struct d7_ordered_map_context* context;
    d7_ordered_set keys;
    d7_hamt_map values;
} d7_ordered_map;

/* Fills the policy with its defaults. */
void d7_ordered_map_policy_init(
    d7_ordered_map_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    d7_ordered_hash_fn hash,
    d7_ordered_equal_fn key_equal,
    d7_ordered_map_value_equal_fn value_equal,
    void* context);

/* Initializes the map in place. */
d7_ordered_status d7_ordered_map_init(
    d7_ordered_map* map,
    const d7_ordered_map_policy* policy);
/* Initializes a second handle on the same map version, taking a reference rather than copying. */
d7_ordered_status d7_ordered_map_clone(
    const d7_ordered_map* source,
    d7_ordered_map* destination);
/* Relocates an initialized map into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_ordered_map_move(
    d7_ordered_map* destination,
    d7_ordered_map* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_ordered_map_destroy(d7_ordered_map* map);

/* The policy the map retains. */
const d7_ordered_map_policy* d7_ordered_map_policy_of(
    const d7_ordered_map* map);
/* Whether the map holds no entries. */
bool d7_ordered_map_empty(const d7_ordered_map* map);
/* Number of entries in the map. */
size_t d7_ordered_map_size(const d7_ordered_map* map);
/* Whether the key is present. */
bool d7_ordered_map_contains_key(
    const d7_ordered_map* map,
    const void* key);
/* Reads the value stored for the key, reporting whether it was present. */
bool d7_ordered_map_try_get(
    const d7_ordered_map* map,
    const void* equal_key,
    const void** actual_key,
    const void** value);
/* Reads the entry at the given rank. */
d7_ordered_status d7_ordered_map_entry_at(
    const d7_ordered_map* map,
    size_t index,
    const void** key,
    const void** value);
/* Reads the first entry. */
d7_ordered_status d7_ordered_map_front(
    const d7_ordered_map* map,
    const void** key,
    const void** value);
/* Reads the last entry. */
d7_ordered_status d7_ordered_map_back(
    const d7_ordered_map* map,
    const void** key,
    const void** value);
/* The rank of the given key. */
bool d7_ordered_map_index_of_key(
    const d7_ordered_map* map,
    const void* equal_key,
    size_t* index);

/* Produces a map containing the given entry. */
d7_ordered_status d7_ordered_map_add(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    d7_ordered_map* result);
/* Adds the entry unless an equivalent one is present, reporting which happened. */
d7_ordered_status d7_ordered_map_try_add(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    bool* added,
    d7_ordered_map* result);
/* Produces a map with the entry placed at the front of the insertion order. */
d7_ordered_status d7_ordered_map_add_first(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    d7_ordered_map* result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
d7_ordered_status d7_ordered_map_insert(
    const d7_ordered_map* map,
    size_t index,
    const void* key,
    const void* value,
    d7_ordered_map* result);
/* Adds an absent key at the end. An existing class retains its first key
 * representative and position while only its value changes. */
d7_ordered_status d7_ordered_map_set(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    d7_ordered_map* result);

/* Moves the cursor before the first entry. */
d7_ordered_status d7_ordered_map_move_to_first(
    const d7_ordered_map* map,
    const void* equal_key,
    d7_ordered_map* result);
/* Moves the cursor after the last entry. */
d7_ordered_status d7_ordered_map_move_to_last(
    const d7_ordered_map* map,
    const void* equal_key,
    d7_ordered_map* result);
/* Moves the cursor to the given position within the same version. */
d7_ordered_status d7_ordered_map_move_to(
    const d7_ordered_map* map,
    size_t final_index,
    const void* equal_key,
    d7_ordered_map* result);

/* Produces a map without that entry. */
d7_ordered_status d7_ordered_map_remove(
    const d7_ordered_map* map,
    const void* equal_key,
    d7_ordered_map* result);
/* Removes the entry, reporting whether it was present. */
d7_ordered_status d7_ordered_map_try_remove(
    const d7_ordered_map* map,
    const void* equal_key,
    bool* removed,
    d7_ordered_map* result);
/* Produces a map without the entry at the position. */
d7_ordered_status d7_ordered_map_remove_at(
    const d7_ordered_map* map,
    size_t index,
    d7_ordered_map* result);
/* Produces a map without its first entry in insertion order. */
d7_ordered_status d7_ordered_map_remove_first(
    const d7_ordered_map* map,
    d7_ordered_map* result);
/* Produces a map without its last entry in insertion order. */
d7_ordered_status d7_ordered_map_remove_last(
    const d7_ordered_map* map,
    d7_ordered_map* result);
/* Produces an empty map retaining the same policies. */
d7_ordered_status d7_ordered_map_clear(
    const d7_ordered_map* map,
    d7_ordered_map* result);

/* Produces the entries in the range. */
d7_ordered_status d7_ordered_map_get_range(
    const d7_ordered_map* map,
    size_t index,
    size_t count,
    d7_ordered_map* result);
/* Produces the first n entries. */
d7_ordered_status d7_ordered_map_take(
    const d7_ordered_map* map,
    size_t count,
    d7_ordered_map* result);
/* Produces the entries after the first n. */
d7_ordered_status d7_ordered_map_drop(
    const d7_ordered_map* map,
    size_t count,
    d7_ordered_map* result);
/* Produces the map in the opposite order. */
d7_ordered_status d7_ordered_map_reverse(
    const d7_ordered_map* map,
    d7_ordered_map* result);
/* Stable one-shot sort; ties retain their previous explicit order. */
d7_ordered_status d7_ordered_map_sort(
    const d7_ordered_map* map,
    d7_ordered_map_compare_fn compare,
    void* compare_context,
    d7_ordered_map* result);

/* Calls the visitor once per entry, in the map's own order. */
d7_ordered_status d7_ordered_map_visit(
    const d7_ordered_map* map,
    d7_ordered_map_visit_fn visitor,
    void* context);
/* Checks the map's structural invariants. For tests and diagnostics. */
bool d7_ordered_map_debug_validate(const d7_ordered_map* map);
/* Whether both handles share their order index. */
bool d7_ordered_map_debug_shares_order(
    const d7_ordered_map* left,
    const d7_ordered_map* right);
/* Whether both handles share their value index. */
bool d7_ordered_map_debug_shares_values(
    const d7_ordered_map* left,
    const d7_ordered_map* right);

#ifdef __cplusplus
}
#endif

#endif
