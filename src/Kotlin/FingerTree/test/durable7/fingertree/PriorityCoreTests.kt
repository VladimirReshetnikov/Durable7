/*
 * Tests for the priority queue, priority search queue, and interval tree, including tie-breaking
 * among equal priorities and overlap pruning.
 */
package durable7.fingertree

import java.util.Collections
import java.util.Random
import java.util.TreeMap
import kotlin.concurrent.thread
import kotlin.random.asKotlinRandom

/** The priority queue, priority search queue, and interval tree test cases. */
internal fun priorityCoreTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "brodalAdversarialShapesDrainInOrder" to ::brodalAdversarialShapesDrainInOrder,
    "brodalRandomizedPersistentHistoryMatchesRetainedModels" to
        ::brodalRandomizedPersistentHistoryMatchesRetainedModels,
    "brodalSharingRepresentativesAndComparatorIdentityAreExact" to
        ::brodalSharingRepresentativesAndComparatorIdentityAreExact,
    "brodalOperationComparisonBoundsStayWorstCase" to ::brodalOperationComparisonBoundsStayWorstCase,
    "brodalConcurrentReadersSeeStableSnapshots" to ::brodalConcurrentReadersSeeStableSnapshots,
    "psqReplacementAndEquivalentKeySemanticsAreExact" to ::psqReplacementAndEquivalentKeySemanticsAreExact,
    "psqNullableKeysPrioritiesAndPayloadsAreUnambiguous" to ::psqNullableKeysPrioritiesAndPayloadsAreUnambiguous,
    "psqRotationsDeletionAndAscendingStackSafetyValidate" to ::psqRotationsDeletionAndAscendingStackSafetyValidate,
    "psqRandomizedRetainedHistoryMatchesModel" to ::psqRandomizedRetainedHistoryMatchesModel,
    "psqRangeThresholdPruningHasAuditedComparisonCounts" to ::psqRangeThresholdPruningHasAuditedComparisonCounts,
    "psqNoOpsPathCopyingAndTiesPreserveIdentity" to ::psqNoOpsPathCopyingAndTiesPreserveIdentity,
    "psqSameReferenceReplacementBypassesEquality" to ::psqSameReferenceReplacementBypassesEquality,
    "psqConcurrentReadersSeeStableSnapshots" to ::psqConcurrentReadersSeeStableSnapshots,
)

private class PsqEqualityCountingValue {
    var equalityCalls: Int = 0
    override fun equals(other: Any?): Boolean { equalityCalls++; return this === other }
    override fun hashCode(): Int = 0
}

private fun psqSameReferenceReplacementBypassesEquality() {
    val value = PsqEqualityCountingValue()
    val queue = PrioritySearchQueue.empty<String, Int, PsqEqualityCountingValue>()
        .setItem("key", 1, value)
    priorityCheck(queue.setItem("key", 1, value) === queue,
        "PSQ same-reference replacement reuses queue")
    priorityCheckEquals(0, value.equalityCalls,
        "PSQ same-reference replacement bypasses equality")
}

private fun brodalAdversarialShapesDrainInOrder() {
    val count = 4_096
    val ascending = BrodalOkasakiHeap.from(0 until count)
    val descending = BrodalOkasakiHeap.from((0 until count).reversed())
    val equal = BrodalOkasakiHeap.from(List(count) { 7 })
    val evens = BrodalOkasakiHeap.from((0 until count / 2).map { it * 2 })
    val odds = BrodalOkasakiHeap.from((0 until count / 2).map { it * 2 + 1 })
    val melded = evens.meld(odds)

    for (heap in listOf(ascending, descending, equal, melded)) {
        val statistics = heap.validateStructure()
        priorityCheckEquals(count, statistics.count, "Brodal adversarial validated count")
        priorityCheck(statistics.maximumRank <= 32, "Brodal adversarial rank remains logarithmic")
    }
    priorityCheckEquals((0 until count).toList(), drainBrodal(ascending), "Brodal ascending drain")
    priorityCheckEquals((0 until count).toList(), drainBrodal(descending), "Brodal descending drain")
    priorityCheckEquals(List(count) { 7 }, drainBrodal(equal), "Brodal equal-priority drain")
    priorityCheckEquals((0 until count).toList(), drainBrodal(melded), "Brodal melded drain")
}

private fun brodalRandomizedPersistentHistoryMatchesRetainedModels() {
    val operationCount = 20_000
    val maximumElements = 2_048
    val random = Random(0x5b0d_a1)
    var current = BrodalVersion.empty()
    val retained = ArrayList<BrodalVersion>().apply { add(current) }
    var inserts = 0
    var melds = 0
    var deletes = 0

    repeat(operationCount) { step ->
        val source = if (retained.size > 1 && random.nextInt(5) == 0) {
            retained[random.nextInt(retained.size)]
        } else {
            current
        }
        val choice = random.nextInt(100)
        current = when {
            choice < 52 || source.model.count == 0 -> {
                inserts++
                source.insert(random.nextInt(257) - 128)
            }
            choice < 76 -> {
                deletes++
                source.deleteMinimum()
            }
            else -> {
                var partner: BrodalVersion? = null
                for (attempt in 0 until 12) {
                    val candidate = retained[random.nextInt(retained.size)]
                    if (source.model.count + candidate.model.count <= maximumElements) {
                        partner = candidate
                        break
                    }
                }
                if (partner == null) {
                    deletes++
                    source.deleteMinimum()
                } else {
                    melds++
                    source.meld(checkNotNull(partner))
                }
            }
        }

        priorityCheckEquals(current.model.count, current.heap.count, "Brodal history count")
        if (current.model.count == 0) {
            priorityCheck(current.heap.isEmpty, "Brodal history empty")
        } else {
            priorityCheckEquals(current.model.minimum(), current.heap.minimum, "Brodal history minimum")
        }
        if (step and 127 == 0) {
            current.heap.validateStructure()
            priorityCheckEquals(current.model.toSortedList(), current.heap.toList().sorted(), "Brodal history model")
        }
        if (step % 37 == 0) {
            if (retained.size < 256) {
                retained.add(current)
            } else {
                retained[random.nextInt(retained.size)] = current
            }
        }
    }

    priorityCheck(inserts > 6_000, "Brodal history contains enough inserts")
    priorityCheck(melds > 500, "Brodal history contains enough melds")
    priorityCheck(deletes > 2_000, "Brodal history contains enough deletes")
    for ((index, version) in retained.withIndex()) {
        if (index % 7 != 0) {
            continue
        }
        priorityCheck(version.root === version.heap.rootIdentityForTesting(), "Brodal retained root identity")
        version.heap.validateStructure()
        priorityCheckEquals(version.model.toSortedList(), version.heap.toList().sorted(), "Brodal retained model")
    }
}

private fun brodalSharingRepresentativesAndComparatorIdentityAreExact() {
    val comparator = Comparator<TaggedPriority> { left, right -> left.priority.compareTo(right.priority) }
    val leftItems = (0 until 1_024).map { TaggedPriority(it % 7, "L$it") }
    val rightItems = (0 until 1_024).map { TaggedPriority(it % 7, "R$it") }
    val heap = BrodalOkasakiHeap.from(leftItems, comparator)
        .meld(BrodalOkasakiHeap.from(rightItems, comparator))
    val drained = drainBrodal(heap)
    priorityCheckEquals(2_048, drained.size, "Brodal retains comparator-equivalent representatives")
    priorityCheckEquals(
        (leftItems + rightItems).map { it.tag }.sorted(),
        drained.map { it.tag }.sorted(),
        "Brodal retains every concrete representative",
    )
    priorityCheck(
        drained.zipWithNext().all { (left, right) -> left.priority <= right.priority },
        "Brodal comparer-equivalent drain is nondecreasing",
    )

    val one = BrodalOkasakiHeap.empty<Int>().insert(0).insert(10)
    val inserted = one.insert(20)
    priorityCheck(one.rootIdentityForTesting() !== inserted.rootIdentityForTesting(), "Brodal insert copies root")
    priorityCheck(one.sharedTreeCount(inserted) >= 1, "Brodal insert shares old off-path tree")
    priorityCheck(one.meld(BrodalOkasakiHeap.empty()) === one, "Brodal right-empty meld is exact no-op")
    priorityCheck(BrodalOkasakiHeap.empty<Int>().meld(one) === one, "Brodal left-empty meld reuses operand")

    val random = Random(0x0ff5_1de)
    val values = (0 until 8_192).shuffled(random.asKotlinRandom())
    val large = BrodalOkasakiHeap.from(values)
    val reduced = large.deleteMinimum()
    val logarithm = 32 - Integer.numberOfLeadingZeros(large.count)
    priorityCheck(
        large.sharedTreeCount(reduced) > large.count - 32 * logarithm,
        "Brodal delete-min shares all but logarithmically many trees",
    )
    large.validateStructure()
    reduced.validateStructure()

    val firstComparator = Comparator<Int> { left, right -> left.compareTo(right) }
    val secondComparator = Comparator<Int> { left, right -> left.compareTo(right) }
    val first = BrodalOkasakiHeap.from(listOf(1, 3), firstComparator)
    val second = BrodalOkasakiHeap.from(listOf(2, 4), secondComparator)
    priorityCheckThrows<IllegalArgumentException>("Brodal comparator identity gate") { first.meld(second) }

    val nullableComparator = Comparator<Int?> { left, right ->
        when {
            left == null && right == null -> 0
            left == null -> -1
            right == null -> 1
            else -> left.compareTo(right)
        }
    }
    val nullable = BrodalOkasakiHeap.empty(nullableComparator).insert(1).insert(null)
    priorityCheckEquals(null, nullable.minimum, "Brodal nullable minimum is unambiguous")
    priorityCheckEquals(null, nullable.minimumView()?.minimum, "Brodal nullable minimum view")
}

private fun brodalOperationComparisonBoundsStayWorstCase() {
    val samples = ArrayList<Pair<Int, Int>>()
    for (count in listOf(1_024, 4_096, 16_384, 65_536)) {
        val comparator = PriorityCountingComparator<Int>(naturalOrder())
        val left = BrodalOkasakiHeap.from(0 until count, comparator)
        val right = BrodalOkasakiHeap.from(count until count * 2, comparator)

        comparator.reset()
        priorityCheckEquals(0, left.minimum, "Brodal minimum value")
        priorityCheckEquals(0, comparator.calls, "Brodal minimum performs no comparisons")

        comparator.reset()
        left.insert(-1)
        priorityCheck(comparator.calls in 1..5, "Brodal insert comparison ceiling at $count")

        comparator.reset()
        left.meld(right)
        priorityCheck(comparator.calls in 1..5, "Brodal meld comparison ceiling at $count")

        comparator.reset()
        for (ignored in left) {
            @Suppress("UNUSED_VARIABLE")
            val keepCompilerHonest = ignored
        }
        priorityCheckEquals(0, comparator.calls, "Brodal iteration performs no comparisons")

        comparator.reset()
        left.deleteMinimum()
        val logarithm = 32 - Integer.numberOfLeadingZeros(count)
        priorityCheck(
            comparator.calls <= 32 * logarithm + 8,
            "Brodal delete-min comparison ceiling at $count: ${comparator.calls}",
        )
        samples.add(count to comparator.calls)
    }
    priorityCheck(
        samples.last().second <= samples.first().second + 32 * 6 + 8,
        "Brodal delete-min comparison growth is logarithmic",
    )
}

private fun brodalConcurrentReadersSeeStableSnapshots() {
    val heap = BrodalOkasakiHeap.from(0 until 4_096)
    val snapshot = heap.insert(-1)
    priorityRunConcurrent("brodal-readers") {
        repeat(16) {
            priorityCheckEquals(4_096, heap.count, "concurrent Brodal count")
            priorityCheckEquals(0, heap.minimum, "concurrent Brodal minimum")
            priorityCheckEquals(-1, snapshot.minimum, "concurrent Brodal snapshot minimum")
            priorityCheckEquals(4_096, heap.toList().size, "concurrent Brodal iteration")
        }
    }
}

private fun psqReplacementAndEquivalentKeySemanticsAreExact() {
    val priorityComparator = Comparator<PriorityProbe> { left, right -> left.rank.compareTo(right.rank) }
    val originalPriority = PriorityProbe(rank = 10, equalityClass = 1)
    val queue = PrioritySearchQueue.empty<Int, PriorityProbe, String>(naturalOrder(), priorityComparator)
        .setItem(7, originalPriority, "payload")

    val comparerEqualButDefaultUnequal = PriorityProbe(rank = 10, equalityClass = 2)
    val representationUpdate = queue.setItem(7, comparerEqualButDefaultUnequal, "payload")
    priorityCheck(queue !== representationUpdate, "PSQ comparer-equal/default-unequal priority updates")
    priorityCheck(
        representationUpdate.getEntryOrNull(7)?.priority === comparerEqualButDefaultUnequal,
        "PSQ stores comparer-equal replacement object",
    )
    priorityCheck(queue.minimum.priority === originalPriority, "PSQ prior snapshot retains priority object")

    val defaultEqualButComparerDistinct = PriorityProbe(rank = 1, equalityClass = 2)
    val semanticUpdate = representationUpdate.setItem(7, defaultEqualButComparerDistinct, "payload")
    priorityCheck(representationUpdate !== semanticUpdate, "PSQ default-equal/comparer-distinct priority updates")
    priorityCheck(
        semanticUpdate.getEntryOrNull(7)?.priority === defaultEqualButComparerDistinct,
        "PSQ reprioritizes stored entry",
    )
    priorityCheck(
        semanticUpdate.setItem(7, defaultEqualButComparerDistinct, "payload") === semanticUpdate,
        "PSQ exact replacement is no-op",
    )

    val originalKey = String(charArrayOf('A', 'l', 'p', 'h', 'a'))
    val keyed = PrioritySearchQueue.empty<String, Int, String>(String.CASE_INSENSITIVE_ORDER, naturalOrder())
        .setItem(originalKey, 9, "first")
    val updated = keyed.setItem("ALPHA", 2, "second")
    val stored = checkNotNull(updated.getEntryOrNull("alpha"))
    priorityCheck(stored.key === originalKey, "PSQ keeps first equivalent key representative")
    priorityCheckEquals(2, stored.priority, "PSQ equivalent-key priority update")
    priorityCheckEquals("second", stored.value, "PSQ equivalent-key payload update")
    val duplicate = updated.tryAdd("aLpHa", 0, "third")
    priorityCheck(!duplicate.added && duplicate.queue === updated, "PSQ duplicate tryAdd is exact no-op")
    val removed = updated.tryRemove("ALpha")
    priorityCheck(removed.removed, "PSQ equivalent-key removal succeeds")
    priorityCheck(removed.entry?.key === originalKey, "PSQ removal returns stored key representative")
    priorityCheck(removed.queue.isEmpty, "PSQ equivalent-key removal empties queue")
    queue.validateStructure()
    representationUpdate.validateStructure()
    semanticUpdate.validateStructure()
    updated.validateStructure()
}

private fun psqNullableKeysPrioritiesAndPayloadsAreUnambiguous() {
    val nullableOrder = Comparator<Int?> { left, right ->
        when {
            left == null && right == null -> 0
            left == null -> -1
            right == null -> 1
            else -> left.compareTo(right)
        }
    }
    val empty = PrioritySearchQueue.empty<Int?, Int?, String?>(nullableOrder, nullableOrder)
    val withNull = empty.setItem(null, null, null)
    val queue = withNull.setItem(1, 1, "one")

    priorityCheckEquals(
        PrioritySearchEntry<Int?, Int?, String?>(null, null, null),
        queue.getEntryOrNull(null),
        "PSQ nullable stored entry lookup",
    )
    priorityCheckEquals(null, queue.getEntryOrNull(2), "PSQ nullable missing entry lookup")
    priorityCheckEquals(
        PrioritySearchEntry<Int?, Int?, String?>(null, null, null),
        queue.minimumOrNull(),
        "PSQ nullable minimum wrapper",
    )
    priorityCheck(
        queue.setItem(null, null, null) === queue,
        "PSQ nullable exact replacement is an identity no-op",
    )

    val minimumView = queue.minimumView() ?: throw AssertionError("PSQ nullable minimum view was absent")
    priorityCheckEquals(
        PrioritySearchEntry<Int?, Int?, String?>(null, null, null),
        minimumView.entry,
        "PSQ nullable minimum view entry",
    )
    priorityCheckEquals(
        listOf(PrioritySearchEntry(1, 1, "one")),
        minimumView.remainder.toList(),
        "PSQ nullable view remainder",
    )

    val removed = queue.tryRemove(null)
    priorityCheck(removed.removed, "PSQ nullable key removal succeeds")
    priorityCheckEquals(
        PrioritySearchEntry<Int?, Int?, String?>(null, null, null),
        removed.entry,
        "PSQ nullable removal returns entry wrapper",
    )
    priorityCheckEquals(
        listOf(PrioritySearchEntry(1, 1, "one")),
        removed.queue.toList(),
        "PSQ nullable removal remainder",
    )
    val absent = queue.tryRemove(2)
    priorityCheck(
        !absent.removed && absent.entry == null && absent.queue === queue,
        "PSQ absent nullable removal is unambiguous",
    )
    queue.validateStructure()
    removed.queue.validateStructure()
}

private fun psqRotationsDeletionAndAscendingStackSafetyValidate() {
    var queue = PrioritySearchQueue.empty<Int, Int, Int>()
    val insertionOrder = (0 until 2_048).sortedBy { Integer.reverse(it) }
    for ((index, key) in insertionOrder.withIndex()) {
        queue = queue.setItem(key, key * 37 % 101, -key)
        if (index and 63 == 0) {
            queue.validateStructure()
        }
    }
    for (key in 0 until 2_048) {
        if (key % 3 != 1) {
            queue = queue.remove(key)
            if (key and 63 == 0) {
                queue.validateStructure()
            }
        }
    }
    priorityCheckEquals(
        (0 until 2_048).filter { it % 3 == 1 },
        queue.map { it.key },
        "PSQ rotation/deletion survivors",
    )
    queue.validateStructure()

    var ascending = PrioritySearchQueue.empty<Int, Int, Int>()
    repeat(50_000) { key ->
        ascending = ascending.setItem(key, 50_000 - key, key)
    }
    val statistics = ascending.validateStructure()
    priorityCheck(statistics.height < 32, "PSQ ascending construction remains logarithmic")
    priorityCheckEquals(49_999, ascending.minimum.key, "PSQ ascending winner cache")
    priorityCheckEquals(50_000, ascending.count, "PSQ ascending stack-safe count")
}

private fun psqRandomizedRetainedHistoryMatchesModel() {
    val random = Random(0x5a17_2026)
    var queue = PrioritySearchQueue.empty<Int, Int, Int>()
    val model = TreeMap<Int, PsqModelValue>()
    val retained = ArrayList<PsqSnapshot>()

    repeat(20_000) { step ->
        val key = random.nextInt(1_537) - 768
        when (random.nextInt(8)) {
            in 0..3 -> {
                val priority = random.nextInt(64)
                queue = queue.setItem(key, priority, step)
                model[key] = PsqModelValue(priority, step)
            }
            4 -> {
                queue = queue.remove(key)
                model.remove(key)
            }
            5 -> {
                val priority = random.nextInt(64)
                val expectedAdded = !model.containsKey(key)
                val result = queue.tryAdd(key, priority, step)
                priorityCheckEquals(expectedAdded, result.added, "PSQ randomized tryAdd result")
                queue = result.queue
                if (expectedAdded) {
                    model[key] = PsqModelValue(priority, step)
                }
            }
            else -> {
                if (model.isEmpty()) {
                    val priority = random.nextInt(64)
                    queue = queue.setItem(key, priority, step)
                    model[key] = PsqModelValue(priority, step)
                } else {
                    val expected = model.entries.minWithOrNull(
                        compareBy<Map.Entry<Int, PsqModelValue>> { it.value.priority }.thenBy { it.key },
                    ) ?: throw AssertionError("PSQ nonempty model had no minimum")
                    val view = queue.deleteMinimum()
                    priorityCheckEquals(
                        PrioritySearchEntry(expected.key, expected.value.priority, expected.value.value),
                        view.entry,
                        "PSQ randomized delete-min",
                    )
                    queue = view.remainder
                    model.remove(expected.key)
                }
            }
        }

        if (step % 127 == 0) {
            assertPsqModel(queue, model)
        }
        if (step % 239 == 0) {
            retained.add(PsqSnapshot(queue, psqModelEntries(model)))
        }
    }

    assertPsqModel(queue, model)
    for (snapshot in retained) {
        priorityCheckEquals(snapshot.entries, snapshot.queue.toList(), "PSQ retained snapshot")
        snapshot.queue.validateStructure()
    }
}

private fun psqRangeThresholdPruningHasAuditedComparisonCounts() {
    val keyComparator = PriorityCountingComparator<Int>(naturalOrder())
    val priorityComparator = PriorityCountingComparator<Int>(naturalOrder())
    var queue = PrioritySearchQueue.empty<Int, Int, Int>(keyComparator, priorityComparator)
    for (key in 0 until 4_095) {
        queue = queue.setItem(key, key % 17, -key)
    }
    queue.validateStructure()

    keyComparator.reset()
    priorityComparator.reset()
    priorityCheckEquals(
        emptyList(),
        queue.enumerateAtMost(0, 4_094, -1).toList(),
        "PSQ impossible threshold",
    )
    priorityCheckEquals(1, keyComparator.calls, "PSQ range validation is one key comparison")
    priorityCheckEquals(1, priorityComparator.calls, "PSQ root winner prunes impossible threshold")

    keyComparator.reset()
    priorityComparator.reset()
    val exact = queue.enumerateAtMost(2_047, 2_047, 16).toList()
    priorityCheckEquals(
        listOf(PrioritySearchEntry(2_047, 2_047 % 17, -2_047)),
        exact,
        "PSQ exact-key range",
    )
    val visitedNodes = priorityComparator.calls - exact.size
    priorityCheck(visitedNodes in 1..queue.height, "PSQ exact range visits one search path")
    priorityCheckEquals(1 + 2 * visitedNodes, keyComparator.calls, "PSQ exact range key comparison equation")

    val expected = (512..1_024)
        .filter { it % 17 <= 3 }
        .map { PrioritySearchEntry(it, it % 17, -it) }
    priorityCheckEquals(
        expected,
        queue.enumerateAtMost(512, 1_024, 3).toList(),
        "PSQ mixed threshold remains in key order",
    )
    priorityCheckThrows<IllegalArgumentException>("PSQ inverted key range") {
        queue.enumerateAtMost(5, 4, 10)
    }
}

private fun psqNoOpsPathCopyingAndTiesPreserveIdentity() {
    val queue = PrioritySearchQueue.from(
        (0 until 256).map { PrioritySearchEntry(it, it + 10, -it) },
    )
    val root = queue.rootIdentityForTesting()
    val farNode = queue.nodeIdentityForTesting(200)
    val exactNoOp = queue.setItem(127, 137, -127)
    priorityCheck(exactNoOp === queue, "PSQ exact replacement reuses queue")
    priorityCheck(exactNoOp.rootIdentityForTesting() === root, "PSQ exact replacement reuses root")
    priorityCheck(queue.remove(-1) === queue, "PSQ absent removal reuses queue")
    val duplicate = queue.tryAdd(127, 0, 0)
    priorityCheck(!duplicate.added && duplicate.queue === queue, "PSQ duplicate insertion reuses queue")

    val updated = queue.setItem(0, -1, 42)
    priorityCheck(updated.rootIdentityForTesting() !== root, "PSQ path update copies root")
    priorityCheck(updated.nodeIdentityForTesting(200) === farNode, "PSQ path update shares far subtree")
    priorityCheck(queue.sharedNodeCount(updated) >= 240, "PSQ path update shares at least 240/256 nodes")
    priorityCheckEquals(PrioritySearchEntry(0, 10, 0), queue.getEntryOrNull(0), "PSQ old snapshot entry")
    priorityCheckEquals(PrioritySearchEntry(0, -1, 42), updated.getEntryOrNull(0), "PSQ new snapshot entry")
    queue.validateStructure()
    updated.validateStructure()

    val descendingKeys = Comparator<Int> { left, right -> right.compareTo(left) }
    val priorityBuckets = Comparator<PriorityProbe> { left, right ->
        (left.rank / 10).compareTo(right.rank / 10)
    }
    var ties = PrioritySearchQueue.empty<Int, PriorityProbe, String>(descendingKeys, priorityBuckets)
    for (key in listOf(2, 8, 0, 6, 9, 1, 7, 3, 5, 4)) {
        ties = ties.setItem(key, PriorityProbe(10 + key, 100 + key), "v$key")
    }
    for (expectedKey in 9 downTo 0) {
        priorityCheckEquals(expectedKey, ties.minimum.key, "PSQ comparer-equal tie winner")
        val view = ties.deleteMinimum()
        priorityCheckEquals(expectedKey, view.entry.key, "PSQ comparer-equal tie deletion")
        priorityCheckEquals("v$expectedKey", view.entry.value, "PSQ tie payload")
        ties = view.remainder
        ties.validateStructure()
    }
    priorityCheck(ties.isEmpty, "PSQ tie drain empties queue")
}

private fun psqConcurrentReadersSeeStableSnapshots() {
    val queue = PrioritySearchQueue.from(
        (0 until 4_096).map { PrioritySearchEntry(it, it % 17, -it) },
    )
    val updated = queue.setItem(4_096, -1, 1)
    priorityRunConcurrent("psq-readers") {
        repeat(32) {
            priorityCheckEquals(4_096, queue.count, "concurrent PSQ count")
            priorityCheckEquals(0, queue.minimum.key, "concurrent PSQ winner")
            priorityCheckEquals(4_096, updated.minimum.key, "concurrent PSQ updated winner")
            priorityCheckEquals(4_096, queue.toList().size, "concurrent PSQ iteration")
            priorityCheckEquals(
                (512..768).count { it % 17 <= 3 },
                queue.enumerateAtMost(512, 768, 3).count(),
                "concurrent PSQ pruned query",
            )
        }
    }
}

private data class TaggedPriority(val priority: Int, val tag: String)

private class PriorityProbe(val rank: Int, private val equalityClass: Int) {
    override fun equals(other: Any?): Boolean =
        other is PriorityProbe && equalityClass == other.equalityClass

    override fun hashCode(): Int = equalityClass
}

private class PriorityCountingComparator<T>(private val inner: Comparator<in T>) : Comparator<T> {
    var calls: Int = 0
        private set

    override fun compare(left: T, right: T): Int {
        calls++
        return inner.compare(left, right)
    }

    fun reset() {
        calls = 0
    }
}

private data class BrodalModel(private val counts: Map<Int, Int>, val count: Int) {
    companion object {
        fun empty(): BrodalModel = BrodalModel(emptyMap(), 0)
    }

    fun minimum(): Int = counts.keys.min()

    fun insert(item: Int): BrodalModel {
        val copy = HashMap(counts)
        copy[item] = (copy[item] ?: 0) + 1
        return BrodalModel(copy, Math.addExact(count, 1))
    }

    fun deleteMinimum(): BrodalModel {
        val minimum = minimum()
        val copy = HashMap(counts)
        val multiplicity = checkNotNull(copy[minimum])
        if (multiplicity == 1) {
            copy.remove(minimum)
        } else {
            copy[minimum] = multiplicity - 1
        }
        return BrodalModel(copy, count - 1)
    }

    fun meld(other: BrodalModel): BrodalModel {
        val copy = HashMap(counts)
        for ((item, multiplicity) in other.counts) {
            copy[item] = Math.addExact(copy[item] ?: 0, multiplicity)
        }
        return BrodalModel(copy, Math.addExact(count, other.count))
    }

    fun toSortedList(): List<Int> = counts.entries
        .sortedBy { it.key }
        .flatMap { (item, multiplicity) -> List(multiplicity) { item } }
}

private data class BrodalVersion(
    val heap: BrodalOkasakiHeap<Int>,
    val model: BrodalModel,
    val root: Any?,
) {
    companion object {
        fun empty(): BrodalVersion {
            val heap = BrodalOkasakiHeap.empty<Int>()
            return BrodalVersion(heap, BrodalModel.empty(), heap.rootIdentityForTesting())
        }
    }

    fun insert(item: Int): BrodalVersion {
        val updated = heap.insert(item)
        return BrodalVersion(updated, model.insert(item), updated.rootIdentityForTesting())
    }

    fun deleteMinimum(): BrodalVersion {
        val view = heap.deleteMinimum()
        return BrodalVersion(view, model.deleteMinimum(), view.rootIdentityForTesting())
    }

    fun meld(other: BrodalVersion): BrodalVersion {
        val updated = heap.meld(other.heap)
        return BrodalVersion(updated, model.meld(other.model), updated.rootIdentityForTesting())
    }
}

private data class PsqModelValue(val priority: Int, val value: Int)

private data class PsqSnapshot(
    val queue: PrioritySearchQueue<Int, Int, Int>,
    val entries: List<PrioritySearchEntry<Int, Int, Int>>,
)

private fun psqModelEntries(model: Map<Int, PsqModelValue>): List<PrioritySearchEntry<Int, Int, Int>> =
    model.entries.map { PrioritySearchEntry(it.key, it.value.priority, it.value.value) }

private fun assertPsqModel(
    queue: PrioritySearchQueue<Int, Int, Int>,
    model: Map<Int, PsqModelValue>,
) {
    priorityCheckEquals(psqModelEntries(model), queue.toList(), "PSQ randomized model entries")
    if (model.isEmpty()) {
        priorityCheckEquals(null, queue.minimumOrNull(), "PSQ empty model minimum")
    } else {
        val expected = model.entries.minWithOrNull(
            compareBy<Map.Entry<Int, PsqModelValue>> { it.value.priority }.thenBy { it.key },
        ) ?: throw AssertionError("PSQ nonempty model had no winner")
        priorityCheckEquals(expected.key, queue.minimum.key, "PSQ randomized winner key")
        priorityCheckEquals(expected.value.priority, queue.minimum.priority, "PSQ randomized winner priority")
        priorityCheckEquals(expected.value.value, queue.minimum.value, "PSQ randomized winner payload")
    }
    val statistics = queue.validateStructure()
    priorityCheckEquals(queue.count, statistics.count, "PSQ validated count")
    priorityCheckEquals(queue.height, statistics.height, "PSQ validated height")
    priorityCheck(statistics.maximumAbsoluteBalanceFactor in 0..1, "PSQ validated balance")
}

private fun <T> drainBrodal(initial: BrodalOkasakiHeap<T>): List<T> {
    var heap = initial
    val result = ArrayList<T>(heap.count)
    while (!heap.isEmpty) {
        val view = heap.minimumView() ?: throw AssertionError("Brodal nonempty heap had no minimum view")
        result.add(view.minimum)
        heap = view.remainder
        if (result.size and 255 == 0) {
            heap.validateStructure()
        }
    }
    return result
}

private fun priorityRunConcurrent(name: String, workers: Int = 8, action: () -> Unit) {
    val failures = Collections.synchronizedList(ArrayList<Throwable>())
    val threads = List(workers) { worker ->
        thread(start = false, name = "$name-$worker") {
            try {
                action()
            } catch (error: Throwable) {
                failures.add(error)
            }
        }
    }
    threads.forEach(Thread::start)
    threads.forEach(Thread::join)
    if (failures.isNotEmpty()) {
        throw AssertionError("$name had ${failures.size} worker failure(s)", failures.first())
    }
}

private inline fun <reified T : Throwable> priorityCheckThrows(message: String, action: () -> Unit) {
    try {
        action()
    } catch (error: Throwable) {
        if (error is T) {
            return
        }
        throw AssertionError("$message expected ${T::class.simpleName}, got ${error::class.simpleName}", error)
    }
    throw AssertionError("$message expected ${T::class.simpleName}")
}

private fun <T> priorityCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message expected <$expected>, actual <$actual>")
    }
}

private fun priorityCheck(condition: Boolean, message: String) {
    if (!condition) {
        throw AssertionError(message)
    }
}
