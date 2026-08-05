/*
 * Tests for the bilateral ancestral deque. The properties under test are that two oppositely
 * oriented ancestry intervals reproduce a sequence oracle exactly, that every produced handle stays
 * growable at both ends, and that the advertised level-ancestor query profiles are exact rather
 * than merely bounded.
 */
package durable7.fingertree

import java.util.Random

private fun bilateralCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> bilateralCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> bilateralCheckThrows(message: String, action: () -> Unit) {
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

/** An arena whose bottom node reports the wrong depth, used to prove eager rejection. */
private class BilateralInvalidBottomArena : IncrementalAncestorArena<Int> {
    var touched: Int = 0
        private set

    override val bottom: Int
        get() = 0

    override val publishedNodeCount: Int
        get() = 0

    override fun addLeaf(parent: Int, value: Int): Int {
        touched++
        throw UnsupportedOperationException("A rejected arena is never asked to publish.")
    }

    override fun depthOf(node: Int): Int = 0

    override fun parentOf(node: Int): Int =
        throw UnsupportedOperationException("A rejected arena is never navigated.")

    override fun ancestorAtDepth(node: Int, depth: Int): Int =
        throw UnsupportedOperationException("A rejected arena is never queried.")

    override fun valueAt(node: Int): Int =
        throw UnsupportedOperationException("A rejected arena holds no values.")
}

/** A faithful Myers arena that can be told to misreport labelled-node depths. */
private class BilateralBiasedArena(
    private val inner: MyersIncrementalAncestorArena<Int>,
) : IncrementalAncestorArena<Int> {
    var depthBias: Int = 0

    override val bottom: Int
        get() = inner.bottom

    override val publishedNodeCount: Int
        get() = inner.publishedNodeCount

    override fun addLeaf(parent: Int, value: Int): Int = inner.addLeaf(parent, value)

    override fun depthOf(node: Int): Int =
        if (node == inner.bottom) inner.depthOf(node) else inner.depthOf(node) + depthBias

    override fun parentOf(node: Int): Int = inner.parentOf(node)

    override fun ancestorAtDepth(node: Int, depth: Int): Int = inner.ancestorAtDepth(node, depth)

    override fun valueAt(node: Int): Int = inner.valueAt(node)
}

private class BilateralWorkerResult(
    val deque: BilateralAncestralDeque<Int>,
    val model: List<Int>,
    val additions: Int,
)

private fun <T> bilateralIterated(values: Iterable<T>): List<T> {
    val result = ArrayList<T>()
    for (value in values) {
        result.add(value)
    }
    return result
}

private fun <T> bilateralAssertDeque(expected: List<T>, actual: BilateralAncestralDeque<T>) {
    bilateralCheckEquals(expected.size, actual.size, "Deque size.")
    bilateralCheckEquals(expected.isEmpty(), actual.isEmpty, "Deque emptiness.")
    bilateralCheckEquals(expected, actual.toList(), "Deque values.")
    bilateralCheckEquals(expected, bilateralIterated(actual), "Deque iteration.")
    for (index in expected.indices) {
        bilateralCheckEquals(expected[index], actual[index], "Deque value at $index.")
    }
    bilateralCheck(actual[expected.size] == null, "An index at the count is outside the deque.")
    bilateralCheck(actual[-1] == null, "A negative index is outside the deque.")
    bilateralCheckEquals(expected.firstOrNull(), actual.front(), "Deque front.")
    bilateralCheckEquals(expected.lastOrNull(), actual.back(), "Deque back.")

    val statistics = actual.validateStructure()
    bilateralCheckEquals(expected.size, statistics.count, "Validated count.")
    bilateralCheckEquals(
        expected.size,
        statistics.leftCount + statistics.rightCount,
        "Validated interval counts.",
    )
}

/** Runs [operation] and asserts it made exactly [expected] level-ancestor queries. */
private fun <R> bilateralQueries(
    arena: MyersIncrementalAncestorArena<*>,
    expected: Int,
    message: String,
    operation: () -> R,
): R {
    val before = arena.statistics().ancestorQueryCount
    val result = operation()
    val after = arena.statistics().ancestorQueryCount
    bilateralCheckEquals(expected.toLong(), after - before, "$message level-ancestor queries.")
    return result
}

private fun bilateralRunConcurrent(name: String, workerCount: Int, action: (Int) -> Unit) {
    val failures = java.util.Collections.synchronizedList(mutableListOf<Throwable>())
    val threads = (0 until workerCount).map { worker ->
        Thread {
            try {
                action(worker)
            } catch (error: Throwable) {
                failures.add(error)
            }
        }.apply { this.name = "$name-$worker" }
    }

    threads.forEach { it.start() }
    threads.forEach { it.join() }
    if (failures.isNotEmpty()) {
        throw AssertionError("$name had ${failures.size} worker failure(s)", failures.first())
    }
}

/** Builds the canonical eight-value bilateral deque `[1..8]` with four values on each arm. */
private fun bilateralEightValueDeque(
    arena: MyersIncrementalAncestorArena<Int>,
): BilateralAncestralDeque<Int> =
    BilateralAncestralDeque.create(arena)
        .addFirst(4)
        .addFirst(3)
        .addFirst(2)
        .addFirst(1)
        .addLast(5)
        .addLast(6)
        .addLast(7)
        .addLast(8)

private fun bilateralEmptyAndNullableValuesHaveStableFailuresAndIdentity() {
    val empty = BilateralAncestralDeque.empty<String?>()

    bilateralCheck(empty.isEmpty, "A fresh deque is empty.")
    bilateralCheckEquals(0, empty.size, "A fresh deque has no values.")
    bilateralCheckEquals(emptyList(), empty.toList(), "A fresh deque copies to nothing.")
    bilateralCheckEquals(0, bilateralIterated(empty).size, "A fresh deque enumerates nothing.")
    bilateralCheck(empty.front() == null, "An empty deque has no front.")
    bilateralCheck(empty.back() == null, "An empty deque has no back.")
    bilateralCheck(empty[0] == null, "An empty deque has no indexed value.")
    bilateralCheck(empty[-1] == null, "A negative index is rejected.")
    bilateralCheck(empty.removeFirst() == null, "An empty deque cannot drop a first value.")
    bilateralCheck(empty.removeLast() == null, "An empty deque cannot drop a last value.")
    bilateralCheck(empty.tryRemoveFirst() == null, "An empty deque pops no first value.")
    bilateralCheck(empty.tryRemoveLast() == null, "An empty deque pops no last value.")

    val identities = listOf(
        "clear" to empty.clear(),
        "take" to empty.take(0)!!,
        "drop" to empty.drop(0)!!,
        "slice" to empty.slice(0, 0)!!,
        "reverse" to empty.reverse(),
    )
    for ((name, produced) in identities) {
        bilateralAssertDeque(emptyList(), produced)
        bilateralCheck(produced === empty, "An empty $name returns the receiver.")
    }
    // Asserting sharesStorageWith on those results would compare `empty` with itself, since each one
    // returns the receiver. A separately constructed empty handle over the same arena is the
    // discriminating case: not the same object, yet denoting the same interval.
    val separateEmpty = BilateralAncestralDeque.create(empty.arena)
    bilateralCheck(separateEmpty !== empty, "A separately created empty handle is a distinct object.")
    bilateralCheck(separateEmpty.sharesStorageWith(empty), "Separate empty handles share storage.")

    val emptySplit = empty.splitAt(0)!!
    bilateralCheck(emptySplit.left === empty, "An empty split keeps the receiver on the left.")
    bilateralCheck(emptySplit.right === empty, "An empty split keeps the receiver on the right.")

    bilateralCheck(empty.take(1) == null, "An overlong prefix is rejected.")
    bilateralCheck(empty.take(-1) == null, "A negative prefix is rejected.")
    bilateralCheck(empty.drop(1) == null, "An overlong suffix is rejected.")
    bilateralCheck(empty.drop(-1) == null, "A negative suffix is rejected.")
    bilateralCheck(empty.splitAt(1) == null, "An overlong boundary is rejected.")
    bilateralCheck(empty.splitAt(-1) == null, "A negative boundary is rejected.")
    bilateralCheck(empty.slice(0, 1) == null, "An overlong empty slice is rejected.")
    bilateralCheck(empty.slice(1, 0) == null, "An out-of-range slice index is rejected.")

    val nullable = empty.addFirst(null).addLast("tail")
    bilateralAssertDeque(listOf(null, "tail"), nullable)
    val popped = nullable.tryRemoveFirst()
    bilateralCheck(popped != null, "A nonempty deque pops even when the value is null.")
    bilateralCheck(popped!!.value == null, "The popped value is the stored null.")
    bilateralAssertDeque(listOf("tail"), popped.rest)
    bilateralAssertDeque(listOf(null, "tail"), nullable)

    val rejected = BilateralInvalidBottomArena()
    bilateralCheckThrows<IllegalArgumentException>("A misplaced bottom node is rejected.") {
        BilateralAncestralDeque.create(rejected)
    }
    bilateralCheckEquals(0, rejected.touched, "A rejected arena publishes nothing.")

    val existing = BilateralAncestralDeque.from(listOf(1, 2, 3))
    bilateralAssertDeque(listOf(1, 2, 3), existing)
    val copied = BilateralAncestralDeque.from(existing)
    bilateralAssertDeque(listOf(1, 2, 3), copied)
    bilateralCheck(copied.arena !== existing.arena, "A collected range owns a fresh arena.")
    bilateralCheck(!copied.sharesStorageWith(existing), "Equal values over two arenas do not share.")
    bilateralAssertDeque(emptyList(), BilateralAncestralDeque.from(emptyList<Int>()))
}

private fun bilateralEndpointOperationsRetainEveryPriorBranch() {
    val root = BilateralAncestralDeque.empty<Int>()
    val one = root.addLast(10)
    val two = one.addLast(20)
    val three = two.addFirst(5)
    val four = three.addFirst(1)
    val five = four.addLast(30)

    bilateralAssertDeque(emptyList(), root)
    bilateralAssertDeque(listOf(10), one)
    bilateralAssertDeque(listOf(10, 20), two)
    bilateralAssertDeque(listOf(5, 10, 20), three)
    bilateralAssertDeque(listOf(1, 5, 10, 20), four)
    bilateralAssertDeque(listOf(1, 5, 10, 20, 30), five)

    bilateralAssertDeque(listOf(5, 10, 20, 30), five.removeFirst()!!)
    bilateralAssertDeque(listOf(1, 5, 10, 20), five.removeLast()!!)
    bilateralAssertDeque(listOf(1, 5, 10, 20, 30), five)

    val rightOnly = root.addLast(1).addLast(2).addLast(3)
    val afterOne = rightOnly.removeFirst()!!
    val afterTwo = afterOne.removeFirst()!!
    bilateralAssertDeque(listOf(2, 3), afterOne)
    bilateralAssertDeque(listOf(3), afterTwo)
    bilateralAssertDeque(emptyList(), afterTwo.removeFirst()!!)
    bilateralAssertDeque(listOf(1, 2, 3), rightOnly)

    val leftOnly = root.addFirst(3).addFirst(2).addFirst(1)
    val withoutLast = leftOnly.removeLast()!!
    val withoutTwo = withoutLast.removeLast()!!
    bilateralAssertDeque(listOf(1, 2), withoutLast)
    bilateralAssertDeque(listOf(1), withoutTwo)
    bilateralAssertDeque(emptyList(), withoutTwo.removeLast()!!)
    bilateralAssertDeque(listOf(1, 2, 3), leftOnly)

    val poppedFront = five.tryRemoveFirst()!!
    bilateralCheckEquals(1, poppedFront.value, "The popped front value.")
    bilateralAssertDeque(listOf(5, 10, 20, 30), poppedFront.rest)
    val poppedBack = five.tryRemoveLast()!!
    bilateralCheckEquals(30, poppedBack.value, "The popped back value.")
    bilateralAssertDeque(listOf(1, 5, 10, 20), poppedBack.rest)

    val leftBranch = two.addFirst(-1)
    val rightBranch = two.addLast(99)
    bilateralAssertDeque(listOf(-1, 10, 20), leftBranch)
    bilateralAssertDeque(listOf(10, 20, 99), rightBranch)
    bilateralAssertDeque(listOf(10, 20), two)

    val cleared = five.clear()
    bilateralAssertDeque(emptyList(), cleared)
    bilateralCheck(cleared !== five, "Clearing a nonempty deque produces a new handle.")
    bilateralCheck(cleared.arena === five.arena, "Clearing keeps the arena.")
    bilateralCheck(cleared.clear() === cleared, "Clearing an empty deque is an identity.")
    bilateralAssertDeque(listOf(-7, 8), cleared.addFirst(-7).addLast(8))
    bilateralAssertDeque(listOf(1, 5, 10, 20, 30), five)
}

private fun bilateralReverseSwapsOrientationWithoutChangingRetainedVersions() {
    val deque = BilateralAncestralDeque.empty<Int>()
        .addFirst(3)
        .addFirst(2)
        .addFirst(1)
        .addLast(4)
        .addLast(5)
        .addLast(6)
        .addLast(7)
    val reversed = deque.reverse()
    val restored = reversed.reverse()

    bilateralAssertDeque(listOf(1, 2, 3, 4, 5, 6, 7), deque)
    bilateralAssertDeque(listOf(7, 6, 5, 4, 3, 2, 1), reversed)
    bilateralAssertDeque(listOf(1, 2, 3, 4, 5, 6, 7), restored)
    bilateralCheck(restored.sharesStorageWith(deque), "Reversing twice restores the representation.")
    bilateralAssertDeque(
        listOf(0, 7, 6, 5, 4, 3, 2, 1, 8),
        reversed.addFirst(0).addLast(8),
    )
    bilateralAssertDeque(listOf(6, 5, 4, 3, 2), reversed.removeFirst()!!.removeLast()!!)
    bilateralAssertDeque(listOf(1, 2, 3, 4, 5, 6, 7), deque)

    val singleton = BilateralAncestralDeque.from(listOf(42))
    bilateralCheck(singleton.reverse() === singleton, "Reversing one value returns the receiver.")
    val none = BilateralAncestralDeque.empty<Int>()
    bilateralCheck(none.reverse() === none, "Reversing nothing returns the receiver.")
}

private fun bilateralSliceAndSplitCrossingBothArmsRemainGrowable() {
    val original = BilateralAncestralDeque.empty<Int>()
        .addFirst(3)
        .addFirst(2)
        .addFirst(1)
        .addLast(4)
        .addLast(5)
        .addLast(6)
        .addLast(7)
    val expected = (1..7).toList()

    for (start in 0..expected.size) {
        for (count in 0..expected.size - start) {
            val slice = original.slice(start, count)!!
            val model = expected.subList(start, start + count)
            bilateralAssertDeque(model, slice)

            val extended = slice.addFirst(-100 - start).addLast(100 + count)
            bilateralAssertDeque(listOf(-100 - start) + model + listOf(100 + count), extended)
            bilateralAssertDeque(model, slice)

            val reversed = slice.reverse().addFirst(-1).addLast(-2)
            bilateralAssertDeque(listOf(-1) + model.reversed() + listOf(-2), reversed)
        }
    }

    for (boundary in 0..expected.size) {
        val split = original.splitAt(boundary)!!
        val leftModel = expected.subList(0, boundary)
        val rightModel = expected.subList(boundary, expected.size)
        bilateralAssertDeque(leftModel, split.left)
        bilateralAssertDeque(rightModel, split.right)
        bilateralAssertDeque(leftModel + listOf(88), split.left.addLast(88))
        bilateralAssertDeque(listOf(77) + rightModel, split.right.addFirst(77))
        bilateralCheckEquals(expected, split.left.toList() + split.right.toList(), "Split halves rejoin.")
    }

    bilateralCheck(original.slice(0, expected.size) === original, "The whole range is an identity.")
    bilateralCheck(original.take(expected.size) === original, "The whole prefix is an identity.")
    bilateralCheck(original.drop(0) === original, "The empty suffix is an identity.")
    val endpointSplit = original.splitAt(0)!!
    bilateralCheck(endpointSplit.right === original, "A zero boundary keeps the receiver.")
    bilateralAssertDeque(emptyList(), endpointSplit.left)
    val finalSplit = original.splitAt(expected.size)!!
    bilateralCheck(finalSplit.left === original, "A final boundary keeps the receiver.")
    bilateralAssertDeque(emptyList(), finalSplit.right)

    bilateralAssertDeque(expected, original)
}

private fun bilateralSmallConstructionWordsMatchTheSequenceOracle() {
    for (length in 0..8) {
        for (word in 0 until (1 shl length)) {
            var deque = BilateralAncestralDeque.empty<Int>()
            val model = ArrayList<Int>()
            for (value in 0 until length) {
                if (word and (1 shl value) == 0) {
                    deque = deque.addFirst(value)
                    model.add(0, value)
                } else {
                    deque = deque.addLast(value)
                    model.add(value)
                }
            }

            bilateralAssertDeque(model, deque)
            for (start in 0..length) {
                for (count in 0..length - start) {
                    val expected = model.subList(start, start + count).toList()
                    val slice = deque.slice(start, count)!!
                    bilateralAssertDeque(expected, slice)
                    bilateralAssertDeque(
                        listOf(-1) + expected + listOf(-2),
                        slice.addFirst(-1).addLast(-2),
                    )
                }
            }

            for (boundary in 0..length) {
                val split = deque.splitAt(boundary)!!
                bilateralAssertDeque(model.subList(0, boundary).toList(), split.left)
                bilateralAssertDeque(model.subList(boundary, length).toList(), split.right)
                bilateralAssertDeque(model.subList(0, boundary).toList(), deque.take(boundary)!!)
                bilateralAssertDeque(model.subList(boundary, length).toList(), deque.drop(boundary)!!)
            }
        }
    }
}

private fun bilateralRandomizedRetainedBranchesMatchAnImmutableListModel() {
    for (seed in longArrayOf(0L, 1L, 17L, 91L, 8_675_309L)) {
        val random = Random(seed)
        val root = BilateralAncestralDeque.empty<Int>()
        val versions = ArrayList<Pair<BilateralAncestralDeque<Int>, List<Int>>>()
        versions.add(root to emptyList())

        for (step in 0 until 500) {
            val (source, sourceModel) = versions[random.nextInt(versions.size)]
            bilateralAssertDeque(sourceModel, source)

            val value = seed.toInt() + step * 31
            var result: BilateralAncestralDeque<Int>
            var model: List<Int>
            when (random.nextInt(11)) {
                0 -> {
                    result = source.addFirst(value)
                    model = listOf(value) + sourceModel
                }
                1 -> {
                    result = source.addLast(value)
                    model = sourceModel + listOf(value)
                }
                2 -> {
                    if (sourceModel.isEmpty()) {
                        result = source.clear()
                        model = emptyList()
                    } else {
                        result = source.removeFirst()!!
                        model = sourceModel.subList(1, sourceModel.size)
                    }
                }
                3 -> {
                    if (sourceModel.isEmpty()) {
                        result = source.clear()
                        model = emptyList()
                    } else {
                        result = source.removeLast()!!
                        model = sourceModel.subList(0, sourceModel.size - 1)
                    }
                }
                4 -> {
                    result = source.reverse()
                    model = sourceModel.reversed()
                }
                5 -> {
                    val start = random.nextInt(sourceModel.size + 1)
                    val count = random.nextInt(sourceModel.size - start + 1)
                    result = source.slice(start, count)!!
                    model = sourceModel.subList(start, start + count)
                }
                6 -> {
                    val boundary = random.nextInt(sourceModel.size + 1)
                    val split = source.splitAt(boundary)!!
                    if (random.nextInt(2) == 0) {
                        result = split.left
                        model = sourceModel.subList(0, boundary)
                    } else {
                        result = split.right
                        model = sourceModel.subList(boundary, sourceModel.size)
                    }
                }
                7 -> {
                    val count = random.nextInt(sourceModel.size + 1)
                    result = source.take(count)!!
                    model = sourceModel.subList(0, count)
                }
                8 -> {
                    val count = random.nextInt(sourceModel.size + 1)
                    result = source.drop(count)!!
                    model = sourceModel.subList(count, sourceModel.size)
                }
                9 -> {
                    result = source.clear()
                    model = emptyList()
                }
                else -> {
                    result = source.addFirst(value).reverse().addLast(value.inv())
                    model = (listOf(value) + sourceModel).reversed() + listOf(value.inv())
                }
            }

            bilateralAssertDeque(model, result)
            bilateralAssertDeque(sourceModel, source)
            versions.add(result to model)

            if (step % 29 == 0) {
                repeat(minOf(versions.size, 12)) {
                    val (retained, retainedModel) = versions[random.nextInt(versions.size)]
                    bilateralAssertDeque(retainedModel, retained)
                }
            }
        }

        bilateralAssertDeque(emptyList(), root)
    }
}

private fun bilateralQueryFreeOperationsSpendNoAncestorQueries() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val root = BilateralAncestralDeque.create(arena)
    val deque = bilateralEightValueDeque(arena)

    bilateralQueries(arena, 0, "Reading the size") { deque.size }
    bilateralQueries(arena, 0, "Reading emptiness") { deque.isEmpty }
    bilateralQueries(arena, 0, "Reading the front") { deque.front() }
    bilateralQueries(arena, 0, "Reading the back") { deque.back() }
    bilateralQueries(arena, 0, "Reversing") { deque.reverse() }
    bilateralQueries(arena, 0, "Clearing") { deque.clear() }
    bilateralQueries(arena, 0, "Adding at the front") { deque.addFirst(0) }
    bilateralQueries(arena, 0, "Adding at the back") { deque.addLast(9) }
    bilateralQueries(arena, 0, "Copying to a list") { deque.toList() }
    bilateralQueries(arena, 0, "Enumerating") { bilateralIterated(deque) }
    bilateralQueries(arena, 0, "Auditing an empty handle") { root.validateStructure() }

    // Endpoint reads stay query-free when one arm is empty and the cached base carries the answer.
    val rightOnly = root.addLast(1).addLast(2).addLast(3)
    val leftOnly = root.addFirst(3).addFirst(2).addFirst(1)
    bilateralQueries(arena, 0, "Reading a right-only front") { rightOnly.front() }
    bilateralQueries(arena, 0, "Reading a right-only back") { rightOnly.back() }
    bilateralQueries(arena, 0, "Reading a left-only front") { leftOnly.front() }
    bilateralQueries(arena, 0, "Reading a left-only back") { leftOnly.back() }

    val before = arena.statistics()
    bilateralAssertDeque(listOf(1, 2, 3), rightOnly)
    bilateralAssertDeque(listOf(1, 2, 3), leftOnly)
    val after = arena.statistics()
    bilateralCheckEquals(
        before.publishedNodeCount,
        after.publishedNodeCount,
        "Reading publishes no nodes.",
    )
    bilateralCheckEquals(before.addLeafCount, after.addLeafCount, "Reading adds no leaves.")
    // Eight for the deque, one each for the probe additions, and three for each single-arm handle.
    bilateralCheckEquals(16, deque.arena.publishedNodeCount, "The shared arena keeps every node.")
}

private fun bilateralIndexingSpendsOneQueryAwayFromTheCachedEndpoints() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val root = BilateralAncestralDeque.create(arena)
    val deque = bilateralEightValueDeque(arena)

    // Visible index 0, the end of the left interval, the first right value, and the last visible
    // index are the four cached endpoints; every other index costs exactly one query.
    val expectedProfile = listOf(0, 1, 1, 0, 0, 1, 1, 0)
    for (index in 0 until deque.size) {
        val value = bilateralQueries(arena, expectedProfile[index], "Indexing $index") { deque[index] }
        bilateralCheckEquals(index + 1, value, "Indexed value at $index.")
    }
    bilateralQueries(arena, 0, "Indexing past the end") { deque[deque.size] }
    bilateralQueries(arena, 0, "Indexing before the start") { deque[-1] }

    val rightOnly = root.addLast(1).addLast(2).addLast(3).addLast(4)
    val rightProfile = listOf(0, 1, 1, 0)
    for (index in 0 until rightOnly.size) {
        val value = bilateralQueries(arena, rightProfile[index], "Right-only index $index") {
            rightOnly[index]
        }
        bilateralCheckEquals(index + 1, value, "Right-only value at $index.")
    }

    val leftOnly = root.addFirst(4).addFirst(3).addFirst(2).addFirst(1)
    val leftProfile = listOf(0, 1, 1, 0)
    for (index in 0 until leftOnly.size) {
        val value = bilateralQueries(arena, leftProfile[index], "Left-only index $index") {
            leftOnly[index]
        }
        bilateralCheckEquals(index + 1, value, "Left-only value at $index.")
    }
}

private fun bilateralSlicingAndSplittingMatchTheirExactQueryProfiles() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val deque = bilateralEightValueDeque(arena)
    val expected = (1..8).toList()

    // Row `start` holds the exact query count for each `count` in `0..8 - start`. The maximum is
    // two, including every cross-arm entry, where each arm spends at most one.
    val sliceProfile = listOf(
        listOf(0, 0, 1, 1, 0, 0, 1, 1, 0),
        listOf(0, 1, 2, 1, 1, 2, 2, 1),
        listOf(0, 1, 1, 1, 2, 2, 1),
        listOf(0, 1, 1, 2, 2, 1),
        listOf(0, 0, 1, 1, 0),
        listOf(0, 1, 2, 1),
        listOf(0, 1, 1),
        listOf(0, 1),
        listOf(0),
    )

    for (start in 0..deque.size) {
        bilateralCheckEquals(
            deque.size - start + 1,
            sliceProfile[start].size,
            "The slice profile row for $start.",
        )
        for (count in 0..deque.size - start) {
            val slice = bilateralQueries(arena, sliceProfile[start][count], "Slicing ($start, $count)") {
                deque.slice(start, count)!!
            }
            bilateralCheckEquals(
                expected.subList(start, start + count),
                slice.toList(),
                "Sliced values ($start, $count).",
            )
        }
    }

    val splitProfile = listOf(0, 1, 2, 2, 0, 1, 2, 2, 0)
    for (boundary in 0..deque.size) {
        val split = bilateralQueries(arena, splitProfile[boundary], "Splitting at $boundary") {
            deque.splitAt(boundary)!!
        }
        bilateralCheckEquals(expected.subList(0, boundary), split.left.toList(), "Split prefix.")
        bilateralCheckEquals(
            expected.subList(boundary, expected.size),
            split.right.toList(),
            "Split suffix.",
        )
    }

    for (count in 0..deque.size) {
        bilateralQueries(arena, sliceProfile[0][count], "Taking $count") { deque.take(count)!! }
        bilateralQueries(arena, sliceProfile[count][deque.size - count], "Dropping $count") {
            deque.drop(count)!!
        }
    }

    // Rejected boundaries validate before touching the arena.
    val before = arena.statistics()
    bilateralCheck(deque.slice(-1, 0) == null, "A negative slice index is rejected.")
    bilateralCheck(deque.slice(0, -1) == null, "A negative slice count is rejected.")
    bilateralCheck(deque.slice(9, 0) == null, "An out-of-range slice index is rejected.")
    bilateralCheck(deque.slice(7, 2) == null, "An overlong slice is rejected.")
    bilateralCheck(deque.slice(Int.MAX_VALUE, 1) == null, "A saturated slice index is rejected.")
    bilateralCheck(deque.slice(1, Int.MAX_VALUE) == null, "A saturated slice count is rejected.")
    bilateralCheck(deque.take(9) == null, "An overlong prefix is rejected.")
    bilateralCheck(deque.drop(9) == null, "An overlong suffix is rejected.")
    bilateralCheck(deque.splitAt(9) == null, "An overlong boundary is rejected.")
    val after = arena.statistics()
    bilateralCheckEquals(
        before.ancestorQueryCount,
        after.ancestorQueryCount,
        "A rejected boundary queries nothing.",
    )
    bilateralCheckEquals(
        before.publishedNodeCount,
        after.publishedNodeCount,
        "A rejected boundary publishes nothing.",
    )
    bilateralAssertDeque(expected, deque)
}

private fun bilateralRemovalsMatchTheirExactQueryProfiles() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val root = BilateralAncestralDeque.create(arena)
    val deque = bilateralEightValueDeque(arena)

    // A removal on its own nonempty arm moves one cached tail to its parent and queries nothing.
    val leftOwn = bilateralQueries(arena, 0, "Removing the first of a bilateral deque") {
        deque.removeFirst()!!
    }
    val rightOwn = bilateralQueries(arena, 0, "Removing the last of a bilateral deque") {
        deque.removeLast()!!
    }
    bilateralAssertDeque((2..8).toList(), leftOwn)
    bilateralAssertDeque((1..7).toList(), rightOwn)

    // Crossing the centre selects the next cached base with one query, except for the last two
    // values, where the count == 2 shortcut and the singleton shortcut both answer from cache.
    val crossingProfile = listOf(1, 1, 0, 0)
    var rightOnly = root.addLast(10).addLast(11).addLast(12).addLast(13)
    for (step in crossingProfile.indices) {
        rightOnly = bilateralQueries(arena, crossingProfile[step], "Right-only removal $step") {
            rightOnly.removeFirst()!!
        }
    }
    bilateralCheck(rightOnly.isEmpty, "Repeated crossing removal empties the deque.")

    var leftOnly = root.addFirst(13).addFirst(12).addFirst(11).addFirst(10)
    for (step in crossingProfile.indices) {
        leftOnly = bilateralQueries(arena, crossingProfile[step], "Left-only removal $step") {
            leftOnly.removeLast()!!
        }
    }
    bilateralCheck(leftOnly.isEmpty, "Repeated crossing removal empties the reversed deque.")

    // The value-returning removals cost exactly what the plain removals do.
    val singleRight = root.addLast(1).addLast(2)
    bilateralQueries(arena, 0, "Popping a two-value right arm") { singleRight.tryRemoveFirst()!! }
    val deepRight = root.addLast(1).addLast(2).addLast(3)
    val popped = bilateralQueries(arena, 1, "Popping a three-value right arm") {
        deepRight.tryRemoveFirst()!!
    }
    bilateralCheckEquals(1, popped.value, "The popped crossing value.")
    bilateralAssertDeque(listOf(2, 3), popped.rest)
    bilateralQueries(arena, 0, "Popping an empty deque") { root.tryRemoveFirst() }
    bilateralQueries(arena, 0, "Removing from an empty deque") { root.removeLast() }
}

private fun bilateralValidationReportsCountsAndRejectsAMisreportingArena() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val root = BilateralAncestralDeque.create(arena)

    bilateralQueries(arena, 0, "Auditing an empty handle") { root.validateStructure() }

    val rightOnly = root.addLast(1).addLast(2).addLast(3)
    val leftOnly = root.addFirst(3).addFirst(2).addFirst(1)
    val bilateral = root.addFirst(2).addFirst(1).addLast(3).addLast(4)

    val rightStatistics = bilateralQueries(arena, 2, "Auditing one forward interval") {
        rightOnly.validateStructure()
    }
    bilateralCheckEquals(
        BilateralAncestralDequeStatistics(3, 0, 3),
        rightStatistics,
        "Right-only statistics.",
    )
    val leftStatistics = bilateralQueries(arena, 2, "Auditing one reversed interval") {
        leftOnly.validateStructure()
    }
    bilateralCheckEquals(
        BilateralAncestralDequeStatistics(3, 3, 0),
        leftStatistics,
        "Left-only statistics.",
    )
    val bilateralStatistics = bilateralQueries(arena, 4, "Auditing two intervals") {
        bilateral.validateStructure()
    }
    bilateralCheckEquals(
        BilateralAncestralDequeStatistics(4, 2, 2),
        bilateralStatistics,
        "Bilateral statistics.",
    )

    val biased = BilateralBiasedArena(MyersIncrementalAncestorArena())
    val overArena = BilateralAncestralDeque.create(biased).addFirst(2).addFirst(1).addLast(3)
    bilateralAssertDeque(listOf(1, 2, 3), overArena)
    biased.depthBias = 1
    bilateralCheckThrows<IllegalStateException>("A misreported depth fails the audit.") {
        overArena.validateStructure()
    }
    biased.depthBias = 0
    bilateralAssertDeque(listOf(1, 2, 3), overArena)
}

private fun bilateralSharedArenaServesIndependentFamilies() {
    val arena = MyersIncrementalAncestorArena<Int>()
    val root = BilateralAncestralDeque.create(arena)
    val first = root.addLast(1).addLast(2)
    val second = root.addFirst(9).addFirst(8)

    bilateralAssertDeque(listOf(1, 2), first)
    bilateralAssertDeque(listOf(8, 9), second)
    bilateralCheckEquals(4, arena.statistics().publishedNodeCount, "The shared arena node count.")
    bilateralCheckEquals(4, root.arena.publishedNodeCount, "The retained arena node count.")
    bilateralCheck(first.arena === second.arena, "Both families share one arena.")
    bilateralCheck(!first.sharesStorageWith(second), "Distinct branches do not share intervals.")
    bilateralCheck(first.sharesStorageWith(first.reverse().reverse()), "A double reversal shares.")
    // `root` is empty, so `root.clear()` returns the receiver and comparing them cannot fail.
    // Clearing a non-empty handle yields a genuinely new handle that must NOT share its intervals.
    bilateralCheck(!first.sharesStorageWith(first.clear()), "Clearing a non-empty handle does not share.")
}

private fun bilateralConcurrentBranchingPreservesEverySnapshot() {
    val workerCount = 8
    val steps = 240
    val arena = MyersIncrementalAncestorArena<Int>()
    var root = BilateralAncestralDeque.create(arena)
    for (value in 0 until 96) {
        root = root.addLast(value)
    }
    val retainedRoot = root
    val rootModel = (0 until 96).toList()
    val results = arrayOfNulls<BilateralWorkerResult>(workerCount)

    bilateralRunConcurrent("bilateralConcurrentBranching", workerCount) { worker ->
        var deque = retainedRoot
        var model = rootModel
        var additions = 0
        for (step in 0 until steps) {
            when (step % 8) {
                0 -> {
                    val value = -1 - worker * steps - step
                    deque = deque.addFirst(value)
                    model = listOf(value) + model
                    additions++
                }
                1 -> {
                    val value = 10_000 + worker * steps + step
                    deque = deque.addLast(value)
                    model = model + listOf(value)
                    additions++
                }
                2 -> {
                    deque = deque.reverse()
                    model = model.reversed()
                }
                3 -> if (model.isNotEmpty()) {
                    deque = deque.removeFirst()!!
                    model = model.subList(1, model.size)
                }
                4 -> if (model.isNotEmpty()) {
                    deque = deque.removeLast()!!
                    model = model.subList(0, model.size - 1)
                }
                5 -> {
                    val boundary = (worker * 17 + step) % (model.size + 1)
                    val split = deque.splitAt(boundary)!!
                    if ((worker + step) % 2 == 0) {
                        deque = split.left
                        model = model.subList(0, boundary)
                    } else {
                        deque = split.right
                        model = model.subList(boundary, model.size)
                    }
                }
                6 -> bilateralCheckEquals(
                    rootModel,
                    retainedRoot.toList(),
                    "A retained root changed during publication.",
                )
                else -> if (step % 31 == 7) {
                    deque = retainedRoot
                    model = rootModel
                }
            }
        }

        results[worker] = BilateralWorkerResult(deque, model, additions)
    }

    var expectedPublications = rootModel.size
    for (result in results) {
        bilateralCheck(result != null, "Every worker produced a result.")
        bilateralAssertDeque(result!!.model, result.deque)
        expectedPublications += result.additions
    }

    bilateralAssertDeque(rootModel, retainedRoot)
    bilateralCheckEquals(
        expectedPublications,
        arena.statistics().publishedNodeCount,
        "Concurrent branching publishes one node per successful addition.",
    )
}

/** The bilateral ancestral deque test cases. */
internal fun bilateralAncestralDequeTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "bilateralEmptyAndNullableValuesHaveStableFailuresAndIdentity" to
        ::bilateralEmptyAndNullableValuesHaveStableFailuresAndIdentity,
    "bilateralEndpointOperationsRetainEveryPriorBranch" to
        ::bilateralEndpointOperationsRetainEveryPriorBranch,
    "bilateralReverseSwapsOrientationWithoutChangingRetainedVersions" to
        ::bilateralReverseSwapsOrientationWithoutChangingRetainedVersions,
    "bilateralSliceAndSplitCrossingBothArmsRemainGrowable" to
        ::bilateralSliceAndSplitCrossingBothArmsRemainGrowable,
    "bilateralSmallConstructionWordsMatchTheSequenceOracle" to
        ::bilateralSmallConstructionWordsMatchTheSequenceOracle,
    "bilateralRandomizedRetainedBranchesMatchAnImmutableListModel" to
        ::bilateralRandomizedRetainedBranchesMatchAnImmutableListModel,
    "bilateralQueryFreeOperationsSpendNoAncestorQueries" to
        ::bilateralQueryFreeOperationsSpendNoAncestorQueries,
    "bilateralIndexingSpendsOneQueryAwayFromTheCachedEndpoints" to
        ::bilateralIndexingSpendsOneQueryAwayFromTheCachedEndpoints,
    "bilateralSlicingAndSplittingMatchTheirExactQueryProfiles" to
        ::bilateralSlicingAndSplittingMatchTheirExactQueryProfiles,
    "bilateralRemovalsMatchTheirExactQueryProfiles" to
        ::bilateralRemovalsMatchTheirExactQueryProfiles,
    "bilateralValidationReportsCountsAndRejectsAMisreportingArena" to
        ::bilateralValidationReportsCountsAndRejectsAMisreportingArena,
    "bilateralSharedArenaServesIndependentFamilies" to
        ::bilateralSharedArenaServesIndependentFamilies,
    "bilateralConcurrentBranchingPreservesEverySnapshot" to
        ::bilateralConcurrentBranchingPreservesEverySnapshot,
)
