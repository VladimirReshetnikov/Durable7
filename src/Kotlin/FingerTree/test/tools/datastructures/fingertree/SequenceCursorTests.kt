package tools.datastructures.fingertree

private fun cursorCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> cursorCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private object CursorAdditiveAlgebra : RangeUpdateAlgebra<Int, Int, Int> {
    override val empty: Int = 0
    override val identityTag: Int = 0

    override fun combine(left: Int, right: Int): Int = left + right

    override fun measure(element: Int): Int = element

    override fun isIdentity(tag: Int): Boolean = tag == 0

    override fun compose(newer: Int, older: Int): Int = newer + older

    override fun applyElement(tag: Int, element: Int): Int = element + tag

    override fun applyMeasure(tag: Int, measure: Int, count: Int): Int = measure + tag * count
}

private fun dequeCursorRetainsVersionsAndDistinguishesStoredNull() {
    val basis = PersistentDeque.from<Int?>(listOf(1, null, 3))
    val cursor = basis.cursorAt(2)!!
    cursorCheck(cursor.peekPrevious() != null, "stored null remains present")
    cursorCheckEquals(null, cursor.peekPrevious()!!.value, "stored null value")
    cursorCheckEquals(3, cursor.peekNext()!!.value, "next value")

    val edited = cursor.insertRange(listOf(7, 8)).deletePrevious()!!.replaceNext(9)!!
    cursorCheckEquals(3, edited.position, "edited position")
    cursorCheckEquals(listOf(1, null, 7, 9), edited.snapshot().toList(), "edited deque")
    cursorCheckEquals(listOf(1, null, 3), basis.toList(), "retained deque")
}

private fun reversibleCursorUsesLogicalOrderAndMapsReverseGap() {
    val cursor = ReversibleDeque.from(listOf(1, 2, 3, 4)).reverse().cursorAt(1)!!
    cursorCheckEquals(4, cursor.peekPrevious()!!.value, "reversible previous")
    cursorCheckEquals(3, cursor.peekNext()!!.value, "reversible next")

    val edited = cursor.insert(9).deleteNext()!!
    cursorCheckEquals(listOf(4, 9, 2, 1), edited.snapshot().toList(), "reversible edit")
    val reversed = edited.reverse()
    cursorCheckEquals(2, reversed.position, "mapped reverse position")
    cursorCheckEquals(listOf(1, 2, 9, 4), reversed.snapshot().toList(), "reversed snapshot")
}

private fun generalCursorExposesMeasuresAndRetainsSource() {
    val tree = FingerTree.from(listOf(2, 3, 5, 7), IntSumMeasure)
    val located = tree.cursorByMeasure { it >= 6 }
    cursorCheck(located.found, "measure cursor found")
    cursorCheckEquals(5, located.cursor.measureBefore, "measure before")
    cursorCheckEquals(12, located.cursor.measureAfter, "measure after")
    cursorCheckEquals(5, located.cursor.peekNext()!!.value, "measured next")

    val edited = located.cursor.insert(11).deleteNext()!!.replaceNext(13)!!
    cursorCheckEquals(listOf(2, 3, 11, 13), edited.snapshot().toList(), "measured edit")
    cursorCheckEquals(listOf(2, 3, 5, 7), tree.toList(), "measured source")
    cursorCheckEquals(11, edited.seekByMeasure { it >= 16 }.cursor.peekNext()!!.value, "measure seek")
}

private fun rrbCursorSplicesVectorsAndKeepsSource() {
    val basis = RrbVector.from(0 until 96)
    val inserted = RrbVector.from(listOf(500, 501, 502))
    val cursor = basis.cursorAt(32)!!.insertRange(inserted)
    cursorCheckEquals(35, cursor.position, "RRB cursor position")
    cursorCheckEquals(
        listOf(30, 31, 500, 501, 502, 32, 33),
        cursor.snapshot().toList().subList(30, 37),
        "RRB splice",
    )
    cursorCheckEquals((0 until 96).toList(), basis.toList(), "RRB retained source")
    cursorCheck(
        basis.leafIdentitiesForTesting().any { it in cursor.snapshot().leafIdentitiesForTesting() },
        "RRB splice retains shared leaves",
    )
}

private fun rangeCursorPreservesMeasuresAndDirectionalTags() {
    val basis = RangeUpdateSequence.from(listOf(1, 2, 3, 4), CursorAdditiveAlgebra)
    val cursor = basis.cursorAt(2)!!
    cursorCheckEquals(3, cursor.measureBefore, "Range measure before")
    cursorCheckEquals(7, cursor.measureAfter, "Range measure after")
    cursorCheckEquals(3, cursor.measurePrevious(2), "Range previous measure")
    cursorCheckEquals(7, cursor.measureNext(2), "Range next measure")

    val edited = cursor.applyPrevious(1, 10).applyNext(2, 20).replaceNext(99)!!
    cursorCheckEquals(2, edited.position, "Range position")
    cursorCheckEquals(listOf(1, 12, 99, 24), edited.snapshot().toList(), "Range edit")
    cursorCheckEquals(listOf(1, 2, 3, 4), basis.toList(), "Range source")
}

private fun generalCursorReportsNullIdentityBoundaryMeasures() {
    // MaxMeasure and MinMeasure are shipped policies whose monoid identity is itself null, so the
    // boundary gaps must report that identity rather than treating it as an out-of-range signal.
    val tree = FingerTree.from(listOf(2, 7, 5), MaxMeasure<Int>())
    val start = tree.cursorAtStart()
    val end = tree.cursorAtEnd()
    cursorCheckEquals(null, start.measureBefore, "max-measure identity before the start gap")
    cursorCheckEquals(7, start.measureAfter, "max-measure suffix at the start gap")
    cursorCheckEquals(7, end.measureBefore, "max-measure prefix at the end gap")
    cursorCheckEquals(null, end.measureAfter, "max-measure identity after the end gap")

    val minimum = FingerTree.from(listOf(2, 7, 5), MinMeasure<Int>())
    cursorCheckEquals(null, minimum.cursorAtStart().measureBefore, "min-measure identity before the start gap")

    val empty = FingerTree.empty(MaxMeasure<Int>())
    cursorCheckEquals(null, empty.cursorAtStart().measureBefore, "empty max-measure prefix")
    cursorCheckEquals(null, empty.cursorAtStart().measureAfter, "empty max-measure suffix")

    // combine(before, after) reconstructs the whole measure at every gap, which is unsatisfiable
    // when one side conflates the identity with a range failure.
    var gap = tree.cursorAtStart()
    for (position in 0..tree.size) {
        cursorCheckEquals(
            tree.measure(),
            tree.policy.combine(gap.measureBefore, gap.measureAfter),
            "max-measure combine law at gap $position",
        )
        gap = gap.moveNext() ?: break
    }
}

private fun sequenceCursorSeekToCurrentPositionRetainsIdentity() {
    val deque = checkNotNull(PersistentDeque.from(listOf(1, 2, 3)).cursorAt(1))
    cursorCheck(deque.seek(1) === deque, "deque seek to the current position retains identity")
    cursorCheck(deque.seek(2) !== deque, "deque seek elsewhere moves the gap")

    val reversible = checkNotNull(ReversibleDeque.from(listOf(1, 2, 3)).cursorAt(1))
    cursorCheck(reversible.seek(1) === reversible, "reversible seek to the current position retains identity")

    val vector = checkNotNull(RrbVector.from(listOf(1, 2, 3)).cursorAt(1))
    cursorCheck(vector.seek(1) === vector, "RRB seek to the current position retains identity")

    val range = checkNotNull(RangeUpdateSequence.from(listOf(1, 2, 3), CursorAdditiveAlgebra).cursorAt(1))
    cursorCheck(range.seek(1) === range, "Range seek to the current position retains identity")

    val tree = FingerTree.from(listOf(2, 3, 5, 7), IntSumMeasure)
    val located = tree.cursorByMeasure { it >= 6 }.cursor
    cursorCheck(
        located.seekByMeasure { it >= 6 }.cursor === located,
        "measured seek to the current gap retains identity",
    )
}

internal fun sequenceCursorTestCases(): List<Pair<String, () -> Unit>> =
    listOf(
        "dequeCursorRetainsVersionsAndDistinguishesStoredNull" to
            ::dequeCursorRetainsVersionsAndDistinguishesStoredNull,
        "reversibleCursorUsesLogicalOrderAndMapsReverseGap" to
            ::reversibleCursorUsesLogicalOrderAndMapsReverseGap,
        "generalCursorExposesMeasuresAndRetainsSource" to ::generalCursorExposesMeasuresAndRetainsSource,
        "generalCursorReportsNullIdentityBoundaryMeasures" to
            ::generalCursorReportsNullIdentityBoundaryMeasures,
        "sequenceCursorSeekToCurrentPositionRetainsIdentity" to
            ::sequenceCursorSeekToCurrentPositionRetainsIdentity,
        "rrbCursorSplicesVectorsAndKeepsSource" to ::rrbCursorSplicesVectorsAndKeepsSource,
        "rangeCursorPreservesMeasuresAndDirectionalTags" to ::rangeCursorPreservesMeasuresAndDirectionalTags,
    )
