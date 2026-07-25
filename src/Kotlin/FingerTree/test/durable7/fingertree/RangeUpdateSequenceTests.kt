package durable7.fingertree

import java.util.Collections
import java.util.Random

private data class RangeAffineTag(
    val alternateIdentity: Boolean = false,
    val assignment: Long? = null,
    val addition: Long = 0L,
)

private object RangeAffineAlgebra : RangeUpdateAlgebra<Long, Long, RangeAffineTag> {
    override val empty: Long = 0L
    override val identityTag: RangeAffineTag = RangeAffineTag()

    override fun combine(left: Long, right: Long): Long = Math.addExact(left, right)

    override fun measure(element: Long): Long = element

    override fun isIdentity(tag: RangeAffineTag): Boolean =
        tag.assignment == null && tag.addition == 0L

    override fun compose(newer: RangeAffineTag, older: RangeAffineTag): RangeAffineTag {
        if (isIdentity(newer)) {
            return older
        }
        if (isIdentity(older)) {
            return newer
        }
        if (newer.assignment != null) {
            return newer
        }
        return if (older.assignment != null) {
            RangeAffineTag(
                assignment = Math.addExact(
                    Math.addExact(older.assignment, older.addition),
                    newer.addition,
                ),
            )
        } else {
            addRangeTag(Math.addExact(older.addition, newer.addition))
        }
    }

    override fun applyElement(tag: RangeAffineTag, element: Long): Long =
        Math.addExact(tag.assignment ?: element, tag.addition)

    override fun applyMeasure(tag: RangeAffineTag, measure: Long, count: Int): Long {
        if (count == 0) {
            return empty
        }
        return if (tag.assignment != null) {
            Math.multiplyExact(Math.addExact(tag.assignment, tag.addition), count.toLong())
        } else {
            Math.addExact(measure, Math.multiplyExact(tag.addition, count.toLong()))
        }
    }
}

private fun addRangeTag(amount: Long): RangeAffineTag = RangeAffineTag(addition = amount)

private fun assignRangeTag(value: Long): RangeAffineTag = RangeAffineTag(assignment = value)

private fun rangeUpdateAlgebraLawsHold() {
    val identities = listOf(RangeAffineAlgebra.identityTag, RangeAffineTag(alternateIdentity = true))
    val tags = identities + listOf(
        addRangeTag(5),
        addRangeTag(-3),
        assignRangeTag(7),
        RangeAffineTag(assignment = 4, addition = 9),
    )
    val values = listOf(-5L, 0L, 11L)
    rangeCheck(RangeAffineAlgebra.isIdentity(identities[1]), "value-distinct identity")

    for (tag in tags) {
        for (value in values) {
            rangeCheckEquals(
                RangeAffineAlgebra.applyElement(tag, value),
                RangeAffineAlgebra.applyElement(
                    RangeAffineAlgebra.compose(RangeAffineAlgebra.identityTag, tag),
                    value,
                ),
                "left tag identity",
            )
            rangeCheckEquals(
                RangeAffineAlgebra.applyElement(tag, value),
                RangeAffineAlgebra.applyElement(
                    RangeAffineAlgebra.compose(tag, identities[1]),
                    value,
                ),
                "right tag identity",
            )
        }
        for (newer in tags) {
            for (value in values) {
                rangeCheckEquals(
                    RangeAffineAlgebra.applyElement(newer, RangeAffineAlgebra.applyElement(tag, value)),
                    RangeAffineAlgebra.applyElement(RangeAffineAlgebra.compose(newer, tag), value),
                    "element composition",
                )
            }
        }
    }
    for (oldest in tags) {
        for (middle in tags) {
            for (newest in tags) {
                for (value in values) {
                    rangeCheckEquals(
                        RangeAffineAlgebra.applyElement(
                            RangeAffineAlgebra.compose(newest, RangeAffineAlgebra.compose(middle, oldest)),
                            value,
                        ),
                        RangeAffineAlgebra.applyElement(
                            RangeAffineAlgebra.compose(RangeAffineAlgebra.compose(newest, middle), oldest),
                            value,
                        ),
                        "tag associativity by action",
                    )
                }
            }
        }
    }
    val measureSamples = listOf(-9L, 0L, 12L)
    for (first in measureSamples) {
        rangeCheckEquals(first, RangeAffineAlgebra.combine(RangeAffineAlgebra.empty, first), "left measure identity")
        rangeCheckEquals(first, RangeAffineAlgebra.combine(first, RangeAffineAlgebra.empty), "right measure identity")
        for (second in measureSamples) {
            for (third in measureSamples) {
                rangeCheckEquals(
                    RangeAffineAlgebra.combine(first, RangeAffineAlgebra.combine(second, third)),
                    RangeAffineAlgebra.combine(RangeAffineAlgebra.combine(first, second), third),
                    "measure associativity",
                )
            }
        }
    }
    for (tag in tags) {
        rangeCheckEquals(
            RangeAffineAlgebra.empty,
            RangeAffineAlgebra.applyMeasure(tag, RangeAffineAlgebra.empty, 0),
            "tag action on empty measure",
        )
        for (value in values) {
            rangeCheckEquals(
                RangeAffineAlgebra.measure(RangeAffineAlgebra.applyElement(tag, value)),
                RangeAffineAlgebra.applyMeasure(tag, RangeAffineAlgebra.measure(value), 1),
                "singleton element/measure agreement",
            )
        }
        val leftMeasure = 5L
        val rightMeasure = 19L
        rangeCheckEquals(
            RangeAffineAlgebra.combine(
                RangeAffineAlgebra.applyMeasure(tag, leftMeasure, 2),
                RangeAffineAlgebra.applyMeasure(tag, rightMeasure, 3),
            ),
            RangeAffineAlgebra.applyMeasure(
                tag,
                RangeAffineAlgebra.combine(leftMeasure, rightMeasure),
                5,
            ),
            "measure action distributes over combination",
        )
        for (newer in tags) {
            val composed = RangeAffineAlgebra.compose(newer, tag)
            rangeCheckEquals(
                RangeAffineAlgebra.applyMeasure(
                    newer,
                    RangeAffineAlgebra.applyMeasure(tag, 17L, 4),
                    4,
                ),
                RangeAffineAlgebra.applyMeasure(composed, 17L, 4),
                "measure action composition",
            )
        }
    }
    rangeCheckEquals(
        7L,
        RangeAffineAlgebra.applyElement(
            RangeAffineAlgebra.compose(assignRangeTag(7), addRangeTag(10)),
            3,
        ),
        "assignment after addition wins",
    )
    rangeCheckEquals(
        17L,
        RangeAffineAlgebra.applyElement(
            RangeAffineAlgebra.compose(addRangeTag(10), assignRangeTag(7)),
            3,
        ),
        "addition after assignment accumulates",
    )
}

private fun rangeUpdateSurfacePreservesSnapshots() {
    val original = RangeUpdateSequence.from((1L..8L).toList(), RangeAffineAlgebra)
    rangeCheckEquals(8, original.size, "base size")
    rangeCheckEquals(36L, original.measure, "base measure")
    rangeCheckEquals(4L, original[3], "base index")
    rangeCheckEquals(null, original[-1], "negative index")
    rangeCheck(RangeUpdateSequence.from(original, RangeAffineAlgebra) === original, "same-lineage factory identity")

    val added = original.applyRange(2, 4, addRangeTag(10)) ?: error("valid addition")
    rangeAssertSequence(listOf(1, 2, 13, 14, 15, 16, 7, 8), added, "range addition")
    val assigned = added.applyRange(3, 3, assignRangeTag(7)) ?: error("valid assignment")
    rangeAssertSequence(listOf(1, 2, 13, 7, 7, 7, 7, 8), assigned, "range assignment")
    rangeCheckEquals(34L, assigned.measureRange(2, 4), "proper range measure")
    rangeCheckEquals(0L, assigned.measureRange(8, 0), "empty range measure")
    rangeCheckEquals((1L..8L).toList(), original.toList(), "original snapshot")

    val fullyTagged = original.applyRange(0, 8, addRangeTag(5)) ?: error("whole update")
    rangeAssertSequence(
        listOf(6, 7, 8, 9, 10, 11, 12, 13),
        fullyTagged,
        "whole update",
    )
    rangeAssertSequence(
        listOf(6, 7, 8, 100, 10, 11, 12, 13),
        fullyTagged.setItem(3, 100) ?: error("valid replacement"),
        "replacement is not retroactively tagged",
    )
    rangeAssertSequence(
        listOf(6, 7, 50, 8, 9, 10, 11, 12, 13),
        fullyTagged.insertAt(2, 50) ?: error("valid insertion"),
        "insertion is not retroactively tagged",
    )
    rangeAssertSequence(
        listOf(6, 8, 9, 10, 11, 12, 13),
        fullyTagged.removeAt(1) ?: error("valid removal"),
        "removal after lazy update",
    )

    val split = assigned.splitAt(3) ?: error("valid split")
    rangeAssertSequence(listOf(1, 2, 13), split.left, "split left")
    rangeAssertSequence(listOf(7, 7, 7, 7, 8), split.right, "split right")
    rangeAssertSequence(assigned.toList(), split.left.concat(split.right), "split round trip")
    rangeAssertSequence(
        listOf(13, 7, 7, 7),
        assigned.getRange(2, 4) ?: error("valid range"),
        "range extraction",
    )

    rangeCheckEquals(null, original.insertAt(9, 0), "invalid insertion")
    rangeCheckEquals(null, original.setItem(8, 0), "invalid replacement")
    rangeCheckEquals(null, original.removeAt(-1), "invalid removal")
    rangeCheckEquals(null, original.splitAt(9), "invalid split")
    rangeCheckEquals(null, original.getRange(7, 2), "invalid range")
    rangeCheckEquals(null, original.applyRange(1, Int.MAX_VALUE, addRangeTag(1)), "overflow-safe range rejection")
}

private data class RangeTrace(val values: List<Long>)

private fun rangeUpdateMeasureOrderIsPreserved() {
    val algebra = object : RangeUpdateAlgebra<Long, RangeTrace, Long> {
        override val empty: RangeTrace = RangeTrace(emptyList())
        override val identityTag: Long = 0L
        override fun combine(left: RangeTrace, right: RangeTrace): RangeTrace =
            RangeTrace(left.values + right.values)
        override fun measure(element: Long): RangeTrace = RangeTrace(listOf(element))
        override fun isIdentity(tag: Long): Boolean = tag == 0L
        override fun compose(newer: Long, older: Long): Long = newer + older
        override fun applyElement(tag: Long, element: Long): Long = tag + element
        override fun applyMeasure(tag: Long, measure: RangeTrace, count: Int): RangeTrace =
            RangeTrace(measure.values.map { it + tag })
    }
    val original = RangeUpdateSequence.from(listOf(1L, 2L, 3L, 4L, 5L), algebra)
    val changed = original.applyRange(1, 3, 10) ?: error("trace update")
    rangeCheckEquals(RangeTrace(listOf(1, 12, 13, 14, 5)), changed.measure, "ordered whole measure")
    rangeCheckEquals(RangeTrace(listOf(12, 13, 14)), changed.measureRange(1, 3), "ordered range measure")

    val empty = RangeTrace(emptyList())
    val first = RangeTrace(listOf(1, 2))
    val second = RangeTrace(listOf(3))
    val third = RangeTrace(listOf(4, 5))
    rangeCheckEquals(first, algebra.combine(empty, first), "trace left identity")
    rangeCheckEquals(first, algebra.combine(first, empty), "trace right identity")
    rangeCheckEquals(
        algebra.combine(first, algebra.combine(second, third)),
        algebra.combine(algebra.combine(first, second), third),
        "trace associativity preserves order",
    )
    rangeCheckEquals(empty, algebra.applyMeasure(10, empty, 0), "trace empty action")
    rangeCheckEquals(
        algebra.measure(algebra.applyElement(10, 7)),
        algebra.applyMeasure(10, algebra.measure(7), 1),
        "trace singleton agreement",
    )
    rangeCheckEquals(
        algebra.combine(algebra.applyMeasure(10, first, 2), algebra.applyMeasure(10, third, 2)),
        algebra.applyMeasure(10, algebra.combine(first, third), 4),
        "trace action distributes without reversing operands",
    )
}

private fun rangeUpdateGeneratedHistoryMatchesList() {
    val random = Random(0x52414e4745555044L)
    val initialValues = (0L until 16L).toList()
    val original = RangeUpdateSequence.from(initialValues, RangeAffineAlgebra)
    var sequence = original
    val model = initialValues.toMutableList()
    val snapshots = mutableListOf(sequence to model.toList())
    var branchCount = 0

    repeat(1_000) { step ->
        if (step > 0 && step % 17 == 0) {
            val (oldSequence, oldModel) = snapshots[random.nextInt(snapshots.size)]
            sequence = oldSequence
            model.clear()
            model.addAll(oldModel)
            branchCount++
        }
        val boundary = random.nextInt(model.size + 1)
        val position = if (model.isEmpty()) 0 else random.nextInt(model.size)
        val start = random.nextInt(model.size + 1)
        val count = random.nextInt(model.size - start + 1)
        when (step % 8) {
            0 -> if (model.size < 64) {
                val value = random.nextInt(2_001).toLong() - 1_000L
                sequence = sequence.insertAt(boundary, value) ?: error("model insert")
                model.add(boundary, value)
            }
            1 -> if (model.isNotEmpty()) {
                sequence = sequence.removeAt(position) ?: error("model remove")
                model.removeAt(position)
            }
            2 -> if (model.isNotEmpty()) {
                val value = 10_000L + step
                sequence = sequence.setItem(position, value) ?: error("model set")
                model[position] = value
            }
            3 -> {
                val tag = addRangeTag(random.nextInt(21).toLong() - 10L)
                sequence = sequence.applyRange(start, count, tag) ?: error("model add")
                for (index in start until start + count) {
                    model[index] = RangeAffineAlgebra.applyElement(tag, model[index])
                }
            }
            4 -> {
                val tag = assignRangeTag(random.nextInt(31).toLong() - 15L)
                sequence = sequence.applyRange(start, count, tag) ?: error("model assign")
                for (index in start until start + count) {
                    model[index] = RangeAffineAlgebra.applyElement(tag, model[index])
                }
            }
            5 -> rangeCheckEquals(model.subList(start, start + count).sum(), sequence.measureRange(start, count), "model query")
            6 -> {
                val split = sequence.splitAt(boundary) ?: error("model split")
                sequence = split.left.concat(split.right)
            }
            else -> {
                val extracted = sequence.getRange(start, count) ?: error("model range")
                rangeCheckEquals(model.subList(start, start + count), extracted.toList(), "model range contents")
            }
        }
        rangeAssertSequence(model, sequence, "generated step $step")
        if (step % 31 == 0) {
            snapshots.add(sequence to model.toList())
        }
    }

    rangeCheck(branchCount > 0, "generated history branches from retained versions")
    rangeAssertSequence(initialValues, original, "retained original")
    for ((snapshot, expected) in snapshots) {
        rangeAssertSequence(expected, snapshot, "retained generated snapshot")
    }
}

private fun rangeUpdateIdentityAndSharingContractsHold() {
    val sequence = RangeUpdateSequence.from((1L..32L).toList(), RangeAffineAlgebra)
    val empty = RangeUpdateSequence.empty<Long, Long, RangeAffineTag>(RangeAffineAlgebra)
    val identity = sequence.applyRange(3, 10, RangeAffineTag(alternateIdentity = true)) ?: error("identity")
    val emptyUpdate = sequence.applyRange(7, 0, addRangeTag(99)) ?: error("empty update")
    val splitRight = sequence.splitAt(0)?.right ?: error("endpoint split")
    rangeCheck(sequence === identity, "identity update retains facade")
    rangeCheck(sequence === emptyUpdate, "empty update retains facade")
    rangeCheck(sequence === splitRight, "endpoint split retains facade")
    rangeCheck(sequence === sequence.concat(empty), "right-empty concat retains facade")
    rangeCheck(sequence === empty.concat(sequence), "left-empty concat retains facade")

    val full = sequence.applyRange(0, sequence.size, addRangeTag(1)) ?: error("full update")
    rangeCheck(!sequence.sharesRootWith(full), "nonidentity full update replaces root")
    rangeCheck(sequence.sharesStructureWith(full), "nonidentity full update retains child structure")
    val statistics = full.validateStructure() ?: error("full update validation")
    rangeCheck(statistics.pendingTagCount > 0, "full update stores pending tag")
}

private fun rangeUpdateValidationPrecedesCallbacks() {
    var identityCalls = 0
    val hostile = object : RangeUpdateAlgebra<Long, Long, RangeAffineTag> by RangeAffineAlgebra {
        override fun isIdentity(tag: RangeAffineTag): Boolean {
            identityCalls++
            throw AssertionError("identity predicate should not run")
        }
    }
    val sequence = RangeUpdateSequence.from(listOf(1L, 2L, 3L), hostile)
    val empty = sequence.applyRange(1, 0, RangeAffineTag(addition = 9)) ?: error("empty update")
    rangeCheck(sequence === empty, "empty range bypasses identity policy")
    rangeCheckEquals(null, sequence.applyRange(4, 0, RangeAffineTag()), "invalid range returns null")
    rangeCheckEquals(0, identityCalls, "validation and empty update precede identity callback")
}

private fun rangeUpdateCallbackFailuresLeaveSourcesUsable() {
    var fail = false
    val throwing = object : RangeUpdateAlgebra<Long, Long, RangeAffineTag> by RangeAffineAlgebra {
        override fun applyElement(tag: RangeAffineTag, element: Long): Long {
            if (fail) {
                throw RangePolicyFailure()
            }
            return RangeAffineAlgebra.applyElement(tag, element)
        }
    }
    val source = RangeUpdateSequence.from((1L..20L).toList(), throwing)
    fail = true
    rangeCheckThrows<RangePolicyFailure>("throwing range update") {
        source.applyRange(0, source.size, addRangeTag(1))
    }
    fail = false
    rangeAssertSequence((1L..20L).toList(), source, "source after callback failure")
    rangeAssertSequence(
        (2L..21L).toList(),
        source.applyRange(0, source.size, addRangeTag(1)) ?: error("retry update"),
        "retry after callback failure",
    )
}

private enum class RangeCallbackKind {
    MEASURE,
    COMBINE,
    IS_IDENTITY,
    COMPOSE,
    APPLY_ELEMENT,
    APPLY_MEASURE,
}

private class RangeFailpointAlgebra : RangeUpdateAlgebra<Long, Long, RangeAffineTag> {
    private var selected: RangeCallbackKind? = null
    private var failureOrdinal: Int = Int.MAX_VALUE
    var selectedCalls: Int = 0
        private set

    override val empty: Long
        get() = RangeAffineAlgebra.empty
    override val identityTag: RangeAffineTag
        get() = RangeAffineAlgebra.identityTag

    fun arm(kind: RangeCallbackKind, ordinal: Int) {
        selected = kind
        failureOrdinal = ordinal
        selectedCalls = 0
    }

    fun disarm() {
        selected = null
        failureOrdinal = Int.MAX_VALUE
        selectedCalls = 0
    }

    private fun hit(kind: RangeCallbackKind) {
        if (selected == kind) {
            selectedCalls++
            if (selectedCalls == failureOrdinal) {
                throw RangePolicyFailure()
            }
        }
    }

    override fun measure(element: Long): Long {
        hit(RangeCallbackKind.MEASURE)
        return RangeAffineAlgebra.measure(element)
    }

    override fun combine(left: Long, right: Long): Long {
        hit(RangeCallbackKind.COMBINE)
        return RangeAffineAlgebra.combine(left, right)
    }

    override fun isIdentity(tag: RangeAffineTag): Boolean {
        hit(RangeCallbackKind.IS_IDENTITY)
        return RangeAffineAlgebra.isIdentity(tag)
    }

    override fun compose(newer: RangeAffineTag, older: RangeAffineTag): RangeAffineTag {
        hit(RangeCallbackKind.COMPOSE)
        return RangeAffineAlgebra.compose(newer, older)
    }

    override fun applyElement(tag: RangeAffineTag, element: Long): Long {
        hit(RangeCallbackKind.APPLY_ELEMENT)
        return RangeAffineAlgebra.applyElement(tag, element)
    }

    override fun applyMeasure(tag: RangeAffineTag, measure: Long, count: Int): Long {
        hit(RangeCallbackKind.APPLY_MEASURE)
        return RangeAffineAlgebra.applyMeasure(tag, measure, count)
    }
}

private fun rangeUpdateEveryPolicyFailureIsAtomicAndRetryable() {
    val algebra = RangeFailpointAlgebra()
    val baseValues = (0L until 32L).toList()
    val base = RangeUpdateSequence.from(baseValues, algebra)
    val once = base.applyRange(0, base.size, addRangeTag(1)) ?: error("first nested tag")
    val shaped = once.setItem(24, once[24] ?: error("tagged point")) ?: error("shape edit")
    val nested = shaped.applyRange(0, shaped.size, addRangeTag(2)) ?: error("second nested tag")
    val nestedValues = baseValues.map { it + 3 }
    val expected = nestedValues.mapIndexed { index, value -> if (index in 5 until 25) value + 3 else value }
    fun operation(): RangeUpdateSequence<Long, Long, RangeAffineTag> =
        nested.applyRange(5, 20, addRangeTag(3)) ?: error("valid failpoint operation")

    for (kind in RangeCallbackKind.entries) {
        algebra.arm(kind, Int.MAX_VALUE)
        operation()
        val reachableCount = algebra.selectedCalls
        algebra.disarm()
        rangeCheck(reachableCount > 0, "$kind has a reachable callback")

        for (ordinal in 1..reachableCount) {
            algebra.arm(kind, ordinal)
            rangeCheckThrows<RangePolicyFailure>("$kind callback ordinal $ordinal") { operation() }
            algebra.disarm()
            rangeAssertSequence(baseValues, base, "$kind failure retains base")
            rangeAssertSequence(nestedValues, nested, "$kind failure retains nested source")
            rangeAssertSequence(expected, operation(), "$kind retry after ordinal $ordinal")
        }
    }
}

private fun rangeUpdateIteratorRetriesPolicyFailuresAtTheSameElement() {
    val algebra = RangeFailpointAlgebra()
    val base = RangeUpdateSequence.from((0L until 32L).toList(), algebra)
    val once = base.applyRange(0, base.size, addRangeTag(1)) ?: error("iterator first tag")
    val shaped = once.setItem(24, once[24] ?: error("iterator tagged point")) ?: error("iterator shape edit")
    val nested = shaped.applyRange(0, shaped.size, addRangeTag(2)) ?: error("iterator second tag")
    val expected = (0L until 32L).map { it + 3 }

    val composeIterator = nested.iterator()
    algebra.arm(RangeCallbackKind.COMPOSE, 1)
    rangeCheckThrows<RangePolicyFailure>("iterator child composition failure") { composeIterator.hasNext() }
    algebra.disarm()
    rangeCheckEquals(expected, composeIterator.asSequence().toList(), "iterator retries child composition")

    val elementIterator = nested.iterator()
    algebra.arm(RangeCallbackKind.APPLY_ELEMENT, 1)
    rangeCheckThrows<RangePolicyFailure>("iterator element action failure") { elementIterator.hasNext() }
    algebra.disarm()
    rangeCheckEquals(expected, elementIterator.asSequence().toList(), "iterator retries current element")
}

private class RangePolicyFailure : RuntimeException()

private fun rangeUpdateConstructionIsOneShotAndOrdered() {
    var enumerationComplete = false
    var emptyCalls = 0
    var measureCalls = 0
    val source = object : Iterable<Long> {
        override fun iterator(): Iterator<Long> = object : Iterator<Long> {
            private var next = 0L
            override fun hasNext(): Boolean {
                val hasNext = next < 8L
                if (!hasNext) {
                    enumerationComplete = true
                }
                return hasNext
            }
            override fun next(): Long = next++
        }
    }
    val algebra = object : RangeUpdateAlgebra<Long, Long, Unit> {
        override val empty: Long
            get() {
                rangeCheck(enumerationComplete, "enumeration completes before empty-measure capture")
                emptyCalls++
                return 0L
            }
        override val identityTag: Unit = Unit
        override fun combine(left: Long, right: Long): Long = left + right
        override fun measure(element: Long): Long {
            rangeCheck(enumerationComplete, "enumeration completes before measurement")
            measureCalls++
            return element
        }
        override fun isIdentity(tag: Unit): Boolean = true
        override fun compose(newer: Unit, older: Unit): Unit = Unit
        override fun applyElement(tag: Unit, element: Long): Long = element
        override fun applyMeasure(tag: Unit, measure: Long, count: Int): Long = measure
    }
    val sequence = RangeUpdateSequence.from(source, algebra)
    rangeCheckEquals((0L until 8L).toList(), sequence.toList(), "one-shot construction contents")
    rangeCheckEquals(1, emptyCalls, "empty measure captured once per construction lineage")
    rangeCheckEquals(8, measureCalls, "one measure callback per constructed element")
}

private fun rangeUpdateNullableElementsMeasuresAndTagsRemainValues() {
    val algebra = object : RangeUpdateAlgebra<String?, String?, String?> {
        override val empty: String? = "empty"
        override val identityTag: String? = "identity"

        override fun combine(left: String?, right: String?): String? =
            if (left == null || right == null) null else left + right

        override fun measure(element: String?): String? = element

        override fun isIdentity(tag: String?): Boolean = tag == identityTag

        override fun compose(newer: String?, older: String?): String? = when {
            isIdentity(newer) -> older
            isIdentity(older) -> newer
            newer == null -> null
            older == null -> newer
            else -> newer + older
        }

        override fun applyElement(tag: String?, element: String?): String? =
            if (tag == null) null else element

        override fun applyMeasure(tag: String?, measure: String?, count: Int): String? =
            if (count == 0) empty else if (tag == null) null else measure
    }

    val source = RangeUpdateSequence.from<String?, String?, String?>(listOf("value"), algebra)
    val changed = source.applyRange(0, 1, null) ?: error("nullable tag update")
    rangeCheckEquals(listOf<String?>(null), changed.toList(), "nullable element after nullable tag")
    rangeCheckEquals(null, changed.measure, "nullable cached measure is not replaced by empty")
    rangeCheckEquals(null, changed.measureRange(0, 1), "nullable range measure")
    rangeCheckEquals(null, changed[0], "nullable indexed element")
    rangeCheck(changed.validateStructure() != null, "nullable structure validates")
    rangeCheck(source.toList() == listOf("value"), "nullable update retains source")
    val identity = changed.applyRange(0, 1, "identity") ?: error("nullable identity update")
    rangeCheck(identity === changed, "nullable lineage identity retains facade")
}

private fun rangeUpdateCheckedMaximumCountIsAtomic() {
    val seed = RangeUpdateSequence.from(listOf(7L), RangeAffineAlgebra)
    var power = seed
    while (power.size <= Int.MAX_VALUE / 2) {
        power = power.concat(power)
    }
    val almostPower = power.removeAt(0) ?: error("maximum fixture removal")
    val maximum = power.concat(almostPower)
    rangeCheckEquals(Int.MAX_VALUE, maximum.size, "maximum shared-DAG count")
    rangeCheckEquals(7L, maximum[0], "maximum front")
    rangeCheckEquals(7L, maximum[Int.MAX_VALUE - 1], "maximum back")
    rangeCheckThrows<ArithmeticException>("maximum insertion") { maximum.append(8) }
    rangeCheckThrows<ArithmeticException>("maximum concatenation") { maximum.concat(seed) }
    rangeCheckEquals(Int.MAX_VALUE, maximum.size, "maximum source remains usable")
}

private fun rangeUpdateConcurrentReadersSeeSnapshots() {
    val original = RangeUpdateSequence.from((0L until 256L).toList(), RangeAffineAlgebra)
    val changed = original.applyRange(64, 128, addRangeTag(1_000)) ?: error("concurrent fixture")
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val threads = (0 until 4).map { worker ->
        Thread {
            try {
                repeat(64) {
                    rangeCheckEquals((0L until 256L).sum(), original.measure, "concurrent original measure $worker")
                    rangeCheckEquals(0L, original[0], "concurrent original front $worker")
                    rangeCheckEquals(1_128L, changed[128], "concurrent changed point $worker")
                    rangeCheckEquals((64L until 192L).sum() + 128_000L, changed.measureRange(64, 128), "concurrent changed range $worker")
                    rangeCheckEquals(256, changed.toList().size, "concurrent enumeration $worker")
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }
    }
    threads.forEach { it.start() }
    threads.forEach { it.join() }
    if (failures.isNotEmpty()) {
        throw AssertionError("range-update concurrent reader failure", failures.first())
    }
}

private fun rangeAssertSequence(
    expected: List<Long>,
    actual: RangeUpdateSequence<Long, Long, RangeAffineTag>,
    message: String,
) {
    rangeCheckEquals(expected, actual.toList(), "$message contents")
    rangeCheckEquals(expected, actual.iterator().asSequence().toList(), "$message iterator")
    rangeCheckEquals(expected.size, actual.size, "$message size")
    rangeCheckEquals(expected.sum(), actual.measure, "$message measure")
    val statistics = actual.validateStructure() ?: throw AssertionError("$message structural validation")
    rangeCheckEquals(expected.size, statistics.count, "$message validated count")
    rangeCheck(statistics.height <= rangeAvlHeightCeiling(expected.size), "$message AVL height")
}

private fun rangeAvlHeightCeiling(count: Int): Int {
    if (count == 0) {
        return 0
    }
    var remaining = count + 1
    var logarithm = 0
    while (remaining > 1) {
        remaining /= 2
        logarithm++
    }
    return 2 * logarithm + 1
}

private fun rangeCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> rangeCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> rangeCheckThrows(message: String, action: () -> Unit) {
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

internal fun rangeUpdateSequenceTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "rangeUpdateAlgebraLawsHold" to ::rangeUpdateAlgebraLawsHold,
    "rangeUpdateSurfacePreservesSnapshots" to ::rangeUpdateSurfacePreservesSnapshots,
    "rangeUpdateMeasureOrderIsPreserved" to ::rangeUpdateMeasureOrderIsPreserved,
    "rangeUpdateGeneratedHistoryMatchesList" to ::rangeUpdateGeneratedHistoryMatchesList,
    "rangeUpdateIdentityAndSharingContractsHold" to ::rangeUpdateIdentityAndSharingContractsHold,
    "rangeUpdateValidationPrecedesCallbacks" to ::rangeUpdateValidationPrecedesCallbacks,
    "rangeUpdateCallbackFailuresLeaveSourcesUsable" to ::rangeUpdateCallbackFailuresLeaveSourcesUsable,
    "rangeUpdateEveryPolicyFailureIsAtomicAndRetryable" to ::rangeUpdateEveryPolicyFailureIsAtomicAndRetryable,
    "rangeUpdateIteratorRetriesPolicyFailuresAtTheSameElement" to ::rangeUpdateIteratorRetriesPolicyFailuresAtTheSameElement,
    "rangeUpdateConstructionIsOneShotAndOrdered" to ::rangeUpdateConstructionIsOneShotAndOrdered,
    "rangeUpdateNullableElementsMeasuresAndTagsRemainValues" to ::rangeUpdateNullableElementsMeasuresAndTagsRemainValues,
    "rangeUpdateCheckedMaximumCountIsAtomic" to ::rangeUpdateCheckedMaximumCountIsAtomic,
    "rangeUpdateConcurrentReadersSeeSnapshots" to ::rangeUpdateConcurrentReadersSeeSnapshots,
)
