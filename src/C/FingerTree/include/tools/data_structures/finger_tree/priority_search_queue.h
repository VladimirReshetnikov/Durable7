#ifndef TOOLS_DATA_STRUCTURES_FINGER_TREE_C_PRIORITY_SEARCH_QUEUE_H
#define TOOLS_DATA_STRUCTURES_FINGER_TREE_C_PRIORITY_SEARCH_QUEUE_H

#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_psq_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_psq_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_psq_compare_fn)(
    const void* left,
    const void* right,
    int* comparison,
    void* context);
typedef ft_status (*ft_psq_equals_fn)(
    const void* left,
    const void* right,
    bool* equal,
    void* context);
typedef void* (*ft_psq_allocate_fn)(size_t size, void* context);
typedef void (*ft_psq_deallocate_fn)(void* allocation, void* context);

typedef struct ft_psq_type_policy {
    size_t size;
    const void* type_identity;
    ft_psq_copy_fn copy;
    ft_psq_destroy_fn destroy;
    ft_psq_equals_fn equals;
    void* context;
} ft_psq_type_policy;

typedef struct ft_psq_allocator {
    ft_psq_allocate_fn allocate;
    ft_psq_deallocate_fn deallocate;
    void* context;
} ft_psq_allocator;

/* Each type_identity is a required non-null stable caller-owned tag. Copy
 * constructs an owned object in uninitialized storage and leaves it
 * ownership-free on failure. Equality and ordering callbacks are independent:
 * exact replacement no-op detection requires both priority-order equivalence
 * and priority equality, plus value equality. Incoming-key ordinary equality
 * is deliberately irrelevant: comparer equivalence retains the first stored
 * key representative. key.equals is optional and is never invoked. Hooks must not reenter an
 * in-flight operation through the same handles. Distinct immutable handles are
 * concurrency-safe only when all reachable hooks/contexts are thread-safe. */
typedef struct ft_psq_policy_config {
    ft_psq_type_policy key;
    ft_psq_type_policy priority;
    ft_psq_type_policy value;
    ft_psq_compare_fn key_compare;
    ft_psq_compare_fn priority_compare;
    ft_psq_allocator allocator;
} ft_psq_policy_config;

typedef struct ft_psq_policy_rep ft_psq_policy_rep;

typedef struct ft_psq_policy {
    ft_psq_policy_rep* rep;
} ft_psq_policy;

void ft_psq_policy_config_init(
    ft_psq_policy_config* config,
    size_t key_size,
    const void* key_type_identity,
    ft_psq_compare_fn key_compare,
    size_t priority_size,
    const void* priority_type_identity,
    ft_psq_compare_fn priority_compare,
    ft_psq_equals_fn priority_equals,
    size_t value_size,
    const void* value_type_identity,
    ft_psq_equals_fn value_equals);
ft_status ft_psq_policy_create(
    ft_psq_policy* policy,
    const ft_psq_policy_config* config);
ft_status ft_psq_policy_copy(
    const ft_psq_policy* source,
    ft_psq_policy* destination);
void ft_psq_policy_move(ft_psq_policy* destination, ft_psq_policy* source);
void ft_psq_policy_dispose(ft_psq_policy* policy);
bool ft_psq_policy_same(const ft_psq_policy* left, const ft_psq_policy* right);

typedef struct ft_priority_search_entry_ref {
    const void* key;
    const void* priority;
    const void* value;
} ft_priority_search_entry_ref;

typedef struct ft_priority_search_input {
    const void* key;
    const void* priority;
    const void* value;
} ft_priority_search_input;

typedef struct ft_psq_entry_rep ft_psq_entry_rep;

/* An owned entry keeps the exact stored key/priority/value representative alive
 * independently of the queue version from which it was removed or selected. */
typedef struct ft_priority_search_entry {
    ft_psq_policy_rep* policy;
    ft_psq_entry_rep* rep;
} ft_priority_search_entry;

ft_status ft_priority_search_entry_copy(
    const ft_priority_search_entry* source,
    ft_priority_search_entry* destination);
void ft_priority_search_entry_move(
    ft_priority_search_entry* destination,
    ft_priority_search_entry* source);
void ft_priority_search_entry_dispose(ft_priority_search_entry* entry);
ft_status ft_priority_search_entry_get_ref(
    const ft_priority_search_entry* entry,
    ft_priority_search_entry_ref* entry_ref);

typedef struct ft_psq_node ft_psq_node;

typedef struct ft_priority_search_queue {
    ft_psq_policy_rep* policy;
    ft_psq_node* root;
} ft_priority_search_queue;

typedef struct ft_priority_search_queue_statistics {
    size_t count;
    size_t height;
    unsigned maximum_absolute_balance_factor;
} ft_priority_search_queue_statistics;

typedef ft_status (*ft_priority_search_visit_fn)(
    ft_priority_search_entry_ref entry,
    void* context);

ft_status ft_priority_search_queue_init(
    ft_priority_search_queue* queue,
    const ft_psq_policy* policy);
/* Entries are applied in array order; equivalent keys are last-wins for
 * priority/value while retaining the first stored key representative. */
ft_status ft_priority_search_queue_from_array(
    ft_priority_search_queue* queue,
    const ft_psq_policy* policy,
    const ft_priority_search_input* entries,
    size_t count);
ft_status ft_priority_search_queue_copy(
    const ft_priority_search_queue* source,
    ft_priority_search_queue* destination);
void ft_priority_search_queue_move(
    ft_priority_search_queue* destination,
    ft_priority_search_queue* source);
void ft_priority_search_queue_dispose(ft_priority_search_queue* queue);

bool ft_priority_search_queue_empty(const ft_priority_search_queue* queue);
size_t ft_priority_search_queue_size(const ft_priority_search_queue* queue);
size_t ft_priority_search_queue_height(const ft_priority_search_queue* queue);

ft_status ft_priority_search_queue_contains_key(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* found);
/* On found=true, entry borrows the exact stored representatives and remains
 * valid while the source queue version is retained. */
ft_status ft_priority_search_queue_try_get_entry_ref(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* found,
    ft_priority_search_entry_ref* entry);

/* Set is last-wins but retains the first comparer-equivalent key. It preserves
 * root identity only when priority compare==0, priority equality is true, and
 * value equality is true. Exact result/source aliasing is supported. */
ft_status ft_priority_search_queue_set(
    const ft_priority_search_queue* queue,
    const void* key,
    const void* priority,
    const void* value,
    ft_priority_search_queue* result);
ft_status ft_priority_search_queue_try_add(
    const ft_priority_search_queue* queue,
    const void* key,
    const void* priority,
    const void* value,
    bool* added,
    ft_priority_search_queue* result);
ft_status ft_priority_search_queue_remove(
    const ft_priority_search_queue* queue,
    const void* key,
    ft_priority_search_queue* result);
/* On removed=true, entry owns the exact stored representatives. On absence,
 * entry is an empty disposable handle and result shares the source root. */
ft_status ft_priority_search_queue_try_remove(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* removed,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result);

/* Minimum selection is O(1), ordered by priority then retained key. */
ft_status ft_priority_search_queue_try_get_minimum(
    const ft_priority_search_queue* queue,
    bool* found,
    ft_priority_search_entry* entry);
ft_status ft_priority_search_queue_delete_minimum(
    const ft_priority_search_queue* queue,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result);
ft_status ft_priority_search_queue_try_delete_minimum(
    const ft_priority_search_queue* queue,
    bool* removed,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result);

/* Visits all entries in key order without copying stored components. */
ft_status ft_priority_search_queue_visit(
    const ft_priority_search_queue* queue,
    ft_priority_search_visit_fn visitor,
    void* context);
/* Eagerly validates minimum_key <= maximum_key, then visits entries in the
 * inclusive key range whose priority <= maximum_priority. Cached winners prune
 * subtrees. Cost is O(log n + v), v <= n; no pennant output bound is claimed. */
ft_status ft_priority_search_queue_visit_at_most(
    const ft_priority_search_queue* queue,
    const void* minimum_key,
    const void* maximum_key,
    const void* maximum_priority,
    ft_priority_search_visit_fn visitor,
    void* context);

const void* ft_priority_search_queue_root_identity(
    const ft_priority_search_queue* queue);
ft_status ft_priority_search_queue_node_identity(
    const ft_priority_search_queue* queue,
    const void* key,
    const void** identity);
ft_status ft_priority_search_queue_shared_node_count(
    const ft_priority_search_queue* left,
    const ft_priority_search_queue* right,
    size_t* shared_count);

/* Validates strict key order, AVL balance, count/height metadata, and exact
 * cached winners. Structural invalidity is status OK with valid=false;
 * allocation/callback failures leave outputs unchanged. */
ft_status ft_priority_search_queue_validate(
    const ft_priority_search_queue* queue,
    bool* valid,
    ft_priority_search_queue_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
