#ifndef DURABLE7_TUNGSTEN_C_TUNGSTEN_H
#define DURABLE7_TUNGSTEN_C_TUNGSTEN_H

#include <durable7/hamt/hamt.h>
#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum d7_tungsten_status {
    D7_TUNGSTEN_OK = 0,
    D7_TUNGSTEN_INVALID_ARGUMENT = 1,
    D7_TUNGSTEN_OUT_OF_RANGE = 2,
    D7_TUNGSTEN_EMPTY = 3,
    D7_TUNGSTEN_NOT_FOUND = 4,
    D7_TUNGSTEN_OUT_OF_MEMORY = 5,
    D7_TUNGSTEN_OVERFLOW = 6,
    D7_TUNGSTEN_DUPLICATE_KEY = 7
} d7_tungsten_status;

typedef uint32_t (*d7_tungsten_hash_fn)(const void* key, void* context);
typedef bool (*d7_tungsten_equal_fn)(const void* left, const void* right, void* context);
/* Constructs the mapped value for `source` into `destination` (uninitialized
 * storage of the result value type's size). The list copies the constructed
 * value with the result type's `copy` and then destroys the original with the
 * result type's `destroy`, so owning result types release per-element
 * resources exactly once. */
typedef void (*d7_tungsten_map_fn)(void* destination, const void* source, void* context);
typedef void (*d7_tungsten_visit_fn)(const void* value, void* context);
typedef void (*d7_tungsten_assoc_visit_fn)(const void* key, const void* value, void* context);

typedef struct d7_tungsten_association_policy {
    ft_value_type key_type;
    ft_value_type value_type;
    d7_tungsten_hash_fn hash_key;
    d7_tungsten_equal_fn key_equal;
    d7_tungsten_equal_fn value_equal;
    void* context;
} d7_tungsten_association_policy;

typedef struct d7_tungsten_assoc_pair {
    const void* key;
    const void* value;
} d7_tungsten_assoc_pair;

struct d7_tungsten_assoc_context;
struct d7_tungsten_assoc_node;

typedef struct d7_tungsten_list {
    ft_tree_policy policy;
    ft_persistent_deque items;
} d7_tungsten_list;

typedef struct d7_tungsten_association {
    struct d7_tungsten_assoc_context* context;
    struct d7_tungsten_assoc_node* root;
    d7_hamt_map index;
} d7_tungsten_association;

void d7_tungsten_association_policy_init(
    d7_tungsten_association_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    d7_tungsten_hash_fn hash_key,
    d7_tungsten_equal_fn key_equal,
    void* context);

d7_tungsten_status d7_tungsten_list_init(d7_tungsten_list* list, const ft_value_type* value_type);
d7_tungsten_status d7_tungsten_list_from_array(
    d7_tungsten_list* list,
    const ft_value_type* value_type,
    const void* values,
    size_t count);
d7_tungsten_status d7_tungsten_list_copy(const d7_tungsten_list* source, d7_tungsten_list* destination);
void d7_tungsten_list_move(d7_tungsten_list* destination, d7_tungsten_list* source);
void d7_tungsten_list_dispose(d7_tungsten_list* list);
bool d7_tungsten_list_empty(const d7_tungsten_list* list);
size_t d7_tungsten_list_size(const d7_tungsten_list* list);
d7_tungsten_status d7_tungsten_list_front(const d7_tungsten_list* list, void* destination);
d7_tungsten_status d7_tungsten_list_back(const d7_tungsten_list* list, void* destination);
d7_tungsten_status d7_tungsten_list_at(const d7_tungsten_list* list, size_t index, void* destination);
d7_tungsten_status d7_tungsten_list_push_front(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_push_back(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_concat(
    const d7_tungsten_list* left,
    const d7_tungsten_list* right,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_insert_at(
    const d7_tungsten_list* list,
    size_t index,
    const void* value,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_insert_range(
    const d7_tungsten_list* list,
    size_t index,
    const void* values,
    size_t count,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_remove_at(
    const d7_tungsten_list* list,
    size_t index,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_remove_range(
    const d7_tungsten_list* list,
    size_t index,
    size_t count,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_set_at(
    const d7_tungsten_list* list,
    size_t index,
    const void* value,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_slice(
    const d7_tungsten_list* list,
    size_t index,
    size_t count,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_take(const d7_tungsten_list* list, size_t count, d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_drop(const d7_tungsten_list* list, size_t count, d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_reverse(const d7_tungsten_list* list, d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_map(
    const d7_tungsten_list* list,
    const ft_value_type* result_value_type,
    d7_tungsten_map_fn map,
    void* map_context,
    d7_tungsten_list* result);
d7_tungsten_status d7_tungsten_list_visit(
    const d7_tungsten_list* list,
    d7_tungsten_visit_fn visitor,
    void* context);
bool d7_tungsten_list_index_of(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_equal_fn equal,
    void* context,
    size_t* index);
bool d7_tungsten_list_contains(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_equal_fn equal,
    void* context);

d7_tungsten_status d7_tungsten_association_init(
    d7_tungsten_association* association,
    const d7_tungsten_association_policy* policy);
d7_tungsten_status d7_tungsten_association_from_pairs(
    d7_tungsten_association* association,
    const d7_tungsten_association_policy* policy,
    const d7_tungsten_assoc_pair* pairs,
    size_t count);
d7_tungsten_status d7_tungsten_association_copy(
    const d7_tungsten_association* source,
    d7_tungsten_association* destination);
void d7_tungsten_association_move(d7_tungsten_association* destination, d7_tungsten_association* source);
void d7_tungsten_association_dispose(d7_tungsten_association* association);
bool d7_tungsten_association_empty(const d7_tungsten_association* association);
size_t d7_tungsten_association_size(const d7_tungsten_association* association);
bool d7_tungsten_association_contains_key(const d7_tungsten_association* association, const void* key);
bool d7_tungsten_association_try_get(
    const d7_tungsten_association* association,
    const void* key,
    void* value);
bool d7_tungsten_association_try_get_key(
    const d7_tungsten_association* association,
    const void* equal_key,
    void* actual_key);
d7_tungsten_status d7_tungsten_association_front(
    const d7_tungsten_association* association,
    void* key,
    void* value);
d7_tungsten_status d7_tungsten_association_back(
    const d7_tungsten_association* association,
    void* key,
    void* value);
d7_tungsten_status d7_tungsten_association_entry_at(
    const d7_tungsten_association* association,
    size_t index,
    void* key,
    void* value);
bool d7_tungsten_association_index_of_key(
    const d7_tungsten_association* association,
    const void* key,
    size_t* index);
d7_tungsten_status d7_tungsten_association_set_item(
    const d7_tungsten_association* association,
    const void* key,
    const void* value,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_set_items(
    const d7_tungsten_association* association,
    const d7_tungsten_assoc_pair* pairs,
    size_t count,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_join(
    const d7_tungsten_association* left,
    const d7_tungsten_association* right,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_append(
    const d7_tungsten_association* association,
    const void* key,
    const void* value,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_prepend(
    const d7_tungsten_association* association,
    const void* key,
    const void* value,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_insert_at(
    const d7_tungsten_association* association,
    size_t index,
    const void* key,
    const void* value,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_remove(
    const d7_tungsten_association* association,
    const void* key,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_try_remove(
    const d7_tungsten_association* association,
    const void* key,
    bool* removed,
    void* value,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_remove_keys(
    const d7_tungsten_association* association,
    const void* const* keys,
    size_t count,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_key_take(
    const d7_tungsten_association* association,
    const void* const* keys,
    size_t count,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_remove_at(
    const d7_tungsten_association* association,
    size_t index,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_slice(
    const d7_tungsten_association* association,
    size_t index,
    size_t count,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_take(
    const d7_tungsten_association* association,
    size_t count,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_drop(
    const d7_tungsten_association* association,
    size_t count,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_reverse(
    const d7_tungsten_association* association,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_key_sort(
    const d7_tungsten_association* association,
    ft_compare_fn compare_key,
    void* compare_context,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_sort(
    const d7_tungsten_association* association,
    ft_compare_fn compare_value,
    void* compare_context,
    d7_tungsten_association* result);
d7_tungsten_status d7_tungsten_association_visit(
    const d7_tungsten_association* association,
    d7_tungsten_assoc_visit_fn visitor,
    void* context);

#ifdef __cplusplus
}
#endif

#endif
