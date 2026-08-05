/*
 * A persistent queue whose every version is one contiguous interval of an append-only ancestry path.
 *
 * A value owns no nodes. It is the constant-sized pair (tail, lowDepth) naming an interval of the
 * unique bottom-to-tail path inside a shared incremental-ancestor arena, plus a redundant count
 * cache, so the visible values are the labels of the ancestors of the tail at absolute depths
 * lowDepth through depthOf(tail). Appending publishes one child below the tail; removing and slicing
 * move only the two interval boundaries. Because the arena is append-only and its published nodes
 * are immutable, every retained handle keeps denoting the same sequence forever, and adding below an
 * old handle starts a sibling branch instead of disturbing it.
 */
package durable7.fingertree

/**
 * One removed endpoint value together with the persistent queue that remains without it.
 *
 * The wrapper keeps a successful removal distinguishable from an empty queue even when the element
 * type is nullable, following this workspace's [RrbPop] precedent.
 *
 * @param T the element type.
 * @property value the removed endpoint value.
 * @property rest the persistent queue remaining after that endpoint is removed.
 */
public data class AncestralSlicePop<T>(
    public val value: T,
    public val rest: AncestralSliceQueue<T>,
)

/**
 * The two appendable ancestry intervals produced by [AncestralSliceQueue.splitAt].
 *
 * @param T the element type.
 * @property left the prefix holding the first `index` visible values.
 * @property right the suffix holding the remaining visible values.
 */
public data class AncestralSliceSplit<T>(
    public val left: AncestralSliceQueue<T>,
    public val right: AncestralSliceQueue<T>,
)

/**
 * Independently recomputed ancestry-interval metadata.
 *
 * @property count the cached visible count the validated handle reported.
 * @property lowDepth the absolute arena depth of the first visible value, which for an empty window
 *   is one greater than the depth of its retained anchor.
 * @property tailDepth the absolute arena depth of the retained tail node; it always equals
 *   `lowDepth + count - 1`, which for an empty window is the anchored-empty rule as an equation.
 * @property walkedNodeCount the number of ancestry nodes reached by following parent links from the
 *   tail, recomputed rather than read from the cached count.
 * @property publishedNodeCount the arena's labelled-node count when the validation ran.
 */
public data class AncestralSliceQueueStatistics(
    public val count: Int,
    public val lowDepth: Int,
    public val tailDepth: Int,
    public val walkedNodeCount: Int,
    public val publishedNodeCount: Int,
)

/**
 * An immutable, fully persistent queue slice denoting one interval of an append ancestry path.
 *
 * A value retains its arena, a tail node, the absolute depth of its first visible element, and a
 * redundant count cache. Empty values retain the node immediately before their window as an anchor,
 * so appending to any empty slice yields exactly the new value without exposing a discarded prefix;
 * a queue drained from the front and a queue drained from the back are both empty yet keep different
 * anchors. All handles are immutable, and adding below an old handle creates a sibling branch in the
 * shared monotone arena. Two handles may enumerate equal values while retaining different tails,
 * anchors, or arenas, so this type promises no constant-time sequence equality, no hashing, and no
 * canonical empty identity, and implements none of them.
 *
 * The bounds are parameterized by the backend. Let `M` be the number of labelled nodes ever
 * published by the arena, `U(M)` the cost of adding a leaf, and `Q(M)` the cost of one level-ancestor
 * query. Then [addLast] costs `U(M)`; [first], [get], [take], [slice], [popFirst], and a nontrivial
 * [splitAt] cost `Q(M)`; and [size], [isEmpty], [last], [removeFirst], [removeLast], [popLast], and
 * [drop] are O(1). Boundary-specialized calls make no ancestor query at all: `take(size)`, `drop(0)`,
 * the whole-range slice, any slice whose result ends at the retained tail, an index at the last
 * visible position, a singleton's [first], and a split at [size]. A split at `0` of a non-empty queue
 * is not query-free, because the anchored-empty rule requires the empty prefix to retain the node at
 * depth `lowDepth - 1` and only an ancestor query can name that node from the retained tail; on an
 * empty queue both split boundaries coincide with the whole range, so that split is query-free.
 *
 * Instantiating the backend with Alstrup-Holm incremental level ancestry would give
 * `U(M) = Q(M) = O(1)` worst case in linear arena space. That backend is not implemented here; the
 * statement is a reduction to a published result. The shipped [MyersIncrementalAncestorArena] has
 * O(1)-amortized leaf addition and O(log M) worst-case ancestor queries, so no amortized bound above
 * may be read as a worst-case bound.
 *
 * The algebra intentionally excludes prepending, point replacement, arbitrary middle edits, and
 * concatenation of unrelated histories; omitting them is precisely why the indexed and slicing bounds
 * are possible. Arena space is charged to all successful historical appends, not to the visible
 * length of one handle: dropping a prefix or releasing a handle reclaims nothing, and every published
 * value stays reachable until the arena itself becomes collectible.
 *
 * Handles are immutable and are safe for concurrent readers to the same extent as their arena and
 * their caller-owned elements. The shipped arena serializes its reads and writes on one private
 * monitor; that is a semantic thread-safety property, not a parallel-progress guarantee.
 *
 * @param T the element type.
 */
public class AncestralSliceQueue<T> private constructor(
    private val arena: IncrementalAncestorArena<T>,
    private val tail: Int,
    private val lowDepth: Int,
    count: Int,
) : Iterable<T> {
    /** Factories for anchored queues over a shipped or caller-supplied ancestor arena. */
    public companion object {
        /**
         * Creates an empty queue owned by an explicit incremental-ancestor arena.
         *
         * This is the public extension seam: supplying a backend with different `U(M)`/`Q(M)` costs
         * changes every parameterized bound this type documents. The arena retains and navigates
         * every appended node. This factory is O(1) and publishes nothing.
         *
         * @param arena the manager that will retain and navigate every appended node.
         * @throws IllegalArgumentException when the arena's bottom node is not at depth `-1`, so
         *   that no anchored-empty queue can be expressed over it.
         */
        public fun <T> empty(arena: IncrementalAncestorArena<T>): AncestralSliceQueue<T> {
            val bottom = arena.bottom
            require(arena.depthOf(bottom) == -1) { "The arena bottom node must have depth -1." }
            return AncestralSliceQueue(arena, bottom, lowDepth = 0, count = 0)
        }

        /**
         * Creates an empty queue backed by a fresh, independently collectible Myers jump-link arena.
         *
         * There is deliberately no shared global empty value: each call owns its own arena, so
         * unrelated workloads never share retained history or write contention. This factory is O(1).
         */
        public fun <T> empty(): AncestralSliceQueue<T> = empty(MyersIncrementalAncestorArena<T>())

        /**
         * Creates a Myers-backed queue holding [values] in iteration order.
         *
         * Publishes one arena node per value, for Theta(k) amortized total time on the shipped
         * backend and no ancestor query.
         *
         * @param values the values to append, captured exactly once in iteration order.
         * @throws ArithmeticException when the visible count or the ancestry depth cannot grow
         *   further.
         */
        public fun <T> from(values: Iterable<T>): AncestralSliceQueue<T> {
            var result = empty<T>()
            for (value in values) {
                result = result.addLast(value)
            }
            return result
        }
    }

    /** The number of visible values, read from the cache in O(1) without consulting the arena. */
    public val size: Int = count

    /** Whether this handle denotes an empty ancestry interval. This read is O(1). */
    public val isEmpty: Boolean
        get() = size == 0

    /**
     * The first visible value, or `null` when the queue is empty.
     *
     * The front is an ancestor selected jointly by the tail and the low boundary, so this costs one
     * ancestor query `Q(M)` rather than being an O(1) endpoint read; a singleton reuses its retained
     * tail and makes no query. A queue whose element type is nullable cannot distinguish a stored
     * `null` from an empty queue through this property; use [isEmpty] or [popFirst] for that.
     */
    public val first: T?
        get() = if (size == 0) null else arena.valueAt(firstNode())

    /**
     * The last visible value, or `null` when the queue is empty.
     *
     * This is O(1): the tail is retained directly by the handle. A queue whose element type is
     * nullable cannot distinguish a stored `null` from an empty queue through this property; use
     * [isEmpty] or [popLast] for that.
     */
    public val last: T?
        get() = if (size == 0) null else arena.valueAt(tail)

    /**
     * Returns the visible value at a zero-based [index], or `null` outside `0 until size`.
     *
     * This costs one ancestor query `Q(M)`; the last visible index reuses the retained tail and makes
     * no query. A queue whose element type is nullable cannot distinguish a stored `null` from an
     * out-of-range index through this operator; validate the index against [size] for that.
     *
     * @param index the zero-based visible index.
     * @throws ArithmeticException when the addressed absolute depth exceeds [Int.MAX_VALUE].
     */
    public operator fun get(index: Int): T? {
        if (index < 0 || index >= size) {
            return null
        }
        val node = if (index == size - 1) tail else arena.ancestorAtDepth(tail, Math.addExact(lowDepth, index))
        return arena.valueAt(node)
    }

    /**
     * Returns a queue with [value] appended below this handle's tail or empty anchor.
     *
     * This costs `U(M)` and publishes exactly one arena node. This handle and every other retained
     * version are unaffected; appending below an old handle starts a sibling branch. The count is
     * checked before the node is published, so a rejected addition publishes nothing.
     *
     * @param value the value to append.
     * @throws ArithmeticException when the visible count or the ancestry depth cannot grow further.
     */
    public fun addLast(value: T): AncestralSliceQueue<T> {
        val grown = Math.addExact(size, 1)
        return AncestralSliceQueue(arena, arena.addLeaf(tail, value), lowDepth, grown)
    }

    /**
     * Returns the queue without its first visible value, or `null` when the queue is empty.
     *
     * This is O(1) and makes no ancestor query, because only the low boundary moves. Removing the one
     * visible value of a singleton this way leaves the old tail as the empty result's anchor.
     *
     * @throws ArithmeticException when the low boundary cannot move past [Int.MAX_VALUE].
     */
    public fun removeFirst(): AncestralSliceQueue<T>? =
        if (size == 0) null else AncestralSliceQueue(arena, tail, Math.addExact(lowDepth, 1), size - 1)

    /**
     * Returns the queue without its last visible value, or `null` when the queue is empty.
     *
     * This is O(1): the new tail is the old tail's parent, which the arena reads in constant time.
     * Emptying a singleton this way anchors the result immediately before the window.
     */
    public fun removeLast(): AncestralSliceQueue<T>? =
        if (size == 0) null else AncestralSliceQueue(arena, arena.parentOf(tail), lowDepth, size - 1)

    /**
     * Removes and returns the first visible value together with the remainder, or `null` when the
     * queue is empty.
     *
     * This costs `Q(M)` because it also reports the front value. Nothing is published on either path,
     * and this handle stays valid and reusable.
     *
     * @throws ArithmeticException when the low boundary cannot move past [Int.MAX_VALUE].
     */
    public fun popFirst(): AncestralSlicePop<T>? {
        if (size == 0) {
            return null
        }
        val value = arena.valueAt(firstNode())
        return AncestralSlicePop(value, AncestralSliceQueue(arena, tail, Math.addExact(lowDepth, 1), size - 1))
    }

    /**
     * Removes and returns the last visible value together with the remainder, or `null` when the
     * queue is empty.
     *
     * This is O(1) and makes no ancestor query.
     */
    public fun popLast(): AncestralSlicePop<T>? {
        if (size == 0) {
            return null
        }
        return AncestralSlicePop(arena.valueAt(tail), AncestralSliceQueue(arena, arena.parentOf(tail), lowDepth, size - 1))
    }

    /**
     * Returns the first [count] visible values, or `null` when [count] is outside `0..size`.
     *
     * This costs `Q(M)`; `take(size)` returns this handle itself and makes no query. The result is a
     * real ancestry slice and remains appendable, and `take(0)` is anchored immediately before the
     * window.
     *
     * @param count the prefix length.
     * @throws ArithmeticException when the addressed absolute depth exceeds [Int.MAX_VALUE].
     */
    public fun take(count: Int): AncestralSliceQueue<T>? {
        if (count < 0 || count > size) {
            return null
        }
        return if (count == size) this else slice(0, count)
    }

    /**
     * Returns the queue after omitting its first [count] visible values, or `null` when [count] is
     * outside `0..size`.
     *
     * This is O(1) and makes no ancestor query, because only the low boundary moves. `drop(0)`
     * returns this handle itself, and `drop(size)` keeps the old tail as the empty result's anchor.
     *
     * @param count the number of values to omit.
     * @throws ArithmeticException when the low boundary cannot move past [Int.MAX_VALUE].
     */
    public fun drop(count: Int): AncestralSliceQueue<T>? {
        if (count < 0 || count > size) {
            return null
        }
        if (count == 0) {
            return this
        }
        return AncestralSliceQueue(arena, tail, Math.addExact(lowDepth, count), size - count)
    }

    /**
     * Returns one contiguous, appendable ancestry slice, or `null` for a range outside this queue.
     *
     * This costs `Q(M)` in general. The whole range returns this handle itself, and any range whose
     * result ends at the retained tail — every suffix — reuses that tail and makes no query. A
     * zero-length range selects the ancestor immediately before its start as an anchor, which is the
     * arena's bottom node when the start is the very front of a queue grown from an empty one.
     *
     * @param index the zero-based first visible index.
     * @param count the number of visible values in the result.
     * @throws ArithmeticException when an addressed absolute depth exceeds [Int.MAX_VALUE].
     */
    public fun slice(index: Int, count: Int): AncestralSliceQueue<T>? {
        if (index < 0 || count < 0 || count > size || index > size - count) {
            return null
        }
        if (index == 0 && count == size) {
            return this
        }

        val low = Math.addExact(lowDepth, index)
        val targetDepth = Math.addExact(low, count - 1)
        val currentTailDepth = Math.addExact(lowDepth, size - 1)
        val node = if (targetDepth == currentTailDepth) tail else arena.ancestorAtDepth(tail, targetDepth)
        return AncestralSliceQueue(arena, node, low, count)
    }

    /**
     * Splits this queue at a zero-based boundary, or returns `null` when [index] is outside `0..size`.
     *
     * This costs `Q(M)`: the prefix performs the one ancestor-seeking query and the suffix only moves
     * the low boundary. Both results are real ancestry slices and can independently start new
     * branches. A split at [size] makes no query, and neither does a split of an empty queue, whose
     * two boundaries coincide with its whole range; a split at `0` of a non-empty queue does make
     * one, because the anchored-empty rule requires the empty prefix to retain the node at depth
     * `lowDepth - 1`.
     *
     * @param index the number of visible values placed in the left result.
     * @throws ArithmeticException when an addressed absolute depth exceeds [Int.MAX_VALUE].
     */
    public fun splitAt(index: Int): AncestralSliceSplit<T>? {
        if (index < 0 || index > size) {
            return null
        }
        val left = take(index) ?: error("A validated boundary must be takeable.")
        val right = drop(index) ?: error("A validated boundary must be droppable.")
        return AncestralSliceSplit(left, right)
    }

    /**
     * Materializes the visible values in front-to-back order.
     *
     * This follows parent links from the tail into one temporary array rather than performing [size]
     * ancestor queries, taking Theta([size]) time and Theta([size]) transient element slots.
     * Concurrent appends cannot enter the captured path.
     */
    public fun toList(): List<T> {
        val values = ArrayList<T>(size)
        if (size == 0) {
            return values
        }

        var node = tail
        for (remaining in size - 1 downTo 0) {
            values.add(arena.valueAt(node))
            if (remaining != 0) {
                node = arena.parentOf(node)
            }
        }
        values.reverse()
        return values
    }

    /**
     * Returns a stable front-to-back iterator over this immutable ancestry interval.
     *
     * The iterator captures the whole path up front, so it observes exactly the handle it was
     * obtained from and never sees a later append. See [toList] for the cost.
     */
    override fun iterator(): Iterator<T> = toList().iterator()

    /** Returns whether both handles are navigated by the exact same arena object. This is O(1). */
    public fun sharesArenaWith(other: AncestralSliceQueue<T>): Boolean = arena === other.arena

    /**
     * Returns whether both handles retain the same arena and the same tail node, and therefore the
     * identical retained ancestry path. Intended for deterministic sharing tests; this is O(1).
     */
    public fun sharesTailWith(other: AncestralSliceQueue<T>): Boolean =
        arena === other.arena && tail == other.tail

    /**
     * Independently recomputes this handle's window against the arena and reports what it found.
     *
     * The validation walks [size] parent links and reads only depths, so it takes Theta([size]) time,
     * allocates O(1), and makes no ancestor query; a counting arena's query statistics are therefore
     * undisturbed.
     *
     * @throws IllegalStateException when the cached count, the low boundary, or the retained tail
     *   depth contradicts the ancestry path, including a violation of the anchored-empty rule.
     */
    public fun validateStructure(): AncestralSliceQueueStatistics {
        check(size >= 0) { "An ancestral slice queue cannot cache a negative count." }
        check(lowDepth >= 0) { "An ancestral slice window cannot begin above the arena bottom." }

        val tailDepth = arena.depthOf(tail)
        check(tailDepth == Math.addExact(lowDepth, size) - 1) {
            "The retained tail depth must equal lowDepth + count - 1."
        }

        var node = tail
        var walked = 0
        while (walked < size) {
            check(arena.depthOf(node) == tailDepth - walked) {
                "An ancestry path depth must decrease by exactly one per parent link."
            }
            walked++
            if (walked < size) {
                node = arena.parentOf(node)
            }
        }
        if (size != 0) {
            check(arena.depthOf(node) == lowDepth) {
                "The oldest visible node must sit at the window's low depth."
            }
        }

        return AncestralSliceQueueStatistics(size, lowDepth, tailDepth, walked, arena.publishedNodeCount)
    }

    /**
     * Returns the constant-sized window header, mirroring the reference debugger display.
     *
     * This is O(1) and reads no values; use [toList] to render the visible contents.
     */
    override fun toString(): String = "AncestralSliceQueue(size=$size, lowDepth=$lowDepth, tail=$tail)"

    private fun firstNode(): Int = if (size == 1) tail else arena.ancestorAtDepth(tail, lowDepth)
}
