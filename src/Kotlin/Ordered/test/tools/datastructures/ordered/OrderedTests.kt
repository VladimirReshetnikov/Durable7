package tools.datastructures.ordered

import java.util.Collections
import java.util.Comparator
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs
import tools.datastructures.hamt.HashPolicy
import tools.datastructures.hamt.defaultHashPolicy

private fun assertThat(condition: Boolean, message: String) {
    if (!condition) throw AssertionError(message)
}

private fun <T> assertEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private fun assertSame(expected: Any?, actual: Any?, message: String) {
    if (expected !== actual) throw AssertionError("$message Expected referential identity.")
}

private fun assertNotSame(left: Any?, right: Any?, message: String) {
    if (left === right) throw AssertionError("$message Expected different references.")
}

private inline fun <reified T : Throwable> assertThrows(message: String, action: () -> Unit): T {
    try {
        action()
    } catch (error: Throwable) {
        if (error is T) return error
        throw AssertionError("$message Expected ${T::class.simpleName}, got ${error::class.simpleName}.", error)
    }
    throw AssertionError("$message Expected ${T::class.simpleName}.")
}

private class Representative(val equivalenceClass: Int, val name: String) {
    override fun toString(): String = "$name:$equivalenceClass"
}

private class RepresentativePolicy(private val bucketCount: Int = 1) : HashPolicy<Representative> {
    var hashCalls: Int = 0
    var equalityCalls: Int = 0
    var failure: Throwable? = null
    var hashesBeforeFailure: Int? = null

    override fun hash(key: Representative): Int {
        hashCalls++
        val remaining = hashesBeforeFailure
        if (remaining != null) {
            if (remaining == 0) throw failure ?: IllegalStateException("hash failure")
            hashesBeforeFailure = remaining - 1
        }
        failure?.let { if (hashesBeforeFailure == null) throw it }
        return Math.floorMod(key.equivalenceClass, bucketCount)
    }

    override fun equivalent(left: Representative, right: Representative): Boolean {
        equalityCalls++
        failure?.let { if (hashesBeforeFailure == null) throw it }
        return left.equivalenceClass == right.equivalenceClass
    }
}

private class FoldingStringPolicy : HashPolicy<String?> {
    override fun hash(key: String?): Int = key?.lowercase()?.hashCode() ?: 0

    override fun equivalent(left: String?, right: String?): Boolean =
        left.equals(right, ignoreCase = true)
}

private class ExactStringPolicy : HashPolicy<String?> {
    override fun hash(key: String?): Int = key?.hashCode() ?: 0

    override fun equivalent(left: String?, right: String?): Boolean = left == right
}

private object IdentityRepresentativePolicy : HashPolicy<Representative> {
    override fun hash(key: Representative): Int = System.identityHashCode(key)

    override fun equivalent(left: Representative, right: Representative): Boolean = left === right
}

private class IeeeDoublePolicy : HashPolicy<Double> {
    override fun hash(key: Double): Int = key.hashCode()

    // Bare IEEE 754 equality: a legitimate strict policy under which NaN is not equivalent to itself.
    override fun equivalent(left: Double, right: Double): Boolean = left == right
}

private fun rep(id: Int, name: String): Representative = Representative(id, name)

private fun <T> assertOrdered(expected: List<T>, actual: PersistentOrderedSet<T>, message: String) {
    assertEquals(expected, actual.toList(), "$message order")
    assertEquals(expected.size, actual.size, "$message size")
    assertEquals(expected.size, actual.count, "$message count")
    assertEquals(expected.isEmpty(), actual.isEmpty, "$message empty")
    assertEquals(expected.size, actual.validateStructure().count, "$message diagnostics")
    for ((index, value) in expected.withIndex()) {
        assertEquals(value, actual[index], "$message get $index")
        assertEquals(index, actual.indexOf(value), "$message index $index")
        assertThat(actual.contains(value), "$message contains $index")
        val lookup = actual.tryGetValue(value)
        assertThat(lookup.found, "$message lookup $index")
    }
}

private fun assertRepresentativeOrder(
    expected: List<Representative>,
    actual: PersistentOrderedSet<Representative>,
    message: String,
) {
    assertEquals(expected.size, actual.size, "$message size")
    for ((index, value) in expected.withIndex()) {
        assertSame(value, actual[index], "$message representative $index")
        assertSame(value, actual.tryGetValue(rep(value.equivalenceClass, "probe")).value, "$message lookup $index")
    }
    actual.validateStructure()
}

private fun constructionRetainsPoliciesAndFirstRepresentatives() {
    val defaultEmpty = PersistentOrderedSet.empty<String?>()
    assertSame(defaultEmpty, PersistentOrderedSet.empty<String?>(), "default empty canonicalization")
    assertSame(defaultEmpty, PersistentOrderedSet.create<String?>(), "create default empty")
    assertSame(
        defaultEmpty,
        PersistentOrderedSet.from<String?>(emptyList()),
        "empty range canonicalization",
    )
    assertSame(defaultHashPolicy<String?>(), defaultEmpty.policy, "default policy")

    val policy = RepresentativePolicy()
    val customEmpty = PersistentOrderedSet.empty(policy)
    assertNotSame(defaultEmpty, customEmpty, "custom empty")
    assertSame(policy, customEmpty.policy, "custom policy retention")
    assertNotSame(customEmpty, PersistentOrderedSet.empty(policy), "custom empties need not canonicalize")

    val first = rep(1, "first")
    val second = rep(2, "second")
    val duplicate = rep(1, "duplicate")
    val third = rep(3, "third")
    var iteratorCreations = 0
    val oneShot = object : Iterable<Representative> {
        override fun iterator(): Iterator<Representative> {
            iteratorCreations++
            return listOf(first, second, duplicate, third).iterator()
        }
    }
    val set = PersistentOrderedSet.createRange(oneShot, policy)
    assertEquals(1, iteratorCreations, "construction enumeration count")
    assertRepresentativeOrder(listOf(first, second, third), set, "construction")
    assertSame(policy, set.policy, "constructed policy")
    assertEquals(0, set.indexOf(duplicate), "duplicate index")
    assertSame(first, set.tryGetValue(duplicate).value, "first stored representative")

    val missing = rep(99, "missing")
    val miss = set.tryGetValue(missing)
    assertThat(!miss.found, "missing lookup discriminator")
    assertSame(missing, miss.value, "missing lookup echo")
    assertEquals(-1, set.indexOf(missing), "missing index")
}

private fun nullAndDuplicatePointOperationsPreserveIdentity() {
    val nullable = PersistentOrderedSet.from<String?>(listOf(null, "x", null, "y"))
    assertOrdered(listOf(null, "x", "y"), nullable, "nullable construction")
    val nullLookup = nullable.tryGetValue(null)
    assertThat(nullLookup.found && nullLookup.value == null, "stored null lookup")
    assertEquals(null, nullable.first, "nullable first")
    assertEquals(null, nullable.moveToLast(null).last, "nullable movement")
    assertThat(!nullable.remove(null).contains(null), "nullable removal")

    val policy = RepresentativePolicy()
    val first = rep(1, "first")
    val middle = rep(2, "middle")
    val last = rep(3, "last")
    val source = PersistentOrderedSet.from(listOf(first, middle, last), policy)
    val duplicate = rep(2, "duplicate")
    assertSame(source, source.add(duplicate), "duplicate append")
    assertSame(source, source.addFirst(duplicate), "duplicate prepend")
    assertSame(source, source.insert(0, duplicate), "duplicate insertion")
    assertRepresentativeOrder(listOf(first, middle, last), source, "duplicate source")

    val appended = rep(4, "appended")
    val prepended = rep(5, "prepended")
    val inserted = rep(6, "inserted")
    assertRepresentativeOrder(listOf(first, middle, last, appended), source.add(appended), "append")
    assertRepresentativeOrder(listOf(prepended, first, middle, last), source.addFirst(prepended), "prepend")
    assertRepresentativeOrder(listOf(first, inserted, middle, last), source.insert(1, inserted), "insert")
    assertRepresentativeOrder(listOf(first, middle, last), source, "persistent source")
}

private fun removalsEndpointsAndEagerPositionValidation() {
    val policy = RepresentativePolicy()
    val items = listOf(rep(1, "first"), rep(2, "middle"), rep(3, "last"))
    val source = PersistentOrderedSet.from(items, policy)
    val missing = rep(9, "missing")
    assertSame(source, source.remove(missing), "remove miss")
    val miss = source.tryRemove(missing)
    assertThat(!miss.removed, "try-remove miss flag")
    assertSame(source, miss.set, "try-remove miss identity")
    assertRepresentativeOrder(listOf(items[0], items[2]), source.remove(rep(2, "probe")), "remove value")
    assertRepresentativeOrder(listOf(items[0], items[2]), source.removeAt(1), "remove position")
    assertRepresentativeOrder(items.drop(1), source.removeFirst(), "remove first")
    assertRepresentativeOrder(items.dropLast(1), source.removeLast(), "remove last")
    assertRepresentativeOrder(items, source, "removal source")

    val cleared = source.clear()
    assertThat(cleared.isEmpty, "clear result")
    assertSame(policy, cleared.policy, "clear policy")
    assertSame(cleared, cleared.clear(), "empty clear identity")

    val empty = PersistentOrderedSet.empty<Int>()
    assertThrows<NoSuchElementException>("empty first") { empty.first }
    assertThrows<NoSuchElementException>("empty last") { empty.last }
    assertThrows<NoSuchElementException>("empty remove first") { empty.removeFirst() }
    assertThrows<NoSuchElementException>("empty remove last") { empty.removeLast() }
    assertThrows<OrderedSetMissingValueException>("empty move first") { empty.moveToFirst(1) }
    assertThrows<OrderedSetMissingValueException>("empty move last") { empty.moveToLast(1) }

    policy.hashCalls = 0
    policy.equalityCalls = 0
    val invalidActions = listOf<() -> Unit>(
        { source.getAt(-1) },
        { source.getAt(source.size) },
        { source.removeAt(-1) },
        { source.removeAt(source.size) },
        { source.insert(-1, missing) },
        { source.insert(source.size + 1, missing) },
        { source.moveTo(-1, missing) },
        { source.moveTo(source.size, missing) },
        { source.getRange(-1, 0) },
        { source.getRange(0, source.size + 1) },
        { source.take(-1) },
        { source.drop(source.size + 1) },
    )
    for (action in invalidActions) assertThrows<IndexOutOfBoundsException>("invalid position", action)
    assertEquals(0, policy.hashCalls, "positional validation hash callbacks")
    assertEquals(0, policy.equalityCalls, "positional validation equality callbacks")
}

private fun movementUsesFinalIndexesAndStoredRepresentatives() {
    for (size in 1..9) {
        val source = PersistentOrderedSet.from((0 until size).toList())
        for (oldIndex in 0 until size) {
            for (finalIndex in 0 until size) {
                val expected = (0 until size).toMutableList()
                val item = expected.removeAt(oldIndex)
                expected.add(finalIndex, item)
                val actual = source.moveTo(finalIndex, oldIndex)
                assertOrdered(expected, actual, "move $size/$oldIndex/$finalIndex")
                if (oldIndex == finalIndex) {
                    assertSame(source, actual, "movement identity")
                } else {
                    assertNotSame(source, actual, "movement change")
                }
            }
        }
    }

    val policy = RepresentativePolicy()
    val first = rep(1, "first")
    val middle = rep(2, "middle")
    val last = rep(3, "last")
    val source = PersistentOrderedSet.from(listOf(first, middle, last), policy)
    assertSame(source, source.moveToFirst(rep(1, "probe")), "already first")
    assertSame(source, source.moveToLast(rep(3, "probe")), "already last")
    assertRepresentativeOrder(listOf(middle, first, last), source.moveToFirst(rep(2, "probe")), "move first")
    assertRepresentativeOrder(listOf(first, last, middle), source.moveToLast(rep(2, "probe")), "move last")
    assertThrows<OrderedSetMissingValueException>("missing movement") { source.moveToFirst(rep(99, "missing")) }
}

private fun sparseLabelsRelabelAndPreserveSiblingSnapshots() {
    var source = PersistentOrderedSet.from(listOf(-2, -1))
    repeat(72) { value ->
        source = source.insert(1, value)
        source.validateStructure()
    }
    val expectedSource = source.toList()
    var left = source
    var right = source
    for (value in 1_000 until 1_080) left = left.insert(1, value)
    for (value in 2_000 until 2_080) right = right.insert(1, value)
    assertEquals(expectedSource, source.toList(), "relabel source snapshot")
    assertEquals(154, left.size, "left relabel size")
    assertEquals(154, right.size, "right relabel size")
    assertEquals(1_079, left[1], "left relabel position")
    assertEquals(2_079, right[1], "right relabel position")
    assertThat(!left.contains(2_079), "left sibling isolation")
    assertThat(!right.contains(1_079), "right sibling isolation")
    left.validateStructure()
    right.validateStructure()

    // Exhaust one interior stride, then fail on the first hash of the unpublished relabel rebuild.
    val policy = RepresentativePolicy()
    var guarded = PersistentOrderedSet.from(listOf(rep(-2, "left"), rep(-1, "right")), policy)
    repeat(20) { guarded = guarded.insert(1, rep(it, "middle-$it")) }
    val before = guarded.toList()
    val failure = IllegalStateException("injected relabel failure")
    policy.failure = failure
    policy.hashesBeforeFailure = 1
    val thrown = assertThrows<IllegalStateException>("relabel failure") {
        guarded.insert(1, rep(100, "trigger"))
    }
    assertSame(failure, thrown, "relabel failure identity")
    policy.failure = null
    policy.hashesBeforeFailure = null
    assertEquals(before, guarded.toList(), "relabel failure source")
    guarded.validateStructure()
}

private fun rangesReverseAndStableSortMatchModels() {
    val policy = RepresentativePolicy(4)
    val items = (0 until 12).map { rep(it, "item-$it") }
    val source = PersistentOrderedSet.from(items, policy)
    for (index in 0..source.size) {
        for (count in 0..(source.size - index)) {
            val actual = source.getRange(index, count)
            assertRepresentativeOrder(items.subList(index, index + count), actual, "range $index/$count")
            assertSame(policy, actual.policy, "range policy")
            if (index == 0 && count == source.size) assertSame(source, actual, "full range identity")
        }
    }
    assertSame(source, source.take(source.size), "full take identity")
    assertSame(source, source.drop(0), "zero drop identity")
    assertRepresentativeOrder(items.take(3), source.take(3), "take")
    assertRepresentativeOrder(items.drop(2), source.drop(2), "drop")
    assertSame(policy, source.take(0).policy, "empty take policy")
    assertSame(policy, source.drop(source.size).policy, "empty drop policy")

    assertRepresentativeOrder(items.asReversed(), source.reverse(), "reverse")
    assertSame(PersistentOrderedSet.empty<Int>(), PersistentOrderedSet.empty<Int>().reverse(), "empty reverse")
    val singleton = PersistentOrderedSet.from(listOf(1))
    assertSame(singleton, singleton.reverse(), "singleton reverse")
    assertSame(PersistentOrderedSet.empty<Int>(), PersistentOrderedSet.empty<Int>().sort(Comparator { _, _ ->
        throw AssertionError("empty sort invoked comparator")
    }), "empty sort")
    assertSame(singleton, singleton.sort(Comparator { _, _ ->
        throw AssertionError("singleton sort invoked comparator")
    }), "singleton sort")

    val stableComparator = Comparator<Representative> { left, right ->
        (left.equivalenceClass % 3).compareTo(right.equivalenceClass % 3)
    }
    val expected = items.withIndex().sortedWith(
        compareBy<IndexedValue<Representative>> { it.value.equivalenceClass % 3 }.thenBy { it.index },
    ).map { it.value }
    val sorted = source.sort(stableComparator)
    assertRepresentativeOrder(expected, sorted, "stable sort")
    assertSame(policy, sorted.policy, "sort policy")
    assertSame(sorted, sorted.sort(stableComparator), "unchanged sort identity")
    assertSame(source, source.sort(Comparator { _, _ -> 0 }), "all-ties sort identity")
    val later = rep(99, "later")
    assertRepresentativeOrder(expected + later, sorted.add(later), "sort is one-shot")

    val natural = PersistentOrderedSet.from(listOf(4, 1, 3, 2)).sort()
    assertOrdered(listOf(1, 2, 3, 4), natural, "natural sort")
}

private fun algebraUsesReceiverPolicyOrderingAndRepresentatives() {
    val policy = FoldingStringPolicy()
    val left = PersistentOrderedSet.from<String?>(listOf("Alpha", "beta", "left", null), policy)
    val right = PersistentOrderedSet.from<String?>(
        listOf("BETA", "gamma", "ALPHA", "delta", "GAMMA", null),
        ExactStringPolicy(),
    )

    assertOrdered(
        listOf("Alpha", "beta", "left", null, "gamma", "delta"),
        left.union(right),
        "union",
    )
    assertOrdered(listOf("Alpha", "beta", null), left.intersect(right), "intersection")
    assertOrdered(listOf("left"), left.except(right), "difference")
    assertOrdered(listOf("left", "gamma", "delta"), left.symmetricExcept(right), "symmetric difference")
    assertSame(policy, left.union(right).policy, "union receiver policy")
    assertSame(policy, left.intersect(right).policy, "intersection receiver policy")

    assertSame(left, left.union(listOf("ALPHA", "BETA", "LEFT", null)), "union no-op identity")
    assertSame(left, left.intersect(listOf(null, "LEFT", "BETA", "ALPHA")), "intersect no-op identity")
    assertSame(left, left.except(emptyList()), "except no-op identity")
    assertSame(left, left.symmetricExcept(emptyList()), "symmetric no-op identity")

    assertThat(left.isSubsetOf(listOf("ALPHA", "BETA", "LEFT", null, "extra")), "subset")
    assertThat(left.isProperSubsetOf(listOf("ALPHA", "BETA", "LEFT", null, "extra")), "proper subset")
    assertThat(left.isSupersetOf(listOf("ALPHA", null)), "superset")
    assertThat(left.isProperSupersetOf(listOf("ALPHA", null)), "proper superset")
    assertThat(left.overlaps(listOf("missing", "BETA")), "overlap")
    assertThat(left.setEquals(listOf(null, "LEFT", "beta", "ALPHA", "alpha")), "set equals")
    assertThat(!left.setEquals(listOf("Alpha", "beta")), "set unequal")

    val representativePolicy = RepresentativePolicy()
    val receiverFirst = rep(1, "receiver-first")
    val receiverSecond = rep(2, "receiver-second")
    val argumentSecond = rep(2, "argument-second")
    val argumentOnlyFirst = rep(3, "argument-only-first")
    val argumentOnlyLater = rep(3, "argument-only-later")
    val representativeLeft = PersistentOrderedSet.from(
        listOf(receiverFirst, receiverSecond),
        representativePolicy,
    )
    val representativeRight = PersistentOrderedSet.from(
        listOf(argumentSecond, argumentOnlyFirst, argumentOnlyLater),
        IdentityRepresentativePolicy,
    )
    assertRepresentativeOrder(
        listOf(receiverFirst, receiverSecond, argumentOnlyFirst),
        representativeLeft.union(representativeRight),
        "algebra representatives union",
    )
    assertRepresentativeOrder(
        listOf(receiverSecond),
        representativeLeft.intersect(representativeRight),
        "algebra representatives intersection",
    )
    assertRepresentativeOrder(
        listOf(receiverFirst),
        representativeLeft.except(representativeRight),
        "algebra representatives difference",
    )
    assertRepresentativeOrder(
        listOf(receiverFirst, argumentOnlyFirst),
        representativeLeft.symmetricExcept(representativeRight),
        "algebra representatives symmetric difference",
    )
}

private fun eagerNormalizationAndCallbackFailuresAreSourceAtomic() {
    val source = PersistentOrderedSet.from(listOf(1, 2, 3))
    val lateFailure = IllegalStateException("late enumeration")
    val throwing = sequence<Int> {
        yield(1)
        yield(2)
        yield(3)
        throw lateFailure
    }.asIterable()
    val operations = listOf<() -> Unit>(
        { source.union(throwing) },
        { source.intersect(throwing) },
        { source.except(throwing) },
        { source.symmetricExcept(throwing) },
        { source.isSubsetOf(throwing) },
        { source.isProperSubsetOf(throwing) },
        { source.isSupersetOf(throwing) },
        { source.isProperSupersetOf(throwing) },
        { source.overlaps(throwing) },
        { source.setEquals(throwing) },
    )
    for (operation in operations) {
        val thrown = assertThrows<IllegalStateException>("eager normalization", operation)
        assertSame(lateFailure, thrown, "late failure identity")
    }
    assertOrdered(listOf(1, 2, 3), source, "normalization failure source")

    val policy = RepresentativePolicy()
    val items = listOf(rep(3, "three"), rep(1, "one"), rep(2, "two"))
    val guarded = PersistentOrderedSet.from(items, policy)
    val policyFailure = IllegalStateException("policy failure")
    policy.failure = policyFailure
    val probe = rep(99, "probe")
    val pointOperations = listOf<() -> Unit>(
        { guarded.contains(probe) },
        { guarded.indexOf(probe) },
        { guarded.add(probe) },
        { guarded.addFirst(probe) },
        { guarded.insert(1, probe) },
        { guarded.remove(probe) },
        { guarded.moveToFirst(probe) },
        { guarded.union(listOf(probe)) },
    )
    for (operation in pointOperations) {
        val thrown = assertThrows<IllegalStateException>("policy failure", operation)
        assertSame(policyFailure, thrown, "policy failure identity")
    }
    policy.failure = null

    val sortFailure = IllegalArgumentException("ordering failure")
    val thrown = assertThrows<IllegalArgumentException>("sort failure") {
        guarded.sort(Comparator { _, _ -> throw sortFailure })
    }
    assertSame(sortFailure, thrown, "sort failure identity")
    assertRepresentativeOrder(items, guarded, "callback failure source")

    policy.failure = policyFailure
    val customEmpty = PersistentOrderedSet.empty(policy)
    assertSame(customEmpty, customEmpty.clear(), "empty clear avoids policy")
    assertSame(customEmpty, customEmpty.reverse(), "empty reverse avoids policy")
    policy.failure = null
}

private class DeterministicRandom(seed: Long) {
    private var state: Long = seed

    fun nextInt(): Int {
        state = state * 6364136223846793005L + 1442695040888963407L
        return (state ushr 32).toInt()
    }

    fun nextIndex(exclusive: Int): Int =
        if (exclusive == 0) 0 else (nextInt().toUInt().toLong() % exclusive).toInt()

    fun nextBetween(low: Int, high: Int): Int = low + nextIndex(high - low + 1)
}

private fun generatedHistoriesMatchAListSetModelAndRetainedBranches() {
    val random = DeterministicRandom(0x0D3E_D5E7L)
    var set = PersistentOrderedSet.empty<Int>()
    val model = ArrayList<Int>()
    val snapshots = ArrayList<Pair<PersistentOrderedSet<Int>, List<Int>>>()

    repeat(1_200) { step ->
        val value = random.nextBetween(-30, 30)
        val before = set
        val beforeModel = model.toList()
        when (random.nextIndex(13)) {
            0 -> {
                set = set.add(value)
                if (value !in model) model.add(value) else assertSame(before, set, "model duplicate add $step")
            }
            1 -> {
                set = set.addFirst(value)
                if (value !in model) model.add(0, value) else assertSame(before, set, "model duplicate first $step")
            }
            2 -> {
                val index = random.nextIndex(model.size + 1)
                set = set.insert(index, value)
                if (value !in model) model.add(index, value) else assertSame(before, set, "model duplicate insert $step")
            }
            3 -> {
                set = set.remove(value)
                if (!model.remove(value)) assertSame(before, set, "model remove miss $step")
            }
            4 -> if (model.isNotEmpty()) {
                val oldIndex = random.nextIndex(model.size)
                val finalIndex = random.nextIndex(model.size)
                val moved = model[oldIndex]
                set = set.moveTo(finalIndex, moved)
                model.removeAt(oldIndex)
                model.add(finalIndex, moved)
                if (oldIndex == finalIndex) assertSame(before, set, "model move no-op $step")
            }
            5 -> if (model.isNotEmpty()) {
                val index = random.nextIndex(model.size)
                set = set.removeAt(index)
                model.removeAt(index)
            }
            6 -> {
                val count = random.nextIndex(model.size + 1)
                set = set.take(count)
                while (model.size > count) model.removeAt(model.lastIndex)
            }
            7 -> {
                val count = random.nextIndex(model.size + 1)
                set = set.drop(count)
                repeat(count) { model.removeAt(0) }
            }
            8 -> {
                set = set.reverse()
                model.reverse()
            }
            9 -> {
                val argument = listOf(value, value + 1, value)
                set = set.union(argument)
                for (item in argument) if (item !in model) model.add(item)
            }
            10 -> {
                val argument = listOf(value, value + 1, value + 2)
                set = set.except(argument)
                model.removeAll(argument.toSet())
            }
            11 -> {
                val comparator = Comparator<Int> { left, right ->
                    val byAbsolute = abs(left).compareTo(abs(right))
                    if (byAbsolute != 0) byAbsolute else 0
                }
                set = set.sort(comparator)
                model.sortWith(comparator)
            }
            else -> {
                snapshots.add(set to model.toList())
                if (snapshots.size > 48) snapshots.removeAt(0)
            }
        }

        assertOrdered(model, set, "generated step $step")
        assertEquals(beforeModel, before.toList(), "generated source persistence $step")
        for ((snapshot, expected) in snapshots.takeLast(3)) {
            assertEquals(expected, snapshot.toList(), "generated retained snapshot $step")
        }
    }

    for ((snapshot, expected) in snapshots) assertOrdered(expected, snapshot, "final retained branch")
}

private fun iteratorsAndConcurrentReadersObserveImmutableSnapshots() {
    val expected = (0 until 512).toList()
    val source = PersistentOrderedSet.from(expected)
    val iterator = source.iterator()
    assertEquals(0, iterator.next(), "iterator first")
    val successor = source.moveToFirst(511).add(512)
    assertEquals(listOf(1, 2, 3), List(3) { iterator.next() }, "version-bound iterator")
    assertEquals(expected, source.toList(), "iterator source")
    assertEquals(511, successor.first, "successor first")
    assertEquals(512, successor.last, "successor last")

    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val completed = AtomicInteger()
    val workers = (0 until 4).map { worker ->
        Thread {
            try {
                repeat(64) {
                    assertEquals(expected, source.toList(), "concurrent source $worker")
                    assertEquals(511, successor.first, "concurrent successor first $worker")
                    assertEquals(513, successor.size, "concurrent successor size $worker")
                    source.validateStructure()
                    successor.validateStructure()
                }
                completed.incrementAndGet()
            } catch (error: Throwable) {
                failures.add(error)
            }
        }.apply { name = "ordered-reader-$worker" }
    }
    workers.forEach { it.start() }
    workers.forEach { it.join() }
    if (failures.isNotEmpty()) throw AssertionError("Concurrent reader failure", failures.first())
    assertEquals(4, completed.get(), "concurrent completed workers")
}

private fun orderedMapRetainsPositionsAcrossPayloadEdits() {
    val source = PersistentOrderedMap.from(listOf(1 to "one", 2 to "two", 3 to "three"))
    val replaced = source.set(2, "TWO")
    val moved = replaced.moveToFirst(3)
    val sorted = moved.sort(Comparator { left, right -> right.value.compareTo(left.value) })
    val range = sorted.getRange(1, 2)
    assertEquals(listOf(1, 2, 3), replaced.toList().map { it.key }, "ordered map replacement order")
    assertEquals("two", source[2], "ordered map snapshot")
    assertThat(source.sharesOrderWith(replaced), "ordered map value edit shares order")
    assertThat(replaced.sharesValuesWith(moved), "ordered map movement shares values")
    assertEquals(listOf(3, 1, 2), moved.toList().map { it.key }, "ordered map movement")
    assertEquals(2, range.size, "ordered map range")
    range.validateStructure()
}

private fun orderedMultimapPreservesGroupedOrderAndRepresentatives() {
    val policy = FoldingStringPolicy()
    val source = PersistentOrderedMultimap.from(
        listOf("Alpha" to "One", "Beta" to "Two", "ALPHA" to "Three", "alpha" to "ONE"),
        policy,
        policy,
    )
    val removed = source.remove("ALPHA", "one").removeKey("beta")
    assertEquals(
        listOf("Alpha" to "One", "Alpha" to "Three", "Beta" to "Two"),
        source.toList().map { it.key to it.value },
        "ordered multimap grouped order",
    )
    assertEquals(2, source.keyCount, "ordered multimap key count")
    assertEquals(3L, source.pairCount, "ordered multimap pair count")
    assertEquals("Alpha", source.actualKey("alpha"), "ordered multimap key representative")
    assertEquals("Three", source.actualValue("alpha", "THREE"), "ordered multimap value representative")
    assertEquals(listOf("Alpha" to "Three"), removed.toList().map { it.key to it.value }, "ordered multimap removal")
    assertThat(source.contains("alpha", "one"), "ordered multimap source snapshot")
    removed.validateStructure()
}

private fun orderedCursorsPreserveGapsPoliciesAndSnapshots() {
    val setSource = PersistentOrderedSet.from<String?>(listOf("a", null, "c"))
    val setCursor = checkNotNull(setSource.cursorAt(1))
    assertEquals(null, checkNotNull(setCursor.peekNext()).value, "ordered-set cursor stored null")
    assertEquals("a", checkNotNull(setCursor.peekPrevious()).value, "ordered-set cursor previous")
    val setInserted = setCursor.insert("x")
    assertEquals(2, setInserted.position, "ordered-set cursor insertion position")
    assertEquals(listOf("a", "x", null, "c"), setInserted.snapshot().toList(), "ordered-set cursor insertion")
    val setDuplicate = setInserted.tryInsert(null)
    assertThat(!setDuplicate.added, "ordered-set cursor duplicate flag")
    assertSame(setInserted, setDuplicate.cursor, "ordered-set duplicate cursor identity")
    val setDeleted = checkNotNull(setInserted.deletePrevious())
    assertEquals(1, setDeleted.position, "ordered-set cursor deletion position")
    assertEquals(setSource.toList(), setDeleted.snapshot().toList(), "ordered-set cursor deletion")
    assertSame(setSource.policy, setDeleted.snapshot().policy, "ordered-set cursor policy")
    val setFound = setSource.findCursor(null)
    assertThat(setFound.found, "ordered-set cursor find null")
    assertEquals(1, setFound.cursor.position, "ordered-set cursor find position")
    assertEquals(setSource.size, setSource.findCursor("missing").cursor.position, "ordered-set cursor miss")
    assertSame(setSource, setCursor.snapshot(), "ordered-set cursor source snapshot")

    val mapSource = PersistentOrderedMap.from(listOf("a" to 1, "b" to 2, "c" to 3))
    val mapCursor = checkNotNull(mapSource.cursorAt(1))
    val mapInserted = mapCursor.insert("x", 9)
    val mapUpdated = checkNotNull(mapInserted.setNextValue(20))
    assertEquals(2, mapUpdated.position, "ordered-map cursor update gap")
    assertEquals(
        listOf("a" to 1, "x" to 9, "b" to 20, "c" to 3),
        mapUpdated.snapshot().toList().map { it.key to it.value },
        "ordered-map cursor edits",
    )
    // The CHAMP payload index is no-op aware, so a value-equivalent update must propagate that rule
    // through the map and the cursor rather than publishing a fresh version.
    assertSame(mapUpdated, mapUpdated.setNextValue(20), "ordered-map cursor value no-op identity")
    assertSame(
        mapUpdated.snapshot(),
        mapUpdated.snapshot().set("b", 20),
        "ordered-map value no-op identity",
    )
    assertThat(
        mapUpdated.snapshot() !== mapUpdated.snapshot().set("b", 21),
        "a changed payload still publishes a new version",
    )
    val mapDuplicate = mapUpdated.tryInsert("b", 200)
    assertThat(!mapDuplicate.added, "ordered-map cursor duplicate flag")
    assertEquals(2, mapDuplicate.cursor.position, "ordered-map duplicate focuses stored key")
    assertSame(mapUpdated.snapshot(), mapDuplicate.cursor.snapshot(), "ordered-map duplicate snapshot")
    val mapDeleted = checkNotNull(checkNotNull(mapUpdated.deletePrevious()).deleteNext())
    assertEquals(listOf("a" to 1, "c" to 3), mapDeleted.snapshot().toList().map { it.key to it.value }, "ordered-map cursor deletes")
    assertEquals(1, mapDeleted.position, "ordered-map cursor delete gap")
    assertSame(mapSource, mapCursor.snapshot(), "ordered-map cursor source snapshot")

    val multimapSource = PersistentOrderedMultimap.from(
        listOf("b" to 2, "a" to 9, "b" to 1, "c" to 7),
    )
    assertEquals(
        listOf("b" to 2, "b" to 1, "a" to 9, "c" to 7),
        multimapSource.toList().map { it.key to it.value },
        "ordered-multimap grouped source",
    )
    val pairFound = multimapSource.findCursor("b", 1)
    assertThat(pairFound.found, "ordered-multimap pair cursor find")
    assertEquals(1L, pairFound.cursor.position, "ordered-multimap pair cursor position")
    val multimapAdded = pairFound.cursor.add("b", 3)
    assertEquals(3L, multimapAdded.position, "ordered-multimap cursor grouped insertion gap")
    val multimapDuplicate = multimapAdded.tryAdd("b", 3)
    assertThat(!multimapDuplicate.added, "ordered-multimap cursor duplicate flag")
    assertSame(multimapAdded, multimapDuplicate.cursor, "ordered-multimap duplicate cursor identity")
    val multimapDeleted = checkNotNull(checkNotNull(multimapAdded.deletePrevious()).deleteNext())
    assertEquals(
        listOf("b" to 2, "b" to 1, "c" to 7),
        multimapDeleted.snapshot().toList().map { it.key to it.value },
        "ordered-multimap cursor deletes and empty-group contraction",
    )
    assertEquals(2L, multimapDeleted.position, "ordered-multimap cursor delete gap")
    assertEquals("c" to 7, checkNotNull(multimapDeleted.peekNext()).value.let { it.key to it.value }, "ordered-multimap cursor next")
    val groupFound = multimapSource.findGroupCursor("a")
    assertThat(groupFound.found, "ordered-multimap group cursor find")
    assertEquals(2L, groupFound.cursor.position, "ordered-multimap group cursor position")
    assertEquals(multimapSource.pairCount, multimapSource.findGroupCursor("z").cursor.position, "ordered-multimap group miss")
    assertSame(multimapSource, pairFound.cursor.snapshot(), "ordered-multimap cursor source snapshot")
}

private fun orderedMultimapCursorToleratesNonReflexiveValues() {
    val nan = Double.NaN
    val values = IeeeDoublePolicy()
    // The value policy must genuinely treat NaN as non-reflexive for this regression to bite.
    assertThat(!values.equivalent(nan, nan), "non-reflexive value policy")

    val source = PersistentOrderedMultimap.from(
        listOf("a" to 1.0, "b" to 2.0, "a" to 3.0),
        defaultHashPolicy<String>(),
        values,
    )
    assertEquals(
        listOf("a" to 1.0, "a" to 3.0, "b" to 2.0),
        source.toList().map { it.key to it.value },
        "non-reflexive grouped source",
    )

    // add() must derive the gap from the key group, not re-scan for the accepted pair by value.
    val addedNan = checkNotNull(source.cursorAt(0L)).add("a", nan)
    assertEquals(4L, addedNan.snapshot().pairCount, "NaN pair accepted")
    assertEquals(3L, addedNan.position, "NaN insertion lands after the key group")
    val stored = checkNotNull(addedNan.peekPrevious()).value
    assertThat(stored.key == "a" && stored.value.isNaN(), "a stored NaN precedes the gap")
    assertEquals(
        listOf("a" to 1.0, "a" to 3.0, "b" to 2.0),
        source.toList().map { it.key to it.value },
        "add persists source",
    )

    // A normal reflexive value still lands at the same group boundary.
    val addedReal = checkNotNull(source.cursorAt(0L)).add("a", 7.0)
    assertEquals(3L, addedReal.position, "reflexive insertion lands after the key group")
    assertEquals(
        "a" to 7.0,
        checkNotNull(addedReal.peekPrevious()).value.let { it.key to it.value },
        "reflexive pair precedes the gap",
    )

    // delete() must not report a false success when remove-by-content is a no-op for a stored NaN.
    val withNan = PersistentOrderedMultimap.empty(defaultHashPolicy<String>(), values).add("a", nan)
    assertEquals(1L, withNan.pairCount, "single NaN pair stored")
    assertThat(checkNotNull(withNan.cursorAt(1L)).deletePrevious() == null, "no-op delete-previous signals null")
    assertThat(checkNotNull(withNan.cursorAt(0L)).deleteNext() == null, "no-op delete-next signals null")

    // A reflexive value deletes normally through the same guarded path.
    val withReal = PersistentOrderedMultimap.empty(defaultHashPolicy<String>(), values).add("a", 5.0)
    val deleted = checkNotNull(checkNotNull(withReal.cursorAt(1L)).deletePrevious())
    assertThat(deleted.snapshot().isEmpty, "reflexive delete removes the pair")
    assertEquals(0L, deleted.position, "reflexive delete decrements the gap")
}

public fun main() {
    val tests = listOf(
        "constructionRetainsPoliciesAndFirstRepresentatives" to ::constructionRetainsPoliciesAndFirstRepresentatives,
        "nullAndDuplicatePointOperationsPreserveIdentity" to ::nullAndDuplicatePointOperationsPreserveIdentity,
        "removalsEndpointsAndEagerPositionValidation" to ::removalsEndpointsAndEagerPositionValidation,
        "movementUsesFinalIndexesAndStoredRepresentatives" to ::movementUsesFinalIndexesAndStoredRepresentatives,
        "sparseLabelsRelabelAndPreserveSiblingSnapshots" to ::sparseLabelsRelabelAndPreserveSiblingSnapshots,
        "rangesReverseAndStableSortMatchModels" to ::rangesReverseAndStableSortMatchModels,
        "algebraUsesReceiverPolicyOrderingAndRepresentatives" to ::algebraUsesReceiverPolicyOrderingAndRepresentatives,
        "eagerNormalizationAndCallbackFailuresAreSourceAtomic" to ::eagerNormalizationAndCallbackFailuresAreSourceAtomic,
        "generatedHistoriesMatchAListSetModelAndRetainedBranches" to ::generatedHistoriesMatchAListSetModelAndRetainedBranches,
        "iteratorsAndConcurrentReadersObserveImmutableSnapshots" to ::iteratorsAndConcurrentReadersObserveImmutableSnapshots,
        "orderedMapRetainsPositionsAcrossPayloadEdits" to ::orderedMapRetainsPositionsAcrossPayloadEdits,
        "orderedMultimapPreservesGroupedOrderAndRepresentatives" to
            ::orderedMultimapPreservesGroupedOrderAndRepresentatives,
        "orderedCursorsPreserveGapsPoliciesAndSnapshots" to
            ::orderedCursorsPreserveGapsPoliciesAndSnapshots,
        "orderedMultimapCursorToleratesNonReflexiveValues" to
            ::orderedMultimapCursorToleratesNonReflexiveValues,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
