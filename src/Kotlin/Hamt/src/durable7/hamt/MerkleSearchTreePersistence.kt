/*
 * Storage, synchronization, and pack handling for Merkle search trees.
 *
 * Synchronization transfers only the blocks a receiver lacks, pruning any subtree whose digest
 * already agrees, so the cost follows the difference rather than the data.
 */
package durable7.hamt

import java.util.ArrayDeque

/** Writes this tree's complete closure after preflighting every destination conflict. */
public fun <K, V> MerkleSearchTree<K, V>.save(store: MerkleBlockStore): Int {
    val pack = exportPack()
    preflightStore(pack.blocks, store)
    var added = 0
    for (block in pack.blocks) if (store.put(block)) added = Math.addExact(added, 1)
    return added
}

/** Exports this tree's complete closure in deterministic preorder. */
public fun <K, V> MerkleSearchTree<K, V>.exportPack(): MerkleBlockPack = MerkleBlockPack(
    MerkleSearchTreePolicy.ALGORITHM_ID,
    policy.domainDigest,
    rootHash,
    enumerateNodesPreorder(root).map { node -> MerkleBlock(node.digest, node.blockBytes) },
)

/** Exports unique explicitly requested blocks in the supplied transfer order. */
public fun <K, V> MerkleSearchTree<K, V>.exportPack(
    digests: Iterable<MerkleDigest>,
): MerkleBlockPack {
    val byDigest = enumerateNodesPreorder(root).associateBy(MerkleNode<K, V>::digest)
    val unique = HashSet<MerkleDigest>()
    val blocks = ArrayList<MerkleBlock>()
    for (digest in digests) {
        if (!unique.add(digest)) {
            throw verificationFailure(
                MerkleVerificationFailureKind.DUPLICATE_BLOCK,
                "Digest '$digest' was requested more than once.",
                digest,
            )
        }
        val node = byDigest[digest] ?: throw verificationFailure(
            MerkleVerificationFailureKind.MISSING_BLOCK,
            "Digest '$digest' does not name a block in this tree.",
            digest,
        )
        blocks.add(MerkleBlock(node.digest, node.blockBytes))
    }
    return MerkleBlockPack(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        policy.domainDigest,
        rootHash,
        blocks,
    )
}

/** Exports every target block absent from [receiverStore], pruning known verified closures. */
public fun <K, V> MerkleSearchTree<K, V>.createSyncPack(
    receiverStore: MerkleBlockStore,
): MerkleBlockPack {
    val missing = ArrayList<MerkleBlock>()
    val pending = ArrayDeque<MerkleNode<K, V>>()
    root?.let(pending::addLast)
    while (pending.isNotEmpty()) {
        val node = pending.removeLast()
        if (receiverStore.contains(node.digest)) continue
        missing.add(MerkleBlock(node.digest, node.blockBytes))
        for (index in node.children.indices.reversed()) node.children[index]?.let(pending::addLast)
    }
    return MerkleBlockPack(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        policy.domainDigest,
        rootHash,
        missing,
    )
}

/** Plans one iterative missing-frontier synchronization round toward this target tree. */
public fun <K, V> MerkleSearchTree<K, V>.planSync(
    localTree: MerkleSearchTree<K, V>,
    receiverStore: MerkleBlockStore,
): MerkleSyncPlan {
    requireCompatible(localTree)
    if (rootHash == localTree.rootHash) {
        return MerkleSyncPlan(
            MerkleSearchTreePolicy.ALGORITHM_ID,
            policy.domainDigest,
            localTree.rootHash,
            rootHash,
            emptyList(),
            0,
            0,
        )
    }

    val requested = ArrayList<MerkleDigest>()
    val pending = ArrayDeque<MerkleNode<K, V>>()
    root?.let(pending::addLast)
    var examinedBlocks = 0
    var examinedBytes = 0L
    while (pending.isNotEmpty()) {
        val node = pending.removeLast()
        examinedBlocks = Math.addExact(examinedBlocks, 1)
        examinedBytes = Math.addExact(examinedBytes, node.blockBytes.size.toLong())
        if (!receiverStore.contains(node.digest)) {
            requested.add(node.digest)
            continue
        }
        for (index in node.children.indices.reversed()) node.children[index]?.let(pending::addLast)
    }
    return MerkleSyncPlan(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        policy.domainDigest,
        localTree.rootHash,
        rootHash,
        requested,
        examinedBlocks,
        examinedBytes,
    )
}

/** Loads and fully verifies a closure under finite [budget] limits. */
internal fun <K, V> loadMerkleTree(
    rootHash: MerkleDigest,
    policy: MerkleSearchTreePolicy<K, V>,
    store: MerkleBlockStore,
    budget: MerkleVerificationBudget = MerkleVerificationBudget(),
): MerkleSearchTree<K, V> {
    val context = MerkleVerificationContext(budget)
    return loadMerkleTreeWithContext(rootHash, policy, store, context)
}

/**
 * Verifies a complete or partial pack and publishes its blocks only after closure verification and
 * destination-conflict preflight have both succeeded.
 */
internal fun <K, V> importMerklePack(
    pack: MerkleBlockPack,
    policy: MerkleSearchTreePolicy<K, V>,
    destinationStore: MerkleBlockStore? = null,
    budget: MerkleVerificationBudget = MerkleVerificationBudget(),
): MerkleSearchTree<K, V> {
    verifyMerkleEnvelope(pack.algorithmId, pack.domainDigest, policy)
    val verifier = MerkleSearchTree.empty(policy)
    val context = MerkleVerificationContext(budget)
    for (block in pack.blocks) verifier.decodeMerkleBlock(block, context, 1)

    val staged = HashMap<MerkleDigest, MerkleBlock>(pack.blockCount)
    for (block in pack.blocks) {
        if (staged.put(block.digest, block) != null) {
            throw verificationFailure(
                MerkleVerificationFailureKind.DUPLICATE_BLOCK,
                "The pack repeats block '${block.digest}'.",
                block.digest,
            )
        }
    }
    val overlay = OverlayMerkleBlockStore(staged, destinationStore)
    val loaded = loadMerkleTreeWithContext(pack.rootHash, policy, overlay, context)
    if (destinationStore != null) {
        preflightStore(pack.blocks, destinationStore)
        for (block in pack.blocks) destinationStore.put(block)
    }
    return loaded
}

private fun <K, V> loadMerkleTreeWithContext(
    rootHash: MerkleDigest,
    policy: MerkleSearchTreePolicy<K, V>,
    store: MerkleBlockStore,
    context: MerkleVerificationContext,
): MerkleSearchTree<K, V> {
    val verifier = MerkleSearchTree.empty(policy)
    if (rootHash == policy.emptyDigest) return verifier
    val cache = HashMap<MerkleDigest, MerkleNode<K, V>>()
    val active = HashSet<MerkleDigest>()
    val root = verifier.loadMerkleNode(rootHash, store, context, cache, active, 1)
    if (root.digest != rootHash) {
        throw verificationFailure(
            MerkleVerificationFailureKind.ROOT_MISMATCH,
            "The reconstructed root does not match the requested root hash.",
            rootHash,
        )
    }
    val tree = MerkleSearchTree.fromVerifiedRoot(root, policy)
    try {
        tree.validateStructure()
    } catch (exception: IllegalStateException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.INVALID_REFERENCE,
            "The verified closure failed final structure validation: ${exception.message}",
            rootHash,
            exception,
        )
    }
    return tree
}

private fun preflightStore(blocks: Iterable<MerkleBlock>, store: MerkleBlockStore): Unit {
    for (block in blocks) {
        val existing = store.get(block.digest)
        if (existing != null && existing != block) {
            throw verificationFailure(
                MerkleVerificationFailureKind.CONFLICTING_BLOCK,
                "Digest '${block.digest}' is already associated with different block bytes.",
                block.digest,
            )
        }
    }
}

/** A block decoded back into its level, entries, and child digests, ready to be verified against its own digest. */
internal class DecodedMerkleBlock<K, V>(
    val block: MerkleBlock,
    val level: Int,
    val subtreeCount: Int,
    val entries: List<MerkleEntry<K, V>>,
    val childDigests: List<MerkleDigest>,
)

/** Accounting state charging a verification against its budget, so untrusted input cannot force unbounded work. */
internal class MerkleVerificationContext(internal val budget: MerkleVerificationBudget) {
    /** How many blocks have been charged so far. */
    internal var blockCount: Int = 0
        private set

    /** How many bytes have been charged so far. */
    internal var totalBytes: Long = 0
        private set
    private var entryCount: Long = 0
    private val blocks: HashSet<MerkleDigest> = HashSet()

    /** Fail when the depth exceeds the budget, bounding how deep a chain the input can force. */
    internal fun checkDepth(depth: Int, digest: MerkleDigest? = null): Unit {
        if (depth <= 0 || depth > budget.maxDepth) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "Merkle verification exceeded the maximum reference depth.",
                digest,
            )
        }
    }

    /**
     * Charge one block, reporting whether it had not already been counted. Blocks already seen are not charged twice,
     * so a shared subtree costs once.
     */
    internal fun account(block: MerkleBlock, depth: Int): Boolean {
        checkDepth(depth, block.digest)
        if (!blocks.add(block.digest)) return false
        if (block.length > budget.maxBlockByteCount) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "A Merkle block exceeds the per-block byte budget.",
                block.digest,
            )
        }
        if (blockCount >= budget.maxBlockCount ||
            totalBytes > budget.maxTotalByteCount - block.length.toLong()
        ) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "Merkle verification exceeded its block or total-byte budget.",
                block.digest,
            )
        }
        blockCount++
        totalBytes += block.length
        return true
    }

    /** Charge a proof query's bytes against both the per-query and total ceilings. */
    internal fun accountProofQuery(byteCount: Int, rootHash: MerkleDigest): Unit {
        if (byteCount > budget.maxProofQueryByteCount ||
            totalBytes > budget.maxTotalByteCount - byteCount.toLong()
        ) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "A Merkle proof query exceeds its query or total-byte budget.",
                rootHash,
            )
        }
        totalBytes += byteCount
    }

    /** Charge decoded entries against the budget. */
    internal fun accountEntries(count: Int, digest: MerkleDigest): Unit {
        if (entryCount > budget.maxEntryCount - count.toLong()) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "Merkle verification exceeded its decoded-entry budget.",
                digest,
            )
        }
        entryCount += count
    }
}

private fun <K, V> MerkleSearchTree<K, V>.loadMerkleNode(
    digest: MerkleDigest,
    store: MerkleBlockStore,
    context: MerkleVerificationContext,
    cache: MutableMap<MerkleDigest, MerkleNode<K, V>>,
    active: MutableSet<MerkleDigest>,
    depth: Int,
): MerkleNode<K, V> {
    context.checkDepth(depth, digest)
    cache[digest]?.let { return it }
    if (!active.add(digest)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.CYCLE_DETECTED,
            "The Merkle closure contains a reference cycle.",
            digest,
        )
    }
    try {
        val block = store.get(digest) ?: throw verificationFailure(
            MerkleVerificationFailureKind.MISSING_BLOCK,
            "Required Merkle block '$digest' is absent.",
            digest,
        )
        val decoded = decodeMerkleBlock(block, context, depth)
        val children = ArrayList<MerkleNode<K, V>?>(decoded.childDigests.size)
        for ((index, childDigest) in decoded.childDigests.withIndex()) {
            if (childDigest == policy.emptyDigest) {
                children.add(null)
            } else {
                val child = loadMerkleNode(
                    childDigest,
                    store,
                    context,
                    cache,
                    active,
                    Math.addExact(depth, 1),
                )
                validateMerkleReference(decoded, index, child)
                children.add(child)
            }
        }
        var actualCount = decoded.entries.size
        for (child in children) if (child != null) actualCount = Math.addExact(actualCount, child.count)
        if (actualCount != decoded.subtreeCount) {
            throw verificationFailure(
                MerkleVerificationFailureKind.INVALID_REFERENCE,
                "Block '$digest' declares ${decoded.subtreeCount} entries but its closure contains $actualCount.",
                digest,
            )
        }
        val node = try {
            newNode(decoded.level, decoded.entries, children)
        } catch (exception: ArithmeticException) {
            throw verificationFailure(
                MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
                "Merkle node metadata or canonical encoding overflowed.",
                digest,
                exception,
            )
        }
        if (node.digest != digest || !node.blockBytes.contentEquals(block.bytesInternal())) {
            throw verificationFailure(
                MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
                "Decoded block bytes do not round-trip canonically.",
                digest,
            )
        }
        cache[digest] = node
        return node
    } finally {
        active.remove(digest)
    }
}

/**
 * Check that a child block sits below its parent's level and stays strictly inside its key separators. Verified on
 * decode so a crafted pack cannot present an out-of-order or level-inverted tree as valid.
 */
internal fun <K, V> MerkleSearchTree<K, V>.validateMerkleReference(
    parent: DecodedMerkleBlock<K, V>,
    childIndex: Int,
    child: MerkleNode<K, V>,
): Unit {
    if (child.level >= parent.level) {
        throw verificationFailure(
            MerkleVerificationFailureKind.INVALID_REFERENCE,
            "A child block is not below its parent level.",
            child.digest,
        )
    }
    if (childIndex != 0 &&
        policy.comparator.compare(child.minimumKey, parent.entries[childIndex - 1].key) <= 0
    ) {
        throw verificationFailure(
            MerkleVerificationFailureKind.INVALID_REFERENCE,
            "A child block crosses its lower key separator.",
            child.digest,
        )
    }
    if (childIndex != parent.entries.size &&
        policy.comparator.compare(child.maximumKey, parent.entries[childIndex].key) >= 0
    ) {
        throw verificationFailure(
            MerkleVerificationFailureKind.INVALID_REFERENCE,
            "A child block crosses its upper key separator.",
            child.digest,
        )
    }
}

/**
 * Decode one block after charging it to the verification budget and confirming its bytes hash to the digest it was
 * fetched by.
 */
internal fun <K, V> MerkleSearchTree<K, V>.decodeMerkleBlock(
    block: MerkleBlock,
    context: MerkleVerificationContext,
    depth: Int,
): DecodedMerkleBlock<K, V> {
    val firstVisit = context.account(block, depth)
    val bytes = block.bytesInternal()
    if (policy.hashBytes(bytes) != block.digest) {
        throw verificationFailure(
            MerkleVerificationFailureKind.DIGEST_MISMATCH,
            "A block's bytes do not match its claimed digest.",
            block.digest,
        )
    }
    if (bytes.size < MerkleBlockHeaderLength + MerkleDigest.BYTE_LENGTH) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle block is shorter than the minimum encoding.",
            block.digest,
        )
    }
    val reader = MerkleByteReader(bytes, block.digest)
    if (!reader.take(MerkleBlockMagic.size).contentEquals(MerkleBlockMagic)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle block has the wrong magic.",
            block.digest,
        )
    }
    if (reader.readUnsignedByte() != MerkleNodeBlockTag) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle block has an unsupported tag.",
            block.digest,
        )
    }
    val domain = MerkleDigest.fromBytes(reader.take(MerkleDigest.BYTE_LENGTH))
    if (domain != policy.domainDigest) {
        throw verificationFailure(
            MerkleVerificationFailureKind.DOMAIN_MISMATCH,
            "A Merkle block belongs to another policy domain.",
            block.digest,
        )
    }
    val level = reader.readUnsignedByte()
    if (level > 64) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle block level exceeds the SHA-256 nibble range.",
            block.digest,
        )
    }
    val subtreeCount = reader.readInt()
    val entryCount = reader.readInt()
    if (subtreeCount <= 0 || entryCount <= 0 || subtreeCount < entryCount) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle block has invalid entry counts.",
            block.digest,
        )
    }
    if (entryCount >= context.budget.maxChildReferencesPerBlock) {
        throw verificationFailure(
            MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
            "A Merkle block exceeds the child-reference budget.",
            block.digest,
        )
    }
    val minimumLength = MerkleBlockHeaderLength.toLong() +
        entryCount.toLong() * (8L + MerkleDigest.BYTE_LENGTH) + MerkleDigest.BYTE_LENGTH
    if (minimumLength > bytes.size.toLong()) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle block cannot contain its declared entries and child references.",
            block.digest,
        )
    }
    if (firstVisit) context.accountEntries(entryCount, block.digest)

    val entries = ArrayList<MerkleEntry<K, V>>(entryCount)
    repeat(entryCount) { index ->
        val keyBytes = reader.readLengthPrefixed()
        val valueBytes = reader.readLengthPrefixed()
        val key = decodeCanonical(policy.keyCodec, keyBytes, "key", block.digest)
        val value = decodeCanonical(policy.valueCodec, valueBytes, "value", block.digest)
        val entryLevel = MerkleSearchTreePolicy.level(policy.hashKeyBytes(keyBytes))
        if (entryLevel != level) {
            throw verificationFailure(
                MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
                "An entry is stored at a level other than its hash-derived level.",
                block.digest,
            )
        }
        val entry = MerkleEntry(key, value, keyBytes, valueBytes, entryLevel)
        if (index != 0 && policy.comparator.compare(entries[index - 1].key, key) >= 0) {
            throw verificationFailure(
                MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
                "Merkle block entries are not strictly comparator-ordered.",
                block.digest,
            )
        }
        entries.add(entry)
    }
    val childDigests = ArrayList<MerkleDigest>(entryCount + 1)
    repeat(entryCount + 1) {
        childDigests.add(MerkleDigest.fromBytes(reader.take(MerkleDigest.BYTE_LENGTH)))
    }
    if (!reader.atEnd) {
        throw verificationFailure(
            MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
            "A Merkle block has trailing bytes.",
            block.digest,
        )
    }
    val canonical = encodeRawMerkleBlock(level, subtreeCount, entries, childDigests, block.digest)
    if (!canonical.contentEquals(bytes)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
            "A Merkle block is not the unique canonical encoding.",
            block.digest,
        )
    }
    return DecodedMerkleBlock(block, level, subtreeCount, entries, childDigests)
}

private fun <T> decodeCanonical(
    codec: MerkleCodec<T>,
    bytes: ByteArray,
    field: String,
    digest: MerkleDigest,
): T {
    val value = try {
        codec.decode(bytes)
    } catch (exception: IllegalArgumentException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.MALFORMED_BLOCK,
            "A Merkle $field codec rejected serialized entry bytes.",
            digest,
            exception,
        )
    }
    val canonical = try {
        codec.encode(value)
    } catch (exception: IllegalArgumentException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
            "A decoded Merkle $field could not be re-encoded.",
            digest,
            exception,
        )
    }
    if (!canonical.contentEquals(bytes)) {
        throw verificationFailure(
            MerkleVerificationFailureKind.NON_CANONICAL_BLOCK,
            "A decoded Merkle $field does not round-trip to its exact encoded bytes.",
            digest,
        )
    }
    return value
}

private fun <K, V> MerkleSearchTree<K, V>.encodeRawMerkleBlock(
    level: Int,
    subtreeCount: Int,
    entries: List<MerkleEntry<K, V>>,
    childDigests: List<MerkleDigest>,
    digest: MerkleDigest,
): ByteArray {
    var length = try {
        Math.addExact(
            MerkleBlockHeaderLength,
            Math.multiplyExact(childDigests.size, MerkleDigest.BYTE_LENGTH),
        )
    } catch (exception: ArithmeticException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
            "A Merkle block length overflowed.",
            digest,
            exception,
        )
    }
    try {
        for (entry in entries) {
            length = Math.addExact(length, 8)
            length = Math.addExact(length, entry.keyBytesInternal().size)
            length = Math.addExact(length, entry.valueBytesInternal().size)
        }
    } catch (exception: ArithmeticException) {
        throw verificationFailure(
            MerkleVerificationFailureKind.RESOURCE_LIMIT_EXCEEDED,
            "A Merkle block length overflowed.",
            digest,
            exception,
        )
    }
    val result = ByteArray(length)
    var offset = 0
    MerkleBlockMagic.copyInto(result, offset)
    offset += MerkleBlockMagic.size
    result[offset++] = MerkleNodeBlockTag.toByte()
    policy.domainDigest.copyInto(result, offset)
    offset += MerkleDigest.BYTE_LENGTH
    result[offset++] = level.toByte()
    writeMerkleInt(result, offset, subtreeCount)
    offset += Int.SIZE_BYTES
    writeMerkleInt(result, offset, entries.size)
    offset += Int.SIZE_BYTES
    for (entry in entries) {
        val keyBytes = entry.keyBytesInternal()
        val valueBytes = entry.valueBytesInternal()
        writeMerkleInt(result, offset, keyBytes.size)
        offset += Int.SIZE_BYTES
        keyBytes.copyInto(result, offset)
        offset += keyBytes.size
        writeMerkleInt(result, offset, valueBytes.size)
        offset += Int.SIZE_BYTES
        valueBytes.copyInto(result, offset)
        offset += valueBytes.size
    }
    for (childDigest in childDigests) {
        childDigest.copyInto(result, offset)
        offset += MerkleDigest.BYTE_LENGTH
    }
    check(offset == result.size)
    return result
}

/**
 * A bounds-checked reader over block bytes; a read past the end is reported as malformed input rather than an internal
 * error.
 */
internal class MerkleByteReader(
    private val bytes: ByteArray,
    private val digest: MerkleDigest? = null,
    private val failureKind: MerkleVerificationFailureKind =
        MerkleVerificationFailureKind.MALFORMED_BLOCK,
) {
    private var offset: Int = 0
    internal val atEnd: Boolean get() = offset == bytes.size

    internal fun readUnsignedByte(): Int = take(1)[0].toInt() and 0xff

    internal fun readInt(): Int {
        val value = take(Int.SIZE_BYTES)
        return (value[0].toInt() and 0xff shl 24) or
            (value[1].toInt() and 0xff shl 16) or
            (value[2].toInt() and 0xff shl 8) or
            (value[3].toInt() and 0xff)
    }

    internal fun readLengthPrefixed(): ByteArray {
        val length = readInt()
        if (length < 0) {
            throw verificationFailure(
                failureKind,
                "A length-prefixed Merkle field has a negative length.",
                digest,
            )
        }
        return take(length)
    }

    internal fun take(length: Int): ByteArray {
        if (length < 0 || offset < 0 || length > bytes.size - offset) {
            throw verificationFailure(
                failureKind,
                "A serialized Merkle field is truncated.",
                digest,
            )
        }
        val result = bytes.copyOfRange(offset, offset + length)
        offset += length
        return result
    }
}

/** Append a signed 32-bit value in the canonical big-endian framing. */
internal fun writeMerkleInt(destination: ByteArray, offset: Int, value: Int): Unit {
    destination[offset] = (value ushr 24).toByte()
    destination[offset + 1] = (value ushr 16).toByte()
    destination[offset + 2] = (value ushr 8).toByte()
    destination[offset + 3] = value.toByte()
}

/** Check a pack or proof envelope's algorithm and domain identifiers before trusting anything inside it. */
internal fun <K, V> verifyMerkleEnvelope(
    algorithmId: String,
    domainDigest: MerkleDigest,
    policy: MerkleSearchTreePolicy<K, V>,
): Unit {
    if (algorithmId != MerkleSearchTreePolicy.ALGORITHM_ID) {
        throw verificationFailure(
            MerkleVerificationFailureKind.UNSUPPORTED_ALGORITHM,
            "Merkle algorithm '$algorithmId' is not supported.",
        )
    }
    if (domainDigest != policy.domainDigest) {
        throw verificationFailure(
            MerkleVerificationFailureKind.DOMAIN_MISMATCH,
            "The Merkle envelope belongs to another policy domain.",
        )
    }
}

private class OverlayMerkleBlockStore(
    private val staged: Map<MerkleDigest, MerkleBlock>,
    private val fallback: MerkleBlockStore?,
) : MerkleBlockStore {
    override val size: Int get() = digests.size
    override val digests: List<MerkleDigest> get() =
        (staged.keys + (fallback?.digests ?: emptyList())).distinct().sorted()

    override fun contains(digest: MerkleDigest): Boolean =
        staged.containsKey(digest) || fallback?.contains(digest) == true

    override fun get(digest: MerkleDigest): MerkleBlock? = staged[digest] ?: fallback?.get(digest)

    override fun put(block: MerkleBlock): Boolean =
        throw UnsupportedOperationException("The verification overlay is read-only.")

    override fun remove(digest: MerkleDigest): Boolean =
        throw UnsupportedOperationException("The verification overlay is read-only.")

    override fun clear(): Unit = throw UnsupportedOperationException("The verification overlay is read-only.")
}
