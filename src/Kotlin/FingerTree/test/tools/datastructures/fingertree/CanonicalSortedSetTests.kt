package tools.datastructures.fingertree

import java.util.Collections
import java.util.TreeSet
import kotlin.random.Random

private fun canonicalCheck(value: Boolean, message: String) {
    if (!value) {
        throw AssertionError(message)
    }
}

private fun <T> canonicalCheckEquals(expected: T, actual: T, message: String) {
    if (expected != actual) {
        throw AssertionError("$message Expected <$expected>, actual <$actual>.")
    }
}

private inline fun <reified T : Throwable> canonicalCheckThrows(message: String, action: () -> Unit) {
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

private fun canonicalHistoriesConvergeOnOneTopology() {
    val policy = ZipTreeRankPolicy.create<Int>(seed = 0x1234_5678_9abc_def0L)
    val values = (-300 until 300).toList()
    val baseline = CanonicalSortedSet.from(values, policy)
    val random = Random(20260718)

    repeat(12) { trial ->
        val permutation = values.shuffled(random)
        val bulk = CanonicalSortedSet.from(permutation, policy)
        var incremental = CanonicalSortedSet.empty(policy)
        for (value in permutation) {
            incremental = incremental.add(value)
        }
        canonicalCheckEquals(baseline.shapeForTesting(), bulk.shapeForTesting(), "bulk permutation $trial shape")
        canonicalCheckEquals(baseline.contentHash, bulk.contentHash, "bulk permutation $trial digest")
        canonicalCheckEquals(
            baseline.shapeForTesting(),
            incremental.shapeForTesting(),
            "incremental permutation $trial shape",
        )
        canonicalCheckEquals(baseline.contentHash, incremental.contentHash, "incremental permutation $trial digest")
        canonicalCheck(baseline.setEquals(incremental), "incremental permutation $trial semantic equality")

        var set = incremental
        for (value in permutation.take(75)) {
            set = set.remove(value)
        }
        for (value in permutation.take(75).asReversed()) {
            set = set.add(value)
        }
        canonicalCheckEquals(baseline.shapeForTesting(), set.shapeForTesting(), "delete/reinsert $trial shape")
    }

    val statistics = baseline.validateStructure()
    canonicalCheckEquals(values.size, statistics.count, "canonical baseline count")
    canonicalCheckEquals(baseline.height, statistics.height, "canonical baseline height")
}

private fun canonicalRandomizedHistoryRetainsSnapshots() {
    val policy = ZipTreeRankPolicy.create<Int>(seed = 42L)
    var actual = CanonicalSortedSet.empty(policy)
    val expected = TreeSet<Int>()
    val snapshots = ArrayList<Pair<CanonicalSortedSet<Int>, List<Int>>>()
    val random = java.util.Random(20260719L)

    repeat(15_000) { operation ->
        val value = random.nextInt(4_001) - 2_000
        if (random.nextInt(3) == 0) {
            actual = actual.remove(value)
            expected.remove(value)
        } else {
            actual = actual.add(value)
            expected.add(value)
        }

        if (operation % 601 == 0) {
            canonicalCheckEquals(expected.toList(), actual.toList(), "randomized model at $operation")
            actual.validateStructure()
        }
        if (operation % 997 == 0) {
            snapshots.add(actual to expected.toList())
        }
    }

    canonicalCheckEquals(expected.toList(), actual.toList(), "randomized final model")
    for ((snapshot, contents) in snapshots) {
        canonicalCheckEquals(contents, snapshot.toList(), "retained canonical snapshot")
        snapshot.validateStructure()
    }
}

private fun canonicalComparatorEquivalenceRequiresCoherentRanks() {
    val comparator = String.CASE_INSENSITIVE_ORDER
    canonicalCheckThrows<IllegalArgumentException>("custom comparator without rank hash") {
        ZipTreeRankPolicy.create<String>(comparator = comparator, seed = 7L)
    }
    canonicalCheckThrows<IllegalArgumentException>("custom comparator keyed without rank hash") {
        ZipTreeRankPolicy.createKeyed(ByteArray(32), comparator = comparator)
    }

    val coherent = ZipTreeRankPolicy.create(
        comparator,
        rankHash = { value: String -> value.lowercase().hashCode().toLong() and 0xffff_ffffL },
        seed = 7L,
    )
    val stored = charArrayOf('A', 'l', 'p', 'h', 'a').concatToString()
    val original = CanonicalSortedSet.empty(coherent).add(stored)
    val duplicate = original.add("ALPHA")
    canonicalCheck(original === duplicate, "comparator-equivalent add preserves set identity")
    val lookup = original.tryGetValue("alpha")
    canonicalCheck(lookup.found, "comparator-equivalent lookup succeeds")
    canonicalCheck(lookup.value === stored, "first representative is retained")

    val incoherent = ZipTreeRankPolicy.create(
        comparator,
        rankHash = { value: String -> if (value.first().isUpperCase()) 1L else 2L },
        seed = 17L,
    )
    val one = CanonicalSortedSet.empty(incoherent).add("alpha")
    canonicalCheckThrows<IllegalStateException>("incremental incoherent rank hash") { one.add("ALPHA") }
    canonicalCheckThrows<IllegalStateException>("bulk incoherent rank hash") {
        CanonicalSortedSet.from(listOf("alpha", "ALPHA"), incoherent)
    }
}

private fun canonicalBulkRepresentativesAndNullableLookupsAreUnambiguous() {
    val comparator = String.CASE_INSENSITIVE_ORDER
    val policy = ZipTreeRankPolicy.create(
        comparator,
        rankHash = { value: String -> value.lowercase().hashCode().toLong() and 0xffff_ffffL },
        seed = 0x2a11_ceL,
    )
    val firstAlpha = charArrayOf('A', 'l', 'P', 'h', 'A').concatToString()
    val laterAlpha = charArrayOf('a', 'L', 'p', 'H', 'a').concatToString()
    val firstZulu = charArrayOf('Z', 'u', 'l', 'u').concatToString()
    val bulk = CanonicalSortedSet.from(
        listOf(firstZulu, firstAlpha, laterAlpha, "ZULU", "bravo"),
        policy,
    )
    canonicalCheckEquals(3, bulk.size, "bulk comparer-equivalence class count")
    canonicalCheck(bulk.tryGetValue("alpha").value === firstAlpha, "bulk keeps first alpha representative")
    canonicalCheck(bulk.tryGetValue("zulu").value === firstZulu, "bulk keeps first zulu representative")

    val nullableComparator = Comparator<Int?> { left, right ->
        when {
            left == right -> 0
            left == null -> -1
            right == null -> 1
            else -> left.compareTo(right)
        }
    }
    val nullablePolicy = ZipTreeRankPolicy.create<Int?>(
        nullableComparator,
        rankHash = { value -> value?.toLong() ?: Long.MIN_VALUE },
        seed = 73L,
    )
    val nullable = CanonicalSortedSet.empty(nullablePolicy).add(null).add(1)
    val storedNull = nullable.tryGetValue(null)
    canonicalCheck(storedNull.found, "stored null lookup reports success")
    canonicalCheckEquals(null, storedNull.value, "stored null lookup retains null")
    val miss = nullable.tryGetValue(2)
    canonicalCheck(!miss.found, "missing nullable lookup reports failure")
    canonicalCheckEquals(2, miss.value, "missing nullable lookup returns the probe")
    nullable.validateStructure()
}

private fun canonicalKeyedRanksReproduceAndSeparatePolicies() {
    val vectorKey = ByteArray(32) { it.toByte() }
    val vectorPolicy = ZipTreeRankPolicy.createKeyed<Int>(
        vectorKey,
        rankHash = { 0x0102_0304_0506_0708L },
    )
    val vectorRank = CanonicalSortedSet.empty(vectorPolicy).rankForTesting(0)
    canonicalCheckEquals(1, vectorRank.geometric, "HMAC vector geometric rank")
    canonicalCheckEquals(0x1975_2063_8af1_f7a6L, vectorRank.secondary, "HMAC vector secondary rank")
    canonicalCheckEquals(0xf31e_be0a_b983_ff0fUL.toLong(), vectorRank.content, "HMAC vector content word")

    val key = ByteArray(32) { index -> (index * 7 + 3).toByte() }
    val retainedKey = key.copyOf()
    val firstPolicy = ZipTreeRankPolicy.createKeyed<Int>(key, rankHash = { it.toLong() and 0xffff_ffffL })
    key.fill(0xff.toByte())
    val secondPolicy = ZipTreeRankPolicy.createKeyed<Int>(retainedKey, rankHash = { it.toLong() and 0xffff_ffffL })
    val values = (-1_000..1_000).filter { it % 3 != 0 }
    val first = CanonicalSortedSet.from(values, firstPolicy)
    val second = CanonicalSortedSet.from(values.asReversed(), secondPolicy)

    canonicalCheck(firstPolicy !== secondPolicy, "reproduced policies remain distinct objects")
    canonicalCheckEquals(first.shapeForTesting(), second.shapeForTesting(), "owned keyed-policy shape")
    canonicalCheckEquals(first.contentHash, second.contentHash, "owned keyed-policy digest")
    canonicalCheckEquals(first.validateStructure(), second.validateStructure(), "owned keyed-policy statistics")
    canonicalCheck(first.setEquals(second), "cross-policy semantic equality")
    canonicalCheckThrows<IllegalArgumentException>("policy identity gates algebra") { first.union(second) }

    val different = ZipTreeRankPolicy.createKeyed<Int>(ByteArray(32) { (it + 1).toByte() })
    canonicalCheck(
        first.rankForTesting(137) != CanonicalSortedSet.empty(different).rankForTesting(137),
        "different HMAC keys separate ranks",
    )
    canonicalCheckThrows<IllegalArgumentException>("short HMAC key") {
        ZipTreeRankPolicy.createKeyed<Int>(ByteArray(31))
    }
}

private fun canonicalPolicyModesAndUnsignedPriorityAreExact() {
    val freshFirst = ZipTreeRankPolicy.create<Int>()
    val freshSecond = ZipTreeRankPolicy.create<Int>()
    val firstFreshRank = CanonicalSortedSet.empty(freshFirst).rankForTesting(0x5eed)
    val secondFreshRank = CanonicalSortedSet.empty(freshSecond).rankForTesting(0x5eed)
    // This is the one intentionally probabilistic assertion: independently generated HMAC keys
    // collide on the retained 128-bit secondary/content pair with probability at most 2^-128.
    canonicalCheck(firstFreshRank != secondFreshRank, "fresh hidden keys separate ranks")
    canonicalCheckEquals(null, freshFirst.seed, "fresh hidden-key policy has no public seed")
    canonicalCheckEquals(null, freshSecond.seed, "second hidden-key policy has no public seed")

    val seed = 0x0123_4567_89ab_cdefL
    val vectorPolicy = ZipTreeRankPolicy.create<Int>(
        seed = seed,
        rankHash = { 0xfedc_ba98_7654_3210UL.toLong() },
    )
    val vectorRank = CanonicalSortedSet.empty(vectorPolicy).rankForTesting(0)
    // Independent vector: SHA-256("ZZT2" || 0123456789abcdef) is
    // 0a2b912916749411e2b58555bfff0d8481d2aceefd74fc13d8a685e889649516.
    canonicalCheckEquals(0, vectorRank.geometric, "public-seed vector geometric rank")
    canonicalCheckEquals(
        0x9efd_aef0_3f68_c6bdUL.toLong(),
        vectorRank.secondary,
        "public-seed vector secondary rank",
    )
    canonicalCheckEquals(0x4da7_5484_837a_7798L, vectorRank.content, "public-seed vector content word")
    canonicalCheckEquals(seed, vectorPolicy.seed, "public seed is retained")

    val seededFirst = ZipTreeRankPolicy.create<Int>(seed = seed)
    val seededSecond = ZipTreeRankPolicy.create<Int>(seed = seed)
    val values = (-256..256).filter { it % 5 != 0 }
    val firstSet = CanonicalSortedSet.from(values, seededFirst)
    val secondSet = CanonicalSortedSet.from(values.asReversed(), seededSecond)
    canonicalCheckEquals(firstSet.shapeForTesting(), secondSet.shapeForTesting(), "same public-seed shape")
    canonicalCheckEquals(firstSet.contentHash, secondSet.contentHash, "same public-seed digest")
    canonicalCheckEquals(
        firstSet.validateStructure(),
        secondSet.validateStructure(),
        "same public-seed validation statistics",
    )

    val priorityPolicy = ZipTreeRankPolicy.create<Int>(seed = 0x7711_22L)
    val priorityProbe = CanonicalSortedSet.empty(priorityPolicy)
    val candidates = (0 until 512).map { value -> value to priorityProbe.rankForTesting(value) }
    val oppositeSignPair = candidates
        .groupBy { it.second.geometric }
        .values
        .firstNotNullOfOrNull { group ->
            val negative = group.firstOrNull { it.second.secondary < 0L }
            val nonnegative = group.firstOrNull { it.second.secondary >= 0L }
            if (negative != null && nonnegative != null) negative to nonnegative else null
        }
        ?: throw AssertionError("deterministic seed did not produce an opposite-sign secondary pair")
    val negativeSecondary = oppositeSignPair.first
    val nonnegativeSecondary = oppositeSignPair.second
    canonicalCheck(
        negativeSecondary.second.secondary < nonnegativeSecondary.second.secondary,
        "signed secondary comparison ranks the negative word lower",
    )
    canonicalCheck(
        java.lang.Long.compareUnsigned(
            negativeSecondary.second.secondary,
            nonnegativeSecondary.second.secondary,
        ) > 0,
        "unsigned secondary comparison ranks the high-bit word higher",
    )
    val twoItem = CanonicalSortedSet.from(
        listOf(nonnegativeSecondary.first, negativeSecondary.first),
        priorityPolicy,
    )
    canonicalCheckEquals(
        negativeSecondary.first,
        twoItem.shapeForTesting().first().item,
        "tree heap order uses unsigned secondary rank",
    )
    twoItem.validateStructure()
}

private fun canonicalCollisionsRemainStackSafeAndDeterministic() {
    val count = 4_096
    val policy = ZipTreeRankPolicy.create<Int>(seed = 1L, rankHash = { 0L })
    val forward = CanonicalSortedSet.from(0 until count, policy)
    val reverse = CanonicalSortedSet.from((0 until count).reversed(), policy)

    canonicalCheckEquals(count, forward.height, "fully colliding height")
    canonicalCheckEquals(forward.shapeForTesting(), reverse.shapeForTesting(), "colliding insertion-order shape")
    canonicalCheckEquals((0 until count).toList(), forward.toList(), "colliding sorted iteration")
    val statistics = forward.validateStructure()
    canonicalCheckEquals(count - 1, statistics.priorityCollisionCount, "colliding priority count")

    val removed = forward.remove(count - 1)
    canonicalCheckEquals(count - 1, removed.height, "deep removal height")
    removed.validateStructure()
    val restored = removed.add(count - 1)
    canonicalCheckEquals(forward.contentHash, restored.contentHash, "deep reinsert digest")
    canonicalCheckEquals(forward.shapeForTesting(), restored.shapeForTesting(), "deep reinsert shape")
    canonicalCheck(forward.setEquals(restored), "deep reinsert semantic equality")
}

private fun canonicalAlgebraNoOpsAndSharingHonorPolicyIdentity() {
    val policy = ZipTreeRankPolicy.create<Int>(seed = 99L)
    val left = CanonicalSortedSet.from(listOf(1, 2, 4, 8), policy)
    val right = CanonicalSortedSet.from(listOf(2, 3, 4, 5), policy)
    val empty = CanonicalSortedSet.empty(policy)

    canonicalCheckEquals(listOf(1, 2, 3, 4, 5, 8), left.union(right).toList(), "canonical union")
    canonicalCheckEquals(listOf(2, 4), left.intersect(right).toList(), "canonical intersection")
    canonicalCheckEquals(listOf(1, 8), left.except(right).toList(), "canonical difference")
    canonicalCheck(left === left.add(2), "duplicate add identity")
    canonicalCheck(left === left.remove(-1), "absent remove identity")
    canonicalCheck(left === left.union(left), "self union identity")
    canonicalCheck(left === left.intersect(left), "self intersection identity")
    canonicalCheck(left === left.except(empty), "empty difference identity")
    canonicalCheck(empty === empty.clear(), "empty clear identity")

    val large = CanonicalSortedSet.from(0 until 2_000, policy)
    val added = large.add(2_000)
    val removed = large.remove(1_000)
    canonicalCheck(large.sharesStorageWith(added), "localized add retains untouched nodes")
    canonicalCheck(large.sharesStorageWith(removed), "localized remove retains untouched nodes")
    val addSharedCount = (0 until 2_000).count { value ->
        large.nodeIdentityForTesting(value) === added.nodeIdentityForTesting(value)
    }
    val removeSharedCount = (0 until 2_000)
        .filter { it != 1_000 }
        .count { value -> large.nodeIdentityForTesting(value) === removed.nodeIdentityForTesting(value) }
    canonicalCheck(addSharedCount >= 1_800, "localized add shares at least 90% of existing nodes")
    canonicalCheck(removeSharedCount >= 1_799, "localized remove shares at least 90% of surviving nodes")
    canonicalCheck(
        !large.sharesStorageWith(CanonicalSortedSet.from(0 until 2_000, policy)),
        "independent build shares no nodes",
    )

    val otherPolicy = ZipTreeRankPolicy.create<Int>(seed = 100L)
    val semanticPeer = CanonicalSortedSet.from(listOf(8, 4, 2, 1), otherPolicy)
    canonicalCheck(left.setEquals(semanticPeer), "cross-policy equality is semantic")
    canonicalCheckThrows<IllegalArgumentException>("union rejects a distinct policy") { left.union(semanticPeer) }
    canonicalCheck(left.isSubsetOf(listOf(8, 4, 2, 1, 1)), "subset relation deduplicates probes")
    canonicalCheck(left.isProperSubsetOf(listOf(8, 4, 2, 1, 16, 16)), "proper subset relation")
    canonicalCheck(left.isSupersetOf(listOf(1, 2, 2)), "superset relation")
    canonicalCheck(left.isProperSupersetOf(listOf(1, 2, 2)), "proper superset relation")
    canonicalCheck(!left.isProperSupersetOf(listOf(8, 4, 2, 1, 1)), "equal set is not a proper superset")
    canonicalCheck(left.overlaps(listOf(-1, 4)), "overlap relation")
    canonicalCheck(!left.overlaps(listOf(-2, -1)), "disjoint overlap relation")
    canonicalCheck(left.setEquals(listOf(8, 4, 2, 1, 1)), "iterable set equality deduplicates probes")
}

private fun canonicalCrossPolicyEqualityUsesTheReceiverComparator() {
    val insensitivePolicy = ZipTreeRankPolicy.create(
        String.CASE_INSENSITIVE_ORDER,
        rankHash = { value: String -> value.lowercase().hashCode().toLong() and 0xffff_ffffL },
        seed = 0x1a51L,
    )
    val sensitivePolicy = ZipTreeRankPolicy.create<String>(seed = 0x5e51L)
    val insensitive = CanonicalSortedSet.from(listOf("alpha"), insensitivePolicy)
    val sensitive = CanonicalSortedSet.from(listOf("alpha", "ALPHA"), sensitivePolicy)

    canonicalCheck(
        insensitive.setEquals(sensitive),
        "case-insensitive receiver collapses the case-sensitive peer's representatives",
    )
    canonicalCheck(
        !sensitive.setEquals(insensitive),
        "case-sensitive receiver observes two representatives and equality is asymmetric",
    )
    canonicalCheck(insensitive.isSubsetOf(sensitive), "receiver comparator governs cross-policy subset")
    canonicalCheck(
        sensitive.isProperSupersetOf(insensitive),
        "case-sensitive receiver governs cross-policy proper superset",
    )
}

private class CanonicalCountingComparator : Comparator<Int> {
    var calls: Int = 0

    override fun compare(left: Int, right: Int): Int {
        calls++
        return left.compareTo(right)
    }
}

private fun canonicalDigestRejectsInequalityAndPublishesAcrossReaders() {
    val comparator = CanonicalCountingComparator()
    val policy = ZipTreeRankPolicy.create(
        comparator,
        rankHash = { value: Int -> value.toLong() and 0xffff_ffffL },
        seed = 0x51a7_1cL,
    )
    val baseline = CanonicalSortedSet.from(0 until 4_096, policy)
    val changed = baseline.remove(2_048).add(-1)
    baseline.contentHash
    changed.contentHash
    comparator.calls = 0
    canonicalCheck(!baseline.setEquals(changed), "digest rejects unequal same-size set")
    canonicalCheckEquals(0, comparator.calls, "digest inequality avoids semantic comparison")

    val independent = CanonicalSortedSet.from((0 until 4_096).reversed(), policy)
    independent.contentHash
    comparator.calls = 0
    canonicalCheck(baseline.setEquals(independent), "equal independent histories compare in lockstep")
    canonicalCheck(comparator.calls > 0, "digest equality is verified semantically")

    val unforced = CanonicalSortedSet.from(0 until 12_000, ZipTreeRankPolicy.create(seed = 31337L))
    val hashes = Collections.synchronizedList(mutableListOf<Long>())
    val failures = Collections.synchronizedList(mutableListOf<Throwable>())
    val readers = (0 until 8).map { worker ->
        Thread {
            try {
                repeat(64) {
                    hashes.add(unforced.contentHash)
                }
            } catch (error: Throwable) {
                failures.add(error)
            }
        }.apply { name = "canonical-digest-$worker" }
    }
    readers.forEach(Thread::start)
    readers.forEach(Thread::join)
    canonicalCheck(failures.isEmpty(), "concurrent digest readers")
    canonicalCheckEquals(1, hashes.distinct().size, "concurrent digest publication")
}

private fun canonicalValidatorDetectsCorruptedMetadata() {
    val disposable = CanonicalSortedSet.from(0 until 32, ZipTreeRankPolicy.create(seed = 123L))
    disposable.validateStructure()
    val rootField = CanonicalSortedSet::class.java.getDeclaredField("root")
    canonicalCheck(rootField.trySetAccessible(), "root reflection is available to the local corruption test")
    val root = rootField.get(disposable) ?: throw AssertionError("nonempty root")
    val countField = root.javaClass.getDeclaredField("count")
    canonicalCheck(countField.trySetAccessible(), "count reflection is available to the local corruption test")
    countField.setInt(root, countField.getInt(root) + 1)
    canonicalCheckThrows<IllegalStateException>("validator detects corrupted cached metadata") {
        disposable.validateStructure()
    }
}

internal fun canonicalSortedSetTestCases(): List<Pair<String, () -> Unit>> = listOf(
    "canonicalHistoriesConvergeOnOneTopology" to ::canonicalHistoriesConvergeOnOneTopology,
    "canonicalRandomizedHistoryRetainsSnapshots" to ::canonicalRandomizedHistoryRetainsSnapshots,
    "canonicalComparatorEquivalenceRequiresCoherentRanks" to ::canonicalComparatorEquivalenceRequiresCoherentRanks,
    "canonicalBulkRepresentativesAndNullableLookupsAreUnambiguous" to
        ::canonicalBulkRepresentativesAndNullableLookupsAreUnambiguous,
    "canonicalKeyedRanksReproduceAndSeparatePolicies" to ::canonicalKeyedRanksReproduceAndSeparatePolicies,
    "canonicalPolicyModesAndUnsignedPriorityAreExact" to ::canonicalPolicyModesAndUnsignedPriorityAreExact,
    "canonicalCollisionsRemainStackSafeAndDeterministic" to ::canonicalCollisionsRemainStackSafeAndDeterministic,
    "canonicalAlgebraNoOpsAndSharingHonorPolicyIdentity" to ::canonicalAlgebraNoOpsAndSharingHonorPolicyIdentity,
    "canonicalCrossPolicyEqualityUsesTheReceiverComparator" to
        ::canonicalCrossPolicyEqualityUsesTheReceiverComparator,
    "canonicalDigestRejectsInequalityAndPublishesAcrossReaders" to
        ::canonicalDigestRejectsInequalityAndPublishesAcrossReaders,
    "canonicalValidatorDetectsCorruptedMetadata" to ::canonicalValidatorDetectsCorruptedMetadata,
)
