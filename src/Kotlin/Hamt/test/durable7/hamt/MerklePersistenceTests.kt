/*
 * Tests for Merkle block storage, verification, and synchronization, exercising verification
 * against corrupted and over-budget input to confirm it fails rather than trusting it.
 */
package durable7.hamt

import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

private typealias PersistenceTree = MerkleSearchTree<Int, String?>

private fun persistencePolicy(
    id: String = "persistence-algorithms-test-v1",
): MerkleSearchTreePolicy<Int, String?> = MerkleSearchTreePolicy.natural(
    id,
    Int32MerkleCodec,
    NullableUtf8MerkleCodec,
)

private fun persistenceTree(
    policy: MerkleSearchTreePolicy<Int, String?>,
    count: Int,
): PersistenceTree {
    val first = -(count / 2)
    return MerkleSearchTree.from(
        (first until first + count).map { key ->
            key to if (key % 29 == 0) null else "value:$key"
        },
        policy,
    )
}

private fun exactGoldenBlockAndMsp2Queries(): Unit {
    val policy = persistencePolicy("golden-int-string-v1")
    val tree = MerkleSearchTree.empty(policy).setItem(42, "forty-two")
    val pack = tree.exportPack()
    persistEquals(1, pack.blockCount, "golden pack block count")
    persistEquals(tree.rootHash, pack.blocks.single().digest, "golden block address")
    persistEquals(
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2" +
            "000000000100000001000000040000002a0000000a01666f7274792d74776f" +
            "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3" +
            "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3",
        persistHex(pack.blocks.single().toByteArray()),
        "exact MST2 golden bytes",
    )

    persistEquals(
        "4d53503200000000040000002a0000000a01666f7274792d74776f",
        persistHex(tree.createProof(42).query()),
        "exact MSP2 membership query",
    )
    persistEquals(
        "4d53503201000000040000002b",
        persistHex(tree.createProof(43).query()),
        "exact MSP2 nonmembership query",
    )
    persistEquals(
        "4d535032020000000400000028000000040000002c",
        persistHex(tree.createRangeProof(40, 44).query()),
        "exact MSP2 range query",
    )
}

private fun saveLoadAndImportRoundTripExactClosure(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 513)
    val pack = tree.exportPack()
    persistCheck(tree.blockCount > 2, "test tree must span several blocks")
    persistEquals(tree.blockCount, pack.blockCount, "complete pack block count")
    persistCheck(pack.containsRootBlock, "complete pack must contain root")
    persistEquals(pack, tree.exportPack(), "complete export determinism")

    val store = InMemoryMerkleBlockStore()
    persistEquals(tree.blockCount, tree.save(store), "first save count")
    persistEquals(0, tree.save(store), "idempotent save count")
    assertPersistenceTreesEqual(tree, MerkleSearchTree.load(tree.rootHash, policy, store))

    val importedStore = InMemoryMerkleBlockStore()
    val imported = MerkleSearchTree.importPack(pack, policy, importedStore)
    assertPersistenceTreesEqual(tree, imported)
    persistEquals(tree.blockCount, importedStore.size, "imported store closure")
    persistEquals(pack, imported.exportPack(), "import exact re-export")

    val empty = MerkleSearchTree.empty(policy)
    assertPersistenceTreesEqual(empty, MerkleSearchTree.importPack(empty.exportPack(), policy))
    assertPersistenceTreesEqual(empty, MerkleSearchTree.load(empty.rootHash, policy, InMemoryMerkleBlockStore()))
}

private fun missingTamperedMalformedAndForeignDataAreRejected(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 257)
    val pack = tree.exportPack()
    val missing = pack.blocks.last()
    val incomplete = MerkleBlockPack(
        pack.algorithmId,
        pack.domainDigest,
        pack.rootHash,
        pack.blocks.filter { it.digest != missing.digest },
    )
    persistFailure(MerkleVerificationFailureKind.MISSING_BLOCK) {
        MerkleSearchTree.importPack(incomplete, policy)
    }
    val missingStore = InMemoryMerkleBlockStore()
    tree.save(missingStore)
    persistCheck(missingStore.remove(missing.digest), "load missing-block setup")
    persistFailure(MerkleVerificationFailureKind.MISSING_BLOCK) {
        MerkleSearchTree.load(tree.rootHash, policy, missingStore)
    }

    val root = pack.blocks.first { it.digest == pack.rootHash }
    val changedBytes = root.toByteArray().also { bytes -> bytes[bytes.lastIndex] = (bytes.last().toInt() xor 0x80).toByte() }
    val changed = replacePackBlock(pack, MerkleBlock(root.digest, changedBytes), pack.rootHash)
    persistFailure(MerkleVerificationFailureKind.DIGEST_MISMATCH) {
        MerkleSearchTree.importPack(changed, policy)
    }

    val trailing = addressedBlock(root.toByteArray() + 0)
    persistFailure(MerkleVerificationFailureKind.NON_CANONICAL_BLOCK) {
        MerkleSearchTree.importPack(
            MerkleBlockPack(pack.algorithmId, pack.domainDigest, trailing.digest, listOf(trailing)),
            policy,
        )
    }
    val wrongMagicBytes = root.toByteArray().also { it[0] = (it[0].toInt() xor 0xff).toByte() }
    val wrongMagic = addressedBlock(wrongMagicBytes)
    persistFailure(MerkleVerificationFailureKind.MALFORMED_BLOCK) {
        MerkleSearchTree.importPack(
            MerkleBlockPack(pack.algorithmId, pack.domainDigest, wrongMagic.digest, listOf(wrongMagic)),
            policy,
        )
    }

    val foreignPolicy = persistencePolicy("foreign-persistence-domain-v1")
    val foreignTree = persistenceTree(foreignPolicy, 17)
    persistFailure(MerkleVerificationFailureKind.DOMAIN_MISMATCH) {
        MerkleSearchTree.importPack(
            MerkleBlockPack(
                pack.algorithmId,
                policy.domainDigest,
                foreignTree.rootHash,
                foreignTree.exportPack().blocks,
            ),
            policy,
        )
    }
    persistFailure(MerkleVerificationFailureKind.UNSUPPORTED_ALGORITHM) {
        MerkleSearchTree.importPack(
            MerkleBlockPack("mst-sha256-b16-v999", pack.domainDigest, pack.rootHash, pack.blocks),
            policy,
        )
    }
}

private fun allSevenVerificationBudgetsAreIndependent(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 513)
    val pack = tree.exportPack()
    val rootLength = pack.blocks.first { it.digest == pack.rootHash }.length
    val maximumBlockLength = pack.blocks.maxOf(MerkleBlock::length)
    val defaults = MerkleVerificationBudget()

    val limits = listOf(
        defaults.copy(maxBlockCount = 1),
        MerkleVerificationBudget(
            maxTotalByteCount = maximumBlockLength.toLong(),
            maxBlockByteCount = maximumBlockLength,
            maxProofQueryByteCount = maximumBlockLength,
        ),
        defaults.copy(
            maxBlockByteCount = rootLength - 1,
            maxProofQueryByteCount = rootLength - 1,
        ),
        defaults.copy(maxDepth = 1),
        defaults.copy(maxEntryCount = 1),
        defaults.copy(maxChildReferencesPerBlock = 1),
    )
    for ((index, budget) in limits.withIndex()) {
        persistFailure(MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED) {
            MerkleSearchTree.importPack(pack, policy, budget = budget)
        }.also { persistCheck(it.message?.isNotBlank() == true, "budget $index diagnostic") }
    }

    val proof = tree.createProof(0)
    val queryBudget = defaults.copy(maxProofQueryByteCount = proof.query().size - 1)
    val result = MerkleSearchTree.verifyProof(proof, policy, queryBudget)
    persistCheck(!result.isValid, "query budget must fail")
    persistEquals(MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED, result.failureKind, "query budget kind")
    persistEquals(0, result.verifiedBlockCount, "query budget block accounting")
    persistEquals(0L, result.verifiedByteCount, "query budget byte accounting")

    val sixLimitSemantics = MerkleVerificationBudget(maxBlockByteCount = 777)
    persistEquals(777, sixLimitSemantics.maxProofQueryByteCount, "omitted query limit follows block limit")
    val copiedSixLimitSemantics = MerkleVerificationBudget().copy(
        maxTotalByteCount = 1_024,
        maxBlockByteCount = 1_024,
    )
    persistEquals(1_024, copiedSixLimitSemantics.maxProofQueryByteCount, "copied query limit follows block limit")
    persistThrows<IllegalArgumentException> { MerkleVerificationBudget(maxBlockCount = 0) }
    persistThrows<IllegalArgumentException> {
        MerkleVerificationBudget(maxTotalByteCount = 10, maxBlockByteCount = 11, maxProofQueryByteCount = 10)
    }
    persistThrows<IllegalArgumentException> {
        MerkleVerificationBudget(maxTotalByteCount = 10, maxBlockByteCount = 10, maxProofQueryByteCount = 11)
    }
}

private fun authenticatedCountAndIntervalCorruptionAreRejected(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 2_049)
    val pack = tree.exportPack()
    val root = pack.blocks.first { it.digest == pack.rootHash }

    val wrongCountBytes = root.toByteArray()
    writeTestInt(wrongCountBytes, 38, readTestInt(wrongCountBytes, 38) + 1)
    val wrongCount = addressedBlock(wrongCountBytes)
    persistFailure(MerkleVerificationFailureKind.INVALID_REFERENCE) {
        MerkleSearchTree.importPack(replaceRoot(pack, wrongCount), policy)
    }

    val crossedBytes = root.toByteArray()
    val childOffset = testChildDigestOffset(crossedBytes)
    val childCount = testChildDigestCount(crossedBytes)
    val nonempty = (0 until childCount).filter { index ->
        MerkleDigest.fromBytes(crossedBytes.copyOfRange(childOffset + index * 32, childOffset + (index + 1) * 32)) !=
            policy.emptyDigest
    }
    persistCheck(nonempty.size >= 2, "root must expose two nonempty child intervals")
    val first = childOffset + nonempty[0] * 32
    val second = childOffset + nonempty[1] * 32
    repeat(32) { byte ->
        val temporary = crossedBytes[first + byte]
        crossedBytes[first + byte] = crossedBytes[second + byte]
        crossedBytes[second + byte] = temporary
    }
    persistFailure(MerkleVerificationFailureKind.INVALID_REFERENCE) {
        MerkleSearchTree.importPack(replaceRoot(pack, addressedBlock(crossedBytes)), policy)
    }
}

private fun saveAndImportPreflightBeforeAnyWrite(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 257)
    val pack = tree.exportPack()
    val late = pack.blocks.last()
    val conflict = MerkleBlock(late.digest, byteArrayOf(0xde.toByte(), 0xad.toByte()))

    val saveStore = RecordingMerkleStore().also { it.seed(conflict) }
    persistFailure(MerkleVerificationFailureKind.CONFLICTING_BLOCK) { tree.save(saveStore) }
    persistEquals(0, saveStore.putCalls, "save conflict must preflight before writes")
    persistEquals(conflict, saveStore.get(late.digest), "save conflict must remain stored")

    val importStore = RecordingMerkleStore().also { it.seed(conflict) }
    persistFailure(MerkleVerificationFailureKind.CONFLICTING_BLOCK) {
        MerkleSearchTree.importPack(pack, policy, importStore)
    }
    persistEquals(0, importStore.putCalls, "import conflict must preflight before writes")

    val sentinel = addressedBlock("sentinel".encodeToByteArray())
    val failureStore = RecordingMerkleStore().also { it.seed(sentinel) }
    val root = pack.blocks.first { it.digest == pack.rootHash }
    val broken = MerkleBlock(
        root.digest,
        root.toByteArray().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() },
    )
    persistFailure(MerkleVerificationFailureKind.DIGEST_MISMATCH) {
        MerkleSearchTree.importPack(replacePackBlock(pack, broken, pack.rootHash), policy, failureStore)
    }
    persistEquals(0, failureStore.putCalls, "verification failure must precede destination writes")
    persistEquals(sentinel, failureStore.get(sentinel.digest), "verification failure preserves destination")
}

private fun completePartialAndIterativeSynchronizationConverge(): Unit {
    val policy = persistencePolicy()
    val target = persistenceTree(policy, 513)
    val local = MerkleSearchTree.empty(policy)

    val emptyStore = InMemoryMerkleBlockStore()
    persistEquals(target.exportPack(), target.createSyncPack(emptyStore), "empty receiver sync pack")
    assertPersistenceTreesEqual(
        target,
        MerkleSearchTree.importPack(target.createSyncPack(emptyStore), policy, emptyStore),
    )
    val rootOnlyAssumption = InMemoryMerkleBlockStore()
    rootOnlyAssumption.put(target.exportPack().blocks.first())
    persistEquals(0, target.createSyncPack(rootOnlyAssumption).blockCount, "known closure root prunes descendants")

    val partialStore = InMemoryMerkleBlockStore()
    target.save(partialStore)
    val missing = target.exportPack().blocks.last()
    persistCheck(missing.digest != target.rootHash, "partial test removes a descendant")
    persistCheck(partialStore.remove(missing.digest), "descendant removal")
    val plan = target.planSync(local, partialStore)
    persistEquals(listOf(missing.digest), plan.requestedBlocks, "partial frontier request")
    persistCheck(plan.examinedBlockCount > 0 && plan.examinedByteCount > 0, "partial plan diagnostics")
    val partialPack = target.exportPack(plan.requestedBlocks)
    persistCheck(!partialPack.containsRootBlock, "partial pack need not contain root")
    val repaired = MerkleSearchTree.importPack(partialPack, policy, partialStore)
    assertPersistenceTreesEqual(target, repaired)
    persistCheck(target.planSync(repaired, partialStore).rootsMatch, "published roots converge")

    val frontierStore = InMemoryMerkleBlockStore()
    var rounds = 0
    while (true) {
        val frontier = target.planSync(local, frontierStore)
        if (!frontier.requiresBlocks) break
        rounds++
        for (block in target.exportPack(frontier.requestedBlocks).blocks) frontierStore.put(block)
    }
    persistCheck(rounds >= target.height, "frontier repair advances one revealed layer per round")
    assertPersistenceTreesEqual(target, MerkleSearchTree.load(target.rootHash, policy, frontierStore))
}

private fun pointRangeAndEmptyProofsVerifyCanonically(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 513)
    val membership = tree.createProof(0)
    val nonmembership = tree.createProof(10_000)
    val range = tree.createRangeProof(-20, 20)
    persistEquals(MerkleProofKind.MEMBERSHIP, membership.kind, "membership kind")
    persistEquals(MerkleProofKind.NON_MEMBERSHIP, nonmembership.kind, "nonmembership kind")
    persistEquals(MerkleProofKind.RANGE, range.kind, "range kind")
    for (proof in listOf(membership, nonmembership, range)) assertPersistenceProofValid(proof, policy)
    persistCheck(range.steps.size >= membership.steps.size, "range expands at least one point path")

    val empty = MerkleSearchTree.empty(policy)
    assertPersistenceProofValid(empty.createProof(1), policy)
    assertPersistenceProofValid(empty.createRangeProof(-1, 1), policy)
}

private fun proofQueryAndShapeBudgetsPrecedeEnvelopeCodecsAndBlocks(): Unit {
    val keyCodec = CountingMerkleCodec(Int32MerkleCodec)
    val valueCodec = CountingMerkleCodec(NullableUtf8MerkleCodec)
    val policy = MerkleSearchTreePolicy.natural(
        "proof-query-budget-v1",
        keyCodec,
        valueCodec,
    )
    for (tree in listOf(
        MerkleSearchTree.empty(policy),
        MerkleSearchTree.empty(policy).setItem(1, "one"),
    )) {
        val proof = tree.createProof(1)
        keyCodec.reset()
        valueCodec.reset()
        val budget = MerkleVerificationBudget(
            maxTotalByteCount = 1L shl 20,
            maxBlockByteCount = 1 shl 20,
            maxProofQueryByteCount = proof.query().size - 1,
        )
        val result = MerkleSearchTree.verifyProof(proof, policy, budget)
        persistEquals(MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED, result.failureKind, "early query gate")
        persistEquals(0, result.verifiedBlockCount, "early query gate blocks")
        persistEquals(0L, result.verifiedByteCount, "early query gate bytes")
        persistEquals(0 to 0, keyCodec.counts(), "early query gate key codec")
        persistEquals(0 to 0, valueCodec.counts(), "early query gate value codec")
    }

    val deepTree = persistenceTree(policy, 513)
    val deepProof = (-256..256).asSequence().map(deepTree::createProof).first { it.steps.size > 1 }
    keyCodec.reset()
    valueCodec.reset()
    val stepBudget = MerkleVerificationBudget(maxBlockCount = deepProof.steps.size - 1)
    val stepResult = MerkleSearchTree.verifyProof(deepProof, policy, stepBudget)
    persistEquals(MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED, stepResult.failureKind, "proof step preflight")
    persistEquals(0, stepResult.verifiedBlockCount, "proof step preflight blocks")
    persistEquals(deepProof.query().size.toLong(), stepResult.verifiedByteCount, "proof step preflight bytes")
    persistEquals(0 to 0, keyCodec.counts(), "proof step preflight key codec")
    persistEquals(0 to 0, valueCodec.counts(), "proof step preflight value codec")

    val foreignOversizedProof = MerkleProof(
        "foreign-merkle-algorithm-v1",
        deepProof.domainDigest,
        deepProof.rootHash,
        deepProof.kind,
        deepProof.query(),
        deepProof.steps,
    )
    val precedenceResult = MerkleSearchTree.verifyProof(foreignOversizedProof, policy, stepBudget)
    persistEquals(
        MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
        precedenceResult.failureKind,
        "proof shape precedes envelope",
    )
    persistEquals(0, precedenceResult.verifiedBlockCount, "proof shape precedence blocks")
    persistEquals(
        foreignOversizedProof.query().size.toLong(),
        precedenceResult.verifiedByteCount,
        "proof shape precedence bytes",
    )

    val expandedSteps = deepProof.steps.toMutableList()
    expandedSteps[0] = MerkleProofStep(expandedSteps[0].block, listOf(0, 1))
    val expandedProof = rebuildProof(deepProof, steps = expandedSteps)
    keyCodec.reset()
    valueCodec.reset()
    val expansionResult = MerkleSearchTree.verifyProof(
        expandedProof,
        policy,
        MerkleVerificationBudget(maxChildReferencesPerBlock = 1),
    )
    persistEquals(
        MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
        expansionResult.failureKind,
        "proof expansion preflight",
    )
    persistEquals(0, expansionResult.verifiedBlockCount, "proof expansion preflight blocks")
    persistEquals(
        expandedProof.query().size.toLong(),
        expansionResult.verifiedByteCount,
        "proof expansion preflight bytes",
    )
    persistEquals(0 to 0, keyCodec.counts(), "proof expansion preflight key codec")
    persistEquals(0 to 0, valueCodec.counts(), "proof expansion preflight value codec")
}

private fun proofsRejectTamperingExtrasAndBadExpansions(): Unit {
    val policy = persistencePolicy()
    val tree = persistenceTree(policy, 513)
    val proof = (-256..256).asSequence().map(tree::createProof).first { it.steps.size > 1 }
    val first = proof.steps.first()

    val changedBlock = MerkleBlock(
        first.block.digest,
        first.block.toByteArray().also { it[it.lastIndex] = (it.last().toInt() xor 0x40).toByte() },
    )
    assertPersistenceProofFailure(
        rebuildProof(proof, steps = listOf(MerkleProofStep(changedBlock, first.expandedChildIndexes)) + proof.steps.drop(1)),
        policy,
        MerkleVerificationFailureKind.DIGEST_MISMATCH,
    )
    assertPersistenceProofFailure(
        rebuildProof(proof, query = proof.query() + 0),
        policy,
        MerkleVerificationFailureKind.PROOF_MISMATCH,
    )
    val changedValue = proof.query().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }
    assertPersistenceProofFailure(
        rebuildProof(proof, query = changedValue),
        policy,
        MerkleVerificationFailureKind.PROOF_MISMATCH,
    )

    val proofDigests = proof.steps.map { it.block.digest }.toSet()
    val extra = tree.exportPack().blocks.first { it.digest !in proofDigests }
    assertPersistenceProofFailure(
        rebuildProof(proof, steps = proof.steps + MerkleProofStep(extra, emptyList())),
        policy,
        MerkleVerificationFailureKind.PROOF_MISMATCH,
    )
    assertPersistenceProofFailure(
        rebuildProof(
            proof,
            steps = listOf(MerkleProofStep(first.block, emptyList())) + proof.steps.drop(1),
        ),
        policy,
        MerkleVerificationFailureKind.PROOF_MISMATCH,
    )
    assertPersistenceProofFailure(
        rebuildProof(proof, steps = proof.steps.dropLast(1)),
        policy,
        MerkleVerificationFailureKind.MISSING_BLOCK,
    )
    persistThrows<IllegalArgumentException> { MerkleProofStep(first.block, listOf(0, 0)) }
    persistFailure(MerkleVerificationFailureKind.DUPLICATE_BLOCK) {
        MerkleProof(
            proof.algorithmId,
            proof.domainDigest,
            proof.rootHash,
            proof.kind,
            proof.query(),
            listOf(first, first),
        )
    }
    assertPersistenceProofFailure(
        MerkleProof(
            "mst-sha256-b16-v999",
            proof.domainDigest,
            proof.rootHash,
            proof.kind,
            proof.query(),
            proof.steps,
        ),
        policy,
        MerkleVerificationFailureKind.UNSUPPORTED_ALGORITHM,
    )
    assertPersistenceProofFailure(
        MerkleProof(
            proof.algorithmId,
            persistencePolicy("other-proof-domain-v1").domainDigest,
            proof.rootHash,
            proof.kind,
            proof.query(),
            proof.steps,
        ),
        policy,
        MerkleVerificationFailureKind.DOMAIN_MISMATCH,
    )
    assertPersistenceProofFailure(
        MerkleProof(
            proof.algorithmId,
            proof.domainDigest,
            MerkleDigest.hash("foreign-root".encodeToByteArray()),
            proof.kind,
            proof.query(),
            proof.steps,
        ),
        policy,
        MerkleVerificationFailureKind.ROOT_MISMATCH,
    )
    assertPersistenceProofFailure(
        rebuildProof(
            proof,
            steps = listOf(MerkleProofStep(first.block, listOf(Int.MAX_VALUE))) + proof.steps.drop(1),
        ),
        policy,
        MerkleVerificationFailureKind.PROOF_MISMATCH,
    )
}

private fun threeWayMergeCombinesResolvesAndWithholds(): Unit {
    val policy = persistencePolicy()
    val base = MerkleSearchTree.empty(policy)
        .setItem(1, "one")
        .setItem(2, "two")
        .setItem(3, "three")
    val left = base.setItem(1, "ONE")
    val right = base.setItem(2, "TWO")
    var resolverCalls = 0
    val disjoint = MerkleSearchTree.merge(base, left, right, resolver = {
        resolverCalls++
        error("A resolver must not run for disjoint edits.")
    })
    persistCheck(disjoint.isSuccess, "disjoint merge succeeds")
    persistEquals(0, resolverCalls, "disjoint resolver calls")
    persistEquals(listOf("ONE", "TWO", "three"), disjoint.mergedTree!!.map { it.value }, "disjoint merge values")

    val sameLeft = base.setItem(3, "THREE")
    val sameRight = base.setItem(3, "THREE")
    persistEquals(
        "THREE",
        MerkleSearchTree.merge(base, sameLeft, sameRight).mergedTree!!.getEntry(3)!!.value,
        "identical descendant edit",
    )

    val conflictLeft = base.setItem(1, "left").setItem(2, "left-two")
    val conflictRight = base.setItem(1, "right").setItem(2, "right-two")
    val unresolved = MerkleSearchTree.merge(base, conflictLeft, conflictRight)
    persistCheck(!unresolved.isSuccess && unresolved.mergedTree == null, "conflicts withhold output")
    persistEquals(2, unresolved.unresolvedConflicts.size, "all unresolved conflicts reported")
    assertPersistenceListUnmodifiable(unresolved.unresolvedConflicts, "merge conflicts")
    val first = unresolved.unresolvedConflicts.first()
    persistEquals(MerkleMergeValue.Present("one"), first.base, "conflict base state")
    persistEquals(MerkleMergeValue.Present("left"), first.left, "conflict left state")
    persistEquals(MerkleMergeValue.Present("right"), first.right, "conflict right state")

    val resolutions = listOf<Pair<MerkleMergeResolution<String?>, String?>>(
        MerkleMergeResolution.UseBase to "one",
        MerkleMergeResolution.UseLeft to "left",
        MerkleMergeResolution.UseRight to "right",
        MerkleMergeResolution.SetValue("resolved") to "resolved",
    )
    for ((resolution, expected) in resolutions) {
        val merged = MerkleSearchTree.merge(
            base,
            base.setItem(1, "left"),
            base.setItem(1, "right"),
            resolver = { resolution },
        ).mergedTree!!
        persistEquals(expected, merged.getEntry(1)!!.value, "merge resolution $resolution")
    }
    val deleted = MerkleSearchTree.merge(
        base,
        base.setItem(1, "left"),
        base.setItem(1, "right"),
        resolver = { MerkleMergeResolution.Delete },
    ).mergedTree!!
    persistCheck(!deleted.containsKey(1), "delete resolution")
}

private fun mergePreservesPresentNullAndReusesCanonicalEntries(): Unit {
    val valueCodec = CountingMerkleCodec(NullableUtf8MerkleCodec)
    val policy = MerkleSearchTreePolicy.natural(
        "merge-present-null-v1",
        Int32MerkleCodec,
        valueCodec,
    )
    val base = MerkleSearchTree.empty(policy).setItem(1, "base").setItem(2, "two")
    val presentNull = base.setItem(1, null)
    val deleted = base.remove(1)
    valueCodec.reset()
    val unresolved = MerkleSearchTree.merge(base, presentNull, deleted)
    val conflict = unresolved.unresolvedConflicts.single()
    persistEquals(MerkleMergeValue.Present(null), conflict.left, "present null remains present")
    persistEquals(MerkleMergeValue.Absent, conflict.right, "deletion remains absent")
    persistEquals(0 to 0, valueCodec.counts(), "unresolved merge reuses canonical entries")

    valueCodec.reset()
    val merged = MerkleSearchTree.merge(
        base,
        presentNull,
        deleted,
        resolver = { MerkleMergeResolution.SetValue(null) },
    ).mergedTree!!
    persistCheck(merged.containsKey(1), "resolved null remains present")
    persistEquals(null, merged.getEntry(1)!!.value, "resolved null value")
    persistEquals(1 to 0, valueCodec.counts(), "custom merge value alone is encoded")
}

private fun inMemoryStoreIsConcurrentIdempotentAndImmutable(): Unit {
    val tree = persistenceTree(persistencePolicy(), 513)
    val blocks = tree.exportPack().blocks
    val store = InMemoryMerkleBlockStore()
    val failures = ConcurrentLinkedQueue<Throwable>()
    val pool = Executors.newFixedThreadPool(8)
    repeat(8) {
        pool.submit {
            try {
                repeat(8) {
                    for (block in blocks) {
                        store.put(block)
                        persistEquals(block, store.get(block.digest), "concurrent block read")
                    }
                }
            } catch (exception: Throwable) {
                failures.add(exception)
            }
        }
    }
    pool.shutdown()
    persistCheck(pool.awaitTermination(60, TimeUnit.SECONDS), "store workers terminate")
    if (failures.isNotEmpty()) throw AssertionError("Concurrent store worker failed", failures.first())
    persistEquals(blocks.size, store.size, "concurrent idempotent address count")
    persistEquals(blocks.map { it.digest }.sorted(), store.digests, "sorted digest snapshot")

    val first = blocks.first()
    persistFailure(MerkleVerificationFailureKind.CONFLICTING_BLOCK) {
        store.put(MerkleBlock(first.digest, "different".encodeToByteArray()))
    }
    val publicBytes = first.toByteArray().also { it.fill(0) }
    persistCheck(!publicBytes.contentEquals(store.get(first.digest)!!.toByteArray()), "block bytes are defensive")
    val proof = tree.createProof(0)
    val query = proof.query().also { it.fill(0) }
    persistCheck(!query.contentEquals(proof.query()), "proof query bytes are defensive")
    assertPersistenceListUnmodifiable(tree.exportPack().blocks, "pack blocks")
    assertPersistenceListUnmodifiable(proof.steps, "proof steps")
    assertPersistenceListUnmodifiable(proof.steps.first().expandedChildIndexes, "expanded indexes")
    assertPersistenceListUnmodifiable(
        tree.planSync(MerkleSearchTree.empty(tree.policy), InMemoryMerkleBlockStore()).requestedBlocks,
        "sync requests",
    )
}

private fun duplicateAndUnknownAddressesAreRejected(): Unit {
    val tree = persistenceTree(persistencePolicy(), 65)
    val pack = tree.exportPack()
    val first = pack.blocks.first()
    persistFailure(MerkleVerificationFailureKind.DUPLICATE_BLOCK) {
        MerkleBlockPack(pack.algorithmId, pack.domainDigest, pack.rootHash, listOf(first, first))
    }
    persistFailure(MerkleVerificationFailureKind.DUPLICATE_BLOCK) {
        tree.exportPack(listOf(first.digest, first.digest))
    }
    persistFailure(MerkleVerificationFailureKind.MISSING_BLOCK) {
        tree.exportPack(listOf(MerkleDigest.hash("unknown".encodeToByteArray())))
    }
}

/** Run the Merkle storage, verification, and synchronization cases. */
internal fun runMerklePersistenceTests(): Unit {
    val tests = listOf(
        "exactGoldenBlockAndMsp2Queries" to ::exactGoldenBlockAndMsp2Queries,
        "saveLoadAndImportRoundTripExactClosure" to ::saveLoadAndImportRoundTripExactClosure,
        "missingTamperedMalformedAndForeignDataAreRejected" to ::missingTamperedMalformedAndForeignDataAreRejected,
        "allSevenVerificationBudgetsAreIndependent" to ::allSevenVerificationBudgetsAreIndependent,
        "authenticatedCountAndIntervalCorruptionAreRejected" to ::authenticatedCountAndIntervalCorruptionAreRejected,
        "saveAndImportPreflightBeforeAnyWrite" to ::saveAndImportPreflightBeforeAnyWrite,
        "completePartialAndIterativeSynchronizationConverge" to ::completePartialAndIterativeSynchronizationConverge,
        "pointRangeAndEmptyProofsVerifyCanonically" to ::pointRangeAndEmptyProofsVerifyCanonically,
        "proofQueryAndShapeBudgetsPrecedeEnvelopeCodecsAndBlocks" to
            ::proofQueryAndShapeBudgetsPrecedeEnvelopeCodecsAndBlocks,
        "proofsRejectTamperingExtrasAndBadExpansions" to ::proofsRejectTamperingExtrasAndBadExpansions,
        "threeWayMergeCombinesResolvesAndWithholds" to ::threeWayMergeCombinesResolvesAndWithholds,
        "mergePreservesPresentNullAndReusesCanonicalEntries" to ::mergePreservesPresentNullAndReusesCanonicalEntries,
        "inMemoryStoreIsConcurrentIdempotentAndImmutable" to ::inMemoryStoreIsConcurrentIdempotentAndImmutable,
        "duplicateAndUnknownAddressesAreRejected" to ::duplicateAndUnknownAddressesAreRejected,
    )
    for ((name, test) in tests) {
        test()
        println("PASS mst.persistence.$name")
    }
}

private fun assertPersistenceTreesEqual(expected: PersistenceTree, actual: PersistenceTree): Unit {
    persistEquals(expected.rootHash, actual.rootHash, "tree root")
    persistEquals(expected.map { it.key to it.value }, actual.map { it.key to it.value }, "tree entries")
    persistEquals(expected.validateStructure(), actual.validateStructure(), "tree statistics")
    persistEquals(expected.exportPack(), actual.exportPack(), "tree exact closure")
}

private fun assertPersistenceProofValid(
    proof: MerkleProof,
    policy: MerkleSearchTreePolicy<Int, String?>,
): Unit {
    val result = MerkleSearchTree.verifyProof(proof, policy)
    persistCheck(result.isValid, "proof failed: ${result.failureKind}: ${result.failureMessage}")
    persistEquals(proof.rootHash, result.computedRootHash, "verified proof root")
    persistEquals(proof.steps.size, result.verifiedBlockCount, "verified proof blocks")
    persistEquals(proof.totalByteCount, result.verifiedByteCount, "verified proof bytes")
}

private fun assertPersistenceProofFailure(
    proof: MerkleProof,
    policy: MerkleSearchTreePolicy<Int, String?>,
    expected: MerkleVerificationFailureKind,
): Unit {
    val result = MerkleSearchTree.verifyProof(proof, policy)
    persistCheck(!result.isValid, "proof unexpectedly verified")
    persistEquals(expected, result.failureKind, "proof failure kind")
}

private fun rebuildProof(
    proof: MerkleProof,
    query: ByteArray = proof.query(),
    steps: List<MerkleProofStep> = proof.steps,
): MerkleProof = MerkleProof(
    proof.algorithmId,
    proof.domainDigest,
    proof.rootHash,
    proof.kind,
    query,
    steps,
)

private fun addressedBlock(bytes: ByteArray): MerkleBlock = MerkleBlock(MerkleDigest.hash(bytes), bytes)

private fun replaceRoot(pack: MerkleBlockPack, replacement: MerkleBlock): MerkleBlockPack =
    replacePackBlock(pack, replacement, replacement.digest)

private fun replacePackBlock(
    pack: MerkleBlockPack,
    replacement: MerkleBlock,
    rootHash: MerkleDigest,
): MerkleBlockPack = MerkleBlockPack(
    pack.algorithmId,
    pack.domainDigest,
    rootHash,
    pack.blocks.map { block -> if (block.digest == pack.rootHash) replacement else block },
)

private fun testChildDigestOffset(block: ByteArray): Int {
    var offset = MerkleBlockHeaderLength
    val entryCount = readTestInt(block, 42)
    repeat(entryCount) {
        val keyLength = readTestInt(block, offset)
        offset += 4 + keyLength
        val valueLength = readTestInt(block, offset)
        offset += 4 + valueLength
    }
    return offset
}

private fun testChildDigestCount(block: ByteArray): Int = readTestInt(block, 42) + 1

private fun readTestInt(bytes: ByteArray, offset: Int): Int =
    (bytes[offset].toInt() and 0xff shl 24) or
        (bytes[offset + 1].toInt() and 0xff shl 16) or
        (bytes[offset + 2].toInt() and 0xff shl 8) or
        (bytes[offset + 3].toInt() and 0xff)

private fun writeTestInt(bytes: ByteArray, offset: Int, value: Int): Unit {
    bytes[offset] = (value ushr 24).toByte()
    bytes[offset + 1] = (value ushr 16).toByte()
    bytes[offset + 2] = (value ushr 8).toByte()
    bytes[offset + 3] = value.toByte()
}

private class RecordingMerkleStore : MerkleBlockStore {
    private val blocks = HashMap<MerkleDigest, MerkleBlock>()
    var putCalls: Int = 0
        private set

    override val size: Int get() = blocks.size
    override val digests: List<MerkleDigest> get() = blocks.keys.sorted()
    override fun contains(digest: MerkleDigest): Boolean = blocks.containsKey(digest)
    override fun get(digest: MerkleDigest): MerkleBlock? = blocks[digest]
    override fun put(block: MerkleBlock): Boolean {
        putCalls++
        val existing = blocks[block.digest]
        if (existing != null) {
            if (existing == block) return false
            throw verificationFailure(
                MerkleVerificationFailureKind.CONFLICTING_BLOCK,
                "Recording store conflict.",
                block.digest,
            )
        }
        blocks[block.digest] = block
        return true
    }
    override fun remove(digest: MerkleDigest): Boolean = blocks.remove(digest) != null
    override fun clear(): Unit = blocks.clear()
    fun seed(block: MerkleBlock): Unit {
        blocks[block.digest] = block
    }
}

private class CountingMerkleCodec<T>(private val inner: MerkleCodec<T>) : MerkleCodec<T> {
    private val encodes = AtomicInteger()
    private val decodes = AtomicInteger()
    override val encodingId: String get() = inner.encodingId
    override fun encode(value: T): ByteArray {
        encodes.incrementAndGet()
        return inner.encode(value)
    }
    override fun decode(encoding: ByteArray): T {
        decodes.incrementAndGet()
        return inner.decode(encoding)
    }
    fun reset(): Unit {
        encodes.set(0)
        decodes.set(0)
    }
    fun counts(): Pair<Int, Int> = encodes.get() to decodes.get()
}

private fun persistCheck(condition: Boolean, message: String): Unit {
    if (!condition) throw AssertionError(message)
}

private fun <T> persistEquals(expected: T, actual: T, message: String): Unit {
    if (expected != actual) throw AssertionError("$message: expected <$expected>, actual <$actual>")
}

private inline fun <reified T : Throwable> persistThrows(operation: () -> Unit): T {
    try {
        operation()
    } catch (exception: Throwable) {
        if (exception is T) return exception
        throw AssertionError("Expected ${T::class.simpleName}, received ${exception::class.simpleName}", exception)
    }
    throw AssertionError("Expected ${T::class.simpleName}")
}

private fun persistFailure(
    expected: MerkleVerificationFailureKind,
    operation: () -> Unit,
): MerkleVerificationException {
    val exception = persistThrows<MerkleVerificationException>(operation)
    persistEquals(expected, exception.failureKind, "verification failure kind")
    return exception
}

@Suppress("UNCHECKED_CAST")
private fun assertPersistenceListUnmodifiable(values: List<*>, message: String): Unit {
    val mutable = values as MutableList<Any?>
    persistThrows<UnsupportedOperationException> { mutable.clear() }
        .also { persistCheck(it.message == null || it.message!!.isNotBlank(), message) }
}

private fun persistHex(bytes: ByteArray): String = buildString(bytes.size * 2) {
    val digits = "0123456789abcdef"
    for (byte in bytes) {
        val value = byte.toInt() and 0xff
        append(digits[value ushr 4])
        append(digits[value and 15])
    }
}
