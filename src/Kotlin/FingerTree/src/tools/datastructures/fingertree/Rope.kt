package tools.datastructures.fingertree

public class Rope<T> private constructor(
    private val items: List<T>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): Rope<T> = Rope(emptyList())

        public fun <T> from(values: Iterable<T>): Rope<T> = Rope(values.toList())

        public fun <T> fromChunks(chunks: Iterable<Iterable<T>>): Rope<T> =
            Rope(chunks.flatMap { it.toList() })

        public fun fromText(text: String): Rope<Char> = Rope(text.toList())
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty()

    public fun front(): T? = items.firstOrNull()

    public fun back(): T? = items.lastOrNull()

    public operator fun get(index: Int): T? = items.getOrNull(index)

    public fun copyTo(index: Int, destination: MutableList<in T>): Boolean {
        if (!isValidRange(index, destination.size, size)) {
            return false
        }

        for (offset in destination.indices) {
            destination[offset] = items[index + offset]
        }

        return true
    }

    public fun pushFront(value: T): Rope<T> = Rope(listOf(value) + items)

    public fun pushBack(value: T): Rope<T> = Rope(items + value)

    public fun setItem(index: Int, value: T): Rope<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        if (items[index] == value) {
            return this
        }

        val next = items.toMutableList()
        next[index] = value
        return Rope(next.toList())
    }

    public fun insertAt(index: Int, value: T): Rope<T>? {
        if (index < 0 || index > size) {
            return null
        }

        return Rope(items.take(index) + value + items.drop(index))
    }

    public fun insertRange(index: Int, values: Iterable<T>): Rope<T>? {
        if (index < 0 || index > size) {
            return null
        }

        return Rope(items.take(index) + values.toList() + items.drop(index))
    }

    public fun removeAt(index: Int): Rope<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        return Rope(items.take(index) + items.drop(index + 1))
    }

    public fun removeRange(index: Int, count: Int): Rope<T>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        return Rope(items.take(index) + items.drop(index + count))
    }

    public fun slice(index: Int, count: Int): Rope<T>? {
        if (!isValidRange(index, count, size)) {
            return null
        }

        return Rope(items.drop(index).take(count))
    }

    public fun splitAt(index: Int): Pair<Rope<T>, Rope<T>>? {
        if (index < 0 || index > size) {
            return null
        }

        return Rope(items.take(index)) to Rope(items.drop(index))
    }

    public fun concat(other: Rope<T>): Rope<T> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> Rope(items + other.items)
        }

    public fun compact(): Rope<T> = this

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: Rope<T>): Boolean = items === other.items

    override fun iterator(): Iterator<T> = items.iterator()
}

public data class MeasuredRopeSplit<T, M>(
    public val left: MeasuredRope<T, M>,
    public val right: MeasuredRope<T, M>,
)

public data class MeasuredRopeLocate<T, M>(
    public val index: Int,
    public val measureBefore: M,
    public val value: T?,
)

public class MeasuredRope<T, M> private constructor(
    private val items: List<T>,
    public val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    public companion object {
        public fun <T, M> empty(policy: MeasurePolicy<T, M>): MeasuredRope<T, M> =
            MeasuredRope(emptyList(), policy)

        public fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): MeasuredRope<T, M> =
            MeasuredRope(values.toList(), policy)

        public fun <T, M> fromChunks(
            chunks: Iterable<Iterable<T>>,
            policy: MeasurePolicy<T, M>,
        ): MeasuredRope<T, M> = MeasuredRope(chunks.flatMap { it.toList() }, policy)
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty()

    public fun measure(): M = items.fold(policy.empty) { total, item ->
        policy.combine(total, policy.measure(item))
    }

    public operator fun get(index: Int): T? = items.getOrNull(index)

    public fun prefixMeasure(count: Int): M? {
        if (count < 0 || count > size) {
            return null
        }

        return items.take(count).fold(policy.empty) { total, item ->
            policy.combine(total, policy.measure(item))
        }
    }

    public fun copyTo(index: Int, destination: MutableList<in T>): Boolean {
        if (!isValidRange(index, destination.size, size)) {
            return false
        }

        for (offset in destination.indices) {
            destination[offset] = items[index + offset]
        }

        return true
    }

    public fun pushBack(value: T): MeasuredRope<T, M> = MeasuredRope(items + value, policy)

    public fun setItem(index: Int, value: T): MeasuredRope<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        if (items[index] == value) {
            return this
        }

        val next = items.toMutableList()
        next[index] = value
        return MeasuredRope(next.toList(), policy)
    }

    public fun splitAt(index: Int): MeasuredRopeSplit<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        return MeasuredRopeSplit(MeasuredRope(items.take(index), policy), MeasuredRope(items.drop(index), policy))
    }

    public fun splitByMeasure(predicate: (M) -> Boolean): MeasuredRopeSplit<T, M> {
        var prefix = policy.empty
        for (index in items.indices) {
            prefix = policy.combine(prefix, policy.measure(items[index]))
            if (predicate(prefix)) {
                return MeasuredRopeSplit(MeasuredRope(items.take(index), policy), MeasuredRope(items.drop(index), policy))
            }
        }

        return MeasuredRopeSplit(this, MeasuredRope(emptyList(), policy))
    }

    public fun locateByMeasure(predicate: (M) -> Boolean): MeasuredRopeLocate<T, M> {
        var before = policy.empty
        for (index in items.indices) {
            val after = policy.combine(before, policy.measure(items[index]))
            if (predicate(after)) {
                return MeasuredRopeLocate(index, before, items[index])
            }

            before = after
        }

        return MeasuredRopeLocate(size, before, null)
    }

    public fun concat(other: MeasuredRope<T, M>): MeasuredRope<T, M> {
        require(policy === other.policy || policy == other.policy) { "Cannot concatenate ropes with different measure policies." }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> MeasuredRope(items + other.items, policy)
        }
    }

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: MeasuredRope<T, M>): Boolean = items === other.items

    override fun iterator(): Iterator<T> = items.iterator()
}

public object NewlineMeasure : MeasurePolicy<Char, Int> {
    override val empty: Int = 0
    override fun measure(element: Char): Int = if (element == '\n') 1 else 0
    override fun combine(left: Int, right: Int): Int = left + right
}

public data class LineColumn(public val line: Int, public val column: Int)

public class TextRope private constructor(
    private val text: String,
) {
    public companion object {
        public fun empty(): TextRope = TextRope("")
        public fun fromText(text: String): TextRope = TextRope(text)
    }

    public val size: Int
        get() = text.length

    public val isEmpty: Boolean
        get() = text.isEmpty()

    public fun asString(): String = text

    public fun lineCount(): Int = text.count { it == '\n' } + 1

    public fun lineOfOffset(offset: Int): Int? {
        if (offset < 0 || offset > size) {
            return null
        }

        return text.take(offset).count { it == '\n' }
    }

    public fun lineStartOffset(line: Int): Int? {
        if (line < 0 || line >= lineCount()) {
            return null
        }

        if (line == 0) {
            return 0
        }

        var seen = 0
        for (index in text.indices) {
            if (text[index] == '\n') {
                seen += 1
                if (seen == line) {
                    return index + 1
                }
            }
        }

        return null
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
        val offset = start + column
        return if (offset <= end) offset else null
    }

    public fun getLine(line: Int): String? {
        val start = lineStartOffset(line) ?: return null
        val end = lineEndOffset(line) ?: return null
        return text.substring(start, end)
    }

    public fun lines(): List<String> = (0 until lineCount()).mapNotNull { getLine(it) }

    public fun toCharRope(): Rope<Char> = Rope.from(text.toList())

    public fun toMeasuredRope(): MeasuredRope<Char, Int> = MeasuredRope.from(text.toList(), NewlineMeasure)

    override fun toString(): String = text

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
