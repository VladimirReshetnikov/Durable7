package durable7.hamt

public data class IndexedMapEntry<K, V>(public val key: K, public val value: V)

public data class IndexedMapAddResult<K, V, I>(
    public val map: PersistentIndexedMap<K, V, I>,
    public val added: Boolean,
)

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

    public val size: Int get() = primary.size
    public val isEmpty: Boolean get() = primary.isEmpty
    public val keyPolicy: HashPolicy<K> get() = primary.policy
    public val indexPolicy: HashPolicy<I> get() = index.keyPolicy
    public val indexKeyCount: Int get() = index.keyCount

    public fun containsKey(key: K): Boolean = primary.containsKey(key)
    public operator fun get(key: K): V? = primary.getEntry(key)?.value?.value
    public fun actualKey(key: K): K? = primary.getEntry(key)?.key
    public fun indexKeyFor(key: K): I? = primary.getEntry(key)?.value?.indexKey
    public fun containsIndexKey(indexKey: I): Boolean = index.containsKey(indexKey)
    public fun countByIndex(indexKey: I): Int = index.valuesFor(indexKey)?.size ?: 0
    public fun keysByIndex(indexKey: I): PersistentHashSet<K>? = index.valuesFor(indexKey)

    public fun add(key: K, value: V): PersistentIndexedMap<K, V, I> {
        if (primary.containsKey(key)) throw DuplicateKeyException()
        val selected = indexSelector(key, value)
        val nextIndex = index.add(selected, key)
        val actualIndex = checkNotNull(nextIndex.actualKey(selected))
        return PersistentIndexedMap(primary.add(key, Entry(value, actualIndex)), nextIndex, indexSelector, valuePolicy)
    }

    public fun tryAdd(key: K, value: V): IndexedMapAddResult<K, V, I> =
        if (primary.containsKey(key)) IndexedMapAddResult(this, false)
        else IndexedMapAddResult(add(key, value), true)

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

    public fun remove(key: K): PersistentIndexedMap<K, V, I> {
        val current = primary.getEntry(key) ?: return this
        return PersistentIndexedMap(
            primary.remove(current.key),
            index.remove(current.value.indexKey, current.key),
            indexSelector,
            valuePolicy,
        )
    }

    public fun clear(): PersistentIndexedMap<K, V, I> = if (isEmpty) this else PersistentIndexedMap(
        primary.clear(), index.clear(), indexSelector, valuePolicy,
    )

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
