package durable7.hamt

public data class MultimapEntry<K, V>(public val key: K, public val value: V)

public data class PersistentHashMultimapStatistics(
    public val keyCount: Int,
    public val pairCount: Long,
)

/** Immutable set-valued multimap retaining independent key and value policies. */
public class PersistentHashMultimap<K, V> private constructor(
    private val groups: PersistentHashMap<K, PersistentHashSet<V>>,
    public val valuePolicy: HashPolicy<V>,
    public val pairCount: Long,
) : Iterable<MultimapEntry<K, V>> {
    public companion object {
        public fun <K, V> empty(
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentHashMultimap<K, V> =
            PersistentHashMultimap(PersistentHashMap.empty(keyPolicy), valuePolicy, 0L)

        public fun <K, V> from(
            entries: Iterable<Pair<K, V>>,
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentHashMultimap<K, V> {
            var result = empty<K, V>(keyPolicy, valuePolicy)
            for ((key, value) in entries) result = result.add(key, value)
            return result
        }
    }

    public val keyPolicy: HashPolicy<K> get() = groups.policy
    public val keyCount: Int get() = groups.size
    public val isEmpty: Boolean get() = pairCount == 0L

    public fun containsKey(key: K): Boolean = groups.containsKey(key)
    public fun contains(key: K, value: V): Boolean = groups.getEntry(key)?.value?.contains(value) == true
    public fun actualKey(key: K): K? = groups.getEntry(key)?.key
    public fun valuesFor(key: K): PersistentHashSet<V>? = groups.getEntry(key)?.value

    public fun add(key: K, value: V): PersistentHashMultimap<K, V> {
        val indexed = groups.getEntry(key)
        if (indexed == null) {
            val group = PersistentHashSet.empty<V>(valuePolicy).add(value)
            return PersistentHashMultimap(groups.add(key, group), valuePolicy, Math.addExact(pairCount, 1L))
        }
        val added = indexed.value.tryAdd(value)
        if (!added.added) return this
        return PersistentHashMultimap(
            groups.put(indexed.key, added.value),
            valuePolicy,
            Math.addExact(pairCount, 1L),
        )
    }

    public fun remove(key: K, value: V): PersistentHashMultimap<K, V> {
        val indexed = groups.getEntry(key) ?: return this
        val removed = indexed.value.tryRemove(value) ?: return this
        val nextGroups = if (removed.set.isEmpty) groups.remove(indexed.key) else groups.put(indexed.key, removed.set)
        return PersistentHashMultimap(nextGroups, valuePolicy, pairCount - 1L)
    }

    public fun removeKey(key: K): PersistentHashMultimap<K, V> {
        val indexed = groups.getEntry(key) ?: return this
        return PersistentHashMultimap(groups.remove(indexed.key), valuePolicy, pairCount - indexed.value.size.toLong())
    }

    public fun clear(): PersistentHashMultimap<K, V> =
        if (isEmpty) this else empty(keyPolicy, valuePolicy)

    /** Receiver-policy union; the complete argument is normalized while enumerating it. */
    public fun union(other: PersistentHashMultimap<K, V>): PersistentHashMultimap<K, V> {
        var result = this
        for (entry in other) result = result.add(entry.key, entry.value)
        return result
    }

    public fun intersect(other: PersistentHashMultimap<K, V>): PersistentHashMultimap<K, V> {
        val normalized = clear().union(other)
        var result = clear()
        for (entry in this) if (normalized.contains(entry.key, entry.value)) result = result.add(entry.key, entry.value)
        return result
    }

    public fun except(other: PersistentHashMultimap<K, V>): PersistentHashMultimap<K, V> {
        val normalized = clear().union(other)
        var result = this
        for (entry in normalized) result = result.remove(entry.key, entry.value)
        return result
    }

    public fun toList(): List<MultimapEntry<K, V>> = iterator().asSequence().toList()

    public fun validateStructure(): PersistentHashMultimapStatistics {
        var counted = 0L
        var countedKeys = 0
        for (group in groups) {
            check(!group.value.isEmpty) { "PersistentHashMultimap stores an empty group." }
            counted = Math.addExact(counted, group.value.size.toLong())
            countedKeys += 1
        }
        check(countedKeys == keyCount && counted == pairCount) { "PersistentHashMultimap counts disagree." }
        return PersistentHashMultimapStatistics(keyCount, pairCount)
    }

    override fun iterator(): Iterator<MultimapEntry<K, V>> = sequence {
        for (group in groups) for (value in group.value) yield(MultimapEntry(group.key, value))
    }.iterator()
}

public data class PersistentRelationStatistics(
    public val leftCount: Int,
    public val rightCount: Int,
    public val pairCount: Long,
)

/** Bidirectional persistent many-to-many relation with exact inverse indexes. */
public class PersistentRelation<L, R> private constructor(
    private val forward: PersistentHashMultimap<L, R>,
    private val reverse: PersistentHashMultimap<R, L>,
) : Iterable<Pair<L, R>> {
    public companion object {
        public fun <L, R> empty(
            leftPolicy: HashPolicy<L> = defaultHashPolicy(),
            rightPolicy: HashPolicy<R> = defaultHashPolicy(),
        ): PersistentRelation<L, R> = PersistentRelation(
            PersistentHashMultimap.empty(leftPolicy, rightPolicy),
            PersistentHashMultimap.empty(rightPolicy, leftPolicy),
        )
    }

    public val pairCount: Long get() = forward.pairCount
    public val leftCount: Int get() = forward.keyCount
    public val rightCount: Int get() = reverse.keyCount
    public val isEmpty: Boolean get() = forward.isEmpty

    public fun contains(left: L, right: R): Boolean = forward.contains(left, right)
    public fun rightsFor(left: L): PersistentHashSet<R>? = forward.valuesFor(left)
    public fun leftsFor(right: R): PersistentHashSet<L>? = reverse.valuesFor(right)

    public fun add(left: L, right: R): PersistentRelation<L, R> =
        if (contains(left, right)) this
        else PersistentRelation(forward.add(left, right), reverse.add(right, left))

    public fun remove(left: L, right: R): PersistentRelation<L, R> =
        if (!contains(left, right)) this
        else PersistentRelation(forward.remove(left, right), reverse.remove(right, left))

    public fun removeLeft(left: L): PersistentRelation<L, R> {
        val rights = rightsFor(left)?.toList() ?: return this
        var result = this
        for (right in rights) result = result.remove(left, right)
        return result
    }

    public fun removeRight(right: R): PersistentRelation<L, R> {
        val lefts = leftsFor(right)?.toList() ?: return this
        var result = this
        for (left in lefts) result = result.remove(left, right)
        return result
    }

    public fun clear(): PersistentRelation<L, R> =
        if (isEmpty) this else PersistentRelation(forward.clear(), reverse.clear())

    public fun inverse(): PersistentRelation<R, L> = PersistentRelation(reverse, forward)

    public fun validateStructure(): PersistentRelationStatistics {
        forward.validateStructure()
        reverse.validateStructure()
        check(forward.pairCount == reverse.pairCount) { "PersistentRelation pair counts disagree." }
        for (entry in forward) check(reverse.contains(entry.value, entry.key)) { "PersistentRelation inverse is missing." }
        for (entry in reverse) check(forward.contains(entry.value, entry.key)) { "PersistentRelation forward index is missing." }
        return PersistentRelationStatistics(leftCount, rightCount, pairCount)
    }

    override fun iterator(): Iterator<Pair<L, R>> = forward.asSequence().map { it.key to it.value }.iterator()
}
