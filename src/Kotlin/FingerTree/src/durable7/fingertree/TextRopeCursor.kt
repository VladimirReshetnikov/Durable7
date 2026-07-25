package durable7.fingertree

/** The result of [TextRope.cursorByMeasure] or [TextRopeCursor.seekByMeasure]. */
public data class TextRopeCursorSearch(
    public val cursor: TextRopeCursor,
    public val found: Boolean,
)

/**
 * An immutable UTF-16 text cursor over one retained [TextRope] snapshot.
 *
 * This is a thin facade over [MeasuredRopeCursor] specialized by [NewlineMeasure]. Navigation retains
 * the exact source [TextRope], while every edit wraps its new measured snapshot in O(1), so [snapshot]
 * continues to expose line and string helpers without materialization. The gap, editing, absolute
 * measure-seek, and complexity contracts are otherwise those of [MeasuredRopeCursor].
 */
public class TextRopeCursor private constructor(
    private val text: TextRope,
    private val measured: MeasuredRopeCursor<Char, Int>,
) {
    internal companion object {
        internal fun create(text: TextRope, measured: MeasuredRopeCursor<Char, Int>): TextRopeCursor {
            check(measured.snapshot() === text.toMeasuredRope()) {
                "A text cursor and its text facade must retain the same measured snapshot."
            }
            return TextRopeCursor(text, measured)
        }
    }

    /** The number of UTF-16 code units in this cursor's retained snapshot. */
    public val size: Int
        get() = measured.size

    /** The gap position in `0..size`, measured in UTF-16 code units. */
    public val position: Int
        get() = measured.position

    /** Whether this cursor's retained snapshot is empty. */
    public val isEmpty: Boolean
        get() = measured.isEmpty

    /** Whether the gap precedes the first UTF-16 code unit. */
    public val isAtStart: Boolean
        get() = measured.isAtStart

    /** Whether the gap follows the last UTF-16 code unit. */
    public val isAtEnd: Boolean
        get() = measured.isAtEnd

    /** The number of newlines before the gap. */
    public val measureBefore: Int
        get() = measured.measureBefore

    /** The number of newlines at or after the gap. */
    public val measureAfter: Int
        get() = measured.measureAfter

    /** Returns the UTF-16 code unit immediately before the gap, or `null` at the start. */
    public fun peekPrevious(): RopeCursorPeek<Char>? = measured.peekPrevious()

    /** Returns the UTF-16 code unit immediately after the gap, or `null` at the end. */
    public fun peekNext(): RopeCursorPeek<Char>? = measured.peekNext()

    /** Moves toward the start, or returns `null` when already at the start. */
    public fun movePrevious(): TextRopeCursor? =
        measured.movePrevious()?.let { create(text, it) }

    /** Moves toward the end, or returns `null` when already at the end. */
    public fun moveNext(): TextRopeCursor? =
        measured.moveNext()?.let { create(text, it) }

    /**
     * Moves to [position], or returns `null` outside `0..size`. A same-position seek returns this
     * text cursor by identity.
     */
    public fun seek(position: Int): TextRopeCursor? =
        when {
            position < 0 || position > size -> null
            position == this.position -> this
            else -> create(text, measured.seek(position)!!)
        }

    /** Performs an absolute newline-prefix seek, returning the end cursor on a miss. */
    public fun seekByMeasure(predicate: (Int) -> Boolean): TextRopeCursorSearch {
        val located = measured.seekByMeasure(predicate)
        val destination = if (located.cursor === measured) this else create(text, located.cursor)
        return TextRopeCursorSearch(destination, located.found)
    }

    /** Returns the zero-based line and UTF-16 column at the gap. */
    public fun lineColumn(): LineColumn = measured.lineColumn()

    /** Returns the exact immutable text snapshot retained by this cursor. */
    public fun snapshot(): TextRope = text

    /** Inserts [value] at the gap and returns a cursor immediately after it. */
    public fun insert(value: Char): TextRopeCursor = fromEdit(measured.insert(value))

    /** Inserts [values] once in iteration order; an empty input returns this cursor by identity. */
    public fun insertRange(values: Iterable<Char>): TextRopeCursor {
        val edited = measured.insertRange(values)
        return if (edited === measured) this else fromEdit(edited)
    }

    /** Deletes the code unit before the gap and moves left, or returns `null` at the start. */
    public fun deletePrevious(): TextRopeCursor? = measured.deletePrevious()?.let(::fromEdit)

    /** Deletes the code unit after the gap without moving, or returns `null` at the end. */
    public fun deleteNext(): TextRopeCursor? = measured.deleteNext()?.let(::fromEdit)

    /** Replaces the code unit after the gap without moving, or returns `null` at the end. */
    public fun replaceNext(value: Char): TextRopeCursor? = measured.replaceNext(value)?.let(::fromEdit)

    private fun fromEdit(cursor: MeasuredRopeCursor<Char, Int>): TextRopeCursor {
        val editedText = TextRope.fromMeasured(cursor.snapshot())
        return create(editedText, cursor)
    }
}
