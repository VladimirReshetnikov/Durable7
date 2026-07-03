package tools.datastructures.fingertree

public data class PriorityEntry<T, P>(
    public val value: T,
    public val priority: P,
)

public data class PriorityDequeue<T, P>(
    public val entry: PriorityEntry<T, P>,
    public val queue: PriorityQueue<T, P>,
)

public class PriorityQueue<T, P> private constructor(
    private val entries: List<PriorityEntry<T, P>>,
    private val comparator: Comparator<in P>,
) : Iterable<PriorityEntry<T, P>> {
    public companion object {
        public fun <T, P : Comparable<P>> empty(): PriorityQueue<T, P> =
            PriorityQueue(emptyList(), naturalComparator())

        public fun <T, P> empty(comparator: Comparator<in P>): PriorityQueue<T, P> =
            PriorityQueue(emptyList(), comparator)
    }

    public val size: Int
        get() = entries.size

    public val isEmpty: Boolean
        get() = entries.isEmpty()

    public fun enqueue(value: T, priority: P): PriorityQueue<T, P> =
        PriorityQueue(entries + PriorityEntry(value, priority), comparator)

    public fun meld(other: PriorityQueue<T, P>): PriorityQueue<T, P> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> PriorityQueue(entries + other.entries, comparator)
        }

    public fun peekEntry(): PriorityEntry<T, P>? = minIndex()?.let { entries[it] }

    public fun peek(): Pair<T, P>? = peekEntry()?.let { it.value to it.priority }

    public fun peekPriority(): P? = peekEntry()?.priority

    public fun dequeue(): PriorityDequeue<T, P>? {
        val index = minIndex() ?: return null
        val entry = entries[index]
        return PriorityDequeue(entry, PriorityQueue(entries.take(index) + entries.drop(index + 1), comparator))
    }

    public fun toList(): List<PriorityEntry<T, P>> = entries.toList()

    public fun sharesStorageWith(other: PriorityQueue<T, P>): Boolean = entries === other.entries

    override fun iterator(): Iterator<PriorityEntry<T, P>> = entries.iterator()

    private fun minIndex(): Int? {
        if (entries.isEmpty()) {
            return null
        }

        var best = 0
        for (index in 1 until entries.size) {
            if (comparator.compare(entries[index].priority, entries[best].priority) < 0) {
                best = index
            }
        }

        return best
    }
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

public class IntervalTree<T : Comparable<T>> private constructor(
    private val intervals: List<Interval<T>>,
) : Iterable<Interval<T>> {
    public companion object {
        public fun <T : Comparable<T>> empty(): IntervalTree<T> = IntervalTree(emptyList())

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
        get() = intervals.isEmpty()

    public fun insert(interval: Interval<T>): IntervalTree<T> {
        val next = intervals + interval
        return IntervalTree(next.sortedWith(compareBy<Interval<T>> { it.low }.thenBy { it.high }))
    }

    public fun contains(interval: Interval<T>): Boolean = intervals.contains(interval)

    public fun remove(interval: Interval<T>): IntervalTree<T> = tryRemove(interval)?.tree ?: this

    public fun tryRemove(interval: Interval<T>): IntervalRemoveResult<T>? {
        val index = intervals.indexOf(interval)
        if (index < 0) {
            return null
        }

        return IntervalRemoveResult(IntervalTree(intervals.take(index) + intervals.drop(index + 1)), intervals[index])
    }

    public fun findOverlap(probe: Interval<T>): Interval<T>? =
        intervals.firstOrNull { it.overlaps(probe) }

    public fun findContaining(point: T): Interval<T>? =
        intervals.firstOrNull { it.containsPoint(point) }

    public fun findOverlaps(probe: Interval<T>): List<Interval<T>> =
        intervals.filter { it.overlaps(probe) }

    public fun countOverlaps(probe: Interval<T>): Int =
        findOverlaps(probe).size

    public fun coalesce(): IntervalTree<T> {
        if (intervals.size < 2) {
            return this
        }

        val result = ArrayList<Interval<T>>()
        var current = intervals.first()
        for (next in intervals.drop(1)) {
            if (next.low <= current.high) {
                val high = if (current.high >= next.high) current.high else next.high
                current = Interval(current.low, high)
            } else {
                result.add(current)
                current = next
            }
        }

        result.add(current)
        return IntervalTree(result)
    }

    public fun toList(): List<Interval<T>> = intervals.toList()

    public fun sharesStorageWith(other: IntervalTree<T>): Boolean = intervals === other.intervals

    override fun iterator(): Iterator<Interval<T>> = intervals.iterator()
}
