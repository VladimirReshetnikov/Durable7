package tools.datastructures.fingertree

private fun measuredCursorCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> measuredCursorCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> measuredCursorCheckThrows(message: String, action: () -> Unit) {
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

private object OrderedTokenMeasure : MeasurePolicy<Int, String> {
    override val empty: String = ""
    override fun measure(element: Int): String = "[$element]"
    override fun combine(left: String, right: String): String = left + right
}

private fun orderedMeasure(values: Iterable<Int>): String = values.joinToString(separator = "", prefix = "", postfix = "") { "[$it]" }

private fun nullableBooleanRank(value: Boolean?): Int = when (value) {
    false -> 0
    null -> 1
    true -> 2
}

private object NullableMaximumMeasure : MeasurePolicy<Int, Boolean?> {
    override val empty: Boolean? = false

    override fun measure(element: Int): Boolean? = when (element) {
        0 -> null
        else -> true
    }

    override fun combine(left: Boolean?, right: Boolean?): Boolean? =
        if (nullableBooleanRank(left) >= nullableBooleanRank(right)) left else right
}

private class MeasuredCursorRepresentative(
    val key: Int,
    val label: String,
) {
    companion object {
        var equalityCalls: Int = 0
    }

    override fun equals(other: Any?): Boolean {
        equalityCalls++
        return other is MeasuredCursorRepresentative && key == other.key
    }

    override fun hashCode(): Int = key
}

private fun measuredCursorHandlesBoundariesNullsIdentityAndBranches() {
    measuredCursorCheckEquals(
        0,
        MeasuredRopeCursor::class.java.constructors.count { !it.isSynthetic },
        "measured cursor callable public constructors",
    )
    measuredCursorCheckEquals(
        0,
        TextRopeCursor::class.java.constructors.count { !it.isSynthetic },
        "text cursor callable public constructors",
    )

    val emptyRope = MeasuredRope.empty<Int, Int>(SizeMeasure())
    val empty = emptyRope.cursor()
    measuredCursorCheckEquals(0, empty.position, "empty position")
    measuredCursorCheckEquals(0, empty.size, "empty size")
    measuredCursorCheck(empty.isEmpty && empty.isAtStart && empty.isAtEnd, "empty boundary state")
    measuredCursorCheckEquals(0, empty.measureBefore, "empty before measure")
    measuredCursorCheckEquals(0, empty.measureAfter, "empty after measure")
    measuredCursorCheckEquals(null, empty.peekPrevious(), "empty previous")
    measuredCursorCheckEquals(null, empty.peekNext(), "empty next")
    measuredCursorCheckEquals(null, empty.movePrevious(), "empty move previous")
    measuredCursorCheckEquals(null, empty.moveNext(), "empty move next")
    measuredCursorCheck(empty.snapshot() === emptyRope, "empty cursor retains exact source")

    val rope = MeasuredRope.from(0 until 256, SizeMeasure())
    val middle = rope.cursorAt(128) ?: throw AssertionError("middle measured cursor")
    measuredCursorCheckEquals(127, middle.peekPrevious()?.value, "middle previous")
    measuredCursorCheckEquals(128, middle.peekNext()?.value, "middle next")
    measuredCursorCheckEquals(128, middle.measureBefore, "middle before measure")
    measuredCursorCheckEquals(128, middle.measureAfter, "middle after measure")
    measuredCursorCheck(middle.seek(128) === middle, "same-position seek preserves identity")
    measuredCursorCheck(middle.insertRange(emptyList()) === middle, "empty insertion preserves identity")
    measuredCursorCheck(middle.moveNext()?.snapshot() === rope, "navigation retains exact source")
    measuredCursorCheckEquals(null, rope.cursorAt(-1), "negative cursor rejected")
    measuredCursorCheckEquals(null, rope.cursorAt(257), "past-end cursor rejected")
    measuredCursorCheckEquals(null, middle.seek(-1), "negative seek rejected")
    measuredCursorCheckEquals(null, middle.seek(257), "past-end seek rejected")

    val oneShot = object : Iterable<Int> {
        var iteratorCalls: Int = 0

        override fun iterator(): Iterator<Int> {
            if (iteratorCalls++ != 0) {
                throw AssertionError("measured cursor range source was consumed more than once")
            }
            return listOf(-2, -3).iterator()
        }
    }
    val ranged = middle.insertRange(oneShot)
    measuredCursorCheckEquals(1, oneShot.iteratorCalls, "one-shot range iteration count")
    measuredCursorCheckEquals(130, ranged.position, "range advances measured gap")
    measuredCursorCheckEquals(-3, ranged.peekPrevious()?.value, "range previous")
    measuredCursorCheckEquals(128, ranged.peekNext()?.value, "range next")
    measuredCursorCheckEquals(130, ranged.measureBefore, "range before measure")
    measuredCursorCheckEquals(128, ranged.measureAfter, "range after measure")

    val inserted = ranged.insert(-1)
    val restored = inserted.deletePrevious() ?: throw AssertionError("measured delete previous")
    measuredCursorCheckEquals(ranged.snapshot().toList(), restored.snapshot().toList(), "backspace restores range")
    val deleted = restored.deleteNext() ?: throw AssertionError("measured delete next")
    measuredCursorCheckEquals(restored.position, deleted.position, "delete next keeps gap")
    measuredCursorCheckEquals(129, deleted.peekNext()?.value, "delete next exposes successor")
    measuredCursorCheckEquals(null, rope.cursor().deletePrevious(), "cannot backspace at start")
    val end = rope.cursorAt(rope.size) ?: throw AssertionError("measured end cursor")
    measuredCursorCheckEquals(null, end.deleteNext(), "cannot delete at end")
    measuredCursorCheckEquals(null, end.replaceNext(0), "cannot replace at end")

    val nullable = MeasuredRope.from(listOf<Int?>(null, 1), SizeMeasure()).cursorAt(1)
        ?: throw AssertionError("nullable measured cursor")
    val storedNull = nullable.peekPrevious() ?: throw AssertionError("stored null requires a peek wrapper")
    measuredCursorCheckEquals(null, storedNull.value, "nullable measured peek value")
    measuredCursorCheckEquals(1, nullable.peekNext()?.value, "nullable measured next")

    val originalRepresentative = MeasuredCursorRepresentative(7, "original")
    val replacementRepresentative = MeasuredCursorRepresentative(7, "replacement")
    val representatives = MeasuredRope.from(
        listOf(
            MeasuredCursorRepresentative(6, "left"),
            originalRepresentative,
            MeasuredCursorRepresentative(8, "right"),
        ),
        SizeMeasure(),
    )
    val representativeSource = representatives.cursorAt(1) ?: throw AssertionError("representative cursor")
    MeasuredCursorRepresentative.equalityCalls = 0
    val representativeEdit = representativeSource.replaceNext(replacementRepresentative)
        ?: throw AssertionError("representative replacement")
    measuredCursorCheckEquals(0, MeasuredCursorRepresentative.equalityCalls, "replacement bypasses equality")
    measuredCursorCheck(
        representativeEdit.peekNext()?.value === replacementRepresentative,
        "replacement stores supplied representative",
    )
    measuredCursorCheck(
        representativeSource.peekNext()?.value === originalRepresentative,
        "source retains original representative",
    )

    val leftBranch = middle.insert(-10)
    val rightBranch = middle.insert(-20)
    measuredCursorCheckEquals(128, middle.peekNext()?.value, "retained source remains unchanged")
    measuredCursorCheckEquals(-10, leftBranch.snapshot()[128], "left branch value")
    measuredCursorCheckEquals(-20, rightBranch.snapshot()[128], "right branch value")
    measuredCursorCheck(rope.sharesStorageWith(leftBranch.snapshot()), "left branch shares source storage")
    measuredCursorCheck(rope.sharesStorageWith(rightBranch.snapshot()), "right branch shares source storage")
    measuredCursorCheck(leftBranch.snapshot().debugIsBalanced(), "left branch remains balanced")
    measuredCursorCheck(rightBranch.snapshot().debugIsBalanced(), "right branch remains balanced")
}

private fun measuredCursorPreservesOrderedNoncommutativeMeasures() {
    val model = (0 until 300).toMutableList()
    val source = MeasuredRope.from(model, OrderedTokenMeasure)
    var cursor = source.cursor()

    for (position in listOf(0, 1, 15, 16, 17, 127, 128, 255, 256, 299, 300)) {
        cursor = cursor.seek(position) ?: throw AssertionError("ordered seek $position")
        measuredCursorCheckEquals(orderedMeasure(model.take(position)), cursor.measureBefore, "ordered prefix at $position")
        measuredCursorCheckEquals(orderedMeasure(model.drop(position)), cursor.measureAfter, "ordered suffix at $position")
        measuredCursorCheckEquals(source.measure(), cursor.measureBefore + cursor.measureAfter, "ordered partition at $position")
    }

    cursor = cursor.seek(128)!!
        .insert(-1)
        .insert(-2)
        .deletePrevious()!!
        .replaceNext(-3)!!
    model.add(128, -1)
    model.add(129, -2)
    model.removeAt(129)
    model[129] = -3

    measuredCursorCheckEquals(129, cursor.position, "ordered edited position")
    measuredCursorCheckEquals(orderedMeasure(model.take(129)), cursor.measureBefore, "ordered edited prefix")
    measuredCursorCheckEquals(orderedMeasure(model.drop(129)), cursor.measureAfter, "ordered edited suffix")
    measuredCursorCheckEquals(model, cursor.snapshot().toList(), "ordered edited snapshot")
    measuredCursorCheckEquals((0 until 300).toList(), source.toList(), "ordered source retained")
}

private fun measuredCursorPreservesNullableMeasureAggregates() {
    val singleton = MeasuredRope.from(listOf(0), NullableMaximumMeasure)
    measuredCursorCheckEquals<Boolean?>(null, singleton.measure(), "nullable singleton aggregate")
    measuredCursorCheckEquals<Boolean?>(null, singleton.cursor().measureAfter, "nullable singleton suffix")
    measuredCursorCheckEquals<Boolean?>(null, singleton.cursorAt(1)?.measureBefore, "nullable singleton prefix")

    val source = MeasuredRope.from(listOf(0, 1), NullableMaximumMeasure)
    val middle = source.cursorAt(1) ?: throw AssertionError("nullable-measure middle cursor")
    measuredCursorCheckEquals<Boolean?>(null, middle.measureBefore, "nullable cached prefix")
    measuredCursorCheckEquals<Boolean?>(true, middle.measureAfter, "nullable cached suffix")

    val suffix = MeasuredRope.from(listOf(1, 0), NullableMaximumMeasure).cursorAt(1)
        ?: throw AssertionError("nullable-measure suffix cursor")
    measuredCursorCheckEquals<Boolean?>(null, suffix.measureAfter, "nullable partial suffix")

    val located = source.cursorByMeasure { it != false }
    measuredCursorCheck(located.found, "nullable aggregate search succeeds")
    measuredCursorCheckEquals(0, located.cursor.position, "nullable aggregate search position")
    measuredCursorCheckEquals<Boolean?>(false, located.cursor.measureBefore, "nullable search prefix")
    measuredCursorCheckEquals<Boolean?>(true, located.cursor.measureAfter, "nullable search suffix")
}

private fun measuredCursorSearchesAbsolutePrefixesAndReturnsEndOnMiss() {
    val source = MeasuredRope.from(listOf(2, 3, 5, 7), IntSumMeasure)
    val receiver = source.cursorAt(3) ?: throw AssertionError("measure receiver")
    val cases = listOf(
        0 to Triple(true, 0, 0),
        1 to Triple(true, 0, 0),
        2 to Triple(true, 0, 0),
        3 to Triple(true, 1, 2),
        5 to Triple(true, 1, 2),
        6 to Triple(true, 2, 5),
        10 to Triple(true, 2, 5),
        11 to Triple(true, 3, 10),
        17 to Triple(true, 3, 10),
        18 to Triple(false, 4, 17),
    )

    for ((threshold, expected) in cases) {
        val factory = source.cursorByMeasure { it >= threshold }
        val seek = receiver.seekByMeasure { it >= threshold }
        for ((name, result) in listOf("factory" to factory, "seek" to seek)) {
            measuredCursorCheckEquals(expected.first, result.found, "$name found at $threshold")
            measuredCursorCheckEquals(expected.second, result.cursor.position, "$name position at $threshold")
            measuredCursorCheckEquals(expected.third, result.cursor.measureBefore, "$name before at $threshold")
            measuredCursorCheckEquals(17 - expected.third, result.cursor.measureAfter, "$name after at $threshold")
            measuredCursorCheck(result.cursor.snapshot() === source, "$name retains exact source at $threshold")
        }
    }

    val same = receiver.seekByMeasure { it >= 11 }
    measuredCursorCheck(same.found, "same-gap measure search succeeds")
    measuredCursorCheck(same.cursor === receiver, "same-gap measure search preserves cursor identity")

    val dirty = receiver.insert(11)
    val dirtyHit = dirty.seekByMeasure { it >= 11 }
    measuredCursorCheck(dirtyHit.found, "edited-version measure search succeeds")
    measuredCursorCheckEquals(3, dirtyHit.cursor.position, "edited-version search is absolute")
    measuredCursorCheckEquals(listOf(2, 3, 5, 11, 7), dirtyHit.cursor.snapshot().toList(), "edited search snapshot")
    measuredCursorCheck(dirtyHit.cursor.snapshot() === dirty.snapshot(), "edited search retains edited version")
    val dirtyMiss = dirty.seekByMeasure { it >= 29 }
    measuredCursorCheck(!dirtyMiss.found, "edited-version measure miss")
    measuredCursorCheckEquals(dirty.size, dirtyMiss.cursor.position, "edited miss returns end cursor")

    val emptyRope = MeasuredRope.empty<Int, Int>(IntSumMeasure)
    val empty = emptyRope.cursorByMeasure { true }
    measuredCursorCheck(!empty.found, "true-at-empty misses on empty rope")
    measuredCursorCheck(empty.cursor.isAtStart && empty.cursor.isAtEnd, "empty miss returns empty end cursor")
    measuredCursorCheckEquals(0, empty.cursor.measureBefore, "empty miss before measure")
    measuredCursorCheckEquals(0, empty.cursor.measureAfter, "empty miss after measure")

    val receiverSnapshot = receiver.snapshot()
    measuredCursorCheckThrows<MeasuredCursorCallbackException>("predicate exception propagates") {
        receiver.seekByMeasure { throw MeasuredCursorCallbackException() }
    }
    measuredCursorCheck(receiver.snapshot() === receiverSnapshot, "predicate failure retains exact receiver snapshot")
    measuredCursorCheckEquals(1, receiver.seekByMeasure { it >= 3 }.cursor.position, "predicate failure remains retryable")
}

private fun measuredCursorHistoryMatchesGapAndMeasureModel() {
    var cursor = MeasuredRope.empty<Int, Int>(SizeMeasure()).cursor()
    val model = mutableListOf<Int>()
    val retained = mutableListOf<Pair<MeasuredRopeCursor<Int, Int>, List<Int>>>()
    var position = 0
    var state = 0x6d2b_79f5L

    for (step in 0 until 750) {
        state = (state * 1_664_525L + 1_013_904_223L) and 0xffff_ffffL
        when ((state % 8L).toInt()) {
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
                    measuredCursorCheckEquals(null, edited, "model backspace at start")
                } else {
                    position--
                    model.removeAt(position)
                    cursor = edited ?: throw AssertionError("model backspace")
                }
            }
            3 -> {
                val edited = cursor.deleteNext()
                if (position == model.size) {
                    measuredCursorCheckEquals(null, edited, "model delete at end")
                } else {
                    model.removeAt(position)
                    cursor = edited ?: throw AssertionError("model delete next")
                }
            }
            4 -> {
                val edited = cursor.replaceNext(step)
                if (position == model.size) {
                    measuredCursorCheckEquals(null, edited, "model replace at end")
                } else {
                    model[position] = step
                    cursor = edited ?: throw AssertionError("model replace next")
                }
            }
            5 -> {
                if ((state and 0x1000L) == 0L) {
                    val moved = cursor.movePrevious()
                    if (position == 0) {
                        measuredCursorCheckEquals(null, moved, "model move before start")
                    } else {
                        position--
                        cursor = moved ?: throw AssertionError("model move previous")
                    }
                } else {
                    val moved = cursor.moveNext()
                    if (position == model.size) {
                        measuredCursorCheckEquals(null, moved, "model move after end")
                    } else {
                        position++
                        cursor = moved ?: throw AssertionError("model move next")
                    }
                }
            }
            6 -> {
                val target = ((state ushr 8) % (model.size + 2).toLong()).toInt()
                val sought = cursor.seek(target)
                if (target > model.size) {
                    measuredCursorCheckEquals(null, sought, "model invalid seek")
                } else {
                    position = target
                    cursor = sought ?: throw AssertionError("model seek")
                }
            }
            else -> {
                retained.add(cursor to model.toList())
                if (retained.size > 32) {
                    retained.removeAt(0)
                }
            }
        }

        measuredCursorCheckEquals(position, cursor.position, "model position at step $step")
        measuredCursorCheckEquals(model.size, cursor.size, "model size at step $step")
        measuredCursorCheckEquals(position, cursor.measureBefore, "model before measure at step $step")
        measuredCursorCheckEquals(model.size - position, cursor.measureAfter, "model after measure at step $step")
        measuredCursorCheckEquals(model, cursor.snapshot().toList(), "model snapshot at step $step")
        measuredCursorCheck(cursor.snapshot().debugIsBalanced(), "model balance at step $step")

        if (retained.isNotEmpty() && step % 29 == 28) {
            val (ancestor, expected) = retained[(state.toInt() and Int.MAX_VALUE) % retained.size]
            val branch = ancestor.insert(-step - 1)
            measuredCursorCheckEquals(expected, ancestor.snapshot().toList(), "retained ancestor at step $step")
            measuredCursorCheckEquals(expected.size + 1, branch.size, "retained branch size at step $step")
        }
    }
}

private class MeasuredCursorCallbackException : RuntimeException()

private class FailingCountMeasure : MeasurePolicy<Int, Int> {
    var measureCalls: Int = 0
        private set
    var combineCalls: Int = 0
        private set
    var equalityCalls: Int = 0
        private set
    private var throwOnMeasure: Int = 0
    private var throwOnCombine: Int = 0
    private var throwOnEquality: Boolean = false

    override val empty: Int = 0

    override fun measure(element: Int): Int {
        measureCalls++
        if (measureCalls == throwOnMeasure) {
            throw MeasuredCursorCallbackException()
        }
        return 1
    }

    override fun combine(left: Int, right: Int): Int {
        combineCalls++
        if (combineCalls == throwOnCombine) {
            throw MeasuredCursorCallbackException()
        }
        return Math.addExact(left, right)
    }

    override fun equals(other: Any?): Boolean {
        equalityCalls++
        if (throwOnEquality) {
            throw MeasuredCursorCallbackException()
        }
        return other is FailingCountMeasure
    }

    override fun hashCode(): Int = FailingCountMeasure::class.hashCode()

    fun reset(
        throwOnMeasure: Int = 0,
        throwOnCombine: Int = 0,
        throwOnEquality: Boolean = false,
    ) {
        measureCalls = 0
        combineCalls = 0
        equalityCalls = 0
        this.throwOnMeasure = throwOnMeasure
        this.throwOnCombine = throwOnCombine
        this.throwOnEquality = throwOnEquality
    }
}

private fun measuredCursorCallbackFailuresAreAtomicAndRetryable() {
    val policy = FailingCountMeasure()
    val source = MeasuredRope.from(0 until 257, policy)
    val cursor = source.cursorAt(128) ?: throw AssertionError("callback cursor")

    policy.reset(throwOnMeasure = 1)
    measuredCursorCheckThrows<MeasuredCursorCallbackException>("insert measure failure") { cursor.insert(-1) }
    policy.reset()
    measuredCursorCheck(cursor.snapshot() === source, "measure failure retains exact source")
    measuredCursorCheckEquals((0 until 257).toList(), cursor.snapshot().toList(), "measure failure retains contents")
    measuredCursorCheckEquals(258, cursor.insert(-1).size, "measure failure retry succeeds")

    policy.reset(throwOnMeasure = 1, throwOnCombine = 1)
    measuredCursorCheck(cursor.snapshot() === source, "snapshot invokes no measure policy callback")
    measuredCursorCheckEquals(0, policy.measureCalls, "snapshot measure callback count")
    measuredCursorCheckEquals(0, policy.combineCalls, "snapshot combine callback count")

    policy.reset(throwOnMeasure = 1)
    measuredCursorCheckThrows<MeasuredCursorCallbackException>("measure search callback failure") {
        cursor.seekByMeasure { it >= 129 }
    }
    policy.reset()
    measuredCursorCheck(cursor.snapshot() === source, "search callback failure retains exact source")
    measuredCursorCheckEquals(128, cursor.seekByMeasure { it >= 129 }.cursor.position, "search failure retry succeeds")

    policy.reset(throwOnCombine = 1)
    measuredCursorCheckThrows<MeasuredCursorCallbackException>("replacement combine failure") {
        cursor.replaceNext(-1)
    }
    policy.reset()
    measuredCursorCheck(cursor.snapshot() === source, "combine failure retains exact source")
    measuredCursorCheckEquals(128, cursor.peekNext()?.value, "combine failure retains next value")
    measuredCursorCheckEquals(-1, cursor.replaceNext(-1)?.peekNext()?.value, "combine failure retry succeeds")
}

private fun measuredRopeAndCursorGrowthOverflowBeforePolicyCallbacks() {
    val policy = FailingCountMeasure()
    val seed = MeasuredRope.from(listOf(7), policy)
    var power = seed
    while (power.size <= Int.MAX_VALUE / 2) {
        power = power.concat(power)
    }

    val almostPower = power.removeAt(0) ?: throw AssertionError("remove from shared measured power")
    val maximum = power.concat(almostPower)
    measuredCursorCheckEquals(Int.MAX_VALUE, maximum.size, "maximum measured rope size")
    measuredCursorCheckEquals(7, maximum.front(), "maximum measured rope front")
    measuredCursorCheckEquals(7, maximum.back(), "maximum measured rope back")

    fun assertPreflight(name: String, action: () -> Unit) {
        policy.reset(throwOnMeasure = 1, throwOnCombine = 1)
        measuredCursorCheckThrows<ArithmeticException>(name, action)
        measuredCursorCheckEquals(0, policy.measureCalls, "$name measure callback count")
        measuredCursorCheckEquals(0, policy.combineCalls, "$name combine callback count")
        measuredCursorCheckEquals(0, policy.equalityCalls, "$name equality callback count")
    }

    assertPreflight("overflowing measured prepend") { maximum.pushFront(8) }
    assertPreflight("overflowing measured append") { maximum.pushBack(8) }
    assertPreflight("overflowing measured point insert") { maximum.insertAt(0, 8) }
    assertPreflight("overflowing measured range insert") { maximum.insertRange(0, listOf(8)) }
    assertPreflight("overflowing measured concat") { maximum.concat(seed) }
    assertPreflight("overflowing measured prefix concat") { seed.concat(maximum) }
    assertPreflight("overflowing measured cursor insert") { maximum.cursor().insert(8) }
    assertPreflight("overflowing measured cursor range") { maximum.cursor().insertRange(listOf(8)) }
    val end = maximum.cursorAt(maximum.size) ?: throw AssertionError("maximum measured end cursor")
    assertPreflight("overflowing measured end cursor insert") { end.insert(8) }

    val distinctPolicySeed = MeasuredRope.from(listOf(8), FailingCountMeasure())
    policy.reset(throwOnMeasure = 1, throwOnCombine = 1, throwOnEquality = true)
    measuredCursorCheckThrows<ArithmeticException>("overflow precedes distinct policy equality") {
        maximum.concat(distinctPolicySeed)
    }
    measuredCursorCheckEquals(0, policy.measureCalls, "distinct policy overflow measure callback count")
    measuredCursorCheckEquals(0, policy.combineCalls, "distinct policy overflow combine callback count")
    measuredCursorCheckEquals(0, policy.equalityCalls, "distinct policy overflow equality callback count")

    policy.reset()
    measuredCursorCheckEquals(Int.MAX_VALUE, maximum.size, "overflow failures preserve maximum rope")
    measuredCursorCheckEquals(1, seed.size, "overflow failures preserve seed")
}

private fun textCursorPreservesUtf16CoordinatesAndTextFacade() {
    val content = "\uD83D\uDE03e\u0301\r\nalpha\n\u03B2"
    val text = TextRope.fromText(content)
    val start = text.cursor()
    measuredCursorCheck(start.snapshot() === text, "text cursor retains exact source facade")
    measuredCursorCheck(start.seek(0) === start, "same-position text seek preserves identity")
    measuredCursorCheck(start.insertRange(emptyList()) === start, "empty text insertion preserves identity")

    for (position in 0..content.length) {
        val cursor = text.cursorAt(position) ?: throw AssertionError("text cursor at $position")
        val before = content.substring(0, position)
        val line = before.count { it == '\n' }
        val lineStart = before.lastIndexOf('\n').let { if (it < 0) 0 else it + 1 }
        val expected = LineColumn(line, position - lineStart)
        measuredCursorCheckEquals(expected, cursor.lineColumn(), "text cursor coordinates at $position")
        measuredCursorCheckEquals(text.lineColumnOf(position), cursor.lineColumn(), "text helper parity at $position")
        measuredCursorCheck(cursor.snapshot() === text, "text navigation retains exact facade at $position")
    }

    measuredCursorCheckEquals(LineColumn(0, 2), text.cursorAt(2)?.lineColumn(), "surrogate pair uses two UTF-16 columns")
    val firstNewline = text.cursorByMeasure { it >= 1 }
    measuredCursorCheck(firstNewline.found, "text newline search succeeds")
    measuredCursorCheckEquals(content.indexOf('\n'), firstNewline.cursor.position, "text newline search gap")
    measuredCursorCheck(firstNewline.cursor.snapshot() === text, "text search retains exact source facade")
    val secondNewline = firstNewline.cursor.seekByMeasure { it >= 2 }
    measuredCursorCheck(secondNewline.found, "text cursor absolute newline seek succeeds")
    measuredCursorCheckEquals(content.lastIndexOf('\n'), secondNewline.cursor.position, "text cursor absolute newline seek gap")
    measuredCursorCheck(secondNewline.cursor.snapshot() === text, "text cursor seek retains exact source facade")

    val inserted = text.cursorAt(content.indexOf('\n'))!!.insert('\n')
    val edited = inserted.snapshot()
    val expectedText = content.substring(0, content.indexOf('\n')) + "\n" + content.substring(content.indexOf('\n'))
    measuredCursorCheckEquals(expectedText, edited.asString(), "edited text cursor snapshot string")
    measuredCursorCheckEquals(expectedText.split('\n'), edited.lines(), "edited text cursor snapshot lines")
    measuredCursorCheckEquals(edited.lineCount(), edited.lines().size, "edited text helper line count")
    measuredCursorCheckEquals(edited.lineColumnOf(inserted.position), inserted.lineColumn(), "edited text cursor coordinates")
    val restored = inserted.deletePrevious() ?: throw AssertionError("text cursor delete previous")
    measuredCursorCheckEquals(content, restored.snapshot().asString(), "text cursor delete restores source content")
    val replaced = text.cursorAt(content.indexOf('\n'))!!.replaceNext('x')
        ?: throw AssertionError("text cursor replacement")
    measuredCursorCheckEquals(
        content.replaceFirst('\n', 'x'),
        replaced.snapshot().asString(),
        "text cursor replacement retains text facade",
    )
    measuredCursorCheckEquals(text.lineCount() - 1, replaced.snapshot().lineCount(), "text replacement updates line measure")
    measuredCursorCheck(text.asString() == content, "text source remains unchanged")

    val generic = MeasuredRope.from(listOf('a'), SizeMeasure<Char>()).cursor()
    measuredCursorCheckThrows<IllegalArgumentException>("generic non-newline cursor rejects line navigation") {
        generic.lineColumn()
    }
}

internal fun measuredRopeCursorTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "measuredCursorHandlesBoundariesNullsIdentityAndBranches" to
        ::measuredCursorHandlesBoundariesNullsIdentityAndBranches,
    "measuredCursorPreservesOrderedNoncommutativeMeasures" to
        ::measuredCursorPreservesOrderedNoncommutativeMeasures,
    "measuredCursorPreservesNullableMeasureAggregates" to
        ::measuredCursorPreservesNullableMeasureAggregates,
    "measuredCursorSearchesAbsolutePrefixesAndReturnsEndOnMiss" to
        ::measuredCursorSearchesAbsolutePrefixesAndReturnsEndOnMiss,
    "measuredCursorHistoryMatchesGapAndMeasureModel" to ::measuredCursorHistoryMatchesGapAndMeasureModel,
    "measuredCursorCallbackFailuresAreAtomicAndRetryable" to
        ::measuredCursorCallbackFailuresAreAtomicAndRetryable,
    "measuredRopeAndCursorGrowthOverflowBeforePolicyCallbacks" to
        ::measuredRopeAndCursorGrowthOverflowBeforePolicyCallbacks,
    "textCursorPreservesUtf16CoordinatesAndTextFacade" to
        ::textCursorPreservesUtf16CoordinatesAndTextFacade,
)
