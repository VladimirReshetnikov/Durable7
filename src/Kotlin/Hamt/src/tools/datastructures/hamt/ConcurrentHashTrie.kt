package tools.datastructures.hamt

import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** Lock-free mutable Ctrie with O(1) immutable generation snapshots. */
public class ConcurrentHashTrie<K, V>(
    public val policy: HashPolicy<K> = defaultHashPolicy(),
) : Iterable<HamtEntry<K, V>> {
    private val root: AtomicReference<Any>
    private val revision = AtomicLong()

    @Volatile
    internal var snapshotMainReadHookForTesting: (() -> Unit)? = null

    @Volatile
    internal var gcasInstalledHookForTesting: (() -> Unit)? = null

    init {
        val generation = Generation()
        root = AtomicReference<Any>(
            Root<K, V>(INode<K, V>(CNode.empty<K, V>(), generation), generation),
        )
    }

    public val generation: Long get() = revision.get()
    public val size: Int get() = snapshot().count()
    public val isEmpty: Boolean get() = snapshot().none()

    public operator fun get(key: K): V? = getEntry(key)?.value
    public fun getEntry(key: K): HamtEntry<K, V>? = getEntry(readRoot(), key)
    public fun containsKey(key: K): Boolean = getEntry(key) != null

    public fun set(key: K, value: V) {
        mutate(key) { exists, current ->
            if (exists && valuesEqual(current, value)) Decision.none() else Decision.set(value)
        }
    }

    public fun tryAdd(key: K, value: V): Boolean =
        mutate(key) { exists, _ -> if (exists) Decision.none() else Decision.set(value) }.changed

    public fun getOrPut(key: K, factory: (K) -> V): V {
        val result = mutate(key) { exists, current ->
            if (exists) Decision.returning(current) else Decision.set(factory(key))
        }
        @Suppress("UNCHECKED_CAST")
        return result.value as V
    }

    /** Atomically adds a missing value or transforms the currently observed value. */
    public fun compute(key: K, add: (K) -> V, update: (K, V) -> V): V {
        val result = mutate(key) { exists, current ->
            @Suppress("UNCHECKED_CAST")
            val next = if (exists) update(key, current as V) else add(key)
            if (exists && valuesEqual(current, next)) Decision.returning(current) else Decision.set(next)
        }
        @Suppress("UNCHECKED_CAST")
        return result.value as V
    }

    public fun remove(key: K): HamtEntry<K, V>? {
        var removedKey: K? = null
        val result = mutate(key) { exists, current, storedKey ->
            if (!exists) Decision.none()
            else {
                removedKey = storedKey
                Decision.remove(current)
            }
        }
        if (!result.changed) return null
        @Suppress("UNCHECKED_CAST")
        return HamtEntry(removedKey as K, result.value as V)
    }

    public fun clear() {
        while (true) {
            val observed = readRoot()
            val main = readMain(observed.node)
            if (main is CNode && main.bitmap == 0) return
            if (gcas(observed.node, main, CNode.empty(), observed)) {
                revision.incrementAndGet()
                return
            }
        }
    }

    /** Advances the root generation and returns the frozen previous generation in O(1). */
    public fun snapshot(): Snapshot<K, V> {
        while (true) {
            val before = readRoot()
            val main = readMain(before.node)
            snapshotMainReadHookForTesting?.invoke()
            val generation = Generation()
            val after = Root(INode(main, generation), generation)
            val descriptor = RootDescriptor(before, main, after)
            if (!root.compareAndSet(before, descriptor)) continue
            complete(descriptor)
            if (descriptor.status.get() == RootDescriptor.COMMITTED) {
                return Snapshot(this, before)
            }
        }
    }

    override fun iterator(): Iterator<HamtEntry<K, V>> = snapshot().iterator()

    private fun valuesEqual(left: V?, right: V?): Boolean = left === right || left == right

    @Suppress("UNCHECKED_CAST")
    private fun mutate(
        key: K,
        transform: (Boolean, V?, K?) -> Decision<V>,
    ): MutationResult<V> {
        val hash = policy.hash(key)
        while (true) {
            val observed = readRoot()
            var node = observed.node
            var parentNode: INode<K, V>? = null
            var parentMain: CNode<K, V>? = null
            var parentPosition = 0
            var parentBit = 0
            var shift = 0
            while (true) {
                when (val main = readMain(node)) {
                    is TNode -> {
                        val parent = parentNode
                            ?: error("The root indirection node cannot contain a tomb.")
                        val parentCNode = parentMain
                            ?: error("A tomb must have a C-node parent.")
                        val cleaned = main.entry?.let { parentCNode.replace(parentPosition, it) }
                            ?: parentCNode.remove(parentPosition, parentBit)
                        gcas(
                            parent,
                            parentCNode,
                            contract(cleaned, parent === observed.node),
                            observed,
                        )
                        break
                    }
                    is LNode -> {
                        val index = main.entries.indexOfFirst {
                            it.hash == hash && policy.equivalent(it.key, key)
                        }
                        val exists = index >= 0
                        val current = if (exists) main.entries[index].value else null
                        val storedKey = if (exists) main.entries[index].key else null
                        val decision = transform(exists, current, storedKey)
                        if (decision.kind == DecisionKind.NONE) {
                            return MutationResult(false, decision.result ?: current)
                        }
                        val replacement: MainNode<K, V> = when (decision.kind) {
                            DecisionKind.REMOVE -> contractCollision(
                                main.entries.toMutableList().also { it.removeAt(index) },
                                main.entries[index].hash,
                                node === observed.node,
                            )
                            DecisionKind.SET -> {
                                val entry = SNode(
                                    if (exists) main.entries[index].hash else hash,
                                    if (exists) storedKey!! else key,
                                    decision.value as V,
                                )
                                if (exists) {
                                    LNode(main.entries.toMutableList().also { it[index] = entry })
                                } else if (main.entries[0].hash == hash) {
                                    LNode(main.entries + entry)
                                } else {
                                    merge(main, entry, shift, observed.generation)
                                }
                            }
                            DecisionKind.NONE -> error("handled above")
                        }
                        if (!gcas(node, main, replacement, observed)) break
                        revision.incrementAndGet()
                        if (decision.kind == DecisionKind.REMOVE) cleanTombs(hash)
                        return MutationResult(true, decision.result)
                    }
                    is CNode -> {
                        val bit = 1 shl ((hash ushr shift) and 31)
                        val position = Integer.bitCount(main.bitmap and (bit - 1))
                        if (main.bitmap and bit == 0) {
                            val decision = transform(false, null, null)
                            if (decision.kind == DecisionKind.NONE) return MutationResult(false, decision.result)
                            val replacement = main.insert(position, bit, SNode(hash, key, decision.value as V))
                            if (!gcas(node, main, replacement, observed)) break
                            revision.incrementAndGet()
                            return MutationResult(true, decision.result)
                        }
                        when (val branch = main.branches[position]) {
                            is SNode -> {
                                if (branch.hash == hash && policy.equivalent(branch.key, key)) {
                                    val decision = transform(true, branch.value, branch.key)
                                    if (decision.kind == DecisionKind.NONE) {
                                        return MutationResult(false, decision.result ?: branch.value)
                                    }
                                    val replacement = if (decision.kind == DecisionKind.REMOVE) {
                                        contract(main.remove(position, bit), node === observed.node)
                                    } else {
                                        main.replace(position, SNode(hash, branch.key, decision.value as V))
                                    }
                                    if (!gcas(node, main, replacement, observed)) break
                                    revision.incrementAndGet()
                                    if (decision.kind == DecisionKind.REMOVE) cleanTombs(hash)
                                    return MutationResult(true, decision.result)
                                }
                                val decision = transform(false, null, null)
                                if (decision.kind == DecisionKind.NONE) return MutationResult(false, decision.result)
                                val child = merge(branch, SNode(hash, key, decision.value as V), shift + 5, observed.generation)
                                if (!gcas(node, main, main.replace(position, child), observed)) break
                                revision.incrementAndGet()
                                return MutationResult(true, decision.result)
                            }
                            is INode -> {
                                var child = branch
                                var descentMain = main
                                if (child.generation !== observed.generation) {
                                    val renewed = INode(readMain(child), observed.generation)
                                    val renewedParent = main.replace(position, renewed)
                                    if (!gcas(node, main, renewedParent, observed)) break
                                    child = renewed
                                    descentMain = renewedParent
                                }
                                parentNode = node
                                parentMain = descentMain
                                parentPosition = position
                                parentBit = bit
                                node = child
                                shift += 5
                                continue
                            }
                        }
                    }
                }
            }
        }
    }

    private fun mutate(key: K, transform: (Boolean, V?) -> Decision<V>): MutationResult<V> =
        mutate(key) { exists, value, _ -> transform(exists, value) }

    private fun getEntry(observed: Root<K, V>, key: K): HamtEntry<K, V>? {
        val hash = policy.hash(key)
        var node = observed.node
        var shift = 0
        while (true) {
            when (val main = readMain(node)) {
                is TNode -> return main.entry?.takeIf {
                    it.hash == hash && policy.equivalent(it.key, key)
                }?.let { HamtEntry(it.key, it.value) }
                is LNode -> return main.entries.firstOrNull {
                    it.hash == hash && policy.equivalent(it.key, key)
                }?.let { HamtEntry(it.key, it.value) }
                is CNode -> {
                    val bit = 1 shl ((hash ushr shift) and 31)
                    if (main.bitmap and bit == 0) return null
                    when (val branch = main.branches[Integer.bitCount(main.bitmap and (bit - 1))]) {
                        is SNode -> return if (branch.hash == hash && policy.equivalent(branch.key, key)) {
                            HamtEntry(branch.key, branch.value)
                        } else null
                        is INode -> {
                            node = branch
                            shift += 5
                        }
                    }
                }
            }
        }
    }

    private fun cleanTombs(hash: Int) {
        while (true) {
            val observed = readRoot()
            var node = observed.node
            var parentNode: INode<K, V>? = null
            var parentMain: CNode<K, V>? = null
            var parentPosition = 0
            var parentBit = 0
            var shift = 0
            while (true) {
                when (val main = readMain(node)) {
                    is TNode -> {
                        val parent = parentNode
                            ?: error("The root indirection node cannot contain a tomb.")
                        val parentCNode = parentMain
                            ?: error("A tomb must have a C-node parent.")
                        val cleaned = main.entry?.let { parentCNode.replace(parentPosition, it) }
                            ?: parentCNode.remove(parentPosition, parentBit)
                        gcas(
                            parent,
                            parentCNode,
                            contract(cleaned, parent === observed.node),
                            observed,
                        )
                        break
                    }
                    is LNode -> return
                    is CNode -> {
                        val bit = 1 shl ((hash ushr shift) and 31)
                        if (main.bitmap and bit == 0) return
                        val position = Integer.bitCount(main.bitmap and (bit - 1))
                        when (val branch = main.branches[position]) {
                            is SNode -> return
                            is INode -> {
                                var child = branch
                                var descentMain = main
                                if (child.generation !== observed.generation) {
                                    val renewed = INode(readMain(child), observed.generation)
                                    val renewedParent = main.replace(position, renewed)
                                    if (!gcas(node, main, renewedParent, observed)) break
                                    child = renewed
                                    descentMain = renewedParent
                                }
                                parentNode = node
                                parentMain = descentMain
                                parentPosition = position
                                parentBit = bit
                                node = child
                                shift += 5
                            }
                        }
                    }
                }
            }
        }
    }

    private fun contract(node: CNode<K, V>, isRoot: Boolean): MainNode<K, V> {
        if (isRoot || node.branches.size > 1) return node
        if (node.branches.isEmpty()) return TNode.empty()
        val only = node.branches[0]
        return if (only is SNode) TNode(only) else node
    }

    private fun contractCollision(entries: List<SNode<K, V>>, hash: Int, isRoot: Boolean): MainNode<K, V> {
        if (entries.size > 1) return LNode(entries)
        if (entries.isEmpty()) return if (isRoot) CNode.empty() else TNode.empty()
        if (!isRoot) return TNode(entries[0])
        val bit = 1 shl (hash and 31)
        return CNode(bit, listOf(entries[0]))
    }

    private fun merge(left: SNode<K, V>, right: SNode<K, V>, shift: Int, generation: Generation): INode<K, V> {
        if (left.hash == right.hash || shift >= 32) return INode(LNode(listOf(left, right)), generation)
        val leftBit = 1 shl ((left.hash ushr shift) and 31)
        val rightBit = 1 shl ((right.hash ushr shift) and 31)
        if (leftBit == rightBit) {
            return INode(CNode(leftBit, listOf(merge(left, right, shift + 5, generation))), generation)
        }
        val branches = if (Integer.compareUnsigned(leftBit, rightBit) < 0) listOf(left, right) else listOf(right, left)
        return INode(CNode(leftBit or rightBit, branches), generation)
    }

    private fun merge(
        collision: LNode<K, V>,
        singleton: SNode<K, V>,
        shift: Int,
        generation: Generation,
    ): MainNode<K, V> {
        val collisionHash = collision.entries[0].hash
        check(collision.entries.all { it.hash == collisionHash })
        check(collisionHash != singleton.hash)
        require(shift < 32) { "Different 32-bit hashes cannot share every Ctrie level." }
        val collisionBit = 1 shl ((collisionHash ushr shift) and 31)
        val singletonBit = 1 shl ((singleton.hash ushr shift) and 31)
        if (collisionBit != singletonBit) {
            val collisionBranch = INode(collision, generation)
            val branches: List<Branch<K, V>> = if (Integer.compareUnsigned(collisionBit, singletonBit) < 0) {
                listOf(collisionBranch, singleton)
            } else {
                listOf(singleton, collisionBranch)
            }
            return CNode(collisionBit or singletonBit, branches)
        }
        val child = INode(merge(collision, singleton, shift + 5, generation), generation)
        return CNode(collisionBit, listOf(child))
    }

    private fun gcas(node: INode<K, V>, before: MainNode<K, V>, after: MainNode<K, V>, observed: Root<K, V>): Boolean {
        val descriptor = Descriptor(before, after, observed)
        if (!node.main.compareAndSet(before, descriptor)) return false
        gcasInstalledHookForTesting?.invoke()
        complete(descriptor)
        return descriptor.status.get() == Descriptor.COMMITTED
    }

    @Suppress("UNCHECKED_CAST")
    private fun readMain(node: INode<K, V>): MainNode<K, V> {
        while (true) {
            when (val value = node.main.get()) {
                is MainNode<*, *> -> return value as MainNode<K, V>
                else -> {
                    val descriptor = value as Descriptor<K, V>
                    complete(descriptor)
                    val selected = if (descriptor.status.get() == Descriptor.COMMITTED) descriptor.after else descriptor.before
                    node.main.compareAndSet(descriptor, selected)
                }
            }
        }
    }

    private fun complete(descriptor: Descriptor<K, V>) {
        val status = if (readRoot() === descriptor.root) Descriptor.COMMITTED else Descriptor.ABORTED
        descriptor.status.compareAndSet(Descriptor.UNDECIDED, status)
    }

    @Suppress("UNCHECKED_CAST")
    private fun readRoot(): Root<K, V> {
        while (true) {
            when (val value = root.get()) {
                is Root<*, *> -> return value as Root<K, V>
                else -> complete(value as RootDescriptor<K, V>)
            }
        }
    }

    private fun complete(descriptor: RootDescriptor<K, V>) {
        // Inspect the raw main slot rather than helping a node-local descriptor. Helping it would
        // need to read the root and create a cycle with this root-descriptor completion.
        val status = if (descriptor.before.node.main.get() === descriptor.expectedMain) {
            RootDescriptor.COMMITTED
        } else {
            RootDescriptor.ABORTED
        }
        descriptor.status.compareAndSet(RootDescriptor.UNDECIDED, status)
        val selected = if (descriptor.status.get() == RootDescriptor.COMMITTED) {
            descriptor.after
        } else {
            descriptor.before
        }
        root.compareAndSet(descriptor, selected)
    }

    private fun entries(observed: Root<K, V>): Sequence<HamtEntry<K, V>> = sequence {
        suspend fun SequenceScope<HamtEntry<K, V>>.walk(node: INode<K, V>) {
            when (val main = readMain(node)) {
                is TNode -> if (main.entry != null) yield(HamtEntry(main.entry.key, main.entry.value))
                is LNode -> for (entry in main.entries) yield(HamtEntry(entry.key, entry.value))
                is CNode -> for (branch in main.branches) when (branch) {
                    is SNode -> yield(HamtEntry(branch.key, branch.value))
                    is INode -> walk(branch)
                }
            }
        }
        walk(observed.node)
    }

    internal fun validateStructureForTesting(): CtrieStatistics {
        val observed = readRoot()
        check(observed.node.generation === observed.generation) {
            "The root indirection node has the wrong generation."
        }
        val counters = ValidationCounters()
        validateNode(
            observed.node,
            shift = 0,
            isRoot = true,
            visited = mutableSetOf(),
            counters = counters,
        )
        return CtrieStatistics(
            counters.indirectionNodes,
            counters.collisionNodes,
            counters.tombNodes,
            counters.entries,
            counters.maxDepth,
        )
    }

    private fun validateNode(
        node: INode<K, V>,
        shift: Int,
        isRoot: Boolean,
        visited: MutableSet<INode<K, V>>,
        counters: ValidationCounters,
    ): List<Int> {
        check(visited.add(node)) { "The Ctrie contains an indirection-node cycle." }
        counters.indirectionNodes++
        counters.maxDepth = maxOf(counters.maxDepth, shift / 5)
        return when (val main = readMain(node)) {
            is TNode -> {
                check(!isRoot) { "The root indirection node contains a tomb." }
                counters.tombNodes++
                main.entry?.let {
                    counters.entries++
                    listOf(it.hash)
                } ?: emptyList()
            }
            is LNode -> {
                check(main.entries.size >= 2) { "A collision node contains fewer than two entries." }
                counters.collisionNodes++
                val hash = main.entries[0].hash
                main.entries.forEachIndexed { index, entry ->
                    check(entry.hash == hash) { "A collision node contains mixed full hashes." }
                    check(main.entries.take(index).none { policy.equivalent(it.key, entry.key) }) {
                        "A collision node contains equivalent duplicate keys."
                    }
                }
                counters.entries += main.entries.size
                main.entries.map { it.hash }
            }
            is CNode -> {
                check(Integer.bitCount(main.bitmap) == main.branches.size) {
                    "A C-node bitmap disagrees with its compact branch list."
                }
                check(isRoot || (main.branches.isNotEmpty() &&
                    (main.branches.size != 1 || main.branches[0] !is SNode))) {
                    "A non-root C-node was not contracted into a tomb."
                }
                check(main.branches.isEmpty() || shift < 32) {
                    "A C-node consumes more than 32 hash bits."
                }
                val hashes = mutableListOf<Int>()
                var position = 0
                for (index in 0 until 32) {
                    val bit = 1 shl index
                    if (main.bitmap and bit == 0) continue
                    val branch = main.branches[position++]
                    val branchHashes = when (branch) {
                        is SNode -> {
                            counters.entries++
                            listOf(branch.hash)
                        }
                        is INode -> validateNode(
                            branch,
                            shift + 5,
                            isRoot = false,
                            visited,
                            counters,
                        )
                    }
                    check(branchHashes.all { (it ushr shift) and 31 == index }) {
                        "A branch contains an entry in the wrong hash partition."
                    }
                    hashes.addAll(branchHashes)
                }
                hashes
            }
        }
    }

    public class Snapshot<K, V> internal constructor(
        private val owner: ConcurrentHashTrie<K, V>,
        private val root: Root<K, V>,
    ) : Iterable<HamtEntry<K, V>> {
        public val size: Int get() = count()
        public operator fun get(key: K): V? = owner.getEntry(root, key)?.value
        public fun getEntry(key: K): HamtEntry<K, V>? = owner.getEntry(root, key)
        public fun containsKey(key: K): Boolean = getEntry(key) != null
        public fun toPersistentHashMap(): PersistentHashMap<K, V> =
            PersistentHashMap.from(map { it.key to it.value }, owner.policy)
        override fun iterator(): Iterator<HamtEntry<K, V>> = owner.entries(root).iterator()
    }

    internal class Generation
    internal class Root<K, V>(val node: INode<K, V>, val generation: Generation)
    internal data class CtrieStatistics(
        val indirectionNodeCount: Int,
        val collisionNodeCount: Int,
        val tombNodeCount: Int,
        val entryCount: Int,
        val maxDepth: Int,
    )
    internal sealed interface Branch<K, V>
    internal sealed interface MainNode<K, V>
    internal class INode<K, V>(main: MainNode<K, V>, val generation: Generation) : Branch<K, V> {
        val main = AtomicReference<Any>(main)
    }
    private data class SNode<K, V>(val hash: Int, val key: K, val value: V) : Branch<K, V>
    private data class CNode<K, V>(val bitmap: Int, val branches: List<Branch<K, V>>) : MainNode<K, V> {
        fun insert(index: Int, bit: Int, branch: Branch<K, V>): CNode<K, V> =
            CNode(bitmap or bit, branches.toMutableList().also { it.add(index, branch) })
        fun replace(index: Int, branch: Branch<K, V>): CNode<K, V> =
            CNode(bitmap, branches.toMutableList().also { it[index] = branch })
        fun remove(index: Int, bit: Int): CNode<K, V> =
            CNode(bitmap and bit.inv(), branches.toMutableList().also { it.removeAt(index) })
        companion object {
            fun <K, V> empty(): CNode<K, V> = CNode(0, emptyList())
        }
    }
    private data class LNode<K, V>(val entries: List<SNode<K, V>>) : MainNode<K, V>
    private data class TNode<K, V>(val entry: SNode<K, V>?) : MainNode<K, V> {
        companion object {
            fun <K, V> empty(): TNode<K, V> = TNode(null)
        }
    }
    private class Descriptor<K, V>(
        val before: MainNode<K, V>,
        val after: MainNode<K, V>,
        val root: Root<K, V>,
    ) {
        val status = AtomicInteger(UNDECIDED)
        companion object { const val UNDECIDED = 0; const val COMMITTED = 1; const val ABORTED = 2 }
    }
    private class RootDescriptor<K, V>(
        val before: Root<K, V>,
        val expectedMain: MainNode<K, V>,
        val after: Root<K, V>,
    ) {
        val status = AtomicInteger(UNDECIDED)
        companion object { const val UNDECIDED = 0; const val COMMITTED = 1; const val ABORTED = 2 }
    }
    private enum class DecisionKind { NONE, SET, REMOVE }
    private data class Decision<V>(val kind: DecisionKind, val value: V?, val result: V?) {
        companion object {
            fun <V> none(): Decision<V> = Decision(DecisionKind.NONE, null, null)
            fun <V> returning(value: V?): Decision<V> = Decision(DecisionKind.NONE, null, value)
            fun <V> set(value: V): Decision<V> = Decision(DecisionKind.SET, value, value)
            fun <V> remove(value: V?): Decision<V> = Decision(DecisionKind.REMOVE, null, value)
        }
    }
    private data class MutationResult<V>(val changed: Boolean, val value: V?)
    private class ValidationCounters {
        var indirectionNodes: Int = 0
        var collisionNodes: Int = 0
        var tombNodes: Int = 0
        var entries: Int = 0
        var maxDepth: Int = 0
    }
}
