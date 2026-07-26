/*
 * The FingerTree workspace's public C API: measured trees and everything built on them.
 *
 * An ft_tree is a persistent sequence caching a monoidal measure at every node. Because each node's
 * measure is readable without descending into it, one generic split answers any monotone question
 * the measure can express without visiting the elements it skips. The deques, ropes, sorted
 * collections, priority queues, and interval trees declared here are all that one tree under
 * different measures.
 *
 * The API uses opaque handles plus explicit policy callbacks rather than C++ templates. Every
 * operation returns a new version and leaves its inputs valid, sharing unchanged structure, so an
 * edit copies a path rather than the whole collection. Handle lifetime, ownership, and the
 * allocation-failure boundary are set out in docs/api-notes.md.
 */

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

/* The shared, reference-counted representation behind an ft_tree handle. Opaque: handles are the
 * API. */
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

/* Initializes the value type in place. */
void ft_value_type_init(ft_value_type* type, size_t size);
/* Initializes the policy in place. */
void ft_size_measure_policy_init(ft_measure_policy* policy);
/* Initializes a tree policy whose measure counts elements, which is what makes the tree indexable
 * by position. */
void ft_tree_policy_init_size(ft_tree_policy* policy, const ft_value_type* value_type);

/* Initializes the tree in place. */
ft_status ft_tree_init(ft_tree* tree, const ft_tree_policy* policy);
/* Initializes a second handle on the same tree version, taking a reference rather than copying. */
ft_status ft_tree_copy(const ft_tree* source, ft_tree* destination);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_tree_dispose(ft_tree* tree);

/* Whether the tree holds no elements. */
bool ft_tree_empty(const ft_tree* tree);
/* Number of elements in the tree. */
size_t ft_tree_size(const ft_tree* tree);
/* The combined measure of every element, read from the cached root measure. */
ft_status ft_tree_measure(const ft_tree* tree, void* destination);

/* Reads the first element. */
ft_status ft_tree_front(const ft_tree* tree, void* destination);
/* Reads the last element. */
ft_status ft_tree_back(const ft_tree* tree, void* destination);
/* Reads the element at the given position. */
ft_status ft_tree_at(const ft_tree* tree, size_t index, void* destination);

/* Produces a tree with the element added at the front. */
ft_status ft_tree_push_front(const ft_tree* tree, const void* value, ft_tree* result);
/* Produces a tree with the element added at the back. */
ft_status ft_tree_push_back(const ft_tree* tree, const void* value, ft_tree* result);
/* Produces a tree without its first element, reading that element out. */
ft_status ft_tree_pop_front(const ft_tree* tree, void* value, ft_tree* rest);
/* Produces a tree without its last element, reading that element out. */
ft_status ft_tree_pop_back(const ft_tree* tree, void* value, ft_tree* rest);
/* Produces the concatenation of two trees, sharing both operands' unchanged structure. */
ft_status ft_tree_concat(const ft_tree* left, const ft_tree* right, ft_tree* result);
/* Splits into the elements before the position and those from it onward. */
ft_status ft_tree_split_at(const ft_tree* tree, size_t index, ft_tree_split_result* result);
/* Produces a tree with the element at the position replaced. */
ft_status ft_tree_set_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result);
/* Produces a tree with the element inserted at the position. */
ft_status ft_tree_insert_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result);
/* Produces a tree without the element at the position. */
ft_status ft_tree_remove_at(const ft_tree* tree, size_t index, ft_tree* result);

/* Finds the element's position. */
ft_status ft_tree_locate(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    void* measure_before,
    void* value);

/* Splits the tree in two. */
ft_status ft_tree_split(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree* left,
    void* value,
    ft_tree* right);

/* Calls the visitor once per element, in the tree's own order. */
ft_status ft_tree_visit(const ft_tree* tree, ft_visit_fn visitor, void* context);

/* Immutable measure-aware gap cursor over one exact general tree snapshot.
 * The position field is representation state; general-tree callers navigate by
 * ordered measures and neighbors rather than treating it as a count contract. */
typedef struct ft_tree_cursor {
    ft_tree tree;
    size_t position;
} ft_tree_cursor;

/* Initializes a cursor before the first element. */
ft_status ft_tree_get_cursor_at_start(const ft_tree* tree, ft_tree_cursor* result);
/* Initializes a cursor after the last element. */
ft_status ft_tree_get_cursor_at_end(const ft_tree* tree, ft_tree_cursor* result);
/* Initializes a cursor at the first gap where the measure satisfies the predicate, descending by
 * cached measures rather than scanning. */
ft_status ft_tree_get_cursor_by_measure(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_tree_cursor_copy(const ft_tree_cursor* source, ft_tree_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_tree_cursor_move(ft_tree_cursor* destination, ft_tree_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_tree_cursor_dispose(ft_tree_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_tree_cursor_valid(const ft_tree_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_tree_cursor_is_at_start(const ft_tree_cursor* cursor, bool* result);
/* Whether the gap follows the last element. */
ft_status ft_tree_cursor_is_at_end(const ft_tree_cursor* cursor, bool* result);
/* The combined measure of everything before the gap. */
ft_status ft_tree_cursor_measure_before(const ft_tree_cursor* cursor, void* destination);
/* The combined measure of everything after the gap. */
ft_status ft_tree_cursor_measure_after(const ft_tree_cursor* cursor, void* destination);
/* Reads the element immediately before the gap, reporting whether one exists. */
ft_status ft_tree_cursor_try_peek_previous(
    const ft_tree_cursor* cursor,
    bool* found,
    void* value);
/* Reads the element immediately after the gap, reporting whether one exists. */
ft_status ft_tree_cursor_try_peek_next(
    const ft_tree_cursor* cursor,
    bool* found,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_tree_cursor_move_previous(const ft_tree_cursor* cursor, ft_tree_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_tree_cursor_move_next(const ft_tree_cursor* cursor, ft_tree_cursor* result);
/* Moves the cursor to the first gap where the measure satisfies the predicate. */
ft_status ft_tree_cursor_seek_by_measure(
    const ft_tree_cursor* cursor,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree_cursor* result);
/* Inserts an element at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_tree_cursor_insert(
    const ft_tree_cursor* cursor,
    const void* value,
    ft_tree_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_tree_cursor_delete_previous(const ft_tree_cursor* cursor, ft_tree_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_tree_cursor_delete_next(const ft_tree_cursor* cursor, ft_tree_cursor* result);
/* Replaces the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_tree_cursor_replace_next(
    const ft_tree_cursor* cursor,
    const void* value,
    ft_tree_cursor* result);
/* Initializes a handle on the tree version this cursor is positioned in. */
ft_status ft_tree_cursor_snapshot(const ft_tree_cursor* cursor, ft_tree* result);

/* A persistent catenable deque, the measure-free view of an ft_tree. */
typedef ft_tree ft_persistent_deque;
/* A gap cursor over one persistent deque version. */
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
/* Number of elements in the deque version the cursor is positioned in. */
size_t ft_persistent_deque_cursor_size(const ft_persistent_deque_cursor* cursor);
/* The cursor's gap position. */
size_t ft_persistent_deque_cursor_position(const ft_persistent_deque_cursor* cursor);
/* Moves the cursor to the given position within the same deque version. */
ft_status ft_persistent_deque_cursor_seek(
    const ft_persistent_deque_cursor* cursor,
    size_t position,
    ft_persistent_deque_cursor* result);
/* Produces a deque with the array's elements inserted at the position. */
ft_status ft_persistent_deque_cursor_insert_array(
    const ft_persistent_deque_cursor* cursor,
    const void* values,
    size_t count,
    ft_persistent_deque_cursor* result);
/* Inserts a deque's elements at the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_persistent_deque_cursor_insert_deque(
    const ft_persistent_deque_cursor* cursor,
    const ft_persistent_deque* values,
    ft_persistent_deque_cursor* result);

/* The shared representation behind a reversible deque, carrying the orientation flag that makes
 * reversal a constant-time operation. */
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

/* Initializes the deque in place. */
ft_status ft_reversible_deque_init(ft_reversible_deque* deque, const ft_tree_policy* policy);
/* Initializes a second handle on the same deque version, taking a reference rather than copying. */
ft_status ft_reversible_deque_copy(const ft_reversible_deque* source, ft_reversible_deque* destination);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_reversible_deque_dispose(ft_reversible_deque* deque);
/* Whether the deque holds no elements. */
bool ft_reversible_deque_empty(const ft_reversible_deque* deque);
/* Number of elements in the deque. */
size_t ft_reversible_deque_size(const ft_reversible_deque* deque);
/* Produces the deque in the opposite order. */
ft_status ft_reversible_deque_reverse(const ft_reversible_deque* deque, ft_reversible_deque* result);
/* Reads the element at the given position. */
ft_status ft_reversible_deque_at(const ft_reversible_deque* deque, size_t index, void* destination);
/* Reads the first element. */
ft_status ft_reversible_deque_front(const ft_reversible_deque* deque, void* destination);
/* Reads the last element. */
ft_status ft_reversible_deque_back(const ft_reversible_deque* deque, void* destination);
/* Produces a deque with the element added at the front. */
ft_status ft_reversible_deque_push_front(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result);
/* Produces a deque with the element added at the back. */
ft_status ft_reversible_deque_push_back(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result);
/* Produces a deque without its first element, reading that element out. */
ft_status ft_reversible_deque_pop_front(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest);
/* Produces a deque without its last element, reading that element out. */
ft_status ft_reversible_deque_pop_back(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest);
/* Produces the concatenation of two deques, sharing both operands' unchanged structure. */
ft_status ft_reversible_deque_concat(
    const ft_reversible_deque* left,
    const ft_reversible_deque* right,
    ft_reversible_deque* result);
/* Splits into the elements before the position and those from it onward. */
ft_status ft_reversible_deque_split_at(
    const ft_reversible_deque* deque,
    size_t index,
    ft_reversible_deque_split_result* result);
/* Produces a deque with the element at the position replaced. */
ft_status ft_reversible_deque_set_at(
    const ft_reversible_deque* deque,
    size_t index,
    const void* value,
    ft_reversible_deque* result);
/* Produces a deque with the element inserted at the position. */
ft_status ft_reversible_deque_insert_at(
    const ft_reversible_deque* deque,
    size_t index,
    const void* value,
    ft_reversible_deque* result);
/* Produces a deque without the element at the position. */
ft_status ft_reversible_deque_remove_at(const ft_reversible_deque* deque, size_t index, ft_reversible_deque* result);
/* Calls the visitor once per element, in the deque's own order. */
ft_status ft_reversible_deque_visit(const ft_reversible_deque* deque, ft_visit_fn visitor, void* context);

/* Initializes a cursor at the given gap of the deque. */
ft_status ft_reversible_deque_get_cursor(
    const ft_reversible_deque* deque,
    size_t position,
    ft_reversible_deque_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_reversible_deque_cursor_copy(
    const ft_reversible_deque_cursor* source,
    ft_reversible_deque_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_reversible_deque_cursor_move(
    ft_reversible_deque_cursor* destination,
    ft_reversible_deque_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_reversible_deque_cursor_dispose(ft_reversible_deque_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_reversible_deque_cursor_valid(const ft_reversible_deque_cursor* cursor);
/* Whether the deque version the cursor is positioned in holds no elements. */
bool ft_reversible_deque_cursor_empty(const ft_reversible_deque_cursor* cursor);
/* Number of elements in the deque version the cursor is positioned in. */
size_t ft_reversible_deque_cursor_size(const ft_reversible_deque_cursor* cursor);
/* The cursor's gap position. */
size_t ft_reversible_deque_cursor_position(const ft_reversible_deque_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_reversible_deque_cursor_is_at_start(
    const ft_reversible_deque_cursor* cursor,
    bool* result);
/* Whether the gap follows the last element. */
ft_status ft_reversible_deque_cursor_is_at_end(
    const ft_reversible_deque_cursor* cursor,
    bool* result);
/* Reads the element immediately before the gap, reporting whether one exists. */
ft_status ft_reversible_deque_cursor_try_peek_previous(
    const ft_reversible_deque_cursor* cursor,
    bool* found,
    void* value);
/* Reads the element immediately after the gap, reporting whether one exists. */
ft_status ft_reversible_deque_cursor_try_peek_next(
    const ft_reversible_deque_cursor* cursor,
    bool* found,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_reversible_deque_cursor_move_previous(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_reversible_deque_cursor_move_next(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
/* Moves the cursor to the given position within the same deque version. */
ft_status ft_reversible_deque_cursor_seek(
    const ft_reversible_deque_cursor* cursor,
    size_t position,
    ft_reversible_deque_cursor* result);
/* Inserts an element at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_reversible_deque_cursor_insert(
    const ft_reversible_deque_cursor* cursor,
    const void* value,
    ft_reversible_deque_cursor* result);
/* Produces a deque with the array's elements inserted at the position. */
ft_status ft_reversible_deque_cursor_insert_array(
    const ft_reversible_deque_cursor* cursor,
    const void* values,
    size_t count,
    ft_reversible_deque_cursor* result);
/* Inserts a deque's elements at the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_reversible_deque_cursor_insert_deque(
    const ft_reversible_deque_cursor* cursor,
    const ft_reversible_deque* values,
    ft_reversible_deque_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_reversible_deque_cursor_delete_previous(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_reversible_deque_cursor_delete_next(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
/* Replaces the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_reversible_deque_cursor_replace_next(
    const ft_reversible_deque_cursor* cursor,
    const void* value,
    ft_reversible_deque_cursor* result);
/* Produces the deque in the opposite order. */
ft_status ft_reversible_deque_cursor_reverse(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque_cursor* result);
/* Initializes a handle on the deque version this cursor is positioned in. */
ft_status ft_reversible_deque_cursor_snapshot(
    const ft_reversible_deque_cursor* cursor,
    ft_reversible_deque* result);

typedef struct ft_sorted_multiset {
    ft_tree tree;
    ft_compare_fn compare;
    void* compare_context;
} ft_sorted_multiset;

/* A persistent sorted set over a caller-owned policy, which must outlive every handle. */
typedef ft_sorted_multiset ft_sorted_set;

/* Initializes the bag in place. */
ft_status ft_sorted_multiset_init(
    ft_sorted_multiset* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context);
/* Initializes a second handle on the same bag version, taking a reference rather than copying. */
ft_status ft_sorted_multiset_copy(const ft_sorted_multiset* source, ft_sorted_multiset* destination);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_sorted_multiset_dispose(ft_sorted_multiset* set);
/* Number of elements in the bag. */
size_t ft_sorted_multiset_size(const ft_sorted_multiset* set);
/* Whether the bag holds no elements. */
bool ft_sorted_multiset_empty(const ft_sorted_multiset* set);
/* Produces a bag containing the given element. */
ft_status ft_sorted_multiset_add(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result);
/* Produces a bag with one occurrence of the element removed, leaving any duplicates. */
ft_status ft_sorted_multiset_remove_one(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result);
/* Whether the element is present. */
bool ft_sorted_multiset_contains(const ft_sorted_multiset* set, const void* value);
/* How many times the element occurs. */
size_t ft_sorted_multiset_count_of(const ft_sorted_multiset* set, const void* value);
/* Reads the element at the given position. */
ft_status ft_sorted_multiset_at(const ft_sorted_multiset* set, size_t index, void* destination);
/* Calls the visitor once per element, in the bag's own order. */
ft_status ft_sorted_multiset_visit(const ft_sorted_multiset* set, ft_visit_fn visitor, void* context);

/* Immutable root-plus-rank gap cursor over one exact sorted-multiset snapshot. */
typedef struct ft_sorted_multiset_cursor {
    ft_sorted_multiset set;
    size_t position;
} ft_sorted_multiset_cursor;

/* Initializes a cursor at the given gap of the bag. */
ft_status ft_sorted_multiset_get_cursor(
    const ft_sorted_multiset* set,
    size_t position,
    ft_sorted_multiset_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_sorted_multiset_get_cursor_lower_bound(
    const ft_sorted_multiset* set,
    const void* value,
    ft_sorted_multiset_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_sorted_multiset_get_cursor_upper_bound(
    const ft_sorted_multiset* set,
    const void* value,
    ft_sorted_multiset_cursor* result);
/* Initializes a cursor at the element, reporting whether it was actually present; on a miss the
 * cursor sits at the insertion point. */
ft_status ft_sorted_multiset_get_cursor_at_item(
    const ft_sorted_multiset* set,
    const void* value,
    bool* found,
    ft_sorted_multiset_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_sorted_multiset_cursor_copy(
    const ft_sorted_multiset_cursor* source,
    ft_sorted_multiset_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_sorted_multiset_cursor_move(
    ft_sorted_multiset_cursor* destination,
    ft_sorted_multiset_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_sorted_multiset_cursor_dispose(ft_sorted_multiset_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_sorted_multiset_cursor_valid(const ft_sorted_multiset_cursor* cursor);
/* Whether the bag version the cursor is positioned in holds no elements. */
bool ft_sorted_multiset_cursor_empty(const ft_sorted_multiset_cursor* cursor);
/* Number of elements in the bag version the cursor is positioned in. */
size_t ft_sorted_multiset_cursor_size(const ft_sorted_multiset_cursor* cursor);
/* The cursor's gap position. */
size_t ft_sorted_multiset_cursor_position(const ft_sorted_multiset_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_sorted_multiset_cursor_is_at_start(
    const ft_sorted_multiset_cursor* cursor,
    bool* result);
/* Whether the gap follows the last element. */
ft_status ft_sorted_multiset_cursor_is_at_end(
    const ft_sorted_multiset_cursor* cursor,
    bool* result);
/* Reads the element immediately before the gap, reporting whether one exists. */
ft_status ft_sorted_multiset_cursor_try_peek_previous(
    const ft_sorted_multiset_cursor* cursor,
    bool* found,
    void* value);
/* Reads the element immediately after the gap, reporting whether one exists. */
ft_status ft_sorted_multiset_cursor_try_peek_next(
    const ft_sorted_multiset_cursor* cursor,
    bool* found,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_sorted_multiset_cursor_move_previous(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_sorted_multiset_cursor_move_next(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_sorted_multiset_cursor_seek_rank(
    const ft_sorted_multiset_cursor* cursor,
    size_t position,
    ft_sorted_multiset_cursor* result);
/* Produces a bag containing the given element. */
ft_status ft_sorted_multiset_cursor_add(
    const ft_sorted_multiset_cursor* cursor,
    const void* value,
    ft_sorted_multiset_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_sorted_multiset_cursor_delete_previous(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_sorted_multiset_cursor_delete_next(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset_cursor* result);
/* Initializes a handle on the bag version this cursor is positioned in. */
ft_status ft_sorted_multiset_cursor_snapshot(
    const ft_sorted_multiset_cursor* cursor,
    ft_sorted_multiset* result);

/* Initializes the set in place. */
ft_status ft_sorted_set_init(
    ft_sorted_set* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context);
/* Initializes a second handle on the same set version, taking a reference rather than copying. */
ft_status ft_sorted_set_copy(const ft_sorted_set* source, ft_sorted_set* destination);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_sorted_set_dispose(ft_sorted_set* set);
/* Number of elements in the set. */
size_t ft_sorted_set_size(const ft_sorted_set* set);
/* Whether the set holds no elements. */
bool ft_sorted_set_empty(const ft_sorted_set* set);
/* Produces a set containing the given element. */
ft_status ft_sorted_set_add(const ft_sorted_set* set, const void* value, ft_sorted_set* result);
/* Produces a set without that element. */
ft_status ft_sorted_set_remove(const ft_sorted_set* set, const void* value, ft_sorted_set* result);
/* Whether the element is present. */
bool ft_sorted_set_contains(const ft_sorted_set* set, const void* value);
/* Reads the element at the given position. */
ft_status ft_sorted_set_at(const ft_sorted_set* set, size_t index, void* destination);
/* Calls the visitor once per element, in the set's own order. */
ft_status ft_sorted_set_visit(const ft_sorted_set* set, ft_visit_fn visitor, void* context);

/* A gap cursor over one sorted set version. */
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

/* Per-map state describing how a key-value entry is copied and destroyed. */
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

/* Initializes the map in place. */
ft_status ft_sorted_map_init(
    ft_sorted_map* map,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_key,
    void* compare_context);
/* Initializes a second handle on the same map version, taking a reference rather than copying. */
ft_status ft_sorted_map_copy(const ft_sorted_map* source, ft_sorted_map* destination);
/* Relocates an initialized map into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_sorted_map_move(ft_sorted_map* destination, ft_sorted_map* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_sorted_map_dispose(ft_sorted_map* map);
/* Whether the map holds no entries. */
bool ft_sorted_map_empty(const ft_sorted_map* map);
/* Number of entries in the map. */
size_t ft_sorted_map_size(const ft_sorted_map* map);
/* Whether the key is present. */
bool ft_sorted_map_contains_key(const ft_sorted_map* map, const void* key);
/* Reads the value stored for the key, reporting whether it was present. */
ft_status ft_sorted_map_try_get(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    void* value);
/* The rank of the given key. */
ft_status ft_sorted_map_index_of_key(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    size_t* index);
/* Reads the entry at the given rank. */
ft_status ft_sorted_map_entry_at(
    const ft_sorted_map* map,
    size_t index,
    void* key,
    void* value);
/* Produces a map with the key bound to the value, adding or replacing as needed. */
ft_status ft_sorted_map_set(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map* result);
/* Inserts the entry unless an equivalent one is present, reporting which happened. */
ft_status ft_sorted_map_try_insert(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    bool* inserted,
    ft_sorted_map* result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_sorted_map_insert(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map* result);
/* Produces a map without that entry. */
ft_status ft_sorted_map_remove(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map* result);
/* Calls the visitor once per entry, in the map's own order. */
ft_status ft_sorted_map_visit(const ft_sorted_map* map, ft_sorted_map_visit_fn visitor, void* context);

typedef struct ft_sorted_map_cursor {
    ft_sorted_map map;
    size_t position;
} ft_sorted_map_cursor;

/* Initializes a cursor at the given gap of the map. */
ft_status ft_sorted_map_get_cursor(
    const ft_sorted_map* map,
    size_t position,
    ft_sorted_map_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_sorted_map_get_cursor_lower_bound(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_sorted_map_get_cursor_upper_bound(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map_cursor* result);
/* Initializes a cursor at the key, reporting whether it was actually present; on a miss the cursor
 * sits at the insertion point. */
ft_status ft_sorted_map_get_cursor_at_key(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    ft_sorted_map_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_sorted_map_cursor_copy(
    const ft_sorted_map_cursor* source,
    ft_sorted_map_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_sorted_map_cursor_move(
    ft_sorted_map_cursor* destination,
    ft_sorted_map_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_sorted_map_cursor_dispose(ft_sorted_map_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_sorted_map_cursor_valid(const ft_sorted_map_cursor* cursor);
/* Whether the map version the cursor is positioned in holds no entries. */
bool ft_sorted_map_cursor_empty(const ft_sorted_map_cursor* cursor);
/* Number of entries in the map version the cursor is positioned in. */
size_t ft_sorted_map_cursor_size(const ft_sorted_map_cursor* cursor);
/* The cursor's gap position. */
size_t ft_sorted_map_cursor_position(const ft_sorted_map_cursor* cursor);
/* Whether the gap precedes the first entry. */
ft_status ft_sorted_map_cursor_is_at_start(const ft_sorted_map_cursor* cursor, bool* result);
/* Whether the gap follows the last entry. */
ft_status ft_sorted_map_cursor_is_at_end(const ft_sorted_map_cursor* cursor, bool* result);
/* Reads the entry immediately before the gap, reporting whether one exists. */
ft_status ft_sorted_map_cursor_try_peek_previous(
    const ft_sorted_map_cursor* cursor,
    bool* found,
    void* key,
    void* value);
/* Reads the entry immediately after the gap, reporting whether one exists. */
ft_status ft_sorted_map_cursor_try_peek_next(
    const ft_sorted_map_cursor* cursor,
    bool* found,
    void* key,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_sorted_map_cursor_move_previous(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_sorted_map_cursor_move_next(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_sorted_map_cursor_seek_rank(
    const ft_sorted_map_cursor* cursor,
    size_t position,
    ft_sorted_map_cursor* result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_sorted_map_cursor_insert(
    const ft_sorted_map_cursor* cursor,
    const void* key,
    const void* value,
    ft_sorted_map_cursor* result);
/* Inserts the entry unless an equivalent one is present, reporting which happened. */
ft_status ft_sorted_map_cursor_try_insert(
    const ft_sorted_map_cursor* cursor,
    const void* key,
    const void* value,
    bool* inserted,
    ft_sorted_map_cursor* result);
/* Produces a map with the key bound to the value, adding or replacing as needed. */
ft_status ft_sorted_map_cursor_set(
    const ft_sorted_map_cursor* cursor,
    const void* key,
    const void* value,
    ft_sorted_map_cursor* result);
/* Replaces the value of the entry after the gap, producing a new version the cursor is then
 * positioned in. */
ft_status ft_sorted_map_cursor_set_next_value(
    const ft_sorted_map_cursor* cursor,
    const void* value,
    ft_sorted_map_cursor* result);
/* Removes the entry before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_sorted_map_cursor_delete_previous(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
/* Removes the entry after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_sorted_map_cursor_delete_next(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map_cursor* result);
/* Initializes a handle on the map version this cursor is positioned in. */
ft_status ft_sorted_map_cursor_snapshot(
    const ft_sorted_map_cursor* cursor,
    ft_sorted_map* result);

/* Per-rope state describing how elements are grouped into chunks. */
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

/* Initializes the rope in place. */
ft_status ft_rope_init(ft_rope* rope, const ft_value_type* value_type);
/* Initializes a rope holding the given elements, built in bulk rather than by repeated
 * insertion. */
ft_status ft_rope_from_array(
    ft_rope* rope,
    const ft_value_type* value_type,
    const void* values,
    size_t count);
/* Initializes a second handle on the same rope version, taking a reference rather than copying. */
ft_status ft_rope_copy(const ft_rope* source, ft_rope* destination);
/* Relocates an initialized rope into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_rope_move(ft_rope* destination, ft_rope* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_rope_dispose(ft_rope* rope);
/* Whether the rope holds no elements. */
bool ft_rope_empty(const ft_rope* rope);
/* Number of elements in the rope. */
size_t ft_rope_size(const ft_rope* rope);
/* Reads the number of elements, reporting failure rather than trapping on an uninitialized
 * handle. */
ft_status ft_rope_try_size(const ft_rope* rope, size_t* size);
/* Reads the element at the given position. */
ft_status ft_rope_at(const ft_rope* rope, size_t index, void* destination);
/* Produces a rope with the element added at the back. */
ft_status ft_rope_push_back(const ft_rope* rope, const void* value, ft_rope* result);
/* Produces a rope with the element inserted at the position. */
ft_status ft_rope_insert_at(const ft_rope* rope, size_t index, const void* value, ft_rope* result);
/* Produces a rope without the element at the position. */
ft_status ft_rope_remove_at(const ft_rope* rope, size_t index, ft_rope* result);
/* Splits into the elements before the position and those from it onward. */
ft_status ft_rope_split_at(const ft_rope* rope, size_t index, ft_rope_split_result* result);
/* Produces the concatenation of two ropes, sharing both operands' unchanged structure. */
ft_status ft_rope_concat(const ft_rope* left, const ft_rope* right, ft_rope* result);
/* Calls the visitor once per element, in the rope's own order. */
ft_status ft_rope_visit(const ft_rope* rope, ft_visit_fn visitor, void* context);

/* Initializes a cursor at the given gap of the rope. */
ft_status ft_rope_get_cursor(const ft_rope* rope, size_t position, ft_rope_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_rope_cursor_copy(const ft_rope_cursor* source, ft_rope_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_rope_cursor_move(ft_rope_cursor* destination, ft_rope_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_rope_cursor_dispose(ft_rope_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_rope_cursor_valid(const ft_rope_cursor* cursor);
/* Whether the rope version the cursor is positioned in holds no elements. */
bool ft_rope_cursor_empty(const ft_rope_cursor* cursor);
/* Number of elements in the rope version the cursor is positioned in. */
size_t ft_rope_cursor_size(const ft_rope_cursor* cursor);
/* Reads the number of elements, reporting failure rather than trapping on an uninitialized
 * handle. */
ft_status ft_rope_cursor_try_size(const ft_rope_cursor* cursor, size_t* size);
/* The cursor's gap position. */
size_t ft_rope_cursor_position(const ft_rope_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_rope_cursor_is_at_start(const ft_rope_cursor* cursor, bool* result);
/* Whether the gap follows the last element. */
ft_status ft_rope_cursor_is_at_end(const ft_rope_cursor* cursor, bool* result);
/* Reads the element immediately before the gap, reporting whether one exists. */
ft_status ft_rope_cursor_try_peek_previous(
    const ft_rope_cursor* cursor,
    bool* found,
    void* value);
/* Reads the element immediately after the gap, reporting whether one exists. */
ft_status ft_rope_cursor_try_peek_next(
    const ft_rope_cursor* cursor,
    bool* found,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_rope_cursor_move_previous(const ft_rope_cursor* cursor, ft_rope_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_rope_cursor_move_next(const ft_rope_cursor* cursor, ft_rope_cursor* result);
/* Moves the cursor to the given position within the same rope version. */
ft_status ft_rope_cursor_seek(
    const ft_rope_cursor* cursor,
    size_t position,
    ft_rope_cursor* result);
/* Inserts an element at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rope_cursor_insert(
    const ft_rope_cursor* cursor,
    const void* value,
    ft_rope_cursor* result);
/* Produces a rope with the array's elements inserted at the position. */
ft_status ft_rope_cursor_insert_array(
    const ft_rope_cursor* cursor,
    const void* values,
    size_t count,
    ft_rope_cursor* result);
/* Inserts a rope's characters at the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_rope_cursor_insert_rope(
    const ft_rope_cursor* cursor,
    const ft_rope* values,
    ft_rope_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rope_cursor_delete_previous(const ft_rope_cursor* cursor, ft_rope_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rope_cursor_delete_next(const ft_rope_cursor* cursor, ft_rope_cursor* result);
/* Replaces the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_rope_cursor_replace_next(
    const ft_rope_cursor* cursor,
    const void* value,
    ft_rope_cursor* result);
/* Initializes a handle on the rope version this cursor is positioned in. */
ft_status ft_rope_cursor_snapshot(const ft_rope_cursor* cursor, ft_rope* result);

/* Per-rope state describing chunking together with the measure policy applied to each chunk. */
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

/* Initializes the rope in place. */
ft_status ft_measured_rope_init(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure);
/* Initializes a rope holding the given elements, built in bulk rather than by repeated
 * insertion. */
ft_status ft_measured_rope_from_array(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure,
    const void* values,
    size_t count);
/* Initializes a second handle on the same rope version, taking a reference rather than copying. */
ft_status ft_measured_rope_copy(const ft_measured_rope* source, ft_measured_rope* destination);
/* Relocates an initialized rope into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_measured_rope_move(ft_measured_rope* destination, ft_measured_rope* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_measured_rope_dispose(ft_measured_rope* rope);
/* Whether the rope holds no elements. */
bool ft_measured_rope_empty(const ft_measured_rope* rope);
/* Number of elements in the rope. */
size_t ft_measured_rope_size(const ft_measured_rope* rope);
/* Reads the number of elements, reporting failure rather than trapping on an uninitialized
 * handle. */
ft_status ft_measured_rope_try_size(const ft_measured_rope* rope, size_t* size);
/* The combined measure of every element, read from the cached root measure. */
ft_status ft_measured_rope_measure(const ft_measured_rope* rope, void* destination);
/* The combined measure of the elements before the position. */
ft_status ft_measured_rope_prefix_measure(const ft_measured_rope* rope, size_t count, void* destination);
/* Reads the element at the given position. */
ft_status ft_measured_rope_at(const ft_measured_rope* rope, size_t index, void* destination);
/* Produces a rope with the element added at the back. */
ft_status ft_measured_rope_push_back(const ft_measured_rope* rope, const void* value, ft_measured_rope* result);
/* Produces a rope with the element inserted at the position. */
ft_status ft_measured_rope_insert_at(
    const ft_measured_rope* rope,
    size_t index,
    const void* value,
    ft_measured_rope* result);
/* Produces a rope without the element at the position. */
ft_status ft_measured_rope_remove_at(const ft_measured_rope* rope, size_t index, ft_measured_rope* result);
/* Splits into the elements before the position and those from it onward. */
ft_status ft_measured_rope_split_at(
    const ft_measured_rope* rope,
    size_t index,
    ft_measured_rope_split_result* result);
/* Splits at the first point where the accumulated measure satisfies the predicate, descending by
 * cached measures rather than scanning. */
ft_status ft_measured_rope_split_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    ft_measured_rope_split_result* result);
/* Finds the first position where the accumulated measure satisfies the predicate. */
ft_status ft_measured_rope_locate_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    size_t* index,
    void* measure_before,
    void* value);
/* Produces the concatenation of two ropes, sharing both operands' unchanged structure. */
ft_status ft_measured_rope_concat(
    const ft_measured_rope* left,
    const ft_measured_rope* right,
    ft_measured_rope* result);
/* Calls the visitor once per element, in the rope's own order. */
ft_status ft_measured_rope_visit(const ft_measured_rope* rope, ft_visit_fn visitor, void* context);

typedef struct ft_measured_rope_cursor {
    ft_measured_rope rope;
    size_t position;
} ft_measured_rope_cursor;

/* Initializes a cursor at the given gap of the rope. */
ft_status ft_measured_rope_get_cursor(
    const ft_measured_rope* rope,
    size_t position,
    ft_measured_rope_cursor* result);
/* Initializes a cursor at the first gap where the measure satisfies the predicate, descending by
 * cached measures rather than scanning. */
ft_status ft_measured_rope_get_cursor_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_measured_rope_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_measured_rope_cursor_copy(
    const ft_measured_rope_cursor* source,
    ft_measured_rope_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_measured_rope_cursor_move(
    ft_measured_rope_cursor* destination,
    ft_measured_rope_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_measured_rope_cursor_dispose(ft_measured_rope_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_measured_rope_cursor_valid(const ft_measured_rope_cursor* cursor);
/* Whether the rope version the cursor is positioned in holds no elements. */
bool ft_measured_rope_cursor_empty(const ft_measured_rope_cursor* cursor);
/* Number of elements in the rope version the cursor is positioned in. */
size_t ft_measured_rope_cursor_size(const ft_measured_rope_cursor* cursor);
/* Reads the number of elements, reporting failure rather than trapping on an uninitialized
 * handle. */
ft_status ft_measured_rope_cursor_try_size(const ft_measured_rope_cursor* cursor, size_t* size);
/* The cursor's gap position. */
size_t ft_measured_rope_cursor_position(const ft_measured_rope_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_measured_rope_cursor_is_at_start(const ft_measured_rope_cursor* cursor, bool* result);
/* Whether the gap follows the last element. */
ft_status ft_measured_rope_cursor_is_at_end(const ft_measured_rope_cursor* cursor, bool* result);
/* The combined measure of everything before the gap. */
ft_status ft_measured_rope_cursor_measure_before(
    const ft_measured_rope_cursor* cursor,
    void* destination);
/* The combined measure of everything after the gap. */
ft_status ft_measured_rope_cursor_measure_after(
    const ft_measured_rope_cursor* cursor,
    void* destination);
/* Reads the element immediately before the gap, reporting whether one exists. */
ft_status ft_measured_rope_cursor_try_peek_previous(
    const ft_measured_rope_cursor* cursor,
    bool* found,
    void* value);
/* Reads the element immediately after the gap, reporting whether one exists. */
ft_status ft_measured_rope_cursor_try_peek_next(
    const ft_measured_rope_cursor* cursor,
    bool* found,
    void* value);
/* Moves the cursor one position earlier. */
ft_status ft_measured_rope_cursor_move_previous(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_measured_rope_cursor_move_next(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
/* Moves the cursor to the given position within the same rope version. */
ft_status ft_measured_rope_cursor_seek(
    const ft_measured_rope_cursor* cursor,
    size_t position,
    ft_measured_rope_cursor* result);
/* Moves the cursor to the first gap where the measure satisfies the predicate. */
ft_status ft_measured_rope_cursor_seek_by_measure(
    const ft_measured_rope_cursor* cursor,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_measured_rope_cursor* result);
/* Inserts an element at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_measured_rope_cursor_insert(
    const ft_measured_rope_cursor* cursor,
    const void* value,
    ft_measured_rope_cursor* result);
/* Produces a rope with the array's elements inserted at the position. */
ft_status ft_measured_rope_cursor_insert_array(
    const ft_measured_rope_cursor* cursor,
    const void* values,
    size_t count,
    ft_measured_rope_cursor* result);
/* Inserts a rope's characters at the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_measured_rope_cursor_insert_rope(
    const ft_measured_rope_cursor* cursor,
    const ft_measured_rope* values,
    ft_measured_rope_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_measured_rope_cursor_delete_previous(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_measured_rope_cursor_delete_next(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope_cursor* result);
/* Replaces the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_measured_rope_cursor_replace_next(
    const ft_measured_rope_cursor* cursor,
    const void* value,
    ft_measured_rope_cursor* result);
/* Initializes a handle on the rope version this cursor is positioned in. */
ft_status ft_measured_rope_cursor_snapshot(
    const ft_measured_rope_cursor* cursor,
    ft_measured_rope* result);

/* Per-queue state describing how a priority entry is copied and destroyed. */
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

/* Initializes the queue in place. */
ft_status ft_priority_queue_init(
    ft_priority_queue* queue,
    const ft_value_type* value_type,
    const ft_value_type* priority_type,
    ft_compare_fn compare_priority,
    void* compare_context);
/* Initializes a second handle on the same queue version, taking a reference rather than copying. */
ft_status ft_priority_queue_copy(const ft_priority_queue* source, ft_priority_queue* destination);
/* Relocates an initialized queue into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_priority_queue_move(ft_priority_queue* destination, ft_priority_queue* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_priority_queue_dispose(ft_priority_queue* queue);
/* Whether the queue holds no entries. */
bool ft_priority_queue_empty(const ft_priority_queue* queue);
/* Number of entries in the queue. */
size_t ft_priority_queue_size(const ft_priority_queue* queue);
/* Produces a queue with the entry added. */
ft_status ft_priority_queue_push(
    const ft_priority_queue* queue,
    const void* value,
    const void* priority,
    ft_priority_queue* result);
/* Reads the next entry without removing it, reporting whether there was one. */
ft_status ft_priority_queue_try_peek(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority);
/* Removes one entry, reporting whether there was one. */
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

/* Initializes the tree in place. */
ft_status ft_interval_tree_i64_init(ft_interval_tree_i64* tree);
/* Initializes a second handle on the same tree version, taking a reference rather than copying. */
ft_status ft_interval_tree_i64_copy(const ft_interval_tree_i64* source, ft_interval_tree_i64* destination);
/* Relocates an initialized tree into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_interval_tree_i64_move(ft_interval_tree_i64* destination, ft_interval_tree_i64* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_interval_tree_i64_dispose(ft_interval_tree_i64* tree);
/* Whether the tree holds no intervals. */
bool ft_interval_tree_i64_empty(const ft_interval_tree_i64* tree);
/* Number of intervals in the tree. */
size_t ft_interval_tree_i64_size(const ft_interval_tree_i64* tree);
/* Inserts an interval at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_i64_insert(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result);
/* Produces a tree with one occurrence of the interval removed, leaving any duplicates. */
ft_status ft_interval_tree_i64_remove_one(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result);
/* Whether the interval is present. */
bool ft_interval_tree_i64_contains(const ft_interval_tree_i64* tree, ft_interval_i64 interval);
/* Finds an interval overlapping the probe, reporting whether one exists. */
ft_status ft_interval_tree_i64_try_find_overlap(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 query,
    bool* found,
    ft_interval_i64* overlap);
/* How many stored intervals overlap the probe. */
size_t ft_interval_tree_i64_count_overlaps(const ft_interval_tree_i64* tree, ft_interval_i64 query);
/* Reads the interval at the given position. */
ft_status ft_interval_tree_i64_at(const ft_interval_tree_i64* tree, size_t index, ft_interval_i64* destination);

typedef struct ft_interval_tree_i64_cursor {
    ft_interval_tree_i64 tree;
    size_t position;
} ft_interval_tree_i64_cursor;

/* Initializes a cursor at the given gap of the tree. */
ft_status ft_interval_tree_i64_get_cursor(
    const ft_interval_tree_i64* tree,
    size_t position,
    ft_interval_tree_i64_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_interval_tree_i64_get_cursor_lower_bound(
    const ft_interval_tree_i64* tree,
    int64_t low,
    ft_interval_tree_i64_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_interval_tree_i64_get_cursor_upper_bound(
    const ft_interval_tree_i64* tree,
    int64_t low,
    ft_interval_tree_i64_cursor* result);
/* Initializes a cursor at the interval, reporting whether it was actually present. */
ft_status ft_interval_tree_i64_get_cursor_at_interval(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    bool* found,
    ft_interval_tree_i64_cursor* result);
/* Initializes a cursor at the first interval overlapping the probe. */
ft_status ft_interval_tree_i64_find_overlap_cursor(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 query,
    bool* found,
    ft_interval_tree_i64_cursor* result);
/* Initializes a cursor at the first interval containing the point. */
ft_status ft_interval_tree_i64_find_containing_cursor(
    const ft_interval_tree_i64* tree,
    int64_t point,
    bool* found,
    ft_interval_tree_i64_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_interval_tree_i64_cursor_copy(
    const ft_interval_tree_i64_cursor* source,
    ft_interval_tree_i64_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_interval_tree_i64_cursor_move(
    ft_interval_tree_i64_cursor* destination,
    ft_interval_tree_i64_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_interval_tree_i64_cursor_dispose(ft_interval_tree_i64_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_interval_tree_i64_cursor_valid(const ft_interval_tree_i64_cursor* cursor);
/* Whether the tree version the cursor is positioned in holds no intervals. */
bool ft_interval_tree_i64_cursor_empty(const ft_interval_tree_i64_cursor* cursor);
/* Number of intervals in the tree version the cursor is positioned in. */
size_t ft_interval_tree_i64_cursor_size(const ft_interval_tree_i64_cursor* cursor);
/* The cursor's gap position. */
size_t ft_interval_tree_i64_cursor_position(const ft_interval_tree_i64_cursor* cursor);
/* Whether the gap precedes the first interval. */
ft_status ft_interval_tree_i64_cursor_is_at_start(
    const ft_interval_tree_i64_cursor* cursor,
    bool* result);
/* Whether the gap follows the last interval. */
ft_status ft_interval_tree_i64_cursor_is_at_end(
    const ft_interval_tree_i64_cursor* cursor,
    bool* result);
/* Reads the interval immediately before the gap, reporting whether one exists. */
ft_status ft_interval_tree_i64_cursor_try_peek_previous(
    const ft_interval_tree_i64_cursor* cursor,
    bool* found,
    ft_interval_i64* interval);
/* Reads the interval immediately after the gap, reporting whether one exists. */
ft_status ft_interval_tree_i64_cursor_try_peek_next(
    const ft_interval_tree_i64_cursor* cursor,
    bool* found,
    ft_interval_i64* interval);
/* Moves the cursor one position earlier. */
ft_status ft_interval_tree_i64_cursor_move_previous(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_interval_tree_i64_cursor_move_next(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_interval_tree_i64_cursor_seek_rank(
    const ft_interval_tree_i64_cursor* cursor,
    size_t position,
    ft_interval_tree_i64_cursor* result);
/* Moves the cursor to the next interval overlapping the probe. */
ft_status ft_interval_tree_i64_cursor_seek_next_overlap(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_i64 query,
    bool* found,
    ft_interval_tree_i64_cursor* result);
/* Inserts an interval at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_i64_cursor_insert(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_i64 interval,
    ft_interval_tree_i64_cursor* result);
/* Removes the interval before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_i64_cursor_delete_previous(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
/* Removes the interval after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_i64_cursor_delete_next(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64_cursor* result);
/* Initializes a handle on the tree version this cursor is positioned in. */
ft_status ft_interval_tree_i64_cursor_snapshot(
    const ft_interval_tree_i64_cursor* cursor,
    ft_interval_tree_i64* result);

/* Per-tree state describing the interval endpoint type and its ordering. */
typedef struct ft_interval_tree_context ft_interval_tree_context;

typedef struct ft_interval_tree {
    ft_tree_policy policy;
    ft_sorted_multiset intervals;
    ft_value_type endpoint_type;
    ft_compare_fn compare_endpoint;
    void* compare_context;
    ft_interval_tree_context* interval_context;
} ft_interval_tree;

/* Initializes the tree in place. */
ft_status ft_interval_tree_init(
    ft_interval_tree* tree,
    const ft_value_type* endpoint_type,
    ft_compare_fn compare_endpoint,
    void* compare_context);
/* Initializes a second handle on the same tree version, taking a reference rather than copying. */
ft_status ft_interval_tree_copy(const ft_interval_tree* source, ft_interval_tree* destination);
/* Relocates an initialized tree into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_interval_tree_move(ft_interval_tree* destination, ft_interval_tree* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_interval_tree_dispose(ft_interval_tree* tree);
/* Whether the tree holds no intervals. */
bool ft_interval_tree_empty(const ft_interval_tree* tree);
/* Number of intervals in the tree. */
size_t ft_interval_tree_size(const ft_interval_tree* tree);
/* Inserts an interval at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_insert(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_tree* result);
/* Produces a tree with one occurrence of the interval removed, leaving any duplicates. */
ft_status ft_interval_tree_remove_one(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_tree* result);
/* Whether the interval is present. */
bool ft_interval_tree_contains(const ft_interval_tree* tree, const void* low, const void* high);
/* Finds an interval overlapping the probe, reporting whether one exists. */
ft_status ft_interval_tree_try_find_overlap(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high);
/* How many stored intervals overlap the probe. */
size_t ft_interval_tree_count_overlaps(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high);
/* Reads the interval at the given position. */
ft_status ft_interval_tree_at(
    const ft_interval_tree* tree,
    size_t index,
    void* low,
    void* high);

typedef struct ft_interval_tree_cursor {
    ft_interval_tree tree;
    size_t position;
} ft_interval_tree_cursor;

/* Initializes a cursor at the given gap of the tree. */
ft_status ft_interval_tree_get_cursor(
    const ft_interval_tree* tree,
    size_t position,
    ft_interval_tree_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_interval_tree_get_cursor_lower_bound(
    const ft_interval_tree* tree,
    const void* low,
    ft_interval_tree_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_interval_tree_get_cursor_upper_bound(
    const ft_interval_tree* tree,
    const void* low,
    ft_interval_tree_cursor* result);
/* Initializes a cursor at the interval, reporting whether it was actually present. */
ft_status ft_interval_tree_get_cursor_at_interval(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    bool* found,
    ft_interval_tree_cursor* result);
/* Initializes a cursor at the first interval overlapping the probe. */
ft_status ft_interval_tree_find_overlap_cursor(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_interval_tree_cursor* result);
/* Initializes a cursor at the first interval containing the point. */
ft_status ft_interval_tree_find_containing_cursor(
    const ft_interval_tree* tree,
    const void* point,
    bool* found,
    ft_interval_tree_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_interval_tree_cursor_copy(
    const ft_interval_tree_cursor* source,
    ft_interval_tree_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_interval_tree_cursor_move(
    ft_interval_tree_cursor* destination,
    ft_interval_tree_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_interval_tree_cursor_dispose(ft_interval_tree_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_interval_tree_cursor_valid(const ft_interval_tree_cursor* cursor);
/* Whether the tree version the cursor is positioned in holds no intervals. */
bool ft_interval_tree_cursor_empty(const ft_interval_tree_cursor* cursor);
/* Number of intervals in the tree version the cursor is positioned in. */
size_t ft_interval_tree_cursor_size(const ft_interval_tree_cursor* cursor);
/* The cursor's gap position. */
size_t ft_interval_tree_cursor_position(const ft_interval_tree_cursor* cursor);
/* Whether the gap precedes the first interval. */
ft_status ft_interval_tree_cursor_is_at_start(
    const ft_interval_tree_cursor* cursor,
    bool* result);
/* Whether the gap follows the last interval. */
ft_status ft_interval_tree_cursor_is_at_end(
    const ft_interval_tree_cursor* cursor,
    bool* result);
/* Reads the interval immediately before the gap, reporting whether one exists. */
ft_status ft_interval_tree_cursor_try_peek_previous(
    const ft_interval_tree_cursor* cursor,
    bool* found,
    void* low,
    void* high);
/* Reads the interval immediately after the gap, reporting whether one exists. */
ft_status ft_interval_tree_cursor_try_peek_next(
    const ft_interval_tree_cursor* cursor,
    bool* found,
    void* low,
    void* high);
/* Moves the cursor one position earlier. */
ft_status ft_interval_tree_cursor_move_previous(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_interval_tree_cursor_move_next(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_interval_tree_cursor_seek_rank(
    const ft_interval_tree_cursor* cursor,
    size_t position,
    ft_interval_tree_cursor* result);
/* Moves the cursor to the next interval overlapping the probe. Subtrees whose cached maximum
 * endpoint falls short of the probe are skipped whole. */
ft_status ft_interval_tree_cursor_seek_next_overlap(
    const ft_interval_tree_cursor* cursor,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_interval_tree_cursor* result);
/* Inserts an interval at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_cursor_insert(
    const ft_interval_tree_cursor* cursor,
    const void* low,
    const void* high,
    ft_interval_tree_cursor* result);
/* Removes the interval before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_cursor_delete_previous(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
/* Removes the interval after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_interval_tree_cursor_delete_next(
    const ft_interval_tree_cursor* cursor,
    ft_interval_tree_cursor* result);
/* Initializes a handle on the tree version this cursor is positioned in. */
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

/* Initializes the rope in place. */
ft_status ft_text_rope_init(ft_text_rope* rope);
/* Initializes a rope holding the given NUL-terminated text. */
ft_status ft_text_rope_from_cstr(const char* text, ft_text_rope* rope);
/* Initializes a second handle on the same rope version, taking a reference rather than copying. */
ft_status ft_text_rope_copy(const ft_text_rope* source, ft_text_rope* destination);
/* Relocates an initialized rope into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_text_rope_move(ft_text_rope* destination, ft_text_rope* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_text_rope_dispose(ft_text_rope* rope);
/* Number of characters in the rope. */
size_t ft_text_rope_size(const ft_text_rope* rope);
/* Reads the number of characters, reporting failure rather than trapping on an uninitialized
 * handle. */
ft_status ft_text_rope_try_size(const ft_text_rope* rope, size_t* size);
/* Reads the character at the given position. */
ft_status ft_text_rope_at(const ft_text_rope* rope, size_t index, char* value);
/* Inserts one character at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_text_rope_insert_char(const ft_text_rope* rope, size_t index, char value, ft_text_rope* result);
/* Produces a rope without the character at the position. */
ft_status ft_text_rope_remove_at(const ft_text_rope* rope, size_t index, ft_text_rope* result);
/* How many lines the text holds. Newline counts are cached in the measure, so this is a cached
 * read. */
size_t ft_text_rope_line_count(const ft_text_rope* rope);
/* Reads the line count, reporting failure rather than trapping on an uninitialized handle. */
ft_status ft_text_rope_try_line_count(const ft_text_rope* rope, size_t* count);
/* Which line the character offset falls on. */
ft_status ft_text_rope_line_of_offset(const ft_text_rope* rope, size_t offset, size_t* line);
/* The character offset where the given line begins. */
ft_status ft_text_rope_line_start_offset(const ft_text_rope* rope, size_t line, size_t* offset);
/* The line and column of a character offset. */
ft_status ft_text_rope_line_column_of(const ft_text_rope* rope, size_t offset, ft_line_column* result);
/* The character offset of a line and column. */
ft_status ft_text_rope_offset_of(const ft_text_rope* rope, size_t line, size_t column, size_t* offset);
/* Calls the visitor once per character, in the rope's own order. */
ft_status ft_text_rope_visit(const ft_text_rope* rope, ft_visit_fn visitor, void* context);
/* Initializes a cursor at the given gap of the rope. */
ft_status ft_text_rope_get_cursor(
    const ft_text_rope* rope,
    size_t position,
    ft_text_rope_cursor* result);
/* Initializes a cursor at the first gap where the measure satisfies the predicate, descending by
 * cached measures rather than scanning. */
ft_status ft_text_rope_get_cursor_by_measure(
    const ft_text_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_text_rope_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_text_rope_cursor_copy(
    const ft_text_rope_cursor* source,
    ft_text_rope_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_text_rope_cursor_move(ft_text_rope_cursor* destination, ft_text_rope_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_text_rope_cursor_dispose(ft_text_rope_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_text_rope_cursor_valid(const ft_text_rope_cursor* cursor);
/* Whether the rope version the cursor is positioned in holds no characters. */
bool ft_text_rope_cursor_empty(const ft_text_rope_cursor* cursor);
/* Number of characters in the rope version the cursor is positioned in. */
size_t ft_text_rope_cursor_size(const ft_text_rope_cursor* cursor);
/* Reads the number of characters, reporting failure rather than trapping on an uninitialized
 * handle. */
ft_status ft_text_rope_cursor_try_size(const ft_text_rope_cursor* cursor, size_t* size);
/* The cursor's gap position. */
size_t ft_text_rope_cursor_position(const ft_text_rope_cursor* cursor);
/* Whether the gap precedes the first character. */
ft_status ft_text_rope_cursor_is_at_start(const ft_text_rope_cursor* cursor, bool* result);
/* Whether the gap follows the last character. */
ft_status ft_text_rope_cursor_is_at_end(const ft_text_rope_cursor* cursor, bool* result);
/* The cursor's position expressed as a line and column. */
ft_status ft_text_rope_cursor_line_column(
    const ft_text_rope_cursor* cursor,
    ft_line_column* result);
/* The combined measure of everything before the gap. */
ft_status ft_text_rope_cursor_measure_before(
    const ft_text_rope_cursor* cursor,
    size_t* newlines);
/* The combined measure of everything after the gap. */
ft_status ft_text_rope_cursor_measure_after(
    const ft_text_rope_cursor* cursor,
    size_t* newlines);
/* Reads the character immediately before the gap, reporting whether one exists. */
ft_status ft_text_rope_cursor_try_peek_previous(
    const ft_text_rope_cursor* cursor,
    bool* found,
    char* value);
/* Reads the character immediately after the gap, reporting whether one exists. */
ft_status ft_text_rope_cursor_try_peek_next(
    const ft_text_rope_cursor* cursor,
    bool* found,
    char* value);
/* Moves the cursor one position earlier. */
ft_status ft_text_rope_cursor_move_previous(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_text_rope_cursor_move_next(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
/* Moves the cursor to the given position within the same rope version. */
ft_status ft_text_rope_cursor_seek(
    const ft_text_rope_cursor* cursor,
    size_t position,
    ft_text_rope_cursor* result);
/* Moves the cursor to the first gap where the measure satisfies the predicate. */
ft_status ft_text_rope_cursor_seek_by_measure(
    const ft_text_rope_cursor* cursor,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_text_rope_cursor* result);
/* Inserts one character at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_text_rope_cursor_insert_char(
    const ft_text_rope_cursor* cursor,
    char value,
    ft_text_rope_cursor* result);
/* Inserts NUL-terminated text at the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_text_rope_cursor_insert_cstr(
    const ft_text_rope_cursor* cursor,
    const char* text,
    ft_text_rope_cursor* result);
/* Inserts a rope's characters at the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_text_rope_cursor_insert_rope(
    const ft_text_rope_cursor* cursor,
    const ft_text_rope* values,
    ft_text_rope_cursor* result);
/* Removes the character before the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_text_rope_cursor_delete_previous(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
/* Removes the character after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_text_rope_cursor_delete_next(
    const ft_text_rope_cursor* cursor,
    ft_text_rope_cursor* result);
/* Replaces the character after the gap, producing a new version the cursor is then positioned
 * in. */
ft_status ft_text_rope_cursor_replace_next(
    const ft_text_rope_cursor* cursor,
    char value,
    ft_text_rope_cursor* result);
/* Initializes a handle on the rope version this cursor is positioned in. */
ft_status ft_text_rope_cursor_snapshot(
    const ft_text_rope_cursor* cursor,
    ft_text_rope* result);

#ifdef __cplusplus
}
#endif

#endif
