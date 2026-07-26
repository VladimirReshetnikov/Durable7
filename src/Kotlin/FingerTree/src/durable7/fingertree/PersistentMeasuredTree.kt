/*
 * The measured tree that most of this package is built on.
 *
 * Caches a monoidal measure at every node, so choosing a different measure specializes the same
 * tree into a different structure: counting gives positional access, summing gives cumulative-
 * weight search, tracking a maximum gives a priority queue.
 */
package durable7.fingertree

import java.util.Collections
import java.util.IdentityHashMap

/** Internal immutable measured AVL sequence shared by the Kotlin facades. */
internal class PersistentMeasuredTree<T, M> private constructor(
    internal val root: Node<T, M>?,
    internal val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    internal class Node<T, M>(
        val left: Node<T, M>?,
        val value: T,
        val right: Node<T, M>?,
        val size: Int,
        val height: Int,
        val measure: M,
    )

    internal data class Locate<T, M>(
        val index: Int,
        val measureBefore: M,
        val value: T?,
        val found: Boolean,
    )

    internal companion object {
        fun <T, M> empty(policy: MeasurePolicy<T, M>): PersistentMeasuredTree<T, M> =
            PersistentMeasuredTree(null, policy)

        fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): PersistentMeasuredTree<T, M> {
            val owned = values.toList()
            return PersistentMeasuredTree(buildBalanced(owned, 0, owned.size, policy), policy)
        }

        private fun <T, M> buildBalanced(
            values: List<T>,
            start: Int,
            end: Int,
            policy: MeasurePolicy<T, M>,
        ): Node<T, M>? {
            if (start >= end) {
                return null
            }

            val middle = start + (end - start) / 2
            return makeNode(
                buildBalanced(values, start, middle, policy),
                values[middle],
                buildBalanced(values, middle + 1, end, policy),
                policy,
            )
        }
    }

    val size: Int
        get() = nodeSize(root)

    val isEmpty: Boolean
        get() = root == null

    fun measure(): M = if (root == null) policy.empty else root.measure

    operator fun get(index: Int): T? {
        if (index < 0 || index >= size) {
            return null
        }

        return getNode(root!!, index)
    }

    fun front(): T? = if (root == null) null else firstNode(root).value

    fun back(): T? = if (root == null) null else lastNode(root).value

    fun prepend(value: T): PersistentMeasuredTree<T, M> =
        PersistentMeasuredTree(joinNodes(null, value, root, policy), policy)

    fun append(value: T): PersistentMeasuredTree<T, M> =
        PersistentMeasuredTree(joinNodes(root, value, null, policy), policy)

    fun concat(other: PersistentMeasuredTree<T, M>): PersistentMeasuredTree<T, M> {
        require(policy === other.policy || policy == other.policy) {
            "Cannot concatenate trees with different measure policies."
        }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> PersistentMeasuredTree(concatNodes(root, other.root, policy), policy)
        }
    }

    fun splitAt(index: Int): Pair<PersistentMeasuredTree<T, M>, PersistentMeasuredTree<T, M>>? {
        if (index < 0 || index > size) {
            return null
        }
        if (index == 0) {
            return empty(policy) to this
        }
        if (index == size) {
            return this to empty(policy)
        }

        val split = splitNode(root, index, policy)
        return PersistentMeasuredTree(split.first, policy) to PersistentMeasuredTree(split.second, policy)
    }

    fun insertAt(index: Int, value: T): PersistentMeasuredTree<T, M>? {
        val split = splitAt(index) ?: return null
        return PersistentMeasuredTree(joinNodes(split.first.root, value, split.second.root, policy), policy)
    }

    fun setItem(index: Int, value: T): PersistentMeasuredTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        return PersistentMeasuredTree(setNode(root!!, index, value, policy), policy)
    }

    fun removeAt(index: Int): PersistentMeasuredTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        val first = splitNode(root, index, policy)
        val second = splitNode(first.second, 1, policy)
        return PersistentMeasuredTree(concatNodes(first.first, second.second, policy), policy)
    }

    fun prefixMeasure(count: Int): M? {
        if (count < 0 || count > size) {
            return null
        }

        return measurePrefix(count)
    }

    /** Returns the ordered measure of the first [count] elements for a validated boundary. */
    fun measurePrefix(count: Int): M {
        require(count >= 0 && count <= size) { "Prefix boundary must be in 0..size." }
        return prefixMeasureNode(root, count, policy)
    }

    /** Returns the ordered measure of the elements at and after [startIndex] for a validated boundary. */
    fun measureSuffix(startIndex: Int): M {
        require(startIndex >= 0 && startIndex <= size) { "Suffix boundary must be in 0..size." }
        return suffixMeasureNode(root, startIndex, policy)
    }

    fun locate(predicate: (M) -> Boolean): Locate<T, M> =
        locateNode(root, 0, policy.empty, predicate, policy)

    /**
     * Returns the number of leading elements matching [isInPrefix]. Requires
     * the sequence to be partitioned: every matching element precedes every
     * non-matching one. The descent walks a single root-to-leaf path, deciding
     * each step from the node's own value, so the cost is O(log n) with one
     * predicate evaluation per visited node.
     */
    fun prefixLength(isInPrefix: (T) -> Boolean): Int {
        var count = 0
        var node = root
        while (node != null) {
            node = if (isInPrefix(node.value)) {
                count += nodeSize(node.left) + 1
                node.right
            } else {
                node.left
            }
        }

        return count
    }

    fun splitByMeasure(predicate: (M) -> Boolean): Pair<PersistentMeasuredTree<T, M>, PersistentMeasuredTree<T, M>> {
        val located = locate(predicate)
        return splitAt(located.index)!!
    }

    fun toList(): List<T> {
        val result = ArrayList<T>(size)
        appendInOrder(root, result)
        return result
    }

    fun sharesStructureWith(other: PersistentMeasuredTree<T, M>): Boolean {
        if (root === other.root) {
            return true
        }
        if (root == null || other.root == null) {
            return false
        }

        val identities = Collections.newSetFromMap(IdentityHashMap<Node<T, M>, Boolean>())
        collectNodes(root, identities)
        return containsSharedNode(other.root, identities)
    }

    fun isBalanced(): Boolean = validateNode(root).valid

    override fun iterator(): Iterator<T> = NodeIterator(root)

    /**
     * Returns an in-order iterator positioned at [startIndex]. The seek skips
     * whole subtrees by cached size, so reaching the first element costs
     * O(log n) and streaming k elements costs O(k + log n) overall.
     */
    fun iteratorFrom(startIndex: Int): Iterator<T> = NodeIterator(root, startIndex)
}

private data class Validation(val valid: Boolean, val size: Int, val height: Int)

private fun <T, M> nodeSize(node: PersistentMeasuredTree.Node<T, M>?): Int = node?.size ?: 0

private fun <T, M> nodeHeight(node: PersistentMeasuredTree.Node<T, M>?): Int = node?.height ?: 0

private fun <T, M> nodeMeasure(node: PersistentMeasuredTree.Node<T, M>?, policy: MeasurePolicy<T, M>): M =
    if (node == null) policy.empty else node.measure

private fun <T, M> makeNode(
    left: PersistentMeasuredTree.Node<T, M>?,
    value: T,
    right: PersistentMeasuredTree.Node<T, M>?,
    policy: MeasurePolicy<T, M>,
): PersistentMeasuredTree.Node<T, M> = PersistentMeasuredTree.Node(
    left,
    value,
    right,
    nodeSize(left) + nodeSize(right) + 1,
    maxOf(nodeHeight(left), nodeHeight(right)) + 1,
    policy.combine(
        policy.combine(nodeMeasure(left, policy), policy.measure(value)),
        nodeMeasure(right, policy),
    ),
)

private fun <T, M> balanceNode(
    left: PersistentMeasuredTree.Node<T, M>?,
    value: T,
    right: PersistentMeasuredTree.Node<T, M>?,
    policy: MeasurePolicy<T, M>,
): PersistentMeasuredTree.Node<T, M> {
    if (nodeHeight(left) > nodeHeight(right) + 1) {
        val tall = left!!
        if (nodeHeight(tall.right) > nodeHeight(tall.left)) {
            val inner = tall.right!!
            return makeNode(
                makeNode(tall.left, tall.value, inner.left, policy),
                inner.value,
                makeNode(inner.right, value, right, policy),
                policy,
            )
        }

        return makeNode(tall.left, tall.value, makeNode(tall.right, value, right, policy), policy)
    }

    if (nodeHeight(right) > nodeHeight(left) + 1) {
        val tall = right!!
        if (nodeHeight(tall.left) > nodeHeight(tall.right)) {
            val inner = tall.left!!
            return makeNode(
                makeNode(left, value, inner.left, policy),
                inner.value,
                makeNode(inner.right, tall.value, tall.right, policy),
                policy,
            )
        }

        return makeNode(makeNode(left, value, tall.left, policy), tall.value, tall.right, policy)
    }

    return makeNode(left, value, right, policy)
}

private fun <T, M> joinNodes(
    left: PersistentMeasuredTree.Node<T, M>?,
    value: T,
    right: PersistentMeasuredTree.Node<T, M>?,
    policy: MeasurePolicy<T, M>,
): PersistentMeasuredTree.Node<T, M> = when {
    nodeHeight(left) > nodeHeight(right) + 1 -> {
        val tall = left!!
        balanceNode(tall.left, tall.value, joinNodes(tall.right, value, right, policy), policy)
    }
    nodeHeight(right) > nodeHeight(left) + 1 -> {
        val tall = right!!
        balanceNode(joinNodes(left, value, tall.left, policy), tall.value, tall.right, policy)
    }
    else -> makeNode(left, value, right, policy)
}

private fun <T, M> concatNodes(
    left: PersistentMeasuredTree.Node<T, M>?,
    right: PersistentMeasuredTree.Node<T, M>?,
    policy: MeasurePolicy<T, M>,
): PersistentMeasuredTree.Node<T, M>? {
    if (left == null) {
        return right
    }
    if (right == null) {
        return left
    }

    val removed = removeFirst(right, policy)
    return joinNodes(left, removed.first, removed.second, policy)
}

private fun <T, M> removeFirst(
    node: PersistentMeasuredTree.Node<T, M>,
    policy: MeasurePolicy<T, M>,
): Pair<T, PersistentMeasuredTree.Node<T, M>?> {
    if (node.left == null) {
        return node.value to node.right
    }

    val removed = removeFirst(node.left, policy)
    return removed.first to balanceNode(removed.second, node.value, node.right, policy)
}

private fun <T, M> splitNode(
    node: PersistentMeasuredTree.Node<T, M>?,
    index: Int,
    policy: MeasurePolicy<T, M>,
): Pair<PersistentMeasuredTree.Node<T, M>?, PersistentMeasuredTree.Node<T, M>?> {
    if (node == null) {
        return null to null
    }

    val leftSize = nodeSize(node.left)
    return if (index <= leftSize) {
        val split = splitNode(node.left, index, policy)
        split.first to joinNodes(split.second, node.value, node.right, policy)
    } else {
        val split = splitNode(node.right, index - leftSize - 1, policy)
        joinNodes(node.left, node.value, split.first, policy) to split.second
    }
}

private fun <T, M> getNode(node: PersistentMeasuredTree.Node<T, M>, index: Int): T {
    val leftSize = nodeSize(node.left)
    return when {
        index < leftSize -> getNode(node.left!!, index)
        index == leftSize -> node.value
        else -> getNode(node.right!!, index - leftSize - 1)
    }
}

private fun <T, M> setNode(
    node: PersistentMeasuredTree.Node<T, M>,
    index: Int,
    value: T,
    policy: MeasurePolicy<T, M>,
): PersistentMeasuredTree.Node<T, M> {
    val leftSize = nodeSize(node.left)
    return when {
        index < leftSize -> makeNode(setNode(node.left!!, index, value, policy), node.value, node.right, policy)
        index == leftSize -> makeNode(node.left, value, node.right, policy)
        else -> makeNode(node.left, node.value, setNode(node.right!!, index - leftSize - 1, value, policy), policy)
    }
}

private fun <T, M> prefixMeasureNode(
    node: PersistentMeasuredTree.Node<T, M>?,
    count: Int,
    policy: MeasurePolicy<T, M>,
): M {
    if (node == null || count == 0) {
        return policy.empty
    }
    if (count == node.size) {
        return node.measure
    }

    val leftSize = nodeSize(node.left)
    return when {
        count <= leftSize -> prefixMeasureNode(node.left, count, policy)
        count == leftSize + 1 -> policy.combine(nodeMeasure(node.left, policy), policy.measure(node.value))
        else -> policy.combine(
            policy.combine(nodeMeasure(node.left, policy), policy.measure(node.value)),
            prefixMeasureNode(node.right, count - leftSize - 1, policy),
        )
    }
}

private fun <T, M> suffixMeasureNode(
    node: PersistentMeasuredTree.Node<T, M>?,
    startIndex: Int,
    policy: MeasurePolicy<T, M>,
): M {
    if (node == null || startIndex == node.size) {
        return policy.empty
    }
    if (startIndex == 0) {
        return node.measure
    }

    val leftSize = nodeSize(node.left)
    return when {
        startIndex <= leftSize -> policy.combine(
            suffixMeasureNode(node.left, startIndex, policy),
            policy.combine(policy.measure(node.value), nodeMeasure(node.right, policy)),
        )
        startIndex == leftSize + 1 -> nodeMeasure(node.right, policy)
        else -> suffixMeasureNode(node.right, startIndex - leftSize - 1, policy)
    }
}

private fun <T, M> locateNode(
    node: PersistentMeasuredTree.Node<T, M>?,
    baseIndex: Int,
    before: M,
    predicate: (M) -> Boolean,
    policy: MeasurePolicy<T, M>,
): PersistentMeasuredTree.Locate<T, M> {
    if (node == null) {
        return PersistentMeasuredTree.Locate(baseIndex, before, null, false)
    }

    val throughLeft = policy.combine(before, nodeMeasure(node.left, policy))
    if (node.left != null && predicate(throughLeft)) {
        return locateNode(node.left, baseIndex, before, predicate, policy)
    }

    val throughValue = policy.combine(throughLeft, policy.measure(node.value))
    if (predicate(throughValue)) {
        return PersistentMeasuredTree.Locate(baseIndex + nodeSize(node.left), throughLeft, node.value, true)
    }

    return locateNode(
        node.right,
        baseIndex + nodeSize(node.left) + 1,
        throughValue,
        predicate,
        policy,
    )
}

private fun <T, M> firstNode(node: PersistentMeasuredTree.Node<T, M>): PersistentMeasuredTree.Node<T, M> =
    if (node.left == null) node else firstNode(node.left)

private fun <T, M> lastNode(node: PersistentMeasuredTree.Node<T, M>): PersistentMeasuredTree.Node<T, M> =
    if (node.right == null) node else lastNode(node.right)

private fun <T, M> appendInOrder(node: PersistentMeasuredTree.Node<T, M>?, destination: MutableList<T>) {
    if (node == null) {
        return
    }
    appendInOrder(node.left, destination)
    destination.add(node.value)
    appendInOrder(node.right, destination)
}

private fun <T, M> collectNodes(
    node: PersistentMeasuredTree.Node<T, M>?,
    destination: MutableSet<PersistentMeasuredTree.Node<T, M>>,
) {
    if (node == null) {
        return
    }
    destination.add(node)
    collectNodes(node.left, destination)
    collectNodes(node.right, destination)
}

private fun <T, M> containsSharedNode(
    node: PersistentMeasuredTree.Node<T, M>?,
    candidates: Set<PersistentMeasuredTree.Node<T, M>>,
): Boolean = node != null && (
    candidates.contains(node) ||
        containsSharedNode(node.left, candidates) ||
        containsSharedNode(node.right, candidates)
    )

private fun <T, M> validateNode(node: PersistentMeasuredTree.Node<T, M>?): Validation {
    if (node == null) {
        return Validation(true, 0, 0)
    }

    val left = validateNode(node.left)
    val right = validateNode(node.right)
    val expectedSize = left.size + right.size + 1
    val expectedHeight = maxOf(left.height, right.height) + 1
    return Validation(
        left.valid && right.valid &&
            kotlin.math.abs(left.height - right.height) <= 1 &&
            node.size == expectedSize && node.height == expectedHeight,
        expectedSize,
        expectedHeight,
    )
}

private class NodeIterator<T, M>(
    root: PersistentMeasuredTree.Node<T, M>?,
    startIndex: Int = 0,
) : Iterator<T> {
    private val stack = ArrayDeque<PersistentMeasuredTree.Node<T, M>>()

    init {
        // Descend once from the root, keeping only the ancestors whose value
        // or right subtree still lies at or after startIndex. Left subtrees
        // that end before startIndex are skipped by cached size, so the seek
        // is O(log n); startIndex == 0 degenerates to pushLeft(root).
        var node = root
        var remaining = startIndex
        while (node != null) {
            val leftSize = nodeSize(node.left)
            when {
                remaining < leftSize -> {
                    stack.addLast(node)
                    node = node.left
                }
                remaining == leftSize -> {
                    stack.addLast(node)
                    node = null
                }
                else -> {
                    remaining -= leftSize + 1
                    node = node.right
                }
            }
        }
    }

    override fun hasNext(): Boolean = stack.isNotEmpty()

    override fun next(): T {
        if (stack.isEmpty()) {
            throw NoSuchElementException()
        }
        val node = stack.removeLast()
        pushLeft(node.right)
        return node.value
    }

    private fun pushLeft(start: PersistentMeasuredTree.Node<T, M>?) {
        var node = start
        while (node != null) {
            stack.addLast(node)
            node = node.left
        }
    }
}
