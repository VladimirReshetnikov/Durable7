/*
 * The measured finger tree that most of this package is built on.
 *
 * This is a persistent Hinze-Paterson 2-3 finger tree: 1-4-item digits at both ends of every
 * level, 2-3 nodes below the surface, and each level's middle subtree held behind a *memoized
 * suspension* - the paper's lazy tree realized in a strict language, exactly as the C#, C++, and
 * C workspaces realize it. Caching a monoidal measure at every node specializes the same tree
 * into different structures: counting gives positional access, summing gives cumulative-weight
 * search, tracking a maximum gives a priority queue.
 *
 * Bounds, with O(1) policy operations: front/back are O(1) worst-case digit reads; prepend,
 * append, and the endpoint views are O(1) amortized and O(log n) worst-case per call; concat is
 * O(log(min(n, m))) amortized; splitting, indexing, and locating are O(log n); iteration is O(n).
 * The amortized bounds hold under fully persistent branching histories, not merely ephemeral
 * linear use, because a forced suspension memoizes its result in a cell shared by every version
 * that references it: work deferred by one version and forced by another is never repeated.
 *
 * Laziness is load-bearing in exactly two places and eager everywhere else. A digit overflow on
 * prepend/append pushes a 2-3 node into the middle *inside* a suspension, and concat recurses
 * into the two middles *inside* a suspension; every size (and every element's and 2-3 node's
 * measure) is computed strictly at construction, so no size read or index descent ever forces
 * structure it does not enter. The suspensions are *defunctionalized*: the deferred operations
 * are stored as data and interpreted by an explicit-stack force loop, because organic append
 * loops build Theta(n)-deep pending chains that would overflow the JVM stack under a recursive
 * force. Forcing publishes through an atomic compare-and-set, so concurrent forces race benignly
 * - a losing thread discards its duplicate result and every reader sees one canonical value -
 * and a deferred computation that throws publishes nothing and may be retried, so policy
 * failures keep every retained version valid. A deep node's total measure is memoized separately
 * and computing it may force the middle spine, which is why measure() is O(1) amortized rather
 * than worst-case.
 */
package durable7.fingertree

import java.util.Collections
import java.util.IdentityHashMap
import java.util.concurrent.atomic.AtomicReference

/** Sentinel marking a suspension cell or memoized measure that has not been computed yet. */
private val UNFORCED = Any()

/**
 * A stored element together with its eagerly computed measure. Eager per-element measurement is
 * what keeps cached-query probes at zero policy calls: only `combine` is ever deferred.
 */
private class MeasuredLeaf(
    @JvmField val value: Any?,
    @JvmField val measure: Any?,
)

/**
 * A 2-3 node: strict size and measure, plus cached first and last descendant elements. The
 * cached extremes are what make front/back worst-case O(1) and keep partition-directed descents
 * ([PersistentMeasuredTree.prefixLength]) at O(log n) predicate evaluations.
 */
private class Node23(
    @JvmField val children: Array<Any>,
    @JvmField val size: Int,
    @JvmField val measure: Any?,
    @JvmField val first: MeasuredLeaf,
    @JvmField val last: MeasuredLeaf,
)

/** A one-item tree. The item is a [MeasuredLeaf] at the surface level, a [Node23] below it. */
private class Single(@JvmField val item: Any)

/**
 * Digits at both ends around a suspended middle of one-level-deeper items.
 *
 * [size] and [middleSize] are strict - every construction site knows them arithmetically - so no
 * size read ever forces the middle. The node's total measure is memoized on first read; the
 * benign publication race writes value-equal results, so a plain volatile field suffices.
 */
private class DeepNode(
    @JvmField val size: Int,
    @JvmField val prefix: Array<Any>,
    @JvmField val middle: Susp,
    @JvmField val middleSize: Int,
    @JvmField val suffix: Array<Any>,
) {
    @Volatile
    var cachedMeasure: Any? = UNFORCED
}

/**
 * A deferred spine operation, stored as data so forcing can interpret arbitrarily long chains
 * with an explicit stack instead of recursing once per link.
 */
private sealed class SuspOperation {
    /** The first input suspension that still needs forcing, or `null` when all are ready. */
    abstract fun firstUnforcedInput(): Susp?

    /** Runs the deferred step. All inputs are forced; a throwing policy publishes nothing. */
    abstract fun interpret(): Any?

    /** Push an overflow node onto the front of the suspended middle. */
    class PushFront(
        private val policy: MeasurePolicy<Any?, Any?>,
        private val item: Any,
        private val inner: Susp,
    ) : SuspOperation() {
        override fun firstUnforcedInput(): Susp? = if (inner.isForced) null else inner

        override fun interpret(): Any? = consTree(policy, item, inner.forcedValue())
    }

    /** Push an overflow node onto the back of the suspended middle. */
    class PushBack(
        private val policy: MeasurePolicy<Any?, Any?>,
        private val inner: Susp,
        private val item: Any,
    ) : SuspOperation() {
        override fun firstUnforcedInput(): Susp? = if (inner.isForced) null else inner

        override fun interpret(): Any? = snocTree(policy, inner.forcedValue(), item)
    }

    /** Concatenate two suspended middles around already-regrouped 2-3 nodes. */
    class ConcatMiddles(
        private val policy: MeasurePolicy<Any?, Any?>,
        private val left: Susp,
        private val between: List<Any>,
        private val right: Susp,
    ) : SuspOperation() {
        override fun firstUnforcedInput(): Susp? = when {
            !left.isForced -> left
            !right.isForced -> right
            else -> null
        }

        override fun interpret(): Any? = app3(policy, left.forcedValue(), between, right.forcedValue())
    }
}

/**
 * A memoized suspension of a subtree.
 *
 * Forcing interprets the pending chain iteratively, publishes each link's result through an
 * atomic compare-and-set (a lost race discards the duplicate result, so every version sharing
 * the cell reads the same forced subtree), and only then drops the pending payload. A deferred
 * computation that throws publishes nothing, leaving the cell pending and retryable, so a failing
 * policy keeps every retained version valid.
 */
private class Susp private constructor(
    initialValue: Any?,
    operation: SuspOperation?,
) {
    @Volatile
    var operation: SuspOperation? = operation

    private val cell = AtomicReference(initialValue)

    companion object {
        /** Wraps an already-built subtree; nothing is deferred. */
        fun ready(tree: Any?): Susp = Susp(tree, null)

        /** Defers [operation] until the suspension is first forced. */
        fun deferred(operation: SuspOperation): Susp = Susp(UNFORCED, operation)
    }

    val isForced: Boolean
        get() = cell.get() !== UNFORCED

    /** The published subtree; callers guarantee the cell was forced first. */
    fun forcedValue(): Any? {
        val value = cell.get()
        check(value !== UNFORCED) { "An interpreted suspension's inputs were forced first." }
        return value
    }

    /**
     * Forces the suspension, interpreting the pending chain iteratively with an explicit stack.
     *
     * Organic endpoint loops build Theta(n)-deep pending chains; a recursive force would take one
     * JVM frame per link and overflow the stack, which is why the deferred operations are data.
     */
    fun force(): Any? {
        val existing = cell.get()
        if (existing !== UNFORCED) {
            return existing
        }

        val stack = ArrayDeque<Susp>()
        stack.addLast(this)
        while (stack.isNotEmpty()) {
            val top = stack.last()
            if (top.cell.get() !== UNFORCED) {
                stack.removeLast()
                // Publication happened first, so dropping the payload cannot lose work.
                top.operation = null
                continue
            }

            // The payload is cleared only after publication, so a missing operation means another
            // thread published between the two reads; re-examine the cell.
            val pending = top.operation ?: continue
            val inner = pending.firstUnforcedInput()
            if (inner != null) {
                stack.addLast(inner)
                continue
            }

            // May throw (a failing policy): nothing is published and the force stays retryable.
            val result = pending.interpret()
            // A lost race discards the duplicate result; the published tree wins everywhere.
            top.cell.compareAndSet(UNFORCED, result)
        }

        return cell.get()
    }
}

/** The shared already-forced empty middle. Excluded from structural-sharing identity walks. */
private val EMPTY_MIDDLE = Susp.ready(null)

// --- item and digit helpers ----------------------------------------------------------------------
//
// An item is a MeasuredLeaf at the surface level and a Node23 below it. Split rebuilds
// legitimately produce trees whose digits mix levels, so every algorithm treats items as opaque
// size/measure/first/last carriers and never assumes a uniform level.

private fun itemSize(item: Any): Int = if (item is Node23) item.size else 1

private fun itemMeasure(item: Any): Any? =
    if (item is Node23) item.measure else (item as MeasuredLeaf).measure

private fun itemFirst(item: Any): MeasuredLeaf =
    if (item is Node23) item.first else item as MeasuredLeaf

private fun itemLast(item: Any): MeasuredLeaf =
    if (item is Node23) item.last else item as MeasuredLeaf

/** The digit an item pulled up from a middle contributes: node children, or the item alone. */
private fun itemDigit(item: Any): Array<Any> = if (item is Node23) item.children else arrayOf(item)

/** Builds a 2-3 node, computing its size and measure strictly and caching its end elements. */
private fun makeNode(policy: MeasurePolicy<Any?, Any?>, children: Array<Any>): Node23 {
    var size = 0
    var measure: Any? = null
    for (position in children.indices) {
        val child = children[position]
        size += itemSize(child)
        val childMeasure = itemMeasure(child)
        measure = if (position == 0) childMeasure else policy.combine(measure, childMeasure)
    }

    return Node23(children, size, measure, itemFirst(children[0]), itemLast(children[children.size - 1]))
}

private fun digitSize(digit: Array<Any>): Int {
    var size = 0
    for (item in digit) {
        size += itemSize(item)
    }
    return size
}

private fun digitMeasure(policy: MeasurePolicy<Any?, Any?>, digit: Array<Any>): Any? {
    var measure = itemMeasure(digit[0])
    for (position in 1 until digit.size) {
        measure = policy.combine(measure, itemMeasure(digit[position]))
    }
    return measure
}

// --- tree helpers --------------------------------------------------------------------------------
//
// A tree is null when empty, a Single, or a DeepNode.

private fun treeSize(tree: Any?): Int = when (tree) {
    null -> 0
    is Single -> itemSize(tree.item)
    is DeepNode -> tree.size
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

/** The tree's total measure, memoized at each deep node. Forces the middles it enters. */
private fun treeMeasure(policy: MeasurePolicy<Any?, Any?>, tree: Any?): Any? = when (tree) {
    null -> policy.empty
    is Single -> itemMeasure(tree.item)
    is DeepNode -> {
        val cached = tree.cachedMeasure
        if (cached !== UNFORCED) {
            cached
        } else {
            var measure = digitMeasure(policy, tree.prefix)
            val middle = tree.middle.force()
            if (middle != null) {
                measure = policy.combine(measure, treeMeasure(policy, middle))
            }
            measure = policy.combine(measure, digitMeasure(policy, tree.suffix))
            // The benign race publishes value-equal results, so a plain volatile write suffices.
            tree.cachedMeasure = measure
            measure
        }
    }
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

private fun deep(prefix: Array<Any>, middle: Susp, middleSize: Int, suffix: Array<Any>): DeepNode =
    DeepNode(digitSize(prefix) + middleSize + digitSize(suffix), prefix, middle, middleSize, suffix)

private fun digitToTree(digit: Array<Any>): Any {
    if (digit.size == 1) {
        return Single(digit[0])
    }

    val half = digit.size / 2
    return deep(digit.copyOfRange(0, half), EMPTY_MIDDLE, 0, digit.copyOfRange(half, digit.size))
}

/** The last item of a non-empty tree, without descending into it. */
private fun lastItemOf(tree: Any): Any = when (tree) {
    is Single -> tree.item
    is DeepNode -> tree.suffix[tree.suffix.size - 1]
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

// --- endpoint operations -------------------------------------------------------------------------

private fun digitPrepend(item: Any, digit: Array<Any>): Array<Any> =
    Array(digit.size + 1) { index -> if (index == 0) item else digit[index - 1] }

private fun digitAppend(digit: Array<Any>, item: Any): Array<Any> =
    Array(digit.size + 1) { index -> if (index < digit.size) digit[index] else item }

private fun consTree(policy: MeasurePolicy<Any?, Any?>, item: Any, tree: Any?): Any = when (tree) {
    null -> Single(item)
    is Single -> deep(arrayOf(item), EMPTY_MIDDLE, 0, arrayOf(tree.item))
    is DeepNode ->
        if (tree.prefix.size < 4) {
            deep(digitPrepend(item, tree.prefix), tree.middle, tree.middleSize, tree.suffix)
        } else {
            val overflow = makeNode(policy, tree.prefix.copyOfRange(1, 4))
            deep(
                arrayOf(item, tree.prefix[0]),
                // The push into the middle is the suspended step Hinze-Paterson amortization needs.
                Susp.deferred(SuspOperation.PushFront(policy, overflow, tree.middle)),
                tree.middleSize + overflow.size,
                tree.suffix,
            )
        }
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

private fun snocTree(policy: MeasurePolicy<Any?, Any?>, tree: Any?, item: Any): Any = when (tree) {
    null -> Single(item)
    is Single -> deep(arrayOf(tree.item), EMPTY_MIDDLE, 0, arrayOf(item))
    is DeepNode ->
        if (tree.suffix.size < 4) {
            deep(tree.prefix, tree.middle, tree.middleSize, digitAppend(tree.suffix, item))
        } else {
            val overflow = makeNode(policy, tree.suffix.copyOfRange(0, 3))
            deep(
                tree.prefix,
                Susp.deferred(SuspOperation.PushBack(policy, tree.middle, overflow)),
                tree.middleSize + overflow.size,
                arrayOf(tree.suffix[3], item),
            )
        }
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

/**
 * Splits off the leftmost item of a non-empty tree, returning it and the remainder. Pulling a
 * replacement digit up from the middle forces one memoized suspension per level entered, which is
 * the O(1)-amortized endpoint pop.
 */
private fun viewLeftTree(policy: MeasurePolicy<Any?, Any?>, tree: Any): Pair<Any, Any?> = when (tree) {
    is Single -> tree.item to null
    is DeepNode -> {
        val head = tree.prefix[0]
        if (tree.prefix.size > 1) {
            head to deep(
                tree.prefix.copyOfRange(1, tree.prefix.size),
                tree.middle,
                tree.middleSize,
                tree.suffix,
            )
        } else {
            val middle = tree.middle.force()
            if (middle == null) {
                head to digitToTree(tree.suffix)
            } else {
                val (node, rest) = viewLeftTree(policy, middle)
                head to deep(
                    itemDigit(node),
                    Susp.ready(rest),
                    tree.middleSize - itemSize(node),
                    tree.suffix,
                )
            }
        }
    }
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

/** Splits off the rightmost item of a non-empty tree; the mirror image of [viewLeftTree]. */
private fun viewRightTree(policy: MeasurePolicy<Any?, Any?>, tree: Any): Pair<Any?, Any> = when (tree) {
    is Single -> null to tree.item
    is DeepNode -> {
        val last = tree.suffix[tree.suffix.size - 1]
        if (tree.suffix.size > 1) {
            deep(
                tree.prefix,
                tree.middle,
                tree.middleSize,
                tree.suffix.copyOfRange(0, tree.suffix.size - 1),
            ) to last
        } else {
            val middle = tree.middle.force()
            if (middle == null) {
                digitToTree(tree.prefix) to last
            } else {
                val (rest, node) = viewRightTree(policy, middle)
                deep(
                    tree.prefix,
                    Susp.ready(rest),
                    tree.middleSize - itemSize(node),
                    itemDigit(node),
                ) to last
            }
        }
    }
    else -> throw IllegalStateException("Unknown finger-tree node.")
}

// --- concatenation -------------------------------------------------------------------------------

/** Groups 2..12 items into 2-3 nodes, the standard Hinze-Paterson regrouping. */
private fun groupNodes(policy: MeasurePolicy<Any?, Any?>, items: List<Any>): List<Any> {
    val nodes = ArrayList<Any>(items.size / 2 + 1)
    var index = 0
    var remaining = items.size
    while (remaining > 0) {
        val take = if (remaining == 2 || remaining == 4) 2 else 3
        nodes.add(makeNode(policy, Array(take) { offset -> items[index + offset] }))
        index += take
        remaining -= take
    }

    return nodes
}

private fun app3(
    policy: MeasurePolicy<Any?, Any?>,
    left: Any?,
    between: List<Any>,
    right: Any?,
): Any? {
    if (left == null) {
        var result = right
        for (position in between.indices.reversed()) {
            result = consTree(policy, between[position], result)
        }
        return result
    }
    if (right == null) {
        var result: Any? = left
        for (item in between) {
            result = snocTree(policy, result, item)
        }
        return result
    }
    if (left is Single) {
        return consTree(policy, left.item, app3(policy, null, between, right))
    }
    if (right is Single) {
        return snocTree(policy, app3(policy, left, between, null), right.item)
    }

    val leftDeep = left as DeepNode
    val rightDeep = right as DeepNode
    val middleItems = ArrayList<Any>(leftDeep.suffix.size + between.size + rightDeep.prefix.size)
    middleItems.addAll(leftDeep.suffix)
    middleItems.addAll(between)
    middleItems.addAll(rightDeep.prefix)
    val nodes = groupNodes(policy, middleItems)
    var nodesSize = 0
    for (node in nodes) {
        nodesSize += itemSize(node)
    }
    return deep(
        leftDeep.prefix,
        // Concatenation recurses into both middles inside a suspension, which is what makes it
        // O(log(min(n, m))) amortized instead of costing its whole recursion up front.
        Susp.deferred(SuspOperation.ConcatMiddles(policy, leftDeep.middle, nodes, rightDeep.middle)),
        leftDeep.middleSize + nodesSize + rightDeep.middleSize,
        rightDeep.suffix,
    )
}

// --- index-directed access and splitting ---------------------------------------------------------

private fun leafInItem(item: Any, index: Int): MeasuredLeaf {
    var current = item
    var remaining = index
    while (current is Node23) {
        for (child in current.children) {
            val childSize = itemSize(child)
            if (remaining < childSize) {
                current = child
                break
            }
            remaining -= childSize
        }
    }
    return current as MeasuredLeaf
}

private fun leafAt(tree: Any?, index: Int): MeasuredLeaf {
    var current = tree
    var remaining = index
    while (true) {
        when (current) {
            is Single -> return leafInItem(current.item, remaining)
            is DeepNode -> {
                var found: Any? = null
                for (item in current.prefix) {
                    val size = itemSize(item)
                    if (remaining < size) {
                        found = item
                        break
                    }
                    remaining -= size
                }
                if (found == null && remaining < current.middleSize) {
                    current = current.middle.force()
                    continue
                }
                if (found == null) {
                    remaining -= current.middleSize
                    for (item in current.suffix) {
                        val size = itemSize(item)
                        if (remaining < size) {
                            found = item
                            break
                        }
                        remaining -= size
                    }
                }
                return leafInItem(
                    checkNotNull(found) { "A validated index must resolve to an element." },
                    remaining,
                )
            }
            else -> throw IllegalStateException("A validated index must land in a non-empty tree.")
        }
    }
}

/** A digit split apart at an element index: (before, item, after, index into the item). */
private class DigitSplit(
    @JvmField val before: List<Any>,
    @JvmField val item: Any,
    @JvmField val after: List<Any>,
    @JvmField val inner: Int,
)

private fun splitDigit(digit: Array<Any>, index: Int): DigitSplit {
    var remaining = index
    for (position in digit.indices) {
        val item = digit[position]
        val size = itemSize(item)
        if (remaining < size) {
            val asList = digit.asList()
            return DigitSplit(
                asList.subList(0, position),
                item,
                asList.subList(position + 1, digit.size),
                remaining,
            )
        }
        remaining -= size
    }

    throw IllegalStateException("A digit split index stays within the digit's size.")
}

/**
 * Splits an item at an element index, collapsing its path into flat item lists.
 *
 * The returned lists deliberately mix levels; every consumer treats items as opaque size and
 * measure carriers, so mixed levels rebuild correctly through [consTree] and [snocTree].
 */
private fun splitItem(item: Any, index: Int): Triple<List<Any>, MeasuredLeaf, List<Any>> {
    val before = ArrayList<Any>()
    var after: MutableList<Any> = ArrayList()
    var current = item
    var remaining = index
    while (current is Node23) {
        var chosen: Any? = null
        val trailing = ArrayList<Any>()
        for (child in current.children) {
            if (chosen == null) {
                val size = itemSize(child)
                if (remaining < size) {
                    chosen = child
                } else {
                    remaining -= size
                    before.add(child)
                }
            } else {
                trailing.add(child)
            }
        }
        trailing.addAll(after)
        after = trailing
        current = checkNotNull(chosen) { "A node split index stays within the node's size." }
    }

    return Triple(before, current as MeasuredLeaf, after)
}

/** Rebuilds a tree whose prefix digit may be empty by pulling a node up from the middle. */
private fun deepLeft(
    policy: MeasurePolicy<Any?, Any?>,
    before: List<Any>,
    middle: Susp,
    middleSize: Int,
    suffix: Array<Any>,
): Any {
    if (before.isNotEmpty()) {
        return deep(before.toTypedArray(), middle, middleSize, suffix)
    }

    val forced = middle.force() ?: return digitToTree(suffix)
    val (node, rest) = viewLeftTree(policy, forced)
    return deep(itemDigit(node), Susp.ready(rest), middleSize - itemSize(node), suffix)
}

/** Rebuilds a tree whose suffix digit may be empty; the mirror image of [deepLeft]. */
private fun deepRight(
    policy: MeasurePolicy<Any?, Any?>,
    prefix: Array<Any>,
    middle: Susp,
    middleSize: Int,
    after: List<Any>,
): Any {
    if (after.isNotEmpty()) {
        return deep(prefix, middle, middleSize, after.toTypedArray())
    }

    val forced = middle.force() ?: return digitToTree(prefix)
    val (rest, node) = viewRightTree(policy, forced)
    return deep(prefix, Susp.ready(rest), middleSize - itemSize(node), itemDigit(node))
}

/** Splits at an element index: (elements before, the element, elements after). */
private fun splitTree(
    policy: MeasurePolicy<Any?, Any?>,
    tree: Any,
    index: Int,
): Triple<Any?, MeasuredLeaf, Any?> = when (tree) {
    is Single -> {
        val (beforeItems, element, afterItems) = splitItem(tree.item, index)
        var left: Any? = null
        for (piece in beforeItems) {
            left = snocTree(policy, left, piece)
        }
        var right: Any? = null
        for (position in afterItems.indices.reversed()) {
            right = consTree(policy, afterItems[position], right)
        }
        Triple(left, element, right)
    }
    is DeepNode -> {
        var remaining = index
        val prefixSize = digitSize(tree.prefix)
        if (remaining < prefixSize) {
            val split = splitDigit(tree.prefix, remaining)
            val (itemBefore, element, itemAfter) = splitItem(split.item, split.inner)
            var left: Any? = null
            for (piece in split.before) {
                left = snocTree(policy, left, piece)
            }
            for (piece in itemBefore) {
                left = snocTree(policy, left, piece)
            }
            var right: Any? = deepLeft(policy, split.after, tree.middle, tree.middleSize, tree.suffix)
            for (position in itemAfter.indices.reversed()) {
                right = consTree(policy, itemAfter[position], right)
            }
            Triple(left, element, right)
        } else {
            remaining -= prefixSize
            if (remaining < tree.middleSize) {
                // The recursion already collapses down to the element level, so its halves are
                // complete trees whose digits may mix levels.
                val middle = checkNotNull(tree.middle.force()) { "A non-zero middle size names a middle." }
                val (middleLeft, element, middleRight) = splitTree(policy, middle, remaining)
                val left = deepRight(
                    policy,
                    tree.prefix,
                    Susp.ready(middleLeft),
                    treeSize(middleLeft),
                    emptyList(),
                )
                val right = deepLeft(
                    policy,
                    emptyList(),
                    Susp.ready(middleRight),
                    treeSize(middleRight),
                    tree.suffix,
                )
                Triple(left, element, right)
            } else {
                remaining -= tree.middleSize
                val split = splitDigit(tree.suffix, remaining)
                val (itemBefore, element, itemAfter) = splitItem(split.item, split.inner)
                var left: Any? = deepRight(policy, tree.prefix, tree.middle, tree.middleSize, split.before)
                for (piece in itemBefore) {
                    left = snocTree(policy, left, piece)
                }
                var right: Any? = null
                for (position in split.after.indices.reversed()) {
                    right = consTree(policy, split.after[position], right)
                }
                for (position in itemAfter.indices.reversed()) {
                    right = consTree(policy, itemAfter[position], right)
                }
                Triple(left, element, right)
            }
        }
    }
    else -> throw IllegalStateException("A split index stays within a non-empty tree.")
}

// --- bulk construction ---------------------------------------------------------------------------

/**
 * Builds a tree bottom-up with ready middles: bulk construction defers nothing.
 *
 * Deferral pays off when later operations may never force the work; a bulk build's caller almost
 * always consumes the result, so suspending every overflow would only add cells to force and then
 * discard. Grouping level by level costs the same O(n) with none of that churn.
 */
private fun buildEager(policy: MeasurePolicy<Any?, Any?>, items: List<Any>): Any? {
    val count = items.size
    if (count == 0) {
        return null
    }
    if (count == 1) {
        return Single(items[0])
    }
    if (count <= 8) {
        val half = count / 2
        return deep(
            Array(half) { position -> items[position] },
            EMPTY_MIDDLE,
            0,
            Array(count - half) { position -> items[half + position] },
        )
    }

    val middle = buildEager(policy, groupNodes(policy, items.subList(3, count - 3)))
    return deep(
        Array(3) { position -> items[position] },
        Susp.ready(middle),
        treeSize(middle),
        Array(3) { position -> items[count - 3 + position] },
    )
}

// --- measure accumulation ------------------------------------------------------------------------

private class MeasureAccumulator(@JvmField var value: Any?)

private fun takeFromItems(
    policy: MeasurePolicy<Any?, Any?>,
    items: Array<Any>,
    remainingStart: Int,
    accumulator: MeasureAccumulator,
): Int {
    var remaining = remainingStart
    for (item in items) {
        if (remaining == 0) {
            return 0
        }
        val size = itemSize(item)
        if (size <= remaining) {
            accumulator.value = policy.combine(accumulator.value, itemMeasure(item))
            remaining -= size
        } else {
            remaining = takeFromItems(policy, (item as Node23).children, remaining, accumulator)
        }
    }
    return remaining
}

private fun takeFromTree(
    policy: MeasurePolicy<Any?, Any?>,
    tree: Any?,
    remainingStart: Int,
    accumulator: MeasureAccumulator,
): Int {
    var remaining = remainingStart
    if (tree == null || remaining == 0) {
        return remaining
    }
    return when (tree) {
        is Single -> takeFromItems(policy, arrayOf(tree.item), remaining, accumulator)
        is DeepNode -> {
            if (tree.size <= remaining) {
                accumulator.value = policy.combine(accumulator.value, treeMeasure(policy, tree))
                return remaining - tree.size
            }
            remaining = takeFromItems(policy, tree.prefix, remaining, accumulator)
            if (remaining > 0) {
                remaining = takeFromTree(policy, tree.middle.force(), remaining, accumulator)
            }
            if (remaining > 0) {
                remaining = takeFromItems(policy, tree.suffix, remaining, accumulator)
            }
            remaining
        }
        else -> throw IllegalStateException("Unknown finger-tree node.")
    }
}

private fun dropFromItems(
    policy: MeasurePolicy<Any?, Any?>,
    items: Array<Any>,
    skipStart: Int,
    accumulator: MeasureAccumulator,
): Int {
    var skip = skipStart
    for (item in items) {
        val size = itemSize(item)
        if (skip >= size) {
            skip -= size
        } else if (skip == 0) {
            accumulator.value = policy.combine(accumulator.value, itemMeasure(item))
        } else {
            skip = dropFromItems(policy, (item as Node23).children, skip, accumulator)
        }
    }
    return skip
}

private fun dropFromTree(
    policy: MeasurePolicy<Any?, Any?>,
    tree: Any?,
    skipStart: Int,
    accumulator: MeasureAccumulator,
): Int {
    var skip = skipStart
    if (tree == null) {
        return skip
    }
    if (skip == 0) {
        accumulator.value = policy.combine(accumulator.value, treeMeasure(policy, tree))
        return 0
    }
    return when (tree) {
        is Single -> dropFromItems(policy, arrayOf(tree.item), skip, accumulator)
        is DeepNode -> {
            if (skip >= tree.size) {
                return skip - tree.size
            }
            skip = dropFromItems(policy, tree.prefix, skip, accumulator)
            skip = if (skip >= tree.middleSize) {
                skip - tree.middleSize
            } else {
                dropFromTree(policy, tree.middle.force(), skip, accumulator)
            }
            dropFromItems(policy, tree.suffix, skip, accumulator)
        }
        else -> throw IllegalStateException("Unknown finger-tree node.")
    }
}

// --- structural audit and sharing diagnostics ----------------------------------------------------

/**
 * Recomputes an item's size, checking every cached size and end-element link along the way.
 * Items are deliberately *not* required to sit at a uniform level: splitting collapses node paths
 * into flat item lists whose rebuilt digits mix elements with nodes, so the audit treats items as
 * opaque carriers exactly as the algorithms do.
 */
private fun auditItem(item: Any): Int? {
    if (item is MeasuredLeaf) {
        return 1
    }
    val node = item as? Node23 ?: return null
    if (node.children.size < 2 || node.children.size > 3) {
        return null
    }

    var total = 0
    for (child in node.children) {
        total += auditItem(child) ?: return null
    }
    if (total != node.size) {
        return null
    }
    if (node.first !== itemFirst(node.children[0])) {
        return null
    }
    if (node.last !== itemLast(node.children[node.children.size - 1])) {
        return null
    }
    return total
}

/** Recomputes a tree's size, forcing suspended middles, and checks every strict cache. */
private fun auditTree(tree: Any?): Int? {
    return when (tree) {
        null -> 0
        is Single -> auditItem(tree.item)
        is DeepNode -> {
            if (tree.prefix.isEmpty() || tree.prefix.size > 4) {
                return null
            }
            if (tree.suffix.isEmpty() || tree.suffix.size > 4) {
                return null
            }

            var total = 0
            for (item in tree.prefix) {
                total += auditItem(item) ?: return null
            }
            val middleSize = auditTree(tree.middle.force()) ?: return null
            if (middleSize != tree.middleSize) {
                return null
            }
            total += middleSize
            for (item in tree.suffix) {
                total += auditItem(item) ?: return null
            }
            if (total != tree.size) null else total
        }
        else -> null
    }
}

/**
 * Collects the identities of every allocation two versions could share: spine nodes, 2-3 nodes,
 * element cells, and suspension cells. The walk forces suspended middles - deferred structure
 * captures shared subtrees inside pending suspensions where no identity walk can see them, so a
 * non-forcing probe would report false negatives. The shared empty-middle singleton is excluded:
 * it appears in every shallow deep node, so counting it would make any two trees "share".
 */
private fun collectIdentities(root: Any, into: MutableSet<Any>) {
    val stack = ArrayDeque<Any>()
    stack.addLast(root)
    while (stack.isNotEmpty()) {
        when (val current = stack.removeLast()) {
            is MeasuredLeaf -> into.add(current)
            is Single -> if (into.add(current)) {
                stack.addLast(current.item)
            }
            is Node23 -> if (into.add(current)) {
                for (child in current.children) {
                    stack.addLast(child)
                }
            }
            is DeepNode -> if (into.add(current)) {
                if (current.middle !== EMPTY_MIDDLE) {
                    into.add(current.middle)
                }
                for (item in current.prefix) {
                    stack.addLast(item)
                }
                for (item in current.suffix) {
                    stack.addLast(item)
                }
                val forced = current.middle.force()
                if (forced != null) {
                    stack.addLast(forced)
                }
            }
        }
    }
}

/** Whether the walk from [root] reaches any identity in [candidates]. Forces like the collector. */
private fun containsSharedIdentity(root: Any, candidates: Set<Any>): Boolean {
    val visited = Collections.newSetFromMap(IdentityHashMap<Any, Boolean>())
    val stack = ArrayDeque<Any>()
    stack.addLast(root)
    while (stack.isNotEmpty()) {
        val current = stack.removeLast()
        if (candidates.contains(current)) {
            return true
        }
        if (!visited.add(current)) {
            continue
        }
        when (current) {
            is Single -> stack.addLast(current.item)
            is Node23 -> for (child in current.children) {
                stack.addLast(child)
            }
            is DeepNode -> {
                if (current.middle !== EMPTY_MIDDLE && candidates.contains(current.middle)) {
                    return true
                }
                for (item in current.prefix) {
                    stack.addLast(item)
                }
                for (item in current.suffix) {
                    stack.addLast(item)
                }
                val forced = current.middle.force()
                if (forced != null) {
                    stack.addLast(forced)
                }
            }
        }
    }
    return false
}

// --- iteration -----------------------------------------------------------------------------------

/** A digit or node-children array mid-iteration. */
private class DigitFrame(@JvmField val items: Array<Any>, @JvmField var index: Int)

/**
 * Streams a finger tree in order with an explicit frame stack, forcing suspended middles as it
 * reaches them. Seeking skips whole items and middles by their strict cached sizes, so reaching
 * the first element costs O(log n) and streaming k elements costs O(k + log n) overall.
 */
private class MeasuredTreeIterator<T>(root: Any?, startIndex: Int) : Iterator<T> {
    private val stack = ArrayDeque<Any>()
    private var remaining: Int = treeSize(root) - startIndex

    init {
        if (remaining > 0) {
            seekTree(root, startIndex)
        }
    }

    private fun seekTree(tree: Any?, startSkip: Int) {
        var skip = startSkip
        var current = tree
        while (true) {
            when (current) {
                null -> return
                is Single -> {
                    if (skip < itemSize(current.item)) {
                        seekItem(current.item, skip)
                    }
                    return
                }
                is DeepNode -> {
                    val prefix = current.prefix
                    for (position in prefix.indices) {
                        val size = itemSize(prefix[position])
                        if (skip < size) {
                            stack.addLast(DigitFrame(current.suffix, 0))
                            stack.addLast(current.middle)
                            stack.addLast(DigitFrame(prefix, position + 1))
                            seekItem(prefix[position], skip)
                            return
                        }
                        skip -= size
                    }
                    if (skip < current.middleSize) {
                        stack.addLast(DigitFrame(current.suffix, 0))
                        current = current.middle.force()
                        continue
                    }
                    skip -= current.middleSize
                    val suffix = current.suffix
                    for (position in suffix.indices) {
                        val size = itemSize(suffix[position])
                        if (skip < size) {
                            stack.addLast(DigitFrame(suffix, position + 1))
                            seekItem(suffix[position], skip)
                            return
                        }
                        skip -= size
                    }
                    return
                }
                else -> throw IllegalStateException("Unknown finger-tree node.")
            }
        }
    }

    private fun seekItem(start: Any, startSkip: Int) {
        var item = start
        var skip = startSkip
        while (item is Node23) {
            val children = item.children
            var position = 0
            while (true) {
                val size = itemSize(children[position])
                if (skip < size) {
                    stack.addLast(DigitFrame(children, position + 1))
                    item = children[position]
                    break
                }
                skip -= size
                position += 1
            }
        }
        stack.addLast(DigitFrame(arrayOf(item), 0))
    }

    override fun hasNext(): Boolean = remaining > 0

    override fun next(): T {
        if (remaining <= 0) {
            throw NoSuchElementException()
        }
        while (true) {
            when (val top = stack.last()) {
                is DigitFrame -> {
                    if (top.index < top.items.size) {
                        val item = top.items[top.index]
                        top.index += 1
                        if (item is Node23) {
                            stack.addLast(DigitFrame(item.children, 0))
                        } else {
                            remaining -= 1
                            @Suppress("UNCHECKED_CAST")
                            return (item as MeasuredLeaf).value as T
                        }
                    } else {
                        stack.removeLast()
                    }
                }
                is Susp -> {
                    stack.removeLast()
                    val forced = top.force()
                    if (forced != null) {
                        stack.addLast(forced)
                    }
                }
                is Single -> {
                    stack.removeLast()
                    stack.addLast(DigitFrame(arrayOf(top.item), 0))
                }
                is DeepNode -> {
                    stack.removeLast()
                    stack.addLast(DigitFrame(top.suffix, 0))
                    stack.addLast(top.middle)
                    stack.addLast(DigitFrame(top.prefix, 0))
                }
                else -> throw IllegalStateException("Unknown iterator frame.")
            }
        }
    }
}

/**
 * Internal immutable measured Hinze-Paterson finger tree shared by the Kotlin facades.
 *
 * See the file header for the representation and the bounds it delivers. The surface level's
 * digits always hold single elements - split rebuilds mix levels only in the middles - so the
 * endpoint views can return elements directly.
 */
internal class PersistentMeasuredTree<T, M> private constructor(
    internal val root: Any?,
    internal val policy: MeasurePolicy<T, M>,
) : Iterable<T> {
    @Suppress("UNCHECKED_CAST")
    private val rawPolicy: MeasurePolicy<Any?, Any?>
        get() = policy as MeasurePolicy<Any?, Any?>

    internal data class Locate<T, M>(
        val index: Int,
        val measureBefore: M,
        val value: T?,
        val found: Boolean,
    )

    internal companion object {
        fun <T, M> empty(policy: MeasurePolicy<T, M>): PersistentMeasuredTree<T, M> =
            PersistentMeasuredTree(null, policy)

        /** Builds a tree from [values] in one eager bottom-up O(n) pass with ready middles. */
        fun <T, M> from(values: Iterable<T>, policy: MeasurePolicy<T, M>): PersistentMeasuredTree<T, M> {
            @Suppress("UNCHECKED_CAST")
            val raw = policy as MeasurePolicy<Any?, Any?>
            val leaves = ArrayList<Any>(if (values is Collection<*>) values.size else 16)
            for (value in values) {
                leaves.add(MeasuredLeaf(value, raw.measure(value)))
            }
            return PersistentMeasuredTree(buildEager(raw, leaves), policy)
        }
    }

    /** Number of elements. O(1); sizes are strict at every node, so no count forces a suspension. */
    val size: Int
        get() = treeSize(root)

    val isEmpty: Boolean
        get() = root == null

    /**
     * The combined measure of every element, in order. O(1) amortized: the root measure is
     * memoized, and the first read of a freshly edited spine may force suspended middles first.
     */
    @Suppress("UNCHECKED_CAST")
    fun measure(): M = treeMeasure(rawPolicy, root) as M

    /** The element at [index], or `null` when the index is out of range. O(log n). */
    @Suppress("UNCHECKED_CAST")
    operator fun get(index: Int): T? {
        if (index < 0 || index >= size) {
            return null
        }

        return leafAt(root, index).value as T
    }

    /** The first element, or `null` when empty. O(1) worst-case: a digit read, no policy call. */
    @Suppress("UNCHECKED_CAST")
    fun front(): T? = when (val tree = root) {
        null -> null
        is Single -> itemFirst(tree.item).value as T
        is DeepNode -> itemFirst(tree.prefix[0]).value as T
        else -> throw IllegalStateException("Unknown finger-tree node.")
    }

    /** The last element, or `null` when empty. O(1) worst-case: a digit read, no policy call. */
    @Suppress("UNCHECKED_CAST")
    fun back(): T? = when (val tree = root) {
        null -> null
        is Single -> itemLast(tree.item).value as T
        is DeepNode -> itemLast(tree.suffix[tree.suffix.size - 1]).value as T
        else -> throw IllegalStateException("Unknown finger-tree node.")
    }

    /**
     * A tree with [value] at the front. O(1) amortized, O(log n) worst-case: a full prefix digit
     * pushes its overflow node into the middle inside a memoized suspension, so the deferred
     * cascade is paid once no matter how many versions later force it. The element's measure is
     * computed eagerly here.
     */
    fun prepend(value: T): PersistentMeasuredTree<T, M> {
        val raw = rawPolicy
        return PersistentMeasuredTree(consTree(raw, MeasuredLeaf(value, raw.measure(value)), root), policy)
    }

    /** A tree with [value] at the back; the mirror image of [prepend]. */
    fun append(value: T): PersistentMeasuredTree<T, M> {
        val raw = rawPolicy
        return PersistentMeasuredTree(snocTree(raw, root, MeasuredLeaf(value, raw.measure(value))), policy)
    }

    /**
     * The first element together with the tree that remains, or `null` when empty. O(1)
     * amortized: pulling a replacement digit up from the middle forces one memoized suspension.
     */
    @Suppress("UNCHECKED_CAST")
    fun viewLeft(): Pair<T, PersistentMeasuredTree<T, M>>? {
        val tree = root ?: return null
        val (item, rest) = viewLeftTree(rawPolicy, tree)
        // The surface level's items are always single elements.
        return (item as MeasuredLeaf).value as T to PersistentMeasuredTree(rest, policy)
    }

    /** The last element together with the tree that remains; the mirror image of [viewLeft]. */
    @Suppress("UNCHECKED_CAST")
    fun viewRight(): Pair<T, PersistentMeasuredTree<T, M>>? {
        val tree = root ?: return null
        val (rest, item) = viewRightTree(rawPolicy, tree)
        return (item as MeasuredLeaf).value as T to PersistentMeasuredTree(rest, policy)
    }

    /**
     * This tree's elements followed by [other]'s. O(log(min(n, m))) amortized: the recursion into
     * the two middles is suspended, and the digits between them are regrouped into 2-3 nodes.
     */
    fun concat(other: PersistentMeasuredTree<T, M>): PersistentMeasuredTree<T, M> {
        require(policy === other.policy || policy == other.policy) {
            "Cannot concatenate trees with different measure policies."
        }
        return when {
            isEmpty -> other
            other.isEmpty -> this
            else -> PersistentMeasuredTree(app3(rawPolicy, root, emptyList(), other.root), policy)
        }
    }

    /** The elements before [index] and those from it on, or `null` outside `0..size`. O(log n). */
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

        val raw = rawPolicy
        val (left, element, right) = splitTree(raw, root!!, index)
        return PersistentMeasuredTree(left, policy) to
            PersistentMeasuredTree(consTree(raw, element, right), policy)
    }

    /** A tree with [value] inserted at [index], or `null` outside `0..size`. O(log n). */
    fun insertAt(index: Int, value: T): PersistentMeasuredTree<T, M>? {
        if (index < 0 || index > size) {
            return null
        }
        if (index == 0) {
            return prepend(value)
        }
        if (index == size) {
            return append(value)
        }

        val raw = rawPolicy
        val leaf = MeasuredLeaf(value, raw.measure(value))
        val (left, element, right) = splitTree(raw, root!!, index)
        return PersistentMeasuredTree(app3(raw, left, listOf(leaf, element), right), policy)
    }

    /** A tree with the element at [index] replaced, or `null` when out of range. O(log n). */
    fun setItem(index: Int, value: T): PersistentMeasuredTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        val raw = rawPolicy
        val leaf = MeasuredLeaf(value, raw.measure(value))
        val (left, _, right) = splitTree(raw, root!!, index)
        return PersistentMeasuredTree(app3(raw, left, listOf<Any>(leaf), right), policy)
    }

    /** A tree without the element at [index], or `null` when out of range. O(log n). */
    fun removeAt(index: Int): PersistentMeasuredTree<T, M>? {
        if (index < 0 || index >= size) {
            return null
        }

        val raw = rawPolicy
        val (left, _, right) = splitTree(raw, root!!, index)
        return PersistentMeasuredTree(app3(raw, left, emptyList(), right), policy)
    }

    fun prefixMeasure(count: Int): M? {
        if (count < 0 || count > size) {
            return null
        }

        return measurePrefix(count)
    }

    /**
     * Returns the ordered measure of the first [count] elements for a validated boundary.
     * O(log n): the prefix is summed from cached node measures, not element by element.
     */
    @Suppress("UNCHECKED_CAST")
    fun measurePrefix(count: Int): M {
        require(count >= 0 && count <= size) { "Prefix boundary must be in 0..size." }
        if (count == 0) {
            return policy.empty
        }
        if (count == size) {
            return measure()
        }

        val raw = rawPolicy
        val accumulator = MeasureAccumulator(raw.empty)
        val leftover = takeFromTree(raw, root, count, accumulator)
        check(leftover == 0) { "A validated prefix boundary must be consumed exactly." }
        return accumulator.value as M
    }

    /** Returns the ordered measure of the elements at and after [startIndex]. O(log n). */
    @Suppress("UNCHECKED_CAST")
    fun measureSuffix(startIndex: Int): M {
        require(startIndex >= 0 && startIndex <= size) { "Suffix boundary must be in 0..size." }
        if (startIndex == size) {
            return policy.empty
        }
        if (startIndex == 0) {
            return measure()
        }

        val raw = rawPolicy
        val accumulator = MeasureAccumulator(raw.empty)
        val leftover = dropFromTree(raw, root, startIndex, accumulator)
        check(leftover == 0) { "A validated suffix boundary must be consumed exactly." }
        return accumulator.value as M
    }

    /**
     * Finds the first position whose inclusive prefix measure satisfies [predicate]. Because
     * every node caches its measure, the search descends without visiting the elements it skips:
     * O(log n) with O(log n) predicate evaluations. The predicate is expected to be monotone; a
     * non-monotone predicate yields an unspecified but valid position. A miss reports the end
     * position with `found` false and the whole-tree measure as `measureBefore`.
     */
    @Suppress("UNCHECKED_CAST")
    fun locate(predicate: (M) -> Boolean): Locate<T, M> {
        val raw = rawPolicy
        var before: Any? = raw.empty
        var index = 0

        fun test(measure: Any?): Boolean = predicate(measure as M)

        fun descendItem(start: Any): MeasuredLeaf {
            var item = start
            while (item is Node23) {
                val children = item.children
                var chosen: Any? = null
                for (position in children.indices) {
                    val child = children[position]
                    val withChild = raw.combine(before, itemMeasure(child))
                    // The last child is the fallback that keeps a non-monotone predicate's
                    // unspecified result terminating.
                    if (test(withChild) || position + 1 == children.size) {
                        chosen = child
                        break
                    }
                    before = withChild
                    index += itemSize(child)
                }
                item = chosen!!
            }
            return item as MeasuredLeaf
        }

        fun scanItems(items: Array<Any>): MeasuredLeaf? {
            for (item in items) {
                val withItem = raw.combine(before, itemMeasure(item))
                if (test(withItem)) {
                    return descendItem(item)
                }
                before = withItem
                index += itemSize(item)
            }
            return null
        }

        fun scanTree(tree: Any?): MeasuredLeaf? = when (tree) {
            null -> null
            is Single -> scanItems(arrayOf(tree.item))
            is DeepNode -> {
                val withTree = raw.combine(before, treeMeasure(raw, tree))
                if (!test(withTree)) {
                    before = withTree
                    index += tree.size
                    null
                } else {
                    scanItems(tree.prefix)
                        ?: scanTree(tree.middle.force())
                        ?: scanItems(tree.suffix)
                }
            }
            else -> throw IllegalStateException("Unknown finger-tree node.")
        }

        val found = scanTree(root)
        return if (found == null) {
            Locate(index, before as M, null, false)
        } else {
            Locate(index, before as M, found.value as T, true)
        }
    }

    /**
     * Returns the number of leading elements matching [isInPrefix]. Requires the sequence to be
     * partitioned: every matching element precedes every non-matching one. Each item caches its
     * last descendant element, so one guided descent decides whole digits and middles from their
     * cached extremes: O(log n) with O(log n) predicate evaluations.
     */
    fun prefixLength(isInPrefix: (T) -> Boolean): Int {
        var count = 0

        @Suppress("UNCHECKED_CAST")
        fun reaches(item: Any): Boolean = isInPrefix(itemLast(item).value as T)

        fun descendItem(start: Any) {
            var item = start
            while (item is Node23) {
                var next: Any? = null
                for (child in item.children) {
                    if (reaches(child)) {
                        count += itemSize(child)
                    } else {
                        next = child
                        break
                    }
                }
                item = next ?: return
            }
        }

        fun scanItems(items: Array<Any>): Boolean {
            for (item in items) {
                if (reaches(item)) {
                    count += itemSize(item)
                } else {
                    descendItem(item)
                    return true
                }
            }
            return false
        }

        fun scanTree(tree: Any?): Boolean = when (tree) {
            null -> false
            is Single -> scanItems(arrayOf(tree.item))
            is DeepNode -> {
                if (scanItems(tree.prefix)) {
                    true
                } else {
                    var stopped = false
                    if (tree.middleSize > 0) {
                        val forced = tree.middle.force()!!
                        if (reaches(lastItemOf(forced))) {
                            count += tree.middleSize
                        } else {
                            scanTree(forced)
                            stopped = true
                        }
                    }
                    if (stopped) true else scanItems(tree.suffix)
                }
            }
            else -> throw IllegalStateException("Unknown finger-tree node.")
        }

        scanTree(root)
        return count
    }

    fun splitByMeasure(predicate: (M) -> Boolean): Pair<PersistentMeasuredTree<T, M>, PersistentMeasuredTree<T, M>> {
        val located = locate(predicate)
        return splitAt(located.index)!!
    }

    fun toList(): List<T> {
        val result = ArrayList<T>(size)
        for (value in this) {
            result.add(value)
        }
        return result
    }

    /**
     * Whether the two trees have any allocation in common by identity. A representation probe for
     * structural-sharing tests, not an equality test. The walk forces suspended middles: deferred
     * structure captures shared subtrees inside pending suspensions where no identity walk can
     * see them, so a non-forcing probe would report false negatives. Forcing replays the deferred
     * work once, memoized and shared by every version, so probing a warmed spine costs nothing.
     */
    fun sharesStructureWith(other: PersistentMeasuredTree<T, M>): Boolean {
        if (root === other.root) {
            return true
        }
        if (root == null || other.root == null) {
            return false
        }

        val identities = Collections.newSetFromMap(IdentityHashMap<Any, Boolean>())
        collectIdentities(root, identities)
        return containsSharedIdentity(other.root, identities)
    }

    /**
     * Audits the whole structure: digit widths 1..4, node arities 2..3, every strict cached size,
     * the cached end-element links, and each deep node's strict middle size against its forced
     * middle. Deliberately does not require items to sit at a uniform level - split rebuilds mix
     * levels by design - and does not recompute measures, whose types need not define equality.
     */
    fun isBalanced(): Boolean = auditTree(root) != null

    override fun iterator(): Iterator<T> = MeasuredTreeIterator(root, 0)

    /**
     * Returns an in-order iterator positioned at [startIndex]. The seek skips whole items and
     * middles by strict cached size, so reaching the first element costs O(log n) and streaming
     * k elements costs O(k + log n) overall.
     */
    fun iteratorFrom(startIndex: Int): Iterator<T> = MeasuredTreeIterator(root, startIndex)
}
