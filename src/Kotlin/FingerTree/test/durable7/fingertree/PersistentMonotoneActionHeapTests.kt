/*
 * Tests for the persistent monotone-action heap. The properties under test are that a pending
 * action is indistinguishable from an applied one across every read path, that composition runs
 * newer-outer at every tag-pushdown site, and that the transform is temporal: entries arriving
 * after it are never retroactively transformed.
 */
package durable7.fingertree

import java.util.Collections
import java.util.Random
import kotlin.concurrent.thread

/** The persistent monotone-action heap test cases. */
internal fun persistentMonotoneActionHeapTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "actionClampAlgebraCollapsesEveryCompositionCase" to
        ::actionClampAlgebraCollapsesEveryCompositionCase,
    "actionClampCompositionKeepsExactRepresentatives" to
        ::actionClampCompositionKeepsExactRepresentatives,
    "actionHeapCompositionDirectionIsNewerOuter" to ::actionHeapCompositionDirectionIsNewerOuter,
    "actionHeapTransformIsConstantTimeAndTemporal" to
        ::actionHeapTransformIsConstantTimeAndTemporal,
    "actionHeapMeldKeepsIndependentActionHistories" to
        ::actionHeapMeldKeepsIndependentActionHistories,
    "actionHeapSelfMeldDuplicatesEveryOccurrence" to ::actionHeapSelfMeldDuplicatesEveryOccurrence,
    "actionHeapDrainsAdversarialShapesInOrder" to ::actionHeapDrainsAdversarialShapesInOrder,
    "actionHeapRandomizedTransformHistoryMatchesModel" to
        ::actionHeapRandomizedTransformHistoryMatchesModel,
    "actionHeapEmptyOperationsReturnNullAndStayUsable" to
        ::actionHeapEmptyOperationsReturnNullAndStayUsable,
    "actionHeapTieBreakingIsExactAndDeterministic" to ::actionHeapTieBreakingIsExactAndDeterministic,
    "actionHeapValidateStructureAuditsTagsRanksAndCount" to
        ::actionHeapValidateStructureAuditsTagsRanksAndCount,
    "actionHeapNullablePayloadsAndPrioritiesAreUnambiguous" to
        ::actionHeapNullablePayloadsAndPrioritiesAreUnambiguous,
    "actionHeapPolicyGatesAndClampBoundsAreEnforced" to
        ::actionHeapPolicyGatesAndClampBoundsAreEnforced,
    "actionHeapConcurrentReadersSeeStableSnapshots" to
        ::actionHeapConcurrentReadersSeeStableSnapshots,
)

/** A priority whose comparison ignores [label], so representative choices become observable. */
private data class ClampMark(val value: Int, val label: String)

private val clampMarkComparator: Comparator<ClampMark> =
    Comparator { left, right -> left.value.compareTo(right.value) }

/** A policy with genuine value equality, exercising the meld gate's `==` half. */
private data class ValueEqualClampPolicy(
    override val comparator: Comparator<in Int>,
) : MonotoneHeapActionPolicy<Int, OrderClamp<Int>> {
    private val delegate = OrderClampPolicy(comparator)

    override val identity: OrderClamp<Int>
        get() = delegate.identity

    override fun isIdentity(action: OrderClamp<Int>): Boolean = delegate.isIdentity(action)

    override fun compose(outer: OrderClamp<Int>, inner: OrderClamp<Int>): OrderClamp<Int> =
        delegate.compose(outer, inner)

    override fun apply(action: OrderClamp<Int>, priority: Int): Int = delegate.apply(action, priority)
}

private fun actionClampAlgebraCollapsesEveryCompositionCase() {
    val policy = OrderClampPolicy.natural<Int>()
    val identity = policy.identity
    val floor = policy.atLeast(10)
    val cap = policy.atMost(5)
    val window = policy.between(2, 8)
    val fixed = policy.constant(7)

    actionCheck(policy.isIdentity(identity), "the clamp identity is recognized")
    actionCheck(!policy.isIdentity(floor), "a floor is not the identity")
    actionCheck(!policy.isIdentity(policy.constant(0)), "a constant is not the identity")
    actionCheck(policy.compose(identity, floor) === floor, "an identity outer short-circuits")
    actionCheck(policy.compose(floor, identity) === floor, "an identity inner short-circuits")

    actionCheckEquals(policy.constant(7), policy.compose(fixed, floor), "an outer constant wins")
    actionCheckEquals(
        policy.constant(10),
        policy.compose(floor, fixed),
        "an inner constant takes the outer floor",
    )
    actionCheckEquals(
        policy.constant(5),
        policy.compose(cap, fixed),
        "an inner constant takes the outer cap",
    )

    val capAfterFloor = policy.compose(cap, floor)
    actionCheck(capAfterFloor.isConstant, "a cap composed after a floor collapses")
    actionCheckEquals(5, capAfterFloor.constantValue, "the collapse keeps the newer cap boundary")
    val floorAfterCap = policy.compose(floor, cap)
    actionCheck(floorAfterCap.isConstant, "a floor composed after a cap collapses")
    actionCheckEquals(10, floorAfterCap.constantValue, "the collapse keeps the newer floor boundary")
    actionCheck(capAfterFloor != floorAfterCap, "clamp composition is not commutative")

    val intersected = policy.compose(policy.between(4, 12), window)
    actionCheck(!intersected.isConstant, "an overlapping composition stays bounded")
    actionCheck(intersected.hasLowerBound && intersected.hasUpperBound, "both bounds survive")
    actionCheckEquals(4, intersected.lowerBound, "the intersected lower bound is the larger one")
    actionCheckEquals(8, intersected.upperBound, "the intersected upper bound is the smaller one")
    actionCheckEquals(
        policy.atLeast(10),
        policy.compose(policy.atLeast(4), floor),
        "a floor join keeps the tighter lower bound",
    )
    actionCheckEquals(
        policy.atMost(5),
        policy.compose(policy.atMost(9), cap),
        "a cap join keeps the tighter upper bound",
    )

    actionCheckEquals(3, policy.apply(identity, 3), "the identity preserves its input")
    actionCheckEquals(7, policy.apply(fixed, -100), "a constant ignores its input")
    actionCheckEquals(7, policy.apply(fixed, 1_000), "a constant ignores a large input too")
    actionCheckEquals(10, policy.apply(floor, 3), "a floor raises")
    actionCheckEquals(11, policy.apply(floor, 11), "a floor preserves above its bound")
    actionCheckEquals(3, policy.apply(cap, 3), "a cap preserves below its bound")
    actionCheckEquals(5, policy.apply(cap, 50), "a cap lowers")
    actionCheckEquals(2, policy.apply(window, 0), "a window raises to its lower bound")
    actionCheckEquals(8, policy.apply(window, 99), "a window lowers to its upper bound")
    actionCheckEquals(5, policy.apply(window, 5), "a window preserves an interior priority")

    // Associativity over a deliberately non-commutative triple.
    val outer = policy.atMost(6)
    val middle = policy.atLeast(3)
    val inner = policy.between(0, 20)
    actionCheckEquals(
        policy.compose(policy.compose(outer, middle), inner),
        policy.compose(outer, policy.compose(middle, inner)),
        "clamp composition is associative",
    )
    for (priority in -5..25) {
        actionCheckEquals(
            policy.apply(outer, policy.apply(middle, policy.apply(inner, priority))),
            policy.apply(policy.compose(policy.compose(outer, middle), inner), priority),
            "composed application agrees with sequential application at $priority",
        )
    }
}

private fun actionClampCompositionKeepsExactRepresentatives() {
    val policy = OrderClampPolicy(clampMarkComparator)
    val older = ClampMark(3, "older")
    val newer = ClampMark(3, "newer")

    val composedLower = policy.compose(policy.atLeast(newer), policy.atLeast(older))
    actionCheckEquals(
        older,
        composedLower.lowerBound,
        "equal lower boundaries keep the inner (older) representative",
    )
    val composedUpper = policy.compose(policy.atMost(newer), policy.atMost(older))
    actionCheckEquals(
        older,
        composedUpper.upperBound,
        "equal upper boundaries keep the inner (older) representative",
    )

    val collapsed = policy.compose(
        policy.atMost(ClampMark(1, "cap")),
        policy.atLeast(ClampMark(9, "floor")),
    )
    actionCheck(collapsed.isConstant, "disjoint marked bounds collapse")
    actionCheckEquals(
        ClampMark(1, "cap"),
        collapsed.constantValue,
        "a disjoint collapse carries the outer (newer) boundary",
    )

    val basis = PersistentMonotoneActionHeap.from(
        listOf(MonotoneHeapEntry("a", ClampMark(3, "stored"))),
        policy,
    )
    actionCheckEquals(
        ClampMark(3, "replacement"),
        basis.transformAll(policy.constant(ClampMark(3, "replacement"))).minimumOrNull()!!.priority,
        "an exact constant hands back its own representative",
    )
    actionCheckEquals(
        ClampMark(3, "stored"),
        basis.transformAll(policy.between(ClampMark(3, "low"), ClampMark(3, "high")))
            .minimumOrNull()!!.priority,
        "an inclusive clamp preserves an equivalent input",
    )
}

private fun actionHeapCompositionDirectionIsNewerOuter() {
    val policy = OrderClampPolicy.natural<Int>()
    val basis = PersistentMonotoneActionHeap.from(
        listOf(entry("low", 0), entry("mid", 7), entry("high", 100)),
        policy,
    )

    // A cap composed after a floor must collapse to the cap's boundary; reversing the tag-pushdown
    // direction anywhere would produce the floor's boundary instead.
    val cappedAfterFloored = basis.transformAll(policy.atLeast(10)).transformAll(policy.atMost(5))
    actionCheckEquals(
        listOf(5, 5, 5),
        drainPriorities(cappedAfterFloored),
        "a cap after a floor collapses to the cap boundary",
    )
    val flooredAfterCapped = basis.transformAll(policy.atMost(5)).transformAll(policy.atLeast(10))
    actionCheckEquals(
        listOf(10, 10, 10),
        drainPriorities(flooredAfterCapped),
        "a floor after a cap collapses to the floor boundary",
    )

    val windowed = basis.transformAll(policy.atLeast(5)).transformAll(policy.atMost(50))
    actionCheckEquals(listOf(5, 7, 50), drainPriorities(windowed), "a floor then a cap intersects")
    actionCheckEquals(listOf(0, 7, 100), drainPriorities(basis), "the source version is unchanged")

    // The same direction has to hold once the tags reach forest spines and fused children, which is
    // where the pushdown sites multiply.
    val deep = PersistentMonotoneActionHeap.from((0 until 200).map { entry("e$it", it) }, policy)
        .transformAll(policy.atLeast(50))
        .transformAll(policy.atMost(20))
    actionCheckEquals(List(200) { 20 }, drainPriorities(deep), "a deep heap collapses the same way")
    deep.validateStructure()

    val stagedThenDeleted = PersistentMonotoneActionHeap
        .from((0 until 200).map { entry("e$it", it) }, policy)
        .transformAll(policy.atLeast(50))
        .deleteMinimum()!!
        .transformAll(policy.atMost(20))
    actionCheckEquals(
        List(199) { 20 },
        drainPriorities(stagedThenDeleted),
        "an interleaved deletion does not disturb the composition direction",
    )
}

private fun actionHeapTransformIsConstantTimeAndTemporal() {
    val policy = OrderClampPolicy.natural<Int>()
    val basis = PersistentMonotoneActionHeap.from((0 until 64).map { entry("e$it", it) }, policy)
    actionCheck(basis.rootChildrenIdentityForTesting() != null, "the basis root has a child forest")

    val capped = basis.transformAll(policy.atMost(3))
    actionCheck(
        capped.rootChildrenIdentityForTesting() === basis.rootChildrenIdentityForTesting(),
        "a whole-heap transform reuses the entire root child forest",
    )
    actionCheck(
        capped.rootIdentityForTesting() !== basis.rootIdentityForTesting(),
        "a whole-heap transform publishes a new root",
    )
    actionCheckEquals(basis.count, capped.count, "a transform preserves the count")
    val cappedPriorities = List(64) { minOf(it, 3) }.sorted()
    actionCheckEquals(cappedPriorities, drainPriorities(capped), "the capped priorities")

    actionCheck(basis.transformAll(policy.identity) === basis, "an identity transform reuses its source")
    val emptyHeap = PersistentMonotoneActionHeap.empty<String, Int, OrderClamp<Int>>(policy)
    actionCheck(
        emptyHeap.transformAll(policy.atMost(3)) === emptyHeap,
        "an empty transform reuses its source",
    )

    // Temporal semantics: an entry inserted after the transform is never retroactively transformed.
    val extended = capped.insert("late", 99)
    actionCheckEquals(
        cappedPriorities + listOf(99),
        drainPriorities(extended),
        "an entry inserted after a transform stays untransformed",
    )
    actionCheckEquals(99, extended.toList().single { it.element == "late" }.priority, "the late priority")
    actionCheckEquals(64, capped.count, "the transformed source keeps its own count")
    extended.validateStructure()

    // A later transform does reach every entry present at that moment, including the late one.
    actionCheckEquals(
        listOf(0, 1) + List(63) { 2 },
        drainPriorities(extended.transformAll(policy.atMost(2))),
        "a later transform reaches every current entry",
    )

    // The same holds when the transformed heap is the loser of the next insert's root comparison.
    val underCut = capped.insert("first", -10)
    actionCheckEquals("first", underCut.minimumOrNull()!!.element, "a smaller insert takes the root")
    actionCheckEquals(
        listOf(-10) + cappedPriorities,
        drainPriorities(underCut),
        "an entry inserted below the root also stays untransformed",
    )
}

private fun actionHeapMeldKeepsIndependentActionHistories() {
    val policy = OrderClampPolicy.natural<Int>()
    val left = PersistentMonotoneActionHeap.from(listOf(entry("a", 1), entry("b", 2)), policy)
        .transformAll(policy.atLeast(10))
    val right = PersistentMonotoneActionHeap.from(listOf(entry("c", 3), entry("d", 4)), policy)
        .transformAll(policy.atMost(1))

    val melded = left.meld(right)
    actionCheckEquals(4, melded.count, "the melded count")
    actionCheckEquals(
        listOf(1, 1, 10, 10),
        drainPriorities(melded),
        "each operand keeps its own non-invertible history",
    )
    actionCheckEquals(
        listOf("c" to 1, "d" to 1, "a" to 10, "b" to 10),
        sortedEntries(melded),
        "melded entries keep the transform they arrived with",
    )
    actionCheckEquals(listOf(10, 10), drainPriorities(left), "the left source is unchanged")
    actionCheckEquals(listOf(1, 1), drainPriorities(right), "the right source is unchanged")
    melded.validateStructure()

    actionCheckEquals(
        listOf(0, 0, 0, 0),
        drainPriorities(melded.transformAll(policy.constant(0))),
        "a later transform reaches both operands",
    )

    val emptyHeap = PersistentMonotoneActionHeap.empty<String, Int, OrderClamp<Int>>(policy)
    actionCheck(emptyHeap.meld(left) === left, "melding into an empty heap returns the other operand")
    actionCheck(left.meld(emptyHeap) === left, "melding an empty heap returns the receiver")

    // A meld arriving after a transform is not retroactively transformed either.
    val staged = left.meld(PersistentMonotoneActionHeap.from(listOf(entry("e", 0)), policy))
    actionCheckEquals(listOf(0, 10, 10), drainPriorities(staged), "a later meld stays untransformed")
    staged.validateStructure()
}

private fun actionHeapSelfMeldDuplicatesEveryOccurrence() {
    val policy = OrderClampPolicy.natural<Int>()
    val basis = PersistentMonotoneActionHeap.from((0 until 8).map { entry("e$it", it) }, policy)

    val doubled = basis.meld(basis)
    actionCheckEquals(16, doubled.count, "a self-meld doubles the count")
    actionCheckEquals(
        (0 until 8).flatMap { listOf(it, it) },
        drainPriorities(doubled),
        "a self-meld duplicates every logical occurrence",
    )
    val quadrupled = doubled.meld(doubled)
    actionCheckEquals(32, quadrupled.count, "a repeated self-meld count")
    actionCheckEquals(
        (0 until 8).flatMap { listOf(it, it, it, it) },
        drainPriorities(quadrupled),
        "a repeated self-meld duplicates again",
    )
    quadrupled.validateStructure()
    actionCheckEquals(8, basis.count, "the self-meld source is unchanged")

    val mixed = basis.transformAll(policy.atMost(2)).meld(basis)
    actionCheckEquals(
        listOf(0, 0, 1, 1) + List(7) { 2 } + listOf(3, 4, 5, 6, 7),
        drainPriorities(mixed),
        "melding a transformed version with its own source keeps both histories",
    )
    mixed.validateStructure()
}

private fun actionHeapDrainsAdversarialShapesInOrder() {
    val policy = OrderClampPolicy.natural<Int>()
    val count = 2_048
    val ascending = PersistentMonotoneActionHeap.from((0 until count).map { entry("e$it", it) }, policy)
    val descending = PersistentMonotoneActionHeap.from(
        (count - 1 downTo 0).map { entry("e$it", it) },
        policy,
    )
    val equal = PersistentMonotoneActionHeap.from((0 until count).map { entry("e$it", 7) }, policy)
    val evens = PersistentMonotoneActionHeap.from(
        (0 until count / 2).map { entry("v$it", it * 2) },
        policy,
    )
    val odds = PersistentMonotoneActionHeap.from(
        (0 until count / 2).map { entry("o$it", it * 2 + 1) },
        policy,
    )
    val melded = evens.meld(odds)
    val clamped = ascending.transformAll(policy.between(16, 1_024))

    for (heap in listOf(ascending, descending, equal, melded, clamped)) {
        val statistics = heap.validateStructure()
        actionCheckEquals(count, statistics.count, "the adversarial validated count")
        actionCheck(statistics.maximumRank <= 32, "the adversarial rank stays logarithmic")
        actionCheck(statistics.rootForestLength <= 34, "the adversarial root forest stays logarithmic")
    }

    actionCheckEquals((0 until count).toList(), drainPriorities(ascending), "the ascending drain")
    actionCheckEquals((0 until count).toList(), drainPriorities(descending), "the descending drain")
    actionCheckEquals(List(count) { 7 }, drainPriorities(equal), "the equal-priority drain")
    actionCheckEquals((0 until count).toList(), drainPriorities(melded), "the melded drain")
    actionCheckEquals(
        (0 until count).map { it.coerceIn(16, 1_024) },
        drainPriorities(clamped),
        "the clamped drain",
    )
}

private fun actionHeapRandomizedTransformHistoryMatchesModel() {
    val policy = OrderClampPolicy.natural<Int>()
    val random = Random(0x5ac_71_0fL)
    var heap = PersistentMonotoneActionHeap.empty<String, Int, OrderClamp<Int>>(policy)
    var model = ArrayList<Pair<String, Int>>()
    var nextId = 0
    val retained = ArrayList<Pair<PersistentMonotoneActionHeap<String, Int, OrderClamp<Int>>,
        List<Pair<String, Int>>>>()

    for (step in 0 until 3_000) {
        when (random.nextInt(10)) {
            0, 1, 2, 3 -> {
                val element = "e${nextId++}"
                val priority = random.nextInt(200) - 50
                heap = heap.insert(element, priority)
                model.add(element to priority)
            }

            4, 5 -> {
                val view = heap.minimumView()
                if (view == null) {
                    actionCheck(model.isEmpty(), "a null minimum view implies an empty model")
                } else {
                    actionCheckEquals(
                        model.minOf { it.second },
                        view.minimum.priority,
                        "the randomized minimum priority",
                    )
                    val index = model.indexOfFirst {
                        it.first == view.minimum.element && it.second == view.minimum.priority
                    }
                    actionCheck(index >= 0, "the deleted entry belongs to the model")
                    model.removeAt(index)
                    heap = view.remainder
                }
            }

            6, 7 -> {
                val action = randomClamp(policy, random)
                heap = heap.transformAll(action)
                model = ArrayList(model.map { it.first to policy.apply(action, it.second) })
            }

            8 -> {
                var other = PersistentMonotoneActionHeap.empty<String, Int, OrderClamp<Int>>(policy)
                val otherModel = ArrayList<Pair<String, Int>>()
                repeat(random.nextInt(5)) {
                    val element = "m${nextId++}"
                    val priority = random.nextInt(200) - 50
                    other = other.insert(element, priority)
                    otherModel.add(element to priority)
                }
                val action = randomClamp(policy, random)
                other = other.transformAll(action)
                heap = heap.meld(other)
                for (item in otherModel) {
                    model.add(item.first to policy.apply(action, item.second))
                }
            }

            else -> Unit
        }

        actionCheckEquals(model.size, heap.count, "the randomized count")
        actionCheckEquals(model.isEmpty(), heap.isEmpty, "the randomized emptiness")
        if (step % 64 == 0) {
            actionCheckEquals(model.size, heap.validateStructure().count, "the randomized audit")
            actionCheckEquals(sortedModel(model), sortedEntries(heap), "the randomized entry multiset")
            actionCheckEquals(
                if (model.isEmpty()) null else model.minOf { it.second },
                heap.minimumOrNull()?.priority,
                "the randomized minimum",
            )
        }
        if (step % 250 == 0 && retained.size < 12) {
            retained.add(heap to ArrayList(model))
        }
    }

    for ((version, versionModel) in retained) {
        actionCheckEquals(versionModel.size, version.validateStructure().count, "a retained audit")
        actionCheckEquals(sortedModel(versionModel), sortedEntries(version), "the retained entries")
        actionCheckEquals(
            sortedModel(versionModel).map { it.second },
            drainPriorities(version),
            "the retained drain order",
        )
    }
}

private fun actionHeapEmptyOperationsReturnNullAndStayUsable() {
    val policy = OrderClampPolicy.natural<Int>()
    val emptyHeap = PersistentMonotoneActionHeap.empty<String, Int, OrderClamp<Int>>(policy)

    actionCheck(emptyHeap.isEmpty, "a fresh heap is empty")
    actionCheckEquals(0, emptyHeap.count, "a fresh heap has no entries")
    actionCheckEquals(null, emptyHeap.minimumOrNull(), "an empty minimum is null")
    actionCheckEquals(null, emptyHeap.deleteMinimum(), "an empty deletion is null")
    actionCheckEquals(null, emptyHeap.minimumView(), "an empty view is null")
    actionCheckEquals(
        emptyList<MonotoneHeapEntry<String, Int>>(),
        emptyHeap.toList(),
        "an empty heap iterates nothing",
    )
    actionCheckEquals(
        PersistentMonotoneActionHeapStatistics(0, 0, 0, 0, 0),
        emptyHeap.validateStructure(),
        "the empty statistics",
    )
    actionCheck(emptyHeap.actionPolicy === policy, "an empty heap retains its policy")
    actionCheck(emptyHeap.comparator === policy.comparator, "an empty heap retains its comparator")
    actionCheck(
        PersistentMonotoneActionHeap.from(emptyList<MonotoneHeapEntry<String, Int>>(), policy).isEmpty,
        "bulk construction from nothing is empty",
    )

    val drained = PersistentMonotoneActionHeap.from(listOf(entry("a", 1)), policy).deleteMinimum()!!
    actionCheck(drained.isEmpty, "a fully drained heap is empty")
    actionCheckEquals(0, drained.count, "a fully drained heap has no entries")
    actionCheck(drained.actionPolicy === policy, "a drained heap keeps its policy usable")
    actionCheckEquals(null, drained.minimumOrNull(), "a drained heap has no minimum")
    actionCheckEquals(null, drained.deleteMinimum(), "a drained heap cannot delete again")
    drained.validateStructure()

    val refilled = drained.insert("b", 5).insert("c", 6).transformAll(policy.atMost(5))
    actionCheckEquals(2, refilled.count, "a drained heap accepts new entries")
    actionCheckEquals(listOf(5, 5), drainPriorities(refilled), "the refilled heap transforms normally")
}

private fun actionHeapTieBreakingIsExactAndDeterministic() {
    val policy = OrderClampPolicy.natural<Int>()

    val ties = PersistentMonotoneActionHeap.from(
        listOf(entry("a", 1), entry("b", 1), entry("c", 1)),
        policy,
    )
    actionCheckEquals("c", ties.minimumOrNull()!!.element, "an insert-root tie favours the new entry")
    actionCheckEquals(listOf("c", "b", "a"), drainElements(ties), "tied inserts drain newest-first")
    actionCheckEquals(
        drainElements(ties),
        drainElements(
            PersistentMonotoneActionHeap.from(
                listOf(entry("a", 1), entry("b", 1), entry("c", 1)),
                policy,
            ),
        ),
        "tie resolution is reproducible",
    )

    val ascending = PersistentMonotoneActionHeap.from(listOf(entry("a", 1), entry("b", 2)), policy)
    actionCheckEquals(
        "a",
        ascending.minimumOrNull()!!.element,
        "a strictly larger insert leaves the incumbent root in place",
    )

    // The forest minimum favours the earliest spine occurrence, which fixes which of three tied
    // rank-zero trees is promoted after the root leaves.
    val forestTies = PersistentMonotoneActionHeap.from(
        listOf(entry("root", 0), entry("x", 5), entry("y", 5), entry("z", 5)),
        policy,
    )
    actionCheckEquals(
        listOf("root", "y", "z", "x"),
        drainElements(forestTies),
        "the tied forest minimum is the earliest spine occurrence",
    )
    forestTies.validateStructure()

    // A tie that survives a transform resolves the same way, because tags never reorder equals.
    actionCheckEquals(
        listOf("c", "b", "a"),
        drainElements(ties.transformAll(policy.atLeast(100))),
        "a transform preserves tie resolution",
    )
}

private fun actionHeapValidateStructureAuditsTagsRanksAndCount() {
    val policy = OrderClampPolicy.natural<Int>()
    val basis = PersistentMonotoneActionHeap.from((0 until 100).map { entry("e$it", it) }, policy)

    val plain = basis.validateStructure()
    actionCheckEquals(100, plain.count, "the audited count")
    actionCheckEquals(0, plain.taggedComponentCount, "an untransformed heap has no pending tags")
    actionCheck(plain.maximumRank >= 1, "a hundred-entry heap has ranked trees")
    actionCheck(plain.rootForestLength >= 1, "a hundred-entry heap has a root forest")
    actionCheck(plain.maximumDepth >= 2, "a hundred-entry heap is deeper than its root")

    val tagged = basis.transformAll(policy.atMost(40))
    val taggedStatistics = tagged.validateStructure()
    actionCheckEquals(plain.count, taggedStatistics.count, "a transform preserves the audited count")
    actionCheckEquals(
        plain.rootForestLength,
        taggedStatistics.rootForestLength,
        "a transform preserves the root forest length",
    )
    actionCheckEquals(plain.maximumRank, taggedStatistics.maximumRank, "a transform preserves ranks")
    actionCheckEquals(plain.maximumDepth, taggedStatistics.maximumDepth, "a transform preserves depth")
    actionCheck(taggedStatistics.taggedComponentCount > 0, "a transformed heap reports pending tags")

    val expected = (0 until 100).map { minOf(it, 40) }.sorted()
    actionCheckEquals(expected, tagged.map { it.priority }.sorted(), "iteration composes every tag")
    actionCheckEquals(expected, drainPriorities(tagged), "draining composes every tag")

    var cursor = tagged
    var remaining = tagged.count
    while (!cursor.isEmpty) {
        actionCheckEquals(remaining, cursor.validateStructure().count, "the audit tracks the drain")
        cursor = cursor.deleteMinimum()!!
        remaining--
    }
    actionCheckEquals(0, remaining, "the drain consumed every entry")
    actionCheckEquals(
        PersistentMonotoneActionHeapStatistics(0, 0, 0, 0, 0),
        cursor.validateStructure(),
        "the drained audit reports nothing",
    )
}

private fun actionHeapNullablePayloadsAndPrioritiesAreUnambiguous() {
    val nullsFirst = Comparator<Int?> { left, right -> compareValues(left, right) }
    val policy = OrderClampPolicy(nullsFirst)
    val heap = PersistentMonotoneActionHeap.from(
        listOf(
            MonotoneHeapEntry<String?, Int?>("x", 3),
            MonotoneHeapEntry<String?, Int?>(null, 1),
            MonotoneHeapEntry<String?, Int?>("z", null),
        ),
        policy,
    )

    val minimum = heap.minimumOrNull()
    actionCheck(minimum != null, "a stored null priority still yields a present minimum")
    actionCheckEquals(null, minimum!!.priority, "the null priority sorts first")
    actionCheckEquals("z", minimum.element, "the null-priority payload")
    actionCheckEquals(
        listOf(null, 1, 3),
        heap.map { it.priority }.sortedWith(nullsFirst),
        "nullable priorities iterate",
    )
    actionCheckEquals(listOf("z", null, "x"), drainElements(heap), "nullable payloads drain in order")

    // A null bound is an ordinary bound value rather than an absence sentinel.
    val flattened = heap.transformAll(policy.atMost(null))
    actionCheckEquals(listOf(null, null, null), flattened.map { it.priority }, "a null cap is a bound")
    actionCheckEquals(3, flattened.count, "a null cap preserves the count")
    flattened.validateStructure()
    actionCheckEquals(listOf(2, 2, 3), drainPriorities(heap.transformAll(policy.atLeast(2))),
        "a floor lifts a null priority")

    val view = heap.minimumView()
    actionCheck(view != null, "a heap of nullable entries yields a present view")
    actionCheckEquals(2, view!!.remainder.count, "the nullable remainder count")
    actionCheckEquals(null, view.minimum.priority, "the viewed null priority")
}

private fun actionHeapPolicyGatesAndClampBoundsAreEnforced() {
    val policy = OrderClampPolicy.natural<Int>()
    val rival = OrderClampPolicy.natural<Int>()
    val left = PersistentMonotoneActionHeap.from(listOf(entry("a", 1)), policy)

    actionCheckThrows<IllegalArgumentException>("melding across distinct policies") {
        left.meld(PersistentMonotoneActionHeap.from(listOf(entry("b", 2)), rival))
    }
    actionCheckEquals(
        2,
        left.meld(PersistentMonotoneActionHeap.from(listOf(entry("b", 2)), policy)).count,
        "the same policy object melds",
    )

    val sharedComparator = Comparator<Int> { left1, right -> left1.compareTo(right) }
    val first = ValueEqualClampPolicy(sharedComparator)
    val second = ValueEqualClampPolicy(sharedComparator)
    actionCheck(first !== second && first == second, "the two policies are distinct but value-equal")
    val melded = PersistentMonotoneActionHeap.from(listOf(entry("a", 1)), first)
        .meld(PersistentMonotoneActionHeap.from(listOf(entry("b", 2)), second))
    actionCheckEquals(2, melded.count, "value-equal policies meld")
    actionCheckEquals(listOf(1, 2), drainPriorities(melded), "the value-equal meld drains in order")

    actionCheckThrows<IllegalArgumentException>("inverted clamp bounds") { policy.between(9, 2) }
    actionCheckEquals(
        policy.between(2, 2),
        policy.between(2, 2),
        "degenerate clamp bounds are accepted and value-equal",
    )
    actionCheckThrows<IllegalStateException>("the constant of a bounded clamp") {
        policy.atLeast(1).constantValue
    }
    actionCheckThrows<IllegalStateException>("the lower bound of a cap") { policy.atMost(1).lowerBound }
    actionCheckThrows<IllegalStateException>("the upper bound of a floor") {
        policy.atLeast(1).upperBound
    }
    actionCheckThrows<IllegalStateException>("a bound of the identity") { policy.identity.lowerBound }
    actionCheckThrows<IllegalStateException>("a bound of a constant") { policy.constant(1).upperBound }

    actionCheck(policy.atLeast(3) != policy.atMost(3), "a floor and a cap are distinct actions")
    actionCheck(policy.constant(3) != policy.between(3, 3), "a constant and an inclusive clamp differ")
    actionCheckEquals(
        policy.atLeast(3).hashCode(),
        policy.atLeast(3).hashCode(),
        "equal clamps hash alike",
    )
}

private fun actionHeapConcurrentReadersSeeStableSnapshots() {
    val policy = OrderClampPolicy.natural<Int>()
    val basis = PersistentMonotoneActionHeap.from((0 until 512).map { entry("e$it", it) }, policy)
    val transformed = basis.transformAll(policy.between(64, 256))
    val expectedBasis = (0 until 512).toList()
    val expectedTransformed = (0 until 512).map { it.coerceIn(64, 256) }
    val failures = Collections.synchronizedList(ArrayList<Throwable>())

    val threads = List(8) { worker ->
        thread(start = false, name = "action-heap-reader-$worker") {
            try {
                repeat(4) {
                    actionCheckEquals(expectedBasis, drainPriorities(basis), "a concurrent basis drain")
                    actionCheckEquals(
                        expectedTransformed,
                        drainPriorities(transformed),
                        "a concurrent transformed drain",
                    )
                    transformed.validateStructure()
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }
    }
    threads.forEach(Thread::start)
    threads.forEach(Thread::join)
    if (failures.isNotEmpty()) {
        throw AssertionError(
            "concurrent action-heap readers had ${failures.size} failure(s)",
            failures.first(),
        )
    }
}

private fun entry(element: String, priority: Int): MonotoneHeapEntry<String, Int> =
    MonotoneHeapEntry(element, priority)

private fun randomClamp(policy: OrderClampPolicy<Int>, random: Random): OrderClamp<Int> =
    when (random.nextInt(5)) {
        0 -> policy.identity
        1 -> policy.atLeast(random.nextInt(200) - 50)
        2 -> policy.atMost(random.nextInt(200) - 50)
        3 -> policy.constant(random.nextInt(200) - 50)
        else -> {
            val first = random.nextInt(200) - 50
            val second = random.nextInt(200) - 50
            policy.between(minOf(first, second), maxOf(first, second))
        }
    }

private fun <E, P, A> drainViews(
    initial: PersistentMonotoneActionHeap<E, P, A>,
): List<MonotoneHeapEntry<E, P>> {
    var heap = initial
    val result = ArrayList<MonotoneHeapEntry<E, P>>(heap.count)
    while (!heap.isEmpty) {
        val view = heap.minimumView()
            ?: throw AssertionError("a nonempty action heap had no minimum view")
        result.add(view.minimum)
        heap = view.remainder
        if (result.size and 255 == 0) {
            heap.validateStructure()
        }
    }
    actionCheckEquals(null, heap.minimumView(), "a drained action heap has no minimum view")
    actionCheckEquals(initial.count, result.size, "a drain yields every entry exactly once")
    return result
}

private fun <E, P, A> drainPriorities(heap: PersistentMonotoneActionHeap<E, P, A>): List<P> =
    drainViews(heap).map { it.priority }

private fun <E, P, A> drainElements(heap: PersistentMonotoneActionHeap<E, P, A>): List<E> =
    drainViews(heap).map { it.element }

private fun <A> sortedEntries(
    heap: PersistentMonotoneActionHeap<String, Int, A>,
): List<Pair<String, Int>> =
    sortedModel(heap.map { it.element to it.priority })

private fun sortedModel(model: List<Pair<String, Int>>): List<Pair<String, Int>> =
    model.sortedWith(compareBy<Pair<String, Int>>({ it.second }, { it.first }))

private inline fun <reified T : Throwable> actionCheckThrows(message: String, action: () -> Unit) {
    try {
        action()
    } catch (error: Throwable) {
        if (error is T) {
            return
        }
        throw AssertionError(
            "$message expected ${T::class.simpleName}, got ${error::class.simpleName}",
            error,
        )
    }
    throw AssertionError("$message expected ${T::class.simpleName}")
}

private fun <T> actionCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message expected <$expected>, actual <$actual>")
    }
}

private fun actionCheck(condition: Boolean, message: String) {
    if (!condition) {
        throw AssertionError(message)
    }
}
