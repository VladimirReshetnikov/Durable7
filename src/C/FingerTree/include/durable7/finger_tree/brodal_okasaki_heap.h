#ifndef DURABLE7_FINGER_TREE_C_BRODAL_OKASAKI_HEAP_H
#define DURABLE7_FINGER_TREE_C_BRODAL_OKASAKI_HEAP_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_brodal_value_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_brodal_value_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_brodal_compare_fn)(
    const void* left,
    const void* right,
    int* comparison,
    void* context);
typedef void* (*ft_brodal_allocate_fn)(size_t size, void* context);
typedef void (*ft_brodal_deallocate_fn)(void* allocation, void* context);

typedef struct ft_brodal_allocator {
    ft_brodal_allocate_fn allocate;
    ft_brodal_deallocate_fn deallocate;
    void* context;
} ft_brodal_allocator;

/* value_type_identity is a required, stable, non-null caller-owned tag. Copy
 * constructs an owned value in uninitialized storage; on failure it must leave
 * that storage ownership-free. Destroy and allocator hooks must return
 * normally. Hooks must not reenter an in-flight operation through the same
 * policy/heap handles. Distinct immutable handles may be used concurrently only
 * when every reachable hook and context is safe for that call pattern. */
typedef struct ft_brodal_policy_config {
    size_t value_size;
    const void* value_type_identity;
    ft_brodal_value_copy_fn copy;
    ft_brodal_value_destroy_fn destroy;
    ft_brodal_compare_fn compare;
    void* callback_context;
    ft_brodal_allocator allocator;
} ft_brodal_policy_config;

typedef struct ft_brodal_policy_rep ft_brodal_policy_rep;

/* Policy identity gates melding. Copy preserves identity; independently
 * recreating an equivalent config deliberately does not. */
typedef struct ft_brodal_policy {
    ft_brodal_policy_rep* rep;
} ft_brodal_policy;

void ft_brodal_policy_config_init(
    ft_brodal_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_brodal_compare_fn compare,
    void* callback_context);
ft_status ft_brodal_policy_create(
    ft_brodal_policy* policy,
    const ft_brodal_policy_config* config);
ft_status ft_brodal_policy_copy(
    const ft_brodal_policy* source,
    ft_brodal_policy* destination);
void ft_brodal_policy_move(
    ft_brodal_policy* destination,
    ft_brodal_policy* source);
void ft_brodal_policy_dispose(ft_brodal_policy* policy);
bool ft_brodal_policy_same(
    const ft_brodal_policy* left,
    const ft_brodal_policy* right);

typedef struct ft_brodal_tree ft_brodal_tree;

typedef struct ft_brodal_heap {
    ft_brodal_policy_rep* policy;
    ft_brodal_tree* root;
    size_t count;
} ft_brodal_heap;

typedef struct ft_brodal_heap_statistics {
    size_t count;
    size_t root_forest_length;
    unsigned maximum_rank;
    size_t maximum_depth;
} ft_brodal_heap_statistics;

typedef ft_status (*ft_brodal_visit_fn)(const void* value, void* context);
typedef ft_status (*ft_brodal_shape_visit_fn)(
    const void* tree_identity,
    const void* value,
    unsigned rank,
    size_t child_count,
    size_t depth,
    void* context);

ft_status ft_brodal_heap_init(
    ft_brodal_heap* heap,
    const ft_brodal_policy* policy);
ft_status ft_brodal_heap_from_array(
    ft_brodal_heap* heap,
    const ft_brodal_policy* policy,
    const void* values,
    size_t count);
ft_status ft_brodal_heap_copy(
    const ft_brodal_heap* source,
    ft_brodal_heap* destination);
void ft_brodal_heap_move(ft_brodal_heap* destination, ft_brodal_heap* source);
void ft_brodal_heap_dispose(ft_brodal_heap* heap);

bool ft_brodal_heap_empty(const ft_brodal_heap* heap);
size_t ft_brodal_heap_size(const ft_brodal_heap* heap);

/* On success, minimum_ref borrows the stored representative and remains valid
 * while the source version is retained. */
ft_status ft_brodal_heap_try_get_minimum_ref(
    const ft_brodal_heap* heap,
    bool* found,
    const void** minimum_ref);
/* On found=true, minimum is independently owned and must be destroyed with the
 * policy's destroy callback when non-null. */
ft_status ft_brodal_heap_try_get_minimum_copy(
    const ft_brodal_heap* heap,
    bool* found,
    void* minimum);

/* Results are published only on success. Exact result/operand aliasing is
 * supported. Meld requires exact policy identity; empty-side melds retain the
 * nonempty operand root, while self-meld preserves both logical occurrences. */
ft_status ft_brodal_heap_insert(
    const ft_brodal_heap* heap,
    const void* value,
    ft_brodal_heap* result);
ft_status ft_brodal_heap_meld(
    const ft_brodal_heap* left,
    const ft_brodal_heap* right,
    ft_brodal_heap* result);
ft_status ft_brodal_heap_delete_minimum(
    const ft_brodal_heap* heap,
    ft_brodal_heap* result);
/* On removed=true, minimum receives an independently owned copy of the exact
 * removed representative. The copy and remainder are both withheld on any
 * failure, including when result aliases heap. */
ft_status ft_brodal_heap_try_delete_minimum(
    const ft_brodal_heap* heap,
    bool* removed,
    void* minimum,
    ft_brodal_heap* result);

/* Structural-order visitation is deliberately unsorted and performs no value
 * comparisons. Both traversals are explicit-stack and visit logical
 * occurrences, including duplicated shared subgraphs produced by self-meld. */
ft_status ft_brodal_heap_visit(
    const ft_brodal_heap* heap,
    ft_brodal_visit_fn visitor,
    void* context);
ft_status ft_brodal_heap_visit_shape(
    const ft_brodal_heap* heap,
    ft_brodal_shape_visit_fn visitor,
    void* context);

const void* ft_brodal_heap_root_identity(const ft_brodal_heap* heap);
/* Structural invalidity is FT_STATUS_OK with valid=false. Allocation and
 * callback failures remain distinguishable and outputs change only on success. */
ft_status ft_brodal_heap_validate(
    const ft_brodal_heap* heap,
    bool* valid,
    ft_brodal_heap_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
