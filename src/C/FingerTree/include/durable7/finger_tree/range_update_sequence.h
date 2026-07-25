#ifndef DURABLE7_FINGER_TREE_C_RANGE_UPDATE_SEQUENCE_H
#define DURABLE7_FINGER_TREE_C_RANGE_UPDATE_SEQUENCE_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_range_update_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_range_update_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_range_update_measure_element_fn)(
    void* destination,
    const void* element,
    void* context);
typedef ft_status (*ft_range_update_combine_fn)(
    void* destination,
    const void* left,
    const void* right,
    void* context);
typedef ft_status (*ft_range_update_equals_fn)(
    const void* left,
    const void* right,
    bool* equal,
    void* context);
typedef ft_status (*ft_range_update_is_identity_fn)(
    const void* tag,
    bool* is_identity,
    void* context);
typedef ft_status (*ft_range_update_compose_fn)(
    void* destination,
    const void* newer,
    const void* older,
    void* context);
typedef ft_status (*ft_range_update_apply_element_fn)(
    void* destination,
    const void* tag,
    const void* element,
    void* context);
typedef ft_status (*ft_range_update_apply_measure_fn)(
    void* destination,
    const void* tag,
    const void* measure,
    size_t count,
    void* context);
typedef void* (*ft_range_update_allocate_fn)(size_t size, void* context);
typedef void (*ft_range_update_deallocate_fn)(void* allocation, void* context);

typedef struct ft_range_update_type_policy {
    size_t size;
    const void* type_identity;
    ft_range_update_copy_fn copy;
    ft_range_update_destroy_fn destroy;
    void* context;
} ft_range_update_type_policy;

typedef struct ft_range_update_allocator {
    ft_range_update_allocate_fn allocate;
    ft_range_update_deallocate_fn deallocate;
    void* context;
} ft_range_update_allocator;

/* Callback destinations denote uninitialized storage. A successful callback
 * constructs one owned value there. A failing callback must leave the storage
 * ownership-free. Compose(newer, older) always means apply older first and
 * newer second. The empty measure and identity tag are copied during policy
 * creation; is_identity must recognize the configured identity tag. Type
 * identities are required non-null stable caller-owned tokens. Hooks must not
 * reenter an in-flight operation through the same handles. Distinct immutable
 * handles may be read concurrently when all reachable hooks and contexts are
 * thread-safe. */
typedef struct ft_range_update_policy_config {
    ft_range_update_type_policy element;
    ft_range_update_type_policy measure;
    ft_range_update_type_policy tag;
    const void* empty_measure;
    const void* identity_tag;
    ft_range_update_measure_element_fn measure_element;
    ft_range_update_combine_fn combine;
    ft_range_update_equals_fn measure_equals;
    ft_range_update_is_identity_fn is_identity;
    ft_range_update_compose_fn compose;
    ft_range_update_apply_element_fn apply_element;
    ft_range_update_apply_measure_fn apply_measure;
    void* algebra_context;
    ft_range_update_allocator allocator;
} ft_range_update_policy_config;

typedef struct ft_range_update_policy_rep ft_range_update_policy_rep;

typedef struct ft_range_update_policy {
    ft_range_update_policy_rep* rep;
} ft_range_update_policy;

void ft_range_update_policy_config_init(
    ft_range_update_policy_config* config,
    size_t element_size,
    const void* element_type_identity,
    size_t measure_size,
    const void* measure_type_identity,
    size_t tag_size,
    const void* tag_type_identity,
    const void* empty_measure,
    const void* identity_tag,
    ft_range_update_measure_element_fn measure_element,
    ft_range_update_combine_fn combine,
    ft_range_update_equals_fn measure_equals,
    ft_range_update_is_identity_fn is_identity,
    ft_range_update_compose_fn compose,
    ft_range_update_apply_element_fn apply_element,
    ft_range_update_apply_measure_fn apply_measure);
ft_status ft_range_update_policy_create(
    ft_range_update_policy* policy,
    const ft_range_update_policy_config* config);
ft_status ft_range_update_policy_copy(
    const ft_range_update_policy* source,
    ft_range_update_policy* destination);
void ft_range_update_policy_move(
    ft_range_update_policy* destination,
    ft_range_update_policy* source);
void ft_range_update_policy_dispose(ft_range_update_policy* policy);
bool ft_range_update_policy_same(
    const ft_range_update_policy* left,
    const ft_range_update_policy* right);

typedef struct ft_range_update_node ft_range_update_node;

/* An immutable sequence handle. Empty handles are still policy-bound. Results
 * may exactly alias their single source handle. Non-aliasing result handles are
 * output-only and must be empty/uninitialized. Split outputs must be distinct;
 * either may alias the source. */
typedef struct ft_range_update_sequence {
    ft_range_update_policy_rep* policy;
    ft_range_update_node* root;
} ft_range_update_sequence;

typedef struct ft_range_update_split_result {
    ft_range_update_sequence left;
    ft_range_update_sequence right;
} ft_range_update_split_result;

typedef struct ft_range_update_sequence_cursor {
    ft_range_update_sequence sequence;
    size_t position;
} ft_range_update_sequence_cursor;

typedef struct ft_range_update_sequence_statistics {
    size_t count;
    size_t height;
    size_t logical_node_count;
    size_t physical_node_count;
    size_t pending_tag_count;
    size_t maximum_absolute_balance_factor;
} ft_range_update_sequence_statistics;

typedef ft_status (*ft_range_update_visit_fn)(
    const void* logical_element,
    void* context);

ft_status ft_range_update_sequence_init(
    ft_range_update_sequence* sequence,
    const ft_range_update_policy* policy);
/* elements is an array of count non-null pointers. The array is consumed in
 * order and is never retained. */
ft_status ft_range_update_sequence_from_array(
    ft_range_update_sequence* sequence,
    const ft_range_update_policy* policy,
    const void* const* elements,
    size_t count);
ft_status ft_range_update_sequence_copy(
    const ft_range_update_sequence* source,
    ft_range_update_sequence* destination);
void ft_range_update_sequence_move(
    ft_range_update_sequence* destination,
    ft_range_update_sequence* source);
void ft_range_update_sequence_dispose(ft_range_update_sequence* sequence);

bool ft_range_update_sequence_empty(const ft_range_update_sequence* sequence);
size_t ft_range_update_sequence_size(const ft_range_update_sequence* sequence);
size_t ft_range_update_sequence_height(const ft_range_update_sequence* sequence);

/* Successful value-returning operations construct one owned value in
 * uninitialized caller storage. The caller destroys it with the matching type
 * policy hook when non-null. */
ft_status ft_range_update_sequence_measure(
    const ft_range_update_sequence* sequence,
    void* destination);
ft_status ft_range_update_sequence_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    void* destination);

ft_status ft_range_update_sequence_prepend(
    const ft_range_update_sequence* sequence,
    const void* element,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_append(
    const ft_range_update_sequence* sequence,
    const void* element,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_insert_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    const void* element,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_set_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    const void* element,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_remove_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_concat(
    const ft_range_update_sequence* left,
    const ft_range_update_sequence* right,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_split_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    ft_range_update_split_result* result);
ft_status ft_range_update_sequence_get_range(
    const ft_range_update_sequence* sequence,
    size_t index,
    size_t count,
    ft_range_update_sequence* result);

ft_status ft_range_update_sequence_apply_range(
    const ft_range_update_sequence* sequence,
    size_t index,
    size_t count,
    const void* tag,
    ft_range_update_sequence* result);
ft_status ft_range_update_sequence_measure_range(
    const ft_range_update_sequence* sequence,
    size_t index,
    size_t count,
    void* destination);

/* logical_element is borrowed only for the duration of the callback. */
ft_status ft_range_update_sequence_visit(
    const ft_range_update_sequence* sequence,
    ft_range_update_visit_fn visitor,
    void* context);

const void* ft_range_update_sequence_root_identity(
    const ft_range_update_sequence* sequence);
ft_status ft_range_update_sequence_physical_node_count(
    const ft_range_update_sequence* sequence,
    size_t* count);
ft_status ft_range_update_sequence_shared_node_count(
    const ft_range_update_sequence* left,
    const ft_range_update_sequence* right,
    size_t* count);
/* Structural invalidity is status OK with valid=false. Allocation or callback
 * failure leaves valid/statistics unchanged. */
ft_status ft_range_update_sequence_validate(
    const ft_range_update_sequence* sequence,
    bool* valid,
    ft_range_update_sequence_statistics* statistics);

ft_status ft_range_update_sequence_get_cursor(
    const ft_range_update_sequence* sequence,
    size_t position,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_copy(
    const ft_range_update_sequence_cursor* source,
    ft_range_update_sequence_cursor* destination);
void ft_range_update_sequence_cursor_move(
    ft_range_update_sequence_cursor* destination,
    ft_range_update_sequence_cursor* source);
void ft_range_update_sequence_cursor_dispose(ft_range_update_sequence_cursor* cursor);
bool ft_range_update_sequence_cursor_valid(const ft_range_update_sequence_cursor* cursor);
bool ft_range_update_sequence_cursor_empty(const ft_range_update_sequence_cursor* cursor);
size_t ft_range_update_sequence_cursor_size(const ft_range_update_sequence_cursor* cursor);
size_t ft_range_update_sequence_cursor_position(const ft_range_update_sequence_cursor* cursor);
ft_status ft_range_update_sequence_cursor_is_at_start(
    const ft_range_update_sequence_cursor* cursor,
    bool* result);
ft_status ft_range_update_sequence_cursor_is_at_end(
    const ft_range_update_sequence_cursor* cursor,
    bool* result);
ft_status ft_range_update_sequence_cursor_measure_before(
    const ft_range_update_sequence_cursor* cursor,
    void* destination);
ft_status ft_range_update_sequence_cursor_measure_after(
    const ft_range_update_sequence_cursor* cursor,
    void* destination);
ft_status ft_range_update_sequence_cursor_try_peek_previous(
    const ft_range_update_sequence_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_range_update_sequence_cursor_try_peek_next(
    const ft_range_update_sequence_cursor* cursor,
    bool* found,
    void* value);
ft_status ft_range_update_sequence_cursor_move_previous(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_move_next(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_seek(
    const ft_range_update_sequence_cursor* cursor,
    size_t position,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_insert(
    const ft_range_update_sequence_cursor* cursor,
    const void* value,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_delete_previous(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_delete_next(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_replace_next(
    const ft_range_update_sequence_cursor* cursor,
    const void* value,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_measure_previous(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    void* destination);
ft_status ft_range_update_sequence_cursor_measure_next(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    void* destination);
ft_status ft_range_update_sequence_cursor_apply_previous(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    const void* tag,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_apply_next(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    const void* tag,
    ft_range_update_sequence_cursor* result);
ft_status ft_range_update_sequence_cursor_snapshot(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence* result);

#ifdef __cplusplus
}
#endif

#endif
