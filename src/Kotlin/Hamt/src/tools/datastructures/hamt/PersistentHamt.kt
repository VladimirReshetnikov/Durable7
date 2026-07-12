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
    val underfullBitmapNodes: Int,
)

private const val BitsPerLevel: Int = 5
private const val BranchMask: Int = 0x1f

private sealed interface Node<K, V> { val entryCount: Int }
private data class Leaf<K, V>(val hash: Int, val key: K, val value: V) : Node<K, V> {
    override val entryCount: Int get() = 1
}
private data class Collision<K, V>(val hash: Int, val entries: List<Leaf<K, V>>) : Node<K, V> {
    override val entryCount: Int get() = entries.size
}

/** CHAMP sparse node: payloads and child nodes occupy independent compact runs. */
private data class BitmapNode<K, V>(
    val dataMap: Int,
    val nodeMap: Int,
    val data: List<Leaf<K, V>>,
    val nodes: List<Node<K, V>>,
) : Node<K, V>
{
    override val entryCount: Int = data.size + nodes.sumOf { it.entryCount }
}

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

    public fun union(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> = combine(other, ChampOperation.UNION)
    public fun intersect(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> = combine(other, ChampOperation.INTERSECT)
    public fun except(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> = combine(other, ChampOperation.EXCEPT)
    public fun symmetricExcept(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> =
        combine(other, ChampOperation.SYMMETRIC_EXCEPT)

    private fun combine(other: PersistentHashMap<K, V>, operation: ChampOperation): PersistentHashMap<K, V> {
        require(policy === other.policy) { "Maps must retain the same hash policy object." }
        val combined = combineNodes(root, other.root, 0, operation, policy)
        if (combined === root) return this
        if (combined == null) return PersistentHashMap(null, 0, policy)
        if (combined === other.root) return other
        return PersistentHashMap(combined, combined.entryCount, policy)
    }

    internal fun champStatistics(): ChampStatistics = root?.let(::champStatistics) ?: ChampStatistics(0, 0, 0, 0, 0)
    internal fun champTopology(): String = root?.let(::champTopology) ?: "E"

    /** Semantic equality for maps retaining the same hash/equality policy object. */
    public fun mapEquals(other: PersistentHashMap<K, V>): Boolean {
        require(policy === other.policy) { "Maps must retain the same hash policy object." }
        if (root === other.root) return true
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
    public fun add(value: T): PersistentHashSet<T> = withMap(map.add(value, Unit))
    public fun tryAdd(value: T): AddResult<PersistentHashSet<T>> {
        val result = map.tryAdd(value, Unit)
        return AddResult(withMap(result.value), result.added)
    }
    public fun put(value: T): PersistentHashSet<T> = withMap(map.put(value, Unit))
    public fun remove(value: T): PersistentHashSet<T> = withMap(map.remove(value))
    public fun tryRemove(value: T): SetRemoveResult<T>? {
        val removed = map.tryRemoveEntry(value) ?: return null
        return SetRemoveResult(withMap(removed.map), removed.entry.key)
    }
    public fun clear(): PersistentHashSet<T> = withMap(map.clear())
    public fun union(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) result = result.put(value)
        return result
    }
    public fun union(other: PersistentHashSet<T>): PersistentHashSet<T> = withMap(map.union(other.map))
    public fun intersect(values: Iterable<T>): PersistentHashSet<T> {
        val probe = empty<T>(policy).union(values)
        var result = empty<T>(policy)
        for (value in this) if (probe.contains(value)) result = result.put(value)
        return result
    }
    public fun intersect(other: PersistentHashSet<T>): PersistentHashSet<T> = withMap(map.intersect(other.map))
    public fun except(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) result = result.remove(value)
        return result
    }
    public fun except(other: PersistentHashSet<T>): PersistentHashSet<T> = withMap(map.except(other.map))
    public fun symmetricExcept(values: Iterable<T>): PersistentHashSet<T> {
        val distinct = empty<T>(policy).union(values)
        var result = this
        for (value in distinct) result = if (result.contains(value)) result.remove(value) else result.put(value)
        return result
    }
    public fun symmetricExcept(other: PersistentHashSet<T>): PersistentHashSet<T> =
        withMap(map.symmetricExcept(other.map))

    public fun isSubsetOf(other: PersistentHashSet<T>): Boolean {
        if (policy !== other.policy) return isSubsetOf(other as Iterable<T>)
        return size <= other.size && map.intersect(other.map).sharesRootWith(map)
    }
    public fun isProperSubsetOf(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) size < other.size && isSubsetOf(other) else isProperSubsetOf(other as Iterable<T>)
    public fun isSupersetOf(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) other.isSubsetOf(this) else isSupersetOf(other as Iterable<T>)
    public fun isProperSupersetOf(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) size > other.size && other.isSubsetOf(this) else isProperSupersetOf(other as Iterable<T>)
    public fun overlaps(other: PersistentHashSet<T>): Boolean {
        if (policy !== other.policy) return overlaps(other as Iterable<T>)
        return map.intersect(other.map).size != 0
    }
    public fun setEquals(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) map.mapEquals(other.map) else setEquals(other as Iterable<T>)
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
    private fun withMap(value: PersistentHashMap<T, Unit>): PersistentHashSet<T> =
        if (value === map) this else PersistentHashSet(value)
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
    is Leaf -> ChampStatistics(1, 0, 0, 0, 0)
    is Collision -> ChampStatistics(0, 0, node.entries.size, 0, 0)
    is BitmapNode -> {
        val children = node.nodes.map(::champStatistics)
        ChampStatistics(
            node.data.size + children.sumOf { it.inlinePayloads },
            1 + children.sumOf { it.bitmapNodes },
            children.sumOf { it.collisionPayloads },
            node.nodes.count { it is Leaf } + children.sumOf { it.invalidLeafChildren },
            (if (node.data.size + node.nodes.size < 2 &&
                !(node.data.isEmpty() && node.nodes.singleOrNull() is BitmapNode)) 1 else 0) +
                children.sumOf { it.underfullBitmapNodes },
        )
    }
}

private enum class ChampOperation { UNION, INTERSECT, EXCEPT, SYMMETRIC_EXCEPT }

private fun <K, V> combineNodes(
    left: Node<K, V>?,
    right: Node<K, V>?,
    shift: Int,
    operation: ChampOperation,
    policy: HashPolicy<K>,
): Node<K, V>? {
    if (left === right) return if (operation == ChampOperation.UNION || operation == ChampOperation.INTERSECT) left else null
    if (left == null) return if (operation == ChampOperation.UNION || operation == ChampOperation.SYMMETRIC_EXCEPT) right else null
    if (right == null) return if (operation != ChampOperation.INTERSECT) left else null
    if (left is Leaf || left is Collision) {
        if (right is Leaf || right is Collision) return combineHashNodes(left, right, shift, operation, policy)
    }

    val slots = arrayOfNulls<Node<K, V>>(32)
    for (index in slots.indices) {
        slots[index] = combineNodes(
            logicalSlot(left, index, shift),
            logicalSlot(right, index, shift),
            shift + BitsPerLevel,
            operation,
            policy,
        )
    }
    return buildLogicalNode(slots, left, shift, policy)
}

private fun <K, V> combineHashNodes(
    left: Node<K, V>,
    right: Node<K, V>,
    shift: Int,
    operation: ChampOperation,
    policy: HashPolicy<K>,
): Node<K, V>? {
    val leftHash = hashOf(left)
    val rightHash = hashOf(right)
    if (leftHash != rightHash) {
        if (operation == ChampOperation.INTERSECT) return null
        if (operation == ChampOperation.EXCEPT) return left
        val slots = arrayOfNulls<Node<K, V>>(32)
        val leftIndex = hashFragment(leftHash, shift)
        val rightIndex = hashFragment(rightHash, shift)
        if (leftIndex != rightIndex) {
            slots[leftIndex] = left
            slots[rightIndex] = right
        } else {
            slots[leftIndex] = combineHashNodes(left, right, shift + BitsPerLevel, operation, policy)
        }
        return buildLogicalNode(slots, left, shift, policy)
    }

    val leftEntries = entriesOf(left)
    val rightEntries = entriesOf(right)
    val result = mutableListOf<Leaf<K, V>>()
    for (leftEntry in leftEntries) {
        val rightEntry = rightEntries.firstOrNull { policy.equivalent(leftEntry.key, it.key) }
        when (operation) {
            ChampOperation.UNION -> result += if (rightEntry == null || leftEntry.value == rightEntry.value) {
                leftEntry
            } else {
                Leaf(leftEntry.hash, leftEntry.key, rightEntry.value)
            }
            ChampOperation.INTERSECT -> if (rightEntry != null) result += leftEntry
            ChampOperation.EXCEPT, ChampOperation.SYMMETRIC_EXCEPT -> if (rightEntry == null) result += leftEntry
        }
    }
    if (operation == ChampOperation.UNION || operation == ChampOperation.SYMMETRIC_EXCEPT) {
        for (rightEntry in rightEntries) {
            if (leftEntries.none { policy.equivalent(it.key, rightEntry.key) }) result += rightEntry
        }
    }
    if (sameEntries(leftEntries, result, policy)) return left
    return when (result.size) {
        0 -> null
        1 -> result[0]
        else -> Collision(leftHash, result)
    }
}

private fun <K, V> hashOf(node: Node<K, V>): Int = when (node) {
    is Leaf -> node.hash
    is Collision -> node.hash
    is BitmapNode -> error("bitmap node has no single hash")
}

private fun <K, V> entriesOf(node: Node<K, V>): List<Leaf<K, V>> = when (node) {
    is Leaf -> listOf(node)
    is Collision -> node.entries
    is BitmapNode -> error("bitmap node is not an entry run")
}

private fun <K, V> sameEntries(
    left: List<Leaf<K, V>>,
    right: List<Leaf<K, V>>,
    policy: HashPolicy<K>,
): Boolean = left.size == right.size && left.indices.all { index ->
    left[index].hash == right[index].hash &&
        policy.equivalent(left[index].key, right[index].key) &&
        left[index].value == right[index].value
}

private fun <K, V> logicalSlot(node: Node<K, V>, index: Int, shift: Int): Node<K, V>? = when (node) {
    is Leaf -> if (hashFragment(node.hash, shift) == index) node else null
    is Collision -> if (hashFragment(node.hash, shift) == index) node else null
    is BitmapNode -> {
        val bit = bitPosition(index)
        when {
            node.dataMap and bit != 0 -> node.data[sparseIndex(node.dataMap, bit)]
            node.nodeMap and bit != 0 -> node.nodes[sparseIndex(node.nodeMap, bit)]
            else -> null
        }
    }
}

private fun <K, V> buildLogicalNode(
    slots: Array<Node<K, V>?>,
    originalLeft: Node<K, V>,
    shift: Int,
    policy: HashPolicy<K>,
): Node<K, V>? {
    if (logicalSlotsMatch(slots, originalLeft, shift, policy)) return originalLeft
    var dataMap = 0
    var nodeMap = 0
    val data = mutableListOf<Leaf<K, V>>()
    val nodes = mutableListOf<Node<K, V>>()
    for (index in slots.indices) {
        val node = slots[index] ?: continue
        val bit = bitPosition(index)
        if (node is Leaf) {
            dataMap = dataMap or bit
            data += node
        } else {
            nodeMap = nodeMap or bit
            nodes += node
        }
    }
    if (data.isEmpty() && nodes.isEmpty()) return null
    if (data.size == 1 && nodes.isEmpty()) return data[0]
    if (data.isEmpty() && nodes.size == 1 && nodes[0] !is BitmapNode) return nodes[0]
    return BitmapNode(dataMap, nodeMap, data, nodes)
}

private fun <K, V> logicalSlotsMatch(
    slots: Array<Node<K, V>?>,
    original: Node<K, V>,
    shift: Int,
    policy: HashPolicy<K>,
): Boolean = slots.indices.all { index ->
    val expected = logicalSlot(original, index, shift)
    val actual = slots[index]
    expected === actual || expected is Leaf && actual is Leaf &&
        expected.hash == actual.hash && policy.equivalent(expected.key, actual.key) && expected.value == actual.value
}

private fun <K, V> champTopology(node: Node<K, V>): String = when (node) {
    is Leaf -> "L${node.hash}"
    is Collision -> "C${node.hash}:${node.entries.size}"
    is BitmapNode -> buildString {
        append("B${node.dataMap}:${node.nodeMap}[")
        node.data.forEach { append(it.hash).append(',') }
        append('|')
        node.nodes.forEach { append(champTopology(it)).append(';') }
        append(']')
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
