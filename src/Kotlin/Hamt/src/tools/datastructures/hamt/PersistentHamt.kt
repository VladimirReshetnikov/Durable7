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

public data class MapRemoveResult<K, V>(
    public val map: PersistentHashMap<K, V>,
    public val value: V,
)

public data class SetRemoveResult<T>(
    public val set: PersistentHashSet<T>,
    public val value: T,
)

private const val BitsPerLevel: Int = 5
private const val BranchMask: Int = 0x1f

private sealed interface Node<K, V>

private data class Leaf<K, V>(
    val hash: Int,
    val key: K,
    val value: V,
) : Node<K, V>

private data class Collision<K, V>(
    val hash: Int,
    val entries: List<HamtEntry<K, V>>,
) : Node<K, V>

private data class Branch<K, V>(
    val bitmap: Int,
    val children: List<Node<K, V>>,
) : Node<K, V>

private data class InsertResult<K, V>(
    val node: Node<K, V>,
    val added: Boolean,
    val changed: Boolean,
    val duplicate: Boolean,
)

private data class RemoveResult<K, V>(
    val node: Node<K, V>?,
    val removed: V?,
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

    public val isEmpty: Boolean
        get() = size == 0

    public fun sharesRootWith(other: PersistentHashMap<K, V>): Boolean = root === other.root

    public fun containsKey(key: K): Boolean = getEntry(key) != null

    public operator fun get(key: K): V? = getEntry(key)?.value

    public fun getEntry(key: K): HamtEntry<K, V>? {
        val current = root ?: return null
        return getInNode(current, policy.hash(key), key, 0, policy)
    }

    public fun put(key: K, value: V): PersistentHashMap<K, V> {
        val hash = policy.hash(key)
        val current = root
        if (current == null) {
            return PersistentHashMap(Leaf(hash, key, value), 1, policy)
        }

        val result = insertNode(current, hash, key, value, 0, overwrite = true, policy)
        if (!result.changed) {
            return this
        }

        return PersistentHashMap(result.node, size + if (result.added) 1 else 0, policy)
    }

    public fun add(key: K, value: V): PersistentHashMap<K, V> {
        val result = tryAdd(key, value)
        if (!result.added) {
            throw DuplicateKeyException()
        }

        return result.value
    }

    public fun tryAdd(key: K, value: V): AddResult<PersistentHashMap<K, V>> {
        val hash = policy.hash(key)
        val current = root
        if (current == null) {
            return AddResult(PersistentHashMap(Leaf(hash, key, value), 1, policy), true)
        }

        val result = insertNode(current, hash, key, value, 0, overwrite = false, policy)
        if (result.duplicate) {
            return AddResult(this, false)
        }

        return AddResult(PersistentHashMap(result.node, size + if (result.added) 1 else 0, policy), result.added)
    }

    public fun setItems(items: Iterable<Pair<K, V>>): PersistentHashMap<K, V> {
        var result = this
        for ((key, value) in items) {
            result = result.put(key, value)
        }

        return result
    }

    public fun remove(key: K): PersistentHashMap<K, V> = tryRemove(key)?.map ?: this

    public fun tryRemove(key: K): MapRemoveResult<K, V>? {
        val current = root ?: return null
        val result = removeNode(current, policy.hash(key), key, 0, policy)
        if (!result.changed) {
            return null
        }

        @Suppress("UNCHECKED_CAST")
        return MapRemoveResult(PersistentHashMap(result.node, size - 1, policy), result.removed as V)
    }

    public fun clear(): PersistentHashMap<K, V> =
        if (isEmpty) this else PersistentHashMap(null, 0, policy)

    public fun entries(): Sequence<HamtEntry<K, V>> = sequence {
        val current = root ?: return@sequence
        yieldEntries(current)
    }

    public fun keys(): Sequence<K> = entries().map { it.key }

    public fun values(): Sequence<V> = entries().map { it.value }

    override fun iterator(): Iterator<HamtEntry<K, V>> = entries().iterator()
}

public class PersistentHashSet<T> private constructor(
    private val map: PersistentHashMap<T, Unit>,
) : Iterable<T> {
    public companion object {
        public fun <T> empty(policy: HashPolicy<T> = defaultHashPolicy()): PersistentHashSet<T> =
            PersistentHashSet(PersistentHashMap.empty(policy))

        public fun <T> from(
            values: Iterable<T>,
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): PersistentHashSet<T> = empty<T>(policy).union(values)
    }

    public val size: Int
        get() = map.size

    public val isEmpty: Boolean
        get() = map.isEmpty

    public val policy: HashPolicy<T>
        get() = map.policy

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
        val stored = get(value) ?: return null
        return SetRemoveResult(remove(value), stored)
    }

    public fun clear(): PersistentHashSet<T> = PersistentHashSet(map.clear())

    public fun union(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) {
            result = result.put(value)
        }

        return result
    }

    public fun intersect(values: Iterable<T>): PersistentHashSet<T> {
        val probe = empty<T>(policy).union(values)
        var result = empty<T>(policy)
        for (value in this) {
            if (probe.contains(value)) {
                result = result.put(value)
            }
        }

        return result
    }

    public fun except(values: Iterable<T>): PersistentHashSet<T> {
        val probe = empty<T>(policy).union(values)
        var result = empty<T>(policy)
        for (value in this) {
            if (!probe.contains(value)) {
                result = result.put(value)
            }
        }

        return result
    }

    public fun symmetricExcept(values: Iterable<T>): PersistentHashSet<T> {
        val other = empty<T>(policy).union(values)
        var result = empty<T>(policy)
        for (value in this) {
            if (!other.contains(value)) {
                result = result.put(value)
            }
        }

        for (value in other) {
            if (!contains(value)) {
                result = result.put(value)
            }
        }

        return result
    }

    public fun isSubsetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return all { probe.contains(it) }
    }

    public fun isSupersetOf(values: Iterable<T>): Boolean =
        values.all { contains(it) }

    public fun isProperSubsetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return size < probe.size && all { probe.contains(it) }
    }

    public fun isProperSupersetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return size > probe.size && probe.all { contains(it) }
    }

    public fun overlaps(values: Iterable<T>): Boolean =
        values.any { contains(it) }

    public fun setEquals(values: Iterable<T>): Boolean {
        val other = empty<T>(policy).union(values)
        return size == other.size && all { other.contains(it) }
    }

    public fun asSequence(): Sequence<T> = map.keys()

    override fun iterator(): Iterator<T> = asSequence().iterator()
}

private fun <K, V> getInNode(
    node: Node<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
): HamtEntry<K, V>? =
    when (node) {
        is Leaf -> if (node.hash == hash && policy.equivalent(node.key, key)) {
            HamtEntry(node.key, node.value)
        } else {
            null
        }

        is Collision -> if (node.hash == hash) {
            node.entries.firstOrNull { policy.equivalent(it.key, key) }
        } else {
            null
        }

        is Branch -> {
            val bit = bitPosition(hashFragment(hash, shift))
            if ((node.bitmap and bit) == 0) {
                null
            } else {
                getInNode(node.children[sparseIndex(node.bitmap, bit)], hash, key, shift + BitsPerLevel, policy)
            }
        }
    }

private fun <K, V> insertNode(
    node: Node<K, V>,
    hash: Int,
    key: K,
    value: V,
    shift: Int,
    overwrite: Boolean,
    policy: HashPolicy<K>,
): InsertResult<K, V> =
    when (node) {
        is Leaf -> insertIntoLeaf(node, hash, key, value, shift, overwrite, policy)
        is Collision -> insertIntoCollision(node, hash, key, value, shift, overwrite, policy)
        is Branch -> insertIntoBranch(node, hash, key, value, shift, overwrite, policy)
    }

private fun <K, V> insertIntoLeaf(
    node: Leaf<K, V>,
    hash: Int,
    key: K,
    value: V,
    shift: Int,
    overwrite: Boolean,
    policy: HashPolicy<K>,
): InsertResult<K, V> {
    if (node.hash == hash && policy.equivalent(node.key, key)) {
        if (!overwrite) {
            return InsertResult(node, added = false, changed = false, duplicate = true)
        }

        if (node.value == value) {
            return InsertResult(node, added = false, changed = false, duplicate = false)
        }

        return InsertResult(Leaf(hash, node.key, value), added = false, changed = true, duplicate = false)
    }

    val newLeaf = Leaf(hash, key, value)
    if (node.hash == hash) {
        return InsertResult(
            Collision(hash, listOf(HamtEntry(node.key, node.value), HamtEntry(key, value))),
            added = true,
            changed = true,
            duplicate = false,
        )
    }

    return InsertResult(mergeTwo(node, node.hash, newLeaf, hash, shift), added = true, changed = true, duplicate = false)
}

private fun <K, V> insertIntoCollision(
    node: Collision<K, V>,
    hash: Int,
    key: K,
    value: V,
    shift: Int,
    overwrite: Boolean,
    policy: HashPolicy<K>,
): InsertResult<K, V> {
    if (node.hash == hash) {
        val index = node.entries.indexOfFirst { policy.equivalent(it.key, key) }
        if (index >= 0) {
            if (!overwrite) {
                return InsertResult(node, added = false, changed = false, duplicate = true)
            }

            if (node.entries[index].value == value) {
                return InsertResult(node, added = false, changed = false, duplicate = false)
            }

            val next = node.entries.toMutableList()
            next[index] = HamtEntry(next[index].key, value)
            return InsertResult(Collision(hash, next.toList()), added = false, changed = true, duplicate = false)
        }

        return InsertResult(
            Collision(hash, node.entries + HamtEntry(key, value)),
            added = true,
            changed = true,
            duplicate = false,
        )
    }

    val newLeaf = Leaf(hash, key, value)
    return InsertResult(mergeTwo(node, node.hash, newLeaf, hash, shift), added = true, changed = true, duplicate = false)
}

private fun <K, V> insertIntoBranch(
    node: Branch<K, V>,
    hash: Int,
    key: K,
    value: V,
    shift: Int,
    overwrite: Boolean,
    policy: HashPolicy<K>,
): InsertResult<K, V> {
    val bit = bitPosition(hashFragment(hash, shift))
    val index = sparseIndex(node.bitmap, bit)
    if ((node.bitmap and bit) == 0) {
        val next = node.children.toMutableList()
        next.add(index, Leaf(hash, key, value))
        return InsertResult(Branch(node.bitmap or bit, next.toList()), added = true, changed = true, duplicate = false)
    }

    val child = insertNode(node.children[index], hash, key, value, shift + BitsPerLevel, overwrite, policy)
    if (child.duplicate || !child.changed) {
        return InsertResult(node, added = false, changed = false, duplicate = child.duplicate)
    }

    val next = node.children.toMutableList()
    next[index] = child.node
    return InsertResult(Branch(node.bitmap, next.toList()), added = child.added, changed = true, duplicate = false)
}

private fun <K, V> removeNode(
    node: Node<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
): RemoveResult<K, V> =
    when (node) {
        is Leaf -> if (node.hash == hash && policy.equivalent(node.key, key)) {
            RemoveResult(null, node.value, changed = true)
        } else {
            RemoveResult(node, null, changed = false)
        }

        is Collision -> removeFromCollision(node, hash, key, policy)
        is Branch -> removeFromBranch(node, hash, key, shift, policy)
    }

private fun <K, V> removeFromCollision(
    node: Collision<K, V>,
    hash: Int,
    key: K,
    policy: HashPolicy<K>,
): RemoveResult<K, V> {
    if (node.hash != hash) {
        return RemoveResult(node, null, changed = false)
    }

    val index = node.entries.indexOfFirst { policy.equivalent(it.key, key) }
    if (index < 0) {
        return RemoveResult(node, null, changed = false)
    }

    val removed = node.entries[index].value
    val next = node.entries.toMutableList()
    next.removeAt(index)
    val nextNode = when (next.size) {
        0 -> null
        1 -> Leaf(hash, next[0].key, next[0].value)
        else -> Collision(hash, next.toList())
    }

    return RemoveResult(nextNode, removed, changed = true)
}

private fun <K, V> removeFromBranch(
    node: Branch<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
): RemoveResult<K, V> {
    val bit = bitPosition(hashFragment(hash, shift))
    if ((node.bitmap and bit) == 0) {
        return RemoveResult(node, null, changed = false)
    }

    val index = sparseIndex(node.bitmap, bit)
    val child = removeNode(node.children[index], hash, key, shift + BitsPerLevel, policy)
    if (!child.changed) {
        return RemoveResult(node, null, changed = false)
    }

    val next = node.children.toMutableList()
    var bitmap = node.bitmap
    if (child.node == null) {
        next.removeAt(index)
        bitmap = bitmap and bit.inv()
    } else {
        next[index] = child.node
    }

    val nextNode = when {
        next.isEmpty() -> null
        next.size == 1 && next[0] !is Branch -> next[0]
        else -> Branch(bitmap, next.toList())
    }

    return RemoveResult(nextNode, child.removed, changed = true)
}

private fun <K, V> mergeTwo(
    left: Node<K, V>,
    leftHash: Int,
    right: Node<K, V>,
    rightHash: Int,
    shift: Int,
): Node<K, V> {
    if (leftHash == rightHash) {
        return Collision(leftHash, collectEntries(left) + collectEntries(right))
    }

    val leftFragment = hashFragment(leftHash, shift)
    val rightFragment = hashFragment(rightHash, shift)
    val leftBit = bitPosition(leftFragment)
    val rightBit = bitPosition(rightFragment)

    if (leftBit == rightBit) {
        return Branch(leftBit, listOf(mergeTwo(left, leftHash, right, rightHash, shift + BitsPerLevel)))
    }

    return if (leftFragment < rightFragment) {
        Branch(leftBit or rightBit, listOf(left, right))
    } else {
        Branch(leftBit or rightBit, listOf(right, left))
    }
}

private fun <K, V> collectEntries(node: Node<K, V>): List<HamtEntry<K, V>> =
    when (node) {
        is Leaf -> listOf(HamtEntry(node.key, node.value))
        is Collision -> node.entries
        is Branch -> node.children.flatMap { collectEntries(it) }
    }

private suspend fun <K, V> SequenceScope<HamtEntry<K, V>>.yieldEntries(node: Node<K, V>) {
    when (node) {
        is Leaf -> yield(HamtEntry(node.key, node.value))
        is Collision -> for (entry in node.entries) yield(entry)
        is Branch -> for (child in node.children) yieldEntries(child)
    }
}

private fun hashFragment(hash: Int, shift: Int): Int = (hash ushr shift) and BranchMask

private fun bitPosition(fragment: Int): Int = 1 shl fragment

private fun sparseIndex(bitmap: Int, bit: Int): Int = Integer.bitCount(bitmap and (bit - 1))
