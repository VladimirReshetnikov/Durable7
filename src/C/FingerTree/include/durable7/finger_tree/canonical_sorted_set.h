/*
 * A persistent sorted set whose shape depends only on its contents, not on its edit history.
 *
 * Each element's rank is derived from the element itself under the policy's seed, so two sets
 * holding the same elements are the same tree however they were built. That makes shapes comparable
 * across independent parties, which a history-dependent balance scheme cannot offer. Every
 * operation returns a new version and leaves its inputs valid, sharing unchanged structure, so an
 * edit copies a path rather than the whole collection.
 */

#ifndef DURABLE7_FINGER_TREE_C_CANONICAL_SORTED_SET_H
#define DURABLE7_FINGER_TREE_C_CANONICAL_SORTED_SET_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_canonical_value_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_canonical_value_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_canonical_compare_fn)(
    const void* left,
    const void* right,
    int* comparison,
    void* context);
typedef ft_status (*ft_canonical_rank_hash_fn)(
    const void* value,
    uint64_t* rank_hash,
    void* context);
typedef void* (*ft_canonical_allocate_fn)(size_t size, void* context);
typedef void (*ft_canonical_deallocate_fn)(void* allocation, void* context);

typedef struct ft_canonical_allocator {
    ft_canonical_allocate_fn allocate;
    ft_canonical_deallocate_fn deallocate;
    void* context;
} ft_canonical_allocator;

/* value_type_identity is a required non-null caller-owned tag. Distinct policy
 * identities may perform semantic equality/relations only when their tag
 * pointers match, preventing same-sized unrelated representations from
 * crossing into receiver callbacks. The tag object's address must remain
 * stable and valid for every retaining policy/set handle. All callbacks may be
 * invoked by logically read-only operations. compare,
 * rank_hash, and a non-null copy callback return an explicit status that is
 * propagated unchanged. A failing copy must leave its destination
 * uninitialized and ownership-free. destroy must return normally. Callback
 * contexts and any objects they reference must outlive every copied policy and
 * set retaining this configuration. Callbacks and allocator hooks must not
 * reenter an in-flight operation through the same policy or set handles,
 * including from destroy during disposal. Concurrent operations through
 * distinct handles are supported only when every callback, hook, and referenced
 * context they can invoke is safe for that concurrent call pattern. Values may
 * require at most fundamental C alignment (alignof(max_align_t));
 * extended-aligned values are unsupported. */
typedef struct ft_canonical_policy_config {
    size_t value_size;
    const void* value_type_identity;
    ft_canonical_value_copy_fn copy;
    ft_canonical_value_destroy_fn destroy;
    ft_canonical_compare_fn compare;
    ft_canonical_rank_hash_fn rank_hash;
    void* callback_context;
    ft_canonical_allocator allocator;
} ft_canonical_policy_config;

typedef struct ft_zip_tree_rank {
    unsigned geometric;
    uint64_t secondary;
    uint64_t content;
} ft_zip_tree_rank;

/* The shared, reference-counted representation behind a canonical set policy, carrying the rank
 * seed that fixes the canonical shape. */
typedef struct ft_canonical_policy_rep ft_canonical_policy_rep;

/* Policy handles are identity-bearing and reference counted. Copying a handle
 * preserves algebra compatibility; independently recreating an identical seed
 * or key reproduces ranks but intentionally has different policy identity. */
typedef struct ft_canonical_policy {
    ft_canonical_policy_rep* rep;
} ft_canonical_policy;

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
void ft_canonical_policy_config_init(
    ft_canonical_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_canonical_compare_fn compare,
    ft_canonical_rank_hash_fn rank_hash,
    void* callback_context);

/* Initializes a policy with a randomly drawn rank seed, so ranks are unpredictable to an adversary
 * choosing insertions. */
ft_status ft_canonical_policy_create_random(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config);
/* Initializes a policy with a caller-chosen private seed, which makes shapes reproducible across
 * runs without publishing the seed. */
ft_status ft_canonical_policy_create_seeded(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    uint64_t seed);
/* Initializes a policy with a published seed, so independent parties derive identical shapes for
 * identical contents. */
ft_status ft_canonical_policy_create_keyed(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    const unsigned char* rank_key,
    size_t rank_key_size);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
ft_status ft_canonical_policy_copy(
    const ft_canonical_policy* source,
    ft_canonical_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_canonical_policy_move(
    ft_canonical_policy* destination,
    ft_canonical_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_canonical_policy_dispose(ft_canonical_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_canonical_policy_same(
    const ft_canonical_policy* left,
    const ft_canonical_policy* right);
/* Whether the policy's seed is published, which is the precondition for two parties agreeing on a
 * shape. */
bool ft_canonical_policy_has_public_seed(const ft_canonical_policy* policy);
/* The published seed. */
ft_status ft_canonical_policy_public_seed(
    const ft_canonical_policy* policy,
    uint64_t* seed);
/* The rank the policy derives for an element. The rank fixes where the element sits, which is what
 * makes the shape depend only on contents. */
ft_status ft_canonical_policy_rank_for(
    const ft_canonical_policy* policy,
    const void* value,
    ft_zip_tree_rank* rank);

/* One node of a canonical sorted set. Opaque. */
typedef struct ft_canonical_node ft_canonical_node;

typedef struct ft_canonical_sorted_set {
    ft_canonical_policy_rep* policy;
    ft_canonical_node* root;
} ft_canonical_sorted_set;

typedef struct ft_canonical_sorted_set_cursor {
    ft_canonical_sorted_set set;
    size_t position;
} ft_canonical_sorted_set_cursor;

/* Sets are immutable after publication. Independently owned handles may be
 * copied and read concurrently subject to the callback contract above. Moving,
 * disposing, or otherwise writing the same handle object concurrently requires
 * external synchronization. */

typedef struct ft_canonical_sorted_set_statistics {
    size_t count;
    size_t height;
    unsigned maximum_geometric_rank;
    size_t priority_collision_count;
} ft_canonical_sorted_set_statistics;

typedef ft_status (*ft_canonical_visit_fn)(const void* value, void* context);
typedef ft_status (*ft_canonical_shape_visit_fn)(
    const void* value,
    size_t left_count,
    size_t right_count,
    void* context);

/* Initializes the set in place. */
ft_status ft_canonical_sorted_set_init(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy);
/* Initializes a set holding the given elements, built in bulk rather than by repeated insertion. */
ft_status ft_canonical_sorted_set_from_array(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same set version, taking a reference rather than copying. */
ft_status ft_canonical_sorted_set_copy(
    const ft_canonical_sorted_set* source,
    ft_canonical_sorted_set* destination);
/* Relocates an initialized set into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_canonical_sorted_set_move(
    ft_canonical_sorted_set* destination,
    ft_canonical_sorted_set* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_canonical_sorted_set_dispose(ft_canonical_sorted_set* set);

/* Whether the set holds no elements. */
bool ft_canonical_sorted_set_empty(const ft_canonical_sorted_set* set);
/* Number of elements in the set. */
size_t ft_canonical_sorted_set_size(const ft_canonical_sorted_set* set);
/* The structure's height. */
size_t ft_canonical_sorted_set_height(const ft_canonical_sorted_set* set);

/* Whether the element is present. */
ft_status ft_canonical_sorted_set_contains(
    const ft_canonical_sorted_set* set,
    const void* value,
    bool* found);
/* On success, value_ref borrows the stored representative. It remains valid
 * while the source version is retained and must not be modified or destroyed. */
ft_status ft_canonical_sorted_set_try_get_ref(
    const ft_canonical_sorted_set* set,
    const void* equal_value,
    bool* found,
    const void** value_ref);

/* Results are written only on success. A distinct result must be uninitialized
 * or disposed; exact aliasing with an operand is supported and published only
 * after the complete successor has been built. */
ft_status ft_canonical_sorted_set_add(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set* result);
/* Produces a set without that element. */
ft_status ft_canonical_sorted_set_remove(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set* result);
/* Produces an empty set retaining the same policies. */
ft_status ft_canonical_sorted_set_clear(
    const ft_canonical_sorted_set* set,
    ft_canonical_sorted_set* result);
/* Produces the elements of both sets. Subtrees the operands already share are adopted whole rather
 * than re-entered. */
ft_status ft_canonical_sorted_set_union(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result);
/* Produces the elements present in both sets. */
ft_status ft_canonical_sorted_set_intersect(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result);
/* Produces this set's elements that are absent from the other. */
ft_status ft_canonical_sorted_set_except(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result);

/* Equality is semantic across policy identities and uses the receiver's
 * comparison/rank policy when normalizing the other operand. */
ft_status ft_canonical_sorted_set_equals(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* equal);
/* Whether every element of this set also occurs in the other. */
ft_status ft_canonical_sorted_set_is_subset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
/* Whether this set is a subset of the other and the other holds an element it lacks. */
ft_status ft_canonical_sorted_set_is_proper_subset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
/* Whether every element of the other occurs in this set. */
ft_status ft_canonical_sorted_set_is_superset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
/* Whether this set is a superset of the other and holds an element the other lacks. */
ft_status ft_canonical_sorted_set_is_proper_superset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
/* Whether the two sets share at least one element. */
ft_status ft_canonical_sorted_set_overlaps(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);

/* A hash over the set's contents, equal for equal contents. */
ft_status ft_canonical_sorted_set_content_hash(
    const ft_canonical_sorted_set* set,
    uint64_t* content_hash);
/* Calls the visitor once per element, in the set's own order. */
ft_status ft_canonical_sorted_set_visit(
    const ft_canonical_sorted_set* set,
    ft_canonical_visit_fn visitor,
    void* context);
/* Calls the visitor over the structure's shape, for tests and diagnostics. */
ft_status ft_canonical_sorted_set_visit_shape(
    const ft_canonical_sorted_set* set,
    ft_canonical_shape_visit_fn visitor,
    void* context);

/* The root node's address, for tests that a no-op shared rather than copied. */
const void* ft_canonical_sorted_set_root_identity(const ft_canonical_sorted_set* set);
/* A node's address, for sharing assertions. */
ft_status ft_canonical_sorted_set_node_identity(
    const ft_canonical_sorted_set* set,
    const void* value,
    const void** identity);
/* How many nodes the two versions have in common. */
ft_status ft_canonical_sorted_set_shared_node_count(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    size_t* shared_count);

/* Structural invalidity is reported as FT_STATUS_OK with valid=false;
 * allocation, cryptographic, and callback failures remain distinguishable. */
ft_status ft_canonical_sorted_set_validate(
    const ft_canonical_sorted_set* set,
    bool* valid,
    ft_canonical_sorted_set_statistics* statistics);

/* Initializes a cursor at the given gap of the set. */
ft_status ft_canonical_sorted_set_get_cursor(
    const ft_canonical_sorted_set* set,
    size_t position,
    ft_canonical_sorted_set_cursor* result);
/* Initializes a cursor before the first key not less than the probe. */
ft_status ft_canonical_sorted_set_get_cursor_lower_bound(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set_cursor* result);
/* Initializes a cursor after any key equal to the probe. */
ft_status ft_canonical_sorted_set_get_cursor_upper_bound(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set_cursor* result);
/* Initializes a cursor at the element, reporting whether it was actually present; on a miss the
 * cursor sits at the insertion point. */
ft_status ft_canonical_sorted_set_get_cursor_at_item(
    const ft_canonical_sorted_set* set,
    const void* value,
    bool* found,
    ft_canonical_sorted_set_cursor* result);
/* Initializes a second cursor at the same position on the same version. */
ft_status ft_canonical_sorted_set_cursor_copy(
    const ft_canonical_sorted_set_cursor* source,
    ft_canonical_sorted_set_cursor* destination);
/* Relocates an initialized cursor into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_canonical_sorted_set_cursor_move(
    ft_canonical_sorted_set_cursor* destination,
    ft_canonical_sorted_set_cursor* source);
/* Releases the cursor's reference on its version. */
void ft_canonical_sorted_set_cursor_dispose(ft_canonical_sorted_set_cursor* cursor);
/* Whether the cursor is initialized and still usable. */
bool ft_canonical_sorted_set_cursor_valid(const ft_canonical_sorted_set_cursor* cursor);
/* Whether the set version the cursor is positioned in holds no elements. */
bool ft_canonical_sorted_set_cursor_empty(const ft_canonical_sorted_set_cursor* cursor);
/* Number of elements in the set version the cursor is positioned in. */
size_t ft_canonical_sorted_set_cursor_size(const ft_canonical_sorted_set_cursor* cursor);
/* The cursor's gap position. */
size_t ft_canonical_sorted_set_cursor_position(const ft_canonical_sorted_set_cursor* cursor);
/* Whether the gap precedes the first element. */
ft_status ft_canonical_sorted_set_cursor_is_at_start(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* result);
/* Whether the gap follows the last element. */
ft_status ft_canonical_sorted_set_cursor_is_at_end(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* result);
/* Borrowed representative references remain valid while the cursor is retained. */
ft_status ft_canonical_sorted_set_cursor_try_peek_previous_ref(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* found,
    const void** value_ref);
/* Borrows the element after the gap in place, reporting whether one exists. */
ft_status ft_canonical_sorted_set_cursor_try_peek_next_ref(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* found,
    const void** value_ref);
/* Moves the cursor one position earlier. */
ft_status ft_canonical_sorted_set_cursor_move_previous(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result);
/* Moves the cursor one position later. */
ft_status ft_canonical_sorted_set_cursor_move_next(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result);
/* Moves the cursor to the given rank within the same version. */
ft_status ft_canonical_sorted_set_cursor_seek_rank(
    const ft_canonical_sorted_set_cursor* cursor,
    size_t position,
    ft_canonical_sorted_set_cursor* result);
/* Produces a set containing the given element. */
ft_status ft_canonical_sorted_set_cursor_add(
    const ft_canonical_sorted_set_cursor* cursor,
    const void* value,
    ft_canonical_sorted_set_cursor* result);
/* Removes the element before the gap, producing a new version the cursor is then positioned in. */
ft_status ft_canonical_sorted_set_cursor_delete_previous(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result);
/* Removes the element after the gap, producing a new version the cursor is then positioned in. */
ft_status ft_canonical_sorted_set_cursor_delete_next(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result);
/* Initializes a handle on the set version this cursor is positioned in. */
ft_status ft_canonical_sorted_set_cursor_snapshot(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set* result);

#ifdef __cplusplus
}
#endif

#endif
