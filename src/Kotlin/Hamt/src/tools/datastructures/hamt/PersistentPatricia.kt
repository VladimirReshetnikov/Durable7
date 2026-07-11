package tools.datastructures.hamt

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

    fun put(key: K, value: V): PatriciaCore<K, V> {
        val path = encode(key)
        val change = put(root, path, key, value)
        if (!change.changed) return this
        return PatriciaCore(change.node, size + if (change.added) 1 else 0, encode)
    }

    private fun put(node: PatriciaNode<K, V>?, path: ULong, key: K, value: V): PatriciaChange<K, V> = when (node) {
        null -> PatriciaChange(PatriciaLeaf(path, key, value), true, true)
        is PatriciaLeaf -> when {
            node.path == path && node.value == value -> PatriciaChange(node, false, false)
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
        node.path == leaf.path && node.value == leaf.value -> if (preferExisting) leaf else node
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
        value == left.value -> left
        value == right.value -> right
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

public class PersistentIntMap<V> private constructor(private val core: PatriciaCore<Int, V>) : Iterable<Pair<Int, V>> {
    public companion object {
        public fun <V> empty(): PersistentIntMap<V> = PersistentIntMap(PatriciaCore(null, 0) { (it xor Int.MIN_VALUE).toUInt().toULong() })
        public fun <V> from(items: Iterable<Pair<Int, V>>): PersistentIntMap<V> {
            var result = empty<V>(); for ((key, value) in items) result = result.put(key, value); return result
        }
    }
    public val size: Int get() = core.size
    public val isEmpty: Boolean get() = size == 0
    public operator fun get(key: Int): V? = core.get(key)
    public fun containsKey(key: Int): Boolean = core.containsKey(key)
    public fun put(key: Int, value: V): PersistentIntMap<V> { val next = core.put(key, value); return if (next === core) this else PersistentIntMap(next) }
    public fun remove(key: Int): PersistentIntMap<V> { val next = core.remove(key); return if (next === core) this else PersistentIntMap(next) }
    public fun clear(): PersistentIntMap<V> = if (isEmpty) this else empty()
    public fun union(other: PersistentIntMap<V>): PersistentIntMap<V> { val next = core.union(other.core); return if (next === core) this else PersistentIntMap(next) }
    public fun union(other: PersistentIntMap<V>, combine: (Int, V, V) -> V): PersistentIntMap<V> { val next = core.union(other.core, combine); return if (next === core) this else PersistentIntMap(next) }
    public fun intersect(other: PersistentIntMap<V>): PersistentIntMap<V> { val next = core.intersect(other.core); return if (next === core) this else PersistentIntMap(next) }
    public fun intersect(other: PersistentIntMap<V>, combine: (Int, V, V) -> V): PersistentIntMap<V> { val next = core.intersect(other.core, combine); return if (next === core) this else PersistentIntMap(next) }
    public fun except(other: PersistentIntMap<*>): PersistentIntMap<V> { val next = core.except(other.core); return if (next === core) this else PersistentIntMap(next) }
    override fun iterator(): Iterator<Pair<Int, V>> = core.iterator()
}

public class PersistentLongMap<V> private constructor(private val core: PatriciaCore<Long, V>) : Iterable<Pair<Long, V>> {
    public companion object {
        public fun <V> empty(): PersistentLongMap<V> = PersistentLongMap(PatriciaCore(null, 0) { (it xor Long.MIN_VALUE).toULong() })
        public fun <V> from(items: Iterable<Pair<Long, V>>): PersistentLongMap<V> { var result = empty<V>(); for ((k, v) in items) result = result.put(k, v); return result }
    }
    public val size: Int get() = core.size
    public val isEmpty: Boolean get() = size == 0
    public operator fun get(key: Long): V? = core.get(key)
    public fun containsKey(key: Long): Boolean = core.containsKey(key)
    public fun put(key: Long, value: V): PersistentLongMap<V> { val next = core.put(key, value); return if (next === core) this else PersistentLongMap(next) }
    public fun remove(key: Long): PersistentLongMap<V> { val next = core.remove(key); return if (next === core) this else PersistentLongMap(next) }
    public fun clear(): PersistentLongMap<V> = if (isEmpty) this else empty()
    public fun union(other: PersistentLongMap<V>): PersistentLongMap<V> { val next = core.union(other.core); return if (next === core) this else PersistentLongMap(next) }
    public fun union(other: PersistentLongMap<V>, combine: (Long, V, V) -> V): PersistentLongMap<V> { val next = core.union(other.core, combine); return if (next === core) this else PersistentLongMap(next) }
    public fun intersect(other: PersistentLongMap<V>): PersistentLongMap<V> { val next = core.intersect(other.core); return if (next === core) this else PersistentLongMap(next) }
    public fun intersect(other: PersistentLongMap<V>, combine: (Long, V, V) -> V): PersistentLongMap<V> { val next = core.intersect(other.core, combine); return if (next === core) this else PersistentLongMap(next) }
    public fun except(other: PersistentLongMap<*>): PersistentLongMap<V> { val next = core.except(other.core); return if (next === core) this else PersistentLongMap(next) }
    override fun iterator(): Iterator<Pair<Long, V>> = core.iterator()
}

public class PersistentIntSet private constructor(private val map: PersistentIntMap<Unit>) : Iterable<Int> {
    public companion object { public fun empty(): PersistentIntSet = PersistentIntSet(PersistentIntMap.empty()); public fun from(items: Iterable<Int>): PersistentIntSet { var r = empty(); for (v in items) r = r.add(v); return r } }
    public val size: Int get() = map.size
    public fun contains(value: Int): Boolean = map.containsKey(value)
    public fun add(value: Int): PersistentIntSet = withMap(map.put(value, Unit))
    public fun remove(value: Int): PersistentIntSet = withMap(map.remove(value))
    public fun union(other: PersistentIntSet): PersistentIntSet = withMap(map.union(other.map))
    public fun intersect(other: PersistentIntSet): PersistentIntSet = withMap(map.intersect(other.map))
    public fun except(other: PersistentIntSet): PersistentIntSet = withMap(map.except(other.map))
    override fun iterator(): Iterator<Int> = map.map { it.first }.iterator()
    private fun withMap(next: PersistentIntMap<Unit>): PersistentIntSet = if (next === map) this else PersistentIntSet(next)
}

public class PersistentLongSet private constructor(private val map: PersistentLongMap<Unit>) : Iterable<Long> {
    public companion object { public fun empty(): PersistentLongSet = PersistentLongSet(PersistentLongMap.empty()); public fun from(items: Iterable<Long>): PersistentLongSet { var r = empty(); for (v in items) r = r.add(v); return r } }
    public val size: Int get() = map.size
    public fun contains(value: Long): Boolean = map.containsKey(value)
    public fun add(value: Long): PersistentLongSet = withMap(map.put(value, Unit))
    public fun remove(value: Long): PersistentLongSet = withMap(map.remove(value))
    public fun union(other: PersistentLongSet): PersistentLongSet = withMap(map.union(other.map))
    public fun intersect(other: PersistentLongSet): PersistentLongSet = withMap(map.intersect(other.map))
    public fun except(other: PersistentLongSet): PersistentLongSet = withMap(map.except(other.map))
    override fun iterator(): Iterator<Long> = map.map { it.first }.iterator()
    private fun withMap(next: PersistentLongMap<Unit>): PersistentLongSet = if (next === map) this else PersistentLongSet(next)
}
