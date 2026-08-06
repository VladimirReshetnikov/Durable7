/*
 * A persistent queue whose every version is one contiguous interval of an append-only tree path.
 *
 * A handle does not own nodes. It is the constant-sized pair (tail, low_depth) naming an interval of
 * the unique root-to-tail path inside a shared ft_incremental_ancestor_arena, plus a redundant count
 * cache. The visible values are the labels of the ancestors of tail at absolute depths low_depth,
 * low_depth + 1, ..., depth(tail), so count = depth(tail) - low_depth + 1. Appending adds one child
 * below tail; removing or slicing moves only the two interval boundaries. Because the arena is
 * append-only and its published nodes are immutable, every retained handle keeps denoting exactly
 * the same sequence forever, and adding below an old handle simply starts a sibling branch. That is
 * what makes the structure fully persistent for its restricted algebra: any historical version can
 * be extended, not only the newest one. It is not confluently persistent, because two unrelated
 * versions cannot be merged.
 *
 * The anchored-empty rule. An empty value is not "nothing": it retains the node immediately BEFORE
 * its window as an anchor, so low_depth == depth(tail) + 1 holds whenever the count is zero.
 * Appending to any empty slice therefore yields exactly the new value and never resurrects the
 * discarded prefix. Empty values reached through different histories can consequently have distinct
 * provenance while denoting the same empty sequence; there is no canonical empty and no
 * process-global empty arena.
 *
 * Bounds. They are parameterized by the backend. Let U be the arena's leaf-add time and Q its
 * level-ancestor query time. ..._add_last costs U; ..._first_ref, ..._first_copy, ..._at_ref,
 * ..._at_copy, ..._take, ..._slice, ..._try_remove_first, and a boundary-moving ..._split_at cost Q;
 * ..._size, ..._empty, ..._last_ref, ..._last_copy, ..._remove_first, ..._remove_last,
 * ..._try_remove_last, and ..._drop are O(1). Alstrup-Holm incremental ancestry gives
 * U = Q = O(1) worst case with linear arena space; NO SUCH BACKEND IS IMPLEMENTED HERE, so that
 * statement is a reduction to a published result rather than a property of this workspace.
 * ..._init_myers instead uses the shipped Myers arena, whose addition is O(1) amortized and whose
 * ancestor query is O(log M) worst case after M historical appends. Under that instantiation the
 * shipped bounds are: ..._add_last O(1) amortized; the Q-costed operations above O(log M) worst
 * case; and the O(1) operations O(1) worst case. Boundary-specialized calls that can reuse an
 * existing tail -- ..._take(count()), ..._drop(0), a ..._slice whose window ends at the current
 * tail, and a ..._split_at at count() -- make no ancestor query at all. No amortized bound above may
 * be read as a worst-case bound.
 *
 * Deliberate limits. The algebra intentionally excludes prepending, point replacement, arbitrary
 * middle edits, and concatenation of unrelated histories; omitting them is precisely why the indexed
 * and slicing bounds are possible. Arena space is charged to ALL successful historical appends, not
 * to the visible length of one handle: dropping a prefix or disposing a handle reclaims nothing, and
 * every published value stays reachable until the last arena handle is disposed. Two handles may
 * enumerate equal values while having different tails, anchors, or arenas, so no constant-time
 * sequence equality, hashing, or canonical empty identity is promised or provided.
 *
 * Concurrency. A queue handle only reads and writes its arena, and the arena is single-threaded
 * unless the caller synchronizes it; see incremental_ancestor.h. Handle reference counts are
 * maintained atomically, so a handle may be disposed on a thread other than the one that created it,
 * but that does not make the queue or its arena concurrent.
 */

#ifndef DURABLE7_FINGER_TREE_C_ANCESTRAL_SLICE_QUEUE_H
#define DURABLE7_FINGER_TREE_C_ANCESTRAL_SLICE_QUEUE_H

#include <durable7/finger_tree/fingertree.h>
#include <durable7/finger_tree/incremental_ancestor.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One immutable queue version: the retained arena plus the interval boundaries. The fields are the
 * representation the proof reasons about and are readable for tests and diagnostics; a caller must
 * obtain a handle from an initializer or a producing operation rather than assembling one, because
 * only those establish the invariant depth(tail) == low_depth + count - 1 that every operation
 * assumes. Copying the struct bytewise duplicates the arena reference without retaining it: use
 * ..._copy or ..._move instead. */
typedef struct ft_ancestral_slice_queue {
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_node tail;
    ft_incremental_ancestor_depth low_depth;
    size_t count;
} ft_ancestral_slice_queue;

/* The two appendable ancestry intervals produced by a split. Both are independent handles the caller
 * disposes separately. */
typedef struct ft_ancestral_slice_queue_split_result {
    ft_ancestral_slice_queue left;
    ft_ancestral_slice_queue right;
} ft_ancestral_slice_queue_split_result;

/* The traversal callback. The borrowed value stays valid while the visited version's arena is
 * retained. Returning anything other than FT_STATUS_OK aborts the traversal and propagates. */
typedef ft_status (*ft_ancestral_slice_queue_visit_fn)(const void* value, void* context);

/* Initializes an empty queue anchored on a caller-supplied arena, which it retains. This is the
 * public extension seam: supplying a backend with different U and Q costs changes every
 * parameterized bound documented above. An arena that does not report depth -1 for its bottom node
 * cannot satisfy the anchored-empty rule and is rejected with FT_STATUS_INVALID_ARGUMENT. This
 * initialization is O(1). */
ft_status ft_ancestral_slice_queue_init(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_arena* arena);
/* Initializes an empty queue over a fresh, independently disposable Myers jump-link arena created
 * from the supplied policy, which the new arena retains. There is deliberately no shared global
 * empty value: each call owns its own arena, so unrelated workloads never share retained history.
 * This initialization is O(1). */
ft_status ft_ancestral_slice_queue_init_myers(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_policy* policy);
/* Initializes a Myers-backed queue holding the array's values in order, appending each one. The
 * array is read as count consecutive elements of the policy's value_size. This publishes one arena
 * node per value, for Theta(count) amortized total time on the shipped backend. */
ft_status ft_ancestral_slice_queue_from_array(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same queue version, taking a reference on its arena rather
 * than copying anything. This copy is O(1). */
ft_status ft_ancestral_slice_queue_copy(
    const ft_ancestral_slice_queue* source,
    ft_ancestral_slice_queue* destination);
/* Relocates an initialized queue into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at arena state it embeds must be moved rather than copied bytewise. */
void ft_ancestral_slice_queue_move(
    ft_ancestral_slice_queue* destination,
    ft_ancestral_slice_queue* source);
/* Releases this handle's reference on its arena. Other versions sharing that arena stay valid, and
 * the arena's published nodes and values survive until its last handle goes away. */
void ft_ancestral_slice_queue_dispose(ft_ancestral_slice_queue* queue);

/* Whether this handle denotes an empty ancestry interval. Read from the cached count in O(1); the
 * arena is not consulted. An uninitialized handle is not a queue and reports false. */
bool ft_ancestral_slice_queue_empty(const ft_ancestral_slice_queue* queue);
/* The number of visible values. Read from the cached count in O(1); the arena is not consulted. An
 * uninitialized handle reports zero. */
size_t ft_ancestral_slice_queue_size(const ft_ancestral_slice_queue* queue);

/* On success, value_ref borrows the stored first value and remains valid while the source version's
 * arena is retained. Costs one ancestor query Q: the front is an ancestor selected jointly by the
 * tail and the low boundary, so it is not an O(1) endpoint read under a logarithmic backend. A
 * one-element queue reuses its retained tail and makes no query. An empty queue is rejected with
 * FT_STATUS_EMPTY. */
ft_status ft_ancestral_slice_queue_first_ref(
    const ft_ancestral_slice_queue* queue,
    const void** value_ref);
/* On success, value receives an independently owned copy of the first value, which the caller must
 * destroy with the policy's destroy callback. Costs Q plus the policy's copy callback. An empty
 * queue is rejected with FT_STATUS_EMPTY. */
ft_status ft_ancestral_slice_queue_first_copy(
    const ft_ancestral_slice_queue* queue,
    void* value);
/* On success, value_ref borrows the stored last value and remains valid while the source version's
 * arena is retained. This read is O(1): the tail is retained directly by the handle. An empty queue
 * is rejected with FT_STATUS_EMPTY. */
ft_status ft_ancestral_slice_queue_last_ref(
    const ft_ancestral_slice_queue* queue,
    const void** value_ref);
/* On success, value receives an independently owned copy of the last value, which the caller must
 * destroy with the policy's destroy callback. This read is O(1) plus the policy's copy callback. An
 * empty queue is rejected with FT_STATUS_EMPTY. */
ft_status ft_ancestral_slice_queue_last_copy(
    const ft_ancestral_slice_queue* queue,
    void* value);
/* On success, value_ref borrows the stored value at a zero-based visible index and remains valid
 * while the source version's arena is retained. Costs one ancestor query Q; the last index reuses
 * the retained tail and makes no query. An index at or beyond the count is rejected with
 * FT_STATUS_OUT_OF_RANGE. */
ft_status ft_ancestral_slice_queue_at_ref(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    const void** value_ref);
/* On success, value receives an independently owned copy of the value at a zero-based visible
 * index, which the caller must destroy with the policy's destroy callback. Costs Q plus the policy's
 * copy callback. An index at or beyond the count is rejected with FT_STATUS_OUT_OF_RANGE. */
ft_status ft_ancestral_slice_queue_at_copy(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    void* value);

/* Produces the queue with one value appended below this handle's tail or empty anchor, publishing
 * the caller's value as an arena-owned copy made through the policy. Costs U and publishes exactly
 * one arena node. The source handle and every other retained version are unaffected; appending below
 * an old handle starts a sibling branch. A rejected addition publishes nothing, leaves every input
 * valid, and leaves the arena exactly as it was. A count that cannot grow further is reported as
 * FT_STATUS_OVERFLOW. Result may alias the source handle, in which case the source's reference is
 * released only after the successor is fully built. */
ft_status ft_ancestral_slice_queue_add_last(
    const ft_ancestral_slice_queue* queue,
    const void* value,
    ft_ancestral_slice_queue* result);
/* Produces the queue without its first visible value. O(1): only the low boundary moves, so no
 * ancestor query is made. Emptying a one-element queue this way leaves the old tail as the result's
 * anchor. An empty queue is rejected with FT_STATUS_EMPTY. */
ft_status ft_ancestral_slice_queue_remove_first(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue* result);
/* Produces the queue without its last visible value. O(1): the new tail is the old tail's parent,
 * which the arena reads in constant time. Emptying a one-element queue this way anchors the result
 * immediately before the window. An empty queue is rejected with FT_STATUS_EMPTY. */
ft_status ft_ancestral_slice_queue_remove_last(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue* result);
/* Removes and reads out the first visible value without an exception-shaped empty case. On
 * removed=true, value receives an independently owned copy of the removed value, which the caller
 * must destroy with the policy's destroy callback, and result holds the remainder. On removed=false
 * the queue was empty, value is untouched, and result holds a second handle on the unchanged source.
 * Costs Q because it also reads the front. The copy and the remainder are both withheld on any
 * failure, including when result aliases queue. */
ft_status ft_ancestral_slice_queue_try_remove_first(
    const ft_ancestral_slice_queue* queue,
    bool* removed,
    void* value,
    ft_ancestral_slice_queue* result);
/* Removes and reads out the last visible value without an exception-shaped empty case. On
 * removed=true, value receives an independently owned copy of the removed value, which the caller
 * must destroy with the policy's destroy callback, and result holds the remainder. On removed=false
 * the queue was empty, value is untouched, and result holds a second handle on the unchanged source.
 * This is O(1) plus the policy's copy callback. The copy and the remainder are both withheld on any
 * failure, including when result aliases queue. */
ft_status ft_ancestral_slice_queue_try_remove_last(
    const ft_ancestral_slice_queue* queue,
    bool* removed,
    void* value,
    ft_ancestral_slice_queue* result);

/* Produces the first count visible values. Costs Q; taking the whole queue reuses the source tail
 * and makes no query. The result is a real ancestry slice and remains appendable, and taking zero
 * values anchors the result immediately before the window. A count beyond the queue is rejected with
 * FT_STATUS_OUT_OF_RANGE. */
ft_status ft_ancestral_slice_queue_take(
    const ft_ancestral_slice_queue* queue,
    size_t count,
    ft_ancestral_slice_queue* result);
/* Produces the queue after omitting its first count visible values. O(1): only the low boundary
 * moves, and dropping everything keeps the old tail as the empty result's anchor. A count beyond the
 * queue is rejected with FT_STATUS_OUT_OF_RANGE. */
ft_status ft_ancestral_slice_queue_drop(
    const ft_ancestral_slice_queue* queue,
    size_t count,
    ft_ancestral_slice_queue* result);
/* Produces one contiguous, appendable ancestry slice starting at a zero-based visible index. Costs Q
 * in general; a window that ends at the current tail -- the whole queue and every suffix -- reuses
 * that tail and makes no query. A zero-length window selects the ancestor immediately before its
 * start as an anchor, which is the arena's bottom node when the start is the very front of a queue
 * grown from an empty one. A window outside the queue is rejected with FT_STATUS_OUT_OF_RANGE. */
ft_status ft_ancestral_slice_queue_slice(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    size_t count,
    ft_ancestral_slice_queue* result);
/* Produces the prefix holding the first index visible values and the suffix holding the rest. Costs
 * Q: the prefix performs the one ancestor-seeking query and the suffix only moves the low boundary.
 * Both results are real ancestry slices and can independently start new branches. Only a split at
 * the count makes no query; a split at zero on a nonempty queue still costs Q here, because the
 * anchored-empty rule requires the empty prefix to retain the node at depth low_depth - 1. That is
 * forced when low_depth is positive, since only an ancestor query can name that node from the
 * retained tail; when low_depth is zero the node is the arena's bottom, which the seam also names
 * in O(1), so the uniform query there is a choice rather than a necessity. Both
 * handles are published together or not at all; an index beyond the queue is rejected with
 * FT_STATUS_OUT_OF_RANGE. */
ft_status ft_ancestral_slice_queue_split_at(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    ft_ancestral_slice_queue_split_result* result);

/* Calls the visitor once per visible value, front to back. Follows parent links from the tail into
 * one temporary array of node handles rather than performing count ancestor queries, taking
 * Theta(count) time, Theta(count) transient handle slots, and no ancestor query at all. The captured
 * path is fixed before the first callback, so a branch published during the traversal cannot enter
 * it. A visitor returning anything other than FT_STATUS_OK aborts the traversal and propagates. */
ft_status ft_ancestral_slice_queue_visit(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue_visit_fn visitor,
    void* context);

/* The shared arena representation's address, for tests that a no-op or a copy shared the retained
 * history rather than duplicating it. */
const void* ft_ancestral_slice_queue_root_identity(const ft_ancestral_slice_queue* queue);
/* Initializes a second handle on this version's arena, taking a reference rather than copying, so a
 * caller holding only a queue can navigate, extend, or measure the arena directly. This read is
 * O(1). */
ft_status ft_ancestral_slice_queue_get_arena(
    const ft_ancestral_slice_queue* queue,
    ft_incremental_ancestor_arena* arena);

#ifdef __cplusplus
}
#endif

#endif
