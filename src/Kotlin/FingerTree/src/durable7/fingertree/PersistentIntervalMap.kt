/*
 * Persistent map keyed by complete intervals.
 *
 * Attaches one payload to each whole interval key, so two entries collide only when their endpoints
 * are equal and a partial overlap is a distinct key. The cached maximum high endpoint lets stabbing
 * and overlap queries skip subtrees that cannot contain a match.
 */
package durable7.fingertree

/** One interval key and its payload. */
public data class IntervalMapEntry<T : Comparable<T>, V>(
    public val interval: Interval<T>,
    public val value: V,
)

/** Entry measurements returned by a successful structural audit. */
public data class PersistentIntervalMapStatistics(public val count: Int)

/** The outcome of a strict insertion; the map is the receiver when nothing was added. */
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
        /** An empty map. */
        public fun <T : Comparable<T>, V> empty(): PersistentIntervalMap<T, V> =
            PersistentIntervalMap(IntervalTree.empty(), SortedMap.empty(intervalComparator()))

        /** A map holding every entry. Later entries overwrite earlier ones with an equal interval key. */
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

    /** Number of entries. O(1). */
    public val size: Int get() = values.size
    /** Whether the map holds no entries. */
    public val isEmpty: Boolean get() = values.isEmpty

    /**
     * Whether an entry keyed by exactly [interval] - matching on both endpoints - is present. This is key identity,
     * not overlap.
     */
    public fun containsKey(interval: Interval<T>): Boolean = values.containsKey(interval)
    /**
     * The value stored for exactly [interval], or `null` when absent. This is a key lookup, not an overlap query;
     * use [findOverlaps] for the latter.
     */
    public operator fun get(interval: Interval<T>): V? = values[interval]

    /**
     * The entry at [index] in interval-key order.
     *
     * @throws IndexOutOfBoundsException if [index] does not identify a stored interval.
     */
    public fun entryAt(index: Int): IntervalMapEntry<T, V> {
        val entry = values.entryAt(index) ?: throw IndexOutOfBoundsException("Index must identify an interval.")
        return IntervalMapEntry(entry.key, entry.value)
    }

    /**
     * A map with the entry added.
     *
     * @throws IllegalArgumentException if an equal interval key is already present; use [tryAdd] to detect that
     * without an exception, or [set] to overwrite.
     */
    public fun add(interval: Interval<T>, value: V): PersistentIntervalMap<T, V> {
        if (containsKey(interval)) throw IllegalArgumentException("An equivalent interval is already present.")
        return PersistentIntervalMap(intervals.insert(interval), values.insert(interval, value))
    }

    /**
     * Add the entry only when its interval key is absent, reporting whether it was added. On a collision the result
     * carries the receiver unchanged.
     */
    public fun tryAdd(interval: Interval<T>, value: V): IntervalMapAddResult<T, V> =
        if (containsKey(interval)) IntervalMapAddResult(this, false) else IntervalMapAddResult(add(interval, value), true)

    /** A map binding [interval] to [value], adding or replacing as needed. */
    public fun set(interval: Interval<T>, value: V): PersistentIntervalMap<T, V> =
        if (containsKey(interval)) PersistentIntervalMap(intervals, values.setItem(interval, value))
        else PersistentIntervalMap(intervals.insert(interval), values.setItem(interval, value))

    /** A map without the entry keyed by exactly [interval]. Returns the receiver unchanged when absent. */
    public fun remove(interval: Interval<T>): PersistentIntervalMap<T, V> =
        if (!containsKey(interval)) this
        else PersistentIntervalMap(intervals.remove(interval), values.remove(interval))

    /** An empty map. Returns the receiver when it is already empty. */
    public fun clear(): PersistentIntervalMap<T, V> = if (isEmpty) this else empty()

    /**
     * Returns the first entry overlapping [probe] in this map's declared `(low, high)` order, or
     * `null` when nothing overlaps.
     *
     * The augmented tree orders by low endpoint only, so its own first overlap is not necessarily the
     * declared-order first one. The tree still supplies the smallest overlapping low endpoint through
     * a pruned descent; the equal-low run in the payload index, which is sorted by high endpoint,
     * then selects the declared-order minimum. This makes the result agree with `findOverlapCursor`
     * by construction. The cost is O(log n) plus the length of that equal-low run.
     */
    public fun findOverlap(probe: Interval<T>): IntervalMapEntry<T, V>? {
        val candidate = intervals.findOverlap(probe) ?: return null
        var rank = values.cursorLowerBound(Interval(candidate.low, candidate.low))
        while (rank < size) {
            val entry = entryAt(rank)
            if (entry.interval.low.compareTo(candidate.low) != 0) break
            if (entry.interval.overlaps(probe)) return entry
            rank += 1
        }

        throw indexDisagreement()
    }

    /** Returns every entry overlapping [probe] in this map's declared `(low, high)` order. */
    public fun findOverlaps(probe: Interval<T>): List<IntervalMapEntry<T, V>> =
        intervals.findOverlaps(probe)
            .map { interval -> values.indexOfKey(interval) ?: throw indexDisagreement() }
            .sorted()
            .map { rank -> entryAt(rank) }

    /** How many stored intervals overlap [probe]. */
    public fun countOverlaps(probe: Interval<T>): Int = intervals.countOverlaps(probe)

    /** The entries in interval-key order. */
    public fun toList(): List<IntervalMapEntry<T, V>> = values.map { IntervalMapEntry(it.key, it.value) }

    /**
     * Whether this map and [other] share their interval index. The two indexes are tracked separately because an
     * edit that changes only a value leaves the interval index shared.
     */
    public fun sharesIntervalStorageWith(other: PersistentIntervalMap<T, V>): Boolean =
        intervals.sharesStorageWith(other.intervals)

    /** Whether this map and [other] share their value index. */
    public fun sharesValueStorageWith(other: PersistentIntervalMap<T, V>): Boolean =
        values.sharesStorageWith(other.values)

    /**
     * Checks that both indexes describe the same entries as an ordered sequence under the declared
     * `(low, high)` order. A set comparison alone cannot see an ordering disagreement between the
     * low-only augmented tree and the lexicographic payload index, which is exactly what would make
     * the overlap and cursor surfaces report different first overlaps.
     */
    public fun validateStructure(): PersistentIntervalMapStatistics {
        check(intervals.size == values.size) { "PersistentIntervalMap indexes have different counts." }
        for (interval in intervals) check(values.containsKey(interval)) { "An interval has no payload." }
        for (entry in values) check(intervals.contains(entry.key)) { "A payload has no interval." }

        val declared = intervals.toList().sortedWith(intervalComparator<T>())
        val payloads = values.keys()
        for (rank in declared.indices) {
            check(declared[rank] == payloads[rank]) { "PersistentIntervalMap indexes disagree in declared order." }
        }

        return PersistentIntervalMapStatistics(size)
    }

    internal fun cursorLowerBound(interval: Interval<T>): Int = values.cursorLowerBound(interval)

    internal fun cursorUpperBound(interval: Interval<T>): Int = values.cursorUpperBound(interval)

    override fun iterator(): Iterator<IntervalMapEntry<T, V>> = toList().iterator()

    private fun indexDisagreement(): IllegalStateException =
        IllegalStateException("PersistentIntervalMap indexes disagree.")
}
