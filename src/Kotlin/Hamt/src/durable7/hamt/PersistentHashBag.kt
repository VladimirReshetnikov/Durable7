package durable7.hamt

/**
 * Immutable unordered multiset backed by [PersistentHashMap].
 *
 * One positive [Int] multiplicity is stored per [HashPolicy] equivalence class. [totalCount] is the
 * expanded [Long] occurrence count; there is deliberately no ambiguous `size` or `count` member.
 */
public class PersistentHashBag<T> private constructor(
    private val counts: PersistentHashMap<T, Int>,
    public val totalCount: Long,
) : Iterable<T> {
    public companion object {
        /** Returns an empty bag retaining [policy] by reference. */
        public fun <T> empty(policy: HashPolicy<T> = defaultHashPolicy()): PersistentHashBag<T> =
            PersistentHashBag(PersistentHashMap.empty(policy), 0L)

        /**
         * Aggregates [items] in iteration order and retains each first equivalent representative.
         * Multiplicity addition is checked and each item is hashed once by the map combinator.
         */
        public fun <T> from(
            items: Iterable<T>,
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): PersistentHashBag<T> {
            var result = empty<T>(policy)
            for (item in items) result = result.add(item)
            return result
        }
    }

    /** Number of policy equivalence classes. */
    public val distinctCount: Int get() = counts.size

    public val isEmpty: Boolean get() = counts.isEmpty

    public val policy: HashPolicy<T> get() = counts.policy

    public fun sharesRootWith(other: PersistentHashBag<T>): Boolean = counts.sharesRootWith(other.counts)

    public fun contains(item: T): Boolean = counts.containsKey(item)

    /** Returns the multiplicity of [item]'s equivalence class, or zero when absent. */
    public fun countOf(item: T): Int = counts.getEntry(item)?.value ?: 0

    /**
     * Returns the retained representative equivalent to [item], or `null` on a miss.
     * For nullable element types, use [contains] to distinguish a stored `null` from absence.
     */
    public fun get(item: T): T? = counts.getEntry(item)?.key

    public fun add(item: T): PersistentHashBag<T> = addCopies(item, 1)

    /**
     * Adds [copyCount] occurrences. Negative counts fail before hashing; zero is an exact no-op.
     * Per-class multiplicity and the defensive total-count addition are checked.
     */
    public fun addCopies(item: T, copyCount: Int): PersistentHashBag<T> {
        require(copyCount >= 0) { "copyCount must be nonnegative." }
        if (copyCount == 0) return this
        val nextTotal = Math.addExact(totalCount, copyCount.toLong())
        val update = counts.addOrUpdate(
            item,
            { copyCount },
            { _, stored -> Math.addExact(stored, copyCount) },
        )
        return withCounts(update.map, nextTotal)
    }

    public fun remove(item: T): PersistentHashBag<T> = removeCopies(item, 1)

    /** Removes up to [copyCount] occurrences, saturating at zero. */
    public fun removeCopies(item: T, copyCount: Int): PersistentHashBag<T> {
        require(copyCount >= 0) { "copyCount must be nonnegative." }
        if (copyCount == 0) return this
        val stored = counts.getEntry(item) ?: return this
        if (copyCount >= stored.value) {
            val removed = counts.tryRemoveEntry(item)
                ?: error("A located hash-bag entry could not be removed.")
            return withCounts(removed.map, totalCount - removed.entry.value.toLong())
        }
        return withCounts(
            counts.put(item, stored.value - copyCount),
            totalCount - copyCount.toLong(),
        )
    }

    /** Removes the complete equivalence class represented by [item]. */
    public fun removeAll(item: T): PersistentHashBag<T> {
        val removed = counts.tryRemoveEntry(item) ?: return this
        return withCounts(removed.map, totalCount - removed.entry.value.toLong())
    }

    public fun clear(): PersistentHashBag<T> = withCounts(counts.clear(), 0L)

    /** Multiset union: maximum multiplicity per receiver-policy class. */
    public fun union(other: PersistentHashBag<T>): PersistentHashBag<T> =
        combine(other, BagOperation.UNION)

    /** Multiset intersection: minimum multiplicity per receiver-policy class. */
    public fun intersect(other: PersistentHashBag<T>): PersistentHashBag<T> =
        combine(other, BagOperation.INTERSECT)

    /** Saturating receiver-minus-argument multiset subtraction. */
    public fun except(other: PersistentHashBag<T>): PersistentHashBag<T> =
        combine(other, BagOperation.EXCEPT)

    /** Checked additive multiset sum. */
    public fun sum(other: PersistentHashBag<T>): PersistentHashBag<T> =
        combine(other, BagOperation.SUM)

    /** One retained representative per class in stable-but-unspecified CHAMP order. */
    public fun distinctItems(): Sequence<T> = counts.keys()

    /** Representative/multiplicity entries in the same order as [distinctItems]. */
    public fun entries(): Sequence<HamtEntry<T, Int>> = counts.entries()

    /** Expanded occurrence sequence; copies of one representative are contiguous. */
    public fun asSequence(): Sequence<T> = sequence {
        for (entry in counts) {
            for (copy in 0 until entry.value) yield(entry.key)
        }
    }

    override fun iterator(): Iterator<T> = asSequence().iterator()

    internal fun hasValidInvariants(): Boolean {
        val statistics = counts.champStatistics()
        if (statistics.invalidLeafChildren != 0 ||
            statistics.underfullBitmapNodes != 0 ||
            statistics.invalidHashRouting != 0 ||
            statistics.inlinePayloads + statistics.collisionPayloads != distinctCount
        ) return false

        var computedTotal = 0L
        return try {
            for (entry in counts) {
                if (entry.value <= 0) return false
                computedTotal = Math.addExact(computedTotal, entry.value.toLong())
            }
            computedTotal == totalCount
        } catch (_: ArithmeticException) {
            false
        }
    }

    private fun combine(other: PersistentHashBag<T>, operation: BagOperation): PersistentHashBag<T> {
        val normalized = if (policy === other.policy) other else normalizeArgument(other)
        if (this === normalized) {
            return when (operation) {
                BagOperation.UNION, BagOperation.INTERSECT -> this
                BagOperation.EXCEPT -> clear()
                BagOperation.SUM -> sumNormalized(normalized)
            }
        }
        return when (operation) {
            BagOperation.UNION -> unionNormalized(normalized)
            BagOperation.INTERSECT -> intersectNormalized(normalized)
            BagOperation.EXCEPT -> exceptNormalized(normalized)
            BagOperation.SUM -> sumNormalized(normalized)
        }
    }

    /** Eagerly collapses [other] under this bag's exact policy object. */
    private fun normalizeArgument(other: PersistentHashBag<T>): PersistentHashBag<T> {
        var normalizedCounts = PersistentHashMap.empty<T, Int>(policy)
        var normalizedTotal = 0L
        for (entry in other.counts) {
            normalizedCounts = normalizedCounts.addOrUpdate(
                entry.key,
                { entry.value },
                { _, stored -> Math.addExact(stored, entry.value) },
            ).map
            normalizedTotal = Math.addExact(normalizedTotal, entry.value.toLong())
        }
        return PersistentHashBag(normalizedCounts, normalizedTotal)
    }

    private fun unionNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (other.isEmpty) return this
        var resultCounts = counts
        var resultTotal = totalCount
        for (entry in other.counts) {
            var increase = entry.value
            val update = resultCounts.addOrUpdate(
                entry.key,
                { entry.value },
                { _, receiverCount ->
                    if (receiverCount >= entry.value) {
                        increase = 0
                        receiverCount
                    } else {
                        increase = entry.value - receiverCount
                        entry.value
                    }
                },
            )
            resultCounts = update.map
            resultTotal = Math.addExact(resultTotal, increase.toLong())
        }
        return withCounts(resultCounts, resultTotal)
    }

    private fun intersectNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (isEmpty) return this
        if (other.isEmpty) return clear()
        var resultCounts = counts
        var resultTotal = totalCount
        for (entry in counts) {
            val argumentCount = other.counts.getEntry(entry.key)?.value
            when {
                argumentCount == null -> {
                    val removed = resultCounts.tryRemoveEntry(entry.key)
                        ?: error("A located hash-bag entry could not be removed.")
                    resultCounts = removed.map
                    resultTotal -= removed.entry.value.toLong()
                }
                argumentCount < entry.value -> {
                    resultCounts = resultCounts.put(entry.key, argumentCount)
                    resultTotal -= (entry.value - argumentCount).toLong()
                }
            }
        }
        return withCounts(resultCounts, resultTotal)
    }

    private fun exceptNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (isEmpty || other.isEmpty) return this
        var resultCounts = counts
        var resultTotal = totalCount
        for (entry in counts) {
            val argumentCount = other.counts.getEntry(entry.key)?.value ?: continue
            if (argumentCount >= entry.value) {
                val removed = resultCounts.tryRemoveEntry(entry.key)
                    ?: error("A located hash-bag entry could not be removed.")
                resultCounts = removed.map
                resultTotal -= removed.entry.value.toLong()
            } else {
                resultCounts = resultCounts.put(entry.key, entry.value - argumentCount)
                resultTotal -= argumentCount.toLong()
            }
        }
        return withCounts(resultCounts, resultTotal)
    }

    private fun sumNormalized(other: PersistentHashBag<T>): PersistentHashBag<T> {
        if (other.isEmpty) return this
        var resultCounts = counts
        var resultTotal = totalCount
        for (entry in other.counts) {
            resultCounts = resultCounts.addOrUpdate(
                entry.key,
                { entry.value },
                { _, receiverCount -> Math.addExact(receiverCount, entry.value) },
            ).map
            resultTotal = Math.addExact(resultTotal, entry.value.toLong())
        }
        return withCounts(resultCounts, resultTotal)
    }

    private fun withCounts(
        value: PersistentHashMap<T, Int>,
        newTotalCount: Long,
    ): PersistentHashBag<T> {
        if (value === counts) {
            check(newTotalCount == totalCount) {
                "An unchanged hash-bag map cannot have a different total count."
            }
            return this
        }
        return PersistentHashBag(value, newTotalCount)
    }

    private enum class BagOperation { UNION, INTERSECT, EXCEPT, SUM }
}
