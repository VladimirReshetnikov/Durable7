/*
 * Persistent insertion-ordered set with explicit repositioning and stable sorting.
 *
 * Pairs a hash index from element to order stamp with an ordered sequence of stamped entries, so
 * membership stays constant time while iteration follows insertion order. Stamps are spaced apart,
 * which lets a move usually pick a stamp between its neighbors instead of restamping everything.
 */
package durable7.ordered

import java.util.Comparator
import durable7.fingertree.FingerTree
import durable7.fingertree.MeasurePolicy
import durable7.hamt.HashPolicy
import durable7.hamt.PersistentHashMap
import durable7.hamt.defaultHashPolicy

private const val StampStride: Long = 1L shl 20

private data class OrderedEntry<T>(val stamp: Long, val item: T)

private object OrderedStampMeasurePolicy : MeasurePolicy<OrderedEntry<Any?>, Long?> {
    override val empty: Long? = null

    override fun measure(element: OrderedEntry<Any?>): Long = element.stamp

    override fun combine(left: Long?, right: Long?): Long? = right ?: left
}

@Suppress("UNCHECKED_CAST")
private fun <T> orderedStampMeasurePolicy(): MeasurePolicy<OrderedEntry<T>, Long?> =
    OrderedStampMeasurePolicy as MeasurePolicy<OrderedEntry<T>, Long?>

/** Presence-discriminated stored-representative lookup result. */
public data class OrderedSetLookup<T>(public val found: Boolean, public val value: T)

/** Result of a removal attempt; misses retain the exact receiver. */
public data class OrderedSetRemoveResult<T>(
    public val removed: Boolean,
    public val set: PersistentOrderedSet<T>,
)

/** Public summary returned after independently recomputing both ordered-set indexes. */
public data class PersistentOrderedSetStatistics(public val count: Int)

/** Raised when an explicit movement operation names an absent equality class. */
public class OrderedSetMissingValueException(
    message: String = "The value is absent from the ordered set.",
) : NoSuchElementException(message)

/**
 * An immutable insertion-ordered set with explicit positional movement.
 *
 * [policy] defines membership, hashing, and representative equivalence. Enumeration follows
 * insertion or explicitly requested positional order. The first representative installed for an
 * equality class is retained until that class is removed. This neutral collection is implemented
 * only over the public HAMT and FingerTree APIs.
 */
public class PersistentOrderedSet<T> private constructor(
    private val order: FingerTree<OrderedEntry<T>, Long?>,
    private val stamps: PersistentHashMap<T, Long>,
) : Iterable<T> {
    public companion object {
        private val sharedDefaultEmpty: PersistentOrderedSet<Any?> = PersistentOrderedSet<Any?>(
            FingerTree.empty(orderedStampMeasurePolicy<Any?>()),
            PersistentHashMap.empty<Any?, Long>(),
        )

        /** Returns an empty set retaining the exact supplied hash/equality policy object. */
        public fun <T> empty(
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): PersistentOrderedSet<T> {
            if (policy === defaultHashPolicy<T>()) {
                @Suppress("UNCHECKED_CAST")
                return sharedDefaultEmpty as PersistentOrderedSet<T>
            }
            return PersistentOrderedSet(
                FingerTree.empty(orderedStampMeasurePolicy()),
                PersistentHashMap.empty(policy),
            )
        }

        /** Alias for [empty] matching the managed reference surface. */
        public fun <T> create(
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): PersistentOrderedSet<T> = empty(policy)

        /**
         * Enumerates [items] once, retaining each class's first representative and position.
         */
        public fun <T> from(
            items: Iterable<T>,
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): PersistentOrderedSet<T> {
            var seen = PersistentHashMap.empty<T, Unit>(policy)
            val distinct = ArrayList<T>()
            for (item in items) {
                val added = seen.tryAdd(item, Unit)
                if (!added.added) continue
                seen = added.value
                if (distinct.size == Int.MAX_VALUE) throw ArithmeticException(SizeOverflowMessage)
                distinct.add(item)
            }
            return buildFromItems(distinct, policy)
        }

        /** Alias for [from] matching the managed reference surface. */
        public fun <T> createRange(
            items: Iterable<T>,
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): PersistentOrderedSet<T> = from(items, policy)

        private const val SizeOverflowMessage: String =
            "The operation would create more than Int32.MaxValue ordered-set elements."

        private fun <T> tryPickStamp(
            order: FingerTree<OrderedEntry<T>, Long?>,
            index: Int,
        ): Long? {
            if (order.isEmpty) return 0L
            if (index == 0) {
                val first = requireNotNull(order.front()).stamp
                return if (first < Long.MIN_VALUE + StampStride) null else first - StampStride
            }
            if (index == order.size) {
                val last = requireNotNull(order.back()).stamp
                return if (last > Long.MAX_VALUE - StampStride) null else last + StampStride
            }

            val left = requireNotNull(order[index - 1]).stamp
            val right = requireNotNull(order[index]).stamp
            val gap = right.toULong() - left.toULong()
            if (gap < 2uL) return null
            return (left.toULong() + (gap shr 1)).toLong()
        }

        private fun <T> buildFromItems(
            items: List<T>,
            policy: HashPolicy<T>,
        ): PersistentOrderedSet<T> {
            if (items.isEmpty()) return empty(policy)
            val entries = ArrayList<OrderedEntry<T>>(items.size)
            val pairs = ArrayList<Pair<T, Long>>(items.size)
            for ((index, item) in items.withIndex()) {
                val stamp = Math.multiplyExact(index.toLong(), StampStride)
                entries.add(OrderedEntry(stamp, item))
                pairs.add(item to stamp)
            }
            return PersistentOrderedSet(
                FingerTree.from(entries, orderedStampMeasurePolicy()),
                PersistentHashMap.from(pairs, policy),
            )
        }

        private fun <T> buildIndex(
            order: FingerTree<OrderedEntry<T>, Long?>,
            policy: HashPolicy<T>,
        ): PersistentHashMap<T, Long> {
            val pairs = ArrayList<Pair<T, Long>>(order.size)
            for (entry in order) pairs.add(entry.item to entry.stamp)
            return PersistentHashMap.from(pairs, policy)
        }

        private fun <T> wrap(
            order: FingerTree<OrderedEntry<T>, Long?>,
            stamps: PersistentHashMap<T, Long>,
        ): PersistentOrderedSet<T> =
            if (order.isEmpty) empty(stamps.policy) else PersistentOrderedSet(order, stamps)

        @Suppress("UNCHECKED_CAST")
        private fun <T> compareByDefault(left: T, right: T): Int {
            if ((left as Any?) === (right as Any?)) return 0
            if (left == null) return -1
            if (right == null) return 1
            val comparable = left as? Comparable<Any?>
                ?: throw IllegalArgumentException(
                    "Elements do not implement Comparable; supply an explicit Comparator.",
                )
            return comparable.compareTo(right)
        }
    }

    /** Number of equality classes in this snapshot. */
    public val size: Int get() = order.size

    /** Alias for [size] matching the managed reference surface. */
    public val count: Int get() = size

    /** Whether this snapshot contains no equality classes. */
    public val isEmpty: Boolean get() = order.isEmpty

    /** The exact effective hash/equality policy object retained by this snapshot. */
    public val policy: HashPolicy<T> get() = stamps.policy

    /** First representative in positional order. */
    public val first: T
        get() {
            if (isEmpty) throw NoSuchElementException("The ordered set is empty.")
            return requireNotNull(order.front()).item
        }

    /** Last representative in positional order. */
    public val last: T
        get() {
            if (isEmpty) throw NoSuchElementException("The ordered set is empty.")
            return requireNotNull(order.back()).item
        }

    /** Tests membership under [policy]. */
    public fun contains(item: T): Boolean = stamps.containsKey(item)

    /** Returns the stored representative on a hit and echoes [equalValue] on a miss. */
    public fun tryGetValue(equalValue: T): OrderedSetLookup<T> {
        val entry = stamps.getEntry(equalValue)
        return if (entry == null) {
            OrderedSetLookup(false, equalValue)
        } else {
            OrderedSetLookup(true, entry.key)
        }
    }

    /** Returns the representative at [index]. */
    public fun getAt(index: Int): T {
        requireElementIndex(index)
        return requireNotNull(order[index]).item
    }

    /** Kotlin indexed-access spelling for [getAt]. */
    public operator fun get(index: Int): T = getAt(index)

    /** Returns the final position of an equivalent class, or `-1` when absent. */
    public fun indexOf(equalValue: T): Int {
        val entry = stamps.getEntry(equalValue) ?: return -1
        return indexOfStamp(entry.value)
    }

    /** Appends an absent class; a duplicate is an exact identity no-op and never moves. */
    public fun add(item: T): PersistentOrderedSet<T> = insertCore(size, item)

    /** Prepends an absent class; a duplicate is an exact identity no-op and never moves. */
    public fun addFirst(item: T): PersistentOrderedSet<T> = insertCore(0, item)

    /** Inserts an absent class before [index], validating the index before hashing. */
    public fun insert(index: Int, item: T): PersistentOrderedSet<T> {
        requireInsertionIndex(index)
        return insertCore(index, item)
    }

    /** Moves an existing class to the first position while retaining its stored representative. */
    public fun moveToFirst(equalValue: T): PersistentOrderedSet<T> = moveExisting(0, equalValue)

    /** Moves an existing class to the last position while retaining its stored representative. */
    public fun moveToLast(equalValue: T): PersistentOrderedSet<T> = moveExisting(size - 1, equalValue)

    /** Moves an existing class to its final-result [index]. */
    public fun moveTo(index: Int, equalValue: T): PersistentOrderedSet<T> {
        requireElementIndex(index)
        return moveExisting(index, equalValue)
    }

    /** Removes an equality class, returning this snapshot on a miss. */
    public fun remove(equalValue: T): PersistentOrderedSet<T> = tryRemove(equalValue).set

    /** Attempts removal; a miss returns `removed == false` and this exact snapshot. */
    public fun tryRemove(equalValue: T): OrderedSetRemoveResult<T> {
        val removed = stamps.tryRemoveEntry(equalValue)
            ?: return OrderedSetRemoveResult(false, this)
        val index = indexOfStamp(removed.entry.value)
        val nextOrder = order.removeAt(index) ?: throw indexDisagreement()
        return OrderedSetRemoveResult(true, wrap(nextOrder, removed.map))
    }

    /** Removes the class at [index]. */
    public fun removeAt(index: Int): PersistentOrderedSet<T> {
        requireElementIndex(index)
        val entry = requireNotNull(order[index])
        val removed = stamps.tryRemoveEntry(entry.item) ?: throw indexDisagreement()
        if (removed.entry.value != entry.stamp) throw indexDisagreement()
        return wrap(requireNotNull(order.removeAt(index)), removed.map)
    }

    /** Removes the first class. */
    public fun removeFirst(): PersistentOrderedSet<T> {
        if (isEmpty) throw NoSuchElementException("The ordered set is empty.")
        return removeAt(0)
    }

    /** Removes the last class. */
    public fun removeLast(): PersistentOrderedSet<T> {
        if (isEmpty) throw NoSuchElementException("The ordered set is empty.")
        return removeAt(size - 1)
    }

    /** Returns a policy-preserving empty snapshot, or this snapshot when already empty. */
    public fun clear(): PersistentOrderedSet<T> = if (isEmpty) this else empty(policy)

    /** Extracts a contiguous range while retaining [policy]. */
    public fun getRange(index: Int, count: Int): PersistentOrderedSet<T> {
        requireInsertionIndex(index)
        if (count < 0 || count > size - index) {
            throw IndexOutOfBoundsException("The range extends past the end of the set.")
        }
        if (count == size) return this
        if (count == 0) return empty(policy)

        val firstSplit = requireNotNull(order.splitAtIndex(index))
        val secondSplit = requireNotNull(firstSplit.right.splitAtIndex(count))
        val kept = secondSplit.left
        val removedCount = size - count
        val nextStamps = if (count <= removedCount) {
            buildIndex(kept, policy)
        } else {
            var candidate = stamps
            for (entry in firstSplit.left) candidate = candidate.remove(entry.item)
            for (entry in secondSplit.right) candidate = candidate.remove(entry.item)
            candidate
        }
        return PersistentOrderedSet(kept, nextStamps)
    }

    /** Keeps the first [count] classes. */
    public fun take(count: Int): PersistentOrderedSet<T> {
        requireCount(count)
        return getRange(0, count)
    }

    /** Discards the first [count] classes. */
    public fun drop(count: Int): PersistentOrderedSet<T> {
        requireCount(count)
        return getRange(count, size - count)
    }

    /** Reverses positional order, returning this snapshot for counts zero and one. */
    public fun reverse(): PersistentOrderedSet<T> =
        if (size <= 1) this else buildFromItems(toList().asReversed(), policy)

    /**
     * Performs a stable one-shot reorder. Later additions append normally; no ordering policy is
     * retained. A null [comparator] uses the elements' natural order with nulls first.
     */
    public fun sort(comparator: Comparator<in T>? = null): PersistentOrderedSet<T> {
        if (size <= 1) return this
        val effective: Comparator<T> = if (comparator == null) {
            Comparator { left, right -> compareByDefault(left, right) }
        } else {
            Comparator { left, right -> comparator.compare(left, right) }
        }
        val original = order.toList()
        val sorted = original.sortedWith { left, right ->
            val byItem = effective.compare(left.item, right.item)
            when {
                byItem < 0 -> -1
                byItem > 0 -> 1
                left.stamp < right.stamp -> -1
                left.stamp > right.stamp -> 1
                else -> 0
            }
        }
        if (sorted.indices.all { sorted[it].stamp == original[it].stamp }) return this
        return buildFromItems(sorted.map { it.item }, policy)
    }

    /** Receiver-order union followed by first argument-only representatives in normalized order. */
    public fun union(other: Iterable<T>): PersistentOrderedSet<T> {
        val argument = normalize(other)
        var result: ArrayList<T>? = null
        for (item in argument.items) {
            if (stamps.containsKey(item)) continue
            if (result == null) result = ArrayList<T>(toList())
            if (result.size == Int.MAX_VALUE) throw ArithmeticException(SizeOverflowMessage)
            result.add(item)
        }
        return if (result == null) this else buildFromItems(result, policy)
    }

    /** Retains receiver representatives and receiver order for classes present in [other]. */
    public fun intersect(other: Iterable<T>): PersistentOrderedSet<T> {
        val argument = normalize(other)
        val result = ArrayList<T>()
        for (entry in order) if (argument.membership.containsKey(entry.item)) result.add(entry.item)
        return if (result.size == size) this else buildFromItems(result, policy)
    }

    /** Removes normalized argument classes while retaining receiver representatives and order. */
    public fun except(other: Iterable<T>): PersistentOrderedSet<T> {
        val argument = normalize(other)
        val result = ArrayList<T>()
        for (entry in order) if (!argument.membership.containsKey(entry.item)) result.add(entry.item)
        return if (result.size == size) this else buildFromItems(result, policy)
    }

    /** Receiver-only order followed by first normalized argument-only representatives. */
    public fun symmetricExcept(other: Iterable<T>): PersistentOrderedSet<T> {
        val argument = normalize(other)
        if (argument.items.isEmpty()) return this
        val result = ArrayList<T>()
        for (entry in order) if (!argument.membership.containsKey(entry.item)) result.add(entry.item)
        for (item in argument.items) {
            if (!stamps.containsKey(item)) {
                if (result.size == Int.MAX_VALUE) throw ArithmeticException(SizeOverflowMessage)
                result.add(item)
            }
        }
        return buildFromItems(result, policy)
    }

    /** Whether every receiver class occurs in receiver-policy-normalized [other]. */
    public fun isSubsetOf(other: Iterable<T>): Boolean {
        val argument = normalize(other)
        return size <= argument.items.size && everyReceiverOccursIn(argument.membership)
    }

    /** Whether this is a strict receiver-policy subset of [other]. */
    public fun isProperSubsetOf(other: Iterable<T>): Boolean {
        val argument = normalize(other)
        return size < argument.items.size && everyReceiverOccursIn(argument.membership)
    }

    /** Whether every receiver-policy-normalized argument class occurs here. */
    public fun isSupersetOf(other: Iterable<T>): Boolean {
        val argument = normalize(other)
        return size >= argument.items.size && everyArgumentOccursHere(argument.items)
    }

    /** Whether this is a strict receiver-policy superset of [other]. */
    public fun isProperSupersetOf(other: Iterable<T>): Boolean {
        val argument = normalize(other)
        return size > argument.items.size && everyArgumentOccursHere(argument.items)
    }

    /** Whether this set shares a receiver-policy class with [other]. */
    public fun overlaps(other: Iterable<T>): Boolean {
        val argument = normalize(other)
        return argument.items.any { stamps.containsKey(it) }
    }

    /** Receiver-policy set equality; positional order is intentionally ignored. */
    public fun setEquals(other: Iterable<T>): Boolean {
        val argument = normalize(other)
        return size == argument.items.size && everyArgumentOccursHere(argument.items)
    }

    /** Returns a fresh list of representatives in positional order. */
    public fun toList(): List<T> = order.mapTo(ArrayList<T>(size)) { it.item }

    /** Recomputes and validates both directions of the independently owned dual-index invariant. */
    public fun validateStructure(): PersistentOrderedSetStatistics {
        if (order.size != stamps.size) throw indexDisagreement("The indexes have different counts.")
        var previous: Long? = null
        for (entry in order) {
            if (previous != null && entry.stamp <= previous) {
                throw indexDisagreement("Order-maintenance stamps are not strictly ascending.")
            }
            previous = entry.stamp
            val indexed = stamps.getEntry(entry.item)
                ?: throw indexDisagreement("An ordered representative has no membership stamp.")
            if (indexed.value != entry.stamp) {
                throw indexDisagreement("An ordered representative has a different membership stamp.")
            }
            if ((indexed.key as Any?) !== (entry.item as Any?)) {
                throw indexDisagreement("The indexes retain different representatives.")
            }
        }

        for (indexed in stamps) {
            val position = indexOfStamp(indexed.value)
            val ordered = requireNotNull(order[position])
            if (!policy.equivalent(indexed.key, ordered.item) ||
                (indexed.key as Any?) !== (ordered.item as Any?)
            ) {
                throw indexDisagreement("A membership entry has no ordered representative.")
            }
        }
        return PersistentOrderedSetStatistics(size)
    }

    override fun iterator(): Iterator<T> {
        val entries = order.iterator()
        return object : Iterator<T> {
            override fun hasNext(): Boolean = entries.hasNext()

            override fun next(): T = entries.next().item
        }
    }

    private data class NormalizedArgument<T>(
        val items: List<T>,
        val membership: PersistentHashMap<T, Unit>,
    )

    private fun normalize(other: Iterable<T>): NormalizedArgument<T> {
        var membership = PersistentHashMap.empty<T, Unit>(policy)
        val items = ArrayList<T>()
        for (item in other) {
            val added = membership.tryAdd(item, Unit)
            if (!added.added) continue
            membership = added.value
            if (items.size == Int.MAX_VALUE) throw ArithmeticException(SizeOverflowMessage)
            items.add(item)
        }
        return NormalizedArgument(items, membership)
    }

    private fun everyReceiverOccursIn(membership: PersistentHashMap<T, Unit>): Boolean {
        for (entry in order) if (!membership.containsKey(entry.item)) return false
        return true
    }

    private fun everyArgumentOccursHere(items: List<T>): Boolean {
        for (item in items) if (!stamps.containsKey(item)) return false
        return true
    }

    private fun insertCore(index: Int, item: T): PersistentOrderedSet<T> {
        val stamp = tryPickStamp(order, index)
        if (stamp != null) {
            val added = stamps.tryAdd(item, stamp)
            if (!added.added) return this
            if (size == Int.MAX_VALUE) throw ArithmeticException(SizeOverflowMessage)
            val entry = OrderedEntry(stamp, item)
            val nextOrder = when (index) {
                0 -> order.prepend(entry)
                size -> order.append(entry)
                else -> requireNotNull(order.insertAt(index, entry))
            }
            return PersistentOrderedSet(nextOrder, added.value)
        }

        // The provisional stamp is used only for duplicate detection; a successful result is
        // discarded because the relabel rebuild assigns every stamp canonically.
        val provisional = stamps.tryAdd(item, 0L)
        if (!provisional.added) return this
        if (size == Int.MAX_VALUE) throw ArithmeticException(SizeOverflowMessage)
        val items = ArrayList<T>(toList())
        items.add(index, item)
        return buildFromItems(items, policy)
    }

    private fun moveExisting(finalIndex: Int, equalValue: T): PersistentOrderedSet<T> {
        val indexed = stamps.getEntry(equalValue) ?: throw OrderedSetMissingValueException()
        val oldIndex = indexOfStamp(indexed.value)
        if (oldIndex == finalIndex) return this

        val stored = requireNotNull(order[oldIndex]).item
        val trimmed = requireNotNull(order.removeAt(oldIndex))
        val stamp = tryPickStamp(trimmed, finalIndex)
        if (stamp == null) {
            val items = trimmed.mapTo(ArrayList<T>(trimmed.size + 1)) { it.item }
            items.add(finalIndex, stored)
            return buildFromItems(items, policy)
        }

        val entry = OrderedEntry(stamp, stored)
        val nextOrder = when (finalIndex) {
            0 -> trimmed.prepend(entry)
            trimmed.size -> trimmed.append(entry)
            else -> requireNotNull(trimmed.insertAt(finalIndex, entry))
        }
        return PersistentOrderedSet(nextOrder, stamps.put(stored, stamp))
    }

    private fun indexOfStamp(stamp: Long): Int {
        val located = order.tryLocate { last -> last != null && last >= stamp }
        val entry = located.item
        if (!located.found || entry == null || entry.stamp != stamp) throw indexDisagreement()
        return located.index
    }

    private fun requireInsertionIndex(index: Int) {
        if (index < 0 || index > size) {
            throw IndexOutOfBoundsException("Index must lie within 0 through the set count.")
        }
    }

    private fun requireElementIndex(index: Int) {
        if (index < 0 || index >= size) {
            throw IndexOutOfBoundsException("Index must identify an element in the set.")
        }
    }

    private fun requireCount(count: Int) {
        if (count < 0 || count > size) {
            throw IndexOutOfBoundsException("Count must lie within 0 through the set count.")
        }
    }

    private fun indexDisagreement(
        message: String = "The ordered set's membership and order indexes disagree.",
    ): IllegalStateException = IllegalStateException(message)
}
