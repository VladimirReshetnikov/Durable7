/*
 * A checkpoint-differential persistent ordered map: current state, a designated checkpoint, and an
 * ordered index of the exact net changes between them.
 *
 * A version carries three immutable roots: the checkpoint state B, the current state S, and a
 * change index D holding exactly one before/after record for every key class on which B and S
 * differ under the value-equivalence policy. The first effective write to a class captures its
 * checkpoint-relative before endpoint; later writes replace only the after endpoint, so repeated
 * writes coalesce; returning a class to its checkpoint state removes its record entirely. When D
 * becomes empty the current root snaps back to the exact checkpoint root, so a wholly cancelled
 * epoch is storage-clean as well as extensionally clean and checkpoint and rollback stay O(1)
 * identity operations.
 *
 * Let N = max(2, |dom(B) union dom(S)|), counting key-policy equivalence classes rather than
 * concrete key representatives, and let k = |D| be the number of net-changed classes. Point lookup,
 * point update and removal, rank, and neighbor queries are O(log N), matching the underlying
 * persistent ordered map. Checkpoint and rollback are O(1). Reading one change is O(log(k + 1)),
 * searching D rather than S. A full change enumeration is Theta(k + 1), which is output-optimal and
 * is the whole point of the design: it avoids the Theta(k log(N/k + 1)) adversarial divergent-path
 * walk that a reference-pruned comparison between two constant-degree path-copied balanced trees
 * must perform, including Theta(log N) work to report a single change. A live version occupies
 * O(N + k) nodes; retaining every successor adds O(log N) nodes per changed point update, the same
 * asymptotic persistence cost as an ordinary path-copied ordered map. These are the shipped bounds
 * of this path-copying implementation; no stronger worst-case update-space refinement is claimed.
 *
 * The mutation surface is deliberately point-only. An eager bulk clear would produce Theta(N)
 * removal records and cannot simultaneously retain an ordinary map's O(1) clear bound, so callers
 * start a fresh epoch with ft_delta_map_init when they do not need an enumerable delta, and remove
 * entries explicitly when they do. ft_delta_map_set_items is not an exception: it is exactly the
 * fold over ft_delta_map_set_item and claims no bound better than the sequence of point writes it
 * replaces.
 *
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection. Key identity and enumeration order come
 * from the policy's key comparison; semantic no-ops and change cancellation come from its separate
 * value equivalence. "Exact" therefore means exact extensionally under those two policies, not by
 * identity of the retained key or value representatives. A baseline key representative is retained
 * across point updates and delete/re-add round trips; a newly added class keeps its first
 * representative while its net-added record is active, and a later addition after complete
 * cancellation begins a new representative episode.
 *
 * The ordered core is private to this module rather than the workspace's ft_sorted_map, because
 * that map's copy hooks return void and cannot report a failing caller callback, while this
 * structure's contract requires every policy callback failure to propagate as
 * FT_STATUS_CALLBACK_FAILURE without publishing a partial successor.
 */

#ifndef DURABLE7_FINGER_TREE_C_PERSISTENT_DELTA_MAP_H
#define DURABLE7_FINGER_TREE_C_PERSISTENT_DELTA_MAP_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_delta_map_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_delta_map_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_delta_map_compare_fn)(
    const void* left,
    const void* right,
    int* comparison,
    void* context);
typedef ft_status (*ft_delta_map_equal_fn)(
    const void* left,
    const void* right,
    bool* equal,
    void* context);
typedef void* (*ft_delta_map_allocate_fn)(size_t size, void* context);
typedef void (*ft_delta_map_deallocate_fn)(void* allocation, void* context);

typedef struct ft_delta_map_allocator {
    ft_delta_map_allocate_fn allocate;
    ft_delta_map_deallocate_fn deallocate;
    void* context;
} ft_delta_map_allocator;

/* key_type_identity and value_type_identity are required, stable, non-null caller-owned tags. A
 * copy hook constructs an owned value in uninitialized storage; on failure it must leave that
 * storage ownership-free. Destroy and allocator hooks must return normally. compare_key must define
 * a stable total preorder and value_equal a stable reflexive, symmetric, transitive equivalence;
 * their side effects cannot be undone, but a failing hook never publishes a partial successor.
 * Hooks must not reenter an in-flight operation through the same policy or map handles. Distinct
 * immutable handles may be used concurrently only when every reachable hook and context is safe for
 * that call pattern. */
typedef struct ft_delta_map_policy_config {
    size_t key_size;
    const void* key_type_identity;
    ft_delta_map_copy_fn key_copy;
    ft_delta_map_destroy_fn key_destroy;
    ft_delta_map_compare_fn compare_key;
    size_t value_size;
    const void* value_type_identity;
    ft_delta_map_copy_fn value_copy;
    ft_delta_map_destroy_fn value_destroy;
    ft_delta_map_equal_fn value_equal;
    void* callback_context;
    ft_delta_map_allocator allocator;
} ft_delta_map_policy_config;

/* The shared, reference-counted representation behind a delta-map policy. */
typedef struct ft_delta_map_policy_rep ft_delta_map_policy_rep;

/* Policy identity gates operations requiring policy identity. Copy preserves identity;
 * independently recreating an equivalent config deliberately does not. */
typedef struct ft_delta_map_policy {
    ft_delta_map_policy_rep* rep;
} ft_delta_map_policy;

/* One node of a persistent ordered root. Opaque: handles are the API. */
typedef struct ft_delta_map_node ft_delta_map_node;

/* The three immutable roots of one version, plus the retained policy. Copy, move, and dispose the
 * handle explicitly; never copy it bytewise. */
typedef struct ft_delta_map {
    ft_delta_map_policy_rep* policy;
    ft_delta_map_node* current;
    ft_delta_map_node* checkpoint;
    ft_delta_map_node* changes;
} ft_delta_map;

/* Classifies one net key change relative to a version's checkpoint. */
typedef enum ft_delta_map_change_kind {
    /* The key class is absent at the checkpoint and present in the current map. */
    FT_DELTA_MAP_CHANGE_ADDED = 0,
    /* The key class is present at the checkpoint and absent from the current map. */
    FT_DELTA_MAP_CHANGE_REMOVED = 1,
    /* The key class is present at both endpoints with non-equivalent values. */
    FT_DELTA_MAP_CHANGE_UPDATED = 2
} ft_delta_map_change_kind;

/* One presence-safe endpoint of a change. C# needs a DeltaMapValue<T> wrapper and Rust an Option
 * only because a stored value may itself be null or empty; the C equivalent is this explicit
 * has_value flag beside the storage. When has_value is false the value pointer is null and must not
 * be read. */
typedef struct ft_delta_map_endpoint {
    bool has_value;
    const void* value;
} ft_delta_map_endpoint;

/* One exact net change between a version's checkpoint and its current state. The pointers borrow
 * the retained representatives and stay valid while the source version is retained. The key is the
 * checkpoint representative when the class was present at the checkpoint; otherwise it is the
 * current addition episode's first representative. */
typedef struct ft_delta_map_change {
    const void* key;
    ft_delta_map_endpoint before;
    ft_delta_map_endpoint after;
} ft_delta_map_change;

typedef struct ft_delta_map_statistics {
    size_t size;
    size_t checkpoint_size;
    size_t change_count;
    size_t added_count;
    size_t removed_count;
    size_t updated_count;
    bool is_clean;
} ft_delta_map_statistics;

/* A visitor returning non-OK aborts the traversal and propagates that status. */
typedef ft_status (*ft_delta_map_entry_visit_fn)(
    const void* key,
    const void* value,
    void* context);
typedef ft_status (*ft_delta_map_change_visit_fn)(
    const ft_delta_map_change* change,
    void* context);

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
void ft_delta_map_policy_config_init(
    ft_delta_map_policy_config* config,
    size_t key_size,
    const void* key_type_identity,
    ft_delta_map_compare_fn compare_key,
    size_t value_size,
    const void* value_type_identity,
    ft_delta_map_equal_fn value_equal,
    void* callback_context);
/* Initializes a policy from the supplied configuration, which it retains. */
ft_status ft_delta_map_policy_create(
    ft_delta_map_policy* policy,
    const ft_delta_map_policy_config* config);
/* Initializes a second handle on the same policy, taking a reference rather than copying. */
ft_status ft_delta_map_policy_copy(
    const ft_delta_map_policy* source,
    ft_delta_map_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_delta_map_policy_move(
    ft_delta_map_policy* destination,
    ft_delta_map_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_delta_map_policy_dispose(ft_delta_map_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_delta_map_policy_same(
    const ft_delta_map_policy* left,
    const ft_delta_map_policy* right);

/* Initializes the empty map, whose current state is also its checkpoint. O(1). This is also the
 * O(1) way to start an unrelated clean epoch; it is deliberately not a clear that retains the old
 * checkpoint. */
ft_status ft_delta_map_init(ft_delta_map* map, const ft_delta_map_policy* policy);
/* Initializes a clean checkpoint from count parallel key and value elements, with the last
 * equivalent key winning: the last entry of a class supplies both the retained key representative
 * and the value. O(m log m) for m entries. */
ft_status ft_delta_map_from_entries(
    ft_delta_map* map,
    const ft_delta_map_policy* policy,
    const void* keys,
    const void* values,
    size_t count);
/* Initializes a second handle on the same version, taking a reference rather than copying. */
ft_status ft_delta_map_copy(const ft_delta_map* source, ft_delta_map* destination);
/* Relocates an initialized map into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_delta_map_move(ft_delta_map* destination, ft_delta_map* source);
/* Releases this handle's references. Other versions sharing the same nodes stay valid. */
void ft_delta_map_dispose(ft_delta_map* map);
/* Initializes a handle on this version's retained policy. */
ft_status ft_delta_map_get_policy(const ft_delta_map* map, ft_delta_map_policy* policy);

/* Whether the current map holds no entries. O(1). */
bool ft_delta_map_empty(const ft_delta_map* map);
/* Number of current entries. O(1). */
size_t ft_delta_map_size(const ft_delta_map* map);
/* Number of checkpoint entries. O(1). */
size_t ft_delta_map_checkpoint_size(const ft_delta_map* map);
/* Number of exact net-changed key classes. O(1). */
size_t ft_delta_map_change_count(const ft_delta_map* map);
/* Whether the current map differs semantically from its checkpoint. O(1). */
bool ft_delta_map_has_changes(const ft_delta_map* map);

/* Whether a current entry exists for the key's class. O(log N). */
ft_status ft_delta_map_contains_key(
    const ft_delta_map* map,
    const void* key,
    bool* found);
/* The key class's zero-based rank among the current entries. O(log N). */
ft_status ft_delta_map_index_of_key(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    size_t* index);

/* On found=true, the outputs borrow the stored representatives and remain valid while the source
 * version is retained. Either output may be null to skip it. O(log N). */
ft_status ft_delta_map_try_get_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* On found=true, the non-null outputs receive independently owned copies the caller destroys with
 * the policy's destroy callbacks. O(log N). */
ft_status ft_delta_map_try_get_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out);
/* Borrows the current entry at zero-based key rank. O(log N). */
ft_status ft_delta_map_entry_at_ref(
    const ft_delta_map* map,
    size_t index,
    const void** key_ref,
    const void** value_ref);
/* Copies the current entry at zero-based key rank. O(log N). */
ft_status ft_delta_map_entry_at_copy(
    const ft_delta_map* map,
    size_t index,
    void* key_out,
    void* value_out);
/* Borrows the least current entry by key. Theta(log N): the ordered core stores subtree sizes but
 * caches no extremes, so an extreme costs the same descent as any other rank. */
ft_status ft_delta_map_try_min_entry_ref(
    const ft_delta_map* map,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* Copies the least current entry by key. Theta(log N). */
ft_status ft_delta_map_try_min_entry_copy(
    const ft_delta_map* map,
    bool* found,
    void* key_out,
    void* value_out);
/* Borrows the greatest current entry by key. Theta(log N). */
ft_status ft_delta_map_try_max_entry_ref(
    const ft_delta_map* map,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* Copies the greatest current entry by key. Theta(log N). */
ft_status ft_delta_map_try_max_entry_copy(
    const ft_delta_map* map,
    bool* found,
    void* key_out,
    void* value_out);
/* Borrows the greatest current entry whose key is at most the probe. O(log N). */
ft_status ft_delta_map_try_floor_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* Copies the greatest current entry whose key is at most the probe. O(log N). */
ft_status ft_delta_map_try_floor_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out);
/* Borrows the least current entry whose key is at least the probe. O(log N). */
ft_status ft_delta_map_try_ceiling_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* Copies the least current entry whose key is at least the probe. O(log N). */
ft_status ft_delta_map_try_ceiling_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out);
/* Borrows the greatest current entry whose key is strictly below the probe. O(log N). */
ft_status ft_delta_map_try_lower_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* Copies the greatest current entry whose key is strictly below the probe. O(log N). */
ft_status ft_delta_map_try_lower_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out);
/* Borrows the least current entry whose key is strictly above the probe. O(log N). */
ft_status ft_delta_map_try_higher_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref);
/* Copies the least current entry whose key is strictly above the probe. O(log N). */
ft_status ft_delta_map_try_higher_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out);

/* Calls the visitor once per current entry in ascending key order. Theta(n) and free of policy
 * callbacks. */
ft_status ft_delta_map_visit(
    const ft_delta_map* map,
    ft_delta_map_entry_visit_fn visitor,
    void* context);
/* Calls the visitor once per checkpoint entry in ascending key order. Theta(n) and free of policy
 * callbacks. */
ft_status ft_delta_map_visit_checkpoint(
    const ft_delta_map* map,
    ft_delta_map_entry_visit_fn visitor,
    void* context);
/* Calls the visitor over the current entries whose keys lie in the inclusive range [low, high],
 * which is the read this workspace spells with a visitor rather than a range snapshot. The range is
 * located by seeking its boundaries, so the cost is O(log N) plus Theta(output) rather than a scan.
 * An inverted range, whose low compares greater than its high under the retained key policy, yields
 * nothing. */
ft_status ft_delta_map_visit_range(
    const ft_delta_map* map,
    const void* low,
    const void* high,
    ft_delta_map_entry_visit_fn visitor,
    void* context);

/* Looks up one exact checkpoint-relative net change, borrowing its retained representatives.
 * O(log(k + 1)), searching the change index rather than the current state. */
ft_status ft_delta_map_try_get_change_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    ft_delta_map_change* change);
/* On found=true, the non-null outputs receive independently owned copies the caller destroys with
 * the policy's destroy callbacks; a present endpoint sets its has_value flag and is copied, an
 * absent endpoint clears the flag and leaves its storage untouched. O(log(k + 1)). */
ft_status ft_delta_map_try_get_change_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    bool* before_has_value,
    void* before_out,
    bool* after_has_value,
    void* after_out);
/* The change classification implied by the two endpoints. A record absent at both endpoints is not
 * a net change, is never produced, and is reported as FT_STATUS_INVALID_ARGUMENT. */
ft_status ft_delta_map_change_kind_of(
    const ft_delta_map_change* change,
    ft_delta_map_change_kind* kind);
/* Calls the visitor once per exact net change in ascending key order. Theta(k + 1) and free of key
 * and value policy callbacks. */
ft_status ft_delta_map_visit_changes(
    const ft_delta_map* map,
    ft_delta_map_change_visit_fn visitor,
    void* context);
/* Calls the visitor over the exact net changes whose keys lie in the inclusive range [low, high],
 * in the same ascending key order the full enumeration uses. The change index is itself an ordered
 * map under the key policy, so this seeks the range boundaries and walks the in-range sub-map
 * rather than filtering all k records: O(log(k + 1)) key comparisons plus Theta(output), and no
 * value comparisons at all. An inverted range yields nothing, matching the range read over the
 * current state; "inverted" is decided by the retained key policy, so under a descending key order
 * the low endpoint is the numerically greater key. */
ft_status ft_delta_map_visit_changes_in_range(
    const ft_delta_map* map,
    const void* low,
    const void* high,
    ft_delta_map_change_visit_fn visitor,
    void* context);

/* Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N). A
 * write whose value is equivalent to the current value is a semantic no-op producing a version that
 * shares all three roots; a write returning a class to its checkpoint state removes that class's
 * record. Results are published only on success, and exact result/operand aliasing is supported. */
ft_status ft_delta_map_set_item(
    const ft_delta_map* map,
    const void* key,
    const void* value,
    ft_delta_map* result);
/* Applies count parallel key and value elements in order, exactly as repeated single assignment
 * would. O(m log N) for m entries. This is a convenience and an allocation saving, not a different
 * algorithm: it is exactly the fold over ft_delta_map_set_item, so every coalescing, cancellation,
 * representative-retention, and checkpoint-snap rule holds verbatim and no stronger bound is
 * claimed. A later equivalent key wins. An empty sequence, or one whose every entry is a semantic
 * no-op, produces a version sharing all three roots. Because each intermediate value is itself a
 * complete immutable successor, a callback failure partway leaves the receiver and every retained
 * branch unchanged. */
ft_status ft_delta_map_set_items(
    const ft_delta_map* map,
    const void* keys,
    const void* values,
    size_t count,
    ft_delta_map* result);
/* Removes one current entry and coalesces its checkpoint-relative change. O(log N). Removing an
 * absent key is a no-op producing a version that shares all three roots; removing a class added
 * since the checkpoint cancels its record instead of recording a removal. */
ft_status ft_delta_map_remove(
    const ft_delta_map* map,
    const void* key,
    ft_delta_map* result);
/* Makes the current state the new checkpoint and resets the change index. O(1), and free of key and
 * value policy callbacks. An already-clean version produces a version sharing all of its storage. */
ft_status ft_delta_map_checkpoint(const ft_delta_map* map, ft_delta_map* result);
/* Returns to this version's checkpoint and resets the change index. O(1), and free of key and value
 * policy callbacks. An already-clean version produces a version sharing all of its storage. */
ft_status ft_delta_map_rollback(const ft_delta_map* map, ft_delta_map* result);

/* The current root's address, for tests that a no-op shared rather than copied. */
const void* ft_delta_map_current_identity(const ft_delta_map* map);
/* The checkpoint root's address, for tests that a checkpoint or rollback shared rather than
 * copied. */
const void* ft_delta_map_checkpoint_identity(const ft_delta_map* map);
/* The change index root's address, for tests that a successor shared rather than copied. */
const void* ft_delta_map_changes_identity(const ft_delta_map* map);
/* Whether both versions are backed by the same policy and the same three roots, so neither can
 * observe a change made through the other. A representation test, not an equality test. O(1). */
bool ft_delta_map_shares_storage(const ft_delta_map* left, const ft_delta_map* right);

/* Walks the whole representation. This is diagnostic validation, not part of any operation's cost:
 * it is O((N + k) log N) and does invoke key and value policy callbacks. Structural invalidity is
 * FT_STATUS_OK with valid=false. Callback failures remain distinguishable and outputs change only
 * on success. */
ft_status ft_delta_map_validate(
    const ft_delta_map* map,
    bool* valid,
    ft_delta_map_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
