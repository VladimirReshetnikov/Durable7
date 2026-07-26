/*
 * Persistent big-endian Patricia tries over fixed-width integer keys.
 *
 * Branches on the bit positions where keys actually differ, skipping runs of shared prefix, so
 * every operation is bounded by the key width rather than the entry count and needs no hashing or
 * rebalancing. Big-endian branching makes traversal ascending and lets merges share whole untouched
 * subtrees.
 */
package durable7.hamt

private fun <T> patriciaValuesEqual(left: T, right: T): Boolean = left === right || left == right

private sealed interface PatriciaNode<K, V> {
    val count: Int
}

private data class PatriciaLeaf<K, V>(val path: ULong, val key: K, val value: V) : PatriciaNode<K, V> {
    override val count: Int get() = 1
}

private data class PatriciaBranch<K, V>(
    val prefix: ULong,
    val mask: ULong,
    val left: PatriciaNode<K, V>,
    val right: PatriciaNode<K, V>,
) : PatriciaNode<K, V> {
    override val count: Int = Math.addExact(left.count, right.count)
}

private data class PatriciaChange<K, V>(val node: PatriciaNode<K, V>?, val changed: Boolean, val added: Boolean)

private class PatriciaCore<K, V>(
    val root: PatriciaNode<K, V>?,
    val size: Int,
    val encode: (K) -> ULong,
) : Iterable<Pair<K, V>> {
    fun get(key: K): V? {
        val path = encode(key)
        var node = root ?: return null
        while (true) when (node) {
            is PatriciaLeaf -> return if (node.path == path) node.value else null
            is PatriciaBranch -> {
                if (prefixOf(path, node.mask) != node.prefix) return null
                node = if (path and node.mask == 0uL) node.left else node.right
            }
        }
    }

    fun containsKey(key: K): Boolean {
        val node = root ?: return false
        return findLeaf(node, encode(key)) != null
    }

    fun entryAt(index: Int): Pair<K, V>? {
        if (index < 0 || index >= size) return null
        var remaining = index
        var node = root ?: return null
        while (node is PatriciaBranch) {
            if (remaining < node.left.count) {
                node = node.left
            } else {
                remaining -= node.left.count
                node = node.right
            }
        }
        node as PatriciaLeaf
        return node.key to node.value
    }

    fun lowerBoundRank(key: K): Pair<Int, Boolean> {
        val path = encode(key)
        var rank = 0
        var node = root ?: return 0 to false
        while (node is PatriciaBranch) {
            if (prefixOf(path, node.mask) != node.prefix) {
                return if (path < node.prefix) rank to false else Math.addExact(rank, node.count) to false
            }
            if (path and node.mask == 0uL) {
                node = node.left
            } else {
                rank = Math.addExact(rank, node.left.count)
                node = node.right
            }
        }
        node as PatriciaLeaf
        return if (node.path < path) Math.addExact(rank, 1) to false else rank to (node.path == path)
    }

    fun put(key: K, value: V): PatriciaCore<K, V> {
        val path = encode(key)
        val change = put(root, path, key, value)
        if (!change.changed) return this
        return PatriciaCore(change.node, size + if (change.added) 1 else 0, encode)
    }

    private fun put(node: PatriciaNode<K, V>?, path: ULong, key: K, value: V): PatriciaChange<K, V> = when (node) {
        null -> PatriciaChange(PatriciaLeaf(path, key, value), true, true)
        is PatriciaLeaf -> when {
            node.path == path && patriciaValuesEqual(node.value, value) -> PatriciaChange(node, false, false)
            node.path == path -> PatriciaChange(PatriciaLeaf(path, node.key, value), true, false)
            else -> PatriciaChange(join(node.path, node, path, PatriciaLeaf(path, key, value)), true, true)
        }
        is PatriciaBranch -> {
            if (prefixOf(path, node.mask) != node.prefix) {
                PatriciaChange(join(node.prefix, node, path, PatriciaLeaf(path, key, value)), true, true)
            } else if (path and node.mask == 0uL) {
                val child = put(node.left, path, key, value)
                if (!child.changed) PatriciaChange(node, false, false)
                else PatriciaChange(node.copy(left = child.node!!), true, child.added)
            } else {
                val child = put(node.right, path, key, value)
                if (!child.changed) PatriciaChange(node, false, false)
                else PatriciaChange(node.copy(right = child.node!!), true, child.added)
            }
        }
    }

    fun remove(key: K): PatriciaCore<K, V> {
        val change = remove(root, encode(key))
        if (!change.changed) return this
        return PatriciaCore(change.node, size - 1, encode)
    }

    fun union(other: PatriciaCore<K, V>): PatriciaCore<K, V> {
        val merged = unionNodes(root, other.root)
        if (merged === root) return this
        return PatriciaCore(merged, merged?.count ?: 0, encode)
    }

    fun union(other: PatriciaCore<K, V>, combine: (K, V, V) -> V): PatriciaCore<K, V> {
        val merged = unionCombinedNodes(root, other.root, combine)
        if (merged === root) return this
        return PatriciaCore(merged, merged?.count ?: 0, encode)
    }

    fun intersect(other: PatriciaCore<K, *>): PatriciaCore<K, V> {
        val merged = intersectNodes(root, other.root)
        if (merged === root) return this
        return PatriciaCore(merged, merged?.count ?: 0, encode)
    }

    fun intersect(other: PatriciaCore<K, V>, combine: (K, V, V) -> V): PatriciaCore<K, V> {
        val merged = intersectCombinedNodes(root, other.root, combine)
        if (merged === root) return this
        return PatriciaCore(merged, merged?.count ?: 0, encode)
    }

    fun except(other: PatriciaCore<K, *>): PatriciaCore<K, V> {
        val merged = exceptNodes(root, other.root)
        if (merged === root) return this
        return PatriciaCore(merged, merged?.count ?: 0, encode)
    }

    private fun remove(node: PatriciaNode<K, V>?, path: ULong): PatriciaChange<K, V> = when (node) {
        null -> PatriciaChange(null, false, false)
        is PatriciaLeaf -> if (node.path == path) PatriciaChange(null, true, false) else PatriciaChange(node, false, false)
        is PatriciaBranch -> {
            if (prefixOf(path, node.mask) != node.prefix) return PatriciaChange(node, false, false)
            if (path and node.mask == 0uL) {
                val child = remove(node.left, path)
                if (!child.changed) PatriciaChange(node, false, false)
                else PatriciaChange(child.node?.let { node.copy(left = it) } ?: node.right, true, false)
            } else {
                val child = remove(node.right, path)
                if (!child.changed) PatriciaChange(node, false, false)
                else PatriciaChange(child.node?.let { node.copy(right = it) } ?: node.left, true, false)
            }
        }
    }

    override fun iterator(): Iterator<Pair<K, V>> = sequence {
        suspend fun SequenceScope<Pair<K, V>>.walk(node: PatriciaNode<K, V>) {
            when (node) {
                is PatriciaLeaf -> yield(node.key to node.value)
                is PatriciaBranch -> { walk(node.left); walk(node.right) }
            }
        }
        root?.let { walk(it) }
    }.iterator()

    companion object {
        private fun prefixOf(path: ULong, mask: ULong): ULong = path and ((mask shl 1) - 1uL).inv()
        private fun <K, V> join(
            leftPath: ULong, left: PatriciaNode<K, V>, rightPath: ULong, right: PatriciaNode<K, V>,
        ): PatriciaNode<K, V> {
            val mask = ULong.MAX_VALUE shr (leftPath xor rightPath).countLeadingZeroBits()
            val branchMask = 1uL shl (63 - mask.countLeadingZeroBits())
            val prefix = prefixOf(leftPath, branchMask)
            return if (leftPath and branchMask == 0uL) PatriciaBranch(prefix, branchMask, left, right)
            else PatriciaBranch(prefix, branchMask, right, left)
        }
    }
}

private fun <K, V> unionNodes(left: PatriciaNode<K, V>?, right: PatriciaNode<K, V>?): PatriciaNode<K, V>? {
    if (left == null) return right
    if (right == null || left === right) return left
    if (left is PatriciaLeaf) return putNode(right, left, preferExisting = true)
    if (right is PatriciaLeaf) return putNode(left, right, preferExisting = false)
    left as PatriciaBranch
    right as PatriciaBranch
    return when {
        left.mask == right.mask && left.prefix == right.prefix -> rebuildPatricia(
            left,
            unionNodes(left.left, right.left)!!,
            unionNodes(left.right, right.right)!!,
        )
        left.mask > right.mask && prefixOfPatricia(right.prefix, left.mask) == left.prefix ->
            if (right.prefix and left.mask == 0uL) rebuildPatricia(left, unionNodes(left.left, right)!!, left.right)
            else rebuildPatricia(left, left.left, unionNodes(left.right, right)!!)
        right.mask > left.mask && prefixOfPatricia(left.prefix, right.mask) == right.prefix ->
            if (left.prefix and right.mask == 0uL) rebuildPatricia(right, unionNodes(left, right.left)!!, right.right)
            else rebuildPatricia(right, right.left, unionNodes(left, right.right)!!)
        else -> joinPatricia(left.prefix, left, right.prefix, right)
    }
}

private fun <K, V> putNode(
    node: PatriciaNode<K, V>, leaf: PatriciaLeaf<K, V>, preferExisting: Boolean,
): PatriciaNode<K, V> = when (node) {
    is PatriciaLeaf -> when {
        node.path == leaf.path && patriciaValuesEqual(node.value, leaf.value) -> if (preferExisting) leaf else node
        node.path == leaf.path && preferExisting -> node
        node.path == leaf.path -> leaf
        else -> joinPatricia(node.path, node, leaf.path, leaf)
    }
    is PatriciaBranch -> if (prefixOfPatricia(leaf.path, node.mask) != node.prefix) {
        joinPatricia(node.prefix, node, leaf.path, leaf)
    } else if (leaf.path and node.mask == 0uL) {
        rebuildPatricia(node, putNode(node.left, leaf, preferExisting), node.right)
    } else {
        rebuildPatricia(node, node.left, putNode(node.right, leaf, preferExisting))
    }
}

private fun <K, V> unionCombinedNodes(
    left: PatriciaNode<K, V>?,
    right: PatriciaNode<K, V>?,
    combine: (K, V, V) -> V,
): PatriciaNode<K, V>? {
    if (left == null) return right
    if (right == null) return left
    if (left is PatriciaLeaf) return unionLeftLeaf(left, right, combine)
    if (right is PatriciaLeaf) return unionRightLeaf(left, right, combine)
    left as PatriciaBranch
    right as PatriciaBranch
    return when {
        left.mask == right.mask && left.prefix == right.prefix -> rebuildPatricia(
            left,
            unionCombinedNodes(left.left, right.left, combine)!!,
            unionCombinedNodes(left.right, right.right, combine)!!,
        )
        left.mask > right.mask && prefixOfPatricia(right.prefix, left.mask) == left.prefix ->
            if (right.prefix and left.mask == 0uL) {
                rebuildPatricia(left, unionCombinedNodes(left.left, right, combine)!!, left.right)
            } else {
                rebuildPatricia(left, left.left, unionCombinedNodes(left.right, right, combine)!!)
            }
        right.mask > left.mask && prefixOfPatricia(left.prefix, right.mask) == right.prefix ->
            if (left.prefix and right.mask == 0uL) {
                rebuildPatricia(right, unionCombinedNodes(left, right.left, combine)!!, right.right)
            } else {
                rebuildPatricia(right, right.left, unionCombinedNodes(left, right.right, combine)!!)
            }
        else -> joinPatricia(left.prefix, left, right.prefix, right)
    }
}

private fun <K, V> unionLeftLeaf(
    left: PatriciaLeaf<K, V>,
    right: PatriciaNode<K, V>,
    combine: (K, V, V) -> V,
): PatriciaNode<K, V> = when (right) {
    is PatriciaLeaf -> if (left.path == right.path) combinedLeaf(left, right, combine)
    else joinPatricia(left.path, left, right.path, right)
    is PatriciaBranch -> if (prefixOfPatricia(left.path, right.mask) != right.prefix) {
        joinPatricia(left.path, left, right.prefix, right)
    } else if (left.path and right.mask == 0uL) {
        rebuildPatricia(right, unionLeftLeaf(left, right.left, combine), right.right)
    } else {
        rebuildPatricia(right, right.left, unionLeftLeaf(left, right.right, combine))
    }
}

private fun <K, V> unionRightLeaf(
    left: PatriciaNode<K, V>,
    right: PatriciaLeaf<K, V>,
    combine: (K, V, V) -> V,
): PatriciaNode<K, V> = when (left) {
    is PatriciaLeaf -> if (left.path == right.path) combinedLeaf(left, right, combine)
    else joinPatricia(left.path, left, right.path, right)
    is PatriciaBranch -> if (prefixOfPatricia(right.path, left.mask) != left.prefix) {
        joinPatricia(left.prefix, left, right.path, right)
    } else if (right.path and left.mask == 0uL) {
        rebuildPatricia(left, unionRightLeaf(left.left, right, combine), left.right)
    } else {
        rebuildPatricia(left, left.left, unionRightLeaf(left.right, right, combine))
    }
}

private fun <K, V> combinedLeaf(
    left: PatriciaLeaf<K, V>,
    right: PatriciaLeaf<K, V>,
    combine: (K, V, V) -> V,
): PatriciaLeaf<K, V> {
    val value = combine(left.key, left.value, right.value)
    return when {
        patriciaValuesEqual(value, left.value) -> left
        patriciaValuesEqual(value, right.value) -> right
        else -> PatriciaLeaf(left.path, left.key, value)
    }
}

private fun <K, V, W> intersectNodes(left: PatriciaNode<K, V>?, right: PatriciaNode<K, W>?): PatriciaNode<K, V>? {
    if (left == null || right == null) return null
    if (left === right) return left
    if (left is PatriciaLeaf) return if (containsPath(right, left.path)) left else null
    if (right is PatriciaLeaf) return findLeaf(left, right.path)
    left as PatriciaBranch
    right as PatriciaBranch
    return when {
        left.mask == right.mask && left.prefix == right.prefix -> collapsePatricia(
            left, intersectNodes(left.left, right.left), intersectNodes(left.right, right.right),
        )
        left.mask > right.mask && prefixOfPatricia(right.prefix, left.mask) == left.prefix ->
            if (right.prefix and left.mask == 0uL) intersectNodes(left.left, right) else intersectNodes(left.right, right)
        right.mask > left.mask && prefixOfPatricia(left.prefix, right.mask) == right.prefix ->
            if (left.prefix and right.mask == 0uL) intersectNodes(left, right.left) else intersectNodes(left, right.right)
        else -> null
    }
}

private fun <K, V> intersectCombinedNodes(
    left: PatriciaNode<K, V>?,
    right: PatriciaNode<K, V>?,
    combine: (K, V, V) -> V,
): PatriciaNode<K, V>? {
    if (left == null || right == null) return null
    if (left is PatriciaLeaf) {
        val rightLeaf = findLeaf(right, left.path) ?: return null
        return combinedLeaf(left, rightLeaf, combine)
    }
    if (right is PatriciaLeaf) {
        val leftLeaf = findLeaf(left, right.path) ?: return null
        return combinedLeaf(leftLeaf, right, combine)
    }
    left as PatriciaBranch
    right as PatriciaBranch
    return when {
        left.mask == right.mask && left.prefix == right.prefix -> collapsePatricia(
            left,
            intersectCombinedNodes(left.left, right.left, combine),
            intersectCombinedNodes(left.right, right.right, combine),
        )
        left.mask > right.mask && prefixOfPatricia(right.prefix, left.mask) == left.prefix ->
            if (right.prefix and left.mask == 0uL) {
                intersectCombinedNodes(left.left, right, combine)
            } else {
                intersectCombinedNodes(left.right, right, combine)
            }
        right.mask > left.mask && prefixOfPatricia(left.prefix, right.mask) == right.prefix ->
            if (left.prefix and right.mask == 0uL) {
                intersectCombinedNodes(left, right.left, combine)
            } else {
                intersectCombinedNodes(left, right.right, combine)
            }
        else -> null
    }
}

private fun <K, V, W> exceptNodes(left: PatriciaNode<K, V>?, right: PatriciaNode<K, W>?): PatriciaNode<K, V>? {
    if (left == null || right == null) return left
    if (left === right) return null
    if (left is PatriciaLeaf) return if (containsPath(right, left.path)) null else left
    if (right is PatriciaLeaf) return removePath(left, right.path)
    left as PatriciaBranch
    right as PatriciaBranch
    return when {
        left.mask == right.mask && left.prefix == right.prefix -> collapsePatricia(
            left, exceptNodes(left.left, right.left), exceptNodes(left.right, right.right),
        )
        left.mask > right.mask && prefixOfPatricia(right.prefix, left.mask) == left.prefix ->
            if (right.prefix and left.mask == 0uL) collapsePatricia(left, exceptNodes(left.left, right), left.right)
            else collapsePatricia(left, left.left, exceptNodes(left.right, right))
        right.mask > left.mask && prefixOfPatricia(left.prefix, right.mask) == right.prefix ->
            if (left.prefix and right.mask == 0uL) exceptNodes(left, right.left) else exceptNodes(left, right.right)
        else -> left
    }
}

private fun <K, V> rebuildPatricia(
    original: PatriciaBranch<K, V>,
    left: PatriciaNode<K, V>,
    right: PatriciaNode<K, V>,
): PatriciaNode<K, V> = if (left === original.left && right === original.right) original
else PatriciaBranch(original.prefix, original.mask, left, right)

private fun <K, V> collapsePatricia(
    original: PatriciaBranch<K, V>,
    left: PatriciaNode<K, V>?,
    right: PatriciaNode<K, V>?,
): PatriciaNode<K, V>? = when {
    left == null -> right
    right == null -> left
    else -> rebuildPatricia(original, left, right)
}

private fun <K, V> containsPath(node: PatriciaNode<K, V>, path: ULong): Boolean = findLeaf(node, path) != null
private fun <K, V> findLeaf(node: PatriciaNode<K, V>, path: ULong): PatriciaLeaf<K, V>? = when (node) {
    is PatriciaLeaf -> if (node.path == path) node else null
    is PatriciaBranch -> if (prefixOfPatricia(path, node.mask) != node.prefix) null
    else findLeaf(if (path and node.mask == 0uL) node.left else node.right, path)
}
private fun <K, V> removePath(node: PatriciaNode<K, V>, path: ULong): PatriciaNode<K, V>? = when (node) {
    is PatriciaLeaf -> if (node.path == path) null else node
    is PatriciaBranch -> if (prefixOfPatricia(path, node.mask) != node.prefix) node
    else if (path and node.mask == 0uL) collapsePatricia(node, removePath(node.left, path), node.right)
    else collapsePatricia(node, node.left, removePath(node.right, path))
}
private fun prefixOfPatricia(path: ULong, mask: ULong): ULong = path and ((mask shl 1) - 1uL).inv()
private fun <K, V> joinPatricia(
    leftPath: ULong, left: PatriciaNode<K, V>, rightPath: ULong, right: PatriciaNode<K, V>,
): PatriciaNode<K, V> {
    val difference = leftPath xor rightPath
    val mask = 1uL shl (63 - difference.countLeadingZeroBits())
    val prefix = prefixOfPatricia(leftPath, mask)
    return if (leftPath and mask == 0uL) PatriciaBranch(prefix, mask, left, right)
    else PatriciaBranch(prefix, mask, right, left)
}

/** A presence-safe map cursor entry whose [value] may itself be null. */
public data class PatriciaMapEntry<K, V>(public val key: K, public val value: V)

/** Exact-search result whose [cursor] remains usable on a miss. */
public data class PatriciaCursorSearch<C>(public val cursor: C, public val found: Boolean)

/** A persistent Patricia map over signed 32-bit keys, bounded by key width rather than entry count. */
public class PersistentIntMap<V> private constructor(private val core: PatriciaCore<Int, V>) : Iterable<Pair<Int, V>> {
    public companion object {
        public fun <V> empty(): PersistentIntMap<V> = PersistentIntMap(PatriciaCore(null, 0) { (it xor Int.MIN_VALUE).toUInt().toULong() })
        public fun <V> from(items: Iterable<Pair<Int, V>>): PersistentIntMap<V> {
            var result = empty<V>(); for ((key, value) in items) result = result.put(key, value); return result
        }
    }

    /** Number of entries. */
    public val size: Int get() = core.size

    /** Whether the map holds no entries. */
    public val isEmpty: Boolean get() = size == 0

    /** The value stored for the key, or `null` when absent. */
    public operator fun get(key: Int): V? = core.get(key)

    /** Whether the key is present. */
    public fun containsKey(key: Int): Boolean = core.containsKey(key)

    /** A cursor before the first entry. */
    public fun cursor(): PersistentIntMapCursor<V> = PersistentIntMapCursor.create(this, 0)

    /** A cursor at the given gap of the map. */
    public fun cursorAt(position: Int): PersistentIntMapCursor<V>? =
        if (position < 0 || position > size) null else PersistentIntMapCursor.create(this, position)

    /** A cursor after the last entry. */
    public fun cursorAtEnd(): PersistentIntMapCursor<V> = PersistentIntMapCursor.create(this, size)

    /** A cursor before the first key not less than the probe. */
    public fun lowerBoundCursor(key: Int): PersistentIntMapCursor<V> =
        PersistentIntMapCursor.create(this, core.lowerBoundRank(key).first)

    /** A cursor after any key equal to the probe. */
    public fun upperBoundCursor(key: Int): PersistentIntMapCursor<V> {
        val (position, found) = core.lowerBoundRank(key)
        return PersistentIntMapCursor.create(this, position + if (found) 1 else 0)
    }

    /** A cursor at the key together with an exact-match discriminator; on a miss it sits at the insertion point. */
    public fun cursorAtKey(key: Int): PatriciaCursorSearch<PersistentIntMapCursor<V>> {
        val (position, found) = core.lowerBoundRank(key)
        return PatriciaCursorSearch(PersistentIntMapCursor.create(this, position), found)
    }

    /** A map with the key bound to the value, adding or replacing as needed. */
    public fun put(key: Int, value: V): PersistentIntMap<V> { val next = core.put(key, value); return if (next === core) this else PersistentIntMap(next) }

    /** A map without that entry; returns the receiver when absent. */
    public fun remove(key: Int): PersistentIntMap<V> { val next = core.remove(key); return if (next === core) this else PersistentIntMap(next) }

    /** An empty map retaining the same policies; returns the receiver when already empty. */
    public fun clear(): PersistentIntMap<V> = if (isEmpty) this else empty()

    /** The entries of both maps. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentIntMap<V>): PersistentIntMap<V> { val next = core.union(other.core); return if (next === core) this else PersistentIntMap(next) }

    /** The entries of both maps. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentIntMap<V>, combine: (Int, V, V) -> V): PersistentIntMap<V> { val next = core.union(other.core, combine); return if (next === core) this else PersistentIntMap(next) }

    /** The entries present in both maps. */
    public fun intersect(other: PersistentIntMap<V>): PersistentIntMap<V> { val next = core.intersect(other.core); return if (next === core) this else PersistentIntMap(next) }

    /** The entries present in both maps. */
    public fun intersect(other: PersistentIntMap<V>, combine: (Int, V, V) -> V): PersistentIntMap<V> { val next = core.intersect(other.core, combine); return if (next === core) this else PersistentIntMap(next) }

    /** This map's entries that are absent from the other. */
    public fun except(other: PersistentIntMap<*>): PersistentIntMap<V> { val next = core.except(other.core); return if (next === core) this else PersistentIntMap(next) }

    /** Iterate the entries. */
    override fun iterator(): Iterator<Pair<Int, V>> = core.iterator()

    /** The entry at the given rank, used by the cursors to address positions. */
    internal fun entryAtForCursor(index: Int): PatriciaMapEntry<Int, V>? =
        core.entryAt(index)?.let { PatriciaMapEntry(it.first, it.second) }

    /**
     * The rank where the probe belongs together with whether it is actually present; branch prefixes let whole subtrees
     * be skipped, so the descent is bounded by the key width.
     */
    internal fun lowerBoundRankForCursor(key: Int): Pair<Int, Boolean> = core.lowerBoundRank(key)
}

/** A persistent Patricia map over signed 64-bit keys, bounded by key width rather than entry count. */
public class PersistentLongMap<V> private constructor(private val core: PatriciaCore<Long, V>) : Iterable<Pair<Long, V>> {
    public companion object {
        public fun <V> empty(): PersistentLongMap<V> = PersistentLongMap(PatriciaCore(null, 0) { (it xor Long.MIN_VALUE).toULong() })
        public fun <V> from(items: Iterable<Pair<Long, V>>): PersistentLongMap<V> { var result = empty<V>(); for ((k, v) in items) result = result.put(k, v); return result }
    }

    /** Number of entries. */
    public val size: Int get() = core.size

    /** Whether the map holds no entries. */
    public val isEmpty: Boolean get() = size == 0

    /** The value stored for the key, or `null` when absent. */
    public operator fun get(key: Long): V? = core.get(key)

    /** Whether the key is present. */
    public fun containsKey(key: Long): Boolean = core.containsKey(key)

    /** A cursor before the first entry. */
    public fun cursor(): PersistentLongMapCursor<V> = PersistentLongMapCursor.create(this, 0)

    /** A cursor at the given gap of the map. */
    public fun cursorAt(position: Int): PersistentLongMapCursor<V>? =
        if (position < 0 || position > size) null else PersistentLongMapCursor.create(this, position)

    /** A cursor after the last entry. */
    public fun cursorAtEnd(): PersistentLongMapCursor<V> = PersistentLongMapCursor.create(this, size)

    /** A cursor before the first key not less than the probe. */
    public fun lowerBoundCursor(key: Long): PersistentLongMapCursor<V> =
        PersistentLongMapCursor.create(this, core.lowerBoundRank(key).first)

    /** A cursor after any key equal to the probe. */
    public fun upperBoundCursor(key: Long): PersistentLongMapCursor<V> {
        val (position, found) = core.lowerBoundRank(key)
        return PersistentLongMapCursor.create(this, position + if (found) 1 else 0)
    }

    /** A cursor at the key together with an exact-match discriminator; on a miss it sits at the insertion point. */
    public fun cursorAtKey(key: Long): PatriciaCursorSearch<PersistentLongMapCursor<V>> {
        val (position, found) = core.lowerBoundRank(key)
        return PatriciaCursorSearch(PersistentLongMapCursor.create(this, position), found)
    }

    /** A map with the key bound to the value, adding or replacing as needed. */
    public fun put(key: Long, value: V): PersistentLongMap<V> { val next = core.put(key, value); return if (next === core) this else PersistentLongMap(next) }

    /** A map without that entry; returns the receiver when absent. */
    public fun remove(key: Long): PersistentLongMap<V> { val next = core.remove(key); return if (next === core) this else PersistentLongMap(next) }

    /** An empty map retaining the same policies; returns the receiver when already empty. */
    public fun clear(): PersistentLongMap<V> = if (isEmpty) this else empty()

    /** The entries of both maps. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentLongMap<V>): PersistentLongMap<V> { val next = core.union(other.core); return if (next === core) this else PersistentLongMap(next) }

    /** The entries of both maps. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentLongMap<V>, combine: (Long, V, V) -> V): PersistentLongMap<V> { val next = core.union(other.core, combine); return if (next === core) this else PersistentLongMap(next) }

    /** The entries present in both maps. */
    public fun intersect(other: PersistentLongMap<V>): PersistentLongMap<V> { val next = core.intersect(other.core); return if (next === core) this else PersistentLongMap(next) }

    /** The entries present in both maps. */
    public fun intersect(other: PersistentLongMap<V>, combine: (Long, V, V) -> V): PersistentLongMap<V> { val next = core.intersect(other.core, combine); return if (next === core) this else PersistentLongMap(next) }

    /** This map's entries that are absent from the other. */
    public fun except(other: PersistentLongMap<*>): PersistentLongMap<V> { val next = core.except(other.core); return if (next === core) this else PersistentLongMap(next) }

    /** Iterate the entries. */
    override fun iterator(): Iterator<Pair<Long, V>> = core.iterator()

    /** The entry at the given rank, used by the cursors to address positions. */
    internal fun entryAtForCursor(index: Int): PatriciaMapEntry<Long, V>? =
        core.entryAt(index)?.let { PatriciaMapEntry(it.first, it.second) }

    /**
     * The rank where the probe belongs together with whether it is actually present; branch prefixes let whole subtrees
     * be skipped, so the descent is bounded by the key width.
     */
    internal fun lowerBoundRankForCursor(key: Long): Pair<Int, Boolean> = core.lowerBoundRank(key)
}

/** Immutable root-plus-rank gap cursor over a signed 32-bit Patricia map. */
public class PersistentIntMapCursor<V> private constructor(
    private val map: PersistentIntMap<V>,
    public val position: Int,
) {
    internal companion object {
        internal fun <V> create(map: PersistentIntMap<V>, position: Int): PersistentIntMapCursor<V> =
            PersistentIntMapCursor(map, position)
    }

    init {
        require(position >= 0 && position <= map.size) { "Cursor position must be in 0..map.size." }
    }

    public val size: Int get() = map.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size
    public fun peekPrevious(): PatriciaMapEntry<Int, V>? =
        if (isAtStart) null else map.entryAtForCursor(position - 1)
    public fun peekNext(): PatriciaMapEntry<Int, V>? = map.entryAtForCursor(position)
    public fun movePrevious(): PersistentIntMapCursor<V>? =
        if (isAtStart) null else create(map, position - 1)
    public fun moveNext(): PersistentIntMapCursor<V>? =
        if (isAtEnd) null else create(map, position + 1)
    public fun seek(position: Int): PersistentIntMapCursor<V>? = when {
        position < 0 || position > size -> null
        position == this.position -> this
        else -> create(map, position)
    }
    public fun insert(key: Int, value: V): PersistentIntMapCursor<V> {
        val (expected, found) = map.lowerBoundRankForCursor(key)
        require(!found) { "The key '$key' is already present." }
        ensureCurrentGap(expected, key)
        return create(map.put(key, value), Math.addExact(position, 1))
    }
    public fun put(key: Int, value: V): PersistentIntMapCursor<V> {
        val (expected, found) = map.lowerBoundRankForCursor(key)
        ensureCurrentGap(expected, key)
        val edited = map.put(key, value)
        if (edited === map) return this
        return create(edited, if (found) position else Math.addExact(position, 1))
    }
    public fun setNextValue(value: V): PersistentIntMapCursor<V>? {
        val next = peekNext() ?: return null
        val edited = map.put(next.key, value)
        return if (edited === map) this else create(edited, position)
    }
    public fun deletePrevious(): PersistentIntMapCursor<V>? {
        val previous = peekPrevious() ?: return null
        return create(map.remove(previous.key), position - 1)
    }
    public fun deleteNext(): PersistentIntMapCursor<V>? {
        val next = peekNext() ?: return null
        return create(map.remove(next.key), position)
    }
    public fun snapshot(): PersistentIntMap<V> = map

    private fun ensureCurrentGap(expected: Int, key: Int) {
        require(expected == position) {
            "Key '$key' belongs at gap $expected, not at the current gap $position."
        }
    }
}

/** Immutable root-plus-rank gap cursor over a signed 64-bit Patricia map. */
public class PersistentLongMapCursor<V> private constructor(
    private val map: PersistentLongMap<V>,
    public val position: Int,
) {
    internal companion object {
        internal fun <V> create(map: PersistentLongMap<V>, position: Int): PersistentLongMapCursor<V> =
            PersistentLongMapCursor(map, position)
    }

    init {
        require(position >= 0 && position <= map.size) { "Cursor position must be in 0..map.size." }
    }

    public val size: Int get() = map.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size
    public fun peekPrevious(): PatriciaMapEntry<Long, V>? =
        if (isAtStart) null else map.entryAtForCursor(position - 1)
    public fun peekNext(): PatriciaMapEntry<Long, V>? = map.entryAtForCursor(position)
    public fun movePrevious(): PersistentLongMapCursor<V>? =
        if (isAtStart) null else create(map, position - 1)
    public fun moveNext(): PersistentLongMapCursor<V>? =
        if (isAtEnd) null else create(map, position + 1)
    public fun seek(position: Int): PersistentLongMapCursor<V>? = when {
        position < 0 || position > size -> null
        position == this.position -> this
        else -> create(map, position)
    }
    public fun insert(key: Long, value: V): PersistentLongMapCursor<V> {
        val (expected, found) = map.lowerBoundRankForCursor(key)
        require(!found) { "The key '$key' is already present." }
        ensureCurrentGap(expected, key)
        return create(map.put(key, value), Math.addExact(position, 1))
    }
    public fun put(key: Long, value: V): PersistentLongMapCursor<V> {
        val (expected, found) = map.lowerBoundRankForCursor(key)
        ensureCurrentGap(expected, key)
        val edited = map.put(key, value)
        if (edited === map) return this
        return create(edited, if (found) position else Math.addExact(position, 1))
    }
    public fun setNextValue(value: V): PersistentLongMapCursor<V>? {
        val next = peekNext() ?: return null
        val edited = map.put(next.key, value)
        return if (edited === map) this else create(edited, position)
    }
    public fun deletePrevious(): PersistentLongMapCursor<V>? {
        val previous = peekPrevious() ?: return null
        return create(map.remove(previous.key), position - 1)
    }
    public fun deleteNext(): PersistentLongMapCursor<V>? {
        val next = peekNext() ?: return null
        return create(map.remove(next.key), position)
    }
    public fun snapshot(): PersistentLongMap<V> = map

    private fun ensureCurrentGap(expected: Int, key: Long) {
        require(expected == position) {
            "Key '$key' belongs at gap $expected, not at the current gap $position."
        }
    }
}

/** A persistent Patricia set over signed 32-bit keys. */
public class PersistentIntSet private constructor(private val map: PersistentIntMap<Unit>) : Iterable<Int> {
    public companion object { public fun empty(): PersistentIntSet = PersistentIntSet(PersistentIntMap.empty()); public fun from(items: Iterable<Int>): PersistentIntSet { var r = empty(); for (v in items) r = r.add(v); return r } }

    /** Number of elements. */
    public val size: Int get() = map.size

    /** Whether the element is present. */
    public fun contains(value: Int): Boolean = map.containsKey(value)

    /** A cursor before the first element. */
    public fun cursor(): PersistentIntSetCursor = PersistentIntSetCursor.create(this, 0)

    /** A cursor at the given gap of the set. */
    public fun cursorAt(position: Int): PersistentIntSetCursor? =
        if (position < 0 || position > size) null else PersistentIntSetCursor.create(this, position)

    /** A cursor after the last element. */
    public fun cursorAtEnd(): PersistentIntSetCursor = PersistentIntSetCursor.create(this, size)

    /** A cursor before the first key not less than the probe. */
    public fun lowerBoundCursor(value: Int): PersistentIntSetCursor =
        PersistentIntSetCursor.create(this, map.lowerBoundRankForCursor(value).first)

    /** A cursor after any key equal to the probe. */
    public fun upperBoundCursor(value: Int): PersistentIntSetCursor {
        val (position, found) = map.lowerBoundRankForCursor(value)
        return PersistentIntSetCursor.create(this, position + if (found) 1 else 0)
    }

    /** A cursor at the element together with an exact-match discriminator. */
    public fun cursorAtItem(value: Int): PatriciaCursorSearch<PersistentIntSetCursor> {
        val (position, found) = map.lowerBoundRankForCursor(value)
        return PatriciaCursorSearch(PersistentIntSetCursor.create(this, position), found)
    }

    /** A set containing the given element; returns the receiver when already present. */
    public fun add(value: Int): PersistentIntSet = withMap(map.put(value, Unit))

    /** A set without that element; returns the receiver when absent. */
    public fun remove(value: Int): PersistentIntSet = withMap(map.remove(value))

    /** The elements of both sets. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentIntSet): PersistentIntSet = withMap(map.union(other.map))

    /** The elements present in both sets. */
    public fun intersect(other: PersistentIntSet): PersistentIntSet = withMap(map.intersect(other.map))

    /** This set's elements that are absent from the other. */
    public fun except(other: PersistentIntSet): PersistentIntSet = withMap(map.except(other.map))

    /** Iterate the elements. */
    override fun iterator(): Iterator<Int> = map.map { it.first }.iterator()

    /** The element at the given rank, used by the cursors to address positions. */
    internal fun itemAtForCursor(index: Int): Int? = map.entryAtForCursor(index)?.key

    /**
     * The rank where the probe belongs together with whether it is actually present; branch prefixes let whole subtrees
     * be skipped, so the descent is bounded by the key width.
     */
    internal fun lowerBoundRankForCursor(value: Int): Pair<Int, Boolean> = map.lowerBoundRankForCursor(value)
    private fun withMap(next: PersistentIntMap<Unit>): PersistentIntSet = if (next === map) this else PersistentIntSet(next)
}

/** A persistent Patricia set over signed 64-bit keys. */
public class PersistentLongSet private constructor(private val map: PersistentLongMap<Unit>) : Iterable<Long> {
    public companion object { public fun empty(): PersistentLongSet = PersistentLongSet(PersistentLongMap.empty()); public fun from(items: Iterable<Long>): PersistentLongSet { var r = empty(); for (v in items) r = r.add(v); return r } }

    /** Number of elements. */
    public val size: Int get() = map.size

    /** Whether the element is present. */
    public fun contains(value: Long): Boolean = map.containsKey(value)

    /** A cursor before the first element. */
    public fun cursor(): PersistentLongSetCursor = PersistentLongSetCursor.create(this, 0)

    /** A cursor at the given gap of the set. */
    public fun cursorAt(position: Int): PersistentLongSetCursor? =
        if (position < 0 || position > size) null else PersistentLongSetCursor.create(this, position)

    /** A cursor after the last element. */
    public fun cursorAtEnd(): PersistentLongSetCursor = PersistentLongSetCursor.create(this, size)

    /** A cursor before the first key not less than the probe. */
    public fun lowerBoundCursor(value: Long): PersistentLongSetCursor =
        PersistentLongSetCursor.create(this, map.lowerBoundRankForCursor(value).first)

    /** A cursor after any key equal to the probe. */
    public fun upperBoundCursor(value: Long): PersistentLongSetCursor {
        val (position, found) = map.lowerBoundRankForCursor(value)
        return PersistentLongSetCursor.create(this, position + if (found) 1 else 0)
    }

    /** A cursor at the element together with an exact-match discriminator. */
    public fun cursorAtItem(value: Long): PatriciaCursorSearch<PersistentLongSetCursor> {
        val (position, found) = map.lowerBoundRankForCursor(value)
        return PatriciaCursorSearch(PersistentLongSetCursor.create(this, position), found)
    }

    /** A set containing the given element; returns the receiver when already present. */
    public fun add(value: Long): PersistentLongSet = withMap(map.put(value, Unit))

    /** A set without that element; returns the receiver when absent. */
    public fun remove(value: Long): PersistentLongSet = withMap(map.remove(value))

    /** The elements of both sets. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentLongSet): PersistentLongSet = withMap(map.union(other.map))

    /** The elements present in both sets. */
    public fun intersect(other: PersistentLongSet): PersistentLongSet = withMap(map.intersect(other.map))

    /** This set's elements that are absent from the other. */
    public fun except(other: PersistentLongSet): PersistentLongSet = withMap(map.except(other.map))

    /** Iterate the elements. */
    override fun iterator(): Iterator<Long> = map.map { it.first }.iterator()

    /** The element at the given rank, used by the cursors to address positions. */
    internal fun itemAtForCursor(index: Int): Long? = map.entryAtForCursor(index)?.key

    /**
     * The rank where the probe belongs together with whether it is actually present; branch prefixes let whole subtrees
     * be skipped, so the descent is bounded by the key width.
     */
    internal fun lowerBoundRankForCursor(value: Long): Pair<Int, Boolean> = map.lowerBoundRankForCursor(value)
    private fun withMap(next: PersistentLongMap<Unit>): PersistentLongSet = if (next === map) this else PersistentLongSet(next)
}

/** Immutable root-plus-rank gap cursor over a signed 32-bit Patricia set. */
public class PersistentIntSetCursor private constructor(
    private val set: PersistentIntSet,
    public val position: Int,
) {
    internal companion object {
        internal fun create(set: PersistentIntSet, position: Int): PersistentIntSetCursor =
            PersistentIntSetCursor(set, position)
    }

    init {
        require(position >= 0 && position <= set.size) { "Cursor position must be in 0..set.size." }
    }

    public val size: Int get() = set.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size
    public fun peekPrevious(): Int? = if (isAtStart) null else set.itemAtForCursor(position - 1)
    public fun peekNext(): Int? = set.itemAtForCursor(position)
    public fun movePrevious(): PersistentIntSetCursor? = if (isAtStart) null else create(set, position - 1)
    public fun moveNext(): PersistentIntSetCursor? = if (isAtEnd) null else create(set, position + 1)
    public fun seek(position: Int): PersistentIntSetCursor? = when {
        position < 0 || position > size -> null
        position == this.position -> this
        else -> create(set, position)
    }
    public fun add(value: Int): PersistentIntSetCursor {
        val (expected, found) = set.lowerBoundRankForCursor(value)
        ensureCurrentGap(expected, value)
        return if (found) this else create(set.add(value), Math.addExact(position, 1))
    }
    public fun deletePrevious(): PersistentIntSetCursor? {
        val previous = peekPrevious() ?: return null
        return create(set.remove(previous), position - 1)
    }
    public fun deleteNext(): PersistentIntSetCursor? {
        val next = peekNext() ?: return null
        return create(set.remove(next), position)
    }
    public fun snapshot(): PersistentIntSet = set

    private fun ensureCurrentGap(expected: Int, value: Int) {
        require(expected == position) {
            "Item '$value' belongs at gap $expected, not at the current gap $position."
        }
    }
}

/** Immutable root-plus-rank gap cursor over a signed 64-bit Patricia set. */
public class PersistentLongSetCursor private constructor(
    private val set: PersistentLongSet,
    public val position: Int,
) {
    internal companion object {
        internal fun create(set: PersistentLongSet, position: Int): PersistentLongSetCursor =
            PersistentLongSetCursor(set, position)
    }

    init {
        require(position >= 0 && position <= set.size) { "Cursor position must be in 0..set.size." }
    }

    public val size: Int get() = set.size
    public val isAtStart: Boolean get() = position == 0
    public val isAtEnd: Boolean get() = position == size
    public fun peekPrevious(): Long? = if (isAtStart) null else set.itemAtForCursor(position - 1)
    public fun peekNext(): Long? = set.itemAtForCursor(position)
    public fun movePrevious(): PersistentLongSetCursor? = if (isAtStart) null else create(set, position - 1)
    public fun moveNext(): PersistentLongSetCursor? = if (isAtEnd) null else create(set, position + 1)
    public fun seek(position: Int): PersistentLongSetCursor? = when {
        position < 0 || position > size -> null
        position == this.position -> this
        else -> create(set, position)
    }
    public fun add(value: Long): PersistentLongSetCursor {
        val (expected, found) = set.lowerBoundRankForCursor(value)
        ensureCurrentGap(expected, value)
        return if (found) this else create(set.add(value), Math.addExact(position, 1))
    }
    public fun deletePrevious(): PersistentLongSetCursor? {
        val previous = peekPrevious() ?: return null
        return create(set.remove(previous), position - 1)
    }
    public fun deleteNext(): PersistentLongSetCursor? {
        val next = peekNext() ?: return null
        return create(set.remove(next), position)
    }
    public fun snapshot(): PersistentLongSet = set

    private fun ensureCurrentGap(expected: Int, value: Long) {
        require(expected == position) {
            "Item '$value' belongs at gap $expected, not at the current gap $position."
        }
    }
}
