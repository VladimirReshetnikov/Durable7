/*
 * Tests for the fixed-length persistent vector with a checkpoint and a maximal dirty-run index.
 *
 * The properties under test are that the run index stays an ordered, non-adjacent, maximal encoding
 * of exactly the policy-differing positions under every edit shape; that accepting or reverting one
 * indexed run is a structural splice consulting no value comparer; that a cancelled write restores
 * the exact checkpoint representative rather than an equal one; and that the retained equality
 * policy is a true equivalence relation even for floating-point payloads.
 */
package durable7.fingertree

import java.util.Collections
import java.util.Random

private fun runDeltaCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> runDeltaCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> runDeltaCheckThrows(message: String, action: () -> Unit) {
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

/** Counts every value-policy call so run splices can be proven comparison-free. */
private class RunDeltaCountingComparer : ValueEqualityComparer<Long> {
    var calls: Int = 0

    override fun areEqual(left: Long, right: Long): Boolean {
        calls++
        return left == right
    }
}

/** Throws on one armed call, so an edit can be interrupted between its two policy calls. */
private class RunDeltaThrowOnceComparer : ValueEqualityComparer<Int> {
    var calls: Int = 0
    var armedCall: Int = 0

    override fun areEqual(left: Int, right: Int): Boolean {
        calls++
        if (calls == armedCall) {
            armedCall = 0
            throw IllegalStateException("value comparer failure")
        }
        return left == right
    }
}

/** A value-equal but reference-distinguishable payload, for the representative-identity tests. */
private data class RunDeltaBox(val tag: Int)

private fun runDeltaExpectedRuns(current: List<Int>, checkpoint: List<Int>): List<PersistentDirtyRun> {
    val runs = ArrayList<PersistentDirtyRun>()
    var index = 0
    while (index < current.size) {
        if (current[index] == checkpoint[index]) {
            index++
            continue
        }
        val start = index
        index++
        while (index < current.size && current[index] != checkpoint[index]) {
            index++
        }
        runs.add(PersistentDirtyRun(start, index - start))
    }
    return runs
}

private fun runDeltaTotalLength(runs: List<PersistentDirtyRun>): Int =
    runs.fold(0) { total, run -> total + run.length }

private fun runDeltaAssertRuns(
    vector: PersistentRunDeltaVector<Int>,
    expected: List<PersistentDirtyRun>,
    label: String,
) {
    runDeltaCheckEquals(runDeltaTotalLength(expected), vector.dirtyCount, "$label dirty count")
    runDeltaCheckEquals(expected.size, vector.dirtyRunCount, "$label run count")
    runDeltaCheckEquals(expected, vector.dirtyRuns(), "$label run enumeration")
    for (rank in expected.indices) {
        val run = expected[rank]
        runDeltaCheckEquals(run, vector.dirtyRunAt(rank), "$label run at rank $rank")
        for (index in run.start until run.endExclusive) {
            runDeltaCheckEquals(true, vector.isDirty(index), "$label dirtiness at $index")
            runDeltaCheckEquals(run, vector.dirtyRunContaining(index), "$label containing run at $index")
            runDeltaCheckEquals(false, vector.sharesCheckpointRepresentativeAt(index), "$label dirty cell at $index")
        }
    }
    runDeltaCheckEquals(null, vector.dirtyRunAt(expected.size), "$label rank past the run index")
    vector.validateStructure()
}

private fun runDeltaAssertMatchesModel(
    vector: PersistentRunDeltaVector<Int>,
    current: List<Int>,
    checkpoint: List<Int>,
    label: String,
) {
    runDeltaCheckEquals(current.size, vector.size, "$label size")
    runDeltaCheckEquals(current, vector.toList(), "$label values")
    val runs = runDeltaExpectedRuns(current, checkpoint)
    for (index in current.indices) {
        runDeltaCheckEquals(current[index], vector[index], "$label value at $index")
        runDeltaCheckEquals(checkpoint[index], vector.getCheckpoint(index), "$label checkpoint at $index")
        val expected = runs.firstOrNull { it.contains(index) }
        runDeltaCheckEquals(expected != null, vector.isDirty(index), "$label dirtiness at $index")
        runDeltaCheckEquals(expected, vector.dirtyRunContaining(index), "$label containing run at $index")
        runDeltaCheckEquals(
            expected == null,
            vector.sharesCheckpointRepresentativeAt(index),
            "$label representative sharing at $index",
        )
    }
    runDeltaAssertRuns(vector, runs, label)
}

private fun runDeltaEmptyAndDescriptorContracts() {
    val empty = PersistentRunDeltaVector.empty<Int>()
    runDeltaCheck(empty.isEmpty, "empty vector is empty")
    runDeltaCheckEquals(0, empty.size, "empty size")
    runDeltaCheck(!empty.hasChanges, "empty vector is clean")
    runDeltaCheckEquals(0, empty.dirtyCount, "empty dirty count")
    runDeltaCheckEquals(0, empty.dirtyRunCount, "empty run count")
    runDeltaCheckEquals(emptyList(), empty.toList(), "empty values")
    runDeltaCheckEquals(emptyList(), empty.dirtyRuns(), "empty runs")
    runDeltaCheck(empty.checkpoint() === empty, "empty checkpoint returns the receiver")
    runDeltaCheck(empty.rollback() === empty, "empty rollback returns the receiver")
    runDeltaCheckEquals(null, empty[0], "empty index")
    runDeltaCheckEquals(null, empty.tryGet(0), "empty nullable-safe index")
    runDeltaCheckEquals(null, empty.getCheckpoint(0), "empty checkpoint index")
    runDeltaCheckEquals(null, empty.tryGetCheckpoint(0), "empty nullable-safe checkpoint index")
    runDeltaCheckEquals(null, empty.isDirty(0), "empty dirtiness")
    runDeltaCheckEquals(null, empty.dirtyRunContaining(0), "empty containing run")
    runDeltaCheckEquals(null, empty.dirtyRunAt(0), "empty run rank")
    runDeltaCheckEquals(null, empty.sharesCheckpointRepresentativeAt(0), "empty representative probe")
    runDeltaCheckEquals(null, empty.acceptDirtyRunAt(0), "empty accept by rank")
    runDeltaCheckEquals(null, empty.revertDirtyRunAt(0), "empty revert by rank")
    runDeltaCheckEquals(null, empty.acceptDirtyRunContaining(0), "empty accept by position")
    runDeltaCheckEquals(null, empty.revertDirtyRunContaining(0), "empty revert by position")
    runDeltaCheckEquals(null, empty.setItem(0, 1), "empty set")
    runDeltaCheckEquals(null, empty.resetItem(0), "empty reset")
    runDeltaCheckEquals(
        PersistentRunDeltaVectorStatistics(0, 0, 0),
        empty.validateStructure(),
        "empty statistics",
    )
    runDeltaCheck(empty.toString().startsWith("PersistentRunDeltaVector("), "vector description")

    runDeltaCheck(PersistentRunDeltaVector.empty<Int>() === empty, "the natural empty vector is shared")
    runDeltaCheck(PersistentRunDeltaVector.from(emptyList<Int>()) === empty, "an empty build reuses the shared vector")
    val policy = ValueEqualityPolicy.custom<Int> { left, right -> left == right }
    val customEmpty = PersistentRunDeltaVector.empty(policy)
    runDeltaCheck(customEmpty !== empty, "a custom-policy empty vector is distinct")
    runDeltaCheck(customEmpty.valuePolicy.isCompatibleWith(policy), "a custom-policy empty vector retains its policy")

    runDeltaCheckEquals(5, PersistentDirtyRun(3, 2).endExclusive, "run end")
    runDeltaCheckEquals("[3, 5)", PersistentDirtyRun(3, 2).toString(), "run description")
    runDeltaCheck(PersistentDirtyRun(3, 2).contains(3), "run contains its start")
    runDeltaCheck(PersistentDirtyRun(3, 2).contains(4), "run contains its interior")
    runDeltaCheck(!PersistentDirtyRun(3, 2).contains(5), "run excludes its end")
    runDeltaCheck(!PersistentDirtyRun(3, 2).contains(2), "run excludes positions before it")
    runDeltaCheckThrows<ArithmeticException>("run end overflow is checked") {
        PersistentDirtyRun(Int.MAX_VALUE, 1).endExclusive
    }

    val built = PersistentRunDeltaVector.from(listOf(0, 1, 2, 3))
    runDeltaCheckEquals(4, built.size, "built size")
    runDeltaCheckEquals(listOf(0, 1, 2, 3), built.toList(), "built values")
    runDeltaCheckEquals(listOf(0, 1, 2, 3), built.toMutableList(), "built iteration")
    runDeltaCheck(!built.hasChanges, "a built vector starts clean")
    // `rollback()` returns the receiver when the vector is already clean, so asserting sharing
    // against it would compare `built` with itself. The discriminating check is that a real edit is
    // reported as NOT sharing; the clean vector's own canonicality is pinned per position below.
    runDeltaCheck(
        !built.setItem(0, 99)!!.sharesCurrentRootWith(built),
        "an edited vector does not share the clean current root",
    )
    for (index in 0 until built.size) {
        runDeltaCheckEquals(true, built.sharesCheckpointRepresentativeAt(index), "built representative at $index")
    }
    runDeltaAssertMatchesModel(built, listOf(0, 1, 2, 3), listOf(0, 1, 2, 3), "built")
}

private fun runDeltaPolicyEqualWriteIsAVacuousNoOp() {
    val policy = ValueEqualityPolicy.custom<String> { left, right -> left.equals(right, ignoreCase = true) }
    val source = PersistentRunDeltaVector.from(listOf("Alpha", "Beta"), policy)

    val noOp = source.setItem(0, "ALPHA")!!
    runDeltaCheck(noOp === source, "a policy-equal write returns the receiver")
    runDeltaCheck(noOp.sharesCurrentRootWith(source), "a policy-equal write shares the current root")
    runDeltaCheck(noOp.sharesCheckpointRootWith(source), "a policy-equal write shares the checkpoint root")
    runDeltaCheck(!noOp.hasChanges, "a policy-equal write records no change")
    runDeltaCheckEquals(0, noOp.dirtyCount, "a policy-equal write leaves the dirty count alone")
    runDeltaCheckEquals(0, noOp.dirtyRunCount, "a policy-equal write leaves the run index alone")
    runDeltaCheckEquals("Alpha", noOp[0], "a policy-equal write keeps the current representative")

    val dirty = source.setItem(0, "Gamma")!!
    runDeltaCheckEquals("Gamma", dirty[0], "an effective write is visible")
    runDeltaCheckEquals("Alpha", dirty.getCheckpoint(0), "an effective write leaves the checkpoint alone")
    runDeltaCheckEquals(listOf(PersistentDirtyRun(0, 1)), dirty.dirtyRuns(), "an effective write records one run")
    runDeltaCheck(dirty.setItem(0, "GAMMA")!! === dirty, "a policy-equal rewrite of a dirty position is vacuous")

    val cancelled = dirty.setItem(0, "alpha")!!
    runDeltaCheck(!cancelled.hasChanges, "returning to the checkpoint class cancels the delta")
    runDeltaCheckEquals("Alpha", cancelled[0], "cancellation restores the checkpoint representative")
    runDeltaCheck(cancelled.sharesCurrentRootWith(source), "cancellation canonicalizes onto the checkpoint root")
    runDeltaCheck(cancelled.sharesCheckpointRootWith(source), "cancellation keeps the checkpoint root")
    runDeltaCheckEquals(true, cancelled.sharesCheckpointRepresentativeAt(0), "cancellation shares the representative")
    cancelled.validateStructure()

    runDeltaCheckEquals("Gamma", dirty[0], "the retained intermediate version is untouched")
    runDeltaCheckEquals(listOf("Alpha", "Beta"), source.toList(), "the retained source is untouched")
    dirty.validateStructure()
    source.validateStructure()
}

private fun runDeltaEqualityRelationsAreReflexive() {
    // The inherited hazard: a non-reflexive relation would record a NaN rewrite as a change and
    // leave a phantom run. On the JVM the two candidate relations behave differently, so which one
    // a policy path actually reaches is load-bearing rather than academic.
    val nan = Double.NaN
    val alsoNan = Double.fromBits(0x7ff8_0000_0000_0001L)
    val nanFloat = Float.NaN
    val alsoNanFloat = Float.fromBits(0x7fc0_0001)
    runDeltaCheck(alsoNan.isNaN() && alsoNanFloat.isNaN(), "the alternate payloads are NaN")
    runDeltaCheck(nan.toRawBits() != alsoNan.toRawBits(), "the two NaNs have distinct payloads")
    runDeltaCheck(!(nan == alsoNan), "primitive IEEE equality is not reflexive on NaN")
    runDeltaCheck(!(nanFloat == alsoNanFloat), "primitive IEEE equality is not reflexive on NaN floats")
    runDeltaCheck(0.0 == -0.0, "primitive IEEE equality identifies signed zeros")

    // The natural policy is generic, so it boxes and reaches java.lang.Double.equals, which compares
    // raw bits: reflexive on NaN, but separating the signed zeros.
    val natural = ValueEqualityPolicy.natural<Double>()
    runDeltaCheck(natural.areEqual(nan, alsoNan), "the natural policy is reflexive on NaN")
    runDeltaCheck(!natural.areEqual(0.0, -0.0), "the natural policy separates signed zeros")
    runDeltaCheck(natural.isCanonical, "the natural policy is canonical")
    runDeltaCheck(natural.isCompatibleWith(ValueEqualityPolicy.natural()), "natural policies are compatible")
    runDeltaCheck(natural.toString().contains("canonical=true"), "policy description")

    val reflexive = ValueEqualityPolicy.reflexiveIeeeDouble()
    runDeltaCheck(reflexive.areEqual(nan, alsoNan), "the reflexive IEEE policy is reflexive on NaN")
    runDeltaCheck(reflexive.areEqual(0.0, -0.0), "the reflexive IEEE policy identifies signed zeros")
    runDeltaCheck(!reflexive.areEqual(1.0, 2.0), "the reflexive IEEE policy still separates distinct values")
    runDeltaCheck(
        reflexive.isCompatibleWith(ValueEqualityPolicy.reflexiveIeeeDouble()),
        "reflexive IEEE policies are compatible",
    )
    val reflexiveFloat = ValueEqualityPolicy.reflexiveIeeeFloat()
    runDeltaCheck(reflexiveFloat.areEqual(nanFloat, alsoNanFloat), "the float reflexive policy is reflexive on NaN")
    runDeltaCheck(reflexiveFloat.areEqual(0.0f, -0.0f), "the float reflexive policy identifies signed zeros")

    val shared = ValueEqualityComparer<Int> { left, right -> left == right }
    runDeltaCheck(
        ValueEqualityPolicy.custom(shared).isCompatibleWith(ValueEqualityPolicy.custom(shared)),
        "policies sharing one comparer identity are compatible",
    )
    runDeltaCheck(
        !ValueEqualityPolicy.custom<Int> { left, right -> left == right }
            .isCompatibleWith(ValueEqualityPolicy.custom { left, right -> left == right }),
        "independently constructed custom policies are incompatible",
    )
    runDeltaCheck(!ValueEqualityPolicy.custom(shared).isCanonical, "a custom policy is not canonical")

    val floats = PersistentRunDeltaVector.from(listOf(Double.NaN, 1.0, 0.0), reflexive)
    val rewritten = floats.setItem(0, Double.NaN)!!
    runDeltaCheck(rewritten === floats, "a NaN rewrite is a no-op under the reflexive IEEE policy")
    runDeltaCheckEquals(0, rewritten.dirtyRunCount, "a NaN rewrite leaves no phantom run")
    runDeltaCheck(rewritten[0]!!.isNaN(), "a NaN rewrite keeps the NaN value")
    rewritten.validateStructure()
    val signedZero = floats.setItem(2, -0.0)!!
    runDeltaCheck(signedZero === floats, "signed zeros are one class under the reflexive IEEE policy")
    val floatDirty = floats.setItem(0, 2.0)!!
    runDeltaCheckEquals(listOf(PersistentDirtyRun(0, 1)), floatDirty.dirtyRuns(), "a genuine float change is recorded")
    val floatCancelled = floatDirty.setItem(0, Double.NaN)!!
    runDeltaCheck(!floatCancelled.hasChanges, "returning NaN cancels the float delta")
    runDeltaCheck(floatCancelled[0]!!.isNaN(), "the cancelled float position is NaN again")
    floatCancelled.validateStructure()

    // Under the natural policy the same NaN rewrite is still a no-op, because the boxed relation is
    // reflexive; only the signed zeros diverge from the reflexive IEEE policy.
    val naturalFloats = PersistentRunDeltaVector.from(listOf(Double.NaN, 0.0))
    runDeltaCheck(
        naturalFloats.setItem(0, Double.NaN)!! === naturalFloats,
        "a NaN rewrite is a no-op under the natural policy too",
    )
    val zeroChange = naturalFloats.setItem(1, -0.0)!!
    runDeltaCheckEquals(listOf(PersistentDirtyRun(1, 1)), zeroChange.dirtyRuns(), "natural equality separates zeros")
    zeroChange.validateStructure()
}

private fun runDeltaCancellationRestoresTheExactRepresentative() {
    val checkpointRep = RunDeltaBox(7)
    val replacementRep = RunDeltaBox(7)
    runDeltaCheckEquals(checkpointRep, replacementRep, "the two representatives are value-equal")
    runDeltaCheckEquals(7, checkpointRep.tag, "representative payload")
    runDeltaCheck(checkpointRep !== replacementRep, "the two representatives are distinct objects")

    // The substrate hazard this vector has to defend against: RrbVector.setItem short circuits on
    // value equality, so storing raw payloads would silently keep an equal-but-distinct object where
    // the exact checkpoint representative was requested. Boxing each slot in an identity-equal cell
    // turns that short circuit into the identity test the invariant actually wants.
    val raw = RrbVector.from(listOf(replacementRep))
    runDeltaCheck(raw.setItem(0, checkpointRep) === raw, "the substrate collapses equal-but-distinct writes")
    runDeltaCheck(raw[0] === replacementRep, "the substrate keeps the old representative on an equal write")

    val identityPolicy = ValueEqualityPolicy.custom<RunDeltaBox> { left, right -> left === right }
    val source = PersistentRunDeltaVector.from(listOf(checkpointRep), identityPolicy)
    val dirty = source.setItem(0, replacementRep)!!
    runDeltaCheckEquals(true, dirty.isDirty(0), "an identity-distinct write is a change under an identity policy")
    runDeltaCheck(dirty[0] === replacementRep, "the written representative is stored exactly")
    runDeltaCheck(dirty.getCheckpoint(0) === checkpointRep, "the checkpoint representative is untouched")
    runDeltaCheckEquals(false, dirty.sharesCheckpointRepresentativeAt(0), "a dirty position has its own cell")
    dirty.validateStructure()

    val reset = dirty.resetItem(0)!!
    runDeltaCheck(!reset.hasChanges, "resetting clears the delta")
    runDeltaCheck(reset[0] === checkpointRep, "resetting restores the exact checkpoint representative")
    runDeltaCheckEquals(true, reset.sharesCheckpointRepresentativeAt(0), "a reset position shares its cell")
    runDeltaCheck(reset.sharesCurrentRootWith(source), "a reset version canonicalizes onto the checkpoint root")
    reset.validateStructure()

    val written = dirty.setItem(0, checkpointRep)!!
    runDeltaCheck(!written.hasChanges, "writing the checkpoint representative cancels the delta")
    runDeltaCheck(written[0] === checkpointRep, "an explicit cancellation restores the exact representative")
    written.validateStructure()

    // Under a policy that considers the two boxes equal, the same write is a vacuous no-op instead.
    val natural = PersistentRunDeltaVector.from(listOf(checkpointRep))
    runDeltaCheck(natural.setItem(0, replacementRep)!! === natural, "a value-equal write is vacuous")
    runDeltaCheck(natural[0] === checkpointRep, "a vacuous write keeps the existing representative")
    runDeltaCheck(dirty[0] === replacementRep, "the retained dirty version is untouched")
}

private fun runDeltaPointEditsMaintainMaximalRuns() {
    val zeros = List(12) { 0 }
    val source = PersistentRunDeltaVector.from(zeros)
    var vector = source.setItem(2, 20)!!.setItem(4, 40)!!
    runDeltaAssertRuns(vector, listOf(PersistentDirtyRun(2, 1), PersistentDirtyRun(4, 1)), "two singletons")

    // A new position between two singleton runs merges both.
    vector = vector.setItem(3, 30)!!
    runDeltaAssertRuns(vector, listOf(PersistentDirtyRun(2, 3)), "both-merge")

    // A detached position, then the gap that joins it to the run on its left.
    vector = vector.setItem(6, 60)!!.setItem(5, 50)!!
    runDeltaAssertRuns(vector, listOf(PersistentDirtyRun(2, 5)), "left-and-right merge")

    // Interior clearing splits one run in two.
    vector = vector.resetItem(4)!!
    runDeltaAssertRuns(vector, listOf(PersistentDirtyRun(2, 2), PersistentDirtyRun(5, 2)), "interior split")

    // Left-edge clearing shrinks from the start.
    vector = vector.resetItem(2)!!
    runDeltaAssertRuns(vector, listOf(PersistentDirtyRun(3, 1), PersistentDirtyRun(5, 2)), "left-edge shrink")

    // Right-edge clearing shrinks from the end.
    vector = vector.resetItem(6)!!
    runDeltaAssertRuns(vector, listOf(PersistentDirtyRun(3, 1), PersistentDirtyRun(5, 1)), "right-edge shrink")

    // Clearing the last dirty position canonicalizes the current root onto the checkpoint root.
    vector = vector.resetItem(3)!!.resetItem(5)!!
    runDeltaCheck(!vector.hasChanges, "the fully cleared version is clean")
    runDeltaCheckEquals(emptyList(), vector.dirtyRuns(), "the fully cleared version has no runs")
    runDeltaCheckEquals(zeros, vector.toList(), "the fully cleared version holds the checkpoint values")
    runDeltaCheck(vector.sharesCurrentRootWith(source), "the fully cleared version reuses the checkpoint root")
    for (index in zeros.indices) {
        runDeltaCheckEquals(true, vector.sharesCheckpointRepresentativeAt(index), "cleared representative at $index")
    }
    vector.validateStructure()
    runDeltaCheck(vector.resetItem(3)!! === vector, "resetting a clean position returns the receiver")

    // The right-only merge: a fresh position immediately before an existing run absorbs it.
    val rightMerge = source.setItem(8, 80)!!.setItem(7, 70)!!
    runDeltaAssertRuns(rightMerge, listOf(PersistentDirtyRun(7, 2)), "right-merge")
    val leftMerge = source.setItem(7, 70)!!.setItem(8, 80)!!
    runDeltaAssertRuns(leftMerge, listOf(PersistentDirtyRun(7, 2)), "left-merge")

    // The four-way removal split, exercised on an interior run of length four.
    val block = source.setItem(4, 40)!!.setItem(5, 50)!!.setItem(6, 60)!!.setItem(7, 70)!!
    runDeltaAssertRuns(block, listOf(PersistentDirtyRun(4, 4)), "interior block")
    runDeltaAssertRuns(block.resetItem(4)!!, listOf(PersistentDirtyRun(5, 3)), "removal at the run start")
    runDeltaAssertRuns(block.resetItem(7)!!, listOf(PersistentDirtyRun(4, 3)), "removal at the run end")
    runDeltaAssertRuns(
        block.resetItem(5)!!,
        listOf(PersistentDirtyRun(4, 1), PersistentDirtyRun(6, 2)),
        "removal inside the run",
    )
    runDeltaAssertRuns(source.setItem(4, 40)!!.resetItem(4)!!, emptyList(), "removal of a singleton run")

    runDeltaCheck(!source.hasChanges, "the retained source is still clean")
    runDeltaAssertMatchesModel(source, zeros, zeros, "retained source")
}

private fun runDeltaRunSplicesMakeNoValueComparisons() {
    val counting = RunDeltaCountingComparer()
    val original = PersistentRunDeltaVector.from(0L until 20L, ValueEqualityPolicy.custom(counting))
    var edited = original
    for (index in (2 until 6) + (9 until 14) + (17 until 18)) {
        edited = edited.setItem(index, 1_000L + index)!!
    }
    val expected = listOf(PersistentDirtyRun(2, 4), PersistentDirtyRun(9, 5), PersistentDirtyRun(17, 1))
    runDeltaCheckEquals(expected, edited.dirtyRuns(), "edited runs")
    runDeltaCheckEquals(10, edited.dirtyCount, "edited dirty count")

    // The positive control: an effective write really does drive the counter this test relies on.
    val controlBefore = counting.calls
    runDeltaCheck(edited.setItem(3, 5_000L) != null, "the control write is in range")
    runDeltaCheck(counting.calls > controlBefore, "the comparer counter is live")

    // Enumerating descriptors is Theta(r) bookkeeping and consults no value comparer.
    var before = counting.calls
    runDeltaCheckEquals(3, edited.dirtyRuns().size, "descriptor enumeration is run-sized")
    runDeltaCheckEquals(expected[1], edited.dirtyRunAt(1), "rank selection")
    runDeltaCheckEquals(expected[1], edited.dirtyRunContaining(11), "position selection")
    runDeltaCheckEquals(counting.calls, before, "descriptor enumeration makes no value comparison")

    before = counting.calls
    val acceptedMiddle = edited.acceptDirtyRunAt(1)!!
    runDeltaCheckEquals(counting.calls, before, "accepting a run makes no value comparison")
    runDeltaCheckEquals(
        listOf(PersistentDirtyRun(2, 4), PersistentDirtyRun(17, 1)),
        acceptedMiddle.dirtyRuns(),
        "runs after accepting the middle one",
    )
    for (index in 9 until 14) {
        runDeltaCheckEquals(1_000L + index, acceptedMiddle[index], "accepted current value at $index")
        runDeltaCheckEquals(1_000L + index, acceptedMiddle.getCheckpoint(index), "accepted checkpoint value at $index")
        runDeltaCheckEquals(true, acceptedMiddle.sharesCheckpointRepresentativeAt(index), "accepted cell at $index")
    }

    before = counting.calls
    val revertedFirst = acceptedMiddle.revertDirtyRunAt(0)!!
    runDeltaCheckEquals(counting.calls, before, "reverting a run makes no value comparison")
    runDeltaCheckEquals(listOf(PersistentDirtyRun(17, 1)), revertedFirst.dirtyRuns(), "runs after reverting the first")
    for (index in 2 until 6) {
        runDeltaCheckEquals(index.toLong(), revertedFirst[index], "reverted value at $index")
    }

    before = counting.calls
    val clean = revertedFirst.acceptDirtyRunAt(0)!!
    runDeltaCheckEquals(counting.calls, before, "accepting the sole run makes no value comparison")
    runDeltaCheck(!clean.hasChanges, "accepting the sole run leaves a clean version")
    runDeltaCheckEquals(1_017L, clean.getCheckpoint(17), "the sole accepted run reached the checkpoint")

    runDeltaCheckEquals(expected, edited.dirtyRuns(), "the retained edited version is untouched")
    runDeltaCheck(!original.hasChanges, "the retained original is still clean")
    runDeltaCheckEquals((0L until 20L).toList(), original.toList(), "the retained original values")
    runDeltaCheckEquals(null, edited.acceptDirtyRunAt(3), "accepting past the last rank")
    runDeltaCheckEquals(null, edited.revertDirtyRunAt(3), "reverting past the last rank")
    runDeltaCheckEquals(null, edited.acceptDirtyRunAt(-1), "accepting a negative rank")
    runDeltaCheckEquals(null, edited.revertDirtyRunAt(-1), "reverting a negative rank")
    for (version in listOf(original, edited, acceptedMiddle, revertedFirst, clean)) {
        version.validateStructure()
    }

    // The k-versus-r witness: thousands of dirty positions described by exactly two records, and one
    // splice that retires 8_190 of them without touching a single position.
    val length = 8_192
    val plain = PersistentRunDeltaVector.from(List(length) { 0 })
    var clustered = plain
    for (index in 0 until length - 2) {
        clustered = clustered.setItem(index, 1)!!
    }
    clustered = clustered.setItem(length - 1, 1)!!
    runDeltaCheckEquals(length - 1, clustered.dirtyCount, "clustered dirty count")
    runDeltaCheckEquals(2, clustered.dirtyRunCount, "clustered run count")
    runDeltaCheckEquals(
        listOf(PersistentDirtyRun(0, length - 2), PersistentDirtyRun(length - 1, 1)),
        clustered.dirtyRuns(),
        "clustered runs",
    )

    val spliced = clustered.acceptDirtyRunAt(0)!!
    runDeltaCheckEquals(listOf(PersistentDirtyRun(length - 1, 1)), spliced.dirtyRuns(), "runs after the big splice")
    runDeltaCheckEquals(1, spliced.dirtyCount, "dirty count after the big splice")
    runDeltaCheckEquals(1, spliced.getCheckpoint(0), "the big splice reached the checkpoint")
    runDeltaCheckEquals(0, spliced.getCheckpoint(length - 1), "the big splice left the other run alone")
    runDeltaCheckEquals(List(length) { if (it == length - 2) 0 else 1 }, spliced.toList(), "values after the splice")
    runDeltaCheckEquals(List(length) { 0 }, plain.toList(), "the retained plain version is untouched")
    clustered.validateStructure()
    spliced.validateStructure()
}

private fun runDeltaRunsAreAddressableByRankAndPosition() {
    val counting = RunDeltaCountingComparer()
    val original = PersistentRunDeltaVector.from(0L until 16L, ValueEqualityPolicy.custom(counting))
    var edited = original
    for (index in (2 until 6) + (9 until 12)) {
        edited = edited.setItem(index, 1_000L + index)!!
    }
    val expected = listOf(PersistentDirtyRun(2, 4), PersistentDirtyRun(9, 3))
    runDeltaCheckEquals(expected, edited.dirtyRuns(), "edited runs")

    runDeltaCheckEquals(null, edited.acceptDirtyRunContaining(16), "accepting a position past the end")
    runDeltaCheckEquals(null, edited.revertDirtyRunContaining(16), "reverting a position past the end")
    runDeltaCheckEquals(null, edited.acceptDirtyRunContaining(-1), "accepting a negative position")
    runDeltaCheckEquals(null, edited.revertDirtyRunContaining(-1), "reverting a negative position")

    // A clean position is vacuous rather than an error: the receiver comes back, sharing every root
    // and consulting no comparer.
    for (index in listOf(0, 6, 8, 15)) {
        runDeltaCheckEquals(false, edited.isDirty(index), "clean position $index")
        val before = counting.calls
        for (unchanged in listOf(edited.acceptDirtyRunContaining(index)!!, edited.revertDirtyRunContaining(index)!!)) {
            runDeltaCheck(unchanged === edited, "a clean position returns the receiver at $index")
            runDeltaCheck(unchanged.sharesCurrentRootWith(edited), "clean-position current root at $index")
            runDeltaCheck(unchanged.sharesCheckpointRootWith(edited), "clean-position checkpoint root at $index")
            runDeltaCheckEquals(edited.dirtyCount, unchanged.dirtyCount, "clean-position dirty count at $index")
            runDeltaCheckEquals(expected, unchanged.dirtyRuns(), "clean-position runs at $index")
        }
        runDeltaCheckEquals(counting.calls, before, "a clean position makes no value comparison at $index")
    }

    // Every position inside a run acts exactly as the rank-addressed operation on that run.
    for (rank in expected.indices) {
        val run = expected[rank]
        val acceptedByRank = edited.acceptDirtyRunAt(rank)!!
        val revertedByRank = edited.revertDirtyRunAt(rank)!!
        for (index in run.start until run.endExclusive) {
            runDeltaCheckEquals(run, edited.dirtyRunContaining(index), "containing run at $index")
            val before = counting.calls
            val acceptedByPosition = edited.acceptDirtyRunContaining(index)!!
            val revertedByPosition = edited.revertDirtyRunContaining(index)!!
            runDeltaCheckEquals(counting.calls, before, "position-addressed splices make no comparison at $index")
            for (pair in listOf(acceptedByPosition to acceptedByRank, revertedByPosition to revertedByRank)) {
                val (byPosition, byRank) = pair
                runDeltaCheckEquals(byRank.toList(), byPosition.toList(), "position-addressed values at $index")
                runDeltaCheckEquals(byRank.dirtyCount, byPosition.dirtyCount, "position-addressed count at $index")
                runDeltaCheckEquals(byRank.dirtyRuns(), byPosition.dirtyRuns(), "position-addressed runs at $index")
                for (position in 0 until edited.size) {
                    runDeltaCheckEquals(
                        byRank.getCheckpoint(position),
                        byPosition.getCheckpoint(position),
                        "position-addressed checkpoint at $position",
                    )
                }
                byPosition.validateStructure()
            }
        }
    }

    // The sole-run case still collapses onto the whole-checkpoint and whole-rollback paths.
    val single = original.setItem(4, 4_000L)!!.setItem(5, 5_000L)!!
    val accepted = single.acceptDirtyRunContaining(5)!!
    runDeltaCheck(!accepted.hasChanges, "accepting the sole run leaves a clean version")
    runDeltaCheckEquals(4_000L, accepted.getCheckpoint(4), "the sole accepted run reached the checkpoint")
    runDeltaCheck(accepted.sharesCheckpointRootWith(single.checkpoint()), "the sole accept is a whole checkpoint")
    val reverted = single.revertDirtyRunContaining(4)!!
    runDeltaCheck(!reverted.hasChanges, "reverting the sole run leaves a clean version")
    runDeltaCheckEquals((0L until 16L).toList(), reverted.toList(), "the sole revert restores every value")

    runDeltaCheckEquals(expected, edited.dirtyRuns(), "the retained edited version is untouched")
    for (version in listOf(original, edited, single, accepted, reverted)) {
        version.validateStructure()
    }
}

private fun runDeltaCheckpointAndRollbackAreRootSwaps() {
    val initial = (0 until 24).toList()
    val source = PersistentRunDeltaVector.from(initial)
    runDeltaCheck(source.checkpoint() === source, "a clean checkpoint returns the receiver")
    runDeltaCheck(source.rollback() === source, "a clean rollback returns the receiver")

    val dirty = source.setItem(3, -3)!!.setItem(4, -4)!!.setItem(20, -20)!!
    runDeltaAssertRuns(dirty, listOf(PersistentDirtyRun(3, 2), PersistentDirtyRun(20, 1)), "dirty")

    val accepted = dirty.checkpoint()
    runDeltaCheck(!accepted.hasChanges, "a checkpoint is clean")
    runDeltaCheck(accepted.sharesCurrentRootWith(dirty), "a checkpoint keeps the current root")
    // Not `accepted.rollback()`, which returns the receiver once clean and so cannot fail: accepting
    // must have moved the checkpoint off the pre-edit root, which this does discriminate.
    runDeltaCheck(
        !accepted.sharesCheckpointRootWith(source),
        "accepting advances the checkpoint off the pre-edit root",
    )
    runDeltaCheckEquals(dirty.toList(), accepted.toList(), "a checkpoint keeps every current value")
    for (index in initial.indices) {
        runDeltaCheckEquals(true, accepted.sharesCheckpointRepresentativeAt(index), "checkpoint cell at $index")
    }
    accepted.validateStructure()

    val rolledBack = dirty.rollback()
    runDeltaCheck(!rolledBack.hasChanges, "a rollback is clean")
    runDeltaCheck(rolledBack.sharesCheckpointRootWith(dirty), "a rollback keeps the checkpoint root")
    runDeltaCheck(rolledBack.sharesCurrentRootWith(source), "a rollback restores the checkpoint root")
    runDeltaCheckEquals(initial, rolledBack.toList(), "a rollback restores every value")
    rolledBack.validateStructure()

    runDeltaAssertMatchesModel(dirty, dirty.toList(), initial, "retained dirty version")
    runDeltaAssertMatchesModel(source, initial, initial, "retained source")
}

private fun runDeltaNullableValuesAndFailureAtomicity() {
    val vector = PersistentRunDeltaVector.from(listOf<String?>(null, "middle", null))
    runDeltaCheckEquals(listOf(null, "middle", null), vector.toList(), "nullable values")
    runDeltaCheckEquals(null, vector[0], "a stored null reads as null")
    runDeltaCheckEquals(RunDeltaValue<String?>(null), vector.tryGet(0), "a stored null is reported as present")
    runDeltaCheckEquals(RunDeltaValue<String?>("middle"), vector.tryGet(1), "a stored value is reported as present")
    runDeltaCheckEquals(null, vector.tryGet(3), "a position past the end is absent")
    runDeltaCheckEquals(null, vector.tryGet(-1), "a negative position is absent")
    runDeltaCheckEquals(RunDeltaValue<String?>(null), vector.tryGetCheckpoint(2), "a stored null checkpoint")
    runDeltaCheckEquals(null, vector.tryGetCheckpoint(3), "a checkpoint position past the end")
    runDeltaCheck(vector.setItem(0, null)!! === vector, "writing an equal null is vacuous")

    val dirty = vector.setItem(0, "front")!!
    runDeltaCheckEquals(listOf(PersistentDirtyRun(0, 1)), dirty.dirtyRuns(), "a write over a stored null is a change")
    runDeltaCheckEquals(RunDeltaValue<String?>(null), dirty.tryGetCheckpoint(0), "the null checkpoint survives")
    val cancelled = dirty.setItem(0, null)!!
    runDeltaCheck(!cancelled.hasChanges, "writing null back cancels the delta")
    runDeltaCheck(cancelled.sharesCurrentRootWith(vector), "the cancelled version canonicalizes")
    dirty.validateStructure()
    cancelled.validateStructure()

    // All policy calls precede successor construction, so an interrupted edit publishes nothing.
    val comparer = RunDeltaThrowOnceComparer()
    val source = PersistentRunDeltaVector.from(listOf(1, 2, 3), ValueEqualityPolicy.custom(comparer))
    comparer.calls = 0
    comparer.armedCall = 2
    runDeltaCheckThrows<IllegalStateException>("the armed comparer fails the edit") { source.setItem(1, 99) }
    runDeltaCheckEquals(listOf(1, 2, 3), source.toList(), "the source survived the failure intact")
    runDeltaCheck(!source.hasChanges, "the source is still clean after the failure")
    source.validateStructure()

    val edited = source.setItem(1, 99)!!
    runDeltaCheckEquals(listOf(1, 99, 3), edited.toList(), "the retried edit succeeds")
    runDeltaCheckEquals(listOf(PersistentDirtyRun(1, 1)), edited.dirtyRuns(), "the retried edit records one run")
    runDeltaCheckEquals(listOf(1, 2, 3), source.toList(), "the source is unchanged by the retry")
    edited.validateStructure()
}

private fun runDeltaRandomizedHistoryMatchesModel() {
    val length = 40
    val random = Random(20_260_805L)
    val initial = (0 until length).map { it % 7 }
    val versions = ArrayList<Triple<PersistentRunDeltaVector<Int>, List<Int>, List<Int>>>()
    versions.add(Triple(PersistentRunDeltaVector.from(initial), initial, initial))

    repeat(1_200) {
        val (parent, parentCurrent, parentCheckpoint) = versions[random.nextInt(versions.size)]
        val current = ArrayList(parentCurrent)
        val checkpoint = ArrayList(parentCheckpoint)
        var result = parent

        when (random.nextInt(7)) {
            0, 1 -> {
                val index = random.nextInt(length)
                val value = random.nextInt(17) - 8
                result = result.setItem(index, value)!!
                current[index] = value
            }
            2 -> {
                val index = random.nextInt(length)
                result = result.resetItem(index)!!
                current[index] = checkpoint[index]
            }
            3 -> {
                result = result.checkpoint()
                for (index in 0 until length) {
                    checkpoint[index] = current[index]
                }
            }
            4 -> {
                result = result.rollback()
                for (index in 0 until length) {
                    current[index] = checkpoint[index]
                }
            }
            5 -> {
                val runs = runDeltaExpectedRuns(current, checkpoint)
                if (runs.isNotEmpty()) {
                    val rank = random.nextInt(runs.size)
                    val run = runs[rank]
                    if (random.nextBoolean()) {
                        result = result.acceptDirtyRunAt(rank)!!
                        for (index in run.start until run.endExclusive) {
                            checkpoint[index] = current[index]
                        }
                    } else {
                        result = result.revertDirtyRunAt(rank)!!
                        for (index in run.start until run.endExclusive) {
                            current[index] = checkpoint[index]
                        }
                    }
                }
            }
            else -> {
                val index = random.nextInt(length)
                val run = runDeltaExpectedRuns(current, checkpoint).firstOrNull { it.contains(index) }
                if (random.nextBoolean()) {
                    result = result.acceptDirtyRunContaining(index)!!
                    if (run != null) {
                        for (position in run.start until run.endExclusive) {
                            checkpoint[position] = current[position]
                        }
                    }
                } else {
                    result = result.revertDirtyRunContaining(index)!!
                    if (run != null) {
                        for (position in run.start until run.endExclusive) {
                            current[position] = checkpoint[position]
                        }
                    }
                }
            }
        }

        runDeltaAssertMatchesModel(result, current, checkpoint, "randomized result")
        runDeltaAssertMatchesModel(parent, parentCurrent, parentCheckpoint, "retained randomized parent")
        versions.add(Triple(result, current, checkpoint))
        if (versions.size > 24) {
            versions.removeAt(1 + random.nextInt(versions.size - 2))
        }
    }
}

private fun runDeltaConcurrentReadersSeeStableSnapshots() {
    val initial = (0 until 2_048).map { it % 17 }
    var built = PersistentRunDeltaVector.from(initial)
    for (index in 100 until 300) {
        built = built.setItem(index, -index)!!
    }
    val source = built
    val expectedCurrent = source.toList()
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val threads = (0 until 8).map { worker ->
        Thread {
            try {
                var branch = source
                val model = ArrayList(expectedCurrent)
                for (iteration in 0 until 200) {
                    val index = (worker * 97 + iteration * 31) % model.size
                    runDeltaCheckEquals(expectedCurrent[index], source[index], "concurrent snapshot read")
                    val value = worker * 10_000 + iteration
                    branch = branch.setItem(index, value)!!
                    model[index] = value
                    if (iteration % 64 == 63) {
                        branch = branch.checkpoint()
                    }
                }
                runDeltaCheckEquals(model.toList(), branch.toList(), "concurrent branch values")
                branch.validateStructure()
            } catch (error: Throwable) {
                failures.add(error)
            }
        }.apply { name = "run-delta-reader-$worker" }
    }
    threads.forEach(Thread::start)
    threads.forEach(Thread::join)
    if (failures.isNotEmpty()) {
        throw AssertionError("run-delta concurrent readers had ${failures.size} failure(s)", failures.first())
    }

    runDeltaAssertMatchesModel(source, expectedCurrent, initial, "concurrent source")
}

/** The run-delta vector test cases. */
internal fun persistentRunDeltaVectorTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "runDeltaEmptyAndDescriptorContracts" to ::runDeltaEmptyAndDescriptorContracts,
    "runDeltaPolicyEqualWriteIsAVacuousNoOp" to ::runDeltaPolicyEqualWriteIsAVacuousNoOp,
    "runDeltaEqualityRelationsAreReflexive" to ::runDeltaEqualityRelationsAreReflexive,
    "runDeltaCancellationRestoresTheExactRepresentative" to ::runDeltaCancellationRestoresTheExactRepresentative,
    "runDeltaPointEditsMaintainMaximalRuns" to ::runDeltaPointEditsMaintainMaximalRuns,
    "runDeltaRunSplicesMakeNoValueComparisons" to ::runDeltaRunSplicesMakeNoValueComparisons,
    "runDeltaRunsAreAddressableByRankAndPosition" to ::runDeltaRunsAreAddressableByRankAndPosition,
    "runDeltaCheckpointAndRollbackAreRootSwaps" to ::runDeltaCheckpointAndRollbackAreRootSwaps,
    "runDeltaNullableValuesAndFailureAtomicity" to ::runDeltaNullableValuesAndFailureAtomicity,
    "runDeltaRandomizedHistoryMatchesModel" to ::runDeltaRandomizedHistoryMatchesModel,
    "runDeltaConcurrentReadersSeeStableSnapshots" to ::runDeltaConcurrentReadersSeeStableSnapshots,
)
