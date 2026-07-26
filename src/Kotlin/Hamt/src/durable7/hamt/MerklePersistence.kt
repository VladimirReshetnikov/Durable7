/*
 * Block storage and verification vocabulary for Merkle search trees.
 *
 * A tree decomposes into content-addressed blocks; loading one re-derives every block's digest from
 * its bytes, so a corrupted or substituted block is rejected rather than trusted. Verification runs
 * against an explicit budget so untrusted input cannot force unbounded work.
 */
package durable7.hamt

import java.io.IOException
import java.util.Collections
import java.util.concurrent.ConcurrentHashMap

/** One immutable serialized block paired with its claimed content address. */
public class MerkleBlock(
    public val digest: MerkleDigest,
    content: ByteArray,
) {
    private val bytes: ByteArray = content.copyOf()

    /** Number of serialized bytes. */
    public val length: Int get() = bytes.size

    /** Returns a newly owned copy of the complete serialized block. */
    public fun toByteArray(): ByteArray = bytes.copyOf()

    internal fun bytesInternal(): ByteArray = bytes

    override fun equals(other: Any?): Boolean = other is MerkleBlock &&
        digest == other.digest && bytes.contentEquals(other.bytes)

    override fun hashCode(): Int = 31 * digest.hashCode() + bytes.contentHashCode()

    override fun toString(): String = "$digest ($length bytes)"
}

/** Identifies why untrusted Merkle persistence data was rejected. */
public enum class MerkleVerificationFailureKind {
    NONE,
    UNSUPPORTED_ALGORITHM,
    DOMAIN_MISMATCH,
    MISSING_BLOCK,
    DIGEST_MISMATCH,
    MALFORMED_BLOCK,
    NON_CANONICAL_BLOCK,
    DUPLICATE_BLOCK,
    CONFLICTING_BLOCK,
    INVALID_REFERENCE,
    CYCLE_DETECTED,
    PROOF_MISMATCH,
    ROOT_MISMATCH,
    RESOURCE_LIMIT_EXCEEDED,
}

/** A precise load, import, store, or proof-verification failure. */
public class MerkleVerificationException(
    public val failureKind: MerkleVerificationFailureKind,
    message: String,
    public val blockDigest: MerkleDigest? = null,
    cause: Throwable? = null,
) : IOException(message, cause) {
    init {
        require(failureKind != MerkleVerificationFailureKind.NONE) {
            "A verification exception must describe a failure."
        }
    }
}

/** Concurrent-safe storage for immutable content-addressed Merkle blocks. */
public interface MerkleBlockStore {
    /** Number of stored addresses. */
    public val size: Int

    /** Sorted point-in-time snapshot of stored addresses. */
    public val digests: List<MerkleDigest>

    /** Returns whether [digest] is present. */
    public fun contains(digest: MerkleDigest): Boolean

    /** Returns the immutable block snapshot at [digest], or `null`. */
    public fun get(digest: MerkleDigest): MerkleBlock?

    /** Stores [block], returning `true` only for a newly added address. */
    public fun put(block: MerkleBlock): Boolean

    /** Removes [digest] when present. */
    public fun remove(digest: MerkleDigest): Boolean

    /** Removes every stored block. */
    public fun clear(): Unit
}

/** Thread-safe ephemeral Merkle block store. */
public class InMemoryMerkleBlockStore : MerkleBlockStore {
    private val blocks: ConcurrentHashMap<MerkleDigest, MerkleBlock> = ConcurrentHashMap()

    /** Number of stored blocks. */
    override val size: Int get() = blocks.size

    /** Every stored digest. */
    override val digests: List<MerkleDigest> get() = blocks.keys.toList().sorted()

    /** Whether the element is present. */
    override fun contains(digest: MerkleDigest): Boolean = blocks.containsKey(digest)

    /** The value stored for the key, or `null` when absent. */
    override fun get(digest: MerkleDigest): MerkleBlock? = blocks[digest]

    /** A collection with the key bound to the value, adding or replacing as needed. */
    override fun put(block: MerkleBlock): Boolean {
        val existing = blocks.putIfAbsent(block.digest, block) ?: return true
        if (existing == block) return false
        throw verificationFailure(
            MerkleVerificationFailureKind.CONFLICTING_BLOCK,
            "Digest '${block.digest}' is already associated with different block bytes.",
            block.digest,
        )
    }

    /** A collection without that element; returns the receiver when absent. */
    override fun remove(digest: MerkleDigest): Boolean = blocks.remove(digest) != null

    /** An empty collection retaining the same policies; returns the receiver when already empty. */
    override fun clear(): Unit = blocks.clear()
}

/** Immutable complete or partial transfer pack with unique block addresses. */
public class MerkleBlockPack(
    public val algorithmId: String,
    public val domainDigest: MerkleDigest,
    public val rootHash: MerkleDigest,
    blocks: Iterable<MerkleBlock>,
) {
    public val blocks: List<MerkleBlock>
    public val totalByteCount: Long
    public val containsRootBlock: Boolean
    public val blockCount: Int get() = blocks.size

    init {
        require(algorithmId.isNotBlank()) { "A Merkle pack algorithm ID must not be blank." }
        val materialized = blocks.toList()
        val unique = HashSet<MerkleDigest>(materialized.size)
        var bytes = 0L
        var containsRoot = false
        for (block in materialized) {
            if (!unique.add(block.digest)) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.DUPLICATE_BLOCK,
                    "The pack contains duplicate digest '${block.digest}'.",
                    block.digest,
                )
            }
            bytes = Math.addExact(bytes, block.length.toLong())
            containsRoot = containsRoot || block.digest == rootHash
        }
        this.blocks = immutablePersistenceList(materialized)
        totalByteCount = bytes
        containsRootBlock = containsRoot
    }

    override fun equals(other: Any?): Boolean = other is MerkleBlockPack &&
        algorithmId == other.algorithmId &&
        domainDigest == other.domainDigest &&
        rootHash == other.rootHash &&
        blocks == other.blocks

    override fun hashCode(): Int {
        var result = algorithmId.hashCode()
        result = 31 * result + domainDigest.hashCode()
        result = 31 * result + rootHash.hashCode()
        result = 31 * result + blocks.hashCode()
        return result
    }
}

/** One deterministic frontier round of block-level synchronization. */
public class MerkleSyncPlan(
    public val algorithmId: String,
    public val domainDigest: MerkleDigest,
    public val localRootHash: MerkleDigest,
    public val targetRootHash: MerkleDigest,
    requestedBlocks: Iterable<MerkleDigest>,
    public val examinedBlockCount: Int,
    public val examinedByteCount: Long,
) {
    public val requestedBlocks: List<MerkleDigest>
    public val rootsMatch: Boolean get() = localRootHash == targetRootHash
    public val requiresBlocks: Boolean get() = requestedBlocks.isNotEmpty()

    init {
        require(algorithmId.isNotBlank()) { "A Merkle synchronization algorithm ID must not be blank." }
        require(examinedBlockCount >= 0) { "The examined block count must not be negative." }
        require(examinedByteCount >= 0) { "The examined byte count must not be negative." }
        val materialized = requestedBlocks.toList()
        require(materialized.toSet().size == materialized.size) {
            "A synchronization plan must not request one digest more than once."
        }
        this.requestedBlocks = immutablePersistenceList(materialized)
    }

    override fun equals(other: Any?): Boolean = other is MerkleSyncPlan &&
        algorithmId == other.algorithmId &&
        domainDigest == other.domainDigest &&
        localRootHash == other.localRootHash &&
        targetRootHash == other.targetRootHash &&
        requestedBlocks == other.requestedBlocks &&
        examinedBlockCount == other.examinedBlockCount &&
        examinedByteCount == other.examinedByteCount

    override fun hashCode(): Int {
        var result = algorithmId.hashCode()
        result = 31 * result + domainDigest.hashCode()
        result = 31 * result + localRootHash.hashCode()
        result = 31 * result + targetRootHash.hashCode()
        result = 31 * result + requestedBlocks.hashCode()
        result = 31 * result + examinedBlockCount
        result = 31 * result + examinedByteCount.hashCode()
        return result
    }
}

/** The claim encoded by an exact `MSP2` query descriptor. */
public enum class MerkleProofKind(internal val wireTag: Int) {
    MEMBERSHIP(0),
    NON_MEMBERSHIP(1),
    RANGE(2),
}

/** One authenticated block and the child intervals expanded elsewhere in a proof. */
public class MerkleProofStep(
    public val block: MerkleBlock,
    expandedChildIndexes: Iterable<Int>,
) {
    public val expandedChildIndexes: List<Int>

    init {
        val indexes = expandedChildIndexes.toList().sorted()
        require(indexes.all { it >= 0 }) { "Expanded child indexes must not be negative." }
        require(indexes.zipWithNext().none { (left, right) -> left == right }) {
            "A proof step must not expand one child index more than once."
        }
        this.expandedChildIndexes = immutablePersistenceList(indexes)
    }

    override fun equals(other: Any?): Boolean = other is MerkleProofStep &&
        block == other.block && expandedChildIndexes == other.expandedChildIndexes

    override fun hashCode(): Int = 31 * block.hashCode() + expandedChildIndexes.hashCode()
}

/** Immutable membership, nonmembership, or inclusive-range proof. */
public class MerkleProof(
    public val algorithmId: String,
    public val domainDigest: MerkleDigest,
    public val rootHash: MerkleDigest,
    public val kind: MerkleProofKind,
    query: ByteArray,
    steps: Iterable<MerkleProofStep>,
) {
    private val queryBytes: ByteArray = query.copyOf()
    public val steps: List<MerkleProofStep>
    public val totalByteCount: Long

    init {
        require(algorithmId.isNotBlank()) { "A Merkle proof algorithm ID must not be blank." }
        val materialized = steps.toList()
        val unique = HashSet<MerkleDigest>(materialized.size)
        var bytes = queryBytes.size.toLong()
        for (step in materialized) {
            if (!unique.add(step.block.digest)) {
                throw verificationFailure(
                    MerkleVerificationFailureKind.DUPLICATE_BLOCK,
                    "The proof contains duplicate block '${step.block.digest}'.",
                    step.block.digest,
                )
            }
            bytes = Math.addExact(bytes, step.block.length.toLong())
        }
        this.steps = immutablePersistenceList(materialized)
        totalByteCount = bytes
    }

    /** Returns a newly owned exact `MSP2` query descriptor. */
    public fun query(): ByteArray = queryBytes.copyOf()

    internal fun queryInternal(): ByteArray = queryBytes

    override fun equals(other: Any?): Boolean = other is MerkleProof &&
        algorithmId == other.algorithmId &&
        domainDigest == other.domainDigest &&
        rootHash == other.rootHash &&
        kind == other.kind &&
        queryBytes.contentEquals(other.queryBytes) &&
        steps == other.steps

    override fun hashCode(): Int {
        var result = algorithmId.hashCode()
        result = 31 * result + domainDigest.hashCode()
        result = 31 * result + rootHash.hashCode()
        result = 31 * result + kind.hashCode()
        result = 31 * result + queryBytes.contentHashCode()
        result = 31 * result + steps.hashCode()
        return result
    }
}

/** Immutable proof-verification outcome. */
@ConsistentCopyVisibility
public data class MerkleProofVerificationResult internal constructor(
    public val isValid: Boolean,
    public val failureKind: MerkleVerificationFailureKind,
    public val failureMessage: String?,
    public val computedRootHash: MerkleDigest?,
    public val verifiedBlockCount: Int,
    public val verifiedByteCount: Long,
)

/** Seven finite limits checked before allocation, decoding, and reference traversal. */
public class MerkleVerificationBudget(
    public val maxBlockCount: Int = 1_000_000,
    public val maxTotalByteCount: Long = 1L shl 30,
    public val maxBlockByteCount: Int = 16 shl 20,
    public val maxDepth: Int = 256,
    public val maxEntryCount: Long = 100_000_000L,
    public val maxChildReferencesPerBlock: Int = 65_536,
    public val maxProofQueryByteCount: Int = maxBlockByteCount,
) {
    init {
        require(maxBlockCount > 0) { "The block-count limit must be positive." }
        require(maxTotalByteCount > 0) { "The total-byte limit must be positive." }
        require(maxBlockByteCount > 0) { "The per-block byte limit must be positive." }
        require(maxDepth > 0) { "The depth limit must be positive." }
        require(maxEntryCount > 0) { "The entry-count limit must be positive." }
        require(maxChildReferencesPerBlock > 0) { "The child-reference limit must be positive." }
        require(maxProofQueryByteCount > 0) { "The proof-query byte limit must be positive." }
        require(maxBlockByteCount.toLong() <= maxTotalByteCount) {
            "The per-block byte limit must not exceed the total-byte limit."
        }
        require(maxProofQueryByteCount.toLong() <= maxTotalByteCount) {
            "The proof-query byte limit must not exceed the total-byte limit."
        }
    }

    /**
     * Copies this budget. When the query limit was coupled to the old block limit and only the
     * block limit changes, the copied query limit follows the new block limit.
     */
    public fun copy(
        maxBlockCount: Int = this.maxBlockCount,
        maxTotalByteCount: Long = this.maxTotalByteCount,
        maxBlockByteCount: Int = this.maxBlockByteCount,
        maxDepth: Int = this.maxDepth,
        maxEntryCount: Long = this.maxEntryCount,
        maxChildReferencesPerBlock: Int = this.maxChildReferencesPerBlock,
        maxProofQueryByteCount: Int = if (
            maxBlockByteCount != this.maxBlockByteCount &&
            this.maxProofQueryByteCount == this.maxBlockByteCount
        ) {
            maxBlockByteCount
        } else {
            this.maxProofQueryByteCount
        },
    ): MerkleVerificationBudget = MerkleVerificationBudget(
        maxBlockCount,
        maxTotalByteCount,
        maxBlockByteCount,
        maxDepth,
        maxEntryCount,
        maxChildReferencesPerBlock,
        maxProofQueryByteCount,
    )

    override fun equals(other: Any?): Boolean = other is MerkleVerificationBudget &&
        maxBlockCount == other.maxBlockCount &&
        maxTotalByteCount == other.maxTotalByteCount &&
        maxBlockByteCount == other.maxBlockByteCount &&
        maxDepth == other.maxDepth &&
        maxEntryCount == other.maxEntryCount &&
        maxChildReferencesPerBlock == other.maxChildReferencesPerBlock &&
        maxProofQueryByteCount == other.maxProofQueryByteCount

    override fun hashCode(): Int {
        var result = maxBlockCount
        result = 31 * result + maxTotalByteCount.hashCode()
        result = 31 * result + maxBlockByteCount
        result = 31 * result + maxDepth
        result = 31 * result + maxEntryCount.hashCode()
        result = 31 * result + maxChildReferencesPerBlock
        result = 31 * result + maxProofQueryByteCount
        return result
    }

    override fun toString(): String =
        "MerkleVerificationBudget(" +
            "maxBlockCount=$maxBlockCount, " +
            "maxTotalByteCount=$maxTotalByteCount, " +
            "maxBlockByteCount=$maxBlockByteCount, " +
            "maxDepth=$maxDepth, " +
            "maxEntryCount=$maxEntryCount, " +
            "maxChildReferencesPerBlock=$maxChildReferencesPerBlock, " +
            "maxProofQueryByteCount=$maxProofQueryByteCount)"
}

/** Presence or absence of a merge side; `Present(null)` differs from [Absent]. */
public sealed interface MerkleMergeValue<out V> {
    /** The state meaning the key is not present on this side. */
    public data object Absent : MerkleMergeValue<Nothing>

    /**
     * The state meaning the key holds a value here; the explicit case keeps a stored null distinguishable from absence.
     */
    public data class Present<V>(public val value: V) : MerkleMergeValue<V>
}

/** One true key-level three-way merge conflict. */
public data class MerkleThreeWayMergeConflict<K, V>(
    public val key: K,
    public val base: MerkleMergeValue<V>,
    public val left: MerkleMergeValue<V>,
    public val right: MerkleMergeValue<V>,
)

/** Typed conflict resolution. */
public sealed interface MerkleMergeResolution<out V> {
    /** Decline to settle the conflict, which fails the whole merge rather than producing a partly merged tree. */
    public data object Unresolved : MerkleMergeResolution<Nothing>

    /** Keep the common ancestor's state for this key. */
    public data object UseBase : MerkleMergeResolution<Nothing>

    /** Keep the left side's state for this key. */
    public data object UseLeft : MerkleMergeResolution<Nothing>

    /** Keep the right side's state for this key. */
    public data object UseRight : MerkleMergeResolution<Nothing>

    /** Leave this key out of the merged tree entirely. */
    public data object Delete : MerkleMergeResolution<Nothing>

    /** Store a value for this key that need not come from any of the three sides. */
    public data class SetValue<V>(public val value: V) : MerkleMergeResolution<V>
}

/** Complete publishable tree or all unresolved conflicts, never a partial tree. */
public class MerkleThreeWayMergeResult<K, V> private constructor(
    public val mergedTree: MerkleSearchTree<K, V>?,
    unresolvedConflicts: List<MerkleThreeWayMergeConflict<K, V>>,
) {
    public val unresolvedConflicts: List<MerkleThreeWayMergeConflict<K, V>> =
        immutablePersistenceList(unresolvedConflicts)
    public val isSuccess: Boolean get() = mergedTree != null

    internal companion object {
        fun <K, V> success(tree: MerkleSearchTree<K, V>): MerkleThreeWayMergeResult<K, V> =
            MerkleThreeWayMergeResult(tree, emptyList())

        fun <K, V> conflicted(
            conflicts: List<MerkleThreeWayMergeConflict<K, V>>,
        ): MerkleThreeWayMergeResult<K, V> {
            require(conflicts.isNotEmpty()) { "A conflicted result must contain a conflict." }
            return MerkleThreeWayMergeResult(null, conflicts)
        }
    }
}

/** Build a typed verification failure carrying its cause. */
internal fun verificationFailure(
    kind: MerkleVerificationFailureKind,
    message: String,
    digest: MerkleDigest? = null,
    cause: Throwable? = null,
): MerkleVerificationException = MerkleVerificationException(kind, message, digest, cause)

private fun <T> immutablePersistenceList(values: Collection<T>): List<T> =
    Collections.unmodifiableList(ArrayList(values))
