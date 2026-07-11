package tools.datastructures.fingertree

public class SortedDuplicateKeyException(message: String = "An equivalent key is already present.") :
    IllegalArgumentException(message)

public data class SortedMapEntry<K, V>(public val key: K, public val value: V)

public data class SortedAddResult<T>(public val value: T, public val added: Boolean)

public data class SortedMapRemoveResult<K, V>(
    public val map: SortedMap<K, V>,
    public val value: V,
)

internal fun <T : Comparable<T>> naturalComparator(): Comparator<T> = Comparator { left, right ->
    left.compareTo(right)
}

private fun <T> compare(comparator: Comparator<in T>, left: T, right: T): Int =
    comparator.compare(left, right)

private fun <T> PersistentDeque<T>.lowerBound(value: T, comparator: Comparator<in T>): Int {
    var low = 0
    var high = size
    while (low < high) {
        val mid = (low + high) ushr 1
        if (compare(comparator, itemAt(mid), value) < 0) {
            low = mid + 1
        } else {
            high = mid
        }
    }

    return low
}

private fun <T> PersistentDeque<T>.upperBound(value: T, comparator: Comparator<in T>): Int {
    var low = 0
    var high = size
    while (low < high) {
        val mid = (low + high) ushr 1
        if (compare(comparator, itemAt(mid), value) <= 0) {
            low = mid + 1
        } else {
            high = mid
        }
    }

    return low
}

public class SortedBag<T> private constructor(
    private val items: PersistentDeque<T>,
    private val comparator: Comparator<in T>,
) : Iterable<T> {
    public companion object {
        public fun <T : Comparable<T>> empty(): SortedBag<T> = SortedBag(PersistentDeque.empty(), naturalComparator())

        public fun <T : Comparable<T>> from(values: Iterable<T>): SortedBag<T> =
            empty<T>().addRange(values)

        public fun <T> empty(comparator: Comparator<in T>): SortedBag<T> =
            SortedBag(PersistentDeque.empty(), comparator)

        public fun <T> from(values: Iterable<T>, comparator: Comparator<in T>): SortedBag<T> =
            empty(comparator).addRange(values)
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun min(): T? = items.front()

    public fun max(): T? = items.back()

    public operator fun get(rank: Int): T? = items[rank]

    public fun contains(value: T): Boolean = countOf(value) > 0

    public fun countLessThan(value: T): Int = items.lowerBound(value, comparator)

    public fun countAtMost(value: T): Int = items.upperBound(value, comparator)

    public fun countOf(value: T): Int = countAtMost(value) - countLessThan(value)

    public fun add(value: T): SortedBag<T> {
        val index = countAtMost(value)
        return SortedBag(items.insertAt(index, value)!!, comparator)
    }

    public fun addRange(values: Iterable<T>): SortedBag<T> {
        var result = this
        for (value in values) {
            result = result.add(value)
        }

        return result
    }

    public fun remove(value: T): SortedBag<T> {
        val index = countLessThan(value)
        if (index == size || compare(comparator, items.itemAt(index), value) != 0) {
            return this
        }

        return SortedBag(items.removeAt(index)!!, comparator)
    }

    public fun removeAll(value: T): SortedBag<T> {
        val start = countLessThan(value)
        val end = countAtMost(value)
        if (start == end) {
            return this
        }

        val first = items.splitAt(start)!!
        val second = first.right.splitAt(end - start)!!
        return SortedBag(first.left.concat(second.right), comparator)
    }

    public fun getRange(start: Int, count: Int): SortedBag<T>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        return SortedBag(items.range(start, count), comparator)
    }

    public fun getValueRange(low: T, high: T): SortedBag<T> {
        if (compare(comparator, low, high) > 0) {
            return SortedBag(PersistentDeque.empty(), comparator)
        }

        val start = countLessThan(low)
        return SortedBag(items.range(start, countAtMost(high) - start), comparator)
    }

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: SortedBag<T>): Boolean = items.sharesStorageWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.debugIsBalanced()

    override fun iterator(): Iterator<T> = items.iterator()
}

public class SortedSet<T> private constructor(
    private val items: PersistentDeque<T>,
    private val comparator: Comparator<in T>,
) : Iterable<T> {
    public companion object {
        public fun <T : Comparable<T>> empty(): SortedSet<T> = SortedSet(PersistentDeque.empty(), naturalComparator())

        public fun <T : Comparable<T>> from(values: Iterable<T>): SortedSet<T> =
            empty<T>().union(values)

        public fun <T> empty(comparator: Comparator<in T>): SortedSet<T> =
            SortedSet(PersistentDeque.empty(), comparator)

        public fun <T> from(values: Iterable<T>, comparator: Comparator<in T>): SortedSet<T> =
            empty(comparator).union(values)
    }

    public val size: Int
        get() = items.size

    public val isEmpty: Boolean
        get() = items.isEmpty

    public fun min(): T? = items.front()

    public fun max(): T? = items.back()

    public operator fun get(rank: Int): T? = items[rank]

    public fun contains(value: T): Boolean {
        val index = items.lowerBound(value, comparator)
        return index < size && compare(comparator, items.itemAt(index), value) == 0
    }

    public fun indexOf(value: T): Int? {
        val index = items.lowerBound(value, comparator)
        return if (index < size && compare(comparator, items.itemAt(index), value) == 0) index else null
    }

    public fun add(value: T): SortedSet<T> {
        val index = items.lowerBound(value, comparator)
        if (index < size && compare(comparator, items.itemAt(index), value) == 0) {
            return this
        }

        return SortedSet(items.insertAt(index, value)!!, comparator)
    }

    public fun union(values: Iterable<T>): SortedSet<T> {
        var result = this
        for (value in values) {
            result = result.add(value)
        }

        return result
    }

    public fun remove(value: T): SortedSet<T> {
        val index = indexOf(value) ?: return this
        return SortedSet(items.removeAt(index)!!, comparator)
    }

    public fun floor(value: T): T? {
        val index = items.upperBound(value, comparator) - 1
        return if (index >= 0) items[index] else null
    }

    public fun ceiling(value: T): T? {
        val index = items.lowerBound(value, comparator)
        return if (index < size) items[index] else null
    }

    public fun lower(value: T): T? {
        val index = items.lowerBound(value, comparator) - 1
        return if (index >= 0) items[index] else null
    }

    public fun higher(value: T): T? {
        val index = items.upperBound(value, comparator)
        return if (index < size) items[index] else null
    }

    public fun getRange(start: Int, count: Int): SortedSet<T>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        return SortedSet(items.range(start, count), comparator)
    }

    public fun getValueRange(low: T, high: T): SortedSet<T> {
        if (compare(comparator, low, high) > 0) {
            return SortedSet(PersistentDeque.empty(), comparator)
        }

        val start = items.lowerBound(low, comparator)
        val end = items.upperBound(high, comparator)
        return SortedSet(items.range(start, end - start), comparator)
    }

    public fun intersect(values: Iterable<T>): SortedSet<T> {
        val other = from(values, comparator)
        return SortedSet(PersistentDeque.from(items.filter { other.contains(it) }), comparator)
    }

    public fun except(values: Iterable<T>): SortedSet<T> {
        val other = from(values, comparator)
        return SortedSet(PersistentDeque.from(items.filterNot { other.contains(it) }), comparator)
    }

    public fun symmetricExcept(values: Iterable<T>): SortedSet<T> {
        val other = from(values, comparator)
        var result = empty(comparator)
        for (value in this) {
            if (!other.contains(value)) {
                result = result.add(value)
            }
        }

        for (value in other) {
            if (!contains(value)) {
                result = result.add(value)
            }
        }

        return result
    }

    public fun isSubsetOf(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return all { other.contains(it) }
    }

    public fun isSupersetOf(values: Iterable<T>): Boolean =
        values.all { contains(it) }

    public fun isProperSubsetOf(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return size < other.size && all { other.contains(it) }
    }

    public fun isProperSupersetOf(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return size > other.size && other.all { contains(it) }
    }

    public fun overlaps(values: Iterable<T>): Boolean =
        values.any { contains(it) }

    public fun setEquals(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return size == other.size && all { other.contains(it) }
    }

    public fun toList(): List<T> = items.toList()

    public fun sharesStorageWith(other: SortedSet<T>): Boolean = items.sharesStorageWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.debugIsBalanced()

    override fun iterator(): Iterator<T> = items.iterator()
}

public class SortedMap<K, V> private constructor(
    private val entries: PersistentDeque<SortedMapEntry<K, V>>,
    private val comparator: Comparator<in K>,
) : Iterable<SortedMapEntry<K, V>> {
    public companion object {
        public fun <K : Comparable<K>, V> empty(): SortedMap<K, V> =
            SortedMap(PersistentDeque.empty(), naturalComparator())

        public fun <K : Comparable<K>, V> from(values: Iterable<Pair<K, V>>): SortedMap<K, V> {
            var result = empty<K, V>()
            for ((key, value) in values) {
                result = result.setItem(key, value)
            }

            return result
        }

        public fun <K, V> from(
            values: Iterable<Pair<K, V>>,
            comparator: Comparator<in K>,
        ): SortedMap<K, V> {
            var result = empty<K, V>(comparator)
            for ((key, value) in values) {
                result = result.setItem(key, value)
            }

            return result
        }

        public fun <K, V> empty(comparator: Comparator<in K>): SortedMap<K, V> =
            SortedMap(PersistentDeque.empty(), comparator)
    }

    public val size: Int
        get() = entries.size

    public val isEmpty: Boolean
        get() = entries.isEmpty

    public fun containsKey(key: K): Boolean = indexOfKey(key) != null

    public operator fun get(key: K): V? {
        val index = indexOfKey(key) ?: return null
        return entries.itemAt(index).value
    }

    public fun entryAt(rank: Int): SortedMapEntry<K, V>? = entries[rank]

    public fun minEntry(): SortedMapEntry<K, V>? = entries.front()

    public fun maxEntry(): SortedMapEntry<K, V>? = entries.back()

    public fun indexOfKey(key: K): Int? {
        val index = lowerBoundByKey(key)
        return if (index < size && compare(comparator, entries.itemAt(index).key, key) == 0) index else null
    }

    public fun setItem(key: K, value: V): SortedMap<K, V> {
        val index = lowerBoundByKey(key)
        if (index < size && compare(comparator, entries.itemAt(index).key, key) == 0) {
            // The C# reference (SortedDictionary.SetItem) stores the supplied
            // key instance, not the previously stored comparer-equal one.
            return SortedMap(entries.setItem(index, SortedMapEntry(key, value))!!, comparator)
        }

        return SortedMap(entries.insertAt(index, SortedMapEntry(key, value))!!, comparator)
    }

    public fun insert(key: K, value: V): SortedMap<K, V> {
        val result = tryInsert(key, value)
        if (!result.added) {
            throw SortedDuplicateKeyException()
        }

        return result.value
    }

    public fun tryInsert(key: K, value: V): SortedAddResult<SortedMap<K, V>> {
        if (containsKey(key)) {
            return SortedAddResult(this, false)
        }

        return SortedAddResult(setItem(key, value), true)
    }

    public fun remove(key: K): SortedMap<K, V> = tryRemove(key)?.map ?: this

    public fun tryRemove(key: K): SortedMapRemoveResult<K, V>? {
        val index = indexOfKey(key) ?: return null
        val value = entries.itemAt(index).value
        return SortedMapRemoveResult(SortedMap(entries.removeAt(index)!!, comparator), value)
    }

    public fun floorEntry(key: K): SortedMapEntry<K, V>? {
        val index = upperBoundByKey(key) - 1
        return if (index >= 0) entries[index] else null
    }

    public fun ceilingEntry(key: K): SortedMapEntry<K, V>? {
        val index = lowerBoundByKey(key)
        return entries[index]
    }

    public fun lowerEntry(key: K): SortedMapEntry<K, V>? {
        val index = lowerBoundByKey(key) - 1
        return if (index >= 0) entries[index] else null
    }

    public fun higherEntry(key: K): SortedMapEntry<K, V>? {
        val index = upperBoundByKey(key)
        return entries[index]
    }

    public fun getRange(start: Int, count: Int): SortedMap<K, V>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        return SortedMap(entries.range(start, count), comparator)
    }

    public fun getKeyRange(low: K, high: K): SortedMap<K, V> {
        if (compare(comparator, low, high) > 0) {
            return SortedMap(PersistentDeque.empty(), comparator)
        }

        val start = lowerBoundByKey(low)
        val end = upperBoundByKey(high)
        return SortedMap(entries.range(start, end - start), comparator)
    }

    public fun toList(): List<SortedMapEntry<K, V>> = entries.toList()

    public fun keys(): List<K> = entries.map { it.key }

    public fun values(): List<V> = entries.map { it.value }

    public fun sharesStorageWith(other: SortedMap<K, V>): Boolean = entries.sharesStorageWith(other.entries)

    internal fun debugIsBalanced(): Boolean = entries.debugIsBalanced()

    override fun iterator(): Iterator<SortedMapEntry<K, V>> = entries.iterator()

    private fun lowerBoundByKey(key: K): Int {
        var low = 0
        var high = entries.size
        while (low < high) {
            val mid = (low + high) ushr 1
            if (compare(comparator, entries.itemAt(mid).key, key) < 0) {
                low = mid + 1
            } else {
                high = mid
            }
        }

        return low
    }

    private fun upperBoundByKey(key: K): Int {
        var low = 0
        var high = entries.size
        while (low < high) {
            val mid = (low + high) ushr 1
            if (compare(comparator, entries.itemAt(mid).key, key) <= 0) {
                low = mid + 1
            } else {
                high = mid
            }
        }

        return low
    }
}

@Suppress("UNCHECKED_CAST")
private fun <T> PersistentDeque<T>.itemAt(index: Int): T = this[index] as T

private fun <T> PersistentDeque<T>.range(start: Int, count: Int): PersistentDeque<T> {
    val suffix = splitAt(start)!!.right
    return suffix.splitAt(count)!!.left
}
