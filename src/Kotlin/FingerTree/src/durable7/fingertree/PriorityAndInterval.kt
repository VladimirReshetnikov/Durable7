/*
 * Persistent priority queue and interval tree over the measured tree.
 *
 * The queue caches the minimum-priority entry as its measure, so peeking is a cached read and
 * removal is a measure-directed descent. The interval tree caches each subtree's maximum high
 * endpoint, so overlap and stabbing queries prune subtrees that cannot reach the probe.
 */
package durable7.fingertree

/** One queued value together with the priority it is ordered by. */
public data class PriorityEntry<T, P>(
    public val value: T,
    public val priority: P,
)

/** The entry removed by a dequeue, together with the queue that remains. */
public data class PriorityDequeue<T, P>(
    public val entry: PriorityEntry<T, P>,
    public val queue: PriorityQueue<T, P>,
)

private data class PrioritySummary<T, P>(val best: PriorityEntry<T, P>?)

private class PriorityMeasurePolicy<T, P>(
    private val comparator: Comparator<in P>,
) : MeasurePolicy<PriorityEntry<T, P>, PrioritySummary<T, P>> {
    override val empty: PrioritySummary<T, P> = PrioritySummary(null)

    override fun measure(element: PriorityEntry<T, P>): PrioritySummary<T, P> = PrioritySummary(element)

    override fun combine(
        left: PrioritySummary<T, P>,
        right: PrioritySummary<T, P>,
    ): PrioritySummary<T, P> {
        val leftEntry = left.best ?: return right
        val rightEntry = right.best ?: return left
        return if (comparator.compare(leftEntry.priority, rightEntry.priority) <= 0) left else right
    }

    override fun equals(other: Any?): Boolean =
        other is PriorityMeasurePolicy<*, *> && (comparator === other.comparator || comparator == other.comparator)

    override fun hashCode(): Int = comparator.hashCode()
}

/**
 * A persistent priority queue caching the minimum-priority entry as its measure, so peeking is a cached read and
 * removal is a measure-directed descent. Equal priorities are served in enqueue order.
 */
public class PriorityQueue<T, P> private constructor(
    private val entries: PersistentMeasuredTree<PriorityEntry<T, P>, PrioritySummary<T, P>>,
    private val comparator: Comparator<in P>,
) : Iterable<PriorityEntry<T, P>> {
    public companion object {
        /**
         * An empty queue ordered by the natural order of priorities. The comparator is the standard library's
         * shared singleton, so two queues built this way can be melded.
         */
        public fun <T, P : Comparable<P>> empty(): PriorityQueue<T, P> =
            empty(naturalComparator())

        /**
         * An empty queue ordered by [comparator], which the queue retains by identity. Only queues carrying the
         * same comparator can be melded.
         */
        public fun <T, P> empty(comparator: Comparator<in P>): PriorityQueue<T, P> =
            PriorityQueue(PersistentMeasuredTree.empty(PriorityMeasurePolicy(comparator)), comparator)
    }

    /** Number of entries. O(1), from the cached measure at the root. */
    public val size: Int
        get() = entries.size

    /** Whether the queue holds no entries. */
    public val isEmpty: Boolean
        get() = entries.isEmpty

    /**
     * A queue with [value] enqueued at [priority]. Amortized O(1): the entry is appended and the cached
     * best-priority summary is updated along the spine, rather than the entry being sifted into place.
     */
    public fun enqueue(value: T, priority: P): PriorityQueue<T, P> =
        PriorityQueue(entries.append(PriorityEntry(value, priority)), comparator)

    /**
     * A queue holding every entry of both.
     *
     * @throws IllegalArgumentException if the two queues do not carry the same comparator, since merging entries
     * ordered by different comparators would produce a queue whose best-priority summary is meaningless.
     */
    public fun meld(other: PriorityQueue<T, P>): PriorityQueue<T, P> {
        require(comparator === other.comparator || comparator == other.comparator) {
            "Cannot meld priority queues with different comparators."
        }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> PriorityQueue(entries.concat(other.entries), comparator)
        }
    }

    /** The entry with the best priority, or `null` when empty. O(1), read from the cached root summary. */
    public fun peekEntry(): PriorityEntry<T, P>? = entries.measure().best

    /** The best-priority value and its priority, or `null` when empty. */
    public fun peek(): Pair<T, P>? = peekEntry()?.let { it.value to it.priority }

    /** The best priority, or `null` when empty. */
    public fun peekPriority(): P? = peekEntry()?.priority

    /**
     * The best-priority entry together with the queue that remains, or `null` when empty. Ties are broken by the
     * entry the cached summary selected, which is stable for a given queue but is not a documented ordering between
     * equal priorities.
     */
    public fun dequeue(): PriorityDequeue<T, P>? {
        val entry = peekEntry() ?: return null
        val located = entries.locate { it.best === entry }
        check(located.found) { "Cached priority summary did not identify an entry." }
        return PriorityDequeue(entry, PriorityQueue(entries.removeAt(located.index)!!, comparator))
    }

    /** The entries in storage order, which is enqueue order rather than priority order. */
    public fun toList(): List<PriorityEntry<T, P>> = entries.toList()

    /**
     * Whether this queue and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common.
     */
    public fun sharesStorageWith(other: PriorityQueue<T, P>): Boolean = entries.sharesStructureWith(other.entries)

    internal fun debugIsBalanced(): Boolean = entries.isBalanced()

    override fun iterator(): Iterator<PriorityEntry<T, P>> = entries.iterator()

}

/**
 * A closed interval. The low endpoint never exceeds the high one; construction rejects an inverted interval rather than
 * letting it produce silently empty query results.
 */
public data class Interval<T : Comparable<T>>(
    public val low: T,
    public val high: T,
) {
    init {
        require(low <= high) { "Interval low endpoint must not exceed high endpoint." }
    }

    /**
     * Whether the two intervals share at least one point. Endpoints are inclusive, so `[1, 2]` and `[2, 3]`
     * overlap.
     */
    public fun overlaps(other: Interval<T>): Boolean =
        low <= other.high && other.low <= high

    /** Whether [point] lies in the interval. Endpoints are inclusive. */
    public fun containsPoint(point: T): Boolean =
        low <= point && point <= high
}

/** The tree remaining after a removal, together with the interval that was removed. */
public data class IntervalRemoveResult<T : Comparable<T>>(
    public val tree: IntervalTree<T>,
    public val interval: Interval<T>,
)

private data class IntervalSummary<T>(
    val maximumHigh: T?,
    val lastLow: T?,
)

private class IntervalMeasurePolicy<T : Comparable<T>> : MeasurePolicy<Interval<T>, IntervalSummary<T>> {
    override val empty: IntervalSummary<T> = IntervalSummary(null, null)

    override fun measure(element: Interval<T>): IntervalSummary<T> =
        IntervalSummary(element.high, element.low)

    override fun combine(left: IntervalSummary<T>, right: IntervalSummary<T>): IntervalSummary<T> {
        val maximumHigh = when {
            left.maximumHigh == null -> right.maximumHigh
            right.maximumHigh == null -> left.maximumHigh
            left.maximumHigh >= right.maximumHigh -> left.maximumHigh
            else -> right.maximumHigh
        }
        return IntervalSummary(maximumHigh, right.lastLow ?: left.lastLow)
    }

    override fun equals(other: Any?): Boolean = other is IntervalMeasurePolicy<*>
    override fun hashCode(): Int = IntervalMeasurePolicy::class.hashCode()
}

/**
 * A persistent bag of closed intervals. Each subtree's maximum high endpoint is cached, so a query visits only the
 * subtrees that can still contain a match. Duplicates are permitted.
 */
public class IntervalTree<T : Comparable<T>> private constructor(
    private val intervals: PersistentMeasuredTree<Interval<T>, IntervalSummary<T>>,
) : Iterable<Interval<T>> {
    public companion object {
        /** An empty tree. */
        public fun <T : Comparable<T>> empty(): IntervalTree<T> =
            IntervalTree(PersistentMeasuredTree.empty(IntervalMeasurePolicy()))

        /**
         * A tree holding every interval, inserted in order. Duplicates are kept: an interval tree is a multiset of
         * intervals.
         */
        public fun <T : Comparable<T>> from(values: Iterable<Interval<T>>): IntervalTree<T> {
            var result = empty<T>()
            for (interval in values) {
                result = result.insert(interval)
            }

            return result
        }
    }

    /** Number of stored intervals. O(1). */
    public val size: Int
        get() = intervals.size

    /** Whether the tree holds no intervals. */
    public val isEmpty: Boolean
        get() = intervals.isEmpty

    /**
     * A tree with [interval] added. Intervals are ordered by low endpoint alone, and a new interval is placed
     * before every stored interval with an equal low endpoint, matching the C# reference.
     */
    public fun insert(interval: Interval<T>): IntervalTree<T> {
        // Matches the C# reference: intervals are ordered by low endpoint
        // only, and a new interval goes before every existing equal-low one.
        val index = lowerBoundByLow(interval.low)
        return IntervalTree(intervals.insertAt(index, interval)!!)
    }

    /** Whether an interval equal to [interval] - matching on both endpoints - is stored. */
    public fun contains(interval: Interval<T>): Boolean = indexOf(interval) >= 0

    /**
     * A tree with one interval equal to [interval] removed. Returns the receiver unchanged when none is stored.
     */
    public fun remove(interval: Interval<T>): IntervalTree<T> = tryRemove(interval)?.tree ?: this

    /**
     * Remove one interval equal to [interval], returning the resulting tree together with the interval that was
     * removed, or `null` when none is stored.
     */
    public fun tryRemove(interval: Interval<T>): IntervalRemoveResult<T>? {
        val index = indexOf(interval)
        if (index < 0) {
            return null
        }

        val stored = intervalAt(index)
        return IntervalRemoveResult(IntervalTree(intervals.removeAt(index)!!), stored)
    }

    private fun lowerBoundByLow(low: T): Int {
        return intervals.locate { summary -> summary.lastLow != null && summary.lastLow >= low }.index
    }

    /**
     * Rank of the first stored interval whose endpoints both compare equal to
     * [interval]'s (matching the C# reference, which matches by comparison,
     * not [equals]) — a low-endpoint search plus a scan over the equal-low run.
     */
    private fun indexOf(interval: Interval<T>): Int {
        var index = lowerBoundByLow(interval.low)
        while (index < intervals.size) {
            val current = intervalAt(index)
            if (current.low.compareTo(interval.low) != 0) {
                return -1
            }

            if (current.high.compareTo(interval.high) == 0) {
                return index
            }

            index += 1
        }

        return -1
    }

    /**
     * Some stored interval overlapping [probe], or `null` when none does. Which one is returned is unspecified when
     * several overlap; use [findOverlaps] to see them all.
     */
    public fun findOverlap(probe: Interval<T>): Interval<T>? = nextOverlap(probe, intervals)?.first

    /**
     * Some stored interval containing [point], or `null` when none does. Equivalent to an overlap query against the
     * degenerate interval `[point, point]`.
     */
    public fun findContaining(point: T): Interval<T>? = findOverlap(Interval(point, point))

    /**
     * Every stored interval overlapping [probe], in low-endpoint order. Costs O(k log n) for k results: the cached
     * maximum high endpoint lets the descent skip subtrees that cannot overlap.
     */
    public fun findOverlaps(probe: Interval<T>): List<Interval<T>> {
        val result = ArrayList<Interval<T>>()
        var remaining = intervals
        while (true) {
            val next = nextOverlap(probe, remaining) ?: break
            result.add(next.first)
            remaining = next.second
        }
        return result
    }

    /** How many stored intervals overlap [probe]. Enumerates them, so it costs the same as [findOverlaps]. */
    public fun countOverlaps(probe: Interval<T>): Int =
        findOverlaps(probe).size

    /**
     * A tree whose overlapping and adjacent intervals have been merged into maximal runs. Returns the receiver when
     * there is nothing to merge.
     */
    public fun coalesce(): IntervalTree<T> {
        if (intervals.size < 2) {
            return this
        }

        val result = ArrayList<Interval<T>>()
        val iterator = intervals.iterator()
        var current = iterator.next()
        while (iterator.hasNext()) {
            val next = iterator.next()
            if (next.low <= current.high) {
                val high = if (current.high >= next.high) current.high else next.high
                current = Interval(current.low, high)
            } else {
                result.add(current)
                current = next
            }
        }

        result.add(current)
        return from(result)
    }

    /** The intervals in low-endpoint order. */
    public fun toList(): List<Interval<T>> = intervals.toList()

    /**
     * Whether this tree and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common.
     */
    public fun sharesStorageWith(other: IntervalTree<T>): Boolean = intervals.sharesStructureWith(other.intervals)

    internal fun debugIsBalanced(): Boolean = intervals.isBalanced()

    internal fun cursorLowerBound(low: T): Int = lowerBoundByLow(low)

    internal fun cursorUpperBound(low: T): Int {
        var index = lowerBoundByLow(low)
        while (index < size && intervalAt(index).low.compareTo(low) == 0) index++
        return index
    }

    internal fun cursorIntervalAt(rank: Int): Interval<T>? =
        if (rank < 0 || rank >= size) null else intervalAt(rank)

    internal fun cursorRemoveAt(rank: Int): IntervalTree<T> =
        IntervalTree(checkNotNull(intervals.removeAt(rank)))

    override fun iterator(): Iterator<Interval<T>> = intervals.iterator()

    @Suppress("UNCHECKED_CAST")
    private fun intervalAt(index: Int): Interval<T> = intervals[index] as Interval<T>

    private fun nextOverlap(
        probe: Interval<T>,
        source: PersistentMeasuredTree<Interval<T>, IntervalSummary<T>>,
    ): Pair<Interval<T>, PersistentMeasuredTree<Interval<T>, IntervalSummary<T>>>? {
        val located = source.locate { summary -> summary.maximumHigh != null && summary.maximumHigh >= probe.low }
        if (!located.found) {
            return null
        }
        @Suppress("UNCHECKED_CAST")
        val candidate = located.value as Interval<T>
        if (candidate.low > probe.high) {
            return null
        }
        val rest = source.splitAt(located.index + 1)!!.second
        return candidate to rest
    }
}
