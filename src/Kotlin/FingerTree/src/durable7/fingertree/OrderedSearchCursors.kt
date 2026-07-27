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

    /** Number of elements in the bag version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first element. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final element. */
    public val isAtEnd: Boolean get() = position == size

    /** The element before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(value.cursorItemAt(position - 1))

    /** The element after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(value.cursorItemAt(position))

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same bag version.
     */
    public fun movePrevious(): SortedBagCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): SortedBagCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given rank gap of the same bag version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): SortedBagCursor<T>? = value.cursorAt(rank)

    /**
     * Add [item] and return a cursor just after it. The gap moves to the item's sorted position
     * rather than staying where it was, because placement is decided by the ordering and not by
     * the cursor. A bag admits duplicates, so this always adds.
     */
    public fun add(item: T): SortedBagCursor<T> {
        val insertionRank = value.countAtMost(item)
        return create(value.add(item), Math.addExact(insertionRank, 1))
    }

    /**
     * Remove the element before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): SortedBagCursor<T>? =
        if (isAtStart) null else create(value.cursorRemoveAt(position - 1), position - 1)

    /**
     * Remove the element after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): SortedBagCursor<T>? =
        if (isAtEnd) null else create(value.cursorRemoveAt(position), position)

    /**
     * The bag version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
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

    /** Number of elements in the set version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first element. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final element. */
    public val isAtEnd: Boolean get() = position == size

    /** The element before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(value.cursorItemAt(position - 1))

    /** The element after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(value.cursorItemAt(position))

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same set version.
     */
    public fun movePrevious(): SortedSetCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): SortedSetCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given rank gap of the same set version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): SortedSetCursor<T>? = value.cursorAt(rank)

    /**
     * Add [item] and return a cursor just after it. The gap moves to the item's sorted position,
     * because placement is decided by the ordering and not by the cursor. Adding an element
     * already present keeps the stored representative and leaves the set unchanged.
     */
    public fun add(item: T): SortedSetCursor<T> {
        val location = value.cursorAtLowerBound(item)
        return create(value.add(item), Math.addExact(location.position, 1))
    }

    /**
     * Remove the element before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): SortedSetCursor<T>? {
        val item = peekPrevious() ?: return null
        return create(value.remove(item.value), position - 1)
    }

    /**
     * Remove the element after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): SortedSetCursor<T>? {
        val item = peekNext() ?: return null
        return create(value.remove(item.value), position)
    }

    /**
     * The set version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
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

    /** Number of entrys in the map version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first entry. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final entry. */
    public val isAtEnd: Boolean get() = position == size

    /** The entry before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): SequenceCursorPeek<SortedMapEntry<K, V>>? =
        if (isAtStart) null else SequenceCursorPeek(checkNotNull(value.entryAt(position - 1)))

    /** The entry after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): SequenceCursorPeek<SortedMapEntry<K, V>>? =
        if (isAtEnd) null else SequenceCursorPeek(checkNotNull(value.entryAt(position)))

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same map version.
     */
    public fun movePrevious(): SortedMapCursor<K, V>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): SortedMapCursor<K, V>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given key-order rank gap of the same map version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): SortedMapCursor<K, V>? = value.cursorAt(rank)

    /**
     * Insert [key] with [item] and return a cursor just after the entry. The gap moves to the
     * key's sorted position. An existing key is overwritten; use [tryInsert] to detect that
     * instead.
     */
    public fun insert(key: K, item: V): SortedMapCursor<K, V> {
        val location = value.cursorAtLowerBound(key)
        return create(value.insert(key, item), Math.addExact(location.position, 1))
    }

    /**
     * Insert [key] with [item] only when the key is absent, reporting whether it was added. On a
     * collision the returned cursor sits at the existing entry and the map is unchanged.
     */
    public fun tryInsert(key: K, item: V): OrderedCursorInsert<SortedMapCursor<K, V>> {
        val location = value.cursorAtLowerBound(key)
        val result = value.tryInsert(key, item)
        return OrderedCursorInsert(
            result.added,
            if (result.added) create(result.value, Math.addExact(location.position, 1)) else location,
        )
    }

    /** Bind [key] to [item], adding or replacing as needed, and return a cursor after the entry. */
    public fun setItem(key: K, item: V): SortedMapCursor<K, V> {
        val location = value.findCursor(key)
        return create(
            value.setItem(key, item),
            if (location.found) location.cursor.position else Math.addExact(location.cursor.position, 1),
        )
    }

    /**
     * Replace the value of the entry after the gap, keeping its key and the gap position, or
     * `null` at the end.
     */
    public fun setNextValue(item: V): SortedMapCursor<K, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.setItem(entry.key, item), position)
    }

    /**
     * Remove the entry before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): SortedMapCursor<K, V>? {
        val entry = peekPrevious()?.value ?: return null
        return create(value.remove(entry.key), position - 1)
    }

    /**
     * Remove the entry after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): SortedMapCursor<K, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.remove(entry.key), position)
    }

    /**
     * The map version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
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

    /** Number of elements in the set version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first element. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final element. */
    public val isAtEnd: Boolean get() = position == size

    /** The element before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(value.cursorItemAt(position - 1))

    /** The element after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(value.cursorItemAt(position))

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same set version.
     */
    public fun movePrevious(): CanonicalSortedSetCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): CanonicalSortedSetCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given rank gap of the same set version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): CanonicalSortedSetCursor<T>? = value.cursorAt(rank)

    /**
     * Add [item] and return a cursor just after it. The gap moves to the item's sorted position,
     * because placement is decided by the ordering and not by the cursor. The resulting topology
     * depends only on the contents and the policy, never on the order the elements arrived in.
     */
    public fun add(item: T): CanonicalSortedSetCursor<T> {
        val location = value.cursorAtLowerBound(item)
        return create(value.add(item), Math.addExact(location.position, 1))
    }

    /**
     * Remove the element before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): CanonicalSortedSetCursor<T>? {
        val item = peekPrevious() ?: return null
        return create(value.remove(item.value), position - 1)
    }

    /**
     * Remove the element after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): CanonicalSortedSetCursor<T>? {
        val item = peekNext() ?: return null
        return create(value.remove(item.value), position)
    }

    /**
     * The set version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
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

    /** Number of entrys in the queue version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first entry. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final entry. */
    public val isAtEnd: Boolean get() = position == size

    /** The entry before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): SequenceCursorPeek<PrioritySearchEntry<K, P, V>>? =
        if (isAtStart) null else SequenceCursorPeek(checkNotNull(value.cursorEntryAt(position - 1)))

    /** The entry after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): SequenceCursorPeek<PrioritySearchEntry<K, P, V>>? =
        if (isAtEnd) null else SequenceCursorPeek(checkNotNull(value.cursorEntryAt(position)))

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same queue version.
     */
    public fun movePrevious(): PrioritySearchQueueCursor<K, P, V>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): PrioritySearchQueueCursor<K, P, V>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given key-order rank gap of the same queue version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): PrioritySearchQueueCursor<K, P, V>? = value.cursorAt(rank)

    /**
     * Insert [key] with [priority] and [item] and return a cursor just after the entry. The gap
     * moves to the key's sorted position.
     *
     * @throws IllegalArgumentException if an equivalent key is already present; use [tryInsert]
     * to detect that without an exception.
     */
    public fun insert(key: K, priority: P, item: V): PrioritySearchQueueCursor<K, P, V> {
        val result = tryInsert(key, priority, item)
        if (!result.added) throw IllegalArgumentException("An equivalent key is already present.")
        return result.cursor
    }

    /**
     * Insert [key] with [priority] and [item] only when the key is absent, reporting whether it
     * was added. On a collision the returned cursor sits at the existing entry and the queue is
     * unchanged.
     */
    public fun tryInsert(key: K, priority: P, item: V): OrderedCursorInsert<PrioritySearchQueueCursor<K, P, V>> {
        val location = value.cursorAtLowerBound(key)
        val result = value.tryAdd(key, priority, item)
        return OrderedCursorInsert(
            result.added,
            if (result.added) create(result.queue, Math.addExact(location.position, 1)) else location,
        )
    }

    /**
     * Bind [key] to [priority] and [item], adding or replacing as needed, and return a cursor
     * after the entry.
     */
    public fun setItem(key: K, priority: P, item: V): PrioritySearchQueueCursor<K, P, V> {
        val location = value.findCursor(key)
        return create(
            value.setItem(key, priority, item),
            if (location.found) location.cursor.position else Math.addExact(location.cursor.position, 1),
        )
    }

    /**
     * Replace the priority and value of the entry after the gap, keeping its key and the gap
     * position, or `null` at the end.
     */
    public fun setNext(priority: P, item: V): PrioritySearchQueueCursor<K, P, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.setItem(entry.key, priority, item), position)
    }

    /**
     * Remove the entry before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): PrioritySearchQueueCursor<K, P, V>? {
        val entry = peekPrevious()?.value ?: return null
        return create(value.remove(entry.key), position - 1)
    }

    /**
     * Remove the entry after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): PrioritySearchQueueCursor<K, P, V>? {
        val entry = peekNext()?.value ?: return null
        return create(value.remove(entry.key), position)
    }

    /**
     * The queue version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
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

    /** Number of intervals in the tree version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first interval. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final interval. */
    public val isAtEnd: Boolean get() = position == size

    /** The interval before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): Interval<T>? =
        if (isAtStart) null else value.cursorIntervalAt(position - 1)

    /** The interval after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): Interval<T>? =
        if (isAtEnd) null else value.cursorIntervalAt(position)

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same tree version.
     */
    public fun movePrevious(): IntervalTreeCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): IntervalTreeCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given low-endpoint rank gap of the same tree version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): IntervalTreeCursor<T>? = value.cursorAt(rank)

    /**
     * The next interval strictly after the gap that overlaps [probe], with whether one was found.
     * A miss leaves the cursor at the end, so repeated calls enumerate every overlap exactly once
     * without rescanning from the start.
     */
    public fun seekNextOverlap(probe: Interval<T>): OrderedCursorSearch<IntervalTreeCursor<T>> =
        value.findOverlapCursorFrom(if (isAtEnd) size else position + 1, probe)

    /**
     * Insert [interval] and return a cursor just after it. The gap moves to the interval's
     * position in low-endpoint order.
     */
    public fun insert(interval: Interval<T>): IntervalTreeCursor<T> {
        val location = value.cursorAtLowerBound(interval.low)
        return create(value.insert(interval), Math.addExact(location.position, 1))
    }

    /**
     * Remove the interval before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): IntervalTreeCursor<T>? =
        if (isAtStart) null else create(value.cursorRemoveAt(position - 1), position - 1)

    /**
     * Remove the interval after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): IntervalTreeCursor<T>? =
        if (isAtEnd) null else create(value.cursorRemoveAt(position), position)

    /**
     * The tree version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
    public fun snapshot(): IntervalTree<T> = value
}

/**
 * A cursor at the given low-endpoint-rank gap of the tree, or `null` when the position lies
 * outside `0..size`.
 */
public fun <T : Comparable<T>> IntervalTree<T>.cursorAt(position: Int): IntervalTreeCursor<T>? =
    if (position in 0..size) IntervalTreeCursor.create(this, position) else null

/** A cursor before the first interval whose low endpoint is not less than [low]. */
public fun <T : Comparable<T>> IntervalTree<T>.cursorAtLowerBound(low: T): IntervalTreeCursor<T> =
    checkNotNull(cursorAt(cursorLowerBound(low)))

/** A cursor after the last interval whose low endpoint is not greater than [low]. */
public fun <T : Comparable<T>> IntervalTree<T>.cursorAtUpperBound(low: T): IntervalTreeCursor<T> =
    checkNotNull(cursorAt(cursorUpperBound(low)))

/**
 * A cursor at [interval] together with whether that exact interval is present; on a miss the
 * cursor sits at the insertion point. Matching is by both endpoints, so this is exact identity
 * rather than overlap - use [findOverlapCursor] for the latter.
 */
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

/**
 * A cursor at the first interval overlapping [probe], with whether one was found. Scans from
 * the start; use the cursor's own `seekNextOverlap` to continue from where this stopped.
 */
public fun <T : Comparable<T>> IntervalTree<T>.findOverlapCursor(
    probe: Interval<T>,
): OrderedCursorSearch<IntervalTreeCursor<T>> = findOverlapCursorFrom(0, probe)

/**
 * A cursor at the first interval containing [point], with whether one was found. Equivalent to
 * an overlap query against the degenerate interval `[point, point]`.
 */
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

    /** Number of entrys in the map version this cursor is positioned in. */
    public val size: Int get() = value.size
    /** Whether the gap precedes the first entry. */
    public val isAtStart: Boolean get() = position == 0
    /** Whether the gap follows the final entry. */
    public val isAtEnd: Boolean get() = position == size

    /** The entry before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): IntervalMapEntry<T, V>? =
        if (isAtStart) null else value.entryAt(position - 1)

    /** The entry after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): IntervalMapEntry<T, V>? =
        if (isAtEnd) null else value.entryAt(position)

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same map version.
     */
    public fun movePrevious(): PersistentIntervalMapCursor<T, V>? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): PersistentIntervalMapCursor<T, V>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given interval-key rank gap of the same map version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Int): PersistentIntervalMapCursor<T, V>? = value.cursorAt(rank)

    /**
     * The next entry strictly after the gap whose interval overlaps [probe], with whether one was
     * found. A miss leaves the cursor at the end, so repeated calls enumerate every overlap
     * exactly once without rescanning from the start.
     */
    public fun seekNextOverlap(probe: Interval<T>): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> =
        value.findOverlapCursorFrom(if (isAtEnd) size else position + 1, probe)

    /**
     * Insert [interval] with [item] and return a cursor just after the entry. The gap moves to
     * the interval's key position. An existing interval key is overwritten; use [tryInsert] to
     * detect that instead.
     */
    public fun insert(interval: Interval<T>, item: V): PersistentIntervalMapCursor<T, V> {
        val location = value.cursorAtLowerBound(interval)
        return create(value.add(interval, item), Math.addExact(location.position, 1))
    }

    /**
     * Insert [interval] with [item] only when that exact interval key is absent, reporting
     * whether it was added. On a collision the returned cursor sits at the existing entry and the
     * map is unchanged. Exact interval identity is distinct from overlap: an interval that
     * overlaps others still counts as absent here.
     */
    public fun tryInsert(interval: Interval<T>, item: V): OrderedCursorInsert<PersistentIntervalMapCursor<T, V>> {
        val location = value.cursorAtLowerBound(interval)
        val result = value.tryAdd(interval, item)
        return OrderedCursorInsert(
            result.added,
            if (result.added) create(result.map, Math.addExact(location.position, 1)) else location,
        )
    }

    /**
     * Replace the value of the entry after the gap, keeping its interval key and the gap
     * position, or `null` at the end.
     */
    public fun setNextValue(item: V): PersistentIntervalMapCursor<T, V>? {
        val entry = peekNext() ?: return null
        return create(value.set(entry.interval, item), position)
    }

    /**
     * Remove the entry before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): PersistentIntervalMapCursor<T, V>? {
        val entry = peekPrevious() ?: return null
        return create(value.remove(entry.interval), position - 1)
    }

    /**
     * Remove the entry after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): PersistentIntervalMapCursor<T, V>? {
        val entry = peekNext() ?: return null
        return create(value.remove(entry.interval), position)
    }

    /**
     * The map version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
    public fun snapshot(): PersistentIntervalMap<T, V> = value
}

/**
 * A cursor at the given interval-key-rank gap of the map, or `null` when the position lies
 * outside `0..size`.
 */
public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.cursorAt(
    position: Int,
): PersistentIntervalMapCursor<T, V>? =
    if (position in 0..size) PersistentIntervalMapCursor.create(this, position) else null

/** A cursor before the first entry whose interval key is not less than [interval]. */
public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.cursorAtLowerBound(
    interval: Interval<T>,
): PersistentIntervalMapCursor<T, V> = checkNotNull(cursorAt(cursorLowerBound(interval)))

/** A cursor after the last entry whose interval key is not greater than [interval]. */
public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.cursorAtUpperBound(
    interval: Interval<T>,
): PersistentIntervalMapCursor<T, V> = checkNotNull(cursorAt(cursorUpperBound(interval)))

/**
 * A cursor at [interval] together with whether that exact interval key is present; on a miss
 * the cursor sits at the insertion point. Matching is by both endpoints, so this is exact
 * identity rather than overlap - use [findOverlapCursor] for the latter.
 */
public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.findCursor(
    interval: Interval<T>,
): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> =
    OrderedCursorSearch(containsKey(interval), cursorAtLowerBound(interval))

/**
 * A cursor at the first entry whose interval overlaps [probe], with whether one was found.
 * Scans from the start; use the cursor's own `seekNextOverlap` to continue from where this
 * stopped.
 */
public fun <T : Comparable<T>, V> PersistentIntervalMap<T, V>.findOverlapCursor(
    probe: Interval<T>,
): OrderedCursorSearch<PersistentIntervalMapCursor<T, V>> = findOverlapCursorFrom(0, probe)

/**
 * A cursor at the first entry whose interval contains [point], with whether one was found.
 * Equivalent to an overlap query against the degenerate interval `[point, point]`.
 */
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

    /** Number of set bits in the bit set version this cursor is positioned in. */
    public val count: Long get() = value.count
    /** Whether the gap precedes the first set bit. */
    public val isAtStart: Boolean get() = position == 0L
    /** Whether the gap follows the final set bit. */
    public val isAtEnd: Boolean get() = position == count

    /** The set bit before the gap, or `null` at the start. Does not move the cursor. */
    public fun peekPrevious(): Int? = if (isAtStart) null else value.select(position - 1)
    /** The set bit after the gap, or `null` at the end. Does not move the cursor. */
    public fun peekNext(): Int? = if (isAtEnd) null else value.select(position)

    /**
     * A cursor one gap earlier, or `null` at the start. The receiver is unchanged;
     * movement produces a new cursor over the same bit set version.
     */
    public fun movePrevious(): PersistentChunkedBitSetCursor? =
        if (isAtStart) null else create(value, position - 1)

    /** A cursor one gap later, or `null` at the end. The receiver is unchanged. */
    public fun moveNext(): PersistentChunkedBitSetCursor? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * A cursor at the given population rank gap of the same bit set version, or `null` when the
     * rank lies outside the collection's bounds.
     */
    public fun seekRank(rank: Long): PersistentChunkedBitSetCursor? = value.cursorAt(rank)

    /**
     * Set [bitIndex] and return a cursor just after it in population order. Returns this cursor
     * by identity when the bit is already set.
     */
    public fun add(bitIndex: Int): PersistentChunkedBitSetCursor {
        if (value.contains(bitIndex)) return this
        val insertionRank = if (bitIndex == 0) 0L else value.rank(bitIndex - 1)
        return create(value.add(bitIndex), Math.addExact(insertionRank, 1))
    }

    /**
     * Remove the set bit before the gap and return a cursor in its place, or `null` at
     * the start.
     */
    public fun deletePrevious(): PersistentChunkedBitSetCursor? {
        val bit = peekPrevious() ?: return null
        return create(value.remove(bit), position - 1)
    }

    /**
     * Remove the set bit after the gap and return a cursor in its place, or `null` at
     * the end.
     */
    public fun deleteNext(): PersistentChunkedBitSetCursor? {
        val bit = peekNext() ?: return null
        return create(value.remove(bit), position)
    }

    /**
     * The bit set version this cursor is positioned in. Edits made through other cursors
     * are not visible here.
     */
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
