package tools.datastructures.fingertree

public data class PriorityEntry<T, P>(
    public val value: T,
    public val priority: P,
)

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

public class PriorityQueue<T, P> private constructor(
    private val entries: PersistentMeasuredTree<PriorityEntry<T, P>, PrioritySummary<T, P>>,
    private val comparator: Comparator<in P>,
) : Iterable<PriorityEntry<T, P>> {
    public companion object {
        public fun <T, P : Comparable<P>> empty(): PriorityQueue<T, P> =
            empty(naturalComparator())

        public fun <T, P> empty(comparator: Comparator<in P>): PriorityQueue<T, P> =
            PriorityQueue(PersistentMeasuredTree.empty(PriorityMeasurePolicy(comparator)), comparator)
    }

    public val size: Int
        get() = entries.size

    public val isEmpty: Boolean
        get() = entries.isEmpty

    public fun enqueue(value: T, priority: P): PriorityQueue<T, P> =
        PriorityQueue(entries.append(PriorityEntry(value, priority)), comparator)

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

    public fun peekEntry(): PriorityEntry<T, P>? = entries.measure().best

    public fun peek(): Pair<T, P>? = peekEntry()?.let { it.value to it.priority }

    public fun peekPriority(): P? = peekEntry()?.priority

    public fun dequeue(): PriorityDequeue<T, P>? {
        val entry = peekEntry() ?: return null
        val located = entries.locate { it.best === entry }
        check(located.found) { "Cached priority summary did not identify an entry." }
        return PriorityDequeue(entry, PriorityQueue(entries.removeAt(located.index)!!, comparator))
    }

    public fun toList(): List<PriorityEntry<T, P>> = entries.toList()

    public fun sharesStorageWith(other: PriorityQueue<T, P>): Boolean = entries.sharesStructureWith(other.entries)

    internal fun debugIsBalanced(): Boolean = entries.isBalanced()

    override fun iterator(): Iterator<PriorityEntry<T, P>> = entries.iterator()

}

public data class Interval<T : Comparable<T>>(
    public val low: T,
    public val high: T,
) {
    init {
        require(low <= high) { "Interval low endpoint must not exceed high endpoint." }
    }

    public fun overlaps(other: Interval<T>): Boolean =
        low <= other.high && other.low <= high

    public fun containsPoint(point: T): Boolean =
        low <= point && point <= high
}

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

public class IntervalTree<T : Comparable<T>> private constructor(
    private val intervals: PersistentMeasuredTree<Interval<T>, IntervalSummary<T>>,
) : Iterable<Interval<T>> {
    public companion object {
        public fun <T : Comparable<T>> empty(): IntervalTree<T> =
            IntervalTree(PersistentMeasuredTree.empty(IntervalMeasurePolicy()))

        public fun <T : Comparable<T>> from(values: Iterable<Interval<T>>): IntervalTree<T> {
            var result = empty<T>()
            for (interval in values) {
                result = result.insert(interval)
            }

            return result
        }
    }

    public val size: Int
        get() = intervals.size

    public val isEmpty: Boolean
        get() = intervals.isEmpty

    public fun insert(interval: Interval<T>): IntervalTree<T> {
        // Matches the C# reference: intervals are ordered by low endpoint
        // only, and a new interval goes before every existing equal-low one.
        val index = lowerBoundByLow(interval.low)
        return IntervalTree(intervals.insertAt(index, interval)!!)
    }

    public fun contains(interval: Interval<T>): Boolean = indexOf(interval) >= 0

    public fun remove(interval: Interval<T>): IntervalTree<T> = tryRemove(interval)?.tree ?: this

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

    public fun findOverlap(probe: Interval<T>): Interval<T>? = nextOverlap(probe, intervals)?.first

    public fun findContaining(point: T): Interval<T>? = findOverlap(Interval(point, point))

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

    public fun countOverlaps(probe: Interval<T>): Int =
        findOverlaps(probe).size

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

    public fun toList(): List<Interval<T>> = intervals.toList()

    public fun sharesStorageWith(other: IntervalTree<T>): Boolean = intervals.sharesStructureWith(other.intervals)

    internal fun debugIsBalanced(): Boolean = intervals.isBalanced()

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
