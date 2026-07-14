package tools.datastructures.hamt

import java.util.Collections
import java.util.Random
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference

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

private class CountingIntPolicy : HashPolicy<Int> {
    var hashCalls: Int = 0
    var equalityCalls: Int = 0
    override fun hash(key: Int): Int { hashCalls++; return key * -0x61c88647 }
    override fun equivalent(left: Int, right: Int): Boolean { equalityCalls++; return left == right }
    fun reset() { hashCalls = 0; equalityCalls = 0 }
}

private class InjectedPolicyFailure : RuntimeException()

private class ThrowingIntPolicy : HashPolicy<Int> {
    var throwOnHash: Boolean = false
    var throwOnEquivalent: Boolean = false

    override fun hash(key: Int): Int {
        if (throwOnHash) throw InjectedPolicyFailure()
        return 0
    }

    override fun equivalent(left: Int, right: Int): Boolean {
        if (throwOnEquivalent) throw InjectedPolicyFailure()
        return left == right
    }
}

private class ThrowingValue(val number: Int) {
    var throwOnEquals: Boolean = false

    override fun equals(other: Any?): Boolean {
        if (throwOnEquals) throw InjectedPolicyFailure()
        return other is ThrowingValue && number == other.number
    }

    override fun hashCode(): Int = number
}

private class ReentrantIntPolicy : HashPolicy<Int> {
    var callback: (() -> Unit)? = null

    override fun hash(key: Int): Int {
        callback?.invoke()
        return key
    }

    override fun equivalent(left: Int, right: Int): Boolean = left == right
}

/**
 * Spreads Int keys across the trie's 5-bit levels so the canonicalization test builds a genuinely
 * sparse trie with unary prefix bridges and single-entry-collapsible branches. Identity hashing on
 * dense keys 0..511 produces a maximally dense two-level trie that structurally cannot contain any
 * shape the canonicalization validator polices (bridge, collision, leaf-as-bitmap-child, under-full
 * node), so the guard would pass vacuously. Every ninth key shares one hash to force a collision
 * run. Mirrors the C# reference's ExplicitHashComparer (`i % 9 == 0 ? 17 : i * 0x01010101`).
 */
private class SpreadingHashPolicy : HashPolicy<Int> {
    override fun hash(key: Int): Int = if (key % 9 == 0) 17 else key * 0x01010101
    override fun equivalent(left: Int, right: Int): Boolean = left == right
}

/** Assigns caller-chosen hashes to specific Int keys so a test can build an exact trie shape. */
private class TableHashPolicy(private val hashes: Map<Int, Int>) : HashPolicy<Int> {
    override fun hash(key: Int): Int = hashes[key] ?: key
    override fun equivalent(left: Int, right: Int): Boolean = left == right
}

/** Forces equivalent and null keys through one collision bucket for Ctrie conversion coverage. */
private class NullableEquivalentKeyPolicy : HashPolicy<EquivalentKey?> {
    override fun hash(key: EquivalentKey?): Int = 0

    override fun equivalent(left: EquivalentKey?, right: EquivalentKey?): Boolean =
        left === right || (left != null && right != null && left.text.equals(right.text, ignoreCase = true))
}

/** Builds an equal-hash collision first, then introduces a hash that diverges at the second level. */
private class CollisionThenSplitPolicy : HashPolicy<Int> {
    override fun hash(key: Int): Int = if (key < 2) 0 else 32
    override fun equivalent(left: Int, right: Int): Boolean = left == right
}

private data class CtrieRepresentativeValue(val text: String)

private class CountingStructuralHashPolicy : HashPolicy<Int> {
    var hashCalls: Int = 0
    var equivalenceCalls: Int = 0

    override fun hash(key: Int): Int {
        hashCalls++
        return key
    }

    override fun equivalent(left: Int, right: Int): Boolean {
        equivalenceCalls++
        return left == right
    }

    fun reset() {
        hashCalls = 0
        equivalenceCalls = 0
    }
}

private class ValueEqualityCounter(var calls: Int = 0)

private class CountedMapValue(
    val number: Int,
    private val equalityCounter: ValueEqualityCounter,
) {
    override fun equals(other: Any?): Boolean {
        equalityCounter.calls++
        return other is CountedMapValue && number == other.number
    }

    override fun hashCode(): Int = number
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

private inline fun <reified T : Throwable> checkThrows(message: String, action: () -> Unit): T {
    try {
        action()
    } catch (error: Throwable) {
        if (error is T) return error
        throw AssertionError("$message Expected ${T::class.java.simpleName}, got ${error::class.java.simpleName}.", error)
    }
    throw AssertionError("$message Expected ${T::class.java.simpleName}.")
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
    // A spreading, partly-colliding hash policy so the canonicalization guards below run against a
    // genuinely sparse trie (unary bridges, collision runs, single-entry-collapsible branches)
    // rather than the maximally dense two-level trie identity hashing on 0..511 would produce.
    val policy = SpreadingHashPolicy()
    val ascending = PersistentHashMap.from((0 until 512).map { it to it }, policy)
    val descending = PersistentHashMap.from((0 until 512).reversed().map { it to it }, policy)
    check(ascending.mapEquals(descending), "independent insertion histories must be semantically equal")
    checkEquals(0, ascending.diff(descending).count(), "equal canonical maps have an empty diff")
    check(ascending.champTopologyEquals(descending), "independent histories must have identical CHAMP topology")

    val statistics = ascending.champStatistics()
    checkEquals(512, statistics.inlinePayloads + statistics.collisionPayloads, "every key is stored as an inline payload or a collision entry")
    check(statistics.collisionPayloads > 0, "spreading policy must exercise collision runs (leaf-vs-collision child handling)")
    check(statistics.bitmapNodes > 1, "test data must exercise nested bitmap nodes")
    checkEquals(0, statistics.invalidLeafChildren, "bitmap child runs must not contain leaf nodes")
    checkEquals(0, statistics.underfullBitmapNodes, "bitmap nodes must be canonically collapsed")
    checkEquals(0, statistics.invalidHashRouting, "payloads and children must follow every stored hash prefix")

    var churned = ascending
    for (key in 0 until 512 step 3) churned = churned.remove(key)
    for (key in (0 until 512 step 3).reversed()) churned = churned.put(key, key)
    check(ascending.champTopologyEquals(churned), "delete/reinsert churn must recover canonical topology")
    val churnStatistics = churned.champStatistics()
    checkEquals(0, churnStatistics.invalidLeafChildren, "churned bitmap child runs must not contain leaves")
    checkEquals(0, churnStatistics.underfullBitmapNodes, "churned bitmap nodes must be canonically collapsed")
    checkEquals(0, churnStatistics.invalidHashRouting, "churned payloads and children retain hash-prefix routing")

    // Non-vacuity guard for the canonical-collapse rule. Build an exact deep two-entry branch under a
    // unary prefix bridge: keys 0 and 1 share the low ten hash bits (fragments 0,0 at shifts 0,5) and
    // diverge at shift 10; key 2 diverges at the root. Removing key 1 leaves the deep branch with a
    // single entry, which canonicalization MUST inline while collapsing the now-unary bridge. If the
    // collapse were skipped, underfullBitmapNodes would be non-zero and the topology would diverge
    // from the direct build of {0, 2} — so these assertions fail loudly on a canonicalization
    // regression, unlike the dense-key data they replace.
    val deepPolicy = TableHashPolicy(mapOf(0 to (1 shl 10), 1 to (2 shl 10), 2 to 1))
    val deep = PersistentHashMap.empty<Int, Int>(deepPolicy).put(0, 0).put(1, 1).put(2, 2)
    check(deep.champStatistics().bitmapNodes >= 3, "deep-collapse fixture must nest a bridge over a two-entry branch")
    val collapsed = deep.remove(1)
    val collapsedStats = collapsed.champStatistics()
    checkEquals(0, collapsedStats.underfullBitmapNodes, "removing one of a deep pair must collapse the branch, not leave an under-full node")
    checkEquals(0, collapsedStats.invalidLeafChildren, "collapse must not leave a leaf as a bitmap child")
    checkEquals(0, collapsedStats.invalidHashRouting, "collapsed bridge preserves hash-prefix routing")
    checkEquals(listOf(0, 2), collapsed.keys().toList().sorted(), "collapse preserves the surviving keys")
    val directBuild = PersistentHashMap.empty<Int, Int>(deepPolicy).put(0, 0).put(2, 2)
    check(directBuild.champTopologyEquals(collapsed), "collapsed topology must equal the direct canonical build")

    // Consume every reachable fragment at the final (shift-30) level, including slot 3.
    val terminalPolicy = TableHashPolicy(
        mapOf(10 to 0, 11 to (1 shl 30), 12 to (1 shl 31), 13 to (3 shl 30)),
    )
    val terminal = PersistentHashMap.empty<Int, Int>(terminalPolicy)
        .put(10, 0)
        .put(11, 1)
        .put(12, 2)
        .put(13, 3)
    val terminalStats = terminal.champStatistics()
    check(terminalStats.bitmapNodes >= 7, "terminal-fragment fixture reaches the shift-30 branch")
    checkEquals(0, terminalStats.invalidHashRouting, "terminal slots 0 through 3 preserve hash-prefix routing")
    checkEquals(3, terminal[13], "terminal slot 3 remains reachable")

    val changed = descending.remove(7).put(9, -9).put(1_000, 1_000)
    val differences = ascending.diff(changed).toList()
    checkEquals(3, differences.size, "typed diff count")
    check(differences.any { it.kind == MapDifferenceKind.REMOVED && it.key == 7 }, "removed difference")
    check(differences.any { it.kind == MapDifferenceKind.CHANGED && it.key == 9 }, "changed difference")
    check(differences.any { it.kind == MapDifferenceKind.ADDED && it.key == 1_000 }, "added difference")
}

private fun champMapEqualsAndDiffPrunePartialSharedSubtrees() {
    val policy = CountingStructuralHashPolicy()
    val equalityCounter = ValueEqualityCounter()
    val values = (0 until 128).map { key -> CountedMapValue(key, equalityCounter) }
    var source = PersistentHashMap.empty<Int, CountedMapValue>(policy)
    for (key in values.indices) source = source.put(key, values[key])
    val changedValue = CountedMapValue(-127, equalityCounter)
    val target = source.put(127, changedValue)
    checkEquals(33, source.champStatistics().bitmapNodes,
        "partial-sharing fixture has one root and thirty-two descendant bitmap subtries")
    check(!source.sharesRootWith(target), "changed map must publish a new root")

    policy.reset()
    equalityCounter.calls = 0
    check(!source.mapEquals(target), "one divergent value makes the maps unequal")
    checkEquals(0, policy.hashCalls, "mapEquals traverses stored bitmap slots without rehashing")
    checkEquals(1, policy.equivalenceCalls,
        "mapEquals prunes every shared descendant before comparing the one changed key")
    checkEquals(1, equalityCounter.calls,
        "mapEquals invokes value equality only for the one changed leaf")

    policy.reset()
    equalityCounter.calls = 0
    val differences = source.diff(target).toList()
    checkEquals(1, differences.size, "partial-shared diff reports only the changed leaf")
    val difference = differences.single()
    checkEquals(MapDifferenceKind.CHANGED, difference.kind, "partial-shared diff kind")
    checkEquals(127, difference.key, "partial-shared diff key")
    check(difference.before === values[127], "diff retains the source value representative")
    check(difference.after === changedValue, "diff retains the target value representative")
    checkEquals(0, policy.hashCalls, "diff traverses stored bitmap slots without rehashing")
    checkEquals(1, policy.equivalenceCalls,
        "diff prunes every shared descendant before comparing the one changed key")
    checkEquals(1, equalityCounter.calls,
        "diff invokes value equality only for the one changed leaf")
}

private fun champTopologyComparatorRejectsDifferentCollisionKeys() {
    val policy = ConstantPolicy<Int>()
    val left = PersistentHashMap.from(listOf(1 to "a", 2 to "b"), policy)
    val sameReversed = PersistentHashMap.from(listOf(2 to "b", 1 to "a"), policy)
    val different = PersistentHashMap.from(listOf(1 to "a", 3 to "c"), policy)

    check(left.champTopologyEquals(sameReversed), "collision topology ignores insertion order")
    check(!left.champTopologyEquals(different), "collision topology compares key contents")
}

private fun champValidatorRejectsMalformedDepthAndBitmapCardinality() {
    val (overDepth, mismatchedBitmap) = champMalformedDiagnosticsForTesting()
    check(overDepth > 0, "over-depth bitmap nodes must be reported before JVM shift-count masking")
    check(mismatchedBitmap > 0, "bitmap/list cardinality mismatches must not be hidden by zip truncation")
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

private fun structuralSetAlgebraPrunesSharedNodesAndMatchesModels() {
    val policy = CountingIntPolicy()
    val basis = PersistentHashSet.from((0 until 2_048).toList(), policy)
    val left = basis.add(-1)
    val right = basis.add(-2)
    policy.reset()

    check(left.union(left) === left, "self union must preserve the set instance")
    check(left.intersect(left) === left, "self intersection must preserve the set instance")
    check(left.except(left).isEmpty, "self difference must be empty")
    check(left.symmetricExcept(left).isEmpty, "self symmetric difference must be empty")
    checkEquals(2_050, left.union(right).size, "shared union count")
    checkEquals(2_048, left.intersect(right).size, "shared intersection count")
    checkEquals(listOf(-1), left.except(right).toList(), "shared difference")
    checkEquals(listOf(-2, -1), left.symmetricExcept(right).toList().sorted(), "shared symmetric difference")
    check(left.isSubsetOf(left) && left.isSupersetOf(left) && left.overlaps(left) && left.setEquals(left),
        "same-type structural relations")
    checkEquals(0, policy.hashCalls, "structural algebra must use stored hashes")
    check(policy.equalityCalls < 256, "shared descendants must be pruned")

    val random = Random(20260712)
    repeat(200) {
        val leftModel = MutableList(random.nextInt(140)) { random.nextInt(401) - 200 }.toSet()
        val rightModel = MutableList(random.nextInt(140)) { random.nextInt(401) - 200 }.toSet()
        val leftSet = PersistentHashSet.from(leftModel, policy)
        val rightSet = PersistentHashSet.from(rightModel, policy)
        checkEquals(leftModel union rightModel, leftSet.union(rightSet).toSet(), "structural union model")
        checkEquals(leftModel intersect rightModel, leftSet.intersect(rightSet).toSet(), "structural intersection model")
        checkEquals(leftModel subtract rightModel, leftSet.except(rightSet).toSet(), "structural difference model")
        checkEquals((leftModel subtract rightModel) union (rightModel subtract leftModel),
            leftSet.symmetricExcept(rightSet).toSet(), "structural symmetric model")
    }
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

private fun transientMapLifecyclePreservesIdentityAndRepresentatives() {
    val policy = EquivalentKeyPolicy()
    val stored = EquivalentKey("alpha", 1)
    val equivalent = EquivalentKey("alpha", 2)
    val source = PersistentHashMap.from(listOf(stored to 10, EquivalentKey("beta", 3) to 20), policy)
    val transient = source.toTransient()
    val iterator = transient.iterator()

    check(transient.policy === policy, "map transient must retain the exact policy object")
    checkEquals(2, transient.size, "map transient exposes active size")
    check(transient.containsKey(equivalent), "map transient uses the retained policy")
    check(transient.getEntry(equivalent)?.key === stored, "map transient returns the first stored key representative")
    check(!transient.put(equivalent, 10), "equal replacement is a logical no-op")
    check(!transient.tryAdd(equivalent, 99), "tryAdd reports an equivalent key")
    checkThrows<DuplicateKeyException>("duplicate add remains atomic") { transient.add(equivalent, 99) }
    check(!transient.remove(EquivalentKey("missing", 4)), "absent removal is a logical no-op")
    check(iterator.hasNext(), "logical no-ops do not invalidate an active iterator")

    val published = transient.persist()
    check(published === source, "clean map publication must return the exact adopted map")
    checkThrows<IllegalStateException>("consumed map size") { transient.size }
    checkThrows<IllegalStateException>("consumed map emptiness") { transient.isEmpty }
    checkThrows<IllegalStateException>("consumed map policy") { transient.policy }
    checkThrows<IllegalStateException>("consumed map lookup") { transient[stored] }
    checkThrows<IllegalStateException>("consumed map contains") { transient.containsKey(stored) }
    checkThrows<IllegalStateException>("consumed map entry lookup") { transient.getEntry(stored) }
    checkThrows<IllegalStateException>("consumed map mutation") { transient.put(stored, 11) }
    checkThrows<IllegalStateException>("consumed map indexed assignment") { transient[stored] = 11 }
    checkThrows<IllegalStateException>("consumed map add") { transient.add(EquivalentKey("new", 5), 50) }
    checkThrows<IllegalStateException>("consumed map tryAdd") { transient.tryAdd(EquivalentKey("new", 5), 50) }
    checkThrows<IllegalStateException>("consumed map remove") { transient.remove(stored) }
    checkThrows<IllegalStateException>("consumed map tryRemove") { transient.tryRemove(stored) }
    checkThrows<IllegalStateException>("consumed map clear") { transient.clear() }
    checkThrows<IllegalStateException>("consumed map enumeration") { transient.entries().toList() }
    checkThrows<IllegalStateException>("consumed map key enumeration") { transient.keys().toList() }
    checkThrows<IllegalStateException>("consumed map value enumeration") { transient.values().toList() }
    checkThrows<IllegalStateException>("consumed map iterator creation") { transient.iterator() }
    checkThrows<IllegalStateException>("consumed map publication") { transient.persist() }
    checkThrows<IllegalStateException>("consumed map iterator alias") { iterator.hasNext() }

    val empty = PersistentHashMap.empty<EquivalentKey, Int>(policy)
    val emptyTransient = empty.toTransient()
    emptyTransient.clear()
    check(emptyTransient.persist() === empty, "clearing an empty adopted map preserves exact identity")

    val factory = PersistentHashMap.createTransient<EquivalentKey, Int>(policy)
    check(factory.policy === policy, "map transient factory retains the policy object")
    val factoryResult = factory.persist()
    check(factoryResult.isEmpty, "map transient factory publishes an empty map")
    check(factoryResult.policy === policy, "factory publication retains policy identity")
}

private fun transientMapPointEditsEnumerationAndFailureAtomicity() {
    val collisionPolicy = ConstantPolicy<String?>()
    val source = PersistentHashMap.empty<String?, String?>(collisionPolicy)
        .put(null, null)
        .put("alpha", "one")
        .put("beta", "two")
    val transient = source.toTransient()

    check(transient.put("alpha", "ONE"), "map transient replacement reports a change")
    check(transient.tryAdd("gamma", null), "map transient tryAdd reports a new key")
    check(!transient.tryAdd("gamma", "duplicate"), "map transient tryAdd rejects a duplicate")
    transient.add("epsilon", "five")
    checkEquals("five", transient["epsilon"], "map transient add publishes into the active session")
    val removedNull = transient.tryRemove(null) ?: throw AssertionError("map transient must remove a stored null key")
    checkEquals(null, removedNull.key, "map transient removal returns the stored null key")
    checkEquals(null, removedNull.value, "map transient removal returns the stored null value")
    check(transient.remove("beta"), "map transient remove reports a stored key")
    check(!transient.remove("missing"), "map transient remove reports a miss")

    val oracle = source.put("alpha", "ONE").put("gamma", null).add("epsilon", "five").remove(null).remove("beta")
    checkEquals(oracle.entries().toList(), transient.entries().toList(), "active enumeration follows persistent CHAMP order")
    checkEquals(oracle.keys().toList(), transient.keys().toList(), "active key enumeration follows persistent CHAMP order")
    checkEquals(oracle.values().toList(), transient.values().toList(), "active value enumeration follows persistent CHAMP order")

    val stale = transient.iterator()
    check(!transient.put("alpha", "ONE"), "equal map replacement remains a no-op")
    check(stale.hasNext(), "a no-op does not invalidate the map iterator")
    check(transient.put("delta", "four"), "new map key changes the session")
    checkThrows<ConcurrentModificationException>("changed map invalidates an iterator") { stale.hasNext() }

    val published = transient.persist()
    checkEquals("one", source["alpha"], "map transient edits do not mutate the adopted source")
    checkEquals("ONE", published["alpha"], "map transient publishes replacement")
    checkEquals("four", published["delta"], "map transient publishes insertion")
    checkEquals("five", published["epsilon"], "map transient publishes add")
    check(!published.containsKey(null), "map transient publishes null-key removal")
    check(!published.containsKey("beta"), "map transient publishes boolean removal")
    check(published.policy === collisionPolicy, "changed map publication retains policy identity")

    val clearTransient = published.toTransient()
    clearTransient.clear()
    clearTransient.clear()
    val cleared = clearTransient.persist()
    check(cleared.isEmpty, "map transient clear publishes an empty map")
    check(cleared.policy === collisionPolicy, "map transient clear retains policy identity")

    val throwingPolicy = ThrowingIntPolicy()
    val failureSource = PersistentHashMap.from(listOf(1 to 10, 2 to 20), throwingPolicy)
    val failureTransient = failureSource.toTransient()
    val expected = failureTransient.entries().toList()
    val failureIterator = failureTransient.iterator()

    throwingPolicy.throwOnHash = true
    checkThrows<InjectedPolicyFailure>("hash failure leaves map transient active") { failureTransient.put(3, 30) }
    throwingPolicy.throwOnHash = false
    checkEquals(expected, failureTransient.entries().toList(), "hash failure leaves map contents unchanged")
    check(failureIterator.hasNext(), "hash failure does not invalidate an iterator")

    throwingPolicy.throwOnEquivalent = true
    checkThrows<InjectedPolicyFailure>("equality failure leaves map transient active") { failureTransient.remove(1) }
    throwingPolicy.throwOnEquivalent = false
    checkEquals(expected, failureTransient.entries().toList(), "equality failure leaves map contents unchanged")
    check(failureTransient.put(3, 30), "map transient remains usable after callback failures")
    checkEquals(3, failureTransient.persist().size, "retry after map callback failure publishes normally")

    val storedValue = ThrowingValue(1)
    val valueTransient = PersistentHashMap.empty<Int, ThrowingValue>().put(1, storedValue).toTransient()
    storedValue.throwOnEquals = true
    checkThrows<InjectedPolicyFailure>("value equality failure leaves map transient active") {
        valueTransient.put(1, ThrowingValue(2))
    }
    storedValue.throwOnEquals = false
    check(valueTransient.getEntry(1)?.value === storedValue, "value equality failure retains the stored value")
    valueTransient[1] = ThrowingValue(2)
    checkEquals(2, valueTransient.persist().getEntry(1)?.value?.number,
        "indexed assignment remains usable after value equality failure")

    val reentrantPolicy = ReentrantIntPolicy()
    val reentrant = PersistentHashMap.empty<Int, Int>(reentrantPolicy).put(1, 10).toTransient()
    reentrantPolicy.callback = { reentrant.put(3, 30) }
    checkThrows<IllegalStateException>("reentrant map mutation is rejected atomically") { reentrant.put(2, 20) }
    reentrantPolicy.callback = null
    checkEquals(listOf(1 to 10), reentrant.entries().map { it.key to it.value }.toList(),
        "reentrant map mutation leaves the session unchanged")
    check(reentrant.put(2, 20), "map session remains usable after rejected reentrancy")
    checkEquals(2, reentrant.persist().size, "reentrant-map retry publishes normally")
}

private fun transientMapDeterministicModelAcrossPublications() {
    val random = Random(0x54524E4D)
    val model = mutableMapOf<Int, Int>()
    var persistent = PersistentHashMap.empty<Int, Int>()

    repeat(16) { epoch ->
        val retained = persistent
        val retainedModel = retained.entries().map { it.key to it.value }.toList().sortedBy { it.first }
        val transient = persistent.toTransient()

        repeat(256) { operation ->
            val key = random.nextInt(96)
            val value = random.nextInt(33) - 16
            when (random.nextInt(6)) {
                0, 1, 2 -> {
                    val changed = !model.containsKey(key) || model[key] != value
                    checkEquals(changed, transient.put(key, value), "map model put result at $epoch/$operation")
                    model[key] = value
                }
                3 -> {
                    val added = !model.containsKey(key)
                    checkEquals(added, transient.tryAdd(key, value), "map model tryAdd result at $epoch/$operation")
                    if (added) model[key] = value
                }
                4 -> {
                    val removed = model.remove(key) != null
                    checkEquals(removed, transient.remove(key), "map model remove result at $epoch/$operation")
                }
                else -> {
                    checkEquals(model[key], transient[key], "map model lookup at $epoch/$operation")
                    if (operation % 127 == 0) {
                        transient.clear()
                        model.clear()
                    }
                }
            }
        }

        checkEquals(retainedModel, retained.entries().map { it.key to it.value }.toList().sortedBy { it.first },
            "map transient preserves retained source at epoch $epoch")
        val expected = model.toList().sortedBy { it.first }
        checkEquals(expected, transient.entries().map { it.key to it.value }.toList().sortedBy { it.first },
            "active map transient matches model at epoch $epoch")
        persistent = transient.persist()
        checkEquals(expected, persistent.entries().map { it.key to it.value }.toList().sortedBy { it.first },
            "published map matches model at epoch $epoch")
    }
}

private fun transientViewsCaptureSnapshotAndVersionAtAcquisition() {
    val mapSource = PersistentHashMap.from(listOf(1 to "one", 2 to "two"))
    val map = mapSource.toTransient()
    val noOpEntries = map.entries()
    val noOpKeys = map.keys()
    val noOpValues = map.values()
    check(!map.put(1, "one"), "map view fixture performs a logical no-op")
    checkEquals(mapSource.entries().toList(), noOpEntries.toList(), "map entries view survives a no-op")
    checkEquals(mapSource.keys().toList(), noOpKeys.toList(), "map keys view survives a no-op")
    checkEquals(mapSource.values().toList(), noOpValues.toList(), "map values view survives a no-op")

    val staleEntries = map.entries()
    val staleKeys = map.keys()
    val staleValues = map.values()
    check(map.put(3, "three"), "map view fixture performs a successful edit")
    checkThrows<ConcurrentModificationException>("pre-edit entries view is version-bound") { staleEntries.toList() }
    checkThrows<ConcurrentModificationException>("pre-edit keys view is version-bound") { staleKeys.toList() }
    checkThrows<ConcurrentModificationException>("pre-edit values view is version-bound") { staleValues.toList() }

    val consumedEntries = map.entries()
    val consumedKeys = map.keys()
    val consumedValues = map.values()
    val mapPublished = map.persist()
    checkEquals("three", mapPublished[3], "map view fixture publishes the successful edit")
    checkThrows<IllegalStateException>("publication consumes an acquired entries view") { consumedEntries.toList() }
    checkThrows<IllegalStateException>("publication consumes an acquired keys view") { consumedKeys.toList() }
    checkThrows<IllegalStateException>("publication consumes an acquired values view") { consumedValues.toList() }

    val setSource = PersistentHashSet.from(listOf(1, 2))
    val set = setSource.toTransient()
    val noOpSetView = set.asSequence()
    check(!set.add(2), "set view fixture performs a logical no-op")
    checkEquals(setSource.toList(), noOpSetView.toList(), "set sequence view survives a no-op")

    val staleSetView = set.asSequence()
    check(set.add(3), "set view fixture performs a successful edit")
    checkThrows<ConcurrentModificationException>("pre-edit set sequence is version-bound") { staleSetView.toList() }

    val consumedSetView = set.asSequence()
    val setPublished = set.persist()
    check(setPublished.contains(3), "set view fixture publishes the successful edit")
    checkThrows<IllegalStateException>("publication consumes an acquired set sequence") { consumedSetView.toList() }
}

private fun transientSetRelationsUseReceiverPolicyAndActiveValues() {
    val moduloPolicy = ModuloTenPolicy()
    val source = PersistentHashSet.from(listOf(1, 2), moduloPolicy)
    val transient = source.toTransient()
    check(transient.add(3), "set relation fixture adds an active-session value")

    check(transient.isSubsetOf(listOf(11, 12, 13, 99)), "transient subset uses receiver policy")
    check(transient.isProperSubsetOf(listOf(11, 12, 13, 99)), "transient proper subset uses receiver policy")
    check(transient.isSupersetOf(listOf(11, 12)), "transient superset uses receiver policy")
    check(transient.isProperSupersetOf(listOf(11, 12)), "transient proper superset uses receiver policy")
    check(transient.overlaps(listOf(42, 99)), "transient overlap uses receiver policy")
    check(transient.setEquals(listOf(11, 12, 13, 23)),
        "transient equality deduplicates argument representatives under receiver policy")
    check(!transient.setEquals(listOf(11, 12)), "transient equality observes active-session additions")
    check(!source.contains(3), "transient relation reads do not mutate the retained source")

    val storedAlpha = EquivalentKey("alpha", 1)
    val storedBeta = EquivalentKey("beta", 2)
    val representativePolicy = EquivalentKeyPolicy()
    val representatives = PersistentHashSet.from(listOf(storedAlpha, storedBeta), representativePolicy).toTransient()
    val alphaProbe = EquivalentKey("alpha", 99)
    val betaProbe = EquivalentKey("beta", 100)
    check(representatives.setEquals(listOf(betaProbe, alphaProbe, EquivalentKey("alpha", 101))),
        "set relation equality uses policy equivalence rather than concrete representative equality")
    check(representatives.isSubsetOf(listOf(alphaProbe, betaProbe)),
        "set relation subset accepts foreign equivalent representatives")
    check(representatives.isSupersetOf(listOf(alphaProbe)),
        "set relation superset accepts a foreign equivalent representative")
    check(representatives.get(alphaProbe) === storedAlpha,
        "set relation queries retain the first concrete representative")
    val representativeResult = representatives.persist()
    check(representativeResult.get(alphaProbe) === storedAlpha,
        "set relation queries publish the same concrete representative")
}

private fun transientSetLifecycleFailureAndDeterministicModel() {
    val representativePolicy = EquivalentKeyPolicy()
    val stored = EquivalentKey("stored", 1)
    val equivalent = EquivalentKey("stored", 2)
    val source = PersistentHashSet.from(listOf(stored, EquivalentKey("other", 3)), representativePolicy)
    val clean = source.toTransient()
    val cleanIterator = clean.iterator()

    check(clean.policy === representativePolicy, "set transient retains policy identity")
    check(clean.get(equivalent) === stored, "set transient returns the first stored representative")
    check(!clean.add(equivalent), "set transient duplicate add is a logical no-op")
    check(!clean.remove(EquivalentKey("missing", 4)), "set transient absent removal is a logical no-op")
    check(cleanIterator.hasNext(), "set logical no-ops preserve active iterators")
    check(clean.persist() === source, "clean set publication returns the exact adopted set")
    checkThrows<IllegalStateException>("consumed set size") { clean.size }
    checkThrows<IllegalStateException>("consumed set emptiness") { clean.isEmpty }
    checkThrows<IllegalStateException>("consumed set policy") { clean.policy }
    checkThrows<IllegalStateException>("consumed set read") { clean.contains(stored) }
    checkThrows<IllegalStateException>("consumed set representative lookup") { clean.get(stored) }
    checkThrows<IllegalStateException>("consumed set mutation") { clean.add(EquivalentKey("new", 5)) }
    checkThrows<IllegalStateException>("consumed set remove") { clean.remove(stored) }
    checkThrows<IllegalStateException>("consumed set clear") { clean.clear() }
    checkThrows<IllegalStateException>("consumed set subset") { clean.isSubsetOf(listOf(stored)) }
    checkThrows<IllegalStateException>("consumed set proper subset") { clean.isProperSubsetOf(listOf(stored)) }
    checkThrows<IllegalStateException>("consumed set superset") { clean.isSupersetOf(listOf(stored)) }
    checkThrows<IllegalStateException>("consumed set proper superset") { clean.isProperSupersetOf(listOf(stored)) }
    checkThrows<IllegalStateException>("consumed set overlap") { clean.overlaps(listOf(stored)) }
    checkThrows<IllegalStateException>("consumed set equality") { clean.setEquals(listOf(stored)) }
    checkThrows<IllegalStateException>("consumed set sequence") { clean.asSequence().toList() }
    checkThrows<IllegalStateException>("consumed set iterator creation") { clean.iterator() }
    checkThrows<IllegalStateException>("consumed set publication") { clean.persist() }
    checkThrows<IllegalStateException>("consumed set iterator alias") { cleanIterator.hasNext() }

    val collisionPolicy = ConstantPolicy<String?>()
    val collision = PersistentHashSet.createTransient<String?>(collisionPolicy)
    check(collision.add(null), "set transient adds null")
    check(collision.add("alpha"), "set transient adds colliding value")
    check(!collision.add("alpha"), "set transient rejects colliding duplicate")
    check(collision.contains(null), "set transient reads stored null")
    val staleCollisionIterator = collision.iterator()
    check(collision.add("beta"), "set transient adds a second colliding value")
    checkThrows<ConcurrentModificationException>("changed set invalidates an iterator") {
        staleCollisionIterator.hasNext()
    }
    check(collision.remove(null), "set transient removes stored null")
    val collisionPublished = collision.persist()
    checkEquals(listOf("alpha", "beta"), collisionPublished.toList(), "set transient publishes collision edits")
    check(collisionPublished.policy === collisionPolicy, "set transient publication retains collision policy")

    val throwingPolicy = ThrowingIntPolicy()
    val failureSource = PersistentHashSet.from(listOf(1, 2), throwingPolicy)
    val failureTransient = failureSource.toTransient()
    val expectedFailureState = failureTransient.toList()
    throwingPolicy.throwOnHash = true
    checkThrows<InjectedPolicyFailure>("set hash failure is atomic") { failureTransient.add(3) }
    throwingPolicy.throwOnHash = false
    checkEquals(expectedFailureState, failureTransient.toList(), "set hash failure leaves contents unchanged")
    throwingPolicy.throwOnEquivalent = true
    checkThrows<InjectedPolicyFailure>("set equality failure is atomic") { failureTransient.remove(1) }
    throwingPolicy.throwOnEquivalent = false
    checkEquals(expectedFailureState, failureTransient.toList(), "set equality failure leaves contents unchanged")
    check(failureTransient.add(3), "set transient remains usable after callback failures")
    checkEquals(3, failureTransient.persist().size, "set retry publishes after callback failure")

    val random = Random(0x54524E53)
    val model = mutableSetOf<Int>()
    var persistent = PersistentHashSet.empty<Int>()
    repeat(16) { epoch ->
        val retained = persistent
        val retainedModel = retained.toList().sorted()
        val transient = persistent.toTransient()
        repeat(256) { operation ->
            val value = random.nextInt(96)
            if (random.nextBoolean()) {
                checkEquals(model.add(value), transient.add(value), "set model add result at $epoch/$operation")
            } else {
                checkEquals(model.remove(value), transient.remove(value), "set model remove result at $epoch/$operation")
            }
            if (operation % 61 == 0) {
                checkEquals(value in model, transient.contains(value), "set model lookup at $epoch/$operation")
            }
        }
        checkEquals(retainedModel, retained.toList().sorted(), "set transient preserves retained source at epoch $epoch")
        checkEquals(model.sorted(), transient.toList().sorted(), "active set transient matches model at epoch $epoch")
        persistent = transient.persist()
        checkEquals(model.sorted(), persistent.toList().sorted(), "published set matches model at epoch $epoch")
    }

    val clearSource = persistent
    val clearSourceContents = clearSource.toList().sorted()
    val clear = clearSource.toTransient()
    clear.clear()
    val stale = clear.iterator()
    clear.clear()
    check(!stale.hasNext(), "clearing an already empty set is an iterator-preserving no-op")
    check(clear.persist().isEmpty, "set transient clear publishes an empty set")
    checkEquals(clearSourceContents, clearSource.toList().sorted(), "set transient clear preserves the retained source")
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

private fun ctrieSnapshotConversionPreservesPolicyRepresentativesOrderAndIsolation() {
    val policy = NullableEquivalentKeyPolicy()
    val firstKey = EquivalentKey("Alpha", 1)
    val equivalentKey = EquivalentKey("ALPHA", 2)
    val finalEquivalentKey = EquivalentKey("alpha", 3)
    val nullValueKey = EquivalentKey("stored-null", 4)
    val initialValue = CtrieRepresentativeValue("initial")
    val winningValue = CtrieRepresentativeValue("winning")
    val equalWinningValue = CtrieRepresentativeValue("winning")
    val nullKeyValue = CtrieRepresentativeValue("null-key")
    val laterValue = CtrieRepresentativeValue("later")
    val postSnapshotKey = EquivalentKey("post-snapshot", 6)
    val trie = ConcurrentHashTrie<EquivalentKey?, CtrieRepresentativeValue?>(policy)

    trie.set(firstKey, initialValue)
    trie.set(equivalentKey, winningValue)
    trie.set(finalEquivalentKey, equalWinningValue)
    trie.set(null, nullKeyValue)
    trie.set(nullValueKey, null)
    val snapshot = trie.snapshot()
    val snapshotEntries = snapshot.toList()

    trie.set(EquivalentKey("aLpHa", 5), laterValue)
    check(trie.remove(null)?.value === nullKeyValue, "live removal returns the stored null-key value")
    trie.set(postSnapshotKey, CtrieRepresentativeValue("new"))
    val generationBeforeConversion = trie.generation

    val persistent = snapshot.toPersistentHashMap()

    check(persistent.policy === policy, "snapshot conversion retains the exact hash-policy object")
    checkEquals(snapshotEntries.size, persistent.size, "snapshot conversion retains every captured entry")
    val equivalentEntry = persistent.getEntry(EquivalentKey("AlPhA", 7))
        ?: throw AssertionError("snapshot conversion lost an equivalent key")
    check(equivalentEntry.key === firstKey, "snapshot conversion retains the first stored key representative")
    check(equivalentEntry.value === winningValue, "equal-value no-op retains the winning value representative")
    val nullKeyEntry = persistent.getEntry(null)
        ?: throw AssertionError("snapshot conversion lost the null key")
    check(nullKeyEntry.key == null, "snapshot conversion retains the null key representative")
    check(nullKeyEntry.value === nullKeyValue, "snapshot conversion retains the null-key value representative")
    val nullValueEntry = persistent.getEntry(EquivalentKey("STORED-NULL", 8))
        ?: throw AssertionError("snapshot conversion lost the present-null entry")
    check(nullValueEntry.key === nullValueKey, "snapshot conversion retains the present-null key representative")
    check(nullValueEntry.value == null, "snapshot conversion distinguishes a present null value from absence")

    val persistentEntries = persistent.toList()
    for (index in snapshotEntries.indices) {
        check(snapshotEntries[index].key === persistentEntries[index].key,
            "snapshot conversion preserves key sequence and identity at index $index")
        check(snapshotEntries[index].value === persistentEntries[index].value,
            "snapshot conversion preserves value sequence and identity at index $index")
    }
    checkEquals(generationBeforeConversion, trie.generation,
        "converting a frozen generation does not publish a live Ctrie update")

    trie.clear()
    check(snapshot.getEntry(EquivalentKey("ALPHA", 9))?.value === winningValue,
        "later live writes do not alter the captured generation")
    check(snapshot.getEntry(null)?.value === nullKeyValue,
        "later live removal does not alter the captured null-key entry")
    check(snapshot.getEntry(nullValueKey) != null,
        "the captured generation retains a present-null entry")
    check(!snapshot.containsKey(postSnapshotKey),
        "the captured generation excludes a later live insertion")
    check(persistent.getEntry(EquivalentKey("alpha", 10))?.value === winningValue,
        "later live writes do not alter the converted CHAMP")
    check(persistent.getEntry(null)?.value === nullKeyValue,
        "later live removal does not alter the converted null-key entry")
    check(persistent.getEntry(nullValueKey) != null,
        "the converted CHAMP retains a present-null entry")
    check(!persistent.containsKey(postSnapshotKey),
        "the converted CHAMP excludes a later live insertion")
}

private fun ctrieSnapshotEnumerationMatchesCanonicalChampAcrossMixedBranchesAndTombs() {
    val trie = ConcurrentHashTrie<Int, String>()
    trie.set(0, "zero")
    trie.set(32, "thirty-two")
    trie.set(1, "one")

    val mixed = trie.snapshot()
    checkEquals(listOf(1, 0, 32), mixed.map { it.key },
        "Ctrie mixed topology emits the CHAMP data run before its child run")
    checkEquals(listOf(1, 0, 32), mixed.toPersistentHashMap().map { it.key },
        "mixed-topology snapshot conversion retains canonical CHAMP order")

    var tombSnapshot: ConcurrentHashTrie.Snapshot<Int, String>? = null
    trie.removalCommittedHookForTesting = { tombSnapshot = trie.snapshot() }
    try {
        checkEquals("thirty-two", trie.remove(32)?.value,
            "removal returns the entry captured before tomb cleanup")
    } finally {
        trie.removalCommittedHookForTesting = null
    }

    val captured = checkNotNull(tombSnapshot)
    checkEquals(listOf(0, 1), captured.map { it.key },
        "a tomb-hidden singleton joins the parent CHAMP data run in bitmap order")
    checkEquals(listOf(0, 1), captured.toPersistentHashMap().map { it.key },
        "tomb-bearing snapshot conversion retains canonical CHAMP order")
}

private class EqualityCountingValue {
    var equalityCalls: Int = 0

    override fun equals(other: Any?): Boolean {
        equalityCalls++
        return this === other
    }

    override fun hashCode(): Int = 0
}

private fun ctrieSameReferenceUpdatesBypassValueEquality() {
    val trie = ConcurrentHashTrie<String, EqualityCountingValue>()
    val value = EqualityCountingValue()
    trie.set("key", value)
    val generation = trie.generation

    trie.set("key", value)
    check(trie.compute("key", { value }, { _, _ -> value }) === value,
        "same-reference compute returns the stored representative")

    checkEquals(generation, trie.generation, "same-reference Ctrie updates do not publish")
    checkEquals(0, value.equalityCalls, "same-reference Ctrie updates bypass value equality")
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

private fun ctrieCollisionNodeSplitsForDifferentFullHash() {
    val trie = ConcurrentHashTrie<Int, String>(CollisionThenSplitPolicy())
    trie.set(0, "zero")
    trie.set(1, "one")
    val collision = trie.validateStructureForTesting()
    checkEquals(2, collision.entryCount, "equal hashes create a two-entry collision node")
    checkEquals(1, collision.collisionNodeCount, "equal hashes share one collision node")

    trie.set(2, "two")

    checkEquals("zero", trie[0], "collision re-split retains the first entry")
    checkEquals("one", trie[1], "collision re-split retains the second entry")
    checkEquals("two", trie[2], "collision re-split publishes the distinct-hash entry")
    val split = trie.validateStructureForTesting()
    checkEquals(3, split.entryCount, "collision re-split retains all entries")
    checkEquals(1, split.collisionNodeCount, "collision re-split retains the equal-hash bucket")
    checkEquals(0, split.tombNodeCount, "collision re-split introduces no tomb")
}

private fun ctrieSnapshotDoesNotLoseCommittedWriter() {
    val trie = ConcurrentHashTrie<Int, Int>()
    val mainRead = CountDownLatch(1)
    val releaseSnapshot = CountDownLatch(1)
    val firstInvocation = AtomicInteger(1)
    val captured = AtomicReference<ConcurrentHashTrie.Snapshot<Int, Int>?>()
    val failure = AtomicReference<Throwable?>()
    trie.snapshotMainReadHookForTesting = {
        if (firstInvocation.getAndSet(0) == 1) {
            mainRead.countDown()
            check(releaseSnapshot.await(30, TimeUnit.SECONDS)) { "snapshot race release timed out" }
        }
    }

    val thread = Thread {
        try {
            captured.set(trie.snapshot())
        } catch (error: Throwable) {
            failure.set(error)
        }
    }.apply { name = "ctrie-snapshot-rdcss" }
    thread.start()
    try {
        check(mainRead.await(30, TimeUnit.SECONDS)) { "snapshot did not reach the post-main-read hook" }
        trie.set(42, 420)
    } finally {
        releaseSnapshot.countDown()
    }
    thread.join(30_000)
    trie.snapshotMainReadHookForTesting = null
    check(!thread.isAlive) { "snapshot race thread did not terminate" }
    failure.get()?.let { throw AssertionError("snapshot race failed", it) }
    val snapshot = captured.get() ?: throw AssertionError("snapshot race produced no snapshot")
    checkEquals(420, snapshot[42], "snapshot linearizes after the racing committed writer")
    checkEquals(420, trie[42], "live trie retains the racing committed writer")
    val statistics = trie.validateStructureForTesting()
    checkEquals(1, statistics.entryCount, "snapshot race entry count")
    checkEquals(0, statistics.tombNodeCount, "snapshot race leaves no tomb")
}

private fun ctrieSnapshotExcludesWriterAfterRootAdvance() {
    val trie = ConcurrentHashTrie<Int, Int>()
    trie.set(1, 10)
    val rootAdvanced = CountDownLatch(1)
    val releaseSnapshot = CountDownLatch(1)
    val firstInvocation = AtomicInteger(1)
    val captured = AtomicReference<ConcurrentHashTrie.Snapshot<Int, Int>?>()
    val failure = AtomicReference<Throwable?>()
    trie.snapshotRootAdvancedHookForTesting = {
        if (firstInvocation.getAndSet(0) == 1) {
            rootAdvanced.countDown()
            check(releaseSnapshot.await(30, TimeUnit.SECONDS)) { "snapshot root-advance release timed out" }
        }
    }

    val thread = Thread {
        try {
            captured.set(trie.snapshot())
        } catch (error: Throwable) {
            failure.set(error)
        }
    }.apply { name = "ctrie-snapshot-after-root-advance" }
    thread.start()
    try {
        check(rootAdvanced.await(30, TimeUnit.SECONDS)) { "snapshot did not advance the root" }
        trie.set(42, 420)
    } finally {
        releaseSnapshot.countDown()
    }
    thread.join(30_000)
    trie.snapshotRootAdvancedHookForTesting = null
    check(!thread.isAlive) { "snapshot root-advance race thread did not terminate" }
    failure.get()?.let { throw AssertionError("snapshot root-advance race failed", it) }
    val snapshot = captured.get() ?: throw AssertionError("snapshot root-advance race produced no snapshot")
    checkEquals(null, snapshot[42], "snapshot excludes the writer that linearized after its root advance")
    checkEquals(420, trie[42], "live trie retains the post-snapshot writer")
    checkEquals(10, snapshot[1], "snapshot retains its preexisting entry")
}

private fun ctrieReaderHelpsInstalledGcas() {
    val trie = ConcurrentHashTrie<Int, Int>()
    val installed = CountDownLatch(1)
    val releaseWriter = CountDownLatch(1)
    val firstInvocation = AtomicInteger(1)
    val failure = AtomicReference<Throwable?>()
    trie.gcasInstalledHookForTesting = {
        if (firstInvocation.getAndSet(0) == 1) {
            installed.countDown()
            check(releaseWriter.await(30, TimeUnit.SECONDS)) { "GCAS writer release timed out" }
        }
    }
    val writer = Thread {
        try {
            trie.set(7, 70)
        } catch (error: Throwable) {
            failure.set(error)
        }
    }.apply { name = "ctrie-gcas-writer" }
    writer.start()
    try {
        check(installed.await(30, TimeUnit.SECONDS)) { "writer did not install a GCAS descriptor" }
        checkEquals(70, trie[7], "reader helps the installed GCAS descriptor")
    } finally {
        releaseWriter.countDown()
    }
    writer.join(30_000)
    trie.gcasInstalledHookForTesting = null
    check(!writer.isAlive) { "GCAS writer did not terminate" }
    failure.get()?.let { throw AssertionError("GCAS writer failed", it) }
    checkEquals(70, trie[7], "helped GCAS remains committed")
    val statistics = trie.validateStructureForTesting()
    checkEquals(1, statistics.entryCount, "helped GCAS structure entry count")
    checkEquals(0, statistics.tombNodeCount, "helped GCAS leaves no tomb")
}

private fun ctrieRemovalContractsDeepTombs() {
    val trie = ConcurrentHashTrie<Int, Int>()
    val deepKey = 1 shl 30
    repeat(512) { iteration ->
        trie.set(0, iteration)
        trie.set(deepKey, iteration)
        checkEquals(iteration, trie.remove(deepKey)?.value, "deep-key removal")
        val singleton = trie.validateStructureForTesting()
        checkEquals(1, singleton.entryCount, "deep contraction retains the survivor")
        checkEquals(0, singleton.tombNodeCount, "deep contraction cleans tombs")
        checkEquals(1, singleton.indirectionNodeCount, "deep contraction pulls the survivor to the root")
        checkEquals(iteration, trie.remove(0)?.value, "survivor removal")
        val empty = trie.validateStructureForTesting()
        checkEquals(0, empty.entryCount, "empty contraction entry count")
        checkEquals(0, empty.tombNodeCount, "empty contraction cleans tombs")
        checkEquals(1, empty.indirectionNodeCount, "empty contraction retains only the root indirection")
    }

    val collisions = ConcurrentHashTrie<Int, Int>(ConstantPolicy())
    repeat(32) { collisions.set(it, it) }
    repeat(31) { checkEquals(it, collisions.remove(it)?.value, "collision contraction removal") }
    val collisionSingleton = collisions.validateStructureForTesting()
    checkEquals(1, collisionSingleton.entryCount, "collision contraction survivor")
    checkEquals(0, collisionSingleton.collisionNodeCount, "singleton collision node is eliminated")
    checkEquals(0, collisionSingleton.tombNodeCount, "collision contraction cleans tombs")
}

private fun ctrieMixedShortHistoriesAreLinearizable() {
    val random = Random(0x43545249)
    repeat(250) { round ->
        val trie = ConcurrentHashTrie<Int, Int>(CtrieScenarioPolicy(round % 3))
        val initial = mutableMapOf<Int, Int>()
        repeat(random.nextInt(3)) { key ->
            val value = random.nextInt(8)
            initial[key] = value
            trie.set(key, value)
        }
        val operations = List(5) { CtrieHistoryOperation.create(random) }
        val start = CountDownLatch(1)
        val clock = AtomicInteger()
        val failures = Collections.synchronizedList(mutableListOf<Throwable>())
        val threads = operations.mapIndexed { index, operation ->
            Thread {
                try {
                    start.await()
                    operation.start = clock.incrementAndGet()
                    operation.execute(trie)
                    operation.end = clock.incrementAndGet()
                } catch (error: Throwable) {
                    failures.add(error)
                }
            }.apply { name = "ctrie-linearizability-$round-$index" }
        }
        threads.forEach { it.start() }
        start.countDown()
        threads.forEach { it.join() }
        if (failures.isNotEmpty()) {
            throw AssertionError("Ctrie linearizability round $round had a worker failure", failures.first())
        }
        val final = trie.snapshot().associate { it.key to it.value }
        check(hasCtrieLinearization(operations, initial, final, 0)) {
            "Non-linearizable Ctrie history in round $round. Initial=$initial operations=$operations final=$final"
        }
        val statistics = trie.validateStructureForTesting()
        checkEquals(final.size, statistics.entryCount, "linearizability structure entry count")
        checkEquals(0, statistics.tombNodeCount, "linearizability structure tomb count")
    }
}

private fun hasCtrieLinearization(
    operations: List<CtrieHistoryOperation>,
    state: Map<Int, Int>,
    final: Map<Int, Int>,
    completedMask: Int,
): Boolean {
    val allCompleted = (1 shl operations.size) - 1
    if (completedMask == allCompleted) return state == final
    for (candidate in operations.indices) {
        val candidateBit = 1 shl candidate
        if (completedMask and candidateBit != 0 ||
            hasUncompletedCtriePredecessor(operations, candidate, completedMask)) {
            continue
        }
        val next = state.toMutableMap()
        if (!tryApplyCtrieOperation(operations[candidate], next)) continue
        if (hasCtrieLinearization(operations, next, final, completedMask or candidateBit)) return true
    }
    return false
}

private fun hasUncompletedCtriePredecessor(
    operations: List<CtrieHistoryOperation>,
    candidate: Int,
    completedMask: Int,
): Boolean = operations.indices.any { index ->
    completedMask and (1 shl index) == 0 && operations[index].end < operations[candidate].start
}

private fun tryApplyCtrieOperation(operation: CtrieHistoryOperation, state: MutableMap<Int, Int>): Boolean =
    when (operation.kind) {
        CtrieOperationKind.SET -> {
            state[operation.key] = operation.argument
            true
        }
        CtrieOperationKind.TRY_ADD -> {
            val expected = !state.containsKey(operation.key)
            if (expected) state[operation.key] = operation.argument
            operation.booleanResult == expected
        }
        CtrieOperationKind.REMOVE -> {
            val expected = state.remove(operation.key)
            operation.booleanResult == (expected != null) &&
                (expected == null || operation.valueResult == expected)
        }
        CtrieOperationKind.READ -> {
            val expected = state[operation.key]
            operation.booleanResult == (expected != null) &&
                (expected == null || operation.valueResult == expected)
        }
        CtrieOperationKind.GET_OR_PUT -> {
            val expected = state.getOrPut(operation.key) { operation.argument }
            operation.valueResult == expected
        }
        CtrieOperationKind.COMPUTE -> {
            val expected = state[operation.key]?.plus(operation.comparison) ?: operation.argument
            state[operation.key] = expected
            operation.valueResult == expected
        }
        CtrieOperationKind.SNAPSHOT -> operation.snapshotResult == state
        CtrieOperationKind.CLEAR -> {
            state.clear()
            true
        }
    }

private enum class CtrieOperationKind { SET, TRY_ADD, REMOVE, READ, GET_OR_PUT, COMPUTE, SNAPSHOT, CLEAR }

private class CtrieHistoryOperation(
    val kind: CtrieOperationKind,
    val key: Int,
    val argument: Int,
    val comparison: Int,
) {
    var start: Int = 0
    var end: Int = 0
    var booleanResult: Boolean = false
    var valueResult: Int? = null
    var snapshotResult: Map<Int, Int>? = null

    fun execute(trie: ConcurrentHashTrie<Int, Int>) {
        when (kind) {
            CtrieOperationKind.SET -> trie.set(key, argument)
            CtrieOperationKind.TRY_ADD -> booleanResult = trie.tryAdd(key, argument)
            CtrieOperationKind.REMOVE -> {
                val removed = trie.remove(key)
                booleanResult = removed != null
                valueResult = removed?.value
            }
            CtrieOperationKind.READ -> {
                val entry = trie.getEntry(key)
                booleanResult = entry != null
                valueResult = entry?.value
            }
            CtrieOperationKind.GET_OR_PUT -> valueResult = trie.getOrPut(key) { argument }
            CtrieOperationKind.COMPUTE -> valueResult = trie.compute(
                key,
                add = { argument },
                update = { _, current -> current + comparison },
            )
            CtrieOperationKind.SNAPSHOT -> snapshotResult = trie.snapshot().associate { it.key to it.value }
            CtrieOperationKind.CLEAR -> trie.clear()
        }
    }

    override fun toString(): String =
        "[$start,$end] $kind(key=$key,arg=$argument,cmp=$comparison)" +
            " => success=$booleanResult value=$valueResult snapshot=$snapshotResult"

    companion object {
        fun create(random: Random): CtrieHistoryOperation = CtrieHistoryOperation(
            CtrieOperationKind.entries[random.nextInt(CtrieOperationKind.entries.size)],
            random.nextInt(4),
            random.nextInt(8),
            random.nextInt(3) + 1,
        )
    }
}

private class CtrieScenarioPolicy(private val scenario: Int) : HashPolicy<Int> {
    override fun hash(key: Int): Int = when (scenario) {
        0 -> key
        1 -> key shl 5
        else -> 0
    }

    override fun equivalent(left: Int, right: Int): Boolean = left == right
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
        "champMapEqualsAndDiffPrunePartialSharedSubtrees" to ::champMapEqualsAndDiffPrunePartialSharedSubtrees,
        "champTopologyComparatorRejectsDifferentCollisionKeys" to ::champTopologyComparatorRejectsDifferentCollisionKeys,
        "champValidatorRejectsMalformedDepthAndBitmapCardinality" to ::champValidatorRejectsMalformedDepthAndBitmapCardinality,
        "iterationStreamsTrieOrder" to ::iterationStreamsTrieOrder,
        "setItemsAreLastWinsAndRetainOriginalKey" to ::setItemsAreLastWinsAndRetainOriginalKey,
        "setAlgebraUsesSetMembership" to ::setAlgebraUsesSetMembership,
        "structuralSetAlgebraPrunesSharedNodesAndMatchesModels" to ::structuralSetAlgebraPrunesSharedNodesAndMatchesModels,
        "crossPolicyRelationsUseReceiverPolicy" to ::crossPolicyRelationsUseReceiverPolicy,
        "concurrentReadersObserveConsistentSnapshots" to ::concurrentReadersObserveConsistentSnapshots,
        "transientMapLifecyclePreservesIdentityAndRepresentatives" to ::transientMapLifecyclePreservesIdentityAndRepresentatives,
        "transientMapPointEditsEnumerationAndFailureAtomicity" to ::transientMapPointEditsEnumerationAndFailureAtomicity,
        "transientMapDeterministicModelAcrossPublications" to ::transientMapDeterministicModelAcrossPublications,
        "transientViewsCaptureSnapshotAndVersionAtAcquisition" to ::transientViewsCaptureSnapshotAndVersionAtAcquisition,
        "transientSetRelationsUseReceiverPolicyAndActiveValues" to ::transientSetRelationsUseReceiverPolicyAndActiveValues,
        "transientSetLifecycleFailureAndDeterministicModel" to ::transientSetLifecycleFailureAndDeterministicModel,
        "ctrieSnapshotsAndAtomicUpdates" to ::ctrieSnapshotsAndAtomicUpdates,
        "ctrieSnapshotConversionPreservesPolicyRepresentativesOrderAndIsolation" to
            ::ctrieSnapshotConversionPreservesPolicyRepresentativesOrderAndIsolation,
        "ctrieSnapshotEnumerationMatchesCanonicalChampAcrossMixedBranchesAndTombs" to
            ::ctrieSnapshotEnumerationMatchesCanonicalChampAcrossMixedBranchesAndTombs,
        "ctrieSameReferenceUpdatesBypassValueEquality" to ::ctrieSameReferenceUpdatesBypassValueEquality,
        "ctrieContentionAndGenerationRenewal" to ::ctrieContentionAndGenerationRenewal,
        "ctrieCollisionNodesRemainStable" to ::ctrieCollisionNodesRemainStable,
        "ctrieCollisionNodeSplitsForDifferentFullHash" to ::ctrieCollisionNodeSplitsForDifferentFullHash,
        "ctrieSnapshotDoesNotLoseCommittedWriter" to ::ctrieSnapshotDoesNotLoseCommittedWriter,
        "ctrieSnapshotExcludesWriterAfterRootAdvance" to ::ctrieSnapshotExcludesWriterAfterRootAdvance,
        "ctrieReaderHelpsInstalledGcas" to ::ctrieReaderHelpsInstalledGcas,
        "ctrieRemovalContractsDeepTombs" to ::ctrieRemovalContractsDeepTombs,
        "ctrieMixedShortHistoriesAreLinearizable" to ::ctrieMixedShortHistoriesAreLinearizable,
        "patriciaMapsAndSetsPreserveSignedOrder" to ::patriciaMapsAndSetsPreserveSignedOrder,
        "patriciaCombiningCountsAndNoOps" to ::patriciaCombiningCountsAndNoOps,
        "merkleSearchTreeCoreAndWire" to ::runMerkleSearchTreeTests,
        "merklePersistenceProofSyncAndMerge" to ::runMerklePersistenceTests,
    )

    for ((name, test) in tests) {
        test()
        println("PASS $name")
    }
}
