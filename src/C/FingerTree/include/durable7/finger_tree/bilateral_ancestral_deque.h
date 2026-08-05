/*
 * A persistent deque represented as two oppositely oriented intervals of an append-only ancestry
 * arena.
 *
 * This is not a balanced sequence tree. A version is a constant-sized handle over one
 * ft_incremental_ancestor_arena: it stores a left interval (l_anchor, l_tail] and a right interval
 * (r_anchor, r_tail], and its logical value is the reverse of the left interval followed by the
 * right interval. A front insertion appends a leaf below the left tail; a back insertion appends one
 * below the right tail. Reversal only exchanges the two intervals, so it costs O(1) and needs no
 * lazy flag or rebuild.
 *
 * The load-bearing property is slice closure: every contiguous slice intersects each interval at
 * most once, so a slice is again a bilateral handle rather than a rebuilt tree. Slicing and
 * splitting each cost at most TWO level-ancestor queries, indexing at most one, end removal zero
 * queries on its own nonempty arm and at most one when it crosses the centre, and the structural
 * audit at most four. Endpoint reads, size, clear, and reverse are O(1).
 *
 * Two layers of complexity claim must not be conflated.
 *
 *   1. The reduction. Let U(M) be arena leaf-add time and Q(M) be arena level-ancestor query time
 *      after M published nodes. End insertion costs U(M); end removal, indexing, slicing, splitting,
 *      and the structural audit cost O(1 + Q(M)) with the query ceilings above. Enumeration is
 *      Theta(n).
 *   2. The optimal backend. An Alstrup-Holm incremental level-ancestor backend has
 *      U(M) = Q(M) = O(1) worst case in linear space, which makes every scalar operation of this
 *      restricted algebra O(1) worst case. No such backend is included here.
 *
 * The shipped Myers arena does not realize that theorem. Backed by it, end insertion is O(1)
 * amortized; endpoint reads, size, reverse, and clear are O(1) worst case; and end removal,
 * indexing, slicing, splitting, and the audit are O(log M) worst case after M historical insertions.
 * Nothing here upgrades an amortized or logarithmic bound to a worst-case constant one.
 *
 * The algebra is deliberately restricted: add or remove at either end, read either end or an indexed
 * value, reverse, take/drop/slice/split, and enumerate front to back. Arbitrary concatenation of
 * unrelated versions, point replacement, and middle editing are intentionally absent, and no
 * comparison here treats an omitted operation as fast. Arena space is charged to all successful
 * historical end insertions, not merely to the values visible from one handle: a handle retains its
 * arena, and the arena retains every node it ever published. Enumeration is Theta(count) time and
 * allocates O(count) temporary node handles, because the forward-oriented right interval is reversed
 * through a temporary buffer to keep enumeration query-free.
 *
 * There is no process-global empty value: an empty deque belongs to an arena, and retaining it
 * retains that arena. One arena may be shared with the other ancestry-backed collections; it is
 * append-only, so their nodes simply interleave. Every operation publishes a new version and leaves
 * its inputs valid. Concurrency is the arena's and no stronger: see incremental_ancestor.h, which
 * makes a deque single-threaded unless the caller synchronizes the arena it retains.
 */

#ifndef DURABLE7_FINGER_TREE_C_BILATERAL_ANCESTRAL_DEQUE_H
#define DURABLE7_FINGER_TREE_C_BILATERAL_ANCESTRAL_DEQUE_H

#include <durable7/finger_tree/fingertree.h>
#include <durable7/finger_tree/incremental_ancestor.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One oriented ancestry interval (anchor, tail], exposed because a deque handle is a by-value
 * struct. It is representation state: produce it only through the operations below.
 *
 * base is redundant but load-bearing: it caches the first physical node after anchor, which keeps
 * both endpoint reads query-free even when the opposite arm is empty. For the left arm the logical
 * first value sits at tail and the logical last at base; for the right arm those roles are
 * exchanged. An empty interval has anchor == base == tail and count zero. */
typedef struct ft_bilateral_deque_interval {
    ft_incremental_ancestor_node anchor;
    ft_incremental_ancestor_node base;
    ft_incremental_ancestor_node tail;
    size_t count;
} ft_bilateral_deque_interval;

/* One immutable deque version: a retained arena plus the two oriented intervals and their total.
 * The logical value is the reverse of the left interval followed by the right interval. */
typedef struct ft_bilateral_deque {
    ft_incremental_ancestor_arena arena;
    ft_bilateral_deque_interval left;
    ft_bilateral_deque_interval right;
    size_t count;
} ft_bilateral_deque;

/* The two persistent versions produced by a split; together they enumerate the original value. */
typedef struct ft_bilateral_deque_split_result {
    ft_bilateral_deque left;
    ft_bilateral_deque right;
} ft_bilateral_deque_split_result;

/* Cached representation counts reported by the structural audit. */
typedef struct ft_bilateral_deque_statistics {
    size_t count;
    size_t left_count;
    size_t right_count;
} ft_bilateral_deque_statistics;

/* Receives each visible value front to back, borrowing the stored representative. Returning any
 * status other than FT_STATUS_OK aborts the traversal and propagates that status. */
typedef ft_status (*ft_bilateral_deque_visit_fn)(const void* value, void* context);

/* Initializes an empty deque owned by an explicit level-ancestor arena, which it retains. The arena
 * determines the bounds this deque inherits. An arena whose bottom node is not at depth -1 is
 * rejected with FT_STATUS_INVALID_ARGUMENT before anything is published. This creation is O(1). */
ft_status ft_bilateral_deque_init(
    ft_bilateral_deque* deque,
    const ft_incremental_ancestor_arena* arena);
/* Initializes an empty deque over a fresh, independently released Myers arena built on the supplied
 * policy, which the new arena retains. Every version derived from the result shares that arena.
 * This creation is O(1). */
ft_status ft_bilateral_deque_init_myers(
    ft_bilateral_deque* deque,
    const ft_incremental_ancestor_policy* policy);
/* Initializes a Myers-backed deque holding the array's values in order, appending each at the back.
 * The values are read as count consecutive elements of the policy's value size. */
ft_status ft_bilateral_deque_from_array(
    ft_bilateral_deque* deque,
    const ft_incremental_ancestor_policy* policy,
    const void* values,
    size_t count);
/* Initializes a second handle on the same deque version, taking a reference on the shared arena
 * rather than copying it. */
ft_status ft_bilateral_deque_copy(
    const ft_bilateral_deque* source,
    ft_bilateral_deque* destination);
/* Relocates an initialized deque into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at arena state it embeds must be moved rather than copied bytewise. */
void ft_bilateral_deque_move(ft_bilateral_deque* destination, ft_bilateral_deque* source);
/* Releases this handle's reference on its arena. Other versions sharing that arena stay valid, and
 * every published node and value lives until the last handle on the arena goes away. */
void ft_bilateral_deque_dispose(ft_bilateral_deque* deque);
/* Initializes a second handle on the arena this deque and every version derived from it publishes
 * into, taking a reference rather than copying. */
ft_status ft_bilateral_deque_get_arena(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_arena* arena);

/* Whether this handle denotes an empty deque. O(1). */
bool ft_bilateral_deque_empty(const ft_bilateral_deque* deque);
/* The number of visible values. O(1). */
size_t ft_bilateral_deque_size(const ft_bilateral_deque* deque);

/* On success, value_ref borrows the stored representative of the first visible value and remains
 * valid while the arena is retained. Reads one cached handle and makes no level-ancestor query, so
 * it is O(1). An empty deque is reported as FT_STATUS_EMPTY. */
ft_status ft_bilateral_deque_first_ref(const ft_bilateral_deque* deque, const void** value_ref);
/* On success, value is an independently owned copy of the first visible value that must be
 * destroyed with the policy's destroy callback. O(1) plus the policy's copy callback. An empty deque
 * is reported as FT_STATUS_EMPTY. */
ft_status ft_bilateral_deque_first_copy(const ft_bilateral_deque* deque, void* value);
/* On success, value_ref borrows the stored representative of the last visible value and remains
 * valid while the arena is retained. Reads one cached handle and makes no level-ancestor query, so
 * it is O(1). An empty deque is reported as FT_STATUS_EMPTY. */
ft_status ft_bilateral_deque_last_ref(const ft_bilateral_deque* deque, const void** value_ref);
/* On success, value is an independently owned copy of the last visible value that must be destroyed
 * with the policy's destroy callback. O(1) plus the policy's copy callback. An empty deque is
 * reported as FT_STATUS_EMPTY. */
ft_status ft_bilateral_deque_last_copy(const ft_bilateral_deque* deque, void* value);
/* On success, value_ref borrows the stored representative at a zero-based visible index and remains
 * valid while the arena is retained. Costs at most one level-ancestor query; the two logical
 * endpoints of each interval are cached and answered with no query at all. An index outside the
 * deque is reported as FT_STATUS_OUT_OF_RANGE. */
ft_status ft_bilateral_deque_at_ref(
    const ft_bilateral_deque* deque,
    size_t index,
    const void** value_ref);
/* On success, value is an independently owned copy of the value at a zero-based visible index that
 * must be destroyed with the policy's destroy callback. Costs at most one level-ancestor query plus
 * the policy's copy callback. An index outside the deque is reported as FT_STATUS_OUT_OF_RANGE. */
ft_status ft_bilateral_deque_at_copy(const ft_bilateral_deque* deque, size_t index, void* value);

/* Every producer below publishes its result only on success, leaves its operand valid and
 * unchanged, and supports the result aliasing the operand exactly. */

/* Produces the deque with one value added at the front. Publishes exactly one arena leaf and makes
 * no level-ancestor query, so it costs one arena leaf addition: O(1) amortized with the shipped
 * Myers arena. A visible count or ancestry depth that cannot grow further is FT_STATUS_OVERFLOW. */
ft_status ft_bilateral_deque_push_front(
    const ft_bilateral_deque* deque,
    const void* value,
    ft_bilateral_deque* result);
/* Produces the deque with one value added at the back. Publishes exactly one arena leaf and makes
 * no level-ancestor query, so it costs one arena leaf addition: O(1) amortized with the shipped
 * Myers arena. A visible count or ancestry depth that cannot grow further is FT_STATUS_OVERFLOW. */
ft_status ft_bilateral_deque_push_back(
    const ft_bilateral_deque* deque,
    const void* value,
    ft_bilateral_deque* result);
/* Produces the deque without its first value. Makes no level-ancestor query when the left arm is
 * nonempty, and at most one when the removal crosses the centre into the right arm. An empty deque
 * is reported as FT_STATUS_EMPTY. */
ft_status ft_bilateral_deque_remove_first(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result);
/* Produces the deque without its last value. Makes no level-ancestor query when the right arm is
 * nonempty, and at most one when the removal crosses the centre into the left arm. An empty deque
 * is reported as FT_STATUS_EMPTY. */
ft_status ft_bilateral_deque_remove_last(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result);
/* Removes the first value and reads it out. On removed=true, value receives an independently owned
 * copy of the exact removed representative, which must be destroyed with the policy's destroy
 * callback. An empty deque yields removed=false with FT_STATUS_OK, a result holding the same empty
 * version, and value untouched. The copy and the remainder are both withheld on any failure,
 * including when result aliases deque. */
ft_status ft_bilateral_deque_try_pop_front(
    const ft_bilateral_deque* deque,
    bool* removed,
    void* value,
    ft_bilateral_deque* result);
/* Removes the last value and reads it out. On removed=true, value receives an independently owned
 * copy of the exact removed representative, which must be destroyed with the policy's destroy
 * callback. An empty deque yields removed=false with FT_STATUS_OK, a result holding the same empty
 * version, and value untouched. The copy and the remainder are both withheld on any failure,
 * including when result aliases deque. */
ft_status ft_bilateral_deque_try_pop_back(
    const ft_bilateral_deque* deque,
    bool* removed,
    void* value,
    ft_bilateral_deque* result);

/* Produces the first count values. A prefix is a slice, so it costs at most two level-ancestor
 * queries. A count above the visible count is FT_STATUS_OUT_OF_RANGE. */
ft_status ft_bilateral_deque_take(
    const ft_bilateral_deque* deque,
    size_t count,
    ft_bilateral_deque* result);
/* Produces the deque after omitting its first count values. A suffix is a slice, so it costs at most
 * two level-ancestor queries. A count above the visible count is FT_STATUS_OUT_OF_RANGE. */
ft_status ft_bilateral_deque_drop(
    const ft_bilateral_deque* deque,
    size_t count,
    ft_bilateral_deque* result);
/* Produces one contiguous slice. Every contiguous slice intersects each interval at most once, so
 * the result is another bilateral handle and the operation costs AT MOST TWO level-ancestor queries
 * regardless of how many values it selects; an empty slice costs none. An index/count pair outside
 * this deque is FT_STATUS_OUT_OF_RANGE. */
ft_status ft_bilateral_deque_slice(
    const ft_bilateral_deque* deque,
    size_t index,
    size_t count,
    ft_bilateral_deque* result);
/* Produces the prefix before a zero-based boundary and the suffix from it onward. Although the split
 * is expressed as a prefix plus a suffix, the pair costs at most two level-ancestor queries in
 * total. A boundary above the visible count is FT_STATUS_OUT_OF_RANGE. Both results are published
 * together or not at all. */
ft_status ft_bilateral_deque_split_at(
    const ft_bilateral_deque* deque,
    size_t index,
    ft_bilateral_deque_split_result* result);
/* Produces the logical reversal by exchanging the two oriented intervals. O(1), no queries. */
ft_status ft_bilateral_deque_reverse(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result);
/* Produces an empty, independently appendable version in the same arena. O(1), no queries. */
ft_status ft_bilateral_deque_clear(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result);

/* Calls the visitor once per visible value, front to back, borrowing each stored representative.
 * Theta(count) time with no level-ancestor query. The reversed left interval streams through parent
 * links, while the forward-oriented right interval is buffered through O(count) temporary node
 * handles allocated from the policy's allocator, which keeps enumeration query-free. */
ft_status ft_bilateral_deque_visit(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_visit_fn visitor,
    void* context);
/* Writes the visible values in logical order into capacity consecutive elements of the policy's
 * value size. Each written element is an independently owned copy that must be destroyed with the
 * policy's destroy callback. Theta(count) time with no level-ancestor query. A capacity below the
 * visible count is FT_STATUS_OUT_OF_RANGE, and on any failure every copy already constructed is
 * destroyed so the caller's storage is left ownership-free. */
ft_status ft_bilateral_deque_copy_to_array(
    const ft_bilateral_deque* deque,
    void* values,
    size_t capacity);

/* The shared arena representation's address, for tests that a no-op or a copy shared the arena
 * rather than duplicating it. */
const void* ft_bilateral_deque_root_identity(const ft_bilateral_deque* deque);
/* Checks the cached interval endpoints and counts of this handle, reporting structural invalidity as
 * FT_STATUS_OK with valid=false so that argument failures stay distinguishable; outputs change only
 * on success and statistics may be null. The audit compares the cached counts against the interval
 * endpoint depths and confirms each cached base and anchor lies on its tail's ancestry path.
 * Confirming that path costs TWO level-ancestor queries per nonempty interval, so the audit costs
 * O(1 + Q(M)) with a ceiling of four queries, not O(1) work; with the shipped Myers arena that is
 * O(log M) worst case after M historical insertions. An empty handle spends no query. */
ft_status ft_bilateral_deque_validate(
    const ft_bilateral_deque* deque,
    bool* valid,
    ft_bilateral_deque_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
