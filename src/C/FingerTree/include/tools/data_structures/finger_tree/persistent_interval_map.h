#ifndef TOOLS_DATA_STRUCTURES_FINGER_TREE_PERSISTENT_INTERVAL_MAP_H
#define TOOLS_DATA_STRUCTURES_FINGER_TREE_PERSISTENT_INTERVAL_MAP_H

#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ft_interval_map_value_equal_fn)(
    const void* left,
    const void* right,
    void* context);
typedef void (*ft_interval_map_visit_fn)(
    const void* low,
    const void* high,
    const void* value,
    void* context);

struct ft_interval_map_context;

/* Unique payload-bearing closed intervals. The interval tree provides pruned
 * overlap navigation; the sorted map provides complete (low, high) keys and
 * payload lookup. Copy/move/dispose handles explicitly. */
typedef struct ft_persistent_interval_map {
    ft_interval_tree intervals;
    ft_sorted_map values;
    struct ft_interval_map_context* context;
} ft_persistent_interval_map;

ft_status ft_persistent_interval_map_init(
    ft_persistent_interval_map* map,
    const ft_value_type* endpoint_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_endpoint,
    void* compare_context,
    ft_interval_map_value_equal_fn value_equal,
    void* value_equal_context);
ft_status ft_persistent_interval_map_copy(
    const ft_persistent_interval_map* source,
    ft_persistent_interval_map* destination);
void ft_persistent_interval_map_move(
    ft_persistent_interval_map* destination,
    ft_persistent_interval_map* source);
void ft_persistent_interval_map_dispose(ft_persistent_interval_map* map);

bool ft_persistent_interval_map_empty(const ft_persistent_interval_map* map);
size_t ft_persistent_interval_map_size(const ft_persistent_interval_map* map);
bool ft_persistent_interval_map_contains_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high);
ft_status ft_persistent_interval_map_try_get(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    void* value);
ft_status ft_persistent_interval_map_entry_at(
    const ft_persistent_interval_map* map,
    size_t index,
    void* low,
    void* high,
    void* value);

ft_status ft_persistent_interval_map_add(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result);
ft_status ft_persistent_interval_map_set(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result);
ft_status ft_persistent_interval_map_remove(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map* result);
ft_status ft_persistent_interval_map_clear(
    const ft_persistent_interval_map* map,
    ft_persistent_interval_map* result);

ft_status ft_persistent_interval_map_try_find_overlap(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high,
    void* value);
size_t ft_persistent_interval_map_count_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high);
ft_status ft_persistent_interval_map_visit(
    const ft_persistent_interval_map* map,
    ft_interval_map_visit_fn visitor,
    void* context);
ft_status ft_persistent_interval_map_visit_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    ft_interval_map_visit_fn visitor,
    void* context);

bool ft_persistent_interval_map_debug_validate(
    const ft_persistent_interval_map* map);

#ifdef __cplusplus
}
#endif

#endif
