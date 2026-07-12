package tools.datastructures.hamt

import java.nio.charset.StandardCharsets

private val MerkleProofMagic: ByteArray = "MSP2".toByteArray(StandardCharsets.US_ASCII)

/** Creates the canonical exact `MSP2` membership or nonmembership proof for [key]. */
public fun <K, V> MerkleSearchTree<K, V>.createProof(key: K): MerkleProof {
    val keyBytes = policy.keyCodec.encode(key)
    val steps = ArrayList<MerkleProofStep>()
    var node = root
    var kind = MerkleProofKind.NON_MEMBERSHIP
    var valueBytes: ByteArray? = null
    while (node != null) {
        val position = findPosition(node.entries, key)
        val expanded = if (!position.found && node.children[position.index] != null) {
            listOf(position.index)
        } else {
            emptyList()
        }
        steps.add(MerkleProofStep(MerkleBlock(node.digest, node.blockBytes), expanded))
        if (position.found) {
            kind = MerkleProofKind.MEMBERSHIP
            valueBytes = node.entries[position.index].valueBytesInternal().copyOf()
            break
        }
        node = node.children[position.index]
    }
    return MerkleProof(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        policy.domainDigest,
        rootHash,
        kind,
        encodePointQuery(kind, keyBytes, valueBytes),
        steps,
    )
}

/** Creates a canonical completeness proof for the inclusive comparator-ordered range. */
public fun <K, V> MerkleSearchTree<K, V>.createRangeProof(
    minimumKey: K,
    maximumKey: K,
): MerkleProof {
    require(policy.comparator.compare(minimumKey, maximumKey) <= 0) {
        "The minimum key must not follow the maximum key."
    }
    val steps = ArrayList<MerkleProofStep>()
    collectRangeProof(root, minimumKey, maximumKey, steps)
    return MerkleProof(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        policy.domainDigest,
        rootHash,
        MerkleProofKind.RANGE,
        encodeRangeQuery(policy.keyCodec.encode(minimumKey), policy.keyCodec.encode(maximumKey)),
        steps,
    )
}

private fun encodePointQuery(
    kind: MerkleProofKind,
    keyBytes: ByteArray,
    valueBytes: ByteArray?,
): ByteArray {
    require(kind == MerkleProofKind.MEMBERSHIP || kind == MerkleProofKind.NON_MEMBERSHIP)
    require((kind == MerkleProofKind.MEMBERSHIP) == (valueBytes != null)) {
        "Only a membership query carries canonical value bytes."
    }
    var length = Math.addExact(MerkleProofMagic.size + 1 + Int.SIZE_BYTES, keyBytes.size)
    if (valueBytes != null) length = Math.addExact(length, Math.addExact(Int.SIZE_BYTES, valueBytes.size))
    val result = ByteArray(length)
    var offset = 0
    MerkleProofMagic.copyInto(result, offset)
    offset += MerkleProofMagic.size
    result[offset++] = kind.wireTag.toByte()
    offset = writeQueryField(result, offset, keyBytes)
    if (valueBytes != null) offset = writeQueryField(result, offset, valueBytes)
    check(offset == result.size)
    return result
}

private fun encodeRangeQuery(minimumBytes: ByteArray, maximumBytes: ByteArray): ByteArray {
    var length = Math.addExact(MerkleProofMagic.size + 1 + Int.SIZE_BYTES * 2, minimumBytes.size)
    length = Math.addExact(length, maximumBytes.size)
    val result = ByteArray(length)
    var offset = 0
    MerkleProofMagic.copyInto(result, offset)
    offset += MerkleProofMagic.size
    result[offset++] = MerkleProofKind.RANGE.wireTag.toByte()
    offset = writeQueryField(result, offset, minimumBytes)
    offset = writeQueryField(result, offset, maximumBytes)
    check(offset == result.size)
    return result
}

private fun writeQueryField(destination: ByteArray, offset: Int, field: ByteArray): Int {
    writeMerkleInt(destination, offset, field.size)
    field.copyInto(destination, offset + Int.SIZE_BYTES)
    return Math.addExact(offset, Math.addExact(Int.SIZE_BYTES, field.size))
}

private fun <K, V> MerkleSearchTree<K, V>.collectRangeProof(
    node: MerkleNode<K, V>?,
    minimumKey: K,
    maximumKey: K,
    steps: MutableList<MerkleProofStep>,
): Unit {
    if (node == null) return
    val expanded = ArrayList<Int>()
    for (index in node.children.indices) {
        if (node.children[index] != null &&
            childIntervalIntersects(node.entries, index, minimumKey, maximumKey)
        ) {
            expanded.add(index)
        }
    }
    steps.add(MerkleProofStep(MerkleBlock(node.digest, node.blockBytes), expanded))
    for (index in expanded) collectRangeProof(node.children[index], minimumKey, maximumKey, steps)
}

private fun <K, V> MerkleSearchTree<K, V>.childIntervalIntersects(
    entries: List<MerkleEntry<K, V>>,
    childIndex: Int,
    minimumKey: K,
    maximumKey: K,
): Boolean =
    (childIndex == 0 || policy.comparator.compare(entries[childIndex - 1].key, maximumKey) < 0) &&
        (childIndex == entries.size || policy.comparator.compare(entries[childIndex].key, minimumKey) > 0)

/** Verifies an untrusted membership, nonmembership, or range proof under finite limits. */
internal fun <K, V> verifyMerkleProof(
    proof: MerkleProof,
    policy: MerkleSearchTreePolicy<K, V>,
    budget: MerkleVerificationBudget = MerkleVerificationBudget(),
): MerkleProofVerificationResult {
    val context = MerkleVerificationContext(budget)
    return try {
        context.accountProofQuery(proof.queryInternal().size, proof.rootHash)
        if (proof.steps.size > budget.maxBlockCount) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "A Merkle proof exceeds the block-count budget.",
                proof.rootHash,
            )
        }
        if (proof.steps.any { step ->
                step.expandedChildIndexes.size > budget.maxChildReferencesPerBlock
            }
        ) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "A Merkle proof step exceeds the child-reference budget.",
                proof.rootHash,
            )
        }
        verifyMerkleEnvelope(proof.algorithmId, proof.domainDigest, policy)
        val verifier = MerkleSearchTree.empty(policy)
        val decoded = HashMap<MerkleDigest, DecodedMerkleBlock<K, V>>(proof.steps.size)
        val steps = HashMap<MerkleDigest, MerkleProofStep>(proof.steps.size)
        for (step in proof.steps) {
            val block = verifier.decodeMerkleBlock(step.block, context, 1)
            if (decoded.put(step.block.digest, block) != null ||
                steps.put(step.block.digest, step) != null
            ) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.DUPLICATE_BLOCK,
                    "The proof repeats block '${step.block.digest}'.",
                    step.block.digest,
                )
            }
            for (index in step.expandedChildIndexes) {
                if (index !in block.childDigests.indices) {
                    throw verificationFailure(
                        MerkleVerificationFailureKind.PROOF_MISMATCH,
                        "A proof expands a child index outside its block.",
                        step.block.digest,
                    )
                }
                if (block.childDigests[index] == policy.emptyDigest) {
                    throw verificationFailure(
                        MerkleVerificationFailureKind.PROOF_MISMATCH,
                        "A proof expands an empty child interval.",
                        step.block.digest,
                    )
                }
            }
        }

        if (proof.rootHash == policy.emptyDigest) {
            if (proof.steps.isNotEmpty()) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.PROOF_MISMATCH,
                    "An empty-root proof must not contain blocks.",
                    proof.rootHash,
                )
            }
            verifier.verifyEmptyProofQuery(proof)
        } else {
            if (!decoded.containsKey(proof.rootHash)) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.ROOT_MISMATCH,
                    "The proof does not contain its declared root block.",
                    proof.rootHash,
                )
            }
            val visited = HashSet<MerkleDigest>()
            when (proof.kind) {
                MerkleProofKind.MEMBERSHIP,
                MerkleProofKind.NON_MEMBERSHIP,
                -> verifier.verifyPointProof(proof, decoded, steps, visited, context)
                MerkleProofKind.RANGE ->
                    verifier.verifyRangeProof(proof, decoded, steps, visited, context)
            }
            if (visited.size != proof.steps.size) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.PROOF_MISMATCH,
                    "The proof contains blocks outside the canonical query expansion.",
                    proof.rootHash,
                )
            }
        }
        MerkleProofVerificationResult(
            true,
            MerkleVerificationFailureKind.NONE,
            null,
            proof.rootHash,
            context.blockCount,
            context.totalBytes,
        )
    } catch (exception: MerkleVerificationException) {
        MerkleProofVerificationResult(
            false,
            exception.failureKind,
            exception.message,
            proof.rootHash,
            context.blockCount,
            context.totalBytes,
        )
    }
}

private fun <K, V> MerkleSearchTree<K, V>.verifyPointProof(
    proof: MerkleProof,
    decoded: Map<MerkleDigest, DecodedMerkleBlock<K, V>>,
    steps: Map<MerkleDigest, MerkleProofStep>,
    visited: MutableSet<MerkleDigest>,
    context: MerkleVerificationContext,
): Unit {
    val query = decodePointQuery(proof)
    var digest = proof.rootHash
    var depth = 1
    while (true) {
        if (!visited.add(digest)) {
            throw verificationFailure(
                MerkleVerificationFailureKind.CYCLE_DETECTED,
                "A proof expansion contains a cycle.",
                digest,
            )
        }
        val block = decoded[digest] ?: throw verificationFailure(
            MerkleVerificationFailureKind.MISSING_BLOCK,
            "A proof omits an expanded point-query block.",
            digest,
        )
        val step = steps[digest] ?: throw verificationFailure(
            MerkleVerificationFailureKind.MISSING_BLOCK,
            "A proof omits an expanded point-query step.",
            digest,
        )
        context.checkDepth(depth, digest)
        val position = findPosition(block.entries, query.key)
        if (position.found) {
            requireExpandedExactly(step, emptyList())
            if (proof.kind != MerkleProofKind.MEMBERSHIP) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.PROOF_MISMATCH,
                    "The proof claims absence for a present key.",
                    digest,
                )
            }
            if (query.valueBytes == null ||
                !block.entries[position.index].valueBytesInternal().contentEquals(query.valueBytes)
            ) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.PROOF_MISMATCH,
                    "The membership proof's value claim does not match the authenticated entry.",
                    digest,
                )
            }
            return
        }
        val childDigest = block.childDigests[position.index]
        if (childDigest == policy.emptyDigest) {
            requireExpandedExactly(step, emptyList())
            if (proof.kind != MerkleProofKind.NON_MEMBERSHIP) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.PROOF_MISMATCH,
                    "The proof claims membership for an absent key.",
                    digest,
                )
            }
            return
        }
        requireExpandedExactly(step, listOf(position.index))
        val child = decoded[childDigest] ?: throw verificationFailure(
            MerkleVerificationFailureKind.MISSING_BLOCK,
            "A proof omits its expanded child block.",
            childDigest,
        )
        validateProofReference(block, position.index, child)
        digest = childDigest
        depth = Math.addExact(depth, 1)
    }
}

private fun <K, V> MerkleSearchTree<K, V>.verifyRangeProof(
    proof: MerkleProof,
    decoded: Map<MerkleDigest, DecodedMerkleBlock<K, V>>,
    steps: Map<MerkleDigest, MerkleProofStep>,
    visited: MutableSet<MerkleDigest>,
    context: MerkleVerificationContext,
): Unit {
    val query = decodeRangeQuery(proof)
    verifyRangeBlock(
        proof.rootHash,
        query.minimum,
        query.maximum,
        decoded,
        steps,
        visited,
        context,
        1,
    )
}

private fun <K, V> MerkleSearchTree<K, V>.verifyRangeBlock(
    digest: MerkleDigest,
    minimumKey: K,
    maximumKey: K,
    decoded: Map<MerkleDigest, DecodedMerkleBlock<K, V>>,
    steps: Map<MerkleDigest, MerkleProofStep>,
    visited: MutableSet<MerkleDigest>,
    context: MerkleVerificationContext,
    depth: Int,
): Unit {
    context.checkDepth(depth, digest)
    if (!visited.add(digest)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.CYCLE_DETECTED,
            "A range-proof expansion contains a repeated block.",
            digest,
        )
    }
    val block = decoded[digest] ?: throw verificationFailure(
        MerkleVerificationFailureKind.MISSING_BLOCK,
        "A range proof omits an expanded block.",
        digest,
    )
    val step = steps[digest] ?: throw verificationFailure(
        MerkleVerificationFailureKind.MISSING_BLOCK,
        "A range proof omits an expanded proof step.",
        digest,
    )
    val expected = ArrayList<Int>()
    for (index in block.childDigests.indices) {
        if (block.childDigests[index] != policy.emptyDigest &&
            childIntervalIntersects(block.entries, index, minimumKey, maximumKey)
        ) {
            expected.add(index)
        }
    }
    requireExpandedExactly(step, expected)
    for (index in expected) {
        val childDigest = block.childDigests[index]
        val child = decoded[childDigest] ?: throw verificationFailure(
            MerkleVerificationFailureKind.MISSING_BLOCK,
            "A range proof omits an expanded child block.",
            childDigest,
        )
        validateProofReference(block, index, child)
        verifyRangeBlock(
            childDigest,
            minimumKey,
            maximumKey,
            decoded,
            steps,
            visited,
            context,
            Math.addExact(depth, 1),
        )
    }
}

private fun <K, V> MerkleSearchTree<K, V>.validateProofReference(
    parent: DecodedMerkleBlock<K, V>,
    childIndex: Int,
    child: DecodedMerkleBlock<K, V>,
): Unit {
    if (child.level >= parent.level) {
        throw verificationFailure(
            MerkleVerificationFailureKind.INVALID_REFERENCE,
            "An expanded proof child is not below its parent level.",
            child.block.digest,
        )
    }
    for (entry in child.entries) {
        if (childIndex != 0 &&
            policy.comparator.compare(entry.key, parent.entries[childIndex - 1].key) <= 0
        ) {
            throw verificationFailure(
                MerkleVerificationFailureKind.INVALID_REFERENCE,
                "An expanded proof child crosses its lower separator.",
                child.block.digest,
            )
        }
        if (childIndex != parent.entries.size &&
            policy.comparator.compare(entry.key, parent.entries[childIndex].key) >= 0
        ) {
            throw verificationFailure(
                MerkleVerificationFailureKind.INVALID_REFERENCE,
                "An expanded proof child crosses its upper separator.",
                child.block.digest,
            )
        }
    }
}

private fun requireExpandedExactly(step: MerkleProofStep, expected: List<Int>): Unit {
    if (step.expandedChildIndexes != expected) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A proof step expands a noncanonical set of child intervals.",
            step.block.digest,
        )
    }
}

private data class PointProofQuery<K>(val key: K, val valueBytes: ByteArray?)
private data class RangeProofQuery<K>(val minimum: K, val maximum: K)

private fun <K, V> MerkleSearchTree<K, V>.decodePointQuery(proof: MerkleProof): PointProofQuery<K> {
    if (proof.kind != MerkleProofKind.MEMBERSHIP && proof.kind != MerkleProofKind.NON_MEMBERSHIP) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A point proof has the wrong proof kind.",
            proof.rootHash,
        )
    }
    val reader = readQueryHeader(proof)
    val keyBytes = reader.readLengthPrefixed()
    val valueBytes = if (proof.kind == MerkleProofKind.MEMBERSHIP) {
        val bytes = reader.readLengthPrefixed()
        decodeCanonicalQueryValue(bytes, proof.rootHash)
        bytes
    } else {
        null
    }
    if (!reader.atEnd) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A point-proof query has trailing bytes.",
            proof.rootHash,
        )
    }
    return PointProofQuery(decodeCanonicalQueryKey(keyBytes, proof.rootHash), valueBytes)
}

private fun <K, V> MerkleSearchTree<K, V>.decodeRangeQuery(proof: MerkleProof): RangeProofQuery<K> {
    if (proof.kind != MerkleProofKind.RANGE) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A range proof has the wrong proof kind.",
            proof.rootHash,
        )
    }
    val reader = readQueryHeader(proof)
    val minimum = decodeCanonicalQueryKey(reader.readLengthPrefixed(), proof.rootHash)
    val maximum = decodeCanonicalQueryKey(reader.readLengthPrefixed(), proof.rootHash)
    if (!reader.atEnd) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A range-proof query has trailing bytes.",
            proof.rootHash,
        )
    }
    if (policy.comparator.compare(minimum, maximum) > 0) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A range-proof query has reversed bounds.",
            proof.rootHash,
        )
    }
    return RangeProofQuery(minimum, maximum)
}

private fun readQueryHeader(proof: MerkleProof): MerkleByteReader {
    val reader = MerkleByteReader(
        proof.queryInternal(),
        proof.rootHash,
        MerkleVerificationFailureKind.PROOF_MISMATCH,
    )
    if (!reader.take(MerkleProofMagic.size).contentEquals(MerkleProofMagic)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A proof query has the wrong magic.",
            proof.rootHash,
        )
    }
    if (reader.readUnsignedByte() != proof.kind.wireTag) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A proof query kind disagrees with its envelope.",
            proof.rootHash,
        )
    }
    return reader
}

private fun <K, V> MerkleSearchTree<K, V>.decodeCanonicalQueryKey(
    bytes: ByteArray,
    rootHash: MerkleDigest,
): K = decodeCanonicalQuery(policy.keyCodec, bytes, "key", rootHash)

private fun <K, V> MerkleSearchTree<K, V>.decodeCanonicalQueryValue(
    bytes: ByteArray,
    rootHash: MerkleDigest,
): V = decodeCanonicalQuery(policy.valueCodec, bytes, "value", rootHash)

private fun <T> decodeCanonicalQuery(
    codec: MerkleCodec<T>,
    bytes: ByteArray,
    field: String,
    rootHash: MerkleDigest,
): T {
    val value = try {
        codec.decode(bytes)
    } catch (exception: IllegalArgumentException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A proof query contains an invalid $field encoding.",
            rootHash,
            exception,
        )
    }
    val canonical = try {
        codec.encode(value)
    } catch (exception: IllegalArgumentException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A proof query $field could not be canonically re-encoded.",
            rootHash,
            exception,
        )
    }
    if (!canonical.contentEquals(bytes)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "A proof query $field is not canonically encoded.",
            rootHash,
        )
    }
    return value
}

private fun <K, V> MerkleSearchTree<K, V>.verifyEmptyProofQuery(proof: MerkleProof): Unit {
    when (proof.kind) {
        MerkleProofKind.NON_MEMBERSHIP -> decodePointQuery(proof)
        MerkleProofKind.RANGE -> decodeRangeQuery(proof)
        MerkleProofKind.MEMBERSHIP -> throw verificationFailure(
            MerkleVerificationFailureKind.PROOF_MISMATCH,
            "An empty tree cannot prove membership.",
            proof.rootHash,
        )
    }
}

/**
 * Combines two descendants relative to a common base. The resolver runs only for true conflicts;
 * unresolved conflicts withhold the entire output tree.
 */
internal fun <K, V> mergeMerkleTrees(
    baseTree: MerkleSearchTree<K, V>,
    left: MerkleSearchTree<K, V>,
    right: MerkleSearchTree<K, V>,
    resolver: ((MerkleThreeWayMergeConflict<K, V>) -> MerkleMergeResolution<V>)? = null,
    valuesEqual: (V, V) -> Boolean = { first, second -> first == second },
): MerkleThreeWayMergeResult<K, V> {
    baseTree.requireCompatible(left)
    baseTree.requireCompatible(right)
    if (left.rootHash == baseTree.rootHash) return MerkleThreeWayMergeResult.success(right)
    if (right.rootHash == baseTree.rootHash || left.rootHash == right.rootHash) {
        return MerkleThreeWayMergeResult.success(left)
    }

    val baseEntries = baseTree.toList()
    val leftEntries = left.toList()
    val rightEntries = right.toList()
    val output = ArrayList<MerkleEntry<K, V>>()
    val conflicts = ArrayList<MerkleThreeWayMergeConflict<K, V>>()
    var baseIndex = 0
    var leftIndex = 0
    var rightIndex = 0
    while (baseIndex < baseEntries.size || leftIndex < leftEntries.size || rightIndex < rightEntries.size) {
        val key = minimumMergeKey(
            baseTree.policy.comparator,
            baseEntries.getOrNull(baseIndex),
            leftEntries.getOrNull(leftIndex),
            rightEntries.getOrNull(rightIndex),
        )
        val baseSide = takeMergeSide(baseTree.policy.comparator, baseEntries, key, baseIndex)
        if (baseSide.consumed) baseIndex++
        val leftSide = takeMergeSide(baseTree.policy.comparator, leftEntries, key, leftIndex)
        if (leftSide.consumed) leftIndex++
        val rightSide = takeMergeSide(baseTree.policy.comparator, rightEntries, key, rightIndex)
        if (rightSide.consumed) rightIndex++

        val baseValue = baseSide.value()
        val leftValue = leftSide.value()
        val rightValue = rightSide.value()
        when {
            mergeValuesEqual(leftValue, baseValue, valuesEqual) -> appendMergeSide(output, rightSide)
            mergeValuesEqual(rightValue, baseValue, valuesEqual) -> appendMergeSide(output, leftSide)
            mergeValuesEqual(leftValue, rightValue, valuesEqual) -> appendMergeSide(output, leftSide)
            else -> {
                val representative = representativeEntry(leftSide, rightSide, baseSide)
                val conflict = MerkleThreeWayMergeConflict(
                    representative.key,
                    baseValue,
                    leftValue,
                    rightValue,
                )
                val resolution = resolver?.invoke(conflict) ?: MerkleMergeResolution.Unresolved
                if (!baseTree.applyMergeResolution(
                        output,
                        representative,
                        resolution,
                        baseSide,
                        leftSide,
                        rightSide,
                    )
                ) {
                    conflicts.add(conflict)
                }
            }
        }
    }
    return if (conflicts.isNotEmpty()) {
        MerkleThreeWayMergeResult.conflicted(conflicts)
    } else {
        MerkleThreeWayMergeResult.success(
            MerkleSearchTree.fromVerifiedRoot(baseTree.buildCanonical(output), baseTree.policy),
        )
    }
}

private sealed interface MergeSide<out K, out V> {
    val consumed: Boolean

    data object Absent : MergeSide<Nothing, Nothing> {
        override val consumed: Boolean = false
    }

    data class Present<K, V>(val entry: MerkleEntry<K, V>) : MergeSide<K, V> {
        override val consumed: Boolean = true
    }
}

private fun <K, V> minimumMergeKey(
    comparator: Comparator<K>,
    base: MerkleEntry<K, V>?,
    left: MerkleEntry<K, V>?,
    right: MerkleEntry<K, V>?,
): K {
    var selected = base ?: left ?: checkNotNull(right)
    if (left != null && comparator.compare(left.key, selected.key) < 0) selected = left
    if (right != null && comparator.compare(right.key, selected.key) < 0) selected = right
    return selected.key
}

private fun <K, V> takeMergeSide(
    comparator: Comparator<K>,
    entries: List<MerkleEntry<K, V>>,
    key: K,
    index: Int,
): MergeSide<K, V> {
    if (index >= entries.size || comparator.compare(entries[index].key, key) != 0) {
        return MergeSide.Absent
    }
    return MergeSide.Present(entries[index])
}

private fun <K, V> MergeSide<K, V>.value(): MerkleMergeValue<V> = when (this) {
    MergeSide.Absent -> MerkleMergeValue.Absent
    is MergeSide.Present -> MerkleMergeValue.Present(entry.value)
}

private fun <K, V> representativeEntry(
    first: MergeSide<K, V>,
    second: MergeSide<K, V>,
    third: MergeSide<K, V>,
): MerkleEntry<K, V> = when (first) {
    is MergeSide.Present -> first.entry
    MergeSide.Absent -> when (second) {
        is MergeSide.Present -> second.entry
        MergeSide.Absent -> when (third) {
            is MergeSide.Present -> third.entry
            MergeSide.Absent -> error("At least one merge side must contain the selected key.")
        }
    }
}

private fun <V> mergeValuesEqual(
    left: MerkleMergeValue<V>,
    right: MerkleMergeValue<V>,
    valuesEqual: (V, V) -> Boolean,
): Boolean = when {
    left is MerkleMergeValue.Absent && right is MerkleMergeValue.Absent -> true
    left is MerkleMergeValue.Present && right is MerkleMergeValue.Present ->
        valuesEqual(left.value, right.value)
    else -> false
}

private fun <K, V> appendMergeSide(
    output: MutableList<MerkleEntry<K, V>>,
    side: MergeSide<K, V>,
): Unit {
    if (side is MergeSide.Present) output.add(side.entry)
}

private fun <K, V> MerkleSearchTree<K, V>.applyMergeResolution(
    output: MutableList<MerkleEntry<K, V>>,
    representative: MerkleEntry<K, V>,
    resolution: MerkleMergeResolution<V>,
    base: MergeSide<K, V>,
    left: MergeSide<K, V>,
    right: MergeSide<K, V>,
): Boolean = when (resolution) {
    MerkleMergeResolution.Unresolved -> false
    MerkleMergeResolution.UseBase -> true.also { appendMergeSide(output, base) }
    MerkleMergeResolution.UseLeft -> true.also { appendMergeSide(output, left) }
    MerkleMergeResolution.UseRight -> true.also { appendMergeSide(output, right) }
    MerkleMergeResolution.Delete -> true
    is MerkleMergeResolution.SetValue -> true.also {
        output.add(
            representative.replacingValue(
                resolution.value,
                policy.valueCodec.encode(resolution.value),
            ),
        )
    }
}
