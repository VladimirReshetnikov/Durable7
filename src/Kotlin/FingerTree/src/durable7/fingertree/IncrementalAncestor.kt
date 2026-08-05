/*
 * The append-only incremental level-ancestor seam shared by the ancestry-backed collections.
 *
 * Unlike the Haskell port, which dissolves the arena into ordinary immutable heap structure, the JVM
 * has the mutable state and the monitor the managed reference assumes, so this port keeps the arena
 * object, its integer handles, and its lock, and reproduces the reference's contract directly.
 */
package durable7.fingertree

/**
 * An append-only incremental level-ancestor service: a monotone forest that grows one leaf at a time
 * and answers "which ancestor of this node sits at absolute depth `d`?".
 *
 * Node handles are stable integers owned by one arena. The distinguished [bottom] node has depth
 * `-1` and no value; every labelled node has a depth of zero or more. A leaf may be added below any
 * previously published node, not only below the most recently added one, so old consumer versions
 * grow into independent branches.
 *
 * An implementation must guarantee that a successful [addLeaf] publishes a fresh, never-recycled
 * handle whose immutable parent is the supplied handle and whose depth is exactly one greater; that
 * [depthOf], [parentOf], [valueAt], and ancestor answers for a published node never change; and that
 * a failed addition publishes no partially initialized node.
 *
 * [depthOf], [parentOf], and [valueAt] must take O(1) time: the parameterized bounds documented by
 * the consuming collections assume those primitive reads are constant-time. Let `U` be leaf-add time
 * and `Q` be level-ancestor query time after `M` published nodes. An implementation with
 * `U = Q = O(1)` worst case gives the consumers their optimal bound; neither logarithmic binary
 * lifting nor the shipped [MyersIncrementalAncestorArena] satisfies that stronger backend bound.
 *
 * Integer handles are arena-relative capabilities. Passing a handle obtained from a different arena
 * violates this interface's precondition even when the same integer happens to be valid locally,
 * because range validation cannot identify an in-range integer copied from another arena. Callers
 * must preserve arena ownership themselves.
 *
 * @param T the value stored in each non-bottom node.
 */
public interface IncrementalAncestorArena<T> {
    /** The distinguished unlabelled node at depth `-1`. */
    public val bottom: Int

    /** The number of labelled nodes published by this arena. */
    public val publishedNodeCount: Int

    /**
     * Adds a labelled leaf below a previously published node and returns its stable handle.
     *
     * @param parent the bottom node or a labelled node owned by this arena.
     * @param value the new node's value.
     */
    public fun addLeaf(parent: Int, value: T): Int

    /**
     * The node's immutable absolute depth; the bottom node has depth `-1`.
     *
     * @param node a node owned by this arena.
     */
    public fun depthOf(node: Int): Int

    /**
     * A labelled node's parent, or [bottom] when the node has depth zero.
     *
     * @param node a labelled node owned by this arena.
     */
    public fun parentOf(node: Int): Int

    /**
     * The unique ancestor of a node at an absolute depth.
     *
     * @param node a node owned by this arena.
     * @param depth a depth from `-1` through the node's own depth.
     */
    public fun ancestorAtDepth(node: Int, depth: Int): Int

    /**
     * The immutable value associated with a labelled node.
     *
     * @param node a non-bottom node owned by this arena.
     */
    public fun valueAt(node: Int): T
}

/**
 * One snapshot of a Myers incremental-ancestor arena's representation and traversal counters.
 *
 * @property publishedNodeCount the number of labelled nodes added to the arena.
 * @property blockCount the number of allocated odd-sized blocks, including the bottom block.
 * @property allocatedSlotCount the total slots in the odd-sized blocks.
 * @property addLeafCount the number of successful leaf additions.
 * @property ancestorQueryCount the number of completed ancestor queries, saturated at [Long.MAX_VALUE].
 * @property lastAncestorHopCount the parent/jump hops made by the most recent query.
 * @property maximumAncestorHopCount the greatest parent/jump hop count observed.
 * @property totalAncestorHopCount the total hops across all queries, saturated at [Long.MAX_VALUE].
 */
public data class MyersIncrementalAncestorStatistics(
    val publishedNodeCount: Int,
    val blockCount: Int,
    val allocatedSlotCount: Long,
    val addLeafCount: Long,
    val ancestorQueryCount: Long,
    val lastAncestorHopCount: Int,
    val maximumAncestorHopCount: Int,
    val totalAncestorHopCount: Long,
)

/**
 * An incremental ancestor arena using the two-link applicative-stack scheme of Myers.
 *
 * Each immutable node keeps one parent link and one coalesced jump link. Adding a leaf performs O(1)
 * link work. Nodes are retained in blocks of sizes 1, 3, 5, ..., whose square boundaries provide
 * O(1) addressing and O(sqrt(M)) unused slots after `M` published nodes; managed block and directory
 * allocation makes insertion O(1) amortized rather than worst case. An ancestor query follows
 * O(log M) parent/jump links in the worst case. Total arena storage is O(M), and every published
 * value stays reachable until the arena is collected.
 *
 * Operations are serialized by one private monitor. Published nodes are immutable, so retained
 * collection handles remain semantically stable; this implementation makes no lock-free progress
 * guarantee. Handle validation is range-based and cannot identify an in-range integer copied from
 * another arena.
 *
 * @param T the value stored in each non-bottom node.
 */
public class MyersIncrementalAncestorArena<T> : IncrementalAncestorArena<T> {
    private val gate = Any()
    private val nodes = OddBlockStore()
    private var addLeafCount: Long = 0
    private var ancestorQueryCount: Long = 0
    private var totalAncestorHopCount: Long = 0
    private var lastAncestorHopCount: Int = 0
    private var maximumAncestorHopCount: Int = 0

    init {
        nodes.append(Node(null, 0, -1, 0, 0))
    }

    /** The distinguished unlabelled node at depth `-1`. This read is O(1). */
    override val bottom: Int
        get() = 0

    /** The number of labelled nodes published by this arena. This read is O(1). */
    override val publishedNodeCount: Int
        get() = synchronized(gate) { nodes.count - 1 }

    /**
     * Adds a labelled leaf below a previously published node.
     *
     * This addition is O(1) amortized over the block and directory allocations.
     *
     * @throws IllegalArgumentException when [parent] was never published by this arena.
     * @throws ArithmeticException when the depth, the node count, or a coalesced jump distance
     *   cannot grow further.
     */
    override fun addLeaf(parent: Int, value: T): Int = synchronized(gate) {
        val parentNode = nodeAt(parent)
        if (parentNode.depth == Int.MAX_VALUE) {
            throw ArithmeticException("The ancestry depth exceeds Int.MAX_VALUE.")
        }

        val skip: Int
        val skipDistance: Int
        if (parent == bottom || parentNode.skip == bottom) {
            skip = parent
            skipDistance = 1
        } else {
            val skipped = nodes[parentNode.skip]
            if (skipped.skipDistance <= parentNode.skipDistance) {
                skip = skipped.skip
                skipDistance = Math.addExact(Math.addExact(1, parentNode.skipDistance), skipped.skipDistance)
            } else {
                skip = parent
                skipDistance = 1
            }
        }

        val handle = nodes.append(Node(value, parent, parentNode.depth + 1, skip, skipDistance))
        addLeafCount++
        handle
    }

    /**
     * The node's immutable absolute depth. This read is O(1).
     *
     * @throws IllegalArgumentException when [node] was never published by this arena.
     */
    override fun depthOf(node: Int): Int = synchronized(gate) { nodeAt(node).depth }

    /**
     * A labelled node's parent. This read is O(1).
     *
     * @throws IllegalArgumentException when [node] is the bottom node, which has no parent, or was
     *   never published by this arena.
     */
    override fun parentOf(node: Int): Int = synchronized(gate) {
        require(node != bottom) { "The bottom node has no parent." }
        nodeAt(node).parent
    }

    /**
     * The unique ancestor of a node at an absolute depth.
     *
     * This query follows O(log M) parent/jump links in the worst case after `M` published nodes.
     *
     * @throws IllegalArgumentException when [node] was never published by this arena, or [depth] is
     *   outside `-1` through the node's own depth.
     */
    override fun ancestorAtDepth(node: Int, depth: Int): Int = synchronized(gate) {
        var current = nodeAt(node)
        require(depth >= -1 && depth <= current.depth) { "The depth is outside the node's ancestry." }

        var handle = node
        var hops = 0
        while (current.depth > depth) {
            val remaining = current.depth - depth
            handle = if (current.skipDistance > 0 && current.skipDistance <= remaining) {
                current.skip
            } else {
                current.parent
            }
            current = nodes[handle]
            hops++
        }

        if (ancestorQueryCount != Long.MAX_VALUE) {
            ancestorQueryCount++
        }
        lastAncestorHopCount = hops
        totalAncestorHopCount = saturatingAdd(totalAncestorHopCount, hops)
        if (hops > maximumAncestorHopCount) {
            maximumAncestorHopCount = hops
        }
        handle
    }

    /**
     * The immutable value associated with a labelled node. This read is O(1).
     *
     * @throws IllegalArgumentException when [node] is the bottom node, which has no value, or was
     *   never published by this arena.
     */
    @Suppress("UNCHECKED_CAST")
    override fun valueAt(node: Int): T = synchronized(gate) {
        require(node != bottom) { "The bottom node has no value." }
        nodeAt(node).value as T
    }

    /** Deterministic representation and traversal counters, read in O(1). */
    public fun statistics(): MyersIncrementalAncestorStatistics = synchronized(gate) {
        MyersIncrementalAncestorStatistics(
            publishedNodeCount = nodes.count - 1,
            blockCount = nodes.blockCount,
            allocatedSlotCount = nodes.allocatedSlotCount,
            addLeafCount = addLeafCount,
            ancestorQueryCount = ancestorQueryCount,
            lastAncestorHopCount = lastAncestorHopCount,
            maximumAncestorHopCount = maximumAncestorHopCount,
            totalAncestorHopCount = totalAncestorHopCount,
        )
    }

    private fun nodeAt(node: Int): Node {
        require(node >= 0 && node < nodes.count) { "The node was never published by this arena." }
        return nodes[node]
    }

    private class Node(
        val value: Any?,
        val parent: Int,
        val depth: Int,
        val skip: Int,
        val skipDistance: Int,
    )

    private class OddBlockStore {
        private val blocks = ArrayList<Array<Any?>>()

        var count: Int = 0
            private set

        var allocatedSlotCount: Long = 0
            private set

        val blockCount: Int
            get() = blocks.size

        operator fun get(index: Int): Node {
            require(index >= 0 && index < count) { "The node index is outside the arena." }
            val block = integerSquareRoot(index)
            return blocks[block][index - block * block] as Node
        }

        fun append(item: Node): Int {
            if (count == Int.MAX_VALUE) {
                throw ArithmeticException("The arena contains Int.MAX_VALUE nodes.")
            }

            val index = count
            val block = integerSquareRoot(index)
            if (block == blocks.size) {
                val blockLength = Math.addExact(Math.multiplyExact(2, block), 1)
                blocks.add(arrayOfNulls(blockLength))
                allocatedSlotCount += blockLength.toLong()
            }

            blocks[block][index - block * block] = item
            count = index + 1
            return index
        }

        private fun integerSquareRoot(value: Int): Int {
            var root = Math.sqrt(value.toDouble()).toInt()
            while ((root + 1).toLong() * (root + 1).toLong() <= value.toLong()) {
                root++
            }
            while (root.toLong() * root.toLong() > value.toLong()) {
                root--
            }
            return root
        }
    }

    private companion object {
        fun saturatingAdd(value: Long, increment: Int): Long =
            if (value > Long.MAX_VALUE - increment) Long.MAX_VALUE else value + increment
    }
}
