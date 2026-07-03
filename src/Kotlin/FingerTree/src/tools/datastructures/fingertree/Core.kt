package tools.datastructures.fingertree

public interface MeasurePolicy<T, M> {
    public val empty: M
    public fun measure(element: T): M
    public fun combine(left: M, right: M): M
}

public class SizeMeasure<T> : MeasurePolicy<T, Int> {
    override val empty: Int = 0
    override fun measure(element: T): Int = 1
    override fun combine(left: Int, right: Int): Int = left + right
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
    private val items: List<T>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): PersistentDeque<T> = PersistentDeque(emptyList())

        public fun <T> from(values: Iterable<T>): PersistentDeque<T> =
            PersistentDeque(values.toList())
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty()

    public fun front(): T? = items.firstOrNull()

    public fun back(): T? = items.lastOrNull()

    public operator fun get(index: Int): T? = items.getOrNull(index)

    public fun prepend(value: T): PersistentDeque<T> = PersistentDeque(listOf(value) + items)

    public fun append(value: T): PersistentDeque<T> = PersistentDeque(items + value)

    public fun concat(other: PersistentDeque<T>): PersistentDeque<T> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> PersistentDeque(items + other.items)
        }

    public fun splitAt(index: Int): DequeSplit<T>? {
        if (index < 0 || index > size) {
            return null
        }

        return DequeSplit(PersistentDeque(items.take(index)), PersistentDeque(items.drop(index)))
    }

    public fun splitItemAt(index: Int): DequeItemSplit<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        return DequeItemSplit(
            PersistentDeque(items.take(index)),
            items[index],
            PersistentDeque(items.drop(index + 1)),
        )
    }

    public fun splitRange(start: Int, count: Int): DequeRangeSplit<T>? {
        if (start < 0 || count < 0 || start + count > size) {
            return null
        }

        return DequeRangeSplit(
            PersistentDeque(items.take(start)),
            PersistentDeque(items.drop(start).take(count)),
            PersistentDeque(items.drop(start + count)),
        )
    }

    public fun insertAt(index: Int, value: T): PersistentDeque<T>? {
        if (index < 0 || index > size) {
            return null
        }

        return PersistentDeque(items.take(index) + value + items.drop(index))
    }

    public fun setItem(index: Int, value: T): PersistentDeque<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        if (items[index] == value) {
            return this
        }

        val next = items.toMutableList()
        next[index] = value
        return PersistentDeque(next.toList())
    }

    public fun removeAt(index: Int): PersistentDeque<T>? {
        val split = splitItemAt(index) ?: return null
        return split.left.concat(split.right)
    }

    public fun tryViewLeft(): DequePop<T>? =
        if (isEmpty) null else DequePop(items.first(), PersistentDeque(items.drop(1)))

    public fun tryViewRight(): DequePop<T>? =
        if (isEmpty) null else DequePop(items.last(), PersistentDeque(items.dropLast(1)))

    public fun reverse(): PersistentDeque<T> =
        if (size <= 1) this else PersistentDeque(items.asReversed().toList())

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: PersistentDeque<T>): Boolean = items === other.items

    override fun iterator(): Iterator<T> = items.iterator()
}

public class ReversibleDeque<T> private constructor(
    private val deque: PersistentDeque<T>,
    private val reversed: Boolean,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): ReversibleDeque<T> = ReversibleDeque(PersistentDeque.empty(), false)

        public fun <T> from(values: Iterable<T>): ReversibleDeque<T> =
            ReversibleDeque(PersistentDeque.from(values), false)
    }

    public val size: Int
        get() = deque.size

    public val isEmpty: Boolean
        get() = deque.isEmpty

    public fun front(): T? = if (reversed) deque.back() else deque.front()

    public fun back(): T? = if (reversed) deque.front() else deque.back()

    public operator fun get(index: Int): T? =
        if (index < 0 || index >= size) null else deque[if (reversed) size - index - 1 else index]

    public fun reverse(): ReversibleDeque<T> = ReversibleDeque(deque, !reversed)

    public fun prepend(value: T): ReversibleDeque<T> =
        ReversibleDeque(if (reversed) deque.append(value) else deque.prepend(value), reversed)

    public fun append(value: T): ReversibleDeque<T> =
        ReversibleDeque(if (reversed) deque.prepend(value) else deque.append(value), reversed)

    public fun concat(other: ReversibleDeque<T>): ReversibleDeque<T> =
        from(toList() + other.toList())

    public fun splitAt(index: Int): Pair<ReversibleDeque<T>, ReversibleDeque<T>>? {
        if (index < 0 || index > size) {
            return null
        }

        val values = toList()
        return from(values.take(index)) to from(values.drop(index))
    }

    public fun tryViewLeft(): Pair<T, ReversibleDeque<T>>? =
        front()?.let { it to from(toList().drop(1)) }

    public fun tryViewRight(): Pair<T, ReversibleDeque<T>>? =
        back()?.let { it to from(toList().dropLast(1)) }

    public fun toList(): List<T> {
        val values = deque.toList()
        return if (reversed) values.asReversed().toList() else values
    }

    public fun sharesStorageWith(other: ReversibleDeque<T>): Boolean =
        deque.sharesStorageWith(other.deque)

    override fun iterator(): Iterator<T> = toList().iterator()
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

public data class LocateResult<T, M>(
    public val index: Int,
    public val measureBefore: M,
    public val item: T?,
)

public class FingerTree<T, M> private constructor(
    private val items: List<T>,
    public val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    public companion object {
        public fun <T, M> empty(policy: MeasurePolicy<T, M>): FingerTree<T, M> =
            FingerTree(emptyList(), policy)

        public fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): FingerTree<T, M> =
            FingerTree(values.toList(), policy)
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty()

    public fun measure(): M = items.fold(policy.empty) { total, item ->
        policy.combine(total, policy.measure(item))
    }

    public fun front(): T? = items.firstOrNull()

    public fun back(): T? = items.lastOrNull()

    public operator fun get(index: Int): T? = items.getOrNull(index)

    public fun prepend(value: T): FingerTree<T, M> = FingerTree(listOf(value) + items, policy)

    public fun append(value: T): FingerTree<T, M> = FingerTree(items + value, policy)

    public fun concat(other: FingerTree<T, M>): FingerTree<T, M> {
        require(policy === other.policy || policy == other.policy) { "Cannot concatenate trees with different measure policies." }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> FingerTree(items + other.items, policy)
        }
    }

    public fun split(predicate: (M) -> Boolean): MeasuredSplit<T, M> {
        var prefix = policy.empty
        for (index in items.indices) {
            prefix = policy.combine(prefix, policy.measure(items[index]))
            if (predicate(prefix)) {
                return MeasuredSplit(FingerTree(items.take(index), policy), FingerTree(items.drop(index), policy))
            }
        }

        return MeasuredSplit(this, FingerTree(emptyList(), policy))
    }

    public fun splitAtIndex(index: Int): MeasuredSplit<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        return MeasuredSplit(FingerTree(items.take(index), policy), FingerTree(items.drop(index), policy))
    }

    public fun trySplitFind(predicate: (M) -> Boolean): MeasuredItemSplit<T, M>? {
        val split = split(predicate)
        val item = split.right.front() ?: return null
        return MeasuredItemSplit(split.left, item, FingerTree(split.right.items.drop(1), policy))
    }

    public fun prefixMeasure(count: Int): M? {
        if (count < 0 || count > size) {
            return null
        }

        return items.take(count).fold(policy.empty) { total, item ->
            policy.combine(total, policy.measure(item))
        }
    }

    public fun tryLocate(predicate: (M) -> Boolean): LocateResult<T, M> {
        var before = policy.empty
        for (index in items.indices) {
            val after = policy.combine(before, policy.measure(items[index]))
            if (predicate(after)) {
                return LocateResult(index, before, items[index])
            }

            before = after
        }

        return LocateResult(size, before, null)
    }

    public fun setItem(index: Int, value: T): FingerTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        if (items[index] == value) {
            return this
        }

        val next = items.toMutableList()
        next[index] = value
        return FingerTree(next.toList(), policy)
    }

    public fun insertAt(index: Int, value: T): FingerTree<T, M>? {
        if (index < 0 || index > size) {
            return null
        }

        return FingerTree(items.take(index) + value + items.drop(index), policy)
    }

    public fun removeAt(index: Int): FingerTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        return FingerTree(items.take(index) + items.drop(index + 1), policy)
    }

    public fun tryViewLeft(): Pair<T, FingerTree<T, M>>? =
        front()?.let { it to FingerTree(items.drop(1), policy) }

    public fun tryViewRight(): Pair<T, FingerTree<T, M>>? =
        back()?.let { it to FingerTree(items.dropLast(1), policy) }

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: FingerTree<T, M>): Boolean = items === other.items

    override fun iterator(): Iterator<T> = items.iterator()
}
