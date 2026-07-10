package tools.datastructures.fingertree

public class Rope<T> private constructor(
    private val items: PersistentDeque<T>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): Rope<T> = Rope(PersistentDeque.empty())

        public fun <T> from(values: Iterable<T>): Rope<T> = Rope(PersistentDeque.from(values))

        public fun <T> fromChunks(chunks: Iterable<Iterable<T>>): Rope<T> =
            Rope(PersistentDeque.from(chunks.flatten()))

        public fun fromText(text: String): Rope<Char> = from(text.asIterable())
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun front(): T? = items.front()

    public fun back(): T? = items.back()

    public operator fun get(index: Int): T? = items[index]

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

    public fun pushFront(value: T): Rope<T> = Rope(items.prepend(value))

    public fun pushBack(value: T): Rope<T> = Rope(items.append(value))

    public fun setItem(index: Int, value: T): Rope<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        // Always store the supplied element (the C# reference replaces
        // unconditionally, even for an equal value).
        return Rope(items.setItem(index, value)!!)
    }

    public fun insertAt(index: Int, value: T): Rope<T>? {
        if (index < 0 || index > size) {
            return null
        }

        return Rope(items.insertAt(index, value)!!)
    }

    public fun insertRange(index: Int, values: Iterable<T>): Rope<T>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        val middle = PersistentDeque.from(values)
        return Rope(split.left.concat(middle).concat(split.right))
    }

    public fun removeAt(index: Int): Rope<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        return Rope(items.removeAt(index)!!)
    }

    public fun removeRange(index: Int, count: Int): Rope<T>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        val first = items.splitAt(index)!!
        val second = first.right.splitAt(count)!!
        return Rope(first.left.concat(second.right))
    }

    public fun slice(index: Int, count: Int): Rope<T>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        val suffix = items.splitAt(index)!!.right
        return Rope(suffix.splitAt(count)!!.left)
    }

    public fun splitAt(index: Int): Pair<Rope<T>, Rope<T>>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        return Rope(split.left) to Rope(split.right)
    }

    public fun concat(other: Rope<T>): Rope<T> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> Rope(items.concat(other.items))
        }

    public fun compact(): Rope<T> = this

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: Rope<T>): Boolean = items.sharesStorageWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.debugIsBalanced()

    override fun iterator(): Iterator<T> = items.iterator()
}

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

public class MeasuredRope<T, M> private constructor(
    private val items: PersistentMeasuredTree<T, M>,
    public val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    public companion object {
        public fun <T, M> empty(policy: MeasurePolicy<T, M>): MeasuredRope<T, M> =
            MeasuredRope(PersistentMeasuredTree.empty(policy), policy)

        public fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredRope<T, M> =
            MeasuredRope(PersistentMeasuredTree.from(values, policy), policy)

        public fun <T, M> fromChunks(
            chunks: Iterable<Iterable<T>>,
            policy: MeasurePolicy<T, M>,
        ): MeasuredRope<T, M> = MeasuredRope(PersistentMeasuredTree.from(chunks.flatten(), policy), policy)
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun measure(): M = items.measure()

    public operator fun get(index: Int): T? = items[index]

    public fun prefixMeasure(count: Int): M? {
        if (count < 0 || count > size) {
            return null
        }

        return items.prefixMeasure(count)
    }

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

    public fun pushBack(value: T): MeasuredRope<T, M> = MeasuredRope(items.append(value), policy)

    public fun setItem(index: Int, value: T): MeasuredRope<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        // Always store the supplied element (the C# reference replaces
        // unconditionally, even for an equal value).
        return MeasuredRope(items.setItem(index, value)!!, policy)
    }

    public fun splitAt(index: Int): MeasuredRopeSplit<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        return MeasuredRopeSplit(MeasuredRope(split.first, policy), MeasuredRope(split.second, policy))
    }

    public fun splitByMeasure(predicate: (M) -> Boolean): MeasuredRopeSplit<T, M> {
        val split = items.splitByMeasure(predicate)
        return MeasuredRopeSplit(MeasuredRope(split.first, policy), MeasuredRope(split.second, policy))
    }

    public fun locateByMeasure(predicate: (M) -> Boolean): MeasuredRopeLocate<T, M> {
        val located = items.locate(predicate)
        return MeasuredRopeLocate(located.index, located.measureBefore, located.value, located.found)
    }

    public fun concat(other: MeasuredRope<T, M>): MeasuredRope<T, M> {
        require(policy === other.policy || policy == other.policy) { "Cannot concatenate ropes with different measure policies." }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> MeasuredRope(items.concat(other.items), policy)
        }
    }

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: MeasuredRope<T, M>): Boolean = items.sharesStructureWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.isBalanced()

    override fun iterator(): Iterator<T> = items.iterator()
}

public object NewlineMeasure : MeasurePolicy<Char, Int> {
    override val empty: Int = 0
    override fun measure(element: Char): Int = if (element == '\n') 1 else 0
    override fun combine(left: Int, right: Int): Int = left + right
}

public data class LineColumn(public val line: Int, public val column: Int)

public class TextRope private constructor(
    private val characters: MeasuredRope<Char, Int>,
) {
    public companion object {
        public fun empty(): TextRope = TextRope(MeasuredRope.empty(NewlineMeasure))
        public fun fromText(text: String): TextRope = TextRope(MeasuredRope.from(text.asIterable(), NewlineMeasure))
    }

    public val size: Int
        get() = characters.size

    public val isEmpty: Boolean
        get() = characters.isEmpty

    public fun asString(): String = characters.toList().joinToString("")

    public fun lineCount(): Int = characters.measure() + 1

    public fun lineOfOffset(offset: Int): Int? {
        if (offset < 0 || offset > size) {
            return null
        }

        return characters.prefixMeasure(offset)
    }

    public fun lineStartOffset(line: Int): Int? {
        if (line < 0 || line >= lineCount()) {
            return null
        }

        if (line == 0) {
            return 0
        }

        val newline = characters.locateByMeasure { it >= line }
        return if (newline.value == null) null else newline.index + 1
    }

    public fun lineColumnOf(offset: Int): LineColumn? {
        val line = lineOfOffset(offset) ?: return null
        val start = lineStartOffset(line) ?: return null
        return LineColumn(line, offset - start)
    }

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

    public fun getLine(line: Int): String? {
        val start = lineStartOffset(line) ?: return null
        val end = lineEndOffset(line) ?: return null
        val suffix = characters.splitAt(start)!!.right
        return suffix.splitAt(end - start)!!.left.toList().joinToString("")
    }

    public fun lines(): List<String> = (0 until lineCount()).mapNotNull { getLine(it) }

    public fun toCharRope(): Rope<Char> = Rope.from(characters)

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

public class RopeBuilder {
    private val builder = StringBuilder()

    public val size: Int
        get() = builder.length

    public val isEmpty: Boolean
        get() = builder.isEmpty()

    public fun asString(): String = builder.toString()

    public fun append(text: String): RopeBuilder {
        builder.append(text)
        return this
    }

    public fun appendChar(value: Char): RopeBuilder {
        builder.append(value)
        return this
    }

    public fun appendLine(text: String): RopeBuilder {
        builder.append(text).append('\n')
        return this
    }

    public fun clear(): RopeBuilder {
        builder.clear()
        return this
    }

    public fun toRope(): Rope<Char> = Rope.fromText(builder.toString())

    public fun toTextRope(): TextRope = TextRope.fromText(builder.toString())

    override fun toString(): String = builder.toString()
}
