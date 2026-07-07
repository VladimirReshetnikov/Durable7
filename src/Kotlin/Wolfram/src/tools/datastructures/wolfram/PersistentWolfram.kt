package tools.datastructures.wolfram

import tools.datastructures.hamt.HashPolicy
import tools.datastructures.hamt.PersistentHashMap
import tools.datastructures.hamt.defaultHashPolicy

private const val StampGap: Long = 1L shl 20

private class SeqNode<T>(
    val left: SeqNode<T>?,
    val value: T,
    val right: SeqNode<T>?,
    val size: Int,
    val height: Int,
)

private fun <T> nodeSize(node: SeqNode<T>?): Int = node?.size ?: 0

private fun <T> nodeHeight(node: SeqNode<T>?): Int = node?.height ?: 0

private fun <T> makeNode(left: SeqNode<T>?, value: T, right: SeqNode<T>?): SeqNode<T> =
    SeqNode(left, value, right, nodeSize(left) + 1 + nodeSize(right), maxOf(nodeHeight(left), nodeHeight(right)) + 1)

private fun <T> rotateRight(node: SeqNode<T>): SeqNode<T> {
    val pivot = node.left ?: return node
    return makeNode(pivot.left, pivot.value, makeNode(pivot.right, node.value, node.right))
}

private fun <T> rotateLeft(node: SeqNode<T>): SeqNode<T> {
    val pivot = node.right ?: return node
    return makeNode(makeNode(node.left, node.value, pivot.left), pivot.value, pivot.right)
}

private fun <T> balance(node: SeqNode<T>): SeqNode<T> {
    val factor = nodeHeight(node.left) - nodeHeight(node.right)
    if (factor > 1) {
        val left = node.left ?: return node
        val adjusted = if (nodeHeight(left.right) > nodeHeight(left.left)) {
            makeNode(rotateLeft(left), node.value, node.right)
        } else {
            node
        }
        return rotateRight(adjusted)
    }

    if (factor < -1) {
        val right = node.right ?: return node
        val adjusted = if (nodeHeight(right.left) > nodeHeight(right.right)) {
            makeNode(node.left, node.value, rotateRight(right))
        } else {
            node
        }
        return rotateLeft(adjusted)
    }

    return node
}

private fun <T> buildBalanced(values: List<T>, start: Int, end: Int): SeqNode<T>? =
    when (end - start) {
        0 -> null
        1 -> makeNode(null, values[start], null)
        else -> {
            val middle = start + (end - start) / 2
            makeNode(buildBalanced(values, start, middle), values[middle], buildBalanced(values, middle + 1, end))
        }
    }

private fun <T> concatNodes(left: SeqNode<T>?, right: SeqNode<T>?): SeqNode<T>? {
    if (left == null) {
        return right
    }

    if (right == null) {
        return left
    }

    if (nodeHeight(left) > nodeHeight(right) + 1) {
        return balance(makeNode(left.left, left.value, concatNodes(left.right, right)))
    }

    if (nodeHeight(right) > nodeHeight(left) + 1) {
        return balance(makeNode(concatNodes(left, right.left), right.value, right.right))
    }

    val removed = removeFirstNode(right)
    return balance(makeNode(left, removed.first, removed.second))
}

private fun <T> splitNode(node: SeqNode<T>?, index: Int): Pair<SeqNode<T>?, SeqNode<T>?> {
    if (node == null) {
        return null to null
    }

    val leftSize = nodeSize(node.left)
    return when {
        index <= leftSize -> {
            val split = splitNode(node.left, index)
            split.first to concatNodes(split.second, makeNode(null, node.value, node.right))
        }
        index == leftSize + 1 -> makeNode(node.left, node.value, null) to node.right
        else -> {
            val split = splitNode(node.right, index - leftSize - 1)
            concatNodes(makeNode(node.left, node.value, null), split.first) to split.second
        }
    }
}

private fun <T> removeFirstNode(node: SeqNode<T>): Pair<T, SeqNode<T>?> =
    if (node.left == null) {
        node.value to node.right
    } else {
        val removed = removeFirstNode(node.left)
        removed.first to balance(makeNode(removed.second, node.value, node.right))
    }

private fun <T> getNode(node: SeqNode<T>?, index: Int): T? {
    var current = node
    var target = index
    while (current != null) {
        val leftSize = nodeSize(current.left)
        when {
            target < leftSize -> current = current.left
            target == leftSize -> return current.value
            else -> {
                target -= leftSize + 1
                current = current.right
            }
        }
    }

    return null
}

private fun <T> setNode(node: SeqNode<T>, index: Int, value: T): SeqNode<T> {
    val leftSize = nodeSize(node.left)
    return when {
        index < leftSize -> balance(makeNode(setNode(node.left!!, index, value), node.value, node.right))
        index == leftSize -> if (node.value == value) node else makeNode(node.left, value, node.right)
        else -> balance(makeNode(node.left, node.value, setNode(node.right!!, index - leftSize - 1, value)))
    }
}

private fun <T> appendToList(node: SeqNode<T>?, destination: MutableList<T>) {
    if (node == null) {
        return
    }

    appendToList(node.left, destination)
    destination.add(node.value)
    appendToList(node.right, destination)
}

private class SeqTree<T> private constructor(
    private val root: SeqNode<T>?,
) : Iterable<T> {
    companion object {
        fun <T> empty(): SeqTree<T> = SeqTree(null)

        fun <T> from(values: Iterable<T>): SeqTree<T> {
            val list = values.toList()
            return SeqTree(buildBalanced(list, 0, list.size))
        }
    }

    val size: Int
        get() = nodeSize(root)

    val isEmpty: Boolean
        get() = root == null

    fun first(): T? = get(0)

    fun last(): T? = if (size == 0) null else get(size - 1)

    operator fun get(index: Int): T? =
        if (index < 0 || index >= size) null else getNode(root, index)

    fun append(value: T): SeqTree<T> = insert(size, value)!!

    fun prepend(value: T): SeqTree<T> = insert(0, value)!!

    fun concat(other: SeqTree<T>): SeqTree<T> =
        when {
            isEmpty -> other
            other.isEmpty -> this
            else -> SeqTree(concatNodes(root, other.root))
        }

    fun insert(index: Int, value: T): SeqTree<T>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = splitNode(root, index)
        return SeqTree(concatNodes(concatNodes(split.first, makeNode(null, value, null)), split.second))
    }

    fun setItem(index: Int, value: T): SeqTree<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        val current = get(index)
        if (current == value) {
            return this
        }

        return SeqTree(setNode(root!!, index, value))
    }

    fun removeAt(index: Int): SeqTree<T>? {
        val split = splitRange(index, 1) ?: return null
        return split.first.concat(split.third)
    }

    fun splitAt(index: Int): Pair<SeqTree<T>, SeqTree<T>>? {
        if (index < 0 || index > size) {
            return null
        }

        val split = splitNode(root, index)
        return SeqTree(split.first) to SeqTree(split.second)
    }

    fun splitRange(index: Int, count: Int): Triple<SeqTree<T>, SeqTree<T>, SeqTree<T>>? {
        if (index < 0 || count < 0 || index > size || count > size - index) {
            return null
        }

        val first = splitNode(root, index)
        val second = splitNode(first.second, count)
        return Triple(SeqTree(first.first), SeqTree(second.first), SeqTree(second.second))
    }

    fun getRange(index: Int, count: Int): SeqTree<T>? = splitRange(index, count)?.second

    fun toList(): List<T> {
        val result = ArrayList<T>(size)
        appendToList(root, result)
        return result
    }

    fun binarySearch(target: T, compare: (T, T) -> Int): Int {
        var current = root
        var offset = 0
        while (current != null) {
            val leftSize = nodeSize(current.left)
            val order = compare(target, current.value)
            when {
                order == 0 -> return offset + leftSize
                order < 0 -> current = current.left
                else -> {
                    offset += leftSize + 1
                    current = current.right
                }
            }
        }

        return -1
    }

    override fun iterator(): Iterator<T> = sequence {
        yieldAll(toList())
    }.iterator()
}

public data class PersistentListSplit<T>(
    public val left: PersistentList<T>,
    public val right: PersistentList<T>,
)

public class PersistentList<T> private constructor(
    private val items: SeqTree<T>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(): PersistentList<T> = PersistentList(SeqTree.empty())

        public fun <T> from(values: Iterable<T>): PersistentList<T> = PersistentList(SeqTree.from(values))
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun first(): T? = items.first()

    public fun last(): T? = items.last()

    public operator fun get(index: Int): T? = items[index]

    public fun append(value: T): PersistentList<T> = PersistentList(items.append(value))

    public fun prepend(value: T): PersistentList<T> = PersistentList(items.prepend(value))

    public fun join(other: PersistentList<T>): PersistentList<T> = PersistentList(items.concat(other.items))

    public fun addRange(values: Iterable<T>): PersistentList<T> = join(from(values))

    public fun insert(index: Int, value: T): PersistentList<T>? = items.insert(index, value)?.let(::PersistentList)

    public fun insertRange(index: Int, values: Iterable<T>): PersistentList<T>? {
        if (index < 0 || index > size) {
            return null
        }

        val inserted = SeqTree.from(values)
        if (inserted.isEmpty) {
            return this
        }

        val split = items.splitAt(index) ?: return null
        return PersistentList(split.first.concat(inserted).concat(split.second))
    }

    public fun removeAt(index: Int): PersistentList<T>? = items.removeAt(index)?.let(::PersistentList)

    public fun removeRange(index: Int, count: Int): PersistentList<T>? {
        val split = items.splitRange(index, count) ?: return null
        return PersistentList(split.first.concat(split.third))
    }

    public fun removeFirst(): PersistentList<T>? = removeAt(0)

    public fun removeLast(): PersistentList<T>? = removeAt(size - 1)

    public fun setItem(index: Int, value: T): PersistentList<T>? = items.setItem(index, value)?.let(::PersistentList)

    public fun updateAt(index: Int, updater: (T) -> T): PersistentList<T>? {
        val current = get(index) ?: return null
        return setItem(index, updater(current))
    }

    public fun getRange(index: Int, count: Int): PersistentList<T>? = items.getRange(index, count)?.let(::PersistentList)

    public fun take(count: Int): PersistentList<T>? = getRange(0, count)

    public fun takeLast(count: Int): PersistentList<T>? =
        if (count < 0 || count > size) null else getRange(size - count, count)

    public fun drop(count: Int): PersistentList<T>? =
        if (count < 0 || count > size) null else getRange(count, size - count)

    public fun dropLast(count: Int): PersistentList<T>? =
        if (count < 0 || count > size) null else getRange(0, size - count)

    public fun splitAt(index: Int): PersistentListSplit<T>? {
        val split = items.splitAt(index) ?: return null
        return PersistentListSplit(PersistentList(split.first), PersistentList(split.second))
    }

    public fun reverse(): PersistentList<T> =
        if (size <= 1) this else from(items.toList().asReversed())

    public fun <R> map(transform: (T) -> R): PersistentList<R> = from(items.map(transform))

    public fun indexOf(value: T, equivalent: (T, T) -> Boolean = { left, right -> left == right }): Int {
        var index = 0
        for (candidate in items) {
            if (equivalent(candidate, value)) {
                return index
            }
            index++
        }

        return -1
    }

    public fun contains(value: T, equivalent: (T, T) -> Boolean = { left, right -> left == right }): Boolean =
        indexOf(value, equivalent) >= 0

    public fun toList(): List<T> = items.toList()

    override fun iterator(): Iterator<T> = items.iterator()
}

private data class Slot<V>(val stamp: Long, val value: V)

private class AssociationEntry<K, V> private constructor(
    val stamp: Long,
    private val storedKey: K?,
    private val storedValue: V?,
    private val hasPayload: Boolean,
) {
    companion object {
        fun <K, V> real(stamp: Long, key: K, value: V): AssociationEntry<K, V> =
            AssociationEntry(stamp, key, value, hasPayload = true)

        fun <K, V> probe(stamp: Long): AssociationEntry<K, V> =
            AssociationEntry(stamp, null, null, hasPayload = false)
    }

    @Suppress("UNCHECKED_CAST")
    fun key(): K {
        check(hasPayload) { "Stamp probes do not carry keys." }
        return storedKey as K
    }

    @Suppress("UNCHECKED_CAST")
    fun value(): V {
        check(hasPayload) { "Stamp probes do not carry values." }
        return storedValue as V
    }
}

public data class AssociationRemoveResult<K, V>(
    public val association: PersistentAssociation<K, V>,
    public val value: V,
)

public class PersistentAssociation<K, V> private constructor(
    private val entries: SeqTree<AssociationEntry<K, V>>,
    private val index: PersistentHashMap<K, Slot<V>>,
) : Iterable<Pair<K, V>> {
    public companion object {
        public fun <K, V> empty(policy: HashPolicy<K> = defaultHashPolicy()): PersistentAssociation<K, V> =
            PersistentAssociation(SeqTree.empty(), PersistentHashMap.empty(policy))

        public fun <K, V> from(
            pairs: Iterable<Pair<K, V>>,
            policy: HashPolicy<K> = defaultHashPolicy(),
        ): PersistentAssociation<K, V> = empty<K, V>(policy).setItems(pairs)
    }

    public val size: Int
        get() = entries.size

    public val isEmpty: Boolean
        get() = entries.isEmpty

    public val policy: HashPolicy<K>
        get() = index.policy

    public fun containsKey(key: K): Boolean = index.containsKey(key)

    public operator fun get(key: K): V? = index[key]?.value

    public fun getStoredKey(key: K): K? = index.getEntry(key)?.key

    public fun first(): Pair<K, V>? = entries.first()?.toPair()

    public fun last(): Pair<K, V>? = entries.last()?.toPair()

    public fun getAt(index: Int): Pair<K, V>? = entries[index]?.toPair()

    public fun indexOfKey(key: K): Int {
        val slot = index[key] ?: return -1
        return indexOfStamp(slot.stamp)
    }

    public fun setItem(key: K, value: V): PersistentAssociation<K, V> {
        val slot = index[key] ?: return appendNew(key, value)
        if (slot.value == value) {
            return this
        }

        val position = indexOfStamp(slot.stamp)
        val nextEntries = entries.setItem(position, AssociationEntry.real(slot.stamp, entries[position]!!.key(), value))!!
        return PersistentAssociation(nextEntries, index.put(key, Slot(slot.stamp, value)))
    }

    public fun setItems(pairs: Iterable<Pair<K, V>>): PersistentAssociation<K, V> {
        var result = this
        for ((key, value) in pairs) {
            result = result.setItem(key, value)
        }

        return result
    }

    public fun join(other: PersistentAssociation<K, V>): PersistentAssociation<K, V> =
        setItems(other)

    public fun append(key: K, value: V): PersistentAssociation<K, V> {
        val slot = index[key] ?: return appendNew(key, value)
        val position = indexOfStamp(slot.stamp)
        if (position == size - 1 && slot.value == value) {
            return this
        }

        val trimmed = entries.removeAt(position)!!
        return withEntryInserted(trimmed, index.remove(key), trimmed.size, key, value)
    }

    public fun prepend(key: K, value: V): PersistentAssociation<K, V> {
        val slot = index[key]
        if (slot != null) {
            val position = indexOfStamp(slot.stamp)
            if (position == 0 && slot.value == value) {
                return this
            }

            val trimmed = entries.removeAt(position)!!
            return withEntryInserted(trimmed, index.remove(key), 0, key, value)
        }

        return withEntryInserted(entries, index, 0, key, value)
    }

    public fun insert(position: Int, key: K, value: V): PersistentAssociation<K, V>? {
        if (position < 0 || position > size) {
            return null
        }

        var nextEntries = entries
        var nextIndex = index
        var insertAt = position
        val slot = nextIndex[key]
        if (slot != null) {
            val oldPosition = indexOfStamp(slot.stamp)
            nextEntries = nextEntries.removeAt(oldPosition)!!
            nextIndex = nextIndex.remove(key)
            if (oldPosition < insertAt) {
                insertAt--
            }
        }

        return withEntryInserted(nextEntries, nextIndex, insertAt, key, value)
    }

    public fun remove(key: K): PersistentAssociation<K, V> =
        tryRemove(key)?.association ?: this

    public fun tryRemove(key: K): AssociationRemoveResult<K, V>? {
        val removed = index.tryRemove(key) ?: return null
        val position = indexOfStamp(removed.value.stamp)
        val nextEntries = entries.removeAt(position)!!
        return AssociationRemoveResult(PersistentAssociation(nextEntries, removed.map), removed.value.value)
    }

    public fun removeRange(keys: Iterable<K>): PersistentAssociation<K, V> {
        var result = this
        for (key in keys) {
            result = result.remove(key)
        }

        return result
    }

    public fun keyTake(keys: Iterable<K>): PersistentAssociation<K, V> {
        var result = empty<K, V>(policy)
        for (key in keys) {
            val entry = index.getEntry(key)
            if (entry != null && !result.containsKey(key)) {
                result = result.appendNew(entry.key, entry.value.value)
            }
        }

        return result
    }

    public fun removeAt(index: Int): PersistentAssociation<K, V>? {
        val entry = entries[index] ?: return null
        val nextEntries = entries.removeAt(index)!!
        return PersistentAssociation(nextEntries, this.index.remove(entry.key()))
    }

    public fun removeFirst(): PersistentAssociation<K, V>? = removeAt(0)

    public fun removeLast(): PersistentAssociation<K, V>? = removeAt(size - 1)

    public fun getRange(index: Int, count: Int): PersistentAssociation<K, V>? {
        val split = entries.splitRange(index, count) ?: return null
        val kept = split.second
        if (kept.size == size) {
            return this
        }

        var nextIndex: PersistentHashMap<K, Slot<V>>
        val removedCount = size - kept.size
        if (kept.size <= removedCount) {
            nextIndex = PersistentHashMap.empty(policy)
            for (entry in kept) {
                nextIndex = nextIndex.put(entry.key(), Slot(entry.stamp, entry.value()))
            }
        } else {
            nextIndex = this.index
            for (entry in split.first) {
                nextIndex = nextIndex.remove(entry.key())
            }
            for (entry in split.third) {
                nextIndex = nextIndex.remove(entry.key())
            }
        }

        return PersistentAssociation(kept, nextIndex)
    }

    public fun take(count: Int): PersistentAssociation<K, V>? = getRange(0, count)

    public fun drop(count: Int): PersistentAssociation<K, V>? =
        if (count < 0 || count > size) null else getRange(count, size - count)

    public fun reverse(): PersistentAssociation<K, V> =
        if (size <= 1) this else rebuilt(entries.toList().asReversed())

    public fun keySort(): PersistentAssociation<K, V> {
        @Suppress("UNCHECKED_CAST")
        return keySortWith { left, right -> (left as Comparable<K>).compareTo(right) }
    }

    public fun keySortWith(comparator: Comparator<in K>): PersistentAssociation<K, V> =
        rebuilt(entries.toList().sortedWith { left, right ->
            val byKey = comparator.compare(left.key(), right.key())
            if (byKey != 0) byKey else left.stamp.compareTo(right.stamp)
        })

    public fun sort(): PersistentAssociation<K, V> {
        @Suppress("UNCHECKED_CAST")
        return sortWith { left, right -> (left as Comparable<V>).compareTo(right) }
    }

    public fun sortWith(comparator: Comparator<in V>): PersistentAssociation<K, V> =
        rebuilt(entries.toList().sortedWith { left, right ->
            val byValue = comparator.compare(left.value(), right.value())
            if (byValue != 0) byValue else left.stamp.compareTo(right.stamp)
        })

    public fun keys(): List<K> = entries.map { it.key() }

    public fun values(): List<V> = entries.map { it.value() }

    public fun toList(): List<Pair<K, V>> = entries.map { it.toPair() }

    override fun iterator(): Iterator<Pair<K, V>> = toList().iterator()

    private fun AssociationEntry<K, V>.toPair(): Pair<K, V> = key() to value()

    private fun appendNew(key: K, value: V): PersistentAssociation<K, V> =
        withEntryInserted(entries, index, entries.size, key, value)

    private fun withEntryInserted(
        sourceEntries: SeqTree<AssociationEntry<K, V>>,
        sourceIndex: PersistentHashMap<K, Slot<V>>,
        insertAt: Int,
        key: K,
        value: V,
    ): PersistentAssociation<K, V> {
        val stamp = pickStamp(sourceEntries, insertAt)
        if (stamp == null) {
            return relabeled(sourceEntries, insertAt, AssociationEntry.real(0, key, value))
        }

        val entry = AssociationEntry.real(stamp, key, value)
        val nextEntries = when (insertAt) {
            0 -> sourceEntries.prepend(entry)
            sourceEntries.size -> sourceEntries.append(entry)
            else -> sourceEntries.insert(insertAt, entry)!!
        }
        return PersistentAssociation(nextEntries, sourceIndex.put(key, Slot(stamp, value)))
    }

    private fun relabeled(
        sourceEntries: SeqTree<AssociationEntry<K, V>>,
        insertAt: Int,
        inserted: AssociationEntry<K, V>,
    ): PersistentAssociation<K, V> {
        val ordered = ArrayList<AssociationEntry<K, V>>(sourceEntries.size + 1)
        var index = 0
        for (entry in sourceEntries) {
            if (index == insertAt) {
                ordered.add(inserted)
            }
            ordered.add(entry)
            index++
        }
        if (insertAt == sourceEntries.size) {
            ordered.add(inserted)
        }
        return rebuilt(ordered)
    }

    private fun rebuilt(ordered: List<AssociationEntry<K, V>>): PersistentAssociation<K, V> {
        var nextIndex = PersistentHashMap.empty<K, Slot<V>>(policy)
        val relabeled = ordered.mapIndexed { position, entry ->
            val stamp = position.toLong() * StampGap
            val next = AssociationEntry.real(stamp, entry.key(), entry.value())
            nextIndex = nextIndex.put(next.key(), Slot(stamp, next.value()))
            next
        }
        return PersistentAssociation(SeqTree.from(relabeled), nextIndex)
    }

    private fun indexOfStamp(stamp: Long): Int {
        val found = entries.binarySearch(AssociationEntry.probe<K, V>(stamp)) { target, current ->
            target.stamp.compareTo(current.stamp)
        }
        check(found >= 0) { "Stamp recorded in keyed index is absent from association order." }
        return found
    }

    private fun pickStamp(sourceEntries: SeqTree<AssociationEntry<K, V>>, insertAt: Int): Long? {
        if (sourceEntries.isEmpty) {
            return 0
        }

        if (insertAt == 0) {
            return sourceEntries.first()!!.stamp.let { first ->
                if (first < Long.MIN_VALUE + StampGap) null else first - StampGap
            }
        }

        if (insertAt == sourceEntries.size) {
            return sourceEntries.last()!!.stamp.let { last ->
                if (last > Long.MAX_VALUE - StampGap) null else last + StampGap
            }
        }

        val left = sourceEntries[insertAt - 1]!!.stamp
        val right = sourceEntries[insertAt]!!.stamp
        if (left + 1 >= right) {
            return null
        }

        return ((left.toBigInteger() + right.toBigInteger()) / 2.toBigInteger()).toLong()
    }
}
