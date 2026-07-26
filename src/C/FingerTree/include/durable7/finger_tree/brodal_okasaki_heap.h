/*
 * A persistent Brodal-Okasaki heap: worst-case constant-time meld and minimum.
 *
 * Melding links two forests rather than merging their contents, which is what keeps it constant
 * time in the worst case rather than only amortized. Every operation returns a new version and
 * leaves its inputs valid, sharing unchanged structure, so an edit copies a path rather than the
 * whole collection.
 */

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

/* The shared, reference-counted representation behind a Brodal-Okasaki heap policy. */
typedef struct ft_brodal_policy_rep ft_brodal_policy_rep;

/* Policy identity gates melding. Copy preserves identity; independently
 * recreating an equivalent config deliberately does not. */
typedef struct ft_brodal_policy {
    ft_brodal_policy_rep* rep;
} ft_brodal_policy;

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
void ft_brodal_policy_config_init(
    ft_brodal_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_brodal_compare_fn compare,
    void* callback_context);
/* Initializes an empty policy using the supplied policies, which it retains. */
ft_status ft_brodal_policy_create(
    ft_brodal_policy* policy,
    const ft_brodal_policy_config* config);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
ft_status ft_brodal_policy_copy(
    const ft_brodal_policy* source,
    ft_brodal_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_brodal_policy_move(
    ft_brodal_policy* destination,
    ft_brodal_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_brodal_policy_dispose(ft_brodal_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_brodal_policy_same(
    const ft_brodal_policy* left,
    const ft_brodal_policy* right);

/* One tournament tree inside a Brodal-Okasaki heap. Opaque. */
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

/* Initializes the heap in place. */
ft_status ft_brodal_heap_init(
    ft_brodal_heap* heap,
    const ft_brodal_policy* policy);
/* Initializes a heap holding the given elements, built in bulk rather than by repeated
 * insertion. */
ft_status ft_brodal_heap_from_array(
    ft_brodal_heap* heap,
    const ft_brodal_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same heap version, taking a reference rather than copying. */
ft_status ft_brodal_heap_copy(
    const ft_brodal_heap* source,
    ft_brodal_heap* destination);
/* Relocates an initialized heap into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_brodal_heap_move(ft_brodal_heap* destination, ft_brodal_heap* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_brodal_heap_dispose(ft_brodal_heap* heap);

/* Whether the heap holds no elements. */
bool ft_brodal_heap_empty(const ft_brodal_heap* heap);
/* Number of elements in the heap. */
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
/* Produces the heap holding both operands' elements. Constant time: melding links two forests
 * rather than merging their contents. */
ft_status ft_brodal_heap_meld(
    const ft_brodal_heap* left,
    const ft_brodal_heap* right,
    ft_brodal_heap* result);
/* Produces the heap without its minimum element, reading that element out. */
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
/* Calls the visitor over the structure's shape, for tests and diagnostics. */
ft_status ft_brodal_heap_visit_shape(
    const ft_brodal_heap* heap,
    ft_brodal_shape_visit_fn visitor,
    void* context);

/* The root node's address, for tests that a no-op shared rather than copied. */
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
