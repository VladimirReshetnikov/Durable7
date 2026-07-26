/*
 * Immutable ordered and query cursors shared by the sorted, canonical, interval, and priority
 * structures.
 *
 * Each cursor pairs a retained snapshot with a gap position, so moving and editing return new
 * cursors and never invalidate the ones already held. A seek reports whether it landed on a match,
 * and on a miss still leaves a usable cursor at the insertion point.
 */
package durable7.fingertree

/** Presence-discriminated ordered or query cursor search. */
public data class OrderedCursorSearch<C>(public val found: Boolean, public val cursor: C)

/** Result of a non-overwriting cursor insertion. */
public data class OrderedCursorInsert<C>(public val added: Boolean, public val cursor: C)

/** Immutable root-plus-population-rank gap cursor over a persistent sorted bag. */
public class SortedBagCursor<T> private constructor(
    private val value: SortedBag<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(value: SortedBag<T>, position: Int): SortedBagCursor<T> =
            SortedBagCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(value.cursorItemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(value.cursorItemAt(position))

    public fun movePrevious(): SortedBagCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): SortedBagCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): SortedBagCursor<T>? = value.cursorAt(rank)

    public fun add(item: T): SortedBagCursor<T> {
        val insertionRank = value.countAtMost(item)
        return create(value.add(item), Math.addExact(insertionRank, 1))
    }

    public fun deletePrevious(): SortedBagCursor<T>? =
        if (isAtStart) null else create(value.cursorRemoveAt(position - 1), position - 1)

    public fun deleteNext(): SortedBagCursor<T>? =
        if (isAtEnd) null else create(value.cursorRemoveAt(position), position)

    public fun snapshot(): SortedBag<T> = value
}

/** A cursor at the given gap of the bag, or `null` when the position is out of range. */
public fun <T> SortedBag<T>.cursorAt(position: Int): SortedBagCursor<T>? =
    if (position in 0..size) SortedBagCursor.create(this, position) else null

/** A cursor before the first element not less than the probe. */
public fun <T> SortedBag<T>.cursorAtLowerBound(item: T): SortedBagCursor<T> =
    checkNotNull(cursorAt(countLessThan(item)))

/** A cursor after the last element not greater than the probe. */
public fun <T> SortedBag<T>.cursorAtUpperBound(item: T): SortedBagCursor<T> =
    checkNotNull(cursorAt(countAtMost(item)))

/**
 * A cursor at the element together with whether it is actually present; on a miss the cursor sits at the insertion
 * point.
 */
public fun <T> SortedBag<T>.findCursor(item: T): OrderedCursorSearch<SortedBagCursor<T>> =
    OrderedCursorSearch(countOf(item) != 0, cursorAtLowerBound(item))

/** Immutable root-plus-rank gap cursor over a persistent sorted set. */
public class SortedSetCursor<T> private constructor(
    private val value: SortedSet<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(value: SortedSet<T>, position: Int): SortedSetCursor<T> =
            SortedSetCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(value.cursorItemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(value.cursorItemAt(position))

    public fun movePrevious(): SortedSetCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): SortedSetCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): SortedSetCursor<T>? = value.cursorAt(rank)

    public fun add(item: T): SortedSetCursor<T> {
        val location = value.cursorAtLowerBound(item)
        return create(value.add(item), Math.addExact(location.position, 1))
    }

    public fun deletePrevious(): SortedSetCursor<T>? {
        val item = peekPrevious() ?: return null
        return create(value.remove(item.value), position - 1)
    }

    public fun deleteNext(): SortedSetCursor<T>? {
        val item = peekNext() ?: return null
        return create(value.remove(item.value), position)
    }

    public fun snapshot(): SortedSet<T> = value
}

/** A cursor at the given gap of the set, or `null` when the position is out of range. */
public fun <T> SortedSet<T>.cursorAt(position: Int): SortedSetCursor<T>? =
    if (position in 0..size) SortedSetCursor.create(this, position) else null

/** A cursor before the first element not less than the probe. */
public fun <T> SortedSet<T>.cursorAtLowerBound(item: T): SortedSetCursor<T> =
    checkNotNull(cursorAt(cursorLowerBound(item)))

/** A cursor after the last element not greater than the probe. */
public fun <T> SortedSet<T>.cursorAtUpperBound(item: T): SortedSetCursor<T> =
    checkNotNull(cursorAt(cursorUpperBound(item)))

/**
 * A cursor at the element together with whether it is actually present; on a miss the cursor sits at the insertion
 * point.
 */
public fun <T> SortedSet<T>.findCursor(item: T): OrderedCursorSearch<SortedSetCursor<T>> =
    OrderedCursorSearch(contains(item), cursorAtLowerBound(item))

/** Immutable key-order root-plus-rank gap cursor over a persistent sorted map. */
public class SortedMapCursor<K, V> private constructor(
    private val value: SortedMap<K, V>,
    public val position: Int,
) {
    public companion object {
        internal fun <K, V> create(value: SortedMap<K, V>, position: Int): SortedMapCursor<K, V> =
            SortedMapCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<SortedMapEntry<K, V>>? =
        if (isAtStart) null else SequenceCursorPeek(checkNotNull(value.entryAt(position - 1)))

    public fun peekNext(): SequenceCursorPeek<SortedMapEntry<K, V>>? =
        if (isAtEnd) null else SequenceCursorPeek(checkNotNull(value.entryAt(position)))

    public fun movePrevious(): SortedMapCursor<K, V>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): SortedMapCursor<K, V>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): SortedMapCursor<K, V>? = value.cursorAt(rank)

    public fun insert(key: K, item: V): SortedMapCursor<K, V> {
        val location = value.cursorAtLowerBound(key)
        return create(value.insert(key, item), Math.addExact(location.position, 1))
    }

    public fun tryInsert(key: K, item: V): OrderedCursorInsert<SortedMapCursor<K, V>> {
        val location = value.cursorAtLowerBound(key)
        val result = value.tryInsert(key, item)
        return OrderedCursorInsert(
            result.added,
            if (result.added) create(result.value, Math.addExact(location.position, 1)) else location,
        )
    }

    public fun setItem(key: K, item: V): SortedMapCursor<K, V> {
        val location = value.findCursor(key)
        return create(
            value.setItem(key, item),
            if (location.found) location.cursor.position else Math.addExact(location.cursor.position, 1),
        )
    }

    public fun setNextValue(item: V): SortedMapCursor<K, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.setItem(entry.key, item), position)
    }

    public fun deletePrevious(): SortedMapCursor<K, V>? {
        val entry = peekPrevious()?.value ?: return null
        return create(value.remove(entry.key), position - 1)
    }

    public fun deleteNext(): SortedMapCursor<K, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.remove(entry.key), position)
    }

    public fun snapshot(): SortedMap<K, V> = value
}

/** A cursor at the given gap of the map, or `null` when the position is out of range. */
public fun <K, V> SortedMap<K, V>.cursorAt(position: Int): SortedMapCursor<K, V>? =
    if (position in 0..size) SortedMapCursor.create(this, position) else null

/** A cursor before the first key not less than the probe. */
public fun <K, V> SortedMap<K, V>.cursorAtLowerBound(key: K): SortedMapCursor<K, V> =
    checkNotNull(cursorAt(cursorLowerBound(key)))

/** A cursor after the last key not greater than the probe. */
public fun <K, V> SortedMap<K, V>.cursorAtUpperBound(key: K): SortedMapCursor<K, V> =
    checkNotNull(cursorAt(cursorUpperBound(key)))

/**
 * A cursor at the key together with whether it is actually present; on a miss the cursor sits at the insertion point.
 */
public fun <K, V> SortedMap<K, V>.findCursor(key: K): OrderedCursorSearch<SortedMapCursor<K, V>> =
    OrderedCursorSearch(containsKey(key), cursorAtLowerBound(key))

/** Immutable policy-preserving root-plus-rank gap cursor over a canonical sorted set. */
public class CanonicalSortedSetCursor<T> private constructor(
    private val value: CanonicalSortedSet<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(value: CanonicalSortedSet<T>, position: Int): CanonicalSortedSetCursor<T> =
            CanonicalSortedSetCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(value.cursorItemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(value.cursorItemAt(position))

    public fun movePrevious(): CanonicalSortedSetCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): CanonicalSortedSetCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): CanonicalSortedSetCursor<T>? = value.cursorAt(rank)

    public fun add(item: T): CanonicalSortedSetCursor<T> {
        val location = value.cursorAtLowerBound(item)
        return create(value.add(item), Math.addExact(location.position, 1))
    }

    public fun deletePrevious(): CanonicalSortedSetCursor<T>? {
        val item = peekPrevious() ?: return null
        return create(value.remove(item.value), position - 1)
    }

    public fun deleteNext(): CanonicalSortedSetCursor<T>? {
        val item = peekNext() ?: return null
        return create(value.remove(item.value), position)
    }

    public fun snapshot(): CanonicalSortedSet<T> = value
}

/** A cursor at the given gap of the set, or `null` when the position is out of range. */
public fun <T> CanonicalSortedSet<T>.cursorAt(position: Int): CanonicalSortedSetCursor<T>? =
    if (position in 0..size) CanonicalSortedSetCursor.create(this, position) else null

/** A cursor before the first element not less than the probe. */
public fun <T> CanonicalSortedSet<T>.cursorAtLowerBound(item: T): CanonicalSortedSetCursor<T> =
    checkNotNull(cursorAt(cursorBoundRank(item, upper = false)))

/** A cursor after the last element not greater than the probe. */
public fun <T> CanonicalSortedSet<T>.cursorAtUpperBound(item: T): CanonicalSortedSetCursor<T> =
    checkNotNull(cursorAt(cursorBoundRank(item, upper = true)))

/**
 * A cursor at the element together with whether it is actually present; on a miss the cursor sits at the insertion
 * point.
 */
public fun <T> CanonicalSortedSet<T>.findCursor(item: T): OrderedCursorSearch<CanonicalSortedSetCursor<T>> {
    val cursor = cursorAtLowerBound(item)
    val next = cursor.peekNext()
    val found = next != null && policy.comparator.compare(next.value, item) == 0
    return OrderedCursorSearch(found, cursor)
}

/** Immutable key-order root-plus-rank gap cursor over a priority-search queue. */
public class PrioritySearchQueueCursor<K, P, V> private constructor(
    private val value: PrioritySearchQueue<K, P, V>,
    public val position: Int,
) {
    public companion object {
        internal fun <K, P, V> create(
            value: PrioritySearchQueue<K, P, V>,
            position: Int,
        ): PrioritySearchQueueCursor<K, P, V> = PrioritySearchQueueCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<PrioritySearchEntry<K, P, V>>? =
        if (isAtStart) null else SequenceCursorPeek(checkNotNull(value.cursorEntryAt(position - 1)))

    public fun peekNext(): SequenceCursorPeek<PrioritySearchEntry<K, P, V>>? =
        if (isAtEnd) null else SequenceCursorPeek(checkNotNull(value.cursorEntryAt(position)))

    public fun movePrevious(): PrioritySearchQueueCursor<K, P, V>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): PrioritySearchQueueCursor<K, P, V>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): PrioritySearchQueueCursor<K, P, V>? = value.cursorAt(rank)

    public fun insert(key: K, priority: P, item: V): PrioritySearchQueueCursor<K, P, V> {
        val result = tryInsert(key, priority, item)
        if (!result.added) throw IllegalArgumentException("An equivalent key is already present.")
        return result.cursor
    }

    public fun tryInsert(key: K, priority: P, item: V): OrderedCursorInsert<PrioritySearchQueueCursor<K, P, V>> {
        val location = value.cursorAtLowerBound(key)
        val result = value.tryAdd(key, priority, item)
        return OrderedCursorInsert(
            result.added,
            if (result.added) create(result.queue, Math.addExact(location.position, 1)) else location,
        )
    }

    public fun setItem(key: K, priority: P, item: V): PrioritySearchQueueCursor<K, P, V> {
        val location = value.findCursor(key)
        return create(
            value.setItem(key, priority, item),
            if (location.found) location.cursor.position else Math.addExact(location.cursor.position, 1),
        )
    }

    public fun setNext(priority: P, item: V): PrioritySearchQueueCursor<K, P, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.setItem(entry.key, priority, item), position)
    }

    public fun deletePrevious(): PrioritySearchQueueCursor<K, P, V>? {
        val entry = peekPrevious()?.value ?: return null
        return create(value.remove(entry.key), position - 1)
    }

    public fun deleteNext(): PrioritySearchQueueCursor<K, P, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.remove(entry.key), position)
    }

    public fun snapshot(): PrioritySearchQueue<K, P, V> = value
}

/** A cursor at the given key-order gap of the queue, or `null` when the position is out of range. */
public fun <K, P, V> PrioritySearchQueue<K, P, V>.cursorAt(position: Int): PrioritySearchQueueCursor<K, P, V>? =
    if (position in 0..size) PrioritySearchQueueCursor.create(this, position) else null

/** A cursor before the first key not less than the probe. */
public fun <K, P, V> PrioritySearchQueue<K, P, V>.cursorAtLowerBound(key: K): PrioritySearchQueueCursor<K, P, V> =
    checkNotNull(cursorAt(cursorBoundRank(key, upper = false)))

/** A cursor after the last key not greater than the probe. */
public fun <K, P, V> PrioritySearchQueue<K, P, V>.cursorAtUpperBound(key: K): PrioritySearchQueueCursor<K, P, V> =
    checkNotNull(cursorAt(cursorBoundRank(key, upper = true)))

/**
 * A cursor at the key together with whether it is actually present; on a miss the cursor sits at the insertion point.
 */
public fun <K, P, V> PrioritySearchQueue<K, P, V>.findCursor(
    key: K,
): OrderedCursorSearch<PrioritySearchQueueCursor<K, P, V>> {
    val cursor = cursorAtLowerBound(key)
    val found = cursor.peekNext()?.value?.let { keyComparator.compare(it.key, key) == 0 } == true
    return OrderedCursorSearch(found, cursor)
}

/**
 * A cursor before the minimum-priority entry, found through the cached priority rather than by scanning. An empty queue
 * yields a cursor at the start.
 */
public fun <K, P, V> PrioritySearchQueue<K, P, V>.cursorAtMinimumPriority(): PrioritySearchQueueCursor<K, P, V> =
    minimumOrNull()?.let { cursorAtLowerBound(it.key) } ?: checkNotNull(cursorAt(0))

/** Immutable low-endpoint-order root-plus-rank gap cursor over an interval tree. */
public class IntervalTreeCursor<T : Comparable<T>> private constructor(
    private val value: IntervalTree<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T : Comparable<T>> create(value: IntervalTree<T>, position: Int): IntervalTreeCursor<T> =
            IntervalTreeCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): Interval<T>? =
        if (isAtStart) null else value.cursorIntervalAt(position - 1)

    public fun peekNext(): Interval<T>? =
        if (isAtEnd) null else value.cursorIntervalAt(position)

    public fun movePrevious(): IntervalTreeCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): IntervalTreeCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): IntervalTreeCursor<T>? = value.cursorAt(rank)

    public fun seekNextOverlap(probe: Interval<T>): OrderedCursorSearch<IntervalTreeCursor<T>> =
        value.findOverlapCursorFrom(if (isAtEnd) size else position + 1, probe)

    public fun insert(interval: Interval<T>): IntervalTreeCursor<T> {
        val location = value.cursorAtLowerBound(interval.low)
        return create(value.insert(interval), Math.addExact(location.position, 1))
    }

    public fun deletePrevious(): IntervalTreeCursor<T>? =
        if (isAtStart) null else create(value.cursorRemoveAt(position - 1), position - 1)

    public fun deleteNext(): IntervalTreeCursor<T>? =
        if (isAtEnd) null else create(value.cursorRemoveAt(position), position)

    public fun snapshot(): IntervalTree<T> = value
}

public fun <T : Comparable<T>> IntervalTree<T>.cursorAt(position: Int): IntervalTreeCursor<T>? =
    if (position in 0..size) IntervalTreeCursor.create(this, position) else null

public fun <T : Comparable<T>> IntervalTree<T>.cursorAtLowerBound(low: T): IntervalTreeCursor<T> =
    checkNotNull(cursorAt(cursorLowerBound(low)))

public fun <T : Comparable<T>> IntervalTree<T>.cursorAtUpperBound(low: T): IntervalTreeCursor<T> =
    checkNotNull(cursorAt(cursorUpperBound(low)))

public fun <T : Comparable<T>> IntervalTree<T>.findCursor(
    interval: Interval<T>,
): OrderedCursorSearch<IntervalTreeCursor<T>> {
    val start = cursorLowerBound(interval.low)
    var rank = start
    while (rank < size) {
        val stored = checkNotNull(cursorIntervalAt(rank))
        if (stored.low.compareTo(interval.low) != 0) break
        if (stored.high.compareTo(interval.high) == 0) {
            return OrderedCursorSearch(true, checkNotNull(cursorAt(rank)))
        }
        rank++
    }
    return OrderedCursorSearch(false, checkNotNull(cursorAt(start)))
}

public fun <T : Comparable<T>> IntervalTree<T>.findOverlapCursor(
    probe: Interval<T>,
): OrderedCursorSearch<IntervalTreeCursor<T>> = findOverlapCursorFrom(0, probe)

public fun <T : Comparable<T>> IntervalTree<T>.findContainingCursor(
    point: T,
): OrderedCursorSearch<IntervalTreeCursor<T>> = findOverlapCursor(Interval(point, point))

internal fun <T : Comparable<T>> IntervalTree<T>.findOverlapCursorFrom(
    start: Int,
    probe: Interval<T>,
): OrderedCursorSearch<IntervalTreeCursor<T>> {
    require(start in 0..size) { "Cursor start is outside the interval tree." }
    for (rank in start until size) {
        val stored = checkNotNull(cursorIntervalAt(rank))
        if (stored.low > probe.high) break
        if (stored.overlaps(probe)) return OrderedCursorSearch(true, checkNotNull(cursorAt(rank)))
    }
    return OrderedCursorSearch(false, checkNotNull(cursorAt(size)))
}

/** Immutable interval-key-order root-plus-rank gap cursor over a persistent interval map. */
public class PersistentIntervalMapCursor<T : Comparable<T>, V> private constructor(
    private val value: PersistentIntervalMap<T, V>,
    public val position: Int,
) {
    public companion object {
        internal fun <T : Comparable<T>, V> create(
            value: PersistentIntervalMap<T, V>,
            position: Int,
        ): PersistentIntervalMapCursor<T, V> = PersistentIntervalMapCursor(value, position)
    }

    public val size: Int get() = value.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size

    public fun peekPrevious(): IntervalMapEntry<T, V>? =
        if (isAtStart) null else value.entryAt(position - 1)

    public fun peekNext(): IntervalMapEntry<T, V>? =
        if (isAtEnd) null else value.entryAt(position)

    public fun movePrevious(): PersistentIntervalMapCursor<T, V>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): PersistentIntervalMapCursor<T, V>? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Int): PersistentIntervalMapCursor<T, V>? = value.cursorAt(rank)

    public fun seekNextOverlap(probe: Interval<T>): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> =
        value.findOverlapCursorFrom(if (isAtEnd) size else position + 1, probe)

    public fun insert(interval: Interval<T>, item: V): PersistentIntervalMapCursor<T, V> {
        val location = value.cursorAtLowerBound(interval)
        return create(value.add(interval, item), Math.addExact(location.position, 1))
    }

    public fun tryInsert(interval: Interval<T>, item: V): OrderedCursorInsert<PersistentIntervalMapCursor<T, V>> {
        val location = value.cursorAtLowerBound(interval)
        val result = value.tryAdd(interval, item)
        return OrderedCursorInsert(
            result.added,
            if (result.added) create(result.map, Math.addExact(location.position, 1)) else location,
        )
    }

    public fun setNextValue(item: V): PersistentIntervalMapCursor<T, V>? {
        val entry = peekNext() ?: return null
        return create(value.set(entry.interval, item), position)
    }

    public fun deletePrevious(): PersistentIntervalMapCursor<T, V>? {
        val entry = peekPrevious() ?: return null
        return create(value.remove(entry.interval), position - 1)
    }

    public fun deleteNext(): PersistentIntervalMapCursor<T, V>? {
        val entry = peekNext() ?: return null
        return create(value.remove(entry.interval), position)
    }

    public fun snapshot(): PersistentIntervalMap<T, V> = value
}

public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.cursorAt(
    position: Int,
): PersistentIntervalMapCursor<T, V>? =
    if (position in 0..size) PersistentIntervalMapCursor.create(this, position) else null

public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.cursorAtLowerBound(
    interval: Interval<T>,
): PersistentIntervalMapCursor<T, V> = checkNotNull(cursorAt(cursorLowerBound(interval)))

public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.cursorAtUpperBound(
    interval: Interval<T>,
): PersistentIntervalMapCursor<T, V> = checkNotNull(cursorAt(cursorUpperBound(interval)))

public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.findCursor(
    interval: Interval<T>,
): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> =
    OrderedCursorSearch(containsKey(interval), cursorAtLowerBound(interval))

public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.findOverlapCursor(
    probe: Interval<T>,
): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> = findOverlapCursorFrom(0, probe)

public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.findContainingCursor(
    point: T,
): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> = findOverlapCursor(Interval(point, point))

internal fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.findOverlapCursorFrom(
    start: Int,
    probe: Interval<T>,
): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> {
    require(start in 0..size) { "Cursor start is outside the interval map." }
    for (rank in start until size) {
        val entry = entryAt(rank)
        if (entry.interval.low > probe.high) break
        if (entry.interval.overlaps(probe)) return OrderedCursorSearch(true, checkNotNull(cursorAt(rank)))
    }
    return OrderedCursorSearch(false, checkNotNull(cursorAt(size)))
}

/** Immutable root-plus-population-rank gap cursor over present set bits. */
public class PersistentChunkedBitSetCursor private constructor(
    private val value: PersistentChunkedBitSet,
    public val position: Long,
) {
    public companion object {
        internal fun create(value: PersistentChunkedBitSet, position: Long): PersistentChunkedBitSetCursor =
            PersistentChunkedBitSetCursor(value, position)
    }

    public val count: Long get() = value.count
    public val isAtStart: Boolean get() = position == 0L
    public val isAtEnd: Boolean get() = position == count

    public fun peekPrevious(): Int? = if (isAtStart) null else value.select(position - 1)
    public fun peekNext(): Int? = if (isAtEnd) null else value.select(position)

    public fun movePrevious(): PersistentChunkedBitSetCursor? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): PersistentChunkedBitSetCursor? =
        if (isAtEnd) null else create(value, position + 1)

    public fun seekRank(rank: Long): PersistentChunkedBitSetCursor? = value.cursorAt(rank)

    public fun add(bitIndex: Int): PersistentChunkedBitSetCursor {
        if (value.contains(bitIndex)) return this
        val insertionRank = if (bitIndex == 0) 0L else value.rank(bitIndex - 1)
        return create(value.add(bitIndex), Math.addExact(insertionRank, 1))
    }

    public fun deletePrevious(): PersistentChunkedBitSetCursor? {
        val bit = peekPrevious() ?: return null
        return create(value.remove(bit), position - 1)
    }

    public fun deleteNext(): PersistentChunkedBitSetCursor? {
        val bit = peekNext() ?: return null
        return create(value.remove(bit), position)
    }

    public fun snapshot(): PersistentChunkedBitSet = value
}

/** A cursor at the given gap in set-bit order, or `null` when the position is out of range. */
public fun PersistentChunkedBitSet.cursorAt(position: Long): PersistentChunkedBitSetCursor? =
    if (position in 0..count) PersistentChunkedBitSetCursor.create(this, position) else null

/** A cursor before the first set bit at or after the given index, located by rank rather than by scanning the gap. */
public fun PersistentChunkedBitSet.cursorAtOrAfter(bitIndex: Int): PersistentChunkedBitSetCursor {
    val position = if (bitIndex <= 0) 0L else rank(bitIndex - 1)
    return checkNotNull(cursorAt(position))
}

/**
 * A cursor at the bit index together with whether that bit is actually set; on a miss the cursor sits before the next
 * set bit.
 */
public fun PersistentChunkedBitSet.findCursor(bitIndex: Int): OrderedCursorSearch<PersistentChunkedBitSetCursor> {
    val cursor = cursorAtOrAfter(bitIndex)
    return OrderedCursorSearch(bitIndex >= 0 && cursor.peekNext() == bitIndex, cursor)
}
