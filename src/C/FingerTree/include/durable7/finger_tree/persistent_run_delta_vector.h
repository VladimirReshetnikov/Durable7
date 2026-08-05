/*
 * A fixed-length persistent vector carrying a checkpoint and an exact maximal-run index of the
 * positions whose current values differ from that checkpoint.
 *
 * A version holds two immutable RRB roots -- the current values and the checkpoint values -- plus
 * one persistent ordered index storing a start/end-exclusive record for every MAXIMAL run of
 * differing positions. The published runs are ordered, non-overlapping, non-adjacent, and maximal,
 * so a contiguous edited block of any size is one record. Dirtiness is relative to the retained
 * equality callback, not to write history: writing an equal value is a semantic no-op, and
 * returning a position to its checkpoint equality class cancels that position's delta and restores
 * the exact checkpoint representative.
 *
 * Let n be the fixed length, k the number of dirty positions, and r the number of maximal dirty
 * runs, so 0 <= r <= k <= n. Against an unindexed pair of current/checkpoint roots the one strict
 * result is that exact dirty-run descriptor discovery is output-optimal Theta(r) rather than
 * worst-case Theta(n), and no operation loses the baseline's asymptotic time or space bound. The
 * gap is unbounded: one contiguous changed block gives k = n and r = 1. Emitting the changed
 * payload VALUES is still Omega(k) and is not claimed to be faster.
 *
 * Shipped bounds:
 *   build n values                        Theta(n)
 *   read a current or checkpoint value    O(log n)
 *   persistent point edit                 O(log n) amortized time and fresh nodes
 *   fork by copying a handle              O(1)
 *   whole checkpoint or rollback          O(1)
 *   visit current values                  Theta(n)
 *   dirty membership                      O(log(r + 2)) amortized
 *   exact dirty-run descriptors           Theta(r)
 *   select a dirty run by rank            O(log(r + 2)) amortized
 *   accept or revert one indexed run      O(log n) amortized, independent of that run's length
 *   live delta metadata                   Theta(r)
 *   total live structure                  O(n)
 *
 * Accepting or reverting one run is implemented by structural RRB splitting and concatenation,
 * never by walking that run's positions, which is what makes it independent of the run's length,
 * and it makes no equality call at all. That splice bound is inherited from RRB trees and is not
 * itself a novelty claim.
 *
 * REPRESENTATIVE IDENTITY. The invariant "a clean position reuses its exact checkpoint
 * representative" is about identity, not value: the retained equality callback may be coarser or
 * finer than representative identity, so it cannot decide it. C# expresses this with a private cell
 * class and Rust with Arc pointer equality. Here each position stores a pointer to a
 * reference-counted cell that owns one copy of the value, and cell address IS representative
 * identity. The honest per-position cost of that choice is one pointer slot in each root plus, per
 * DISTINCT representative, two allocations -- the cell header and the value bytes it owns, matching
 * the boxing the neighbouring modules use for shared values. A clean version therefore holds
 * exactly n cells and both roots share every one of them; each effective point edit adds exactly
 * one cell and retains the rest. Cell identity also turns the underlying vector's
 * equal-replacement short circuit into a pointer test, which is exactly the test wanted there, so
 * rewriting a position with its own representative allocates nothing.
 *
 * Length is fixed at construction: there is deliberately no insertion, deletion, concatenation,
 * split, range assignment, or merge of unrelated branches, because checkpoint correspondence would
 * need a position-transformation policy this abstract data type does not define. There is one
 * checkpoint per version.
 *
 * Every version is immutable and every producing operation leaves its operand valid, so any
 * retained version can be branched again. Every equality call an edit makes happens before any
 * successor structure is built, so a failing callback publishes no partial version. Distinct
 * immutable handles may be used concurrently only when every reachable callback and context is safe
 * for that call pattern; the equality callback must stay one stable equivalence relation, and
 * values retained by a version must not mutate state that callback observes, or the maintained
 * index can go stale.
 */

#ifndef DURABLE7_FINGER_TREE_C_PERSISTENT_RUN_DELTA_VECTOR_H
#define DURABLE7_FINGER_TREE_C_PERSISTENT_RUN_DELTA_VECTOR_H

#include <durable7/finger_tree/fingertree.h>
#include <durable7/finger_tree/rrb_vector.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_run_delta_value_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_run_delta_value_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_run_delta_equal_fn)(
    const void* left,
    const void* right,
    bool* equal,
    void* context);
typedef void* (*ft_run_delta_allocate_fn)(size_t size, void* context);
typedef void (*ft_run_delta_deallocate_fn)(void* allocation, void* context);

typedef struct ft_run_delta_allocator {
    ft_run_delta_allocate_fn allocate;
    ft_run_delta_deallocate_fn deallocate;
    void* context;
} ft_run_delta_allocator;

/* value_type_identity is a required, stable, non-null caller-owned tag. Copy
 * constructs an owned value in uninitialized storage; on failure it must leave
 * that storage ownership-free. A null copy means the value is trivially
 * copyable and is then memcpy'd, in which case destroy must also be null.
 * Destroy, equal, and allocator hooks must return normally. Equal defines
 * checkpoint-relative dirtiness and must be a stable equivalence relation for
 * every retained version's lifetime; it must not report a value unequal to
 * itself, or a rewrite would record a phantom run. Hooks must not reenter an
 * in-flight operation through the same policy/vector handles. */
typedef struct ft_run_delta_policy_config {
    size_t value_size;
    const void* value_type_identity;
    ft_run_delta_value_copy_fn copy;
    ft_run_delta_value_destroy_fn destroy;
    ft_run_delta_equal_fn equal;
    void* callback_context;
    ft_run_delta_allocator allocator;
} ft_run_delta_policy_config;

/* The shared, reference-counted representation behind a run-delta vector policy. */
typedef struct ft_run_delta_policy_rep ft_run_delta_policy_rep;

/* Policy identity is the retained equality relation. Copy preserves identity;
 * independently recreating an equivalent config deliberately does not. */
typedef struct ft_run_delta_policy {
    ft_run_delta_policy_rep* rep;
} ft_run_delta_policy;

/* One node of the persistent maximal-run index. Opaque. */
typedef struct ft_run_delta_run_node ft_run_delta_run_node;

/* One maximal half-open run of checkpoint-relative changes, covering the positions
 * start .. start + length. A published run is never empty. */
typedef struct ft_run_delta_run {
    size_t start;
    size_t length;
} ft_run_delta_run;

typedef struct ft_run_delta_vector {
    ft_run_delta_policy_rep* policy;
    ft_rrb_vector current;
    ft_rrb_vector checkpoint;
    ft_run_delta_run_node* runs;
    size_t dirty_count;
} ft_run_delta_vector;

typedef struct ft_run_delta_statistics {
    size_t count;
    size_t dirty_count;
    size_t dirty_run_count;
} ft_run_delta_statistics;

typedef ft_status (*ft_run_delta_visit_fn)(const void* value, void* context);
typedef ft_status (*ft_run_delta_run_visit_fn)(
    const ft_run_delta_run* run,
    void* context);

/* The exclusive end position of a run. */
size_t ft_run_delta_run_end_exclusive(const ft_run_delta_run* run);
/* Whether the position lies inside the run. */
bool ft_run_delta_run_contains(const ft_run_delta_run* run, size_t index);

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
void ft_run_delta_policy_config_init(
    ft_run_delta_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_run_delta_equal_fn equal,
    void* callback_context);
/* Initializes a policy from the supplied configuration, which it retains. */
ft_status ft_run_delta_policy_create(
    ft_run_delta_policy* policy,
    const ft_run_delta_policy_config* config);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
ft_status ft_run_delta_policy_copy(
    const ft_run_delta_policy* source,
    ft_run_delta_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_run_delta_policy_move(
    ft_run_delta_policy* destination,
    ft_run_delta_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_run_delta_policy_dispose(ft_run_delta_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_run_delta_policy_same(
    const ft_run_delta_policy* left,
    const ft_run_delta_policy* right);

/* Initializes the clean empty vector in place. */
ft_status ft_run_delta_vector_init(
    ft_run_delta_vector* vector,
    const ft_run_delta_policy* policy);
/* Initializes a clean fixed-length vector holding the given values, built in bulk rather than by
 * repeated insertion. Theta(n): both roots start as one shared root, so the version begins with no
 * dirty runs. */
ft_status ft_run_delta_vector_from_array(
    ft_run_delta_vector* vector,
    const ft_run_delta_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same vector version, taking a reference rather than copying.
 * O(1): this is how a version is forked, and both handles stay independently editable. */
ft_status ft_run_delta_vector_copy(
    const ft_run_delta_vector* source,
    ft_run_delta_vector* destination);
/* Relocates an initialized vector into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_run_delta_vector_move(
    ft_run_delta_vector* destination,
    ft_run_delta_vector* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_run_delta_vector_dispose(ft_run_delta_vector* vector);
/* Initializes a handle on this version's retained policy. */
ft_status ft_run_delta_vector_get_policy(
    const ft_run_delta_vector* vector,
    ft_run_delta_policy* policy);

/* Whether the vector has no positions. */
bool ft_run_delta_vector_empty(const ft_run_delta_vector* vector);
/* The fixed number of positions. */
size_t ft_run_delta_vector_size(const ft_run_delta_vector* vector);
/* The number of positions that differ from the checkpoint. O(1). */
size_t ft_run_delta_vector_dirty_count(const ft_run_delta_vector* vector);
/* The number of maximal dirty runs. O(1). */
size_t ft_run_delta_vector_dirty_run_count(const ft_run_delta_vector* vector);
/* Whether at least one current value differs from its checkpoint value. O(1). */
bool ft_run_delta_vector_has_changes(const ft_run_delta_vector* vector);

/* On success, value_ref borrows the stored representative and remains valid
 * while the source version is retained. O(log n). */
ft_status ft_run_delta_vector_at_ref(
    const ft_run_delta_vector* vector,
    size_t index,
    const void** value_ref);
/* On success, value is independently owned and must be destroyed with the
 * policy's destroy callback when non-null. O(log n). */
ft_status ft_run_delta_vector_at_copy(
    const ft_run_delta_vector* vector,
    size_t index,
    void* value);
/* On success, value_ref borrows the stored checkpoint representative and
 * remains valid while the source version is retained. O(log n). */
ft_status ft_run_delta_vector_checkpoint_at_ref(
    const ft_run_delta_vector* vector,
    size_t index,
    const void** value_ref);
/* On success, value is an independently owned copy of the checkpoint value and
 * must be destroyed with the policy's destroy callback when non-null.
 * O(log n). */
ft_status ft_run_delta_vector_checkpoint_at_copy(
    const ft_run_delta_vector* vector,
    size_t index,
    void* value);

/* Whether one position differs from its checkpoint value. A position outside the vector is
 * FT_STATUS_OUT_OF_RANGE. O(log(r + 2)) amortized. */
ft_status ft_run_delta_vector_is_dirty(
    const ft_run_delta_vector* vector,
    size_t index,
    bool* dirty);
/* The maximal dirty run containing one position. On found=true, run receives that run; a clean
 * position is found=false rather than a failure. O(log(r + 2)) amortized. */
ft_status ft_run_delta_vector_try_get_dirty_run_containing(
    const ft_run_delta_vector* vector,
    size_t index,
    bool* found,
    ft_run_delta_run* run);
/* The maximal dirty run at a zero-based ascending-start rank. A rank outside the run index is
 * FT_STATUS_OUT_OF_RANGE. O(log(r + 2)) amortized. */
ft_status ft_run_delta_vector_dirty_run_at(
    const ft_run_delta_vector* vector,
    size_t rank,
    ft_run_delta_run* run);

/* Results are published only on success, and result may alias the operand. */

/* Produces a version with one current value replaced. An equal replacement is a semantic no-op
 * that keeps the existing current representative; a replacement landing back in the checkpoint's
 * equality class cancels that position's delta and restores the exact checkpoint representative,
 * and when the last dirty position clears the current root snaps back to the checkpoint root.
 * Otherwise the position becomes or stays dirty and at most two neighbouring run records change.
 * O(log n) amortized time and fresh nodes. */
ft_status ft_run_delta_vector_set(
    const ft_run_delta_vector* vector,
    size_t index,
    const void* value,
    ft_run_delta_vector* result);
/* Produces a version whose current value at one position is reset to its checkpoint value. An
 * already clean position is a no-op that produces the receiver's contents. O(log n) amortized. */
ft_status ft_run_delta_vector_reset(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_vector* result);
/* Accepts every current value as a new checkpoint. O(1): a root swap, not a copy. An already clean
 * version produces its own contents. */
ft_status ft_run_delta_vector_checkpoint(
    const ft_run_delta_vector* vector,
    ft_run_delta_vector* result);
/* Reverts every current value to the checkpoint. O(1): a root swap, not a copy. An already clean
 * version produces its own contents. */
ft_status ft_run_delta_vector_rollback(
    const ft_run_delta_vector* vector,
    ft_run_delta_vector* result);

/* Accepts the maximal dirty run at a rank into the checkpoint, leaving every other run untouched.
 * Structural RRB splicing, so O(log n) amortized INDEPENDENTLY of that run's length, and it makes
 * no equality call at all. A rank outside the run index is FT_STATUS_OUT_OF_RANGE. */
ft_status ft_run_delta_vector_accept_dirty_run_at(
    const ft_run_delta_vector* vector,
    size_t rank,
    ft_run_delta_vector* result);
/* Reverts the maximal dirty run at a rank from the checkpoint, leaving every other run untouched.
 * Structural RRB splicing, so O(log n) amortized INDEPENDENTLY of that run's length, and it makes
 * no equality call at all. A rank outside the run index is FT_STATUS_OUT_OF_RANGE. */
ft_status ft_run_delta_vector_revert_dirty_run_at(
    const ft_run_delta_vector* vector,
    size_t rank,
    ft_run_delta_vector* result);
/* Accepts the maximal dirty run CONTAINING one position into the checkpoint. The argument is a
 * zero-based position, not a run rank, so a run found by position can be acted on without
 * rediscovering its rank. A clean position is a documented no-op producing the receiver's contents
 * and sharing every root; a position outside the vector is FT_STATUS_OUT_OF_RANGE. Locating the
 * containing run is O(log(r + 2)) amortized and the splice is O(log n) amortized, independent of
 * that run's length. */
ft_status ft_run_delta_vector_accept_dirty_run_containing(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_vector* result);
/* Reverts the maximal dirty run CONTAINING one position from the checkpoint. The argument is a
 * zero-based position, not a run rank, so a run found by position can be acted on without
 * rediscovering its rank. A clean position is a documented no-op producing the receiver's contents
 * and sharing every root; a position outside the vector is FT_STATUS_OUT_OF_RANGE. Locating the
 * containing run is O(log(r + 2)) amortized and the splice is O(log n) amortized, independent of
 * that run's length. */
ft_status ft_run_delta_vector_revert_dirty_run_containing(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_vector* result);

/* Calls the visitor once per current value, in positional order. Theta(n). A visitor returning
 * non-OK ends the traversal and that status propagates. */
ft_status ft_run_delta_vector_visit(
    const ft_run_delta_vector* vector,
    ft_run_delta_visit_fn visitor,
    void* context);
/* Calls the visitor once per maximal dirty run, in ascending position order. Output-optimal
 * Theta(r): the cost depends on the number of runs, not on how many positions they cover. A
 * visitor returning non-OK ends the traversal and that status propagates. */
ft_status ft_run_delta_vector_visit_dirty_runs(
    const ft_run_delta_vector* vector,
    ft_run_delta_run_visit_fn visitor,
    void* context);

/* The current root node's address, for tests that a no-op shared rather than copied. */
const void* ft_run_delta_vector_current_root_identity(const ft_run_delta_vector* vector);
/* The checkpoint root node's address, for tests that a no-op shared rather than copied. */
const void* ft_run_delta_vector_checkpoint_root_identity(const ft_run_delta_vector* vector);
/* Whether both handles are backed by the same current root. A representation check, not an
 * equality test. */
bool ft_run_delta_vector_shares_current_root(
    const ft_run_delta_vector* left,
    const ft_run_delta_vector* right);
/* Whether both handles are backed by the same checkpoint root. A representation check, not an
 * equality test. */
bool ft_run_delta_vector_shares_checkpoint_root(
    const ft_run_delta_vector* left,
    const ft_run_delta_vector* right);
/* Checks both RRB roots, the run index's own shape, run ordering, non-adjacency, maximality,
 * cardinality, equality-relative truth at every dirty position, and exact representative sharing at
 * every clean position.
 * O(n log n): an auditor, not a hot-path operation. Structural invalidity is FT_STATUS_OK with
 * valid=false; allocation and callback failures remain distinguishable and outputs change only on
 * success. */
ft_status ft_run_delta_vector_validate(
    const ft_run_delta_vector* vector,
    bool* valid,
    ft_run_delta_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
