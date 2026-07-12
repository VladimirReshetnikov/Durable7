package tools.datastructures.fingertree

import java.util.Collections
import java.util.ConcurrentModificationException
import java.util.Random

private fun rrbCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> rrbCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> rrbCheckThrows(message: String, action: () -> Unit) {
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

private class RrbEqualityCountingValue {
    var equalityCalls: Int = 0
    override fun equals(other: Any?): Boolean { equalityCalls++; return this === other }
    override fun hashCode(): Int = 0
}

private fun rrbSameReferenceReplacementBypassesEquality() {
    val value = RrbEqualityCountingValue()
    val vector = RrbVector.from(listOf(value))
    rrbCheck(vector.setItem(0, value) === vector, "RRB same-reference replacement reuses vector")
    rrbCheckEquals(0, value.equalityCalls, "RRB same-reference replacement bypasses equality")
}

private fun rrbConstructionCrossesRadixBoundaries() {
    for (count in listOf(0, 1, 31, 32, 33, 1_023, 1_024, 1_025, 100_000)) {
        val vector = RrbVector.from(0 until count)
        rrbCheckEquals(count, vector.size, "RRB size at $count")
        rrbCheckEquals((0 until count).toList(), vector.toList(), "RRB enumeration at $count")
        var index = 0
        val step = maxOf(1, count / 101)
        while (index < count) {
            rrbCheckEquals(index, vector[index], "RRB index $index of $count")
            index += step
        }
        val statistics = vector.validateStructure() ?: throw AssertionError("invalid RRB at $count")
        rrbCheckEquals(count, statistics.count, "RRB cached count at $count")
        rrbCheckEquals(0, statistics.relaxedBranchCount, "packed RRB should not allocate size tables at $count")
    }
}

private fun rrbConcatenatesUnequalHeights() {
    for ((leftCount, rightCount) in listOf(
        1 to 100_000,
        100_000 to 1,
        31 to 33,
        1_023 to 1_025,
        50_000 to 50_000,
    )) {
        val left = RrbVector.from(0 until leftCount)
        val right = RrbVector.from(leftCount until leftCount + rightCount)
        val combined = left.concat(right)
        rrbCheckEquals(leftCount + rightCount, combined.size, "unequal-height concat size")
        rrbCheckEquals((0 until leftCount + rightCount).toList(), combined.toList(), "unequal-height concat")
        rrbCheck(combined.validateStructure() != null, "unequal-height concat invariants")
        rrbCheck(left.concat(RrbVector.empty()).rootIdentity === left.rootIdentity, "empty suffix root identity")
        rrbCheck(RrbVector.empty<Int>().concat(right).rootIdentity === right.rootIdentity, "empty prefix root identity")
    }
}

private fun rrbSplitsReuseExactBoundaryLeaves() {
    val vector = RrbVector.from(0 until 32 * 128)
    val originalLeaves = vector.leafIdentitiesForTesting()
    for (index in listOf(0, 32, 64, 32 * 31, 32 * 32, 32 * 33, 32 * 96, 32 * 127, vector.size)) {
        val split = vector.splitAt(index) ?: throw AssertionError("split $index")
        rrbCheckEquals((0 until index).toList(), split.first.toList(), "split prefix $index")
        rrbCheckEquals((index until vector.size).toList(), split.second.toList(), "split suffix $index")
        val retainedLeaves = split.first.leafIdentitiesForTesting() + split.second.leafIdentitiesForTesting()
        rrbCheckEquals(originalLeaves.size, retainedLeaves.size, "split leaf count $index")
        for (leafIndex in originalLeaves.indices) {
            rrbCheck(originalLeaves[leafIndex] === retainedLeaves[leafIndex], "split leaf identity $index/$leafIndex")
        }
        rrbCheck(split.first.validateStructure() != null, "split prefix invariants $index")
        rrbCheck(split.second.validateStructure() != null, "split suffix invariants $index")
    }
    rrbCheck(vector.splitAt(0)!!.second === vector, "zero split returns receiver suffix")
    rrbCheck(vector.splitAt(vector.size)!!.first === vector, "end split returns receiver prefix")

    val relaxed = vector.splitAt(1)!!.second
    val statistics = relaxed.validateStructure() ?: throw AssertionError("relaxed suffix invalid")
    rrbCheck(statistics.relaxedBranchCount > 0, "non-aligned split should create relaxed branches")
    for (index in 0 until relaxed.size step 997) {
        rrbCheckEquals(index + 1, relaxed[index], "relaxed indexed lookup")
    }
}

private fun rrbEditsShareAllUntouchedLeaves() {
    val left = RrbVector.from(0 until 32 * 128)
    val right = RrbVector.from(32 * 128 until 32 * 256)
    val concatenated = left.concat(right)
    val expectedConcatLeaves = left.leafIdentitiesForTesting() + right.leafIdentitiesForTesting()
    val actualConcatLeaves = concatenated.leafIdentitiesForTesting()
    rrbCheckEquals(expectedConcatLeaves.size, actualConcatLeaves.size, "concat leaf count")
    for (index in expectedConcatLeaves.indices) {
        rrbCheck(expectedConcatLeaves[index] === actualConcatLeaves[index], "concat leaf identity $index")
    }

    val originalLeaves = concatenated.leafIdentitiesForTesting()
    val changedIndex = 32 * 73 + 11
    val changed = concatenated.setItem(changedIndex, -1) ?: throw AssertionError("set")
    val changedLeaves = changed.leafIdentitiesForTesting()
    for (index in originalLeaves.indices) {
        if (index == changedIndex / 32) {
            rrbCheck(originalLeaves[index] !== changedLeaves[index], "changed leaf must be copied")
        } else {
            rrbCheck(originalLeaves[index] === changedLeaves[index], "untouched leaf $index must be shared")
        }
    }
    rrbCheck(concatenated.setItem(changedIndex, concatenated[changedIndex]!!) === concatenated, "equal set identity")
    rrbCheckEquals(changedIndex, concatenated[changedIndex], "old snapshot after set")
    rrbCheckEquals(-1, changed[changedIndex], "new snapshot after set")
}

private fun rrbRandomizedHistoryMatchesListModel() {
    val random = Random(20260714)
    var vector = RrbVector.empty<Int>()
    val model = ArrayList<Int>()
    val snapshots = ArrayList<Pair<RrbVector<Int>, List<Int>>>()

    repeat(10_000) { step ->
        when (random.nextInt(7)) {
            0 -> {
                vector = vector.append(step)
                model.add(step)
            }
            1 -> {
                vector = vector.prepend(step)
                model.add(0, step)
            }
            2 -> if (model.isNotEmpty()) {
                val index = random.nextInt(model.size)
                vector = vector.setItem(index, -step)!!
                model[index] = -step
            }
            3 -> {
                val index = random.nextInt(model.size + 1)
                val values = listOf(step, step + 1, step + 2)
                vector = vector.insertRange(index, values)!!
                model.addAll(index, values)
            }
            4 -> if (model.isNotEmpty()) {
                val index = random.nextInt(model.size)
                val count = random.nextInt(model.size - index + 1)
                vector = vector.removeRange(index, count)!!
                repeat(count) { model.removeAt(index) }
            }
            5 -> {
                val index = random.nextInt(model.size + 1)
                val split = vector.splitAt(index)!!
                vector = split.first.concat(split.second)
            }
            6 -> if (model.isNotEmpty()) {
                val popped = vector.tryRemoveLast() ?: throw AssertionError("pop")
                rrbCheckEquals(model.removeAt(model.lastIndex), popped.value, "randomized pop")
                vector = popped.rest
            }
        }

        rrbCheckEquals(model.size, vector.size, "randomized RRB size at $step")
        if (step % 701 == 0) {
            snapshots.add(vector to model.toList())
        }
        if (step % 997 == 0) {
            rrbCheck(vector.validateStructure() != null, "randomized RRB invariants at $step")
        }
    }

    rrbCheckEquals(model, vector.toList(), "randomized RRB final contents")
    for ((snapshot, expected) in snapshots) {
        rrbCheckEquals(expected, snapshot.toList(), "retained randomized RRB snapshot")
    }
}

private fun rrbAdversarialHistoriesStayDenseAndShallow() {
    val count = 32 * 1_024
    var vector = RrbVector.from(0 until count)
    val random = Random(20260724)
    repeat(2_000) { operation ->
        val boundary = when (operation % 7) {
            0 -> 1
            1 -> 31
            2 -> 32
            3 -> 1_023
            4 -> 1_024
            5 -> count - 1
            else -> 1 + random.nextInt(count - 1)
        }
        val split = vector.splitAt(boundary)!!
        vector = split.first.concat(split.second)
        if (operation % 127 == 0) {
            rrbCheck(vector.validateStructure() != null, "adversarial RRB invariants at $operation")
        }
    }

    rrbCheckEquals((0 until count).toList(), vector.toList(), "adversarial split/concat contents")
    val statistics = vector.validateStructure() ?: throw AssertionError("adversarial vector invalid")
    rrbCheck(statistics.height <= minimumRrbHeight(count) + 1, "adversarial RRB height ${statistics.height}")
    rrbCheck(statistics.leafCount <= (count + 15) / 16, "adversarial RRB leaf density")
    rrbCheck(statistics.branchCount < statistics.leafCount, "adversarial RRB branch density")

    var fragments = RrbVector.empty<Int>()
    val model = ArrayList<Int>()
    repeat(2_048) { fragment ->
        val length = 1 + fragment * 17 % 63
        val values = (model.size until model.size + length).toList()
        fragments = fragments.concat(RrbVector.from(values))
        model.addAll(values)
    }
    rrbCheckEquals(model, fragments.toList(), "uneven fragment concatenation")
    val fragmentStatistics = fragments.validateStructure() ?: throw AssertionError("fragment vector invalid")
    rrbCheck(fragmentStatistics.height <= minimumRrbHeight(fragments.size) + 1, "fragment RRB height")
    rrbCheck(fragmentStatistics.leafCount <= (fragments.size + 15) / 16, "fragment RRB leaf density")
}

private fun rrbBuilderCachesAndIsolatesSnapshots() {
    val builder = RrbVector.builder<Int>()
    val source = Array(1_000) { it }
    builder.appendAll(source)
    source[0] = -1
    val first = builder.toImmutable()

    rrbCheck(first === builder.toImmutable(), "clean builder freeze should be cached")
    rrbCheckEquals(1_000, builder.size, "builder frozen size")
    rrbCheckEquals((0 until 1_000).toList(), first.toList(), "builder copied source array")
    val firstStatistics = first.validateStructure() ?: throw AssertionError("first builder snapshot invalid")
    rrbCheckEquals(0, firstStatistics.relaxedBranchCount, "builder should produce packed radix nodes")

    builder.append(1_000).appendAll(1_001 until 2_000)
    val second = builder.toImmutable()
    rrbCheckEquals((0 until 1_000).toList(), first.toList(), "first builder snapshot isolation")
    rrbCheckEquals((0 until 2_000).toList(), second.toList(), "second builder snapshot")
    rrbCheck(second.validateStructure() != null, "second builder snapshot invariants")

    val adopted = second.toBuilder()
    rrbCheck(adopted.toImmutable() === second, "builder adopts frozen prefix")
    adopted.append(2_000)
    val third = adopted.toImmutable()
    rrbCheckEquals((0 until 2_000).toList(), second.toList(), "adopted prefix isolation")
    rrbCheckEquals((0..2_000).toList(), third.toList(), "adopted builder snapshot")

    val iterator = adopted.iterator()
    adopted.append(2_001)
    rrbCheckThrows<ConcurrentModificationException>("builder fail-fast iterator") { iterator.hasNext() }
    adopted.clear()
    rrbCheckEquals(0, adopted.size, "cleared builder size")
    rrbCheck(adopted.toImmutable() === RrbVector.empty<Int>(), "cleared builder shared empty")
}

private fun rrbNullablePopAndOverflowContracts() {
    val vector = RrbVector.from(listOf<String?>(null, "middle", null))
    rrbCheckEquals(listOf(null, "middle", null), vector.toList(), "nullable RRB contents")
    rrbCheckEquals(null, vector[0], "nullable RRB index")
    rrbCheckEquals("middle", vector[1], "nullable RRB non-null index")
    rrbCheck(vector.setItem(0, null) === vector, "nullable equal set identity")

    val popped = vector.tryRemoveLast() ?: throw AssertionError("nullable pop must succeed")
    rrbCheckEquals(null, popped.value, "stored null pop value")
    rrbCheckEquals(listOf(null, "middle"), popped.rest.toList(), "nullable pop rest")
    rrbCheckEquals(null, RrbVector.empty<String?>().tryRemoveLast(), "empty nullable pop")

    rrbCheckEquals(null, vector[-1], "negative index")
    rrbCheckEquals(null, vector[vector.size], "past-end index")
    rrbCheckEquals(null, vector.setItem(Int.MAX_VALUE, "x"), "overflowing set index")
    rrbCheckEquals(null, vector.splitAt(Int.MAX_VALUE), "overflowing split boundary")
    rrbCheckEquals(null, vector.insertRange(Int.MAX_VALUE, emptyList()), "overflowing insert boundary")
    rrbCheckEquals(null, vector.removeRange(1, Int.MAX_VALUE), "overflowing removal count")
    rrbCheck(vector.insertRange(1, emptyList()) === vector, "empty insertion identity")
    rrbCheck(vector.removeRange(1, 0) === vector, "empty removal identity")
    rrbCheckThrows<ArithmeticException>("RRB checked count addition") {
        RrbVector.checkedElementCountForTesting(Int.MAX_VALUE, 1)
    }
}

private fun rrbConcurrentReadersSeeStableSnapshots() {
    val original = RrbVector.from(0 until 10_000)
    val changed = original.setItem(5_000, -1)!!
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val threads = (0 until 8).map { worker ->
        Thread {
            try {
                repeat(256) { iteration ->
                    val index = (worker * 997 + iteration * 37) % original.size
                    rrbCheckEquals(index, original[index], "concurrent original index")
                    rrbCheckEquals(if (index == 5_000) -1 else index, changed[index], "concurrent changed index")
                }
                var sum = 0L
                for (value in original) {
                    sum += value
                }
                rrbCheckEquals(49_995_000L, sum, "concurrent RRB iteration")
                rrbCheck(original.validateStructure() != null, "concurrent original invariants")
                rrbCheck(changed.validateStructure() != null, "concurrent changed invariants")
            } catch (error: Throwable) {
                failures.add(error)
            }
        }.apply { name = "rrb-reader-$worker" }
    }
    threads.forEach(Thread::start)
    threads.forEach(Thread::join)
    if (failures.isNotEmpty()) {
        throw AssertionError("RRB concurrent readers had ${failures.size} failure(s)", failures.first())
    }
}

private fun minimumRrbHeight(count: Int): Int {
    var height = 0
    var capacity = 32L
    while (capacity < count) {
        capacity *= 32L
        height++
    }
    return height
}

internal fun rrbVectorTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "rrbSameReferenceReplacementBypassesEquality" to ::rrbSameReferenceReplacementBypassesEquality,
    "rrbConstructionCrossesRadixBoundaries" to ::rrbConstructionCrossesRadixBoundaries,
    "rrbConcatenatesUnequalHeights" to ::rrbConcatenatesUnequalHeights,
    "rrbSplitsReuseExactBoundaryLeaves" to ::rrbSplitsReuseExactBoundaryLeaves,
    "rrbEditsShareAllUntouchedLeaves" to ::rrbEditsShareAllUntouchedLeaves,
    "rrbRandomizedHistoryMatchesListModel" to ::rrbRandomizedHistoryMatchesListModel,
    "rrbAdversarialHistoriesStayDenseAndShallow" to ::rrbAdversarialHistoriesStayDenseAndShallow,
    "rrbBuilderCachesAndIsolatesSnapshots" to ::rrbBuilderCachesAndIsolatesSnapshots,
    "rrbNullablePopAndOverflowContracts" to ::rrbNullablePopAndOverflowContracts,
    "rrbConcurrentReadersSeeStableSnapshots" to ::rrbConcurrentReadersSeeStableSnapshots,
)
