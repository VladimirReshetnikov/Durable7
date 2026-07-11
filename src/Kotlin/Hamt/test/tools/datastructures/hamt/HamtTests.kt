package tools.datastructures.hamt

import java.util.Collections

private class ConstantPolicy<T> : HashPolicy<T> {
    override fun hash(key: T): Int = 0
    override fun equivalent(left: T, right: T): Boolean = left == right
}

private data class EquivalentKey(val text: String, val identity: Int)

private class EquivalentKeyPolicy : HashPolicy<EquivalentKey> {
    override fun hash(key: EquivalentKey): Int = key.text.hashCode()
    override fun equivalent(left: EquivalentKey, right: EquivalentKey): Boolean = left.text == right.text
}

private class ModuloTenPolicy : HashPolicy<Int> {
    override fun hash(key: Int): Int = key.mod(10)
    override fun equivalent(left: Int, right: Int): Boolean = left.mod(10) == right.mod(10)
}

private fun check(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> checkEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private fun runConcurrent(name: String, workerCount: Int = 8, action: (Int) -> Unit) {
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

private fun mapUpdatesPreserveOldVersions() {
    val empty = PersistentHashMap.empty<String, Int>()
    val one = empty.put("a", 1)
    val two = one.put("b", 2)
    val replaced = two.put("a", 3)

    checkEquals(null, empty["a"], "empty map must remain empty")
    checkEquals(1, one["a"], "first version keeps its value")
    checkEquals(1, two["a"], "intermediate version keeps its value")
    checkEquals(3, replaced["a"], "new version sees replacement")
    checkEquals(2, replaced["b"], "new version keeps unrelated key")
}

private fun noOpUpdateAndAbsentRemoveShareRoots() {
    val map = PersistentHashMap.empty<String, Int>().put("a", 1).put("b", 2)
    val sameValue = map.put("a", 1)
    val absentRemoved = map.remove("missing")

    check(map.sharesRootWith(sameValue), "same-value replacement should preserve the root")
    check(map.sharesRootWith(absentRemoved), "absent removal should preserve the root")
}

private fun addRejectsDuplicates() {
    val map = PersistentHashMap.empty<String, Int>().put("a", 1)
    val result = map.tryAdd("a", 2)

    check(!result.added, "tryAdd should report duplicate")
    check(map.sharesRootWith(result.value), "duplicate tryAdd should preserve the root")

    var threw = false
    try {
        map.add("a", 2)
    } catch (_: DuplicateKeyException) {
        threw = true
    }

    check(threw, "add should throw on duplicate key")
}

private fun collisionsAreStoredAndRemoved() {
    val policy = ConstantPolicy<Int>()
    val map = PersistentHashMap.empty<Int, Int>(policy)
        .put(1, 10)
        .put(2, 20)
        .put(3, 30)

    checkEquals(10, map[1], "collision bucket keeps first value")
    checkEquals(20, map[2], "collision bucket keeps second value")
    checkEquals(30, map[3], "collision bucket keeps third value")

    val removed = map.tryRemove(2) ?: throw AssertionError("expected removal")
    checkEquals(20, removed.value, "tryRemove returns removed value")
    checkEquals(listOf(1, 3), removed.map.keys().toList().sorted(), "removal keeps other collision entries")
}

private fun champCanonicalizationAndDiff() {
    val policy = defaultHashPolicy<Int>()
    val ascending = PersistentHashMap.from((0 until 512).map { it to it }, policy)
    val descending = PersistentHashMap.from((0 until 512).reversed().map { it to it }, policy)
    check(ascending.mapEquals(descending), "independent insertion histories must be semantically equal")
    checkEquals(0, ascending.diff(descending).count(), "equal canonical maps have an empty diff")

    val statistics = ascending.champStatistics()
    checkEquals(512, statistics.inlinePayloads, "CHAMP must inline ordinary payloads")
    check(statistics.bitmapNodes > 1, "test data must exercise nested bitmap nodes")
    checkEquals(0, statistics.invalidLeafChildren, "bitmap child runs must not contain leaf nodes")

    val changed = descending.remove(7).put(9, -9).put(1_000, 1_000)
    val differences = ascending.diff(changed).toList()
    checkEquals(3, differences.size, "typed diff count")
    check(differences.any { it.kind == MapDifferenceKind.REMOVED && it.key == 7 }, "removed difference")
    check(differences.any { it.kind == MapDifferenceKind.CHANGED && it.key == 9 }, "changed difference")
    check(differences.any { it.kind == MapDifferenceKind.ADDED && it.key == 1_000 }, "added difference")
}

private fun iterationStreamsTrieOrder() {
    val policy = ConstantPolicy<Int>()
    val map = PersistentHashMap.empty<Int, Int>(policy).setItems((0 until 64).map { it to it * it })

    checkEquals(64, map.entries().count(), "iterator count")
    checkEquals((0 until 64).map { it to it * it }, map.entries().map { it.key to it.value }.toList(), "collision iteration order")
}

private fun setItemsAreLastWinsAndRetainOriginalKey() {
    val map = PersistentHashMap.from(
        listOf(
            EquivalentKey("x", 1) to 10,
            EquivalentKey("x", 2) to 20,
            EquivalentKey("y", 3) to 30,
        ),
        EquivalentKeyPolicy(),
    )

    val entry = map.getEntry(EquivalentKey("x", 99)) ?: throw AssertionError("expected key")
    checkEquals(1, entry.key.identity, "replacement keeps original key object")
    checkEquals(20, entry.value, "replacement uses new value")
    checkEquals(2, map.size, "equivalent key replacement does not grow the map")
}

private fun setAlgebraUsesSetMembership() {
    val left = PersistentHashSet.from(listOf(1, 2, 3))

    check(left.union(listOf(3, 4, 5)).setEquals(listOf(1, 2, 3, 4, 5)), "union")
    check(left.intersect(listOf(2, 3, 9)).setEquals(listOf(2, 3)), "intersection")
    check(left.except(listOf(1, 3)).setEquals(listOf(2)), "difference")
    check(left.symmetricExcept(listOf(3, 4)).setEquals(listOf(1, 2, 4)), "symmetric difference")
    check(left.intersect(listOf(2, 3, 9)).isProperSubsetOf(listOf(1, 2, 3, 3)), "proper subset")
    check(left.isProperSupersetOf(listOf(1, 3, 3)), "proper superset")
    check(!left.isProperSubsetOf(listOf(1, 2, 3)), "non-proper subset")
    check(!left.isProperSupersetOf(listOf(1, 2, 3)), "non-proper superset")
}

private fun crossPolicyRelationsUseReceiverPolicy() {
    val receiver = PersistentHashSet.from(listOf(1, 2), ModuloTenPolicy())
    val equivalentArgument = PersistentHashSet.from(listOf(11, 12))
    val strictSupersetArgument = PersistentHashSet.from(listOf(11, 12, 99))

    check(receiver.isSubsetOf(equivalentArgument), "cross-policy subset")
    check(receiver.isSupersetOf(equivalentArgument), "cross-policy superset")
    check(receiver.setEquals(equivalentArgument), "cross-policy equality")
    check(receiver.isProperSubsetOf(strictSupersetArgument), "cross-policy proper subset")
    check(receiver.overlaps(PersistentHashSet.from(listOf(42))), "cross-policy overlap")
}

private fun concurrentReadersObserveConsistentSnapshots() {
    val expectedMap = (0 until 256).map { it to it * 3 - 100 }
    val map = PersistentHashMap.empty<Int, Int>().setItems(expectedMap)
    val set = PersistentHashSet.from(0 until 256)

    runConcurrent("hamt-readers") {
        repeat(128) {
            checkEquals(256, map.size, "concurrent map size")
            checkEquals(284, map[128], "concurrent map lookup")
            checkEquals(
                expectedMap,
                map.entries().map { it.key to it.value }.toList().sortedBy { it.first },
                "concurrent map contents",
            )
            checkEquals(256, set.size, "concurrent set size")
            check(set.contains(200), "concurrent set membership")
            check(set.setEquals(0 until 256), "concurrent set equality")
        }
    }
}

private fun ctrieSnapshotsAndAtomicUpdates() {
    val trie = ConcurrentHashTrie<String, Int>()
    check(trie.tryAdd("alpha", 1), "Ctrie first add")
    check(!trie.tryAdd("alpha", 2), "Ctrie duplicate add")
    trie.set("alpha", 1)
    checkEquals(1L, trie.generation, "equal-value Ctrie update is a no-op")
    val snapshot = trie.snapshot()
    trie.compute("alpha", { 1 }, { _, value -> value + 1 })
    trie.set("beta", 2)
    checkEquals(1, snapshot["alpha"], "frozen generation retains old value")
    checkEquals(null, snapshot["beta"], "frozen generation excludes later key")
    checkEquals(1, snapshot.toPersistentHashMap().size, "snapshot converts explicitly to CHAMP")
    checkEquals(2, trie["alpha"], "live Ctrie advances")
}

private fun ctrieContentionAndGenerationRenewal() {
    val trie = ConcurrentHashTrie<Int, Int>()
    runConcurrent("ctrie-unique-add") { worker ->
        repeat(1_000) { offset ->
            val key = worker * 1_000 + offset
            check(trie.tryAdd(key, key * 3), "unique Ctrie add")
        }
    }
    val retained = trie.snapshot()
    runConcurrent("ctrie-counter") {
        repeat(2_000) { trie.compute(-1, { 1 }, { _, value -> value + 1 }) }
    }
    checkEquals(8_000, trie.size - 1, "all unique Ctrie entries survive contention")
    checkEquals(16_000, trie[-1], "node-local GCAS counter")
    checkEquals(8_000, retained.size, "snapshot remains stable after generation renewal")
}

private fun ctrieCollisionNodesRemainStable() {
    val trie = ConcurrentHashTrie<Int, Int>(ConstantPolicy())
    runConcurrent("ctrie-collisions") { worker ->
        repeat(200) { offset ->
            val key = worker * 200 + offset
            trie.set(key, key)
        }
    }
    val snapshot = trie.snapshot()
    runConcurrent("ctrie-collision-removal") { worker ->
        repeat(50) { offset ->
            val key = worker * 200 + offset
            checkEquals(key, trie.remove(key)?.value, "collision removal")
        }
    }
    checkEquals(1_600, snapshot.size, "collision snapshot size")
    checkEquals(1_200, trie.size, "collision live size")
    checkEquals(1_599, snapshot[1_599], "collision snapshot lookup")
}

private fun patriciaMapsAndSetsPreserveSignedOrder() {
    val intKeys = listOf(Int.MIN_VALUE, -1, 0, 1, Int.MAX_VALUE)
    val intMap = PersistentIntMap.from(intKeys.reversed().map { it to it.toString() })
    checkEquals(intKeys, intMap.map { it.first }, "32-bit Patricia signed order")
    checkEquals(Int.MIN_VALUE.toString(), intMap[Int.MIN_VALUE], "32-bit boundary lookup")

    val longKeys = listOf(Long.MIN_VALUE, -1L, 0L, 1L, Long.MAX_VALUE)
    val longMap = PersistentLongMap.from(longKeys.reversed().map { it to it.toString() })
    checkEquals(longKeys, longMap.map { it.first }, "64-bit Patricia signed order")
    checkEquals(Long.MAX_VALUE.toString(), longMap[Long.MAX_VALUE], "64-bit boundary lookup")

    var actual = PersistentIntMap.empty<Int>()
    val expected = mutableMapOf<Int, Int>()
    var state = 0x1234ABCD
    repeat(10_000) {
        state = state * 1664525 + 1013904223
        val key = (state ushr 8) % 401 - 200
        if (state and 3 == 0) {
            actual = actual.remove(key)
            expected.remove(key)
        } else {
            actual = actual.put(key, state)
            expected[key] = state
        }
    }
    checkEquals(expected.toSortedMap().toList(), actual.toList(), "Patricia randomized history")

    val left = PersistentIntSet.from(listOf(-3, -1, 1, 3))
    val right = PersistentIntSet.from(listOf(-1, 0, 1))
    checkEquals(listOf(-3, -1, 0, 1, 3), left.union(right).toList(), "Patricia set union")
    checkEquals(listOf(-1, 1), left.intersect(right).toList(), "Patricia set intersection")
    checkEquals(listOf(-3, 3), left.except(right).toList(), "Patricia set difference")

    val mapLeft = PersistentIntMap.from(listOf(1 to "left", 2 to "two"))
    val mapRight = PersistentIntMap.from(listOf(1 to "right", 3 to "three"))
    checkEquals(listOf(1 to "right", 2 to "two", 3 to "three"), mapLeft.union(mapRight).toList(), "Patricia map union is right-biased")
    checkEquals(listOf(1 to "left"), mapLeft.intersect(mapRight).toList(), "Patricia map intersection retains left values")
}

private fun patriciaCombiningCountsAndNoOps() {
    val left = PersistentIntMap.from(listOf(-4 to 40, 2 to 20, 7 to 70))
    val right = PersistentIntMap.from(listOf(2 to 3, 7 to 5, 9 to 90))
    val combine: (Int, Int, Int) -> Int = { key, leftValue, rightValue ->
        key + leftValue * 100 + rightValue
    }

    val union = left.union(right, combine)
    checkEquals(listOf(-4 to 40, 2 to 2_005, 7 to 7_012, 9 to 90), union.toList(), "Patricia combining union")
    checkEquals(4, union.size, "Patricia combining union cached count")

    val intersection = left.intersect(right, combine)
    checkEquals(listOf(2 to 2_005, 7 to 7_012), intersection.toList(), "Patricia combining intersection")
    checkEquals(2, intersection.size, "Patricia combining intersection cached count")

    val independentCopy = PersistentIntMap.from(left.toList())
    check(left.put(2, 20) === left, "same-value Patricia put should preserve receiver identity")
    check(left.remove(1_000) === left, "absent Patricia remove should preserve receiver identity")
    check(left.union(independentCopy) === left, "equal Patricia union should preserve receiver identity")
    check(left.intersect(independentCopy) === left, "equal Patricia intersection should preserve receiver identity")
    check(left.union(independentCopy) { _, leftValue, _ -> leftValue } === left,
        "combining Patricia union should preserve identity when values are unchanged")
    check(left.intersect(independentCopy) { _, leftValue, _ -> leftValue } === left,
        "combining Patricia intersection should preserve identity when values are unchanged")
    check(left.except(PersistentIntMap.from(listOf(1_000 to 1, 2_000 to 2))) === left,
        "disjoint Patricia difference should preserve receiver identity")

    val set = PersistentIntSet.from(left.map { it.first })
    val equalSet = PersistentIntSet.from(set.toList().reversed())
    check(set.add(2) === set, "duplicate Patricia set add should preserve receiver identity")
    check(set.remove(1_000) === set, "absent Patricia set remove should preserve receiver identity")
    check(set.union(equalSet) === set, "equal Patricia set union should preserve receiver identity")
    check(set.intersect(equalSet) === set, "equal Patricia set intersection should preserve receiver identity")
    check(set.except(PersistentIntSet.from(listOf(1_000, 2_000))) === set,
        "disjoint Patricia set difference should preserve receiver identity")

    val nullable = PersistentIntMap.empty<String?>().put(1, null)
    check(nullable.containsKey(1), "Patricia containsKey should distinguish a stored null value")

    val longLeft = PersistentLongMap.from(listOf(Long.MIN_VALUE to 4L, 6L to 7L))
    val longRight = PersistentLongMap.from(listOf(6L to 11L, Long.MAX_VALUE to 8L))
    val longUnion = longLeft.union(longRight) { key, leftValue, rightValue -> key xor leftValue xor rightValue }
    checkEquals(
        listOf(Long.MIN_VALUE to 4L, 6L to (6L xor 7L xor 11L), Long.MAX_VALUE to 8L),
        longUnion.toList(),
        "64-bit Patricia combining union",
    )
    checkEquals(3, longUnion.size, "64-bit Patricia combining union cached count")

    var state = 0x6D2B79F5
    repeat(256) { history ->
        val leftModel = mutableMapOf<Int, Int>()
        val rightModel = mutableMapOf<Int, Int>()
        repeat(48) {
            state = state * 1_664_525 + 1_013_904_223
            val key = (state ushr 9).mod(129) - 64
            if (state and 1 == 0) leftModel[key] = state else rightModel[key] = state
        }

        val actualLeft = PersistentIntMap.from(leftModel.map { it.key to it.value })
        val actualRight = PersistentIntMap.from(rightModel.map { it.key to it.value })
        val expectedUnion = leftModel.toMutableMap()
        for ((key, rightValue) in rightModel) {
            expectedUnion[key] = if (leftModel.containsKey(key)) {
                key xor leftModel.getValue(key) xor rightValue
            } else {
                rightValue
            }
        }
        val expectedIntersection = leftModel.keys.intersect(rightModel.keys).associateWith { key ->
            key xor leftModel.getValue(key) xor rightModel.getValue(key)
        }

        val actualUnion = actualLeft.union(actualRight) { key, leftValue, rightValue -> key xor leftValue xor rightValue }
        val actualIntersection = actualLeft.intersect(actualRight) { key, leftValue, rightValue -> key xor leftValue xor rightValue }
        val actualDifference = actualLeft.except(actualRight)
        val expectedDifference = leftModel.filterKeys { it !in rightModel }

        checkEquals(expectedUnion.toSortedMap().toList(), actualUnion.toList(), "randomized Patricia combining union $history")
        checkEquals(expectedUnion.size, actualUnion.size, "randomized Patricia union cached count $history")
        checkEquals(expectedIntersection.toSortedMap().toList(), actualIntersection.toList(), "randomized Patricia combining intersection $history")
        checkEquals(expectedIntersection.size, actualIntersection.size, "randomized Patricia intersection cached count $history")
        checkEquals(expectedDifference.toSortedMap().toList(), actualDifference.toList(), "randomized Patricia difference $history")
        checkEquals(expectedDifference.size, actualDifference.size, "randomized Patricia difference cached count $history")
    }
}

private fun exceptAndSymmetricExceptPreserveUntouchedRoots() {
    val set = PersistentHashSet.from((0..63).toList())

    val exceptNothing = set.except(emptyList())
    check(set.sharesRootWith(exceptNothing), "except of nothing should preserve the root")

    val symmetricNothing = set.symmetricExcept(emptyList())
    check(set.sharesRootWith(symmetricNothing), "symmetricExcept of nothing should preserve the root")

    val except = set.except(listOf(1, 63, 100))
    checkEquals(62, except.size, "except should remove present values only")
    check(!except.contains(1) && !except.contains(63), "except should remove the requested values")

    val symmetric = set.symmetricExcept(listOf(0, 1, 64, 65))
    checkEquals(64, symmetric.size, "symmetricExcept should toggle membership")
    check(!symmetric.contains(0) && symmetric.contains(64) && symmetric.contains(65), "toggle results")
}

private fun setTryRemoveDistinguishesStoredNull() {
    val set = PersistentHashSet.from(listOf<String?>(null, "a"))
    check(set.contains(null), "the set should contain the stored null element")

    val removed = set.tryRemove(null) ?: throw AssertionError("a stored null element must be removable")
    checkEquals(1, removed.set.size, "removal should drop exactly the null element")
    check(!removed.set.contains(null), "null should be gone after removal")
}

public fun main() {
    val tests = listOf(
        "mapUpdatesPreserveOldVersions" to ::mapUpdatesPreserveOldVersions,
        "noOpUpdateAndAbsentRemoveShareRoots" to ::noOpUpdateAndAbsentRemoveShareRoots,
        "exceptAndSymmetricExceptPreserveUntouchedRoots" to ::exceptAndSymmetricExceptPreserveUntouchedRoots,
        "setTryRemoveDistinguishesStoredNull" to ::setTryRemoveDistinguishesStoredNull,
        "addRejectsDuplicates" to ::addRejectsDuplicates,
        "collisionsAreStoredAndRemoved" to ::collisionsAreStoredAndRemoved,
        "champCanonicalizationAndDiff" to ::champCanonicalizationAndDiff,
        "iterationStreamsTrieOrder" to ::iterationStreamsTrieOrder,
        "setItemsAreLastWinsAndRetainOriginalKey" to ::setItemsAreLastWinsAndRetainOriginalKey,
        "setAlgebraUsesSetMembership" to ::setAlgebraUsesSetMembership,
        "crossPolicyRelationsUseReceiverPolicy" to ::crossPolicyRelationsUseReceiverPolicy,
        "concurrentReadersObserveConsistentSnapshots" to ::concurrentReadersObserveConsistentSnapshots,
        "ctrieSnapshotsAndAtomicUpdates" to ::ctrieSnapshotsAndAtomicUpdates,
        "ctrieContentionAndGenerationRenewal" to ::ctrieContentionAndGenerationRenewal,
        "ctrieCollisionNodesRemainStable" to ::ctrieCollisionNodesRemainStable,
        "patriciaMapsAndSetsPreserveSignedOrder" to ::patriciaMapsAndSetsPreserveSignedOrder,
        "patriciaCombiningCountsAndNoOps" to ::patriciaCombiningCountsAndNoOps,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
