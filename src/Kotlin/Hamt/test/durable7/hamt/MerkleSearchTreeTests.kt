package durable7.hamt

import java.util.TreeMap
import java.util.UUID
import java.util.concurrent.atomic.AtomicReference
import kotlin.random.Random

private fun intStringPolicy(
    id: String = "golden-int-string-v1",
): MerkleSearchTreePolicy<Int, String?> = MerkleSearchTreePolicy.natural(
    id,
    Int32MerkleCodec,
    NullableUtf8MerkleCodec,
)

private fun codecAndDigestVectorsAreStrict(): Unit {
    mstBytes(byteArrayOf(1, 2, 3, 4), Int32MerkleCodec.encode(0x01020304), "i32 bytes")
    mstEquals(Int.MIN_VALUE, Int32MerkleCodec.decode(byteArrayOf(0x80.toByte(), 0, 0, 0)), "i32 min")
    mstEquals(-1, Int32MerkleCodec.decode(ByteArray(4) { 0xff.toByte() }), "i32 minus one")
    mstThrows<MerkleCodecException> { Int32MerkleCodec.decode(ByteArray(3)) }
    mstThrows<MerkleCodecException> { Int32MerkleCodec.decode(ByteArray(5)) }

    mstBytes(
        byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8),
        Int64MerkleCodec.encode(0x0102030405060708L),
        "i64 bytes",
    )
    mstEquals(-1L, Int64MerkleCodec.decode(ByteArray(8) { 0xff.toByte() }), "i64 minus one")
    mstThrows<MerkleCodecException> { Int64MerkleCodec.decode(ByteArray(7)) }

    val text = "Aé😀"
    val textBytes = byteArrayOf(1, 0x41, 0xc3.toByte(), 0xa9.toByte(), 0xf0.toByte(), 0x9f.toByte(), 0x98.toByte(), 0x80.toByte())
    mstBytes(textBytes, NullableUtf8MerkleCodec.encode(text), "strict UTF-8 bytes")
    mstEquals(text, NullableUtf8MerkleCodec.decode(textBytes), "strict UTF-8 round trip")
    mstBytes(byteArrayOf(0), NullableUtf8MerkleCodec.encode(null), "nullable UTF-8 null")
    mstEquals(null, NullableUtf8MerkleCodec.decode(byteArrayOf(0)), "nullable UTF-8 decode null")
    for (malformed in listOf(
        byteArrayOf(),
        byteArrayOf(0, 0),
        byteArrayOf(2),
        byteArrayOf(1, 0xc0.toByte(), 0x80.toByte()),
        byteArrayOf(1, 0xe2.toByte(), 0x82.toByte()),
        byteArrayOf(1, 0xed.toByte(), 0xa0.toByte(), 0x80.toByte()),
    )) mstThrows<MerkleCodecException> { NullableUtf8MerkleCodec.decode(malformed) }
    mstThrows<MerkleCodecException> { NullableUtf8MerkleCodec.encode("\uD800") }

    val payload = byteArrayOf(0, 1, 0xff.toByte())
    mstBytes(byteArrayOf(1, 0, 1, 0xff.toByte()), NullableBytesMerkleCodec.encode(payload), "nullable bytes")
    mstBytes(payload, checkNotNull(NullableBytesMerkleCodec.decode(byteArrayOf(1, 0, 1, 0xff.toByte()))), "nullable bytes round trip")
    mstEquals(null, NullableBytesMerkleCodec.decode(byteArrayOf(0)), "nullable bytes null")
    mstThrows<MerkleCodecException> { NullableBytesMerkleCodec.decode(byteArrayOf(0, 1)) }

    val uuid = UUID.fromString("00112233-4455-6677-8899-aabbccddeeff")
    mstEquals("00112233445566778899aabbccddeeff", mstHex(Rfc4122UuidMerkleCodec.encode(uuid)), "UUID network bytes")
    mstEquals(uuid, Rfc4122UuidMerkleCodec.decode(mstUnhex("00112233445566778899aabbccddeeff")), "UUID round trip")
    mstThrows<MerkleCodecException> { Rfc4122UuidMerkleCodec.decode(ByteArray(15)) }

    val digestBytes = ByteArray(32) { it.toByte() }
    val digest = MerkleDigest.fromBytes(digestBytes)
    val hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    mstEquals(hex, digest.toString(), "digest lowercase hex")
    mstEquals(digest, MerkleDigest.fromHex(hex.uppercase()), "digest uppercase parse")
    mstBytes(digestBytes, digest.toByteArray(), "digest bytes")
    mstThrows<IllegalArgumentException> { MerkleDigest.fromBytes(ByteArray(31)) }
    mstThrows<IllegalArgumentException> { MerkleDigest.fromHex("00") }
    mstThrows<IllegalArgumentException> { MerkleDigest.fromHex("x" + hex.drop(1)) }
    val destination = ByteArray(40) { 0x55 }
    mstCheck(digest.tryWriteBytes(destination), "digest write should fit")
    mstBytes(digestBytes, destination.copyOfRange(0, 32), "digest write prefix")
    mstBytes(ByteArray(8) { 0x55 }, destination.copyOfRange(32, 40), "digest write suffix")
    val short = ByteArray(31) { 0x66 }
    mstCheck(!digest.tryWriteBytes(short), "short digest write must fail")
    mstBytes(ByteArray(31) { 0x66 }, short, "short digest destination remains unchanged")
}

private class NamedIntCodec(override val encodingId: String) : MerkleCodec<Int> {
    override fun encode(value: Int): ByteArray = Int32MerkleCodec.encode(value)
    override fun decode(encoding: ByteArray): Int = Int32MerkleCodec.decode(encoding)
}

private fun policyDomainAndGoldenBlockMatchCSharpAndRust(): Unit {
    mstEquals("mst-sha256-b16-v2", MerkleSearchTreePolicy.ALGORITHM_ID, "algorithm ID")
    for (invalid in listOf("", " ", "codec", "-v1", "codec-v", "codec-vx", " codec-v1", "codec-v1 ")) {
        mstThrows<IllegalArgumentException> {
            MerkleSearchTreePolicy.natural("wire-policy-v1", NamedIntCodec(invalid), Int32MerkleCodec)
        }
    }
    mstThrows<IllegalArgumentException> {
        MerkleSearchTreePolicy.natural(" ", Int32MerkleCodec, Int32MerkleCodec)
    }

    val first = MerkleSearchTreePolicy.natural("wire-policy-v1", Int32MerkleCodec, Int32MerkleCodec)
    val same = MerkleSearchTreePolicy.natural("wire-policy-v1", Int32MerkleCodec, Int32MerkleCodec)
    val different = MerkleSearchTreePolicy.natural("wire-policy-v2", Int32MerkleCodec, Int32MerkleCodec)
    mstCheck(first.isCompatibleWith(same), "equal policy manifests are compatible")
    mstCheck(first.domainDigest != different.domainDigest, "policy ID binds the domain")
    val manifest = ByteArray(37)
    "MST2".encodeToByteArray().copyInto(manifest)
    first.domainDigest.tryWriteBytes(manifest, 5)
    mstEquals(MerkleDigest.hash(manifest), first.emptyDigest, "canonical empty manifest")
    mstEquals(first.emptyDigest, MerkleSearchTree.empty(first).rootHash, "empty root")
    mstEquals(64, MerkleSearchTreePolicy.level(MerkleDigest.fromBytes(ByteArray(32))), "all-zero level")

    val policy = intStringPolicy()
    val tree = MerkleSearchTree.empty(policy).setItem(42, "forty-two")
    mstEquals(
        "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2",
        policy.domainDigest.toString(),
        "golden policy domain",
    )
    mstEquals(
        "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94",
        tree.rootHash.toString(),
        "golden root",
    )
    val block = tree.blocksPreorder().single()
    mstEquals(tree.rootHash, block.digest, "golden block address")
    mstEquals(
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3",
        mstHex(block.toByteArray()),
        "golden MST2 block",
    )
    mstEquals(tree.rootHash, MerkleDigest.hash(block.toByteArray()), "block digest")
    mstEquals(1, tree.validateStructure().count, "golden structure")

    val widePolicy = MerkleSearchTreePolicy.natural(
        "golden-wide-i32-i32-v1",
        Int32MerkleCodec,
        Int32MerkleCodec,
    )
    val wideKeys = listOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 38, 44, 59, 464)
    val wideTree = MerkleSearchTree.from(wideKeys.map { key -> key to -key - 1 }, widePolicy)
    val wideRoot = wideTree.blocksPreorder().first()
    mstEquals(
        "eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917",
        widePolicy.domainDigest.toString(),
        "wide golden policy domain",
    )
    mstEquals(
        "9afd7ba98ec91f72074c5f2c272ca1334244fb43a631e0fb440e02799eee8755",
        wideTree.rootHash.toString(),
        "wide golden root",
    )
    mstEquals(14, wideTree.size, "wide golden count")
    mstEquals(4, wideTree.blockCount, "wide golden block count")
    mstEquals(3, wideTree.height, "wide golden height")
    mstEquals(
        "4d53543201eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917020000000e00000002000000040000003b00000004ffffffc400000004000001d000000004fffffe2f790b862e0ef81c9e6debdf38c1099c565887fe87aed84f26dfba736de256d4d5018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608",
        mstHex(wideRoot.toByteArray()),
        "wide multi-level MST2 root block",
    )
    mstEquals(2.toByte(), wideRoot.toByteArray()[37], "wide root level byte")
    mstEquals(14, wideTree.validateStructure().count, "wide golden structure")
}

private fun constructionAndChurnAreHistoryIndependent(): Unit {
    val entries = (0 until 4_096).map { key -> key to "value-$key" }
    val forward = MerkleSearchTree.from(entries, intStringPolicy())
    val reverse = MerkleSearchTree.from(entries.reversed(), intStringPolicy())
    var incremental = MerkleSearchTree.empty(intStringPolicy())
    for ((key, value) in entries) incremental = incremental.setItem(key, value)
    mstCheck(forward.contentEquals(reverse), "forward/reverse roots")
    mstCheck(forward.contentEquals(incremental), "bulk/incremental roots")
    mstEquals(forward.shape(), reverse.shape(), "forward/reverse topology")
    mstEquals(forward.shape(), incremental.shape(), "bulk/incremental topology")
    mstEquals(entries.size, forward.validateStructure().count, "validated count")
    mstCheck(forward.height >= 3, "test should span several levels")
    mstCheck(forward.blockCount > forward.height, "test should contain several branches")

    val removed = incremental.remove(2_047)
    mstCheck(!removed.containsKey(2_047), "removed key")
    val restored = removed.setItem(2_047, "value-2047")
    mstCheck(forward.contentEquals(restored), "remove/reinsert root")
    mstEquals(forward.shape(), restored.shape(), "remove/reinsert topology")
    mstCheck(forward === forward.remove(-1), "absent remove preserves tree identity")
    mstCheck(forward === forward.setItem(2_000, "value-2000"), "encoded no-op preserves identity")
}

private fun updatesShareUnaffectedBlocksAndRetainOldVersions(): Unit {
    val original = MerkleSearchTree.from(
        (0 until 8_193).map { key -> key to key.toString() },
        intStringPolicy(),
    )
    val updated = original.setItem(4_096, "changed")
    mstEquals("4096", original[4_096], "old snapshot value")
    mstEquals("changed", updated[4_096], "new snapshot value")
    mstCheck(original.rootHash != updated.rootHash, "updated root differs")
    mstCheck(
        original.sharedBlockCount(updated) >= original.blockCount - original.height,
        "one value replacement copies at most one root-to-entry path",
    )
    original.validateStructure()
    updated.validateStructure()
}

private fun rangesDiffAndNullableValuesRemainUnambiguous(): Unit {
    val source = MerkleSearchTree.from(
        (0 until 100).map { key -> key to if (key % 11 == 0) null else key.toString() },
        intStringPolicy(),
    )
    mstEquals((23..31).toList(), source.enumerateRange(23, 31).map { it.key }.toList(), "inclusive range")
    mstThrows<IllegalArgumentException> { source.enumerateRange(5, 4) }
    mstCheck(source.containsKey(0) && source[0] == null, "present null differs from absence")

    val target = source.remove(7).setItem(8, "eight").setItem(1_000, null)
    val differences = source.diff(target)
    mstEquals(3, differences.size, "diff count")
    mstCheck(differences.any { it is MerkleMapDifference.Removed && it.key == 7 }, "removed diff")
    mstCheck(differences.any {
        it is MerkleMapDifference.Changed && it.key == 8 && it.before == "8" && it.after == "eight"
    }, "changed diff")
    mstCheck(differences.any {
        it is MerkleMapDifference.Added && it.key == 1_000 && it.value == null
    }, "added present-null diff")
    mstCheck(source.diff(source).isEmpty(), "shared root diff fast path")

    val caseSource = MerkleSearchTree.from(listOf(1 to "Alpha", 2 to null), intStringPolicy())
    val caseTarget = MerkleSearchTree.from(listOf(1 to "alpha", 2 to null), intStringPolicy())
    val caseInsensitive: (String?, String?) -> Boolean = { left, right ->
        if (left == null || right == null) left == right else left.equals(right, ignoreCase = true)
    }
    mstCheck(!caseSource.mapEquals(caseTarget), "ordinary equality observes case")
    mstCheck(caseSource.mapEquals(caseTarget, caseInsensitive), "custom equality")
    mstCheck(caseSource.diff(caseTarget, caseInsensitive).isEmpty(), "custom diff equality")
}

private data class RepresentativeKey(val logical: Int, val representative: Int)

private object RepresentativeKeyCodec : MerkleCodec<RepresentativeKey> {
    override val encodingId: String = "representative-key-v1"
    override fun encode(value: RepresentativeKey): ByteArray = Int32MerkleCodec.encode(value.logical)
    override fun decode(encoding: ByteArray): RepresentativeKey = RepresentativeKey(Int32MerkleCodec.decode(encoding), 0)
}

private class BoxValue(var number: Int)

private object BoxValueCodec : MerkleCodec<BoxValue> {
    override val encodingId: String = "box-value-v1"
    override fun encode(value: BoxValue): ByteArray = Int32MerkleCodec.encode(value.number)
    override fun decode(encoding: ByteArray): BoxValue = BoxValue(Int32MerkleCodec.decode(encoding))
}

private fun equivalentKeysAndValuesPreserveOriginalReferences(): Unit {
    val policy = MerkleSearchTreePolicy.create(
        "representatives-v1",
        Comparator<RepresentativeKey> { left, right -> left.logical.compareTo(right.logical) },
        RepresentativeKeyCodec,
        Int32MerkleCodec,
    )
    val first = RepresentativeKey(5, 1)
    val tree = MerkleSearchTree.from(
        listOf(first to 10, RepresentativeKey(2, 2) to 20, RepresentativeKey(5, 3) to 30),
        policy,
    )
    val query = RepresentativeKey(5, 99)
    mstCheck(tree.getEntry(query)?.key === first, "bulk construction retains first key reference")
    mstEquals(30, tree[query], "bulk construction uses last value")
    val replaced = tree.setItem(RepresentativeKey(5, 4), 40)
    mstCheck(replaced.getEntry(query)?.key === first, "replacement retains first key reference")

    val boxPolicy = MerkleSearchTreePolicy.natural("box-values-v1", Int32MerkleCodec, BoxValueCodec)
    val originalValue = BoxValue(10)
    val boxed = MerkleSearchTree.empty(boxPolicy).setItem(1, originalValue)
    val noOp = boxed.setItem(1, BoxValue(10))
    mstCheck(noOp === boxed, "encoded-equal value preserves tree identity")
    mstCheck(noOp.getEntry(1)?.value === originalValue, "encoded-equal value preserves stored reference")
    boxed.validateStructure()
    originalValue.number = 11
    mstThrows<IllegalStateException> { boxed.validateStructure() }
}

private fun randomizedRetainedVersionsMatchTreeMap(): Unit {
    var tree = MerkleSearchTree.empty(intStringPolicy())
    val model = TreeMap<Int, String?>()
    val retained = ArrayList<Pair<MerkleSearchTree<Int, String?>, TreeMap<Int, String?>>>()
    var state = 0x4d595df4d0f33173L
    repeat(12_000) { step ->
        state = state * 6_364_136_223_846_793_005L + 1_442_695_040_888_963_407L
        val key = ((state ushr 19) % 1_024).toInt() - 512
        if (state and 7L == 0L) {
            tree = tree.remove(key)
            model.remove(key)
        } else {
            val value = if (state and 15L == 0L) null else "$step:${state.toULong().toString(16)}"
            tree = tree.setItem(key, value)
            model[key] = value
        }
        if (step % 997 == 0) retained.add(tree to TreeMap(model))
        if (step % 257 == 0) assertTreeMatchesModel(tree, model)
    }
    assertTreeMatchesModel(tree, model)
    for ((snapshot, expected) in retained) assertTreeMatchesModel(snapshot, expected)
}

private fun assertTreeMatchesModel(tree: MerkleSearchTree<Int, String?>, model: TreeMap<Int, String?>): Unit {
    mstEquals(model.size, tree.size, "model count")
    mstEquals(model.entries.map { it.key to it.value }, tree.map { it.key to it.value }, "model entries")
    for ((key, value) in model) {
        mstCheck(tree.containsKey(key), "model key $key is present")
        mstEquals(value, tree.getEntry(key)?.value, "model value $key")
    }
    tree.validateStructure()
}

private fun immutableSnapshotsSupportConcurrentReaders(): Unit {
    val tree = MerkleSearchTree.from(
        (0 until 2_048).map { key -> key to (key * 3).toString() },
        intStringPolicy(),
    )
    val failure = AtomicReference<Throwable?>(null)
    val threads = (0 until 8).map { worker ->
        Thread({
            try {
                repeat(32) { pass ->
                    val key = (worker * 239 + pass * 17) % 2_048
                    mstEquals((key * 3).toString(), tree[key], "concurrent lookup")
                    mstEquals(2_048, tree.entries().count(), "concurrent iteration")
                }
            } catch (exception: Throwable) {
                failure.compareAndSet(null, exception)
            }
        }, "mst-reader-$worker").also(Thread::start)
    }
    threads.forEach(Thread::join)
    failure.get()?.let { throw AssertionError("concurrent reader failed", it) }
}

private data class LayeredKey(val order: Int, val nonce: Int)

private val layeredComparator: Comparator<LayeredKey> = Comparator { left, right ->
    val comparison = left.order.compareTo(right.order)
    if (comparison != 0) comparison else left.nonce.compareTo(right.nonce)
}

private object LayeredKeyCodec : MerkleCodec<LayeredKey> {
    override val encodingId: String = "test-layered-key-v1"

    override fun encode(value: LayeredKey): ByteArray =
        Int32MerkleCodec.encode(value.order) + Int32MerkleCodec.encode(value.nonce)

    override fun decode(encoding: ByteArray): LayeredKey {
        if (encoding.size != 8) throw MerkleCodecException("A layered key must contain exactly eight bytes.")
        return LayeredKey(
            Int32MerkleCodec.decode(encoding.copyOfRange(0, 4)),
            Int32MerkleCodec.decode(encoding.copyOfRange(4, 8)),
        )
    }
}

private fun adversarialLayersInsertionAndRemovalStayCanonical(): Unit {
    val policy = MerkleSearchTreePolicy.create(
        "core-stress-layered-v1",
        layeredComparator,
        LayeredKeyCodec,
        Int32MerkleCodec,
    )
    val entries = (-64..64).map { order ->
        val absolute = kotlin.math.abs(order)
        val level = when {
            order == 0 -> 4
            absolute % 48 == 0 -> 3
            absolute % 16 == 0 -> 2
            absolute % 4 == 0 -> 1
            else -> 0
        }
        findLayeredKey(policy, order, level) to level
    }
    val expectedPairs = entries.map { (key, _) -> key to key.order * 17 }
    val expected = MerkleSearchTree.from(expectedPairs, policy)
    val statistics = expected.validateStructure()
    mstEquals(entries.size, statistics.count, "adversarial count")
    mstEquals(5, statistics.height, "exact five-level geometry")
    mstCheck(statistics.maximumEntriesPerBlock > 1, "high same-level run")

    var ascending = MerkleSearchTree.empty(policy)
    for ((key, _) in entries.sortedWith(compareBy(layeredComparator) { it.first })) {
        ascending = ascending.setItem(key, key.order * 17)
        ascending.validateStructure()
    }
    val shuffledEntries = entries.shuffled(Random(0x5a172026))
    var shuffled = MerkleSearchTree.empty(policy)
    for ((key, _) in shuffledEntries) shuffled = shuffled.setItem(key, key.order * 17)
    assertCanonicalEquivalent(expected, ascending, "ascending adversarial construction")
    assertCanonicalEquivalent(expected, shuffled, "shuffled adversarial construction")

    val remaining = TreeMap<LayeredKey, Int>(layeredComparator)
    expectedPairs.forEach { (key, value) -> remaining[key] = value }
    var current = shuffled
    val removalOrder = entries.sortedWith(
        compareByDescending<Pair<LayeredKey, Int>> { it.second }
            .thenBy { kotlin.math.abs(it.first.order) }
            .thenBy { it.first.order },
    )
    for ((key, _) in removalOrder) {
        current = current.remove(key)
        remaining.remove(key)
        val rebuilt = MerkleSearchTree.from(remaining.entries.map { it.key to it.value }, policy)
        assertCanonicalEquivalent(rebuilt, current, "adversarial contraction ${key.order}")
        current.validateStructure()
    }
    mstCheck(current.isEmpty, "adversarial contraction reaches empty")
    mstEquals(policy.emptyDigest, current.rootHash, "adversarial empty root")
}

private fun findLayeredKey(
    policy: MerkleSearchTreePolicy<LayeredKey, Int>,
    order: Int,
    expectedLevel: Int,
): LayeredKey {
    for (nonce in 0 until 4_000_000) {
        val key = LayeredKey(order, nonce)
        if (MerkleSearchTreePolicy.level(policy.hashKey(key)) == expectedLevel) return key
    }
    throw AssertionError("Could not find a level-$expectedLevel key for order $order")
}

private fun independentHistoriesConvergeAfterChurn(): Unit {
    val policy = MerkleSearchTreePolicy.natural("independent-history-v1", Int32MerkleCodec, Int32MerkleCodec)
    val finalEntries = (-384..384).map { key -> key to key * 1_000_003 }
    val canonical = MerkleSearchTree.from(finalEntries, policy)
    repeat(8) { history ->
        val random = Random(0x71c30000 + history * 7_919)
        var tree = MerkleSearchTree.empty(policy)
        for ((key, value) in finalEntries.shuffled(random)) {
            if (random.nextInt(3) == 0) tree = tree.setItem(key, value.inv())
            tree = tree.setItem(key, value)
        }
        repeat(512) {
            val key = random.nextInt(1_401) - 700
            tree = tree.setItem(key, random.nextInt())
            if (key !in -384..384) tree = tree.remove(key)
        }
        for ((key, value) in finalEntries.shuffled(random)) tree = tree.setItem(key, value)
        for (key in tree.map { it.key }.filter { it !in -384..384 }.toList()) tree = tree.remove(key)
        assertCanonicalEquivalent(canonical, tree, "independent history $history")
    }
}

private fun <K, V> assertCanonicalEquivalent(
    expected: MerkleSearchTree<K, V>,
    actual: MerkleSearchTree<K, V>,
    message: String,
): Unit {
    mstEquals(expected.rootHash, actual.rootHash, "$message root")
    mstEquals(expected.shape(), actual.shape(), "$message topology")
    mstEquals(expected.map { it.key to it.value }, actual.map { it.key to it.value }, "$message entries")
    mstEquals(expected.validateStructure(), actual.validateStructure(), "$message statistics")
    mstEquals(expected.blocksPreorder(), actual.blocksPreorder(), "$message exact blocks")
}

internal fun runMerkleSearchTreeTests(): Unit {
    val tests = listOf(
        "cursorNavigatesRanksAndPublishesCanonicalEdits" to ::cursorNavigatesRanksAndPublishesCanonicalEdits,
        "cursorCachedRanksAndBoundsMatchSortedModel" to ::cursorCachedRanksAndBoundsMatchSortedModel,
        "codecAndDigestVectorsAreStrict" to ::codecAndDigestVectorsAreStrict,
        "policyDomainAndGoldenBlockMatchCSharpAndRust" to ::policyDomainAndGoldenBlockMatchCSharpAndRust,
        "constructionAndChurnAreHistoryIndependent" to ::constructionAndChurnAreHistoryIndependent,
        "updatesShareUnaffectedBlocksAndRetainOldVersions" to ::updatesShareUnaffectedBlocksAndRetainOldVersions,
        "rangesDiffAndNullableValuesRemainUnambiguous" to ::rangesDiffAndNullableValuesRemainUnambiguous,
        "equivalentKeysAndValuesPreserveOriginalReferences" to ::equivalentKeysAndValuesPreserveOriginalReferences,
        "randomizedRetainedVersionsMatchTreeMap" to ::randomizedRetainedVersionsMatchTreeMap,
        "immutableSnapshotsSupportConcurrentReaders" to ::immutableSnapshotsSupportConcurrentReaders,
        "adversarialLayersInsertionAndRemovalStayCanonical" to ::adversarialLayersInsertionAndRemovalStayCanonical,
        "independentHistoriesConvergeAfterChurn" to ::independentHistoriesConvergeAfterChurn,
    )
    for ((name, test) in tests) {
        test()
        println("PASS mst.$name")
    }
}

private fun cursorNavigatesRanksAndPublishesCanonicalEdits(): Unit {
    val source = MerkleSearchTree.from(listOf(-10 to "a", 0 to null, 10 to "c"), intStringPolicy())
    val keys = listOf(-10, 0, 10)
    for (position in 0..source.size) {
        val cursor = checkNotNull(source.cursorAt(position))
        mstEquals(position, cursor.position, "cursor position")
        mstEquals(position == 0, cursor.isAtStart, "cursor start")
        mstEquals(position == source.size, cursor.isAtEnd, "cursor end")
        mstCheck(cursor.snapshot() === source, "cursor snapshot identity")
        mstEquals(keys.getOrNull(position - 1), cursor.peekPrevious()?.key, "cursor previous")
        mstEquals(keys.getOrNull(position), cursor.peekNext()?.key, "cursor next")
    }

    mstEquals(1, source.lowerBoundCursor(-5).position, "cursor lower bound")
    mstEquals(2, source.upperBoundCursor(0).position, "cursor upper bound")
    mstCheck(source.cursorAtKey(0).found, "cursor exact hit")
    mstEquals(2, source.cursorAtKey(5).cursor.position, "cursor exact miss rank")
    mstCheck(!source.cursorAtKey(5).found, "cursor exact miss")

    val exact = source.cursorAtKey(0).cursor
    mstCheck(exact.setNextValue(null) === exact, "cursor no-op identity")
    val changed = checkNotNull(exact.setNextValue("b")).snapshot()
    mstEquals("b", changed.getEntry(0)?.value, "cursor changed value")
    mstCheck(changed.rootHash != source.rootHash, "cursor changed root hash")
    mstCheck(changed.policy === source.policy, "cursor policy identity")
    mstEquals(null, source.getEntry(0)?.value, "cursor source nullable value")

    val inserted = source.lowerBoundCursor(5).insert(5, "five")
    mstEquals(3, inserted.position, "cursor inserted position")
    mstEquals(listOf(-10, 0, 5, 10), inserted.snapshot().map { it.key }, "cursor inserted keys")
    mstEquals(source.rootHash, checkNotNull(inserted.deletePrevious()).snapshot().rootHash, "cursor restored root")
    mstEquals(null, source.cursorAt(4), "cursor invalid rank")
    mstEquals(null, source.cursor().movePrevious(), "cursor before start")
    mstEquals(null, source.cursorAtEnd().moveNext(), "cursor after end")
    mstThrows<IllegalArgumentException> { exact.insert(0, "duplicate") }
    mstThrows<IllegalArgumentException> { source.cursor().insert(5, "wrong gap") }
}

private fun cursorCachedRanksAndBoundsMatchSortedModel(): Unit {
    val keys = (-500..500).filter { it % 7 != 0 }
    val tree = MerkleSearchTree.from(keys.map { it to it.toString() }, intStringPolicy())
    for ((position, key) in keys.withIndex()) {
        mstEquals(key, checkNotNull(tree.cursorAt(position)?.peekNext()).key, "cursor rank selection")
    }
    for (probe in -550..550 step 11) {
        val rank = keys.indexOfFirst { it >= probe }.let { if (it < 0) keys.size else it }
        val found = keys.getOrNull(rank) == probe
        mstEquals(rank, tree.lowerBoundCursor(probe).position, "cursor lower rank")
        mstEquals(rank + if (found) 1 else 0, tree.upperBoundCursor(probe).position, "cursor upper rank")
        val result = tree.cursorAtKey(probe)
        mstEquals(rank, result.cursor.position, "cursor exact rank")
        mstEquals(found, result.found, "cursor exact presence")
    }
}

private fun mstCheck(condition: Boolean, message: String): Unit {
    if (!condition) throw AssertionError(message)
}

private fun <T> mstEquals(expected: T, actual: T, message: String): Unit {
    if (expected != actual) throw AssertionError("$message: expected <$expected>, actual <$actual>")
}

private fun mstBytes(expected: ByteArray, actual: ByteArray, message: String): Unit {
    if (!expected.contentEquals(actual)) {
        throw AssertionError("$message: expected <${mstHex(expected)}>, actual <${mstHex(actual)}>")
    }
}

private inline fun <reified T : Throwable> mstThrows(operation: () -> Unit): Unit {
    try {
        operation()
    } catch (exception: Throwable) {
        if (exception is T) return
        throw AssertionError("Expected ${T::class.simpleName}, received ${exception::class.simpleName}", exception)
    }
    throw AssertionError("Expected ${T::class.simpleName}")
}

private fun mstHex(bytes: ByteArray): String = buildString(bytes.size * 2) {
    val digits = "0123456789abcdef"
    for (byte in bytes) {
        val value = byte.toInt() and 0xff
        append(digits[value ushr 4])
        append(digits[value and 0x0f])
    }
}

private fun mstUnhex(value: String): ByteArray = ByteArray(value.length / 2) { index ->
    value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
}
