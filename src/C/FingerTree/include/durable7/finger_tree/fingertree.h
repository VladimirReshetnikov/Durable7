#ifndef DURABLE7_FINGER_TREE_C_FINGERTREE_H
#define DURABLE7_FINGER_TREE_C_FINGERTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ft_status {
    FT_STATUS_OK = 0,
    FT_STATUS_INVALID_ARGUMENT = 1,
    FT_STATUS_OUT_OF_RANGE = 2,
    FT_STATUS_EMPTY = 3,
    FT_STATUS_NOT_FOUND = 4,
    FT_STATUS_NO_MEMORY = 5,
    FT_STATUS_OVERFLOW = 6,
    FT_STATUS_ALREADY_EXISTS = 7,
    FT_STATUS_CALLBACK_FAILURE = 8,
    FT_STATUS_CRYPTO_FAILURE = 9,
    FT_STATUS_INCOMPATIBLE_POLICY = 10,
    FT_STATUS_INCONSISTENT_POLICY = 11
} ft_status;

typedef void (*ft_copy_fn)(void* destination, const void* source, void* context);
typedef void (*ft_destroy_fn)(void* value, void* context);
typedef int (*ft_compare_fn)(const void* left, const void* right, void* context);

typedef struct ft_value_type {
    size_t size;
    ft_copy_fn copy;
    ft_destroy_fn destroy;
    void* context;
} ft_value_type;

typedef struct ft_measure_policy {
    size_t size;
    void (*identity)(void* destination, void* context);
    void (*measure)(void* destination, const void* value, void* context);
    void (*combine)(void* destination, const void* left, const void* right, void* context);
    void* context;
} ft_measure_policy;

typedef struct ft_tree_policy {
    ft_value_type value;
    ft_measure_policy measure;
} ft_tree_policy;

typedef struct ft_tree_rep ft_tree_rep;

typedef struct ft_tree {
    const ft_tree_policy* policy;
    ft_tree_rep* rep;
} ft_tree;

typedef bool (*ft_measure_predicate_fn)(const void* measure, void* context);
typedef void (*ft_visit_fn)(const void* value, void* context);

typedef struct ft_tree_split_result {
    ft_tree left;
    ft_tree right;
} ft_tree_split_result;

void ft_value_type_init(ft_value_type* type, size_t size);
void ft_size_measure_policy_init(ft_measure_policy* policy);
void ft_tree_policy_init_size(ft_tree_policy* policy, const ft_value_type* value_type);

ft_status ft_tree_init(ft_tree* tree, const ft_tree_policy* policy);
ft_status ft_tree_copy(const ft_tree* source, ft_tree* destination);
void ft_tree_dispose(ft_tree* tree);

bool ft_tree_empty(const ft_tree* tree);
size_t ft_tree_size(const ft_tree* tree);
ft_status ft_tree_measure(const ft_tree* tree, void* destination);

ft_status ft_tree_front(const ft_tree* tree, void* destination);
ft_status ft_tree_back(const ft_tree* tree, void* destination);
ft_status ft_tree_at(const ft_tree* tree, size_t index, void* destination);

ft_status ft_tree_push_front(const ft_tree* tree, const void* value, ft_tree* result);
ft_status ft_tree_push_back(const ft_tree* tree, const void* value, ft_tree* result);
ft_status ft_tree_pop_front(const ft_tree* tree, void* value, ft_tree* rest);
ft_status ft_tree_pop_back(const ft_tree* tree, void* value, ft_tree* rest);
ft_status ft_tree_concat(const ft_tree* left, const ft_tree* right, ft_tree* result);
ft_status ft_tree_split_at(const ft_tree* tree, size_t index, ft_tree_split_result* result);
ft_status ft_tree_set_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result);
ft_status ft_tree_insert_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result);
ft_status ft_tree_remove_at(const ft_tree* tree, size_t index, ft_tree* result);

ft_status ft_tree_locate(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    void* measure_before,
    void* value);

ft_status ft_tree_split(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree* left,
    void* value,
    ft_tree* right);

ft_status ft_tree_visit(const ft_tree* tree, ft_visit_fn visitor, void* context);

/* Immutable measure-aware gap cursor over one exact general tree snapshot.
 * The position field is representation state; general-tree callers navigate by
 * ordered measures and neighbors rather than treating it as a count contract. */
typedef struct ft_tree_cursor {
    ft_tree tree;
    size_t position;
} ft_tree_cursor;

ft_status ft_tree_get_cursor_at_start(const ft_tree* tree, ft_tree_cursor* result);
ft_status ft_tree_get_cursor_at_end(const ft_tree* tree, ft_tree_cursor* result);
ft_status ft_tree_get_cursor_by_measure(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree_cursor* result);
ft_status ft_tree_cursor_copy(const ft_tree_cursor* source, ft_tree_cursor* destination);
void ft_tree_cursor_move(ft_tree_cursor* destination, ft_tree_cursor* source);
void ft_tree_cursor_dispose(ft_tree_cursor* cursor);
bool ft_tree_cursor_valid(const ft_tree_cursor* cursor);
ft_status ft_tree_cursor_is_at_start(const ft_tree_cursor* cursor, bool* result);
ft_status ft_tree_cursor_is_at_end(const ft_tree_cursor* cursor, bool* result);
ft_status ft_tree_cursor_measure_before(const ft_tree_cursor* cursor, void* destination);
ft_status ft_tree_cursor_measure_after(const ft_tree_cursor* cursor, void* destination);
ft_status ft_tree_cursor_try_peek_previous(
    const ft_tree_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_tree_cursor_try_peek_next(
    const ft_tree_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_tree_cursor_move_previous(const ft_tree_cursor* cursor, ft_tree_cursor* result);
ft_status ft_tree_cursor_move_next(const ft_tree_cursor* cursor, ft_tree_cursor* result);
ft_status ft_tree_cursor_seek_by_measure(
    const ft_tree_cursor* cursor,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree_cursor* result);
ft_status ft_tree_cursor_insert(
    const ft_tree_cursor* cursor,
    const void* value,
    ft_tree_cursor* result);
ft_status ft_tree_cursor_delete_previous(const ft_tree_cursor* cursor, ft_tree_cursor* result);
ft_status ft_tree_cursor_delete_next(const ft_tree_cursor* cursor, ft_tree_cursor* result);
ft_status ft_tree_cursor_replace_next(
    const ft_tree_cursor* cursor,
    const void* value,
    ft_tree_cursor* result);
ft_status ft_tree_cursor_snapshot(const ft_tree_cursor* cursor, ft_tree* result);

typedef ft_tree ft_persistent_deque;
typedef ft_tree_cursor ft_persistent_deque_cursor;

#define ft_persistent_deque_init ft_tree_init
#define ft_persistent_deque_copy ft_tree_copy
#define ft_persistent_deque_dispose ft_tree_dispose
#define ft_persistent_deque_empty ft_tree_empty
#define ft_persistent_deque_size ft_tree_size
#define ft_persistent_deque_front ft_tree_front
#define ft_persistent_deque_back ft_tree_back
#define ft_persistent_deque_at ft_tree_at
#define ft_persistent_deque_push_front ft_tree_push_front
#define ft_persistent_deque_push_back ft_tree_push_back
#define ft_persistent_deque_pop_front ft_tree_pop_front
#define ft_persistent_deque_pop_back ft_tree_pop_back
#define ft_persistent_deque_concat ft_tree_concat
#define ft_persistent_deque_split_at ft_tree_split_at
#define ft_persistent_deque_set_at ft_tree_set_at
#define ft_persistent_deque_insert_at ft_tree_insert_at
#define ft_persistent_deque_remove_at ft_tree_remove_at
#define ft_persistent_deque_visit ft_tree_visit

ft_status ft_persistent_deque_get_cursor(
    const ft_persistent_deque* deque,
    size_t position,
    ft_persistent_deque_cursor* result);
#define ft_persistent_deque_cursor_copy ft_tree_cursor_copy
#define ft_persistent_deque_cursor_move ft_tree_cursor_move
#define ft_persistent_deque_cursor_dispose ft_tree_cursor_dispose
#define ft_persistent_deque_cursor_valid ft_tree_cursor_valid
#define ft_persistent_deque_cursor_is_at_start ft_tree_cursor_is_at_start
#define ft_persistent_deque_cursor_is_at_end ft_tree_cursor_is_at_end
#define ft_persistent_deque_cursor_try_peek_previous ft_tree_cursor_try_peek_previous
#define ft_persistent_deque_cursor_try_peek_next ft_tree_cursor_try_peek_next
#define ft_persistent_deque_cursor_move_previous ft_tree_cursor_move_previous
#define ft_persistent_deque_cursor_move_next ft_tree_cursor_move_next
#define ft_persistent_deque_cursor_insert ft_tree_cursor_insert
#define ft_persistent_deque_cursor_delete_previous ft_tree_cursor_delete_previous
#define ft_persistent_deque_cursor_delete_next ft_tree_cursor_delete_next
#define ft_persistent_deque_cursor_replace_next ft_tree_cursor_replace_next
#define ft_persistent_deque_cursor_snapshot ft_tree_cursor_snapshot
bool ft_persistent_deque_cursor_empty(const ft_persistent_deque_cursor* cursor);
size_t ft_persistent_deque_cursor_size(const ft_persistent_deque_cursor* cursor);
size_t ft_persistent_deque_cursor_position(const ft_persistent_deque_cursor* cursor);
ft_status ft_persistent_deque_cursor_seek(
    const ft_persistent_deque_cursor* cursor,
    size_t position,
    ft_persistent_deque_cursor* result);
ft_status ft_persistent_deque_cursor_insert_array(
    const ft_persistent_deque_cursor* cursor,
    const void* values,
    size_t count,
    ft_persistent_deque_cursor* result);
ft_status ft_persistent_deque_cursor_insert_deque(
    const ft_persistent_deque_cursor* cursor,
    const ft_persistent_deque* values,
    ft_persistent_deque_cursor* result);

typedef struct ft_reversible_deque_rep ft_reversible_deque_rep;

typedef struct ft_reversible_deque {
    const ft_tree_policy* policy;
    ft_reversible_deque_rep* rep;
} ft_reversible_deque;

typedef struct ft_reversible_deque_split_result {
    ft_reversible_deque left;
    ft_reversible_deque right;
} ft_reversible_deque_split_result;

typedef struct ft_reversible_deque_cursor {
    ft_reversible_deque deque;
    size_t position;
} ft_reversible_deque_cursor;

ft_status ft_reversible_deque_init(ft_reversible_deque* deque, const ft_tree_policy* policy);
ft_status ft_reversible_deque_copy(const ft_reversible_deque* source, ft_reversible_deque* destination);
void ft_reversible_deque_dispose(ft_reversible_deque* deque);
bool ft_reversible_deque_empty(const ft_reversible_deque* deque);
size_t ft_reversible_deque_size(const ft_reversible_deque* deque);
ft_status ft_reversible_deque_reverse(const ft_reversible_deque* deque, ft_reversible_deque* result);
ft_status ft_reversible_deque_at(const ft_reversible_deque* deque, size_t index, void* destination);
ft_status ft_reversible_deque_front(const ft_reversible_deque* deque, void* destination);
ft_status ft_reversible_deque_back(const ft_reversible_deque* deque, void* destination);
ft_status ft_reversible_deque_push_front(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result);
ft_status ft_reversible_deque_push_back(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result);
ft_status ft_reversible_deque_pop_front(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest);
ft_status ft_reversible_deque_pop_back(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest);
ft_status ft_reversible_deque_concat(
    const ft_reversible_deque* left,
    const ft_reversible_deque* right,
    ft_reversible_deque* result);
ft_status ft_reversible_deque_split_at(
    const ft_reversible_deque* deque,
    size_t index,
    ft_reversible_deque_split_result* result);
ft_status ft_reversible_deque_set_at(
    const ft_reversible_deque* deque,
    size_t index,
    const void* value,
    ft_reversible_deque* result);
ft_status ft_reversible_deque_insert_at(
    const ft_reversible_deque* deque,
    size_t index,
    const void* value,
    ft_reversible_deque* result);
ft_status ft_reversible_deque_remove_at(const ft_reversible_deque* deque, size_t index, ft_reversible_deque* result);
ft_status ft_reversible_deque_visit(const ft_reversible_deque* deque, ft_visit_fn visitor, void* context);

ft_status ft_reversible_deque_get_cursor(
    const ft_reversible_deque* deque,
    size_t position,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_copy(
    const ft_reversible_deque_cursor* source,
    ft_reversible_deque_cursor* destination);
void ft_reversible_deque_cursor_move(
    ft_reversible_deque_cursor* destination,
    ft_reversible_deque_cursor* source);
void ft_reversible_deque_cursor_dispose(ft_reversible_deque_cursor* cursor);
bool ft_reversible_deque_cursor_valid(const ft_reversible_deque_cursor* cursor);
bool ft_reversible_deque_cursor_empty(const ft_reversible_deque_cursor* cursor);
size_t ft_reversible_deque_cursor_size(const ft_reversible_deque_cursor* cursor);
size_t ft_reversible_deque_cursor_position(const ft_reversible_deque_cursor* cursor);
ft_status ft_reversible_deque_cursor_is_at_start(
    const ft_reversible_deque_cursor* cursor,
    bool* result);
ft_status ft_reversible_deque_cursor_is_at_end(
    const ft_reversible_deque_cursor* cursor,
    bool* result);
ft_status ft_reversible_deque_cursor_try_peek_previous(
    const ft_reversible_deque_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_reversible_deque_cursor_try_peek_next(
    const ft_reversible_deque_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_reversible_deque_cursor_move_previous(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_move_next(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_seek(
    const ft_reversible_deque_cursor* cursor,
    size_t position,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_insert(
    const ft_reversible_deque_cursor* cursor,
    const void* value,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_insert_array(
    const ft_reversible_deque_cursor* cursor,
    const void* values,
    size_t count,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_insert_deque(
    const ft_reversible_deque_cursor* cursor,
    const ft_reversible_deque* values,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_delete_previous(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_delete_next(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_replace_next(
    const ft_reversible_deque_cursor* cursor,
    const void* value,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_reverse(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
ft_status ft_reversible_deque_cursor_snapshot(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque* result);

typedef struct ft_sorted_multiset {
    ft_tree tree;
    ft_compare_fn compare;
    void* compare_context;
} ft_sorted_multiset;

typedef ft_sorted_multiset ft_sorted_set;

ft_status ft_sorted_multiset_init(
    ft_sorted_multiset* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context);
ft_status ft_sorted_multiset_copy(const ft_sorted_multiset* source, ft_sorted_multiset* destination);
void ft_sorted_multiset_dispose(ft_sorted_multiset* set);
size_t ft_sorted_multiset_size(const ft_sorted_multiset* set);
bool ft_sorted_multiset_empty(const ft_sorted_multiset* set);
ft_status ft_sorted_multiset_add(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result);
ft_status ft_sorted_multiset_remove_one(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result);
bool ft_sorted_multiset_contains(const ft_sorted_multiset* set, const void* value);
size_t ft_sorted_multiset_count_of(const ft_sorted_multiset* set, const void* value);
ft_status ft_sorted_multiset_at(const ft_sorted_multiset* set, size_t index, void* destination);
ft_status ft_sorted_multiset_visit(const ft_sorted_multiset* set, ft_visit_fn visitor, void* context);

/* Immutable root-plus-rank gap cursor over one exact sorted-multiset snapshot. */
typedef struct ft_sorted_multiset_cursor {
    ft_sorted_multiset set;
    size_t position;
} ft_sorted_multiset_cursor;

ft_status ft_sorted_multiset_get_cursor(
    const ft_sorted_multiset* set,
    size_t position,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_get_cursor_lower_bound(
    const ft_sorted_multiset* set,
    const void* value,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_get_cursor_upper_bound(
    const ft_sorted_multiset* set,
    const void* value,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_get_cursor_at_item(
    const ft_sorted_multiset* set,
    const void* value,
    bool* found,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_copy(
    const ft_sorted_multiset_cursor* source,
    ft_sorted_multiset_cursor* destination);
void ft_sorted_multiset_cursor_move(
    ft_sorted_multiset_cursor* destination,
    ft_sorted_multiset_cursor* source);
void ft_sorted_multiset_cursor_dispose(ft_sorted_multiset_cursor* cursor);
bool ft_sorted_multiset_cursor_valid(const ft_sorted_multiset_cursor* cursor);
bool ft_sorted_multiset_cursor_empty(const ft_sorted_multiset_cursor* cursor);
size_t ft_sorted_multiset_cursor_size(const ft_sorted_multiset_cursor* cursor);
size_t ft_sorted_multiset_cursor_position(const ft_sorted_multiset_cursor* cursor);
ft_status ft_sorted_multiset_cursor_is_at_start(
    const ft_sorted_multiset_cursor* cursor,
    bool* result);
ft_status ft_sorted_multiset_cursor_is_at_end(
    const ft_sorted_multiset_cursor* cursor,
    bool* result);
ft_status ft_sorted_multiset_cursor_try_peek_previous(
    const ft_sorted_multiset_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_sorted_multiset_cursor_try_peek_next(
    const ft_sorted_multiset_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_sorted_multiset_cursor_move_previous(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_move_next(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_seek_rank(
    const ft_sorted_multiset_cursor* cursor,
    size_t position,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_add(
    const ft_sorted_multiset_cursor* cursor,
    const void* value,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_delete_previous(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_delete_next(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
ft_status ft_sorted_multiset_cursor_snapshot(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset* result);

ft_status ft_sorted_set_init(
    ft_sorted_set* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context);
ft_status ft_sorted_set_copy(const ft_sorted_set* source, ft_sorted_set* destination);
void ft_sorted_set_dispose(ft_sorted_set* set);
size_t ft_sorted_set_size(const ft_sorted_set* set);
bool ft_sorted_set_empty(const ft_sorted_set* set);
ft_status ft_sorted_set_add(const ft_sorted_set* set, const void* value, ft_sorted_set* result);
ft_status ft_sorted_set_remove(const ft_sorted_set* set, const void* value, ft_sorted_set* result);
bool ft_sorted_set_contains(const ft_sorted_set* set, const void* value);
ft_status ft_sorted_set_at(const ft_sorted_set* set, size_t index, void* destination);
ft_status ft_sorted_set_visit(const ft_sorted_set* set, ft_visit_fn visitor, void* context);

typedef ft_sorted_multiset_cursor ft_sorted_set_cursor;

#define ft_sorted_set_get_cursor ft_sorted_multiset_get_cursor
#define ft_sorted_set_get_cursor_lower_bound ft_sorted_multiset_get_cursor_lower_bound
#define ft_sorted_set_get_cursor_upper_bound ft_sorted_multiset_get_cursor_upper_bound
#define ft_sorted_set_get_cursor_at_item ft_sorted_multiset_get_cursor_at_item
#define ft_sorted_set_cursor_copy ft_sorted_multiset_cursor_copy
#define ft_sorted_set_cursor_move ft_sorted_multiset_cursor_move
#define ft_sorted_set_cursor_dispose ft_sorted_multiset_cursor_dispose
#define ft_sorted_set_cursor_valid ft_sorted_multiset_cursor_valid
#define ft_sorted_set_cursor_empty ft_sorted_multiset_cursor_empty
#define ft_sorted_set_cursor_size ft_sorted_multiset_cursor_size
#define ft_sorted_set_cursor_position ft_sorted_multiset_cursor_position
#define ft_sorted_set_cursor_is_at_start ft_sorted_multiset_cursor_is_at_start
#define ft_sorted_set_cursor_is_at_end ft_sorted_multiset_cursor_is_at_end
#define ft_sorted_set_cursor_try_peek_previous ft_sorted_multiset_cursor_try_peek_previous
#define ft_sorted_set_cursor_try_peek_next ft_sorted_multiset_cursor_try_peek_next
#define ft_sorted_set_cursor_move_previous ft_sorted_multiset_cursor_move_previous
#define ft_sorted_set_cursor_move_next ft_sorted_multiset_cursor_move_next
#define ft_sorted_set_cursor_seek_rank ft_sorted_multiset_cursor_seek_rank
#define ft_sorted_set_cursor_delete_previous ft_sorted_multiset_cursor_delete_previous
#define ft_sorted_set_cursor_delete_next ft_sorted_multiset_cursor_delete_next
#define ft_sorted_set_cursor_snapshot ft_sorted_multiset_cursor_snapshot
ft_status ft_sorted_set_cursor_add(
    const ft_sorted_set_cursor* cursor,
    const void* value,
    ft_sorted_set_cursor* result);

typedef void (*ft_sorted_map_visit_fn)(const void* key, const void* value, void* context);

typedef struct ft_sorted_map_entry_context ft_sorted_map_entry_context;

typedef struct ft_sorted_map {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type key_type;
    ft_value_type value_type;
    ft_compare_fn compare_key;
    void* compare_context;
    ft_sorted_map_entry_context* entry_context;
} ft_sorted_map;

ft_status ft_sorted_map_init(
    ft_sorted_map* map,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_key,
    void* compare_context);
ft_status ft_sorted_map_copy(const ft_sorted_map* source, ft_sorted_map* destination);
void ft_sorted_map_move(ft_sorted_map* destination, ft_sorted_map* source);
void ft_sorted_map_dispose(ft_sorted_map* map);
bool ft_sorted_map_empty(const ft_sorted_map* map);
size_t ft_sorted_map_size(const ft_sorted_map* map);
bool ft_sorted_map_contains_key(const ft_sorted_map* map, const void* key);
ft_status ft_sorted_map_try_get(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    void* value);
ft_status ft_sorted_map_index_of_key(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    size_t* index);
ft_status ft_sorted_map_entry_at(
    const ft_sorted_map* map,
    size_t index,
    void* key,
    void* value);
ft_status ft_sorted_map_set(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map* result);
ft_status ft_sorted_map_try_insert(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    bool* inserted,
    ft_sorted_map* result);
ft_status ft_sorted_map_insert(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map* result);
ft_status ft_sorted_map_remove(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map* result);
ft_status ft_sorted_map_visit(const ft_sorted_map* map, ft_sorted_map_visit_fn visitor, void* context);

typedef struct ft_sorted_map_cursor {
    ft_sorted_map map;
    size_t position;
} ft_sorted_map_cursor;

ft_status ft_sorted_map_get_cursor(
    const ft_sorted_map* map,
    size_t position,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_get_cursor_lower_bound(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_get_cursor_upper_bound(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_get_cursor_at_key(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_copy(
    const ft_sorted_map_cursor* source,
    ft_sorted_map_cursor* destination);
void ft_sorted_map_cursor_move(
    ft_sorted_map_cursor* destination,
    ft_sorted_map_cursor* source);
void ft_sorted_map_cursor_dispose(ft_sorted_map_cursor* cursor);
bool ft_sorted_map_cursor_valid(const ft_sorted_map_cursor* cursor);
bool ft_sorted_map_cursor_empty(const ft_sorted_map_cursor* cursor);
size_t ft_sorted_map_cursor_size(const ft_sorted_map_cursor* cursor);
size_t ft_sorted_map_cursor_position(const ft_sorted_map_cursor* cursor);
ft_status ft_sorted_map_cursor_is_at_start(const ft_sorted_map_cursor* cursor, bool* result);
ft_status ft_sorted_map_cursor_is_at_end(const ft_sorted_map_cursor* cursor, bool* result);
ft_status ft_sorted_map_cursor_try_peek_previous(
    const ft_sorted_map_cursor* cursor,
    bool* found,
    void* key,
    void* value);
ft_status ft_sorted_map_cursor_try_peek_next(
    const ft_sorted_map_cursor* cursor,
    bool* found,
    void* key,
    void* value);
ft_status ft_sorted_map_cursor_move_previous(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_move_next(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_seek_rank(
    const ft_sorted_map_cursor* cursor,
    size_t position,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_insert(
    const ft_sorted_map_cursor* cursor,
    const void* key,
    const void* value,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_try_insert(
    const ft_sorted_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_set(
    const ft_sorted_map_cursor* cursor,
    const void* key,
    const void* value,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_set_next_value(
    const ft_sorted_map_cursor* cursor,
    const void* value,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_delete_previous(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_delete_next(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
ft_status ft_sorted_map_cursor_snapshot(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map* result);

typedef struct ft_rope_chunk_context ft_rope_chunk_context;

typedef struct ft_rope {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type value_type;
    size_t max_chunk_length;
    ft_rope_chunk_context* chunk_context;
} ft_rope;

typedef struct ft_rope_split_result {
    ft_rope left;
    ft_rope right;
} ft_rope_split_result;

typedef struct ft_rope_cursor {
    ft_rope rope;
    size_t position;
} ft_rope_cursor;

ft_status ft_rope_init(ft_rope* rope, const ft_value_type* value_type);
ft_status ft_rope_from_array(
    ft_rope* rope,
    const ft_value_type* value_type,
    const void* values,
    size_t count);
ft_status ft_rope_copy(const ft_rope* source, ft_rope* destination);
void ft_rope_move(ft_rope* destination, ft_rope* source);
void ft_rope_dispose(ft_rope* rope);
bool ft_rope_empty(const ft_rope* rope);
size_t ft_rope_size(const ft_rope* rope);
ft_status ft_rope_try_size(const ft_rope* rope, size_t* size);
ft_status ft_rope_at(const ft_rope* rope, size_t index, void* destination);
ft_status ft_rope_push_back(const ft_rope* rope, const void* value, ft_rope* result);
ft_status ft_rope_insert_at(const ft_rope* rope, size_t index, const void* value, ft_rope* result);
ft_status ft_rope_remove_at(const ft_rope* rope, size_t index, ft_rope* result);
ft_status ft_rope_split_at(const ft_rope* rope, size_t index, ft_rope_split_result* result);
ft_status ft_rope_concat(const ft_rope* left, const ft_rope* right, ft_rope* result);
ft_status ft_rope_visit(const ft_rope* rope, ft_visit_fn visitor, void* context);

ft_status ft_rope_get_cursor(const ft_rope* rope, size_t position, ft_rope_cursor* result);
ft_status ft_rope_cursor_copy(const ft_rope_cursor* source, ft_rope_cursor* destination);
void ft_rope_cursor_move(ft_rope_cursor* destination, ft_rope_cursor* source);
void ft_rope_cursor_dispose(ft_rope_cursor* cursor);
bool ft_rope_cursor_valid(const ft_rope_cursor* cursor);
bool ft_rope_cursor_empty(const ft_rope_cursor* cursor);
size_t ft_rope_cursor_size(const ft_rope_cursor* cursor);
ft_status ft_rope_cursor_try_size(const ft_rope_cursor* cursor, size_t* size);
size_t ft_rope_cursor_position(const ft_rope_cursor* cursor);
ft_status ft_rope_cursor_is_at_start(const ft_rope_cursor* cursor, bool* result);
ft_status ft_rope_cursor_is_at_end(const ft_rope_cursor* cursor, bool* result);
ft_status ft_rope_cursor_try_peek_previous(
    const ft_rope_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_rope_cursor_try_peek_next(
    const ft_rope_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_rope_cursor_move_previous(const ft_rope_cursor* cursor, ft_rope_cursor* result);
ft_status ft_rope_cursor_move_next(const ft_rope_cursor* cursor, ft_rope_cursor* result);
ft_status ft_rope_cursor_seek(
    const ft_rope_cursor* cursor,
    size_t position,
    ft_rope_cursor* result);
ft_status ft_rope_cursor_insert(
    const ft_rope_cursor* cursor,
    const void* value,
    ft_rope_cursor* result);
ft_status ft_rope_cursor_insert_array(
    const ft_rope_cursor* cursor,
    const void* values,
    size_t count,
    ft_rope_cursor* result);
ft_status ft_rope_cursor_insert_rope(
    const ft_rope_cursor* cursor,
    const ft_rope* values,
    ft_rope_cursor* result);
ft_status ft_rope_cursor_delete_previous(const ft_rope_cursor* cursor, ft_rope_cursor* result);
ft_status ft_rope_cursor_delete_next(const ft_rope_cursor* cursor, ft_rope_cursor* result);
ft_status ft_rope_cursor_replace_next(
    const ft_rope_cursor* cursor,
    const void* value,
    ft_rope_cursor* result);
ft_status ft_rope_cursor_snapshot(const ft_rope_cursor* cursor, ft_rope* result);

typedef struct ft_measured_rope_chunk_context ft_measured_rope_chunk_context;

typedef struct ft_measured_rope {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type value_type;
    ft_measure_policy user_measure;
    size_t max_chunk_length;
    ft_measured_rope_chunk_context* chunk_context;
} ft_measured_rope;

typedef struct ft_measured_rope_split_result {
    ft_measured_rope left;
    ft_measured_rope right;
} ft_measured_rope_split_result;

ft_status ft_measured_rope_init(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure);
ft_status ft_measured_rope_from_array(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure,
    const void* values,
    size_t count);
ft_status ft_measured_rope_copy(const ft_measured_rope* source, ft_measured_rope* destination);
void ft_measured_rope_move(ft_measured_rope* destination, ft_measured_rope* source);
void ft_measured_rope_dispose(ft_measured_rope* rope);
bool ft_measured_rope_empty(const ft_measured_rope* rope);
size_t ft_measured_rope_size(const ft_measured_rope* rope);
ft_status ft_measured_rope_try_size(const ft_measured_rope* rope, size_t* size);
ft_status ft_measured_rope_measure(const ft_measured_rope* rope, void* destination);
ft_status ft_measured_rope_prefix_measure(const ft_measured_rope* rope, size_t count, void* destination);
ft_status ft_measured_rope_at(const ft_measured_rope* rope, size_t index, void* destination);
ft_status ft_measured_rope_push_back(const ft_measured_rope* rope, const void* value, ft_measured_rope* result);
ft_status ft_measured_rope_insert_at(
    const ft_measured_rope* rope,
    size_t index,
    const void* value,
    ft_measured_rope* result);
ft_status ft_measured_rope_remove_at(const ft_measured_rope* rope, size_t index, ft_measured_rope* result);
ft_status ft_measured_rope_split_at(
    const ft_measured_rope* rope,
    size_t index,
    ft_measured_rope_split_result* result);
ft_status ft_measured_rope_split_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    ft_measured_rope_split_result* result);
ft_status ft_measured_rope_locate_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    size_t* index,
    void* measure_before,
    void* value);
ft_status ft_measured_rope_concat(
    const ft_measured_rope* left,
    const ft_measured_rope* right,
    ft_measured_rope* result);
ft_status ft_measured_rope_visit(const ft_measured_rope* rope, ft_visit_fn visitor, void* context);

typedef struct ft_measured_rope_cursor {
    ft_measured_rope rope;
    size_t position;
} ft_measured_rope_cursor;

ft_status ft_measured_rope_get_cursor(
    const ft_measured_rope* rope,
    size_t position,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_get_cursor_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_copy(
    const ft_measured_rope_cursor* source,
    ft_measured_rope_cursor* destination);
void ft_measured_rope_cursor_move(
    ft_measured_rope_cursor* destination,
    ft_measured_rope_cursor* source);
void ft_measured_rope_cursor_dispose(ft_measured_rope_cursor* cursor);
bool ft_measured_rope_cursor_valid(const ft_measured_rope_cursor* cursor);
bool ft_measured_rope_cursor_empty(const ft_measured_rope_cursor* cursor);
size_t ft_measured_rope_cursor_size(const ft_measured_rope_cursor* cursor);
ft_status ft_measured_rope_cursor_try_size(const ft_measured_rope_cursor* cursor, size_t* size);
size_t ft_measured_rope_cursor_position(const ft_measured_rope_cursor* cursor);
ft_status ft_measured_rope_cursor_is_at_start(const ft_measured_rope_cursor* cursor, bool* result);
ft_status ft_measured_rope_cursor_is_at_end(const ft_measured_rope_cursor* cursor, bool* result);
ft_status ft_measured_rope_cursor_measure_before(
    const ft_measured_rope_cursor* cursor,
    void* destination);
ft_status ft_measured_rope_cursor_measure_after(
    const ft_measured_rope_cursor* cursor,
    void* destination);
ft_status ft_measured_rope_cursor_try_peek_previous(
    const ft_measured_rope_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_measured_rope_cursor_try_peek_next(
    const ft_measured_rope_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_measured_rope_cursor_move_previous(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_move_next(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_seek(
    const ft_measured_rope_cursor* cursor,
    size_t position,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_seek_by_measure(
    const ft_measured_rope_cursor* cursor,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_insert(
    const ft_measured_rope_cursor* cursor,
    const void* value,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_insert_array(
    const ft_measured_rope_cursor* cursor,
    const void* values,
    size_t count,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_insert_rope(
    const ft_measured_rope_cursor* cursor,
    const ft_measured_rope* values,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_delete_previous(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_delete_next(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_replace_next(
    const ft_measured_rope_cursor* cursor,
    const void* value,
    ft_measured_rope_cursor* result);
ft_status ft_measured_rope_cursor_snapshot(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope* result);

typedef struct ft_priority_queue_entry_context ft_priority_queue_entry_context;

typedef struct ft_priority_queue {
    ft_tree_policy policy;
    ft_tree tree;
    ft_value_type value_type;
    ft_value_type priority_type;
    ft_compare_fn compare_priority;
    void* compare_context;
    uint64_t next_ordinal;
    ft_priority_queue_entry_context* entry_context;
} ft_priority_queue;

ft_status ft_priority_queue_init(
    ft_priority_queue* queue,
    const ft_value_type* value_type,
    const ft_value_type* priority_type,
    ft_compare_fn compare_priority,
    void* compare_context);
ft_status ft_priority_queue_copy(const ft_priority_queue* source, ft_priority_queue* destination);
void ft_priority_queue_move(ft_priority_queue* destination, ft_priority_queue* source);
void ft_priority_queue_dispose(ft_priority_queue* queue);
bool ft_priority_queue_empty(const ft_priority_queue* queue);
size_t ft_priority_queue_size(const ft_priority_queue* queue);
ft_status ft_priority_queue_push(
    const ft_priority_queue* queue,
    const void* value,
    const void* priority,
    ft_priority_queue* result);
ft_status ft_priority_queue_try_peek(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority);
ft_status ft_priority_queue_try_pop(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority,
    ft_priority_queue* rest);

typedef struct ft_interval_i64 {
    int64_t low;
    int64_t high;
} ft_interval_i64;

typedef struct ft_interval_tree_i64 {
    ft_tree_policy policy;
    ft_sorted_multiset intervals;
} ft_interval_tree_i64;

ft_status ft_interval_tree_i64_init(ft_interval_tree_i64* tree);
ft_status ft_interval_tree_i64_copy(const ft_interval_tree_i64* source, ft_interval_tree_i64* destination);
void ft_interval_tree_i64_move(ft_interval_tree_i64* destination, ft_interval_tree_i64* source);
void ft_interval_tree_i64_dispose(ft_interval_tree_i64* tree);
bool ft_interval_tree_i64_empty(const ft_interval_tree_i64* tree);
size_t ft_interval_tree_i64_size(const ft_interval_tree_i64* tree);
ft_status ft_interval_tree_i64_insert(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result);
ft_status ft_interval_tree_i64_remove_one(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result);
bool ft_interval_tree_i64_contains(const ft_interval_tree_i64* tree, ft_interval_i64 interval);
ft_status ft_interval_tree_i64_try_find_overlap(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 query,
    bool* found,
    ft_interval_i64* overlap);
size_t ft_interval_tree_i64_count_overlaps(const ft_interval_tree_i64* tree, ft_interval_i64 query);
ft_status ft_interval_tree_i64_at(const ft_interval_tree_i64* tree, size_t index, ft_interval_i64* destination);

typedef struct ft_interval_tree_i64_cursor {
    ft_interval_tree_i64 tree;
    size_t position;
} ft_interval_tree_i64_cursor;

ft_status ft_interval_tree_i64_get_cursor(
    const ft_interval_tree_i64* tree,
    size_t position,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_get_cursor_lower_bound(
    const ft_interval_tree_i64* tree,
    int64_t low,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_get_cursor_upper_bound(
    const ft_interval_tree_i64* tree,
    int64_t low,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_get_cursor_at_interval(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    bool* found,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_find_overlap_cursor(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 query,
    bool* found,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_find_containing_cursor(
    const ft_interval_tree_i64* tree,
    int64_t point,
    bool* found,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_copy(
    const ft_interval_tree_i64_cursor* source,
    ft_interval_tree_i64_cursor* destination);
void ft_interval_tree_i64_cursor_move(
    ft_interval_tree_i64_cursor* destination,
    ft_interval_tree_i64_cursor* source);
void ft_interval_tree_i64_cursor_dispose(ft_interval_tree_i64_cursor* cursor);
bool ft_interval_tree_i64_cursor_valid(const ft_interval_tree_i64_cursor* cursor);
bool ft_interval_tree_i64_cursor_empty(const ft_interval_tree_i64_cursor* cursor);
size_t ft_interval_tree_i64_cursor_size(const ft_interval_tree_i64_cursor* cursor);
size_t ft_interval_tree_i64_cursor_position(const ft_interval_tree_i64_cursor* cursor);
ft_status ft_interval_tree_i64_cursor_is_at_start(
    const ft_interval_tree_i64_cursor* cursor,
    bool* result);
ft_status ft_interval_tree_i64_cursor_is_at_end(
    const ft_interval_tree_i64_cursor* cursor,
    bool* result);
ft_status ft_interval_tree_i64_cursor_try_peek_previous(
    const ft_interval_tree_i64_cursor* cursor,
    bool* found,
    ft_interval_i64* interval);
ft_status ft_interval_tree_i64_cursor_try_peek_next(
    const ft_interval_tree_i64_cursor* cursor,
    bool* found,
    ft_interval_i64* interval);
ft_status ft_interval_tree_i64_cursor_move_previous(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_move_next(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_seek_rank(
    const ft_interval_tree_i64_cursor* cursor,
    size_t position,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_seek_next_overlap(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_i64 query,
    bool* found,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_insert(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_i64 interval,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_delete_previous(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_delete_next(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
ft_status ft_interval_tree_i64_cursor_snapshot(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64* result);

typedef struct ft_interval_tree_context ft_interval_tree_context;

typedef struct ft_interval_tree {
    ft_tree_policy policy;
    ft_sorted_multiset intervals;
    ft_value_type endpoint_type;
    ft_compare_fn compare_endpoint;
    void* compare_context;
    ft_interval_tree_context* interval_context;
} ft_interval_tree;

ft_status ft_interval_tree_init(
    ft_interval_tree* tree,
    const ft_value_type* endpoint_type,
    ft_compare_fn compare_endpoint,
    void* compare_context);
ft_status ft_interval_tree_copy(const ft_interval_tree* source, ft_interval_tree* destination);
void ft_interval_tree_move(ft_interval_tree* destination, ft_interval_tree* source);
void ft_interval_tree_dispose(ft_interval_tree* tree);
bool ft_interval_tree_empty(const ft_interval_tree* tree);
size_t ft_interval_tree_size(const ft_interval_tree* tree);
ft_status ft_interval_tree_insert(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_tree* result);
ft_status ft_interval_tree_remove_one(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_tree* result);
bool ft_interval_tree_contains(const ft_interval_tree* tree, const void* low, const void* high);
ft_status ft_interval_tree_try_find_overlap(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high);
size_t ft_interval_tree_count_overlaps(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high);
ft_status ft_interval_tree_at(
    const ft_interval_tree* tree,
    size_t index,
    void* low,
    void* high);

typedef struct ft_interval_tree_cursor {
    ft_interval_tree tree;
    size_t position;
} ft_interval_tree_cursor;

ft_status ft_interval_tree_get_cursor(
    const ft_interval_tree* tree,
    size_t position,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_get_cursor_lower_bound(
    const ft_interval_tree* tree,
    const void* low,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_get_cursor_upper_bound(
    const ft_interval_tree* tree,
    const void* low,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_get_cursor_at_interval(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    bool* found,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_find_overlap_cursor(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_find_containing_cursor(
    const ft_interval_tree* tree,
    const void* point,
    bool* found,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_copy(
    const ft_interval_tree_cursor* source,
    ft_interval_tree_cursor* destination);
void ft_interval_tree_cursor_move(
    ft_interval_tree_cursor* destination,
    ft_interval_tree_cursor* source);
void ft_interval_tree_cursor_dispose(ft_interval_tree_cursor* cursor);
bool ft_interval_tree_cursor_valid(const ft_interval_tree_cursor* cursor);
bool ft_interval_tree_cursor_empty(const ft_interval_tree_cursor* cursor);
size_t ft_interval_tree_cursor_size(const ft_interval_tree_cursor* cursor);
size_t ft_interval_tree_cursor_position(const ft_interval_tree_cursor* cursor);
ft_status ft_interval_tree_cursor_is_at_start(
    const ft_interval_tree_cursor* cursor,
    bool* result);
ft_status ft_interval_tree_cursor_is_at_end(
    const ft_interval_tree_cursor* cursor,
    bool* result);
ft_status ft_interval_tree_cursor_try_peek_previous(
    const ft_interval_tree_cursor* cursor,
    bool* found,
    void* low,
    void* high);
ft_status ft_interval_tree_cursor_try_peek_next(
    const ft_interval_tree_cursor* cursor,
    bool* found,
    void* low,
    void* high);
ft_status ft_interval_tree_cursor_move_previous(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_move_next(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_seek_rank(
    const ft_interval_tree_cursor* cursor,
    size_t position,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_seek_next_overlap(
    const ft_interval_tree_cursor* cursor,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_insert(
    const ft_interval_tree_cursor* cursor,
    const void* low,
    const void* high,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_delete_previous(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_delete_next(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
ft_status ft_interval_tree_cursor_snapshot(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree* result);

typedef struct ft_text_rope {
    ft_measured_rope rope;
} ft_text_rope;

typedef struct ft_text_rope_cursor {
    ft_measured_rope_cursor cursor;
} ft_text_rope_cursor;

typedef struct ft_line_column {
    size_t line;
    size_t column;
} ft_line_column;

ft_status ft_text_rope_init(ft_text_rope* rope);
ft_status ft_text_rope_from_cstr(const char* text, ft_text_rope* rope);
ft_status ft_text_rope_copy(const ft_text_rope* source, ft_text_rope* destination);
void ft_text_rope_move(ft_text_rope* destination, ft_text_rope* source);
void ft_text_rope_dispose(ft_text_rope* rope);
size_t ft_text_rope_size(const ft_text_rope* rope);
ft_status ft_text_rope_try_size(const ft_text_rope* rope, size_t* size);
ft_status ft_text_rope_at(const ft_text_rope* rope, size_t index, char* value);
ft_status ft_text_rope_insert_char(const ft_text_rope* rope, size_t index, char value, ft_text_rope* result);
ft_status ft_text_rope_remove_at(const ft_text_rope* rope, size_t index, ft_text_rope* result);
size_t ft_text_rope_line_count(const ft_text_rope* rope);
ft_status ft_text_rope_try_line_count(const ft_text_rope* rope, size_t* count);
ft_status ft_text_rope_line_of_offset(const ft_text_rope* rope, size_t offset, size_t* line);
ft_status ft_text_rope_line_start_offset(const ft_text_rope* rope, size_t line, size_t* offset);
ft_status ft_text_rope_line_column_of(const ft_text_rope* rope, size_t offset, ft_line_column* result);
ft_status ft_text_rope_offset_of(const ft_text_rope* rope, size_t line, size_t column, size_t* offset);
ft_status ft_text_rope_visit(const ft_text_rope* rope, ft_visit_fn visitor, void* context);
ft_status ft_text_rope_get_cursor(
    const ft_text_rope* rope,
    size_t position,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_get_cursor_by_measure(
    const ft_text_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_copy(
    const ft_text_rope_cursor* source,
    ft_text_rope_cursor* destination);
void ft_text_rope_cursor_move(ft_text_rope_cursor* destination, ft_text_rope_cursor* source);
void ft_text_rope_cursor_dispose(ft_text_rope_cursor* cursor);
bool ft_text_rope_cursor_valid(const ft_text_rope_cursor* cursor);
bool ft_text_rope_cursor_empty(const ft_text_rope_cursor* cursor);
size_t ft_text_rope_cursor_size(const ft_text_rope_cursor* cursor);
ft_status ft_text_rope_cursor_try_size(const ft_text_rope_cursor* cursor, size_t* size);
size_t ft_text_rope_cursor_position(const ft_text_rope_cursor* cursor);
ft_status ft_text_rope_cursor_is_at_start(const ft_text_rope_cursor* cursor, bool* result);
ft_status ft_text_rope_cursor_is_at_end(const ft_text_rope_cursor* cursor, bool* result);
ft_status ft_text_rope_cursor_line_column(
    const ft_text_rope_cursor* cursor,
    ft_line_column* result);
ft_status ft_text_rope_cursor_measure_before(
    const ft_text_rope_cursor* cursor,
    size_t* newlines);
ft_status ft_text_rope_cursor_measure_after(
    const ft_text_rope_cursor* cursor,
    size_t* newlines);
ft_status ft_text_rope_cursor_try_peek_previous(
    const ft_text_rope_cursor* cursor,
    bool* found,
    char* value);
ft_status ft_text_rope_cursor_try_peek_next(
    const ft_text_rope_cursor* cursor,
    bool* found,
    char* value);
ft_status ft_text_rope_cursor_move_previous(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_move_next(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_seek(
    const ft_text_rope_cursor* cursor,
    size_t position,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_seek_by_measure(
    const ft_text_rope_cursor* cursor,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_insert_char(
    const ft_text_rope_cursor* cursor,
    char value,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_insert_cstr(
    const ft_text_rope_cursor* cursor,
    const char* text,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_insert_rope(
    const ft_text_rope_cursor* cursor,
    const ft_text_rope* values,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_delete_previous(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_delete_next(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_replace_next(
    const ft_text_rope_cursor* cursor,
    char value,
    ft_text_rope_cursor* result);
ft_status ft_text_rope_cursor_snapshot(
    const ft_text_rope_cursor* cursor,
    ft_text_rope* result);

#ifdef __cplusplus
}
#endif

#endif
