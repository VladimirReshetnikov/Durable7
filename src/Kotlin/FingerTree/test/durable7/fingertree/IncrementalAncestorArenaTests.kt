/*
 * Tests for the incremental level-ancestor arena itself, rather than for the collections layered on
 * it. The properties under test are that the Myers jump links really keep an ancestor query
 * logarithmic instead of merely correct, that the odd-block directory allocates exactly at the
 * square boundaries it claims, that the traversal counters describe each query rather than a running
 * sum, and that every primitive rejects a handle or depth the arena never published without
 * disturbing the arena it rejected the call on.
 */
package durable7.fingertree

import java.util.Random

private fun arenaCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> arenaCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> arenaCheckThrows(message: String, action: () -> Unit) {
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

/** The least exponent whose power of two reaches the value, computed without floating point. */
private fun arenaCeilingLog2(value: Int): Int {
    var power = 1L
    var result = 0
    while (power < value) {
        power = power shl 1
        result++
    }
    return result
}

/** The floor of the square root, computed the way the arena's own block addressing computes it. */
private fun arenaIntegerSquareRoot(value: Int): Int {
    var root = Math.sqrt(value.toDouble()).toInt()
    while ((root + 1).toLong() * (root + 1).toLong() <= value.toLong()) {
        root++
    }
    while (root.toLong() * root.toLong() > value.toLong()) {
        root--
    }
    return root
}

/**
 * Runs one ancestor query and returns its answer together with the hops it actually performed.
 *
 * The hop count is read as the delta of [MyersIncrementalAncestorStatistics.totalAncestorHopCount],
 * which the arena maintains independently of
 * [MyersIncrementalAncestorStatistics.lastAncestorHopCount], so the equality checked here pins the
 * latter to the work this single query did rather than to any accumulated total.
 */
private fun <T> arenaAncestorWithMeasuredHops(
    arena: MyersIncrementalAncestorArena<T>,
    node: Int,
    depth: Int,
): Pair<Int, Int> {
    val before = arena.statistics()
    val ancestor = arena.ancestorAtDepth(node, depth)
    val after = arena.statistics()

    val hops = (after.totalAncestorHopCount - before.totalAncestorHopCount).toInt()
    arenaCheckEquals(before.ancestorQueryCount + 1L, after.ancestorQueryCount, "The measured query count.")
    arenaCheckEquals(hops, after.lastAncestorHopCount, "The last hop count must report this query's own hops.")
    arenaCheck(
        after.maximumAncestorHopCount >= hops,
        "The maximum hop count ${after.maximumAncestorHopCount} must cover the $hops hops just made.",
    )
    return ancestor to hops
}

/**
 * Guards the Myers backend against silently degenerating into a linear parent walk.
 *
 * Every answer a coalescing-free arena returns is still correct, so only a hop bound can tell the
 * two apart. The bound asserted here is `4 * ceil(log2(M + 1))` over `M` published nodes: its
 * *shape* is what discriminates. Myers' two-link scheme reaches any ancestor in O(log d) hops for a
 * travelled distance `d <= M`, and the factor of four is slack for the exact constant rather than a
 * second parameter, so the bound stays Theta(log M). For the 32,768-node chain below it evaluates to
 * 64, while a plain parent walk from the tail to the bottom node costs 32,768 hops - three orders of
 * magnitude past the bound - so an arena that stopped coalescing its jump links could not satisfy it
 * at any chain length worth testing.
 */
private fun arenaDeepChainQueriesUseConservativelyLogarithmicHops() {
    val nodeCount = 32_768
    val arena = MyersIncrementalAncestorArena<Int>()
    val nodesByDepth = IntArray(nodeCount + 1)
    nodesByDepth[0] = arena.bottom

    var tail = arena.bottom
    for (value in 0 until nodeCount) {
        tail = arena.addLeaf(tail, value)
        nodesByDepth[value + 1] = tail
    }

    val afterAdds = arena.statistics()
    arenaCheckEquals(nodeCount, afterAdds.publishedNodeCount, "The chain's published node count.")
    arenaCheckEquals(nodeCount.toLong(), afterAdds.addLeafCount, "The chain's leaf addition count.")
    arenaCheckEquals(nodeCount - 1, arena.depthOf(tail), "The chain tail depth.")
    arenaCheckEquals(0L, afterAdds.ancestorQueryCount, "Building a chain must query no ancestor.")
    arenaCheckEquals(0, afterAdds.lastAncestorHopCount, "Building a chain must record no last hop.")
    arenaCheckEquals(0, afterAdds.maximumAncestorHopCount, "Building a chain must record no maximum hop.")
    arenaCheckEquals(0L, afterAdds.totalAncestorHopCount, "Building a chain must record no total hops.")

    for (targetDepth in nodeCount - 1 downTo -1) {
        arenaCheckEquals(
            nodesByDepth[targetDepth + 1],
            arena.ancestorAtDepth(tail, targetDepth),
            "The tail's ancestor at depth $targetDepth.",
        )
    }

    val afterQueries = arena.statistics()
    val conservativeLogarithmicBound = 4 * arenaCeilingLog2(nodeCount + 1)
    arenaCheckEquals(nodeCount + 1L, afterQueries.ancestorQueryCount, "The completed query count.")
    arenaCheck(afterQueries.maximumAncestorHopCount >= 1, "A walk off the tail must cost at least one hop.")
    arenaCheck(
        afterQueries.maximumAncestorHopCount <= conservativeLogarithmicBound,
        "The maximum hop count ${afterQueries.maximumAncestorHopCount} exceeded the conservative " +
            "logarithmic bound $conservativeLogarithmicBound, so the jump links no longer coalesce.",
    )
    arenaCheck(afterQueries.totalAncestorHopCount >= 1, "The queries must record at least one hop in total.")
    arenaCheck(
        afterQueries.totalAncestorHopCount <= (nodeCount + 1L) * conservativeLogarithmicBound,
        "The total hop count ${afterQueries.totalAncestorHopCount} exceeded the conservative " +
            "logarithmic budget ${(nodeCount + 1L) * conservativeLogarithmicBound}.",
    )
    arenaCheck(
        afterQueries.lastAncestorHopCount <= conservativeLogarithmicBound,
        "The final query's hop count ${afterQueries.lastAncestorHopCount} exceeded the conservative " +
            "logarithmic bound $conservativeLogarithmicBound.",
    )
}

/** Verifies the last-hop counter reports only the most recent query rather than accumulating. */
private fun arenaLastHopCountReportsOnlyTheMostRecentQuery() {
    val chainLength = 1_024
    val arena = MyersIncrementalAncestorArena<Int>()
    var tail = arena.bottom
    for (value in 0 until chainLength) {
        tail = arena.addLeaf(tail, value)
    }

    val tailDepth = arena.depthOf(tail)
    arenaCheckEquals(chainLength - 1, tailDepth, "The chain tail depth.")
    arenaCheckEquals(0, arena.statistics().lastAncestorHopCount, "An unqueried arena reports no last hops.")

    // A query for the node itself terminates before the first hop.
    val (selfAncestor, selfHops) = arenaAncestorWithMeasuredHops(arena, tail, tailDepth)
    arenaCheckEquals(tail, selfAncestor, "The tail is its own ancestor at its own depth.")
    arenaCheckEquals(0, selfHops, "A self query makes no hop.")

    // A query for the immediate parent costs exactly one hop: whichever link the arena picks for a
    // remaining distance of one lands on the parent, because a jump of distance one is the parent.
    val (parentAncestor, parentHops) = arenaAncestorWithMeasuredHops(arena, tail, tailDepth - 1)
    arenaCheckEquals(arena.parentOf(tail), parentAncestor, "The tail's ancestor one level up is its parent.")
    arenaCheckEquals(1, parentHops, "A parent query costs exactly one hop.")
    arenaCheckEquals(1, arena.statistics().lastAncestorHopCount, "The last hop count follows the parent query.")

    // A full walk to the bottom node costs more than one hop, and the counter reports that query's
    // own cost rather than a running sum of the queries before it.
    val (bottomAncestor, bottomHops) = arenaAncestorWithMeasuredHops(arena, tail, -1)
    arenaCheckEquals(arena.bottom, bottomAncestor, "The ancestor at depth -1 is the bottom node.")
    arenaCheck(bottomHops > 1, "A walk from a deep tail to the bottom node costs more than one hop.")

    // Repeating the zero-hop query drives the counter back down, so it is replaced by each query
    // rather than accumulated across queries.
    val (repeatedAncestor, repeatedHops) = arenaAncestorWithMeasuredHops(arena, tail, tailDepth)
    arenaCheckEquals(tail, repeatedAncestor, "The repeated self query answers the tail.")
    arenaCheckEquals(0, repeatedHops, "The repeated self query makes no hop.")

    val statistics = arena.statistics()
    arenaCheckEquals(0, statistics.lastAncestorHopCount, "The last hop count never accumulates.")
    arenaCheckEquals(bottomHops, statistics.maximumAncestorHopCount, "The maximum hop count keeps the deepest walk.")
    arenaCheckEquals(
        (selfHops + parentHops + bottomHops + repeatedHops).toLong(),
        statistics.totalAncestorHopCount,
        "The total hop count sums every query.",
    )
    arenaCheckEquals(4L, statistics.ancestorQueryCount, "The completed query count.")
}

/** Verifies the odd-block directory allocates exactly at the labelled-node square boundaries. */
private fun arenaOddBlockStatisticsMatchTheExactSquareLayout() {
    val maximumPublishedCount = 4_096
    val arena = MyersIncrementalAncestorArena<Int>()
    var parent = arena.bottom

    for (publishedCount in 0..maximumPublishedCount) {
        val statistics = arena.statistics()
        val highestBlock = arenaIntegerSquareRoot(publishedCount)
        val expectedBlockCount = highestBlock + 1
        val expectedSlotCount = expectedBlockCount.toLong() * expectedBlockCount.toLong()

        arenaCheckEquals(publishedCount, arena.publishedNodeCount, "The published node count at $publishedCount.")
        arenaCheckEquals(
            publishedCount,
            statistics.publishedNodeCount,
            "The reported published node count at $publishedCount.",
        )
        arenaCheckEquals(publishedCount.toLong(), statistics.addLeafCount, "The addition count at $publishedCount.")
        arenaCheckEquals(expectedBlockCount, statistics.blockCount, "The block count at $publishedCount.")
        arenaCheckEquals(expectedSlotCount, statistics.allocatedSlotCount, "The allocated slot count at $publishedCount.")
        arenaCheckEquals(0L, statistics.ancestorQueryCount, "Building must query no ancestor at $publishedCount.")
        arenaCheckEquals(0L, statistics.totalAncestorHopCount, "Building must record no hops at $publishedCount.")

        // The O(sqrt(M)) slack bound: the bottom node occupies one slot, and the partially filled
        // top block never leaves more than the 2 * isqrt(M) slots of an odd block unused.
        val unusedSlotCount = statistics.allocatedSlotCount - (publishedCount + 1L)
        arenaCheck(
            unusedSlotCount >= 0L && unusedSlotCount <= 2L * highestBlock,
            "The unused slot count $unusedSlotCount at $publishedCount is outside 0..${2L * highestBlock}.",
        )

        if (publishedCount != maximumPublishedCount) {
            parent = arena.addLeaf(parent, publishedCount)
        }
    }
}

/** Verifies every arena primitive rejects handles and depths the arena never published. */
private fun arenaPrimitivesRejectUnpublishedHandlesAndDepthsOutsideTheAncestry() {
    val arena = MyersIncrementalAncestorArena<String>()
    arenaCheckEquals(-1, arena.depthOf(arena.bottom), "The bottom node sits at depth -1.")
    arenaCheckEquals(0, arena.publishedNodeCount, "A fresh arena publishes nothing.")

    val fresh = arena.statistics()
    arenaCheckEquals(1, fresh.blockCount, "A fresh arena holds the one block of the bottom node.")
    arenaCheckEquals(1L, fresh.allocatedSlotCount, "A fresh arena holds the one slot of the bottom node.")
    arenaCheckEquals(0L, fresh.addLeafCount, "A fresh arena added no leaf.")

    val root = arena.addLeaf(arena.bottom, "root")
    val child = arena.addLeaf(root, "child")
    val grandchild = arena.addLeaf(child, "grandchild")

    arenaCheckEquals(0, arena.depthOf(root), "The root depth.")
    arenaCheckEquals(2, arena.depthOf(grandchild), "The grandchild depth.")
    arenaCheckEquals(arena.bottom, arena.parentOf(root), "The root's parent is the bottom node.")
    arenaCheckEquals(child, arena.parentOf(grandchild), "The grandchild's parent.")
    arenaCheckEquals("grandchild", arena.valueAt(grandchild), "The grandchild value.")
    arenaCheckEquals(root, arena.ancestorAtDepth(grandchild, 0), "The grandchild's ancestor at depth 0.")
    arenaCheckEquals(arena.bottom, arena.ancestorAtDepth(grandchild, -1), "The grandchild's ancestor at depth -1.")
    arenaCheckEquals(arena.bottom, arena.ancestorAtDepth(arena.bottom, -1), "The bottom node is its own ancestor.")

    // The bottom node is unlabelled: it has neither a parent nor a value.
    arenaCheckThrows<IllegalArgumentException>("The bottom node must have no parent.") { arena.parentOf(arena.bottom) }
    arenaCheckThrows<IllegalArgumentException>("The bottom node must have no value.") { arena.valueAt(arena.bottom) }

    // A depth outside -1..depthOf(node) names no ancestor, at the bottom node and above it alike.
    arenaCheckThrows<IllegalArgumentException>("A depth below -1 must be rejected at the bottom node.") {
        arena.ancestorAtDepth(arena.bottom, -2)
    }
    arenaCheckThrows<IllegalArgumentException>("A depth above the bottom node must be rejected.") {
        arena.ancestorAtDepth(arena.bottom, 0)
    }
    arenaCheckThrows<IllegalArgumentException>("A depth below -1 must be rejected.") {
        arena.ancestorAtDepth(grandchild, -2)
    }
    arenaCheckThrows<IllegalArgumentException>("A depth below every ancestry must be rejected.") {
        arena.ancestorAtDepth(grandchild, Int.MIN_VALUE)
    }
    arenaCheckThrows<IllegalArgumentException>("A depth past the node's own depth must be rejected.") {
        arena.ancestorAtDepth(grandchild, 3)
    }
    arenaCheckThrows<IllegalArgumentException>("A depth past a shallow node's depth must be rejected.") {
        arena.ancestorAtDepth(root, 1)
    }
    arenaCheckThrows<IllegalArgumentException>("The greatest depth must be rejected.") {
        arena.ancestorAtDepth(grandchild, Int.MAX_VALUE)
    }

    // Handle validation is range-based, so every integer outside 0 until publishedNodeCount + 1 is
    // a handle this arena never published and every primitive must refuse it.
    val unpublished = listOf(-1, arena.publishedNodeCount + 1, Int.MAX_VALUE, Int.MIN_VALUE)
    for (handle in unpublished) {
        arenaCheckThrows<IllegalArgumentException>("depthOf must reject the unpublished handle $handle.") {
            arena.depthOf(handle)
        }
        arenaCheckThrows<IllegalArgumentException>("parentOf must reject the unpublished handle $handle.") {
            arena.parentOf(handle)
        }
        arenaCheckThrows<IllegalArgumentException>("valueAt must reject the unpublished handle $handle.") {
            arena.valueAt(handle)
        }
        arenaCheckThrows<IllegalArgumentException>("ancestorAtDepth must reject the unpublished handle $handle.") {
            arena.ancestorAtDepth(handle, -1)
        }
    }

    // A rejected read leaves the traversal counters exactly as it found them, so the query count
    // measures completed queries rather than attempted ones.
    val beforeRejections = arena.statistics()
    arenaCheckThrows<IllegalArgumentException>("A rejected query must still be rejected.") {
        arena.ancestorAtDepth(grandchild, 3)
    }
    arenaCheckEquals(beforeRejections, arena.statistics(), "A rejected query must leave every counter untouched.")

    // The interface guarantee for a failed addition: no partially initialized node is published and
    // no counter moves, so the next successful addition still receives the handle it would have.
    for (handle in unpublished) {
        arenaCheckThrows<IllegalArgumentException>("addLeaf must reject the unpublished parent $handle.") {
            arena.addLeaf(handle, "rejected")
        }
    }
    arenaCheckEquals(beforeRejections, arena.statistics(), "A failed addition must leave every counter untouched.")
    arenaCheckEquals(3, arena.publishedNodeCount, "A failed addition must publish no node.")
    arenaCheckThrows<IllegalArgumentException>("A failed addition must publish no handle.") {
        arena.depthOf(arena.publishedNodeCount + 1)
    }
    arenaCheckEquals("grandchild", arena.valueAt(grandchild), "A failed addition must not disturb a published node.")

    val resumed = arena.addLeaf(grandchild, "resumed")
    arenaCheckEquals(grandchild + 1, resumed, "The next handle follows the last successful addition.")
    arenaCheckEquals(3, arena.depthOf(resumed), "The resumed node's depth.")
    arenaCheckEquals(4, arena.publishedNodeCount, "The published node count after resuming.")
}

/**
 * Compares Myers answers on a deep, irregular tree with a naive parent oracle and bounds the hops.
 *
 * A branching arena coalesces jump links across sibling branches instead of along one straight
 * chain, so it exercises the coalescing predicate on shapes the single-chain guard never reaches.
 * The same `4 * ceil(log2(M + 1))` bound applies, and for the same reason: it is logarithmic in the
 * published node count, which bounds every depth in the forest.
 */
private fun arenaBranchedQueriesMatchTheNaiveOracleWithinTheLogarithmicBound() {
    val chainLength = 256
    val branchCount = 768
    val nodeCount = chainLength + branchCount
    val arena = MyersIncrementalAncestorArena<Int>()
    val random = Random(0xA11CEL)
    val parents = IntArray(nodeCount + 1)
    val depths = IntArray(nodeCount + 1)
    depths[arena.bottom] = -1

    var frontier = arena.bottom
    for (value in 0 until nodeCount) {
        // Most additions extend the current frontier; the rest hang a branch off an arbitrary older
        // node, so the arena grows a forest of unequal arms rather than one path.
        val parent = if (value < chainLength || random.nextInt(8) != 0) frontier else random.nextInt(value + 1)
        val node = arena.addLeaf(parent, value)
        arenaCheckEquals(value + 1, node, "Handles are published in order at $value.")
        parents[node] = parent
        depths[node] = depths[parent] + 1
        frontier = node
    }

    arenaCheckEquals(nodeCount, arena.publishedNodeCount, "The branched published node count.")

    var expectedQueryCount = 0L
    for (node in 1..nodeCount) {
        arenaCheckEquals(depths[node], arena.depthOf(node), "The depth of node $node.")
        arenaCheckEquals(parents[node], arena.parentOf(node), "The parent of node $node.")
        arenaCheckEquals(node - 1, arena.valueAt(node), "The value of node $node.")

        var expected = node
        for (targetDepth in depths[node] downTo -1) {
            arenaCheckEquals(expected, arena.ancestorAtDepth(node, targetDepth), "Node $node at depth $targetDepth.")
            expectedQueryCount++
            if (targetDepth >= 0) {
                expected = parents[expected]
            }
        }
    }

    val statistics = arena.statistics()
    val conservativeLogarithmicBound = 4 * arenaCeilingLog2(nodeCount + 1)
    arenaCheckEquals(nodeCount.toLong(), statistics.addLeafCount, "The branched addition count.")
    arenaCheckEquals(expectedQueryCount, statistics.ancestorQueryCount, "The branched query count.")
    arenaCheck(statistics.maximumAncestorHopCount >= 1, "A branched walk must cost at least one hop.")
    arenaCheck(
        statistics.maximumAncestorHopCount <= conservativeLogarithmicBound,
        "The maximum hop count ${statistics.maximumAncestorHopCount} exceeded the conservative " +
            "logarithmic bound $conservativeLogarithmicBound on a branched forest.",
    )
    arenaCheck(
        statistics.totalAncestorHopCount <= expectedQueryCount * conservativeLogarithmicBound,
        "The total hop count ${statistics.totalAncestorHopCount} exceeded the conservative " +
            "logarithmic budget ${expectedQueryCount * conservativeLogarithmicBound}.",
    )
    arenaCheck(
        statistics.lastAncestorHopCount <= conservativeLogarithmicBound,
        "The final query's hop count ${statistics.lastAncestorHopCount} exceeded the conservative " +
            "logarithmic bound $conservativeLogarithmicBound.",
    )
}

/** The incremental ancestor arena test cases. */
internal fun incrementalAncestorArenaTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "arenaDeepChainQueriesUseConservativelyLogarithmicHops" to
        ::arenaDeepChainQueriesUseConservativelyLogarithmicHops,
    "arenaLastHopCountReportsOnlyTheMostRecentQuery" to ::arenaLastHopCountReportsOnlyTheMostRecentQuery,
    "arenaOddBlockStatisticsMatchTheExactSquareLayout" to ::arenaOddBlockStatisticsMatchTheExactSquareLayout,
    "arenaPrimitivesRejectUnpublishedHandlesAndDepthsOutsideTheAncestry" to
        ::arenaPrimitivesRejectUnpublishedHandlesAndDepthsOutsideTheAncestry,
    "arenaBranchedQueriesMatchTheNaiveOracleWithinTheLogarithmicBound" to
        ::arenaBranchedQueriesMatchTheNaiveOracleWithinTheLogarithmicBound,
)
