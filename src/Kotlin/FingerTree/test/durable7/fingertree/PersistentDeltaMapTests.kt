/*
 * Tests for the checkpoint-differential persistent sorted map.
 *
 * The properties under test are that the change index is the exact net difference between the two
 * retained roots at every step, that it is reached without ever scanning either state, and that a
 * wholly cancelled epoch is storage-clean rather than merely extensionally clean.
 */
package durable7.fingertree

import java.util.Collections
import java.util.TreeMap
import java.util.TreeSet

private fun deltaCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> deltaCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> deltaCheckThrows(message: String, action: () -> Unit) {
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

/** A policy call counter, so a test can assert that an operation performed no callbacks at all. */
private class DeltaCounter {
    var count: Int = 0
        private set

    fun hit() {
        count += 1
    }

    fun reset() {
        count = 0
    }
}

private fun deltaCountingComparator(counter: DeltaCounter): Comparator<Int> =
    Comparator { left, right ->
        counter.hit()
        left.compareTo(right)
    }

private fun deltaCountingEquality(counter: DeltaCounter): ValueEqualityPolicy<String> =
    ValueEqualityPolicy.custom { left, right ->
        counter.hit()
        left == right
    }

private val deltaDescending: Comparator<Int> = Comparator { left, right -> right.compareTo(left) }

private val deltaCaseInsensitiveKeys: Comparator<String> =
    Comparator { left, right -> left.lowercase().compareTo(right.lowercase()) }

private val deltaCaseInsensitiveValues: ValueEqualityPolicy<String> =
    ValueEqualityPolicy.custom { left, right -> left.equals(right, ignoreCase = true) }

private fun deltaEntries(map: PersistentDeltaMap<Int, String>): List<Pair<Int, String>> =
    map.toList().map { it.key to it.value }

private fun deltaChangeKeys(map: PersistentDeltaMap<Int, String>): List<Int> =
    map.getChanges().map { it.key }

/** A deterministic generator so a randomized history replays exactly from its seed. */
private class DeltaLcg(private var state: Long) {
    fun below(bound: Int): Int {
        state = state * 6364136223846793005L + 1442695040888963407L
        return ((state ushr 33).toInt() and Int.MAX_VALUE) % bound
    }
}

private fun deltaCopy(source: TreeMap<Int, Int>): TreeMap<Int, Int> {
    val copy = TreeMap<Int, Int>()
    copy.putAll(source)
    return copy
}

private fun deltaModelChanges(
    checkpoint: Map<Int, Int>,
    current: Map<Int, Int>,
): List<PersistentMapChange<Int, Int>> {
    val keys = TreeSet<Int>()
    keys.addAll(checkpoint.keys)
    keys.addAll(current.keys)
    val expected = ArrayList<PersistentMapChange<Int, Int>>()
    for (key in keys) {
        val before = if (checkpoint.containsKey(key)) DeltaMapValue(checkpoint.getValue(key)) else null
        val after = if (current.containsKey(key)) DeltaMapValue(current.getValue(key)) else null
        if (before == null || after == null || before.value != after.value) {
            expected.add(PersistentMapChange(key, before, after))
        }
    }

    return expected
}

private fun deltaAssertModel(
    map: PersistentDeltaMap<Int, Int>,
    checkpoint: Map<Int, Int>,
    current: Map<Int, Int>,
    message: String,
) {
    deltaCheckEquals(current.entries.map { it.key to it.value }, map.toList().map { it.key to it.value }, "$message current")
    deltaCheckEquals(
        checkpoint.entries.map { it.key to it.value },
        map.checkpointSnapshot.toList().map { it.key to it.value },
        "$message checkpoint",
    )

    val expected = deltaModelChanges(checkpoint, current)
    deltaCheckEquals(expected, map.getChanges(), "$message changes")
    deltaCheckEquals(expected.size, map.changeCount, "$message change count")
    deltaCheckEquals(expected.isNotEmpty(), map.hasChanges, "$message has changes")
    deltaCheckEquals(expected.isEmpty(), map.isClean, "$message clean canonicalization")

    val statistics = map.validateStructure()
    deltaCheckEquals(current.size, statistics.size, "$message validated size")
    deltaCheckEquals(checkpoint.size, statistics.checkpointSize, "$message validated checkpoint size")
    deltaCheckEquals(expected.size, statistics.changeCount, "$message validated change count")
    deltaCheckEquals(
        expected.count { it.kind == PersistentMapChangeKind.ADDED },
        statistics.addedCount,
        "$message validated additions",
    )
    deltaCheckEquals(
        expected.count { it.kind == PersistentMapChangeKind.REMOVED },
        statistics.removedCount,
        "$message validated removals",
    )
    deltaCheckEquals(
        expected.count { it.kind == PersistentMapChangeKind.UPDATED },
        statistics.updatedCount,
        "$message validated updates",
    )
    for (change in expected) {
        deltaCheckEquals(change, map.getChange(change.key), "$message point change lookup")
    }
}

private fun deltaMapPointEditsClassifyEveryKind() {
    val source = PersistentDeltaMap.from(listOf(1 to "one", 2 to "two"))
    deltaCheck(source.isClean, "a fresh checkpoint is clean")
    deltaCheckEquals(emptyList(), source.getChanges(), "a fresh checkpoint records nothing")

    val edited = source.setItem(2, "TWO").setItem(3, "three").remove(1)
    deltaCheckEquals(listOf(2 to "TWO", 3 to "three"), deltaEntries(edited), "point-edited current state")
    deltaCheckEquals(listOf(1 to "one", 2 to "two"), edited.checkpointSnapshot.toList().map { it.key to it.value }, "retained checkpoint")

    val changes = edited.getChanges()
    deltaCheckEquals(listOf(1, 2, 3), changes.map { it.key }, "changes are in ascending key order")
    deltaCheckEquals(
        listOf(PersistentMapChangeKind.REMOVED, PersistentMapChangeKind.UPDATED, PersistentMapChangeKind.ADDED),
        changes.map { it.kind },
        "change classification",
    )
    deltaCheckEquals(DeltaMapValue("one"), changes[0].before, "removal before endpoint")
    deltaCheckEquals(null, changes[0].after, "removal after endpoint")
    deltaCheckEquals(DeltaMapValue("two"), changes[1].before, "update before endpoint")
    deltaCheckEquals(DeltaMapValue("TWO"), changes[1].after, "update after endpoint")
    deltaCheckEquals(null, changes[2].before, "addition before endpoint")
    deltaCheckEquals(DeltaMapValue("three"), changes[2].after, "addition after endpoint")

    deltaCheckEquals(3, edited.changeCount, "net-changed class count")
    deltaCheck(edited.hasChanges, "an edited version has changes")
    deltaCheck(!edited.isClean, "an edited version is not clean")
    deltaCheckEquals(null, edited.getChange(4), "an unchanged key has no record")
    deltaCheckThrows<IllegalStateException>("a change absent at both endpoints") {
        PersistentMapChange<Int, String>(1, null, null).kind
    }

    // The source is untouched: every successor is a published-nothing new version.
    deltaCheckEquals(listOf(1 to "one", 2 to "two"), deltaEntries(source), "the receiver is unchanged")
    deltaCheck(source.isClean, "the receiver stays clean")
    source.validateStructure()
    edited.validateStructure()
}

private fun deltaMapEndpointsDistinguishStoredNullFromAbsence() {
    val map = PersistentDeltaMap.empty<Int, String?>()
    val added = map.setItem(1, null)

    deltaCheck(added.containsKey(1), "a stored null is a present entry")
    deltaCheckEquals(null, added[1], "the plain lookup cannot separate a stored null from a miss")
    deltaCheckEquals(SortedMapEntry<Int, String?>(1, null), added.getEntry(1), "the presence-safe lookup can")
    deltaCheckEquals(null, added.getEntry(2), "the presence-safe lookup reports a real miss")

    val addition = added.getChange(1) ?: throw AssertionError("a stored null is a recorded addition")
    deltaCheckEquals(null, addition.before, "an absent endpoint is null")
    deltaCheckEquals(DeltaMapValue<String?>(null), addition.after, "a present null endpoint is a wrapper carrying null")
    deltaCheckEquals(PersistentMapChangeKind.ADDED, addition.kind, "a stored null classifies as an addition")

    // Checkpointing a stored null makes the *before* endpoint a present null, which a bare V? could
    // never distinguish from absence.
    val based = added.checkpoint()
    deltaCheck(based.isClean, "checkpointing a stored null is clean")
    deltaCheck(based.setItem(1, null).isClean, "rewriting a stored null is a semantic no-op")

    val replaced = based.setItem(1, "value")
    val update = replaced.getChange(1) ?: throw AssertionError("replacing a stored null is a recorded change")
    deltaCheckEquals(DeltaMapValue<String?>(null), update.before, "the before endpoint is a present null")
    deltaCheckEquals(DeltaMapValue<String?>("value"), update.after, "the after endpoint is the new value")
    deltaCheckEquals(PersistentMapChangeKind.UPDATED, update.kind, "a present null updates rather than adds")

    val cleared = replaced.setItem(1, null)
    deltaCheck(cleared.isClean, "restoring the present null cancels the record")
    deltaCheck(cleared.currentSnapshot === based.currentSnapshot, "cancellation snaps back to the checkpoint root")
    replaced.validateStructure()
    cleared.validateStructure()
}

private fun deltaMapWritesCoalesceAndRoundTripsCancel() {
    val source = PersistentDeltaMap.from(listOf(1 to "one", 2 to "two"))

    // Repeated writes to one class coalesce into a single record that keeps the first `before`.
    val coalesced = source.setItem(1, "a").setItem(1, "b").setItem(1, "c")
    deltaCheckEquals(1, coalesced.changeCount, "repeated writes coalesce into one record")
    val record = coalesced.getChange(1) ?: throw AssertionError("the coalesced record exists")
    deltaCheckEquals(DeltaMapValue("one"), record.before, "the first effective write captured before")
    deltaCheckEquals(DeltaMapValue("c"), record.after, "later writes replace only after")

    // Set then restore cancels.
    val restored = coalesced.setItem(1, "one")
    deltaCheckEquals(0, restored.changeCount, "returning a class to its checkpoint state removes its record")
    deltaCheck(restored.isClean, "a wholly cancelled epoch is clean")
    deltaCheck(restored.currentSnapshot === source.currentSnapshot, "the current root snaps back to the checkpoint root")
    deltaCheck(restored.checkpointSnapshot === source.checkpointSnapshot, "the checkpoint root is retained")

    // Add then remove cancels; the added class was never in the checkpoint.
    val added = source.setItem(9, "nine")
    deltaCheckEquals(1, added.changeCount, "an addition is one record")
    val cancelled = added.remove(9)
    deltaCheckEquals(0, cancelled.changeCount, "removing an added class cancels its record")
    deltaCheck(cancelled.isClean, "an add/remove round trip is clean")
    deltaCheck(cancelled.currentSnapshot === source.currentSnapshot, "the round trip snaps back to the checkpoint root")

    // Remove then re-add with the checkpoint value cancels too.
    val roundTripped = source.remove(2).setItem(2, "two")
    deltaCheckEquals(0, roundTripped.changeCount, "a delete/re-add round trip to the checkpoint value cancels")
    deltaCheck(roundTripped.currentSnapshot === source.currentSnapshot, "the delete/re-add round trip snaps back")

    // A partial cancellation leaves the surviving records and stays off the checkpoint root.
    val partial = source.setItem(1, "x").setItem(2, "y").setItem(1, "one")
    deltaCheckEquals(listOf(2), deltaChangeKeys(partial), "only the uncancelled record survives")
    deltaCheck(!partial.isClean, "a partially cancelled epoch is not clean")
    deltaCheck(partial.currentSnapshot !== source.currentSnapshot, "a dirty version does not reuse the checkpoint root")

    // Removing an absent key is a no-op returning the receiver itself.
    deltaCheck(source.remove(42) === source, "removing an absent key returns the receiver")
    deltaCheck(source.remove(42).getChanges().isEmpty(), "removing an absent key records nothing")

    for (version in listOf(coalesced, restored, added, cancelled, roundTripped, partial)) {
        version.validateStructure()
    }
}

private fun deltaMapEquivalentWritesAreSemanticNoOps() {
    val source = PersistentDeltaMap.from(
        listOf("Alpha" to "one", "Beta" to "two"),
        deltaCaseInsensitiveKeys,
        deltaCaseInsensitiveValues,
    )

    val sameValue = source.setItem("Alpha", "one")
    deltaCheck(sameValue === source, "an identical write returns the receiver")

    // The retained value policy, not the platform's equality, decides what a no-op is.
    val equivalentValue = source.setItem("ALPHA", "ONE")
    deltaCheck(equivalentValue === source, "a policy-equivalent write returns the receiver")
    deltaCheckEquals(0, equivalentValue.changeCount, "a no-op records no change")
    deltaCheckEquals("one", equivalentValue["Alpha"], "a no-op keeps the stored value representative")

    // The same write under the natural policy is a real change, proving the policy is load-bearing.
    val natural = PersistentDeltaMap.from(
        listOf("Alpha" to "one"),
        deltaCaseInsensitiveKeys,
    )
    val changed = natural.setItem("ALPHA", "ONE")
    deltaCheck(changed !== natural, "the natural policy sees a different value")
    deltaCheckEquals(1, changed.changeCount, "the natural policy records the change")
    deltaCheckEquals("ONE", changed["alpha"], "the new value is stored")
    // Negative controls over two genuinely distinct instances. Asserting the sharing diagnostics on
    // the no-op result would compare `source` with itself, since the no-op returns the receiver, and
    // could not fail; the discriminating question is whether a real edit is reported as NOT sharing.
    deltaCheck(!changed.sharesRootsWith(natural), "a real edit does not share every root")
    deltaCheck(!changed.sharesStorageWith(natural), "a real edit does not share all storage")

    // A no-op cannot resurrect a cancelled record, and a write to an absent class is never a no-op.
    val addition = source.setItem("Gamma", "three")
    deltaCheckEquals(1, addition.changeCount, "a write to an absent class is never a no-op")
    deltaCheck(addition.setItem("GAMMA", "THREE") === addition, "an equivalent rewrite of an addition is a no-op")

    deltaCheckEquals(0, source.setItems(emptyList()).changeCount, "an empty batch records nothing")
    deltaCheck(source.setItems(emptyList()) === source, "an empty batch returns the receiver")
    deltaCheck(
        source.setItems(listOf("alpha" to "ONE", "beta" to "TWO")) === source,
        "an all-no-op batch returns the receiver",
    )
    source.validateStructure()
    changed.validateStructure()
    addition.validateStructure()
}

private fun deltaMapRepresentativesSurviveUpdatesAndRoundTrips() {
    val source = PersistentDeltaMap.from(listOf("Alpha" to 1), deltaCaseInsensitiveKeys)
    deltaCheckEquals(listOf("Alpha"), source.keys(), "the checkpoint representative")

    // A point update keeps the baseline representative rather than the supplied key.
    val updated = source.setItem("ALPHA", 2)
    deltaCheckEquals(listOf("Alpha"), updated.keys(), "a point update keeps the baseline representative")
    deltaCheckEquals(listOf("Alpha"), updated.getChanges().map { it.key }, "the record keeps the baseline representative")

    // A delete/re-add round trip keeps it too, because the record supplies it once the entry is gone.
    val readded = updated.remove("alpha").setItem("aLpHa", 3)
    deltaCheckEquals(listOf("Alpha"), readded.keys(), "a delete/re-add round trip keeps the baseline representative")
    deltaCheckEquals(listOf("Alpha"), readded.getChanges().map { it.key }, "the reopened record keeps it as well")
    deltaCheckEquals(3, readded["ALPHA"], "the round trip stored the new value")

    // A newly added class keeps its first representative while its record is active...
    val addition = source.setItem("Beta", 10).setItem("BETA", 20)
    deltaCheckEquals(listOf("Alpha", "Beta"), addition.keys(), "an active addition keeps its first representative")
    deltaCheckEquals(listOf("Beta"), addition.getChanges().map { it.key }, "the addition record keeps it as well")

    // ...and a later addition after complete cancellation begins a new representative episode.
    val reopened = addition.remove("beta").setItem("bEtA", 30)
    deltaCheckEquals(listOf("Alpha", "bEtA"), reopened.keys(), "a new episode takes the supplied representative")
    deltaCheckEquals(listOf("bEtA"), reopened.getChanges().map { it.key }, "the new record takes it too")

    // A record's key is the checkpoint representative even when the probe differs everywhere.
    val removed = source.remove("ALPHA")
    deltaCheckEquals(listOf("Alpha"), removed.getChanges().map { it.key }, "a removal record keeps the checkpoint key")
    deltaCheckEquals(emptyList(), removed.keys(), "the removal emptied the current state")

    for (version in listOf(updated, readded, addition, reopened, removed)) {
        version.validateStructure()
    }
}

private fun deltaMapCheckpointAndRollbackAreRootSwaps() {
    val keyCounter = DeltaCounter()
    val valueCounter = DeltaCounter()
    val source = PersistentDeltaMap.from(
        listOf(1 to "one", 2 to "two"),
        deltaCountingComparator(keyCounter),
        deltaCountingEquality(valueCounter),
    )

    deltaCheck(source.checkpoint() === source, "checkpointing a clean version returns the receiver")
    deltaCheck(source.rollback() === source, "rolling back a clean version returns the receiver")

    val edited = source.setItem(1, "ONE").setItem(3, "three").remove(2)
    deltaCheckEquals(3, edited.changeCount, "the edited version records three classes")

    keyCounter.reset()
    valueCounter.reset()
    val based = edited.checkpoint()
    deltaCheckEquals(0, keyCounter.count, "checkpoint invokes no key comparison")
    deltaCheckEquals(0, valueCounter.count, "checkpoint invokes no value comparison")
    deltaCheck(based.isClean, "the new checkpoint is clean")
    deltaCheck(based.currentSnapshot === edited.currentSnapshot, "checkpoint keeps the current root")
    deltaCheck(based.checkpointSnapshot === edited.currentSnapshot, "checkpoint promotes the current root")
    deltaCheckEquals(listOf(1 to "ONE", 3 to "three"), deltaEntries(based), "the promoted state")

    keyCounter.reset()
    valueCounter.reset()
    val rolled = edited.rollback()
    deltaCheckEquals(0, keyCounter.count, "rollback invokes no key comparison")
    deltaCheckEquals(0, valueCounter.count, "rollback invokes no value comparison")
    deltaCheck(rolled.isClean, "the rolled-back version is clean")
    deltaCheck(rolled.currentSnapshot === edited.checkpointSnapshot, "rollback restores the checkpoint root")
    deltaCheck(rolled.checkpointSnapshot === edited.checkpointSnapshot, "rollback keeps the checkpoint root")
    deltaCheckEquals(listOf(1 to "one", 2 to "two"), deltaEntries(rolled), "the restored state")

    // The edited version is unaffected by either, and both derived versions keep evolving apart.
    deltaCheckEquals(3, edited.changeCount, "the edited version is unchanged by checkpoint or rollback")
    deltaCheckEquals(1, based.setItem(1, "again").changeCount, "the new epoch measures against the new checkpoint")
    deltaCheckEquals(1, rolled.setItem(1, "again").changeCount, "the restored epoch measures against the old checkpoint")

    keyCounter.reset()
    valueCounter.reset()
    deltaCheckEquals(listOf(1, 2, 3), edited.getChanges().map { it.key }, "the change enumeration")
    deltaCheckEquals(0, keyCounter.count, "enumerating changes invokes no key comparison")
    deltaCheckEquals(0, valueCounter.count, "enumerating changes invokes no value comparison")

    edited.validateStructure()
    based.validateStructure()
    rolled.validateStructure()
}

private fun deltaBuildChangedMap(baseline: Int, counters: Pair<DeltaCounter, DeltaCounter>): PersistentDeltaMap<Int, String> {
    var map = PersistentDeltaMap.empty<Int, String>(
        deltaCountingComparator(counters.first),
        deltaCountingEquality(counters.second),
    )
    map = map.setItems((0 until baseline).map { it * 4 to "v$it" }).checkpoint()
    return map.setItem(2, "a").setItem(6, "b").setItem(10, "c")
}

private fun deltaMapChangeEnumerationIsOutputOptimal() {
    val small = deltaBuildChangedMap(8, DeltaCounter() to DeltaCounter())
    val large = deltaBuildChangedMap(4096, DeltaCounter() to DeltaCounter())

    deltaCheckEquals(8, small.checkpointSize, "the small baseline")
    deltaCheckEquals(4096, large.checkpointSize, "the large baseline")
    deltaCheckEquals(3, small.changeCount, "the small change count")
    deltaCheckEquals(3, large.changeCount, "the large change count")
    deltaCheckEquals(
        small.getChanges(),
        large.getChanges(),
        "the change set does not depend on the baseline size",
    )

    // Enumeration touches only the change index: no policy callback fires at any baseline size.
    val keyCounter = DeltaCounter()
    val valueCounter = DeltaCounter()
    val counted = deltaBuildChangedMap(4096, keyCounter to valueCounter)
    keyCounter.reset()
    valueCounter.reset()
    deltaCheckEquals(3, counted.getChanges().size, "the counted change set")
    deltaCheckEquals(0, keyCounter.count, "full enumeration invokes no key comparison")
    deltaCheckEquals(0, valueCounter.count, "full enumeration invokes no value comparison")

    // A point change lookup searches the change index, not the state, so it stays cheap at any N.
    keyCounter.reset()
    valueCounter.reset()
    deltaCheckEquals(DeltaMapValue("a"), counted.getChange(2)?.after, "the point change lookup")
    deltaCheck(keyCounter.count < 32, "a point change lookup searches the change index: ${keyCounter.count}")
    deltaCheckEquals(0, valueCounter.count, "a point change lookup invokes no value comparison")
    counted.validateStructure()
}

private fun deltaRangeComparisons(changeCount: Int): Int {
    val keyCounter = DeltaCounter()
    val valueCounter = DeltaCounter()
    var map = PersistentDeltaMap.empty<Int, String>(
        deltaCountingComparator(keyCounter),
        deltaCountingEquality(valueCounter),
    )
    map = map.setItems((0 until changeCount).map { it * 2 to "v$it" })
    deltaCheckEquals(changeCount, map.changeCount, "the seeded change count")

    keyCounter.reset()
    valueCounter.reset()
    val window = map.getChanges(4, 5)
    deltaCheckEquals(listOf(4), window.map { it.key }, "the range window at k=$changeCount")
    deltaCheckEquals(0, valueCounter.count, "a range query invokes no value comparison at k=$changeCount")
    return keyCounter.count
}

private fun deltaMapRangeRestrictedChangesSeekBoundaries() {
    val source = PersistentDeltaMap.empty<Int, String>()
        .setItems((0 until 10).map { it to "v$it" })
        .checkpoint()
    val edited = source.setItems(listOf(1 to "a", 3 to "b", 5 to "c", 7 to "d")).remove(9)

    deltaCheckEquals(listOf(1, 3, 5, 7, 9), edited.getChanges().map { it.key }, "the full change set")
    deltaCheckEquals(listOf(3, 5, 7), edited.getChanges(3, 7).map { it.key }, "an inclusive window")
    deltaCheckEquals(listOf(3, 5), edited.getChanges(2, 6).map { it.key }, "a window between recorded keys")
    deltaCheckEquals(listOf(1), edited.getChanges(0, 1).map { it.key }, "a low-boundary window")
    deltaCheckEquals(listOf(9), edited.getChanges(8, 100).map { it.key }, "a window past the last change")
    deltaCheckEquals(emptyList(), edited.getChanges(-10, -1).map { it.key }, "a window below every change")
    deltaCheckEquals(emptyList(), edited.getChanges(4, 4).map { it.key }, "a window straddling no change")
    deltaCheckEquals(edited.getChanges(), edited.getChanges(Int.MIN_VALUE, Int.MAX_VALUE), "a window over everything")

    // The window carries the same records the unrestricted enumeration does, in the same order.
    deltaCheckEquals(
        edited.getChanges().filter { it.key in 3..7 },
        edited.getChanges(3, 7),
        "the window agrees with the full enumeration",
    )

    // An inverted range yields nothing rather than failing, under the ascending comparator...
    deltaCheckEquals(emptyList(), edited.getChanges(7, 3).map { it.key }, "an inverted ascending range")
    deltaCheckEquals(0, edited.getKeyRange(7, 3).size, "an inverted ascending state range")

    // ...and "inverted" is decided by the retained comparator, so a descending map is the mirror image.
    val descending = PersistentDeltaMap.empty<Int, String>(deltaDescending)
        .setItems((0 until 10).map { it to "v$it" })
        .checkpoint()
        .setItems(listOf(1 to "a", 3 to "b", 5 to "c", 7 to "d"))
    deltaCheckEquals(listOf(7, 5, 3, 1), descending.getChanges().map { it.key }, "descending change order")
    deltaCheckEquals(listOf(7, 5, 3), descending.getChanges(7, 3).map { it.key }, "a descending window is not inverted")
    deltaCheckEquals(emptyList(), descending.getChanges(3, 7).map { it.key }, "a descending inverted range")
    deltaCheckEquals(listOf(9, 8, 7), descending.getKeyRange(9, 7).keys(), "a descending state range")

    // The window is a boundary seek, not a filter: growing k by 64x must not grow the comparison
    // count linearly. A filter over all k records would cost at least k comparisons.
    val fewComparisons = deltaRangeComparisons(16)
    val manyComparisons = deltaRangeComparisons(1024)
    deltaCheck(manyComparisons < 128, "a range seek stays logarithmic at k=1024: $manyComparisons")
    deltaCheck(
        manyComparisons <= fewComparisons * 4 + 8,
        "a 64x larger change index costs a bounded multiple: $fewComparisons then $manyComparisons",
    )

    edited.validateStructure()
    descending.validateStructure()
}

private fun deltaMapSetItemsFoldsPointWrites() {
    val source = PersistentDeltaMap.from(listOf(1 to "one", 2 to "two", 3 to "three"))
    val batch = listOf(1 to "a", 4 to "d", 1 to "b", 2 to "two", 4 to "dd")

    var folded = source
    for ((key, value) in batch) {
        folded = folded.setItem(key, value)
    }
    val bulk = source.setItems(batch)

    deltaCheckEquals(deltaEntries(folded), deltaEntries(bulk), "the batch is the fold of point writes")
    deltaCheckEquals(folded.getChanges(), bulk.getChanges(), "the batch coalesces like the fold")
    deltaCheckEquals(listOf(1, 4), deltaChangeKeys(bulk), "a no-op entry inside a batch leaves no record")
    deltaCheckEquals(DeltaMapValue("one"), bulk.getChange(1)?.before, "the batch kept the first before endpoint")
    deltaCheckEquals(DeltaMapValue("b"), bulk.getChange(1)?.after, "the batch kept the last after endpoint")

    // A batch that ends by returning every touched class to its checkpoint state snaps back.
    val cancelling = source.setItems(listOf(1 to "x", 2 to "y", 1 to "one", 2 to "two"))
    deltaCheck(cancelling.isClean, "a wholly cancelling batch is clean")
    deltaCheck(cancelling.currentSnapshot === source.currentSnapshot, "a wholly cancelling batch snaps back")

    bulk.validateStructure()
    cancelling.validateStructure()
}

private fun deltaMapReadSurfaceTracksTheCurrentState() {
    val map = PersistentDeltaMap.from(listOf(10 to "a", 20 to "b", 30 to "c"))
        .setItem(40, "d")
        .remove(20)

    deltaCheckEquals(3, map.size, "current size")
    deltaCheck(!map.isEmpty, "a populated map is not empty")
    deltaCheckEquals(3, map.checkpointSize, "checkpoint size")
    deltaCheckEquals(listOf(10, 30, 40), map.keys(), "current keys")
    deltaCheckEquals(listOf("a", "c", "d"), map.values(), "current values")
    deltaCheckEquals(listOf(10 to "a", 30 to "c", 40 to "d"), map.map { it.key to it.value }, "iteration order")

    deltaCheckEquals("c", map[30], "point lookup")
    deltaCheckEquals(null, map[20], "removed key lookup")
    deltaCheck(map.containsKey(40), "added key presence")
    deltaCheck(!map.containsKey(20), "removed key absence")
    deltaCheckEquals(1, map.indexOfKey(30), "rank of a present key")
    deltaCheckEquals(null, map.indexOfKey(20), "rank of an absent key")
    deltaCheckEquals(SortedMapEntry(30, "c"), map.entryAt(1), "select by rank")
    deltaCheckEquals(null, map.entryAt(3), "select past the end")
    deltaCheckEquals(SortedMapEntry(10, "a"), map.minEntry(), "least entry")
    deltaCheckEquals(SortedMapEntry(40, "d"), map.maxEntry(), "greatest entry")

    deltaCheckEquals(SortedMapEntry(10, "a"), map.floorEntry(20), "floor entry")
    deltaCheckEquals(SortedMapEntry(30, "c"), map.ceilingEntry(20), "ceiling entry")
    deltaCheckEquals(SortedMapEntry(10, "a"), map.lowerEntry(30), "strict predecessor")
    deltaCheckEquals(SortedMapEntry(40, "d"), map.higherEntry(30), "strict successor")
    deltaCheckEquals(null, map.lowerEntry(10), "no predecessor below the least key")
    deltaCheckEquals(null, map.higherEntry(40), "no successor above the greatest key")
    deltaCheckEquals(listOf(10, 30), map.getKeyRange(5, 35).keys(), "a state key range")

    val empty = PersistentDeltaMap.empty<Int, String>()
    deltaCheck(empty.isEmpty, "an empty map is empty")
    deltaCheck(empty.isClean, "an empty map is clean")
    deltaCheckEquals(0, empty.size, "empty size")
    deltaCheckEquals(null, empty.minEntry(), "an empty map has no least entry")
    deltaCheckEquals(null, empty.maxEntry(), "an empty map has no greatest entry")
    deltaCheckEquals(emptyList(), empty.getChanges(), "an empty map records nothing")
    deltaCheckEquals(emptyList(), empty.getChanges(0, 10), "an empty change index has an empty window")
    deltaCheck(empty.remove(1) === empty, "removing from an empty map returns the receiver")
    deltaCheck(empty.checkpoint() === empty, "checkpointing an empty map returns the receiver")
    deltaCheck(empty.rollback() === empty, "rolling back an empty map returns the receiver")
    deltaCheck(empty.toString().contains("changeCount=0"), "the diagnostic representation names the change count")

    // A comparator-carrying map stays usable once every entry and every change is gone.
    val drained = PersistentDeltaMap.empty<Int, String>(deltaDescending).setItem(1, "x").remove(1)
    deltaCheck(drained.isClean, "a drained map is clean")
    deltaCheckEquals(listOf(3, 2, 1), drained.setItems(listOf(1 to "a", 2 to "b", 3 to "c")).keys(), "the comparator is retained")

    empty.validateStructure()
    map.validateStructure()
    drained.validateStructure()
}

private fun deltaMapRetainedBranchesEvolveIndependently() {
    val source = PersistentDeltaMap.from(listOf(1 to "one", 2 to "two"))
    val left = source.setItem(1, "left").setItem(3, "left3")
    val right = source.setItem(1, "right").remove(2)
    val leftBased = left.checkpoint().setItem(2, "after")
    val rightRolled = right.rollback().setItem(2, "back")

    deltaCheckEquals(listOf(1 to "one", 2 to "two"), deltaEntries(source), "the shared source is unchanged")
    deltaCheckEquals(listOf(1 to "left", 2 to "two", 3 to "left3"), deltaEntries(left), "the left branch")
    deltaCheckEquals(listOf(1 to "right"), deltaEntries(right), "the right branch")
    deltaCheckEquals(listOf(1, 3), deltaChangeKeys(left), "the left change set")
    deltaCheckEquals(listOf(1, 2), deltaChangeKeys(right), "the right change set")

    deltaCheckEquals(listOf(2), deltaChangeKeys(leftBased), "the left branch's new epoch")
    deltaCheckEquals(DeltaMapValue("two"), leftBased.getChange(2)?.before, "the promoted checkpoint supplied before")
    deltaCheckEquals(listOf(2), deltaChangeKeys(rightRolled), "the right branch's restored epoch")
    deltaCheckEquals(DeltaMapValue("two"), rightRolled.getChange(2)?.before, "the restored checkpoint supplied before")
    deltaCheckEquals(listOf(1 to "one", 2 to "back"), deltaEntries(rightRolled), "the rolled-back branch state")

    deltaCheck(left.sharesStorageWith(left), "a version shares storage with itself")
    deltaCheck(!left.sharesRootsWith(right), "diverged branches do not share all roots")
    deltaCheck(source.sharesRootsWith(source.checkpoint()), "checkpointing a clean version keeps every root")

    for (version in listOf(source, left, right, leftBased, rightRolled)) {
        version.validateStructure()
    }
}

private fun deltaMapRandomizedHistoriesMatchTheModel() {
    val random = DeltaLcg(0x5eed_1234L)
    var map = PersistentDeltaMap.empty<Int, Int>()
    var checkpoint = TreeMap<Int, Int>()
    var current = TreeMap<Int, Int>()
    val retained = ArrayList<Triple<PersistentDeltaMap<Int, Int>, TreeMap<Int, Int>, TreeMap<Int, Int>>>()

    for (step in 0 until 1200) {
        val key = random.below(24)
        when (random.below(10)) {
            0, 1, 2, 3 -> {
                val value = random.below(4)
                map = map.setItem(key, value)
                current[key] = value
            }
            4, 5 -> {
                // Deliberately write the checkpoint value back, to exercise cancellation.
                val value = checkpoint[key] ?: random.below(4)
                map = map.setItem(key, value)
                current[key] = value
            }
            6, 7 -> {
                map = map.remove(key)
                current.remove(key)
            }
            8 -> {
                map = map.checkpoint()
                checkpoint = deltaCopy(current)
            }
            else -> {
                map = map.rollback()
                current = deltaCopy(checkpoint)
            }
        }

        deltaAssertModel(map, checkpoint, current, "step $step")
        if (step % 97 == 0) {
            retained.add(Triple(map, deltaCopy(checkpoint), deltaCopy(current)))
        }
    }

    deltaCheck(retained.size > 8, "the history retained several versions")
    for ((index, version) in retained.withIndex()) {
        deltaAssertModel(version.first, version.second, version.third, "retained $index")
    }
}

private fun deltaMapConcurrentReadersSeeSnapshots() {
    val source = PersistentDeltaMap.empty<Int, String>()
        .setItems((0 until 256).map { it to "v$it" })
        .checkpoint()
        .setItems((0 until 256 step 8).map { it to "changed$it" })
    val expectedChanges = source.getChanges()
    val expectedEntries = deltaEntries(source)
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())

    val threads = (0 until 8).map { worker ->
        Thread {
            try {
                repeat(24) {
                    deltaCheckEquals(expectedChanges, source.getChanges(), "worker $worker changes")
                    deltaCheckEquals(expectedEntries, deltaEntries(source), "worker $worker entries")
                    deltaCheckEquals(
                        expectedChanges.filter { change -> change.key in 40..80 },
                        source.getChanges(40, 80),
                        "worker $worker window",
                    )
                    val derived = source.setItem(worker, "worker$worker").remove(worker + 1)
                    derived.validateStructure()
                    deltaCheckEquals(expectedChanges, source.getChanges(), "worker $worker source after deriving")
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }
    }

    threads.forEach(Thread::start)
    threads.forEach(Thread::join)
    if (failures.isNotEmpty()) {
        throw AssertionError("concurrent readers observed a mutated version", failures[0])
    }
}

/** The checkpoint-differential persistent sorted map test cases. */
internal fun persistentDeltaMapTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "deltaMapPointEditsClassifyEveryKind" to ::deltaMapPointEditsClassifyEveryKind,
    "deltaMapEndpointsDistinguishStoredNullFromAbsence" to ::deltaMapEndpointsDistinguishStoredNullFromAbsence,
    "deltaMapWritesCoalesceAndRoundTripsCancel" to ::deltaMapWritesCoalesceAndRoundTripsCancel,
    "deltaMapEquivalentWritesAreSemanticNoOps" to ::deltaMapEquivalentWritesAreSemanticNoOps,
    "deltaMapRepresentativesSurviveUpdatesAndRoundTrips" to ::deltaMapRepresentativesSurviveUpdatesAndRoundTrips,
    "deltaMapCheckpointAndRollbackAreRootSwaps" to ::deltaMapCheckpointAndRollbackAreRootSwaps,
    "deltaMapChangeEnumerationIsOutputOptimal" to ::deltaMapChangeEnumerationIsOutputOptimal,
    "deltaMapRangeRestrictedChangesSeekBoundaries" to ::deltaMapRangeRestrictedChangesSeekBoundaries,
    "deltaMapSetItemsFoldsPointWrites" to ::deltaMapSetItemsFoldsPointWrites,
    "deltaMapReadSurfaceTracksTheCurrentState" to ::deltaMapReadSurfaceTracksTheCurrentState,
    "deltaMapRetainedBranchesEvolveIndependently" to ::deltaMapRetainedBranchesEvolveIndependently,
    "deltaMapRandomizedHistoriesMatchTheModel" to ::deltaMapRandomizedHistoriesMatchTheModel,
    "deltaMapConcurrentReadersSeeSnapshots" to ::deltaMapConcurrentReadersSeeSnapshots,
)
