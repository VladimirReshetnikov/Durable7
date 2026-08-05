/*
 * Fixed-length persistent vector carrying a checkpoint and an exact maximal-run index of the
 * positions whose current values differ from that checkpoint.
 *
 * Two immutable RRB roots hold the current and checkpoint views of the same fixed-length sequence,
 * and one persistent ordered map holds a `start -> endExclusive` record for every maximal run of
 * differing positions. Dirtiness is relative to a retained equality policy rather than to write
 * history: writing a policy-equal value is a semantic no-op, and returning a position to its
 * checkpoint equality class cancels that position's delta and restores the exact checkpoint
 * representative.
 *
 * Let n be the fixed length, k the number of dirty positions, and r the number of maximal dirty
 * runs, so 0 <= r <= k <= n. Against a bare pair of unindexed roots, the one strict result is that
 * exact dirty-run descriptor discovery is output-optimal Theta(r) rather than worst-case Theta(n);
 * the gap is unbounded, because one contiguous changed block gives k = n and r = 1. Emitting the
 * changed payload values is still Omega(k) and is not claimed to be faster. The run index is an
 * application of the Discrete Interval Encoding Tree idea, not a new interval algorithm, and the
 * splice bound is inherited from the RRB substrate.
 */
package durable7.fingertree

/**
 * A nullable-safe carrier for one stored value.
 *
 * A non-null wrapper reports that the position exists even when [value] itself is `null`, which a
 * bare `T?` result cannot distinguish from an out-of-range position.
 */
public data class RunDeltaValue<T>(
    /** The stored value, which may itself be `null`. */
    public val value: T,
)

/**
 * One maximal half-open run of checkpoint-relative changes covering `start until start + length`.
 *
 * The runs published by one version are ordered, non-overlapping, non-adjacent, and maximal.
 */
public data class PersistentDirtyRun(
    /** The zero-based first changed position. */
    public val start: Int,
    /** The number of consecutive changed positions. */
    public val length: Int,
) {
    /**
     * The exclusive end position.
     *
     * @throws ArithmeticException if the sum overflows [Int].
     */
    public val endExclusive: Int
        get() = Math.addExact(start, length)

    /** Whether [index] lies inside this run. */
    public fun contains(index: Int): Boolean = index >= start && index < endExclusive

    /** The half-open interval in mathematical notation, for example `[3, 5)`. */
    override fun toString(): String = "[$start, $endExclusive)"
}

/** Run and cardinality measurements returned by a successful structural audit. */
public data class PersistentRunDeltaVectorStatistics(
    /** The fixed number of positions. */
    public val count: Int,
    /** The number of positions differing from the checkpoint, recounted from the run index. */
    public val dirtyCount: Int,
    /** The number of maximal dirty runs. */
    public val dirtyRunCount: Int,
)

/*
 * One element slot, identified by reference rather than by value.
 *
 * The cleanliness invariant is "a clean position reuses its exact checkpoint representative", which
 * needs reference identity: the retained equality policy may be coarser or finer than natural
 * equality, so it cannot decide representative identity. This class deliberately does not override
 * `equals`, so it inherits identity equality from `Any`. That has a second, load-bearing effect:
 * `RrbVector.setItem` short-circuits on `left === right || left == right`, and over cells both
 * disjuncts are the identity test, so the substrate can never silently retain an equal-but-distinct
 * representative in place of the one this vector asked it to store.
 */
private class RunDeltaCell<T>(val value: T)

/**
 * A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.
 *
 * Current and checkpoint values are immutable RRB roots, and a persistent ordered map stores one
 * `start -> endExclusive` entry per maximal dirty run. One effective point edit copies one RRB path
 * and changes at most two neighboring run records. Forking by retaining a version, accepting every
 * change, and reverting every change are O(1) root swaps.
 *
 * Shipped bounds, over `n` positions and `r` maximal runs:
 *
 * | Operation | Bound |
 * | --- | --- |
 * | Build `n` values | Theta(n) |
 * | Read a current or checkpoint value | O(log n) |
 * | Persistent point edit | O(log n) amortized time and fresh nodes |
 * | Fork by retaining a version | O(1) |
 * | Whole checkpoint or rollback | O(1) |
 * | Enumerate current values | Theta(n) |
 * | Dirty membership | O(log(r + 2)) amortized |
 * | Exact dirty-run descriptors | Theta(r) |
 * | Select a dirty run by rank | O(log(r + 2)) amortized |
 * | Accept or revert one indexed run | O(log n) amortized, independent of that run's length |
 * | Total live structure | O(n) |
 *
 * Accepting or reverting one run splits and concatenates the RRB roots structurally instead of
 * walking that run's positions, which is what makes it independent of the run's length and free of
 * any value-policy call. Starting from one `n`-element version, `m` retained effective point edits
 * use O(n + m log n) worst-case total structural space; that history bound does not collapse to
 * O(n).
 *
 * Length is fixed at construction: there is deliberately no insertion, deletion, concatenation,
 * split, range assignment, rebase, or merge of unrelated branches, because checkpoint correspondence
 * would need a position-transformation policy this abstract data type does not define. There is one
 * checkpoint per version.
 *
 * Every version is immutable and every producing operation leaves its receiver usable. All policy
 * calls in an edit happen before any successor is constructed, so a throwing comparer publishes no
 * partial version. Concurrent reads and independent branch derivation are safe when the retained
 * [ValueEqualityPolicy] is safe for concurrent calls and stays one stable equivalence relation.
 */
public class PersistentRunDeltaVector<T> private constructor(
    private val currentRoot: RrbVector<RunDeltaCell<T>>,
    private val checkpointRoot: RrbVector<RunDeltaCell<T>>,
    private val runIndex: SortedMap<Int, Int>,
    /** The number of positions that differ from the checkpoint. O(1). */
    public val dirtyCount: Int,
    /** The retained policy defining checkpoint-relative value equality. */
    public val valuePolicy: ValueEqualityPolicy<T>,
) : Iterable<T> {
    /** Factories for clean vectors. */
    public companion object {
        private val emptyNatural: PersistentRunDeltaVector<Any?> = PersistentRunDeltaVector(
            RrbVector.empty<RunDeltaCell<Any?>>(),
            RrbVector.empty<RunDeltaCell<Any?>>(),
            SortedMap.empty<Int, Int>(),
            dirtyCount = 0,
            valuePolicy = ValueEqualityPolicy.natural(),
        )

        /** The shared clean empty vector under the canonical natural-equality policy. */
        @Suppress("UNCHECKED_CAST")
        public fun <T> empty(): PersistentRunDeltaVector<T> = emptyNatural as PersistentRunDeltaVector<T>

        /** The clean empty vector retaining [policy]. Returns the shared instance for the natural policy. */
        public fun <T> empty(policy: ValueEqualityPolicy<T>): PersistentRunDeltaVector<T> =
            if (policy === ValueEqualityPolicy.natural<T>()) {
                empty()
            } else {
                PersistentRunDeltaVector(
                    RrbVector.empty<RunDeltaCell<T>>(),
                    RrbVector.empty<RunDeltaCell<T>>(),
                    SortedMap.empty<Int, Int>(),
                    dirtyCount = 0,
                    valuePolicy = policy,
                )
            }

        /** A clean fixed-length vector holding [values] in order, under the natural-equality policy. Theta(n). */
        public fun <T> from(values: Iterable<T>): PersistentRunDeltaVector<T> =
            from(values, ValueEqualityPolicy.natural())

        /**
         * A clean fixed-length vector holding [values] in order, retaining [policy]. Theta(n).
         *
         * Both roots start as the same root, so the vector begins with no dirty runs.
         *
         * @throws ArithmeticException if [values] yields more than [Int.MAX_VALUE] elements.
         */
        public fun <T> from(
            values: Iterable<T>,
            policy: ValueEqualityPolicy<T>,
        ): PersistentRunDeltaVector<T> {
            val builder = RrbVector.builder<RunDeltaCell<T>>()
            for (value in values) {
                builder.append(RunDeltaCell(value))
            }

            val root = builder.toImmutable()
            return if (root.isEmpty) {
                empty(policy)
            } else {
                PersistentRunDeltaVector(root, root, SortedMap.empty(), dirtyCount = 0, valuePolicy = policy)
            }
        }
    }

    /** The fixed number of positions. O(1). */
    public val size: Int
        get() = currentRoot.size

    /** Whether the vector has no positions. */
    public val isEmpty: Boolean
        get() = currentRoot.isEmpty

    /** The number of maximal dirty runs. O(1). */
    public val dirtyRunCount: Int
        get() = runIndex.size

    /** Whether at least one current value differs from its checkpoint value. O(1). */
    public val hasChanges: Boolean
        get() = dirtyCount != 0

    /**
     * The current value at [index], or `null` when [index] is outside the vector. O(log n).
     *
     * A stored `null` is indistinguishable from an out-of-range position here; use [tryGet] when
     * that matters.
     */
    public operator fun get(index: Int): T? = currentRoot[index]?.value

    /** The current value at [index] in a nullable-safe wrapper, or `null` when [index] is out of range. */
    public fun tryGet(index: Int): RunDeltaValue<T>? {
        val cell = currentRoot[index] ?: return null
        return RunDeltaValue(cell.value)
    }

    /**
     * The checkpoint value at [index], or `null` when [index] is outside the vector. O(log n).
     *
     * A stored `null` is indistinguishable from an out-of-range position here; use [tryGetCheckpoint]
     * when that matters.
     */
    public fun getCheckpoint(index: Int): T? = checkpointRoot[index]?.value

    /** The checkpoint value at [index] in a nullable-safe wrapper, or `null` when [index] is out of range. */
    public fun tryGetCheckpoint(index: Int): RunDeltaValue<T>? {
        val cell = checkpointRoot[index] ?: return null
        return RunDeltaValue(cell.value)
    }

    /**
     * Whether [index] differs from its checkpoint value, or `null` when [index] is outside the
     * vector. O(log(r + 2)) amortized.
     */
    public fun isDirty(index: Int): Boolean? =
        if (index < 0 || index >= size) null else findDirtyRun(index) != null

    /**
     * The maximal dirty run containing [index], or `null` when that position is clean or out of
     * range. O(log(r + 2)) amortized.
     *
     * Use [isDirty] to tell a clean position from an out-of-range one.
     */
    public fun dirtyRunContaining(index: Int): PersistentDirtyRun? =
        if (index < 0 || index >= size) null else findDirtyRun(index)

    /**
     * The maximal dirty run at zero-based ascending-start [rank], or `null` when [rank] falls outside
     * the run index. O(log(r + 2)) amortized.
     */
    public fun dirtyRunAt(rank: Int): PersistentDirtyRun? = runIndex.entryAt(rank)?.let(::toRun)

    /**
     * The maximal dirty runs in ascending position order.
     *
     * Output-optimal Theta(r): the cost depends on the number of runs, not on how many positions
     * they cover.
     */
    public fun dirtyRuns(): List<PersistentDirtyRun> = runIndex.map(::toRun)

    /**
     * A version with the current value at [index] replaced by [value], or `null` when [index] is
     * outside the vector.
     *
     * A policy-equal replacement is a semantic no-op and returns the receiver, retaining the existing
     * current representative and changing no run accounting. A replacement landing back in the
     * checkpoint's equality class cancels that position's delta and restores the exact checkpoint
     * representative; when the last dirty position clears, the current root canonicalizes onto the
     * checkpoint root. Otherwise the position becomes or stays dirty and at most two neighboring run
     * records change.
     *
     * O(log n) amortized time and fresh nodes. Every policy call happens before any successor is
     * built, so a throwing comparer publishes no partial version.
     *
     * @throws ArithmeticException if the dirty cardinality would exceed [Int.MAX_VALUE].
     */
    public fun setItem(index: Int, value: T): PersistentRunDeltaVector<T>? {
        val current = currentRoot[index] ?: return null
        if (valuePolicy.areEqual(current.value, value)) {
            return this
        }

        val checkpointCell = checkpointRoot[index]!!
        val remainsDirty = !valuePolicy.areEqual(checkpointCell.value, value)
        val replacement = if (remainsDirty) RunDeltaCell(value) else checkpointCell
        return replaceCell(index, replacement, remainsDirty)
    }

    /**
     * A version whose current value at [index] is reset to its checkpoint value, or `null` when
     * [index] is outside the vector.
     *
     * An already clean position returns the receiver. Otherwise the exact checkpoint representative
     * is restored, not merely a policy-equal one. O(log n) amortized.
     */
    public fun resetItem(index: Int): PersistentRunDeltaVector<T>? {
        val checkpointCell = checkpointRoot[index] ?: return null
        val current = currentRoot[index]!!
        if (valuePolicy.areEqual(current.value, checkpointCell.value)) {
            return this
        }

        return replaceCell(index, checkpointCell, remainsDirty = false)
    }

    /**
     * Accepts every current value as a new checkpoint. O(1).
     *
     * An already clean version returns the receiver.
     */
    public fun checkpoint(): PersistentRunDeltaVector<T> =
        if (!hasChanges) {
            this
        } else {
            PersistentRunDeltaVector(
                currentRoot,
                currentRoot,
                SortedMap.empty(),
                dirtyCount = 0,
                valuePolicy = valuePolicy,
            )
        }

    /**
     * Reverts every current value to the checkpoint. O(1).
     *
     * An already clean version returns the receiver.
     */
    public fun rollback(): PersistentRunDeltaVector<T> =
        if (!hasChanges) {
            this
        } else {
            PersistentRunDeltaVector(
                checkpointRoot,
                checkpointRoot,
                SortedMap.empty(),
                dirtyCount = 0,
                valuePolicy = valuePolicy,
            )
        }

    /**
     * Accepts the maximal dirty run at [rank] into the checkpoint, or `null` when [rank] falls
     * outside the run index.
     *
     * Only the selected run becomes clean; every other run is untouched. The splice is structural
     * RRB splitting and concatenation, so it costs O(log n) amortized independently of that run's
     * length and makes no value-policy call at all.
     */
    public fun acceptDirtyRunAt(rank: Int): PersistentRunDeltaVector<T>? {
        val run = dirtyRunAt(rank) ?: return null
        return acceptRun(run)
    }

    /**
     * Reverts the maximal dirty run at [rank] to the checkpoint, or `null` when [rank] falls outside
     * the run index.
     *
     * Only the selected run becomes clean; every other run is untouched. The splice is structural
     * RRB splitting and concatenation, so it costs O(log n) amortized independently of that run's
     * length and makes no value-policy call at all.
     */
    public fun revertDirtyRunAt(rank: Int): PersistentRunDeltaVector<T>? {
        val run = dirtyRunAt(rank) ?: return null
        return revertRun(run)
    }

    /**
     * Accepts the maximal dirty run containing the position [index] into the checkpoint, or `null`
     * when [index] falls outside the vector.
     *
     * The argument is a zero-based position, not a run rank, so a descriptor obtained from
     * [dirtyRunContaining] can be acted on without rediscovering its rank. A clean position is
     * vacuous rather than an error: the receiver comes back, sharing every root and consulting no
     * comparer.
     *
     * O(log n) amortized and independent of that run's length: locating the containing run is
     * O(log(r + 2)) through the start-keyed run index, never a scan.
     */
    public fun acceptDirtyRunContaining(index: Int): PersistentRunDeltaVector<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        val run = findDirtyRun(index) ?: return this
        return acceptRun(run)
    }

    /**
     * Reverts the maximal dirty run containing the position [index] to the checkpoint, or `null` when
     * [index] falls outside the vector.
     *
     * The argument is a zero-based position, not a run rank, so a descriptor obtained from
     * [dirtyRunContaining] can be acted on without rediscovering its rank. A clean position is
     * vacuous rather than an error: the receiver comes back, sharing every root and consulting no
     * comparer.
     *
     * O(log n) amortized and independent of that run's length: locating the containing run is
     * O(log(r + 2)) through the start-keyed run index, never a scan.
     */
    public fun revertDirtyRunContaining(index: Int): PersistentRunDeltaVector<T>? {
        if (index < 0 || index >= size) {
            return null
        }

        val run = findDirtyRun(index) ?: return this
        return revertRun(run)
    }

    /**
     * Whether this version and [other] are backed by the same current root. A representation test,
     * not an equality test.
     */
    public fun sharesCurrentRootWith(other: PersistentRunDeltaVector<T>): Boolean =
        currentRoot.rootIdentity === other.currentRoot.rootIdentity

    /**
     * Whether this version and [other] are backed by the same checkpoint root. A representation test,
     * not an equality test.
     */
    public fun sharesCheckpointRootWith(other: PersistentRunDeltaVector<T>): Boolean =
        checkpointRoot.rootIdentity === other.checkpointRoot.rootIdentity

    /**
     * Whether position [index] holds the very same representative object in both roots, or `null`
     * when [index] is outside the vector.
     *
     * This is the diagnostic behind the cleanliness invariant: a clean position must reuse its exact
     * checkpoint representative, which no value comparison can establish. It is a representation
     * test, not an equality test.
     */
    public fun sharesCheckpointRepresentativeAt(index: Int): Boolean? {
        val current = currentRoot[index] ?: return null
        return current === checkpointRoot[index]
    }

    /** The current values in positional order. Theta(n). */
    public fun toList(): List<T> = currentRoot.map { it.value }

    /** Iterates the current values in positional order. Theta(n). */
    override fun iterator(): Iterator<T> = RunDeltaValueIterator(currentRoot.iterator())

    /** A short description that enumerates no values and calls no comparer. */
    override fun toString(): String =
        "PersistentRunDeltaVector(size=$size, dirtyCount=$dirtyCount, dirtyRunCount=$dirtyRunCount)"

    /**
     * Independently re-audits both RRB roots, run ordering, non-adjacency, maximality, the dirty
     * cardinality sum, policy-relative truth at every dirty position, and exact representative
     * sharing at every clean position.
     *
     * O(n log n) in the number of positions: an auditor, not a hot-path operation.
     *
     * @throws IllegalStateException if any invariant is broken.
     */
    public fun validateStructure(): PersistentRunDeltaVectorStatistics {
        check(currentRoot.size == checkpointRoot.size) {
            "The current and checkpoint roots have different lengths."
        }
        check(dirtyCount >= 0 && dirtyCount <= size) {
            "The dirty cardinality is outside the position count."
        }
        check(currentRoot.validateStructure() != null) { "The current RRB root is structurally invalid." }
        check(checkpointRoot.validateStructure() != null) { "The checkpoint RRB root is structurally invalid." }
        if (runIndex.isEmpty) {
            check(dirtyCount == 0 && currentRoot.rootIdentity === checkpointRoot.rootIdentity) {
                "A clean version is not canonicalized to its checkpoint root."
            }
        }

        var previousEnd = -1
        var countedDirty = 0
        for (entry in runIndex) {
            val start = entry.key
            val end = entry.value
            check(start >= 0 && start < end && end <= size && start > previousEnd) {
                "Dirty runs are empty, out of range, overlapping, adjacent, or unordered."
            }
            countedDirty = Math.addExact(countedDirty, end - start)
            previousEnd = end
        }
        check(countedDirty == dirtyCount) { "Dirty run lengths do not sum to the dirty cardinality." }

        for (index in 0 until size) {
            val dirty = findDirtyRun(index) != null
            val current = currentRoot[index]!!
            val checkpointCell = checkpointRoot[index]!!
            check(dirty || current === checkpointCell) {
                "A clean position does not reuse its exact checkpoint representative."
            }
            check(!dirty || !valuePolicy.areEqual(current.value, checkpointCell.value)) {
                "A dirty position is policy-equal to its checkpoint value."
            }
        }

        return PersistentRunDeltaVectorStatistics(size, dirtyCount, runIndex.size)
    }

    private fun findDirtyRun(index: Int): PersistentDirtyRun? {
        val entry = runIndex.floorEntry(index) ?: return null
        return if (entry.value > index) toRun(entry) else null
    }

    private fun replaceCell(
        index: Int,
        replacement: RunDeltaCell<T>,
        remainsDirty: Boolean,
    ): PersistentRunDeltaVector<T> {
        val wasDirty = findDirtyRun(index) != null
        var nextCurrent = currentRoot.setItem(index, replacement)!!
        var nextRuns = runIndex
        var nextDirtyCount = dirtyCount

        if (!wasDirty && remainsDirty) {
            nextRuns = addDirtyPosition(nextRuns, index)
            nextDirtyCount = Math.addExact(nextDirtyCount, 1)
        } else if (wasDirty && !remainsDirty) {
            nextRuns = removeDirtyPosition(nextRuns, index)
            nextDirtyCount -= 1
        }

        if (nextRuns.isEmpty) {
            nextCurrent = checkpointRoot
        }
        return PersistentRunDeltaVector(nextCurrent, checkpointRoot, nextRuns, nextDirtyCount, valuePolicy)
    }

    private fun acceptRun(run: PersistentDirtyRun): PersistentRunDeltaVector<T> {
        if (runIndex.size == 1) {
            return checkpoint()
        }

        return PersistentRunDeltaVector(
            currentRoot,
            replaceRange(checkpointRoot, currentRoot, run),
            runIndex.remove(run.start),
            dirtyCount - run.length,
            valuePolicy,
        )
    }

    private fun revertRun(run: PersistentDirtyRun): PersistentRunDeltaVector<T> {
        if (runIndex.size == 1) {
            return rollback()
        }

        return PersistentRunDeltaVector(
            replaceRange(currentRoot, checkpointRoot, run),
            checkpointRoot,
            runIndex.remove(run.start),
            dirtyCount - run.length,
            valuePolicy,
        )
    }
}

private class RunDeltaValueIterator<T>(
    private val cells: Iterator<RunDeltaCell<T>>,
) : Iterator<T> {
    override fun hasNext(): Boolean = cells.hasNext()

    override fun next(): T = cells.next().value
}

private fun toRun(entry: SortedMapEntry<Int, Int>): PersistentDirtyRun =
    PersistentDirtyRun(entry.key, entry.value - entry.key)

/*
 * Records `index` as dirty, merging the runs on both sides, extending one side, or inserting a fresh
 * singleton run. The result stays ordered, non-overlapping, non-adjacent, and maximal, and at most
 * two records change.
 */
private fun addDirtyPosition(runs: SortedMap<Int, Int>, index: Int): SortedMap<Int, Int> {
    val next = Math.addExact(index, 1)
    val left = runs.floorEntry(index)?.takeIf { it.value == index }
    val right = runs.ceilingEntry(index)?.takeIf { it.key == next }
    return when {
        // The left record is overwritten in place rather than removed and reinserted: setItem
        // replaces a present key, so dropping the redundant remove saves one persistent rebuild
        // without changing the resulting map.
        left != null && right != null -> runs.remove(right.key).setItem(left.key, right.value)
        left != null -> runs.setItem(left.key, next)
        right != null -> runs.remove(right.key).setItem(index, right.value)
        else -> runs.setItem(index, next)
    }
}

/*
 * Clears `index`, deleting, shrinking at either edge, or splitting its unique containing run.
 */
private fun removeDirtyPosition(runs: SortedMap<Int, Int>, index: Int): SortedMap<Int, Int> {
    val containing = checkNotNull(runs.floorEntry(index)) {
        "The cleared dirty position is absent from the run index."
    }
    val start = containing.key
    val end = containing.value
    check(end > index) { "The cleared dirty position is absent from the run index." }

    val next = Math.addExact(index, 1)
    return when {
        start == index && end == next -> runs.remove(start)
        start == index -> runs.remove(start).setItem(next, end)
        end == next -> runs.setItem(start, index)
        else -> runs.setItem(start, index).setItem(next, end)
    }
}

/*
 * Splices `source`'s run slice over `destination`'s, sharing every untouched subtree. Four splits
 * and two boundary-spine concatenations, so O(log n) regardless of the run's length, and no value
 * comparison anywhere.
 */
private fun <T> replaceRange(
    destination: RrbVector<RunDeltaCell<T>>,
    source: RrbVector<RunDeltaCell<T>>,
    run: PersistentDirtyRun,
): RrbVector<RunDeltaCell<T>> {
    if (run.start == 0 && run.length == destination.size) {
        return source
    }

    val destinationSplit = destination.splitAt(run.start)!!
    val right = destinationSplit.second.splitAt(run.length)!!.second
    val sourceTail = source.splitAt(run.start)!!.second
    val middle = sourceTail.splitAt(run.length)!!.first
    return destinationSplit.first.concat(middle).concat(right)
}
