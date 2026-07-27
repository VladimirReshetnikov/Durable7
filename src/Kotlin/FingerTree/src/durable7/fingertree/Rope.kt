/*
 * Persistent chunked sequences, in positional, measured, and text flavors.
 *
 * Elements live in contiguous chunks at the leaves rather than one per leaf, so bulk work runs over
 * slices while the tree above still gives logarithmic indexing, splitting, and concatenation. The
 * text layer caches newline counts, making offset-to-line conversion logarithmic instead of a scan.
 */
package durable7.fingertree

/**
 * A persistent positional sequence backed by the shared immutable measured AVL tree.
 *
 * Every growth operation uses checked [Int] arithmetic. If a result would exceed [Int.MAX_VALUE],
 * the operation throws [ArithmeticException] before publication and every input rope remains valid.
 */
public class Rope<T> private constructor(
    private val items: PersistentDeque<T>,
) : Iterable<T> {
    public companion object {
        /** An empty rope. */
        public fun <T> empty(): Rope<T> = Rope(PersistentDeque.empty())

        /** A rope holding every value, in iteration order. */
        public fun <T> from(values: Iterable<T>): Rope<T> = Rope(PersistentDeque.from(values))

        /**
         * A rope holding the elements of every chunk, concatenated in order. The chunks are copied into rope-owned
         * storage, so a caller may keep mutating the collections it passed.
         */
        public fun <T> fromChunks(chunks: Iterable<Iterable<T>>): Rope<T> =
            Rope(PersistentDeque.from(chunks.flatten()))

        /**
         * A rope of the string's UTF-16 code units. Surrogate pairs are two elements here; use [TextRope] when line
         * and column positions matter.
         */
        public fun fromText(text: String): Rope<Char> = from(text.asIterable())
    }

    /** Number of elements. O(1), from the cached measure at the root. */
    public val size: Int
        get() = items.size

    /** Whether the rope holds no elements. */
    public val isEmpty: Boolean
        get() = items.isEmpty

    /** Returns an immutable positional cursor at the gap before the first element. */
    public fun cursor(): RopeCursor<T> = RopeCursor.create(this, 0)

    /** Returns a cursor at [position], or `null` when the position is outside `0..size`. */
    public fun cursorAt(position: Int): RopeCursor<T>? =
        if (position < 0 || position > size) null else RopeCursor.create(this, position)

    /** The first element, or `null` when empty. */
    public fun front(): T? = items.front()

    /** The last element, or `null` when empty. */
    public fun back(): T? = items.back()

    /** The element at [index], or `null` when the index is out of range. */
    public operator fun get(index: Int): T? = items[index]

    /**
     * Copy `destination.size` elements starting at [index] into [destination], reporting whether the range was
     * valid. Returns `false` and copies nothing when it is not, so a partial copy is never left behind.
     */
    public fun copyTo(index: Int, destination: MutableList<in T>): Boolean {
        if (!isValidRange(index, destination.size, size)) {
            return false
        }

        // Seek to the start index once, then stream in order: O(k + log n)
        // rather than a full root-to-leaf descent per copied element.
        val source = items.iteratorFrom(index)
        for (offset in destination.indices) {
            destination[offset] = source.next()
        }

        return true
    }

    /**
     * Returns a rope with [value] prepended.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun pushFront(value: T): Rope<T> {
        Math.addExact(size, 1)
        return Rope(items.prepend(value))
    }

    /**
     * Returns a rope with [value] appended.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun pushBack(value: T): Rope<T> {
        Math.addExact(size, 1)
        return Rope(items.append(value))
    }

    /** A rope with the element at [index] replaced, or `null` when the index is out of range. */
    public fun setItem(index: Int, value: T): Rope<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        // Always store the supplied element (the C# reference replaces
        // unconditionally, even for an equal value).
        return Rope(items.setItem(index, value)!!)
    }

    /**
     * Returns a rope with [value] inserted at [index], or `null` when [index] is not a valid gap.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun insertAt(index: Int, value: T): Rope<T>? {
        if (index < 0 || index > size) {
            return null
        }

        Math.addExact(size, 1)
        return Rope(items.insertAt(index, value)!!)
    }

    /**
     * Returns a rope with [values] inserted at [index], or `null` when [index] is not a valid gap.
     * The iterable is consumed exactly once. An empty input returns this rope by identity.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun insertRange(index: Int, values: Iterable<T>): Rope<T>? {
        if (index < 0 || index > size) {
            return null
        }

        val owned = values.toList()
        if (owned.isEmpty()) {
            return this
        }

        Math.addExact(size, owned.size)
        val split = items.splitAt(index)!!
        val middle = PersistentDeque.from(owned)
        return Rope(split.left.concat(middle).concat(split.right))
    }

    /** A rope without the element at [index], or `null` when the index is out of range. */
    public fun removeAt(index: Int): Rope<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        return Rope(items.removeAt(index)!!)
    }

    /**
     * A rope without the [count] elements starting at [index], or `null` when that range does not lie inside the
     * rope.
     */
    public fun removeRange(index: Int, count: Int): Rope<T>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        val first = items.splitAt(index)!!
        val second = first.right.splitAt(count)!!
        return Rope(first.left.concat(second.right))
    }

    /**
     * The [count] elements starting at [index], as a rope, or `null` when that range does not lie inside the rope.
     * The slice shares storage with the receiver rather than copying.
     */
    public fun slice(index: Int, count: Int): Rope<T>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        val suffix = items.splitAt(index)!!.right
        return Rope(suffix.splitAt(count)!!.left)
    }

    /**
     * The elements before [index] and those from [index] on, or `null` when the index lies outside `0..size`. Both
     * halves share storage with the receiver.
     */
    public fun splitAt(index: Int): Pair<Rope<T>, Rope<T>>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        return Rope(split.left) to Rope(split.right)
    }

    /**
     * Concatenates this rope with [other]. Empty operands preserve the existing rope by identity.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun concat(other: Rope<T>): Rope<T> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> {
                Math.addExact(size, other.size)
                Rope(items.concat(other.items))
            }
        }

    /**
     * Returns the receiver. This port stores elements individually rather than in chunks, so there is no
     * fragmentation to compact; the member exists for parity with the ports that do chunk, where it is meaningful.
     */
    public fun compact(): Rope<T> = this

    /** The elements in order. */
    public fun toList(): List<T> = items.toList()

    /**
     * Whether this rope and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common. A diagnostic for confirming structural sharing, not a substitute for equality.
     */
    public fun sharesStorageWith(other: Rope<T>): Boolean = items.sharesStorageWith(other.items)

    internal fun itemAt(index: Int): T = items.itemAt(index)

    internal fun debugIsBalanced(): Boolean = items.debugIsBalanced()

    override fun iterator(): Iterator<T> = items.iterator()
}

/**
 * A nullable-safe result from [RopeCursor.peekPrevious] or [RopeCursor.peekNext].
 *
 * A non-null wrapper reports that a neighbor exists even when [value] itself is `null`.
 */
public data class RopeCursorPeek<T>(public val value: T)

/**
 * An immutable positional editing cursor over one retained [Rope] snapshot.
 *
 * [position] denotes a gap in `0..size`: the previous element is at `position - 1` and the next
 * element is at `position`. Every movement or edit returns another immutable cursor, so retained
 * cursors branch independently. This semantic checkpoint stores a rope snapshot plus its gap; it
 * does not implement the C# focused cursor representation and makes no amortized-locality claim.
 *
 * Creation, movement, seek, and [snapshot] are O(1). Peeks and point edits are O(log n), and
 * inserting `m` values is O(m + log n). Failed edits leave the receiver reusable. Like [Rope],
 * cursors are structurally safe for concurrent readers; callers remain responsible for any mutable
 * state reachable through stored elements. The type has no public constructor or default instance.
 */
public class RopeCursor<T> private constructor(
    private val rope: Rope<T>,
    public val position: Int,
) {
    internal companion object {
        internal fun <T> create(rope: Rope<T>, position: Int): RopeCursor<T> = RopeCursor(rope, position)
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

    /** Returns the element immediately before the gap, or `null` at the start. */
    public fun peekPrevious(): RopeCursorPeek<T>? =
        if (isAtStart) null else RopeCursorPeek(rope.itemAt(position - 1))

    /** Returns the element immediately after the gap, or `null` at the end. */
    public fun peekNext(): RopeCursorPeek<T>? =
        if (isAtEnd) null else RopeCursorPeek(rope.itemAt(position))

    /** Moves the gap toward the start, or returns `null` when already at the start. */
    public fun movePrevious(): RopeCursor<T>? =
        if (isAtStart) null else create(rope, position - 1)

    /** Moves the gap toward the end, or returns `null` when already at the end. */
    public fun moveNext(): RopeCursor<T>? =
        if (isAtEnd) null else create(rope, position + 1)

    /**
     * Moves the gap to [position], or returns `null` when it is outside `0..size`.
     * Seeking to the current position returns this cursor by identity.
     */
    public fun seek(position: Int): RopeCursor<T>? =
        when {
            position < 0 || position > size -> null
            position == this.position -> this
            else -> create(rope, position)
        }

    /** Returns the exact immutable rope snapshot retained by this cursor. */
    public fun snapshot(): Rope<T> = rope

    /**
     * Inserts [value] at the gap and returns a cursor immediately after it.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun insert(value: T): RopeCursor<T> {
        val nextPosition = Math.addExact(position, 1)
        val edited = rope.insertAt(position, value)
            ?: error("A validated cursor gap must be a valid insertion position.")
        return create(edited, nextPosition)
    }

    /**
     * Inserts [values] at the gap in iteration order and returns a cursor after the range.
     * The iterable is consumed exactly once. An empty input returns this cursor by identity.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun insertRange(values: Iterable<T>): RopeCursor<T> {
        val owned = values.toList()
        if (owned.isEmpty()) {
            return this
        }

        val nextPosition = Math.addExact(position, owned.size)
        val edited = rope.insertRange(position, owned)
            ?: error("A validated cursor gap must be a valid range insertion position.")
        return create(edited, nextPosition)
    }

    /** Deletes the element before the gap and moves left, or returns `null` at the start. */
    public fun deletePrevious(): RopeCursor<T>? {
        if (isAtStart) {
            return null
        }

        val nextPosition = position - 1
        val edited = rope.removeAt(nextPosition)
            ?: error("A non-start cursor must have a previous element.")
        return create(edited, nextPosition)
    }

    /** Deletes the element after the gap without moving, or returns `null` at the end. */
    public fun deleteNext(): RopeCursor<T>? {
        if (isAtEnd) {
            return null
        }

        val edited = rope.removeAt(position)
            ?: error("A non-end cursor must have a next element.")
        return create(edited, position)
    }

    /**
     * Unconditionally replaces the element after the gap, or returns `null` at the end.
     * Equality is not consulted, the supplied representative is stored, and the gap does not move.
     */
    public fun replaceNext(value: T): RopeCursor<T>? {
        if (isAtEnd) {
            return null
        }

        val edited = rope.setItem(position, value)
            ?: error("A non-end cursor must have a next element.")
        return create(edited, position)
    }
}

/** The two ropes produced by a split, each with its own recomputed measure. */
public data class MeasuredRopeSplit<T, M>(
    public val left: MeasuredRope<T, M>,
    public val right: MeasuredRope<T, M>,
)

/**
 * The result of [MeasuredRope.locateByMeasure]. [found] reports whether the
 * predicate selected a stored element; when the rope stores null elements it
 * is the only reliable discriminator, because [value] is null both for a
 * stored null and for a predicate that never became true (matching the C#
 * reference, whose TryLocate returns a boolean alongside the out parameters).
 */
public data class MeasuredRopeLocate<T, M>(
    public val index: Int,
    public val measureBefore: M,
    public val value: T?,
    public val found: Boolean,
)

/**
 * The result of [MeasuredRope.prefixMeasure] for an in-range boundary. The wrapper keeps an invalid
 * count distinguishable from a legitimate aggregate, because a measure policy may make the monoid
 * identity itself null (as [MaxMeasure] and [MinMeasure] do); this mirrors [RopeCursorPeek], which
 * separates absence from a stored null in the same way.
 */
public data class MeasuredRopePrefix<M>(public val measure: M)

/**
 * A persistent measured sequence backed by the shared immutable measured AVL tree.
 *
 * Every growth operation uses checked [Int] arithmetic. If a result would exceed [Int.MAX_VALUE],
 * the operation throws [ArithmeticException] before invoking the measure policy or publishing a result.
 */
public class MeasuredRope<T, M> private constructor(
    private val items: PersistentMeasuredTree<T, M>,
    public val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    public companion object {
        /**
         * An empty rope measured by [policy], which the rope retains. Operations between two ropes require the same
         * policy.
         */
        public fun <T, M> empty(policy: MeasurePolicy<T, M>): MeasuredRope<T, M> =
            MeasuredRope(PersistentMeasuredTree.empty(policy), policy)

        /** A rope holding every value, measured by [policy]. */
        public fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredRope<T, M> =
            MeasuredRope(PersistentMeasuredTree.from(values, policy), policy)

        /**
         * A rope holding the elements of every chunk, concatenated in order and measured by [policy]. The chunks
         * are copied into rope-owned storage.
         */
        public fun <T, M> fromChunks(
            chunks: Iterable<Iterable<T>>,
            policy: MeasurePolicy<T, M>,
        ): MeasuredRope<T, M> = MeasuredRope(PersistentMeasuredTree.from(chunks.flatten(), policy), policy)
    }

    /** Number of elements. O(1). */
    public val size: Int
        get() = items.size

    /** Whether the rope holds no elements. */
    public val isEmpty: Boolean
        get() = items.isEmpty

    /** Returns an immutable measured cursor at the gap before the first element. */
    public fun cursor(): MeasuredRopeCursor<T, M> = MeasuredRopeCursor.create(this, 0)

    /** Returns a measured cursor at [position], or `null` when it is outside `0..size`. */
    public fun cursorAt(position: Int): MeasuredRopeCursor<T, M>? =
        if (position < 0 || position > size) null else MeasuredRopeCursor.create(this, position)

    /**
     * Locates the gap immediately before the first element whose inclusive absolute prefix satisfies
     * [predicate]. A miss, including an empty rope, returns the end cursor with [MeasuredRopeCursorSearch.found]
     * set to `false`.
     */
    public fun cursorByMeasure(predicate: (M) -> Boolean): MeasuredRopeCursorSearch<T, M> {
        val located = locateByMeasure(predicate)
        return MeasuredRopeCursorSearch(
            MeasuredRopeCursor.create(this, located.index),
            located.found,
        )
    }

    /** The measure of the whole rope, read from the cached root measure rather than recomputed. O(1). */
    public fun measure(): M = items.measure()

    /** The first element, or `null` when empty. */
    public fun front(): T? = items.front()

    /** The last element, or `null` when empty. */
    public fun back(): T? = items.back()

    /** The element at [index], or `null` when the index is out of range. */
    public operator fun get(index: Int): T? = items[index]

    /**
     * Returns the ordered measure of the first [count] elements, or `null` when [count] lies outside
     * `0..size`. The present result is wrapped so an invalid count stays distinguishable from an
     * aggregate that is itself null under the policy's monoid identity.
     */
    public fun prefixMeasure(count: Int): MeasuredRopePrefix<M>? {
        if (count < 0 || count > size) {
            return null
        }

        return MeasuredRopePrefix(items.measurePrefix(count))
    }

    /**
     * Copy `destination.size` elements starting at [index] into [destination], reporting whether the range was
     * valid. Returns `false` and copies nothing when it is not.
     */
    public fun copyTo(index: Int, destination: MutableList<in T>): Boolean {
        if (!isValidRange(index, destination.size, size)) {
            return false
        }

        // Seek to the start index once, then stream in order: O(k + log n)
        // rather than a full root-to-leaf descent per copied element.
        val source = items.iteratorFrom(index)
        for (offset in destination.indices) {
            destination[offset] = source.next()
        }

        return true
    }

    /**
     * Returns a rope with [value] prepended.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun pushFront(value: T): MeasuredRope<T, M> {
        Math.addExact(size, 1)
        return MeasuredRope(items.prepend(value), policy)
    }

    /**
     * Returns a rope with [value] appended.
     *
     * @throws ArithmeticException if the resulting size cannot be represented by [Int].
     */
    public fun pushBack(value: T): MeasuredRope<T, M> {
        Math.addExact(size, 1)
        return MeasuredRope(items.append(value), policy)
    }

    /**
     * A rope with the element at [index] replaced, or `null` when the index is out of range. Measures on the path
     * to the element are recomputed; the rest are reused.
     */
    public fun setItem(index: Int, value: T): MeasuredRope<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        // Always store the supplied element (the C# reference replaces
        // unconditionally, even for an equal value).
        return MeasuredRope(items.setItem(index, value)!!, policy)
    }

    /**
     * A rope with [value] inserted so that it ends up at [index], or `null` when the index lies outside `0..size`.
     */
    public fun insertAt(index: Int, value: T): MeasuredRope<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        Math.addExact(size, 1)
        return MeasuredRope(items.insertAt(index, value)!!, policy)
    }

    /**
     * A rope with every element of [values] inserted at [index], in order, or `null` when the index lies outside
     * `0..size`. Splits and joins once regardless of how many are inserted.
     */
    public fun insertRange(index: Int, values: Iterable<T>): MeasuredRope<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        val owned = values.toList()
        if (owned.isEmpty()) {
            return this
        }

        Math.addExact(size, owned.size)
        val split = items.splitAt(index)!!
        val middle = PersistentMeasuredTree.from(owned, policy)
        return MeasuredRope(split.first.concat(middle).concat(split.second), policy)
    }

    /** A rope without the element at [index], or `null` when the index is out of range. */
    public fun removeAt(index: Int): MeasuredRope<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        return MeasuredRope(items.removeAt(index)!!, policy)
    }

    /**
     * A rope without the [count] elements starting at [index], or `null` when that range does not lie inside the
     * rope.
     */
    public fun removeRange(index: Int, count: Int): MeasuredRope<T, M>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        val first = items.splitAt(index)!!
        val second = first.second.splitAt(count)!!
        return MeasuredRope(first.first.concat(second.second), policy)
    }

    /**
     * The [count] elements starting at [index], as a rope, or `null` when that range does not lie inside the rope.
     * The slice shares storage with the receiver.
     */
    public fun slice(index: Int, count: Int): MeasuredRope<T, M>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        val suffix = items.splitAt(index)!!.second
        return MeasuredRope(suffix.splitAt(count)!!.first, policy)
    }

    /** The elements before [index] and those from [index] on, or `null` when the index lies outside `0..size`. */
    public fun splitAt(index: Int): MeasuredRopeSplit<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        return MeasuredRopeSplit(MeasuredRope(split.first, policy), MeasuredRope(split.second, policy))
    }

    /**
     * Split at the first point where the accumulated prefix measure satisfies [predicate].
     *
     * The predicate must be monotone - once true it stays true as more elements are accumulated - because the
     * descent commits to a branch on each node's cached measure without visiting the elements it skips.
     */
    public fun splitByMeasure(predicate: (M) -> Boolean): MeasuredRopeSplit<T, M> {
        val split = items.splitByMeasure(predicate)
        return MeasuredRopeSplit(MeasuredRope(split.first, policy), MeasuredRope(split.second, policy))
    }

    /**
     * The first index at which [predicate] becomes true, with the measure accumulated before it and the element
     * there. Reports `found = false` and the end position when no prefix satisfies the predicate, so a miss stays
     * distinct from a hit at the end.
     */
    public fun locateByMeasure(predicate: (M) -> Boolean): MeasuredRopeLocate<T, M> {
        val located = items.locate(predicate)
        return MeasuredRopeLocate(located.index, located.measureBefore, located.value, located.found)
    }

    /**
     * A rope holding this one's elements followed by [other]'s.
     *
     * @throws IllegalArgumentException if the two ropes do not carry the same measure policy, since combining
     * measures computed under different policies would leave the cached measures meaningless.
     *
     * @throws ArithmeticException if the combined length overflows [Int].
     */
    public fun concat(other: MeasuredRope<T, M>): MeasuredRope<T, M> {
        Math.addExact(size, other.size)
        require(policy === other.policy || policy == other.policy) { "Cannot concatenate ropes with different measure policies." }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> MeasuredRope(items.concat(other.items), policy)
        }
    }

    /**
     * Returns the receiver. This port stores elements individually rather than in chunks, so there is no
     * fragmentation to compact; the member exists for parity with the ports that do chunk.
     */
    public fun compact(): MeasuredRope<T, M> = this

    /** The elements in order. */
    public fun toList(): List<T> = items.toList()

    /**
     * Whether this rope and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common. A diagnostic for confirming structural sharing, not a substitute for equality.
     */
    public fun sharesStorageWith(other: MeasuredRope<T, M>): Boolean = items.sharesStructureWith(other.items)

    @Suppress("UNCHECKED_CAST")
    internal fun itemAt(index: Int): T = items[index] as T

    internal fun measurePrefix(count: Int): M = items.measurePrefix(count)

    internal fun measureSuffix(startIndex: Int): M = items.measureSuffix(startIndex)

    internal fun debugIsBalanced(): Boolean = items.isBalanced()

    override fun iterator(): Iterator<T> = items.iterator()
}

/** Counts line feeds, which is what gives a text rope logarithmic offset-to-line conversion. */
public object NewlineMeasure : MeasurePolicy<Char, Int> {
    /** The identity: zero line feeds. */
    override val empty: Int = 0

    /** A line feed counts as one; every other character as zero. */
    override fun measure(element: Char): Int = if (element == '\n') 1 else 0

    /** Add two line counts. */
    override fun combine(left: Int, right: Int): Int = left + right
}

/** A zero-based line and column position within a text rope. */
public data class LineColumn(public val line: Int, public val column: Int)

/**
 * A persistent text rope over characters, with newline counts cached in its measure so converting between an offset and
 * a line/column is logarithmic rather than a scan.
 */
public class TextRope private constructor(
    private val characters: MeasuredRope<Char, Int>,
) {
    public companion object {
        /** An empty text rope. */
        public fun empty(): TextRope = TextRope(MeasuredRope.empty(NewlineMeasure))
        /**
         * A text rope holding [text]. Positions are UTF-16 code-unit offsets, matching Kotlin's own [String]
         * indexing.
         */
        public fun fromText(text: String): TextRope = TextRope(MeasuredRope.from(text.asIterable(), NewlineMeasure))

        internal fun fromMeasured(characters: MeasuredRope<Char, Int>): TextRope {
            require(characters.policy === NewlineMeasure) { "Text ropes require NewlineMeasure." }
            return TextRope(characters)
        }
    }

    /** Number of UTF-16 code units. O(1). */
    public val size: Int
        get() = characters.size

    /** Whether the rope holds no characters. */
    public val isEmpty: Boolean
        get() = characters.isEmpty

    /** Returns a newline-measured cursor at the gap before the first UTF-16 code unit. */
    public fun cursor(): TextRopeCursor = TextRopeCursor.create(this, characters.cursor())

    /** Returns a newline-measured cursor at [position], or `null` outside `0..size`. */
    public fun cursorAt(position: Int): TextRopeCursor? =
        characters.cursorAt(position)?.let { TextRopeCursor.create(this, it) }

    /** Locates a newline-measured cursor by an absolute prefix predicate. */
    public fun cursorByMeasure(predicate: (Int) -> Boolean): TextRopeCursorSearch {
        val located = characters.cursorByMeasure(predicate)
        return TextRopeCursorSearch(TextRopeCursor.create(this, located.cursor), located.found)
    }

    /**
     * The whole text as a [String]. O(n) and allocates the entire result; prefer [getLine] or [slice] on the
     * underlying rope when only part is needed.
     */
    public fun asString(): String = characters.toList().joinToString("")

    /**
     * Returns the number of lines, which is one more than the stored newline count.
     *
     * @throws ArithmeticException if the count cannot be represented by [Int]. A wrapped count would
     *   make every line helper report a miss and [lines] return an empty list, which is a consistent
     *   but wrong answer rather than a detectable failure.
     */
    public fun lineCount(): Int = Math.addExact(characters.measure(), 1)

    /**
     * The zero-based line containing [offset], or `null` when the offset lies outside `0..size`. Logarithmic: the
     * cached newline counts locate the line without scanning.
     */
    public fun lineOfOffset(offset: Int): Int? {
        if (offset < 0 || offset > size) {
            return null
        }

        return characters.prefixMeasure(offset)?.measure
    }

    /** The offset at which the zero-based [line] starts, or `null` when the line does not exist. */
    public fun lineStartOffset(line: Int): Int? {
        if (line < 0 || line >= lineCount()) {
            return null
        }

        if (line == 0) {
            return 0
        }

        // [found] is the discriminator the locate contract names; [value] is null both for a stored
        // null and for a predicate that never became true.
        val newline = characters.locateByMeasure { it >= line }
        return if (!newline.found) null else newline.index + 1
    }

    /** The zero-based line and column of [offset], or `null` when the offset lies outside `0..size`. */
    public fun lineColumnOf(offset: Int): LineColumn? {
        val line = lineOfOffset(offset) ?: return null
        val start = lineStartOffset(line) ?: return null
        return LineColumn(line, offset - start)
    }

    /**
     * The offset of the zero-based [line] and [column], or `null` when that position does not exist. A column past
     * the end of its line is a miss rather than a clamp.
     */
    public fun offsetOf(line: Int, column: Int): Int? {
        if (column < 0) {
            return null
        }

        val start = lineStartOffset(line) ?: return null
        val end = lineEndOffset(line) ?: return null
        // Widen so a huge column cannot overflow the check and pass as a
        // negative offset (matches the C# reference's (long)start + column).
        val offset = start.toLong() + column
        return if (offset <= end) offset.toInt() else null
    }

    /**
     * The text of the zero-based [line], excluding its terminating newline, or `null` when the line does not exist.
     */
    public fun getLine(line: Int): String? {
        val start = lineStartOffset(line) ?: return null
        val end = lineEndOffset(line) ?: return null
        val suffix = characters.splitAt(start)!!.right
        return suffix.splitAt(end - start)!!.left.toList().joinToString("")
    }

    /**
     * Every line, excluding terminating newlines. A rope ending in a newline therefore yields a final empty line,
     * since [lineCount] counts one more line than there are newlines.
     */
    public fun lines(): List<String> = (0 until lineCount()).mapNotNull { getLine(it) }

    /**
     * The characters as a plain [Rope], dropping the newline measure. Cheap: the elements are shared, not copied.
     */
    public fun toCharRope(): Rope<Char> = Rope.from(characters)

    /** The underlying newline-measured rope, for callers that want the measure directly. */
    public fun toMeasuredRope(): MeasuredRope<Char, Int> = characters

    override fun toString(): String = asString()

    internal fun debugIsBalanced(): Boolean = characters.debugIsBalanced()

    private fun lineEndOffset(line: Int): Int? {
        if (line < 0 || line >= lineCount()) {
            return null
        }

        if (line + 1 < lineCount()) {
            return (lineStartOffset(line + 1) ?: return null) - 1
        }

        return size
    }
}

/**
 * A mutable accumulator for building a character or text rope in bulk. Deliberately mutable and not a snapshot: text is
 * appended into a plain buffer and turned into a persistent rope only on demand.
 */
public class RopeBuilder {
    private val builder = StringBuilder()

    /** Number of characters accumulated so far. */
    public val size: Int
        get() = builder.length

    /** Whether nothing has been appended yet. */
    public val isEmpty: Boolean
        get() = builder.isEmpty()

    /** The accumulated text, without building a rope. */
    public fun asString(): String = builder.toString()

    /** Append text, returning the builder for chaining. */
    public fun append(text: String): RopeBuilder {
        builder.append(text)
        return this
    }

    /** Append one character, returning the builder for chaining. */
    public fun appendChar(value: Char): RopeBuilder {
        builder.append(value)
        return this
    }

    /** Append text followed by a line feed, returning the builder for chaining. */
    public fun appendLine(text: String): RopeBuilder {
        builder.append(text).append('\n')
        return this
    }

    /** An empty rope retaining the same policies; returns the receiver when already empty. */
    public fun clear(): RopeBuilder {
        builder.clear()
        return this
    }

    /** Build a character rope from the accumulated text, leaving the builder usable. */
    public fun toRope(): Rope<Char> = Rope.fromText(builder.toString())

    /** Build a text rope from the accumulated text, leaving the builder usable. */
    public fun toTextRope(): TextRope = TextRope.fromText(builder.toString())

    /** The accumulated text. */
    override fun toString(): String = builder.toString()
}
