/*
 * Persistent relaxed radix-balanced vector.
 *
 * A strict radix vector indexes in effectively constant time but cannot split or concatenate
 * cheaply. The relaxed variant allows irregular nodes carrying a size table, which makes those
 * operations logarithmic while regular nodes keep pure radix arithmetic and store no table at all.
 */
package durable7.fingertree

import java.util.ConcurrentModificationException

private const val RrbRadixBits: Int = 5
private const val RrbBranchFactor: Int = 32
// The first term is the greatest minimum height required anywhere in the count domain;
// boundary-only concatenation may legally retain one additional level of slack.
private const val RrbMaximumHeight: Int = (Int.SIZE_BITS - 1) / RrbRadixBits + 1

/** A successful removal from the back of an [RrbVector]. */
public data class RrbPop<T>(
    public val value: T,
    public val rest: RrbVector<T>,
)

/**
 * An immutable relaxed radix-balanced vector with 32-element leaves and 32-way branches.
 *
 * Regular branches use five-bit radix arithmetic and allocate no size table. Branches made
 * irregular by split or concatenation cache cumulative child sizes. Updates copy one search path;
 * concatenation rebuilds only the two boundary spines and does not impose a global minimum
 * occupancy on nodes away from that seam.
 */
public class RrbVector<T> private constructor(
    private val root: RrbNode?,
) : Iterable<T> {
    public companion object {
        private val emptyInstance: RrbVector<Any?> = RrbVector(root = null)

        /** Returns the shared empty vector. */
        @Suppress("UNCHECKED_CAST")
        public fun <T> empty(): RrbVector<T> = emptyInstance as RrbVector<T>

        /** Builds a vector from [values] in iteration order. */
        @Suppress("UNCHECKED_CAST")
        public fun <T> from(values: Iterable<T>): RrbVector<T> {
            if (values is RrbVector<*>) {
                return values as RrbVector<T>
            }
            if (values is Collection<*> && values.isEmpty()) {
                return empty()
            }

            val builder = builder<T>()
            builder.appendAll(values)
            return builder.toImmutable()
        }

        /** Creates an empty append builder. */
        public fun <T> builder(): Builder<T> = Builder(empty())

        internal fun checkedElementCountForTesting(left: Int, right: Int): Int =
            checkedElementCount(left, right)
    }

    /** Number of elements. */
    public val size: Int
        get() = root?.count ?: 0

    /** Whether this vector has no elements. */
    public val isEmpty: Boolean
        get() = root == null

    /** Creates an immutable cursor before the first element. */
    public fun cursor(): RrbVectorCursor<T> = RrbVectorCursor.create(this, 0)

    /** Creates an immutable cursor at a boundary in `0..size`. */
    public fun cursorAt(position: Int): RrbVectorCursor<T>? =
        if (position < 0 || position > size) null else RrbVectorCursor.create(this, position)

    /** Internal tree height, where a leaf has height zero. */
    public val height: Int
        get() = root?.height ?: 0

    /** Returns the element at [index], or null when [index] is outside the vector. */
    public operator fun get(index: Int): T? =
        if (index < 0 || index >= size) null else itemAt(index)

    /** Returns the first element, or null for an empty vector. */
    public fun front(): T? = if (isEmpty) null else itemAt(0)

    /** Returns the last element, or null for an empty vector. */
    public fun back(): T? = if (isEmpty) null else itemAt(size - 1)

    /** Appends [value]. */
    public fun append(value: T): RrbVector<T> = concat(singleton(value))

    /** Prepends [value]. */
    public fun prepend(value: T): RrbVector<T> = singleton(value).concat(this)

    /**
     * Replaces the element at [index], returning null for an invalid index and this vector for an
     * equal-value replacement.
     */
    public fun setItem(index: Int, value: T): RrbVector<T>? {
        if (index < 0 || index >= size) {
            return null
        }
        val updated = setNode(root!!, index, value)
        return if (updated === root) this else RrbVector(updated)
    }

    /** Concatenates [other], rebalancing only the boundary spines. */
    public fun concat(other: RrbVector<T>): RrbVector<T> {
        if (root == null) {
            return other
        }
        if (other.root == null) {
            return this
        }
        checkedElementCount(size, other.size)

        val roots = concatNodes(root, other.root)
        return fromRoot(if (roots.size == 1) roots[0] else RrbBranch(roots))
    }

    /** Splits at [index], returning null when the boundary is invalid. */
    public fun splitAt(index: Int): Pair<RrbVector<T>, RrbVector<T>>? {
        if (index < 0 || index > size) {
            return null
        }
        if (index == 0) {
            return empty<T>() to this
        }
        if (index == size) {
            return this to empty()
        }

        val split = splitNode(root!!, index)
        return fromRoot(split.first) to fromRoot(split.second)
    }

    /** Inserts [value] at [index], or returns null when the boundary is invalid. */
    public fun insertAt(index: Int, value: T): RrbVector<T>? = insertRange(index, listOf(value))

    /** Inserts [values] at [index], or returns null when the boundary is invalid. */
    public fun insertRange(index: Int, values: Iterable<T>): RrbVector<T>? {
        val split = splitAt(index) ?: return null
        val middle = from(values)
        return if (middle.isEmpty) this else split.first.concat(middle).concat(split.second)
    }

    /** Removes the element at [index], or returns null when the index is invalid. */
    public fun removeAt(index: Int): RrbVector<T>? = removeRange(index, 1)

    /** Removes [count] elements at [index], or returns null when the range is invalid. */
    public fun removeRange(index: Int, count: Int): RrbVector<T>? {
        if (!isValidRrbRange(index, count, size)) {
            return null
        }
        if (count == 0) {
            return this
        }

        val first = splitAt(index)!!
        val second = first.second.splitAt(count)!!
        return first.first.concat(second.second)
    }

    /** Removes and returns the last element, or null when the vector is empty. */
    public fun tryRemoveLast(): RrbPop<T>? =
        if (isEmpty) null else RrbPop(itemAt(size - 1), removeRange(size - 1, 1)!!)

    /** Returns the shared empty vector, or this vector when already empty. */
    public fun clear(): RrbVector<T> = if (isEmpty) this else empty()

    /** Creates an append builder that adopts this vector as an O(1) frozen prefix. */
    public fun toBuilder(): Builder<T> = Builder(this)

    /** Materializes the elements in order. */
    public fun toList(): List<T> {
        val result = ArrayList<T>(size)
        for (item in this) {
            result.add(item)
        }
        return result
    }

    override fun iterator(): Iterator<T> = RrbIterator(root)

    internal val rootIdentity: Any?
        get() = root

    internal fun leafIdentitiesForTesting(): List<Any> {
        val node = root ?: return emptyList()
        val result = ArrayList<Any>()
        val stack = ArrayDeque<RrbNode>()
        stack.addLast(node)
        while (stack.isNotEmpty()) {
            when (val current = stack.removeLast()) {
                is RrbLeaf -> result.add(current)
                is RrbBranch -> {
                    for (index in current.children.lastIndex downTo 0) {
                        stack.addLast(current.children[index])
                    }
                }
            }
        }
        return result
    }

    /** Returns representation statistics when every internal invariant is valid. */
    internal fun validateStructure(): RrbVectorStatistics? {
        val node = root
        if (node == null) {
            return RrbVectorStatistics(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        }

        val accumulator = RrbValidationAccumulator()
        val result = validateNode(node, isRoot = true, accumulator)
        val statistics = accumulator.toStatistics(result.count, result.height)
        return if (
            result.valid &&
            result.count == size &&
            result.height == height &&
            result.height <= RrbMaximumHeight
        ) statistics else null
    }

    @Suppress("UNCHECKED_CAST")
    private fun itemAt(index: Int): T = getNode(root!!, index) as T

    private fun singleton(value: T): RrbVector<T> = RrbVector(RrbLeaf(arrayOf(value)))

    private fun fromRoot(candidate: RrbNode?): RrbVector<T> {
        var node = candidate
        while (node is RrbBranch && node.children.size == 1) {
            node = node.children[0]
        }
        return if (node == null) empty() else RrbVector(node)
    }

    /** Mutable append-only staging surface with cached, isolated immutable snapshots. */
    public class Builder<T> internal constructor(
        private var prefix: RrbVector<T>,
    ) : Iterable<T> {
        private val leaves: MutableList<RrbNode> = ArrayList()
        private var tail: Array<Any?> = arrayOfNulls(RrbBranchFactor)
        private var tailCount: Int = 0
        private var stagedCount: Int = 0
        private var version: Int = 0

        /** Number of elements in the frozen prefix and mutable staging area. */
        public val size: Int
            get() = checkedElementCount(prefix.size, stagedCount)

        /** Appends one element in amortized O(1). */
        public fun append(value: T): Builder<T> {
            ensureCanAppend(1)
            appendCore(value)
            version++
            return this
        }

        /** Appends every element in [values] exactly once. */
        public fun appendAll(values: Iterable<T>): Builder<T> {
            for (value in values) {
                append(value)
            }
            return this
        }

        /** Copies and appends every element in [values]. */
        public fun appendAll(values: Array<out T>): Builder<T> {
            if (values.isEmpty()) {
                return this
            }
            ensureCanAppend(values.size)
            var sourceIndex = 0
            while (sourceIndex < values.size) {
                val copied = minOf(RrbBranchFactor - tailCount, values.size - sourceIndex)
                System.arraycopy(values, sourceIndex, tail, tailCount, copied)
                sourceIndex += copied
                tailCount += copied
                stagedCount += copied
                if (tailCount == RrbBranchFactor) {
                    freezeFullTail()
                }
            }
            version++
            return this
        }

        /** Clears both the frozen prefix and staged elements. */
        public fun clear(): Builder<T> {
            if (size == 0) {
                return this
            }
            prefix = empty()
            leaves.clear()
            tail.fill(null, 0, tailCount)
            tailCount = 0
            stagedCount = 0
            version++
            return this
        }

        /** Freezes staged leaves; repeated clean calls return the same vector instance. */
        public fun toImmutable(): RrbVector<T> {
            if (stagedCount == 0) {
                return prefix
            }

            val nodes = ArrayList<RrbNode>(leaves.size + if (tailCount == 0) 0 else 1)
            nodes.addAll(leaves)
            if (tailCount != 0) {
                nodes.add(RrbLeaf(tail.copyOf(tailCount)))
                tail.fill(null, 0, tailCount)
            }

            val staged = RrbVector<T>(buildLevel(nodes))
            prefix = if (prefix.isEmpty) staged else prefix.concat(staged)
            leaves.clear()
            tailCount = 0
            stagedCount = 0
            return prefix
        }

        override fun iterator(): Iterator<T> {
            val capturedVersion = version
            val iterator = toImmutable().iterator()
            return object : Iterator<T> {
                override fun hasNext(): Boolean {
                    throwIfVersionChanged(capturedVersion)
                    return iterator.hasNext()
                }

                override fun next(): T {
                    throwIfVersionChanged(capturedVersion)
                    return iterator.next()
                }
            }
        }

        private fun appendCore(value: T) {
            tail[tailCount++] = value
            stagedCount++
            if (tailCount == RrbBranchFactor) {
                freezeFullTail()
            }
        }

        private fun freezeFullTail() {
            check(tailCount == RrbBranchFactor)
            leaves.add(RrbLeaf(tail))
            tail = arrayOfNulls(RrbBranchFactor)
            tailCount = 0
        }

        private fun ensureCanAppend(count: Int) {
            if (count < 0 || count > Int.MAX_VALUE - size) {
                throw ArithmeticException("An RRB vector cannot contain more than Int.MAX_VALUE elements.")
            }
        }

        private fun throwIfVersionChanged(capturedVersion: Int) {
            if (capturedVersion != version) {
                throw ConcurrentModificationException("The RRB vector builder was modified during iteration.")
            }
        }
    }
}

/**
 * Shape measurements returned by a successful structural audit. The regular and relaxed branch counts show how much of
 * the tree still uses pure radix addressing.
 */
internal data class RrbVectorStatistics(
    val count: Int,
    val height: Int,
    val leafCount: Int,
    val branchCount: Int,
    val regularBranchCount: Int,
    val relaxedBranchCount: Int,
    val minimumLeafLength: Int,
    val maximumLeafLength: Int,
    val minimumBranchingFactor: Int,
    val maximumBranchingFactor: Int,
)

private sealed class RrbNode {
    abstract val count: Int
    abstract val height: Int
}

private class RrbLeaf(
    val items: Array<Any?>,
) : RrbNode() {
    override val count: Int
        get() = items.size

    override val height: Int
        get() = 0
}

private class RrbBranch(
    val children: Array<RrbNode>,
) : RrbNode() {
    override val height: Int
    override val count: Int
    val cumulativeSizes: IntArray?

    val isRegular: Boolean
        get() = cumulativeSizes == null

    init {
        require(children.size in 1..RrbBranchFactor)
        val childHeight = children[0].height
        require(children.all { it.height == childHeight })
        height = Math.addExact(childHeight, 1)
        var total = 0
        for (child in children) {
            total = Math.addExact(total, child.count)
        }
        count = total
        cumulativeSizes = if (hasRegularLayout(children, height)) null else buildSizes(children)
    }

    fun findChild(index: Int): RrbChildLocation {
        check(index in 0 until count)
        val sizes = cumulativeSizes
        if (sizes == null) {
            val shift = height * RrbRadixBits
            val childIndex = (index.toLong() shr shift).toInt()
            val before = (childIndex.toLong() shl shift).toInt()
            check(childIndex in children.indices)
            return RrbChildLocation(childIndex, before)
        }

        var low = 0
        var high = sizes.lastIndex
        while (low < high) {
            val middle = (low + high) ushr 1
            if (index < sizes[middle]) {
                high = middle
            } else {
                low = middle + 1
            }
        }
        return RrbChildLocation(low, if (low == 0) 0 else sizes[low - 1])
    }

    companion object {
        fun hasRegularLayout(children: Array<RrbNode>, height: Int): Boolean {
            if (children.isEmpty() || height !in 1..RrbMaximumHeight) {
                return false
            }
            val childCapacity = 1L shl (height * RrbRadixBits)
            for (index in 0 until children.lastIndex) {
                if (children[index].count.toLong() != childCapacity) {
                    return false
                }
            }
            return children.last().count.toLong() <= childCapacity
        }

        private fun buildSizes(children: Array<RrbNode>): IntArray {
            val sizes = IntArray(children.size)
            var count = 0
            for (index in children.indices) {
                count = Math.addExact(count, children[index].count)
                sizes[index] = count
            }
            return sizes
        }
    }
}

private data class RrbChildLocation(val index: Int, val before: Int)

private class RrbIterator<T>(root: RrbNode?) : Iterator<T> {
    private val stack: ArrayDeque<RrbNode> = ArrayDeque()
    private var items: Array<Any?>? = null
    private var itemIndex: Int = 0

    init {
        if (root != null) {
            stack.addLast(root)
        }
        advanceLeaf()
    }

    override fun hasNext(): Boolean = items != null

    @Suppress("UNCHECKED_CAST")
    override fun next(): T {
        val leafItems = items ?: throw NoSuchElementException()
        val result = leafItems[itemIndex++] as T
        if (itemIndex == leafItems.size) {
            advanceLeaf()
        }
        return result
    }

    private fun advanceLeaf() {
        items = null
        itemIndex = 0
        while (stack.isNotEmpty()) {
            when (val node = stack.removeLast()) {
                is RrbLeaf -> {
                    items = node.items
                    return
                }
                is RrbBranch -> {
                    for (index in node.children.lastIndex downTo 0) {
                        stack.addLast(node.children[index])
                    }
                }
            }
        }
    }
}

private fun checkedElementCount(left: Int, right: Int): Int = Math.addExact(left, right)

private fun isValidRrbRange(index: Int, count: Int, size: Int): Boolean =
    index >= 0 && count >= 0 && count <= size && index <= size - count

private fun getNode(root: RrbNode, initialIndex: Int): Any? {
    var node = root
    var index = initialIndex
    while (node is RrbBranch) {
        val location = node.findChild(index)
        index -= location.before
        node = node.children[location.index]
    }
    return (node as RrbLeaf).items[index]
}

private fun setNode(node: RrbNode, index: Int, value: Any?): RrbNode {
    if (node is RrbLeaf) {
        if (rrbValuesEqual(node.items[index], value)) {
            return node
        }
        val items = node.items.copyOf()
        items[index] = value
        return RrbLeaf(items)
    }

    node as RrbBranch
    val location = node.findChild(index)
    val child = setNode(node.children[location.index], index - location.before, value)
    if (child === node.children[location.index]) {
        return node
    }
    val children = node.children.copyOf()
    children[location.index] = child
    return RrbBranch(children)
}

private fun <T> rrbValuesEqual(left: T, right: T): Boolean = left === right || left == right

private fun concatNodes(left: RrbNode, right: RrbNode): Array<RrbNode> {
    if (left.height == right.height) {
        return concatSameHeight(left, right)
    }
    if (left.height > right.height) {
        left as RrbBranch
        val boundary = concatNodes(left.children.last(), right)
        val children = ArrayList<RrbNode>(left.children.size - 1 + boundary.size)
        for (index in 0 until left.children.lastIndex) {
            children.add(left.children[index])
        }
        children.addAll(boundary)
        return partition(children)
    }

    right as RrbBranch
    val leading = concatNodes(left, right.children[0])
    val children = ArrayList<RrbNode>(leading.size + right.children.size - 1)
    children.addAll(leading)
    for (index in 1 until right.children.size) {
        children.add(right.children[index])
    }
    return partition(children)
}

private fun concatSameHeight(left: RrbNode, right: RrbNode): Array<RrbNode> {
    if (left is RrbLeaf) {
        right as RrbLeaf
        if (left.count == RrbBranchFactor && right.count == RrbBranchFactor) {
            return arrayOf(left, right)
        }

        val combined = arrayOfNulls<Any?>(left.count + right.count)
        System.arraycopy(left.items, 0, combined, 0, left.count)
        System.arraycopy(right.items, 0, combined, left.count, right.count)
        if (combined.size <= RrbBranchFactor) {
            return arrayOf(RrbLeaf(combined))
        }
        val split = combined.size / 2
        return arrayOf(
            RrbLeaf(combined.copyOfRange(0, split)),
            RrbLeaf(combined.copyOfRange(split, combined.size)),
        )
    }

    left as RrbBranch
    right as RrbBranch
    val boundary = concatSameHeight(left.children.last(), right.children[0])
    val children = ArrayList<RrbNode>(left.children.size - 1 + boundary.size + right.children.size - 1)
    for (index in 0 until left.children.lastIndex) {
        children.add(left.children[index])
    }
    children.addAll(boundary)
    for (index in 1 until right.children.size) {
        children.add(right.children[index])
    }
    return partition(children)
}

private fun partition(children: List<RrbNode>): Array<RrbNode> {
    if (children.size <= RrbBranchFactor) {
        return arrayOf(RrbBranch(children.toTypedArray()))
    }
    val split = children.size / 2
    return arrayOf(
        RrbBranch(children.subList(0, split).toTypedArray()),
        RrbBranch(children.subList(split, children.size).toTypedArray()),
    )
}

private fun splitNode(node: RrbNode, index: Int): Pair<RrbNode?, RrbNode?> {
    if (index == 0) {
        return null to node
    }
    if (index == node.count) {
        return node to null
    }
    if (node is RrbLeaf) {
        return RrbLeaf(node.items.copyOfRange(0, index)) to
            RrbLeaf(node.items.copyOfRange(index, node.items.size))
    }

    node as RrbBranch
    val location = node.findChild(index)
    val childSplit = splitNode(node.children[location.index], index - location.before)
    val left = ArrayList<RrbNode>(location.index + 1)
    for (childIndex in 0 until location.index) {
        left.add(node.children[childIndex])
    }
    childSplit.first?.let(left::add)

    val right = ArrayList<RrbNode>(node.children.size - location.index)
    childSplit.second?.let(right::add)
    for (childIndex in location.index + 1 until node.children.size) {
        right.add(node.children[childIndex])
    }
    return buildSameHeight(left) to buildSameHeight(right)
}

private fun buildSameHeight(nodes: List<RrbNode>): RrbNode? =
    if (nodes.isEmpty()) null else RrbBranch(nodes.toTypedArray())

private fun buildLevel(nodes: List<RrbNode>): RrbNode {
    require(nodes.isNotEmpty())
    var level = nodes
    while (level.size > 1) {
        val parents = ArrayList<RrbNode>((level.size + RrbBranchFactor - 1) / RrbBranchFactor)
        var index = 0
        while (index < level.size) {
            val end = minOf(index + RrbBranchFactor, level.size)
            parents.add(RrbBranch(level.subList(index, end).toTypedArray()))
            index = end
        }
        level = parents
    }
    return level[0]
}

private data class RrbValidationResult(val valid: Boolean, val count: Int, val height: Int)

private fun validateNode(
    node: RrbNode,
    isRoot: Boolean,
    accumulator: RrbValidationAccumulator,
): RrbValidationResult {
    if (node is RrbLeaf) {
        val count = node.items.size
        accumulator.addLeaf(count)
        return RrbValidationResult(
            valid = count in 1..RrbBranchFactor && node.count == count && node.height == 0,
            count = count,
            height = 0,
        )
    }

    node as RrbBranch
    val children = node.children
    accumulator.addBranch(children.size, node.isRegular)
    if (children.size !in 1..RrbBranchFactor || isRoot && children.size == 1) {
        return RrbValidationResult(false, node.count, node.height)
    }

    var valid = true
    var total = 0L
    var childHeight = -1
    for (index in children.indices) {
        val child = validateNode(children[index], isRoot = false, accumulator)
        valid = valid && child.valid
        if (index == 0) {
            childHeight = child.height
        } else if (child.height != childHeight) {
            valid = false
        }
        total += child.count.toLong()
    }

    val height = childHeight + 1
    val count = if (total <= Int.MAX_VALUE) total.toInt() else Int.MAX_VALUE
    valid = valid && total <= Int.MAX_VALUE && node.count.toLong() == total && node.height == height

    val regularLayout = RrbBranch.hasRegularLayout(children, height)
    valid = valid && node.isRegular == regularLayout
    val sizes = node.cumulativeSizes
    if (sizes == null) {
        valid = valid && regularLayout
    } else {
        valid = valid && !regularLayout && sizes.size == children.size
        var cumulative = 0L
        for (index in children.indices) {
            cumulative += children[index].count.toLong()
            valid = valid && sizes[index].toLong() == cumulative
        }
    }

    return RrbValidationResult(valid, count, height)
}

private class RrbValidationAccumulator {
    private var minimumLeafLength: Int = Int.MAX_VALUE
    private var maximumLeafLength: Int = 0
    private var minimumBranchingFactor: Int = Int.MAX_VALUE
    private var maximumBranchingFactor: Int = 0
    private var leafCount: Int = 0
    private var branchCount: Int = 0
    private var regularBranchCount: Int = 0
    private var relaxedBranchCount: Int = 0

    fun addLeaf(length: Int) {
        leafCount++
        minimumLeafLength = minOf(minimumLeafLength, length)
        maximumLeafLength = maxOf(maximumLeafLength, length)
    }

    fun addBranch(branchingFactor: Int, regular: Boolean) {
        branchCount++
        if (regular) {
            regularBranchCount++
        } else {
            relaxedBranchCount++
        }
        minimumBranchingFactor = minOf(minimumBranchingFactor, branchingFactor)
        maximumBranchingFactor = maxOf(maximumBranchingFactor, branchingFactor)
    }

    fun toStatistics(count: Int, height: Int): RrbVectorStatistics = RrbVectorStatistics(
        count = count,
        height = height,
        leafCount = leafCount,
        branchCount = branchCount,
        regularBranchCount = regularBranchCount,
        relaxedBranchCount = relaxedBranchCount,
        minimumLeafLength = if (leafCount == 0) 0 else minimumLeafLength,
        maximumLeafLength = maximumLeafLength,
        minimumBranchingFactor = if (branchCount == 0) 0 else minimumBranchingFactor,
        maximumBranchingFactor = maximumBranchingFactor,
    )
}
