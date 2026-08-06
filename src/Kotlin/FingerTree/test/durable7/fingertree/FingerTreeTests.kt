/*
 * Tests for the measured tree and deque core: measured splitting and locating, positional edits,
 * and structure sharing between versions.
 */
package durable7.fingertree

import java.util.Collections
import java.math.BigDecimal

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

private inline fun <reified T : Throwable> checkThrows(message: String, action: () -> Unit) {
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

private fun realTreeSubstrateBalancesAndSharesAcrossFacades() {
    val values = (0 until 4096).toList()
    val deque = PersistentDeque.from(values)
    val dequeEdit = deque.setItem(2048, -1) ?: throw AssertionError("deque edit")
    check(deque.debugIsBalanced() && dequeEdit.debugIsBalanced(), "deque structural invariant")
    check(deque.sharesStorageWith(dequeEdit), "deque edit shares untouched nodes")

    val measuredTree = FingerTree.from(values, SizeMeasure<Int>())
    val measuredTreeEdit = measuredTree.insertAt(2048, -1) ?: throw AssertionError("tree insert")
    check(measuredTree.debugIsBalanced() && measuredTreeEdit.debugIsBalanced(), "measured tree structural invariant")
    check(measuredTree.sharesStorageWith(measuredTreeEdit), "measured tree insert shares untouched nodes")
    checkEquals(4097, measuredTreeEdit.measure(), "cached size measure after insert")
    val compatible = FingerTree.from(listOf(4096, 4097), SizeMeasure<Int>())
    checkEquals(4098, measuredTree.concat(compatible).measure(), "value-equal policies concatenate")

    val descending = values.asReversed()
    val bag = SortedBag.from(descending)
    val bagEdit = bag.add(2048)
    check(bag.debugIsBalanced() && bagEdit.debugIsBalanced(), "sorted bag structural invariant")
    check(bag.sharesStorageWith(bagEdit), "sorted bag edit shares untouched nodes")
    val set = SortedSet.from(descending)
    val setEdit = set.add(5000)
    check(set.debugIsBalanced() && setEdit.debugIsBalanced(), "sorted set structural invariant")
    check(set.sharesStorageWith(setEdit), "sorted set edit shares untouched nodes")
    val map = SortedMap.from(descending.map { it to -it })
    val mapEdit = map.setItem(2048, 1)
    check(map.debugIsBalanced() && mapEdit.debugIsBalanced(), "sorted map structural invariant")
    check(map.sharesStorageWith(mapEdit), "sorted map edit shares untouched nodes")

    var queue = PriorityQueue.empty<Int, Int>()
    for (value in values) {
        queue = queue.enqueue(value, 4096 - value)
    }
    val queueEdit = queue.enqueue(-1, -1)
    check(queue.debugIsBalanced() && queueEdit.debugIsBalanced(), "priority queue structural invariant")
    check(queue.sharesStorageWith(queueEdit), "priority enqueue shares untouched nodes")
    checkEquals(-1, queueEdit.peekEntry()?.value, "cached priority summary")

    val intervalTree = IntervalTree.from(values.map { Interval(it * 2, it * 2 + 1) })
    val intervalEdit = intervalTree.insert(Interval(4095, 10000))
    check(intervalTree.debugIsBalanced() && intervalEdit.debugIsBalanced(), "interval tree structural invariant")
    check(intervalTree.sharesStorageWith(intervalEdit), "interval insertion shares untouched nodes")
    checkEquals(Interval(4095, 10000), intervalEdit.findOverlap(Interval(9000, 9001)), "max-high search")

    val rope = Rope.from(values)
    val ropeEdit = rope.setItem(2048, -1) ?: throw AssertionError("rope set")
    check(rope.debugIsBalanced() && ropeEdit.debugIsBalanced(), "rope structural invariant")
    check(rope.sharesStorageWith(ropeEdit), "rope edit shares untouched nodes")
    val measuredRope = MeasuredRope.from(values, IntSumMeasure)
    val measuredRopeEdit = measuredRope.setItem(2048, -1) ?: throw AssertionError("measured rope set")
    check(measuredRope.debugIsBalanced() && measuredRopeEdit.debugIsBalanced(), "measured rope structural invariant")
    check(measuredRope.sharesStorageWith(measuredRopeEdit), "measured rope edit shares untouched nodes")
    checkEquals(values.sum() - 2048 - 1, measuredRopeEdit.measure(), "cached rope measure after set")
    val text = TextRope.fromText((0 until 4096).joinToString("\n"))
    check(text.debugIsBalanced(), "text rope measured structural invariant")
    checkEquals(4096, text.lineCount(), "text rope cached newline measure")
}

private fun generatedRealTreeEditsMatchListModel() {
    var state = 0x13579BDFL
    fun nextInt(bound: Int): Int {
        state = state * 6364136223846793005L + 1442695040888963407L
        return if (bound == 0) 0 else ((state ushr 32).toInt().toUInt().toLong() % bound).toInt()
    }

    var actual = PersistentDeque.empty<Int>()
    val model = ArrayList<Int>()
    repeat(5000) { step ->
        when (nextInt(5)) {
            0 -> {
                val index = nextInt(model.size + 1)
                val value = nextInt(100000)
                actual = actual.insertAt(index, value) ?: throw AssertionError("generated insert")
                model.add(index, value)
            }
            1 -> if (model.isNotEmpty()) {
                val index = nextInt(model.size)
                actual = actual.removeAt(index) ?: throw AssertionError("generated remove")
                model.removeAt(index)
            }
            2 -> if (model.isNotEmpty()) {
                val index = nextInt(model.size)
                val value = -nextInt(100000)
                actual = actual.setItem(index, value) ?: throw AssertionError("generated set")
                model[index] = value
            }
            3 -> {
                val value = nextInt(100000)
                actual = actual.append(value)
                model.add(value)
            }
            else -> {
                val index = nextInt(model.size + 1)
                val split = actual.splitAt(index) ?: throw AssertionError("generated split")
                actual = split.left.concat(split.right)
            }
        }

        check(actual.debugIsBalanced(), "generated structural invariant at step $step")
        if (step % 64 == 0) {
            checkEquals(model, actual.toList(), "generated tree model at step $step")
        }
    }
    checkEquals(model, actual.toList(), "generated tree final model")
}

private fun largeRealTreeConstructionKeepsCachedMeasures() {
    val count = 100_000
    val values = 0 until count
    val deque = PersistentDeque.from(values)
    val tree = FingerTree.from(values, SizeMeasure<Int>())
    checkEquals(count, deque.size, "large deque size")
    checkEquals(count - 1, deque[count - 1], "large deque tail")
    check(deque.debugIsBalanced(), "large deque structural invariant")
    checkEquals(count, tree.measure(), "large measured tree cached size")
    check(tree.debugIsBalanced(), "large measured tree structural invariant")
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

private fun sortedMapSetItemStoresTheSuppliedKey() {
    // Ordering compares case-insensitively, so the keys are comparer-equal
    // but distinct; the C# reference stores the supplied key on setItem.
    val map = SortedMap.empty<String, Int>(String.CASE_INSENSITIVE_ORDER)
        .setItem("key", 1)
        .setItem("KEY", 2)

    checkEquals(1, map.size, "comparer-equal keys occupy one entry")
    checkEquals(SortedMapEntry("KEY", 2), map.entryAt(0), "setItem stores the supplied key and value")
}

private fun intervalTreeInsertsNewEqualLowIntervalsFirst() {
    // Matches the C# reference: insert splits at the first stored interval
    // with low >= the new low, so newer equal-low intervals come first.
    val tree = IntervalTree.empty<Int>()
        .insert(Interval(1, 3))
        .insert(Interval(1, 5))
        .insert(Interval(1, 4))
        .insert(Interval(0, 9))

    checkEquals(
        listOf(Interval(0, 9), Interval(1, 4), Interval(1, 5), Interval(1, 3)),
        tree.toList(),
        "equal-low tie order",
    )

    check(tree.contains(Interval(1, 5)), "membership within the equal-low run")
    check(!tree.contains(Interval(1, 6)), "absent interval")
    val removed = tree.remove(Interval(1, 5))
    checkEquals(3, removed.size, "removal within the equal-low run")
    check(removed.contains(Interval(1, 4)) && removed.contains(Interval(1, 3)), "survivors intact")
}

private fun recurringPortingRegressionsStayLocked() {
    val scaled = IntervalTree.empty<BigDecimal>()
        .insert(Interval(BigDecimal("1.0"), BigDecimal("2.00")))
    check(scaled.contains(Interval(BigDecimal("1.00"), BigDecimal("2.0"))), "interval membership uses comparison")
    checkEquals(0, scaled.remove(Interval(BigDecimal("1.00"), BigDecimal("2.0"))).size, "interval removal uses comparison")

    val text = TextRope.fromText("a\nb")
    checkEquals(null, text.offsetOf(1, Int.MAX_VALUE), "huge text column cannot overflow")

    val ascending = Comparator<Int> { left, right -> left.compareTo(right) }
    val descending = Comparator<Int> { left, right -> right.compareTo(left) }
    checkThrows<IllegalArgumentException>("meld rejects incompatible comparators") {
        PriorityQueue.empty<String, Int>(ascending).enqueue("a", 1)
            .meld(PriorityQueue.empty<String, Int>(descending).enqueue("b", 2))
    }
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
    checkEquals(5, measured.prefixMeasure(2)?.measure, "measured prefix")
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

private class CursorRepresentative(
    val key: Int,
    val label: String,
) {
    companion object {
        var equalityCalls: Int = 0
    }

    override fun equals(other: Any?): Boolean {
        equalityCalls++
        return other is CursorRepresentative && key == other.key
    }

    override fun hashCode(): Int = key
}

private fun ropeCursorHandlesBoundariesNullsAndPersistentEdits() {
    val emptyRope = Rope.empty<Int>()
    val empty = emptyRope.cursor()
    checkEquals(0, empty.position, "empty cursor position")
    checkEquals(0, empty.size, "empty cursor size")
    check(empty.isEmpty, "empty cursor snapshot")
    check(empty.isAtStart && empty.isAtEnd, "empty cursor is both boundaries")
    checkEquals(null, empty.peekPrevious(), "empty cursor previous")
    checkEquals(null, empty.peekNext(), "empty cursor next")
    checkEquals(null, empty.movePrevious(), "empty cursor move previous")
    checkEquals(null, empty.moveNext(), "empty cursor move next")
    check(empty.snapshot() === emptyRope, "empty cursor retains exact source rope")

    val rope = Rope.from(0 until 130)
    val start = rope.cursor()
    checkEquals(0, start.position, "cursor start position")
    checkEquals(rope.size, start.size, "cursor start size")
    check(!start.isEmpty, "nonempty cursor snapshot")
    check(start.isAtStart, "cursor starts at first gap")
    check(!start.isAtEnd, "nonempty start is not end")
    checkEquals(null, start.peekPrevious(), "start has no previous value")
    checkEquals(0, start.peekNext()?.value, "start next value")
    checkEquals(null, start.movePrevious(), "cannot move before start")
    check(start.snapshot() === rope, "cursor retains exact source rope")
    checkEquals(null, rope.cursorAt(-1), "negative cursor gap rejected")
    checkEquals(null, rope.cursorAt(rope.size + 1), "past-end cursor gap rejected")

    val middle = rope.cursorAt(64) ?: throw AssertionError("middle cursor")
    checkEquals(63, middle.peekPrevious()?.value, "middle previous value")
    checkEquals(64, middle.peekNext()?.value, "middle next value")
    check(middle.seek(64) === middle, "same-position seek preserves cursor identity")
    check(middle.moveNext()?.snapshot() === rope, "navigation retains exact source rope")
    checkEquals(null, middle.seek(-1), "negative seek rejected")
    checkEquals(null, middle.seek(rope.size + 1), "past-end seek rejected")
    check(middle.insertRange(emptyList()) === middle, "empty range insertion preserves cursor identity")

    val oneShot = object : Iterable<Int> {
        var iteratorCalls: Int = 0

        override fun iterator(): Iterator<Int> {
            if (iteratorCalls++ != 0) {
                throw AssertionError("cursor range source was enumerated more than once")
            }
            return listOf(-2, -3).iterator()
        }
    }
    val ranged = middle.insertRange(oneShot)
    checkEquals(1, oneShot.iteratorCalls, "cursor range source enumeration count")
    checkEquals(66, ranged.position, "range insertion advances gap")
    checkEquals(-3, ranged.peekPrevious()?.value, "range insertion previous value")
    checkEquals(64, ranged.peekNext()?.value, "range insertion next value")

    val inserted = ranged.insert(-1)
    checkEquals(67, inserted.position, "single insertion advances gap")
    checkEquals(-1, inserted.peekPrevious()?.value, "single insertion previous value")
    val restored = inserted.deletePrevious() ?: throw AssertionError("delete previous")
    checkEquals(ranged.snapshot().toList(), restored.snapshot().toList(), "backspace restores ranged contents")

    val replaced = restored.replaceNext(99_999) ?: throw AssertionError("replace next")
    checkEquals(restored.position, replaced.position, "replacement keeps gap")
    checkEquals(99_999, replaced.peekNext()?.value, "replacement stores supplied value")
    val deleted = replaced.deleteNext() ?: throw AssertionError("delete next")
    checkEquals(replaced.position, deleted.position, "delete next keeps gap")
    checkEquals(65, deleted.peekNext()?.value, "delete next exposes successor")

    val end = rope.cursorAt(rope.size) ?: throw AssertionError("end cursor")
    check(end.isAtEnd, "end cursor reports end")
    checkEquals(129, end.peekPrevious()?.value, "end previous value")
    checkEquals(null, end.peekNext(), "end has no next value")
    checkEquals(null, end.moveNext(), "cannot move beyond end")
    checkEquals(null, start.deletePrevious(), "cannot backspace at start")
    checkEquals(null, end.deleteNext(), "cannot delete next at end")
    checkEquals(null, end.replaceNext(0), "cannot replace next at end")

    val nullable = Rope.from(listOf<Int?>(null, 1)).cursorAt(1) ?: throw AssertionError("nullable cursor")
    val storedNull = nullable.peekPrevious() ?: throw AssertionError("stored null has a peek wrapper")
    checkEquals(null, storedNull.value, "peek wrapper retains stored null")
    checkEquals(1, nullable.peekNext()?.value, "nullable cursor next value")

    val originalRepresentative = CursorRepresentative(7, "original")
    val replacementRepresentative = CursorRepresentative(7, "replacement")
    check(originalRepresentative == replacementRepresentative, "representatives are value-equal")
    val representativeRope = Rope.from(
        listOf(CursorRepresentative(6, "left"), originalRepresentative, CursorRepresentative(8, "right")),
    )
    val representativeSource = representativeRope.cursorAt(1) ?: throw AssertionError("representative cursor")
    CursorRepresentative.equalityCalls = 0
    val representativeEdit = representativeSource.replaceNext(replacementRepresentative)
        ?: throw AssertionError("representative replacement")
    checkEquals(0, CursorRepresentative.equalityCalls, "replacement performs no equality calls")
    check(
        representativeEdit.peekNext()?.value === replacementRepresentative,
        "equal replacement stores the supplied representative",
    )
    check(
        representativeSource.peekNext()?.value === originalRepresentative,
        "equal replacement preserves the source representative",
    )
    check(representativeEdit.snapshot() !== representativeSource.snapshot(), "replacement publishes a new rope")
    check(
        representativeEdit.snapshot().sharesStorageWith(representativeSource.snapshot()),
        "replacement retains untouched rope nodes",
    )

    val retained = Rope.from(0 until 256).cursorAt(128) ?: throw AssertionError("retained cursor")
    val leftBranch = retained.insert(-10)
    val rightBranch = retained.insert(-20)
    checkEquals(128, retained.peekNext()?.value, "retained source remains unchanged")
    checkEquals(-10, leftBranch.snapshot()[128], "left branch value")
    checkEquals(-20, rightBranch.snapshot()[128], "right branch value")
    check(retained.snapshot().sharesStorageWith(leftBranch.snapshot()), "left branch shares source storage")
    check(retained.snapshot().sharesStorageWith(rightBranch.snapshot()), "right branch shares source storage")
    check(ranged.snapshot().debugIsBalanced(), "ranged cursor snapshot remains balanced")
    check(deleted.snapshot().debugIsBalanced(), "deleted cursor snapshot remains balanced")
}

private fun ropeCursorHistoryMatchesGapListModel() {
    var cursor = Rope.empty<Int>().cursor()
    val model = mutableListOf<Int>()
    var position = 0
    var state = 0x9e37_79b9L

    for (step in 0 until 750) {
        state = (state * 1_664_525L + 1_013_904_223L) and 0xffff_ffffL
        when ((state % 7L).toInt()) {
            0 -> {
                cursor = cursor.insert(step)
                model.add(position, step)
                position++
            }
            1 -> {
                val values = listOf(step, -step)
                cursor = cursor.insertRange(values)
                model.addAll(position, values)
                position += values.size
            }
            2 -> {
                val edited = cursor.deletePrevious()
                if (position == 0) {
                    checkEquals(null, edited, "model backspace at start")
                } else {
                    position--
                    model.removeAt(position)
                    cursor = edited ?: throw AssertionError("model backspace")
                }
            }
            3 -> {
                val edited = cursor.deleteNext()
                if (position == model.size) {
                    checkEquals(null, edited, "model delete at end")
                } else {
                    model.removeAt(position)
                    cursor = edited ?: throw AssertionError("model delete next")
                }
            }
            4 -> {
                val edited = cursor.replaceNext(step)
                if (position == model.size) {
                    checkEquals(null, edited, "model replacement at end")
                } else {
                    model[position] = step
                    cursor = edited ?: throw AssertionError("model replace next")
                }
            }
            5 -> {
                if ((state and 0x1000L) == 0L) {
                    val moved = cursor.movePrevious()
                    if (position == 0) {
                        checkEquals(null, moved, "model move before start")
                    } else {
                        position--
                        cursor = moved ?: throw AssertionError("model move previous")
                    }
                } else {
                    val moved = cursor.moveNext()
                    if (position == model.size) {
                        checkEquals(null, moved, "model move beyond end")
                    } else {
                        position++
                        cursor = moved ?: throw AssertionError("model move next")
                    }
                }
            }
            else -> {
                val target = ((state ushr 8) % (model.size + 2).toLong()).toInt()
                val sought = cursor.seek(target)
                if (target > model.size) {
                    checkEquals(null, sought, "model invalid seek")
                } else {
                    position = target
                    cursor = sought ?: throw AssertionError("model seek")
                }
            }
        }

        checkEquals(position, cursor.position, "model cursor position at step $step")
        checkEquals(model.size, cursor.size, "model cursor size at step $step")
        checkEquals(position == 0, cursor.isAtStart, "model cursor start at step $step")
        checkEquals(position == model.size, cursor.isAtEnd, "model cursor end at step $step")
        checkEquals(
            if (position == 0) null else model[position - 1],
            cursor.peekPrevious()?.value,
            "model previous value at step $step",
        )
        checkEquals(
            if (position == model.size) null else model[position],
            cursor.peekNext()?.value,
            "model next value at step $step",
        )
        checkEquals(model, cursor.snapshot().toList(), "model snapshot at step $step")
        check(cursor.snapshot().debugIsBalanced(), "model snapshot balance at step $step")
    }
}

private fun ropeGrowthOverflowIsCheckedAndAtomic() {
    val seed = Rope.from(listOf(7))
    var power = seed
    while (power.size <= Int.MAX_VALUE / 2) {
        power = power.concat(power)
    }

    val powerSize = power.size
    checkEquals(1 shl 30, powerSize, "shared power rope size")
    checkEquals(7, power.front(), "shared power rope front")
    checkEquals(7, power.back(), "shared power rope back")
    checkThrows<ArithmeticException>("overflowing rope concat") { power.concat(power) }
    checkEquals(powerSize, power.size, "overflowing concat preserves source size")

    val almostPower = power.removeAt(0) ?: throw AssertionError("remove from shared power rope")
    val maximum = power.concat(almostPower)
    checkEquals(Int.MAX_VALUE, maximum.size, "maximum representable rope size")
    checkThrows<ArithmeticException>("overflowing rope prepend") { maximum.pushFront(8) }
    checkThrows<ArithmeticException>("overflowing rope append") { maximum.pushBack(8) }
    checkThrows<ArithmeticException>("overflowing rope point insertion") { maximum.insertAt(0, 8) }
    checkThrows<ArithmeticException>("overflowing rope range insertion") {
        maximum.insertRange(0, listOf(8))
    }
    checkThrows<ArithmeticException>("overflowing maximum rope concat") { maximum.concat(seed) }
    checkThrows<ArithmeticException>("overflowing maximum rope prefix concat") { seed.concat(maximum) }
    checkThrows<ArithmeticException>("overflowing cursor point insertion at start") {
        maximum.cursor().insert(8)
    }
    checkThrows<ArithmeticException>("overflowing cursor insertion at start") {
        maximum.cursor().insertRange(listOf(8))
    }
    val maximumEnd = maximum.cursorAt(maximum.size) ?: throw AssertionError("maximum end cursor")
    checkThrows<ArithmeticException>("overflowing cursor position") { maximumEnd.insert(8) }

    checkEquals(Int.MAX_VALUE, maximum.size, "failed growth preserves maximum rope")
    checkEquals(7, maximum.front(), "failed growth preserves maximum front")
    checkEquals(7, maximum.back(), "failed growth preserves maximum back")
    checkEquals(1, seed.size, "failed growth preserves seed size")
    checkEquals(7, seed.front(), "failed growth preserves seed value")
}

private fun ropeCopyToStreamsAcrossChunkBoundaries() {
    val expected = (0 until 200).toList()
    val rope = Rope.fromChunks(expected.chunked(7))
    val measured = MeasuredRope.fromChunks(expected.chunked(7), IntSumMeasure)

    for (start in listOf(0, 1, 6, 7, 13, 97, expected.size - 5)) {
        val window = expected.subList(start, start + 5)
        val destination = MutableList<Int?>(5) { null }
        check(rope.copyTo(start, destination), "rope copyTo at $start")
        checkEquals(window, destination, "rope copy window at $start")

        val measuredDestination = MutableList<Int?>(5) { null }
        check(measured.copyTo(start, measuredDestination), "measured copyTo at $start")
        checkEquals(window, measuredDestination, "measured copy window at $start")
    }

    val full = MutableList<Int?>(expected.size) { null }
    check(rope.copyTo(0, full), "rope full copy")
    checkEquals(expected, full, "rope full copy contents")
    val measuredFull = MutableList<Int?>(expected.size) { null }
    check(measured.copyTo(0, measuredFull), "measured full copy")
    checkEquals(expected, measuredFull, "measured full copy contents")

    check(rope.copyTo(expected.size, mutableListOf<Int?>()), "empty copy at end succeeds")
    check(!rope.copyTo(expected.size - 4, MutableList<Int?>(5) { null }), "overflowing copy rejected")
    check(!rope.copyTo(-1, MutableList<Int?>(1) { null }), "negative start rejected")
    check(!measured.copyTo(expected.size - 4, MutableList<Int?>(5) { null }), "measured overflowing copy rejected")
}

private fun locateReportsFoundForStoredNulls() {
    val tree = FingerTree.from(listOf<Int?>(null, null, null), SizeMeasure())
    val hit = tree.tryLocate { it >= 2 }
    check(hit.found, "stored-null locate reports found")
    checkEquals(1, hit.index, "stored-null locate index")
    checkEquals(1, hit.measureBefore, "stored-null locate prefix")
    checkEquals(null, hit.item, "stored-null locate item")

    val miss = tree.tryLocate { it >= 5 }
    check(!miss.found, "unsatisfied locate reports not found")
    checkEquals(3, miss.index, "unsatisfied locate index")
    checkEquals(null, miss.item, "unsatisfied locate item")

    val rope = MeasuredRope.from(listOf<Int?>(null, null), SizeMeasure())
    val ropeHit = rope.locateByMeasure { it >= 1 }
    check(ropeHit.found, "measured rope stored-null locate reports found")
    checkEquals(0, ropeHit.index, "measured rope stored-null locate index")
    checkEquals(null, ropeHit.value, "measured rope stored-null locate value")
    check(!rope.locateByMeasure { it >= 3 }.found, "measured rope unsatisfied locate reports not found")
}

private fun measuredRopeSupportsThePositionalEditingSurface() {
    val originalValues = (0 until 128).toList()
    val original = MeasuredRope.from(originalValues, IntSumMeasure)
    val model = originalValues.toMutableList()

    var edited = original.pushFront(-1)
    model.add(0, -1)
    edited = edited.insertAt(65, 1_000) ?: throw AssertionError("measured insertAt")
    model.add(65, 1_000)
    edited = edited.insertRange(10, listOf(700, 701, 702)) ?: throw AssertionError("measured insertRange")
    model.addAll(10, listOf(700, 701, 702))
    edited = edited.removeAt(20) ?: throw AssertionError("measured removeAt")
    model.removeAt(20)
    edited = edited.removeRange(30, 5) ?: throw AssertionError("measured removeRange")
    repeat(5) { model.removeAt(30) }
    edited = edited.setItem(0, 999) ?: throw AssertionError("measured setItem")
    model[0] = 999

    checkEquals(model, edited.toList(), "measured editing model")
    checkEquals(model.sum(), edited.measure(), "measured editing total")
    checkEquals(model.first(), edited.front(), "measured front")
    checkEquals(model.last(), edited.back(), "measured back")
    checkEquals(model.take(40).sum(), edited.prefixMeasure(40)?.measure, "measured editing prefix")
    checkEquals(originalValues, original.toList(), "measured original snapshot")
    check(original.sharesStorageWith(edited), "measured edits retain untouched storage")

    val slice = edited.slice(8, 80) ?: throw AssertionError("measured slice")
    checkEquals(model.subList(8, 88), slice.toList(), "measured slice contents")
    checkEquals(model.subList(8, 88).sum(), slice.measure(), "measured slice total")
    checkEquals(slice.toList(), slice.compact().toList(), "measured compact")
    checkEquals(null, edited.insertAt(-1, 0), "measured negative insert")
    checkEquals(null, edited.removeRange(edited.size - 2, Int.MAX_VALUE), "measured overflowing remove")
    checkEquals(null, edited.slice(edited.size - 2, Int.MAX_VALUE), "measured overflowing slice")
}

private fun sortedMapBulkFactoryHonorsComparatorAndLastWins() {
    val map = SortedMap.from(
        listOf("beta" to 1, "ALPHA" to 2, "Beta" to 3, "gamma" to 4, "alpha" to 5),
        String.CASE_INSENSITIVE_ORDER,
    )

    checkEquals(3, map.size, "comparator bulk map size")
    checkEquals(
        listOf(
            SortedMapEntry("alpha", 5),
            SortedMapEntry("Beta", 3),
            SortedMapEntry("gamma", 4),
        ),
        map.toList(),
        "comparator bulk map order and last wins",
    )
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

private class CountingComparator : Comparator<Int> {
    var calls: Int = 0

    override fun compare(left: Int, right: Int): Int {
        calls++
        return left.compareTo(right)
    }
}

private fun sortedBoundsDescendOnceOnLargeCollections() {
    val upper = 65_536
    val target = 53_217
    // The finger tree over 65,536 elements has a spine at most ~17 levels deep. One guided
    // descent inspects at most a digit's four items plus the middle's cached last element per
    // spine level, and at most three children per node level, so the comparison count is bounded
    // by a constant times the depth - unlike binary search over indexing, whose every probe
    // repeats an O(log n) positional walk. The budget is derived from the depth, not guessed.
    val depth = 32 - Integer.numberOfLeadingZeros(upper)
    val budget = 10 * depth
    val comparator = CountingComparator()

    val bag = SortedBag.from((0 until upper) + target, comparator)
    comparator.calls = 0
    checkEquals(target, bag.countLessThan(target), "bag lower bound on 65,537 elements")
    check(comparator.calls <= budget, "bag countLessThan comparisons (${comparator.calls})")
    comparator.calls = 0
    checkEquals(target + 2, bag.countAtMost(target), "bag upper bound with duplicate")
    check(comparator.calls <= budget, "bag countAtMost comparisons (${comparator.calls})")
    comparator.calls = 0
    checkEquals(2, bag.countOf(target), "bag duplicate count")
    check(comparator.calls <= 2 * budget, "bag countOf comparisons (${comparator.calls})")
    for (probe in listOf(0, 1, 25_000, target, upper - 1)) {
        val expectedBelow = if (probe > target) probe + 1 else probe
        checkEquals(expectedBelow, bag.countLessThan(probe), "bag lower bound at $probe")
        checkEquals(expectedBelow + if (probe == target) 2 else 1, bag.countAtMost(probe), "bag upper bound at $probe")
    }
    comparator.calls = 0
    checkEquals(0, bag.countLessThan(-1), "bag lower bound before front")
    checkEquals(upper + 1, bag.countAtMost(upper), "bag upper bound past back")
    check(comparator.calls <= 2 * budget, "bag boundary comparisons (${comparator.calls})")

    val set = SortedSet.from(0 until upper, comparator)
    comparator.calls = 0
    checkEquals(target, set.indexOf(target), "set rank lookup on 65,536 elements")
    check(comparator.calls <= budget + 1, "set indexOf comparisons (${comparator.calls})")
    comparator.calls = 0
    checkEquals(target - 1, set.lower(target), "set lower neighbor")
    checkEquals(target + 1, set.higher(target), "set higher neighbor")
    checkEquals(target, set.floor(target), "set floor")
    checkEquals(target, set.ceiling(target), "set ceiling")
    check(comparator.calls <= 4 * budget, "set navigation comparisons (${comparator.calls})")

    val map = SortedMap.from((0 until upper).map { it to -it }, comparator)
    comparator.calls = 0
    checkEquals(-target, map[target], "map keyed lookup on 65,536 entries")
    check(comparator.calls <= budget + 1, "map keyed lookup comparisons (${comparator.calls})")
    comparator.calls = 0
    checkEquals(SortedMapEntry(target - 1, -(target - 1)), map.lowerEntry(target), "map lower entry")
    checkEquals(SortedMapEntry(target + 1, -(target + 1)), map.higherEntry(target), "map higher entry")
    check(comparator.calls <= 2 * budget, "map navigation comparisons (${comparator.calls})")
}

private fun concurrentReadersObserveConsistentSnapshots() {
    val expected = (0 until 256).toList()
    val deque = PersistentDeque.from(expected)
    val reversible = ReversibleDeque.from(expected).reverse()
    val rope = Rope.from(expected)
    val cursor = rope.cursorAt(128) ?: throw AssertionError("concurrent cursor")
    val measuredValues = (1..128).toList()
    val measured = MeasuredRope.from(measuredValues, IntSumMeasure)
    val measuredCursor = measured.cursorAt(64) ?: throw AssertionError("concurrent measured cursor")
    val text = TextRope.fromText("alpha\nbeta\ngamma")
    val textCursor = text.cursorAt(8) ?: throw AssertionError("concurrent text cursor")

    runConcurrent("fingertree-readers") {
        repeat(128) {
            checkEquals(256, deque.size, "concurrent deque size")
            checkEquals(128, deque[128], "concurrent deque index")
            checkEquals(expected, deque.toList(), "concurrent deque contents")
            checkEquals(expected.asReversed(), reversible.toList(), "concurrent reversible contents")
            checkEquals(256, rope.size, "concurrent rope size")
            checkEquals(128, rope[128], "concurrent rope index")
            checkEquals(expected, rope.toList(), "concurrent rope contents")
            checkEquals(127, cursor.peekPrevious()?.value, "concurrent cursor previous")
            checkEquals(128, cursor.peekNext()?.value, "concurrent cursor next")
            check(cursor.snapshot() === rope, "concurrent cursor snapshot identity")
            checkEquals(128, measured.size, "concurrent measured size")
            checkEquals(measuredValues.sum(), measured.measure(), "concurrent measured total")
            checkEquals(64, measured[63], "concurrent measured index")
            checkEquals(measuredValues.take(64).sum(), measuredCursor.measureBefore, "concurrent measured cursor prefix")
            checkEquals(measuredValues.drop(64).sum(), measuredCursor.measureAfter, "concurrent measured cursor suffix")
            checkEquals(64, measuredCursor.peekPrevious()?.value, "concurrent measured cursor previous")
            checkEquals(65, measuredCursor.peekNext()?.value, "concurrent measured cursor next")
            check(measuredCursor.snapshot() === measured, "concurrent measured cursor snapshot identity")
            checkEquals(LineColumn(1, 2), textCursor.lineColumn(), "concurrent text cursor coordinates")
            check(textCursor.snapshot() === text, "concurrent text cursor snapshot identity")
        }
    }
}

private fun persistentIntervalMapCombinesExactAndOverlapIndexes() {
    val a = Interval(1, 5)
    val b = Interval(1, 3)
    val c = Interval(4, 9)
    val source = PersistentIntervalMap.from(listOf(a to "a", b to "b", c to "c"))
    val replaced = source.set(b, "B")
    val removed = replaced.remove(a)
    val probe = Interval(2, 6)
    checkEquals("B", replaced[b], "interval map replacement")
    checkEquals("b", source[b], "interval map snapshot")
    checkEquals(3, replaced.countOverlaps(probe), "interval map overlap count")
    checkEquals(3, replaced.findOverlaps(probe).size, "interval map overlap enumeration")
    check(!removed.containsKey(a), "interval map removal")
    removed.validateStructure()
}

private fun persistentChunkedBitSetSupportsRankSelectAndSparseAlgebra() {
    val source = PersistentChunkedBitSet.from(listOf(0, 1, 63, 64, 130, 130))
    val edited = source.add(65).remove(64)
    val other = PersistentChunkedBitSet.from(listOf(1, 64, 129))
    checkEquals(listOf(0, 1, 63, 64, 130), source.toList(), "chunked bit-set enumeration")
    checkEquals(5L, source.count, "chunked bit-set count")
    checkEquals(3, source.chunkCount, "chunked bit-set chunks")
    checkEquals(4L, source.rank(64), "chunked bit-set inclusive rank")
    checkEquals(63, source.select(2), "chunked bit-set select")
    checkEquals(listOf(0, 1, 63, 65, 130), edited.toList(), "chunked bit-set point edits")
    checkEquals(listOf(0, 1, 63, 64, 129, 130), source.union(other).toList(), "chunked bit-set union")
    checkEquals(listOf(1, 64), source.intersect(other).toList(), "chunked bit-set intersection")
    checkEquals(listOf(0, 63, 130), source.except(other).toList(), "chunked bit-set difference")
    check(source.contains(64), "chunked bit-set source snapshot")
    edited.validateStructure()
}

/** Entry point running this workspace's test suite and reporting each case. */
public fun main() {
    val tests = listOf(
        "dequePreservesSnapshots" to ::dequePreservesSnapshots,
        "reversibleDequeUsesLogicalOrientation" to ::reversibleDequeUsesLogicalOrientation,
        "reversibleDequeConcatenatesMixedOrientations" to ::reversibleDequeConcatenatesMixedOrientations,
        "reversibleDequeKeepsMixedConcatHistoriesNavigable" to ::reversibleDequeKeepsMixedConcatHistoriesNavigable,
        "measuredTreeSplitsAndLocatesByPrefix" to ::measuredTreeSplitsAndLocatesByPrefix,
        "realTreeSubstrateBalancesAndSharesAcrossFacades" to ::realTreeSubstrateBalancesAndSharesAcrossFacades,
        "generatedRealTreeEditsMatchListModel" to ::generatedRealTreeEditsMatchListModel,
        "largeRealTreeConstructionKeepsCachedMeasures" to ::largeRealTreeConstructionKeepsCachedMeasures,
        "sortedCollectionsKeepOrderAndRelations" to ::sortedCollectionsKeepOrderAndRelations,
        "sortedMapIsLastWinsAndNavigable" to ::sortedMapIsLastWinsAndNavigable,
        "sortedMapSetItemStoresTheSuppliedKey" to ::sortedMapSetItemStoresTheSuppliedKey,
        "priorityQueueDequeuesStably" to ::priorityQueueDequeuesStably,
        "intervalTreeUsesClosedOverlapAndCoalesces" to ::intervalTreeUsesClosedOverlapAndCoalesces,
        "intervalTreeInsertsNewEqualLowIntervalsFirst" to ::intervalTreeInsertsNewEqualLowIntervalsFirst,
        "persistentIntervalMapCombinesExactAndOverlapIndexes" to
            ::persistentIntervalMapCombinesExactAndOverlapIndexes,
        "persistentChunkedBitSetSupportsRankSelectAndSparseAlgebra" to
            ::persistentChunkedBitSetSupportsRankSelectAndSparseAlgebra,
        "recurringPortingRegressionsStayLocked" to ::recurringPortingRegressionsStayLocked,
        "ropesEditAndNavigateText" to ::ropesEditAndNavigateText,
        "ropeCursorHandlesBoundariesNullsAndPersistentEdits" to ::ropeCursorHandlesBoundariesNullsAndPersistentEdits,
        "ropeCursorHistoryMatchesGapListModel" to ::ropeCursorHistoryMatchesGapListModel,
        "ropeGrowthOverflowIsCheckedAndAtomic" to ::ropeGrowthOverflowIsCheckedAndAtomic,
        "ropeCopyToStreamsAcrossChunkBoundaries" to ::ropeCopyToStreamsAcrossChunkBoundaries,
        "locateReportsFoundForStoredNulls" to ::locateReportsFoundForStoredNulls,
        "measuredRopeSupportsThePositionalEditingSurface" to ::measuredRopeSupportsThePositionalEditingSurface,
        "sortedMapBulkFactoryHonorsComparatorAndLastWins" to ::sortedMapBulkFactoryHonorsComparatorAndLastWins,
        "sortedBoundsDescendOnceOnLargeCollections" to ::sortedBoundsDescendOnceOnLargeCollections,
        "overflowingRangesAreRejected" to ::overflowingRangesAreRejected,
        "concurrentReadersObserveConsistentSnapshots" to ::concurrentReadersObserveConsistentSnapshots,
    ) + measuredRopeCursorTestCases() + rrbVectorTestCases() + dabaLiteTestCases() +
        canonicalSortedSetTestCases() + priorityCoreTestCases() + rangeUpdateSequenceTestCases() +
        sequenceCursorTestCases() + orderedSearchCursorTestCases() +
        incrementalAncestorArenaTestCases() +
        ancestralSliceQueueTestCases() + bilateralAncestralDequeTestCases() +
        contextualRankSequenceTestCases() + persistentDeltaMapTestCases() +
        persistentRunDeltaVectorTestCases() + persistentMonotoneActionHeapTestCases() +
        measuredFingerTreeProbeTestCases()

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
