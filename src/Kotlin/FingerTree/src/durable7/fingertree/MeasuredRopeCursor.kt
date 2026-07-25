package durable7.fingertree

/**
 * The result of [MeasuredRope.cursorByMeasure] or [MeasuredRopeCursor.seekByMeasure].
 *
 * [found] distinguishes a selected boundary element from a miss. On a miss, [cursor] is the end
 * cursor and still exposes the whole-rope prefix measure.
 */
public data class MeasuredRopeCursorSearch<T, M>(
    public val cursor: MeasuredRopeCursor<T, M>,
    public val found: Boolean,
)

/**
 * An immutable measured editing cursor over one retained [MeasuredRope] snapshot.
 *
 * [position] denotes a gap in `0..size`: elements `[0, position)` precede the gap and elements
 * `[position, size)` follow it. Every edit returns another cursor, so retained cursors and snapshots
 * remain branchable. [measureBefore] and [measureAfter] preserve that exact order and assume neither
 * an inverse nor a commutative measure.
 *
 * This Kotlin semantic checkpoint deliberately retains a canonical rope plus its gap. Creation,
 * movement, seek, and [snapshot] are O(1). Measure reads, peeks, point edits, and absolute measure
 * seeks are O(log n) over the measured AVL substrate; inserting `m` values is O(m + log n). Policy
 * callbacks used by measure reads, searches, or edits may propagate exceptions, but the immutable
 * receiver remains unchanged and retryable. There is no C# focused cursor representation, memo-cell, allocation, or
 * amortized-locality claim.
 */
public class MeasuredRopeCursor<T, M> private constructor(
    private val rope: MeasuredRope<T, M>,
    public val position: Int,
) {
    internal companion object {
        internal fun <T, M> create(
            rope: MeasuredRope<T, M>,
            position: Int,
        ): MeasuredRopeCursor<T, M> = MeasuredRopeCursor(rope, position)
    }

    init {
        require(position >= 0 && position <= rope.size) { "Cursor position must be in 0..rope.size." }
    }

    /** The number of elements in this cursor's retained snapshot. */
    public val size: Int
        get() = rope.size

    /** Whether this cursor's retained snapshot is empty. */
    public val isEmpty: Boolean
        get() = rope.isEmpty

    /** Whether the gap precedes the first element. */
    public val isAtStart: Boolean
        get() = position == 0

    /** Whether the gap follows the last element. */
    public val isAtEnd: Boolean
        get() = position == size

    /** The ordered measure of `[0, position)`. This read is O(log n). */
    public val measureBefore: M
        get() = rope.measurePrefix(position)

    /** The ordered measure of `[position, size)`. This read is O(log n). */
    public val measureAfter: M
        get() = rope.measureSuffix(position)

    /** Returns the element immediately before the gap, or `null` at the start. */
    public fun peekPrevious(): RopeCursorPeek<T>? =
        if (isAtStart) null else RopeCursorPeek(rope.itemAt(position - 1))

    /** Returns the element immediately after the gap, or `null` at the end. */
    public fun peekNext(): RopeCursorPeek<T>? =
        if (isAtEnd) null else RopeCursorPeek(rope.itemAt(position))

    /** Moves the gap toward the start, or returns `null` when already at the start. */
    public fun movePrevious(): MeasuredRopeCursor<T, M>? =
        if (isAtStart) null else create(rope, position - 1)

    /** Moves the gap toward the end, or returns `null` when already at the end. */
    public fun moveNext(): MeasuredRopeCursor<T, M>? =
        if (isAtEnd) null else create(rope, position + 1)

    /**
     * Moves the gap to [position], or returns `null` when it is outside `0..size`.
     * Seeking to the current position returns this cursor by identity.
     */
    public fun seek(position: Int): MeasuredRopeCursor<T, M>? =
        when {
            position < 0 || position > size -> null
            position == this.position -> this
            else -> create(rope, position)
        }

    /**
     * Performs an absolute measure seek over the whole retained version. The result selects the gap
     * immediately before the first element whose inclusive prefix satisfies [predicate]. A miss returns
     * the end cursor. If the selected gap is already current, the result retains this cursor by identity.
     */
    public fun seekByMeasure(predicate: (M) -> Boolean): MeasuredRopeCursorSearch<T, M> {
        val located = rope.locateByMeasure(predicate)
        val destination = seek(located.index)
            ?: error("A measured locate result must identify a valid cursor gap.")
        return MeasuredRopeCursorSearch(destination, located.found)
    }

    /** Returns the exact immutable rope snapshot retained by this cursor. */
    public fun snapshot(): MeasuredRope<T, M> = rope

    /**
     * Inserts [value] at the gap and returns a cursor immediately after it.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun insert(value: T): MeasuredRopeCursor<T, M> {
        Math.addExact(size, 1)
        val nextPosition = Math.addExact(position, 1)
        val edited = rope.insertAt(position, value)
            ?: error("A validated cursor gap must be a valid insertion position.")
        return create(edited, nextPosition)
    }

    /**
     * Inserts [values] at the gap in iteration order and returns a cursor after the range. The source
     * is consumed exactly once; an empty input returns this cursor by identity.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun insertRange(values: Iterable<T>): MeasuredRopeCursor<T, M> {
        val owned = values.toList()
        if (owned.isEmpty()) {
            return this
        }

        Math.addExact(size, owned.size)
        val nextPosition = Math.addExact(position, owned.size)
        val edited = rope.insertRange(position, owned)
            ?: error("A validated cursor gap must be a valid range insertion position.")
        return create(edited, nextPosition)
    }

    /** Deletes the element before the gap and moves left, or returns `null` at the start. */
    public fun deletePrevious(): MeasuredRopeCursor<T, M>? {
        if (isAtStart) {
            return null
        }

        val nextPosition = position - 1
        val edited = rope.removeAt(nextPosition)
            ?: error("A non-start cursor must have a previous element.")
        return create(edited, nextPosition)
    }

    /** Deletes the element after the gap without moving, or returns `null` at the end. */
    public fun deleteNext(): MeasuredRopeCursor<T, M>? {
        if (isAtEnd) {
            return null
        }

        val edited = rope.removeAt(position)
            ?: error("A non-end cursor must have a next element.")
        return create(edited, position)
    }

    /**
     * Unconditionally replaces the element after the gap, or returns `null` at the end. Equality is
     * not consulted, the supplied representative is stored, and the gap does not move.
     */
    public fun replaceNext(value: T): MeasuredRopeCursor<T, M>? {
        if (isAtEnd) {
            return null
        }

        val edited = rope.setItem(position, value)
            ?: error("A non-end cursor must have a next element.")
        return create(edited, position)
    }
}

/**
 * Returns the zero-based line and UTF-16 column at this newline-measured cursor's gap.
 *
 * The receiver must use [NewlineMeasure]; the runtime check substitutes for C#'s measure-policy type
 * parameter, which statically pins that specialization there.
 */
public fun MeasuredRopeCursor<Char, Int>.lineColumn(): LineColumn {
    val text = snapshot()
    require(text.policy === NewlineMeasure) { "Line/column navigation requires NewlineMeasure." }

    val line = measureBefore
    val lineStart = if (line == 0) {
        0
    } else {
        val precedingNewline = text.locateByMeasure { it >= line }
        check(precedingNewline.found) { "The cached newline prefix must identify its preceding newline." }
        precedingNewline.index + 1
    }
    return LineColumn(line, position - lineStart)
}
