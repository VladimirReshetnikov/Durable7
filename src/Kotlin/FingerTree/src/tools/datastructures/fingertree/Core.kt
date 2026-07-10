package tools.datastructures.fingertree

internal fun isValidRange(start: Int, count: Int, size: Int): Boolean =
    start >= 0 && count >= 0 && start <= size && count <= size - start

public interface MeasurePolicy<T, M> {
    public val empty: M
    public fun measure(element: T): M
    public fun combine(left: M, right: M): M
}

public class SizeMeasure<T> : MeasurePolicy<T, Int> {
    override val empty: Int = 0
    override fun measure(element: T): Int = 1
    override fun combine(left: Int, right: Int): Int = left + right

    // Stateless: two instances are interchangeable, so concat's policy
    // compatibility check must accept distinct instances.
    override fun equals(other: Any?): Boolean = other is SizeMeasure<*>
    override fun hashCode(): Int = SizeMeasure::class.hashCode()
}

public object IntSumMeasure : MeasurePolicy<Int, Int> {
    override val empty: Int = 0
    override fun measure(element: Int): Int = element
    override fun combine(left: Int, right: Int): Int = left + right
}

public class MaxMeasure<T : Comparable<T>> : MeasurePolicy<T, T?> {
    override val empty: T? = null
    override fun measure(element: T): T = element
    override fun combine(left: T?, right: T?): T? =
        when {
            left == null -> right
            right == null -> left
            left >= right -> left
            else -> right
        }

    override fun equals(other: Any?): Boolean = other is MaxMeasure<*>
    override fun hashCode(): Int = MaxMeasure::class.hashCode()
}

public class MinMeasure<T : Comparable<T>> : MeasurePolicy<T, T?> {
    override val empty: T? = null
    override fun measure(element: T): T = element
    override fun combine(left: T?, right: T?): T? =
        when {
            left == null -> right
            right == null -> left
            left <= right -> left
            else -> right
        }

    override fun equals(other: Any?): Boolean = other is MinMeasure<*>
    override fun hashCode(): Int = MinMeasure::class.hashCode()
}

public data class MeasurePair<A, B>(public val first: A, public val second: B)

public class ProductMeasure<T, A, B>(
    private val first: MeasurePolicy<T, A>,
    private val second: MeasurePolicy<T, B>,
) : MeasurePolicy<T, MeasurePair<A, B>> {
    override val empty: MeasurePair<A, B> = MeasurePair(first.empty, second.empty)

    override fun measure(element: T): MeasurePair<A, B> =
        MeasurePair(first.measure(element), second.measure(element))

    override fun combine(left: MeasurePair<A, B>, right: MeasurePair<A, B>): MeasurePair<A, B> =
        MeasurePair(first.combine(left.first, right.first), second.combine(left.second, right.second))

    override fun equals(other: Any?): Boolean =
        other is ProductMeasure<*, *, *> && first == other.first && second == other.second

    override fun hashCode(): Int = 31 * first.hashCode() + second.hashCode()
}

public data class DequeSplit<T>(
    public val left: PersistentDeque<T>,
    public val right: PersistentDeque<T>,
)

public data class DequeItemSplit<T>(
    public val left: PersistentDeque<T>,
    public val item: T,
    public val right: PersistentDeque<T>,
)

public data class DequeRangeSplit<T>(
    public val before: PersistentDeque<T>,
    public val range: PersistentDeque<T>,
    public val after: PersistentDeque<T>,
)

public data class DequePop<T>(
    public val value: T,
    public val rest: PersistentDeque<T>,
)

public class PersistentDeque<T> private constructor(
    private val items: PersistentMeasuredTree<T, Int>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): PersistentDeque<T> = PersistentDeque(PersistentMeasuredTree.empty(SizeMeasure()))

        public fun <T> from(values: Iterable<T>): PersistentDeque<T> =
            PersistentDeque(PersistentMeasuredTree.from(values, SizeMeasure()))
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun front(): T? = items.front()

    public fun back(): T? = items.back()

    public operator fun get(index: Int): T? = items[index]

    public fun prepend(value: T): PersistentDeque<T> = PersistentDeque(items.prepend(value))

    public fun append(value: T): PersistentDeque<T> = PersistentDeque(items.append(value))

    public fun concat(other: PersistentDeque<T>): PersistentDeque<T> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> PersistentDeque(items.concat(other.items))
        }

    public fun splitAt(index: Int): DequeSplit<T>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        return DequeSplit(PersistentDeque(split.first), PersistentDeque(split.second))
    }

    public fun splitItemAt(index: Int): DequeItemSplit<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        return DequeItemSplit(
            splitAt(index)!!.left,
            itemAt(index),
            splitAt(index + 1)!!.right,
        )
    }

    public fun splitRange(start: Int, count: Int): DequeRangeSplit<T>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        val first = items.splitAt(start)!!
        val second = first.second.splitAt(count)!!
        return DequeRangeSplit(PersistentDeque(first.first), PersistentDeque(second.first), PersistentDeque(second.second))
    }

    public fun insertAt(index: Int, value: T): PersistentDeque<T>? {
        if (index < 0 || index > size) {
            return null
        }

        return PersistentDeque(items.insertAt(index, value)!!)
    }

    public fun setItem(index: Int, value: T): PersistentDeque<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        // Always store the supplied element (the C# reference replaces
        // unconditionally, even for an equal value).
        return PersistentDeque(items.setItem(index, value)!!)
    }

    public fun removeAt(index: Int): PersistentDeque<T>? {
        val split = splitItemAt(index) ?: return null
        return split.left.concat(split.right)
    }

    public fun tryViewLeft(): DequePop<T>? =
        if (isEmpty) null else DequePop(itemAt(0), splitAt(1)!!.right)

    public fun tryViewRight(): DequePop<T>? =
        if (isEmpty) null else DequePop(itemAt(size - 1), splitAt(size - 1)!!.left)

    public fun reverse(): PersistentDeque<T> =
        if (size <= 1) this else from(toList().asReversed())

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: PersistentDeque<T>): Boolean = items.sharesStructureWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.isBalanced()

    override fun iterator(): Iterator<T> = items.iterator()

    internal fun iteratorFrom(startIndex: Int): Iterator<T> = items.iteratorFrom(startIndex)

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = items[index] as T
}

private const val ReversibleDequeLeafCapacity = 32

private sealed class ReversibleDequeNode<T> {
    abstract val size: Int
    abstract val height: Int
    abstract val storageToken: Any

    val isEmpty: Boolean
        get() = size == 0

    abstract fun front(): T?
    abstract fun back(): T?
    abstract fun get(index: Int): T
    abstract fun reverse(): ReversibleDequeNode<T>
    abstract fun splitAt(index: Int): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>>
    abstract fun appendTo(destination: MutableList<T>)
    abstract fun appendToReversed(destination: MutableList<T>)
    abstract fun iterator(): Iterator<T>
    abstract fun reversedIterator(): Iterator<T>

    open fun logicalParts(): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>>? = null
}

private object EmptyReversibleDequeNode : ReversibleDequeNode<Nothing>() {
    override val size: Int = 0
    override val height: Int = 0
    override val storageToken: Any = this

    override fun front(): Nothing? = null

    override fun back(): Nothing? = null

    override fun get(index: Int): Nothing = throw IndexOutOfBoundsException(index.toString())

    override fun reverse(): ReversibleDequeNode<Nothing> = this

    override fun splitAt(index: Int): Pair<ReversibleDequeNode<Nothing>, ReversibleDequeNode<Nothing>> {
        require(index == 0) { "Split index out of range." }
        return this to this
    }

    override fun appendTo(destination: MutableList<Nothing>) {
    }

    override fun appendToReversed(destination: MutableList<Nothing>) {
    }

    override fun iterator(): Iterator<Nothing> = emptyList<Nothing>().iterator()

    override fun reversedIterator(): Iterator<Nothing> = emptyList<Nothing>().iterator()
}

private class ReversibleDequeLeafNode<T>(
    private val values: List<T>,
) : ReversibleDequeNode<T>() {
    override val size: Int = values.size
    override val height: Int = 1
    override val storageToken: Any = values

    override fun front(): T? = values.firstOrNull()

    override fun back(): T? = values.lastOrNull()

    override fun get(index: Int): T = values[index]

    override fun reverse(): ReversibleDequeNode<T> =
        if (size <= 1) this else ReversedReversibleDequeNode(this)

    override fun splitAt(index: Int): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>> {
        require(index in 0..size) { "Split index out of range." }
        if (index == 0) {
            return emptyReversibleDequeNode<T>() to this
        }

        if (index == size) {
            return this to emptyReversibleDequeNode<T>()
        }

        return reversibleNodeFromSmallList(values.subList(0, index)) to
            reversibleNodeFromSmallList(values.subList(index, size))
    }

    override fun appendTo(destination: MutableList<T>) {
        destination.addAll(values)
    }

    override fun appendToReversed(destination: MutableList<T>) {
        for (index in values.indices.reversed()) {
            destination.add(values[index])
        }
    }

    override fun iterator(): Iterator<T> = values.iterator()

    override fun reversedIterator(): Iterator<T> = values.asReversed().iterator()
}

private class ReversibleDequeConcatNode<T>(
    private val left: ReversibleDequeNode<T>,
    private val right: ReversibleDequeNode<T>,
) : ReversibleDequeNode<T>() {
    override val size: Int = left.size + right.size
    override val height: Int = maxOf(left.height, right.height) + 1
    override val storageToken: Any = this

    override fun front(): T? = left.front()

    override fun back(): T? = right.back()

    override fun get(index: Int): T =
        if (index < left.size) left.get(index) else right.get(index - left.size)

    override fun reverse(): ReversibleDequeNode<T> = ReversedReversibleDequeNode(this)

    override fun splitAt(index: Int): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>> {
        require(index in 0..size) { "Split index out of range." }
        return when {
            index == 0 -> emptyReversibleDequeNode<T>() to this
            index == size -> this to emptyReversibleDequeNode()
            index < left.size -> {
                val split = left.splitAt(index)
                split.first to concatReversibleDequeNodes(split.second, right)
            }
            index == left.size -> left to right
            else -> {
                val split = right.splitAt(index - left.size)
                concatReversibleDequeNodes(left, split.first) to split.second
            }
        }
    }

    override fun appendTo(destination: MutableList<T>) {
        left.appendTo(destination)
        right.appendTo(destination)
    }

    override fun appendToReversed(destination: MutableList<T>) {
        right.appendToReversed(destination)
        left.appendToReversed(destination)
    }

    override fun iterator(): Iterator<T> = sequence {
        yieldAll(left.iterator())
        yieldAll(right.iterator())
    }.iterator()

    override fun reversedIterator(): Iterator<T> = sequence {
        yieldAll(right.reversedIterator())
        yieldAll(left.reversedIterator())
    }.iterator()

    override fun logicalParts(): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>> = left to right
}

private class ReversedReversibleDequeNode<T>(
    private val source: ReversibleDequeNode<T>,
) : ReversibleDequeNode<T>() {
    override val size: Int = source.size
    override val height: Int = source.height
    override val storageToken: Any = source.storageToken

    override fun front(): T? = source.back()

    override fun back(): T? = source.front()

    override fun get(index: Int): T = source.get(size - index - 1)

    override fun reverse(): ReversibleDequeNode<T> = source

    override fun splitAt(index: Int): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>> {
        require(index in 0..size) { "Split index out of range." }
        val split = source.splitAt(size - index)
        return split.second.reverse() to split.first.reverse()
    }

    override fun appendTo(destination: MutableList<T>) {
        source.appendToReversed(destination)
    }

    override fun appendToReversed(destination: MutableList<T>) {
        source.appendTo(destination)
    }

    override fun iterator(): Iterator<T> = source.reversedIterator()

    override fun reversedIterator(): Iterator<T> = source.iterator()

    override fun logicalParts(): Pair<ReversibleDequeNode<T>, ReversibleDequeNode<T>>? {
        val parts = source.logicalParts() ?: return null
        return parts.second.reverse() to parts.first.reverse()
    }
}

@Suppress("UNCHECKED_CAST")
private fun <T> emptyReversibleDequeNode(): ReversibleDequeNode<T> =
    EmptyReversibleDequeNode as ReversibleDequeNode<T>

private fun <T> reversibleNodeFromSmallList(values: List<T>): ReversibleDequeNode<T> =
    if (values.isEmpty()) {
        emptyReversibleDequeNode()
    } else {
        ReversibleDequeLeafNode(values.toList())
    }

private fun <T> reversibleNodeFromValues(values: Iterable<T>): ReversibleDequeNode<T> {
    val nodes = mutableListOf<ReversibleDequeNode<T>>()
    var chunk = ArrayList<T>(ReversibleDequeLeafCapacity)
    for (value in values) {
        chunk.add(value)
        if (chunk.size == ReversibleDequeLeafCapacity) {
            nodes.add(ReversibleDequeLeafNode(chunk.toList()))
            chunk = ArrayList(ReversibleDequeLeafCapacity)
        }
    }

    if (chunk.isNotEmpty()) {
        nodes.add(ReversibleDequeLeafNode(chunk.toList()))
    }

    return buildBalancedReversibleDequeNodes(nodes, 0, nodes.size)
}

private fun <T> buildBalancedReversibleDequeNodes(
    nodes: List<ReversibleDequeNode<T>>,
    start: Int,
    end: Int,
): ReversibleDequeNode<T> =
    when (end - start) {
        0 -> emptyReversibleDequeNode()
        1 -> nodes[start]
        else -> {
            val middle = start + (end - start) / 2
            concatReversibleDequeNodes(
                buildBalancedReversibleDequeNodes(nodes, start, middle),
                buildBalancedReversibleDequeNodes(nodes, middle, end),
            )
        }
    }

private fun <T> concatReversibleDequeNodes(
    left: ReversibleDequeNode<T>,
    right: ReversibleDequeNode<T>,
): ReversibleDequeNode<T> {
    if (left.isEmpty) {
        return right
    }

    if (right.isEmpty) {
        return left
    }

    if (left.height > right.height + 1) {
        val parts = left.logicalParts()
        if (parts != null) {
            return rebalanceReversibleDequeNodes(
                parts.first,
                concatReversibleDequeNodes(parts.second, right),
            )
        }
    }

    if (right.height > left.height + 1) {
        val parts = right.logicalParts()
        if (parts != null) {
            return rebalanceReversibleDequeNodes(
                concatReversibleDequeNodes(left, parts.first),
                parts.second,
            )
        }
    }

    return makeReversibleDequeConcatNode(left, right)
}

private fun <T> makeReversibleDequeConcatNode(
    left: ReversibleDequeNode<T>,
    right: ReversibleDequeNode<T>,
): ReversibleDequeNode<T> =
    when {
        left.isEmpty -> right
        right.isEmpty -> left
        else -> ReversibleDequeConcatNode(left, right)
    }

private fun <T> rebalanceReversibleDequeNodes(
    left: ReversibleDequeNode<T>,
    right: ReversibleDequeNode<T>,
): ReversibleDequeNode<T> {
    if (left.height > right.height + 1) {
        val parts = left.logicalParts() ?: return makeReversibleDequeConcatNode(left, right)
        val innerParts = parts.second.logicalParts()
        if (innerParts != null && parts.second.height > parts.first.height) {
            return makeReversibleDequeConcatNode(
                makeReversibleDequeConcatNode(parts.first, innerParts.first),
                makeReversibleDequeConcatNode(innerParts.second, right),
            )
        }

        return makeReversibleDequeConcatNode(
            parts.first,
            makeReversibleDequeConcatNode(parts.second, right),
        )
    }

    if (right.height > left.height + 1) {
        val parts = right.logicalParts() ?: return makeReversibleDequeConcatNode(left, right)
        val innerParts = parts.first.logicalParts()
        if (innerParts != null && parts.first.height > parts.second.height) {
            return makeReversibleDequeConcatNode(
                makeReversibleDequeConcatNode(left, innerParts.first),
                makeReversibleDequeConcatNode(innerParts.second, parts.second),
            )
        }

        return makeReversibleDequeConcatNode(
            makeReversibleDequeConcatNode(left, parts.first),
            parts.second,
        )
    }

    return makeReversibleDequeConcatNode(left, right)
}

public class ReversibleDeque<T> private constructor(
    private val root: ReversibleDequeNode<T>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): ReversibleDeque<T> = ReversibleDeque(emptyReversibleDequeNode())

        public fun <T> from(values: Iterable<T>): ReversibleDeque<T> =
            ReversibleDeque(reversibleNodeFromValues(values))
    }

    public val size: Int
        get() = root.size

    public val isEmpty: Boolean
        get() = root.isEmpty

    public fun front(): T? = root.front()

    public fun back(): T? = root.back()

    public operator fun get(index: Int): T? =
        if (index < 0 || index >= size) null else root.get(index)

    public fun reverse(): ReversibleDeque<T> = ReversibleDeque(root.reverse())

    public fun prepend(value: T): ReversibleDeque<T> =
        ReversibleDeque(concatReversibleDequeNodes(reversibleNodeFromSmallList(listOf(value)), root))

    public fun append(value: T): ReversibleDeque<T> =
        ReversibleDeque(concatReversibleDequeNodes(root, reversibleNodeFromSmallList(listOf(value))))

    public fun concat(other: ReversibleDeque<T>): ReversibleDeque<T> =
        ReversibleDeque(concatReversibleDequeNodes(root, other.root))

    public fun splitAt(index: Int): Pair<ReversibleDeque<T>, ReversibleDeque<T>>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = root.splitAt(index)
        return ReversibleDeque(split.first) to ReversibleDeque(split.second)
    }

    public fun tryViewLeft(): Pair<T, ReversibleDeque<T>>? {
        if (isEmpty) {
            return null
        }

        val split = root.splitAt(1)
        return root.get(0) to ReversibleDeque(split.second)
    }

    public fun tryViewRight(): Pair<T, ReversibleDeque<T>>? {
        if (isEmpty) {
            return null
        }

        val split = root.splitAt(size - 1)
        return root.get(size - 1) to ReversibleDeque(split.first)
    }

    public fun toList(): List<T> {
        val values = ArrayList<T>(size)
        root.appendTo(values)
        return values
    }

    public fun sharesStorageWith(other: ReversibleDeque<T>): Boolean =
        root.storageToken === other.root.storageToken

    override fun iterator(): Iterator<T> = root.iterator()
}

public data class MeasuredSplit<T, M>(
    public val left: FingerTree<T, M>,
    public val right: FingerTree<T, M>,
)

public data class MeasuredItemSplit<T, M>(
    public val left: FingerTree<T, M>,
    public val item: T,
    public val right: FingerTree<T, M>,
)

/**
 * The result of [FingerTree.tryLocate]. [found] reports whether the predicate
 * selected a stored element; when the tree stores null elements it is the only
 * reliable discriminator, because [item] is null both for a stored null and
 * for a predicate that never became true (matching the C# reference, whose
 * TryLocate returns a boolean alongside the out parameters).
 */
public data class LocateResult<T, M>(
    public val index: Int,
    public val measureBefore: M,
    public val item: T?,
    public val found: Boolean,
)

public class FingerTree<T, M> private constructor(
    private val items: PersistentMeasuredTree<T, M>,
    public val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    public companion object {
        public fun <T, M> empty(policy: MeasurePolicy<T, M>): FingerTree<T, M> =
            FingerTree(PersistentMeasuredTree.empty(policy), policy)

        public fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): FingerTree<T, M> =
            FingerTree(PersistentMeasuredTree.from(values, policy), policy)
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun measure(): M = items.measure()

    public fun front(): T? = items.front()

    public fun back(): T? = items.back()

    public operator fun get(index: Int): T? = items[index]

    public fun prepend(value: T): FingerTree<T, M> = FingerTree(items.prepend(value), policy)

    public fun append(value: T): FingerTree<T, M> = FingerTree(items.append(value), policy)

    public fun concat(other: FingerTree<T, M>): FingerTree<T, M> {
        require(policy === other.policy || policy == other.policy) { "Cannot concatenate trees with different measure policies." }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> FingerTree(items.concat(other.items), policy)
        }
    }

    public fun split(predicate: (M) -> Boolean): MeasuredSplit<T, M> {
        val split = items.splitByMeasure(predicate)
        return MeasuredSplit(FingerTree(split.first, policy), FingerTree(split.second, policy))
    }

    public fun splitAtIndex(index: Int): MeasuredSplit<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = items.splitAt(index)!!
        return MeasuredSplit(FingerTree(split.first, policy), FingerTree(split.second, policy))
    }

    public fun trySplitFind(predicate: (M) -> Boolean): MeasuredItemSplit<T, M>? {
        val located = items.locate(predicate)
        if (!located.found) {
            return null
        }
        val first = items.splitAt(located.index)!!
        val second = first.second.splitAt(1)!!
        @Suppress("UNCHECKED_CAST")
        val item = located.value as T
        return MeasuredItemSplit(FingerTree(first.first, policy), item, FingerTree(second.second, policy))
    }

    public fun prefixMeasure(count: Int): M? {
        if (count < 0 || count > size) {
            return null
        }

        return items.prefixMeasure(count)
    }

    public fun tryLocate(predicate: (M) -> Boolean): LocateResult<T, M> {
        val located = items.locate(predicate)
        return LocateResult(located.index, located.measureBefore, located.value, located.found)
    }

    public fun setItem(index: Int, value: T): FingerTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        // Always store the supplied element (the C# reference replaces
        // unconditionally, even for an equal value).
        return FingerTree(items.setItem(index, value)!!, policy)
    }

    public fun insertAt(index: Int, value: T): FingerTree<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        return FingerTree(items.insertAt(index, value)!!, policy)
    }

    public fun removeAt(index: Int): FingerTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        return FingerTree(items.removeAt(index)!!, policy)
    }

    public fun tryViewLeft(): Pair<T, FingerTree<T, M>>? =
        if (isEmpty) null else itemAt(0) to splitAtIndex(1)!!.right

    public fun tryViewRight(): Pair<T, FingerTree<T, M>>? =
        if (isEmpty) null else itemAt(size - 1) to splitAtIndex(size - 1)!!.left

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: FingerTree<T, M>): Boolean = items.sharesStructureWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.isBalanced()

    override fun iterator(): Iterator<T> = items.iterator()

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = items[index] as T
}
