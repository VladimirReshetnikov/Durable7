/*
 * Order-statistic sorted bag, set, and map over the measured tree.
 *
 * The measure caches both element counts and the largest key of each subtree, so one split answers
 * ordered questions and positional questions alike - rank and select as well as lookup, which an
 * ordinary balanced tree cannot do without extra bookkeeping.
 */
package durable7.fingertree

/** Raised when duplicate-rejecting insertion sees an equivalent key. */
public class SortedDuplicateKeyException(message: String = "An equivalent key is already present.") :
    IllegalArgumentException(message)

/** One key-value pair as the sorted map stores and presents it. */
public data class SortedMapEntry<K, V>(public val key: K, public val value: V)

/** The outcome of a non-overwriting insertion; the value is the receiver when nothing was added. */
public data class SortedAddResult<T>(public val value: T, public val added: Boolean)

/** The map remaining after a removal, together with the value that was removed. */
public data class SortedMapRemoveResult<K, V>(
    public val map: SortedMap<K, V>,
    public val value: V,
)

// Every call must return the same instance: PriorityQueue.meld accepts only queues whose
// comparators are identical or equal, and Comparator instances compare by identity. The stdlib's
// naturalOrder() is a shared singleton object, unlike a lambda, whose per-call-site caching is a
// compiler detail.
internal fun <T : Comparable<T>> naturalComparator(): Comparator<T> = naturalOrder()

private fun <T> compare(comparator: Comparator<in T>, left: T, right: T): Int =
    comparator.compare(left, right)

// The facades keep their storage comparator-sorted, so each bound is the
// partition point of a monotone predicate: one guided root-to-leaf descent
// costs O(log n) comparisons, unlike binary search over indexing, whose every
// probe repeats an O(log n) positional walk.
private fun <T> PersistentDeque<T>.lowerBound(value: T, comparator: Comparator<in T>): Int =
    prefixLength { compare(comparator, it, value) < 0 }

private fun <T> PersistentDeque<T>.upperBound(value: T, comparator: Comparator<in T>): Int =
    prefixLength { compare(comparator, it, value) <= 0 }

/**
 * A persistent sorted multiset with rank and select. The cached element counts are what make those a descent rather
 * than a scan.
 */
public class SortedBag<T> private constructor(
    private val items: PersistentDeque<T>,
    private val comparator: Comparator<in T>,
) : Iterable<T> {
    public companion object {
        /**
         * An empty bag ordered by natural order. The comparator is the standard library's shared singleton, so two
         * bags built this way can be combined by the operations that require comparator identity.
         */
        public fun <T : Comparable<T>> empty(): SortedBag<T> = SortedBag(PersistentDeque.empty(), naturalComparator())

        /** A bag holding every value, in natural order. Duplicates are kept: a bag is a multiset. */
        public fun <T : Comparable<T>> from(values: Iterable<T>): SortedBag<T> =
            empty<T>().addRange(values)

        /**
         * An empty bag ordered by [comparator], which the bag retains by identity. Operations between two bags
         * require the same comparator instance.
         */
        public fun <T> empty(comparator: Comparator<in T>): SortedBag<T> =
            SortedBag(PersistentDeque.empty(), comparator)

        /** A bag holding every value, ordered by [comparator]. Duplicates are kept. */
        public fun <T> from(values: Iterable<T>, comparator: Comparator<in T>): SortedBag<T> =
            empty(comparator).addRange(values)
    }

    /** Number of elements, counting duplicates. O(1), from the cached subtree counts. */
    public val size: Int
        get() = items.size

    /** Whether the bag holds no elements. */
    public val isEmpty: Boolean
        get() = items.isEmpty

    /** The smallest element, or `null` when empty. */
    public fun min(): T? = items.front()

    /** The largest element, or `null` when empty. */
    public fun max(): T? = items.back()

    /**
     * The element at [rank] in sorted order, or `null` when the rank is out of range. This is select: a single
     * descent guided by the cached counts, not a scan.
     */
    public operator fun get(rank: Int): T? = items[rank]

    /** Whether at least one element compares equal to [value]. */
    public fun contains(value: T): Boolean = countOf(value) > 0

    /**
     * How many elements sort strictly before [value]. This is [value]'s rank had it been present, so it doubles as
     * the insertion point.
     */
    public fun countLessThan(value: T): Int = items.lowerBound(value, comparator)

    /** How many elements sort at or before [value]. */
    public fun countAtMost(value: T): Int = items.upperBound(value, comparator)

    /**
     * How many stored elements compare equal to [value]. The difference of the two bounds, so it costs two descents
     * rather than a scan.
     */
    public fun countOf(value: T): Int = countAtMost(value) - countLessThan(value)

    /**
     * A bag with [value] added. It is placed after any equal elements already stored, so repeated additions of
     * equal values preserve their arrival order.
     */
    public fun add(value: T): SortedBag<T> {
        val index = countAtMost(value)
        return SortedBag(items.insertAt(index, value)!!, comparator)
    }

    /** A bag with every element of [values] added, in order. Equivalent to repeated [add]. */
    public fun addRange(values: Iterable<T>): SortedBag<T> {
        var result = this
        for (value in values) {
            result = result.add(value)
        }

        return result
    }

    /**
     * A bag with one element equal to [value] removed. Returns the receiver unchanged when none is present;
     * removing an absent value is not an error. Use [removeAll] to drop every occurrence.
     */
    public fun remove(value: T): SortedBag<T> {
        val index = countLessThan(value)
        if (index == size || compare(comparator, items.itemAt(index), value) != 0) {
            return this
        }

        return SortedBag(items.removeAt(index)!!, comparator)
    }

    /** A bag with every element equal to [value] removed. Returns the receiver unchanged when none is present. */
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

    /**
     * The [count] elements starting at rank [start], as a bag, or `null` when that range does not lie inside the
     * bag.
     */
    public fun getRange(start: Int, count: Int): SortedBag<T>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        return SortedBag(items.range(start, count), comparator)
    }

    /**
     * The elements in the closed value range `[low, high]`, as a bag. An inverted range yields an empty bag rather
     * than an error.
     */
    public fun getValueRange(low: T, high: T): SortedBag<T> {
        if (compare(comparator, low, high) > 0) {
            return SortedBag(PersistentDeque.empty(), comparator)
        }

        val start = countLessThan(low)
        return SortedBag(items.range(start, countAtMost(high) - start), comparator)
    }

    /** The elements in sorted order. */
    public fun toList(): List<T> = items.toList()

    /**
     * Whether this bag and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common. A diagnostic for confirming structural sharing, not a substitute for equality.
     */
    public fun sharesStorageWith(other: SortedBag<T>): Boolean = items.sharesStorageWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.debugIsBalanced()

    internal fun cursorRemoveAt(rank: Int): SortedBag<T> =
        SortedBag(checkNotNull(items.removeAt(rank)), comparator)

    internal fun cursorItemAt(rank: Int): T = items.itemAt(rank)

    override fun iterator(): Iterator<T> = items.iterator()
}

/** A persistent sorted set with rank and select, keeping the first representative of each equivalence class. */
public class SortedSet<T> private constructor(
    private val items: PersistentDeque<T>,
    private val comparator: Comparator<in T>,
) : Iterable<T> {
    public companion object {
        /**
         * An empty set ordered by natural order. The comparator is the standard library's shared singleton, so two
         * sets built this way can be combined by the operations that require comparator identity.
         */
        public fun <T : Comparable<T>> empty(): SortedSet<T> = SortedSet(PersistentDeque.empty(), naturalComparator())

        /**
         * A set holding the distinct values, in natural order. The first of each group of equal values is the one
         * stored.
         */
        public fun <T : Comparable<T>> from(values: Iterable<T>): SortedSet<T> =
            empty<T>().union(values)

        /** An empty set ordered by [comparator], which the set retains by identity. */
        public fun <T> empty(comparator: Comparator<in T>): SortedSet<T> =
            SortedSet(PersistentDeque.empty(), comparator)

        /**
         * A set holding the distinct values, ordered by [comparator]. The first of each group of equal values is
         * the one stored.
         */
        public fun <T> from(values: Iterable<T>, comparator: Comparator<in T>): SortedSet<T> =
            empty(comparator).union(values)
    }

    /** Number of distinct elements. O(1), from the cached subtree counts. */
    public val size: Int
        get() = items.size

    /** Whether the set holds no elements. */
    public val isEmpty: Boolean
        get() = items.isEmpty

    /** The smallest element, or `null` when empty. */
    public fun min(): T? = items.front()

    /** The largest element, or `null` when empty. */
    public fun max(): T? = items.back()

    /**
     * The element at [rank] in sorted order, or `null` when the rank is out of range. This is select: a single
     * descent guided by the cached counts.
     */
    public operator fun get(rank: Int): T? = items[rank]

    /** Whether an element comparing equal to [value] is present. */
    public fun contains(value: T): Boolean {
        val index = items.lowerBound(value, comparator)
        return index < size && compare(comparator, items.itemAt(index), value) == 0
    }

    /** The rank of the element equal to [value], or `null` when absent. This is rank: the inverse of [get]. */
    public fun indexOf(value: T): Int? {
        val index = items.lowerBound(value, comparator)
        return if (index < size && compare(comparator, items.itemAt(index), value) == 0) index else null
    }

    /**
     * A set containing [value]. Returns the receiver unchanged when an equal element is already present, so the
     * first stored representative is kept rather than replaced.
     */
    public fun add(value: T): SortedSet<T> {
        val index = items.lowerBound(value, comparator)
        if (index < size && compare(comparator, items.itemAt(index), value) == 0) {
            return this
        }

        return SortedSet(items.insertAt(index, value)!!, comparator)
    }

    /**
     * A set containing everything in the receiver and in [values]. Elements from [values] are compared under the
     * receiver's comparator, and the receiver's representatives win ties.
     */
    public fun union(values: Iterable<T>): SortedSet<T> {
        var result = this
        for (value in values) {
            result = result.add(value)
        }

        return result
    }

    /** A set without the element equal to [value]. Returns the receiver unchanged when absent. */
    public fun remove(value: T): SortedSet<T> {
        val index = indexOf(value) ?: return this
        return SortedSet(items.removeAt(index)!!, comparator)
    }

    /** The largest element at or below [value], or `null` when none is. */
    public fun floor(value: T): T? {
        val index = items.upperBound(value, comparator) - 1
        return if (index >= 0) items[index] else null
    }

    /** The smallest element at or above [value], or `null` when none is. */
    public fun ceiling(value: T): T? {
        val index = items.lowerBound(value, comparator)
        return if (index < size) items[index] else null
    }

    /** The largest element strictly below [value], or `null` when none is. */
    public fun lower(value: T): T? {
        val index = items.lowerBound(value, comparator) - 1
        return if (index >= 0) items[index] else null
    }

    /** The smallest element strictly above [value], or `null` when none is. */
    public fun higher(value: T): T? {
        val index = items.upperBound(value, comparator)
        return if (index < size) items[index] else null
    }

    /**
     * The [count] elements starting at rank [start], as a set, or `null` when that range does not lie inside the
     * set.
     */
    public fun getRange(start: Int, count: Int): SortedSet<T>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        return SortedSet(items.range(start, count), comparator)
    }

    /**
     * The elements in the closed value range `[low, high]`, as a set. An inverted range yields an empty set rather
     * than an error.
     */
    public fun getValueRange(low: T, high: T): SortedSet<T> {
        if (compare(comparator, low, high) > 0) {
            return SortedSet(PersistentDeque.empty(), comparator)
        }

        val start = items.lowerBound(low, comparator)
        val end = items.upperBound(high, comparator)
        return SortedSet(items.range(start, end - start), comparator)
    }

    /** The elements present in both the receiver and [values], under the receiver's comparator. */
    public fun intersect(values: Iterable<T>): SortedSet<T> {
        val other = from(values, comparator)
        return SortedSet(PersistentDeque.from(items.filter { other.contains(it) }), comparator)
    }

    /** The elements of the receiver absent from [values], under the receiver's comparator. */
    public fun except(values: Iterable<T>): SortedSet<T> {
        val other = from(values, comparator)
        return SortedSet(PersistentDeque.from(items.filterNot { other.contains(it) }), comparator)
    }

    /** The elements in exactly one of the receiver and [values], under the receiver's comparator. */
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

    /** Whether every element of the receiver appears in [values]. */
    public fun isSubsetOf(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return all { other.contains(it) }
    }

    /** Whether every element of [values] appears in the receiver. */
    public fun isSupersetOf(values: Iterable<T>): Boolean =
        values.all { contains(it) }

    /** [isSubsetOf] and the two are not equal as sets. */
    public fun isProperSubsetOf(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return size < other.size && all { other.contains(it) }
    }

    /** [isSupersetOf] and the two are not equal as sets. */
    public fun isProperSupersetOf(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return size > other.size && other.all { contains(it) }
    }

    /** Whether the receiver and [values] share at least one element. */
    public fun overlaps(values: Iterable<T>): Boolean =
        values.any { contains(it) }

    /** Whether the receiver and [values] hold exactly the same elements, under the receiver's comparator. */
    public fun setEquals(values: Iterable<T>): Boolean {
        val other = from(values, comparator)
        return size == other.size && all { other.contains(it) }
    }

    /** The elements in sorted order. */
    public fun toList(): List<T> = items.toList()

    /**
     * Whether this set and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common. A diagnostic for confirming structural sharing, not a substitute for equality.
     */
    public fun sharesStorageWith(other: SortedSet<T>): Boolean = items.sharesStorageWith(other.items)

    internal fun debugIsBalanced(): Boolean = items.debugIsBalanced()

    internal fun cursorLowerBound(value: T): Int = items.lowerBound(value, comparator)

    internal fun cursorUpperBound(value: T): Int = items.upperBound(value, comparator)

    internal fun cursorItemAt(rank: Int): T = items.itemAt(rank)

    override fun iterator(): Iterator<T> = items.iterator()
}

/**
 * A persistent sorted map with rank and select over its keys, retaining the first key representative across value
 * replacement.
 */
public class SortedMap<K, V> private constructor(
    private val entries: PersistentDeque<SortedMapEntry<K, V>>,
    private val comparator: Comparator<in K>,
) : Iterable<SortedMapEntry<K, V>> {
    public companion object {
        /**
         * An empty map ordered by natural key order. The comparator is the standard library's shared singleton, so
         * two maps built this way can be combined by the operations that require comparator identity.
         */
        public fun <K : Comparable<K>, V> empty(): SortedMap<K, V> =
            SortedMap(PersistentDeque.empty(), naturalComparator())

        /**
         * A map holding every pair, in natural key order. Later pairs overwrite earlier ones with an equal key.
         */
        public fun <K : Comparable<K>, V> from(values: Iterable<Pair<K, V>>): SortedMap<K, V> {
            var result = empty<K, V>()
            for ((key, value) in values) {
                result = result.setItem(key, value)
            }

            return result
        }

        /**
         * A map holding every pair, ordered by [comparator]. Later pairs overwrite earlier ones with an equal key.
         */
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

        /** An empty map ordered by [comparator], which the map retains by identity. */
        public fun <K, V> empty(comparator: Comparator<in K>): SortedMap<K, V> =
            SortedMap(PersistentDeque.empty(), comparator)
    }

    /** Number of entries. O(1), from the cached subtree counts. */
    public val size: Int
        get() = entries.size

    /** Whether the map holds no entries. */
    public val isEmpty: Boolean
        get() = entries.isEmpty

    /** Whether an entry with a key equal to [key] is present. */
    public fun containsKey(key: K): Boolean = indexOfKey(key) != null

    /**
     * The value stored for [key], or `null` when absent. A stored `null` value is therefore indistinguishable from
     * a miss here; use [indexOfKey] or [containsKey] when that matters.
     */
    public operator fun get(key: K): V? {
        val index = indexOfKey(key) ?: return null
        return entries.itemAt(index).value
    }

    /** The entry at [rank] in key order, or `null` when the rank is out of range. */
    public fun entryAt(rank: Int): SortedMapEntry<K, V>? = entries[rank]

    /** The entry with the smallest key, or `null` when empty. */
    public fun minEntry(): SortedMapEntry<K, V>? = entries.front()

    /** The entry with the largest key, or `null` when empty. */
    public fun maxEntry(): SortedMapEntry<K, V>? = entries.back()

    /** The rank of the entry whose key equals [key], or `null` when absent. */
    public fun indexOfKey(key: K): Int? {
        val index = lowerBoundByKey(key)
        return if (index < size && compare(comparator, entries.itemAt(index).key, key) == 0) index else null
    }

    /**
     * A map binding [key] to [value], adding or replacing as needed.
     *
     * Replacement stores the *supplied* key instance rather than keeping the comparer-equal one already there,
     * matching the C# reference. That is observable whenever equal keys are distinguishable - a case-insensitive
     * comparator, or keys carrying payload outside the comparison.
     */
    public fun setItem(key: K, value: V): SortedMap<K, V> {
        val index = lowerBoundByKey(key)
        if (index < size && compare(comparator, entries.itemAt(index).key, key) == 0) {
            // The C# reference (SortedDictionary.SetItem) stores the supplied
            // key instance, not the previously stored comparer-equal one.
            return SortedMap(entries.setItem(index, SortedMapEntry(key, value))!!, comparator)
        }

        return SortedMap(entries.insertAt(index, SortedMapEntry(key, value))!!, comparator)
    }

    /**
     * A map with the entry added.
     *
     * @throws SortedDuplicateKeyException if an equal key is already present; use [tryInsert] to detect that
     * without an exception, or [setItem] to overwrite.
     */
    public fun insert(key: K, value: V): SortedMap<K, V> {
        val result = tryInsert(key, value)
        if (!result.added) {
            throw SortedDuplicateKeyException()
        }

        return result.value
    }

    /**
     * Add the entry only when [key] is absent, reporting whether it was added. On a collision the result carries
     * the receiver unchanged.
     */
    public fun tryInsert(key: K, value: V): SortedAddResult<SortedMap<K, V>> {
        if (containsKey(key)) {
            return SortedAddResult(this, false)
        }

        return SortedAddResult(setItem(key, value), true)
    }

    /**
     * A map without the entry whose key equals [key]. Returns the receiver unchanged when absent; use [tryRemove]
     * to recover the removed value.
     */
    public fun remove(key: K): SortedMap<K, V> = tryRemove(key)?.map ?: this

    /**
     * Remove the entry whose key equals [key], returning the resulting map together with the value that was
     * removed, or `null` when the key is absent. The `null` here means "no such key" and is unaffected by a stored
     * `null` value.
     */
    public fun tryRemove(key: K): SortedMapRemoveResult<K, V>? {
        val index = indexOfKey(key) ?: return null
        val value = entries.itemAt(index).value
        return SortedMapRemoveResult(SortedMap(entries.removeAt(index)!!, comparator), value)
    }

    /** The entry with the largest key at or below [key], or `null` when none is. */
    public fun floorEntry(key: K): SortedMapEntry<K, V>? {
        val index = upperBoundByKey(key) - 1
        return if (index >= 0) entries[index] else null
    }

    /** The entry with the smallest key at or above [key], or `null` when none is. */
    public fun ceilingEntry(key: K): SortedMapEntry<K, V>? {
        val index = lowerBoundByKey(key)
        return entries[index]
    }

    /** The entry with the largest key strictly below [key], or `null` when none is. */
    public fun lowerEntry(key: K): SortedMapEntry<K, V>? {
        val index = lowerBoundByKey(key) - 1
        return if (index >= 0) entries[index] else null
    }

    /** The entry with the smallest key strictly above [key], or `null` when none is. */
    public fun higherEntry(key: K): SortedMapEntry<K, V>? {
        val index = upperBoundByKey(key)
        return entries[index]
    }

    /**
     * The [count] entries starting at rank [start], as a map, or `null` when that range does not lie inside the
     * map.
     */
    public fun getRange(start: Int, count: Int): SortedMap<K, V>? {
        if (!isValidRange(start, count, size)) {
            return null
        }

        return SortedMap(entries.range(start, count), comparator)
    }

    /**
     * The entries whose keys fall in the closed range `[low, high]`, as a map. An inverted range yields an empty
     * map rather than an error.
     */
    public fun getKeyRange(low: K, high: K): SortedMap<K, V> {
        if (compare(comparator, low, high) > 0) {
            return SortedMap(PersistentDeque.empty(), comparator)
        }

        val start = lowerBoundByKey(low)
        val end = upperBoundByKey(high)
        return SortedMap(entries.range(start, end - start), comparator)
    }

    /** The entries in key order. */
    public fun toList(): List<SortedMapEntry<K, V>> = entries.toList()

    /** The keys in order. */
    public fun keys(): List<K> = entries.map { it.key }

    /** The values in key order. */
    public fun values(): List<V> = entries.map { it.value }

    /**
     * Whether this map and [other] share storage, meaning one was derived from the other and the two retain nodes
     * in common. A diagnostic for confirming structural sharing, not a substitute for equality.
     */
    public fun sharesStorageWith(other: SortedMap<K, V>): Boolean = entries.sharesStorageWith(other.entries)

    internal fun debugIsBalanced(): Boolean = entries.debugIsBalanced()

    internal fun cursorLowerBound(key: K): Int = lowerBoundByKey(key)

    internal fun cursorUpperBound(key: K): Int = upperBoundByKey(key)

    override fun iterator(): Iterator<SortedMapEntry<K, V>> = entries.iterator()

    private fun lowerBoundByKey(key: K): Int =
        entries.prefixLength { compare(comparator, it.key, key) < 0 }

    private fun upperBoundByKey(key: K): Int =
        entries.prefixLength { compare(comparator, it.key, key) <= 0 }
}

@Suppress("UNCHECKED_CAST")
private fun <T> PersistentDeque<T>.itemAt(index: Int): T = this[index] as T

private fun <T> PersistentDeque<T>.range(start: Int, count: Int): PersistentDeque<T> {
    val suffix = splitAt(start)!!.right
    return suffix.splitAt(count)!!.left
}
