/*
 * Tests for the contextual rank sequence. The property under test is that a cached per-state effect
 * table is indistinguishable from replaying the machine over a prefix, across every read path and
 * after arbitrary persistent editing.
 */
package durable7.fingertree

import java.util.Collections
import java.util.Random
import java.util.concurrent.atomic.AtomicLong

/** Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event. */
private object ContextualQuotedCommaMachine : ContextualEventMachine<Char> {
    override val stateCount: Int = 2

    override fun transition(state: Int, element: Char): ContextualEventTransition = when {
        element == '"' -> ContextualEventTransition(1 - state, 0L)
        element == ',' && state == 0 -> ContextualEventTransition(state, 1L)
        else -> ContextualEventTransition(state, 0L)
    }
}

/** Three states, with transitions that emit zero, one, or two events. */
private object ContextualTernaryMachine : ContextualEventMachine<Int> {
    override val stateCount: Int = 3

    override fun transition(state: Int, element: Int): ContextualEventTransition {
        val magnitude = ((element % 3) + 3) % 3
        val next = (state + magnitude) % 3
        return ContextualEventTransition(next, next.toLong())
    }
}

/**
 * Two states whose per-element actions do not commute: `r` resets to state zero while emitting an
 * event, `f` flips the state silently, and every other character emits an event only from state one.
 */
private object ContextualResetFlipMachine : ContextualEventMachine<Char> {
    override val stateCount: Int = 2

    override fun transition(state: Int, element: Char): ContextualEventTransition = when (element) {
        'r' -> ContextualEventTransition(0, 1L)
        'f' -> ContextualEventTransition(1 - state, 0L)
        else -> ContextualEventTransition(state, if (state == 1) 1L else 0L)
    }
}

/** Counts every transition so tests can prove cached queries do not rescan the input. */
private object ContextualCountingMachine : ContextualEventMachine<Int> {
    val calls: AtomicLong = AtomicLong()

    override val stateCount: Int = 2

    override fun transition(state: Int, element: Int): ContextualEventTransition {
        calls.incrementAndGet()
        return ContextualEventTransition(
            (state + (element and 1)) and 1,
            if (((element xor state) and 3) == 0) 1L else 0L,
        )
    }
}

/** Declares one state but leaves it, violating the machine contract. */
private object ContextualInvalidStateMachine : ContextualEventMachine<Int> {
    override val stateCount: Int = 1

    override fun transition(state: Int, element: Int): ContextualEventTransition =
        ContextualEventTransition(1, 0L)
}

/** Emits a negative event count, violating the machine contract. */
private object ContextualNegativeEventMachine : ContextualEventMachine<Int> {
    override val stateCount: Int = 1

    override fun transition(state: Int, element: Int): ContextualEventTransition =
        ContextualEventTransition(0, -1L)
}

/** Declares no states at all, which is a policy bug. */
private object ContextualStatelessMachine : ContextualEventMachine<Int> {
    override val stateCount: Int = 0

    override fun transition(state: Int, element: Int): ContextualEventTransition =
        ContextualEventTransition(state, 0L)
}

/** Emits the largest representable event count, so two elements overflow the composed total. */
private object ContextualHugeEventMachine : ContextualEventMachine<Int> {
    override val stateCount: Int = 1

    override fun transition(state: Int, element: Int): ContextualEventTransition =
        ContextualEventTransition(0, Long.MAX_VALUE)
}

/**
 * A value-equal machine family: two instances built with the same [shift] compare equal and are
 * therefore concatenation-compatible, while different shifts are not.
 */
private data class ContextualShiftMachine(val shift: Int) : ContextualEventMachine<Int> {
    override val stateCount: Int
        get() = 2

    override fun transition(state: Int, element: Int): ContextualEventTransition {
        val next = (state + element + shift) and 1
        return ContextualEventTransition(next, next.toLong())
    }
}

private fun contextualCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> contextualCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> contextualCheckThrows(message: String, action: () -> Unit) {
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

private fun contextualRunConcurrent(name: String, workerCount: Int, action: (Int) -> Unit) {
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

/**
 * Checks a sequence against an independent list plus a direct machine scan, for every initial state,
 * every prefix boundary, and every individual event.
 */
private fun <T> contextualMatchesModel(
    sequence: ContextualRankSequence<T>,
    model: List<T>,
    label: String,
) {
    contextualCheckEquals(model, sequence.toList(), "$label elements.")
    contextualCheckEquals(model, sequence.iterator().asSequence().toList(), "$label iterator.")
    contextualCheckEquals(model.size, sequence.size, "$label size.")
    contextualCheckEquals(model.isEmpty(), sequence.isEmpty, "$label emptiness.")
    contextualCheckEquals(model.size, sequence.validateStructure().count, "$label validated count.")

    val machine = sequence.machine
    for (initialState in 0 until sequence.stateCount) {
        var state = initialState
        var events = 0L
        val expected = ArrayList<ContextualEventLocation>()
        contextualCheckEquals(
            ContextualPrefixSummary(state, events),
            sequence.evaluatePrefix(0, initialState),
            "$label empty prefix from $initialState.",
        )

        for ((index, element) in model.withIndex()) {
            val step = machine.transition(state, element)
            var ordinal = 0L
            while (ordinal < step.eventCount) {
                expected.add(
                    ContextualEventLocation(index, ordinal, state, step.nextState, step.eventCount),
                )
                ordinal++
            }
            state = step.nextState
            events += step.eventCount
            contextualCheckEquals(
                ContextualPrefixSummary(state, events),
                sequence.evaluatePrefix(index + 1, initialState),
                "$label prefix ${index + 1} from $initialState.",
            )
            contextualCheckEquals(
                events,
                sequence.eventRank(index + 1, initialState),
                "$label rank ${index + 1} from $initialState.",
            )
            contextualCheckEquals(element, sequence[index], "$label element $index.")
        }

        contextualCheckEquals(
            ContextualPrefixSummary(state, events),
            sequence.evaluate(initialState),
            "$label evaluate from $initialState.",
        )
        for ((eventIndex, location) in expected.withIndex()) {
            contextualCheckEquals(
                location,
                sequence.trySelectEvent(eventIndex.toLong(), initialState),
                "$label select $eventIndex from $initialState.",
            )
        }
        contextualCheckEquals(
            null,
            sequence.trySelectEvent(expected.size.toLong(), initialState),
            "$label select past the last event from $initialState.",
        )
    }
}

private fun contextualQuotedMachineRanksAndSelectsByContext() {
    val text = ContextualRankSequence.from("\"a,b\",c,d".toList(), ContextualQuotedCommaMachine)

    contextualCheckEquals(ContextualPrefixSummary(0, 2L), text.evaluate(0), "quoted evaluate outside.")
    contextualCheckEquals(ContextualPrefixSummary(1, 1L), text.evaluate(1), "quoted evaluate inside.")
    contextualCheckEquals(0L, text.eventRank(5, 0), "quoted rank before the field comma.")
    contextualCheckEquals(1L, text.eventRank(6, 0), "quoted rank after the field comma.")

    contextualCheckEquals(
        ContextualEventLocation(5, 0L, 0, 0, 1L),
        text.trySelectEvent(0L, 0),
        "quoted select of the first field comma.",
    )
    contextualCheckEquals(7, text.trySelectEvent(1L, 0)?.elementIndex, "quoted select of the second comma.")
    contextualCheckEquals(null, text.trySelectEvent(2L, 0), "quoted select past the last comma.")

    // Starting inside a quoted region makes the comma at index 2 the event instead.
    contextualCheckEquals(2, text.trySelectEvent(0L, 1)?.elementIndex, "quoted select from the inside state.")
    contextualCheckEquals("\"a,b\",c,d".toList(), text.toList(), "quoted snapshot.")

    // Editing publishes a new version and leaves the retained one exactly as it was.
    val edited = text.insertAt(0, '"') ?: error("valid boundary")
    contextualCheckEquals(ContextualPrefixSummary(0, 2L), text.evaluate(0), "quoted source after editing.")
    contextualCheckEquals(ContextualPrefixSummary(1, 1L), edited.evaluate(0), "quoted edited evaluate.")
    contextualMatchesModel(text, "\"a,b\",c,d".toList(), "quoted source")
    contextualMatchesModel(edited, "\"\"a,b\",c,d".toList(), "quoted edited")
}

private fun contextualCompositionIsOrderSensitive() {
    val flip = ContextualRankSequence.from(listOf('f'), ContextualResetFlipMachine)
    val resetting = ContextualRankSequence.from(listOf('x', 'r'), ContextualResetFlipMachine)
    val forward = flip.concat(resetting)
    val backward = resetting.concat(flip)

    // f then x then r: flip to state one, emit one event from state one, then reset and emit again.
    contextualCheckEquals(ContextualPrefixSummary(0, 2L), forward.evaluate(0), "ordered composition.")
    // x then r then f: nothing from state zero, reset emits once, then flip to state one.
    contextualCheckEquals(ContextualPrefixSummary(1, 1L), backward.evaluate(0), "reversed composition.")
    contextualCheck(
        forward.evaluate(0) != backward.evaluate(0),
        "the lifted measure must not be commutative in either component.",
    )
    contextualCheckEquals(listOf('f', 'x', 'r'), forward.toList(), "forward order.")
    contextualCheckEquals(listOf('x', 'r', 'f'), backward.toList(), "backward order.")

    // The cached table must agree with running the machine by hand in sequence order.
    contextualMatchesModel(forward, listOf('f', 'x', 'r'), "forward")
    contextualMatchesModel(backward, listOf('x', 'r', 'f'), "backward")
    for (initialState in 0 until forward.stateCount) {
        contextualCheckEquals(
            flip.evaluate(initialState),
            forward.evaluatePrefix(1, initialState),
            "the one-element prefix agrees with the left operand from $initialState.",
        )
        contextualCheckEquals(
            resetting.evaluate(initialState),
            backward.evaluatePrefix(2, initialState),
            "the two-element prefix agrees with the left operand from $initialState.",
        )
    }

    // Select is likewise order sensitive: the same event rank lands on different elements.
    contextualCheckEquals(1, forward.trySelectEvent(0L, 0)?.elementIndex, "forward first event.")
    contextualCheckEquals(1, backward.trySelectEvent(0L, 0)?.elementIndex, "backward first event.")
    contextualCheckEquals(2, forward.trySelectEvent(1L, 0)?.elementIndex, "forward second event.")
    contextualCheckEquals(null, backward.trySelectEvent(1L, 0), "backward has only one event.")
}

private fun contextualExhaustiveSmallWordsMatchDirectScanner() {
    val alphabet = listOf('"', ',', 'x')
    for (length in 0..6) {
        var code = 0
        val total = generateSequence(1) { it * alphabet.size }.elementAt(length)
        while (code < total) {
            var value = code
            val word = ArrayList<Char>(length)
            repeat(length) {
                word.add(alphabet[value % alphabet.size])
                value /= alphabet.size
            }

            val sequence = ContextualRankSequence.from(word, ContextualQuotedCommaMachine)
            contextualMatchesModel(sequence, word, "exhaustive word $code of length $length")
            code++
        }
    }
}

private fun contextualBoundariesAndInvalidArgumentsReturnNull() {
    val sequence = ContextualRankSequence.from(listOf(2, -3, 4, 0), ContextualTernaryMachine)
    val empty = ContextualRankSequence.empty<Int>(ContextualTernaryMachine)

    for (index in 0..sequence.size) {
        val split = sequence.splitAt(index) ?: error("valid boundary")
        contextualCheckEquals(sequence.toList(), split.left.concat(split.right).toList(), "split rejoin at $index.")
        contextualCheckEquals(sequence.size, split.left.size + split.right.size, "split sizes at $index.")
    }

    contextualCheck(sequence === sequence.concat(empty), "right-empty concat retains the facade.")
    contextualCheck(sequence === empty.concat(sequence), "left-empty concat retains the facade.")
    contextualCheck(sequence === sequence.getRange(0, sequence.size), "full range retains the facade.")
    contextualCheck(sequence === sequence.splitAt(0)?.right, "zero split retains the facade.")
    contextualCheck(sequence === sequence.splitAt(sequence.size)?.left, "end split retains the facade.")
    contextualCheck(
        sequence === ContextualRankSequence.from(sequence, ContextualTernaryMachine),
        "rebuilding from the same machine retains the facade.",
    )

    val located = sequence.trySelectEvent(0L, 0) ?: error("the sequence emits events")
    contextualCheck(
        located.eventIndexInElement < located.elementEventCount,
        "an event ordinal stays inside its element.",
    )
    contextualCheck(
        (0 until sequence.size).any { index ->
            val after = sequence.eventRank(index + 1, 0) ?: error("valid boundary")
            val before = sequence.eventRank(index, 0) ?: error("valid boundary")
            after - before > 1L
        },
        "the ternary machine must exercise multi-event transitions.",
    )

    contextualCheckEquals(null, sequence.evaluate(3), "state past the machine range.")
    contextualCheckEquals(null, sequence.evaluate(-1), "negative state.")
    contextualCheckEquals(null, sequence.evaluatePrefix(0, 3), "prefix with an invalid state.")
    contextualCheckEquals(null, sequence.evaluatePrefix(sequence.size + 1, 0), "prefix past the end.")
    contextualCheckEquals(null, sequence.evaluatePrefix(-1, 0), "negative prefix.")
    contextualCheckEquals(null, sequence.eventRank(sequence.size + 1, 0), "rank past the end.")
    contextualCheckEquals(null, sequence.trySelectEvent(0L, 3), "select with an invalid state.")
    contextualCheckEquals(null, sequence.trySelectEvent(-1L, 0), "select with a negative rank.")
    contextualCheckEquals(null, sequence.trySelectEvent(Long.MAX_VALUE, 0), "select past the last event.")
    contextualCheckEquals(null, sequence[sequence.size], "index past the end.")
    contextualCheckEquals(null, sequence[-1], "negative index.")
    contextualCheckEquals(null, sequence.insertAt(sequence.size + 1, 0), "insert past the end.")
    contextualCheckEquals(null, sequence.insertAt(-1, 0), "insert at a negative boundary.")
    contextualCheckEquals(null, sequence.setItem(sequence.size, 0), "replace past the end.")
    contextualCheckEquals(null, sequence.removeAt(sequence.size), "remove past the end.")
    contextualCheckEquals(null, sequence.splitAt(sequence.size + 1), "split past the end.")
    contextualCheckEquals(null, sequence.splitAt(-1), "split at a negative boundary.")
    contextualCheckEquals(null, sequence.getRange(1, sequence.size), "range past the end.")
    contextualCheckEquals(null, sequence.getRange(0, Int.MAX_VALUE), "range length overflow.")
    contextualCheckEquals(null, sequence.getRange(Int.MAX_VALUE, 1), "range start overflow.")
    contextualCheckEquals(null, sequence.getRange(-1, 1), "negative range start.")

    contextualCheck(empty.isEmpty, "the empty sequence is empty.")
    contextualCheckEquals(ContextualPrefixSummary(2, 0L), empty.evaluate(2), "empty evaluate is the identity.")
    contextualCheckEquals(ContextualPrefixSummary(1, 0L), empty.evaluatePrefix(0, 1), "empty prefix is the identity.")
    contextualCheckEquals(null, empty.trySelectEvent(0L, 0), "the empty sequence emits no events.")
    contextualCheck(empty.getRange(0, 0)?.isEmpty == true, "an empty range of an empty sequence.")
    contextualCheckEquals(0, empty.validateStructure().count, "empty validated count.")
    contextualMatchesModel(empty, emptyList(), "empty")

    val singleton = empty.append(5)
    contextualCheckEquals(1, singleton.size, "singleton size.")
    contextualCheckEquals(5, singleton[0], "singleton element.")
    contextualCheck(singleton.removeAt(0)?.isEmpty == true, "removing the only element empties the sequence.")
    contextualMatchesModel(singleton, listOf(5), "singleton")
    contextualMatchesModel(sequence, listOf(2, -3, 4, 0), "ternary source")
}

private fun contextualRetainedVersionsStayIndependent() {
    val source = ContextualRankSequence.from("a,b,c".toList(), ContextualQuotedCommaMachine)
    val quoted = source.insertAt(0, '"') ?: error("valid boundary")
    val trimmed = source.removeAt(1) ?: error("valid index")
    val replaced = source.setItem(1, '"') ?: error("valid index")

    contextualCheckEquals(ContextualPrefixSummary(0, 2L), source.evaluate(0), "source evaluate.")
    contextualCheckEquals(ContextualPrefixSummary(1, 0L), quoted.evaluate(0), "quoted branch evaluate.")
    contextualCheckEquals(ContextualPrefixSummary(0, 1L), trimmed.evaluate(0), "trimmed branch evaluate.")
    contextualCheckEquals(ContextualPrefixSummary(1, 0L), replaced.evaluate(0), "replaced branch evaluate.")

    contextualMatchesModel(source, "a,b,c".toList(), "branch source")
    contextualMatchesModel(quoted, "\"a,b,c".toList(), "branch quoted")
    contextualMatchesModel(trimmed, "ab,c".toList(), "branch trimmed")
    contextualMatchesModel(replaced, "a\"b,c".toList(), "branch replaced")
    contextualCheck(source.sharesStructureWith(replaced), "a point edit keeps sharing structure.")
}

private fun contextualGeneratedHistoryMatchesListModel() {
    val random = Random(0x434f4e5445585455L)
    val versions = mutableListOf(
        ContextualRankSequence.empty<Int>(ContextualTernaryMachine) to emptyList<Int>(),
    )
    var branchCount = 0

    repeat(300) { step ->
        val parentIndex = random.nextInt(versions.size)
        val parent = versions[parentIndex].first
        val model = versions[parentIndex].second.toMutableList()
        if (parentIndex != versions.size - 1) {
            branchCount++
        }

        val next = when (random.nextInt(8)) {
            0 -> {
                val item = random.nextInt(19) - 9
                model.add(0, item)
                parent.prepend(item)
            }
            1 -> {
                val item = random.nextInt(19) - 9
                model.add(item)
                parent.append(item)
            }
            2 -> {
                val item = random.nextInt(19) - 9
                val index = random.nextInt(model.size + 1)
                model.add(index, item)
                parent.insertAt(index, item) ?: error("valid boundary")
            }
            3 -> if (model.isEmpty()) {
                parent
            } else {
                val index = random.nextInt(model.size)
                val item = random.nextInt(19) - 9
                model[index] = item
                parent.setItem(index, item) ?: error("valid index")
            }
            4 -> if (model.isEmpty()) {
                parent
            } else {
                val index = random.nextInt(model.size)
                model.removeAt(index)
                parent.removeAt(index) ?: error("valid index")
            }
            5 -> {
                val index = random.nextInt(model.size + 1)
                val count = random.nextInt(model.size - index + 1)
                val kept = model.subList(index, index + count).toList()
                model.clear()
                model.addAll(kept)
                parent.getRange(index, count) ?: error("valid range")
            }
            6 -> {
                val boundary = random.nextInt(model.size + 1)
                val split = parent.splitAt(boundary) ?: error("valid boundary")
                split.left.concat(split.right)
            }
            else -> {
                val suffixIndex = random.nextInt(versions.size)
                val suffix = versions[suffixIndex]
                if (model.size + suffix.second.size > 48) {
                    parent
                } else {
                    model.addAll(suffix.second)
                    parent.concat(suffix.first)
                }
            }
        }

        contextualMatchesModel(next, model, "generated step $step")
        versions.add(next to model.toList())
        if (versions.size > 24) {
            versions.removeAt(1 + random.nextInt(versions.size - 2))
        }
    }

    contextualCheck(branchCount > 0, "the generated history branches from retained versions.")
    for ((index, retained) in versions.withIndex()) {
        contextualMatchesModel(retained.first, retained.second, "retained version $index")
    }
}

private fun contextualCachedQueriesDoNotRescanTheInput() {
    val sequence = ContextualRankSequence.from((0 until 4_096).toList(), ContextualCountingMachine)
    val stateCount = sequence.stateCount
    contextualCheckEquals(
        stateCount.toLong() * 4_096L,
        ContextualCountingMachine.calls.get(),
        "construction runs the machine once per element per state.",
    )

    ContextualCountingMachine.calls.set(0L)
    var boundary = 0
    while (boundary <= sequence.size) {
        sequence.eventRank(boundary, boundary and 1) ?: error("valid boundary and state")
        boundary += 17
    }
    contextualCheckEquals(0L, ContextualCountingMachine.calls.get(), "cached prefix ranks must not invoke the machine.")

    val total = sequence.evaluate(0)?.eventCount ?: error("valid state")
    contextualCheck(total > 0L, "the counting machine must emit events.")
    var eventIndex = 0L
    var selected = 0
    while (eventIndex < total) {
        sequence.trySelectEvent(eventIndex, 0) ?: error("valid event rank")
        selected++
        eventIndex += 31L
    }
    contextualCheck(selected > 0, "at least one select must succeed.")
    contextualCheckEquals(0L, ContextualCountingMachine.calls.get(), "cached selects must not invoke the machine.")

    for (index in 0 until sequence.size step 257) {
        contextualCheckEquals(index, sequence[index], "cached indexing.")
    }
    contextualCheckEquals(0L, ContextualCountingMachine.calls.get(), "indexing must not invoke the machine.")

    sequence.append(4_096)
    contextualCheckEquals(
        stateCount.toLong(),
        ContextualCountingMachine.calls.get(),
        "admitting one element runs the machine exactly once per state.",
    )
}

private fun contextualMachineContractViolationsAreAtomic() {
    contextualCheckThrows<IllegalArgumentException>("a zero-state machine must be rejected.") {
        ContextualRankSequence.empty<Int>(ContextualStatelessMachine)
    }
    contextualCheckThrows<IllegalArgumentException>("a zero-state machine must be rejected by from.") {
        ContextualRankSequence.from(listOf(1), ContextualStatelessMachine)
    }

    val invalid = ContextualRankSequence.empty<Int>(ContextualInvalidStateMachine)
    contextualCheckThrows<IllegalStateException>("an out-of-range next state must be rejected.") {
        invalid.append(1)
    }
    contextualCheckThrows<IllegalStateException>("an out-of-range next state must be rejected by from.") {
        ContextualRankSequence.from(listOf(1), ContextualInvalidStateMachine)
    }
    contextualCheck(invalid.isEmpty, "a rejected admission publishes no successor.")

    val negative = ContextualRankSequence.empty<Int>(ContextualNegativeEventMachine)
    contextualCheckThrows<IllegalStateException>("a negative event count must be rejected.") {
        negative.prepend(1)
    }
    contextualCheck(negative.isEmpty, "a rejected negative count publishes no successor.")

    val huge = ContextualRankSequence.from(listOf(1), ContextualHugeEventMachine)
    contextualCheckEquals(
        ContextualPrefixSummary(0, Long.MAX_VALUE),
        huge.evaluate(0),
        "the saturating machine reaches the largest total.",
    )
    // The measured core defers summary combination, matching the reference's lazy finger tree,
    // so the event-count overflow surfaces on the first summary read of a successor rather than
    // inside the edit itself. Failure atomicity is preserved in the form the laziness contract
    // gives it: a raising force publishes nothing, every retained version stays valid, and the
    // read is retryable - both reads below raise, proving the first failure memoized no poison.
    val overflowing = listOf(
        "append" to huge.append(2),
        "prepend" to huge.prepend(2),
        "concat" to huge.concat(huge),
        "insert" to (huge.insertAt(0, 2) ?: throw AssertionError("a valid boundary must insert.")),
    )
    for ((operation, successor) in overflowing) {
        contextualCheckThrows<ArithmeticException>(
            "an event-count overflow via $operation must surface on the first forced summary read.",
        ) {
            successor.evaluate(0)
        }
        contextualCheckThrows<ArithmeticException>(
            "a failed force via $operation publishes nothing, so the read stays retryable.",
        ) {
            successor.evaluate(0)
        }
    }
    contextualCheckEquals(listOf(1), huge.toList(), "the retained version survives a rejected successor.")
    contextualCheckEquals(
        ContextualPrefixSummary(0, Long.MAX_VALUE),
        huge.evaluate(0),
        "the retained version's cached table survives.",
    )
    contextualCheckEquals(1, huge.validateStructure().count, "the retained version stays valid.")
}

private fun contextualConcatenationRequiresCompatibleMachines() {
    val left = ContextualRankSequence.from(listOf(1, 0), ContextualShiftMachine(1))
    val equalRight = ContextualRankSequence.from(listOf(0, 1), ContextualShiftMachine(1))
    val differentRight = ContextualRankSequence.from(listOf(0, 1), ContextualShiftMachine(0))

    contextualCheck(
        left.machine !== equalRight.machine && left.machine == equalRight.machine,
        "the two operands must retain distinct but value-equal machines.",
    )
    contextualCheckEquals(
        listOf(1, 0, 0, 1),
        left.concat(equalRight).toList(),
        "value-equal machines concatenate.",
    )
    contextualMatchesModel(left.concat(equalRight), listOf(1, 0, 0, 1), "value-equal concat")

    contextualCheckEquals(
        left.machine.stateCount,
        differentRight.machine.stateCount,
        "the incompatible operands still agree on their state count.",
    )
    contextualCheckThrows<IllegalArgumentException>("unequal machines must be rejected.") {
        left.concat(differentRight)
    }
    contextualCheckThrows<IllegalArgumentException>("unequal machines must be rejected in either order.") {
        differentRight.concat(left)
    }

    // The empty operand shortcut runs after the compatibility check, so it cannot smuggle a machine in.
    val empty = ContextualRankSequence.empty<Int>(ContextualShiftMachine(0))
    contextualCheckThrows<IllegalArgumentException>("an empty incompatible operand is still rejected.") {
        left.concat(empty)
    }
    contextualCheckEquals(2, left.stateCount, "the retained state count.")
    contextualCheckEquals(listOf(1, 0), left.toList(), "the left operand is unchanged.")
}

private fun contextualValidateStructureReportsRecomputedStatistics() {
    val sequence = ContextualRankSequence.from("\"a,b\",c,d".toList(), ContextualQuotedCommaMachine)
    val statistics = sequence.validateStructure()

    contextualCheckEquals(9, statistics.count, "validated element count.")
    contextualCheckEquals(2, statistics.stateCount, "validated state count.")
    contextualCheckEquals(2L, statistics.maximumTotalEventCount, "validated maximum event total.")
    contextualCheckEquals(
        ContextualRankSequenceStatistics(0, 3, 0L),
        ContextualRankSequence.empty<Int>(ContextualTernaryMachine).validateStructure(),
        "empty statistics.",
    )

    val large = ContextualRankSequence.from((0 until 2_048).toList(), ContextualTernaryMachine)
    // 1000 and 8 have different magnitudes modulo three, so the replacement is observable.
    val edited = large.setItem(1_000, 8) ?: error("valid index")
    contextualCheckEquals(2_048, large.validateStructure().count, "large validated count.")
    contextualCheckEquals(2_048, edited.validateStructure().count, "edited validated count.")
    contextualCheck(large.sharesStructureWith(edited), "a point edit shares untouched structure.")
    contextualCheck(large[1_000] != edited[1_000], "the replaced element differs between versions.")
    contextualCheck(
        large.evaluate(0) != edited.evaluate(0),
        "the replacement changes the whole-sequence summary.",
    )
}

private fun contextualConcurrentReadersObserveStableVersions() {
    val model = (-128..128).toList()
    val source = ContextualRankSequence.from(model, ContextualTernaryMachine)
    val expected = (0 until source.stateCount).map { state ->
        source.evaluate(state) ?: error("valid state")
    }

    contextualRunConcurrent("contextualRankSequence", 6) { worker ->
        var branch = source
        repeat(120) { iteration ->
            val state = (worker + iteration) % expected.size
            contextualCheckEquals(expected[state], source.evaluate(state), "concurrent evaluate.")
            val boundary = (worker * 31 + iteration * 17) % (source.size + 1)
            source.eventRank(boundary, state) ?: error("valid boundary and state")
            val total = expected[state].eventCount
            if (total > 0L) {
                val rank = (worker.toLong() + iteration.toLong()) % total
                source.trySelectEvent(rank, state) ?: error("valid event rank")
            }
            branch = branch.setItem(iteration % branch.size, worker - iteration) ?: error("valid index")
        }
        contextualCheckEquals(model, source.toList(), "concurrent snapshot.")
        contextualCheckEquals(source.size, branch.size, "concurrent branch size.")
    }

    contextualCheckEquals(model, source.toList(), "the shared version is unchanged.")
    contextualCheckEquals(model.size, source.validateStructure().count, "the shared version stays valid.")
}

/** The contextual rank sequence test cases. */
internal fun contextualRankSequenceTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "contextualQuotedMachineRanksAndSelectsByContext" to ::contextualQuotedMachineRanksAndSelectsByContext,
    "contextualCompositionIsOrderSensitive" to ::contextualCompositionIsOrderSensitive,
    "contextualExhaustiveSmallWordsMatchDirectScanner" to ::contextualExhaustiveSmallWordsMatchDirectScanner,
    "contextualBoundariesAndInvalidArgumentsReturnNull" to ::contextualBoundariesAndInvalidArgumentsReturnNull,
    "contextualRetainedVersionsStayIndependent" to ::contextualRetainedVersionsStayIndependent,
    "contextualGeneratedHistoryMatchesListModel" to ::contextualGeneratedHistoryMatchesListModel,
    "contextualCachedQueriesDoNotRescanTheInput" to ::contextualCachedQueriesDoNotRescanTheInput,
    "contextualMachineContractViolationsAreAtomic" to ::contextualMachineContractViolationsAreAtomic,
    "contextualConcatenationRequiresCompatibleMachines" to ::contextualConcatenationRequiresCompatibleMachines,
    "contextualValidateStructureReportsRecomputedStatistics" to ::contextualValidateStructureReportsRecomputedStatistics,
    "contextualConcurrentReadersObserveStableVersions" to ::contextualConcurrentReadersObserveStableVersions,
)
