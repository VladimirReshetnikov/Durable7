package tools.datastructures.tungsten

import tools.datastructures.hamt.HashPolicy

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

private fun <K, V> checkAssociation(
    expected: List<Pair<K, V>>,
    actual: PersistentAssociation<K, V>,
    message: String,
) {
    checkEquals(expected, actual.toList(), "$message order")
    checkEquals(expected.map { it.first }, actual.keys(), "$message keys")
    checkEquals(expected.map { it.second }, actual.values(), "$message values")
    checkEquals(expected.size, actual.size, "$message size")
    for ((index, pair) in expected.withIndex()) {
        checkEquals(pair, actual.getAt(index), "$message getAt $index")
        checkEquals(index, actual.indexOfKey(pair.first), "$message indexOfKey ${pair.first}")
        checkEquals(pair.second, actual[pair.first], "$message lookup ${pair.first}")
    }
}

private class CaseInsensitivePolicy : HashPolicy<String> {
    override fun hash(key: String): Int = key.lowercase().hashCode()

    override fun equivalent(left: String, right: String): Boolean =
        left.equals(right, ignoreCase = true)
}

private class DeterministicRandom(seed: Long) {
    private var state = seed

    fun next(): Int {
        state = state * 6364136223846793005L + 1442695040888963407L
        return (state ushr 32).toInt()
    }

    fun nextIndex(exclusive: Int): Int =
        if (exclusive == 0) 0 else (next().toUInt().toLong() % exclusive).toInt()

    fun nextInt(low: Int, high: Int): Int = low + nextIndex(high - low + 1)
}

private fun listExamplesMatchTungstenSurface() {
    val list = PersistentList.from(listOf(1, 2, 3))

    checkEquals(1, list.first(), "first")
    checkEquals(3, list.last(), "last")
    checkEquals(2, list[1], "part")
    checkEquals(listOf(1, 2, 3, 4), list.append(4).toList(), "append")
    checkEquals(listOf(0, 1, 2, 3), list.prepend(0).toList(), "prepend")
    checkEquals(listOf(1, 9, 2, 3), list.insert(1, 9)?.toList(), "insert")
    checkEquals(listOf(1, 3), list.removeAt(1)?.toList(), "delete")
    checkEquals(listOf(1, 8, 3), list.setItem(1, 8)?.toList(), "replace part")
    checkEquals(listOf(1, 2), list.take(2)?.toList(), "take")
    checkEquals(listOf(2, 3), list.drop(1)?.toList(), "drop")
    checkEquals(listOf(3, 2, 1), list.reverse().toList(), "reverse")
    checkEquals(listOf(10, 20, 30), list.map { it * 10 }.toList(), "map")
    checkEquals(1, list.indexOf(2), "first position")
    check(list.contains(3), "member")

    val split = list.splitAt(2) ?: throw AssertionError("split")
    checkEquals(listOf(1, 2), split.left.toList(), "split left")
    checkEquals(listOf(3), split.right.toList(), "split right")
}

private fun listGeneratedHistoriesPreserveSnapshots() {
    val random = DeterministicRandom(0x51A7E57L)
    var list = PersistentList.empty<Int>()
    val model = ArrayList<Int>()
    val snapshots = ArrayList<Pair<PersistentList<Int>, List<Int>>>()

    repeat(500) {
        val value = random.nextInt(-1000, 1000)
        when (random.nextIndex(10)) {
            0 -> {
                list = list.append(value)
                model.add(value)
            }
            1 -> {
                list = list.prepend(value)
                model.add(0, value)
            }
            2 -> {
                val index = random.nextIndex(model.size + 1)
                list = list.insert(index, value) ?: throw AssertionError("insert")
                model.add(index, value)
            }
            3 -> if (model.isNotEmpty()) {
                val index = random.nextIndex(model.size)
                list = list.removeAt(index) ?: throw AssertionError("remove")
                model.removeAt(index)
            }
            4 -> if (model.isNotEmpty()) {
                val index = random.nextIndex(model.size)
                list = list.setItem(index, value) ?: throw AssertionError("set")
                model[index] = value
            }
            5 -> {
                val count = random.nextIndex(model.size + 1)
                list = list.take(count) ?: throw AssertionError("take")
                while (model.size > count) {
                    model.removeAt(model.lastIndex)
                }
            }
            6 -> {
                val count = random.nextIndex(model.size + 1)
                list = list.drop(count) ?: throw AssertionError("drop")
                repeat(count) {
                    model.removeAt(0)
                }
            }
            7 -> {
                list = list.reverse()
                model.reverse()
            }
            8 -> {
                list = list.join(PersistentList.from(listOf(value, value + 1)))
                model.add(value)
                model.add(value + 1)
            }
            else -> {
                snapshots.add(list to model.toList())
                if (snapshots.size > 32) {
                    snapshots.removeAt(0)
                }
            }
        }

        checkEquals(model.toList(), list.toList(), "random list model")
    }

    for ((snapshot, expected) in snapshots) {
        checkEquals(expected, snapshot.toList(), "retained list snapshot")
    }
}

private fun associationOrderingExamplesMatchTungstenRules() {
    checkAssociation(
        listOf("a" to 3, "b" to 2),
        PersistentAssociation.from(listOf("a" to 1, "b" to 2, "a" to 3)),
        "duplicate construction",
    )

    val assoc = PersistentAssociation.from(listOf("a" to 1, "b" to 2))
    checkAssociation(listOf("a" to 5, "b" to 2), assoc.setItem("a", 5), "set in place")
    checkAssociation(listOf("a" to 1, "b" to 2, "c" to 3), assoc.setItem("c", 3), "set appends")
    checkAssociation(listOf("b" to 2, "a" to 3), assoc.append("a", 3), "append moves existing")
    checkAssociation(listOf("b" to 3, "a" to 1), assoc.prepend("b", 3), "prepend moves existing")

    val joined = assoc.join(PersistentAssociation.from(listOf("a" to 9, "c" to 4)))
    checkAssociation(listOf("a" to 9, "b" to 2, "c" to 4), joined, "join")
    checkAssociation(listOf("a" to 1, "b" to 2), assoc, "source snapshot")
}

private fun associationPositionOperationsAndSortsMatchModel() {
    val assoc = PersistentAssociation.from(listOf("a" to 1, "b" to 2, "c" to 3))

    checkAssociation(listOf("b" to 2, "a" to 9, "c" to 3), assoc.insert(2, "a", 9)!!, "insert old key")
    checkAssociation(listOf("a" to 1, "c" to 3), assoc.remove("b"), "remove key")
    checkAssociation(listOf("a" to 1, "c" to 3), assoc.removeAt(1)!!, "remove at")
    checkAssociation(listOf("a" to 1, "b" to 2), assoc.take(2)!!, "take")
    checkAssociation(listOf("b" to 2, "c" to 3), assoc.drop(1)!!, "drop")
    checkAssociation(listOf("c" to 3, "b" to 2, "a" to 1), assoc.reverse(), "reverse")
    checkAssociation(listOf("c" to 3, "a" to 1), assoc.keyTake(listOf("c", "missing", "a", "c")), "key take")
    checkAssociation(listOf("b" to 2), assoc.removeRange(listOf("c", "a", "missing")), "key drop")

    val unsorted = PersistentAssociation.from(listOf("b" to 2, "c" to 0, "a" to 1))
    checkAssociation(listOf("a" to 1, "b" to 2, "c" to 0), unsorted.keySort(), "key sort")
    checkAssociation(listOf("c" to 0, "a" to 1, "b" to 2), unsorted.sort(), "value sort")

    val ties = PersistentAssociation.from(listOf("b" to 7, "a" to 7, "c" to 7))
    checkAssociation(listOf("b" to 7, "a" to 7, "c" to 7), ties.sort(), "stable value sort")
}

private fun associationCustomPolicyRecoversStoredKeys() {
    val assoc = PersistentAssociation.empty<String, Int>(CaseInsensitivePolicy())
        .setItem("Alpha", 1)
        .setItem("beta", 2)

    checkEquals(1, assoc["ALPHA"], "case-insensitive lookup")
    checkEquals(0, assoc.indexOfKey("ALPHA"), "case-insensitive index")
    checkEquals("Alpha", assoc.getStoredKey("aLpHa"), "stored key")

    val updated = assoc.setItem("ALPHA", 9)
    checkEquals("Alpha", updated.getAt(0)?.first, "set keeps stored key")

    val appended = assoc.append("ALPHA", 9)
    checkEquals("ALPHA", appended.getAt(1)?.first, "append stores supplied key")

    val taken = assoc.keyTake(listOf("ALPHA"))
    checkEquals("Alpha", taken.getAt(0)?.first, "key take uses stored key")
}

private fun associationRelabelStressPreservesPositionsAndLookups() {
    var assoc = PersistentAssociation.from(listOf("start" to -1, "end" to -2))
    for (index in 0 until 100) {
        assoc = assoc.insert(1, "k$index", index) ?: throw AssertionError("insert")
    }

    checkEquals(102, assoc.size, "relabel size")
    checkEquals("start", assoc.getAt(0)?.first, "first after relabel")
    checkEquals("end", assoc.getAt(101)?.first, "last after relabel")
    for (index in 0 until 100) {
        val key = "k$index"
        checkEquals(index, assoc[key], "lookup $key")
        checkEquals(1 + (99 - index), assoc.indexOfKey(key), "index $key")
    }
}

private fun associationGeneratedHistoriesMatchOrderedModelAndSnapshots() {
    val random = DeterministicRandom(0xA550C1A710BL)
    var assoc = PersistentAssociation.empty<Int, Int>()
    val model = ArrayList<Pair<Int, Int>>()
    val snapshots = ArrayList<Pair<PersistentAssociation<Int, Int>, List<Pair<Int, Int>>>>()

    fun find(key: Int): Int = model.indexOfFirst { it.first == key }

    repeat(700) {
        val key = random.nextInt(-20, 20)
        val value = random.nextInt(-1000, 1000)
        val position = random.nextIndex(model.size + 1)

        when (random.nextIndex(11)) {
            0 -> {
                assoc = assoc.setItem(key, value)
                val found = find(key)
                if (found >= 0) {
                    model[found] = key to value
                } else {
                    model.add(key to value)
                }
            }
            1 -> {
                assoc = assoc.append(key, value)
                val found = find(key)
                if (found >= 0) {
                    model.removeAt(found)
                }
                model.add(key to value)
            }
            2 -> {
                assoc = assoc.prepend(key, value)
                val found = find(key)
                if (found >= 0) {
                    model.removeAt(found)
                }
                model.add(0, key to value)
            }
            3 -> {
                assoc = assoc.remove(key)
                val found = find(key)
                if (found >= 0) {
                    model.removeAt(found)
                }
            }
            4 -> if (model.isNotEmpty()) {
                val index = random.nextIndex(model.size)
                assoc = assoc.removeAt(index) ?: throw AssertionError("removeAt")
                model.removeAt(index)
            }
            5 -> {
                assoc = assoc.insert(position, key, value) ?: throw AssertionError("insert")
                var insertAt = position
                val found = find(key)
                if (found >= 0) {
                    model.removeAt(found)
                    if (found < insertAt) {
                        insertAt--
                    }
                }
                model.add(insertAt, key to value)
            }
            6 -> {
                val count = random.nextIndex(model.size + 1)
                assoc = assoc.take(count) ?: throw AssertionError("take")
                while (model.size > count) {
                    model.removeAt(model.lastIndex)
                }
            }
            7 -> {
                val count = random.nextIndex(model.size + 1)
                assoc = assoc.drop(count) ?: throw AssertionError("drop")
                repeat(count) {
                    model.removeAt(0)
                }
            }
            8 -> {
                assoc = assoc.reverse()
                model.reverse()
            }
            9 -> {
                val other = PersistentAssociation.empty<Int, Int>()
                    .setItem(key, value)
                    .setItem(key + 1, value + 1)
                assoc = assoc.join(other)
                for (pair in other) {
                    val found = find(pair.first)
                    if (found >= 0) {
                        model[found] = pair
                    } else {
                        model.add(pair)
                    }
                }
            }
            else -> {
                snapshots.add(assoc to model.toList())
                if (snapshots.size > 32) {
                    snapshots.removeAt(0)
                }
            }
        }

        checkAssociation(model.toList(), assoc, "generated association")
    }

    for ((snapshot, expected) in snapshots) {
        checkAssociation(expected, snapshot, "retained association snapshot")
    }
}

public fun main() {
    val tests = listOf(
        "listExamplesMatchTungstenSurface" to ::listExamplesMatchTungstenSurface,
        "listGeneratedHistoriesPreserveSnapshots" to ::listGeneratedHistoriesPreserveSnapshots,
        "associationOrderingExamplesMatchTungstenRules" to ::associationOrderingExamplesMatchTungstenRules,
        "associationPositionOperationsAndSortsMatchModel" to ::associationPositionOperationsAndSortsMatchModel,
        "associationCustomPolicyRecoversStoredKeys" to ::associationCustomPolicyRecoversStoredKeys,
        "associationRelabelStressPreservesPositionsAndLookups" to ::associationRelabelStressPreservesPositionsAndLookups,
        "associationGeneratedHistoriesMatchOrderedModelAndSnapshots" to ::associationGeneratedHistoriesMatchOrderedModelAndSnapshots,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
