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
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
