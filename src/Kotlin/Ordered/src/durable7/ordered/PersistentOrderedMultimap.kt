/*
 * Persistent insertion-ordered multimap: ordered keys, each with an ordered value group.
 *
 * Order is retained at both levels - keys in the order they first acquired a value, values in the
 * order they were first added to their key. Value groups are sets, so re-adding a pair disturbs
 * neither ordering.
 */
package durable7.ordered

import durable7.hamt.HashPolicy
import durable7.hamt.defaultHashPolicy

/** One key-value pair as the insertion-ordered multimap presents it. */
public data class OrderedMultimapEntry<K, V>(public val key: K, public val value: V)

/** Key and pair counts returned by a successful structural audit. */
public data class PersistentOrderedMultimapStatistics(
    public val keyCount: Int,
    public val pairCount: Long,
)

/**
 * Immutable key-grouped multimap preserving first-insertion order for key groups and, independently,
 * for distinct values within every group. Iteration is grouped, not a global interleaving of pair arrivals.
 */
public class PersistentOrderedMultimap<K, V> private constructor(
    private val groups: PersistentOrderedMap<K, PersistentOrderedSet<V>>,
    public val valuePolicy: HashPolicy<V>,
    public val pairCount: Long,
) : Iterable<OrderedMultimapEntry<K, V>> {
    public companion object {
        public fun <K, V> empty(
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentOrderedMultimap<K, V> =
            PersistentOrderedMultimap(PersistentOrderedMap.empty(keyPolicy), valuePolicy, 0L)

        public fun <K, V> from(
            entries: Iterable<Pair<K, V>>,
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentOrderedMultimap<K, V> {
            var result = empty<K, V>(keyPolicy, valuePolicy)
            for ((key, value) in entries) result = result.add(key, value)
            return result
        }
    }

    public val keyPolicy: HashPolicy<K> get() = groups.policy
    public val keyCount: Int get() = groups.size
    public val isEmpty: Boolean get() = pairCount == 0L

    public fun containsKey(key: K): Boolean = groups.containsKey(key)
    public fun contains(key: K, value: V): Boolean = groups[key]?.contains(value) == true
    public fun countValues(key: K): Int = groups[key]?.size ?: 0
    public fun valuesFor(key: K): PersistentOrderedSet<V>? = groups[key]
    public fun actualKey(key: K): K? = groups.tryGet(key).let { if (it.found) it.key else null }
    public fun actualValue(key: K, value: V): V? =
        groups[key]?.tryGetValue(value)?.let { if (it.found) it.value else null }

    public fun add(key: K, value: V): PersistentOrderedMultimap<K, V> {
        val indexed = groups.tryGet(key)
        if (!indexed.found) {
            val group = PersistentOrderedSet.empty<V>(valuePolicy).add(value)
            return PersistentOrderedMultimap(groups.add(key, group), valuePolicy, Math.addExact(pairCount, 1L))
        }
        val group = requireNotNull(indexed.value)
        val updated = group.add(value)
        if (updated === group) return this
        return PersistentOrderedMultimap(
            groups.set(indexed.key, updated),
            valuePolicy,
            Math.addExact(pairCount, 1L),
        )
    }

    public fun remove(key: K, value: V): PersistentOrderedMultimap<K, V> {
        val indexed = groups.tryGet(key)
        if (!indexed.found) return this
        val group = requireNotNull(indexed.value)
        val removed = group.tryRemove(value)
        if (!removed.removed) return this
        val nextGroups = if (removed.set.isEmpty) groups.remove(indexed.key) else groups.set(indexed.key, removed.set)
        return PersistentOrderedMultimap(nextGroups, valuePolicy, pairCount - 1L)
    }

    public fun removeKey(key: K): PersistentOrderedMultimap<K, V> {
        val indexed = groups.tryGet(key)
        if (!indexed.found) return this
        val group = requireNotNull(indexed.value)
        return PersistentOrderedMultimap(groups.remove(indexed.key), valuePolicy, pairCount - group.size.toLong())
    }

    public fun clear(): PersistentOrderedMultimap<K, V> =
        if (isEmpty) this else empty(keyPolicy, valuePolicy)

    public fun toList(): List<OrderedMultimapEntry<K, V>> = iterator().asSequence().toList()

    public fun sharesGroupsWith(other: PersistentOrderedMultimap<K, V>): Boolean =
        groups.sharesOrderWith(other.groups) && groups.sharesValuesWith(other.groups)

    public fun validateStructure(): PersistentOrderedMultimapStatistics {
        groups.validateStructure()
        var counted = 0L
        for (group in groups) {
            check(!group.value.isEmpty) { "PersistentOrderedMultimap stores an empty group." }
            group.value.validateStructure()
            counted = Math.addExact(counted, group.value.size.toLong())
        }
        check(counted == pairCount) { "PersistentOrderedMultimap pair count disagrees with its groups." }
        return PersistentOrderedMultimapStatistics(keyCount, pairCount)
    }

    override fun iterator(): Iterator<OrderedMultimapEntry<K, V>> = sequence {
        for (group in groups) for (value in group.value) yield(OrderedMultimapEntry(group.key, value))
    }.iterator()
}
