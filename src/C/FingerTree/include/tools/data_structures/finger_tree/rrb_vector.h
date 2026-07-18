#ifndef TOOLS_DATA_STRUCTURES_FINGER_TREE_C_RRB_VECTOR_H
#define TOOLS_DATA_STRUCTURES_FINGER_TREE_C_RRB_VECTOR_H

#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ft_rrb_equal_fn)(const void* left, const void* right, void* context);
typedef void* (*ft_rrb_allocate_fn)(size_t size, void* context);
typedef void (*ft_rrb_deallocate_fn)(void* allocation, void* context);

typedef struct ft_rrb_allocator {
    ft_rrb_allocate_fn allocate;
    ft_rrb_deallocate_fn deallocate;
    void* context;
} ft_rrb_allocator;

typedef struct ft_rrb_policy {
    ft_value_type value;
    ft_rrb_equal_fn equal;
    void* equal_context;
    ft_rrb_allocator allocator;
} ft_rrb_policy;

typedef struct ft_rrb_node ft_rrb_node;

typedef struct ft_rrb_vector {
    const ft_rrb_policy* policy;
    ft_rrb_node* root;
} ft_rrb_vector;

typedef struct ft_rrb_split_result {
    ft_rrb_vector left;
    ft_rrb_vector right;
} ft_rrb_split_result;

typedef struct ft_rrb_vector_cursor {
    ft_rrb_vector vector;
    size_t position;
} ft_rrb_vector_cursor;

typedef struct ft_rrb_statistics {
    size_t count;
    unsigned height;
    size_t leaf_count;
    size_t branch_count;
    size_t regular_branch_count;
    size_t relaxed_branch_count;
    size_t minimum_leaf_length;
    size_t maximum_leaf_length;
    size_t minimum_branching_factor;
    size_t maximum_branching_factor;
} ft_rrb_statistics;

typedef void (*ft_rrb_leaf_visit_fn)(const void* leaf_identity, size_t count, void* context);

typedef struct ft_rrb_builder_rep ft_rrb_builder_rep;

typedef struct ft_rrb_builder {
    const ft_rrb_policy* policy;
    ft_rrb_builder_rep* rep;
} ft_rrb_builder;

void ft_rrb_policy_init(
    ft_rrb_policy* policy,
    const ft_value_type* value_type,
    ft_rrb_equal_fn equal,
    void* equal_context);

ft_status ft_rrb_vector_init(ft_rrb_vector* vector, const ft_rrb_policy* policy);
ft_status ft_rrb_vector_from_array(
    ft_rrb_vector* vector,
    const ft_rrb_policy* policy,
    const void* values,
    size_t count);
ft_status ft_rrb_vector_copy(const ft_rrb_vector* source, ft_rrb_vector* destination);
void ft_rrb_vector_move(ft_rrb_vector* destination, ft_rrb_vector* source);
void ft_rrb_vector_dispose(ft_rrb_vector* vector);

bool ft_rrb_vector_empty(const ft_rrb_vector* vector);
size_t ft_rrb_vector_size(const ft_rrb_vector* vector);
unsigned ft_rrb_vector_height(const ft_rrb_vector* vector);

ft_status ft_rrb_vector_at(const ft_rrb_vector* vector, size_t index, void* destination);
ft_status ft_rrb_vector_front(const ft_rrb_vector* vector, void* destination);
ft_status ft_rrb_vector_back(const ft_rrb_vector* vector, void* destination);
ft_status ft_rrb_vector_set(
    const ft_rrb_vector* vector,
    size_t index,
    const void* value,
    ft_rrb_vector* result);
ft_status ft_rrb_vector_push_front(
    const ft_rrb_vector* vector,
    const void* value,
    ft_rrb_vector* result);
ft_status ft_rrb_vector_push_back(
    const ft_rrb_vector* vector,
    const void* value,
    ft_rrb_vector* result);
ft_status ft_rrb_vector_pop_front(
    const ft_rrb_vector* vector,
    void* value,
    ft_rrb_vector* rest);
ft_status ft_rrb_vector_pop_back(
    const ft_rrb_vector* vector,
    void* value,
    ft_rrb_vector* rest);
ft_status ft_rrb_vector_concat(
    const ft_rrb_vector* left,
    const ft_rrb_vector* right,
    ft_rrb_vector* result);
ft_status ft_rrb_vector_split_at(
    const ft_rrb_vector* vector,
    size_t index,
    ft_rrb_split_result* result);
ft_status ft_rrb_vector_insert_range(
    const ft_rrb_vector* vector,
    size_t index,
    const void* values,
    size_t count,
    ft_rrb_vector* result);
ft_status ft_rrb_vector_remove_range(
    const ft_rrb_vector* vector,
    size_t index,
    size_t count,
    ft_rrb_vector* result);

ft_status ft_rrb_vector_visit(const ft_rrb_vector* vector, ft_visit_fn visitor, void* context);
ft_status ft_rrb_vector_visit_leaves(
    const ft_rrb_vector* vector,
    ft_rrb_leaf_visit_fn visitor,
    void* context);

const void* ft_rrb_vector_root_identity(const ft_rrb_vector* vector);
bool ft_rrb_vector_shares_root(const ft_rrb_vector* left, const ft_rrb_vector* right);
bool ft_rrb_vector_validate(const ft_rrb_vector* vector, ft_rrb_statistics* statistics);

ft_status ft_rrb_vector_get_cursor(
    const ft_rrb_vector* vector,
    size_t position,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_copy(
    const ft_rrb_vector_cursor* source,
    ft_rrb_vector_cursor* destination);
void ft_rrb_vector_cursor_move(
    ft_rrb_vector_cursor* destination,
    ft_rrb_vector_cursor* source);
void ft_rrb_vector_cursor_dispose(ft_rrb_vector_cursor* cursor);
bool ft_rrb_vector_cursor_valid(const ft_rrb_vector_cursor* cursor);
bool ft_rrb_vector_cursor_empty(const ft_rrb_vector_cursor* cursor);
size_t ft_rrb_vector_cursor_size(const ft_rrb_vector_cursor* cursor);
size_t ft_rrb_vector_cursor_position(const ft_rrb_vector_cursor* cursor);
ft_status ft_rrb_vector_cursor_is_at_start(const ft_rrb_vector_cursor* cursor, bool* result);
ft_status ft_rrb_vector_cursor_is_at_end(const ft_rrb_vector_cursor* cursor, bool* result);
ft_status ft_rrb_vector_cursor_try_peek_previous(
    const ft_rrb_vector_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_rrb_vector_cursor_try_peek_next(
    const ft_rrb_vector_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_rrb_vector_cursor_move_previous(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_move_next(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_seek(
    const ft_rrb_vector_cursor* cursor,
    size_t position,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_insert(
    const ft_rrb_vector_cursor* cursor,
    const void* value,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_insert_array(
    const ft_rrb_vector_cursor* cursor,
    const void* values,
    size_t count,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_insert_vector(
    const ft_rrb_vector_cursor* cursor,
    const ft_rrb_vector* values,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_delete_previous(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_delete_next(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_replace_next(
    const ft_rrb_vector_cursor* cursor,
    const void* value,
    ft_rrb_vector_cursor* result);
ft_status ft_rrb_vector_cursor_snapshot(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector* result);

ft_status ft_rrb_builder_init(ft_rrb_builder* builder, const ft_rrb_policy* policy);
ft_status ft_rrb_builder_init_from_vector(ft_rrb_builder* builder, const ft_rrb_vector* vector);
void ft_rrb_builder_dispose(ft_rrb_builder* builder);
size_t ft_rrb_builder_size(const ft_rrb_builder* builder);
ft_status ft_rrb_builder_append(ft_rrb_builder* builder, const void* value);
ft_status ft_rrb_builder_append_range(ft_rrb_builder* builder, const void* values, size_t count);
ft_status ft_rrb_builder_to_vector(ft_rrb_builder* builder, ft_rrb_vector* result);
void ft_rrb_builder_clear(ft_rrb_builder* builder);

#ifdef __cplusplus
}
#endif

#endif
