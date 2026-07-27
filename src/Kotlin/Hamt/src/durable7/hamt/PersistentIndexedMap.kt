/*
 * Persistent map with one automatically maintained secondary index.
 *
 * A primary map plus a derived nonunique index keyed by a caller-supplied projection. Every
 * mutation updates both and publishes only when both are complete, so the index cannot drift from
 * the primary map the way a hand-maintained side table can.
 */
package durable7.hamt

/** One primary key and its value. */
public data class IndexedMapEntry<K, V>(public val key: K, public val value: V)

/** The outcome of a strict insertion; the map is the receiver when nothing was added. */
public data class IndexedMapAddResult<K, V, I>(
    public val map: PersistentIndexedMap<K, V, I>,
    public val added: Boolean,
)

/** Primary and index cardinalities returned by a successful structural audit. */
public data class PersistentIndexedMapStatistics(
    public val count: Int,
    public val indexKeyCount: Int,
)

/** Immutable primary map with one automatically maintained non-unique secondary index. */
public class PersistentIndexedMap<K, V, I> private constructor(
    private val primary: PersistentHashMap<K, Entry<V, I>>,
    private val index: PersistentHashMultimap<I, K>,
    public val indexSelector: (K, V) -> I,
    public val valuePolicy: EqualityPolicy<V>,
) : Iterable<IndexedMapEntry<K, V>> {
    private data class Entry<V, I>(val value: V, val indexKey: I)

    public companion object {
        /**
         * An empty map with one automatically maintained secondary index.
         *
         * [indexSelector] derives each entry's index key from its key and value, and is invoked on every insertion
         * and on every value change. It must be a pure function of its arguments: the map re-derives the index key
         * rather than storing what it was told, so a selector that returns different results for the same input
         * leaves the index disagreeing with the primary map.
         */
        public fun <K, V, I> empty(
            indexSelector: (K, V) -> I,
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: EqualityPolicy<V> = defaultEqualityPolicy(),
            indexPolicy: HashPolicy<I> = defaultHashPolicy(),
        ): PersistentIndexedMap<K, V, I> = PersistentIndexedMap(
            PersistentHashMap.empty(keyPolicy),
            PersistentHashMultimap.empty(indexPolicy, keyPolicy),
            indexSelector,
            valuePolicy,
        )

        /**
         * A map holding every pair, with the secondary index derived by [indexSelector]. Later pairs replace
         * earlier ones with an equal key.
         */
        public fun <K, V, I> from(
            items: Iterable<Pair<K, V>>,
            indexSelector: (K, V) -> I,
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: EqualityPolicy<V> = defaultEqualityPolicy(),
            indexPolicy: HashPolicy<I> = defaultHashPolicy(),
        ): PersistentIndexedMap<K, V, I> {
            var result = empty(indexSelector, keyPolicy, valuePolicy, indexPolicy)
            for ((key, value) in items) result = result.set(key, value)
            return result
        }
    }

    /** Number of entries in the primary map. */
    public val size: Int get() = primary.size
    /** Whether the map holds no entries. */
    public val isEmpty: Boolean get() = primary.isEmpty
    /** The hash policy governing primary keys. */
    public val keyPolicy: HashPolicy<K> get() = primary.policy
    /** The hash policy governing index keys. */
    public val indexPolicy: HashPolicy<I> get() = index.keyPolicy
    /** Number of distinct index keys, which is at most [size] and is fewer whenever entries share an index key. */
    public val indexKeyCount: Int get() = index.keyCount

    /** Whether an entry keyed by [key] is present. */
    public fun containsKey(key: K): Boolean = primary.containsKey(key)
    /** The value stored for [key], or `null` when absent or stored as `null`. */
    public operator fun get(key: K): V? = primary.getEntry(key)?.value?.value
    /** The *stored* key representative equal to [key], or `null` when absent. */
    public fun actualKey(key: K): K? = primary.getEntry(key)?.key
    /** The index key currently derived for [key]'s entry, or `null` when the key is absent. */
    public fun indexKeyFor(key: K): I? = primary.getEntry(key)?.value?.indexKey
    /** Whether any entry currently has [indexKey]. */
    public fun containsIndexKey(indexKey: I): Boolean = index.containsKey(indexKey)
    /**
     * How many entries currently have [indexKey]. Zero when none do; the index is nonunique, so this may exceed
     * one.
     */
    public fun countByIndex(indexKey: I): Int = index.valuesFor(indexKey)?.size ?: 0
    /** The primary keys of the entries with [indexKey], or `null` when none has it. */
    public fun keysByIndex(indexKey: I): PersistentHashSet<K>? = index.valuesFor(indexKey)

    /**
     * A map with the entry added and indexed.
     *
     * @throws DuplicateKeyException if an equal key is already present; use [tryAdd] to detect that without an
     * exception, or [set] to overwrite.
     */
    public fun add(key: K, value: V): PersistentIndexedMap<K, V, I> {
        if (primary.containsKey(key)) throw DuplicateKeyException()
        val selected = indexSelector(key, value)
        val nextIndex = index.add(selected, key)
        val actualIndex = checkNotNull(nextIndex.actualKey(selected))
        return PersistentIndexedMap(primary.add(key, Entry(value, actualIndex)), nextIndex, indexSelector, valuePolicy)
    }

    /**
     * Add the entry only when [key] is absent, reporting whether it was added. On a collision the result carries
     * the receiver unchanged.
     */
    public fun tryAdd(key: K, value: V): IndexedMapAddResult<K, V, I> =
        if (primary.containsKey(key)) IndexedMapAddResult(this, false)
        else IndexedMapAddResult(add(key, value), true)

    /**
     * A map binding [key] to [value], adding or replacing as needed and moving the entry between index buckets when
     * its derived index key changes. Returns the receiver unchanged when the value is equivalent under the value
     * policy, and keeps the stored key representative rather than adopting the one passed in.
     *
     * [indexSelector] runs before either index is rebuilt, so a selector that throws leaves the receiver untouched
     * rather than half-updated.
     */
    public fun set(key: K, value: V): PersistentIndexedMap<K, V, I> {
        val current = primary.getEntry(key) ?: return add(key, value)
        if (valuePolicy.equivalent(current.value.value, value)) return this
        val selected = indexSelector(current.key, value)
        if (indexPolicy.equivalent(current.value.indexKey, selected)) {
            return PersistentIndexedMap(
                primary.put(current.key, Entry(value, current.value.indexKey)),
                index,
                indexSelector,
                valuePolicy,
            )
        }
        val nextIndex = index.remove(current.value.indexKey, current.key).add(selected, current.key)
        val actualIndex = checkNotNull(nextIndex.actualKey(selected))
        return PersistentIndexedMap(
            primary.put(current.key, Entry(value, actualIndex)),
            nextIndex,
            indexSelector,
            valuePolicy,
        )
    }

    /**
     * A map without the entry keyed by [key], removing it from the secondary index as well. Returns the receiver
     * unchanged when absent.
     */
    public fun remove(key: K): PersistentIndexedMap<K, V, I> {
        val current = primary.getEntry(key) ?: return this
        return PersistentIndexedMap(
            primary.remove(current.key),
            index.remove(current.value.indexKey, current.key),
            indexSelector,
            valuePolicy,
        )
    }

    /** An empty map retaining the same selector and policies. */
    public fun clear(): PersistentIndexedMap<K, V, I> = if (isEmpty) this else PersistentIndexedMap(
        primary.clear(), index.clear(), indexSelector, valuePolicy,
    )

    /**
     * Walk both indexes and check that they agree - matching counts, and every primary entry present under its
     * currently derived index key. A defensive audit for tests, not a routine operation.
     *
     * @throws IllegalStateException on the first violation found.
     */
    public fun validateStructure(): PersistentIndexedMapStatistics {
        index.validateStructure()
        check(index.pairCount == size.toLong()) { "PersistentIndexedMap primary and index counts disagree." }
        for (entry in primary) {
            check(index.contains(entry.value.indexKey, entry.key)) { "A primary row is absent from its index group." }
        }
        for (indexed in index) {
            val row = primary.getEntry(indexed.value)
            check(row != null && indexPolicy.equivalent(row.value.indexKey, indexed.key)) {
                "An indexed key does not resolve to a matching primary row."
            }
        }
        return PersistentIndexedMapStatistics(size, indexKeyCount)
    }

    override fun iterator(): Iterator<IndexedMapEntry<K, V>> =
        primary.asSequence().map { IndexedMapEntry(it.key, it.value.value) }.iterator()
}
