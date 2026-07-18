package tools.datastructures.fingertree

public data class IntervalMapEntry<T : Comparable<T>, V>(
    public val interval: Interval<T>,
    public val value: V,
)

public data class PersistentIntervalMapStatistics(public val count: Int)
public data class IntervalMapAddResult<T : Comparable<T>, V>(
    public val map: PersistentIntervalMap<T, V>,
    public val added: Boolean,
)

/** Unique payload-bearing closed intervals with augmented overlap navigation. */
public class PersistentIntervalMap<T : Comparable<T>, V> private constructor(
    private val intervals: IntervalTree<T>,
    private val values: SortedMap<Interval<T>, V>,
) : Iterable<IntervalMapEntry<T, V>> {
    public companion object {
        public fun <T : Comparable<T>, V> empty(): PersistentIntervalMap<T, V> =
            PersistentIntervalMap(IntervalTree.empty(), SortedMap.empty(intervalComparator()))

        public fun <T : Comparable<T>, V> from(
            entries: Iterable<Pair<Interval<T>, V>>,
        ): PersistentIntervalMap<T, V> {
            var result = empty<T, V>()
            for ((interval, value) in entries) result = result.set(interval, value)
            return result
        }

        private fun <T : Comparable<T>> intervalComparator(): Comparator<Interval<T>> =
            Comparator { left, right ->
                val low = left.low.compareTo(right.low)
                if (low != 0) low else left.high.compareTo(right.high)
            }
    }

    public val size: Int get() = values.size
    public val isEmpty: Boolean get() = values.isEmpty

    public fun containsKey(interval: Interval<T>): Boolean = values.containsKey(interval)
    public operator fun get(interval: Interval<T>): V? = values[interval]

    public fun entryAt(index: Int): IntervalMapEntry<T, V> {
        val entry = values.entryAt(index) ?: throw IndexOutOfBoundsException("Index must identify an interval.")
        return IntervalMapEntry(entry.key, entry.value)
    }

    public fun add(interval: Interval<T>, value: V): PersistentIntervalMap<T, V> {
        if (containsKey(interval)) throw IllegalArgumentException("An equivalent interval is already present.")
        return PersistentIntervalMap(intervals.insert(interval), values.insert(interval, value))
    }

    public fun tryAdd(interval: Interval<T>, value: V): IntervalMapAddResult<T, V> =
        if (containsKey(interval)) IntervalMapAddResult(this, false) else IntervalMapAddResult(add(interval, value), true)

    public fun set(interval: Interval<T>, value: V): PersistentIntervalMap<T, V> =
        if (containsKey(interval)) PersistentIntervalMap(intervals, values.setItem(interval, value))
        else PersistentIntervalMap(intervals.insert(interval), values.setItem(interval, value))

    public fun remove(interval: Interval<T>): PersistentIntervalMap<T, V> =
        if (!containsKey(interval)) this
        else PersistentIntervalMap(intervals.remove(interval), values.remove(interval))

    public fun clear(): PersistentIntervalMap<T, V> = if (isEmpty) this else empty()

    public fun findOverlap(probe: Interval<T>): IntervalMapEntry<T, V>? {
        val interval = intervals.findOverlap(probe) ?: return null
        val index = values.indexOfKey(interval) ?: throw indexDisagreement()
        val entry = requireNotNull(values.entryAt(index))
        return IntervalMapEntry(entry.key, entry.value)
    }

    public fun findOverlaps(probe: Interval<T>): List<IntervalMapEntry<T, V>> =
        intervals.findOverlaps(probe).map { interval ->
            val index = values.indexOfKey(interval) ?: throw indexDisagreement()
            val entry = requireNotNull(values.entryAt(index))
            IntervalMapEntry(entry.key, entry.value)
        }

    public fun countOverlaps(probe: Interval<T>): Int = intervals.countOverlaps(probe)

    public fun toList(): List<IntervalMapEntry<T, V>> = values.map { IntervalMapEntry(it.key, it.value) }

    public fun sharesIntervalStorageWith(other: PersistentIntervalMap<T, V>): Boolean =
        intervals.sharesStorageWith(other.intervals)

    public fun sharesValueStorageWith(other: PersistentIntervalMap<T, V>): Boolean =
        values.sharesStorageWith(other.values)

    public fun validateStructure(): PersistentIntervalMapStatistics {
        check(intervals.size == values.size) { "PersistentIntervalMap indexes have different counts." }
        for (interval in intervals) check(values.containsKey(interval)) { "An interval has no payload." }
        for (entry in values) check(intervals.contains(entry.key)) { "A payload has no interval." }
        return PersistentIntervalMapStatistics(size)
    }

    internal fun cursorLowerBound(interval: Interval<T>): Int = values.cursorLowerBound(interval)

    internal fun cursorUpperBound(interval: Interval<T>): Int = values.cursorUpperBound(interval)

    override fun iterator(): Iterator<IntervalMapEntry<T, V>> = toList().iterator()

    private fun indexDisagreement(): IllegalStateException =
        IllegalStateException("PersistentIntervalMap indexes disagree.")
}
