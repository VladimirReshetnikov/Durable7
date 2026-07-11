package tools.datastructures.hamt

public interface HashPolicy<K> {
    public fun hash(key: K): Int
    public fun equivalent(left: K, right: K): Boolean
}

private object DefaultHashPolicy : HashPolicy<Any?> {
    override fun hash(key: Any?): Int = key?.hashCode() ?: 0
    override fun equivalent(left: Any?, right: Any?): Boolean = left == right
}

@Suppress("UNCHECKED_CAST")
public fun <K> defaultHashPolicy(): HashPolicy<K> = DefaultHashPolicy as HashPolicy<K>

public class DuplicateKeyException(message: String = "An equivalent key is already present.") :
    IllegalArgumentException(message)

public data class HamtEntry<K, V>(public val key: K, public val value: V)
public data class AddResult<T>(public val value: T, public val added: Boolean)
public data class MapRemoveResult<K, V>(public val map: PersistentHashMap<K, V>, public val value: V)
public data class MapRemoveEntryResult<K, V>(
    public val map: PersistentHashMap<K, V>,
    public val entry: HamtEntry<K, V>,
)
public data class SetRemoveResult<T>(public val set: PersistentHashSet<T>, public val value: T)

public enum class MapDifferenceKind { ADDED, REMOVED, CHANGED }
public data class MapDifference<K, V>(
    public val kind: MapDifferenceKind,
    public val key: K,
    public val before: V?,
    public val after: V?,
)

internal data class ChampStatistics(
    val inlinePayloads: Int,
    val bitmapNodes: Int,
    val collisionPayloads: Int,
    val invalidLeafChildren: Int,
)

private const val BitsPerLevel: Int = 5
private const val BranchMask: Int = 0x1f

private sealed interface Node<K, V>
private data class Leaf<K, V>(val hash: Int, val key: K, val value: V) : Node<K, V>
private data class Collision<K, V>(val hash: Int, val entries: List<Leaf<K, V>>) : Node<K, V>

/** CHAMP sparse node: payloads and child nodes occupy independent compact runs. */
private data class BitmapNode<K, V>(
    val dataMap: Int,
    val nodeMap: Int,
    val data: List<Leaf<K, V>>,
    val nodes: List<Node<K, V>>,
) : Node<K, V>

private data class InsertResult<K, V>(
    val node: Node<K, V>,
    val added: Boolean,
    val changed: Boolean,
    val duplicate: Boolean,
)
private data class RemoveResult<K, V>(
    val node: Node<K, V>?,
    val removed: HamtEntry<K, V>?,
    val changed: Boolean,
)

public class PersistentHashMap<K, V> private constructor(
    private val root: Node<K, V>?,
    public val size: Int,
    public val policy: HashPolicy<K>,
) : Iterable<HamtEntry<K, V>> {
    public companion object {
        public fun <K, V> empty(policy: HashPolicy<K> = defaultHashPolicy()): PersistentHashMap<K, V> =
            PersistentHashMap(null, 0, policy)

        public fun <K, V> from(
            items: Iterable<Pair<K, V>>,
            policy: HashPolicy<K> = defaultHashPolicy(),
        ): PersistentHashMap<K, V> = empty<K, V>(policy).setItems(items)
    }

    public val isEmpty: Boolean get() = size == 0
    public fun sharesRootWith(other: PersistentHashMap<K, V>): Boolean = root === other.root
    public fun containsKey(key: K): Boolean = getEntry(key) != null
    public operator fun get(key: K): V? = getEntry(key)?.value

    public fun getEntry(key: K): HamtEntry<K, V>? {
        val current = root ?: return null
        return getInNode(current, policy.hash(key), key, 0, policy)
    }

    public fun put(key: K, value: V): PersistentHashMap<K, V> {
        val hash = policy.hash(key)
        val current = root ?: return PersistentHashMap(Leaf(hash, key, value), 1, policy)
        val result = insertNode(current, hash, key, value, 0, overwrite = true, policy)
        if (!result.changed) return this
        return PersistentHashMap(result.node, size + if (result.added) 1 else 0, policy)
    }

    public fun add(key: K, value: V): PersistentHashMap<K, V> {
        val result = tryAdd(key, value)
        if (!result.added) throw DuplicateKeyException()
        return result.value
    }

    public fun tryAdd(key: K, value: V): AddResult<PersistentHashMap<K, V>> {
        val hash = policy.hash(key)
        val current = root
            ?: return AddResult(PersistentHashMap(Leaf(hash, key, value), 1, policy), true)
        val result = insertNode(current, hash, key, value, 0, overwrite = false, policy)
        if (result.duplicate) return AddResult(this, false)
        return AddResult(PersistentHashMap(result.node, size + if (result.added) 1 else 0, policy), result.added)
    }

    public fun setItems(items: Iterable<Pair<K, V>>): PersistentHashMap<K, V> {
        var result = this
        for ((key, value) in items) result = result.put(key, value)
        return result
    }

    public fun remove(key: K): PersistentHashMap<K, V> = tryRemove(key)?.map ?: this
    public fun tryRemove(key: K): MapRemoveResult<K, V>? {
        val entry = tryRemoveEntry(key) ?: return null
        return MapRemoveResult(entry.map, entry.entry.value)
    }

    public fun tryRemoveEntry(key: K): MapRemoveEntryResult<K, V>? {
        val current = root ?: return null
        val result = removeNode(current, policy.hash(key), key, 0, policy)
        if (!result.changed) return null
        return MapRemoveEntryResult(
            PersistentHashMap(result.node, size - 1, policy),
            checkNotNull(result.removed),
        )
    }

    public fun clear(): PersistentHashMap<K, V> = if (isEmpty) this else PersistentHashMap(null, 0, policy)

    internal fun champStatistics(): ChampStatistics = root?.let(::champStatistics) ?: ChampStatistics(0, 0, 0, 0)

    /** Semantic equality for maps retaining the same hash/equality policy object. */
    public fun mapEquals(other: PersistentHashMap<K, V>): Boolean {
        require(policy === other.policy) { "Maps must retain the same hash policy object." }
        return size == other.size && all { entry -> other.getEntry(entry.key)?.value == entry.value }
    }

    /** Reports additions, removals, and value changes between two policy-compatible maps. */
    public fun diff(other: PersistentHashMap<K, V>): Sequence<MapDifference<K, V>> = sequence {
        require(policy === other.policy) { "Maps must retain the same hash policy object." }
        if (root === other.root) return@sequence
        for (entry in this@PersistentHashMap) {
            val after = other.getEntry(entry.key)
            if (after == null) yield(MapDifference(MapDifferenceKind.REMOVED, entry.key, entry.value, null))
            else if (entry.value != after.value) {
                yield(MapDifference(MapDifferenceKind.CHANGED, entry.key, entry.value, after.value))
            }
        }
        for (entry in other) {
            if (getEntry(entry.key) == null) {
                yield(MapDifference(MapDifferenceKind.ADDED, entry.key, null, entry.value))
            }
        }
    }

    public fun entries(): Sequence<HamtEntry<K, V>> = sequence {
        val current = root ?: return@sequence
        yieldEntries(current)
    }
    public fun keys(): Sequence<K> = entries().map { it.key }
    public fun values(): Sequence<V> = entries().map { it.value }
    override fun iterator(): Iterator<HamtEntry<K, V>> = entries().iterator()
}

public class PersistentHashSet<T> private constructor(private val map: PersistentHashMap<T, Unit>) : Iterable<T> {
    public companion object {
        public fun <T> empty(policy: HashPolicy<T> = defaultHashPolicy()): PersistentHashSet<T> =
            PersistentHashSet(PersistentHashMap.empty(policy))
        public fun <T> from(values: Iterable<T>, policy: HashPolicy<T> = defaultHashPolicy()): PersistentHashSet<T> =
            empty<T>(policy).union(values)
    }

    public val size: Int get() = map.size
    public val isEmpty: Boolean get() = map.isEmpty
    public val policy: HashPolicy<T> get() = map.policy
    public fun sharesRootWith(other: PersistentHashSet<T>): Boolean = map.sharesRootWith(other.map)
    public fun contains(value: T): Boolean = map.containsKey(value)
    public fun get(value: T): T? = map.getEntry(value)?.key
    public fun add(value: T): PersistentHashSet<T> = PersistentHashSet(map.add(value, Unit))
    public fun tryAdd(value: T): AddResult<PersistentHashSet<T>> {
        val result = map.tryAdd(value, Unit)
        return AddResult(PersistentHashSet(result.value), result.added)
    }
    public fun put(value: T): PersistentHashSet<T> = PersistentHashSet(map.put(value, Unit))
    public fun remove(value: T): PersistentHashSet<T> = PersistentHashSet(map.remove(value))
    public fun tryRemove(value: T): SetRemoveResult<T>? {
        val removed = map.tryRemoveEntry(value) ?: return null
        return SetRemoveResult(PersistentHashSet(removed.map), removed.entry.key)
    }
    public fun clear(): PersistentHashSet<T> = PersistentHashSet(map.clear())
    public fun union(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) result = result.put(value)
        return result
    }
    public fun intersect(values: Iterable<T>): PersistentHashSet<T> {
        val probe = empty<T>(policy).union(values)
        var result = empty<T>(policy)
        for (value in this) if (probe.contains(value)) result = result.put(value)
        return result
    }
    public fun except(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) result = result.remove(value)
        return result
    }
    public fun symmetricExcept(values: Iterable<T>): PersistentHashSet<T> {
        val distinct = empty<T>(policy).union(values)
        var result = this
        for (value in distinct) result = if (result.contains(value)) result.remove(value) else result.put(value)
        return result
    }
    public fun isSubsetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return all { probe.contains(it) }
    }
    public fun isSupersetOf(values: Iterable<T>): Boolean = values.all { contains(it) }
    public fun isProperSubsetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return size < probe.size && all { probe.contains(it) }
    }
    public fun isProperSupersetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return size > probe.size && probe.all { contains(it) }
    }
    public fun overlaps(values: Iterable<T>): Boolean = values.any { contains(it) }
    public fun setEquals(values: Iterable<T>): Boolean {
        val other = empty<T>(policy).union(values)
        return size == other.size && all { other.contains(it) }
    }
    public fun asSequence(): Sequence<T> = map.keys()
    override fun iterator(): Iterator<T> = asSequence().iterator()
}

private fun <K, V> getInNode(
    node: Node<K, V>, hash: Int, key: K, shift: Int, policy: HashPolicy<K>,
): HamtEntry<K, V>? = when (node) {
    is Leaf -> if (node.hash == hash && policy.equivalent(node.key, key)) HamtEntry(node.key, node.value) else null
    is Collision -> if (node.hash == hash) {
        node.entries.firstOrNull { policy.equivalent(it.key, key) }?.let { HamtEntry(it.key, it.value) }
    } else null
    is BitmapNode -> {
        val bit = bitPosition(hashFragment(hash, shift))
        when {
            node.dataMap and bit != 0 -> {
                val leaf = node.data[sparseIndex(node.dataMap, bit)]
                if (leaf.hash == hash && policy.equivalent(leaf.key, key)) HamtEntry(leaf.key, leaf.value) else null
            }
            node.nodeMap and bit != 0 -> getInNode(
                node.nodes[sparseIndex(node.nodeMap, bit)], hash, key, shift + BitsPerLevel, policy,
            )
            else -> null
        }
    }
}

private fun <K, V> insertNode(
    node: Node<K, V>, hash: Int, key: K, value: V, shift: Int,
    overwrite: Boolean, policy: HashPolicy<K>,
): InsertResult<K, V> = when (node) {
    is Leaf -> insertLeaf(node, hash, key, value, shift, overwrite, policy)
    is Collision -> insertCollision(node, hash, key, value, shift, overwrite, policy)
    is BitmapNode -> insertBitmap(node, hash, key, value, shift, overwrite, policy)
}

private fun <K, V> insertLeaf(
    node: Leaf<K, V>, hash: Int, key: K, value: V, shift: Int,
    overwrite: Boolean, policy: HashPolicy<K>,
): InsertResult<K, V> {
    if (node.hash == hash && policy.equivalent(node.key, key)) {
        if (!overwrite) return InsertResult(node, false, false, true)
        if (node.value == value) return InsertResult(node, false, false, false)
        return InsertResult(Leaf(hash, node.key, value), false, true, false)
    }
    return InsertResult(mergeNodes(node, node.hash, Leaf(hash, key, value), hash, shift), true, true, false)
}

private fun <K, V> insertCollision(
    node: Collision<K, V>, hash: Int, key: K, value: V, shift: Int,
    overwrite: Boolean, policy: HashPolicy<K>,
): InsertResult<K, V> {
    if (node.hash != hash) {
        return InsertResult(mergeNodes(node, node.hash, Leaf(hash, key, value), hash, shift), true, true, false)
    }
    val index = node.entries.indexOfFirst { policy.equivalent(it.key, key) }
    if (index < 0) return InsertResult(Collision(hash, node.entries + Leaf(hash, key, value)), true, true, false)
    if (!overwrite) return InsertResult(node, false, false, true)
    if (node.entries[index].value == value) return InsertResult(node, false, false, false)
    val next = node.entries.toMutableList()
    next[index] = Leaf(hash, next[index].key, value)
    return InsertResult(Collision(hash, next.toList()), false, true, false)
}

private fun <K, V> insertBitmap(
    node: BitmapNode<K, V>, hash: Int, key: K, value: V, shift: Int,
    overwrite: Boolean, policy: HashPolicy<K>,
): InsertResult<K, V> {
    val bit = bitPosition(hashFragment(hash, shift))
    if (node.dataMap and bit != 0) {
        val dataIndex = sparseIndex(node.dataMap, bit)
        val leaf = node.data[dataIndex]
        if (leaf.hash == hash && policy.equivalent(leaf.key, key)) {
            if (!overwrite) return InsertResult(node, false, false, true)
            if (leaf.value == value) return InsertResult(node, false, false, false)
            return InsertResult(node.copy(data = replaced(node.data, dataIndex, Leaf(hash, leaf.key, value))), false, true, false)
        }
        val child = mergeNodes(leaf, leaf.hash, Leaf(hash, key, value), hash, shift + BitsPerLevel)
        return InsertResult(
            BitmapNode(
                node.dataMap and bit.inv(), node.nodeMap or bit,
                removed(node.data, dataIndex),
                inserted(node.nodes, sparseIndex(node.nodeMap, bit), child),
            ), true, true, false,
        )
    }
    if (node.nodeMap and bit != 0) {
        val index = sparseIndex(node.nodeMap, bit)
        val child = insertNode(node.nodes[index], hash, key, value, shift + BitsPerLevel, overwrite, policy)
        if (!child.changed) return InsertResult(node, false, false, child.duplicate)
        return InsertResult(node.copy(nodes = replaced(node.nodes, index, child.node)), child.added, true, false)
    }
    return InsertResult(
        node.copy(dataMap = node.dataMap or bit, data = inserted(node.data, sparseIndex(node.dataMap, bit), Leaf(hash, key, value))),
        true, true, false,
    )
}

private fun <K, V> removeNode(
    node: Node<K, V>, hash: Int, key: K, shift: Int, policy: HashPolicy<K>,
): RemoveResult<K, V> = when (node) {
    is Leaf -> if (node.hash == hash && policy.equivalent(node.key, key)) {
        RemoveResult(null, HamtEntry(node.key, node.value), true)
    } else RemoveResult(node, null, false)
    is Collision -> {
        if (node.hash != hash) RemoveResult(node, null, false)
        else {
            val index = node.entries.indexOfFirst { policy.equivalent(it.key, key) }
            if (index < 0) RemoveResult(node, null, false)
            else {
                val removed = node.entries[index]
                val next = removed(node.entries, index)
                val replacement: Node<K, V>? = when (next.size) {
                    0 -> null
                    1 -> next[0]
                    else -> Collision(hash, next)
                }
                RemoveResult(replacement, HamtEntry(removed.key, removed.value), true)
            }
        }
    }
    is BitmapNode -> removeBitmap(node, hash, key, shift, policy)
}

private fun <K, V> removeBitmap(
    node: BitmapNode<K, V>, hash: Int, key: K, shift: Int, policy: HashPolicy<K>,
): RemoveResult<K, V> {
    val bit = bitPosition(hashFragment(hash, shift))
    if (node.dataMap and bit != 0) {
        val index = sparseIndex(node.dataMap, bit)
        val leaf = node.data[index]
        if (leaf.hash != hash || !policy.equivalent(leaf.key, key)) return RemoveResult(node, null, false)
        return RemoveResult(
            normalize(BitmapNode(node.dataMap and bit.inv(), node.nodeMap, removed(node.data, index), node.nodes)),
            HamtEntry(leaf.key, leaf.value), true,
        )
    }
    if (node.nodeMap and bit == 0) return RemoveResult(node, null, false)
    val index = sparseIndex(node.nodeMap, bit)
    val child = removeNode(node.nodes[index], hash, key, shift + BitsPerLevel, policy)
    if (!child.changed) return RemoveResult(node, null, false)
    val singleton = child.node?.let(::singletonLeaf)
    val replacement = when {
        child.node == null -> BitmapNode(node.dataMap, node.nodeMap and bit.inv(), node.data, removed(node.nodes, index))
        singleton != null -> BitmapNode(
            node.dataMap or bit, node.nodeMap and bit.inv(),
            inserted(node.data, sparseIndex(node.dataMap, bit), singleton), removed(node.nodes, index),
        )
        else -> node.copy(nodes = replaced(node.nodes, index, child.node))
    }
    return RemoveResult(normalize(replacement), child.removed, true)
}

private fun <K, V> mergeNodes(
    left: Node<K, V>, leftHash: Int, right: Node<K, V>, rightHash: Int, shift: Int,
): Node<K, V> {
    if (leftHash == rightHash) return Collision(leftHash, collectLeaves(left) + collectLeaves(right))
    val leftBit = bitPosition(hashFragment(leftHash, shift))
    val rightBit = bitPosition(hashFragment(rightHash, shift))
    if (leftBit == rightBit) {
        return BitmapNode(0, leftBit, emptyList(), listOf(mergeNodes(left, leftHash, right, rightHash, shift + BitsPerLevel)))
    }
    var dataMap = 0
    var nodeMap = 0
    val data = mutableListOf<Pair<Int, Leaf<K, V>>>()
    val nodes = mutableListOf<Pair<Int, Node<K, V>>>()
    fun add(bit: Int, node: Node<K, V>) {
        if (node is Leaf) { dataMap = dataMap or bit; data += bit to node }
        else { nodeMap = nodeMap or bit; nodes += bit to node }
    }
    add(leftBit, left)
    add(rightBit, right)
    return BitmapNode(dataMap, nodeMap, data.sortedBy { it.first.toUInt() }.map { it.second }, nodes.sortedBy { it.first.toUInt() }.map { it.second })
}

private fun <K, V> normalize(node: BitmapNode<K, V>): Node<K, V>? = when {
    node.data.isEmpty() && node.nodes.isEmpty() -> null
    node.data.size == 1 && node.nodes.isEmpty() -> node.data[0]
    node.data.isEmpty() && node.nodes.size == 1 && node.nodes[0] !is BitmapNode -> node.nodes[0]
    else -> node
}

private fun <K, V> singletonLeaf(node: Node<K, V>): Leaf<K, V>? = when (node) {
    is Leaf -> node
    is Collision -> if (node.entries.size == 1) node.entries[0] else null
    is BitmapNode -> if (node.data.size == 1 && node.nodes.isEmpty()) node.data[0] else null
}

private fun <K, V> collectLeaves(node: Node<K, V>): List<Leaf<K, V>> = when (node) {
    is Leaf -> listOf(node)
    is Collision -> node.entries
    is BitmapNode -> node.data + node.nodes.flatMap(::collectLeaves)
}

private fun <K, V> champStatistics(node: Node<K, V>): ChampStatistics = when (node) {
    is Leaf -> ChampStatistics(1, 0, 0, 0)
    is Collision -> ChampStatistics(0, 0, node.entries.size, 0)
    is BitmapNode -> {
        val children = node.nodes.map(::champStatistics)
        ChampStatistics(
            node.data.size + children.sumOf { it.inlinePayloads },
            1 + children.sumOf { it.bitmapNodes },
            children.sumOf { it.collisionPayloads },
            node.nodes.count { it is Leaf } + children.sumOf { it.invalidLeafChildren },
        )
    }
}

private suspend fun <K, V> SequenceScope<HamtEntry<K, V>>.yieldEntries(node: Node<K, V>) {
    when (node) {
        is Leaf -> yield(HamtEntry(node.key, node.value))
        is Collision -> for (leaf in node.entries) yield(HamtEntry(leaf.key, leaf.value))
        is BitmapNode -> {
            for (leaf in node.data) yield(HamtEntry(leaf.key, leaf.value))
            for (child in node.nodes) yieldEntries(child)
        }
    }
}

private fun hashFragment(hash: Int, shift: Int): Int = (hash ushr shift) and BranchMask
private fun bitPosition(fragment: Int): Int = 1 shl fragment
private fun sparseIndex(bitmap: Int, bit: Int): Int = Integer.bitCount(bitmap and (bit - 1))
private fun <T> replaced(values: List<T>, index: Int, value: T): List<T> = values.toMutableList().also { it[index] = value }
private fun <T> inserted(values: List<T>, index: Int, value: T): List<T> = values.toMutableList().also { it.add(index, value) }
private fun <T> removed(values: List<T>, index: Int): List<T> = values.toMutableList().also { it.removeAt(index) }
