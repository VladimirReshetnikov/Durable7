#ifndef TOOLS_DATA_STRUCTURES_WOLFRAM_C_WOLFRAM_H
#define TOOLS_DATA_STRUCTURES_WOLFRAM_C_WOLFRAM_H

#include <Tools/DataStructures/Hamt/hamt.h>
#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tds_wolfram_status {
    TDS_WOLFRAM_OK = 0,
    TDS_WOLFRAM_INVALID_ARGUMENT = 1,
    TDS_WOLFRAM_OUT_OF_RANGE = 2,
    TDS_WOLFRAM_EMPTY = 3,
    TDS_WOLFRAM_NOT_FOUND = 4,
    TDS_WOLFRAM_OUT_OF_MEMORY = 5,
    TDS_WOLFRAM_OVERFLOW = 6,
    TDS_WOLFRAM_DUPLICATE_KEY = 7
} tds_wolfram_status;

typedef uint32_t (*tds_wolfram_hash_fn)(const void* key, void* context);
typedef bool (*tds_wolfram_equal_fn)(const void* left, const void* right, void* context);
typedef void (*tds_wolfram_map_fn)(void* destination, const void* source, void* context);
typedef void (*tds_wolfram_visit_fn)(const void* value, void* context);
typedef void (*tds_wolfram_assoc_visit_fn)(const void* key, const void* value, void* context);

typedef struct tds_wolfram_association_policy {
    ft_value_type key_type;
    ft_value_type value_type;
    tds_wolfram_hash_fn hash_key;
    tds_wolfram_equal_fn key_equal;
    tds_wolfram_equal_fn value_equal;
    void* context;
} tds_wolfram_association_policy;

typedef struct tds_wolfram_assoc_pair {
    const void* key;
    const void* value;
} tds_wolfram_assoc_pair;

struct tds_wolfram_assoc_context;
struct tds_wolfram_assoc_node;

typedef struct tds_wolfram_list {
    ft_tree_policy policy;
    ft_persistent_deque items;
} tds_wolfram_list;

typedef struct tds_wolfram_association {
    struct tds_wolfram_assoc_context* context;
    struct tds_wolfram_assoc_node* root;
    tds_hamt_map index;
} tds_wolfram_association;

void tds_wolfram_association_policy_init(
    tds_wolfram_association_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    tds_wolfram_hash_fn hash_key,
    tds_wolfram_equal_fn key_equal,
    void* context);

tds_wolfram_status tds_wolfram_list_init(tds_wolfram_list* list, const ft_value_type* value_type);
tds_wolfram_status tds_wolfram_list_from_array(
    tds_wolfram_list* list,
    const ft_value_type* value_type,
    const void* values,
    size_t count);
tds_wolfram_status tds_wolfram_list_copy(const tds_wolfram_list* source, tds_wolfram_list* destination);
void tds_wolfram_list_move(tds_wolfram_list* destination, tds_wolfram_list* source);
void tds_wolfram_list_dispose(tds_wolfram_list* list);
bool tds_wolfram_list_empty(const tds_wolfram_list* list);
size_t tds_wolfram_list_size(const tds_wolfram_list* list);
tds_wolfram_status tds_wolfram_list_front(const tds_wolfram_list* list, void* destination);
tds_wolfram_status tds_wolfram_list_back(const tds_wolfram_list* list, void* destination);
tds_wolfram_status tds_wolfram_list_at(const tds_wolfram_list* list, size_t index, void* destination);
tds_wolfram_status tds_wolfram_list_push_front(
    const tds_wolfram_list* list,
    const void* value,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_push_back(
    const tds_wolfram_list* list,
    const void* value,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_concat(
    const tds_wolfram_list* left,
    const tds_wolfram_list* right,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_insert_at(
    const tds_wolfram_list* list,
    size_t index,
    const void* value,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_insert_range(
    const tds_wolfram_list* list,
    size_t index,
    const void* values,
    size_t count,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_remove_at(
    const tds_wolfram_list* list,
    size_t index,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_remove_range(
    const tds_wolfram_list* list,
    size_t index,
    size_t count,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_set_at(
    const tds_wolfram_list* list,
    size_t index,
    const void* value,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_slice(
    const tds_wolfram_list* list,
    size_t index,
    size_t count,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_take(const tds_wolfram_list* list, size_t count, tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_drop(const tds_wolfram_list* list, size_t count, tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_reverse(const tds_wolfram_list* list, tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_map(
    const tds_wolfram_list* list,
    const ft_value_type* result_value_type,
    tds_wolfram_map_fn map,
    void* map_context,
    tds_wolfram_list* result);
tds_wolfram_status tds_wolfram_list_visit(
    const tds_wolfram_list* list,
    tds_wolfram_visit_fn visitor,
    void* context);
bool tds_wolfram_list_index_of(
    const tds_wolfram_list* list,
    const void* value,
    tds_wolfram_equal_fn equal,
    void* context,
    size_t* index);
bool tds_wolfram_list_contains(
    const tds_wolfram_list* list,
    const void* value,
    tds_wolfram_equal_fn equal,
    void* context);

tds_wolfram_status tds_wolfram_association_init(
    tds_wolfram_association* association,
    const tds_wolfram_association_policy* policy);
tds_wolfram_status tds_wolfram_association_from_pairs(
    tds_wolfram_association* association,
    const tds_wolfram_association_policy* policy,
    const tds_wolfram_assoc_pair* pairs,
    size_t count);
tds_wolfram_status tds_wolfram_association_copy(
    const tds_wolfram_association* source,
    tds_wolfram_association* destination);
void tds_wolfram_association_move(tds_wolfram_association* destination, tds_wolfram_association* source);
void tds_wolfram_association_dispose(tds_wolfram_association* association);
bool tds_wolfram_association_empty(const tds_wolfram_association* association);
size_t tds_wolfram_association_size(const tds_wolfram_association* association);
bool tds_wolfram_association_contains_key(const tds_wolfram_association* association, const void* key);
bool tds_wolfram_association_try_get(
    const tds_wolfram_association* association,
    const void* key,
    void* value);
bool tds_wolfram_association_try_get_key(
    const tds_wolfram_association* association,
    const void* equal_key,
    void* actual_key);
tds_wolfram_status tds_wolfram_association_front(
    const tds_wolfram_association* association,
    void* key,
    void* value);
tds_wolfram_status tds_wolfram_association_back(
    const tds_wolfram_association* association,
    void* key,
    void* value);
tds_wolfram_status tds_wolfram_association_entry_at(
    const tds_wolfram_association* association,
    size_t index,
    void* key,
    void* value);
bool tds_wolfram_association_index_of_key(
    const tds_wolfram_association* association,
    const void* key,
    size_t* index);
tds_wolfram_status tds_wolfram_association_set_item(
    const tds_wolfram_association* association,
    const void* key,
    const void* value,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_set_items(
    const tds_wolfram_association* association,
    const tds_wolfram_assoc_pair* pairs,
    size_t count,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_join(
    const tds_wolfram_association* left,
    const tds_wolfram_association* right,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_append(
    const tds_wolfram_association* association,
    const void* key,
    const void* value,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_prepend(
    const tds_wolfram_association* association,
    const void* key,
    const void* value,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_insert_at(
    const tds_wolfram_association* association,
    size_t index,
    const void* key,
    const void* value,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_remove(
    const tds_wolfram_association* association,
    const void* key,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_try_remove(
    const tds_wolfram_association* association,
    const void* key,
    bool* removed,
    void* value,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_remove_keys(
    const tds_wolfram_association* association,
    const void* const* keys,
    size_t count,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_key_take(
    const tds_wolfram_association* association,
    const void* const* keys,
    size_t count,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_remove_at(
    const tds_wolfram_association* association,
    size_t index,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_slice(
    const tds_wolfram_association* association,
    size_t index,
    size_t count,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_take(
    const tds_wolfram_association* association,
    size_t count,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_drop(
    const tds_wolfram_association* association,
    size_t count,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_reverse(
    const tds_wolfram_association* association,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_key_sort(
    const tds_wolfram_association* association,
    ft_compare_fn compare_key,
    void* compare_context,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_sort(
    const tds_wolfram_association* association,
    ft_compare_fn compare_value,
    void* compare_context,
    tds_wolfram_association* result);
tds_wolfram_status tds_wolfram_association_visit(
    const tds_wolfram_association* association,
    tds_wolfram_assoc_visit_fn visitor,
    void* context);

#ifdef __cplusplus
}
#endif

#endif
