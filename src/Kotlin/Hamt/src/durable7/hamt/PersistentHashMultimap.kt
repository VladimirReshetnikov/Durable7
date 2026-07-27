/*
 * Persistent set-valued hash multimap.
 *
 * Maps each key to a nonempty value set, so adding a present pair is a no-op and removing a key's
 * last value removes the key itself. Distinct-key and pair cardinalities are tracked separately,
 * because neither can be derived from the other in constant time.
 */
package durable7.hamt

/** One key-value pair as the multimap presents it. */
public data class MultimapEntry<K, V>(public val key: K, public val value: V)

/** Key and pair counts returned by a successful structural audit. */
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
        /** An empty multimap. The key and value policies are independent and both are retained by identity. */
        public fun <K, V> empty(
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentHashMultimap<K, V> =
            PersistentHashMultimap(PersistentHashMap.empty(keyPolicy), valuePolicy, 0L)

        /** A multimap holding every pair. Duplicate pairs collapse, since the values for a key form a set. */
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

    /** The hash policy used for keys. */
    public val keyPolicy: HashPolicy<K> get() = groups.policy
    /** Number of distinct keys, which is fewer than [pairCount] whenever any key has several values. */
    public val keyCount: Int get() = groups.size
    /** Whether the multimap holds no pairs. */
    public val isEmpty: Boolean get() = pairCount == 0L

    /**
     * Whether [key] has at least one value. A key never remains with an empty group: removing its last value
     * removes the key.
     */
    public fun containsKey(key: K): Boolean = groups.containsKey(key)
    /** Whether the pair is present. */
    public fun contains(key: K, value: V): Boolean = groups.getEntry(key)?.value?.contains(value) == true
    /**
     * The *stored* key representative equal to [key], or `null` when absent. The first representative of each
     * equivalence class is the one kept, so this need not be the instance passed in.
     */
    public fun actualKey(key: K): K? = groups.getEntry(key)?.key
    /**
     * The values for [key] as a set, or `null` when the key is absent. Never an empty set: absence and emptiness
     * are the same state here.
     */
    public fun valuesFor(key: K): PersistentHashSet<V>? = groups.getEntry(key)?.value

    /**
     * A multimap with the pair added. Returns the receiver unchanged when the pair is already present, since the
     * values for a key form a set.
     */
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

    /**
     * A multimap without the pair. Removing a key's last value removes the key itself, so no empty group is left
     * behind. Returns the receiver unchanged when the pair is absent.
     */
    public fun remove(key: K, value: V): PersistentHashMultimap<K, V> {
        val indexed = groups.getEntry(key) ?: return this
        val removed = indexed.value.tryRemove(value) ?: return this
        val nextGroups = if (removed.set.isEmpty) groups.remove(indexed.key) else groups.put(indexed.key, removed.set)
        return PersistentHashMultimap(nextGroups, valuePolicy, pairCount - 1L)
    }

    /** A multimap without [key] and all of its values. Returns the receiver unchanged when the key is absent. */
    public fun removeKey(key: K): PersistentHashMultimap<K, V> {
        val indexed = groups.getEntry(key) ?: return this
        return PersistentHashMultimap(groups.remove(indexed.key), valuePolicy, pairCount - indexed.value.size.toLong())
    }

    /** An empty multimap retaining both policies. */
    public fun clear(): PersistentHashMultimap<K, V> =
        if (isEmpty) this else empty(keyPolicy, valuePolicy)

    /** Receiver-policy union; the complete argument is normalized while enumerating it. */
    public fun union(other: PersistentHashMultimap<K, V>): PersistentHashMultimap<K, V> {
        var result = this
        for (entry in other) result = result.add(entry.key, entry.value)
        return result
    }

    /** The pairs present in both multimaps, compared under the receiver's policies. */
    public fun intersect(other: PersistentHashMultimap<K, V>): PersistentHashMultimap<K, V> {
        val normalized = clear().union(other)
        var result = clear()
        for (entry in this) if (normalized.contains(entry.key, entry.value)) result = result.add(entry.key, entry.value)
        return result
    }

    /** The pairs of this multimap absent from [other], compared under the receiver's policies. */
    public fun except(other: PersistentHashMultimap<K, V>): PersistentHashMultimap<K, V> {
        val normalized = clear().union(other)
        var result = this
        for (entry in normalized) result = result.remove(entry.key, entry.value)
        return result
    }

    /** Every pair, as a flat list. */
    public fun toList(): List<MultimapEntry<K, V>> = iterator().asSequence().toList()

    /**
     * Walk the whole multimap and check its invariants - the cached pair count, the key count, and that no group is
     * empty - returning its shape measurements. A defensive audit for tests, not a routine operation.
     *
     * @throws IllegalStateException on the first violation found.
     */
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

/** Left, right, and pair counts returned by a successful structural audit. */
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
        /** An empty relation. The left and right policies are independent and both are retained by identity. */
        public fun <L, R> empty(
            leftPolicy: HashPolicy<L> = defaultHashPolicy(),
            rightPolicy: HashPolicy<R> = defaultHashPolicy(),
        ): PersistentRelation<L, R> = PersistentRelation(
            PersistentHashMultimap.empty(leftPolicy, rightPolicy),
            PersistentHashMultimap.empty(rightPolicy, leftPolicy),
        )
    }

    /** Number of related pairs. */
    public val pairCount: Long get() = forward.pairCount
    /** Number of distinct left values that relate to something. */
    public val leftCount: Int get() = forward.keyCount
    /** Number of distinct right values that relate to something. */
    public val rightCount: Int get() = reverse.keyCount
    /** Whether the relation holds no pairs. */
    public val isEmpty: Boolean get() = forward.isEmpty

    /** Whether [left] and [right] are related. */
    public fun contains(left: L, right: R): Boolean = forward.contains(left, right)
    /** The right values related to [left], or `null` when it relates to nothing. Never an empty set. */
    public fun rightsFor(left: L): PersistentHashSet<R>? = forward.valuesFor(left)
    /**
     * The left values related to [right], or `null` when it relates to nothing. Costs the same as [rightsFor],
     * since both directions are indexed.
     */
    public fun leftsFor(right: R): PersistentHashSet<L>? = reverse.valuesFor(right)

    /**
     * A relation with the pair added to both directions. Returns the receiver unchanged when the pair is already
     * present.
     */
    public fun add(left: L, right: R): PersistentRelation<L, R> =
        if (contains(left, right)) this
        else PersistentRelation(forward.add(left, right), reverse.add(right, left))

    /** A relation without the pair, removed from both directions. Returns the receiver unchanged when absent. */
    public fun remove(left: L, right: R): PersistentRelation<L, R> =
        if (!contains(left, right)) this
        else PersistentRelation(forward.remove(left, right), reverse.remove(right, left))

    /** A relation without [left] and every pair it participates in. */
    public fun removeLeft(left: L): PersistentRelation<L, R> {
        val rights = rightsFor(left)?.toList() ?: return this
        var result = this
        for (right in rights) result = result.remove(left, right)
        return result
    }

    /** A relation without [right] and every pair it participates in. */
    public fun removeRight(right: R): PersistentRelation<L, R> {
        val lefts = leftsFor(right)?.toList() ?: return this
        var result = this
        for (left in lefts) result = result.remove(left, right)
        return result
    }

    /** An empty relation retaining both policies. */
    public fun clear(): PersistentRelation<L, R> =
        if (isEmpty) this else PersistentRelation(forward.clear(), reverse.clear())

    /**
     * The relation with its two sides exchanged. O(1): both directions are already indexed, so this swaps them
     * rather than rebuilding.
     */
    public fun inverse(): PersistentRelation<R, L> = PersistentRelation(reverse, forward)

    /**
     * Walk both directions and check that they agree - matching pair counts, and every forward pair present in
     * reverse - returning shape measurements. A defensive audit for tests, not a routine operation.
     *
     * @throws IllegalStateException on the first violation found.
     */
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
