/*
 * Persistent measured sequence with algebraic lazy range updates.
 *
 * Applying an update to a range stores a pending tag at the root of each fully covered subtree
 * instead of touching every element, so the cost follows the tree depth rather than the range
 * length while cached measures stay correct. Pushing a tag down is a path copy, so earlier versions
 * keep observing the values they always had.
 */
package durable7.fingertree

import java.util.ArrayDeque
import java.util.Collections
import java.util.IdentityHashMap

/**
 * Ordered measurement together with a monoid of lazy range-update tags.
 *
 * [compose] is directional: `compose(newer, older)` represents applying [older] first and [newer]
 * second. Tags must form a monoid, their actions on elements and cached measures must agree on
 * singletons, and the measure action must distribute over ordered [combine]. Every tag accepted by
 * [isIdentity] must have complete identity behavior even when it is value-distinct from
 * [identityTag].
 */
public interface RangeUpdateAlgebra<T, M, Tag> : MeasurePolicy<T, M> {
    /** The tag-monoid identity; it is never used as an absence sentinel. */
    public val identityTag: Tag

    /** Returns whether [tag] has complete algebraic identity behavior. */
    public fun isIdentity(tag: Tag): Boolean

    /** Returns the tag equivalent to applying [older] and then [newer]. */
    public fun compose(newer: Tag, older: Tag): Tag

    /** Applies [tag] to one current logical [element]. */
    public fun applyElement(tag: Tag, element: T): T

    /** Applies [tag] to the measure of [count] current logical elements. */
    public fun applyMeasure(tag: Tag, measure: M, count: Int): M
}

/** The two immutable results of splitting a [RangeUpdateSequence] at a boundary. */
public data class RangeUpdateSplit<T, M, Tag>(
    public val left: RangeUpdateSequence<T, M, Tag>,
    public val right: RangeUpdateSequence<T, M, Tag>,
)

/** Independently recomputed implicit-AVL metadata. */
public data class RangeUpdateValidationStatistics(
    public val count: Int,
    public val height: Int,
    public val nodeCount: Int,
    public val pendingTagCount: Int,
)

/**
 * An immutable indexed sequence with O(log n) algebraic range updates and aggregate queries.
 *
 * The representation is a path-copied implicit-key AVL tree. Every node caches height, count, and
 * its ordered logical measure. A pending tag has already transformed that node's value and cached
 * measure but not its child roots. Structural descent pushes the tag immutably before rotations;
 * reads carry inherited tags without publishing replacement nodes. A nonidentity whole-sequence
 * update therefore replaces one root in O(1), while proper range operations visit or copy O(log n)
 * boundary nodes.
 *
 * Instances are safe for concurrent reads when caller-owned elements, measures, tags, and policy
 * state support the same access. Throwing policy callbacks cannot mutate any retained version.
 */
public class RangeUpdateSequence<T, M, Tag> private constructor(
    private val root: RangeUpdateNode<T, M, Tag>?,
    public val algebra: RangeUpdateAlgebra<T, M, Tag>,
    private val emptyMeasure: M,
) : Iterable<T> {
    public companion object {
        /** Creates an empty sequence and captures this policy lineage's empty measure once. */
        public fun <T, M, Tag> empty(
            algebra: RangeUpdateAlgebra<T, M, Tag>,
        ): RangeUpdateSequence<T, M, Tag> =
            RangeUpdateSequence(null, algebra, algebra.empty)

        /**
         * Eagerly captures [values] once, then constructs a deterministically balanced sequence in
         * O(n). Source enumeration completes before element-measure callbacks begin.
         */
        public fun <T, M, Tag> from(
            values: Iterable<T>,
            algebra: RangeUpdateAlgebra<T, M, Tag>,
        ): RangeUpdateSequence<T, M, Tag> {
            if (values is RangeUpdateSequence<*, *, *> && values.algebra === algebra) {
                @Suppress("UNCHECKED_CAST")
                return values as RangeUpdateSequence<T, M, Tag>
            }

            val owned = values.toList()
            val capturedEmpty = algebra.empty
            return RangeUpdateSequence(
                rangeBuildBalanced(owned, 0, owned.size, algebra),
                algebra,
                capturedEmpty,
            )
        }
    }

    /** The cached element count. */
    public val size: Int
        get() = rangeCount(root)

    /** Whether this sequence contains no elements. */
    public val isEmpty: Boolean
        get() = root == null

    /** Creates an immutable logical cursor before the first element. */
    public fun cursor(): RangeUpdateSequenceCursor<T, M, Tag> =
        RangeUpdateSequenceCursor.create(this, 0)

    /** Creates an immutable logical cursor at a boundary in `0..size`. */
    public fun cursorAt(position: Int): RangeUpdateSequenceCursor<T, M, Tag>? =
        if (position < 0 || position > size) null else RangeUpdateSequenceCursor.create(this, position)

    /** The cached ordered logical measure. */
    public val measure: M
        get() = if (root == null) emptyMeasure else root.measure

    /** Returns the logical element at [index], or `null` outside `0 until size`. */
    public operator fun get(index: Int): T? {
        if (index < 0 || index >= size) {
            return null
        }
        return rangeGetElement(root!!, index, null, algebra)
    }

    /** Returns a sequence with [value] inserted at the beginning. */
    public fun prepend(value: T): RangeUpdateSequence<T, M, Tag> =
        insertAt(0, value) ?: error("A zero boundary must be valid.")

    /** Returns a sequence with [value] inserted at the end. */
    public fun append(value: T): RangeUpdateSequence<T, M, Tag> =
        insertAt(size, value) ?: error("The end boundary must be valid.")

    /**
     * Inserts [value] at [index], or returns `null` outside `0..size`.
     *
     * @throws ArithmeticException when the resulting count exceeds [Int.MAX_VALUE].
     */
    public fun insertAt(index: Int, value: T): RangeUpdateSequence<T, M, Tag>? {
        if (index < 0 || index > size) {
            return null
        }
        Math.addExact(size, 1)
        return wrap(rangeInsert(root, index, value, algebra))
    }

    /** Replaces one logical element without applying older range tags to [value]. */
    public fun setItem(index: Int, value: T): RangeUpdateSequence<T, M, Tag>? {
        if (index < 0 || index >= size) {
            return null
        }
        return wrap(rangeSet(root!!, index, value, algebra))
    }

    /** Removes one logical element, or returns `null` outside `0 until size`. */
    public fun removeAt(index: Int): RangeUpdateSequence<T, M, Tag>? {
        if (index < 0 || index >= size) {
            return null
        }
        return wrap(rangeRemove(root!!, index, algebra))
    }

    /**
     * Concatenates [other] after this sequence. Compatible operands must retain the same algebra
     * object or algebra values that compare equal.
     *
     * @throws IllegalArgumentException when the policies differ.
     * @throws ArithmeticException when the combined count exceeds [Int.MAX_VALUE].
     */
    public fun concat(other: RangeUpdateSequence<T, M, Tag>): RangeUpdateSequence<T, M, Tag> {
        Math.addExact(size, other.size)
        require(algebra === other.algebra || algebra == other.algebra) {
            "Cannot concatenate range-update sequences with different algebras."
        }
        if (isEmpty) {
            return other
        }
        if (other.isEmpty) {
            return this
        }

        val (pivot, remainder) = rangeExtractMinimum(other.root!!, algebra)
        return wrap(rangeJoin(root, pivot, remainder, algebra))
    }

    /** Splits at [index], or returns `null` outside `0..size`. */
    public fun splitAt(index: Int): RangeUpdateSplit<T, M, Tag>? {
        if (index < 0 || index > size) {
            return null
        }
        if (index == 0) {
            return RangeUpdateSplit(emptyInLineage(), this)
        }
        if (index == size) {
            return RangeUpdateSplit(this, emptyInLineage())
        }

        val (left, right) = rangeSplit(root, index, algebra)
        return RangeUpdateSplit(wrap(left), wrap(right))
    }

    /** Returns a contiguous subsequence, or `null` when the range is invalid. */
    public fun getRange(start: Int, count: Int): RangeUpdateSequence<T, M, Tag>? {
        if (!isValidRange(start, count, size)) {
            return null
        }
        if (count == 0) {
            return emptyInLineage()
        }
        if (count == size) {
            return this
        }

        val (_, tail) = rangeSplit(root, start, algebra)
        val (middle, _) = rangeSplit(tail, count, algebra)
        return wrap(middle)
    }

    /**
     * Applies [tag] after every update already represented by this sequence. Empty ranges bypass
     * identity recognition; empty ranges and recognized identities return this instance.
     */
    public fun applyRange(start: Int, count: Int, tag: Tag): RangeUpdateSequence<T, M, Tag>? {
        if (!isValidRange(start, count, size)) {
            return null
        }
        if (count == 0) {
            return this
        }
        if (algebra.isIdentity(tag)) {
            return this
        }
        if (count == size) {
            return wrap(rangeApplySubtree(root!!, tag, algebra))
        }

        val (left, tail) = rangeSplit(root, start, algebra)
        val (middle, right) = rangeSplit(tail, count, algebra)
        val changed = rangeApplySubtree(middle!!, tag, algebra)
        return wrap(rangeConcatNodes(rangeConcatNodes(left, changed, algebra), right, algebra))
    }

    /** Returns the ordered measure of a contiguous range, or `null` for invalid bounds. */
    public fun measureRange(start: Int, count: Int): M? {
        if (!isValidRange(start, count, size)) {
            return null
        }
        if (count == 0) {
            return emptyMeasure
        }
        if (count == size) {
            return root!!.measure
        }
        return rangeMeasure(root!!, start, count, null, algebra)
    }

    /** Materializes logical elements in sequence order. */
    public fun toList(): List<T> {
        val result = ArrayList<T>(size)
        for (value in this) {
            result.add(value)
        }
        return result
    }

    /** Independently validates AVL metadata, pending-tag canonicality, and cached measures. */
    public fun validateStructure(): RangeUpdateValidationStatistics? =
        if (root == null) {
            RangeUpdateValidationStatistics(0, 0, 0, 0)
        } else {
            rangeValidate(root, algebra, emptyMeasure)?.statistics
        }

    /** Returns whether both facades retain the exact same root object. */
    public fun sharesRootWith(other: RangeUpdateSequence<T, M, Tag>): Boolean = root === other.root

    /** Returns whether the two roots share any physical node. Intended for deterministic tests. */
    public fun sharesStructureWith(other: RangeUpdateSequence<T, M, Tag>): Boolean {
        if (root === other.root) {
            return true
        }
        if (root == null || other.root == null) {
            return false
        }
        val identities = Collections.newSetFromMap(IdentityHashMap<RangeUpdateNode<T, M, Tag>, Boolean>())
        rangeCollectNodes(root, identities)
        return rangeContainsSharedNode(other.root, identities)
    }

    override fun iterator(): Iterator<T> = RangeUpdateIterator(root, algebra)

    private fun wrap(node: RangeUpdateNode<T, M, Tag>?): RangeUpdateSequence<T, M, Tag> =
        RangeUpdateSequence(node, algebra, emptyMeasure)

    private fun emptyInLineage(): RangeUpdateSequence<T, M, Tag> = wrap(null)
}

private class RangeTag<Tag>(val value: Tag)

private class RangeUpdateNode<T, M, Tag>(
    val value: T,
    val left: RangeUpdateNode<T, M, Tag>?,
    val right: RangeUpdateNode<T, M, Tag>?,
    val height: Int,
    val count: Int,
    val measure: M,
    val pending: RangeTag<Tag>?,
)

private data class RangeRemovedMinimum<T, M, Tag>(
    val minimum: T,
    val remainder: RangeUpdateNode<T, M, Tag>?,
)

private data class RangeValidation<M>(
    val measure: M,
    val statistics: RangeUpdateValidationStatistics,
)

private fun <T, M, Tag> rangeHeight(node: RangeUpdateNode<T, M, Tag>?): Int = node?.height ?: 0

private fun <T, M, Tag> rangeCount(node: RangeUpdateNode<T, M, Tag>?): Int = node?.count ?: 0

private fun <T, M, Tag> rangeMakeNode(
    value: T,
    left: RangeUpdateNode<T, M, Tag>?,
    right: RangeUpdateNode<T, M, Tag>?,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    // Structural impossibility precedes every user-policy callback.
    val count = Math.addExact(Math.addExact(rangeCount(left), 1), rangeCount(right))
    val height = Math.addExact(maxOf(rangeHeight(left), rangeHeight(right)), 1)
    var measure = algebra.measure(value)
    if (left != null) {
        measure = algebra.combine(left.measure, measure)
    }
    if (right != null) {
        measure = algebra.combine(measure, right.measure)
    }
    return RangeUpdateNode(value, left, right, height, count, measure, null)
}

private fun <T, M, Tag> rangeBuildBalanced(
    values: List<T>,
    start: Int,
    count: Int,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag>? {
    if (count == 0) {
        return null
    }
    val leftCount = count / 2
    val middle = start + leftCount
    return rangeMakeNode(
        values[middle],
        rangeBuildBalanced(values, start, leftCount, algebra),
        rangeBuildBalanced(values, middle + 1, count - leftCount - 1, algebra),
        algebra,
    )
}

private fun <T, M, Tag> rangeApplySubtree(
    node: RangeUpdateNode<T, M, Tag>,
    newer: Tag,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    val value = algebra.applyElement(newer, node.value)
    val measure = algebra.applyMeasure(newer, node.measure, node.count)
    val pending = if (node.pending == null) {
        RangeTag(newer)
    } else {
        val composed = algebra.compose(newer, node.pending.value)
        if (algebra.isIdentity(composed)) null else RangeTag(composed)
    }
    return RangeUpdateNode(value, node.left, node.right, node.height, node.count, measure, pending)
}

private fun <T, M, Tag> rangePush(
    node: RangeUpdateNode<T, M, Tag>,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    val pending = node.pending ?: return node
    return RangeUpdateNode(
        node.value,
        node.left?.let { rangeApplySubtree(it, pending.value, algebra) },
        node.right?.let { rangeApplySubtree(it, pending.value, algebra) },
        node.height,
        node.count,
        node.measure,
        null,
    )
}

private fun <T, M, Tag> rangeChildTag(
    node: RangeUpdateNode<T, M, Tag>,
    inherited: RangeTag<Tag>?,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeTag<Tag>? {
    val pending = node.pending
    if (pending == null) {
        return inherited
    }
    if (inherited == null) {
        return pending
    }
    val composed = algebra.compose(inherited.value, pending.value)
    return if (algebra.isIdentity(composed)) null else RangeTag(composed)
}

private fun <T, M, Tag> rangeGetElement(
    startingNode: RangeUpdateNode<T, M, Tag>,
    startingIndex: Int,
    startingInherited: RangeTag<Tag>?,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): T {
    var node = startingNode
    var index = startingIndex
    var inherited = startingInherited
    while (true) {
        val leftCount = rangeCount(node.left)
        if (index == leftCount) {
            return if (inherited == null) node.value else algebra.applyElement(inherited.value, node.value)
        }
        inherited = rangeChildTag(node, inherited, algebra)
        if (index < leftCount) {
            node = node.left!!
        } else {
            index -= leftCount + 1
            node = node.right!!
        }
    }
}

private fun <T, M, Tag> rangeInsert(
    original: RangeUpdateNode<T, M, Tag>?,
    index: Int,
    value: T,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    if (original == null) {
        return rangeMakeNode(value, null, null, algebra)
    }
    val node = rangePush(original, algebra)
    val leftCount = rangeCount(node.left)
    return if (index <= leftCount) {
        rangeBalance(rangeMakeNode(node.value, rangeInsert(node.left, index, value, algebra), node.right, algebra), algebra)
    } else {
        rangeBalance(
            rangeMakeNode(
                node.value,
                node.left,
                rangeInsert(node.right, index - leftCount - 1, value, algebra),
                algebra,
            ),
            algebra,
        )
    }
}

private fun <T, M, Tag> rangeSet(
    original: RangeUpdateNode<T, M, Tag>,
    index: Int,
    value: T,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    val node = rangePush(original, algebra)
    val leftCount = rangeCount(node.left)
    return when {
        index < leftCount -> rangeMakeNode(node.value, rangeSet(node.left!!, index, value, algebra), node.right, algebra)
        index > leftCount -> rangeMakeNode(
            node.value,
            node.left,
            rangeSet(node.right!!, index - leftCount - 1, value, algebra),
            algebra,
        )
        else -> rangeMakeNode(value, node.left, node.right, algebra)
    }
}

private fun <T, M, Tag> rangeRemove(
    original: RangeUpdateNode<T, M, Tag>,
    index: Int,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag>? {
    val node = rangePush(original, algebra)
    val leftCount = rangeCount(node.left)
    return when {
        index < leftCount -> rangeBalance(
            rangeMakeNode(node.value, rangeRemove(node.left!!, index, algebra), node.right, algebra),
            algebra,
        )
        index > leftCount -> rangeBalance(
            rangeMakeNode(
                node.value,
                node.left,
                rangeRemove(node.right!!, index - leftCount - 1, algebra),
                algebra,
            ),
            algebra,
        )
        node.left == null -> node.right
        node.right == null -> node.left
        else -> {
            val removed = rangeExtractMinimum(node.right, algebra)
            rangeBalance(rangeMakeNode(removed.minimum, node.left, removed.remainder, algebra), algebra)
        }
    }
}

private fun <T, M, Tag> rangeExtractMinimum(
    original: RangeUpdateNode<T, M, Tag>,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeRemovedMinimum<T, M, Tag> {
    val node = rangePush(original, algebra)
    if (node.left == null) {
        return RangeRemovedMinimum(node.value, node.right)
    }
    val removed = rangeExtractMinimum(node.left, algebra)
    val remainder = rangeBalance(rangeMakeNode(node.value, removed.remainder, node.right, algebra), algebra)
    return RangeRemovedMinimum(removed.minimum, remainder)
}

private fun <T, M, Tag> rangeSplit(
    root: RangeUpdateNode<T, M, Tag>?,
    index: Int,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): Pair<RangeUpdateNode<T, M, Tag>?, RangeUpdateNode<T, M, Tag>?> {
    if (root == null) {
        return null to null
    }
    if (index == 0) {
        return null to root
    }
    if (index == root.count) {
        return root to null
    }

    val node = rangePush(root, algebra)
    val leftCount = rangeCount(node.left)
    return if (index <= leftCount) {
        val (left, middle) = rangeSplit(node.left, index, algebra)
        left to rangeJoin(middle, node.value, node.right, algebra)
    } else {
        val (middle, right) = rangeSplit(node.right, index - leftCount - 1, algebra)
        rangeJoin(node.left, node.value, middle, algebra) to right
    }
}

private fun <T, M, Tag> rangeJoin(
    left: RangeUpdateNode<T, M, Tag>?,
    pivot: T,
    right: RangeUpdateNode<T, M, Tag>?,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    val leftHeight = rangeHeight(left)
    val rightHeight = rangeHeight(right)
    if (leftHeight <= rightHeight + 1 && rightHeight <= leftHeight + 1) {
        return rangeMakeNode(pivot, left, right, algebra)
    }
    if (leftHeight > rightHeight) {
        val node = rangePush(left!!, algebra)
        val boundary = rangeJoin(node.right, pivot, right, algebra)
        return rangeBalance(rangeMakeNode(node.value, node.left, boundary, algebra), algebra)
    }
    val node = rangePush(right!!, algebra)
    val leading = rangeJoin(left, pivot, node.left, algebra)
    return rangeBalance(rangeMakeNode(node.value, leading, node.right, algebra), algebra)
}

private fun <T, M, Tag> rangeConcatNodes(
    left: RangeUpdateNode<T, M, Tag>?,
    right: RangeUpdateNode<T, M, Tag>?,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag>? {
    if (left == null) {
        return right
    }
    if (right == null) {
        return left
    }
    val removed = rangeExtractMinimum(right, algebra)
    return rangeJoin(left, removed.minimum, removed.remainder, algebra)
}

private fun <T, M, Tag> rangeBalance(
    original: RangeUpdateNode<T, M, Tag>,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    var node = original
    val factor = rangeHeight(node.left) - rangeHeight(node.right)
    if (factor > 1) {
        val left = node.left!!
        if (rangeHeight(left.left) < rangeHeight(left.right)) {
            node = rangePush(node, algebra)
            node = rangeMakeNode(node.value, rangeRotateLeft(node.left!!, algebra), node.right, algebra)
        }
        return rangeRotateRight(node, algebra)
    }
    if (factor < -1) {
        val right = node.right!!
        if (rangeHeight(right.right) < rangeHeight(right.left)) {
            node = rangePush(node, algebra)
            node = rangeMakeNode(node.value, node.left, rangeRotateRight(node.right!!, algebra), algebra)
        }
        return rangeRotateLeft(node, algebra)
    }
    return node
}

private fun <T, M, Tag> rangeRotateLeft(
    original: RangeUpdateNode<T, M, Tag>,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    val node = rangePush(original, algebra)
    val pivot = rangePush(node.right!!, algebra)
    val lower = rangeMakeNode(node.value, node.left, pivot.left, algebra)
    return rangeMakeNode(pivot.value, lower, pivot.right, algebra)
}

private fun <T, M, Tag> rangeRotateRight(
    original: RangeUpdateNode<T, M, Tag>,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): RangeUpdateNode<T, M, Tag> {
    val node = rangePush(original, algebra)
    val pivot = rangePush(node.left!!, algebra)
    val lower = rangeMakeNode(node.value, pivot.right, node.right, algebra)
    return rangeMakeNode(pivot.value, pivot.left, lower, algebra)
}

private fun <T, M, Tag> rangeMeasure(
    node: RangeUpdateNode<T, M, Tag>,
    start: Int,
    count: Int,
    inherited: RangeTag<Tag>?,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
): M {
    if (start == 0 && count == node.count) {
        return if (inherited == null) node.measure else algebra.applyMeasure(inherited.value, node.measure, node.count)
    }

    val end = Math.addExact(start, count)
    val leftCount = rangeCount(node.left)
    var childTagComputed = false
    var cachedChildTag: RangeTag<Tag>? = null
    fun childTag(): RangeTag<Tag>? {
        if (!childTagComputed) {
            cachedChildTag = rangeChildTag(node, inherited, algebra)
            childTagComputed = true
        }
        return cachedChildTag
    }
    var result: RangeMeasureBox<M>? = null
    fun add(part: M) {
        result = RangeMeasureBox(if (result == null) part else algebra.combine(result!!.value, part))
    }

    if (start < leftCount) {
        val childCount = minOf(count, leftCount - start)
        add(rangeMeasure(node.left!!, start, childCount, childTag(), algebra))
    }
    if (start <= leftCount && end > leftCount) {
        var singleton = algebra.measure(node.value)
        if (inherited != null) {
            singleton = algebra.applyMeasure(inherited.value, singleton, 1)
        }
        add(singleton)
    }
    val rightStart = leftCount + 1
    if (end > rightStart) {
        val localStart = maxOf(0, start - rightStart)
        val localEnd = end - rightStart
        add(rangeMeasure(node.right!!, localStart, localEnd - localStart, childTag(), algebra))
    }
    return result?.value ?: error("A nonempty validated range must contribute a measure.")
}

private class RangeMeasureBox<M>(val value: M)

private fun <T, M, Tag> rangeValidate(
    node: RangeUpdateNode<T, M, Tag>,
    algebra: RangeUpdateAlgebra<T, M, Tag>,
    emptyMeasure: M,
): RangeValidation<M>? {
    val emptyValidation = RangeValidation(emptyMeasure, RangeUpdateValidationStatistics(0, 0, 0, 0))
    val left = if (node.left == null) {
        emptyValidation
    } else {
        rangeValidate(node.left, algebra, emptyMeasure) ?: return null
    }
    val right = if (node.right == null) {
        emptyValidation
    } else {
        rangeValidate(node.right, algebra, emptyMeasure) ?: return null
    }
    val expectedCount = left.statistics.count.toLong() + 1L + right.statistics.count.toLong()
    val expectedHeight = maxOf(left.statistics.height, right.statistics.height) + 1
    if (expectedCount > Int.MAX_VALUE || node.count != expectedCount.toInt() ||
        node.height != expectedHeight || kotlin.math.abs(left.statistics.height - right.statistics.height) > 1
    ) {
        return null
    }
    if (node.pending != null && algebra.isIdentity(node.pending.value)) {
        return null
    }

    val leftMeasure = if (node.pending == null || left.statistics.count == 0) {
        left.measure
    } else {
        algebra.applyMeasure(node.pending.value, left.measure, left.statistics.count)
    }
    val rightMeasure = if (node.pending == null || right.statistics.count == 0) {
        right.measure
    } else {
        algebra.applyMeasure(node.pending.value, right.measure, right.statistics.count)
    }
    var expectedMeasure = algebra.measure(node.value)
    if (left.statistics.count != 0) {
        expectedMeasure = algebra.combine(leftMeasure, expectedMeasure)
    }
    if (right.statistics.count != 0) {
        expectedMeasure = algebra.combine(expectedMeasure, rightMeasure)
    }
    if (expectedMeasure != node.measure) {
        return null
    }
    return RangeValidation(
        node.measure,
        RangeUpdateValidationStatistics(
            node.count,
            node.height,
            left.statistics.nodeCount + 1 + right.statistics.nodeCount,
            left.statistics.pendingTagCount + (if (node.pending == null) 0 else 1) + right.statistics.pendingTagCount,
        ),
    )
}

private fun <T, M, Tag> rangeCollectNodes(
    node: RangeUpdateNode<T, M, Tag>,
    destination: MutableSet<RangeUpdateNode<T, M, Tag>>,
) {
    if (!destination.add(node)) {
        return
    }
    node.left?.let { rangeCollectNodes(it, destination) }
    node.right?.let { rangeCollectNodes(it, destination) }
}

private fun <T, M, Tag> rangeContainsSharedNode(
    node: RangeUpdateNode<T, M, Tag>,
    candidates: Set<RangeUpdateNode<T, M, Tag>>,
): Boolean =
    candidates.contains(node) ||
        (node.left?.let { rangeContainsSharedNode(it, candidates) } == true) ||
        (node.right?.let { rangeContainsSharedNode(it, candidates) } == true)

private class RangeUpdateIterator<T, M, Tag>(
    root: RangeUpdateNode<T, M, Tag>?,
    private val algebra: RangeUpdateAlgebra<T, M, Tag>,
) : Iterator<T> {
    private val stack = ArrayDeque<RangeFrame<T, M, Tag>>()
    private var prepared = false
    private var finished = false
    private var nextValue: T? = null

    init {
        if (root != null) {
            stack.addLast(RangeFrame(root, null))
        }
    }

    override fun hasNext(): Boolean {
        prepare()
        return !finished
    }

    override fun next(): T {
        prepare()
        if (finished) {
            throw NoSuchElementException()
        }
        prepared = false
        @Suppress("UNCHECKED_CAST")
        return nextValue as T
    }

    private fun prepare() {
        if (prepared || finished) {
            return
        }
        while (stack.isNotEmpty()) {
            val frame = stack.peekLast()
            when (frame.stage) {
                0 -> {
                    val left = frame.node.left
                    if (left == null) {
                        frame.stage = 1
                    } else {
                        val inherited = frame.childTag(algebra)
                        frame.stage = 1
                        stack.addLast(RangeFrame(left, inherited))
                    }
                }
                1 -> {
                    val value = if (frame.inherited == null) {
                        frame.node.value
                    } else {
                        algebra.applyElement(frame.inherited.value, frame.node.value)
                    }
                    frame.stage = 2
                    nextValue = value
                    prepared = true
                    return
                }
                2 -> {
                    val right = frame.node.right
                    if (right == null) {
                        frame.stage = 3
                    } else {
                        val inherited = frame.childTag(algebra)
                        frame.stage = 3
                        stack.addLast(RangeFrame(right, inherited))
                    }
                }
                else -> stack.removeLast()
            }
        }
        nextValue = null
        finished = true
    }
}

private class RangeFrame<T, M, Tag>(
    val node: RangeUpdateNode<T, M, Tag>,
    val inherited: RangeTag<Tag>?,
) {
    var stage: Int = 0
    private var childComputed: Boolean = false
    private var cachedChildTag: RangeTag<Tag>? = null

    fun childTag(algebra: RangeUpdateAlgebra<T, M, Tag>): RangeTag<Tag>? {
        if (!childComputed) {
            cachedChildTag = rangeChildTag(node, inherited, algebra)
            childComputed = true
        }
        return cachedChildTag
    }
}
