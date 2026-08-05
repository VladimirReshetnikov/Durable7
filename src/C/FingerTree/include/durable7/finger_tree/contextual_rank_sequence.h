/*
 * A persistent sequence that ranks and selects events whose occurrence depends on finite left
 * context.
 *
 * A sum-measured sequence can rank elements carrying a fixed local weight. It cannot rank a comma
 * that is a field separator in one prefix context and quoted data in another, because no single
 * weight is correct for both. A streaming automaton keeps that context but has to replay a prefix
 * after every edit.
 *
 * This structure lifts a finite deterministic machine into the measure of the workspace's measured
 * finger tree. Every measured subtree caches, for EVERY possible incoming machine state, the
 * outgoing state and the number of events emitted while consuming that subtree. Ordered composition
 * feeds the left summary's outgoing state into the right summary, which turns a context-dependent
 * scan into an associative measure:
 *
 *     (A * B).next(q)   = B.next(A.next(q))
 *     (A * B).events(q) = A.events(q) + B.events(A.next(q))
 *     (A * B).count     = A.count + B.count
 *
 * The identity maps every state to itself, emits zero events, and holds zero elements. Composition
 * is NOT commutative. Because event counts are nonnegative, the select predicate
 * prefix.events(q0) > r is monotone, so one measure-directed descent finds the element that emitted
 * event r without a binary search and without replaying a prefix.
 *
 * Let n be the element count and s the machine's fixed state count. Full-sequence evaluation is
 * O(1); prefix evaluation, indexing, contextual event rank and select, insertion, replacement,
 * removal, splitting, and slicing are O(s log n) amortized; endpoint updates are O(s) amortized;
 * concatenation is O(s log(min(n,m))) amortized; visitation is O(n). When the machine is fixed s is
 * a constant, giving O(log n) rank/select and edits. Each retained changed spine adds O(s log n)
 * summary storage while unaffected structure stays shared, so the structure costs O(s n) summary
 * space rather than O(n).
 *
 * This module is layered on the measured finger tree declared in fingertree.h. That tree's measure
 * seam carries a caller-supplied monoid whose width is a run-time policy field, which is exactly
 * what a per-state effect table needs, and its digits are what make the endpoint bound O(s)
 * amortized rather than O(s log n). Because the underlying tree owns its own nodes, the policy's
 * allocator serves this module's own blocks - stored elements, cached leaf summaries, and query
 * scratch - while the shared tree spine is allocated by the C runtime allocator.
 *
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection. A failing transition, an
 * out-of-contract transition, or an event-count overflow publishes no successor and leaves every
 * older version usable.
 *
 * Deliberate limits: context must be finite and must flow left to right; event weights must be
 * nonnegative so select stays monotone; one input element occupies one measured leaf, so this is
 * not a chunked rope and makes no cache-density claim; and there is no way to change the machine of
 * an existing sequence, because rebuilding under another machine costs O(s n).
 */

#ifndef DURABLE7_FINGER_TREE_C_CONTEXTUAL_RANK_SEQUENCE_H
#define DURABLE7_FINGER_TREE_C_CONTEXTUAL_RANK_SEQUENCE_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_crs_value_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_crs_value_destroy_fn)(void* value, void* context);
/* One transition of a deterministic additive event machine: consumes one element from one valid
 * state and reports the outgoing state and the nonnegative number of logical events emitted. A
 * single transition may emit more than one event. Out-parameters are written only on success. */
typedef ft_status (*ft_crs_transition_fn)(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context);
typedef void* (*ft_crs_allocate_fn)(size_t size, void* context);
typedef void (*ft_crs_deallocate_fn)(void* allocation, void* context);

typedef struct ft_crs_allocator {
    ft_crs_allocate_fn allocate;
    ft_crs_deallocate_fn deallocate;
    void* context;
} ft_crs_allocator;

/* value_type_identity is a required, stable, non-null caller-owned tag. Copy
 * constructs an owned value in uninitialized storage; on failure it must leave
 * that storage ownership-free. Destroy and allocator hooks must return normally.
 * state_count must be positive and is fixed for the lifetime of the policy; the
 * per-node summary is a pair of arrays of that width. For every state in
 * 0..state_count-1 the transition must report a state in the same range and a
 * nonnegative event count, and it must be deterministic and side-effect-free. A
 * transition that violates either range requirement is reported as
 * FT_STATUS_INCONSISTENT_POLICY, while a transition that itself fails is
 * reported as FT_STATUS_CALLBACK_FAILURE. Hooks must not reenter an in-flight
 * operation through the same policy/sequence handles. Distinct immutable handles
 * may be used concurrently only when every reachable hook and context is safe
 * for that call pattern. */
typedef struct ft_crs_policy_config {
    size_t value_size;
    const void* value_type_identity;
    ft_crs_value_copy_fn copy;
    ft_crs_value_destroy_fn destroy;
    size_t state_count;
    ft_crs_transition_fn transition;
    void* callback_context;
    ft_crs_allocator allocator;
} ft_crs_policy_config;

/* The shared, reference-counted representation behind a contextual rank sequence policy. */
typedef struct ft_crs_policy_rep ft_crs_policy_rep;

/* Policy identity gates concatenation, exactly as the machine type parameter does in the managed
 * reference. Copy preserves identity; independently recreating an equivalent config deliberately
 * does not. */
typedef struct ft_crs_policy {
    ft_crs_policy_rep* rep;
} ft_crs_policy;

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
void ft_crs_policy_config_init(
    ft_crs_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    size_t state_count,
    ft_crs_transition_fn transition,
    void* callback_context);
/* Initializes a policy using the supplied configuration, which it retains. A machine declaring no
 * states is FT_STATUS_INVALID_ARGUMENT. */
ft_status ft_crs_policy_create(
    ft_crs_policy* policy,
    const ft_crs_policy_config* config);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
ft_status ft_crs_policy_copy(
    const ft_crs_policy* source,
    ft_crs_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_crs_policy_move(ft_crs_policy* destination, ft_crs_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_crs_policy_dispose(ft_crs_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_crs_policy_same(const ft_crs_policy* left, const ft_crs_policy* right);
/* The machine's finite state count, or zero for an uninitialized handle. */
size_t ft_crs_policy_state_count(const ft_crs_policy* policy);

/* The shared root of one sequence version, held by the measured finger tree. Opaque. */
typedef struct ft_tree_rep ft_contextual_rank_sequence_root;

/* An immutable sequence handle. Empty handles are still policy-bound. Results
 * may exactly alias their single source handle, or either operand of a binary
 * operation. Non-aliasing result handles are output-only and must be
 * empty/uninitialized. Split outputs must be distinct; either may alias the
 * source. */
typedef struct ft_contextual_rank_sequence {
    ft_crs_policy_rep* policy;
    ft_contextual_rank_sequence_root* root;
} ft_contextual_rank_sequence;

typedef struct ft_contextual_rank_sequence_split_result {
    ft_contextual_rank_sequence left;
    ft_contextual_rank_sequence right;
} ft_contextual_rank_sequence_split_result;

/* Summarizes running the machine over a prefix: the state after the prefix and the number of
 * events the prefix emitted. */
typedef struct ft_contextual_prefix_summary {
    size_t final_state;
    int64_t event_count;
} ft_contextual_prefix_summary;

/* Identifies one contextual event and the input element that emitted it: the emitting element's
 * zero-based index, the event's zero-based ordinal among the events that one transition emitted,
 * the states on either side of the element, and that transition's total event count. */
typedef struct ft_contextual_event_location {
    size_t element_index;
    int64_t event_index_in_element;
    size_t state_before;
    size_t state_after;
    int64_t element_event_count;
} ft_contextual_event_location;

typedef struct ft_contextual_rank_sequence_statistics {
    size_t count;
    size_t state_count;
    int64_t maximum_total_event_count;
} ft_contextual_rank_sequence_statistics;

/* element is borrowed only for the duration of the callback. A visitor returning a status other
 * than FT_STATUS_OK aborts the traversal and propagates that status. */
typedef ft_status (*ft_contextual_rank_sequence_visit_fn)(
    const void* element,
    void* context);

/* Initializes an empty sequence in place. Its summary maps every state to itself with zero
 * events. */
ft_status ft_contextual_rank_sequence_init(
    ft_contextual_rank_sequence* sequence,
    const ft_crs_policy* policy);
/* Initializes a sequence holding the given elements in input order. values is an array of count
 * elements of the policy's value size, consumed in order and never retained. O(s n). */
ft_status ft_contextual_rank_sequence_from_array(
    ft_contextual_rank_sequence* sequence,
    const ft_crs_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same sequence version, taking a reference rather than
 * copying. */
ft_status ft_contextual_rank_sequence_copy(
    const ft_contextual_rank_sequence* source,
    ft_contextual_rank_sequence* destination);
/* Relocates an initialized sequence into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_contextual_rank_sequence_move(
    ft_contextual_rank_sequence* destination,
    ft_contextual_rank_sequence* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_contextual_rank_sequence_dispose(ft_contextual_rank_sequence* sequence);

/* Whether the sequence holds no elements. */
bool ft_contextual_rank_sequence_empty(const ft_contextual_rank_sequence* sequence);
/* Number of stored elements. O(1). */
size_t ft_contextual_rank_sequence_size(const ft_contextual_rank_sequence* sequence);
/* The machine's finite state count, or zero for an uninitialized handle. */
size_t ft_contextual_rank_sequence_state_count(
    const ft_contextual_rank_sequence* sequence);

/* On success, element_ref borrows the stored representative and remains valid
 * while the source version is retained. O(log n) amortized: indexing composes no
 * summary. */
ft_status ft_contextual_rank_sequence_at_ref(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    const void** element_ref);
/* On success, element is independently owned and must be destroyed with the
 * policy's destroy callback when non-null. O(log n) amortized. */
ft_status ft_contextual_rank_sequence_at_copy(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    void* element);

/* Runs the machine over the whole sequence from an initial state. O(1): the answer is the cached
 * root summary. */
ft_status ft_contextual_rank_sequence_evaluate(
    const ft_contextual_rank_sequence* sequence,
    size_t initial_state,
    ft_contextual_prefix_summary* summary);
/* Runs the machine over the first element_count elements from an initial state. O(s log n)
 * amortized, and O(1) at either sequence endpoint. */
ft_status ft_contextual_rank_sequence_evaluate_prefix(
    const ft_contextual_rank_sequence* sequence,
    size_t element_count,
    size_t initial_state,
    ft_contextual_prefix_summary* summary);
/* The number of contextual events strictly before an element boundary, counting from the state
 * before the complete sequence. O(s log n) amortized. */
ft_status ft_contextual_rank_sequence_event_rank(
    const ft_contextual_rank_sequence* sequence,
    size_t element_count,
    size_t initial_state,
    int64_t* event_rank);
/* Locates the zero-based contextual event with the given rank, counting from the state before the
 * complete sequence. A negative or past-end rank is FT_STATUS_OK with found=false; only found=true
 * writes the location. O(s log n) amortized: one measure-directed descent, and no machine
 * transition, because the located element carries its own cached effect table. */
ft_status ft_contextual_rank_sequence_try_select_event(
    const ft_contextual_rank_sequence* sequence,
    int64_t event_index,
    size_t initial_state,
    bool* found,
    ft_contextual_event_location* location);

/* Produces the sequence with one element prepended. O(s) amortized. */
ft_status ft_contextual_rank_sequence_push_front(
    const ft_contextual_rank_sequence* sequence,
    const void* element,
    ft_contextual_rank_sequence* result);
/* Produces the sequence with one element appended. O(s) amortized. */
ft_status ft_contextual_rank_sequence_push_back(
    const ft_contextual_rank_sequence* sequence,
    const void* element,
    ft_contextual_rank_sequence* result);
/* Produces the concatenation of two sequences built under the same policy, sharing both operands'
 * unchanged structure and retaining the nonempty operand's root when the other is empty. Requires
 * exact policy identity. O(s log(min(n,m))) amortized. */
ft_status ft_contextual_rank_sequence_concat(
    const ft_contextual_rank_sequence* left,
    const ft_contextual_rank_sequence* right,
    ft_contextual_rank_sequence* result);
/* Produces the sequence with the element inserted so that index old elements precede it.
 * O(s log n) amortized, and O(s) amortized at either endpoint. */
ft_status ft_contextual_rank_sequence_insert_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    const void* element,
    ft_contextual_rank_sequence* result);
/* Produces the sequence with the element at the position replaced. O(s log n) amortized. */
ft_status ft_contextual_rank_sequence_set_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    const void* element,
    ft_contextual_rank_sequence* result);
/* Produces the sequence without the element at the position. O(s log n) amortized. */
ft_status ft_contextual_rank_sequence_remove_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    ft_contextual_rank_sequence* result);
/* Splits at a zero-based element boundary into the elements before it and those from it onward.
 * Splitting at either endpoint retains the source root in the nonempty half. O(s log n)
 * amortized. */
ft_status ft_contextual_rank_sequence_split_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    ft_contextual_rank_sequence_split_result* result);
/* Produces the count elements starting at index. A range covering the whole sequence retains the
 * source root. O(s log n) amortized. */
ft_status ft_contextual_rank_sequence_get_range(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    size_t count,
    ft_contextual_rank_sequence* result);

/* Calls the visitor once per element, in sequence order. O(n). */
ft_status ft_contextual_rank_sequence_visit(
    const ft_contextual_rank_sequence* sequence,
    ft_contextual_rank_sequence_visit_fn visitor,
    void* context);

/* The root node's address, for tests that a no-op shared rather than copied. */
const void* ft_contextual_rank_sequence_root_identity(
    const ft_contextual_rank_sequence* sequence);
/* Recomputes every root summary by direct machine scan and compares it with the cached measure. A
 * defensive audit; ordinary operations maintain these invariants. Structural invalidity is
 * FT_STATUS_OK with valid=false. Allocation and callback failures remain distinguishable and
 * outputs change only on success. O(s n). */
ft_status ft_contextual_rank_sequence_validate(
    const ft_contextual_rank_sequence* sequence,
    bool* valid,
    ft_contextual_rank_sequence_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
