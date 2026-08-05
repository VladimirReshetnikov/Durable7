/*
 * A persistent deque held as two oppositely oriented intervals of an append-only ancestry arena.
 *
 * This is not a balanced sequence tree. A version is a constant-sized handle over one
 * IncrementalAncestorArena: it stores a left interval (l-anchor, l-tail] and a right interval
 * (r-anchor, r-tail], and its logical value is reverse(left) followed by right. Front insertion
 * appends a leaf below the left tail, back insertion below the right tail, and reversal only
 * exchanges the two intervals.
 *
 * The load-bearing property is slice closure: a contiguous slice intersects each interval at most
 * once, so it is again a bilateral handle rather than a rebuilt tree.
 */
package durable7.fingertree

import kotlin.math.max
import kotlin.math.min

/**
 * An endpoint value together with the deque that remains after removing it.
 *
 * A `null` result from [BilateralAncestralDeque.tryRemoveFirst] or
 * [BilateralAncestralDeque.tryRemoveLast] means the deque was empty; a non-null wrapper reports a
 * successful removal even when [value] itself is `null`.
 *
 * @property value the removed endpoint value.
 * @property rest the deque without that value.
 */
public data class BilateralAncestralDequePop<T>(
    public val value: T,
    public val rest: BilateralAncestralDeque<T>,
)

/**
 * The two persistent deques produced by [BilateralAncestralDeque.splitAt].
 *
 * Together they enumerate the original value, and both retain the source arena.
 *
 * @property left the prefix holding the values before the boundary.
 * @property right the suffix holding the values from the boundary onward.
 */
public data class BilateralAncestralDequeSplit<T>(
    public val left: BilateralAncestralDeque<T>,
    public val right: BilateralAncestralDeque<T>,
)

/**
 * Representation counts reported by [BilateralAncestralDeque.validateStructure].
 *
 * @property count the number of visible values, which equals [leftCount] plus [rightCount].
 * @property leftCount the number of values held by the reversed left interval.
 * @property rightCount the number of values held by the forward right interval.
 */
public data class BilateralAncestralDequeStatistics(
    public val count: Int,
    public val leftCount: Int,
    public val rightCount: Int,
)

/**
 * An immutable deque represented as two oppositely oriented intervals of an ancestry arena.
 *
 * Each handle stores a left interval `(l-anchor, l-tail]` and a right interval
 * `(r-anchor, r-tail]`. Its logical value is `reverse(left)` followed by `right`. [addFirst]
 * appends a leaf below the left tail; [addLast] appends one below the right tail. [reverse] only
 * exchanges the two intervals, so it needs no lazy flag and no rebuild. Every contiguous slice
 * intersects each interval at most once, so [slice] and [splitAt] again produce bilateral handles.
 *
 * Two layers of complexity claim must not be conflated.
 *
 * 1. *The reduction.* Let `U(M)` be arena leaf-add time and `Q(M)` be arena level-ancestor query
 *    time after `M` published nodes. End insertion costs `U(M)`. [get] costs at most one query;
 *    [slice] and [splitAt] cost at most two; [removeFirst] and [removeLast] cost none on their own
 *    nonempty arm and at most one when the removal crosses the centre; [validateStructure] costs
 *    at most four. [size], [isEmpty], [front], [back], [clear], and [reverse] are O(1) with no
 *    query, and enumeration is `Theta(size)` with no query.
 * 2. *The optimal backend.* An Alstrup–Holm incremental level-ancestor backend has
 *    `U(M) = Q(M) = O(1)` worst case in linear space, which would make every scalar operation of
 *    this restricted algebra O(1) worst case. No such backend ships here.
 *
 * Backed by the shipped [MyersIncrementalAncestorArena], end insertion is O(1) *amortized* and the
 * query-bounded operations above are O(log M) worst case after `M` historical insertions. Nothing
 * here upgrades an amortized or logarithmic bound to a worst-case constant one.
 *
 * The algebra is deliberately restricted: add or remove at either end, read either end or an
 * indexed value, reverse, take/drop/slice/split, and enumerate front to back. Arbitrary
 * concatenation of unrelated versions, point replacement, and middle editing are absent, and no
 * comparison here treats an omitted operation as fast. Arena space is charged to all successful
 * historical end insertions, not merely to the values visible from one handle: a handle retains its
 * arena, and the arena retains every node it ever published.
 *
 * There is no process-wide empty value: an empty deque belongs to an arena, and retaining it
 * retains that arena. Handles are immutable, so retained versions are safe for concurrent reads to
 * the same extent as the arena and the caller-owned values they reach.
 *
 * @param T the element type.
 */
public class BilateralAncestralDeque<T> private constructor(
    /** The append-only arena this handle and every version derived from it publishes into. */
    public val arena: IncrementalAncestorArena<T>,
    private val left: Segment,
    private val right: Segment,
    /** The number of visible values. This read is O(1). */
    public val size: Int,
) : Iterable<T> {
    public companion object {
        /**
         * Creates an empty deque owned by an explicit incremental-ancestor arena.
         *
         * The arena retains and navigates every inserted node and therefore fixes the bounds this
         * deque inherits. Sharing one arena between independent deque families is allowed; the
         * arena is append-only, so their nodes simply interleave.
         *
         * @param arena the manager that retains and navigates every inserted node.
         * @throws IllegalArgumentException when the arena's bottom node is not at depth `-1`.
         */
        public fun <T> create(arena: IncrementalAncestorArena<T>): BilateralAncestralDeque<T> {
            val bottom = arena.bottom
            require(arena.depthOf(bottom) == -1) { "The arena bottom node must have depth -1." }
            val empty = Segment.emptyAt(bottom)
            return BilateralAncestralDeque(arena, empty, empty, 0)
        }

        /**
         * Creates an empty deque backed by a fresh, independently collectible Myers arena.
         *
         * The arena is private to this deque family: every version derived from the result shares
         * it, and it stays reachable while any of those versions does.
         */
        public fun <T> empty(): BilateralAncestralDeque<T> =
            create(MyersIncrementalAncestorArena())

        /**
         * Creates a Myers-backed deque holding [values] in iteration order, each appended at the
         * back in `Theta(n)` arena leaf additions.
         *
         * Unlike the workspace's purely functional factories, this never adopts an existing handle:
         * the result always owns a fresh arena, matching the reference `CreateRange`.
         */
        public fun <T> from(values: Iterable<T>): BilateralAncestralDeque<T> {
            var result = empty<T>()
            for (value in values) {
                result = result.addLast(value)
            }
            return result
        }
    }

    /** Whether this handle denotes an empty deque. This read is O(1). */
    public val isEmpty: Boolean
        get() = size == 0

    /**
     * The first visible value, or `null` when the deque is empty.
     *
     * Reads one cached endpoint handle and makes no level-ancestor query, so it is O(1). A stored
     * `null` element is reported as `null` too; [isEmpty] distinguishes the two, and
     * [tryRemoveFirst] reports a present value unambiguously.
     */
    public fun front(): T? =
        if (isEmpty) null else arena.valueAt(if (left.count != 0) left.tail else right.base)

    /**
     * The last visible value, or `null` when the deque is empty.
     *
     * Reads one cached endpoint handle and makes no level-ancestor query, so it is O(1). A stored
     * `null` element is reported as `null` too; [isEmpty] distinguishes the two, and
     * [tryRemoveLast] reports a present value unambiguously.
     */
    public fun back(): T? =
        if (isEmpty) null else arena.valueAt(if (right.count != 0) right.tail else left.base)

    /**
     * The value at a zero-based visible index, or `null` when the index is outside the deque.
     *
     * Costs at most one level-ancestor query. The four cached endpoints — visible index zero, the
     * end of the left interval, the first value of the right interval, and the last visible index —
     * are answered from cached handles with no query at all. A stored `null` element is reported as
     * `null`; compare [index] against [size] to distinguish the two.
     *
     * @param index the zero-based visible index.
     */
    public operator fun get(index: Int): T? =
        if (index < 0 || index >= size) null else arena.valueAt(nodeAt(index))

    /**
     * A deque with [value] added at the front; the receiver is unchanged.
     *
     * Publishes exactly one arena leaf and makes no level-ancestor query, so it costs one leaf
     * addition: O(1) amortized with the shipped Myers arena.
     *
     * @throws ArithmeticException when the visible count or the ancestry depth cannot grow further.
     */
    public fun addFirst(value: T): BilateralAncestralDeque<T> {
        val count = Math.addExact(size, 1)
        return BilateralAncestralDeque(arena, addToTail(left, value), right, count)
    }

    /**
     * A deque with [value] added at the back; the receiver is unchanged.
     *
     * Publishes exactly one arena leaf and makes no level-ancestor query, so it costs one leaf
     * addition: O(1) amortized with the shipped Myers arena.
     *
     * @throws ArithmeticException when the visible count or the ancestry depth cannot grow further.
     */
    public fun addLast(value: T): BilateralAncestralDeque<T> {
        val count = Math.addExact(size, 1)
        return BilateralAncestralDeque(arena, left, addToTail(right, value), count)
    }

    /**
     * The deque without its first value, or `null` when the deque is empty.
     *
     * Makes no level-ancestor query while the left arm is nonempty, and at most one when the
     * removal crosses the centre into the right arm. Emptying the last value and dropping the
     * second of exactly two values on the far arm both cost no query.
     */
    public fun removeFirst(): BilateralAncestralDeque<T>? =
        if (isEmpty) null else removeFirstChecked()

    /**
     * The deque without its last value, or `null` when the deque is empty.
     *
     * Makes no level-ancestor query while the right arm is nonempty, and at most one when the
     * removal crosses the centre into the left arm. Emptying the last value and dropping the
     * second of exactly two values on the far arm both cost no query.
     */
    public fun removeLast(): BilateralAncestralDeque<T>? =
        if (isEmpty) null else removeLastChecked()

    /**
     * Removes the first value and returns it with the remaining deque, or `null` when the deque is
     * empty. The receiver is unchanged.
     *
     * The wrapper keeps a stored `null` element distinguishable from an empty deque.
     */
    public fun tryRemoveFirst(): BilateralAncestralDequePop<T>? {
        if (isEmpty) {
            return null
        }
        val node = if (left.count != 0) left.tail else right.base
        return BilateralAncestralDequePop(arena.valueAt(node), removeFirstChecked())
    }

    /**
     * Removes the last value and returns it with the remaining deque, or `null` when the deque is
     * empty. The receiver is unchanged.
     *
     * The wrapper keeps a stored `null` element distinguishable from an empty deque.
     */
    public fun tryRemoveLast(): BilateralAncestralDequePop<T>? {
        if (isEmpty) {
            return null
        }
        val node = if (right.count != 0) right.tail else left.base
        return BilateralAncestralDequePop(arena.valueAt(node), removeLastChecked())
    }

    /**
     * The first [count] values, or `null` when [count] is outside `0..size`.
     *
     * A prefix is a slice, so it costs at most two level-ancestor queries. Taking every value
     * returns the receiver itself.
     */
    public fun take(count: Int): BilateralAncestralDeque<T>? =
        if (count < 0 || count > size) null else takeChecked(count)

    /**
     * The deque after omitting its first [count] values, or `null` when [count] is outside
     * `0..size`.
     *
     * A suffix is a slice, so it costs at most two level-ancestor queries. Omitting nothing returns
     * the receiver itself.
     */
    public fun drop(count: Int): BilateralAncestralDeque<T>? =
        if (count < 0 || count > size) null else dropChecked(count)

    /**
     * One contiguous slice, or `null` when the index/count pair is outside this deque.
     *
     * Every contiguous slice intersects each interval at most once, so the result is another
     * bilateral handle and the operation costs **at most two** level-ancestor queries regardless of
     * how many values it selects — including the cross-arm case, where each arm spends at most one.
     * The whole range returns the receiver itself and an empty slice costs no query.
     *
     * @param index the zero-based first visible index.
     * @param count the number of visible values in the result.
     */
    public fun slice(index: Int, count: Int): BilateralAncestralDeque<T>? {
        if (index < 0 || count < 0 || index > size || index > size - count) {
            return null
        }
        return sliceChecked(index, count)
    }

    /**
     * The prefix and suffix at a zero-based boundary, or `null` when [index] is outside `0..size`.
     *
     * The two results together enumerate the original value. Although the split is expressed as a
     * prefix plus a suffix, the pair costs **at most two** level-ancestor queries in total, and an
     * endpoint boundary costs none: one half is the receiver itself and the other is an empty
     * handle.
     *
     * @param index the number of values placed in the left result.
     */
    public fun splitAt(index: Int): BilateralAncestralDequeSplit<T>? {
        if (index < 0 || index > size) {
            return null
        }
        return BilateralAncestralDequeSplit(takeChecked(index), dropChecked(index))
    }

    /**
     * The logical reversal, obtained by exchanging the two oriented intervals. O(1) with no query
     * and no rebuild; a deque of at most one value returns the receiver itself.
     */
    public fun reverse(): BilateralAncestralDeque<T> =
        if (size <= 1) this else BilateralAncestralDeque(arena, right, left, size)

    /**
     * An empty, independently appendable handle in the same arena. O(1) with no query; an already
     * empty deque returns the receiver itself.
     */
    public fun clear(): BilateralAncestralDeque<T> = if (isEmpty) this else emptyHandle()

    /**
     * The visible values in logical order.
     *
     * `Theta(size)` time with no level-ancestor query: both intervals are walked through parent
     * links. The forward-oriented right interval is collected tail first and reversed in place.
     */
    public fun toList(): List<T> {
        if (isEmpty) {
            return emptyList()
        }

        val values = ArrayList<T>(size)
        var node = left.tail
        for (index in 0 until left.count) {
            values.add(arena.valueAt(node))
            if (index + 1 != left.count) {
                node = arena.parentOf(node)
            }
        }

        val rightStart = values.size
        node = right.tail
        for (index in right.count - 1 downTo 0) {
            values.add(arena.valueAt(node))
            if (index != 0) {
                node = arena.parentOf(node)
            }
        }
        values.subList(rightStart, values.size).reverse()
        return values
    }

    /**
     * A stable front-to-back iterator over this immutable handle.
     *
     * Enumeration takes `Theta(size)` time and makes no level-ancestor query. The reversed left
     * interval streams through parent links with constant iterator state, while the forward-oriented
     * right interval is buffered when it is first reached, so the iterator can retain `O(size)`
     * values in the worst case.
     */
    override fun iterator(): Iterator<T> = SegmentIterator(arena, left.tail, left.count, right)

    /**
     * Independently checks this handle's cached interval endpoints and counts, and reports the
     * validated counts.
     *
     * The audit inspects a constant number of cached fields: it compares the cached counts against
     * the interval endpoint depths and confirms each cached base and anchor lies on its tail's
     * ancestry path. Confirming the ancestry path spends **two** level-ancestor queries per nonempty
     * interval, so the audit costs `O(1 + Q(M))` with a ceiling of four queries rather than O(1)
     * work; an empty handle spends none. It ports the reference's internal `ValidateInvariants`
     * audit, whose failures signal a corrupt representation rather than a caller error.
     *
     * @throws IllegalStateException when an interval endpoint, anchor, base, or count is invalid.
     */
    public fun validateStructure(): BilateralAncestralDequeStatistics {
        check(size == Math.addExact(left.count, right.count)) {
            "The total count disagrees with the interval counts."
        }
        validateSegment(left)
        validateSegment(right)
        return BilateralAncestralDequeStatistics(size, left.count, right.count)
    }

    /**
     * Whether both handles retain the same arena object and the same cached intervals, so they
     * denote the same values through the same physical nodes.
     *
     * This is an O(1) representation diagnostic intended for deterministic tests. Two handles over
     * different arenas never share storage even when they enumerate equal values.
     */
    public fun sharesStorageWith(other: BilateralAncestralDeque<T>): Boolean =
        arena === other.arena && size == other.size && left == other.left && right == other.right

    private fun nodeAt(index: Int): Int {
        if (index < left.count) {
            return when {
                index == 0 -> left.tail
                index == left.count - 1 -> left.base
                else -> arena.ancestorAtDepth(
                    left.tail,
                    Math.subtractExact(arena.depthOf(left.tail), index),
                )
            }
        }

        val rightIndex = index - left.count
        return when {
            rightIndex == 0 -> right.base
            rightIndex == right.count - 1 -> right.tail
            else -> arena.ancestorAtDepth(
                right.tail,
                Math.addExact(Math.addExact(arena.depthOf(right.anchor), rightIndex), 1),
            )
        }
    }

    private fun removeFirstChecked(): BilateralAncestralDeque<T> = when {
        size == 1 -> emptyHandle()
        left.count != 0 -> BilateralAncestralDeque(arena, removeTail(left), right, size - 1)
        else -> BilateralAncestralDeque(arena, left, removeBase(right), size - 1)
    }

    private fun removeLastChecked(): BilateralAncestralDeque<T> = when {
        size == 1 -> emptyHandle()
        right.count != 0 -> BilateralAncestralDeque(arena, left, removeTail(right), size - 1)
        else -> BilateralAncestralDeque(arena, removeBase(left), right, size - 1)
    }

    private fun takeChecked(count: Int): BilateralAncestralDeque<T> =
        if (count == size) this else sliceChecked(0, count)

    private fun dropChecked(count: Int): BilateralAncestralDeque<T> =
        if (count == 0) this else sliceChecked(count, size - count)

    private fun sliceChecked(index: Int, count: Int): BilateralAncestralDeque<T> {
        if (index == 0 && count == size) {
            return this
        }
        if (count == 0) {
            return emptyHandle()
        }

        val end = index + count
        val leftStart = min(index, left.count)
        val leftEnd = min(end, left.count)
        val rightStart = max(0, index - left.count)
        val rightEnd = max(0, end - left.count)

        return BilateralAncestralDeque(
            arena,
            sliceLeft(left, leftStart, leftEnd - leftStart),
            sliceRight(right, rightStart, rightEnd - rightStart),
            count,
        )
    }

    private fun emptyHandle(): BilateralAncestralDeque<T> {
        val empty = Segment.emptyAt(arena.bottom)
        return BilateralAncestralDeque(arena, empty, empty, 0)
    }

    private fun addToTail(segment: Segment, value: T): Segment {
        val tail = arena.addLeaf(segment.tail, value)
        val base = if (segment.count == 0) tail else segment.base
        return Segment(segment.anchor, base, tail, segment.count + 1)
    }

    private fun removeTail(segment: Segment): Segment {
        val tail = arena.parentOf(segment.tail)
        if (segment.count == 1) {
            return Segment.emptyAt(tail)
        }
        return Segment(segment.anchor, segment.base, tail, segment.count - 1)
    }

    private fun removeBase(segment: Segment): Segment {
        if (segment.count == 1) {
            return Segment.emptyAt(segment.tail)
        }

        val base = if (segment.count == 2) {
            segment.tail
        } else {
            arena.ancestorAtDepth(
                segment.tail,
                Math.addExact(Math.subtractExact(arena.depthOf(segment.tail), segment.count), 2),
            )
        }
        return Segment(segment.base, base, segment.tail, segment.count - 1)
    }

    private fun sliceLeft(segment: Segment, start: Int, count: Int): Segment {
        if (count == 0) {
            return Segment.emptyAt(arena.bottom)
        }
        if (start == 0 && count == segment.count) {
            return segment
        }

        val tailDepth = arena.depthOf(segment.tail)
        val baseDepth = Math.addExact(Math.subtractExact(tailDepth, segment.count), 1)
        val slicedTailDepth = Math.subtractExact(tailDepth, start)
        val slicedBaseDepth = Math.addExact(Math.subtractExact(slicedTailDepth, count), 1)

        val tail = if (slicedTailDepth == tailDepth) {
            segment.tail
        } else {
            arena.ancestorAtDepth(segment.tail, slicedTailDepth)
        }
        val base = when {
            slicedBaseDepth == slicedTailDepth -> tail
            slicedBaseDepth == baseDepth -> segment.base
            else -> arena.ancestorAtDepth(segment.tail, slicedBaseDepth)
        }

        val anchor = if (base == segment.base) segment.anchor else arena.parentOf(base)
        return Segment(anchor, base, tail, count)
    }

    private fun sliceRight(segment: Segment, start: Int, count: Int): Segment {
        if (count == 0) {
            return Segment.emptyAt(arena.bottom)
        }
        if (start == 0 && count == segment.count) {
            return segment
        }

        val anchorDepth = arena.depthOf(segment.anchor)
        val slicedBaseDepth = Math.addExact(Math.addExact(anchorDepth, start), 1)
        val slicedTailDepth = Math.subtractExact(Math.addExact(slicedBaseDepth, count), 1)
        val end = start + count

        val base = if (start == 0) {
            segment.base
        } else {
            arena.ancestorAtDepth(segment.tail, slicedBaseDepth)
        }
        val tail = when {
            slicedTailDepth == slicedBaseDepth -> base
            end == segment.count -> segment.tail
            else -> arena.ancestorAtDepth(segment.tail, slicedTailDepth)
        }

        val anchor = if (start == 0) segment.anchor else arena.parentOf(base)
        return Segment(anchor, base, tail, count)
    }

    private fun validateSegment(segment: Segment) {
        if (segment.count == 0) {
            check(segment.anchor == segment.base && segment.base == segment.tail) {
                "An empty interval does not have one shared endpoint."
            }
            return
        }

        val anchorDepth = arena.depthOf(segment.anchor)
        val tailDepth = arena.depthOf(segment.tail)
        check(Math.subtractExact(tailDepth, anchorDepth) == segment.count) {
            "An interval count disagrees with its endpoint depths."
        }
        check(arena.parentOf(segment.base) == segment.anchor) {
            "The cached base is not the first node after the anchor."
        }
        check(arena.ancestorAtDepth(segment.tail, anchorDepth) == segment.anchor) {
            "The interval anchor is not an ancestor of its tail."
        }
        check(arena.ancestorAtDepth(segment.tail, Math.addExact(anchorDepth, 1)) == segment.base) {
            "The cached base is not on the tail's ancestry path."
        }
    }

    /**
     * One oriented ancestry interval `(anchor, tail]`.
     *
     * `base` is redundant but load-bearing: it caches the first physical node after `anchor`, which
     * keeps both endpoint reads query-free even when the opposite arm is empty. For the left arm the
     * logical first value sits at `tail` and the logical last at `base`; for the right arm those
     * roles are exchanged.
     */
    private data class Segment(val anchor: Int, val base: Int, val tail: Int, val count: Int) {
        companion object {
            /** The empty interval whose three endpoints are one shared node. */
            fun emptyAt(node: Int): Segment = Segment(node, node, node, 0)
        }
    }

    /**
     * A front-to-back iterator over one immutable bilateral handle.
     *
     * The reversed left interval streams through parent links. The forward right interval is
     * buffered when it is first reached, which keeps enumeration query-free at the cost of retaining
     * up to `O(count)` values.
     */
    private class SegmentIterator<E>(
        private val arena: IncrementalAncestorArena<E>,
        private var leftNode: Int,
        private var leftRemaining: Int,
        private val right: Segment,
    ) : Iterator<E> {
        private var rightValues: List<E>? = null
        private var rightIndex: Int = 0

        override fun hasNext(): Boolean = leftRemaining != 0 || rightIndex != right.count

        override fun next(): E {
            if (leftRemaining != 0) {
                val value = arena.valueAt(leftNode)
                leftRemaining--
                if (leftRemaining != 0) {
                    leftNode = arena.parentOf(leftNode)
                }
                return value
            }
            if (rightIndex == right.count) {
                throw NoSuchElementException("The bilateral ancestral deque iterator is exhausted.")
            }

            val values = rightValues ?: buildRightValues().also { rightValues = it }
            return values[rightIndex++]
        }

        private fun buildRightValues(): List<E> {
            val values = ArrayList<E>(right.count)
            var node = right.tail
            for (index in right.count - 1 downTo 0) {
                values.add(arena.valueAt(node))
                if (index != 0) {
                    node = arena.parentOf(node)
                }
            }
            values.reverse()
            return values
        }
    }
}
