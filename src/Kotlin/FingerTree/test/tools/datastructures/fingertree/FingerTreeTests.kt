package tools.datastructures.fingertree

import java.util.Collections

private fun check(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> checkEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private fun runConcurrent(name: String, workerCount: Int = 8, action: (Int) -> Unit) {
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
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

private fun <T> iterated(values: Iterable<T>): List<T> {
    val result = ArrayList<T>()
    for (value in values) {
        result.add(value)
    }
    return result
}

private fun dequePreservesSnapshots() {
    val empty = PersistentDeque.empty<Int>()
    val one = empty.append(1)
    val two = one.prepend(0).append(2)
    val removed = two.removeAt(1) ?: throw AssertionError("remove")

    checkEquals(emptyList(), empty.toList(), "empty snapshot")
    checkEquals(listOf(1), one.toList(), "one snapshot")
    checkEquals(listOf(0, 1, 2), two.toList(), "two snapshot")
    checkEquals(listOf(0, 2), removed.toList(), "removed")
    checkEquals(listOf(0), two.splitAt(1)?.left?.toList(), "split left")
    checkEquals(listOf(1, 2), two.splitAt(1)?.right?.toList(), "split right")
}

private fun reversibleDequeUsesLogicalOrientation() {
    val deque = ReversibleDeque.from(listOf(1, 2, 3)).reverse().append(0).prepend(4)

    checkEquals(listOf(4, 3, 2, 1, 0), deque.toList(), "reversed append/prepend")
    checkEquals(listOf(4, 3, 2, 1, 0), iterated(deque), "reversed iterator")
    checkEquals(4, deque.front(), "front")
    checkEquals(0, deque.back(), "back")
    check(deque.sharesStorageWith(deque.reverse()), "reverse shares storage")
    checkEquals(listOf(4, 3), deque.splitAt(2)?.first?.toList(), "split first")
    checkEquals(listOf(2, 1, 0), deque.splitAt(2)?.second?.toList(), "split second")
}

private fun reversibleDequeConcatenatesMixedOrientations() {
    val left = ReversibleDeque.from(listOf(0, 1, 2, 3))
    val right = ReversibleDeque.from(listOf(10, 11, 12))
    val cases = listOf(
        "forward/forward" to (left.concat(right) to listOf(0, 1, 2, 3, 10, 11, 12)),
        "reverse/forward" to (left.reverse().concat(right) to listOf(3, 2, 1, 0, 10, 11, 12)),
        "forward/reverse" to (left.concat(right.reverse()) to listOf(0, 1, 2, 3, 12, 11, 10)),
        "reverse/reverse" to (left.reverse().concat(right.reverse()) to listOf(3, 2, 1, 0, 12, 11, 10)),
    )

    for ((name, pair) in cases) {
        val actual = pair.first
        val expected = pair.second
        checkEquals(expected, actual.toList(), "$name concat")
        checkEquals(expected.first(), actual.front(), "$name front")
        checkEquals(expected.last(), actual.back(), "$name back")
        checkEquals(expected[expected.size / 2], actual[expected.size / 2], "$name index")

        val split = actual.splitAt(4) ?: throw AssertionError("$name split")
        checkEquals(expected.take(4), split.first.toList(), "$name split first")
        checkEquals(expected.drop(4), split.second.toList(), "$name split second")
        checkEquals(expected, split.first.concat(split.second).toList(), "$name split rejoin")
    }

    val mixed = left.reverse().concat(right.reverse()).append(99).prepend(-1)
    val mixedExpected = listOf(-1, 3, 2, 1, 0, 12, 11, 10, 99)
    val leftView = mixed.tryViewLeft() ?: throw AssertionError("left view")
    val rightView = mixed.tryViewRight() ?: throw AssertionError("right view")
    checkEquals(-1, leftView.first, "left view value")
    checkEquals(mixedExpected.drop(1), leftView.second.toList(), "left view rest")
    checkEquals(99, rightView.first, "right view value")
    checkEquals(mixedExpected.dropLast(1), rightView.second.toList(), "right view rest")

    val nullable = ReversibleDeque.from(listOf<Int?>(null, 1)).concat(ReversibleDeque.from(listOf<Int?>(null)).reverse())
    val nullableLeft = nullable.tryViewLeft() ?: throw AssertionError("nullable left view")
    checkEquals(listOf(null, 1, null), nullable.toList(), "nullable elements")
    checkEquals(null, nullableLeft.first, "nullable left value")
    checkEquals(listOf(1, null), nullableLeft.second.toList(), "nullable left rest")
}

private fun reversibleDequeKeepsMixedConcatHistoriesNavigable() {
    var actual = ReversibleDeque.empty<Int>()
    var expected = emptyList<Int>()

    for (chunkIndex in 0 until 96) {
        val values = (0 until 5).map { chunkIndex * 5 + it }
        val chunkValues = if (chunkIndex % 2 == 0) values else values.asReversed()
        val chunk = if (chunkIndex % 2 == 0) {
            ReversibleDeque.from(values)
        } else {
            ReversibleDeque.from(values).reverse()
        }

        when (chunkIndex % 3) {
            0 -> {
                actual = actual.concat(chunk)
                expected = expected + chunkValues
            }
            1 -> {
                actual = chunk.concat(actual)
                expected = chunkValues + expected
            }
            else -> {
                actual = actual.reverse().concat(chunk).reverse()
                expected = (expected.asReversed() + chunkValues).asReversed()
            }
        }
    }

    checkEquals(expected.size, actual.size, "mixed history size")
    checkEquals(expected.first(), actual.front(), "mixed history front")
    checkEquals(expected.last(), actual.back(), "mixed history back")
    checkEquals(expected[0], actual[0], "mixed history first index")
    checkEquals(expected[expected.size / 2], actual[actual.size / 2], "mixed history middle index")
    checkEquals(expected.last(), actual[actual.size - 1], "mixed history last index")
    checkEquals(expected, actual.toList(), "mixed history snapshot")
    checkEquals(expected, iterated(actual), "mixed history iterator")

    val middle = actual.size / 2
    val split = actual.splitAt(middle) ?: throw AssertionError("mixed history split")
    checkEquals(expected.take(middle), split.first.toList(), "mixed history split first")
    checkEquals(expected.drop(middle), split.second.toList(), "mixed history split second")
    checkEquals(expected, split.first.concat(split.second).toList(), "mixed history split rejoin")
}

private fun measuredTreeSplitsAndLocatesByPrefix() {
    val tree = FingerTree.from(listOf(2, 3, 5, 7), IntSumMeasure)

    checkEquals(17, tree.measure(), "sum measure")
    checkEquals(5, tree.prefixMeasure(2), "prefix measure")

    val split = tree.split { it >= 6 }
    checkEquals(listOf(2, 3), split.left.toList(), "measure split left")
    checkEquals(listOf(5, 7), split.right.toList(), "measure split right")

    val located = tree.tryLocate { it >= 10 }
    checkEquals(2, located.index, "located index")
    checkEquals(5, located.measureBefore, "located prefix")
    checkEquals(5, located.item, "located item")
}

private fun sortedCollectionsKeepOrderAndRelations() {
    val bag = SortedBag.from(listOf(3, 1, 2, 3, 2, 3))
    checkEquals(listOf(1, 2, 2, 3, 3, 3), bag.toList(), "bag sort")
    checkEquals(3, bag.countOf(3), "bag count")
    checkEquals(listOf(2, 2, 3, 3, 3), bag.getValueRange(2, 3).toList(), "bag value range")
    checkEquals(listOf(1, 2, 2), bag.removeAll(3).toList(), "bag remove all")

    val set = SortedSet.from(listOf(4, 1, 2, 2, 3))
    checkEquals(listOf(1, 2, 3, 4), set.toList(), "set unique sort")
    checkEquals(2, set.floor(2), "floor")
    checkEquals(3, set.higher(2), "higher")
    check(set.intersect(listOf(2, 4, 9)).setEquals(listOf(2, 4)), "intersection")
    check(set.symmetricExcept(listOf(3, 5)).setEquals(listOf(1, 2, 4, 5)), "symmetric except")
}

private fun sortedMapIsLastWinsAndNavigable() {
    val map = SortedMap.from(listOf(3 to "c", 1 to "a", 2 to "b", 2 to "B"))

    checkEquals(listOf(1, 2, 3), map.keys(), "keys sorted")
    checkEquals("B", map[2], "last wins")
    checkEquals(SortedMapEntry(2, "B"), map.floorEntry(2), "floor entry")
    checkEquals(SortedMapEntry(3, "c"), map.higherEntry(2), "higher entry")
    checkEquals(listOf(SortedMapEntry(2, "B"), SortedMapEntry(3, "c")), map.getKeyRange(2, 3).toList(), "key range")

    val duplicate = map.tryInsert(2, "x")
    check(!duplicate.added, "duplicate insert")
    check(map.sharesStorageWith(duplicate.value), "duplicate insert keeps storage")
}

private fun priorityQueueDequeuesStably() {
    val queue = PriorityQueue.empty<String, Int>()
        .enqueue("first", 2)
        .enqueue("second", 1)
        .enqueue("third", 1)

    checkEquals("second" to 1, queue.peek(), "peek")
    val first = queue.dequeue() ?: throw AssertionError("dequeue")
    checkEquals(PriorityEntry("second", 1), first.entry, "first dequeue")
    checkEquals("third" to 1, first.queue.peek(), "stable equal priority")

    val melded = PriorityQueue.empty<String, Int>().enqueue("a", 1)
        .meld(PriorityQueue.empty<String, Int>().enqueue("b", 1))
    checkEquals("a", melded.dequeue()?.entry?.value, "meld stable left")
}

private fun intervalTreeUsesClosedOverlapAndCoalesces() {
    val tree = IntervalTree.from(listOf(Interval(5, 8), Interval(1, 3), Interval(3, 4), Interval(10, 12)))

    checkEquals(Interval(1, 3), tree.findOverlap(Interval(0, 1)), "closed overlap at endpoint")
    checkEquals(Interval(5, 8), tree.findContaining(6), "contains point")
    checkEquals(3, tree.countOverlaps(Interval(2, 5)), "overlap count")
    checkEquals(listOf(Interval(1, 4), Interval(5, 8), Interval(10, 12)), tree.coalesce().toList(), "coalesce")
}

private fun ropesEditAndNavigateText() {
    val rope = Rope.from(listOf("a", "b", "d"))
        .insertAt(2, "c")
        ?.pushBack("e")
        ?: throw AssertionError("insert")

    checkEquals(listOf("a", "b", "c", "d", "e"), rope.toList(), "rope insert")
    checkEquals(listOf("b", "c"), rope.slice(1, 2)?.toList(), "rope slice")
    val copied = MutableList<String?>(3) { null }
    check(rope.copyTo(1, copied), "copyTo")
    checkEquals(listOf("b", "c", "d"), copied, "copy target")

    val measured = MeasuredRope.from(listOf(2, 3, 5, 7), IntSumMeasure)
    checkEquals(17, measured.measure(), "measured rope sum")
    checkEquals(5, measured.prefixMeasure(2), "measured prefix")
    checkEquals(2, measured.locateByMeasure { it >= 6 }.index, "measured locate")

    val text = TextRope.fromText("alpha\nbeta\ngamma")
    checkEquals(3, text.lineCount(), "line count")
    checkEquals(LineColumn(1, 1), text.lineColumnOf(7), "line column")
    checkEquals(6, text.lineStartOffset(1), "line start")
    checkEquals(7, text.offsetOf(1, 1), "offset of")
    checkEquals("beta", text.getLine(1), "get line")
    checkEquals(listOf("alpha", "beta", "gamma"), text.lines(), "lines")

    val builder = RopeBuilder().append("x").appendLine("y").appendChar('z')
    checkEquals("xy\nz", builder.toTextRope().asString(), "builder")
}

private fun overflowingRangesAreRejected() {
    val deque = PersistentDeque.from(listOf(1, 2, 3))
    checkEquals(null, deque.splitRange(2, Int.MAX_VALUE), "deque overflow range")

    val rope = Rope.from(listOf(1, 2, 3))
    checkEquals(null, rope.removeRange(2, Int.MAX_VALUE), "rope overflow remove")
    checkEquals(null, rope.slice(2, Int.MAX_VALUE), "rope overflow slice")

    val bag = SortedBag.from(listOf(1, 2, 3))
    checkEquals(null, bag.getRange(2, Int.MAX_VALUE), "bag overflow range")

    val set = SortedSet.from(listOf(1, 2, 3))
    checkEquals(null, set.getRange(2, Int.MAX_VALUE), "set overflow range")

    val map = SortedMap.from(listOf(1 to "a", 2 to "b", 3 to "c"))
    checkEquals(null, map.getRange(2, Int.MAX_VALUE), "map overflow range")
}

private fun concurrentReadersObserveConsistentSnapshots() {
    val expected = (0 until 256).toList()
    val deque = PersistentDeque.from(expected)
    val reversible = ReversibleDeque.from(expected).reverse()
    val rope = Rope.from(expected)
    val measuredValues = (1..128).toList()
    val measured = MeasuredRope.from(measuredValues, IntSumMeasure)

    runConcurrent("fingertree-readers") {
        repeat(128) {
            checkEquals(256, deque.size, "concurrent deque size")
            checkEquals(128, deque[128], "concurrent deque index")
            checkEquals(expected, deque.toList(), "concurrent deque contents")
            checkEquals(expected.asReversed(), reversible.toList(), "concurrent reversible contents")
            checkEquals(256, rope.size, "concurrent rope size")
            checkEquals(128, rope[128], "concurrent rope index")
            checkEquals(expected, rope.toList(), "concurrent rope contents")
            checkEquals(128, measured.size, "concurrent measured size")
            checkEquals(measuredValues.sum(), measured.measure(), "concurrent measured total")
            checkEquals(64, measured[63], "concurrent measured index")
        }
    }
}

public fun main() {
    val tests = listOf(
        "dequePreservesSnapshots" to ::dequePreservesSnapshots,
        "reversibleDequeUsesLogicalOrientation" to ::reversibleDequeUsesLogicalOrientation,
        "reversibleDequeConcatenatesMixedOrientations" to ::reversibleDequeConcatenatesMixedOrientations,
        "reversibleDequeKeepsMixedConcatHistoriesNavigable" to ::reversibleDequeKeepsMixedConcatHistoriesNavigable,
        "measuredTreeSplitsAndLocatesByPrefix" to ::measuredTreeSplitsAndLocatesByPrefix,
        "sortedCollectionsKeepOrderAndRelations" to ::sortedCollectionsKeepOrderAndRelations,
        "sortedMapIsLastWinsAndNavigable" to ::sortedMapIsLastWinsAndNavigable,
        "priorityQueueDequeuesStably" to ::priorityQueueDequeuesStably,
        "intervalTreeUsesClosedOverlapAndCoalesces" to ::intervalTreeUsesClosedOverlapAndCoalesces,
        "ropesEditAndNavigateText" to ::ropesEditAndNavigateText,
        "overflowingRangesAreRejected" to ::overflowingRangesAreRejected,
        "concurrentReadersObserveConsistentSnapshots" to ::concurrentReadersObserveConsistentSnapshots,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
