package tools.datastructures.hamt

import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** Lock-free mutable Ctrie with O(1) immutable generation snapshots. */
public class ConcurrentHashTrie<K, V>(
    public val policy: HashPolicy<K> = defaultHashPolicy(),
) : Iterable<HamtEntry<K, V>> {
    private val root: AtomicReference<Root<K, V>>
    private val revision = AtomicLong()

    init {
        val generation = Generation()
        root = AtomicReference(Root(INode(CNode.empty(), generation), generation))
    }

    public val generation: Long get() = revision.get()
    public val size: Int get() = snapshot().count()
    public val isEmpty: Boolean get() = snapshot().none()

    public operator fun get(key: K): V? = getEntry(key)?.value
    public fun getEntry(key: K): HamtEntry<K, V>? = getEntry(root.get(), key)
    public fun containsKey(key: K): Boolean = getEntry(key) != null

    public fun set(key: K, value: V) {
        mutate(key) { exists, current ->
            if (exists && current == value) Decision.none() else Decision.set(value)
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
            if (exists && current == next) Decision.returning(current) else Decision.set(next)
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
            val observed = root.get()
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
            val before = root.get()
            val main = readMain(before.node)
            val generation = Generation()
            val after = Root(INode(main, generation), generation)
            if (root.compareAndSet(before, after)) return Snapshot(this, before)
        }
    }

    override fun iterator(): Iterator<HamtEntry<K, V>> = snapshot().iterator()

    @Suppress("UNCHECKED_CAST")
    private fun mutate(
        key: K,
        transform: (Boolean, V?, K?) -> Decision<V>,
    ): MutationResult<V> {
        val hash = policy.hash(key)
        while (true) {
            val observed = root.get()
            var node = observed.node
            var shift = 0
            while (true) {
                when (val main = readMain(node)) {
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
                        val entries = main.entries.toMutableList()
                        when (decision.kind) {
                            DecisionKind.REMOVE -> entries.removeAt(index)
                            DecisionKind.SET -> {
                                val entry = SNode(hash, if (exists) storedKey!! else key, decision.value as V)
                                if (exists) entries[index] = entry else entries.add(entry)
                            }
                            DecisionKind.NONE -> error("handled above")
                        }
                        val replacement: MainNode<K, V> = when (entries.size) {
                            0 -> CNode.empty()
                            else -> LNode(entries.toList())
                        }
                        if (!gcas(node, main, replacement, observed)) break
                        revision.incrementAndGet()
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
                                        main.remove(position, bit)
                                    } else {
                                        main.replace(position, SNode(hash, branch.key, decision.value as V))
                                    }
                                    if (!gcas(node, main, replacement, observed)) break
                                    revision.incrementAndGet()
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
                                if (child.generation !== observed.generation) {
                                    val renewed = INode(readMain(child), observed.generation)
                                    if (!gcas(node, main, main.replace(position, renewed), observed)) break
                                    child = renewed
                                }
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

    private fun gcas(node: INode<K, V>, before: MainNode<K, V>, after: MainNode<K, V>, observed: Root<K, V>): Boolean {
        val descriptor = Descriptor(before, after, observed)
        if (!node.main.compareAndSet(before, descriptor)) return false
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
        val status = if (root.get() === descriptor.root) Descriptor.COMMITTED else Descriptor.ABORTED
        descriptor.status.compareAndSet(Descriptor.UNDECIDED, status)
    }

    private fun entries(observed: Root<K, V>): Sequence<HamtEntry<K, V>> = sequence {
        suspend fun SequenceScope<HamtEntry<K, V>>.walk(node: INode<K, V>) {
            when (val main = readMain(node)) {
                is LNode -> for (entry in main.entries) yield(HamtEntry(entry.key, entry.value))
                is CNode -> for (branch in main.branches) when (branch) {
                    is SNode -> yield(HamtEntry(branch.key, branch.value))
                    is INode -> walk(branch)
                }
            }
        }
        walk(observed.node)
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
    private class Descriptor<K, V>(
        val before: MainNode<K, V>,
        val after: MainNode<K, V>,
        val root: Root<K, V>,
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
}
