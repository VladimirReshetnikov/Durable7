#ifndef TOOLS_DATA_STRUCTURES_FINGER_TREE_C_CANONICAL_SORTED_SET_H
#define TOOLS_DATA_STRUCTURES_FINGER_TREE_C_CANONICAL_SORTED_SET_H

#include <tools/data_structures/finger_tree/fingertree.h>

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

typedef struct ft_canonical_policy_rep ft_canonical_policy_rep;

/* Policy handles are identity-bearing and reference counted. Copying a handle
 * preserves algebra compatibility; independently recreating an identical seed
 * or key reproduces ranks but intentionally has different policy identity. */
typedef struct ft_canonical_policy {
    ft_canonical_policy_rep* rep;
} ft_canonical_policy;

void ft_canonical_policy_config_init(
    ft_canonical_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_canonical_compare_fn compare,
    ft_canonical_rank_hash_fn rank_hash,
    void* callback_context);

ft_status ft_canonical_policy_create_random(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config);
ft_status ft_canonical_policy_create_seeded(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    uint64_t seed);
ft_status ft_canonical_policy_create_keyed(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    const unsigned char* rank_key,
    size_t rank_key_size);
ft_status ft_canonical_policy_copy(
    const ft_canonical_policy* source,
    ft_canonical_policy* destination);
void ft_canonical_policy_move(
    ft_canonical_policy* destination,
    ft_canonical_policy* source);
void ft_canonical_policy_dispose(ft_canonical_policy* policy);
bool ft_canonical_policy_same(
    const ft_canonical_policy* left,
    const ft_canonical_policy* right);
bool ft_canonical_policy_has_public_seed(const ft_canonical_policy* policy);
ft_status ft_canonical_policy_public_seed(
    const ft_canonical_policy* policy,
    uint64_t* seed);
ft_status ft_canonical_policy_rank_for(
    const ft_canonical_policy* policy,
    const void* value,
    ft_zip_tree_rank* rank);

typedef struct ft_canonical_node ft_canonical_node;

typedef struct ft_canonical_sorted_set {
    ft_canonical_policy_rep* policy;
    ft_canonical_node* root;
} ft_canonical_sorted_set;

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

ft_status ft_canonical_sorted_set_init(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy);
ft_status ft_canonical_sorted_set_from_array(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy,
    const void* values,
    size_t count);
ft_status ft_canonical_sorted_set_copy(
    const ft_canonical_sorted_set* source,
    ft_canonical_sorted_set* destination);
void ft_canonical_sorted_set_move(
    ft_canonical_sorted_set* destination,
    ft_canonical_sorted_set* source);
void ft_canonical_sorted_set_dispose(ft_canonical_sorted_set* set);

bool ft_canonical_sorted_set_empty(const ft_canonical_sorted_set* set);
size_t ft_canonical_sorted_set_size(const ft_canonical_sorted_set* set);
size_t ft_canonical_sorted_set_height(const ft_canonical_sorted_set* set);

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
ft_status ft_canonical_sorted_set_remove(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set* result);
ft_status ft_canonical_sorted_set_clear(
    const ft_canonical_sorted_set* set,
    ft_canonical_sorted_set* result);
ft_status ft_canonical_sorted_set_union(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result);
ft_status ft_canonical_sorted_set_intersect(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result);
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
ft_status ft_canonical_sorted_set_is_subset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
ft_status ft_canonical_sorted_set_is_proper_subset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
ft_status ft_canonical_sorted_set_is_superset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
ft_status ft_canonical_sorted_set_is_proper_superset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);
ft_status ft_canonical_sorted_set_overlaps(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result);

ft_status ft_canonical_sorted_set_content_hash(
    const ft_canonical_sorted_set* set,
    uint64_t* content_hash);
ft_status ft_canonical_sorted_set_visit(
    const ft_canonical_sorted_set* set,
    ft_canonical_visit_fn visitor,
    void* context);
ft_status ft_canonical_sorted_set_visit_shape(
    const ft_canonical_sorted_set* set,
    ft_canonical_shape_visit_fn visitor,
    void* context);

const void* ft_canonical_sorted_set_root_identity(const ft_canonical_sorted_set* set);
ft_status ft_canonical_sorted_set_node_identity(
    const ft_canonical_sorted_set* set,
    const void* value,
    const void** identity);
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

#ifdef __cplusplus
}
#endif

#endif
