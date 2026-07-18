package tools.datastructures.fingertree

private fun orderedCursorCheck(value: Boolean, message: String) {
    if (!value) throw AssertionError(message)
}

private fun <T> orderedCursorCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) throw AssertionError("$message Expected <$expected>, actual <$actual>.")
}

private class CursorRanked(val rank: Int, val label: String) : Comparable<CursorRanked> {
    override fun compareTo(other: CursorRanked): Int = rank.compareTo(other.rank)
}

private fun sortedCursorsPreserveGapsNullsAndSelectedOccurrences() {
    val bag = SortedBag.from(
        listOf(
            CursorRanked(1, "low"),
            CursorRanked(2, "first"),
            CursorRanked(2, "second"),
            CursorRanked(2, "third"),
        ),
    )
    val selected = checkNotNull(bag.cursorAt(2))
    orderedCursorCheckEquals("second", selected.peekNext()?.value?.label, "selected duplicate")
    val edited = checkNotNull(selected.deleteNext())
    orderedCursorCheckEquals(2, edited.position, "duplicate deletion position")
    orderedCursorCheckEquals(
        listOf("low", "first", "third"),
        edited.snapshot().map { it.label },
        "exact duplicate occurrence deletion",
    )
    orderedCursorCheckEquals(4, bag.size, "source bag retained")

    val nullComparator = Comparator<Int?> { left, right ->
        when {
            left == null && right == null -> 0
            left == null -> -1
            right == null -> 1
            else -> left.compareTo(right)
        }
    }
    val nullable = SortedBag.from(listOf(null, 2), nullComparator).cursorAt(1)!!
    orderedCursorCheck(nullable.peekPrevious() != null, "stored null remains present")
    orderedCursorCheckEquals(null, nullable.peekPrevious()!!.value, "stored null payload")

    val set = SortedSet.from(listOf(1, 3, 5))
    val gap = set.cursorAtLowerBound(4)
    orderedCursorCheckEquals(2, gap.position, "set lower bound")
    orderedCursorCheckEquals(3, gap.peekPrevious()?.value, "set previous")
    orderedCursorCheckEquals(5, gap.peekNext()?.value, "set next")
    orderedCursorCheckEquals(listOf(1, 3, 4, 5), gap.add(4).snapshot().toList(), "set insertion")

    val map = SortedMap.from(listOf(1 to "one", 3 to "three", 5 to "five"))
    val found = map.findCursor(3)
    orderedCursorCheck(found.found, "map exact cursor")
    val changed = checkNotNull(found.cursor.setNextValue("THREE"))
    val inserted = changed.insert(4, "four")
    orderedCursorCheckEquals(3, inserted.position, "map insertion gap")
    orderedCursorCheckEquals(
        listOf(1 to "one", 3 to "THREE", 4 to "four", 5 to "five"),
        inserted.snapshot().map { it.key to it.value },
        "map cursor edits",
    )
}

private fun canonicalAndPrioritySearchCursorsRetainPolicies() {
    val policy = ZipTreeRankPolicy.create<Int>(seed = 0x5a17L)
    val set = CanonicalSortedSet.from(listOf(1, 3, 5), policy)
    val inserted = set.cursorAtLowerBound(4).add(4)
    orderedCursorCheckEquals(3, inserted.position, "canonical insertion gap")
    orderedCursorCheck(inserted.snapshot().policy === policy, "canonical policy retained")
    orderedCursorCheckEquals(listOf(1, 3, 4, 5), inserted.snapshot().toList(), "canonical edit")

    val nullComparator = Comparator<Int?> { left, right ->
        when {
            left == null && right == null -> 0
            left == null -> -1
            right == null -> 1
            else -> left.compareTo(right)
        }
    }
    val nullablePolicy = ZipTreeRankPolicy.create(
        nullComparator,
        rankHash = { value: Int? -> value?.toLong() ?: 0L },
        seed = 71L,
    )
    val nullable = CanonicalSortedSet.from(listOf<Int?>(null, 2), nullablePolicy).findCursor(null)
    orderedCursorCheck(nullable.found, "canonical null search found")
    orderedCursorCheck(nullable.cursor.peekNext() != null, "canonical stored null remains present")
    orderedCursorCheckEquals(null, nullable.cursor.peekNext()!!.value, "canonical stored null payload")

    val queue = PrioritySearchQueue.empty<Int, Int, String>()
        .setItem(1, 30, "one")
        .setItem(3, 10, "three")
        .setItem(5, 20, "five")
    val minimum = queue.cursorAtMinimumPriority()
    orderedCursorCheckEquals(3, minimum.peekNext()?.value?.key, "minimum cursor key")
    val changed = checkNotNull(minimum.setNext(40, "THREE"))
    orderedCursorCheckEquals(5, changed.snapshot().minimum.key, "winner after update")
    val added = changed.tryInsert(4, 5, "four")
    orderedCursorCheck(added.added, "new priority-search key added")
    orderedCursorCheckEquals(3, added.cursor.position, "priority-search insertion gap")
    orderedCursorCheckEquals(4, added.cursor.snapshot().minimum.key, "winner after insertion")
}

private fun intervalCursorsCheckpointQueriesAndDeleteExactOccurrences() {
    val first = Interval(CursorRanked(1, "first-low"), CursorRanked(5, "first-high"))
    val second = Interval(CursorRanked(1, "second-low"), CursorRanked(5, "second-high"))
    val tree = IntervalTree.empty<CursorRanked>()
        .insert(first)
        .insert(second)
        .insert(Interval(CursorRanked(7, "late-low"), CursorRanked(9, "late-high")))
    orderedCursorCheckEquals("second-low", tree.cursorAt(0)?.peekNext()?.low?.label, "equal-low order")
    val selected = checkNotNull(tree.cursorAt(1))
    orderedCursorCheckEquals("first-low", selected.peekNext()?.low?.label, "selected interval")
    val edited = checkNotNull(selected.deleteNext())
    orderedCursorCheckEquals("second-low", edited.peekPrevious()?.low?.label, "remaining duplicate")
    orderedCursorCheckEquals("late-low", edited.peekNext()?.low?.label, "successor after deletion")

    val probe = Interval(CursorRanked(4, "probe-low"), CursorRanked(8, "probe-high"))
    val hit = tree.findOverlapCursor(probe)
    orderedCursorCheck(hit.found, "first overlap")
    orderedCursorCheckEquals(0, hit.cursor.position, "first overlap rank")
    val next = hit.cursor.seekNextOverlap(probe)
    orderedCursorCheck(next.found, "continued overlap")
    orderedCursorCheckEquals(1, next.cursor.position, "continued overlap rank")
}

private fun intervalMapAndBitSetCursorsKeepSourceSnapshots() {
    val map = PersistentIntervalMap.empty<Int, String>()
        .add(Interval(1, 3), "one")
        .add(Interval(5, 8), "five")
        .add(Interval(10, 12), "ten")
    val overlap = map.findOverlapCursor(Interval(2, 11))
    orderedCursorCheck(overlap.found, "map overlap")
    orderedCursorCheckEquals("one", overlap.cursor.peekNext()?.value, "first map overlap")
    val next = overlap.cursor.seekNextOverlap(Interval(2, 11))
    orderedCursorCheckEquals("five", next.cursor.peekNext()?.value, "continued map overlap")
    val changed = checkNotNull(next.cursor.setNextValue("FIVE"))
    orderedCursorCheckEquals("FIVE", changed.peekNext()?.value, "map payload update")
    orderedCursorCheckEquals("five", map[Interval(5, 8)], "map source retained")

    val bits = PersistentChunkedBitSet.from(listOf(1, 64, 130))
    val gap = bits.cursorAtOrAfter(65)
    orderedCursorCheckEquals(2L, gap.position, "bit-set lower bound")
    orderedCursorCheckEquals(64, gap.peekPrevious(), "bit-set previous")
    orderedCursorCheckEquals(130, gap.peekNext(), "bit-set next")
    val inserted = gap.add(100)
    orderedCursorCheckEquals(listOf(1, 64, 100, 130), inserted.snapshot().toList(), "bit-set insertion")
    val deleted = checkNotNull(inserted.deletePrevious())
    orderedCursorCheckEquals(listOf(1, 64, 130), deleted.snapshot().toList(), "bit-set deletion")
    orderedCursorCheckEquals(listOf(1, 64, 130), bits.toList(), "bit-set source retained")
}

internal fun orderedSearchCursorTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "sortedCursorsPreserveGapsNullsAndSelectedOccurrences" to
        ::sortedCursorsPreserveGapsNullsAndSelectedOccurrences,
    "canonicalAndPrioritySearchCursorsRetainPolicies" to ::canonicalAndPrioritySearchCursorsRetainPolicies,
    "intervalCursorsCheckpointQueriesAndDeleteExactOccurrences" to
        ::intervalCursorsCheckpointQueriesAndDeleteExactOccurrences,
    "intervalMapAndBitSetCursorsKeepSourceSnapshots" to ::intervalMapAndBitSetCursorsKeepSourceSnapshots,
)
