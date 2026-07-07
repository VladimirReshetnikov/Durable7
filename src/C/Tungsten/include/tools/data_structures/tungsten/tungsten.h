#ifndef TOOLS_DATA_STRUCTURES_TUNGSTEN_C_TUNGSTEN_H
#define TOOLS_DATA_STRUCTURES_TUNGSTEN_C_TUNGSTEN_H

#include <Tools/DataStructures/Hamt/hamt.h>
#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tds_tungsten_status {
    TDS_TUNGSTEN_OK = 0,
    TDS_TUNGSTEN_INVALID_ARGUMENT = 1,
    TDS_TUNGSTEN_OUT_OF_RANGE = 2,
    TDS_TUNGSTEN_EMPTY = 3,
    TDS_TUNGSTEN_NOT_FOUND = 4,
    TDS_TUNGSTEN_OUT_OF_MEMORY = 5,
    TDS_TUNGSTEN_OVERFLOW = 6,
    TDS_TUNGSTEN_DUPLICATE_KEY = 7
} tds_tungsten_status;

typedef uint32_t (*tds_tungsten_hash_fn)(const void* key, void* context);
typedef bool (*tds_tungsten_equal_fn)(const void* left, const void* right, void* context);
typedef void (*tds_tungsten_map_fn)(void* destination, const void* source, void* context);
typedef void (*tds_tungsten_visit_fn)(const void* value, void* context);
typedef void (*tds_tungsten_assoc_visit_fn)(const void* key, const void* value, void* context);

typedef struct tds_tungsten_association_policy {
    ft_value_type key_type;
    ft_value_type value_type;
    tds_tungsten_hash_fn hash_key;
    tds_tungsten_equal_fn key_equal;
    tds_tungsten_equal_fn value_equal;
    void* context;
} tds_tungsten_association_policy;

typedef struct tds_tungsten_assoc_pair {
    const void* key;
    const void* value;
} tds_tungsten_assoc_pair;

struct tds_tungsten_assoc_context;
struct tds_tungsten_assoc_node;

typedef struct tds_tungsten_list {
    ft_tree_policy policy;
    ft_persistent_deque items;
} tds_tungsten_list;

typedef struct tds_tungsten_association {
    struct tds_tungsten_assoc_context* context;
    struct tds_tungsten_assoc_node* root;
    tds_hamt_map index;
} tds_tungsten_association;

void tds_tungsten_association_policy_init(
    tds_tungsten_association_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    tds_tungsten_hash_fn hash_key,
    tds_tungsten_equal_fn key_equal,
    void* context);

tds_tungsten_status tds_tungsten_list_init(tds_tungsten_list* list, const ft_value_type* value_type);
tds_tungsten_status tds_tungsten_list_from_array(
    tds_tungsten_list* list,
    const ft_value_type* value_type,
    const void* values,
    size_t count);
tds_tungsten_status tds_tungsten_list_copy(const tds_tungsten_list* source, tds_tungsten_list* destination);
void tds_tungsten_list_move(tds_tungsten_list* destination, tds_tungsten_list* source);
void tds_tungsten_list_dispose(tds_tungsten_list* list);
bool tds_tungsten_list_empty(const tds_tungsten_list* list);
size_t tds_tungsten_list_size(const tds_tungsten_list* list);
tds_tungsten_status tds_tungsten_list_front(const tds_tungsten_list* list, void* destination);
tds_tungsten_status tds_tungsten_list_back(const tds_tungsten_list* list, void* destination);
tds_tungsten_status tds_tungsten_list_at(const tds_tungsten_list* list, size_t index, void* destination);
tds_tungsten_status tds_tungsten_list_push_front(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_push_back(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_concat(
    const tds_tungsten_list* left,
    const tds_tungsten_list* right,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_insert_at(
    const tds_tungsten_list* list,
    size_t index,
    const void* value,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_insert_range(
    const tds_tungsten_list* list,
    size_t index,
    const void* values,
    size_t count,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_remove_at(
    const tds_tungsten_list* list,
    size_t index,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_remove_range(
    const tds_tungsten_list* list,
    size_t index,
    size_t count,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_set_at(
    const tds_tungsten_list* list,
    size_t index,
    const void* value,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_slice(
    const tds_tungsten_list* list,
    size_t index,
    size_t count,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_take(const tds_tungsten_list* list, size_t count, tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_drop(const tds_tungsten_list* list, size_t count, tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_reverse(const tds_tungsten_list* list, tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_map(
    const tds_tungsten_list* list,
    const ft_value_type* result_value_type,
    tds_tungsten_map_fn map,
    void* map_context,
    tds_tungsten_list* result);
tds_tungsten_status tds_tungsten_list_visit(
    const tds_tungsten_list* list,
    tds_tungsten_visit_fn visitor,
    void* context);
bool tds_tungsten_list_index_of(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_equal_fn equal,
    void* context,
    size_t* index);
bool tds_tungsten_list_contains(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_equal_fn equal,
    void* context);

tds_tungsten_status tds_tungsten_association_init(
    tds_tungsten_association* association,
    const tds_tungsten_association_policy* policy);
tds_tungsten_status tds_tungsten_association_from_pairs(
    tds_tungsten_association* association,
    const tds_tungsten_association_policy* policy,
    const tds_tungsten_assoc_pair* pairs,
    size_t count);
tds_tungsten_status tds_tungsten_association_copy(
    const tds_tungsten_association* source,
    tds_tungsten_association* destination);
void tds_tungsten_association_move(tds_tungsten_association* destination, tds_tungsten_association* source);
void tds_tungsten_association_dispose(tds_tungsten_association* association);
bool tds_tungsten_association_empty(const tds_tungsten_association* association);
size_t tds_tungsten_association_size(const tds_tungsten_association* association);
bool tds_tungsten_association_contains_key(const tds_tungsten_association* association, const void* key);
bool tds_tungsten_association_try_get(
    const tds_tungsten_association* association,
    const void* key,
    void* value);
bool tds_tungsten_association_try_get_key(
    const tds_tungsten_association* association,
    const void* equal_key,
    void* actual_key);
tds_tungsten_status tds_tungsten_association_front(
    const tds_tungsten_association* association,
    void* key,
    void* value);
tds_tungsten_status tds_tungsten_association_back(
    const tds_tungsten_association* association,
    void* key,
    void* value);
tds_tungsten_status tds_tungsten_association_entry_at(
    const tds_tungsten_association* association,
    size_t index,
    void* key,
    void* value);
bool tds_tungsten_association_index_of_key(
    const tds_tungsten_association* association,
    const void* key,
    size_t* index);
tds_tungsten_status tds_tungsten_association_set_item(
    const tds_tungsten_association* association,
    const void* key,
    const void* value,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_set_items(
    const tds_tungsten_association* association,
    const tds_tungsten_assoc_pair* pairs,
    size_t count,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_join(
    const tds_tungsten_association* left,
    const tds_tungsten_association* right,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_append(
    const tds_tungsten_association* association,
    const void* key,
    const void* value,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_prepend(
    const tds_tungsten_association* association,
    const void* key,
    const void* value,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_insert_at(
    const tds_tungsten_association* association,
    size_t index,
    const void* key,
    const void* value,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_remove(
    const tds_tungsten_association* association,
    const void* key,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_try_remove(
    const tds_tungsten_association* association,
    const void* key,
    bool* removed,
    void* value,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_remove_keys(
    const tds_tungsten_association* association,
    const void* const* keys,
    size_t count,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_key_take(
    const tds_tungsten_association* association,
    const void* const* keys,
    size_t count,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_remove_at(
    const tds_tungsten_association* association,
    size_t index,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_slice(
    const tds_tungsten_association* association,
    size_t index,
    size_t count,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_take(
    const tds_tungsten_association* association,
    size_t count,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_drop(
    const tds_tungsten_association* association,
    size_t count,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_reverse(
    const tds_tungsten_association* association,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_key_sort(
    const tds_tungsten_association* association,
    ft_compare_fn compare_key,
    void* compare_context,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_sort(
    const tds_tungsten_association* association,
    ft_compare_fn compare_value,
    void* compare_context,
    tds_tungsten_association* result);
tds_tungsten_status tds_tungsten_association_visit(
    const tds_tungsten_association* association,
    tds_tungsten_assoc_visit_fn visitor,
    void* context);

#ifdef __cplusplus
}
#endif

#endif
