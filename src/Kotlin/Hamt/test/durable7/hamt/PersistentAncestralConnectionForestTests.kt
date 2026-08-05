/*
 * Tests for the branching persistent ancestral-connection forest: witness selection from the two
 * current parent paths rather than a history search, distinct child versions for redundant links,
 * union-by-size with a first-endpoint tie-break, identity-distinct sibling version tokens, and
 * stack safety over a history hundreds of thousands of links deep.
 */
package durable7.hamt

import java.util.Collections
import java.util.IdentityHashMap

private typealias Forest = PersistentAncestralConnectionForest

private fun forestCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> forestCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> forestCheckThrows(message: String, action: () -> Unit) {
    try {
        action()
    } catch (error: Throwable) {
        if (error is T) {
            return
        }
        throw AssertionError("$message Expected ${T::class.simpleName}, got ${error::class.simpleName}.", error)
    }
    throw AssertionError("$message Expected ${T::class.simpleName}.")
}

private fun newForest(vertexCount: Int): Forest = Forest.create(vertexCount)

private fun Forest.linked(left: Int, right: Int): Forest =
    link(left, right) ?: throw AssertionError("link($left, $right) rejected an in-universe pair.")

private fun Forest.findOrFail(vertex: Int): Int =
    find(vertex) ?: throw AssertionError("find($vertex) rejected an in-universe vertex.")

private fun Forest.connectedOrFail(left: Int, right: Int): Boolean =
    connected(left, right) ?: throw AssertionError("connected($left, $right) rejected an in-universe pair.")

private fun Forest.sizeOrFail(vertex: Int): Int =
    componentSize(vertex) ?: throw AssertionError("componentSize($vertex) rejected an in-universe vertex.")

/** The first-connection witness, or `null` for a disconnected pair; an out-of-universe pair fails the test. */
private fun Forest.witness(left: Int, right: Int): AncestralConnectionVersion? {
    val lookup = firstConnected(left, right)
        ?: throw AssertionError("firstConnected($left, $right) rejected an in-universe pair.")
    return (lookup as? AncestralConnectionLookup.Found)?.version
}

/** A small deterministic generator, so a failure is reproducible without a captured seed. */
private class ForestRandom(private var state: Long) {
    fun next(bound: Int): Int {
        state = state * 6_364_136_223_846_793_005L + 1_442_695_040_888_963_407L
        return ((state ushr 33).toInt()) % bound
    }
}

private fun ancestralForestCreateProducesSingletonComponentsAndRootHistory() {
    val root = newForest(5)

    forestCheckEquals(5, root.vertexCount, "the root reports its fixed universe.")
    forestCheckEquals(5, root.componentCount, "every vertex starts as its own component.")
    forestCheckEquals(0L, root.version.depth, "the history root has depth zero.")
    forestCheck(root.version.parent == null, "the history root has no parent.")
    forestCheck(root.version.isSameVersion(root.version.root), "the history root is its own root identity.")
    for (vertex in 0 until root.vertexCount) {
        forestCheck(root.containsVertex(vertex), "a universe vertex is in range.")
        forestCheckEquals(vertex, root.findOrFail(vertex), "a singleton is its own representative.")
        forestCheck(root.connectedOrFail(vertex, vertex), "a vertex is connected to itself.")
        forestCheckEquals(1, root.sizeOrFail(vertex), "a singleton component holds one vertex.")
        forestCheck(root.witness(vertex, vertex) === root.version, "self-connection witnesses the history root.")
    }

    forestCheck(!root.containsVertex(-1) && !root.containsVertex(5), "the universe excludes its endpoints' neighbours.")
    forestCheck(!root.connectedOrFail(0, 1), "distinct singletons are disconnected.")
    forestCheckEquals(
        AncestralConnectionLookup.Disconnected,
        root.firstConnected(0, 1),
        "a disconnected in-universe pair reports Disconnected rather than null.",
    )

    val statistics = root.validateStructure()
    forestCheckEquals(0, statistics.storedCellCount, "an edgeless forest stores no cells.")
    forestCheckEquals(5, statistics.componentCount, "the audit recounts every component.")
    forestCheckEquals(0, statistics.maximumParentPathLength, "an edgeless forest has no parent edges.")
    forestCheckEquals(0L, statistics.historyDepth, "the audit reports the history depth.")
    forestCheckEquals(5, statistics.vertexCount, "the audit reports the universe size.")

    val empty = newForest(0)
    forestCheckEquals(0, empty.vertexCount, "an empty universe is representable.")
    forestCheckEquals(0, empty.componentCount, "an empty universe has no components.")
    forestCheck(!empty.containsVertex(0), "an empty universe contains nothing.")
    empty.validateStructure()

    forestCheckThrows<IllegalArgumentException>("a negative universe is a contract violation.") {
        newForest(-1)
    }
}

private fun ancestralForestLinkRecordsTheFirstConnectionVersion() {
    val root = newForest(6)
    val first = root.linked(0, 1)
    val second = first.linked(2, 3)
    val bridge = second.linked(0, 2)
    val attach = bridge.linked(4, 0)

    forestCheckEquals(2, attach.componentCount, "four unions over six vertices leave two components.")
    forestCheck(attach.witness(0, 1) === first.version, "the first pair's witness is its own link.")
    forestCheck(attach.witness(2, 3) === second.version, "the second pair's witness is its own link.")
    forestCheck(attach.witness(0, 3) === bridge.version, "a cross-component pair is witnessed by the bridge.")
    forestCheck(attach.witness(1, 2) === bridge.version, "the bridge witnesses every newly joined pair.")
    forestCheck(attach.witness(4, 1) === attach.version, "the attaching link witnesses its own pair.")
    forestCheck(attach.witness(4, 4) === root.version, "a self pair still witnesses the history root.")
    forestCheck(attach.witness(0, 5) == null, "an untouched vertex stays disconnected.")

    forestCheckEquals(0, attach.findOrFail(0), "union by size keeps the larger component's root.")
    forestCheckEquals(0, attach.findOrFail(4), "the attached singleton adopts the larger root.")

    // Every retained version keeps its own connectivity, untouched by later descendants.
    forestCheckEquals(6, root.componentCount, "the retained root still has six components.")
    forestCheck(!root.connectedOrFail(0, 1), "the retained root never saw the first union.")
    forestCheck(!first.connectedOrFail(0, 2), "the retained first version never saw the bridge.")
    forestCheckEquals(5, first.componentCount, "the retained first version keeps its own count.")
    for (retained in listOf(root, first, second, bridge, attach)) {
        retained.validateStructure()
    }
}

private fun ancestralForestRedundantLinksCreateVersionsAndShareConnectivityState() {
    val root = newForest(4)
    val linked = root.linked(0, 1)
    val reverseDuplicate = linked.linked(1, 0)
    val siblingDuplicate = linked.linked(1, 0)
    val selfDuplicate = reverseDuplicate.linked(0, 0)

    forestCheckEquals(1L, linked.version.depth, "the first link is one event deep.")
    forestCheckEquals(2L, reverseDuplicate.version.depth, "a redundant link still advances the history.")
    forestCheckEquals(3L, selfDuplicate.version.depth, "a self link still advances the history.")
    forestCheck(reverseDuplicate.version.parent === linked.version, "a child version points at its parent.")
    forestCheck(siblingDuplicate.version.parent === linked.version, "a sibling branches from the same parent.")
    forestCheck(selfDuplicate.version.parent === reverseDuplicate.version, "the chain follows the receiver.")
    forestCheck(!linked.version.isSameVersion(reverseDuplicate.version), "a redundant link publishes a distinct version.")
    forestCheck(
        !reverseDuplicate.version.isSameVersion(siblingDuplicate.version),
        "two redundant links off one parent are distinct versions.",
    )
    forestCheckEquals(
        linked.componentCount,
        reverseDuplicate.componentCount,
        "a redundant link does not change the component count.",
    )
    forestCheckEquals(linked.componentCount, selfDuplicate.componentCount, "a self link does not change the count.")
    forestCheck(linked.sharesConnectivityRootWith(reverseDuplicate), "a redundant link shares the connectivity index.")
    forestCheck(reverseDuplicate.sharesConnectivityRootWith(selfDuplicate), "a self link shares the connectivity index.")
    forestCheck(!root.sharesConnectivityRootWith(linked), "a successful union publishes a new connectivity index.")
    forestCheck(selfDuplicate.witness(0, 1) === linked.version, "redundant history does not move the witness.")
    forestCheck(selfDuplicate.witness(2, 2) === root.version, "a self pair witnesses the history root.")
    selfDuplicate.validateStructure()
}

private fun ancestralForestSiblingBranchesKeepEqualDepthVersionsDistinct() {
    val root = newForest(5)
    val common = root.linked(0, 1)
    val left = common.linked(0, 2)
    val right = common.linked(0, 3)
    val leftLater = left.linked(2, 4)
    val rightLater = right.linked(3, 4)

    forestCheckEquals(left.version.depth, right.version.depth, "sibling branches sit at one depth.")
    forestCheck(!left.version.isSameVersion(right.version), "equal depth in sibling branches is not equal identity.")
    forestCheckEquals(
        AncestralConnectionLookup.Found(left.version),
        left.firstConnected(0, 2),
        "a lookup carries the branch's own version.",
    )
    forestCheck(
        AncestralConnectionLookup.Found(left.version) != AncestralConnectionLookup.Found(right.version),
        "lookups over sibling branches are not equal, because the tokens are identity-distinct.",
    )
    forestCheck(left.witness(0, 1) === common.version, "both branches inherit the shared ancestor's witness.")
    forestCheck(right.witness(0, 1) === common.version, "both branches inherit the shared ancestor's witness.")
    forestCheck(leftLater.witness(1, 2) === left.version, "the left branch keeps its own witness.")
    forestCheck(rightLater.witness(1, 3) === right.version, "the right branch keeps its own witness.")
    forestCheck(leftLater.witness(0, 4) === leftLater.version, "the left branch's newest link witnesses its own pair.")
    forestCheck(rightLater.witness(0, 4) === rightLater.version, "the right branch's newest link witnesses its own pair.")
    forestCheck(!leftLater.connectedOrFail(0, 3), "the left branch never saw the right branch's union.")
    forestCheck(!rightLater.connectedOrFail(0, 2), "the right branch never saw the left branch's union.")
    forestCheck(leftLater.version.root.isSameVersion(root.version), "every branch shares one history root.")
    forestCheck(rightLater.version.root.isSameVersion(root.version), "every branch shares one history root.")
    leftLater.validateStructure()
    rightLater.validateStructure()
}

private fun ancestralForestFirstConnectedUsesPairLcaRatherThanLatestComponentBirth() {
    val root = newForest(8)
    val ab = root.linked(0, 1)
    val cd = ab.linked(2, 3)
    val abcd = cd.linked(1, 3)
    val ef = abcd.linked(4, 5)
    val gh = ef.linked(6, 7)
    val efgh = gh.linked(4, 6)
    val all = efgh.linked(0, 4)

    forestCheck(all.witness(0, 1) === ab.version, "a pair's witness survives every later merge.")
    forestCheck(all.witness(2, 3) === cd.version, "a pair's witness survives every later merge.")
    forestCheck(all.witness(0, 2) === abcd.version, "the witness is the pair's own join, not the component's birth.")
    forestCheck(all.witness(4, 5) === ef.version, "a later-built component keeps its own early witnesses.")
    forestCheck(all.witness(6, 7) === gh.version, "a later-built component keeps its own early witnesses.")
    forestCheck(all.witness(5, 7) === efgh.version, "the second half's bridge witnesses its cross pairs.")
    forestCheck(all.witness(1, 6) === all.version, "the final bridge witnesses only the pairs it first joined.")
    all.validateStructure()
}

private fun ancestralForestFirstConnectedHandlesAnEndpointAtTheLcaAfterItBecomesAChild() {
    val root = newForest(5)
    val pair = root.linked(0, 1)
    val otherPair = pair.linked(2, 3)
    val larger = otherPair.linked(2, 4)
    val merged = larger.linked(2, 0)

    forestCheckEquals(2, merged.findOrFail(0), "the smaller component's root becomes a child.")
    forestCheck(merged.witness(0, 1) === pair.version, "a demoted root keeps the pair's original witness.")
    forestCheck(merged.witness(0, 2) === merged.version, "one endpoint sitting at the LCA is still handled.")
    forestCheck(merged.witness(1, 4) === merged.version, "the merging link witnesses the newly joined pair.")
    merged.validateStructure()
}

private fun ancestralForestBalancedUnionsRespectTheHeightBoundAndRetainWitnesses() {
    val count = 1_024
    val root = newForest(count)
    var current = root
    var firstPairVersion: AncestralConnectionVersion? = null
    var finalBridgeVersion: AncestralConnectionVersion? = null

    var width = 1
    while (width < count) {
        var start = 0
        while (start < count) {
            current = current.linked(start, start + width)
            if (start == 0 && width == 1) firstPairVersion = current.version
            if (start == 0 && width == count / 2) finalBridgeVersion = current.version
            start += width * 2
        }
        width *= 2
    }

    forestCheckEquals(1, current.componentCount, "a balanced union tree ends with one component.")
    forestCheckEquals(0, current.findOrFail(count - 1), "the first endpoint's root wins every size tie.")
    forestCheck(current.witness(0, 1) === firstPairVersion, "the earliest pair keeps the earliest witness.")
    forestCheck(current.witness(0, count - 1) === finalBridgeVersion, "the widest pair is witnessed by the last bridge.")
    forestCheckEquals(count, root.componentCount, "the retained root is untouched by 1023 descendants.")

    val statistics = current.validateStructure()
    forestCheckEquals(count, statistics.storedCellCount, "every vertex now owns an explicit cell.")
    forestCheckEquals(1, statistics.componentCount, "the audit recounts one component.")
    forestCheckEquals(10, statistics.maximumParentPathLength, "union by size holds the path at floor(log2 1024).")
    forestCheckEquals((count - 1).toLong(), statistics.historyDepth, "1023 links produce depth 1023.")
}

private class ForestModelVersion(
    val actual: Forest,
    val classes: IntArray,
    val parent: Int?,
)

private fun firstConnectedByScan(
    states: List<ForestModelVersion>,
    index: Int,
    left: Int,
    right: Int,
): AncestralConnectionVersion? {
    val lineage = mutableListOf<Int>()
    var current: Int? = index
    while (current != null) {
        lineage.add(current)
        current = states[current].parent
    }

    for (position in lineage.asReversed()) {
        val candidate = states[position]
        if (candidate.classes[left] == candidate.classes[right]) {
            return candidate.actual.version
        }
    }

    return null
}

private fun ancestralForestRandomBranchingHistoriesMatchTheNaiveAncestorScan() {
    val vertexCount = 12
    val random = ForestRandom(0x5EED_2026L)
    val states = mutableListOf(
        ForestModelVersion(newForest(vertexCount), IntArray(vertexCount) { it }, null),
    )

    for (operation in 0 until 250) {
        val parentIndex = random.next(states.size)
        val left = random.next(vertexCount)
        val right = random.next(vertexCount)
        val classes = states[parentIndex].classes.copyOf()
        val leftClass = classes[left]
        val rightClass = classes[right]
        if (leftClass != rightClass) {
            for (position in classes.indices) {
                if (classes[position] == rightClass) classes[position] = leftClass
            }
        }

        val actual = states[parentIndex].actual.linked(left, right)
        forestCheck(
            actual.version.parent === states[parentIndex].actual.version,
            "every link branches from the receiver's version.",
        )
        forestCheckEquals(
            states[parentIndex].actual.version.depth + 1,
            actual.version.depth,
            "every link is exactly one event deeper.",
        )
        forestCheckEquals(classes.toSortedSet().size, actual.componentCount, "the component count tracks the model.")

        val index = states.size
        states.add(ForestModelVersion(actual, classes, parentIndex))

        val state = states[index]
        for (queryLeft in 0 until vertexCount) {
            for (queryRight in 0 until vertexCount) {
                forestCheckEquals(
                    state.classes[queryLeft] == state.classes[queryRight],
                    state.actual.connectedOrFail(queryLeft, queryRight),
                    "connectivity tracks the model.",
                )
                forestCheck(
                    state.actual.witness(queryLeft, queryRight) ===
                        firstConnectedByScan(states, index, queryLeft, queryRight),
                    "the path-derived witness equals the naive ancestor scan.",
                )
            }
        }

        if (operation % 25 == 0) state.actual.validateStructure()
    }

    // Recheck retained versions after every sibling branch has been constructed.
    repeat(80) {
        val index = random.next(states.size)
        val left = random.next(vertexCount)
        val right = random.next(vertexCount)
        forestCheck(
            states[index].actual.witness(left, right) === firstConnectedByScan(states, index, left, right),
            "a retained version still matches the naive ancestor scan.",
        )
    }
}

private fun ancestralForestDeepRedundantHistoryStaysStackSafeAndKeepsItsWitness() {
    val depth = 50_000
    val root = newForest(3)
    val first = root.linked(0, 1)
    var current = first
    repeat(depth) { current = current.linked(2, 2) }

    forestCheckEquals((depth + 1).toLong(), current.version.depth, "every redundant link advances the history.")
    forestCheck(current.witness(0, 1) === first.version, "a long redundant history never replaces the witness.")
    forestCheck(current.witness(0, 2) == null, "redundant self links never connect anything.")
    forestCheck(first.sharesConnectivityRootWith(current), "the whole redundant run shares one connectivity index.")

    // The query and the audit both walk the version chain iteratively, so a chain far deeper than the
    // JVM's recursion limit must not overflow the stack.
    val statistics = current.validateStructure()
    forestCheckEquals((depth + 1).toLong(), statistics.historyDepth, "the audit reports the deep history depth.")
    forestCheckEquals(2, statistics.storedCellCount, "only the two linked vertices own cells.")
    forestCheckEquals("AncestralConnectionVersion(depth=${depth + 1})", current.version.toString(),
        "a version renders its depth without walking its parents.")
}

private fun ancestralForestHugeSparseUniverseStoresOnlyTouchedVertices() {
    val root = newForest(Int.MAX_VALUE)
    forestCheckEquals(0, root.validateStructure().storedCellCount, "a huge edgeless universe stores nothing.")

    val linked = root.linked(0, Int.MAX_VALUE - 1)
    forestCheckEquals(Int.MAX_VALUE - 1, linked.componentCount, "one union over a huge universe removes one component.")
    forestCheck(linked.connectedOrFail(0, Int.MAX_VALUE - 1), "the linked pair is connected.")
    forestCheck(!linked.connectedOrFail(0, Int.MAX_VALUE - 2), "an untouched vertex stays isolated.")
    forestCheckEquals(Int.MAX_VALUE - 2, linked.findOrFail(Int.MAX_VALUE - 2), "an untouched vertex is its own root.")
    forestCheckEquals(1, linked.sizeOrFail(Int.MAX_VALUE - 2), "an untouched vertex is a singleton.")
    forestCheck(linked.witness(0, Int.MAX_VALUE - 1) === linked.version, "the union witnesses its own pair.")
    forestCheck(!linked.containsVertex(Int.MAX_VALUE), "the universe stops one short of Int.MAX_VALUE.")

    val statistics = linked.validateStructure()
    forestCheckEquals(2, statistics.storedCellCount, "only the two touched vertices own cells.")
    forestCheckEquals(1, statistics.maximumParentPathLength, "one union produces one parent edge.")
}

private fun ancestralForestOperationsRejectVerticesOutsideTheUniverse() {
    val root = newForest(3)

    forestCheck(root.find(-1) == null && root.find(3) == null, "find rejects an out-of-universe vertex.")
    forestCheck(root.connected(-1, 0) == null && root.connected(0, 3) == null, "connected rejects either endpoint.")
    forestCheck(root.link(-1, 0) == null && root.link(0, 3) == null, "link rejects either endpoint.")
    forestCheck(root.componentSize(-1) == null && root.componentSize(3) == null, "componentSize rejects out of range.")
    forestCheck(
        root.firstConnected(-1, 0) == null && root.firstConnected(0, 3) == null,
        "firstConnected reports an out-of-universe vertex as null.",
    )

    // The two failure modes stay distinguishable: null is a rejected argument, Disconnected is a
    // valid pair with no answer.
    forestCheckEquals(
        AncestralConnectionLookup.Disconnected,
        root.firstConnected(0, 1),
        "a valid disconnected pair is Disconnected, not null.",
    )
    forestCheck(!root.containsVertex(-1) && !root.containsVertex(3), "containsVertex disambiguates the two modes.")

    // A rejected operation publishes nothing.
    forestCheck(root.version.isSameVersion(root.version.root), "a rejected link leaves the version untouched.")
    forestCheckEquals(0L, root.version.depth, "a rejected link does not advance the history.")
    forestCheckEquals(3, root.componentCount, "a rejected link does not change the component count.")
    root.validateStructure()
}

/** Sums one cached size per distinct current root, which must recover the whole universe. */
private fun totalComponentSize(forest: Forest): Int {
    val roots = HashMap<Int, Int>()
    for (vertex in 0 until forest.vertexCount) {
        roots[forest.findOrFail(vertex)] = forest.sizeOrFail(vertex)
    }

    forestCheckEquals(forest.componentCount, roots.size, "distinct roots agree with the cached component count.")
    return roots.values.sum()
}

private fun ancestralForestComponentSizeReportsTheCachedUnionSize() {
    val root = newForest(6)
    for (vertex in 0 until root.vertexCount) {
        forestCheckEquals(1, root.sizeOrFail(vertex), "an isolated vertex is a singleton.")
    }
    forestCheckEquals(6, totalComponentSize(root), "the singleton sizes recover the universe.")

    val pair = root.linked(0, 1)
    val triple = pair.linked(1, 2)
    val quad = triple.linked(2, 3)

    forestCheckEquals(2, pair.sizeOrFail(0), "a union grows exactly the touched component.")
    forestCheckEquals(2, pair.sizeOrFail(1), "both endpoints agree on the size.")
    forestCheckEquals(1, pair.sizeOrFail(5), "an untouched vertex keeps its own size.")
    forestCheckEquals(3, triple.sizeOrFail(2), "a chained union keeps growing the component.")
    for (vertex in 0 until 4) {
        forestCheckEquals(4, quad.sizeOrFail(vertex), "every member of a component agrees on its size.")
    }
    forestCheckEquals(1, quad.sizeOrFail(4), "an untouched vertex is unaffected.")
    forestCheckEquals(6, totalComponentSize(quad), "the cached sizes still recover the universe.")

    val redundant = quad.linked(3, 0)
    for (vertex in 0 until quad.vertexCount) {
        forestCheckEquals(quad.sizeOrFail(vertex), redundant.sizeOrFail(vertex), "a redundant link changes no size.")
    }
    forestCheckEquals(6, totalComponentSize(redundant), "a redundant link preserves the size sum.")

    val branch = quad.linked(4, 5)
    forestCheckEquals(2, branch.sizeOrFail(4), "a branch grows only its own component.")
    forestCheckEquals(1, quad.sizeOrFail(4), "the retained parent keeps its own sizes.")
    forestCheckEquals(1, pair.sizeOrFail(2), "an older retained version keeps its own sizes.")
    forestCheckEquals(1, root.sizeOrFail(0), "the history root keeps its own sizes.")
    forestCheckEquals(6, totalComponentSize(branch), "the branch's sizes recover the universe.")
    forestCheckEquals(6, totalComponentSize(pair), "an older version's sizes recover the universe.")

    val merged = branch.linked(5, 0)
    forestCheckEquals(1, merged.componentCount, "the final union leaves one component.")
    for (vertex in 0 until merged.vertexCount) {
        forestCheckEquals(6, merged.sizeOrFail(vertex), "one component holds the whole universe.")
    }
    merged.validateStructure()

    val sparse = newForest(Int.MAX_VALUE).linked(0, Int.MAX_VALUE - 1)
    forestCheckEquals(2, sparse.sizeOrFail(0), "a huge universe still caches the union size.")
    forestCheckEquals(2, sparse.sizeOrFail(Int.MAX_VALUE - 1), "both endpoints agree in a huge universe.")
    forestCheckEquals(1, sparse.sizeOrFail(Int.MAX_VALUE - 2), "an unstored singleton still reports one.")
}

private fun ancestralForestSeparateForestsHaveSeparateVersionTrees() {
    val leftRoot = newForest(2)
    val rightRoot = newForest(2)
    val left = leftRoot.linked(0, 1)
    val right = rightRoot.linked(0, 1)

    forestCheck(!leftRoot.version.isSameVersion(rightRoot.version), "two forests have distinct history roots.")
    forestCheck(!left.version.isSameVersion(right.version), "two forests have distinct child versions.")
    forestCheckEquals(left.version.depth, right.version.depth, "equal histories reach equal depths.")
    forestCheck(left.witness(0, 1) === left.version, "each forest witnesses with its own version.")
    forestCheck(right.witness(0, 1) === right.version, "each forest witnesses with its own version.")
    forestCheck(left.witness(0, 1) !== right.witness(0, 1), "identity keeps structurally identical histories apart.")
    forestCheck(
        AncestralConnectionLookup.Found(left.version) != AncestralConnectionLookup.Found(right.version),
        "a lookup wrapping identity-distinct versions is not equal.",
    )
    // sharesConnectivityRootWith is a representation test on the CHAMP index alone, and every
    // edgeless forest has the same empty index, so it says nothing about history identity.
    forestCheck(leftRoot.sharesConnectivityRootWith(rightRoot), "two edgeless forests share the empty index.")
    forestCheck(!left.sharesConnectivityRootWith(right), "two separately built unions do not share an index.")
}

private fun ancestralForestSuccessfulUnionRebuildsOnlyTheTouchedCells() {
    val root = newForest(16)
    val first = root.linked(0, 1)
    val second = first.linked(2, 3)

    forestCheck(!first.sharesConnectivityRootWith(second), "a successful union publishes a new connectivity index.")
    forestCheck(second.sharesConnectivityRootWith(second.linked(0, 1)), "a redundant link shares the index.")
    forestCheck(second.sharesConnectivityRootWith(second.linked(3, 2)), "a reversed redundant link shares the index.")

    // Union by size: on a tie the first endpoint's root stays the representative.
    forestCheckEquals(0, second.findOrFail(1), "a size tie keeps the first endpoint's root.")
    forestCheckEquals(2, second.findOrFail(3), "a size tie keeps the first endpoint's root.")
    val merged = second.linked(3, 1)
    forestCheckEquals(2, merged.findOrFail(0), "the tie-break follows the argument order, not the vertex order.")
    forestCheckEquals(13, merged.componentCount, "three unions over sixteen vertices leave thirteen components.")
    merged.validateStructure()

    // The larger component always absorbs the smaller one, whichever side it is passed on.
    val leftBiased = merged.linked(4, 0)
    val rightBiased = merged.linked(0, 4)
    forestCheckEquals(2, leftBiased.findOrFail(4), "a singleton passed first still joins the larger root.")
    forestCheckEquals(2, rightBiased.findOrFail(4), "a singleton passed second still joins the larger root.")
}

private fun ancestralForestLaterTieRuleKeepsTheLeftCandidate() {
    val root = newForest(6)
    val first = root.linked(0, 1)
    val second = first.linked(2, 3)
    val sibling = first.linked(2, 3)

    forestCheckEquals(second.version.depth, sibling.version.depth, "sibling branches share one depth.")
    forestCheck(!second.version.isSameVersion(sibling.version), "sibling versions stay identity-distinct.")

    forestCheck(laterConnectionVersion(null, null) == null, "no candidate yields no witness.")
    forestCheck(laterConnectionVersion(second.version, null) === second.version, "an absent right keeps the left.")
    forestCheck(laterConnectionVersion(null, second.version) === second.version, "an absent left takes the right.")
    forestCheck(laterConnectionVersion(first.version, second.version) === second.version, "a deeper right wins.")
    forestCheck(laterConnectionVersion(second.version, first.version) === second.version, "a shallower right loses.")
    forestCheck(laterConnectionVersion(second.version, sibling.version) === second.version, "an equal depth keeps the left.")
    forestCheck(
        laterConnectionVersion(sibling.version, second.version) === sibling.version,
        "the tie rule keeps the left candidate whichever equal-depth version arrives first.",
    )

    // The tie branch is unreachable through the public API: every link tags at most one edge, so all
    // union tags inside one forest are distinct versions occupying distinct depths.
    val vertexCount = 32
    val random = ForestRandom(0x1EAF_2026L)
    var current = newForest(vertexCount)
    repeat(120) { current = current.linked(random.next(vertexCount), random.next(vertexCount)) }
    current.validateStructure()

    val identities = IdentityHashMap<AncestralConnectionVersion, Unit>()
    val depths = HashSet<Long>()
    for (left in 0 until vertexCount) {
        for (right in 0 until vertexCount) {
            if (left == right) continue
            val witness = current.witness(left, right) ?: continue
            identities[witness] = Unit
            depths.add(witness.depth)
        }
    }
    forestCheck(identities.isNotEmpty(), "a random history produces union witnesses.")
    forestCheckEquals(identities.size, depths.size, "distinct union tags occupy distinct depths, so ties cannot arise.")
}

private fun ancestralForestValidatorDetectsCorruptedCachedCounts() {
    val healthy = newForest(4).linked(0, 1)
    healthy.validateStructure()

    val corrupted = newForest(4).linked(0, 1)
    val field = corrupted.javaClass.getDeclaredField("componentCount")
    forestCheck(field.trySetAccessible(), "component-count reflection is available to the local corruption test.")
    field.setInt(corrupted, 4)
    forestCheckThrows<IllegalStateException>("the audit rejects a cached count that disagrees with the forest.") {
        corrupted.validateStructure()
    }

    val impossible = newForest(4)
    val countField = impossible.javaClass.getDeclaredField("componentCount")
    forestCheck(countField.trySetAccessible(), "component-count reflection is available to the local corruption test.")
    countField.setInt(impossible, 5)
    forestCheckThrows<IllegalStateException>("the audit rejects a component count above the universe size.") {
        impossible.validateStructure()
    }
}

private fun ancestralForestSnapshotsAreSafeForConcurrentReaders() {
    val vertexCount = 64
    var current = newForest(vertexCount)
    val checkpoints = mutableListOf<Pair<Forest, AncestralConnectionVersion>>()
    for (step in 0 until 32) {
        current = current.linked(2 * step, 2 * step + 1)
        checkpoints.add(current to current.version)
    }

    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val threads = (0 until 4).map { worker ->
        Thread {
            try {
                repeat(20) {
                    for ((step, checkpoint) in checkpoints.withIndex()) {
                        val (snapshot, expected) = checkpoint
                        forestCheck(snapshot.version === expected, "a retained version keeps its own token.")
                        forestCheckEquals(
                            vertexCount - (step + 1),
                            snapshot.componentCount,
                            "a retained version keeps its own component count.",
                        )
                        forestCheck(snapshot.connectedOrFail(2 * step, 2 * step + 1), "a retained union stays visible.")
                        forestCheck(snapshot.witness(2 * step, 2 * step + 1) === expected, "a retained witness is stable.")
                        forestCheck(!snapshot.connectedOrFail(0, 63), "unlinked vertices stay apart in every version.")
                        if (step % 8 == worker % 8) snapshot.validateStructure()
                    }
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }.apply { name = "ancestral-forest-reader-$worker" }
    }

    threads.forEach { it.start() }
    threads.forEach { it.join() }
    if (failures.isNotEmpty()) {
        throw AssertionError("concurrent connection-forest readers had ${failures.size} failure(s)", failures[0])
    }
}

/** The ancestral-connection forest's test cases, each a name paired with its assertion body. */
internal fun persistentAncestralConnectionForestTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "ancestralForestCreateProducesSingletonComponentsAndRootHistory" to
        ::ancestralForestCreateProducesSingletonComponentsAndRootHistory,
    "ancestralForestLinkRecordsTheFirstConnectionVersion" to
        ::ancestralForestLinkRecordsTheFirstConnectionVersion,
    "ancestralForestRedundantLinksCreateVersionsAndShareConnectivityState" to
        ::ancestralForestRedundantLinksCreateVersionsAndShareConnectivityState,
    "ancestralForestSiblingBranchesKeepEqualDepthVersionsDistinct" to
        ::ancestralForestSiblingBranchesKeepEqualDepthVersionsDistinct,
    "ancestralForestFirstConnectedUsesPairLcaRatherThanLatestComponentBirth" to
        ::ancestralForestFirstConnectedUsesPairLcaRatherThanLatestComponentBirth,
    "ancestralForestFirstConnectedHandlesAnEndpointAtTheLcaAfterItBecomesAChild" to
        ::ancestralForestFirstConnectedHandlesAnEndpointAtTheLcaAfterItBecomesAChild,
    "ancestralForestBalancedUnionsRespectTheHeightBoundAndRetainWitnesses" to
        ::ancestralForestBalancedUnionsRespectTheHeightBoundAndRetainWitnesses,
    "ancestralForestRandomBranchingHistoriesMatchTheNaiveAncestorScan" to
        ::ancestralForestRandomBranchingHistoriesMatchTheNaiveAncestorScan,
    "ancestralForestDeepRedundantHistoryStaysStackSafeAndKeepsItsWitness" to
        ::ancestralForestDeepRedundantHistoryStaysStackSafeAndKeepsItsWitness,
    "ancestralForestHugeSparseUniverseStoresOnlyTouchedVertices" to
        ::ancestralForestHugeSparseUniverseStoresOnlyTouchedVertices,
    "ancestralForestOperationsRejectVerticesOutsideTheUniverse" to
        ::ancestralForestOperationsRejectVerticesOutsideTheUniverse,
    "ancestralForestComponentSizeReportsTheCachedUnionSize" to
        ::ancestralForestComponentSizeReportsTheCachedUnionSize,
    "ancestralForestSeparateForestsHaveSeparateVersionTrees" to
        ::ancestralForestSeparateForestsHaveSeparateVersionTrees,
    "ancestralForestSuccessfulUnionRebuildsOnlyTheTouchedCells" to
        ::ancestralForestSuccessfulUnionRebuildsOnlyTheTouchedCells,
    "ancestralForestLaterTieRuleKeepsTheLeftCandidate" to
        ::ancestralForestLaterTieRuleKeepsTheLeftCandidate,
    "ancestralForestValidatorDetectsCorruptedCachedCounts" to
        ::ancestralForestValidatorDetectsCorruptedCachedCounts,
    "ancestralForestSnapshotsAreSafeForConcurrentReaders" to
        ::ancestralForestSnapshotsAreSafeForConcurrentReaders,
)
