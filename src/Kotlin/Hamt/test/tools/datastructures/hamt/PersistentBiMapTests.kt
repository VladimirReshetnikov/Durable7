package tools.datastructures.hamt

import java.util.concurrent.ConcurrentLinkedQueue

private data class BiRepresentative(val logical: String, val spelling: String)

private class BiRepresentativePolicy : HashPolicy<BiRepresentative> {
    override fun hash(key: BiRepresentative): Int = key.logical.hashCode()
    override fun equivalent(left: BiRepresentative, right: BiRepresentative): Boolean =
        left.logical == right.logical
}

private class BiThrowingPolicy : HashPolicy<Int> {
    var throwOnHash: Boolean = false
    override fun hash(key: Int): Int {
        if (throwOnHash) error("injected bimap hash failure")
        return key
    }
    override fun equivalent(left: Int, right: Int): Boolean = left == right
}

internal fun runPersistentBiMapTests() {
    val source = PersistentBiMap.empty<Int, String>().add(1, "one")
    val keyConflict = source.tryAdd(1, "uno")
    require(!keyConflict.added && keyConflict.conflict == BiMapConflict.KEY && keyConflict.map === source)
    val valueConflict = source.tryAdd(2, "one")
    require(!valueConflict.added && valueConflict.conflict == BiMapConflict.VALUE && valueConflict.map === source)
    require(source.tryAdd(1, "one").conflict == BiMapConflict.KEY)

    val two = source.add(2, "two")
    try {
        two.set(1, "two")
        error("expected occupied-value conflict")
    } catch (error: BiMapConflictException) {
        require(error.conflict == BiMapConflict.VALUE)
    }
    val replaced = two.set(1, "uno")
    require(replaced.lookup(1) == BiMapLookup.Found("uno"))
    require(replaced.lookupKey("uno") == BiMapLookup.Found(1))
    require(replaced.lookupKey("one") == BiMapLookup.Missing)
    require(two.lookup(1) == BiMapLookup.Found("one"))

    val policy = BiRepresentativePolicy()
    val storedKey = BiRepresentative("alpha", "Alpha")
    val storedValue = BiRepresentative("one", "One")
    val representativeMap = PersistentBiMap.empty<BiRepresentative, BiRepresentative>(policy, policy)
        .add(storedKey, storedValue)
    val equivalent = representativeMap.set(
        BiRepresentative("alpha", "ALPHA"),
        BiRepresentative("one", "ONE"),
    )
    require(equivalent === representativeMap)
    val representativeReplacement = representativeMap.set(
        BiRepresentative("alpha", "ignored"),
        BiRepresentative("two", "Two"),
    )
    require(
        representativeReplacement.lookupKey(BiRepresentative("two", "lookup")) ==
            BiMapLookup.Found(storedKey),
    )

    val inverse = replaced.inverse
    require(inverse.lookup("uno") == BiMapLookup.Found(1))
    require(inverse.inverse === replaced)
    require(replaced.inverse === inverse)
    val removedKey = replaced.tryRemoveKey(1)
    require(removedKey.removed && removedKey.opposite == "uno")
    require(!removedKey.map.containsKey(1) && !removedKey.map.containsValue("uno"))
    val removedValue = replaced.tryRemoveValue("two")
    require(removedValue.removed && removedValue.opposite == 2)
    require(replaced.tryRemoveKey(99).map === replaced)

    val nullable = PersistentBiMap.empty<Int, String?>().add(1, null)
    require(nullable.lookup(1) == BiMapLookup.Found(null))
    val nullableRemoval = nullable.tryRemoveKey(1)
    require(nullableRemoval.removed && nullableRemoval.opposite == null)
    val cleared = replaced.clear()
    require(cleared.isEmpty && cleared.keyPolicy === replaced.keyPolicy && cleared.valuePolicy === replaced.valuePolicy)
    require(cleared.clear() === cleared)

    runBiMapModel()

    val throwing = BiThrowingPolicy()
    val failureSource = PersistentBiMap.empty<Int, Int>(defaultHashPolicy(), throwing).add(1, 10)
    throwing.throwOnHash = true
    try {
        failureSource.add(2, 20)
        error("expected injected policy failure")
    } catch (_: IllegalStateException) {
        // Expected from error().
    }
    throwing.throwOnHash = false
    require(failureSource.lookup(1) == BiMapLookup.Found(10) && failureSource.validateStructure())

    val concurrent = PersistentBiMap.from((0 until 512).map { it to it + 10_000 })
    val failures = ConcurrentLinkedQueue<Throwable>()
    val threads = (0 until 4).map {
        Thread {
            try {
                repeat(25) {
                    for (value in 0 until 512) {
                        require(concurrent.lookup(value) == BiMapLookup.Found(value + 10_000))
                        require(concurrent.lookupKey(value + 10_000) == BiMapLookup.Found(value))
                    }
                    require(concurrent.validateStructure())
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }
    }
    threads.forEach(Thread::start)
    threads.forEach(Thread::join)
    require(failures.isEmpty()) { "concurrent bimap reader failure: ${failures.peek()}" }
}

private fun runBiMapModel() {
    var map = PersistentBiMap.empty<Int, Int>()
    val forward = mutableMapOf<Int, Int>()
    val backward = mutableMapOf<Int, Int>()
    val snapshots = mutableListOf<PersistentBiMap<Int, Int>>()
    var state = 0xB1A4D00Du
    repeat(2_000) { step ->
        state = state * 1_664_525u + 1_013_904_223u
        val operation = (state % 4u).toInt()
        state = state * 1_664_525u + 1_013_904_223u
        val key = (state % 32u).toInt()
        state = state * 1_664_525u + 1_013_904_223u
        val value = 100 + (state % 32u).toInt()
        when (operation) {
            0 -> {
                val result = map.tryAdd(key, value)
                val expected = key !in forward && value !in backward
                require(result.added == expected)
                if (expected) {
                    map = result.map
                    forward[key] = value
                    backward[value] = key
                }
            }
            1 -> {
                val owner = backward[value]
                if (owner != null && owner != key) {
                    try {
                        map.set(key, value)
                        error("model expected value conflict")
                    } catch (_: BiMapConflictException) { }
                } else {
                    forward[key]?.let(backward::remove)
                    map = map.set(key, value)
                    forward[key] = value
                    backward[value] = key
                }
            }
            2 -> {
                val old = forward.remove(key)
                val result = map.tryRemoveKey(key)
                require(result.removed == (old != null))
                if (old != null) {
                    backward.remove(old)
                    map = result.map
                }
            }
            else -> {
                val old = backward.remove(value)
                val result = map.tryRemoveValue(value)
                require(result.removed == (old != null))
                if (old != null) {
                    forward.remove(old)
                    map = result.map
                }
            }
        }
        require(map.size == forward.size && map.validateStructure())
        for ((expectedKey, expectedValue) in forward) {
            require(map.lookup(expectedKey) == BiMapLookup.Found(expectedValue))
            require(map.lookupKey(expectedValue) == BiMapLookup.Found(expectedKey))
        }
        if (step % 127 == 0) snapshots.add(map)
    }
    require(snapshots.all(PersistentBiMap<Int, Int>::validateStructure))
}
