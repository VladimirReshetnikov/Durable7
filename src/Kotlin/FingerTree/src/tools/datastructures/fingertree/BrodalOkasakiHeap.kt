package tools.datastructures.fingertree

import java.util.IdentityHashMap
import kotlin.math.max

/** Statistics returned by the full bootstrapped skew-binomial invariant audit. */
public data class BrodalOkasakiHeapStatistics(
    val count: Int,
    val rootForestLength: Int,
    val maximumRank: Int,
    val maximumDepth: Int,
)

/** One successful minimum deletion, keeping nullable heap elements unambiguous. */
public data class BrodalMinimumView<T>(
    val minimum: T,
    val remainder: BrodalOkasakiHeap<T>,
)

/**
 * An immutable Brodal-Okasaki bootstrapped skew-binomial min-heap.
 *
 * [insert], [meld], and [minimum] use worst-case O(1) comparisons and structural work;
 * [deleteMinimum] is worst-case O(log n). Every update retains prior versions. Custom-comparator
 * heaps may meld only when they retain the exact same [Comparator] object.
 */
public class BrodalOkasakiHeap<T> private constructor(
    private val root: Tree<T>?,
    public val count: Int,
    public val comparator: Comparator<in T>,
) : Iterable<T> {
    public companion object {
        /** Creates an empty natural-order heap. Kotlin's natural comparator is a shared singleton. */
        public fun <T : Comparable<T>> empty(): BrodalOkasakiHeap<T> =
            BrodalOkasakiHeap(root = null, count = 0, comparator = naturalOrder())

        /** Creates an empty heap retaining [comparator] by identity. */
        public fun <T> empty(comparator: Comparator<in T>): BrodalOkasakiHeap<T> =
            BrodalOkasakiHeap(root = null, count = 0, comparator)

        /** O(n) bulk construction by repeated worst-case-O(1) insertion. */
        public fun <T : Comparable<T>> from(items: Iterable<T>): BrodalOkasakiHeap<T> =
            from(items, naturalOrder())

        /** O(n) bulk construction retaining [comparator]. */
        public fun <T> from(
            items: Iterable<T>,
            comparator: Comparator<in T>,
        ): BrodalOkasakiHeap<T> {
            var result = empty(comparator)
            for (item in items) {
                result = result.insert(item)
            }
            return result
        }
    }

    public val isEmpty: Boolean
        get() = root == null

    /** The minimum in O(1), including when T itself is nullable. */
    public val minimum: T
        get() {
            val actualRoot = root ?: throw NoSuchElementException("The heap is empty.")
            return actualRoot.value
        }

    /** Returns a successful value wrapper, or null only when the heap is empty. */
    public fun minimumView(): BrodalMinimumView<T>? {
        val actualRoot = root ?: return null
        return BrodalMinimumView(actualRoot.value, deleteMinimum())
    }

    /** Inserts [item] in worst-case O(1). */
    public fun insert(item: T): BrodalOkasakiHeap<T> {
        val actualRoot = root
            ?: return BrodalOkasakiHeap(Tree(0, item, children = null), 1, comparator)
        val updatedRoot = if (lessOrEqual(item, actualRoot.value)) {
            Tree(0, item, Forest(actualRoot, tail = null))
        } else {
            Tree(0, actualRoot.value, skewInsert(Tree(0, item, children = null), actualRoot.children))
        }
        return BrodalOkasakiHeap(updatedRoot, Math.addExact(count, 1), comparator)
    }

    /**
     * Melds two heaps in worst-case O(1). The exact comparator object is representation policy.
     */
    public fun meld(other: BrodalOkasakiHeap<T>): BrodalOkasakiHeap<T> {
        require(comparator === other.comparator) {
            "Heap melding requires the same comparator object."
        }
        val leftRoot = root ?: return other
        val rightRoot = other.root ?: return this
        val updatedRoot = if (lessOrEqual(leftRoot.value, rightRoot.value)) {
            Tree(0, leftRoot.value, skewInsert(rightRoot, leftRoot.children))
        } else {
            Tree(0, rightRoot.value, skewInsert(leftRoot, rightRoot.children))
        }
        return BrodalOkasakiHeap(
            updatedRoot,
            Math.addExact(count, other.count),
            comparator,
        )
    }

    /** Removes the minimum in worst-case O(log n). */
    public fun deleteMinimum(): BrodalOkasakiHeap<T> {
        val actualRoot = root ?: throw NoSuchElementException("The heap is empty.")
        val children = actualRoot.children ?: return empty(comparator)
        val (minimumTree, remainder) = getMinimum(children)
        val split = splitForest(minimumTree.rank, minimumTree.children)
        var merged = skewMeld(skewMeld(split.trees, remainder), split.embeddedForest)
        val zeros = toArray(split.zeros)
        for (index in zeros.lastIndex downTo 0) {
            merged = skewInsert(zeros[index], merged)
        }
        return BrodalOkasakiHeap(
            Tree(0, minimumTree.value, merged),
            Math.subtractExact(count, 1),
            comparator,
        )
    }

    /** Structural-order iteration is O(n), stack-safe, and performs no comparisons. */
    override fun iterator(): Iterator<T> = object : Iterator<T> {
        private val pending = ArrayList<Tree<T>>().apply { root?.let(::add) }

        override fun hasNext(): Boolean = pending.isNotEmpty()

        override fun next(): T {
            if (pending.isEmpty()) {
                throw NoSuchElementException()
            }
            val tree = pending.removeAt(pending.lastIndex)
            var forest = tree.children
            while (forest != null) {
                pending.add(forest.head)
                forest = forest.tail
            }
            return tree.value
        }
    }

    /**
     * Validates the global root, every fused primitive-child/embedded-forest boundary, skew ranks,
     * heap order, logical count, and depth with an explicit O(n)-space worklist.
     */
    public fun validateStructure(): BrodalOkasakiHeapStatistics {
        val actualRoot = root
        if (actualRoot == null) {
            check(count == 0) { "An empty Brodal-Okasaki heap has a nonzero count." }
            return BrodalOkasakiHeapStatistics(0, 0, 0, 0)
        }
        check(actualRoot.rank == 0) { "The Brodal-Okasaki global root must have rank zero." }
        val rootForestLength = validateSkewForest(actualRoot.children)
        val pending = ArrayList<TreeFrame<T>>()
        pending.add(TreeFrame(actualRoot, depth = 1))
        var logicalCount = 0
        var maximumRank = 0
        var maximumDepth = 0
        while (pending.isNotEmpty()) {
            val frame = pending.removeAt(pending.lastIndex)
            val tree = frame.tree
            check(tree.rank >= 0) { "A Brodal-Okasaki tree has a negative rank." }
            logicalCount = Math.addExact(logicalCount, 1)
            maximumRank = max(maximumRank, tree.rank)
            maximumDepth = max(maximumDepth, frame.depth)
            validateSkewForest(validateFusedChildren(tree))

            var forest = tree.children
            while (forest != null) {
                check(lessOrEqual(tree.value, forest.head.value)) {
                    "A Brodal-Okasaki child outranks its parent."
                }
                pending.add(TreeFrame(forest.head, Math.addExact(frame.depth, 1)))
                forest = forest.tail
            }
        }
        check(logicalCount == count) {
            "Brodal-Okasaki logical count disagrees with its tree graph."
        }
        return BrodalOkasakiHeapStatistics(count, rootForestLength, maximumRank, maximumDepth)
    }

    /** Counts distinct tree objects shared by identity; intended for persistence audits. */
    internal fun sharedTreeCount(other: BrodalOkasakiHeap<T>): Int {
        val oldTrees = IdentityHashMap<Tree<T>, Boolean>()
        collectTrees(root, oldTrees)
        val visited = IdentityHashMap<Tree<T>, Boolean>()
        val pending = ArrayList<Tree<T>>().apply { other.root?.let(::add) }
        var shared = 0
        while (pending.isNotEmpty()) {
            val tree = pending.removeAt(pending.lastIndex)
            if (visited.put(tree, true) != null) {
                continue
            }
            if (oldTrees.containsKey(tree)) {
                shared++
            }
            var forest = tree.children
            while (forest != null) {
                pending.add(forest.head)
                forest = forest.tail
            }
        }
        return shared
    }

    internal fun rootIdentityForTesting(): Any? = root

    private fun collectTrees(tree: Tree<T>?, destination: IdentityHashMap<Tree<T>, Boolean>) {
        val pending = ArrayList<Tree<T>>().apply { tree?.let(::add) }
        while (pending.isNotEmpty()) {
            val current = pending.removeAt(pending.lastIndex)
            if (destination.put(current, true) != null) {
                continue
            }
            var forest = current.children
            while (forest != null) {
                pending.add(forest.head)
                forest = forest.tail
            }
        }
    }

    private fun lessOrEqual(left: T, right: T): Boolean = comparator.compare(left, right) <= 0

    private fun skewInsert(tree: Tree<T>, forest: Forest<T>?): Forest<T> {
        val tail = forest?.tail
        return if (forest != null && tail != null && forest.head.rank == tail.head.rank) {
            Forest(skewLink(tree, forest.head, tail.head), tail.tail)
        } else {
            Forest(tree, forest)
        }
    }

    private fun skewLink(zero: Tree<T>, first: Tree<T>, second: Tree<T>): Tree<T> {
        check(first.rank == second.rank) { "Invalid skew-link ranks." }
        return when {
            lessOrEqual(first.value, zero.value) && lessOrEqual(first.value, second.value) ->
                Tree(first.rank + 1, first.value, Forest(zero, Forest(second, first.children)))
            lessOrEqual(second.value, zero.value) && lessOrEqual(second.value, first.value) ->
                Tree(second.rank + 1, second.value, Forest(zero, Forest(first, second.children)))
            else -> Tree(first.rank + 1, zero.value, Forest(first, Forest(second, zero.children)))
        }
    }

    private fun link(first: Tree<T>, second: Tree<T>): Tree<T> {
        check(first.rank == second.rank) { "Invalid binomial-link ranks." }
        return if (lessOrEqual(first.value, second.value)) {
            Tree(first.rank + 1, first.value, Forest(second, first.children))
        } else {
            Tree(second.rank + 1, second.value, Forest(first, second.children))
        }
    }

    private fun getMinimum(forest: Forest<T>): MinimumTree<T> {
        var minimumForest = forest
        var cursor = forest.tail
        while (cursor != null) {
            if (comparator.compare(cursor.head.value, minimumForest.head.value) < 0) {
                minimumForest = cursor
            }
            cursor = cursor.tail
        }

        val prefix = ArrayList<Tree<T>>()
        cursor = forest
        while (cursor !== minimumForest) {
            prefix.add(cursor!!.head)
            cursor = cursor.tail
        }
        var remainder = minimumForest.tail
        for (index in prefix.lastIndex downTo 0) {
            remainder = Forest(prefix[index], remainder)
        }
        return MinimumTree(minimumForest.head, remainder)
    }

    private fun splitForest(rank: Int, initialForest: Forest<T>?): SplitForest<T> {
        var currentRank = rank
        var zeros: Forest<T>? = null
        var trees: Forest<T>? = null
        var forest = initialForest
        while (true) {
            if (currentRank == 0) {
                return SplitForest(zeros, trees, forest)
            }
            val firstForest = forest
                ?: throw IllegalStateException("Invalid skew-binomial forest invariant.")
            val tail = firstForest.tail
            if (currentRank == 1 && tail == null) {
                return SplitForest(zeros, Forest(firstForest.head, trees), embeddedForest = null)
            }
            tail ?: throw IllegalStateException("Invalid skew-binomial forest invariant.")
            val first = firstForest.head
            val second = tail.head
            val rest = tail.tail
            if (currentRank == 1) {
                return if (second.rank == 0) {
                    SplitForest(Forest(first, zeros), Forest(second, trees), rest)
                } else {
                    SplitForest(zeros, Forest(first, trees), Forest(second, rest))
                }
            }
            if (first.rank == second.rank) {
                return SplitForest(zeros, Forest(first, Forest(second, trees)), rest)
            }
            if (first.rank == 0) {
                zeros = Forest(first, zeros)
                trees = Forest(second, trees)
                forest = rest
            } else {
                trees = Forest(first, trees)
                forest = Forest(second, rest)
            }
            currentRank--
        }
    }

    private fun skewMeld(left: Forest<T>?, right: Forest<T>?): Forest<T>? =
        unionUnique(uniquify(left), uniquify(right))

    private fun uniquify(forest: Forest<T>?): Forest<T>? =
        if (forest == null) null else insertRanked(forest.head, uniquify(forest.tail))

    private fun insertRanked(tree: Tree<T>, forest: Forest<T>?): Forest<T> {
        if (forest == null) {
            return Forest(tree, tail = null)
        }
        check(tree.rank <= forest.head.rank) { "Invalid ranked insertion order." }
        return if (tree.rank < forest.head.rank) {
            Forest(tree, forest)
        } else {
            insertRanked(link(tree, forest.head), forest.tail)
        }
    }

    private fun unionUnique(left: Forest<T>?, right: Forest<T>?): Forest<T>? {
        if (left == null) {
            return right
        }
        if (right == null) {
            return left
        }
        return when {
            left.head.rank < right.head.rank -> Forest(left.head, unionUnique(left.tail, right))
            left.head.rank > right.head.rank -> Forest(right.head, unionUnique(left, right.tail))
            else -> insertRanked(
                link(left.head, right.head),
                unionUnique(left.tail, right.tail),
            )
        }
    }

    private fun validateFusedChildren(tree: Tree<T>): Forest<T>? {
        var rank = tree.rank
        var forest = tree.children
        while (rank > 0) {
            val firstForest = forest
                ?: throw IllegalStateException("A ranked Brodal-Okasaki tree is missing structural children.")
            val first = firstForest.head
            if (rank == 1) {
                check(first.rank == 0) {
                    "A rank-one Brodal-Okasaki tree must begin with a rank-zero child."
                }
                val tail = firstForest.tail ?: return null
                return if (tail.head.rank == 0) tail.tail else tail
            }
            val tail = firstForest.tail
                ?: throw IllegalStateException("A ranked Brodal-Okasaki tree has an incomplete child encoding.")
            val second = tail.head
            if (first.rank == second.rank) {
                check(first.rank == rank - 1) {
                    "A skew-linked Brodal-Okasaki tree has invalid child ranks."
                }
                return tail.tail
            }
            if (first.rank == 0) {
                check(second.rank == rank - 1) {
                    "A skew-linked Brodal-Okasaki tree has an invalid ranked child."
                }
                forest = tail.tail
            } else {
                check(first.rank == rank - 1) {
                    "A linked Brodal-Okasaki tree has an invalid ranked child."
                }
                forest = tail
            }
            rank--
        }
        return forest
    }

    private fun validateSkewForest(initialForest: Forest<T>?): Int {
        var length = 0
        var previousRank = -1
        var duplicate = false
        var forest = initialForest
        while (forest != null) {
            val rank = forest.head.rank
            check(rank >= 0) { "A Brodal-Okasaki forest contains a negative rank." }
            check(rank >= previousRank) {
                "Brodal-Okasaki root-forest ranks are not nondecreasing."
            }
            if (rank == previousRank) {
                check(length == 1 && !duplicate) {
                    "Only the first two skew-forest ranks may be equal."
                }
                duplicate = true
            }
            previousRank = rank
            length++
            forest = forest.tail
        }
        return length
    }

    private fun toArray(forest: Forest<T>?): List<Tree<T>> {
        val result = ArrayList<Tree<T>>()
        var cursor = forest
        while (cursor != null) {
            result.add(cursor.head)
            cursor = cursor.tail
        }
        return result
    }

    private class Tree<T>(
        val rank: Int,
        val value: T,
        val children: Forest<T>?,
    )

    private class Forest<T>(
        val head: Tree<T>,
        val tail: Forest<T>?,
    )

    private data class TreeFrame<T>(val tree: Tree<T>, val depth: Int)

    private data class MinimumTree<T>(val tree: Tree<T>, val remainder: Forest<T>?)

    private data class SplitForest<T>(
        val zeros: Forest<T>?,
        val trees: Forest<T>?,
        val embeddedForest: Forest<T>?,
    )
}
