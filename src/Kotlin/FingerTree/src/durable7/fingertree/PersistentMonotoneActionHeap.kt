/*
 * Persistent Brodal-Okasaki heap with lazily composable monotone priority actions.
 *
 * This is the action-tagged sibling of BrodalOkasakiHeap. It keeps the same bootstrapped
 * skew-binomial representation -- a global minimum root sitting above a skew-binomial forest -- and
 * adds one immutable pending action to every tree and every forest spine cell. A tree tag applies
 * to that tree and all of its descendants; a forest tag applies uniformly to every tree in that
 * spine suffix.
 *
 * The tags exist for transformAll, which retags one root in O(1) and thereby applies a monotone
 * action to every priority currently present. Its semantics are deliberately temporal: entries
 * inserted later, and entries arriving through a later meld, are never retroactively transformed.
 * That is why the tags are recursive rather than a single global adjustment, and it is what lets a
 * clamped heap still meld in O(1) with a differently clamped one even though a clamp has no
 * inverse.
 */
package durable7.fingertree

import kotlin.math.max

/**
 * A constant-time-composable monotone action family over heap priorities, together with the exact
 * priority [comparator] the family is monotone for.
 *
 * Implementations supply a semantic identity and an associative composition where
 * `compose(outer, inner)` denotes `outer(inner(priority))`; the newer action is always the outer
 * one. Every action must be monotone for [comparator]: if `p <= q` then
 * `apply(action, p) <= apply(action, q)`. [isIdentity], [compose], and [apply] are part of the
 * heap's advertised bounds and must each run in O(1) over fixed-size actions, and every result must
 * stay deterministic and semantically stable for the lifetime of every heap version.
 *
 * The policy is trusted. Violating identity, associativity, monotonicity, or the O(1) contract
 * invalidates both the semantic and the complexity guarantees of every heap retaining it.
 */
public interface MonotoneHeapActionPolicy<P, A> {
    /** The exact total preorder over priorities that this action family is monotone for. */
    public val comparator: Comparator<in P>

    /** The semantic identity action. */
    public val identity: A

    /** Returns whether [action] has complete identity behavior. */
    public fun isIdentity(action: A): Boolean

    /** Returns the action equivalent to applying [inner] first and [outer] second. */
    public fun compose(outer: A, inner: A): A

    /** Applies [action] to one priority. */
    public fun apply(action: A, priority: P): P
}

/**
 * A payload paired with its current logical priority, with every pending action already applied.
 *
 * The wrapper itself is never null, so a `null` result from a heap read means "empty heap" even
 * when [element] or [priority] is a stored null.
 */
public data class MonotoneHeapEntry<E, P>(
    /** The payload. */
    public val element: E,
    /** The logical priority. */
    public val priority: P,
)

/**
 * A lower bound, an upper bound, both bounds, an exact constant, or the identity over an ordered
 * domain.
 *
 * Values are minted by [OrderClampPolicy] so that two-sided clamps are validated with the same
 * comparator later used for composition and application. A missing bound denotes negative or
 * positive infinity. Presence is carried by [isConstant], [hasLowerBound], and [hasUpperBound]
 * rather than by null sentinels, so `null` remains usable as an ordinary bound value.
 */
public class OrderClamp<T> private constructor(
    /** Whether this action is an exact constant function. */
    public val isConstant: Boolean,
    /** Whether this action has a lower bound. */
    public val hasLowerBound: Boolean,
    /** Whether this action has an upper bound. */
    public val hasUpperBound: Boolean,
    private val constantSlot: Any?,
    private val lowerSlot: Any?,
    private val upperSlot: Any?,
) {
    internal companion object {
        /** Builds the identity clamp. */
        fun <T> identity(): OrderClamp<T> = OrderClamp(false, false, false, null, null, null)

        /** Builds an exact constant clamp retaining [value] as its representative. */
        fun <T> ofConstant(value: T): OrderClamp<T> =
            OrderClamp(true, false, false, value, null, null)

        /** Builds a bounded clamp from raw slots so an absent bound never needs a `T` witness. */
        fun <T> ofBounds(
            hasLowerBound: Boolean,
            lowerSlot: Any?,
            hasUpperBound: Boolean,
            upperSlot: Any?,
        ): OrderClamp<T> =
            OrderClamp(false, hasLowerBound, hasUpperBound, null, lowerSlot, upperSlot)
    }

    /**
     * The exact constant result.
     *
     * @throws IllegalStateException when the action is not constant.
     */
    public val constantValue: T
        get() {
            check(isConstant) { "The clamp is not constant." }
            return constantOrDefault
        }

    /**
     * The lower bound.
     *
     * @throws IllegalStateException when the action has no lower bound.
     */
    public val lowerBound: T
        get() {
            check(hasLowerBound) { "The clamp has no lower bound." }
            return lowerOrDefault
        }

    /**
     * The upper bound.
     *
     * @throws IllegalStateException when the action has no upper bound.
     */
    public val upperBound: T
        get() {
            check(hasUpperBound) { "The clamp has no upper bound." }
            return upperOrDefault
        }

    @Suppress("UNCHECKED_CAST")
    internal val constantOrDefault: T
        get() = constantSlot as T

    @Suppress("UNCHECKED_CAST")
    internal val lowerOrDefault: T
        get() = lowerSlot as T

    @Suppress("UNCHECKED_CAST")
    internal val upperOrDefault: T
        get() = upperSlot as T

    /** Compares presence flags and only those slots the flags declare present. */
    override fun equals(other: Any?): Boolean {
        if (this === other) {
            return true
        }
        if (other !is OrderClamp<*>) {
            return false
        }
        return isConstant == other.isConstant &&
            hasLowerBound == other.hasLowerBound &&
            hasUpperBound == other.hasUpperBound &&
            (!isConstant || constantSlot == other.constantSlot) &&
            (!hasLowerBound || lowerSlot == other.lowerSlot) &&
            (!hasUpperBound || upperSlot == other.upperSlot)
    }

    /** Hashes exactly the components [equals] compares. */
    override fun hashCode(): Int {
        var result = if (isConstant) 1 else 0
        if (isConstant) {
            result = 31 * result + constantSlot.hashCode()
        }
        result = 31 * result + if (hasLowerBound) 1 else 0
        if (hasLowerBound) {
            result = 31 * result + lowerSlot.hashCode()
        }
        result = 31 * result + if (hasUpperBound) 1 else 0
        if (hasUpperBound) {
            result = 31 * result + upperSlot.hashCode()
        }
        return result
    }

    /** Renders the clamp shape for diagnostics. */
    override fun toString(): String = when {
        isConstant -> "OrderClamp(constant=$constantSlot)"
        hasLowerBound && hasUpperBound -> "OrderClamp(lower=$lowerSlot, upper=$upperSlot)"
        hasLowerBound -> "OrderClamp(lower=$lowerSlot)"
        hasUpperBound -> "OrderClamp(upper=$upperSlot)"
        else -> "OrderClamp(identity)"
    }
}

/**
 * The shipped closed monotone action family `x -> clamp(x, lower, upper)`.
 *
 * Missing bounds denote negative or positive infinity, and composition always yields either one
 * constant-sized clamp or an explicit constant function, so [compose] and [apply] are O(1). The
 * explicit constant case preserves exact results even when [comparator] treats distinct priority
 * values as equal: an inclusive `[k, k]` clamp preserves an input equivalent to `k`, whereas the
 * constant function must hand back the exact `k` representative it stored.
 *
 * Two independently constructed policies are distinct even when their comparators agree; the
 * instance is representation-compatibility state for [PersistentMonotoneActionHeap.meld]. This
 * class deliberately does not override equality, so sharing one instance is how callers declare two
 * heaps meldable.
 */
public class OrderClampPolicy<T>(
    override val comparator: Comparator<in T>,
) : MonotoneHeapActionPolicy<T, OrderClamp<T>> {
    public companion object {
        /** Creates a clamp family over Kotlin's shared natural-order comparator. */
        public fun <T : Comparable<T>> natural(): OrderClampPolicy<T> =
            OrderClampPolicy(naturalOrder())
    }

    override val identity: OrderClamp<T> = OrderClamp.identity()

    /** Creates a floor action `x -> max(x, lowerBound)` in O(1). */
    public fun atLeast(lowerBound: T): OrderClamp<T> =
        OrderClamp.ofBounds(true, lowerBound, false, null)

    /** Creates a cap action `x -> min(x, upperBound)` in O(1). */
    public fun atMost(upperBound: T): OrderClamp<T> =
        OrderClamp.ofBounds(false, null, true, upperBound)

    /** Creates an exact constant action `x -> value` in O(1), retaining that representative. */
    public fun constant(value: T): OrderClamp<T> = OrderClamp.ofConstant(value)

    /**
     * Creates a two-sided clamp action in O(1), validated with this policy's [comparator].
     *
     * @throws IllegalArgumentException when [lowerBound] compares greater than [upperBound].
     */
    public fun between(lowerBound: T, upperBound: T): OrderClamp<T> {
        require(comparator.compare(lowerBound, upperBound) <= 0) {
            "The clamp lower bound must not compare greater than the upper bound."
        }
        return OrderClamp.ofBounds(true, lowerBound, true, upperBound)
    }

    override fun isIdentity(action: OrderClamp<T>): Boolean =
        !action.isConstant && !action.hasLowerBound && !action.hasUpperBound

    /**
     * Composes in O(1) as `outer(inner(x))`.
     *
     * Either operand may short-circuit when it is the identity. An outer constant wins outright; an
     * inner constant is replaced by the outer action applied to it. Disjoint bounds cannot be
     * satisfied together and collapse to a constant carrying the outer (newer) boundary. Otherwise
     * the bounds intersect, and when two boundaries compare equal the inner (older) representative
     * is kept.
     */
    override fun compose(outer: OrderClamp<T>, inner: OrderClamp<T>): OrderClamp<T> {
        if (isIdentity(outer)) {
            return inner
        }
        if (isIdentity(inner)) {
            return outer
        }
        if (outer.isConstant) {
            return outer
        }
        if (inner.isConstant) {
            return OrderClamp.ofConstant(apply(outer, inner.constantOrDefault))
        }

        if (inner.hasUpperBound && outer.hasLowerBound &&
            comparator.compare(inner.upperOrDefault, outer.lowerOrDefault) < 0
        ) {
            return OrderClamp.ofConstant(outer.lowerOrDefault)
        }
        if (inner.hasLowerBound && outer.hasUpperBound &&
            comparator.compare(inner.lowerOrDefault, outer.upperOrDefault) > 0
        ) {
            return OrderClamp.ofConstant(outer.upperOrDefault)
        }

        val hasLower = inner.hasLowerBound || outer.hasLowerBound
        val lower: Any? = when {
            !inner.hasLowerBound -> outer.lowerOrDefault
            !outer.hasLowerBound ||
                comparator.compare(inner.lowerOrDefault, outer.lowerOrDefault) >= 0 ->
                inner.lowerOrDefault
            else -> outer.lowerOrDefault
        }
        val hasUpper = inner.hasUpperBound || outer.hasUpperBound
        val upper: Any? = when {
            !inner.hasUpperBound -> outer.upperOrDefault
            !outer.hasUpperBound ||
                comparator.compare(inner.upperOrDefault, outer.upperOrDefault) <= 0 ->
                inner.upperOrDefault
            else -> outer.upperOrDefault
        }
        return OrderClamp.ofBounds(hasLower, lower, hasUpper, upper)
    }

    /** Applies in O(1), testing the constant first, then the lower bound, then the upper bound. */
    override fun apply(action: OrderClamp<T>, priority: T): T {
        if (action.isConstant) {
            return action.constantOrDefault
        }
        if (action.hasLowerBound && comparator.compare(priority, action.lowerOrDefault) < 0) {
            return action.lowerOrDefault
        }
        if (action.hasUpperBound && comparator.compare(priority, action.upperOrDefault) > 0) {
            return action.upperOrDefault
        }
        return priority
    }
}

/** Statistics returned by the full action-tagged skew-binomial invariant audit. */
public data class PersistentMonotoneActionHeapStatistics(
    /** The logical entry count. */
    public val count: Int,
    /** The number of skew-binomial trees immediately below the global root. */
    public val rootForestLength: Int,
    /** The largest skew-binomial rank encountered. */
    public val maximumRank: Int,
    /** The deepest global-root-to-tree path. */
    public val maximumDepth: Int,
    /**
     * The number of pending tree/forest-tag observations made during the audit.
     *
     * This is not a distinct-object count: exposing one uniform forest tag produces several tagged
     * logical views of the same retained cells.
     */
    public val taggedComponentCount: Int,
)

/** One successful minimum deletion, keeping nullable payloads and priorities unambiguous. */
public data class MonotoneActionMinimumView<E, P, A>(
    /** The removed minimum entry, with every pending action already applied. */
    public val minimum: MonotoneHeapEntry<E, P>,
    /** The persistent heap remaining after that removal. */
    public val remainder: PersistentMonotoneActionHeap<E, P, A>,
)

/**
 * An immutable bootstrapped skew-binomial min-heap carrying pending monotone priority actions.
 *
 * [insert], [minimumOrNull], and [meld] are worst-case O(1); [deleteMinimum] and [minimumView] are
 * worst-case O(log n); [transformAll] is worst-case O(1) time and allocates O(1) new structure;
 * iteration and [validateStructure] are Theta(n); retaining a version as a fork is O(1). These
 * bounds include the O(1)-time policy calls and priority comparisons they perform, so a policy
 * whose [MonotoneHeapActionPolicy.compose], [MonotoneHeapActionPolicy.apply], or
 * [MonotoneHeapActionPolicy.isIdentity] is not O(1), or whose composed actions are not fixed size,
 * invalidates every one of them.
 *
 * Actions are stored recursively on trees and forest spines. That is load-bearing: a transformed
 * heap can still receive an untransformed insertion, or meld with a differently transformed heap,
 * including when the action has no inverse. Tags are only ever composed onto components the
 * underlying bootstrapped skew-binomial algorithm already exposes, which is why the whole-heap
 * transform stays O(1) and why later arrivals are never retroactively transformed.
 *
 * Every update is persistent and leaves all source versions usable. Instances are safe for
 * concurrent reads whenever the caller-owned payloads, priorities, comparator, and action policy
 * support the same access; mutating comparator-, policy-, or priority-observable state can
 * retroactively invalidate existing versions.
 */
public class PersistentMonotoneActionHeap<E, P, A> private constructor(
    private val root: Tree<E, P, A>?,
    /** The number of entries. */
    public val count: Int,
    /** The retained action policy, kept even by empty results so they stay usable. */
    public val actionPolicy: MonotoneHeapActionPolicy<P, A>,
) : Iterable<MonotoneHeapEntry<E, P>> {
    public companion object {
        /** Creates an empty heap retaining [actionPolicy] and the comparator it names. */
        public fun <E, P, A> empty(
            actionPolicy: MonotoneHeapActionPolicy<P, A>,
        ): PersistentMonotoneActionHeap<E, P, A> =
            PersistentMonotoneActionHeap(root = null, count = 0, actionPolicy = actionPolicy)

        /** O(n) bulk construction by repeated worst-case-O(1) insertion. */
        public fun <E, P, A> from(
            entries: Iterable<MonotoneHeapEntry<E, P>>,
            actionPolicy: MonotoneHeapActionPolicy<P, A>,
        ): PersistentMonotoneActionHeap<E, P, A> {
            var result = empty<E, P, A>(actionPolicy)
            for (entry in entries) {
                result = result.insert(entry.element, entry.priority)
            }
            return result
        }
    }

    /** The retained priority comparator, which is exactly the one [actionPolicy] names. */
    public val comparator: Comparator<in P>
        get() = actionPolicy.comparator

    /** Whether the heap holds no entries. */
    public val isEmpty: Boolean
        get() = root == null

    /** Returns a minimum entry in worst-case O(1), or `null` only when the heap is empty. */
    public fun minimumOrNull(): MonotoneHeapEntry<E, P>? {
        val actualRoot = root ?: return null
        return entryOf(actualRoot)
    }

    /**
     * Inserts an entry in worst-case O(1).
     *
     * The new entry joins untransformed: earlier [transformAll] actions never reach it, because the
     * old root is exposed before the loser is skew-inserted beneath it. A tie at the root favours
     * the new entry, which then becomes the exposed minimum.
     *
     * @throws ArithmeticException when the count would exceed [Int.MAX_VALUE].
     */
    public fun insert(element: E, priority: P): PersistentMonotoneActionHeap<E, P, A> {
        val singleton = Tree(0, element, priority, children = null, action = actionPolicy.identity)
        val actualRoot = root ?: return newVersion(singleton, 1)

        val updatedRoot = if (lessOrEqual(singleton, actualRoot)) {
            Tree(0, element, priority, cons(actualRoot, tail = null), actionPolicy.identity)
        } else {
            // Exposing first is what stops the old root's pending action from leaking onto a child
            // that did not exist when that action was applied.
            val exposed = expose(actualRoot)
            Tree(
                0,
                exposed.element,
                exposed.priority,
                skewInsert(singleton, exposed.children),
                actionPolicy.identity,
            )
        }
        return newVersion(updatedRoot, Math.addExact(count, 1))
    }

    /**
     * Applies [action] to every priority present in this version in worst-case O(1), allocating
     * O(1) new structure.
     *
     * Later insertions and later melds are not retroactively transformed. An empty heap or an
     * identity action returns this same instance.
     */
    public fun transformAll(action: A): PersistentMonotoneActionHeap<E, P, A> {
        val actualRoot = root ?: return this
        if (actionPolicy.isIdentity(action)) {
            return this
        }
        return newVersion(tagTree(actualRoot, action), count)
    }

    /**
     * Melds two heaps in worst-case O(1). Each operand keeps its own action history, so neither
     * heap's pending actions reach the other's entries even for non-invertible actions.
     *
     * Melding a heap with itself duplicates every logical occurrence. Compatible operands must
     * retain the same action-policy object or action-policy values that compare equal, matching the
     * precedent set by `RangeUpdateSequence.concat`.
     *
     * @throws IllegalArgumentException when the retained policies or comparators differ.
     * @throws ArithmeticException when the combined count exceeds [Int.MAX_VALUE].
     */
    public fun meld(
        other: PersistentMonotoneActionHeap<E, P, A>,
    ): PersistentMonotoneActionHeap<E, P, A> {
        require(actionPolicy === other.actionPolicy || actionPolicy == other.actionPolicy) {
            "Action-heap melding requires identical or value-equal action policies."
        }
        require(comparator === other.comparator || comparator == other.comparator) {
            "Action-heap melding requires identical or value-equal priority comparators."
        }
        val leftRoot = root ?: return other
        val rightRoot = other.root ?: return this

        val leftWins = lessOrEqual(leftRoot, rightRoot)
        val winner = expose(if (leftWins) leftRoot else rightRoot)
        val loser = if (leftWins) rightRoot else leftRoot
        val updatedRoot = Tree(
            0,
            winner.element,
            winner.priority,
            skewInsert(loser, winner.children),
            actionPolicy.identity,
        )
        return newVersion(updatedRoot, Math.addExact(count, other.count))
    }

    /**
     * Removes a minimum entry in worst-case O(log n), or returns `null` only when the heap is
     * empty. The result retains this heap's action policy, so an emptied heap stays usable.
     */
    public fun deleteMinimum(): PersistentMonotoneActionHeap<E, P, A>? {
        val actualRoot = root ?: return null
        val exposedRoot = expose(actualRoot)
        val children = exposedRoot.children ?: return newVersion(root = null, count = 0)

        val split = getMinimum(children)
        val minimum = expose(split.tree)
        val decomposed = splitForest(minimum.rank, minimum.children)
        var merged = skewMeld(skewMeld(decomposed.trees, split.remainder), decomposed.embeddedForest)
        val zeros = forestToList(decomposed.zeros)
        for (index in zeros.lastIndex downTo 0) {
            merged = skewInsert(zeros[index], merged)
        }
        return newVersion(
            Tree(0, minimum.element, minimum.priority, merged, actionPolicy.identity),
            Math.subtractExact(count, 1),
        )
    }

    /**
     * Removes and returns a minimum entry in worst-case O(log n), or returns `null` only when the
     * heap is empty.
     */
    public fun minimumView(): MonotoneActionMinimumView<E, P, A>? {
        val minimum = minimumOrNull() ?: return null
        val remainder = deleteMinimum()
            ?: throw IllegalStateException("A present root must permit minimum deletion.")
        return MonotoneActionMinimumView(minimum, remainder)
    }

    /**
     * Iterates every entry in Theta(n) in unspecified structural order, composing each pending
     * action exactly once on the way down.
     */
    override fun iterator(): Iterator<MonotoneHeapEntry<E, P>> =
        object : Iterator<MonotoneHeapEntry<E, P>> {
            private val pending = ArrayList<Tree<E, P, A>>().apply { root?.let(::add) }

            override fun hasNext(): Boolean = pending.isNotEmpty()

            override fun next(): MonotoneHeapEntry<E, P> {
                if (pending.isEmpty()) {
                    throw NoSuchElementException()
                }
                val tree = expose(pending.removeAt(pending.lastIndex))
                var forest = tree.children
                while (forest != null) {
                    pending.add(forestHead(forest))
                    forest = forestTail(forest)
                }
                return MonotoneHeapEntry(tree.element, tree.priority)
            }
        }

    /**
     * Independently audits the representation in Theta(n) with an explicit worklist: the rank-zero
     * global root, every fused primitive-child/embedded-forest boundary, skew-forest rank rules
     * (nondecreasing, with only the first two permitted to be equal), logical heap order through
     * every composed pending action, and the reconciled logical count.
     *
     * @throws IllegalStateException when any invariant fails.
     */
    public fun validateStructure(): PersistentMonotoneActionHeapStatistics {
        val actualRoot = root
        if (actualRoot == null) {
            check(count == 0) { "An empty action heap has a nonzero count." }
            return PersistentMonotoneActionHeapStatistics(0, 0, 0, 0, 0)
        }
        check(actualRoot.rank == 0) { "The action-heap global root must have rank zero." }

        var taggedComponentCount = 0
        val normalizedRoot = expose(actualRoot)
        val rootForestLength = validateSkewForest(normalizedRoot.children)
        val pending = ArrayList<TreeFrame<E, P, A>>()
        pending.add(TreeFrame(actualRoot, depth = 1))
        var logicalCount = 0
        var maximumRank = 0
        var maximumDepth = 0
        while (pending.isNotEmpty()) {
            val frame = pending.removeAt(pending.lastIndex)
            if (!actionPolicy.isIdentity(frame.tree.action)) {
                taggedComponentCount++
            }
            val tree = expose(frame.tree)
            check(tree.rank >= 0) { "An action-heap tree has a negative rank." }
            logicalCount = Math.addExact(logicalCount, 1)
            maximumRank = max(maximumRank, tree.rank)
            maximumDepth = max(maximumDepth, frame.depth)
            validateSkewForest(validateFusedChildren(tree))

            var forest = tree.children
            while (forest != null) {
                if (!actionPolicy.isIdentity(forest.action)) {
                    taggedComponentCount++
                }
                val child = forestHead(forest)
                check(lessOrEqual(tree, child)) { "An action-heap child outranks its parent." }
                pending.add(TreeFrame(child, Math.addExact(frame.depth, 1)))
                forest = forestTail(forest)
            }
        }
        check(logicalCount == count) {
            "The action heap's logical count disagrees with its tree graph."
        }
        return PersistentMonotoneActionHeapStatistics(
            count,
            rootForestLength,
            maximumRank,
            maximumDepth,
            taggedComponentCount,
        )
    }

    /** The retained root object, so persistence audits can prove a no-op reused its source. */
    internal fun rootIdentityForTesting(): Any? = root

    /**
     * The retained root child forest, so audits can prove a whole-heap transform composed one tag
     * at the root instead of rebuilding the forest.
     */
    internal fun rootChildrenIdentityForTesting(): Any? = root?.children

    // -- version plumbing -------------------------------------------------------------------

    private fun newVersion(
        root: Tree<E, P, A>?,
        count: Int,
    ): PersistentMonotoneActionHeap<E, P, A> =
        PersistentMonotoneActionHeap(root, count, actionPolicy)

    private fun logicalPriority(tree: Tree<E, P, A>): P =
        actionPolicy.apply(tree.action, tree.priority)

    private fun entryOf(tree: Tree<E, P, A>): MonotoneHeapEntry<E, P> =
        MonotoneHeapEntry(tree.element, logicalPriority(tree))

    private fun lessOrEqual(left: Tree<E, P, A>, right: Tree<E, P, A>): Boolean =
        comparator.compare(logicalPriority(left), logicalPriority(right)) <= 0

    // -- tag algebra ------------------------------------------------------------------------

    /** Pushes a newer action onto one tree; [outer] is the newer half of the composition. */
    private fun tagTree(tree: Tree<E, P, A>, outer: A): Tree<E, P, A> {
        if (actionPolicy.isIdentity(outer)) {
            return tree
        }
        return Tree(
            tree.rank,
            tree.element,
            tree.priority,
            tree.children,
            actionPolicy.compose(outer, tree.action),
        )
    }

    /** Pushes a newer action onto one forest suffix; [outer] is the newer half. */
    private fun tagForest(forest: Forest<E, P, A>, outer: A): Forest<E, P, A> {
        if (actionPolicy.isIdentity(outer)) {
            return forest
        }
        return Forest(
            forest.rawHead,
            forest.rawTail,
            actionPolicy.compose(outer, forest.action),
        )
    }

    /**
     * Materializes one tree's pending action into its own priority and pushes it onto the single
     * head cell of its child forest, which keeps exposure O(1).
     */
    private fun expose(tree: Tree<E, P, A>): Tree<E, P, A> {
        if (actionPolicy.isIdentity(tree.action)) {
            return tree
        }
        val children = tree.children
        return Tree(
            tree.rank,
            tree.element,
            actionPolicy.apply(tree.action, tree.priority),
            if (children == null) null else tagForest(children, tree.action),
            actionPolicy.identity,
        )
    }

    /** Builds an identity-tagged spine cell, so an attached tree keeps only its own history. */
    private fun cons(head: Tree<E, P, A>, tail: Forest<E, P, A>?): Forest<E, P, A> =
        Forest(head, tail, actionPolicy.identity)

    private fun forestHead(forest: Forest<E, P, A>): Tree<E, P, A> =
        tagTree(forest.rawHead, forest.action)

    private fun forestTail(forest: Forest<E, P, A>): Forest<E, P, A>? {
        val rawTail = forest.rawTail ?: return null
        return tagForest(rawTail, forest.action)
    }

    private fun forestToList(initialForest: Forest<E, P, A>?): List<Tree<E, P, A>> {
        val result = ArrayList<Tree<E, P, A>>()
        var forest = initialForest
        while (forest != null) {
            result.add(forestHead(forest))
            forest = forestTail(forest)
        }
        return result
    }

    // -- skew-binomial kernel over logical views ---------------------------------------------

    private fun skewInsert(tree: Tree<E, P, A>, forest: Forest<E, P, A>?): Forest<E, P, A> {
        // Ranks are tag-invariant, so the raw tail's rank decides the link before any tagged tail
        // cell is materialized. Only the linking branch pays for the tagged tail.
        val rawTail = forest?.rawTail
        if (forest != null && rawTail != null && forest.rawHead.rank == rawTail.rawHead.rank) {
            val tail = tagForest(rawTail, forest.action)
            return cons(skewLink(tree, forestHead(forest), forestHead(tail)), forestTail(tail))
        }
        return cons(tree, forest)
    }

    /**
     * Links a rank-0 tree and two trees of equal rank `r` into one of rank `r + 1`. [zero] is always
     * the rank-0 singleton the skew insertion carries; only [first] and [second] must agree on rank,
     * which is why the resulting rank is `r + 1` and not `zero.rank + 1`. Only the logical winner is
     * exposed; the losers attach through identity-tagged spine cells and keep their own histories.
     */
    private fun skewLink(
        zero: Tree<E, P, A>,
        first: Tree<E, P, A>,
        second: Tree<E, P, A>,
    ): Tree<E, P, A> {
        check(first.rank == second.rank) { "Invalid skew-link ranks." }
        if (lessOrEqual(first, zero) && lessOrEqual(first, second)) {
            val winner = expose(first)
            return Tree(
                first.rank + 1,
                winner.element,
                winner.priority,
                cons(zero, cons(second, winner.children)),
                actionPolicy.identity,
            )
        }
        if (lessOrEqual(second, zero) && lessOrEqual(second, first)) {
            val winner = expose(second)
            return Tree(
                second.rank + 1,
                winner.element,
                winner.priority,
                cons(zero, cons(first, winner.children)),
                actionPolicy.identity,
            )
        }
        val winner = expose(zero)
        return Tree(
            first.rank + 1,
            winner.element,
            winner.priority,
            cons(first, cons(second, winner.children)),
            actionPolicy.identity,
        )
    }

    private fun link(first: Tree<E, P, A>, second: Tree<E, P, A>): Tree<E, P, A> {
        check(first.rank == second.rank) { "Invalid binomial-link ranks." }
        val firstWins = lessOrEqual(first, second)
        val winner = expose(if (firstWins) first else second)
        val loser = if (firstWins) second else first
        return Tree(
            winner.rank + 1,
            winner.element,
            winner.priority,
            cons(loser, winner.children),
            actionPolicy.identity,
        )
    }

    /**
     * Lifts a minimum-priority tree out of a forest, favouring the earliest spine occurrence on
     * ties, and rebuilds only the prefix that preceded it.
     */
    private fun getMinimum(forest: Forest<E, P, A>): MinimumSplit<E, P, A> {
        val cells = ArrayList<Forest<E, P, A>>()
        var cursor: Forest<E, P, A>? = forest
        while (cursor != null) {
            cells.add(cursor)
            cursor = forestTail(cursor)
        }
        val heads = ArrayList<Tree<E, P, A>>(cells.size)
        for (cell in cells) {
            heads.add(forestHead(cell))
        }

        var best = 0
        for (index in 1 until heads.size) {
            val candidate = comparator.compare(
                logicalPriority(heads[index]),
                logicalPriority(heads[best]),
            )
            if (candidate < 0) {
                best = index
            }
        }
        var remainder = forestTail(cells[best])
        for (index in best - 1 downTo 0) {
            remainder = cons(heads[index], remainder)
        }
        return MinimumSplit(heads[best], remainder)
    }

    /**
     * Decomposes the fused children of a rank-`initialRank` tree into rank-zero singletons, the
     * ranked trees they were linked with, and the embedded skew-binomial forest.
     */
    private fun splitForest(
        initialRank: Int,
        initialForest: Forest<E, P, A>?,
    ): SplitForest<E, P, A> {
        var rank = initialRank
        var zeros: Forest<E, P, A>? = null
        var trees: Forest<E, P, A>? = null
        var forest = initialForest
        while (true) {
            if (rank == 0) {
                return SplitForest(zeros, trees, forest)
            }
            val firstCell = forest
                ?: throw IllegalStateException("Invalid skew-binomial forest invariant.")
            val first = forestHead(firstCell)
            val tailCell = forestTail(firstCell)
            if (rank == 1 && tailCell == null) {
                return SplitForest(zeros, cons(first, trees), embeddedForest = null)
            }
            val tail = tailCell
                ?: throw IllegalStateException("Invalid skew-binomial forest invariant.")
            val second = forestHead(tail)
            val rest = forestTail(tail)
            if (rank == 1) {
                // The rank-zero ambiguity is resolved in favour of the structural prefix.
                return if (second.rank == 0) {
                    SplitForest(cons(first, zeros), cons(second, trees), rest)
                } else {
                    SplitForest(zeros, cons(first, trees), cons(second, rest))
                }
            }
            if (first.rank == second.rank) {
                return SplitForest(zeros, cons(first, cons(second, trees)), rest)
            }
            if (first.rank == 0) {
                zeros = cons(first, zeros)
                trees = cons(second, trees)
                forest = rest
            } else {
                trees = cons(first, trees)
                forest = cons(second, rest)
            }
            rank--
        }
    }

    private fun skewMeld(left: Forest<E, P, A>?, right: Forest<E, P, A>?): Forest<E, P, A>? =
        unionUnique(uniquify(left), uniquify(right))

    private fun uniquify(forest: Forest<E, P, A>?): Forest<E, P, A>? =
        if (forest == null) null else insertRanked(forestHead(forest), uniquify(forestTail(forest)))

    private fun insertRanked(tree: Tree<E, P, A>, forest: Forest<E, P, A>?): Forest<E, P, A> {
        if (forest == null) {
            return cons(tree, tail = null)
        }
        val head = forestHead(forest)
        check(tree.rank <= head.rank) { "Invalid ranked insertion order." }
        return if (tree.rank < head.rank) {
            cons(tree, forest)
        } else {
            insertRanked(link(tree, head), forestTail(forest))
        }
    }

    private fun unionUnique(
        left: Forest<E, P, A>?,
        right: Forest<E, P, A>?,
    ): Forest<E, P, A>? {
        if (left == null) {
            return right
        }
        if (right == null) {
            return left
        }
        val leftHead = forestHead(left)
        val rightHead = forestHead(right)
        return when {
            leftHead.rank < rightHead.rank -> cons(leftHead, unionUnique(forestTail(left), right))
            leftHead.rank > rightHead.rank -> cons(rightHead, unionUnique(left, forestTail(right)))
            else -> insertRanked(
                link(leftHead, rightHead),
                unionUnique(forestTail(left), forestTail(right)),
            )
        }
    }

    // -- validation helpers -------------------------------------------------------------------

    /** Walks one exposed tree's fused child encoding and returns its embedded forest. */
    private fun validateFusedChildren(tree: Tree<E, P, A>): Forest<E, P, A>? {
        var rank = tree.rank
        var forest = tree.children
        while (rank > 0) {
            val firstCell = forest
                ?: throw IllegalStateException("A ranked action-heap tree is missing children.")
            val first = forestHead(firstCell)
            val tail = forestTail(firstCell)
            if (rank == 1) {
                check(first.rank == 0) { "A rank-one action-heap tree must begin with rank zero." }
                val actualTail = tail ?: return null
                return if (forestHead(actualTail).rank == 0) {
                    forestTail(actualTail)
                } else {
                    actualTail
                }
            }
            val actualTail = tail
                ?: throw IllegalStateException("A ranked action-heap tree has incomplete children.")
            val second = forestHead(actualTail)
            if (first.rank == second.rank) {
                check(first.rank == rank - 1) {
                    "A skew-linked action-heap tree has invalid child ranks."
                }
                return forestTail(actualTail)
            }
            if (first.rank == 0) {
                check(second.rank == rank - 1) {
                    "A skew-linked action-heap tree has an invalid ranked child."
                }
                forest = forestTail(actualTail)
            } else {
                check(first.rank == rank - 1) {
                    "A linked action-heap tree has an invalid ranked child."
                }
                forest = actualTail
            }
            rank--
        }
        return forest
    }

    /** Checks nondecreasing forest ranks where only the first two may be equal, and returns length. */
    private fun validateSkewForest(initialForest: Forest<E, P, A>?): Int {
        var length = 0
        var previousRank = -1
        var duplicate = false
        var forest = initialForest
        while (forest != null) {
            // Ranks are tag-invariant, so the raw head is authoritative and costs no allocation.
            val rank = forest.rawHead.rank
            check(rank >= 0) { "An action-heap forest contains a negative rank." }
            check(rank >= previousRank) { "Action-heap forest ranks are not nondecreasing." }
            if (rank == previousRank) {
                check(length == 1 && !duplicate) {
                    "Only the first two action-heap forest ranks may be equal."
                }
                duplicate = true
            }
            previousRank = rank
            length++
            forest = forestTail(forest)
        }
        return length
    }

    // -- representation -------------------------------------------------------------------------

    /** One skew-binomial tree carrying a pending action for itself and all of its descendants. */
    private class Tree<E, P, A>(
        val rank: Int,
        val element: E,
        /** The raw priority; the logical priority is `apply(action, priority)`. */
        val priority: P,
        /** The raw children; the logical children are `tagForest(children, action)`. */
        val children: Forest<E, P, A>?,
        val action: A,
    )

    /** One forest spine cell carrying a pending action for its whole suffix. */
    private class Forest<E, P, A>(
        /** The raw head; the logical head is `tagTree(rawHead, action)`. */
        val rawHead: Tree<E, P, A>,
        /** The raw tail; the logical tail is `tagForest(rawTail, action)`. */
        val rawTail: Forest<E, P, A>?,
        val action: A,
    )

    private class TreeFrame<E, P, A>(val tree: Tree<E, P, A>, val depth: Int)

    private class MinimumSplit<E, P, A>(
        val tree: Tree<E, P, A>,
        val remainder: Forest<E, P, A>?,
    )

    private class SplitForest<E, P, A>(
        val zeros: Forest<E, P, A>?,
        val trees: Forest<E, P, A>?,
        val embeddedForest: Forest<E, P, A>?,
    )
}
