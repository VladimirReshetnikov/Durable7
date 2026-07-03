package tools.datastructures.hamt

private class ConstantPolicy<T> : HashPolicy<T> {
    override fun hash(key: T): Int = 0
    override fun equivalent(left: T, right: T): Boolean = left == right
}

private data class EquivalentKey(val text: String, val identity: Int)

private class EquivalentKeyPolicy : HashPolicy<EquivalentKey> {
    override fun hash(key: EquivalentKey): Int = key.text.hashCode()
    override fun equivalent(left: EquivalentKey, right: EquivalentKey): Boolean = left.text == right.text
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

public fun main() {
    val tests = listOf(
        "mapUpdatesPreserveOldVersions" to ::mapUpdatesPreserveOldVersions,
        "noOpUpdateAndAbsentRemoveShareRoots" to ::noOpUpdateAndAbsentRemoveShareRoots,
        "addRejectsDuplicates" to ::addRejectsDuplicates,
        "collisionsAreStoredAndRemoved" to ::collisionsAreStoredAndRemoved,
        "iterationStreamsTrieOrder" to ::iterationStreamsTrieOrder,
        "setItemsAreLastWinsAndRetainOriginalKey" to ::setItemsAreLastWinsAndRetainOriginalKey,
        "setAlgebraUsesSetMembership" to ::setAlgebraUsesSetMembership,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
