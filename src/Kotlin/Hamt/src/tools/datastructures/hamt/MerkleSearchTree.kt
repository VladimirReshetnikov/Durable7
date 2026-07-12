package tools.datastructures.hamt

import java.nio.charset.StandardCharsets
import java.util.ArrayDeque
import java.util.IdentityHashMap
import kotlin.math.max

internal val MerkleBlockMagic: ByteArray = "MST2".toByteArray(StandardCharsets.US_ASCII)
internal const val MerkleNodeBlockTag: Int = 1
internal const val MerkleBlockHeaderLength: Int = 4 + 1 + MerkleDigest.BYTE_LENGTH + 1 + 4 + 4

/** One retained key/value representative and its immutable canonical encodings. */
public class MerkleEntry<K, V> internal constructor(
    public val key: K,
    public val value: V,
    private val encodedKey: ByteArray,
    private val encodedValue: ByteArray,
    public val level: Int,
) {
    /** Returns a newly owned copy of the canonical key encoding. */
    public fun keyBytes(): ByteArray = encodedKey.copyOf()

    /** Returns a newly owned copy of the canonical value encoding. */
    public fun valueBytes(): ByteArray = encodedValue.copyOf()

    internal fun keyBytesInternal(): ByteArray = encodedKey
    internal fun valueBytesInternal(): ByteArray = encodedValue

    internal fun replacingValue(newValue: V, newValueBytes: ByteArray): MerkleEntry<K, V> =
        MerkleEntry(key, newValue, encodedKey, newValueBytes, level)
}

internal class MerkleNode<K, V>(
    internal val level: Int,
    internal val entries: List<MerkleEntry<K, V>>,
    internal val children: List<MerkleNode<K, V>?>,
    internal val count: Int,
    internal val height: Int,
    internal val blockCount: Int,
    internal val minimumKey: K,
    internal val maximumKey: K,
    internal val blockBytes: ByteArray,
    internal val digest: MerkleDigest,
)

/** One block-level topology record in deterministic preorder. */
public data class MerkleShapeEntry<K>(
    public val level: Int,
    public val key: K,
    public val entriesInBlock: Int,
    public val subtreeCount: Int,
)

/** Exact immutable `(digest, canonical bytes)` diagnostic block. */
public class MerkleEncodedBlock(
    public val digest: MerkleDigest,
    bytes: ByteArray,
) {
    private val content: ByteArray = bytes.copyOf()

    public val length: Int get() = content.size

    /** Returns a newly owned byte copy. */
    public fun toByteArray(): ByteArray = content.copyOf()

    override fun equals(other: Any?): Boolean = other is MerkleEncodedBlock &&
        digest == other.digest && content.contentEquals(other.content)

    override fun hashCode(): Int = 31 * digest.hashCode() + content.contentHashCode()

    override fun toString(): String = "$digest ($length bytes)"
}

/** Validated wide-tree representation statistics. */
public data class MerkleSearchTreeStatistics(
    public val count: Int,
    public val blockCount: Int,
    public val height: Int,
    public val minimumEntriesPerBlock: Int,
    public val maximumEntriesPerBlock: Int,
    public val minimumBlockBytes: Int,
    public val maximumBlockBytes: Int,
)

/** One semantic key-level difference. Variants preserve nullable values without ambiguity. */
public sealed interface MerkleMapDifference<K, V> {
    public data class Added<K, V>(public val key: K, public val value: V) : MerkleMapDifference<K, V>
    public data class Removed<K, V>(public val key: K, public val value: V) : MerkleMapDifference<K, V>
    public data class Changed<K, V>(
        public val key: K,
        public val before: V,
        public val after: V,
    ) : MerkleMapDifference<K, V>
}

/** Immutable canonical B=16 wide Merkle search tree. */
public class MerkleSearchTree<K, V> private constructor(
    internal val root: MerkleNode<K, V>?,
    public val policy: MerkleSearchTreePolicy<K, V>,
) : Iterable<MerkleEntry<K, V>> {
    public companion object {
        /** Creates an empty tree retaining [policy]. */
        public fun <K, V> empty(policy: MerkleSearchTreePolicy<K, V>): MerkleSearchTree<K, V> =
            MerkleSearchTree(null, policy)

        /** Creates a canonical tree with first-equivalent-key and last-value semantics. */
        public fun <K, V> from(
            items: Iterable<Pair<K, V>>,
            policy: MerkleSearchTreePolicy<K, V>,
        ): MerkleSearchTree<K, V> {
            val pending = items.mapIndexed { sequence, (key, value) ->
                PendingEntry(key, value, sequence)
            }.toMutableList()
            if (pending.isEmpty()) return empty(policy)
            pending.sortWith { left, right ->
                val comparison = policy.comparator.compare(left.key, right.key)
                if (comparison != 0) comparison else left.sequence.compareTo(right.sequence)
            }
            val tree = empty(policy)
            val grouped = ArrayList<MerkleEntry<K, V>>()
            var index = 0
            while (index < pending.size) {
                val first = pending[index]
                var value = first.value
                var next = index + 1
                while (next < pending.size && policy.comparator.compare(first.key, pending[next].key) == 0) {
                    value = pending[next].value
                    next++
                }
                grouped.add(tree.createEntry(first.key, value))
                index = next
            }
            return MerkleSearchTree(tree.buildCanonical(grouped), policy)
        }

        internal fun <K, V> fromVerifiedRoot(
            root: MerkleNode<K, V>?,
            policy: MerkleSearchTreePolicy<K, V>,
        ): MerkleSearchTree<K, V> = MerkleSearchTree(root, policy)

        /** Loads and fully verifies a content-addressed block closure under finite limits. */
        public fun <K, V> load(
            rootHash: MerkleDigest,
            policy: MerkleSearchTreePolicy<K, V>,
            store: MerkleBlockStore,
            budget: MerkleVerificationBudget = MerkleVerificationBudget(),
        ): MerkleSearchTree<K, V> = loadMerkleTree(rootHash, policy, store, budget)

        /** Verifies and imports a complete or store-completed partial block pack. */
        public fun <K, V> importPack(
            pack: MerkleBlockPack,
            policy: MerkleSearchTreePolicy<K, V>,
            destinationStore: MerkleBlockStore? = null,
            budget: MerkleVerificationBudget = MerkleVerificationBudget(),
        ): MerkleSearchTree<K, V> = importMerklePack(pack, policy, destinationStore, budget)

        /** Verifies an untrusted exact `MSP2` proof under finite limits. */
        public fun <K, V> verifyProof(
            proof: MerkleProof,
            policy: MerkleSearchTreePolicy<K, V>,
            budget: MerkleVerificationBudget = MerkleVerificationBudget(),
        ): MerkleProofVerificationResult = verifyMerkleProof(proof, policy, budget)

        /** Combines two descendants relative to a common base without publishing partial conflicts. */
        public fun <K, V> merge(
            baseTree: MerkleSearchTree<K, V>,
            left: MerkleSearchTree<K, V>,
            right: MerkleSearchTree<K, V>,
            resolver: ((MerkleThreeWayMergeConflict<K, V>) -> MerkleMergeResolution<V>)? = null,
            valuesEqual: (V, V) -> Boolean = { first, second -> first == second },
        ): MerkleThreeWayMergeResult<K, V> =
            mergeMerkleTrees(baseTree, left, right, resolver, valuesEqual)
    }

    public val size: Int get() = root?.count ?: 0
    public val isEmpty: Boolean get() = root == null
    public val height: Int get() = root?.height ?: 0
    public val blockCount: Int get() = root?.blockCount ?: 0
    public val rootHash: MerkleDigest get() = root?.digest ?: policy.emptyDigest

    /** Returns whether two versions retain the exact same root object. */
    public fun sharesRootWith(other: MerkleSearchTree<K, V>): Boolean = root === other.root

    /** Counts block objects shared by reference identity. */
    public fun sharedBlockCount(other: MerkleSearchTree<K, V>): Int {
        val left = IdentityHashMap<MerkleNode<K, V>, Unit>()
        for (node in enumerateNodesPreorder(root)) left[node] = Unit
        val visited = IdentityHashMap<MerkleNode<K, V>, Unit>()
        var shared = 0
        for (node in enumerateNodesPreorder(other.root)) {
            if (visited.put(node, Unit) != null) continue
            if (left.containsKey(node)) shared++
        }
        return shared
    }

    /** Returns exact canonical blocks in deterministic preorder. */
    public fun blocksPreorder(): List<MerkleEncodedBlock> = enumerateNodesPreorder(root).map { node ->
        MerkleEncodedBlock(node.digest, node.blockBytes)
    }

    /** Returns block-level shape records in deterministic preorder. */
    public fun shape(): List<MerkleShapeEntry<K>> = buildList(size) {
        for (node in enumerateNodesPreorder(root)) {
            for (entry in node.entries) {
                add(MerkleShapeEntry(node.level, entry.key, node.entries.size, node.count))
            }
        }
    }

    public fun containsKey(key: K): Boolean = getEntry(key) != null

    /** Returns the retained entry, preserving the first equivalent key representative. */
    public fun getEntry(key: K): MerkleEntry<K, V>? {
        var node = root
        while (node != null) {
            val position = findPosition(node.entries, key)
            if (position.found) return node.entries[position.index]
            node = node.children[position.index]
        }
        return null
    }

    /** Returns a value, or `null` on absence; use [getEntry] when `V` itself is nullable. */
    public operator fun get(key: K): V? = getEntry(key)?.value

    /** Adds or replaces an entry by copying only affected immutable blocks. */
    public fun setItem(key: K, value: V): MerkleSearchTree<K, V> {
        val existing = getEntry(key)
        if (existing != null) {
            val valueBytes = policy.valueCodec.encode(value)
            if (existing.valueBytesInternal().contentEquals(valueBytes)) return this
            val replacement = existing.replacingValue(value, valueBytes)
            return MerkleSearchTree(updateValue(checkNotNull(root), key, replacement), policy)
        }
        return MerkleSearchTree(insert(root, createEntry(key, value)), policy)
    }

    /** Removes an equivalent key and contracts empty block shells. */
    public fun remove(key: K): MerkleSearchTree<K, V> {
        val result = removeNode(root, key)
        return if (!result.removed) this else MerkleSearchTree(result.node, policy)
    }

    /** Returns an empty tree retaining this policy. */
    public fun clear(): MerkleSearchTree<K, V> = if (isEmpty) this else empty(policy)

    /** Compares policy domains and root addresses in O(1). */
    public fun contentEquals(other: MerkleSearchTree<K, V>): Boolean =
        policy.domainDigest == other.policy.domainDigest && rootHash == other.rootHash

    /** Compares semantic map contents with a caller-supplied value relation. */
    public fun mapEquals(
        other: MerkleSearchTree<K, V>,
        valuesEqual: (V, V) -> Boolean = { left, right -> left == right },
    ): Boolean {
        if (size != other.size || policy.domainDigest != other.policy.domainDigest) return false
        if (rootHash == other.rootHash) return nodesEqual(root, other.root, valuesEqual)
        val left = iterator()
        val right = other.iterator()
        while (left.hasNext() && right.hasNext()) {
            val leftEntry = left.next()
            val rightEntry = right.next()
            if (policy.comparator.compare(leftEntry.key, rightEntry.key) != 0 ||
                !valuesEqual(leftEntry.value, rightEntry.value)
            ) return false
        }
        return !left.hasNext() && !right.hasNext()
    }

    /** Computes a block-digest-pruned semantic diff. */
    public fun diff(
        other: MerkleSearchTree<K, V>,
        valuesEqual: (V, V) -> Boolean = { left, right -> left == right },
    ): List<MerkleMapDifference<K, V>> {
        requireCompatible(other)
        val result = ArrayList<MerkleMapDifference<K, V>>()
        diffNodes(root, other.root, valuesEqual, result)
        return result
    }

    /** Returns a fresh in-order sequence with an explicit traversal stack. */
    public fun entries(): Sequence<MerkleEntry<K, V>> = Sequence { MerkleTreeIterator(root) }

    override fun iterator(): Iterator<MerkleEntry<K, V>> = MerkleTreeIterator(root)

    /** Eagerly validates and lazily enumerates an inclusive comparer-ordered range. */
    public fun enumerateRange(minimumKey: K, maximumKey: K): Sequence<MerkleEntry<K, V>> {
        require(policy.comparator.compare(minimumKey, maximumKey) <= 0) {
            "The minimum key must not follow the maximum key."
        }
        return Sequence { MerkleRangeIterator(root, policy, minimumKey, maximumKey) }
    }

    /** Validates ordering, levels, metadata, exact block bytes, and content addresses. */
    public fun validateStructure(): MerkleSearchTreeStatistics {
        val current = root ?: return MerkleSearchTreeStatistics(0, 0, 0, 0, 0, 0, 0)
        val accumulator = ValidationAccumulator()
        validateNode(current, accumulator)
        check(accumulator.entryCount == size && accumulator.blockCount == blockCount) {
            "Merkle root metadata disagrees with validated contents."
        }
        return accumulator.toStatistics(height)
    }

    internal fun requireCompatible(other: MerkleSearchTree<K, V>): Unit {
        require(policy.domainDigest == other.policy.domainDigest) {
            "Merkle trees must use the same algorithm, policy, and codec domain."
        }
    }

    internal fun createEntry(key: K, value: V): MerkleEntry<K, V> {
        val keyBytes = policy.keyCodec.encode(key)
        val valueBytes = policy.valueCodec.encode(value)
        val level = MerkleSearchTreePolicy.level(policy.hashKeyBytes(keyBytes))
        return MerkleEntry(key, value, keyBytes, valueBytes, level)
    }

    internal fun findPosition(entries: List<MerkleEntry<K, V>>, key: K): SearchPosition {
        var low = 0
        var high = entries.size
        while (low < high) {
            val middle = (low + high) ushr 1
            if (policy.comparator.compare(entries[middle].key, key) < 0) low = middle + 1 else high = middle
        }
        return SearchPosition(
            low,
            low < entries.size && policy.comparator.compare(entries[low].key, key) == 0,
        )
    }

    internal fun buildCanonical(entries: List<MerkleEntry<K, V>>): MerkleNode<K, V>? {
        if (entries.isEmpty()) return null
        val maximumLevel = entries.maxOf(MerkleEntry<K, V>::level)
        val blockEntries = ArrayList<MerkleEntry<K, V>>()
        val children = ArrayList<MerkleNode<K, V>?>()
        var segmentStart = 0
        for (index in entries.indices) {
            if (entries[index].level != maximumLevel) continue
            children.add(buildCanonical(entries.subList(segmentStart, index)))
            blockEntries.add(entries[index])
            segmentStart = index + 1
        }
        children.add(buildCanonical(entries.subList(segmentStart, entries.size)))
        return newNode(maximumLevel, blockEntries, children)
    }

    private fun updateValue(
        node: MerkleNode<K, V>,
        key: K,
        replacement: MerkleEntry<K, V>,
    ): MerkleNode<K, V> {
        val position = findPosition(node.entries, key)
        if (position.found) {
            val entries = node.entries.toMutableList()
            entries[position.index] = replacement
            return newNode(node.level, entries, node.children)
        }
        val children = node.children.toMutableList()
        children[position.index] = updateValue(checkNotNull(children[position.index]), key, replacement)
        return newNode(node.level, node.entries, children)
    }

    private fun insert(node: MerkleNode<K, V>?, entry: MerkleEntry<K, V>): MerkleNode<K, V> {
        if (node == null) return newNode(entry.level, listOf(entry), listOf(null, null))
        if (entry.level > node.level) {
            val split = split(node, entry.key)
            return newNode(entry.level, listOf(entry), listOf(split.left, split.right))
        }
        val position = findPosition(node.entries, entry.key)
        check(!position.found)
        if (entry.level < node.level) {
            val children = node.children.toMutableList()
            children[position.index] = insert(children[position.index], entry)
            return newNode(node.level, node.entries, children)
        }
        val childSplit = split(node.children[position.index], entry.key)
        val entries = node.entries.toMutableList()
        entries.add(position.index, entry)
        val children = node.children.toMutableList()
        children[position.index] = childSplit.left
        children.add(position.index + 1, childSplit.right)
        return newNode(node.level, entries, children)
    }

    private fun split(node: MerkleNode<K, V>?, key: K): NodeSplit<K, V> {
        if (node == null) return NodeSplit(null, null)
        val position = findPosition(node.entries, key)
        check(!position.found)
        val childSplit = split(node.children[position.index], key)
        val leftEntries = node.entries.subList(0, position.index).toList()
        val leftChildren = node.children.subList(0, position.index + 1).toMutableList()
        leftChildren[position.index] = childSplit.left
        val rightEntries = node.entries.subList(position.index, node.entries.size).toList()
        val rightChildren = node.children.subList(position.index, node.children.size).toMutableList()
        rightChildren[0] = childSplit.right
        return NodeSplit(
            createOrCollapse(node.level, leftEntries, leftChildren),
            createOrCollapse(node.level, rightEntries, rightChildren),
        )
    }

    private fun removeNode(node: MerkleNode<K, V>?, key: K): NodeRemoval<K, V> {
        if (node == null) return NodeRemoval(null, false)
        val position = findPosition(node.entries, key)
        if (!position.found) {
            val child = removeNode(node.children[position.index], key)
            if (!child.removed) return NodeRemoval(node, false)
            val children = node.children.toMutableList()
            children[position.index] = child.node
            return NodeRemoval(newNode(node.level, node.entries, children), true)
        }
        val entries = node.entries.toMutableList()
        entries.removeAt(position.index)
        val children = node.children.toMutableList()
        children[position.index] = join(children[position.index], children[position.index + 1])
        children.removeAt(position.index + 1)
        return NodeRemoval(createOrCollapse(node.level, entries, children), true)
    }

    private fun join(left: MerkleNode<K, V>?, right: MerkleNode<K, V>?): MerkleNode<K, V>? {
        if (left == null) return right
        if (right == null) return left
        if (left.level > right.level) {
            val children = left.children.toMutableList()
            children[children.lastIndex] = join(children.last(), right)
            return newNode(left.level, left.entries, children)
        }
        if (left.level < right.level) {
            val children = right.children.toMutableList()
            children[0] = join(left, children[0])
            return newNode(right.level, right.entries, children)
        }
        val entries = left.entries + right.entries
        val children = ArrayList<MerkleNode<K, V>?>(entries.size + 1)
        children.addAll(left.children.subList(0, left.entries.size))
        children.add(join(left.children.last(), right.children.first()))
        children.addAll(right.children.subList(1, right.children.size))
        return newNode(left.level, entries, children)
    }

    private fun createOrCollapse(
        level: Int,
        entries: List<MerkleEntry<K, V>>,
        children: List<MerkleNode<K, V>?>,
    ): MerkleNode<K, V>? {
        check(children.size == entries.size + 1)
        return if (entries.isEmpty()) children.first() else newNode(level, entries, children)
    }

    internal fun newNode(
        level: Int,
        entries: List<MerkleEntry<K, V>>,
        children: List<MerkleNode<K, V>?>,
    ): MerkleNode<K, V> {
        require(level in 0..64 && entries.isNotEmpty() && children.size == entries.size + 1) {
            "An MST block must contain a valid level, entries, and one extra child interval."
        }
        var count = entries.size
        var height = 1
        var blocks = 1
        for (child in children) {
            if (child == null) continue
            count = Math.addExact(count, child.count)
            height = max(height, Math.addExact(1, child.height))
            blocks = Math.addExact(blocks, child.blockCount)
        }
        val immutableEntries = entries.toList()
        val immutableChildren = children.toList()
        val blockBytes = encodeBlock(level, count, immutableEntries, immutableChildren)
        return MerkleNode(
            level,
            immutableEntries,
            immutableChildren,
            count,
            height,
            blocks,
            immutableChildren.first()?.minimumKey ?: immutableEntries.first().key,
            immutableChildren.last()?.maximumKey ?: immutableEntries.last().key,
            blockBytes,
            MerkleDigest.hash(blockBytes),
        )
    }

    internal fun encodeBlock(
        level: Int,
        subtreeCount: Int,
        entries: List<MerkleEntry<K, V>>,
        children: List<MerkleNode<K, V>?>,
    ): ByteArray {
        var length = Math.addExact(
            MerkleBlockHeaderLength,
            Math.multiplyExact(children.size, MerkleDigest.BYTE_LENGTH),
        )
        for (entry in entries) {
            length = Math.addExact(length, 8)
            length = Math.addExact(length, entry.keyBytesInternal().size)
            length = Math.addExact(length, entry.valueBytesInternal().size)
        }
        val result = ByteArray(length)
        var offset = 0
        MerkleBlockMagic.copyInto(result, offset)
        offset += MerkleBlockMagic.size
        result[offset++] = MerkleNodeBlockTag.toByte()
        policy.domainDigest.copyInto(result, offset)
        offset += MerkleDigest.BYTE_LENGTH
        result[offset++] = level.toByte()
        writeIntBigEndian(result, offset, subtreeCount)
        offset += 4
        writeIntBigEndian(result, offset, entries.size)
        offset += 4
        for (entry in entries) {
            writeIntBigEndian(result, offset, entry.keyBytesInternal().size)
            offset += 4
            entry.keyBytesInternal().copyInto(result, offset)
            offset += entry.keyBytesInternal().size
            writeIntBigEndian(result, offset, entry.valueBytesInternal().size)
            offset += 4
            entry.valueBytesInternal().copyInto(result, offset)
            offset += entry.valueBytesInternal().size
        }
        for (child in children) {
            (child?.digest ?: policy.emptyDigest).copyInto(result, offset)
            offset += MerkleDigest.BYTE_LENGTH
        }
        check(offset == result.size)
        return result
    }

    private fun nodesEqual(
        left: MerkleNode<K, V>?,
        right: MerkleNode<K, V>?,
        valuesEqual: (V, V) -> Boolean,
    ): Boolean {
        if (left == null || right == null) return left == null && right == null
        if (left.level != right.level || left.entries.size != right.entries.size) return false
        for (index in left.entries.indices) {
            val leftEntry = left.entries[index]
            val rightEntry = right.entries[index]
            if (leftEntry !== rightEntry &&
                (policy.comparator.compare(leftEntry.key, rightEntry.key) != 0 ||
                    !valuesEqual(leftEntry.value, rightEntry.value))
            ) return false
        }
        return left.children.indices.all { index ->
            nodesEqual(left.children[index], right.children[index], valuesEqual)
        }
    }

    private fun diffNodes(
        left: MerkleNode<K, V>?,
        right: MerkleNode<K, V>?,
        valuesEqual: (V, V) -> Boolean,
        result: MutableList<MerkleMapDifference<K, V>>,
    ): Unit {
        if (left === right || left?.digest == right?.digest) return
        if (left == null) {
            for (entry in MerkleTreeIterator(right)) {
                result.add(MerkleMapDifference.Added(entry.key, entry.value))
            }
            return
        }
        if (right == null) {
            for (entry in MerkleTreeIterator(left)) {
                result.add(MerkleMapDifference.Removed(entry.key, entry.value))
            }
            return
        }
        if (sameSeparators(left, right)) {
            for (index in left.entries.indices) {
                diffNodes(left.children[index], right.children[index], valuesEqual, result)
                if (!valuesEqual(left.entries[index].value, right.entries[index].value)) {
                    result.add(
                        MerkleMapDifference.Changed(
                            left.entries[index].key,
                            left.entries[index].value,
                            right.entries[index].value,
                        ),
                    )
                }
            }
            diffNodes(left.children.last(), right.children.last(), valuesEqual, result)
            return
        }
        val leftIterator = PeekingIterator(MerkleTreeIterator(left))
        val rightIterator = PeekingIterator(MerkleTreeIterator(right))
        while (leftIterator.hasNext() || rightIterator.hasNext()) {
            val comparison = when {
                !rightIterator.hasNext() -> -1
                !leftIterator.hasNext() -> 1
                else -> policy.comparator.compare(leftIterator.peek().key, rightIterator.peek().key)
            }
            when {
                comparison < 0 -> leftIterator.next().also { entry ->
                    result.add(MerkleMapDifference.Removed(entry.key, entry.value))
                }
                comparison > 0 -> rightIterator.next().also { entry ->
                    result.add(MerkleMapDifference.Added(entry.key, entry.value))
                }
                else -> {
                    val before = leftIterator.next()
                    val after = rightIterator.next()
                    if (!valuesEqual(before.value, after.value)) {
                        result.add(MerkleMapDifference.Changed(before.key, before.value, after.value))
                    }
                }
            }
        }
    }

    private fun sameSeparators(left: MerkleNode<K, V>, right: MerkleNode<K, V>): Boolean =
        left.level == right.level && left.entries.size == right.entries.size &&
            left.entries.indices.all { index ->
                policy.comparator.compare(left.entries[index].key, right.entries[index].key) == 0
            }

    private fun validateNode(node: MerkleNode<K, V>, accumulator: ValidationAccumulator): Unit {
        check(node.entries.isNotEmpty() && node.children.size == node.entries.size + 1 && node.level in 0..64) {
            "An MST block has invalid level or entry/child arity."
        }
        var count = node.entries.size
        var height = 1
        var blocks = 1
        for (index in node.entries.indices) {
            val entry = node.entries[index]
            check(entry.level == node.level) { "An MST entry is stored in the wrong level." }
            val keyBytes = policy.keyCodec.encode(entry.key)
            val valueBytes = policy.valueCodec.encode(entry.value)
            check(keyBytes.contentEquals(entry.keyBytesInternal())) {
                "An MST entry's retained key no longer matches its canonical bytes."
            }
            check(valueBytes.contentEquals(entry.valueBytesInternal())) {
                "An MST entry's retained value no longer matches its canonical bytes."
            }
            check(MerkleSearchTreePolicy.level(policy.hashKeyBytes(keyBytes)) == entry.level) {
                "An MST entry's key-derived level is invalid."
            }
            if (index != 0) {
                check(policy.comparator.compare(node.entries[index - 1].key, entry.key) < 0) {
                    "MST block entries are not strictly ordered."
                }
            }
        }
        for (index in node.children.indices) {
            val child = node.children[index] ?: continue
            check(child.level < node.level) { "An MST child does not occupy a lower level." }
            if (index != 0) {
                check(policy.comparator.compare(child.minimumKey, node.entries[index - 1].key) > 0) {
                    "An MST child crosses its lower key separator."
                }
            }
            if (index != node.entries.size) {
                check(policy.comparator.compare(child.maximumKey, node.entries[index].key) < 0) {
                    "An MST child crosses its upper key separator."
                }
            }
            validateNode(child, accumulator)
            count = Math.addExact(count, child.count)
            height = max(height, Math.addExact(1, child.height))
            blocks = Math.addExact(blocks, child.blockCount)
        }
        check(count == node.count && height == node.height && blocks == node.blockCount) {
            "An MST block has invalid cached metadata."
        }
        val minimumKey = node.children.first()?.minimumKey ?: node.entries.first().key
        val maximumKey = node.children.last()?.maximumKey ?: node.entries.last().key
        check(policy.comparator.compare(node.minimumKey, minimumKey) == 0 &&
            policy.comparator.compare(node.maximumKey, maximumKey) == 0
        ) {
            "An MST block has invalid cached key bounds."
        }
        val encoded = encodeBlock(node.level, node.count, node.entries, node.children)
        check(encoded.contentEquals(node.blockBytes) && MerkleDigest.hash(node.blockBytes) == node.digest) {
            "An MST block has invalid canonical bytes or digest."
        }
        accumulator.add(node)
    }
}

internal data class SearchPosition(val index: Int, val found: Boolean)
private data class PendingEntry<K, V>(val key: K, val value: V, val sequence: Int)
private data class NodeSplit<K, V>(val left: MerkleNode<K, V>?, val right: MerkleNode<K, V>?)
private data class NodeRemoval<K, V>(val node: MerkleNode<K, V>?, val removed: Boolean)

private sealed interface IterationFrame<K, V> {
    data class Visit<K, V>(val node: MerkleNode<K, V>, val index: Int) : IterationFrame<K, V>
    data class Emit<K, V>(val entry: MerkleEntry<K, V>) : IterationFrame<K, V>
}

private class MerkleTreeIterator<K, V>(root: MerkleNode<K, V>?) : Iterator<MerkleEntry<K, V>> {
    private val pending: ArrayDeque<IterationFrame<K, V>> = ArrayDeque()
    private var prepared: Boolean = false
    private var nextEntry: MerkleEntry<K, V>? = null

    init {
        if (root != null) pending.addLast(IterationFrame.Visit(root, 0))
    }

    override fun hasNext(): Boolean {
        prepare()
        return nextEntry != null
    }

    override fun next(): MerkleEntry<K, V> {
        prepare()
        val result = nextEntry ?: throw NoSuchElementException()
        prepared = false
        nextEntry = null
        return result
    }

    private fun prepare(): Unit {
        if (prepared) return
        prepared = true
        while (pending.isNotEmpty()) {
            when (val frame = pending.removeLast()) {
                is IterationFrame.Emit -> {
                    nextEntry = frame.entry
                    return
                }
                is IterationFrame.Visit -> {
                    val node = frame.node
                    if (frame.index < node.entries.size) {
                        pending.addLast(IterationFrame.Visit(node, frame.index + 1))
                        pending.addLast(IterationFrame.Emit(node.entries[frame.index]))
                        node.children[frame.index]?.let { child ->
                            pending.addLast(IterationFrame.Visit(child, 0))
                        }
                    } else {
                        node.children.last()?.let { child ->
                            pending.addLast(IterationFrame.Visit(child, 0))
                        }
                    }
                }
            }
        }
    }
}

private class MerkleRangeIterator<K, V>(
    root: MerkleNode<K, V>?,
    private val policy: MerkleSearchTreePolicy<K, V>,
    private val minimumKey: K,
    private val maximumKey: K,
) : Iterator<MerkleEntry<K, V>> {
    private val pending: ArrayDeque<IterationFrame<K, V>> = ArrayDeque()
    private var prepared: Boolean = false
    private var nextEntry: MerkleEntry<K, V>? = null

    init {
        if (root != null) pending.addLast(IterationFrame.Visit(root, 0))
    }

    override fun hasNext(): Boolean {
        prepare()
        return nextEntry != null
    }

    override fun next(): MerkleEntry<K, V> {
        prepare()
        val result = nextEntry ?: throw NoSuchElementException()
        prepared = false
        nextEntry = null
        return result
    }

    private fun prepare(): Unit {
        if (prepared) return
        prepared = true
        while (pending.isNotEmpty()) {
            when (val frame = pending.removeLast()) {
                is IterationFrame.Emit -> {
                    nextEntry = frame.entry
                    return
                }
                is IterationFrame.Visit -> {
                    val node = frame.node
                    if (frame.index < node.entries.size) {
                        val entry = node.entries[frame.index]
                        val low = policy.comparator.compare(entry.key, minimumKey)
                        val high = policy.comparator.compare(entry.key, maximumKey)
                        if (high <= 0) {
                            pending.addLast(IterationFrame.Visit(node, frame.index + 1))
                            if (low >= 0) pending.addLast(IterationFrame.Emit(entry))
                        }
                        if (low > 0) {
                            node.children[frame.index]?.let { child ->
                                pending.addLast(IterationFrame.Visit(child, 0))
                            }
                        }
                    } else if (policy.comparator.compare(node.entries.last().key, maximumKey) < 0) {
                        node.children.last()?.let { child ->
                            pending.addLast(IterationFrame.Visit(child, 0))
                        }
                    }
                }
            }
        }
    }
}

private class PeekingIterator<T>(private val iterator: Iterator<T>) {
    private var prepared: Boolean = false
    private var nextValue: T? = null

    fun hasNext(): Boolean {
        prepare()
        return nextValue != null
    }

    fun peek(): T {
        prepare()
        return nextValue ?: throw NoSuchElementException()
    }

    fun next(): T {
        prepare()
        val result = nextValue ?: throw NoSuchElementException()
        prepared = false
        nextValue = null
        return result
    }

    private fun prepare(): Unit {
        if (prepared) return
        prepared = true
        if (iterator.hasNext()) nextValue = iterator.next()
    }
}

private class ValidationAccumulator {
    var entryCount: Int = 0
    var blockCount: Int = 0
    private var minimumEntries: Int = Int.MAX_VALUE
    private var maximumEntries: Int = 0
    private var minimumBlockBytes: Int = Int.MAX_VALUE
    private var maximumBlockBytes: Int = 0

    fun <K, V> add(node: MerkleNode<K, V>): Unit {
        entryCount = Math.addExact(entryCount, node.entries.size)
        blockCount = Math.addExact(blockCount, 1)
        minimumEntries = minOf(minimumEntries, node.entries.size)
        maximumEntries = maxOf(maximumEntries, node.entries.size)
        minimumBlockBytes = minOf(minimumBlockBytes, node.blockBytes.size)
        maximumBlockBytes = maxOf(maximumBlockBytes, node.blockBytes.size)
    }

    fun toStatistics(height: Int): MerkleSearchTreeStatistics = MerkleSearchTreeStatistics(
        entryCount,
        blockCount,
        height,
        if (blockCount == 0) 0 else minimumEntries,
        maximumEntries,
        if (blockCount == 0) 0 else minimumBlockBytes,
        maximumBlockBytes,
    )
}

internal fun <K, V> enumerateNodesPreorder(root: MerkleNode<K, V>?): List<MerkleNode<K, V>> {
    if (root == null) return emptyList()
    val result = ArrayList<MerkleNode<K, V>>()
    val pending = ArrayDeque<MerkleNode<K, V>>()
    pending.addLast(root)
    while (pending.isNotEmpty()) {
        val node = pending.removeLast()
        for (index in node.children.indices.reversed()) {
            node.children[index]?.let(pending::addLast)
        }
        result.add(node)
    }
    return result
}

private fun writeIntBigEndian(destination: ByteArray, offset: Int, value: Int): Unit {
    destination[offset] = (value ushr 24).toByte()
    destination[offset + 1] = (value ushr 16).toByte()
    destination[offset + 2] = (value ushr 8).toByte()
    destination[offset + 3] = value.toByte()
}
