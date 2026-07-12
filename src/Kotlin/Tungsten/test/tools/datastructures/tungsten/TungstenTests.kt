package tools.datastructures.tungsten

import tools.datastructures.hamt.HashPolicy
import java.util.Collections

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
        check(list.debugSequenceIsBalanced(), "random list SeqTree balance")
    }

    for ((snapshot, expected) in snapshots) {
        checkEquals(expected, snapshot.toList(), "retained list snapshot")
    }
}

private fun seqTreeMaintainsAvlBoundThroughLargeSplits() {
    val count = 20_000
    var list = PersistentList.from(0 until count)
    check(list.debugSequenceIsBalanced(), "initial 20k SeqTree balance")

    repeat(2_000) { step ->
        val index = (step * 7919) % (count + 1)
        val split = list.splitAt(index) ?: throw AssertionError("large split")
        check(split.left.debugSequenceIsBalanced(), "large split left balance at $step")
        check(split.right.debugSequenceIsBalanced(), "large split right balance at $step")
        list = split.left.join(PersistentList.from(listOf(-step - 1))).join(split.right)
        check(list.debugSequenceIsBalanced(), "large join balance at $step")
        list = list.removeAt(index) ?: throw AssertionError("remove inserted pivot")
        check(list.debugSequenceIsBalanced(), "large removal balance at $step")
    }

    checkEquals((0 until count).toList(), list.toList(), "large split/join content")
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

private class CountingAssociationValue(private val value: Int) {
    var equalityCalls: Int = 0

    override fun equals(other: Any?): Boolean {
        equalityCalls++
        return other is CountingAssociationValue && value == other.value
    }

    override fun hashCode(): Int = value
}

private fun associationSameReferenceUpdatesBypassValueEquality() {
    val value = CountingAssociationValue(1)
    val association = PersistentAssociation.empty<String, CountingAssociationValue>()
        .setItem("a", value)
        .setItem("b", CountingAssociationValue(2))

    association.setItem("a", value)
    checkEquals(0, value.equalityCalls, "setItem same-reference equality calls")

    association.append("a", value)
    checkEquals(0, value.equalityCalls, "append same-reference equality calls")

    association.prepend("a", value)
    checkEquals(0, value.equalityCalls, "prepend same-reference equality calls")
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
        check(assoc.debugSequenceIsBalanced(), "generated association SeqTree balance")
    }

    for ((snapshot, expected) in snapshots) {
        checkAssociation(expected, snapshot, "retained association snapshot")
    }
}

private fun iteratorsStreamInOrderAfterMixedEdits() {
    val list = PersistentList.from(listOf(1, 2, 3, 4, 5))
        .prepend(0)
        .append(6)
        .insert(3, 99)!!
        .removeAt(5)!!
        .setItem(0, -1)!!
    val expectedList = listOf(-1, 1, 2, 99, 3, 5, 6)

    val listIterated = ArrayList<Int>()
    for (value in list) {
        listIterated.add(value)
    }
    checkEquals(expectedList, listIterated, "list iterator order")
    checkEquals(list.toList(), listIterated, "list iterator matches toList")

    val listPrefix = ArrayList<Int>()
    val listIterator = list.iterator()
    repeat(3) { listPrefix.add(listIterator.next()) }
    checkEquals(expectedList.take(3), listPrefix, "list iterator early termination")
    check(listIterator.hasNext(), "list iterator continues after prefix")

    val association = PersistentAssociation.empty<String, Int>()
        .setItem("a", 1)
        .setItem("b", 2)
        .setItem("c", 3)
        .prepend("d", 4)
        .insert(2, "e", 5)!!
        .remove("b")
        .setItem("a", 10)
        .append("d", 40)
    val expectedPairs = listOf("a" to 10, "e" to 5, "c" to 3, "d" to 40)

    val associationIterated = ArrayList<Pair<String, Int>>()
    for (pair in association) {
        associationIterated.add(pair)
    }
    checkEquals(expectedPairs, associationIterated, "association iterator order")
    checkEquals(association.toList(), associationIterated, "association iterator matches toList")

    val pairPrefix = ArrayList<Pair<String, Int>>()
    val pairIterator = association.iterator()
    repeat(2) { pairPrefix.add(pairIterator.next()) }
    checkEquals(expectedPairs.take(2), pairPrefix, "association iterator early termination")
    check(pairIterator.hasNext(), "association iterator continues after prefix")
}

private fun concurrentReadersObserveConsistentSnapshots() {
    val expectedList = (0 until 256).toList()
    val list = PersistentList.from(expectedList)
    val expectedAssociation = (0 until 128).map { it to -it }
    val association = PersistentAssociation.from(expectedAssociation)

    runConcurrent("tungsten-readers") {
        repeat(128) {
            checkEquals(256, list.size, "concurrent list size")
            checkEquals(128, list[128], "concurrent list index")
            checkEquals(expectedList, list.toList(), "concurrent list contents")
            checkAssociation(expectedAssociation, association, "concurrent association")
            checkEquals(-96, association[96], "concurrent association lookup")
            checkEquals(96, association.indexOfKey(96), "concurrent association index")
        }
    }
}

public fun main() {
    val tests = listOf(
        "listExamplesMatchTungstenSurface" to ::listExamplesMatchTungstenSurface,
        "listGeneratedHistoriesPreserveSnapshots" to ::listGeneratedHistoriesPreserveSnapshots,
        "seqTreeMaintainsAvlBoundThroughLargeSplits" to ::seqTreeMaintainsAvlBoundThroughLargeSplits,
        "associationOrderingExamplesMatchTungstenRules" to ::associationOrderingExamplesMatchTungstenRules,
        "associationSameReferenceUpdatesBypassValueEquality" to ::associationSameReferenceUpdatesBypassValueEquality,
        "associationPositionOperationsAndSortsMatchModel" to ::associationPositionOperationsAndSortsMatchModel,
        "associationCustomPolicyRecoversStoredKeys" to ::associationCustomPolicyRecoversStoredKeys,
        "associationRelabelStressPreservesPositionsAndLookups" to ::associationRelabelStressPreservesPositionsAndLookups,
        "associationGeneratedHistoriesMatchOrderedModelAndSnapshots" to ::associationGeneratedHistoriesMatchOrderedModelAndSnapshots,
        "iteratorsStreamInOrderAfterMixedEdits" to ::iteratorsStreamInOrderAfterMixedEdits,
        "concurrentReadersObserveConsistentSnapshots" to ::concurrentReadersObserveConsistentSnapshots,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
