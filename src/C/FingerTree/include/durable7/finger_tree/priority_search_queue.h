/*
 * A persistent priority search queue: ordered by key, searchable by minimum priority.
 *
 * A pennant tournament tree carries both orders at once, so a key lookup and a minimum-priority
 * lookup are each a descent rather than a scan of the other order. Every operation returns a new
 * version and leaves its inputs valid, sharing unchanged structure, so an edit copies a path rather
 * than the whole collection.
 */

#ifndef DURABLE7_FINGER_TREE_C_PRIORITY_SEARCH_QUEUE_H
#define DURABLE7_FINGER_TREE_C_PRIORITY_SEARCH_QUEUE_H

#include <durable7/finger_tree/fingertree.h>

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

/* The shared, reference-counted representation behind a priority search queue policy. */
typedef struct ft_psq_policy_rep ft_psq_policy_rep;

typedef struct ft_psq_policy {
    ft_psq_policy_rep* rep;
} ft_psq_policy;

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
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
/* Initializes an empty policy using the supplied policies, which it retains. */
ft_status ft_psq_policy_create(
    ft_psq_policy* policy,
    const ft_psq_policy_config* config);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
ft_status ft_psq_policy_copy(
    const ft_psq_policy* source,
    ft_psq_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_psq_policy_move(ft_psq_policy* destination, ft_psq_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_psq_policy_dispose(ft_psq_policy* policy);
/* Whether both handles denote the same policy identity. */
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

/* The shared representation of one queued entry. */
typedef struct ft_psq_entry_rep ft_psq_entry_rep;

/* An owned entry keeps the exact stored key/priority/value representative alive
 * independently of the queue version from which it was removed or selected. */
typedef struct ft_priority_search_entry {
    ft_psq_policy_rep* policy;
    ft_psq_entry_rep* rep;
} ft_priority_search_entry;

/* Initializes a second handle on the same entry version, taking a reference rather than copying. */
ft_status ft_priority_search_entry_copy(
    const ft_priority_search_entry* source,
    ft_priority_search_entry* destination);
/* Relocates an initialized entry into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_priority_search_entry_move(
    ft_priority_search_entry* destination,
    ft_priority_search_entry* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_priority_search_entry_dispose(ft_priority_search_entry* entry);
/* Borrows the stored value in place. The pointer stays valid only while this version does. */
ft_status ft_priority_search_entry_get_ref(
    const ft_priority_search_entry* entry,
    ft_priority_search_entry_ref* entry_ref);

/* One node of a priority search queue. Opaque. */
typedef struct ft_psq_node ft_psq_node;

typedef struct ft_priority_search_queue {
    ft_psq_policy_rep* policy;
    ft_psq_node* root;
} ft_priority_search_queue;

/* Immutable key-order root-plus-rank gap cursor. The retained queue keeps the
 * policy and all borrowed entry representatives alive. */
typedef struct ft_priority_search_queue_cursor {
    ft_priority_search_queue queue;
    size_t position;
} ft_priority_search_queue_cursor;

typedef struct ft_priority_search_queue_statistics {
    size_t count;
    size_t height;
    unsigned maximum_absolute_balance_factor;
} ft_priority_search_queue_statistics;

typedef ft_status (*ft_priority_search_visit_fn)(
    ft_priority_search_entry_ref entry,
    void* context);

/* Initializes the queue in place. */
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
/* Initializes a second handle on the same queue version, taking a reference rather than copying. */
ft_status ft_priority_search_queue_copy(
    const ft_priority_search_queue* source,
    ft_priority_search_queue* destination);
/* Relocates an initialized queue into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_priority_search_queue_move(
    ft_priority_search_queue* destination,
    ft_priority_search_queue* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_priority_search_queue_dispose(ft_priority_search_queue* queue);

/* Whether the queue holds no entries. */
bool ft_priority_search_queue_empty(const ft_priority_search_queue* queue);
/* Number of entries in the queue. */
size_t ft_priority_search_queue_size(const ft_priority_search_queue* queue);
/* The structure's height. */
size_t ft_priority_search_queue_height(const ft_priority_search_queue* queue);

/* Whether the key is present. */
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
/* Adds the entry unless an equivalent one is present, reporting which happened. */
ft_status ft_priority_search_queue_try_add(
    const ft_priority_search_queue* queue,
    const void* key,
    const void* priority,
    const void* value,
    bool* added,
    ft_priority_search_queue* result);
/* Produces a queue without that entry. */
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
/* Produces the queue without its minimum-priority entry, reading that entry out. */
ft_status ft_priority_search_queue_delete_minimum(
    const ft_priority_search_queue* queue,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result);
/* Removes the minimum-priority entry, reporting whether the queue had one. */
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

/* The root node's address, for tests that a no-op shared rather than copied. */
const void* ft_priority_search_queue_root_identity(
    const ft_priority_search_queue* queue);
/* A node's address, for sharing assertions. */
ft_status ft_priority_search_queue_node_identity(
    const ft_priority_search_queue* queue,
    const void* key,
    const void** identity);
/* How many nodes the two versions have in common. */
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

/* Initializes a cursor at the given gap of the queue. */
ft_status ft_priority_search_queue_get_cursor(
    const ft_priority_search_queue* queue,
    size_t position,
    ft_priority_search_queue_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_priority_search_queue_get_cursor_lower_bound(
    const ft_priority_search_queue* queue,
    const void* key,
    ft_priority_search_queue_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_priority_search_queue_get_cursor_upper_bound(
    const ft_priority_search_queue* queue,
    const void* key,
    ft_priority_search_queue_cursor* result);
/* Initializes a cursor at the key, reporting whether it was actually present; on a miss the cursor
 * sits at the insertion point. */
ft_status ft_priority_search_queue_get_cursor_at_key(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* found,
    ft_priority_search_queue_cursor* result);
/* Initializes a cursor at the minimum-priority entry, found through the cached priority rather than
 * by scanning. */
ft_status ft_priority_search_queue_get_cursor_at_minimum_priority(
    const ft_priority_search_queue* queue,
    ft_priority_search_queue_cursor* result);

/* Initializes a second cursor at the same position on the same version. */
ft_status ft_priority_search_queue_cursor_copy(
    const ft_priority_search_queue_cursor* source,
    ft_priority_search_queue_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_priority_search_queue_cursor_move(
    ft_priority_search_queue_cursor* destination,
    ft_priority_search_queue_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_priority_search_queue_cursor_dispose(ft_priority_search_queue_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_priority_search_queue_cursor_valid(const ft_priority_search_queue_cursor* cursor);
/* Whether the queue version the cursor is positioned in holds no entries. */
bool ft_priority_search_queue_cursor_empty(const ft_priority_search_queue_cursor* cursor);
/* Number of entries in the queue version the cursor is positioned in. */
size_t ft_priority_search_queue_cursor_size(const ft_priority_search_queue_cursor* cursor);
/* The cursor's gap position. */
size_t ft_priority_search_queue_cursor_position(const ft_priority_search_queue_cursor* cursor);
/* Whether the gap precedes the first entry. */
ft_status ft_priority_search_queue_cursor_is_at_start(
    const ft_priority_search_queue_cursor* cursor,
    bool* result);
/* Whether the gap follows the last entry. */
ft_status ft_priority_search_queue_cursor_is_at_end(
    const ft_priority_search_queue_cursor* cursor,
    bool* result);
/* Borrowed entry references remain valid while the cursor is retained. */
ft_status ft_priority_search_queue_cursor_try_peek_previous_ref(
    const ft_priority_search_queue_cursor* cursor,
    bool* found,
    ft_priority_search_entry_ref* entry);
/* Borrows the entry after the gap in place, reporting whether one exists. */
ft_status ft_priority_search_queue_cursor_try_peek_next_ref(
    const ft_priority_search_queue_cursor* cursor,
    bool* found,
    ft_priority_search_entry_ref* entry);
/* Moves the cursor one position earlier. */
ft_status ft_priority_search_queue_cursor_move_previous(
    const ft_priority_search_queue_cursor* cursor,
    ft_priority_search_queue_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_priority_search_queue_cursor_move_next(
    const ft_priority_search_queue_cursor* cursor,
    ft_priority_search_queue_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_priority_search_queue_cursor_seek_rank(
    const ft_priority_search_queue_cursor* cursor,
    size_t position,
    ft_priority_search_queue_cursor* result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
ft_status ft_priority_search_queue_cursor_insert(
    const ft_priority_search_queue_cursor* cursor,
    const void* key,
    const void* priority,
    const void* value,
    ft_priority_search_queue_cursor* result);
/* Inserts the entry unless an equivalent one is present, reporting which happened. */
ft_status ft_priority_search_queue_cursor_try_insert(
    const ft_priority_search_queue_cursor* cursor,
    const void* key,
    const void* priority,
    const void* value,
    bool* inserted,
    ft_priority_search_queue_cursor* result);
/* Produces a queue with the key bound to the value. */
ft_status ft_priority_search_queue_cursor_set_item(
    const ft_priority_search_queue_cursor* cursor,
    const void* key,
    const void* priority,
    const void* value,
    ft_priority_search_queue_cursor* result);
/* Replaces the entry after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_priority_search_queue_cursor_set_next(
    const ft_priority_search_queue_cursor* cursor,
    const void* priority,
    const void* value,
    ft_priority_search_queue_cursor* result);
/* Removes the entry before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_priority_search_queue_cursor_delete_previous(
    const ft_priority_search_queue_cursor* cursor,
    ft_priority_search_queue_cursor* result);
/* Removes the entry after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_priority_search_queue_cursor_delete_next(
    const ft_priority_search_queue_cursor* cursor,
    ft_priority_search_queue_cursor* result);
/* Initializes a handle on the queue version this cursor is positioned in. */
ft_status ft_priority_search_queue_cursor_snapshot(
    const ft_priority_search_queue_cursor* cursor,
    ft_priority_search_queue* result);

#ifdef __cplusplus
}
#endif

#endif
