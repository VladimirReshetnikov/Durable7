package tools.datastructures.fingertree

/** Present neighbor returned by a sequence cursor, including a stored null value. */
public data class SequenceCursorPeek<T>(public val value: T)

/** Result of an inclusive-prefix measure search for a general finger-tree cursor. */
public data class FingerTreeCursorSearch<T, M>(
    public val cursor: FingerTreeCursor<T, M>,
    public val found: Boolean,
)

/** Immutable snapshot-plus-position gap cursor over a persistent deque. */
public class PersistentDequeCursor<T> private constructor(
    private val value: PersistentDeque<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(value: PersistentDeque<T>, position: Int): PersistentDequeCursor<T> =
            PersistentDequeCursor(value, position)
    }

    public val size: Int
        get() = value.size

    public val isAtStart: Boolean
        get() = position == 0

    public val isAtEnd: Boolean
        get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(itemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(itemAt(position))

    public fun movePrevious(): PersistentDequeCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): PersistentDequeCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /** Moves the gap to [newPosition]; seeking to the current position returns this cursor by identity. */
    public fun seek(newPosition: Int): PersistentDequeCursor<T>? =
        if (newPosition == position) this else value.cursorAt(newPosition)

    public fun insert(item: T): PersistentDequeCursor<T> =
        create(value.insertAt(position, item)!!, Math.addExact(position, 1))

    public fun insertRange(items: Iterable<T>): PersistentDequeCursor<T> {
        val captured = items.toList()
        if (captured.isEmpty()) {
            return this
        }
        val split = value.splitAt(position)!!
        val middle = PersistentDeque.from(captured)
        return create(
            split.left.concat(middle).concat(split.right),
            Math.addExact(position, captured.size),
        )
    }

    public fun deletePrevious(): PersistentDequeCursor<T>? =
        if (isAtStart) null else create(value.removeAt(position - 1)!!, position - 1)

    public fun deleteNext(): PersistentDequeCursor<T>? =
        if (isAtEnd) null else create(value.removeAt(position)!!, position)

    public fun replaceNext(item: T): PersistentDequeCursor<T>? =
        if (isAtEnd) null else create(value.setItem(position, item)!!, position)

    public fun snapshot(): PersistentDeque<T> = value

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = value[index] as T
}

/** Immutable logical-order gap cursor over a reversible deque. */
public class ReversibleDequeCursor<T> private constructor(
    private val value: ReversibleDeque<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(value: ReversibleDeque<T>, position: Int): ReversibleDequeCursor<T> =
            ReversibleDequeCursor(value, position)
    }

    public val size: Int
        get() = value.size

    public val isAtStart: Boolean
        get() = position == 0

    public val isAtEnd: Boolean
        get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(itemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(itemAt(position))

    public fun movePrevious(): ReversibleDequeCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): ReversibleDequeCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /** Moves the gap to [newPosition]; seeking to the current position returns this cursor by identity. */
    public fun seek(newPosition: Int): ReversibleDequeCursor<T>? =
        if (newPosition == position) this else value.cursorAt(newPosition)

    public fun insert(item: T): ReversibleDequeCursor<T> {
        val split = value.splitAt(position)!!
        return create(split.first.append(item).concat(split.second), Math.addExact(position, 1))
    }

    public fun insertRange(items: Iterable<T>): ReversibleDequeCursor<T> {
        val captured = items.toList()
        if (captured.isEmpty()) {
            return this
        }
        val split = value.splitAt(position)!!
        return create(
            split.first.concat(ReversibleDeque.from(captured)).concat(split.second),
            Math.addExact(position, captured.size),
        )
    }

    public fun deletePrevious(): ReversibleDequeCursor<T>? {
        if (isAtStart) {
            return null
        }
        val first = value.splitAt(position - 1)!!
        val second = first.second.splitAt(1)!!
        return create(first.first.concat(second.second), position - 1)
    }

    public fun deleteNext(): ReversibleDequeCursor<T>? {
        if (isAtEnd) {
            return null
        }
        val first = value.splitAt(position)!!
        val second = first.second.splitAt(1)!!
        return create(first.first.concat(second.second), position)
    }

    public fun replaceNext(item: T): ReversibleDequeCursor<T>? =
        deleteNext()?.insert(item)?.movePrevious()

    public fun reverse(): ReversibleDequeCursor<T> = create(value.reverse(), size - position)

    public fun snapshot(): ReversibleDeque<T> = value

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = value[index] as T
}

/** Immutable measure-aware cursor over one exact general finger-tree snapshot. */
public class FingerTreeCursor<T, M> private constructor(
    private val value: FingerTree<T, M>,
    private val position: Int,
) {
    public companion object {
        internal fun <T, M> create(value: FingerTree<T, M>, position: Int): FingerTreeCursor<T, M> =
            FingerTreeCursor(value, position)
    }

    public val isAtStart: Boolean
        get() = position == 0

    public val isAtEnd: Boolean
        get() = position == value.size

    /**
     * The ordered measure of `[0, position)`. The boundary is already validated by construction, so
     * the read never conflates an out-of-range count with the monoid identity: a policy whose identity
     * is itself `null`, such as [MaxMeasure] or [MinMeasure], reports that identity at the start gap.
     * This read descends one root-to-leaf path and allocates nothing.
     */
    public val measureBefore: M
        get() = value.measurePrefix(position)

    /**
     * The ordered measure of `[position, size)`, so that `combine(measureBefore, measureAfter)` is the
     * whole snapshot measure. Like [measureBefore] this is a read-only descent rather than a
     * structural split, so it allocates no discarded nodes.
     */
    public val measureAfter: M
        get() = value.measureSuffix(position)

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(itemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(itemAt(position))

    public fun movePrevious(): FingerTreeCursor<T, M>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): FingerTreeCursor<T, M>? =
        if (isAtEnd) null else create(value, position + 1)

    /**
     * Performs an absolute inclusive-prefix measure seek over the retained snapshot; a miss selects the
     * end gap. If the selected gap is already current, the result retains this cursor by identity.
     */
    public fun seekByMeasure(predicate: (M) -> Boolean): FingerTreeCursorSearch<T, M> {
        val located = value.cursorByMeasure(predicate)
        return if (located.cursor.position == position) FingerTreeCursorSearch(this, located.found) else located
    }

    public fun insert(item: T): FingerTreeCursor<T, M> =
        create(value.insertAt(position, item)!!, Math.addExact(position, 1))

    public fun deletePrevious(): FingerTreeCursor<T, M>? =
        if (isAtStart) null else create(value.removeAt(position - 1)!!, position - 1)

    public fun deleteNext(): FingerTreeCursor<T, M>? =
        if (isAtEnd) null else create(value.removeAt(position)!!, position)

    public fun replaceNext(item: T): FingerTreeCursor<T, M>? =
        if (isAtEnd) null else create(value.setItem(position, item)!!, position)

    public fun snapshot(): FingerTree<T, M> = value

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = value[index] as T
}

/** Immutable snapshot-plus-position gap cursor over a persistent RRB vector. */
public class RrbVectorCursor<T> private constructor(
    private val value: RrbVector<T>,
    public val position: Int,
) {
    public companion object {
        internal fun <T> create(value: RrbVector<T>, position: Int): RrbVectorCursor<T> =
            RrbVectorCursor(value, position)
    }

    public val size: Int
        get() = value.size

    public val isAtStart: Boolean
        get() = position == 0

    public val isAtEnd: Boolean
        get() = position == size

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(itemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(itemAt(position))

    public fun movePrevious(): RrbVectorCursor<T>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): RrbVectorCursor<T>? =
        if (isAtEnd) null else create(value, position + 1)

    /** Moves the gap to [newPosition]; seeking to the current position returns this cursor by identity. */
    public fun seek(newPosition: Int): RrbVectorCursor<T>? =
        if (newPosition == position) this else value.cursorAt(newPosition)

    public fun insert(item: T): RrbVectorCursor<T> =
        create(value.insertAt(position, item)!!, Math.addExact(position, 1))

    public fun insertRange(items: Iterable<T>): RrbVectorCursor<T> {
        val captured = RrbVector.from(items)
        return insertRange(captured)
    }

    /** Splices an existing vector while retaining shared subtrees. */
    public fun insertRange(items: RrbVector<T>): RrbVectorCursor<T> {
        if (items.isEmpty) {
            return this
        }
        val split = value.splitAt(position)!!
        return create(
            split.first.concat(items).concat(split.second),
            Math.addExact(position, items.size),
        )
    }

    public fun deletePrevious(): RrbVectorCursor<T>? =
        if (isAtStart) null else create(value.removeAt(position - 1)!!, position - 1)

    public fun deleteNext(): RrbVectorCursor<T>? =
        if (isAtEnd) null else create(value.removeAt(position)!!, position)

    public fun replaceNext(item: T): RrbVectorCursor<T>? =
        if (isAtEnd) null else create(value.setItem(position, item)!!, position)

    public fun snapshot(): RrbVector<T> = value

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = value[index] as T
}

/** Immutable positional and measured cursor over a lazy range-update sequence. */
public class RangeUpdateSequenceCursor<T, M, Tag> private constructor(
    private val value: RangeUpdateSequence<T, M, Tag>,
    public val position: Int,
) {
    public companion object {
        internal fun <T, M, Tag> create(
            value: RangeUpdateSequence<T, M, Tag>,
            position: Int,
        ): RangeUpdateSequenceCursor<T, M, Tag> = RangeUpdateSequenceCursor(value, position)
    }

    public val size: Int
        get() = value.size

    public val isAtStart: Boolean
        get() = position == 0

    public val isAtEnd: Boolean
        get() = position == size

    public val measureBefore: M
        get() = measureRange(0, position)

    public val measureAfter: M
        get() = measureRange(position, size - position)

    public fun peekPrevious(): SequenceCursorPeek<T>? =
        if (isAtStart) null else SequenceCursorPeek(itemAt(position - 1))

    public fun peekNext(): SequenceCursorPeek<T>? =
        if (isAtEnd) null else SequenceCursorPeek(itemAt(position))

    public fun movePrevious(): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (isAtStart) null else create(value, position - 1)

    public fun moveNext(): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (isAtEnd) null else create(value, position + 1)

    /** Moves the gap to [newPosition]; seeking to the current position returns this cursor by identity. */
    public fun seek(newPosition: Int): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (newPosition == position) this else value.cursorAt(newPosition)

    public fun insert(item: T): RangeUpdateSequenceCursor<T, M, Tag> =
        create(value.insertAt(position, item)!!, Math.addExact(position, 1))

    public fun deletePrevious(): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (isAtStart) null else create(value.removeAt(position - 1)!!, position - 1)

    public fun deleteNext(): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (isAtEnd) null else create(value.removeAt(position)!!, position)

    public fun replaceNext(item: T): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (isAtEnd) null else create(value.setItem(position, item)!!, position)

    public fun measurePrevious(count: Int): M {
        require(count >= 0 && count <= position) { "count exceeds the elements before the cursor" }
        return measureRange(position - count, count)
    }

    public fun measureNext(count: Int): M {
        require(count >= 0 && count <= size - position) { "count exceeds the elements after the cursor" }
        return measureRange(position, count)
    }

    public fun applyPrevious(count: Int, tag: Tag): RangeUpdateSequenceCursor<T, M, Tag> {
        require(count >= 0 && count <= position) { "count exceeds the elements before the cursor" }
        return create(value.applyRange(position - count, count, tag)!!, position)
    }

    public fun applyNext(count: Int, tag: Tag): RangeUpdateSequenceCursor<T, M, Tag> {
        require(count >= 0 && count <= size - position) { "count exceeds the elements after the cursor" }
        return create(value.applyRange(position, count, tag)!!, position)
    }

    public fun snapshot(): RangeUpdateSequence<T, M, Tag> = value

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = value[index] as T

    @Suppress("UNCHECKED_CAST")
    private fun measureRange(start: Int, count: Int): M = value.measureRange(start, count) as M
}
