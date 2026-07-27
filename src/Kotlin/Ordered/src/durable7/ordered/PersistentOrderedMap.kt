/*
 * Persistent insertion-ordered map with explicit repositioning.
 *
 * Separates identity, decided by hashing and equality, from position, which follows insertion order
 * unless the caller moves an entry. Re-adding an existing key keeps its position and stored key
 * representative while replacing the payload.
 */
package durable7.ordered

import durable7.hamt.HashPolicy
import durable7.hamt.PersistentHashMap
import durable7.hamt.defaultHashPolicy

/** One key-value pair as the insertion-ordered map presents it. */
public data class OrderedMapEntry<K, V>(public val key: K, public val value: V)

/** A presence-safe lookup result, so a stored null stays distinct from absence. */
public data class OrderedMapLookup<K, V>(
    public val found: Boolean,
    public val key: K,
    public val value: V?,
)

/** The outcome of a strict insertion; the map is the receiver when nothing was added. */
public data class OrderedMapAddResult<K, V>(
    public val map: PersistentOrderedMap<K, V>,
    public val added: Boolean,
)

/** The map remaining after a removal, together with the removed key representative and value. */
public data class OrderedMapRemoveResult<K, V>(
    public val map: PersistentOrderedMap<K, V>,
    public val removed: Boolean,
)

/** Entry counts returned by a successful dual-index audit. */
public data class PersistentOrderedMapStatistics(public val count: Int)

/** Immutable comparer-keyed map with insertion and explicit-position order. */
public class PersistentOrderedMap<K, V> private constructor(
    private val keys: PersistentOrderedSet<K>,
    private val values: PersistentHashMap<K, V>,
) : Iterable<OrderedMapEntry<K, V>> {
    public companion object {
        /** An empty map using [policy] for key identity, which the map retains by identity. */
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

    /** Number of entries. */
    public val size: Int get() = keys.size
    /** Number of entries; an alias for [size]. */
    public val count: Int get() = size
    /** Whether the map holds no entries. */
    public val isEmpty: Boolean get() = keys.isEmpty
    /**
     * The hash policy governing key identity. Membership is decided by this policy; position is decided separately,
     * by insertion and explicit movement.
     */
    public val policy: HashPolicy<K> get() = keys.policy
    /**
     * The first entry in order.
     *
     * @throws IndexOutOfBoundsException if the map is empty.
     */
    public val first: OrderedMapEntry<K, V> get() = getAt(0)
    /**
     * The last entry in order.
     *
     * @throws IndexOutOfBoundsException if the map is empty.
     */
    public val last: OrderedMapEntry<K, V> get() = getAt(size - 1)

    /** Whether an entry keyed by [key] is present. */
    public fun containsKey(key: K): Boolean = values.containsKey(key)
    /**
     * The value for [key], or `null` when absent or stored as `null`. Use [tryGet] when the two must be told apart.
     */
    public operator fun get(key: K): V? = values.getEntry(key)?.value

    /**
     * The entry for [key] as a presence-discriminated result, carrying the *stored* key representative. The first
     * representative of each equivalence class is the one kept, so it need not be the instance passed in.
     */
    public fun tryGet(key: K): OrderedMapLookup<K, V> {
        val indexed = values.getEntry(key) ?: return OrderedMapLookup(false, key, null)
        val stored = keys.tryGetValue(key)
        check(stored.found) { "PersistentOrderedMap indexes disagree." }
        return OrderedMapLookup(true, stored.value, indexed.value)
    }

    /**
     * The entry at ordinal [index] in the map's order.
     *
     * @throws IndexOutOfBoundsException if the index is out of range.
     */
    public fun getAt(index: Int): OrderedMapEntry<K, V> {
        val key = keys[index]
        val indexed = values.getEntry(key) ?: throw indexDisagreement()
        return OrderedMapEntry(key, indexed.value)
    }

    /** The ordinal position of [key], or `-1` when absent. */
    public fun indexOfKey(key: K): Int = keys.indexOf(key)

    /**
     * A map with the entry appended at the end.
     *
     * @throws IllegalArgumentException if an equal key is already present; use [tryAdd] to detect that without an
     * exception. Order is the caller's to control here, so an existing key is not silently moved.
     */
    public fun add(key: K, value: V): PersistentOrderedMap<K, V> = insert(size, key, value)
    /**
     * A map with the entry inserted at the front.
     *
     * @throws IllegalArgumentException if an equal key is already present.
     */
    public fun addFirst(key: K, value: V): PersistentOrderedMap<K, V> = insert(0, key, value)

    /**
     * Append the entry only when [key] is absent, reporting whether it was added. On a collision the result carries
     * the receiver unchanged.
     */
    public fun tryAdd(key: K, value: V): OrderedMapAddResult<K, V> =
        if (containsKey(key)) OrderedMapAddResult(this, false) else OrderedMapAddResult(add(key, value), true)

    /**
     * A map with the entry inserted so that it ends up at ordinal [index].
     *
     * @throws IndexOutOfBoundsException if [index] lies outside `0..size`.
     *
     * @throws IllegalArgumentException if an equal key is already present.
     */
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

    /**
     * A map with [key]'s entry moved to the front, keeping every other entry's relative order. Returns the receiver
     * unchanged when the key is absent.
     */
    public fun moveToFirst(key: K): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.moveToFirst(key), values)
    /** A map with [key]'s entry moved to the end, keeping every other entry's relative order. */
    public fun moveToLast(key: K): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.moveToLast(key), values)
    /**
     * A map with [key]'s entry moved to ordinal [index], keeping every other entry's relative order. The index
     * names the position in the *resulting* map.
     */
    public fun moveTo(index: Int, key: K): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.moveTo(index, key), values)

    /**
     * A map without the entry keyed by [key]. Returns the receiver unchanged when absent; use [tryRemove] to learn
     * whether anything was removed.
     */
    public fun remove(key: K): PersistentOrderedMap<K, V> = tryRemove(key).map
    /** Remove the entry keyed by [key], reporting whether anything was removed. */
    public fun tryRemove(key: K): OrderedMapRemoveResult<K, V> {
        val removedKeys = keys.tryRemove(key)
        if (!removedKeys.removed) return OrderedMapRemoveResult(this, false)
        return OrderedMapRemoveResult(PersistentOrderedMap(removedKeys.set, values.remove(key)), true)
    }

    /**
     * A map without the entry at ordinal [index].
     *
     * @throws IndexOutOfBoundsException if the index is out of range.
     */
    public fun removeAt(index: Int): PersistentOrderedMap<K, V> {
        val key = keys[index]
        return PersistentOrderedMap(keys.removeAt(index), values.remove(key))
    }

    /**
     * A map without its first entry.
     *
     * @throws IndexOutOfBoundsException if the map is empty.
     */
    public fun removeFirst(): PersistentOrderedMap<K, V> = removeAt(0)
    /**
     * A map without its last entry.
     *
     * @throws IndexOutOfBoundsException if the map is empty.
     */
    public fun removeLast(): PersistentOrderedMap<K, V> = removeAt(size - 1)
    /** An empty map retaining the same policy. */
    public fun clear(): PersistentOrderedMap<K, V> = if (isEmpty) this else empty(policy)

    /**
     * The [count] entries starting at ordinal [index], as a map, keeping their relative order.
     *
     * @throws IndexOutOfBoundsException if that range does not lie inside the map.
     */
    public fun getRange(index: Int, count: Int): PersistentOrderedMap<K, V> {
        val nextKeys = keys.getRange(index, count)
        var nextValues = PersistentHashMap.empty<K, V>(policy)
        for (key in nextKeys) {
            val indexed = values.getEntry(key) ?: throw indexDisagreement()
            nextValues = nextValues.add(indexed.key, indexed.value)
        }
        return PersistentOrderedMap(nextKeys, nextValues)
    }

    /** The first [count] entries, as a map. */
    public fun take(count: Int): PersistentOrderedMap<K, V> = getRange(0, count)
    /** Every entry after the first [count], as a map. */
    public fun drop(count: Int): PersistentOrderedMap<K, V> = getRange(count, size - count)
    /** A map with the entries in the opposite order. Key identity and values are unchanged; only position is. */
    public fun reverse(): PersistentOrderedMap<K, V> = PersistentOrderedMap(keys.reverse(), values)

    /** Stable one-shot entry sort; no ordering policy is retained. */
    public fun sort(comparator: Comparator<in OrderedMapEntry<K, V>>): PersistentOrderedMap<K, V> {
        val nextKeys = keys.sort(Comparator { left, right -> comparator.compare(attach(left), attach(right)) })
        return if (nextKeys === keys) this else PersistentOrderedMap(nextKeys, values)
    }

    /** The entries in order. */
    public fun toList(): List<OrderedMapEntry<K, V>> = keys.map { attach(it) }

    /**
     * Whether this map and [other] share their order index. The two indexes are tracked separately, so an edit that
     * changes only a value leaves the order index shared.
     */
    public fun sharesOrderWith(other: PersistentOrderedMap<K, V>): Boolean = keys === other.keys
    /** Whether this map and [other] share their value index. */
    public fun sharesValuesWith(other: PersistentOrderedMap<K, V>): Boolean = values.sharesRootWith(other.values)

    /**
     * Walk both indexes and check that they agree - matching counts, and every ordered key present in the value
     * index. A defensive audit for tests, not a routine operation.
     *
     * @throws IllegalStateException on the first violation found.
     */
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
