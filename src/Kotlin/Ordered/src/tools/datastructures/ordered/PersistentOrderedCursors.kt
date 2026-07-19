package tools.datastructures.ordered

/** Present neighbor returned by an ordered-collection cursor, including a stored null value. */
public data class OrderedCursorPeek<T>(public val value: T)

/** Presence-discriminated lookup of an equality class or grouped pair. */
public data class OrderedCursorSearch<C>(public val found: Boolean, public val cursor: C)

/** Result of a non-overwriting cursor insertion. */
public data class OrderedCursorInsert<C>(public val added: Boolean, public val cursor: C)

/** Immutable snapshot-plus-position gap cursor over a persistent ordered set. */
public class PersistentOrderedSetCursor<T> private constructor(
    private val value: PersistentOrderedSet<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(
            value: PersistentOrderedSet<T>,
            position: Int,
        ): PersistentOrderedSetCursor<T> = PersistentOrderedSetCursor(value, position)
    }

    /** Representative count in this cursor version. */
    public val size: Int get() = value.size

    /** Alias for [size]. */
    public val count: Int get() = size

    /** Whether the gap precedes every representative. */
    public val isAtStart: Boolean get() = position == 0

    /** Whether the gap follows every representative. */
    public val isAtEnd: Boolean get() = position == size

    /** Reads the representative immediately before the gap, when present. */
    public fun peekPrevious(): OrderedCursorPeek<T>? =
        if (isAtStart) null else OrderedCursorPeek(value.getAt(position - 1))

    /** Reads the representative immediately after the gap, when present. */
    public fun peekNext(): OrderedCursorPeek<T>? =
        if (isAtEnd) null else OrderedCursorPeek(value.getAt(position))

    /** Moves the gap one representative toward the start. */
    public fun movePrevious(): PersistentOrderedSetCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    /** Moves the gap one representative toward the end. */
    public fun moveNext(): PersistentOrderedSetCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /** Moves the gap to an absolute explicit-order position. */
    public fun seek(newPosition: Int): PersistentOrderedSetCursor<T>? = value.cursorAt(newPosition)

    /**
     * Inserts an absent representative at the gap and returns its following gap. An equivalent
     * stored representative preserves this exact cursor and position.
     */
    public fun insert(item: T): PersistentOrderedSetCursor<T> {
        val snapshot = value.insert(position, item)
        return if (snapshot === value) this else create(snapshot, Math.addExact(position, 1))
    }

    /** Attempts insertion at the gap without disturbing this cursor on equivalence. */
    public fun tryInsert(item: T): OrderedCursorInsert<PersistentOrderedSetCursor<T>> {
        val result = insert(item)
        return OrderedCursorInsert(result !== this, result)
    }

    /** Deletes the representative immediately before the gap. */
    public fun deletePrevious(): PersistentOrderedSetCursor<T>? =
        if (isAtStart) null else create(value.removeAt(position - 1), position - 1)

    /** Deletes the representative immediately after the gap. */
    public fun deleteNext(): PersistentOrderedSetCursor<T>? =
        if (isAtEnd) null else create(value.removeAt(position), position)

    /** Returns the exact persistent ordered-set version represented by this cursor. */
    public fun snapshot(): PersistentOrderedSet<T> = value
}

/** Creates an explicit-order gap cursor at [position] in `0..size`. */
public fun <T> PersistentOrderedSet<T>.cursorAt(
    position: Int = 0,
): PersistentOrderedSetCursor<T>? =
    if (position in 0..size) PersistentOrderedSetCursor.create(this, position) else null

/** Focuses an equality class; a miss returns the append-position cursor. */
public fun <T> PersistentOrderedSet<T>.findCursor(
    equalValue: T,
): OrderedCursorSearch<PersistentOrderedSetCursor<T>> {
    val index = indexOf(equalValue)
    return OrderedCursorSearch(index >= 0, checkNotNull(cursorAt(if (index < 0) size else index)))
}

/** Immutable snapshot-plus-position gap cursor over a persistent ordered map. */
public class PersistentOrderedMapCursor<K, V> private constructor(
    private val value: PersistentOrderedMap<K, V>,
    public val position: Int,
) {
    public companion object {
        internal fun <K, V> create(
            value: PersistentOrderedMap<K, V>,
            position: Int,
        ): PersistentOrderedMapCursor<K, V> = PersistentOrderedMapCursor(value, position)
    }

    /** Entry count in this cursor version. */
    public val size: Int get() = value.size

    /** Alias for [size]. */
    public val count: Int get() = size

    /** Whether the gap precedes every entry. */
    public val isAtStart: Boolean get() = position == 0

    /** Whether the gap follows every entry. */
    public val isAtEnd: Boolean get() = position == size

    /** Reads the entry immediately before the gap, when present. */
    public fun peekPrevious(): OrderedCursorPeek<OrderedMapEntry<K, V>>? =
        if (isAtStart) null else OrderedCursorPeek(value.getAt(position - 1))

    /** Reads the entry immediately after the gap, when present. */
    public fun peekNext(): OrderedCursorPeek<OrderedMapEntry<K, V>>? =
        if (isAtEnd) null else OrderedCursorPeek(value.getAt(position))

    /** Moves the gap one entry toward the start. */
    public fun movePrevious(): PersistentOrderedMapCursor<K, V>? =
        if (isAtStart) null else create(value, position - 1)

    /** Moves the gap one entry toward the end. */
    public fun moveNext(): PersistentOrderedMapCursor<K, V>? =
        if (isAtEnd) null else create(value, position + 1)

    /** Moves the gap to an absolute explicit-order position. */
    public fun seek(newPosition: Int): PersistentOrderedMapCursor<K, V>? = value.cursorAt(newPosition)

    /** Strictly inserts an absent entry at the gap and returns its following gap. */
    public fun insert(key: K, item: V): PersistentOrderedMapCursor<K, V> =
        create(value.insert(position, key, item), Math.addExact(position, 1))

    /** Attempts insertion; a duplicate returns a cursor immediately before its stored entry. */
    public fun tryInsert(key: K, item: V): OrderedCursorInsert<PersistentOrderedMapCursor<K, V>> {
        val existing = value.indexOfKey(key)
        if (existing >= 0) {
            return OrderedCursorInsert(false, create(value, existing))
        }
        return OrderedCursorInsert(true, insert(key, item))
    }

    /**
     * Updates the next entry's payload while retaining its key representative and this gap. A payload
     * the value policy already considers equivalent is a no-op that returns this exact cursor.
     */
    public fun setNextValue(item: V): PersistentOrderedMapCursor<K, V>? {
        val next = peekNext()?.value ?: return null
        val snapshot = value.set(next.key, item)
        return if (snapshot === value) this else create(snapshot, position)
    }

    /** Deletes the entry immediately before the gap. */
    public fun deletePrevious(): PersistentOrderedMapCursor<K, V>? =
        if (isAtStart) null else create(value.removeAt(position - 1), position - 1)

    /** Deletes the entry immediately after the gap. */
    public fun deleteNext(): PersistentOrderedMapCursor<K, V>? =
        if (isAtEnd) null else create(value.removeAt(position), position)

    /** Returns the exact persistent ordered-map version represented by this cursor. */
    public fun snapshot(): PersistentOrderedMap<K, V> = value
}

/** Creates an explicit-order gap cursor at [position] in `0..size`. */
public fun <K, V> PersistentOrderedMap<K, V>.cursorAt(
    position: Int = 0,
): PersistentOrderedMapCursor<K, V>? =
    if (position in 0..size) PersistentOrderedMapCursor.create(this, position) else null

/** Focuses an equivalent key; a miss returns the append-position cursor. */
public fun <K, V> PersistentOrderedMap<K, V>.findCursor(
    key: K,
): OrderedCursorSearch<PersistentOrderedMapCursor<K, V>> {
    val index = indexOfKey(key)
    return OrderedCursorSearch(index >= 0, checkNotNull(cursorAt(if (index < 0) size else index)))
}

/** Immutable snapshot-plus-pair-rank gap cursor over a grouped persistent ordered multimap. */
public class PersistentOrderedMultimapCursor<K, V> private constructor(
    private val value: PersistentOrderedMultimap<K, V>,
    public val position: Long,
) {
    public companion object {
        internal fun <K, V> create(
            value: PersistentOrderedMultimap<K, V>,
            position: Long,
        ): PersistentOrderedMultimapCursor<K, V> = PersistentOrderedMultimapCursor(value, position)
    }

    /** Flattened key-grouped pair count in this cursor version. */
    public val pairCount: Long get() = value.pairCount

    /** Whether the gap precedes every pair. */
    public val isAtStart: Boolean get() = position == 0L

    /** Whether the gap follows every pair. */
    public val isAtEnd: Boolean get() = position == pairCount

    /** Reads the key-grouped pair immediately before the gap, when present. */
    public fun peekPrevious(): OrderedCursorPeek<OrderedMultimapEntry<K, V>>? =
        if (isAtStart) null else OrderedCursorPeek(value.cursorEntryAt(position - 1L))

    /** Reads the key-grouped pair immediately after the gap, when present. */
    public fun peekNext(): OrderedCursorPeek<OrderedMultimapEntry<K, V>>? =
        if (isAtEnd) null else OrderedCursorPeek(value.cursorEntryAt(position))

    /** Moves the gap one key-grouped pair toward the start. */
    public fun movePrevious(): PersistentOrderedMultimapCursor<K, V>? =
        if (isAtStart) null else create(value, position - 1L)

    /** Moves the gap one key-grouped pair toward the end. */
    public fun moveNext(): PersistentOrderedMultimapCursor<K, V>? =
        if (isAtEnd) null else create(value, position + 1L)

    /** Moves the gap to an absolute flattened key-grouped pair rank. */
    public fun seek(newPosition: Long): PersistentOrderedMultimapCursor<K, V>? =
        value.cursorAt(newPosition)

    /**
     * Adds a pair using grouped collection semantics and returns its following pair gap. A present
     * equivalent pair preserves this exact cursor; insertion is not forced into another group gap.
     */
    public fun add(key: K, item: V): PersistentOrderedMultimapCursor<K, V> {
        val snapshot = value.add(key, item)
        if (snapshot === value) return this
        val index = snapshot.cursorIndexOf(key, item)
        check(index >= 0L) { "The inserted pair is missing from the ordered multimap." }
        return create(snapshot, Math.addExact(index, 1L))
    }

    /** Attempts grouped pair insertion without disturbing this cursor on equivalence. */
    public fun tryAdd(key: K, item: V): OrderedCursorInsert<PersistentOrderedMultimapCursor<K, V>> {
        val result = add(key, item)
        return OrderedCursorInsert(result !== this, result)
    }

    /** Deletes the pair immediately before the gap, contracting an empty group. */
    public fun deletePrevious(): PersistentOrderedMultimapCursor<K, V>? {
        val previous = peekPrevious()?.value ?: return null
        return create(value.remove(previous.key, previous.value), position - 1L)
    }

    /** Deletes the pair immediately after the gap, contracting an empty group. */
    public fun deleteNext(): PersistentOrderedMultimapCursor<K, V>? {
        val next = peekNext()?.value ?: return null
        return create(value.remove(next.key, next.value), position)
    }

    /** Returns the exact persistent ordered-multimap version represented by this cursor. */
    public fun snapshot(): PersistentOrderedMultimap<K, V> = value
}

/** Creates a key-grouped pair gap cursor at [position] in `0..pairCount`. */
public fun <K, V> PersistentOrderedMultimap<K, V>.cursorAt(
    position: Long = 0L,
): PersistentOrderedMultimapCursor<K, V>? =
    if (position in 0L..pairCount) PersistentOrderedMultimapCursor.create(this, position) else null

/** Focuses an equivalent grouped pair; a miss returns the append-position cursor. */
public fun <K, V> PersistentOrderedMultimap<K, V>.findCursor(
    key: K,
    value: V,
): OrderedCursorSearch<PersistentOrderedMultimapCursor<K, V>> {
    val index = cursorIndexOf(key, value)
    return OrderedCursorSearch(index >= 0L, checkNotNull(cursorAt(if (index < 0L) pairCount else index)))
}

/** Focuses the first pair in an equivalent key group; a miss returns the end cursor. */
public fun <K, V> PersistentOrderedMultimap<K, V>.findGroupCursor(
    key: K,
): OrderedCursorSearch<PersistentOrderedMultimapCursor<K, V>> {
    var index = 0L
    for (entry in this) {
        if (keyPolicy.equivalent(entry.key, key)) {
            return OrderedCursorSearch(true, PersistentOrderedMultimapCursor.create(this, index))
        }
        index = Math.addExact(index, 1L)
    }
    return OrderedCursorSearch(false, PersistentOrderedMultimapCursor.create(this, pairCount))
}

private fun <K, V> PersistentOrderedMultimap<K, V>.cursorEntryAt(
    rank: Long,
): OrderedMultimapEntry<K, V> {
    require(rank in 0L until pairCount) { "Pair rank must lie within the multimap." }
    var index = 0L
    for (entry in this) {
        if (index == rank) return entry
        index = Math.addExact(index, 1L)
    }
    error("PersistentOrderedMultimap pair count disagrees with its groups.")
}

private fun <K, V> PersistentOrderedMultimap<K, V>.cursorIndexOf(key: K, value: V): Long {
    var index = 0L
    for (entry in this) {
        if (keyPolicy.equivalent(entry.key, key) && valuePolicy.equivalent(entry.value, value)) {
            return index
        }
        index = Math.addExact(index, 1L)
    }
    return -1L
}
