/*
 * Checkpoint-differential persistent sorted map.
 *
 * A version carries three immutable roots: the checkpoint state, the current state, and an ordered
 * change index holding exactly one before/after record per key class on which the two states differ.
 * A plain persistent map makes versions cheap but a root handle alone does not name the changed keys,
 * and a write log names operations while leaving repeated writes, cancellations, and ordering to be
 * resolved later. This structure maintains the exact net answer online, as the edits happen.
 */
package durable7.fingertree

/**
 * A presence-safe endpoint value in a recorded change.
 *
 * An endpoint is `DeltaMapValue<V>?`: `null` means the key class was absent at that endpoint, while a
 * non-null wrapper means it was present and carries [value], which may itself be `null` when [V] is a
 * nullable type. A bare `V?` cannot express that distinction, which is why the wrapper exists - the
 * same reason [RopeCursorPeek] and [RrbPop] exist elsewhere in this workspace.
 *
 * Structural equality on this wrapper is the value type's own equality, not the map's retained
 * [ValueEqualityPolicy]; the map never uses it to decide cancellation.
 */
public data class DeltaMapValue<V>(public val value: V)

/** Classifies one net key change relative to a [PersistentDeltaMap] checkpoint. */
public enum class PersistentMapChangeKind {
    /** The key class is absent at the checkpoint and present in the current map. */
    ADDED,

    /** The key class is present at the checkpoint and absent from the current map. */
    REMOVED,

    /** The key class is present at both endpoints with non-equivalent values. */
    UPDATED,
}

/**
 * One exact net change between a version's checkpoint and its current state.
 *
 * [key] is the checkpoint representative when the class was present at the checkpoint; otherwise it is
 * the current addition episode's first representative. [before] and [after] are presence-safe: `null`
 * means absent at that endpoint, so a stored `null` value stays distinguishable from a missing entry.
 *
 * The constructor is public for diagnostics and for building expectations in tests. A map never
 * produces a record that is absent at both endpoints; [kind] rejects one if it is handed one.
 */
public data class PersistentMapChange<K, V>(
    public val key: K,
    public val before: DeltaMapValue<V>?,
    public val after: DeltaMapValue<V>?,
) {
    /**
     * The classification implied by the two endpoints.
     *
     * @throws IllegalStateException if both endpoints are absent, which is not a net change.
     */
    public val kind: PersistentMapChangeKind
        get() = when {
            before != null && after != null -> PersistentMapChangeKind.UPDATED
            before != null -> PersistentMapChangeKind.REMOVED
            after != null -> PersistentMapChangeKind.ADDED
            else -> throw IllegalStateException("A change cannot be absent at both endpoints.")
        }
}

/**
 * Representation statistics returned by a successful [PersistentDeltaMap.validateStructure] audit.
 *
 * [addedCount], [removedCount], and [updatedCount] partition [changeCount]. [isClean] reports whether
 * the current root is the exact checkpoint root, which the canonicalization rule ties to an empty
 * change index in both directions.
 */
public data class PersistentDeltaMapStatistics(
    public val size: Int,
    public val checkpointSize: Int,
    public val changeCount: Int,
    public val addedCount: Int,
    public val removedCount: Int,
    public val updatedCount: Int,
    public val isClean: Boolean,
)

/**
 * An immutable, fully persistent sorted map with an explicit checkpoint and a coalesced ordered index
 * of the exact net changes from that checkpoint.
 *
 * The first effective write to a key class captures its checkpoint-relative `before`; later writes
 * replace only `after`; returning a class to its checkpoint state removes its record entirely. When
 * the change index becomes empty the current root snaps back to the *exact* checkpoint root, so a
 * wholly cancelled epoch is storage-clean as well as extensionally clean and [checkpoint] and
 * [rollback] stay O(1) root swaps.
 *
 * ## Bounds
 *
 * Let `N = max(2, |dom(checkpoint) union dom(current)|)`, counting comparator-equivalence classes
 * rather than concrete key objects, and let `k` be the number of net-changed classes. The substrate is
 * this workspace's [SortedMap] over a height-balanced measured tree, so point lookup, point update and
 * removal, rank, select, and neighbor queries are O(log N). An effective point edit costs
 * `O(log N + log(k + 1)) = O(log N)` because it touches the current state and at most one record.
 * [size], [isEmpty], [changeCount], [hasChanges], and [isClean] read cached measures or references and
 * are O(1). [checkpoint] and [rollback] are O(1). [getChange] is O(log(k + 1)), searching the change
 * index rather than the current state.
 *
 * [minEntry] and [maxEntry] are **Theta(log N), not O(1)**: the substrate caches subtree sizes but no
 * extremes, so reaching the leftmost or rightmost leaf costs a full descent like any other rank. The
 * C# baseline documents them as O(1); this port does not, because this substrate does not deliver it.
 *
 * Fully consuming [getChanges] is Theta(k + 1), which is output-optimal and is the point of the
 * design: it avoids the Theta(k log(N/k + 1)) adversarial divergent-path walk that a reference-pruned
 * comparison between two path-copied balanced trees must perform. Because the change index is itself
 * an ordered map under the same comparator, [getChanges] with bounds is a boundary seek plus a bounded
 * walk - two partition-point descents and two O(log(k + 1)) splits - and never a filter over all `k`
 * records. A live version occupies O(N + k) nodes; retaining every successor adds O(log N) nodes per
 * changed point update, the same asymptotic persistence cost as an ordinary path-copied ordered map.
 *
 * ## Policies
 *
 * Key identity and output order come from a retained [Comparator]; two keys are the same class exactly
 * when it compares them equal. Semantic no-ops and change cancellation come from a retained
 * [ValueEqualityPolicy] over values. "Exact" therefore means exact *extensionally* under those two
 * policies, not by object identity of the retained key or value representatives. A baseline key
 * representative is retained across point updates and delete/re-add round trips; a newly added class
 * retains its first representative while its net-added record is active, and a later addition after
 * complete cancellation begins a new representative episode. Both policies are retained at
 * construction, so an emptied or range-restricted result stays usable.
 *
 * Policy side effects cannot be undone, but a throwing policy never publishes a partial successor:
 * every version is immutable, so the receiver and every retained branch remain valid and unchanged.
 * [checkpoint], [rollback], and [getChanges] invoke no key or value policy callback at all; the
 * bounded [getChanges] overload invokes only the O(log(k + 1)) key comparisons needed to seek the two
 * boundaries, and no value comparison. Concurrent reads over retained versions are safe when both
 * retained policy objects are safe for concurrent calls.
 *
 * The mutation surface is deliberately point-only. An eager bulk clear would produce Theta(N) removal
 * records and cannot simultaneously retain an ordinary map's O(1) clear bound, so callers start a fresh
 * epoch with [empty] when they do not need an enumerable delta, and remove entries explicitly when they
 * do. [setItems] is not an exception: it is exactly the fold over [setItem].
 */
public class PersistentDeltaMap<K, V> private constructor(
    public val currentSnapshot: SortedMap<K, V>,
    public val checkpointSnapshot: SortedMap<K, V>,
    private val changeIndex: SortedMap<K, DeltaMapRecord<V>>,
    public val comparator: Comparator<in K>,
    public val valueEquality: ValueEqualityPolicy<V>,
) : Iterable<SortedMapEntry<K, V>> {
    private data class DeltaMapRecord<V>(
        val before: DeltaMapValue<V>?,
        val after: DeltaMapValue<V>?,
    )

    public companion object {
        /**
         * An empty map using natural key order and natural value equality, whose current state is also
         * its checkpoint. O(1).
         */
        public fun <K : Comparable<K>, V> empty(): PersistentDeltaMap<K, V> =
            empty(naturalComparator(), ValueEqualityPolicy.natural())

        /**
         * An empty map ordered by [comparator] with value no-ops decided by [valueEquality], whose
         * current state is also its checkpoint. O(1).
         *
         * This is also the O(1) way to start an unrelated clean epoch. It is not the same as clearing
         * while retaining the old checkpoint, which the point-only mutation surface omits.
         */
        public fun <K, V> empty(
            comparator: Comparator<in K>,
            valueEquality: ValueEqualityPolicy<V> = ValueEqualityPolicy.natural(),
        ): PersistentDeltaMap<K, V> {
            val state = SortedMap.empty<K, V>(comparator)
            return PersistentDeltaMap(state, state, SortedMap.empty(comparator), comparator, valueEquality)
        }

        /**
         * A clean checkpoint holding every pair, in natural key order, with the last equivalent key
         * winning: the last pair of a class supplies both the retained key representative and the
         * value. O(m log m) for m supplied pairs.
         */
        public fun <K : Comparable<K>, V> from(entries: Iterable<Pair<K, V>>): PersistentDeltaMap<K, V> =
            from(entries, naturalComparator(), ValueEqualityPolicy.natural())

        /**
         * A clean checkpoint holding every pair, ordered by [comparator], with the last equivalent key
         * winning. O(m log m) for m supplied pairs.
         */
        public fun <K, V> from(
            entries: Iterable<Pair<K, V>>,
            comparator: Comparator<in K>,
            valueEquality: ValueEqualityPolicy<V> = ValueEqualityPolicy.natural(),
        ): PersistentDeltaMap<K, V> {
            val state = SortedMap.from(entries, comparator)
            return PersistentDeltaMap(state, state, SortedMap.empty(comparator), comparator, valueEquality)
        }
    }

    /** Number of current entries. O(1), from the substrate's cached subtree counts. */
    public val size: Int
        get() = currentSnapshot.size

    /** Whether the current map holds no entries. O(1). */
    public val isEmpty: Boolean
        get() = currentSnapshot.isEmpty

    /** Number of checkpoint entries. O(1). */
    public val checkpointSize: Int
        get() = checkpointSnapshot.size

    /** Number of exact net-changed key classes. O(1). */
    public val changeCount: Int
        get() = changeIndex.size

    /** Whether the current map differs semantically from its checkpoint. O(1). */
    public val hasChanges: Boolean
        get() = !changeIndex.isEmpty

    /**
     * Whether the current root is the exact checkpoint root, so this version records nothing. O(1), a
     * reference test rather than a comparison.
     *
     * This is the canonicalization invariant in both directions: a version with no changes reuses its
     * checkpoint root, and a version reusing its checkpoint root has no changes.
     */
    public val isClean: Boolean
        get() = changeIndex.isEmpty && currentSnapshot === checkpointSnapshot

    /** Whether a current entry exists for [key]'s class. O(log N). */
    public fun containsKey(key: K): Boolean = entryOf(currentSnapshot, key) != null

    /**
     * The current value for [key]'s class, or `null` when absent. O(log N).
     *
     * A stored `null` value is indistinguishable from a miss here; use [getEntry] or [containsKey]
     * when that matters.
     */
    public operator fun get(key: K): V? = entryOf(currentSnapshot, key)?.value

    /**
     * The retained current representative and value for [key]'s class, or `null` when absent. O(log N).
     *
     * This is the presence-safe lookup: a present entry is a non-null [SortedMapEntry] even when its
     * value is `null`.
     */
    public fun getEntry(key: K): SortedMapEntry<K, V>? = entryOf(currentSnapshot, key)

    /** The current entry at zero-based key rank, or `null` when the rank is out of range. O(log N). */
    public fun entryAt(rank: Int): SortedMapEntry<K, V>? = currentSnapshot.entryAt(rank)

    /** The zero-based rank of [key]'s class in the current map, or `null` when absent. O(log N). */
    public fun indexOfKey(key: K): Int? = currentSnapshot.indexOfKey(key)

    /**
     * The least current entry by key, or `null` when empty. Theta(log N), **not O(1)**.
     *
     * This is a rank-0 select, not a cached extreme: the substrate caches subtree sizes but no
     * extremes, so reaching the leftmost leaf costs the same descent as any other rank.
     */
    public fun minEntry(): SortedMapEntry<K, V>? = currentSnapshot.minEntry()

    /**
     * The greatest current entry by key, or `null` when empty. Theta(log N), **not O(1)**.
     *
     * A rank-`size - 1` select, for the same reason [minEntry] is a rank-0 select.
     */
    public fun maxEntry(): SortedMapEntry<K, V>? = currentSnapshot.maxEntry()

    /** The current entry with the largest key at or below [key], or `null` when none is. O(log N). */
    public fun floorEntry(key: K): SortedMapEntry<K, V>? = currentSnapshot.floorEntry(key)

    /** The current entry with the smallest key at or above [key], or `null` when none is. O(log N). */
    public fun ceilingEntry(key: K): SortedMapEntry<K, V>? = currentSnapshot.ceilingEntry(key)

    /** The current entry with the largest key strictly below [key], or `null` when none is. O(log N). */
    public fun lowerEntry(key: K): SortedMapEntry<K, V>? = currentSnapshot.lowerEntry(key)

    /** The current entry with the smallest key strictly above [key], or `null` when none is. O(log N). */
    public fun higherEntry(key: K): SortedMapEntry<K, V>? = currentSnapshot.higherEntry(key)

    /**
     * A plain current-state snapshot for the closed key range `[low, high]`. O(log N) to seek the two
     * boundaries and split, plus the substrate's structural sharing for the interior.
     *
     * This is a read: the result is an ordinary ordered map and does not open an epoch of its own. An
     * inverted range - [low] comparing greater than [high] under the retained comparator - yields an
     * empty map rather than failing.
     */
    public fun getKeyRange(low: K, high: K): SortedMap<K, V> = currentSnapshot.getKeyRange(low, high)

    /** The current keys in ascending order. Theta(n). */
    public fun keys(): List<K> = currentSnapshot.keys()

    /** The current values in ascending key order. Theta(n). */
    public fun values(): List<V> = currentSnapshot.values()

    /** The current entries in ascending key order. Theta(n). */
    public fun toList(): List<SortedMapEntry<K, V>> = currentSnapshot.toList()

    /**
     * Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N).
     *
     * A write whose value is equivalent to the current value under the retained [valueEquality] is a
     * semantic no-op and returns the receiver itself, recording nothing. A write that returns a class
     * to its checkpoint state removes that class's record, and once the last record goes the current
     * root snaps back to the exact checkpoint root.
     *
     * The class keeps its baseline key representative when it is present in the current map or has an
     * active record; only a genuinely new class stores the supplied [key].
     *
     * @throws ArithmeticException if the current map or the change index is already at [Int] capacity.
     */
    public fun setItem(key: K, value: V): PersistentDeltaMap<K, V> {
        val currentEntry = entryOf(currentSnapshot, key)
        if (currentEntry != null && valueEquality.areEqual(currentEntry.value, value)) {
            return this
        }

        val changeEntry = entryOf(changeIndex, key)
        if (currentEntry == null) {
            Math.addExact(currentSnapshot.size, 1)
        }
        if (changeEntry == null) {
            // The endpoints cannot cancel here: an absent class gains a present `after`, and a present
            // one already failed the no-op test above. A new record is therefore always stored.
            Math.addExact(changeIndex.size, 1)
        }

        // A class present in the current map keeps its baseline representative, a still-active addition
        // episode keeps its first one, and only a genuinely new class takes the supplied key. The
        // branches are explicit rather than elvis chains because a stored key may itself be null.
        val representative = when {
            currentEntry != null -> currentEntry.key
            changeEntry != null -> changeEntry.key
            else -> key
        }
        // The first effective write captures `before`; later writes preserve it. The explicit branch
        // matters: an active addition episode has a null - absent - `before` that must not be refilled.
        val before = if (changeEntry != null) changeEntry.value.before else currentEntry?.let { DeltaMapValue(it.value) }
        val after = DeltaMapValue(value)
        val nextCurrent = currentSnapshot.setItem(representative, value)
        val nextChanges = if (equivalentEndpoints(before, after)) {
            changeIndex.remove(representative)
        } else {
            changeIndex.setItem(representative, DeltaMapRecord(before, after))
        }

        return successor(nextCurrent, nextChanges)
    }

    /**
     * Applies [entries] in iteration order, exactly as repeated [setItem] would. O(m log N) for m
     * supplied pairs.
     *
     * This is a convenience and an allocation saving over repeated single assignment, not a different
     * algorithm: it *is* the left fold of [setItem], so every coalescing, cancellation,
     * representative-retention, and checkpoint-snap rule holds verbatim and no stronger bound is
     * claimed. A later pair for the same class overwrites an earlier one. An empty sequence, or one
     * whose every pair is a semantic no-op, returns the receiver itself.
     *
     * @throws ArithmeticException if the current map or the change index reaches [Int] capacity.
     */
    public fun setItems(entries: Iterable<Pair<K, V>>): PersistentDeltaMap<K, V> {
        var result = this
        for ((key, value) in entries) {
            result = result.setItem(key, value)
        }

        return result
    }

    /**
     * Removes one current entry and coalesces its checkpoint-relative change. O(log N).
     *
     * Removing an absent key is a no-op that returns the receiver itself. Removing a class that was
     * added since the checkpoint cancels its record rather than recording a removal, and once the last
     * record goes the current root snaps back to the exact checkpoint root.
     *
     * @throws ArithmeticException if the change index is already at [Int] capacity.
     */
    public fun remove(key: K): PersistentDeltaMap<K, V> {
        val currentEntry = entryOf(currentSnapshot, key) ?: return this
        val changeEntry = entryOf(changeIndex, key)
        if (changeEntry == null) {
            // A class with no record was present at the checkpoint, so removing it always stores one.
            Math.addExact(changeIndex.size, 1)
        }

        val representative = if (changeEntry != null) changeEntry.key else currentEntry.key
        val before = if (changeEntry != null) changeEntry.value.before else DeltaMapValue(currentEntry.value)
        val nextCurrent = currentSnapshot.remove(currentEntry.key)
        val nextChanges = if (equivalentEndpoints(before, null)) {
            changeIndex.remove(representative)
        } else {
            changeIndex.setItem(representative, DeltaMapRecord(before, null))
        }

        return successor(nextCurrent, nextChanges)
    }

    /**
     * Makes the current snapshot the new checkpoint and resets the change index. O(1).
     *
     * An already-clean version returns the receiver itself. No key or value policy callback is invoked.
     */
    public fun checkpoint(): PersistentDeltaMap<K, V> =
        if (isClean) {
            this
        } else {
            PersistentDeltaMap(currentSnapshot, currentSnapshot, SortedMap.empty(comparator), comparator, valueEquality)
        }

    /**
     * Returns to this version's checkpoint and resets the change index. O(1).
     *
     * An already-clean version returns the receiver itself. No key or value policy callback is invoked.
     */
    public fun rollback(): PersistentDeltaMap<K, V> =
        if (isClean) {
            this
        } else {
            PersistentDeltaMap(
                checkpointSnapshot,
                checkpointSnapshot,
                SortedMap.empty(comparator),
                comparator,
                valueEquality,
            )
        }

    /** One exact checkpoint-relative net change for [key]'s class, or `null` when it has none. O(log(k + 1)). */
    public fun getChange(key: K): PersistentMapChange<K, V>? {
        val entry = entryOf(changeIndex, key) ?: return null
        return PersistentMapChange(entry.key, entry.value.before, entry.value.after)
    }

    /**
     * The exact net changes, once each, in ascending key order. Theta(k + 1), which is output-optimal
     * in the number of net-changed classes and independent of the checkpoint's size.
     *
     * No key or value policy callback is invoked.
     *
     * Divergence from the C# baseline: that one returns a lazily evaluated enumerable, so abandoning it
     * after one element costs Theta(log k). This port materializes a list, matching the Rust port. Full
     * consumption is Theta(k + 1) either way; a partial enumeration pays for the whole change set here.
     */
    public fun getChanges(): List<PersistentMapChange<K, V>> = project(changeIndex)

    /**
     * The exact net changes whose keys lie in the closed range `[low, high]`, once each, in the same
     * ascending key order [getChanges] uses. O(log(k + 1)) to seek the boundaries, plus Theta(output).
     *
     * The change index is itself an ordered map under this collection's comparator, so this is a
     * boundary seek and a bounded walk over the in-range sub-index - two partition-point descents and
     * two splits - never a filter over all `k` records. The cost does not depend on how many changes
     * fall outside the range. Only the boundaries are compared, so the comparator is invoked
     * O(log(k + 1)) times and [valueEquality] not at all.
     *
     * An inverted range - [low] comparing greater than [high] under the retained comparator - yields no
     * changes rather than failing, matching [getKeyRange]. "Inverted" is decided by that comparator, so
     * under a descending order the low endpoint is the numerically greater key.
     */
    public fun getChanges(low: K, high: K): List<PersistentMapChange<K, V>> =
        project(changeIndex.getKeyRange(low, high))

    /**
     * Whether this version and [other] are backed by the same three roots, so neither can observe a
     * change made through the other. O(1), a reference test rather than an equality test.
     */
    public fun sharesRootsWith(other: PersistentDeltaMap<K, V>): Boolean =
        currentSnapshot === other.currentSnapshot &&
            checkpointSnapshot === other.checkpointSnapshot &&
            changeIndex === other.changeIndex

    /**
     * Whether this version and [other] retain nodes in common in all three roots, meaning one was
     * derived from the other.
     *
     * A diagnostic for confirming structural sharing, not a substitute for equality and not a cheap
     * test: this substrate answers it by collecting node identities, so it costs O(N + k) rather than
     * the O(1) root comparison [sharesRootsWith] performs.
     */
    public fun sharesStorageWith(other: PersistentDeltaMap<K, V>): Boolean =
        currentSnapshot.sharesStorageWith(other.currentSnapshot) &&
            checkpointSnapshot.sharesStorageWith(other.checkpointSnapshot) &&
            changeIndex.sharesStorageWith(other.changeIndex)

    /**
     * Recomputes the whole representation and returns its statistics.
     *
     * This is diagnostic validation, not part of any operation's cost: it is O((N + k) log N) and does
     * invoke both policies.
     *
     * @throws IllegalStateException if any invariant is violated.
     */
    public fun validateStructure(): PersistentDeltaMapStatistics {
        checkAscending(currentSnapshot.keys(), "current state")
        checkAscending(checkpointSnapshot.keys(), "checkpoint state")
        checkAscending(changeIndex.keys(), "change index")
        check(!changeIndex.isEmpty || currentSnapshot === checkpointSnapshot) {
            "PersistentDeltaMap invariant violated: a version without changes does not reuse its checkpoint root."
        }
        check(changeIndex.isEmpty || currentSnapshot !== checkpointSnapshot) {
            "PersistentDeltaMap invariant violated: a version with changes reuses its checkpoint root."
        }

        var added = 0
        var removed = 0
        var updated = 0
        for (entry in changeIndex) {
            val record = entry.value
            check(!equivalentEndpoints(record.before, record.after)) {
                "PersistentDeltaMap invariant violated: a recorded change has equivalent endpoints."
            }
            check(endpointMatches(record.before, entryOf(checkpointSnapshot, entry.key))) {
                "PersistentDeltaMap invariant violated: a change's before endpoint differs from the checkpoint state."
            }
            check(endpointMatches(record.after, entryOf(currentSnapshot, entry.key))) {
                "PersistentDeltaMap invariant violated: a change's after endpoint differs from the current state."
            }

            when {
                record.before == null && record.after != null -> added++
                record.before != null && record.after == null -> removed++
                record.before != null && record.after != null -> updated++
                else -> throw IllegalStateException(
                    "PersistentDeltaMap invariant violated: a recorded change is absent at both endpoints.",
                )
            }
        }

        var expected = 0
        for (entry in checkpointSnapshot) {
            val currentEntry = entryOf(currentSnapshot, entry.key)
            if (currentEntry == null || !valueEquality.areEqual(entry.value, currentEntry.value)) {
                expected++
                check(entryOf(changeIndex, entry.key) != null) {
                    "PersistentDeltaMap invariant violated: a checkpoint difference has no recorded change."
                }
            }
        }
        for (entry in currentSnapshot) {
            if (entryOf(checkpointSnapshot, entry.key) == null) {
                expected++
                check(entryOf(changeIndex, entry.key) != null) {
                    "PersistentDeltaMap invariant violated: a current addition has no recorded change."
                }
            }
        }
        check(expected == changeIndex.size) {
            "PersistentDeltaMap invariant violated: the recorded change cardinality is not exact."
        }

        return PersistentDeltaMapStatistics(
            size,
            checkpointSize,
            changeCount,
            added,
            removed,
            updated,
            isClean,
        )
    }

    /** The current entries in ascending key order. */
    override fun iterator(): Iterator<SortedMapEntry<K, V>> = currentSnapshot.iterator()

    /** A diagnostic representation naming the current size and the net-change count. */
    override fun toString(): String = "PersistentDeltaMap(size=$size, changeCount=$changeCount)"

    private fun successor(
        current: SortedMap<K, V>,
        changes: SortedMap<K, DeltaMapRecord<V>>,
    ): PersistentDeltaMap<K, V> {
        // A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
        // canonical and later checkpoint, rollback, and change queries stay O(1).
        val canonical = if (changes.isEmpty) checkpointSnapshot else current
        return PersistentDeltaMap(canonical, checkpointSnapshot, changes, comparator, valueEquality)
    }

    private fun project(records: SortedMap<K, DeltaMapRecord<V>>): List<PersistentMapChange<K, V>> =
        records.toList().map { PersistentMapChange(it.key, it.value.before, it.value.after) }

    private fun <T> entryOf(map: SortedMap<K, T>, key: K): SortedMapEntry<K, T>? {
        val entry = map.ceilingEntry(key) ?: return null
        return if (comparator.compare(entry.key, key) == 0) entry else null
    }

    private fun equivalentEndpoints(left: DeltaMapValue<V>?, right: DeltaMapValue<V>?): Boolean = when {
        left == null -> right == null
        right == null -> false
        else -> valueEquality.areEqual(left.value, right.value)
    }

    private fun endpointMatches(endpoint: DeltaMapValue<V>?, stored: SortedMapEntry<K, V>?): Boolean = when {
        endpoint == null -> stored == null
        stored == null -> false
        else -> valueEquality.areEqual(endpoint.value, stored.value)
    }

    private fun checkAscending(keys: List<K>, what: String) {
        for (rank in 1 until keys.size) {
            check(comparator.compare(keys[rank - 1], keys[rank]) < 0) {
                "PersistentDeltaMap invariant violated: the $what is not strictly ascending by key."
            }
        }
    }
}
