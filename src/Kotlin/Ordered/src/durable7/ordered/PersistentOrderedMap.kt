package durable7.ordered

import durable7.hamt.HashPolicy
import durable7.hamt.PersistentHashMap
import durable7.hamt.defaultHashPolicy

public data class OrderedMapEntry<K, V>(public val key: K, public val value: V)
public data class OrderedMapLookup<K, V>(
    public val found: Boolean,
    public val key: K,
    public val value: V?,
)
public data class OrderedMapAddResult<K, V>(
    public val map: PersistentOrderedMap<K, V>,
    public val added: Boolean,
)
public data class OrderedMapRemoveResult<K, V>(
    public val map: PersistentOrderedMap<K, V>,
    public val removed: Boolean,
)
public data class PersistentOrderedMapStatistics(public val count: Int)

/** Immutable comparer-keyed map with insertion and explicit-position order. */
public class PersistentOrderedMap<K, V> private constructor(
    private val keys: PersistentOrderedSet<K>,
    private val values: PersistentHashMap<K, V>,
) : Iterable<OrderedMapEntry<K, V>> {
    public companion object {
        public fun <K, V> empty(policy: HashPolicy<K> = defaultHashPolicy()): PersistentOrderedMap<K, V> =
            PersistentOrderedMap(PersistentOrderedSet.empty(policy), PersistentHashMap.empty(policy))

        /** First key representative and position win; the last payload wins. */
        public fun <K, V> from(
            entries: Iterable<Pair<K, V>>,
            policy: HashPolicy<K> = defaultHashPolicy(),
        ): PersistentOrderedMap<K, V> {
            var result = empty<K, V>(policy)
            for ((key, value) in entries) result = result.set(key, value)
            return result
        }
    }

    public val size: Int get() = keys.size
    public val count: Int get() = size
    public val isEmpty: Boolean get() = keys.isEmpty
    public val policy: HashPolicy<K> get() = keys.policy
    public val first: OrderedMapEntry<K, V> get() = getAt(0)
    public val last: OrderedMapEntry<K, V> get() = getAt(size - 1)

    public fun containsKey(key: K): Boolean = values.containsKey(key)
    public operator fun get(key: K): V? = values.getEntry(key)?.value

    public fun tryGet(key: K): OrderedMapLookup<K, V> {
        val indexed = values.getEntry(key) ?: return OrderedMapLookup(false, key, null)
        val stored = keys.tryGetValue(key)
        check(stored.found) { "PersistentOrderedMap indexes disagree." }
        return OrderedMapLookup(true, stored.value, indexed.value)
    }

    public fun getAt(index: Int): OrderedMapEntry<K, V> {
        val key = keys[index]
        val indexed = values.getEntry(key) ?: throw indexDisagreement()
        return OrderedMapEntry(key, indexed.value)
    }

    public fun indexOfKey(key: K): Int = keys.indexOf(key)

    public fun add(key: K, value: V): PersistentOrderedMap<K, V> = insert(size, key, value)
    public fun addFirst(key: K, value: V): PersistentOrderedMap<K, V> = insert(0, key, value)

    public fun tryAdd(key: K, value: V): OrderedMapAddResult<K, V> =
        if (containsKey(key)) OrderedMapAddResult(this, false) else OrderedMapAddResult(add(key, value), true)

    public fun insert(index: Int, key: K, value: V): PersistentOrderedMap<K, V> {
        if (index < 0 || index > size) throw IndexOutOfBoundsException("Index must lie within 0 through the map count.")
        if (containsKey(key)) throw IllegalArgumentException("An equivalent key is already present.")
        return PersistentOrderedMap(keys.insert(index, key), values.add(key, value))
    }

    /**
     * Adds an absent key at the end; an existing key retains representative and position. A payload
     * the value policy already considers equivalent is a no-op that returns this exact map, so the
     * receiver-preserving rule the underlying CHAMP applies is not lost at this layer.
     */
    public fun set(key: K, value: V): PersistentOrderedMap<K, V> {
        if (!containsKey(key)) return PersistentOrderedMap(keys.add(key), values.add(key, value))
        val nextValues = values.put(key, value)
        return if (nextValues === values) this else PersistentOrderedMap(keys, nextValues)
    }

    public fun moveToFirst(key: K): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.moveToFirst(key), values)
    public fun moveToLast(key: K): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.moveToLast(key), values)
    public fun moveTo(index: Int, key: K): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.moveTo(index, key), values)

    public fun remove(key: K): PersistentOrderedMap<K, V> = tryRemove(key).map
    public fun tryRemove(key: K): OrderedMapRemoveResult<K, V> {
        val removedKeys = keys.tryRemove(key)
        if (!removedKeys.removed) return OrderedMapRemoveResult(this, false)
        return OrderedMapRemoveResult(PersistentOrderedMap(removedKeys.set, values.remove(key)), true)
    }

    public fun removeAt(index: Int): PersistentOrderedMap<K, V> {
        val key = keys[index]
        return PersistentOrderedMap(keys.removeAt(index), values.remove(key))
    }

    public fun removeFirst(): PersistentOrderedMap<K, V> = removeAt(0)
    public fun removeLast(): PersistentOrderedMap<K, V> = removeAt(size - 1)
    public fun clear(): PersistentOrderedMap<K, V> = if (isEmpty) this else empty(policy)

    public fun getRange(index: Int, count: Int): PersistentOrderedMap<K, V> {
        val nextKeys = keys.getRange(index, count)
        var nextValues = PersistentHashMap.empty<K, V>(policy)
        for (key in nextKeys) {
            val indexed = values.getEntry(key) ?: throw indexDisagreement()
            nextValues = nextValues.add(indexed.key, indexed.value)
        }
        return PersistentOrderedMap(nextKeys, nextValues)
    }

    public fun take(count: Int): PersistentOrderedMap<K, V> = getRange(0, count)
    public fun drop(count: Int): PersistentOrderedMap<K, V> = getRange(count, size - count)
    public fun reverse(): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.reverse(), values)

    /** Stable one-shot entry sort; no ordering policy is retained. */
    public fun sort(comparator: Comparator<in OrderedMapEntry<K, V>>): PersistentOrderedMap<K, V> {
        val nextKeys = keys.sort(Comparator { left, right -> comparator.compare(attach(left), attach(right)) })
        return if (nextKeys === keys) this else PersistentOrderedMap(nextKeys, values)
    }

    public fun toList(): List<OrderedMapEntry<K, V>> = keys.map { attach(it) }

    public fun sharesOrderWith(other: PersistentOrderedMap<K, V>): Boolean = keys === other.keys
    public fun sharesValuesWith(other: PersistentOrderedMap<K, V>): Boolean = values.sharesRootWith(other.values)

    public fun validateStructure(): PersistentOrderedMapStatistics {
        keys.validateStructure()
        check(keys.size == values.size) { "PersistentOrderedMap indexes have different counts." }
        for (key in keys) check(values.containsKey(key)) { "An ordered key has no payload." }
        for (entry in values) check(keys.contains(entry.key)) { "A payload key has no ordered representative." }
        return PersistentOrderedMapStatistics(size)
    }

    override fun iterator(): Iterator<OrderedMapEntry<K, V>> = toList().iterator()

    private fun attach(key: K): OrderedMapEntry<K, V> {
        val indexed = values.getEntry(key) ?: throw indexDisagreement()
        return OrderedMapEntry(key, indexed.value)
    }

    private fun indexDisagreement(): IllegalStateException =
        IllegalStateException("PersistentOrderedMap indexes disagree.")
}
