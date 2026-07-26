/*
 * A persistent relaxed radix-balanced vector: indexed access with cheap concatenation.
 *
 * Radix addressing gives effectively constant-time indexing; allowing nodes to be slightly
 * underfull ('relaxed') is what makes concatenation and splitting logarithmic instead of linear.
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_FINGER_TREE_C_RRB_VECTOR_H
#define DURABLE7_FINGER_TREE_C_RRB_VECTOR_H

#include <durable7/finger_tree/fingertree.h>

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

/* One node of a relaxed radix-balanced vector, regular or relaxed. Opaque. */
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

/* The shared representation behind a bulk vector builder. */
typedef struct ft_rrb_builder_rep ft_rrb_builder_rep;

typedef struct ft_rrb_builder {
    const ft_rrb_policy* policy;
    ft_rrb_builder_rep* rep;
} ft_rrb_builder;

/* Fills a vector policy with its defaults. */
void ft_rrb_policy_init(
    ft_rrb_policy* policy,
    const ft_value_type* value_type,
    ft_rrb_equal_fn equal,
    void* equal_context);

/* Initializes the vector in place. */
ft_status ft_rrb_vector_init(ft_rrb_vector* vector, const ft_rrb_policy* policy);
/* Initializes a vector holding the given elements, built in bulk rather than by repeated
 * insertion. */
ft_status ft_rrb_vector_from_array(
    ft_rrb_vector* vector,
    const ft_rrb_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same vector version, taking a reference rather than
 * copying. */
ft_status ft_rrb_vector_copy(const ft_rrb_vector* source, ft_rrb_vector* destination);
/* Relocates an initialized vector into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_rrb_vector_move(ft_rrb_vector* destination, ft_rrb_vector* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_rrb_vector_dispose(ft_rrb_vector* vector);

/* Whether the vector holds no elements. */
bool ft_rrb_vector_empty(const ft_rrb_vector* vector);
/* Number of elements in the vector. */
size_t ft_rrb_vector_size(const ft_rrb_vector* vector);
/* The structure's height. */
unsigned ft_rrb_vector_height(const ft_rrb_vector* vector);

/* Reads the element at the given position. */
ft_status ft_rrb_vector_at(const ft_rrb_vector* vector, size_t index, void* destination);
/* Reads the first element. */
ft_status ft_rrb_vector_front(const ft_rrb_vector* vector, void* destination);
/* Reads the last element. */
ft_status ft_rrb_vector_back(const ft_rrb_vector* vector, void* destination);
/* Produces a vector with the key bound to the value, adding or replacing as needed. */
ft_status ft_rrb_vector_set(
    const ft_rrb_vector* vector,
    size_t index,
    const void* value,
    ft_rrb_vector* result);
/* Produces a vector with the element added at the front. */
ft_status ft_rrb_vector_push_front(
    const ft_rrb_vector* vector,
    const void* value,
    ft_rrb_vector* result);
/* Produces a vector with the element added at the back. */
ft_status ft_rrb_vector_push_back(
    const ft_rrb_vector* vector,
    const void* value,
    ft_rrb_vector* result);
/* Produces a vector without its first element, reading that element out. */
ft_status ft_rrb_vector_pop_front(
    const ft_rrb_vector* vector,
    void* value,
    ft_rrb_vector* rest);
/* Produces a vector without its last element, reading that element out. */
ft_status ft_rrb_vector_pop_back(
    const ft_rrb_vector* vector,
    void* value,
    ft_rrb_vector* rest);
/* Produces the concatenation of two vectors, sharing both operands' unchanged structure. */
ft_status ft_rrb_vector_concat(
    const ft_rrb_vector* left,
    const ft_rrb_vector* right,
    ft_rrb_vector* result);
/* Splits into the elements before the position and those from it onward. */
ft_status ft_rrb_vector_split_at(
    const ft_rrb_vector* vector,
    size_t index,
    ft_rrb_split_result* result);
/* Produces a vector with a range inserted at the position. */
ft_status ft_rrb_vector_insert_range(
    const ft_rrb_vector* vector,
    size_t index,
    const void* values,
    size_t count,
    ft_rrb_vector* result);
/* Produces a vector without the elements in the range. */
ft_status ft_rrb_vector_remove_range(
    const ft_rrb_vector* vector,
    size_t index,
    size_t count,
    ft_rrb_vector* result);

/* Calls the visitor once per element, in the vector's own order. */
ft_status ft_rrb_vector_visit(const ft_rrb_vector* vector, ft_visit_fn visitor, void* context);
/* Calls the visitor once per leaf, for tests and diagnostics. */
ft_status ft_rrb_vector_visit_leaves(
    const ft_rrb_vector* vector,
    ft_rrb_leaf_visit_fn visitor,
    void* context);

/* The root node's address, for tests that a no-op shared rather than copied. */
const void* ft_rrb_vector_root_identity(const ft_rrb_vector* vector);
/* Whether both handles reference the same root node. A representation check used to confirm a no-op
 * avoided copying, not an equality test. */
bool ft_rrb_vector_shares_root(const ft_rrb_vector* left, const ft_rrb_vector* right);
/* Checks the vector's structural invariants, for tests and diagnostics. */
bool ft_rrb_vector_validate(const ft_rrb_vector* vector, ft_rrb_statistics* statistics);

/* Initializes a cursor at the given gap of the vector. */
ft_status ft_rrb_vector_get_cursor(
    const ft_rrb_vector* vector,
    size_t position,
    ft_rrb_vector_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_rrb_vector_cursor_copy(
    const ft_rrb_vector_cursor* source,
    ft_rrb_vector_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_rrb_vector_cursor_move(
    ft_rrb_vector_cursor* destination,
    ft_rrb_vector_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_rrb_vector_cursor_dispose(ft_rrb_vector_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_rrb_vector_cursor_valid(const ft_rrb_vector_cursor* cursor);
/* Whether the vector version the cursor is positioned in holds no elements. */
bool ft_rrb_vector_cursor_empty(const ft_rrb_vector_cursor* cursor);
/* Number of elements in the vector version the cursor is positioned in. */
size_t ft_rrb_vector_cursor_size(const ft_rrb_vector_cursor* cursor);
/* The cursor's gap position. */
size_t ft_rrb_vector_cursor_position(const ft_rrb_vector_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_rrb_vector_cursor_is_at_start(const ft_rrb_vector_cursor* cursor, bool* result);
/* Whether the gap follows the last element. */
ft_status ft_rrb_vector_cursor_is_at_end(const ft_rrb_vector_cursor* cursor, bool* result);
/* Reads the element immediately before the gap, reporting whether one exists. */
ft_status ft_rrb_vector_cursor_try_peek_previous(
    const ft_rrb_vector_cursor* cursor,
    bool* found,
    void* value);
/* Reads the element immediately after the gap, reporting whether one exists. */
ft_status ft_rrb_vector_cursor_try_peek_next(
    const ft_rrb_vector_cursor* cursor,
    bool* found,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_rrb_vector_cursor_move_previous(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_rrb_vector_cursor_move_next(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
/* Moves the cursor to the given position within the same vector version. */
ft_status ft_rrb_vector_cursor_seek(
    const ft_rrb_vector_cursor* cursor,
    size_t position,
    ft_rrb_vector_cursor* result);
/* Inserts an element at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rrb_vector_cursor_insert(
    const ft_rrb_vector_cursor* cursor,
    const void* value,
    ft_rrb_vector_cursor* result);
/* Produces a vector with the array's elements inserted at the position. */
ft_status ft_rrb_vector_cursor_insert_array(
    const ft_rrb_vector_cursor* cursor,
    const void* values,
    size_t count,
    ft_rrb_vector_cursor* result);
/* Produces a sequence with the vector's elements inserted at the position. */
ft_status ft_rrb_vector_cursor_insert_vector(
    const ft_rrb_vector_cursor* cursor,
    const ft_rrb_vector* values,
    ft_rrb_vector_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rrb_vector_cursor_delete_previous(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rrb_vector_cursor_delete_next(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector_cursor* result);
/* Replaces the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rrb_vector_cursor_replace_next(
    const ft_rrb_vector_cursor* cursor,
    const void* value,
    ft_rrb_vector_cursor* result);
/* Initializes a handle on the vector version this cursor is positioned in. */
ft_status ft_rrb_vector_cursor_snapshot(
    const ft_rrb_vector_cursor* cursor,
    ft_rrb_vector* result);

/* Initializes the builder in place. */
ft_status ft_rrb_builder_init(ft_rrb_builder* builder, const ft_rrb_policy* policy);
/* Initializes the value from a vector's elements. */
ft_status ft_rrb_builder_init_from_vector(ft_rrb_builder* builder, const ft_rrb_vector* vector);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_rrb_builder_dispose(ft_rrb_builder* builder);
/* Number of elements in the builder. */
size_t ft_rrb_builder_size(const ft_rrb_builder* builder);
/* Appends the given input. */
ft_status ft_rrb_builder_append(ft_rrb_builder* builder, const void* value);
/* Appends a range of elements. */
ft_status ft_rrb_builder_append_range(ft_rrb_builder* builder, const void* values, size_t count);
/* Copies the elements out into a vector. */
ft_status ft_rrb_builder_to_vector(ft_rrb_builder* builder, ft_rrb_vector* result);
/* Produces an empty builder retaining the same policies. */
void ft_rrb_builder_clear(ft_rrb_builder* builder);

#ifdef __cplusplus
}
#endif

#endif
