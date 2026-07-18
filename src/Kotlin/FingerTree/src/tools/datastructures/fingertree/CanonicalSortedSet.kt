package tools.datastructures.fingertree

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.IdentityHashMap
import java.util.TreeSet
import java.util.concurrent.atomic.AtomicReference
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import kotlin.math.max

/**
 * Defines item order and deterministic pseudorandom ranks for a [CanonicalSortedSet].
 *
 * A policy is retained by every derived set. Its [rankHash] must be stable and constant on the
 * [comparator]'s equivalence classes. Public seeds reproduce ranks but are not secret; use
 * [createKeyed] with a protected caller-retained key when rank prediction is part of the threat
 * model.
 */
public class ZipTreeRankPolicy<T> private constructor(
    public val comparator: Comparator<in T>,
    private val rankHash: (T) -> Long,
    private val rankKey: ByteArray,
    public val seed: Long?,
) {
    public companion object {
        private const val MINIMUM_KEY_BYTES: Int = 32
        private const val RANK_KEY_BYTES: Int = 32

        /**
         * Creates a natural-order policy with a fresh hidden key, or with a key derived from
         * [seed]. A non-null seed is a public reproducibility salt, not a secret key.
         */
        public fun <T : Comparable<T>> create(
            seed: Long? = null,
            rankHash: ((T) -> Long)? = null,
        ): ZipTreeRankPolicy<T> = ZipTreeRankPolicy(
            kotlin.comparisons.naturalOrder(),
            rankHash ?: ::defaultRankHash,
            if (seed == null) randomKey() else derivePublicSeedKey(seed),
            seed,
        )

        /**
         * Creates a comparator-aware policy with a fresh hidden key, or with a key derived from
         * [seed]. An explicit comparator requires an explicit equivalence-class-coherent hash.
         */
        public fun <T> create(
            comparator: Comparator<in T>,
            rankHash: ((T) -> Long)? = null,
            seed: Long? = null,
        ): ZipTreeRankPolicy<T> {
            requireNotNull(rankHash) {
                "An explicit comparator requires a rank hash that is constant on its equivalence classes."
            }
            return ZipTreeRankPolicy(
                comparator,
                rankHash,
                if (seed == null) randomKey() else derivePublicSeedKey(seed),
                seed,
            )
        }

        /** Creates a natural-order policy using an owned copy of a caller-retained HMAC key. */
        public fun <T : Comparable<T>> createKeyed(
            rankKey: ByteArray,
            rankHash: ((T) -> Long)? = null,
        ): ZipTreeRankPolicy<T> = ZipTreeRankPolicy(
            kotlin.comparisons.naturalOrder(),
            rankHash ?: ::defaultRankHash,
            copyAndValidateKey(rankKey),
            seed = null,
        )

        /**
         * Creates a comparator-aware policy using an owned copy of a caller-retained HMAC key.
         * An explicit comparator requires an explicit equivalence-class-coherent hash.
         */
        public fun <T> createKeyed(
            rankKey: ByteArray,
            comparator: Comparator<in T>,
            rankHash: ((T) -> Long)? = null,
        ): ZipTreeRankPolicy<T> {
            requireNotNull(rankHash) {
                "An explicit comparator requires a rank hash that is constant on its equivalence classes."
            }
            return ZipTreeRankPolicy(comparator, rankHash, copyAndValidateKey(rankKey), seed = null)
        }

        private fun defaultRankHash(value: Any?): Long = value.hashCode().toLong() and 0xffff_ffffL

        private fun randomKey(): ByteArray = ByteArray(RANK_KEY_BYTES).also(SecureRandom()::nextBytes)

        private fun derivePublicSeedKey(seed: Long): ByteArray {
            val material = ByteBuffer.allocate(12).order(ByteOrder.BIG_ENDIAN)
            material.put('Z'.code.toByte())
            material.put('Z'.code.toByte())
            material.put('T'.code.toByte())
            material.put('2'.code.toByte())
            material.putLong(seed)
            return MessageDigest.getInstance("SHA-256").digest(material.array())
        }

        private fun copyAndValidateKey(rankKey: ByteArray): ByteArray {
            require(rankKey.size >= MINIMUM_KEY_BYTES) {
                "A zip-zip rank key must contain at least $MINIMUM_KEY_BYTES bytes."
            }
            return rankKey.copyOf()
        }

        internal fun mix(value: Long): Long {
            var mixed = value
            mixed = mixed xor (mixed ushr 30)
            mixed *= 0xbf58476d1ce4e5b9UL.toLong()
            mixed = mixed xor (mixed ushr 27)
            mixed *= 0x94d049bb133111ebUL.toLong()
            return mixed xor (mixed ushr 31)
        }
    }

    internal fun rank(item: T): ZipTreeRank {
        val source = ByteBuffer.allocate(Long.SIZE_BYTES)
            .order(ByteOrder.BIG_ENDIAN)
            .putLong(rankHash(item))
            .array()
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(rankKey, "HmacSHA256"))
        val digest = ByteBuffer.wrap(mac.doFinal(source)).order(ByteOrder.BIG_ENDIAN)
        val primary = digest.long
        val secondary = digest.long
        val content = digest.long
        return ZipTreeRank(java.lang.Long.numberOfLeadingZeros(primary), secondary, content)
    }
}

internal data class ZipTreeRank(
    val geometric: Int,
    val secondary: Long,
    val content: Long,
)

/** A comparer-aware lookup result that remains unambiguous when a set stores nullable values. */
public data class CanonicalSetLookup<T>(
    public val found: Boolean,
    public val value: T,
)

/** Structural statistics returned by [CanonicalSortedSet.validateStructure]. */
public data class CanonicalSortedSetStatistics(
    public val count: Int,
    public val height: Int,
    public val maximumGeometricRank: Int,
    public val priorityCollisionCount: Int,
)

/**
 * An immutable policy-canonical sorted set backed by a zip-zip-inspired Cartesian tree.
 *
 * Stable comparer and rank-hash behavior make topology independent of update history within one
 * rank policy. Expected operations are O(log n); colliding or adversarial ranks can produce O(n)
 * height. All traversals and updates are iterative and remain stack-safe in that case.
 */
public class CanonicalSortedSet<T> private constructor(
    private val root: Node<T>?,
    public val policy: ZipTreeRankPolicy<T>,
) : Iterable<T> {
    public companion object {
        /** Creates an empty set retaining [policy]. */
        public fun <T> empty(policy: ZipTreeRankPolicy<T>): CanonicalSortedSet<T> =
            CanonicalSortedSet(root = null, policy)

        /** Creates a set retaining the first representative from each comparator-equivalence class. */
        public fun <T> from(
            values: Iterable<T>,
            policy: ZipTreeRankPolicy<T>,
        ): CanonicalSortedSet<T> {
            val pending = ArrayList<PendingItem<T>>()
            var sequence = 0
            for (value in values) {
                pending.add(PendingItem(value, sequence))
                sequence = Math.addExact(sequence, 1)
            }
            if (pending.isEmpty()) {
                return empty(policy)
            }

            pending.sortWith { left, right ->
                val comparison = policy.comparator.compare(left.item, right.item)
                if (comparison != 0) comparison else left.sequence.compareTo(right.sequence)
            }

            val unique = ArrayList<BuilderNode<T>>(pending.size)
            var index = 0
            while (index < pending.size) {
                val representative = pending[index].item
                val rank = policy.rank(representative)
                var end = index + 1
                while (end < pending.size && policy.comparator.compare(representative, pending[end].item) == 0) {
                    if (policy.rank(pending[end].item) != rank) {
                        throw inconsistentRankHash()
                    }
                    end++
                }
                unique.add(BuilderNode(representative, rank))
                index = end
            }

            return CanonicalSortedSet(buildCanonical(unique, policy.comparator), policy)
        }

        /** C#-surface alias for [empty]. */
        public fun <T> create(policy: ZipTreeRankPolicy<T>): CanonicalSortedSet<T> = empty(policy)

        /** C#-surface alias for [from]. */
        public fun <T> createRange(
            values: Iterable<T>,
            policy: ZipTreeRankPolicy<T>,
        ): CanonicalSortedSet<T> = from(values, policy)

        private fun <T> buildCanonical(
            nodes: List<BuilderNode<T>>,
            comparator: Comparator<in T>,
        ): Node<T>? {
            if (nodes.isEmpty()) {
                return null
            }

            val spine = ArrayList<BuilderNode<T>>()
            for (node in nodes) {
                var left: BuilderNode<T>? = null
                while (spine.isNotEmpty() && higher(
                        node.item,
                        node.rank,
                        spine.last().item,
                        spine.last().rank,
                        comparator,
                    )
                ) {
                    left = spine.removeAt(spine.lastIndex)
                }
                node.left = left
                if (spine.isNotEmpty()) {
                    spine.last().right = node
                }
                spine.add(node)
            }

            while (spine.size > 1) {
                spine.removeAt(spine.lastIndex)
            }
            val root = spine.single()
            val pending = ArrayList<BuilderFrame<T>>()
            pending.add(BuilderFrame(root, expanded = false))
            while (pending.isNotEmpty()) {
                val frame = pending.removeAt(pending.lastIndex)
                if (frame.expanded) {
                    frame.node.frozen = Node(
                        frame.node.item,
                        frame.node.rank,
                        frame.node.left?.frozen,
                        frame.node.right?.frozen,
                    )
                } else {
                    pending.add(BuilderFrame(frame.node, expanded = true))
                    frame.node.right?.let { pending.add(BuilderFrame(it, expanded = false)) }
                    frame.node.left?.let { pending.add(BuilderFrame(it, expanded = false)) }
                }
            }
            return root.frozen
        }

        private fun <T> insert(
            root: Node<T>?,
            item: Node<T>,
            comparator: Comparator<in T>,
        ): Node<T> {
            if (root == null) {
                return item
            }
            val path = ArrayList<PathStep<T>>()
            var cursor: Node<T>? = root
            while (cursor != null && !higher(item, cursor, comparator)) {
                val current = cursor
                val wentLeft = comparator.compare(item.item, current.item) < 0
                path.add(PathStep(current, wentLeft))
                cursor = if (wentLeft) current.left else current.right
            }

            val split = split(cursor, item.item, comparator)
            var result = Node(item.item, item.rank, split.first, split.second)
            while (path.isNotEmpty()) {
                val step = path.removeAt(path.lastIndex)
                result = if (step.wentLeft) {
                    Node(step.node.item, step.node.rank, result, step.node.right)
                } else {
                    Node(step.node.item, step.node.rank, step.node.left, result)
                }
            }
            return result
        }

        private fun <T> split(
            root: Node<T>?,
            item: T,
            comparator: Comparator<in T>,
        ): Pair<Node<T>?, Node<T>?> {
            val path = ArrayList<PathStep<T>>()
            var cursor = root
            while (cursor != null) {
                val current = cursor
                val wentLeft = comparator.compare(item, current.item) < 0
                path.add(PathStep(current, wentLeft))
                cursor = if (wentLeft) current.left else current.right
            }

            var left: Node<T>? = null
            var right: Node<T>? = null
            while (path.isNotEmpty()) {
                val step = path.removeAt(path.lastIndex)
                if (step.wentLeft) {
                    right = Node(step.node.item, step.node.rank, right, step.node.right)
                } else {
                    left = Node(step.node.item, step.node.rank, step.node.left, left)
                }
            }
            return left to right
        }

        private fun <T> remove(
            root: Node<T>?,
            item: T,
            comparator: Comparator<in T>,
        ): RemoveResult<T> {
            val path = ArrayList<PathStep<T>>()
            var cursor = root
            while (cursor != null) {
                val comparison = comparator.compare(item, cursor.item)
                if (comparison == 0) {
                    break
                }
                val current = cursor
                val wentLeft = comparison < 0
                path.add(PathStep(current, wentLeft))
                cursor = if (wentLeft) current.left else current.right
            }
            if (cursor == null) {
                return RemoveResult(root, removed = false)
            }

            var result = merge(cursor.left, cursor.right, comparator)
            while (path.isNotEmpty()) {
                val step = path.removeAt(path.lastIndex)
                result = if (step.wentLeft) {
                    Node(step.node.item, step.node.rank, result, step.node.right)
                } else {
                    Node(step.node.item, step.node.rank, step.node.left, result)
                }
            }
            return RemoveResult(result, removed = true)
        }

        private fun <T> merge(
            initialLeft: Node<T>?,
            initialRight: Node<T>?,
            comparator: Comparator<in T>,
        ): Node<T>? {
            if (initialLeft == null) {
                return initialRight
            }
            if (initialRight == null) {
                return initialLeft
            }

            var left: Node<T>? = initialLeft
            var right: Node<T>? = initialRight
            val path = ArrayList<MergeStep<T>>()
            while (left != null && right != null) {
                if (higher(left, right, comparator)) {
                    path.add(MergeStep(left, choseLeft = true))
                    left = left.right
                } else {
                    path.add(MergeStep(right, choseLeft = false))
                    right = right.left
                }
            }

            var result = left ?: right
            while (path.isNotEmpty()) {
                val step = path.removeAt(path.lastIndex)
                result = if (step.choseLeft) {
                    Node(step.node.item, step.node.rank, step.node.left, result)
                } else {
                    Node(step.node.item, step.node.rank, result, step.node.right)
                }
            }
            return result
        }

        private fun <T> higher(
            left: Node<T>,
            right: Node<T>,
            comparator: Comparator<in T>,
        ): Boolean = higher(left.item, left.rank, right.item, right.rank, comparator)

        private fun <T> higher(
            leftItem: T,
            leftRank: ZipTreeRank,
            rightItem: T,
            rightRank: ZipTreeRank,
            comparator: Comparator<in T>,
        ): Boolean {
            val geometric = leftRank.geometric.compareTo(rightRank.geometric)
            if (geometric != 0) {
                return geometric > 0
            }
            val secondary = java.lang.Long.compareUnsigned(leftRank.secondary, rightRank.secondary)
            if (secondary != 0) {
                return secondary > 0
            }
            return comparator.compare(leftItem, rightItem) < 0
        }

        private fun inconsistentRankHash(): IllegalStateException = IllegalStateException(
            "The rank hash is not constant on the set comparator's equivalence classes.",
        )
    }

    /** Number of represented comparator-equivalence classes. */
    public val size: Int
        get() = root?.count ?: 0

    /** Alias for [size] matching the reference implementation's terminology. */
    public val count: Int
        get() = size

    public val isEmpty: Boolean
        get() = root == null

    /** Cached tree height, primarily for diagnostics and adversarial validation. */
    public val height: Int
        get() = root?.height ?: 0

    /**
     * Memoized non-cryptographic tree digest, or zero for an empty set. Within one coherent policy,
     * digest inequality proves semantic inequality; equality still requires item comparison.
     */
    public val contentHash: Long
        get() = root?.digest() ?: 0L

    public operator fun contains(value: T): Boolean = findNode(value) != null

    /** Recovers the stored representative for a comparator-equivalent [value]. */
    public fun tryGetValue(value: T): CanonicalSetLookup<T> {
        val node = findNode(value)
        return if (node == null) CanonicalSetLookup(found = false, value) else CanonicalSetLookup(true, node.item)
    }

    /** Adds [value], retaining this instance and the existing representative for a duplicate. */
    public fun add(value: T): CanonicalSortedSet<T> {
        val existing = findNode(value)
        if (existing != null) {
            if (existing.rank != policy.rank(value)) {
                throw inconsistentRankHash()
            }
            return this
        }
        val newRoot = insert(root, Node(value, policy.rank(value), left = null, right = null), policy.comparator)
        return CanonicalSortedSet(newRoot, policy)
    }

    /** Removes the comparator-equivalent value, preserving identity when it is absent. */
    public fun remove(value: T): CanonicalSortedSet<T> {
        val result = remove(root, value, policy.comparator)
        return if (!result.removed) this else CanonicalSortedSet(result.root, policy)
    }

    /** Returns an empty set retaining this policy, preserving identity when already empty. */
    public fun clear(): CanonicalSortedSet<T> = if (isEmpty) this else empty(policy)

    /** Returns a canonical union; both operands must retain the same policy object. */
    public fun union(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> {
        ensureCompatible(other)
        if (root === other.root) {
            return this
        }
        var result = this
        for (value in other) {
            result = result.add(value)
        }
        return result
    }

    /** Returns a canonical intersection; both operands must retain the same policy object. */
    public fun intersect(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> {
        ensureCompatible(other)
        if (root === other.root) {
            return this
        }
        var result = empty(policy)
        for (value in this) {
            if (value in other) {
                result = result.add(value)
            }
        }
        return result
    }

    /** Returns this set minus [other]; both operands must retain the same policy object. */
    public fun except(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> {
        ensureCompatible(other)
        if (root === other.root) {
            return empty(policy)
        }
        var result = this
        for (value in other) {
            result = result.remove(value)
        }
        return result
    }

    /**
     * Compares mathematical contents. Same-policy sets use count and memoized-digest rejection,
     * then iterative lockstep comparison that prunes shared nodes.
     */
    public fun setEquals(other: CanonicalSortedSet<T>): Boolean {
        if (this === other) {
            return true
        }
        if (policy === other.policy) {
            if (size != other.size || contentHash != other.contentHash) {
                return false
            }
            return nodesEqual(root, other.root)
        }
        return semanticSetEquals(other)
    }

    /** Compares contents using this set's comparator, independently of rank-policy identity. */
    public fun setEquals(values: Iterable<T>): Boolean {
        if (values is CanonicalSortedSet<T> && policy === values.policy) {
            return setEquals(values)
        }
        return semanticSetEquals(values)
    }

    public fun isSubsetOf(values: Iterable<T>): Boolean {
        val other = semanticProbe(values)
        return all { other.contains(it) }
    }

    public fun isProperSubsetOf(values: Iterable<T>): Boolean {
        val other = semanticProbe(values)
        return size < other.size && all { other.contains(it) }
    }

    public fun isSupersetOf(values: Iterable<T>): Boolean = values.all { it in this }

    public fun isProperSupersetOf(values: Iterable<T>): Boolean {
        val other = semanticProbe(values)
        return size > other.size && other.all { it in this }
    }

    public fun overlaps(values: Iterable<T>): Boolean = values.any { it in this }

    /** Reports whether the two values retain at least one node by identity. */
    public fun sharesStorageWith(other: CanonicalSortedSet<T>): Boolean {
        if (root == null || other.root == null) {
            return false
        }
        if (root === other.root) {
            return true
        }
        val identities = IdentityHashMap<Node<T>, Boolean>()
        val first = ArrayList<Node<T>>()
        first.add(root)
        while (first.isNotEmpty()) {
            val node = first.removeAt(first.lastIndex)
            if (identities.put(node, true) == null) {
                node.left?.let(first::add)
                node.right?.let(first::add)
            }
        }
        val second = ArrayList<Node<T>>()
        second.add(other.root)
        while (second.isNotEmpty()) {
            val node = second.removeAt(second.lastIndex)
            if (identities.containsKey(node)) {
                return true
            }
            node.left?.let(second::add)
            node.right?.let(second::add)
        }
        return false
    }

    /** Validates order, heap priority, rank reproducibility, uniqueness, and cached metadata. */
    public fun validateStructure(): CanonicalSortedSetStatistics {
        val actualRoot = root ?: return CanonicalSortedSetStatistics(0, 0, 0, 0)
        val pending = ArrayList<ValidationFrame<T>>()
        pending.add(ValidationFrame(actualRoot, lower = null, upper = null, depth = 1))
        val visited = IdentityHashMap<Node<T>, Boolean>()
        val priorities = HashSet<PriorityKey>()
        var validatedCount = 0
        var maximumDepth = 0
        var maximumGeometricRank = 0
        var priorityCollisions = 0

        while (pending.isNotEmpty()) {
            val frame = pending.removeAt(pending.lastIndex)
            val node = frame.node
            check(visited.put(node, true) == null) {
                "A canonical sorted-set node is reached more than once or participates in a cycle."
            }
            frame.lower?.let {
                check(policy.comparator.compare(node.item, it.value) > 0) {
                    "A canonical sorted-set node crosses its lower key bound."
                }
            }
            frame.upper?.let {
                check(policy.comparator.compare(node.item, it.value) < 0) {
                    "A canonical sorted-set node crosses its upper key bound."
                }
            }
            check(node.rank == policy.rank(node.item)) {
                "A canonical sorted-set node has a non-reproducible rank."
            }
            node.left?.let {
                check(higher(node, it, policy.comparator)) {
                    "A canonical sorted-set left child outranks its parent."
                }
            }
            node.right?.let {
                check(higher(node, it, policy.comparator)) {
                    "A canonical sorted-set right child outranks its parent."
                }
            }

            val expectedCount = Math.addExact(1, Math.addExact(node.left?.count ?: 0, node.right?.count ?: 0))
            val expectedHeight = Math.addExact(1, max(node.left?.height ?: 0, node.right?.height ?: 0))
            check(node.count == expectedCount && node.height == expectedHeight) {
                "A canonical sorted-set node has invalid cached metadata."
            }

            validatedCount = Math.addExact(validatedCount, 1)
            maximumDepth = max(maximumDepth, frame.depth)
            maximumGeometricRank = max(maximumGeometricRank, node.rank.geometric)
            if (!priorities.add(PriorityKey(node.rank.geometric, node.rank.secondary))) {
                priorityCollisions = Math.addExact(priorityCollisions, 1)
            }
            node.right?.let {
                pending.add(ValidationFrame(it, Bound(node.item), frame.upper, Math.addExact(frame.depth, 1)))
            }
            node.left?.let {
                pending.add(ValidationFrame(it, frame.lower, Bound(node.item), Math.addExact(frame.depth, 1)))
            }
        }

        check(validatedCount == size && maximumDepth == height) {
            "Canonical sorted-set root metadata disagrees with the validated tree."
        }
        return CanonicalSortedSetStatistics(
            validatedCount,
            maximumDepth,
            maximumGeometricRank,
            priorityCollisions,
        )
    }

    override fun iterator(): Iterator<T> = object : Iterator<T> {
        private val pending = ArrayList<Node<T>>()
        private var cursor = root

        init {
            pushLeftSpine()
        }

        override fun hasNext(): Boolean = pending.isNotEmpty()

        override fun next(): T {
            if (pending.isEmpty()) {
                throw NoSuchElementException()
            }
            val node = pending.removeAt(pending.lastIndex)
            cursor = node.right
            pushLeftSpine()
            return node.item
        }

        private fun pushLeftSpine() {
            while (cursor != null) {
                val node = cursor ?: break
                pending.add(node)
                cursor = node.left
            }
        }
    }

    internal fun shapeForTesting(): List<CanonicalNodeShape<T>> {
        val actualRoot = root ?: return emptyList()
        val result = ArrayList<CanonicalNodeShape<T>>(size)
        val pending = ArrayList<Node<T>>()
        pending.add(actualRoot)
        while (pending.isNotEmpty()) {
            val node = pending.removeAt(pending.lastIndex)
            result.add(CanonicalNodeShape(node.item, node.left?.count ?: 0, node.right?.count ?: 0))
            node.right?.let(pending::add)
            node.left?.let(pending::add)
        }
        return result
    }

    internal fun rankForTesting(value: T): ZipTreeRank = policy.rank(value)

    internal fun nodeIdentityForTesting(value: T): Any? = findNode(value)

    internal fun cursorBoundRank(value: T, upper: Boolean): Int {
        var rank = 0
        var cursor = root
        while (cursor != null) {
            val comparison = policy.comparator.compare(cursor.item, value)
            if (comparison < 0 || (upper && comparison == 0)) {
                rank = Math.addExact(rank, Math.addExact(cursor.left?.count ?: 0, 1))
                cursor = cursor.right
            } else {
                cursor = cursor.left
            }
        }
        return rank
    }

    internal fun cursorItemAt(rank: Int): T {
        require(rank in 0 until size) { "Rank must identify a canonical-set item." }
        var remaining = rank
        var cursor = root
        while (cursor != null) {
            val leftCount = cursor.left?.count ?: 0
            when {
                remaining < leftCount -> cursor = cursor.left
                remaining == leftCount -> return cursor.item
                else -> {
                    remaining -= leftCount + 1
                    cursor = cursor.right
                }
            }
        }
        error("Canonical-set rank metadata is inconsistent.")
    }

    private fun findNode(value: T): Node<T>? {
        var cursor = root
        while (cursor != null) {
            val comparison = policy.comparator.compare(value, cursor.item)
            if (comparison == 0) {
                return cursor
            }
            cursor = if (comparison < 0) cursor.left else cursor.right
        }
        return null
    }

    private fun semanticProbe(values: Iterable<T>): TreeSet<T> {
        val probe = TreeSet<T>(policy.comparator)
        for (value in values) {
            probe.add(value)
        }
        return probe
    }

    private fun semanticSetEquals(values: Iterable<T>): Boolean {
        val probe = semanticProbe(values)
        return size == probe.size && probe.all { it in this }
    }

    private fun ensureCompatible(other: CanonicalSortedSet<T>) {
        require(policy === other.policy) {
            "Canonical set algebra requires the same rank-policy object."
        }
    }

    private fun nodesEqual(leftRoot: Node<T>?, rightRoot: Node<T>?): Boolean {
        val pending = ArrayList<NodePair<T>>()
        pending.add(NodePair(leftRoot, rightRoot))
        while (pending.isNotEmpty()) {
            val pair = pending.removeAt(pending.lastIndex)
            if (pair.left === pair.right) {
                continue
            }
            val left = pair.left ?: return false
            val right = pair.right ?: return false
            if (policy.comparator.compare(left.item, right.item) != 0) {
                return false
            }
            pending.add(NodePair(left.right, right.right))
            pending.add(NodePair(left.left, right.left))
        }
        return true
    }

    private class Node<T>(
        val item: T,
        val rank: ZipTreeRank,
        val left: Node<T>?,
        val right: Node<T>?,
    ) {
        val count: Int = Math.addExact(1, Math.addExact(left?.count ?: 0, right?.count ?: 0))
        val height: Int = Math.addExact(1, max(left?.height ?: 0, right?.height ?: 0))
        private val memoizedDigest = AtomicReference<DigestBox?>()

        fun digest(): Long {
            memoizedDigest.get()?.let { return it.value }
            val pending = ArrayList<DigestFrame<T>>()
            pending.add(DigestFrame(this, expanded = false))
            while (pending.isNotEmpty()) {
                val frame = pending.removeAt(pending.lastIndex)
                if (frame.node.memoizedDigest.get() != null) {
                    continue
                }
                if (!frame.expanded) {
                    pending.add(DigestFrame(frame.node, expanded = true))
                    frame.node.right?.let {
                        if (it.memoizedDigest.get() == null) pending.add(DigestFrame(it, expanded = false))
                    }
                    frame.node.left?.let {
                        if (it.memoizedDigest.get() == null) pending.add(DigestFrame(it, expanded = false))
                    }
                } else {
                    val leftHash = frame.node.left?.memoizedDigest?.get()?.value ?: 0x243f6a8885a308d3L
                    val rightHash = frame.node.right?.memoizedDigest?.get()?.value ?: 0x13198a2e03707344L
                    val digest = ZipTreeRankPolicy.mix(
                        frame.node.rank.content xor
                            java.lang.Long.rotateLeft(leftHash, 17) xor
                            java.lang.Long.rotateLeft(rightHash, 43),
                    )
                    frame.node.memoizedDigest.compareAndSet(null, DigestBox(digest))
                }
            }
            return memoizedDigest.get()!!.value
        }
    }

    private data class PendingItem<T>(val item: T, val sequence: Int)

    private class BuilderNode<T>(val item: T, val rank: ZipTreeRank) {
        var left: BuilderNode<T>? = null
        var right: BuilderNode<T>? = null
        var frozen: Node<T>? = null
    }

    private data class BuilderFrame<T>(val node: BuilderNode<T>, val expanded: Boolean)
    private data class PathStep<T>(val node: Node<T>, val wentLeft: Boolean)
    private data class MergeStep<T>(val node: Node<T>, val choseLeft: Boolean)
    private data class RemoveResult<T>(val root: Node<T>?, val removed: Boolean)
    private data class Bound<T>(val value: T)
    private data class ValidationFrame<T>(
        val node: Node<T>,
        val lower: Bound<T>?,
        val upper: Bound<T>?,
        val depth: Int,
    )
    private data class PriorityKey(val geometric: Int, val secondary: Long)
    private data class NodePair<T>(val left: Node<T>?, val right: Node<T>?)
    private data class DigestFrame<T>(val node: Node<T>, val expanded: Boolean)
    private data class DigestBox(val value: Long)
}

internal data class CanonicalNodeShape<T>(
    val item: T,
    val leftCount: Int,
    val rightCount: Int,
)
