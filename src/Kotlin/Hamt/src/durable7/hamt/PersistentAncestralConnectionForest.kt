/*
 * Fully branching persistent insertion-only connectivity forest with a first-connection query.
 *
 * A persistent union-find over the fixed integer vertex universe `0 until vertexCount`. Each
 * successful link performs union by size *without* path compression and labels the one new
 * root-parent edge with the child version token. A redundant (already connected) link still creates
 * a child history version, but shares the complete connectivity index.
 *
 * The parent cells live sparsely in a CHAMP `PersistentHashMap`. An absent cell denotes a singleton
 * root, so creation is O(1) rather than O(vertexCount). Union by size limits every parent path to
 * O(log n) cells. `firstConnected` compares the two current parent paths and takes the latest
 * edge-version below their forest lowest common ancestor, so it never searches the version history
 * and its cost is independent of history depth.
 *
 * Cost of the CHAMP path
 * ----------------------
 * Throughout this file w denotes the cost of one `PersistentHashMap` cell lookup or insertion. Here
 * w is bounded *unconditionally*, not merely in expectation, and the reason is worth stating because
 * sibling ports differ. The cell map is built over [VertexHashPolicy], whose hash of a vertex is the
 * vertex itself. That is injective on `Int`, and this workspace's CHAMP forms a collision bucket
 * only when two keys share their entire 32-bit hash, so no bucket can ever hold two distinct
 * vertices. Routing consumes 5 hash bits per level over a 32-bit hash, so a path is at most
 * `ceil(32 / 5) = 7` nodes and each node copies a run of at most 32 slots. w is therefore O(1)
 * worst case, and every bound below that carries a w factor is worst case, matching the managed
 * reference. The Rust and Haskell ports state *expected* bounds instead, because their hashes are
 * randomized or truncated and genuinely collide; that divergence is a property of their hash
 * policies, not of this algorithm.
 *
 * Stack depth
 * -----------
 * A history is a linked list that can be hundreds of thousands of links deep. Every walk over a
 * version chain or a parent path in this file is an explicit loop, never recursion, so a deep
 * history cannot overflow the JVM stack in a query or in [PersistentAncestralConnectionForest
 * .validateStructure]. Unlike the Rust port there is no release hazard to answer: the JVM collector
 * reclaims a chain iteratively, so no hand-written unlinking is needed. [AncestralConnectionVersion]
 * also keeps [AncestralConnectionVersion.toString] free of any parent traversal.
 *
 * Instances and version tokens are immutable snapshots safe for concurrent readers. Deletion,
 * confluent merging, retroactive updates, and path compression are deliberately outside this
 * contract. The O(log n) parent-path factor is worst-case logarithmic in component size rather than
 * the inverse-Ackermann amortized bound of an ephemeral linear-history union-find.
 */
package durable7.hamt

/**
 * The identity hash policy governing the sparse parent map's `Int` vertex keys.
 *
 * Hashing a vertex to itself is injective over the whole `Int` universe, which is what lets this
 * file claim a worst-case rather than an expected CHAMP path factor. The policy is pinned here
 * rather than inherited from [defaultHashPolicy] so the claim rests on a locally visible fact.
 */
private object VertexHashPolicy : HashPolicy<Int> {
    override fun hash(key: Int): Int = key
    override fun equivalent(left: Int, right: Int): Boolean = left == right
}

/**
 * Identifies one immutable version in a [PersistentAncestralConnectionForest] history tree.
 *
 * Tokens use *reference identity*: this class deliberately does not override `equals` or `hashCode`,
 * so `==` is `===` and two versions at equal depths in sibling branches are never equal. That is the
 * faithful reading of the managed reference, which relies on the same property, and it is why a hash
 * container of tokens partitions by identity without any custom comparator. Use [isSameVersion] when
 * the intent should be explicit at the call site.
 *
 * The parent chain contains history identities, not mutable connectivity state.
 */
public class AncestralConnectionVersion private constructor(
    /** This version's parent, or `null` at the history root. O(1). */
    public val parent: AncestralConnectionVersion?,
    private val storedRoot: AncestralConnectionVersion?,
) {
    internal companion object {
        /** A fresh history root: no parent, depth zero, and its own root identity. */
        fun newRoot(): AncestralConnectionVersion = AncestralConnectionVersion(null, null)

        /**
         * A fresh child of [parent], one link event later on the same history tree.
         *
         * @throws ArithmeticException if the history depth would exceed [Long.MAX_VALUE].
         */
        fun child(parent: AncestralConnectionVersion): AncestralConnectionVersion =
            AncestralConnectionVersion(parent, parent.root)
    }

    /** The number of link events from the history root to this version. O(1). */
    public val depth: Long = parent?.let { Math.addExact(it.depth, 1L) } ?: 0L

    /**
     * The root identity of this version tree. O(1).
     *
     * The root is stored as a nullable field that is `null` exactly at the root itself, so reading it
     * stays O(1) without the token holding a reference to itself.
     */
    public val root: AncestralConnectionVersion get() = storedRoot ?: this

    /** Whether this and [other] denote the very same version. O(1). */
    public fun isSameVersion(other: AncestralConnectionVersion): Boolean = this === other

    /** A depth-only rendering. Deliberately does not walk the parent chain, which may be very deep. */
    override fun toString(): String = "AncestralConnectionVersion(depth=$depth)"
}

/**
 * The outcome of a first-connection query whose vertices both lie inside the forest's universe.
 *
 * [PersistentAncestralConnectionForest.firstConnected] has two distinct failure modes and this type
 * exists to keep them apart: a `null` *result* means a vertex was outside the universe, while
 * [Disconnected] means both vertices were valid and simply are not connected in the queried version.
 */
public sealed interface AncestralConnectionLookup {
    /** The pair is connected, and [version] is the ancestor version in which it first became so. */
    public data class Found(public val version: AncestralConnectionVersion) : AncestralConnectionLookup

    /** Both vertices are in the universe, but no ancestor version connects them. */
    public data object Disconnected : AncestralConnectionLookup
}

/** Shape measurements returned by a successful connection-forest audit. */
public data class PersistentAncestralConnectionForestStatistics(
    /** The fixed vertex-universe size. */
    public val vertexCount: Int,
    /** The number of connected components, recounted from the parent forest. */
    public val componentCount: Int,
    /** The number of explicitly stored parent cells; absent cells are singleton roots. */
    public val storedCellCount: Int,
    /** The longest parent path found, in edges. Bounded by `floor(log2(vertexCount))`. */
    public val maximumParentPathLength: Int,
    /** The number of link events from the history root to the validated version. */
    public val historyDepth: Long,
)

/**
 * The later of two candidate witness versions, or `null` when neither is present.
 *
 * On equal depth [left] is kept, which makes the reduction in
 * [PersistentAncestralConnectionForest.firstConnected] independent of the order in which candidates
 * arrive. A well-formed forest never actually produces a tie, because every link tags at most one
 * edge and therefore all stored tags are distinct versions at distinct depths; the rule is a
 * determinism guarantee for a case the construction cannot reach. Internal so that the rule itself
 * stays directly testable without widening the public surface.
 */
internal fun laterConnectionVersion(
    left: AncestralConnectionVersion?,
    right: AncestralConnectionVersion?,
): AncestralConnectionVersion? =
    if (right != null && (left == null || right.depth > left.depth)) right else left

/**
 * A fully branching persistent insertion-only connectivity forest that reports the first ancestor
 * version in which two vertices became connected.
 *
 * Every version is an immutable snapshot; retaining one keeps its own connectivity observable no
 * matter how its descendants or siblings evolve. Fallible operations return `null` when a vertex
 * argument falls outside the fixed universe rather than throwing, matching this workspace's
 * out-of-range convention; genuine contract violations still throw.
 */
public class PersistentAncestralConnectionForest private constructor(
    /** The fixed number of vertices, whose universe is `0 until vertexCount`. O(1). */
    public val vertexCount: Int,
    /** The number of connected components in this version. O(1). */
    public val componentCount: Int,
    private val cells: PersistentHashMap<Int, Cell>,
    /** The identity token for this history version. O(1). */
    public val version: AncestralConnectionVersion,
) {
    /**
     * One sparse parent record.
     *
     * [size] is meaningful only for a root ([parent] equal to the vertex itself), and [joinedAt] is
     * present only on a non-root edge. An absent cell canonically means "singleton root of size 1",
     * and a cell is never stored as `null`, so an absent map entry is unambiguous.
     */
    private data class Cell(val parent: Int, val size: Int, val joinedAt: AncestralConnectionVersion?) {
        companion object {
            fun root(vertex: Int, size: Int): Cell = Cell(vertex, size, null)
            fun child(parent: Int, joinedAt: AncestralConnectionVersion): Cell = Cell(parent, 0, joinedAt)
        }
    }

    private data class PathStep(val vertex: Int, val joinedAt: AncestralConnectionVersion?)

    public companion object {
        /**
         * An edgeless root version over vertices `0 until vertexCount`. O(1).
         *
         * Every vertex starts as its own component. Because a singleton is represented by the
         * *absence* of a cell, construction costs the same for a universe of two vertices and for one
         * of [Int.MAX_VALUE].
         *
         * @throws IllegalArgumentException if [vertexCount] is negative.
         */
        public fun create(vertexCount: Int): PersistentAncestralConnectionForest {
            require(vertexCount >= 0) { "Vertex count cannot be negative." }
            return PersistentAncestralConnectionForest(
                vertexCount,
                vertexCount,
                PersistentHashMap.empty(VertexHashPolicy),
                AncestralConnectionVersion.newRoot(),
            )
        }
    }

    /**
     * Whether [vertex] lies inside the fixed universe. O(1).
     *
     * The disambiguator for the `null` returned by the operations below, which report an
     * out-of-universe argument rather than an absent answer.
     */
    public fun containsVertex(vertex: Int): Boolean = vertex >= 0 && vertex < vertexCount

    /**
     * The current union-find representative of [vertex], or `null` when it is outside the universe.
     * O(w log n) worst case.
     *
     * The representative is selected by deterministic union-by-size history and is an implementation
     * artifact: sibling versions need not agree on it.
     */
    public fun find(vertex: Int): Int? = if (containsVertex(vertex)) findRoot(vertex) else null

    /**
     * Whether two vertices have the same current root, or `null` when either is outside the universe.
     * O(w log n) worst case.
     */
    public fun connected(left: Int, right: Int): Boolean? =
        if (containsVertex(left) && containsVertex(right)) findRoot(left) == findRoot(right) else null

    /**
     * The number of vertices in [vertex]'s connected component, or `null` when it is outside the
     * universe. O(w log n) worst case.
     *
     * An isolated vertex is still a singleton root and reports `1`. The answer is the union-by-size
     * count cached at the component's current root, so the walk reads the structure without
     * compressing it; this forest deliberately has no path compression.
     */
    public fun componentSize(vertex: Int): Int? = if (containsVertex(vertex)) sizeOf(findRoot(vertex)) else null

    /**
     * A child history version containing an undirected connection between two vertices, or `null`
     * when either endpoint is outside the universe.
     *
     * The returned version is always distinct. When the endpoints were already connected its
     * connectivity cells are shared unchanged, which [sharesConnectivityRootWith] can confirm;
     * otherwise exactly one union-by-size root edge is added and labelled with the child version. On
     * a size tie the first endpoint's root stays the representative, making the result deterministic
     * without changing the logarithmic-height proof. Linking a vertex to itself is a redundant link.
     *
     * O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1) space
     * when redundant. The receiver is left untouched, including on rejection: both endpoints are
     * checked before anything is published.
     *
     * @throws ArithmeticException if the history depth would exceed [Long.MAX_VALUE].
     */
    public fun link(left: Int, right: Int): PersistentAncestralConnectionForest? {
        if (!containsVertex(left) || !containsVertex(right)) return null
        var representative = findRoot(left)
        var absorbed = findRoot(right)
        val childVersion = AncestralConnectionVersion.child(version)
        if (representative == absorbed) {
            return PersistentAncestralConnectionForest(vertexCount, componentCount, cells, childVersion)
        }

        var representativeSize = sizeOf(representative)
        var absorbedSize = sizeOf(absorbed)
        if (representativeSize < absorbedSize) {
            val swappedRoot = representative
            representative = absorbed
            absorbed = swappedRoot
            val swappedSize = representativeSize
            representativeSize = absorbedSize
            absorbedSize = swappedSize
        }

        val combinedSize = Math.addExact(representativeSize, absorbedSize)
        val nextCells = cells
            .put(representative, Cell.root(representative, combinedSize))
            .put(absorbed, Cell.child(representative, childVersion))
        return PersistentAncestralConnectionForest(vertexCount, componentCount - 1, nextCells, childVersion)
    }

    /**
     * The earliest version on this version's root-to-current history in which two vertices were
     * connected, or `null` when either vertex is outside the universe.
     *
     * A vertex is connected to itself at the history root. The answer is computed from the two
     * current parent paths as the latest union-edge version strictly below their forest lowest common
     * ancestor; the version history is never searched, so the cost is independent of history depth.
     * The result is never a version on another branch, and a later merge of the pair's component into
     * a bigger one never replaces the pair's earlier witness.
     *
     * O(w log n) worst-case time and O(log n) temporary path space.
     *
     * @throws IllegalStateException if a connected pair somehow carries no union-edge witness, which
     * a well-formed forest cannot produce.
     */
    public fun firstConnected(left: Int, right: Int): AncestralConnectionLookup? {
        if (!containsVertex(left) || !containsVertex(right)) return null
        if (left == right) return AncestralConnectionLookup.Found(version.root)

        val leftPath = buildPath(left)
        val rightPath = buildPath(right)
        if (leftPath[leftPath.size - 1].vertex != rightPath[rightPath.size - 1].vertex) {
            return AncestralConnectionLookup.Disconnected
        }

        // Delete the common rootward suffix. The remaining steps are precisely the two paths strictly
        // below the forest LCA, and every one of those steps owns a successful-union tag.
        var leftLast = leftPath.size - 1
        var rightLast = rightPath.size - 1
        while (leftLast >= 0 && rightLast >= 0 && leftPath[leftLast].vertex == rightPath[rightLast].vertex) {
            leftLast--
            rightLast--
        }

        var latest: AncestralConnectionVersion? = null
        for (index in 0..leftLast) latest = laterConnectionVersion(latest, leftPath[index].joinedAt)
        for (index in 0..rightLast) latest = laterConnectionVersion(latest, rightPath[index].joinedAt)

        // Distinct connected vertices have at least one edge strictly below their LCA.
        return AncestralConnectionLookup.Found(
            latest ?: error("A connected pair has no union-edge witness."),
        )
    }

    /**
     * Whether two versions retain the exact same sparse connectivity index, so neither can observe a
     * union recorded in the other. A representation test, not an equality test. O(1).
     *
     * It reports on the CHAMP index alone and says nothing about history identity: any two edgeless
     * versions share the one empty index, even when they belong to unrelated forests.
     */
    public fun sharesConnectivityRootWith(other: PersistentAncestralConnectionForest): Boolean =
        cells.sharesRootWith(other.cells)

    /**
     * Walk the version chain and every stored parent path and check the forest's invariants -
     * root consistency, sparse-cell canonicality, union-tag ancestry and strictly increasing order,
     * the union-by-size height ceiling `floor(log2 n)`, the component count, and the cached root
     * sizes - returning its shape measurements. A defensive audit for tests, not a routine operation.
     *
     * O(H + m w log n), where H is the history depth and m is the explicit-cell count. Both the
     * version chain and each parent path are walked iteratively, so a deep history is safe.
     *
     * @throws IllegalStateException on the first violation found.
     */
    public fun validateStructure(): PersistentAncestralConnectionForestStatistics {
        check(vertexCount >= 0 && componentCount >= 0 && componentCount <= vertexCount) {
            "Connection-forest counts are invalid."
        }

        val ancestors = java.util.IdentityHashMap<AncestralConnectionVersion, Unit>()
        var expectedDepth = version.depth
        var walked: AncestralConnectionVersion? = version
        while (walked != null) {
            check(walked.depth == expectedDepth && walked.root === version.root) {
                "The history-token parent chain is inconsistent."
            }
            expectedDepth--
            ancestors[walked] = Unit
            walked = walked.parent
        }
        val historyRoot = version.root
        check(
            expectedDepth == -1L &&
                ancestors.containsKey(historyRoot) &&
                historyRoot.parent == null &&
                historyRoot.depth == 0L,
        ) {
            "The history root is inconsistent."
        }

        for (entry in cells) {
            val cell = entry.value
            check(containsVertex(entry.key) && containsVertex(cell.parent)) {
                "A sparse parent cell addresses a vertex outside the universe."
            }

            if (cell.parent == entry.key) {
                check(cell.size > 1 && cell.joinedAt == null) {
                    "A stored root cell is not a canonical non-singleton root."
                }
            } else {
                val joinedAt = cell.joinedAt
                check(
                    cell.size == 0 &&
                        joinedAt != null &&
                        ancestors.containsKey(joinedAt) &&
                        cells.containsKey(cell.parent),
                ) {
                    "A non-root cell has an invalid size or union-version tag."
                }
            }
        }

        val componentSizes = HashMap<Int, Int>()
        val maximumHeight = if (vertexCount <= 1) 0 else 31 - Integer.numberOfLeadingZeros(vertexCount)
        var maximumParentPathLength = 0
        for (entry in cells) {
            var current = entry.key
            var height = 0
            var previousDepth = -1L
            while (true) {
                val cell = cellOf(current)
                if (cell.parent == current) break
                val joinedAt = cell.joinedAt
                check(joinedAt != null && joinedAt.depth > previousDepth && ancestors.containsKey(joinedAt)) {
                    "Union-edge versions do not strictly increase toward the root."
                }

                previousDepth = joinedAt.depth
                current = cell.parent
                height++
                check(height <= maximumHeight) { "A parent path exceeds the union-by-size height bound." }
            }

            if (height > maximumParentPathLength) maximumParentPathLength = height
            componentSizes[current] = (componentSizes[current] ?: 0) + 1
        }

        val storedCellCount = cells.size
        val absentSingletons = vertexCount.toLong() - storedCellCount.toLong()
        check(absentSingletons >= 0L && absentSingletons + componentSizes.size == componentCount.toLong()) {
            "The cached component count disagrees with the parent forest."
        }
        for ((root, size) in componentSizes) {
            check(sizeOf(root) == size) { "A root's cached union size is incorrect." }
        }

        return PersistentAncestralConnectionForestStatistics(
            vertexCount,
            componentCount,
            storedCellCount,
            maximumParentPathLength,
            version.depth,
        )
    }

    /** A shape-only rendering; it never walks the version chain or the parent forest. */
    override fun toString(): String =
        "PersistentAncestralConnectionForest(vertexCount=$vertexCount, " +
            "componentCount=$componentCount, depth=${version.depth})"

    private fun findRoot(vertex: Int): Int {
        var current = vertex
        while (true) {
            val parent = cells[current]?.parent ?: return current
            if (parent == current) return current
            current = parent
        }
    }

    private fun buildPath(vertex: Int): List<PathStep> {
        val path = mutableListOf<PathStep>()
        var current = vertex
        while (true) {
            val cell = cells[current]
            path.add(PathStep(current, cell?.joinedAt))
            val parent = cell?.parent ?: current
            if (parent == current) return path
            current = parent
        }
    }

    /** The cell for [vertex]; an absent cell is canonically its own singleton root. */
    private fun cellOf(vertex: Int): Cell = cells[vertex] ?: Cell.root(vertex, 1)

    /** The component size cached at the root [vertex]; an absent cell is a singleton. */
    private fun sizeOf(vertex: Int): Int = cells[vertex]?.size ?: 1
}
