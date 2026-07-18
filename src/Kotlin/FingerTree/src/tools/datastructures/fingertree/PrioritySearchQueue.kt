package tools.datastructures.fingertree

import java.util.IdentityHashMap
import kotlin.math.abs
import kotlin.math.max

private fun <T> psqValuesEqual(left: T, right: T): Boolean = left === right || left == right

/** One unique ordered key, its priority, and its payload. Lower priorities win. */
public data class PrioritySearchEntry<K, P, V>(
    val key: K,
    val priority: P,
    val value: V,
)

/** Statistics returned by the independent winner-cached AVL audit. */
public data class PrioritySearchQueueStatistics(
    val count: Int,
    val height: Int,
    val maximumAbsoluteBalanceFactor: Int,
)

/** Result of a non-overwriting keyed insertion. */
public data class PrioritySearchAddResult<K, P, V>(
    val added: Boolean,
    val queue: PrioritySearchQueue<K, P, V>,
)

/** Result of a keyed removal. [entry] is non-null exactly when [removed] is true. */
public data class PrioritySearchRemoveResult<K, P, V>(
    val removed: Boolean,
    val entry: PrioritySearchEntry<K, P, V>?,
    val queue: PrioritySearchQueue<K, P, V>,
)

/** One successful minimum deletion. */
public data class PrioritySearchMinimumView<K, P, V>(
    val entry: PrioritySearchEntry<K, P, V>,
    val remainder: PrioritySearchQueue<K, P, V>,
)

/**
 * A persistent ordered map whose AVL nodes cache their minimum-priority entry.
 *
 * One entry is retained per [keyComparator] equivalence class. Equal priorities break by retained
 * key order, making [minimum] deterministic. Keyed updates and minimum deletion are O(log n),
 * minimum lookup is O(1), and [enumerateAtMost] prunes a subtree when its cached winner already
 * exceeds the threshold.
 */
public class PrioritySearchQueue<K, P, V> private constructor(
    private val root: Node<K, P, V>?,
    public val keyComparator: Comparator<in K>,
    public val priorityComparator: Comparator<in P>,
) : Iterable<PrioritySearchEntry<K, P, V>> {
    public companion object {
        /** Creates an empty queue with shared natural-order comparators. */
        public fun <K : Comparable<K>, P : Comparable<P>, V> empty(): PrioritySearchQueue<K, P, V> =
            PrioritySearchQueue(root = null, naturalOrder(), naturalOrder())

        /** Creates an empty queue retaining the supplied comparison policies. */
        public fun <K, P, V> empty(
            keyComparator: Comparator<in K>,
            priorityComparator: Comparator<in P>,
        ): PrioritySearchQueue<K, P, V> =
            PrioritySearchQueue(root = null, keyComparator, priorityComparator)

        /** O(n log n) last-wins construction using natural ordering. */
        public fun <K : Comparable<K>, P : Comparable<P>, V> from(
            entries: Iterable<PrioritySearchEntry<K, P, V>>,
        ): PrioritySearchQueue<K, P, V> = from(entries, naturalOrder(), naturalOrder())

        /** O(n log n) last-wins construction retaining explicit comparators. */
        public fun <K, P, V> from(
            entries: Iterable<PrioritySearchEntry<K, P, V>>,
            keyComparator: Comparator<in K>,
            priorityComparator: Comparator<in P>,
        ): PrioritySearchQueue<K, P, V> {
            var result = empty<K, P, V>(keyComparator, priorityComparator)
            for (entry in entries) {
                result = result.setItem(entry.key, entry.priority, entry.value)
            }
            return result
        }
    }

    public val count: Int
        get() = root?.count ?: 0

    public val size: Int
        get() = count

    public val isEmpty: Boolean
        get() = root == null

    /** Cached AVL height for diagnostics and logarithmic-bound audits. */
    public val height: Int
        get() = root?.height ?: 0

    /** The global minimum-priority entry in O(1). */
    public val minimum: PrioritySearchEntry<K, P, V>
        get() = root?.winner ?: throw NoSuchElementException("The priority search queue is empty.")

    public fun minimumOrNull(): PrioritySearchEntry<K, P, V>? = root?.winner

    public fun containsKey(key: K): Boolean = findNode(key) != null

    /** Returns the stored entry and therefore the first retained key representative. */
    public fun getEntryOrNull(key: K): PrioritySearchEntry<K, P, V>? = findNode(key)?.entry

    /**
     * Adds or replaces an entry in O(log n). An equivalent key retains its first concrete key.
     * Replacement is a no-op only when priority ordering, priority equality, and payload equality
     * all agree with the stored representation.
     */
    public fun setItem(key: K, priority: P, value: V): PrioritySearchQueue<K, P, V> {
        val result = set(root, PrioritySearchEntry(key, priority, value), overwrite = true)
        return if (result.node === root) this else PrioritySearchQueue(result.node, keyComparator, priorityComparator)
    }

    /** Attempts to add a new key without replacing an equivalent existing key. */
    public fun tryAdd(key: K, priority: P, value: V): PrioritySearchAddResult<K, P, V> {
        val result = set(root, PrioritySearchEntry(key, priority, value), overwrite = false)
        val queue = if (result.added) {
            PrioritySearchQueue(result.node, keyComparator, priorityComparator)
        } else {
            this
        }
        return PrioritySearchAddResult(result.added, queue)
    }

    /** Removes an equivalent key in O(log n), preserving identity when absent. */
    public fun remove(key: K): PrioritySearchQueue<K, P, V> {
        val result = removeNode(root, key)
        return if (result.entry == null) this else PrioritySearchQueue(result.node, keyComparator, priorityComparator)
    }

    /** Attempts keyed removal while returning the stored representative. */
    public fun tryRemove(key: K): PrioritySearchRemoveResult<K, P, V> {
        val result = removeNode(root, key)
        val removed = result.entry != null
        return PrioritySearchRemoveResult(
            removed,
            result.entry,
            if (removed) PrioritySearchQueue(result.node, keyComparator, priorityComparator) else this,
        )
    }

    /** Removes and returns the global winner in O(log n). */
    public fun deleteMinimum(): PrioritySearchMinimumView<K, P, V> {
        val entry = root?.winner ?: throw NoSuchElementException("The priority search queue is empty.")
        val result = removeNode(root, entry.key)
        check(result.entry != null) { "The cached priority winner was not present by key." }
        return PrioritySearchMinimumView(entry, PrioritySearchQueue(result.node, keyComparator, priorityComparator))
    }

    /** Returns null only when the queue is empty. */
    public fun minimumView(): PrioritySearchMinimumView<K, P, V>? =
        if (root == null) null else deleteMinimum()

    /**
     * Lazily enumerates entries in the inclusive key range whose priorities are at most the
     * inclusive threshold. Results are in key order. The cost is O(log n + v), where v is the
     * number of nodes whose subtrees winner pruning cannot exclude; v may be n.
     */
    public fun enumerateAtMost(
        minimumKey: K,
        maximumKey: K,
        maximumPriority: P,
    ): Sequence<PrioritySearchEntry<K, P, V>> {
        require(keyComparator.compare(minimumKey, maximumKey) <= 0) {
            "The minimum key must not follow the maximum key."
        }
        val snapshot = root
        return sequence {
            val actualRoot = snapshot ?: return@sequence
            val pending = ArrayList<QueryFrame<K, P, V>>()
            pending.add(QueryFrame(actualRoot, emit = false))
            while (pending.isNotEmpty()) {
                val frame = pending.removeAt(pending.lastIndex)
                val node = frame.node
                if (frame.emit) {
                    yield(node.entry)
                    continue
                }
                if (priorityComparator.compare(node.winner.priority, maximumPriority) > 0) {
                    continue
                }

                val lower = keyComparator.compare(node.entry.key, minimumKey)
                val upper = keyComparator.compare(node.entry.key, maximumKey)
                if (upper < 0) {
                    node.right?.let { pending.add(QueryFrame(it, emit = false)) }
                }
                if (lower >= 0 && upper <= 0 &&
                    priorityComparator.compare(node.entry.priority, maximumPriority) <= 0
                ) {
                    pending.add(QueryFrame(node, emit = true))
                }
                if (lower > 0) {
                    node.left?.let { pending.add(QueryFrame(it, emit = false)) }
                }
            }
        }
    }

    /** In-order iteration is explicit-stack and performs no priority comparisons. */
    override fun iterator(): Iterator<PrioritySearchEntry<K, P, V>> = object : Iterator<PrioritySearchEntry<K, P, V>> {
        private val pending = ArrayList<Node<K, P, V>>()
        private var cursor = root

        init {
            pushLeftSpine()
        }

        override fun hasNext(): Boolean = pending.isNotEmpty()

        override fun next(): PrioritySearchEntry<K, P, V> {
            if (pending.isEmpty()) {
                throw NoSuchElementException()
            }
            val node = pending.removeAt(pending.lastIndex)
            cursor = node.right
            pushLeftSpine()
            return node.entry
        }

        private fun pushLeftSpine() {
            while (cursor != null) {
                val node = cursor ?: break
                pending.add(node)
                cursor = node.left
            }
        }
    }

    /**
     * Validates strict BST bounds, AVL balance, cached count/height, and every cached winner with an
     * explicit worklist. The recursive update depth is bounded by the validated AVL height.
     */
    public fun validateStructure(): PrioritySearchQueueStatistics {
        val actualRoot = root ?: return PrioritySearchQueueStatistics(0, 0, 0)
        val pending = ArrayList<ValidationFrame<K, P, V>>()
        pending.add(ValidationFrame(actualRoot, lower = null, upper = null))
        var validatedCount = 0
        var maximumBalance = 0
        while (pending.isNotEmpty()) {
            val frame = pending.removeAt(pending.lastIndex)
            val node = frame.node
            frame.lower?.let {
                check(keyComparator.compare(node.entry.key, it.value) > 0) {
                    "A priority-search node crosses its lower key bound."
                }
            }
            frame.upper?.let {
                check(keyComparator.compare(node.entry.key, it.value) < 0) {
                    "A priority-search node crosses its upper key bound."
                }
            }

            val expectedCount = Math.addExact(1, Math.addExact(node.left?.count ?: 0, node.right?.count ?: 0))
            val expectedHeight = Math.addExact(1, max(heightOf(node.left), heightOf(node.right)))
            val balance = heightOf(node.left) - heightOf(node.right)
            check(node.count == expectedCount && node.height == expectedHeight) {
                "A priority-search node has invalid cached metadata."
            }
            check(abs(balance) <= 1) { "A priority-search node violates AVL balance." }

            val expectedWinner = selectWinner(node.entry, node.left, node.right)
            check(sameStoredEntry(expectedWinner, node.winner)) {
                "A priority-search node has an invalid cached winner."
            }

            validatedCount = Math.addExact(validatedCount, 1)
            maximumBalance = max(maximumBalance, abs(balance))
            node.right?.let {
                pending.add(ValidationFrame(it, KeyBound(node.entry.key), frame.upper))
            }
            node.left?.let {
                pending.add(ValidationFrame(it, frame.lower, KeyBound(node.entry.key)))
            }
        }
        check(validatedCount == count) {
            "Priority-search root metadata disagrees with the validated tree."
        }
        return PrioritySearchQueueStatistics(validatedCount, height, maximumBalance)
    }

    /** Counts node objects retained by identity across versions. */
    internal fun sharedNodeCount(other: PrioritySearchQueue<K, P, V>): Int {
        val identities = IdentityHashMap<Node<K, P, V>, Boolean>()
        collectNodes(root, identities)
        val visited = IdentityHashMap<Node<K, P, V>, Boolean>()
        val pending = ArrayList<Node<K, P, V>>().apply { other.root?.let(::add) }
        var shared = 0
        while (pending.isNotEmpty()) {
            val node = pending.removeAt(pending.lastIndex)
            if (visited.put(node, true) != null) {
                continue
            }
            if (identities.containsKey(node)) {
                shared++
            }
            node.left?.let(pending::add)
            node.right?.let(pending::add)
        }
        return shared
    }

    internal fun rootIdentityForTesting(): Any? = root

    internal fun nodeIdentityForTesting(key: K): Any? = findNode(key)

    internal fun cursorBoundRank(key: K, upper: Boolean): Int {
        var rank = 0
        var cursor = root
        while (cursor != null) {
            val comparison = keyComparator.compare(cursor.entry.key, key)
            if (comparison < 0 || (upper && comparison == 0)) {
                rank = Math.addExact(rank, Math.addExact(cursor.left?.count ?: 0, 1))
                cursor = cursor.right
            } else {
                cursor = cursor.left
            }
        }
        return rank
    }

    internal fun cursorEntryAt(rank: Int): PrioritySearchEntry<K, P, V>? {
        if (rank < 0 || rank >= count) return null
        var remaining = rank
        var cursor = root
        while (cursor != null) {
            val leftCount = cursor.left?.count ?: 0
            when {
                remaining < leftCount -> cursor = cursor.left
                remaining == leftCount -> return cursor.entry
                else -> {
                    remaining -= leftCount + 1
                    cursor = cursor.right
                }
            }
        }
        return null
    }

    private fun collectNodes(
        initialRoot: Node<K, P, V>?,
        destination: IdentityHashMap<Node<K, P, V>, Boolean>,
    ) {
        val pending = ArrayList<Node<K, P, V>>().apply { initialRoot?.let(::add) }
        while (pending.isNotEmpty()) {
            val node = pending.removeAt(pending.lastIndex)
            if (destination.put(node, true) != null) {
                continue
            }
            node.left?.let(pending::add)
            node.right?.let(pending::add)
        }
    }

    private fun findNode(key: K): Node<K, P, V>? {
        var node = root
        while (node != null) {
            val comparison = keyComparator.compare(key, node.entry.key)
            if (comparison == 0) {
                return node
            }
            node = if (comparison < 0) node.left else node.right
        }
        return null
    }

    private fun set(
        node: Node<K, P, V>?,
        entry: PrioritySearchEntry<K, P, V>,
        overwrite: Boolean,
    ): SetResult<K, P, V> {
        if (node == null) {
            return SetResult(newNode(entry, left = null, right = null), added = true, changed = true)
        }
        val comparison = keyComparator.compare(entry.key, node.entry.key)
        return when {
            comparison == 0 -> {
                if (!overwrite ||
                    (priorityComparator.compare(entry.priority, node.entry.priority) == 0 &&
                        psqValuesEqual(entry.priority, node.entry.priority) && psqValuesEqual(entry.value, node.entry.value))
                ) {
                    SetResult(node, added = false, changed = false)
                } else {
                    val retained = PrioritySearchEntry(node.entry.key, entry.priority, entry.value)
                    SetResult(newNode(retained, node.left, node.right), added = false, changed = true)
                }
            }
            comparison < 0 -> {
                val result = set(node.left, entry, overwrite)
                if (!result.changed) {
                    SetResult(node, result.added, changed = false)
                } else {
                    SetResult(
                        balance(newNode(node.entry, result.node, node.right)),
                        result.added,
                        changed = true,
                    )
                }
            }
            else -> {
                val result = set(node.right, entry, overwrite)
                if (!result.changed) {
                    SetResult(node, result.added, changed = false)
                } else {
                    SetResult(
                        balance(newNode(node.entry, node.left, result.node)),
                        result.added,
                        changed = true,
                    )
                }
            }
        }
    }

    private fun removeNode(node: Node<K, P, V>?, key: K): NodeRemoval<K, P, V> {
        node ?: return NodeRemoval(node = null, entry = null)
        val comparison = keyComparator.compare(key, node.entry.key)
        return when {
            comparison == 0 -> when {
                node.left == null -> NodeRemoval(node.right, node.entry)
                node.right == null -> NodeRemoval(node.left, node.entry)
                else -> {
                    val successor = minimumKeyNode(node.right)
                    val right = removeNode(node.right, successor.entry.key)
                    NodeRemoval(balance(newNode(successor.entry, node.left, right.node)), node.entry)
                }
            }
            comparison < 0 -> {
                val result = removeNode(node.left, key)
                if (result.entry == null) {
                    NodeRemoval(node, entry = null)
                } else {
                    NodeRemoval(balance(newNode(node.entry, result.node, node.right)), result.entry)
                }
            }
            else -> {
                val result = removeNode(node.right, key)
                if (result.entry == null) {
                    NodeRemoval(node, entry = null)
                } else {
                    NodeRemoval(balance(newNode(node.entry, node.left, result.node)), result.entry)
                }
            }
        }
    }

    private fun balance(node: Node<K, P, V>): Node<K, P, V> {
        val factor = heightOf(node.left) - heightOf(node.right)
        if (factor > 1) {
            val left = checkNotNull(node.left)
            return if (heightOf(left.left) < heightOf(left.right)) {
                rotateRight(newNode(node.entry, rotateLeft(left), node.right))
            } else {
                rotateRight(node)
            }
        }
        if (factor < -1) {
            val right = checkNotNull(node.right)
            return if (heightOf(right.right) < heightOf(right.left)) {
                rotateLeft(newNode(node.entry, node.left, rotateRight(right)))
            } else {
                rotateLeft(node)
            }
        }
        return node
    }

    private fun rotateLeft(node: Node<K, P, V>): Node<K, P, V> {
        val pivot = checkNotNull(node.right)
        return newNode(pivot.entry, newNode(node.entry, node.left, pivot.left), pivot.right)
    }

    private fun rotateRight(node: Node<K, P, V>): Node<K, P, V> {
        val pivot = checkNotNull(node.left)
        return newNode(pivot.entry, pivot.left, newNode(node.entry, pivot.right, node.right))
    }

    private fun newNode(
        entry: PrioritySearchEntry<K, P, V>,
        left: Node<K, P, V>?,
        right: Node<K, P, V>?,
    ): Node<K, P, V> = Node(entry, left, right, selectWinner(entry, left, right))

    private fun selectWinner(
        entry: PrioritySearchEntry<K, P, V>,
        left: Node<K, P, V>?,
        right: Node<K, P, V>?,
    ): PrioritySearchEntry<K, P, V> {
        var winner = entry
        if (left != null && isBefore(left.winner, winner)) {
            winner = left.winner
        }
        if (right != null && isBefore(right.winner, winner)) {
            winner = right.winner
        }
        return winner
    }

    private fun sameStoredEntry(
        left: PrioritySearchEntry<K, P, V>,
        right: PrioritySearchEntry<K, P, V>,
    ): Boolean =
        keyComparator.compare(left.key, right.key) == 0 && psqValuesEqual(left.key, right.key) &&
            priorityComparator.compare(left.priority, right.priority) == 0 && psqValuesEqual(left.priority, right.priority) &&
            psqValuesEqual(left.value, right.value)

    private fun isBefore(
        left: PrioritySearchEntry<K, P, V>,
        right: PrioritySearchEntry<K, P, V>,
    ): Boolean {
        val priority = priorityComparator.compare(left.priority, right.priority)
        return priority < 0 || (priority == 0 && keyComparator.compare(left.key, right.key) < 0)
    }

    private fun minimumKeyNode(initialNode: Node<K, P, V>): Node<K, P, V> {
        var node = initialNode
        while (true) {
            val left = node.left ?: return node
            node = left
        }
    }

    private fun heightOf(node: Node<K, P, V>?): Int = node?.height ?: 0

    private class Node<K, P, V>(
        val entry: PrioritySearchEntry<K, P, V>,
        val left: Node<K, P, V>?,
        val right: Node<K, P, V>?,
        val winner: PrioritySearchEntry<K, P, V>,
    ) {
        val height: Int = Math.addExact(1, max(left?.height ?: 0, right?.height ?: 0))
        val count: Int = Math.addExact(1, Math.addExact(left?.count ?: 0, right?.count ?: 0))
    }

    private data class SetResult<K, P, V>(
        val node: Node<K, P, V>,
        val added: Boolean,
        val changed: Boolean,
    )

    private data class NodeRemoval<K, P, V>(
        val node: Node<K, P, V>?,
        val entry: PrioritySearchEntry<K, P, V>?,
    )

    private data class QueryFrame<K, P, V>(val node: Node<K, P, V>, val emit: Boolean)

    private data class KeyBound<K>(val value: K)

    private data class ValidationFrame<K, P, V>(
        val node: Node<K, P, V>,
        val lower: KeyBound<K>?,
        val upper: KeyBound<K>?,
    )
}
