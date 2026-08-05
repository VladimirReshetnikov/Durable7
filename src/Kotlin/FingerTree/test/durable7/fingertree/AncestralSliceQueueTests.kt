/*
 * Tests for the ancestral slice queue. The properties under test are that a handle denotes exactly
 * one interval of an append-only ancestry path forever, that an empty handle keeps the anchor its
 * history gave it, and that each scalar operation performs no more backend work than specified.
 */
package durable7.fingertree

import java.util.Collections
import java.util.Random

private fun ancestralCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> ancestralCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> ancestralCheckThrows(message: String, action: () -> Unit) {
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

/** An arena whose bottom node is not at depth -1, which no anchored queue may accept. */
private object AncestralFlatBottomArena : IncrementalAncestorArena<Int> {
    override val bottom: Int = 0
    override val publishedNodeCount: Int = 0

    override fun addLeaf(parent: Int, value: Int): Int =
        error("The queue must reject this arena before using it.")

    override fun depthOf(node: Int): Int = 0

    override fun parentOf(node: Int): Int =
        error("The queue must reject this arena before using it.")

    override fun ancestorAtDepth(node: Int, depth: Int): Int =
        error("The queue must reject this arena before using it.")

    override fun valueAt(node: Int): Int =
        error("The queue must reject this arena before using it.")
}

/** Checks every read path of one handle against an independent list model. */
private fun <T> ancestralAssertState(queue: AncestralSliceQueue<T>, expected: List<T>, label: String) {
    ancestralCheckEquals(expected.size, queue.size, "$label size.")
    ancestralCheckEquals(expected.isEmpty(), queue.isEmpty, "$label emptiness.")
    ancestralCheckEquals(expected, queue.toList(), "$label materialization.")

    val iterated = ArrayList<T>()
    for (value in queue) {
        iterated.add(value)
    }
    ancestralCheckEquals(expected, iterated, "$label iteration.")

    for (index in expected.indices) {
        ancestralCheckEquals(expected[index], queue[index], "$label value at $index.")
    }
    ancestralCheck(queue[expected.size] == null, "$label must report no value past its end.")
    ancestralCheck(queue[-1] == null, "$label must report no value before its front.")

    if (expected.isEmpty()) {
        ancestralCheck(queue.first == null, "$label must have no front.")
        ancestralCheck(queue.last == null, "$label must have no back.")
        ancestralCheck(queue.removeFirst() == null, "$label must refuse front removal.")
        ancestralCheck(queue.removeLast() == null, "$label must refuse back removal.")
        ancestralCheck(queue.popFirst() == null, "$label must refuse a front pop.")
        ancestralCheck(queue.popLast() == null, "$label must refuse a back pop.")
    } else {
        ancestralCheckEquals(expected.first(), queue.first, "$label front.")
        ancestralCheckEquals(expected.last(), queue.last, "$label back.")
    }

    val statistics = queue.validateStructure()
    ancestralCheckEquals(expected.size, statistics.count, "$label validated count.")
    ancestralCheckEquals(expected.size, statistics.walkedNodeCount, "$label walked node count.")
    ancestralCheckEquals(
        statistics.lowDepth + statistics.count - 1,
        statistics.tailDepth,
        "$label anchored window equation.",
    )
}

/** Runs an action and checks the arena performed exactly the expected additions and queries. */
private fun ancestralAssertArenaDelta(
    arena: MyersIncrementalAncestorArena<Int>,
    expectedAdds: Long,
    expectedQueries: Long,
    label: String,
    action: () -> Unit,
) {
    val before = arena.statistics()
    action()
    val after = arena.statistics()

    ancestralCheckEquals(before.addLeafCount + expectedAdds, after.addLeafCount, "$label leaf additions.")
    ancestralCheckEquals(
        before.publishedNodeCount + expectedAdds.toInt(),
        after.publishedNodeCount,
        "$label published nodes.",
    )
    ancestralCheckEquals(
        before.ancestorQueryCount + expectedQueries,
        after.ancestorQueryCount,
        "$label ancestor queries.",
    )
}

private fun ancestralQueueEndpointContractsCoverEmptySingletonAndStoredNulls() {
    val empty = AncestralSliceQueue.empty<String?>()

    ancestralAssertState(empty, emptyList(), "The empty queue")
    ancestralCheck(empty.popFirst() == null, "An empty queue pops no front.")
    ancestralCheck(empty.popLast() == null, "An empty queue pops no back.")

    // A stored null is a present value; absence is reported by the pop wrapper being null.
    val singleton = empty.addLast(null)
    ancestralAssertState(singleton, listOf(null), "The singleton holding a stored null")
    ancestralCheck(singleton.first == null, "The stored null is the front value.")
    ancestralCheck(singleton.last == null, "The stored null is the back value.")

    val poppedNull = singleton.popFirst()
    ancestralCheck(poppedNull != null, "A queue holding a stored null still pops.")
    ancestralCheck(poppedNull!!.value == null, "The popped value is the stored null.")
    ancestralAssertState(poppedNull.rest, emptyList(), "The remainder after popping a stored null")

    val pair = singleton.addLast("tail")
    ancestralAssertState(pair, listOf(null, "tail"), "The pair")
    ancestralAssertState(singleton, listOf(null), "The retained singleton")

    val front = pair.popFirst()!!
    ancestralCheck(front.value == null, "The popped front is the stored null.")
    ancestralAssertState(front.rest, listOf("tail"), "The remainder after a front pop")

    val back = pair.popLast()!!
    ancestralCheckEquals("tail", back.value, "The popped back value.")
    ancestralAssertState(back.rest, listOf(null), "The remainder after a back pop")
    ancestralAssertState(pair, listOf(null, "tail"), "The retained pair")
}

private fun ancestralQueueSlicesEveryBoundaryAndKeepsEmptyAnchorsAppendable() {
    val length = 65
    val values = (0 until length).toList()
    val source = AncestralSliceQueue.from(values)

    for (start in 0..length) {
        for (count in 0..(length - start)) {
            val slice = source.slice(start, count)!!
            ancestralAssertState(slice, values.subList(start, start + count), "The slice at ($start, $count)")

            if (count == 0) {
                // The anchored-empty rule: appending to any empty slice yields exactly the new
                // values and never resurrects the discarded prefix.
                val continued = slice.addLast(-start).addLast(10_000 + start)
                ancestralAssertState(continued, listOf(-start, 10_000 + start), "The branch from the empty slice at $start")
                ancestralAssertState(slice, emptyList(), "The empty slice at $start")
            }
        }
    }

    ancestralCheck(source.slice(0, source.size) === source, "The whole-range slice must return the receiver.")
    ancestralCheck(source.take(source.size) === source, "take(size) must return the receiver.")
    ancestralCheck(source.drop(0) === source, "drop(0) must return the receiver.")
    ancestralAssertState(source, values, "The source after every slice")
}

private fun ancestralQueueTakeDropAndSplitPartitionEveryBoundary() {
    val values = (0 until 257).toList()
    val source = AncestralSliceQueue.from(values)

    for (position in 0..values.size) {
        val prefix = source.take(position)!!
        val suffix = source.drop(position)!!
        ancestralAssertState(prefix, values.subList(0, position), "The prefix at $position")
        ancestralAssertState(suffix, values.subList(position, values.size), "The suffix at $position")

        val split = source.splitAt(position)!!
        ancestralAssertState(split.left, values.subList(0, position), "The split left at $position")
        ancestralAssertState(split.right, values.subList(position, values.size), "The split right at $position")

        // Both results remain real ancestry slices and start independent branches.
        val marker = -position - 1
        ancestralAssertState(
            prefix.addLast(marker),
            values.subList(0, position) + listOf(marker),
            "The prefix branch at $position",
        )
        ancestralAssertState(
            suffix.addLast(marker),
            values.subList(position, values.size) + listOf(marker),
            "The suffix branch at $position",
        )
    }

    ancestralAssertState(source, values, "The source after every partition")
}

private fun ancestralQueueHandlesSquareAndOddBlockSeams() {
    val maximumRoot = 128
    val length = maximumRoot * maximumRoot + 1
    val values = (0 until length).toList()
    val source = AncestralSliceQueue.from(values)

    // A dedicated arena, grown to each square in turn, reports the odd-block directory the seams
    // below are named after; the queue keeps its own arena private.
    val layoutArena = MyersIncrementalAncestorArena<Int>()

    for (root in 0..maximumRoot) {
        val square = root * root
        ancestralCheckEquals(square, source[square], "The value at the square index $square.")

        val start = maxOf(0, square - 1)
        val count = minOf(3, source.size - start)
        ancestralAssertState(
            source.slice(start, count)!!,
            values.subList(start, start + count),
            "The seam slice at $start",
        )

        val marker = -square - 1
        val emptyAtSquare = source.slice(square, 0)!!
        ancestralAssertState(emptyAtSquare.addLast(marker), listOf(marker), "The branch from the empty slice at $square")

        if (square > 0) {
            // The queue length equals the tail handle here, so the versions of length square - 1 and
            // square end immediately before and at the odd-block seam.
            ancestralAssertState(
                source.take(square - 1)!!.addLast(marker - 1),
                values.subList(0, square - 1) + listOf(marker - 1),
                "The branch before the seam at $square",
            )
            ancestralAssertState(
                source.take(square)!!.addLast(marker - 2),
                values.subList(0, square) + listOf(marker - 2),
                "The branch at the seam at $square",
            )
        }

        if (root == maximumRoot) {
            continue
        }

        val nextSquare = (root + 1) * (root + 1)

        // The block spanning [square, nextSquare) is allocated by the arena, not by this arithmetic:
        // an arena holding exactly `square` labelled nodes plus its bottom node must have opened
        // root + 1 blocks totalling (root + 1)^2 slots, which is what makes that span 2 * root + 1
        // slots wide and puts an allocation boundary exactly at every square.
        while (layoutArena.publishedNodeCount < square) {
            layoutArena.addLeaf(layoutArena.bottom, layoutArena.publishedNodeCount)
        }
        val layout = layoutArena.statistics()
        ancestralCheckEquals(square, layout.publishedNodeCount, "The layout arena publication count at root $root.")
        ancestralCheckEquals(root + 1, layout.blockCount, "The arena block count at root $root.")
        ancestralCheckEquals(
            (root + 1).toLong() * (root + 1).toLong(),
            layout.allocatedSlotCount,
            "The arena allocated slot count at root $root.",
        )
        ancestralCheckEquals(
            (2 * root + 1).toLong(),
            layout.allocatedSlotCount - square.toLong(),
            "The odd block length at root $root.",
        )
        ancestralAssertState(
            source.slice(square, nextSquare - square)!!,
            values.subList(square, nextSquare),
            "The whole odd block at $square",
        )
    }

    ancestralAssertState(source, values, "The source after every seam")
}

private fun ancestralQueueIterationIsRepeatableAndIgnoresLaterBranches() {
    val values = (0 until 100).toList()
    val source = AncestralSliceQueue.from(values)
    val slice = source.slice(20, 40)!!
    val captured = slice.iterator()

    val branch = slice.addLast(-1).addLast(-2)

    ancestralCheckEquals(values.subList(20, 60), slice.toList(), "The first materialization.")
    ancestralCheckEquals(values.subList(20, 60), slice.toList(), "The repeated materialization.")
    ancestralCheckEquals(values.subList(20, 60) + listOf(-1, -2), branch.toList(), "The branch materialization.")

    // The iterator captured before the branch existed still yields the original path.
    val capturedValues = ArrayList<Int>()
    while (captured.hasNext()) {
        capturedValues.add(captured.next())
    }
    ancestralCheckEquals(values.subList(20, 60), capturedValues, "The captured iteration.")
    ancestralAssertState(source, values, "The source after iteration")
}

private fun ancestralQueueOperationsRespectTheBackendQueryBoundary() {
    val arena = MyersIncrementalAncestorArena<Int>()
    var built = AncestralSliceQueue.empty(arena)
    for (value in 0 until 16) {
        built = built.addLast(value)
    }
    val queue = built

    ancestralAssertArenaDelta(arena, 0, 0, "Cached reads") {
        ancestralCheckEquals(16, queue.size, "The cached count.")
        ancestralCheck(!queue.isEmpty, "The queue is not empty.")
        ancestralCheckEquals(15, queue.last, "The retained tail value.")
        ancestralCheckEquals(15, queue[15], "The value at the last visible index.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "The front read") {
        ancestralCheckEquals(0, queue.first, "The front value.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "An interior index") {
        ancestralCheckEquals(7, queue[7], "The interior value.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "Front removal") {
        ancestralCheckEquals(15, queue.removeFirst()!!.size, "The size after front removal.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "Back removal") {
        ancestralCheckEquals(15, queue.removeLast()!!.size, "The size after back removal.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "A nonzero drop") {
        ancestralCheckEquals(9, queue.drop(7)!!.size, "The dropped size.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "drop(0)") {
        ancestralCheck(queue.drop(0) === queue, "drop(0) must return the receiver.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "A proper take") {
        ancestralCheckEquals(7, queue.take(7)!!.size, "The taken size.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "take(size)") {
        ancestralCheck(queue.take(16) === queue, "take(size) must return the receiver.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "An interior slice") {
        ancestralCheckEquals(5, queue.slice(3, 5)!!.size, "The interior slice size.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "A suffix slice") {
        // A suffix keeps the retained tail, so it makes no ancestor query.
        ancestralCheckEquals(13, queue.slice(3, 13)!!.size, "The suffix slice size.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "The whole-range slice") {
        ancestralCheck(queue.slice(0, 16) === queue, "The whole-range slice must return the receiver.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "An interior split") {
        val split = queue.splitAt(7)!!
        ancestralCheckEquals(7, split.left.size, "The interior split prefix size.")
        ancestralCheckEquals(9, split.right.size, "The interior split suffix size.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "A split at zero") {
        // Not query-free: the anchored-empty rule makes the empty prefix retain the node at
        // lowDepth - 1, which only an ancestor query can name from the retained tail.
        val split = queue.splitAt(0)!!
        ancestralCheckEquals(0, split.left.size, "The zero split prefix size.")
        ancestralCheckEquals(16, split.right.size, "The zero split suffix size.")
        ancestralCheck(split.right === queue, "The zero split suffix must be the receiver.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "A split at the size") {
        val split = queue.splitAt(queue.size)!!
        ancestralCheck(split.left === queue, "The full split prefix must be the receiver.")
        ancestralCheckEquals(0, split.right.size, "The full split suffix size.")
    }
    ancestralAssertArenaDelta(arena, 0, 1, "A front pop") {
        val popped = queue.popFirst()!!
        ancestralCheckEquals(0, popped.value, "The popped front value.")
        ancestralCheckEquals(15, popped.rest.size, "The size after a front pop.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "A back pop") {
        val popped = queue.popLast()!!
        ancestralCheckEquals(15, popped.value, "The popped back value.")
        ancestralCheckEquals(15, popped.rest.size, "The size after a back pop.")
    }
    ancestralAssertArenaDelta(arena, 0, 0, "Structure validation") {
        ancestralCheckEquals(16, queue.validateStructure().walkedNodeCount, "The validated path length.")
    }
    ancestralAssertArenaDelta(arena, 1, 0, "An append") {
        val appended = queue.addLast(16)
        ancestralCheckEquals(17, appended.size, "The appended size.")
        ancestralCheckEquals(16, appended.last, "The appended tail value.")
    }

    // On an empty queue both split boundaries coincide with the whole range, so the split that costs
    // one query on a non-empty queue is query-free here.
    val emptyQueue = AncestralSliceQueue.empty(arena)
    ancestralAssertArenaDelta(arena, 0, 0, "An empty split at zero") {
        val split = emptyQueue.splitAt(0)!!
        ancestralCheck(split.left === emptyQueue, "The empty split prefix must be the receiver.")
        ancestralCheck(split.right === emptyQueue, "The empty split suffix must be the receiver.")
    }

    var singleton = emptyQueue
    ancestralAssertArenaDelta(arena, 1, 0, "A singleton append") {
        singleton = emptyQueue.addLast(99)
    }
    ancestralAssertArenaDelta(arena, 0, 0, "A singleton front read") {
        // A singleton's front is its retained tail, so it makes no ancestor query.
        ancestralCheckEquals(99, singleton.first, "The singleton front value.")
        ancestralCheckEquals(99, singleton[0], "The singleton indexed value.")
    }
}

private fun ancestralQueueFactoriesAreIndependentAndRejectUnanchoredArenas() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val explicitQueue = AncestralSliceQueue.empty(arena).addLast(10).addLast(20)
    ancestralAssertState(explicitQueue, listOf(10, 20), "The explicitly anchored queue")
    ancestralCheckEquals(2, arena.publishedNodeCount, "The explicit arena publication count.")

    val range = AncestralSliceQueue.from(listOf(1, 2, 3))
    ancestralAssertState(range, listOf(1, 2, 3), "The range factory result")
    ancestralAssertState(range.addLast(4), listOf(1, 2, 3, 4), "The branch from the range factory result")
    ancestralAssertState(range, listOf(1, 2, 3), "The retained range factory result")

    // Each convenience factory owns a private arena, so unrelated queues share no history.
    val first = AncestralSliceQueue.empty<Int>().addLast(1)
    val second = AncestralSliceQueue.empty<Int>().addLast(2)
    ancestralAssertState(first, listOf(1), "The first private-arena queue")
    ancestralAssertState(second, listOf(2), "The second private-arena queue")
    ancestralCheck(!first.sharesArenaWith(second), "Each convenience factory must own a private arena.")
    ancestralCheck(first.sharesArenaWith(first.addLast(3)), "An append must stay in the same arena.")

    ancestralCheckThrows<IllegalArgumentException>("An unanchored arena must be rejected.") {
        AncestralSliceQueue.empty(AncestralFlatBottomArena)
    }
}

private fun ancestralQueueEndpointRemovalHistoriesKeepDistinctAnchors() {
    val values = (0 until 512).toList()
    val source = AncestralSliceQueue.from(values)
    val frontVersions = ArrayList<AncestralSliceQueue<Int>>()
    val backVersions = ArrayList<AncestralSliceQueue<Int>>()
    frontVersions.add(source)
    backVersions.add(source)

    for (removed in values.indices) {
        frontVersions.add(frontVersions[removed].removeFirst()!!)
        backVersions.add(backVersions[removed].removeLast()!!)
    }

    for (removed in 0..values.size) {
        ancestralAssertState(frontVersions[removed], values.subList(removed, values.size), "The front lineage at $removed")
        ancestralAssertState(backVersions[removed], values.subList(0, values.size - removed), "The back lineage at $removed")
        ancestralCheck(
            frontVersions[removed].sharesTailWith(source),
            "Front removal must retain the source tail at $removed.",
        )
    }

    // Both fully drained versions behave as empty queues while keeping different anchors.
    val drainedFront = frontVersions[values.size]
    val drainedBack = backVersions[values.size]
    ancestralCheck(drainedFront.isEmpty && drainedBack.isEmpty, "Both lineages must drain to empty.")
    ancestralCheck(drainedFront.sharesArenaWith(drainedBack), "Both lineages must share one arena.")
    ancestralCheck(!drainedFront.sharesTailWith(drainedBack), "The two drained lineages must keep different anchors.")
    ancestralCheckEquals(
        511,
        drainedFront.validateStructure().tailDepth,
        "The front-drained anchor is the old tail.",
    )
    ancestralCheckEquals(
        -1,
        drainedBack.validateStructure().tailDepth,
        "The back-drained anchor is the arena bottom.",
    )
    ancestralAssertState(drainedFront.addLast(-1), listOf(-1), "The branch from the front-drained empty")
    ancestralAssertState(drainedBack.addLast(-2), listOf(-2), "The branch from the back-drained empty")
    ancestralAssertState(source, values, "The source after both lineages")
}

private fun ancestralQueueRetainedBranchesStayIndependent() {
    val values = (0 until 200).toList()
    val source = AncestralSliceQueue.from(values)
    val middle = source.slice(50, 100)!!

    val branches = listOf(
        middle.addLast(-1),
        middle.addLast(-2).addLast(-3),
        middle.removeFirst()!!.addLast(-4),
        middle.removeLast()!!.addLast(-5),
        middle.take(40)!!.addLast(-6),
        middle.drop(60)!!.addLast(-7),
        middle.slice(30, 0)!!.addLast(-8),
    )

    ancestralAssertState(branches[0], values.subList(50, 150) + listOf(-1), "The appended branch")
    ancestralAssertState(branches[1], values.subList(50, 150) + listOf(-2, -3), "The twice-appended branch")
    ancestralAssertState(branches[2], values.subList(51, 150) + listOf(-4), "The front-trimmed branch")
    ancestralAssertState(branches[3], values.subList(50, 149) + listOf(-5), "The back-trimmed branch")
    ancestralAssertState(branches[4], values.subList(50, 90) + listOf(-6), "The prefix branch")
    ancestralAssertState(branches[5], values.subList(110, 150) + listOf(-7), "The suffix branch")
    ancestralAssertState(branches[6], listOf(-8), "The interior empty-slice branch")
    ancestralAssertState(middle, values.subList(50, 150), "The retained interior slice")
    ancestralAssertState(source, values, "The source after every branch")
}

private fun ancestralQueueRandomizedBranchingHistoryMatchesListModel() {
    val steps = 2_000
    val random = Random(0x51_1CEL)
    val histories = ArrayList<Pair<AncestralSliceQueue<Int>, List<Int>>>()
    histories.add(AncestralSliceQueue.empty<Int>() to emptyList())

    for (step in 0 until steps) {
        val selected = histories[random.nextInt(histories.size)]
        val source = selected.first
        val model = selected.second
        val length = model.size
        val choice = random.nextInt(9)

        val outcome: Pair<AncestralSliceQueue<Int>, List<Int>> = when {
            choice == 1 && length != 0 -> source.removeFirst()!! to model.subList(1, length).toList()
            choice == 2 && length != 0 -> source.removeLast()!! to model.subList(0, length - 1).toList()
            choice == 3 -> {
                val count = random.nextInt(length + 1)
                source.take(count)!! to model.subList(0, count).toList()
            }
            choice == 4 -> {
                val count = random.nextInt(length + 1)
                source.drop(count)!! to model.subList(count, length).toList()
            }
            choice == 5 -> {
                val start = random.nextInt(length + 1)
                val count = random.nextInt(length - start + 1)
                source.slice(start, count)!! to model.subList(start, start + count).toList()
            }
            choice == 6 -> {
                val position = random.nextInt(length + 1)
                val split = source.splitAt(position)!!
                if (random.nextInt(2) == 0) {
                    split.left to model.subList(0, position).toList()
                } else {
                    split.right to model.subList(position, length).toList()
                }
            }
            choice == 7 && length != 0 -> {
                val popped = source.popFirst()!!
                ancestralCheckEquals(model[0], popped.value, "The randomized front pop value at step $step.")
                popped.rest to model.subList(1, length).toList()
            }
            choice == 8 && length != 0 -> {
                val popped = source.popLast()!!
                ancestralCheckEquals(model[length - 1], popped.value, "The randomized back pop value at step $step.")
                popped.rest to model.subList(0, length - 1).toList()
            }
            else -> source.addLast(step) to model + listOf(step)
        }

        ancestralAssertState(source, model, "The randomized source at step $step")
        ancestralAssertState(outcome.first, outcome.second, "The randomized result at step $step")
        histories.add(outcome)

        if (step % 211 == 0) {
            repeat(minOf(16, histories.size)) {
                val retained = histories[random.nextInt(histories.size)]
                ancestralAssertState(retained.first, retained.second, "A randomized retained version at step $step")
            }
        }
    }

    for (index in histories.indices step 97) {
        val retained = histories[index]
        ancestralAssertState(retained.first, retained.second, "The retained version $index")
    }
}

private fun ancestralQueueConcurrentBranchesPublishCompleteVersions() {
    val workerCount = 8
    val branchesPerWorker = 16
    val values = (0 until 512).toList()
    val source = AncestralSliceQueue.from(values)
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val published = Collections.synchronizedList(mutableListOf<Pair<Int, AncestralSliceQueue<Int>>>())

    val threads = (0 until workerCount).map { worker ->
        Thread {
            try {
                for (offset in 0 until branchesPerWorker) {
                    val marker = -(worker * branchesPerWorker + offset) - 1
                    val branch = source.addLast(marker)
                    ancestralCheckEquals(values.size + 1, branch.size, "The concurrent branch size.")
                    ancestralCheckEquals(marker, branch.last, "The concurrent branch tail value.")

                    // Read level ancestors while sibling nodes are being published elsewhere.
                    var index = offset % 31
                    while (index < values.size) {
                        ancestralCheckEquals(values[index], source[index], "The concurrent indexed read at $index.")
                        index += 127
                    }
                    ancestralCheckEquals(
                        values.subList(100, 140),
                        source.slice(100, 40)!!.toList(),
                        "The concurrent slice materialization.",
                    )
                    published.add(marker to branch)
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }
    }

    threads.forEach(Thread::start)
    threads.forEach(Thread::join)

    ancestralCheck(failures.isEmpty(), "Concurrent branching failed with ${failures.firstOrNull()}.")
    ancestralCheckEquals(workerCount * branchesPerWorker, published.size, "The published branch count.")
    for (entry in published) {
        ancestralAssertState(entry.second, values + listOf(entry.first), "The concurrently published branch ${entry.first}")
    }
    ancestralAssertState(source, values, "The source after concurrent branching")
}

private fun ancestralQueueRejectsInvalidPositionsWithoutChangingTheSource() {
    val source = AncestralSliceQueue.from(listOf(10, 20, 30))

    ancestralCheck(source[-1] == null, "A negative index reports no value.")
    ancestralCheck(source[3] == null, "An index at the size reports no value.")
    ancestralCheck(source[Int.MIN_VALUE] == null, "The most negative index reports no value.")
    ancestralCheck(source.slice(-1, 0) == null, "A negative slice index is invalid.")
    ancestralCheck(source.slice(0, -1) == null, "A negative slice count is invalid.")
    ancestralCheck(source.slice(4, 0) == null, "A slice starting past the end is invalid.")
    ancestralCheck(source.slice(2, 2) == null, "A slice running past the end is invalid.")
    ancestralCheck(source.slice(2, Int.MAX_VALUE) == null, "An overflowing slice count is invalid.")
    ancestralCheck(source.take(-1) == null, "A negative take is invalid.")
    ancestralCheck(source.take(4) == null, "A take past the end is invalid.")
    ancestralCheck(source.drop(-1) == null, "A negative drop is invalid.")
    ancestralCheck(source.drop(4) == null, "A drop past the end is invalid.")
    ancestralCheck(source.splitAt(-1) == null, "A negative split boundary is invalid.")
    ancestralCheck(source.splitAt(4) == null, "A split boundary past the end is invalid.")
    ancestralAssertState(source, listOf(10, 20, 30), "The source after every rejected position")

    val empty = AncestralSliceQueue.empty<Int>()
    ancestralCheck(empty.take(0) === empty, "An empty take(0) returns the receiver.")
    ancestralCheck(empty.drop(0) === empty, "An empty drop(0) returns the receiver.")
    ancestralCheck(empty.slice(0, 0) === empty, "An empty whole-range slice returns the receiver.")
    ancestralCheck(empty.take(1) == null, "An empty queue cannot take one value.")
    ancestralCheck(empty.drop(1) == null, "An empty queue cannot drop one value.")
    ancestralCheck(empty.splitAt(1) == null, "An empty queue cannot split at one.")

    val split = empty.splitAt(0)!!
    ancestralAssertState(split.left, emptyList(), "The empty split prefix")
    ancestralAssertState(split.right, emptyList(), "The empty split suffix")
    ancestralAssertState(split.left.addLast(1), listOf(1), "The branch from the empty split prefix")
    ancestralAssertState(split.right.addLast(2), listOf(2), "The branch from the empty split suffix")

    // A singleton emptied from either end appends to exactly one visible value.
    val singleton = empty.addLast(7)
    ancestralAssertState(singleton.removeFirst()!!.addLast(8), listOf(8), "The branch from a front-emptied singleton")
    ancestralAssertState(singleton.removeLast()!!.addLast(9), listOf(9), "The branch from a back-emptied singleton")
    ancestralAssertState(singleton, listOf(7), "The retained singleton")
}

private fun ancestralQueueValidatesStructureAndReportsTailSharing() {
    val arena = MyersIncrementalAncestorArena<Int>()
    var source = AncestralSliceQueue.empty(arena)
    for (value in 0 until 32) {
        source = source.addLast(value)
    }

    val statistics = source.validateStructure()
    ancestralCheckEquals(32, statistics.count, "The validated source count.")
    ancestralCheckEquals(0, statistics.lowDepth, "The validated source low depth.")
    ancestralCheckEquals(31, statistics.tailDepth, "The validated source tail depth.")
    ancestralCheckEquals(32, statistics.walkedNodeCount, "The validated source path length.")
    ancestralCheckEquals(32, statistics.publishedNodeCount, "The validated arena publication count.")

    val suffix = source.drop(10)!!
    val suffixStatistics = suffix.validateStructure()
    ancestralCheck(suffix.sharesTailWith(source), "A drop must retain the source tail.")
    ancestralCheckEquals(10, suffixStatistics.lowDepth, "The suffix low depth.")
    ancestralCheckEquals(31, suffixStatistics.tailDepth, "The suffix tail depth.")
    ancestralCheckEquals(22, suffixStatistics.count, "The suffix count.")

    val prefix = source.take(10)!!
    val prefixStatistics = prefix.validateStructure()
    ancestralCheck(!prefix.sharesTailWith(source), "A proper take must move the tail.")
    ancestralCheck(prefix.sharesArenaWith(source), "A proper take must keep the arena.")
    ancestralCheckEquals(0, prefixStatistics.lowDepth, "The prefix low depth.")
    ancestralCheckEquals(9, prefixStatistics.tailDepth, "The prefix tail depth.")

    val emptyPrefix = source.take(0)!!
    val emptyPrefixStatistics = emptyPrefix.validateStructure()
    ancestralCheckEquals(0, emptyPrefixStatistics.count, "The empty prefix count.")
    ancestralCheckEquals(0, emptyPrefixStatistics.lowDepth, "The empty prefix low depth.")
    ancestralCheckEquals(-1, emptyPrefixStatistics.tailDepth, "The empty prefix anchors at the arena bottom.")
    ancestralCheckEquals(0, emptyPrefixStatistics.walkedNodeCount, "The empty prefix path length.")

    val emptySuffix = source.drop(32)!!
    val emptySuffixStatistics = emptySuffix.validateStructure()
    ancestralCheckEquals(32, emptySuffixStatistics.lowDepth, "The empty suffix low depth.")
    ancestralCheckEquals(31, emptySuffixStatistics.tailDepth, "The empty suffix anchors at the old tail.")
    ancestralCheck(emptySuffix.sharesTailWith(source), "An exhausting drop must retain the source tail.")
    ancestralCheck(!emptyPrefix.sharesTailWith(emptySuffix), "The two empty anchors must differ.")
    ancestralCheck(emptyPrefix.sharesArenaWith(emptySuffix), "The two empty handles must share one arena.")

    ancestralCheck(
        !source.sharesArenaWith(AncestralSliceQueue.from(listOf(0, 1, 2))),
        "Independent arenas must never be reported as shared.",
    )
    ancestralCheck(source.toString().contains("size=32"), "The header rendering reports the visible count.")
}

/** The ancestral slice queue test cases. */
internal fun ancestralSliceQueueTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "ancestralQueueEndpointContractsCoverEmptySingletonAndStoredNulls" to
        ::ancestralQueueEndpointContractsCoverEmptySingletonAndStoredNulls,
    "ancestralQueueSlicesEveryBoundaryAndKeepsEmptyAnchorsAppendable" to
        ::ancestralQueueSlicesEveryBoundaryAndKeepsEmptyAnchorsAppendable,
    "ancestralQueueTakeDropAndSplitPartitionEveryBoundary" to ::ancestralQueueTakeDropAndSplitPartitionEveryBoundary,
    "ancestralQueueHandlesSquareAndOddBlockSeams" to ::ancestralQueueHandlesSquareAndOddBlockSeams,
    "ancestralQueueIterationIsRepeatableAndIgnoresLaterBranches" to
        ::ancestralQueueIterationIsRepeatableAndIgnoresLaterBranches,
    "ancestralQueueOperationsRespectTheBackendQueryBoundary" to ::ancestralQueueOperationsRespectTheBackendQueryBoundary,
    "ancestralQueueFactoriesAreIndependentAndRejectUnanchoredArenas" to
        ::ancestralQueueFactoriesAreIndependentAndRejectUnanchoredArenas,
    "ancestralQueueEndpointRemovalHistoriesKeepDistinctAnchors" to
        ::ancestralQueueEndpointRemovalHistoriesKeepDistinctAnchors,
    "ancestralQueueRetainedBranchesStayIndependent" to ::ancestralQueueRetainedBranchesStayIndependent,
    "ancestralQueueRandomizedBranchingHistoryMatchesListModel" to
        ::ancestralQueueRandomizedBranchingHistoryMatchesListModel,
    "ancestralQueueConcurrentBranchesPublishCompleteVersions" to ::ancestralQueueConcurrentBranchesPublishCompleteVersions,
    "ancestralQueueRejectsInvalidPositionsWithoutChangingTheSource" to
        ::ancestralQueueRejectsInvalidPositionsWithoutChangingTheSource,
    "ancestralQueueValidatesStructureAndReportsTailSharing" to ::ancestralQueueValidatesStructureAndReportsTailSharing,
)
