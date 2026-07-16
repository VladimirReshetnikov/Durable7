#ifndef TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_MAP_H
#define TOOLS_DATA_STRUCTURES_ORDERED_C_ORDERED_MAP_H

#include <tools/data_structures/ordered/ordered_set.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*tds_ordered_map_value_equal_fn)(
    const void* left,
    const void* right,
    void* context);
typedef void (*tds_ordered_map_visit_fn)(
    const void* key,
    const void* value,
    void* context);
typedef int (*tds_ordered_map_compare_fn)(
    const void* left_key,
    const void* left_value,
    const void* right_key,
    const void* right_value,
    void* context);

/* Callback contexts, and the contexts embedded in both value types, must
 * remain usable until every map initialized from this policy is destroyed. */
typedef struct tds_ordered_map_policy {
    ft_value_type key_type;
    ft_value_type value_type;
    tds_ordered_hash_fn hash;
    tds_ordered_equal_fn key_equal;
    tds_ordered_map_value_equal_fn value_equal;
    void* context;
} tds_ordered_map_policy;

struct tds_ordered_map_context;

/* Persistent value handle. Clone rather than assigning, move to transfer
 * ownership, and destroy each initialized handle once. Result parameters must
 * be uninitialized and must not alias the source map. */
typedef struct tds_ordered_map {
    struct tds_ordered_map_context* context;
    tds_ordered_set keys;
    tds_hamt_map values;
} tds_ordered_map;

void tds_ordered_map_policy_init(
    tds_ordered_map_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    tds_ordered_hash_fn hash,
    tds_ordered_equal_fn key_equal,
    tds_ordered_map_value_equal_fn value_equal,
    void* context);

tds_ordered_status tds_ordered_map_init(
    tds_ordered_map* map,
    const tds_ordered_map_policy* policy);
tds_ordered_status tds_ordered_map_clone(
    const tds_ordered_map* source,
    tds_ordered_map* destination);
void tds_ordered_map_move(
    tds_ordered_map* destination,
    tds_ordered_map* source);
void tds_ordered_map_destroy(tds_ordered_map* map);

const tds_ordered_map_policy* tds_ordered_map_policy_of(
    const tds_ordered_map* map);
bool tds_ordered_map_empty(const tds_ordered_map* map);
size_t tds_ordered_map_size(const tds_ordered_map* map);
bool tds_ordered_map_contains_key(
    const tds_ordered_map* map,
    const void* key);
bool tds_ordered_map_try_get(
    const tds_ordered_map* map,
    const void* equal_key,
    const void** actual_key,
    const void** value);
tds_ordered_status tds_ordered_map_entry_at(
    const tds_ordered_map* map,
    size_t index,
    const void** key,
    const void** value);
tds_ordered_status tds_ordered_map_front(
    const tds_ordered_map* map,
    const void** key,
    const void** value);
tds_ordered_status tds_ordered_map_back(
    const tds_ordered_map* map,
    const void** key,
    const void** value);
bool tds_ordered_map_index_of_key(
    const tds_ordered_map* map,
    const void* equal_key,
    size_t* index);

tds_ordered_status tds_ordered_map_add(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_try_add(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    bool* added,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_add_first(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_insert(
    const tds_ordered_map* map,
    size_t index,
    const void* key,
    const void* value,
    tds_ordered_map* result);
/* Adds an absent key at the end. An existing class retains its first key
 * representative and position while only its value changes. */
tds_ordered_status tds_ordered_map_set(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    tds_ordered_map* result);

tds_ordered_status tds_ordered_map_move_to_first(
    const tds_ordered_map* map,
    const void* equal_key,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_move_to_last(
    const tds_ordered_map* map,
    const void* equal_key,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_move_to(
    const tds_ordered_map* map,
    size_t final_index,
    const void* equal_key,
    tds_ordered_map* result);

tds_ordered_status tds_ordered_map_remove(
    const tds_ordered_map* map,
    const void* equal_key,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_try_remove(
    const tds_ordered_map* map,
    const void* equal_key,
    bool* removed,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_remove_at(
    const tds_ordered_map* map,
    size_t index,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_remove_first(
    const tds_ordered_map* map,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_remove_last(
    const tds_ordered_map* map,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_clear(
    const tds_ordered_map* map,
    tds_ordered_map* result);

tds_ordered_status tds_ordered_map_get_range(
    const tds_ordered_map* map,
    size_t index,
    size_t count,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_take(
    const tds_ordered_map* map,
    size_t count,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_drop(
    const tds_ordered_map* map,
    size_t count,
    tds_ordered_map* result);
tds_ordered_status tds_ordered_map_reverse(
    const tds_ordered_map* map,
    tds_ordered_map* result);
/* Stable one-shot sort; ties retain their previous explicit order. */
tds_ordered_status tds_ordered_map_sort(
    const tds_ordered_map* map,
    tds_ordered_map_compare_fn compare,
    void* compare_context,
    tds_ordered_map* result);

tds_ordered_status tds_ordered_map_visit(
    const tds_ordered_map* map,
    tds_ordered_map_visit_fn visitor,
    void* context);
bool tds_ordered_map_debug_validate(const tds_ordered_map* map);
bool tds_ordered_map_debug_shares_order(
    const tds_ordered_map* left,
    const tds_ordered_map* right);
bool tds_ordered_map_debug_shares_values(
    const tds_ordered_map* left,
    const tds_ordered_map* right);

#ifdef __cplusplus
}
#endif

#endif
