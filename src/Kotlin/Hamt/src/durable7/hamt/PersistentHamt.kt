/*
 * Persistent CHAMP hash map and hash set, with bulk builders and one-way editing sessions.
 *
 * Branching is 32-way and bitmap-indexed, with immutable collision buckets, so lookup, insertion,
 * and removal are constant time in expectation while versions share every unchanged node. Key
 * equivalence comes from a retained hash policy, and a no-op returns a collection sharing the
 * receiver's root.
 */
package durable7.hamt

/**
 * Hashing and equivalence policy for the persistent hash collections. A collection retains its policy, so equivalence
 * classes are defined by the policy rather than by the platform's own comparison.
 */
public interface HashPolicy<K> {
    /** A hash for the key. Keys the policy treats as equivalent must hash identically. */
    public fun hash(key: K): Int

    /** Whether the two keys belong to the same equivalence class. */
    public fun equivalent(left: K, right: K): Boolean
}

private object DefaultHashPolicy : HashPolicy<Any?> {
    override fun hash(key: Any?): Int = key?.hashCode() ?: 0
    override fun equivalent(left: Any?, right: Any?): Boolean = left == right
}

@Suppress("UNCHECKED_CAST")
/** The shared policy using the platform's own hashing and equality. */
public fun <K> defaultHashPolicy(): HashPolicy<K> = DefaultHashPolicy as HashPolicy<K>

private fun <T> hamtValuesEqual(left: T, right: T): Boolean = left === right || left == right

/** Raised when a strict insertion sees an equivalent key. */
public class DuplicateKeyException(message: String = "An equivalent key is already present.") :
    IllegalArgumentException(message)

/** One stored key representative and its value. */
public data class HamtEntry<K, V>(public val key: K, public val value: V)

/** The outcome of a non-overwriting insertion; the collection is the receiver when nothing was added. */
public data class AddResult<T>(public val value: T, public val added: Boolean)

/** The map produced by a factory update together with the value actually stored. */
public data class MapValueResult<K, V>(
    public val map: PersistentHashMap<K, V>,
    public val value: V,
)

/** The map remaining after a removal, together with the removed value. */
public data class MapRemoveResult<K, V>(public val map: PersistentHashMap<K, V>, public val value: V)

/** The map remaining after a removal, together with the removed key representative and value. */
public data class MapRemoveEntryResult<K, V>(
    public val map: PersistentHashMap<K, V>,
    public val entry: HamtEntry<K, V>,
)

/** The set remaining after a removal, together with the removed representative. */
public data class SetRemoveResult<T>(public val set: PersistentHashSet<T>, public val value: T)

/** Which side of a diff an entry falls on. */
public enum class MapDifferenceKind { ADDED, REMOVED, CHANGED }

/** One entry-level difference between two maps. */
public data class MapDifference<K, V>(
    public val kind: MapDifferenceKind,
    public val key: K,
    public val before: V?,
    public val after: V?,
)

/** Node and entry measurements returned by a successful structural audit. */
internal data class ChampStatistics(
    val inlinePayloads: Int,
    val bitmapNodes: Int,
    val collisionPayloads: Int,
    val invalidLeafChildren: Int,
    val underfullBitmapNodes: Int,
    val invalidHashRouting: Int,
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
private data class FactoryUpdateResult<K, V>(
    val node: Node<K, V>,
    val added: Boolean,
    val selected: V,
)

private const val ConsumedTransientMessage: String =
    "This transient editing session has already been persisted."
private const val ReentrantTransientMutationMessage: String =
    "Transient editing sessions do not allow reentrant mutation or publication."

private class VersionBoundIterator<T>(
    private val source: Iterator<T>,
    private val validate: () -> Unit,
) : Iterator<T> {
    override fun hasNext(): Boolean {
        validate()
        return source.hasNext()
    }

    override fun next(): T {
        validate()
        return source.next()
    }
}

/**
 * A persistent hash map backed by a CHAMP trie. Branching is 32-way and bitmap-indexed with immutable collision
 * buckets, so operations are constant time in expectation while versions share every unchanged node.
 */
public class PersistentHashMap<K, V> private constructor(
    private val root: Node<K, V>?,
    public val size: Int,
    public val policy: HashPolicy<K>,
) : Iterable<HamtEntry<K, V>> {
    public companion object {
        /**
         * An empty map using [policy] for hashing and equality, which the map retains by identity. Operations
         * between two maps require the same policy object.
         */
        public fun <K, V> empty(policy: HashPolicy<K> = defaultHashPolicy()): PersistentHashMap<K, V> =
            PersistentHashMap(null, 0, policy)

        /** Creates an active one-way editing session over an empty map. */
        public fun <K, V> createTransient(
            policy: HashPolicy<K> = defaultHashPolicy(),
        ): Transient<K, V> = Transient(empty(policy))

        /** A map holding every pair, using [policy]. Later pairs replace earlier ones with an equal key. */
        public fun <K, V> from(
            items: Iterable<Pair<K, V>>,
            policy: HashPolicy<K> = defaultHashPolicy(),
        ): PersistentHashMap<K, V> = empty<K, V>(policy).setItems(items)
    }

    /** Whether the map holds no entries. */
    public val isEmpty: Boolean get() = size == 0

    /**
     * Adopts this map into a one-way editing session in O(1) time.
     *
     * The Kotlin session is a lifecycle facade over persistent path-copying operations. It does not
     * mutate CHAMP nodes in place and makes no allocation or throughput improvement claim.
     */
    public fun toTransient(): Transient<K, V> = Transient(this)

    /**
     * Whether this map and [other] share a root, meaning one was derived from the other with no intervening change.
     * A cheap sufficient test for equality, not a necessary one: two equal maps built independently do not share a
     * root.
     */
    public fun sharesRootWith(other: PersistentHashMap<K, V>): Boolean = root === other.root
    /** Whether an entry with a key equal to [key] under the policy is present. */
    public fun containsKey(key: K): Boolean = getEntry(key) != null
    /**
     * The value stored for [key], or `null` when absent. A stored `null` value is therefore indistinguishable from
     * a miss here; use [getEntry] or [containsKey] when that matters.
     */
    public operator fun get(key: K): V? = getEntry(key)?.value

    /**
     * The stored entry for [key], or `null` when absent. The entry carries the *stored* key representative, which
     * may not be the instance passed in: the first representative of each equivalence class is the one kept.
     */
    public fun getEntry(key: K): HamtEntry<K, V>? {
        val current = root ?: return null
        return getInNode(current, policy.hash(key), key, 0, policy)
    }

    /**
     * A map with [key] bound to [value], adding or replacing as needed. Returns the receiver unchanged when the
     * binding is already present and equal.
     */
    public fun put(key: K, value: V): PersistentHashMap<K, V> {
        val hash = policy.hash(key)
        val current = root ?: return PersistentHashMap(Leaf(hash, key, value), 1, policy)
        val result = insertNode(current, hash, key, value, 0, overwrite = true, policy)
        if (!result.changed) return this
        return PersistentHashMap(result.node, size + if (result.added) 1 else 0, policy)
    }

    /**
     * A map with the entry added.
     *
     * @throws DuplicateKeyException if an equal key is already present; use [tryAdd] to detect that without an
     * exception, or [put] to overwrite.
     */
    public fun add(key: K, value: V): PersistentHashMap<K, V> {
        val result = tryAdd(key, value)
        if (!result.added) throw DuplicateKeyException()
        return result.value
    }

    /**
     * Add the entry only when [key] is absent, reporting whether it was added. On a collision the result carries
     * the receiver unchanged.
     */
    public fun tryAdd(key: K, value: V): AddResult<PersistentHashMap<K, V>> {
        val hash = policy.hash(key)
        val current = root
            ?: return AddResult(PersistentHashMap(Leaf(hash, key, value), 1, policy), true)
        val result = insertNode(current, hash, key, value, 0, overwrite = false, policy)
        if (result.duplicate) return AddResult(this, false)
        return AddResult(PersistentHashMap(result.node, size + if (result.added) 1 else 0, policy), result.added)
    }

    /**
     * Returns this map and its stored value on a hit, or invokes [addFactory] exactly once and
     * returns a successor containing the caller's key on a miss.
     *
     * Kotlin's non-null function parameter is checked at the JVM method boundary before this body
     * hashes [key]. The operation computes one hash and performs one recursive CHAMP descent.
     */
    public fun getOrAdd(key: K, addFactory: (K) -> V): MapValueResult<K, V> =
        applyFactoryUpdate(key, addFactory, null)

    /**
     * Invokes exactly one selected factory in one hashed CHAMP descent. On a hit [updateFactory]
     * receives the caller's lookup key and stored value; the stored key representative remains in
     * the map. An equal result retains and returns the stored value instance and this map.
     */
    public fun addOrUpdate(
        key: K,
        addFactory: (K) -> V,
        updateFactory: (K, V) -> V,
    ): MapValueResult<K, V> = applyFactoryUpdate(key, addFactory, updateFactory)

    private fun applyFactoryUpdate(
        key: K,
        addFactory: (K) -> V,
        updateFactory: ((K, V) -> V)?,
    ): MapValueResult<K, V> {
        val hash = policy.hash(key)
        val current = root
        if (current == null) {
            val selected = addFactory(key)
            return MapValueResult(PersistentHashMap(Leaf(hash, key, selected), 1, policy), selected)
        }

        val result = factoryUpdateNode(current, hash, key, 0, policy, addFactory, updateFactory)
        if (result.node === current) return MapValueResult(this, result.selected)
        val nextSize = if (result.added) Math.addExact(size, 1) else size
        return MapValueResult(PersistentHashMap(result.node, nextSize, policy), result.selected)
    }

    /** A map with every pair applied in order, each adding or replacing as [put] does. */
    public fun setItems(items: Iterable<Pair<K, V>>): PersistentHashMap<K, V> {
        var result = this
        for ((key, value) in items) result = result.put(key, value)
        return result
    }

    /**
     * A map without the entry for [key]. Returns the receiver unchanged when absent; use [tryRemove] to recover the
     * removed value.
     */
    public fun remove(key: K): PersistentHashMap<K, V> = tryRemove(key)?.map ?: this
    /**
     * Remove the entry for [key], returning the resulting map together with the value that was removed, or `null`
     * when the key is absent. The `null` here means "no such key" and is unaffected by a stored `null` value.
     */
    public fun tryRemove(key: K): MapRemoveResult<K, V>? {
        val entry = tryRemoveEntry(key) ?: return null
        return MapRemoveResult(entry.map, entry.entry.value)
    }

    /**
     * Like [tryRemove], but returns the whole removed entry, so the stored key representative is recoverable and
     * not just the value.
     */
    public fun tryRemoveEntry(key: K): MapRemoveEntryResult<K, V>? {
        val current = root ?: return null
        val result = removeNode(current, policy.hash(key), key, 0, policy)
        if (!result.changed) return null
        return MapRemoveEntryResult(
            PersistentHashMap(result.node, size - 1, policy),
            checkNotNull(result.removed),
        )
    }

    /** An empty map retaining the same policy. Returns the receiver when it is already empty. */
    public fun clear(): PersistentHashMap<K, V> = if (isEmpty) this else PersistentHashMap(null, 0, policy)

    /**
     * The entries of both maps, with [other]'s values winning on an equal key.
     *
     * @throws IllegalArgumentException if the two maps do not retain the same policy object. Set algebra over maps
     * hashed under different policies would compare keys inconsistently, so it is rejected rather than coerced.
     */
    public fun union(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> = combine(other, ChampOperation.UNION)
    /**
     * The entries whose keys appear in both maps.
     *
     * @throws IllegalArgumentException if the two maps do not retain the same policy object.
     */
    public fun intersect(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> = combine(other, ChampOperation.INTERSECT)
    /**
     * The entries of this map whose keys are absent from [other].
     *
     * @throws IllegalArgumentException if the two maps do not retain the same policy object.
     */
    public fun except(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> = combine(other, ChampOperation.EXCEPT)
    /**
     * The entries whose keys appear in exactly one of the two maps.
     *
     * @throws IllegalArgumentException if the two maps do not retain the same policy object.
     */
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

    internal fun champStatistics(): ChampStatistics =
        root?.let { champStatistics(it, 0, 0, 0) } ?: ChampStatistics(0, 0, 0, 0, 0, 0)
    internal fun <V2> champTopologyEquals(other: PersistentHashMap<K, V2>): Boolean =
        policy === other.policy && champTopologyEqual(root, other.root, policy)

    /** Semantic equality for maps retaining the same hash/equality policy object. */
    public fun mapEquals(other: PersistentHashMap<K, V>): Boolean {
        require(policy === other.policy) { "Maps must retain the same hash policy object." }
        if (root === other.root) return true
        return size == other.size && champNodesEqual(root, other.root, policy)
    }

    /** Reports additions, removals, and value changes between two policy-compatible maps. */
    public fun diff(other: PersistentHashMap<K, V>): Sequence<MapDifference<K, V>> = sequence {
        require(policy === other.policy) { "Maps must retain the same hash policy object." }
        diffChampNodes(root, other.root, 0, policy)
    }

    /**
     * The entries, in an order determined by the trie shape rather than by insertion or key order. Lazy: nodes are
     * visited as the sequence is consumed.
     */
    public fun entries(): Sequence<HamtEntry<K, V>> = sequence {
        val current = root ?: return@sequence
        yieldEntries(current)
    }
    /** The stored key representatives, in the same order as [entries]. */
    public fun keys(): Sequence<K> = entries().map { it.key }
    /** The values, in the same order as [entries]. */
    public fun values(): Sequence<V> = entries().map { it.value }
    override fun iterator(): Iterator<HamtEntry<K, V>> = entries().iterator()

    /**
     * A mutable-looking, single-owner editing session with one-way O(1) publication.
     *
     * Each successful edit still invokes the ordinary persistent map operation and therefore
     * path-copies the affected CHAMP path. A callback failure occurs before the session reference is
     * replaced, leaving the active session unchanged. The session is unsynchronized; callers must
     * externally serialize access and policy callbacks must not reenter mutation or publication.
     */
    public class Transient<K, V> internal constructor(source: PersistentHashMap<K, V>) :
        Iterable<HamtEntry<K, V>> {
        private var current: PersistentHashMap<K, V>? = source
        private var version: Int = 0
        private var mutationInProgress: Boolean = false

        /**
         * Number of entries currently in the session.
         *
         * @throws IllegalStateException if the session has already been published.
         */
        public val size: Int get() = active().size
        /**
         * Whether the session holds no entries.
         *
         * @throws IllegalStateException if the session has already been published.
         */
        public val isEmpty: Boolean get() = active().isEmpty
        /** The hash policy the session inherited from the map it was created over. */
        public val policy: HashPolicy<K> get() = active().policy

        /** Whether an entry with a key equal to [key] is present in the session. */
        public fun containsKey(key: K): Boolean = active().containsKey(key)
        /** The value stored for [key] in the session, or `null` when absent or stored as `null`. */
        public operator fun get(key: K): V? = active()[key]
        /** The stored entry for [key] in the session, or `null` when absent. */
        public fun getEntry(key: K): HamtEntry<K, V>? = active().getEntry(key)

        /** Sets a key/value pair and reports whether the logical map changed. */
        public fun put(key: K, value: V): Boolean = mutate { before ->
            publish(before, before.put(key, value))
        }

        /** Kotlin indexed-assignment spelling for [put]; the change result is intentionally discarded. */
        public operator fun set(key: K, value: V): Unit {
            put(key, value)
        }

        /** Adds a key/value pair or throws [DuplicateKeyException] for an equivalent key. */
        public fun add(key: K, value: V): Unit = mutate { before ->
            publish(before, before.add(key, value))
        }

        /** Adds a key/value pair if absent and reports whether it was added. */
        public fun tryAdd(key: K, value: V): Boolean = mutate { before ->
            val result = before.tryAdd(key, value)
            publish(before, result.value)
            result.added
        }

        /** Removes an equivalent key and reports whether an entry was removed. */
        public fun remove(key: K): Boolean = tryRemove(key) != null

        /** Removes and returns the stored key/value representatives, or `null` on a miss. */
        public fun tryRemove(key: K): HamtEntry<K, V>? = mutate { before ->
            val result = before.tryRemoveEntry(key) ?: return@mutate null
            publish(before, result.map)
            result.entry
        }

        /** Clears the active map. Clearing an empty map is an exact logical no-op. */
        public fun clear(): Unit = mutate { before ->
            publish(before, before.clear())
        }

        /**
         * The session's entries. The returned sequence is bound to the session version current when it was created,
         * so an edit made while it is being consumed fails the iteration rather than yielding a mixture of two
         * versions.
         */
        public fun entries(): Sequence<HamtEntry<K, V>> {
            val snapshot = active()
            val expectedVersion = version
            return Sequence {
                VersionBoundIterator(snapshot.iterator()) { validateIterator(expectedVersion) }
            }
        }
        /** The stored key representatives, in the same order as [entries]. */
        public fun keys(): Sequence<K> = entries().map { it.key }
        /** The values, in the same order as [entries]. */
        public fun values(): Sequence<V> = entries().map { it.value }

        override fun iterator(): Iterator<HamtEntry<K, V>> {
            val snapshot = active()
            val expectedVersion = version
            return VersionBoundIterator(snapshot.iterator()) { validateIterator(expectedVersion) }
        }

        /**
         * Publishes the current persistent map by reference in O(1) time and consumes this session.
         * A session containing only logical no-ops returns the exact adopted map object.
         */
        public fun persist(): PersistentHashMap<K, V> {
            val result = active()
            if (mutationInProgress) throw IllegalStateException(ReentrantTransientMutationMessage)
            current = null
            version++
            return result
        }

        private fun active(): PersistentHashMap<K, V> =
            current ?: throw IllegalStateException(ConsumedTransientMessage)

        private inline fun <R> mutate(operation: (PersistentHashMap<K, V>) -> R): R {
            val before = active()
            if (mutationInProgress) throw IllegalStateException(ReentrantTransientMutationMessage)
            mutationInProgress = true
            return try {
                operation(before)
            } finally {
                mutationInProgress = false
            }
        }

        private fun publish(
            before: PersistentHashMap<K, V>,
            candidate: PersistentHashMap<K, V>,
        ): Boolean {
            if (candidate === before) return false
            current = candidate
            version++
            return true
        }

        private fun validateIterator(expectedVersion: Int) {
            active()
            if (version != expectedVersion) {
                throw ConcurrentModificationException("The transient map changed during iteration.")
            }
        }
    }
}

/**
 * A persistent hash set backed by the same CHAMP trie, retaining the first representative of each equivalence class.
 */
public class PersistentHashSet<T> private constructor(private val map: PersistentHashMap<T, Unit>) : Iterable<T> {
    public companion object {
        /**
         * An empty set using [policy] for hashing and equality, which the set retains by identity. Operations
         * between two sets require the same policy object.
         */
        public fun <T> empty(policy: HashPolicy<T> = defaultHashPolicy()): PersistentHashSet<T> =
            PersistentHashSet(PersistentHashMap.empty(policy))

        /** Creates an active one-way editing session over an empty set. */
        public fun <T> createTransient(
            policy: HashPolicy<T> = defaultHashPolicy(),
        ): Transient<T> = Transient(empty(policy))

        /**
         * A set holding the distinct values, using [policy]. The first representative of each equivalence class is
         * the one kept.
         */
        public fun <T> from(values: Iterable<T>, policy: HashPolicy<T> = defaultHashPolicy()): PersistentHashSet<T> =
            empty<T>(policy).union(values)
    }

    /** Number of elements. */
    public val size: Int get() = map.size

    /** Whether the set holds no elements. */
    public val isEmpty: Boolean get() = map.isEmpty

    /** The retained policy defining equivalence. */
    public val policy: HashPolicy<T> get() = map.policy

    /**
     * Adopts this set into a one-way editing session in O(1) time.
     * Point edits retain the persistent set's path-copying implementation.
     */
    public fun toTransient(): Transient<T> = Transient(this)

    /**
     * Whether both sets reference the same trie root. A representation test used to confirm a no-op avoided copying,
     * not an equality test.
     */
    public fun sharesRootWith(other: PersistentHashSet<T>): Boolean = map.sharesRootWith(other.map)

    /** Whether the element is present. */
    public fun contains(value: T): Boolean = map.containsKey(value)

    /**
     * The stored representative equivalent to the probe, or `null` when absent. Distinct from `contains` because the
     * set keeps the first representative of each equivalence class, which need not be the value passed in.
     */
    public fun get(value: T): T? = map.getEntry(value)?.key

    /** A set containing the given element; returns the receiver when already present. */
    public fun add(value: T): PersistentHashSet<T> = withMap(map.add(value, Unit))

    /** Add the element, reporting whether it was added rather than throwing on a duplicate. */
    public fun tryAdd(value: T): AddResult<PersistentHashSet<T>> {
        val result = map.tryAdd(value, Unit)
        return AddResult(withMap(result.value), result.added)
    }

    /**
     * A set holding the given value as the representative of its equivalence class, replacing any representative
     * already there.
     */
    public fun put(value: T): PersistentHashSet<T> = withMap(map.put(value, Unit))

    /** A set without that element; returns the receiver when absent. */
    public fun remove(value: T): PersistentHashSet<T> = withMap(map.remove(value))

    /** Remove the element and report the stored representative, or nothing when absent. */
    public fun tryRemove(value: T): SetRemoveResult<T>? {
        val removed = map.tryRemoveEntry(value) ?: return null
        return SetRemoveResult(withMap(removed.map), removed.entry.key)
    }

    /** An empty set retaining the same policies; returns the receiver when already empty. */
    public fun clear(): PersistentHashSet<T> = withMap(map.clear())

    /**
     * The elements of both sets. Subtrees the operands already share are adopted whole rather than re-entered. The
     * operand is any iterable, whose elements are read through this collection's own policy rather than their own.
     */
    public fun union(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) result = result.put(value)
        return result
    }

    /** The elements of both sets. Subtrees the operands already share are adopted whole rather than re-entered. */
    public fun union(other: PersistentHashSet<T>): PersistentHashSet<T> = withMap(map.union(other.map))

    /**
     * The elements present in both sets. The operand is any iterable, whose elements are read through this collection's
     * own policy rather than their own.
     */
    public fun intersect(values: Iterable<T>): PersistentHashSet<T> {
        val probe = empty<T>(policy).union(values)
        var result = empty<T>(policy)
        for (value in this) if (probe.contains(value)) result = result.put(value)
        return result
    }

    /** The elements present in both sets. */
    public fun intersect(other: PersistentHashSet<T>): PersistentHashSet<T> = withMap(map.intersect(other.map))

    /**
     * This set's elements that are absent from the other. The operand is any iterable, whose elements are read through
     * this collection's own policy rather than their own.
     */
    public fun except(values: Iterable<T>): PersistentHashSet<T> {
        var result = this
        for (value in values) result = result.remove(value)
        return result
    }

    /** This set's elements that are absent from the other. */
    public fun except(other: PersistentHashSet<T>): PersistentHashSet<T> = withMap(map.except(other.map))

    /**
     * The elements present in exactly one of the two sets. The operand is any iterable, whose elements are read through
     * this collection's own policy rather than their own.
     */
    public fun symmetricExcept(values: Iterable<T>): PersistentHashSet<T> {
        val distinct = empty<T>(policy).union(values)
        var result = this
        for (value in distinct) result = if (result.contains(value)) result.remove(value) else result.put(value)
        return result
    }

    /** The elements present in exactly one of the two sets. */
    public fun symmetricExcept(other: PersistentHashSet<T>): PersistentHashSet<T> =
        withMap(map.symmetricExcept(other.map))

    /** Whether every element of this set also occurs in the other. */
    public fun isSubsetOf(other: PersistentHashSet<T>): Boolean {
        if (policy !== other.policy) return isSubsetOf(other as Iterable<T>)
        return size <= other.size && map.intersect(other.map).sharesRootWith(map)
    }

    /** Whether this set is a subset of the other and the other holds an element it lacks. */
    public fun isProperSubsetOf(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) size < other.size && isSubsetOf(other) else isProperSubsetOf(other as Iterable<T>)

    /** Whether every element of the other occurs in this set. */
    public fun isSupersetOf(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) other.isSubsetOf(this) else isSupersetOf(other as Iterable<T>)

    /** Whether this set is a superset of the other and holds an element the other lacks. */
    public fun isProperSupersetOf(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) size > other.size && other.isSubsetOf(this) else isProperSupersetOf(other as Iterable<T>)

    /** Whether the two sets share at least one element. */
    public fun overlaps(other: PersistentHashSet<T>): Boolean {
        if (policy !== other.policy) return overlaps(other as Iterable<T>)
        return map.intersect(other.map).size != 0
    }

    /** Whether both sets hold the same elements. */
    public fun setEquals(other: PersistentHashSet<T>): Boolean =
        if (policy === other.policy) map.mapEquals(other.map) else setEquals(other as Iterable<T>)

    /**
     * Whether every element of this set also occurs in the other. The operand is any iterable, whose elements are read
     * through this collection's own policy rather than their own.
     */
    public fun isSubsetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return all { probe.contains(it) }
    }

    /**
     * Whether every element of the other occurs in this set. The operand is any iterable, whose elements are read
     * through this collection's own policy rather than their own.
     */
    public fun isSupersetOf(values: Iterable<T>): Boolean = values.all { contains(it) }

    /**
     * Whether this set is a subset of the other and the other holds an element it lacks. The operand is any iterable,
     * whose elements are read through this collection's own policy rather than their own.
     */
    public fun isProperSubsetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return size < probe.size && all { probe.contains(it) }
    }

    /**
     * Whether this set is a superset of the other and holds an element the other lacks. The operand is any iterable,
     * whose elements are read through this collection's own policy rather than their own.
     */
    public fun isProperSupersetOf(values: Iterable<T>): Boolean {
        val probe = empty<T>(policy).union(values)
        return size > probe.size && probe.all { contains(it) }
    }

    /**
     * Whether the two sets share at least one element. The operand is any iterable, whose elements are read through
     * this collection's own policy rather than their own.
     */
    public fun overlaps(values: Iterable<T>): Boolean = values.any { contains(it) }

    /**
     * Whether both sets hold the same elements. The operand is any iterable, whose elements are read through this
     * collection's own policy rather than their own.
     */
    public fun setEquals(values: Iterable<T>): Boolean {
        val other = empty<T>(policy).union(values)
        return size == other.size && all { other.contains(it) }
    }

    /** Iterate the elements lazily, in canonical trie order. */
    public fun asSequence(): Sequence<T> = map.keys()

    /** Iterate the elements. */
    override fun iterator(): Iterator<T> = asSequence().iterator()

    /** Mutable-set facade over persistent CHAMP successors with one-way O(1) publication. */
    public class Transient<T> internal constructor(source: PersistentHashSet<T>) : Iterable<T> {
        private var current: PersistentHashSet<T>? = source
        private var version: Int = 0
        private var mutationInProgress: Boolean = false

        /** Number of elements. */
        public val size: Int get() = active().size

        /** Whether the collection holds no elements. */
        public val isEmpty: Boolean get() = active().isEmpty

        /** The retained policy defining equivalence. */
        public val policy: HashPolicy<T> get() = active().policy

        /** Whether the element is present. */
        public fun contains(value: T): Boolean = active().contains(value)

        /**
         * Returns the first stored representative equivalent to [value], or `null` on a miss.
         * For nullable element types, use [contains] to distinguish a stored `null` from a miss.
         */
        public fun get(value: T): T? = active().get(value)

        /** Adds [value] if absent and reports whether the set changed. */
        public fun add(value: T): Boolean = mutate { before ->
            val result = before.tryAdd(value)
            publish(before, result.value)
            result.added
        }

        /** Removes an equivalent value and reports whether the set changed. */
        public fun remove(value: T): Boolean = mutate { before ->
            val result = before.tryRemove(value) ?: return@mutate false
            publish(before, result.set)
            true
        }

        /** Clears the active set. Clearing an empty set is an exact logical no-op. */
        public fun clear(): Unit = mutate { before ->
            publish(before, before.clear())
        }

        /** Uses this session's retained policy to test whether all active values occur in [values]. */
        public fun isSubsetOf(values: Iterable<T>): Boolean = active().isSubsetOf(values)

        /** Uses this session's retained policy to test strict subset membership. */
        public fun isProperSubsetOf(values: Iterable<T>): Boolean = active().isProperSubsetOf(values)

        /** Uses this session's retained policy to test whether it contains every value in [values]. */
        public fun isSupersetOf(values: Iterable<T>): Boolean = active().isSupersetOf(values)

        /** Uses this session's retained policy to test strict superset membership. */
        public fun isProperSupersetOf(values: Iterable<T>): Boolean = active().isProperSupersetOf(values)

        /** Uses this session's retained policy to test whether any equivalent value overlaps. */
        public fun overlaps(values: Iterable<T>): Boolean = active().overlaps(values)

        /** Uses this session's retained policy to compare distinct equivalence classes. */
        public fun setEquals(values: Iterable<T>): Boolean = active().setEquals(values)

        /** Iterate the session's current elements lazily. */
        public fun asSequence(): Sequence<T> {
            val snapshot = active()
            val expectedVersion = version
            return Sequence {
                VersionBoundIterator(snapshot.iterator()) { validateIterator(expectedVersion) }
            }
        }

        /** Iterate the elements. */
        override fun iterator(): Iterator<T> {
            val snapshot = active()
            val expectedVersion = version
            return VersionBoundIterator(snapshot.iterator()) { validateIterator(expectedVersion) }
        }

        /**
         * Publishes the current persistent set by reference in O(1) time and consumes this session.
         * A session containing only logical no-ops returns the exact adopted set object.
         */
        public fun persist(): PersistentHashSet<T> {
            val result = active()
            if (mutationInProgress) throw IllegalStateException(ReentrantTransientMutationMessage)
            current = null
            version++
            return result
        }

        private fun active(): PersistentHashSet<T> =
            current ?: throw IllegalStateException(ConsumedTransientMessage)

        private inline fun <R> mutate(operation: (PersistentHashSet<T>) -> R): R {
            val before = active()
            if (mutationInProgress) throw IllegalStateException(ReentrantTransientMutationMessage)
            mutationInProgress = true
            return try {
                operation(before)
            } finally {
                mutationInProgress = false
            }
        }

        private fun publish(
            before: PersistentHashSet<T>,
            candidate: PersistentHashSet<T>,
        ): Boolean {
            if (candidate === before) return false
            current = candidate
            version++
            return true
        }

        private fun validateIterator(expectedVersion: Int) {
            active()
            if (version != expectedVersion) {
                throw ConcurrentModificationException("The transient set changed during iteration.")
            }
        }
    }

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
        if (hamtValuesEqual(node.value, value)) return InsertResult(node, false, false, false)
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
    if (hamtValuesEqual(node.entries[index].value, value)) return InsertResult(node, false, false, false)
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
            if (hamtValuesEqual(leaf.value, value)) return InsertResult(node, false, false, false)
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

private data class FactoryValueSelection<V>(
    val changed: Boolean,
    val selected: V,
)

private fun <K, V> selectFactoryValue(
    key: K,
    stored: V,
    updateFactory: ((K, V) -> V)?,
): FactoryValueSelection<V> {
    if (updateFactory == null) return FactoryValueSelection(false, stored)
    val selected = updateFactory(key, stored)
    return if (hamtValuesEqual(stored, selected)) {
        FactoryValueSelection(false, stored)
    } else {
        FactoryValueSelection(true, selected)
    }
}

private fun <K, V> factoryUpdateNode(
    node: Node<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
    addFactory: (K) -> V,
    updateFactory: ((K, V) -> V)?,
): FactoryUpdateResult<K, V> = when (node) {
    is Leaf -> factoryUpdateLeaf(node, hash, key, shift, policy, addFactory, updateFactory)
    is Collision -> factoryUpdateCollision(node, hash, key, shift, policy, addFactory, updateFactory)
    is BitmapNode -> factoryUpdateBitmap(node, hash, key, shift, policy, addFactory, updateFactory)
}

private fun <K, V> factoryUpdateLeaf(
    node: Leaf<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
    addFactory: (K) -> V,
    updateFactory: ((K, V) -> V)?,
): FactoryUpdateResult<K, V> {
    if (node.hash == hash && policy.equivalent(node.key, key)) {
        val selection = selectFactoryValue(key, node.value, updateFactory)
        return if (selection.changed) {
            FactoryUpdateResult(Leaf(hash, node.key, selection.selected), false, selection.selected)
        } else {
            FactoryUpdateResult(node, false, selection.selected)
        }
    }

    val selected = addFactory(key)
    return FactoryUpdateResult(
        mergeNodes(node, node.hash, Leaf(hash, key, selected), hash, shift),
        true,
        selected,
    )
}

private fun <K, V> factoryUpdateCollision(
    node: Collision<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
    addFactory: (K) -> V,
    updateFactory: ((K, V) -> V)?,
): FactoryUpdateResult<K, V> {
    if (node.hash != hash) {
        val selected = addFactory(key)
        return FactoryUpdateResult(
            mergeNodes(node, node.hash, Leaf(hash, key, selected), hash, shift),
            true,
            selected,
        )
    }

    val index = node.entries.indexOfFirst { policy.equivalent(it.key, key) }
    if (index < 0) {
        val selected = addFactory(key)
        return FactoryUpdateResult(
            Collision(hash, node.entries + Leaf(hash, key, selected)),
            true,
            selected,
        )
    }

    val stored = node.entries[index]
    val selection = selectFactoryValue(key, stored.value, updateFactory)
    if (!selection.changed) return FactoryUpdateResult(node, false, selection.selected)
    val entries = node.entries.toMutableList()
    entries[index] = Leaf(hash, stored.key, selection.selected)
    return FactoryUpdateResult(Collision(hash, entries.toList()), false, selection.selected)
}

private fun <K, V> factoryUpdateBitmap(
    node: BitmapNode<K, V>,
    hash: Int,
    key: K,
    shift: Int,
    policy: HashPolicy<K>,
    addFactory: (K) -> V,
    updateFactory: ((K, V) -> V)?,
): FactoryUpdateResult<K, V> {
    val bit = bitPosition(hashFragment(hash, shift))
    if (node.dataMap and bit != 0) {
        val index = sparseIndex(node.dataMap, bit)
        val stored = node.data[index]
        if (stored.hash == hash && policy.equivalent(stored.key, key)) {
            val selection = selectFactoryValue(key, stored.value, updateFactory)
            if (!selection.changed) return FactoryUpdateResult(node, false, selection.selected)
            return FactoryUpdateResult(
                node.copy(data = replaced(node.data, index, Leaf(hash, stored.key, selection.selected))),
                false,
                selection.selected,
            )
        }

        val selected = addFactory(key)
        val child = mergeNodes(stored, stored.hash, Leaf(hash, key, selected), hash, shift + BitsPerLevel)
        return FactoryUpdateResult(
            BitmapNode(
                node.dataMap and bit.inv(),
                node.nodeMap or bit,
                removed(node.data, index),
                inserted(node.nodes, sparseIndex(node.nodeMap, bit), child),
            ),
            true,
            selected,
        )
    }

    if (node.nodeMap and bit != 0) {
        val index = sparseIndex(node.nodeMap, bit)
        val child = factoryUpdateNode(
            node.nodes[index],
            hash,
            key,
            shift + BitsPerLevel,
            policy,
            addFactory,
            updateFactory,
        )
        if (child.node === node.nodes[index]) return FactoryUpdateResult(node, child.added, child.selected)
        return FactoryUpdateResult(
            node.copy(nodes = replaced(node.nodes, index, child.node)),
            child.added,
            child.selected,
        )
    }

    val selected = addFactory(key)
    return FactoryUpdateResult(
        node.copy(
            dataMap = node.dataMap or bit,
            data = inserted(node.data, sparseIndex(node.dataMap, bit), Leaf(hash, key, selected)),
        ),
        true,
        selected,
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

/**
 * Compares canonical CHAMP nodes in bitmap-slot lockstep.
 *
 * Reference-identical nodes and leaves are accepted before policy or value equality is invoked, so
 * lineage-related maps pay comparison cost only on divergent paths. Collision entry order remains
 * unobservable and is matched through the retained key policy.
 */
private fun <K, V> champNodesEqual(
    left: Node<K, V>?,
    right: Node<K, V>?,
    policy: HashPolicy<K>,
): Boolean {
    if (left === right) return true
    if (left == null || right == null) return false
    return when {
        left is Leaf && right is Leaf ->
            left.hash == right.hash &&
                policy.equivalent(left.key, right.key) &&
                hamtValuesEqual(left.value, right.value)
        left is Collision && right is Collision ->
            left.hash == right.hash && collisionEntriesEqual(left.entries, right.entries, policy)
        left is BitmapNode && right is BitmapNode ->
            left.dataMap == right.dataMap &&
                left.nodeMap == right.nodeMap &&
                left.data.size == right.data.size &&
                left.nodes.size == right.nodes.size &&
                left.data.indices.all { index ->
                    champNodesEqual(left.data[index], right.data[index], policy)
                } &&
                left.nodes.indices.all { index ->
                    champNodesEqual(left.nodes[index], right.nodes[index], policy)
                }
        else -> false
    }
}

private fun <K, V> collisionEntriesEqual(
    left: List<Leaf<K, V>>,
    right: List<Leaf<K, V>>,
    policy: HashPolicy<K>,
): Boolean {
    if (left.size != right.size) return false
    val matchedRight = BooleanArray(right.size)
    for (leftEntry in left) {
        val rightIndex = matchingLeafIndex(leftEntry, right, matchedRight, policy)
        if (rightIndex < 0) return false
        matchedRight[rightIndex] = true
        val rightEntry = right[rightIndex]
        if (leftEntry !== rightEntry && !hamtValuesEqual(leftEntry.value, rightEntry.value)) return false
    }
    return true
}

private fun <K, V> matchingLeafIndex(
    entry: Leaf<K, V>,
    candidates: List<Leaf<K, V>>,
    matched: BooleanArray,
    policy: HashPolicy<K>,
): Int {
    for (index in candidates.indices) {
        if (!matched[index] && entry === candidates[index]) return index
    }
    for (index in candidates.indices) {
        if (!matched[index] && policy.equivalent(entry.key, candidates[index].key)) return index
    }
    return -1
}

/** Traverses logical bitmap slots together and skips every reference-identical descendant. */
private suspend fun <K, V> SequenceScope<MapDifference<K, V>>.diffChampNodes(
    left: Node<K, V>?,
    right: Node<K, V>?,
    shift: Int,
    policy: HashPolicy<K>,
) {
    if (left === right) return
    if (left == null) {
        appendSubtreeDifferences(checkNotNull(right), added = true)
        return
    }
    if (right == null) {
        appendSubtreeDifferences(left, added = false)
        return
    }

    val leftIsHashNode = left is Leaf || left is Collision
    val rightIsHashNode = right is Leaf || right is Collision
    if (leftIsHashNode && rightIsHashNode) {
        diffHashNodes(left, right, policy)
        return
    }

    for (index in 0 until 32) {
        diffChampNodes(
            logicalSlot(left, index, shift),
            logicalSlot(right, index, shift),
            shift + BitsPerLevel,
            policy,
        )
    }
}

private suspend fun <K, V> SequenceScope<MapDifference<K, V>>.diffHashNodes(
    left: Node<K, V>,
    right: Node<K, V>,
    policy: HashPolicy<K>,
) {
    val leftEntries = entriesOf(left)
    val rightEntries = entriesOf(right)
    if (hashOf(left) != hashOf(right)) {
        for (entry in leftEntries) appendLeafDifference(entry, added = false)
        for (entry in rightEntries) appendLeafDifference(entry, added = true)
        return
    }

    val matchedRight = BooleanArray(rightEntries.size)
    for (leftEntry in leftEntries) {
        val rightIndex = matchingLeafIndex(leftEntry, rightEntries, matchedRight, policy)
        if (rightIndex < 0) {
            appendLeafDifference(leftEntry, added = false)
            continue
        }
        matchedRight[rightIndex] = true
        val rightEntry = rightEntries[rightIndex]
        if (leftEntry !== rightEntry && !hamtValuesEqual(leftEntry.value, rightEntry.value)) {
            yield(MapDifference(
                MapDifferenceKind.CHANGED,
                leftEntry.key,
                leftEntry.value,
                rightEntry.value,
            ))
        }
    }
    for (rightIndex in rightEntries.indices) {
        if (!matchedRight[rightIndex]) appendLeafDifference(rightEntries[rightIndex], added = true)
    }
}

private suspend fun <K, V> SequenceScope<MapDifference<K, V>>.appendSubtreeDifferences(
    node: Node<K, V>,
    added: Boolean,
) {
    when (node) {
        is Leaf -> appendLeafDifference(node, added)
        is Collision -> for (entry in node.entries) appendLeafDifference(entry, added)
        is BitmapNode -> {
            for (index in 0 until 32) {
                val bit = bitPosition(index)
                when {
                    node.dataMap and bit != 0 ->
                        appendLeafDifference(node.data[sparseIndex(node.dataMap, bit)], added)
                    node.nodeMap and bit != 0 ->
                        appendSubtreeDifferences(node.nodes[sparseIndex(node.nodeMap, bit)], added)
                }
            }
        }
    }
}

private suspend fun <K, V> SequenceScope<MapDifference<K, V>>.appendLeafDifference(
    entry: Leaf<K, V>,
    added: Boolean,
) {
    yield(if (added) {
        MapDifference(MapDifferenceKind.ADDED, entry.key, null, entry.value)
    } else {
        MapDifference(MapDifferenceKind.REMOVED, entry.key, entry.value, null)
    })
}

private fun <K, V> champStatistics(
    node: Node<K, V>,
    shift: Int,
    prefix: Int,
    prefixMask: Int,
): ChampStatistics = when (node) {
    is Leaf -> ChampStatistics(
        1, 0, 0, 0, 0,
        if (hashHasPrefix(node.hash, prefix, prefixMask)) 0 else 1,
    )
    is Collision -> ChampStatistics(
        0, 0, node.entries.size, 0, 0,
        (if (hashHasPrefix(node.hash, prefix, prefixMask)) 0 else 1) +
            node.entries.count { it.hash != node.hash },
    )
    is BitmapNode -> {
        val bitmapCardinalityMismatch =
            cardinalityDifference(Integer.bitCount(node.dataMap), node.data.size) +
                cardinalityDifference(Integer.bitCount(node.nodeMap), node.nodes.size)
        if (shift > 30) {
            ChampStatistics(
                node.data.size,
                1,
                0,
                node.nodes.count { it is Leaf },
                if (node.data.size + node.nodes.size < 2 &&
                    !(node.data.isEmpty() && node.nodes.singleOrNull() is BitmapNode)) 1 else 0,
                1 + bitmapCardinalityMismatch,
            )
        } else {
            val nextMask = if (shift >= 27) -1 else (1 shl (shift + BitsPerLevel)) - 1
            val dataSlots = bitmapSlots(node.dataMap)
            val nodeSlots = bitmapSlots(node.nodeMap)
            fun slotPrefix(slot: Int): Int = prefix or (slot shl shift)
            val children = node.nodes.zip(nodeSlots).map { (child, slot) ->
                champStatistics(child, shift + BitsPerLevel, slotPrefix(slot), nextMask)
            }
            val invalidDataRouting = node.data.zip(dataSlots).count { (entry, slot) ->
                (shift == 30 && slot > 3) || !hashHasPrefix(entry.hash, slotPrefix(slot), nextMask)
            }
            val invalidChildSlots = nodeSlots.count { shift == 30 && it > 3 }
            ChampStatistics(
                node.data.size + children.sumOf { it.inlinePayloads },
                1 + children.sumOf { it.bitmapNodes },
                children.sumOf { it.collisionPayloads },
                node.nodes.count { it is Leaf } + children.sumOf { it.invalidLeafChildren },
                (if (node.data.size + node.nodes.size < 2 &&
                    !(node.data.isEmpty() && node.nodes.singleOrNull() is BitmapNode)) 1 else 0) +
                    children.sumOf { it.underfullBitmapNodes },
                bitmapCardinalityMismatch + invalidDataRouting + invalidChildSlots +
                    children.sumOf { it.invalidHashRouting },
            )
        }
    }
}

private fun cardinalityDifference(expected: Int, actual: Int): Int =
    if (expected >= actual) expected - actual else actual - expected

/** Construct deliberately malformed trie states, so the tests can check that validation rejects them. */
internal fun champMalformedDiagnosticsForTesting(): Pair<Int, Int> {
    val leaf = Leaf(0, 0, Unit)
    val overDepth = BitmapNode(1, 0, listOf(leaf), emptyList())
    val mismatchedBitmap = BitmapNode(3, 0, listOf(leaf), emptyList())
    return champStatistics(overDepth, 35, 0, -1).invalidHashRouting to
        champStatistics(mismatchedBitmap, 0, 0, 0).invalidHashRouting
}

private fun hashHasPrefix(hash: Int, prefix: Int, prefixMask: Int): Boolean =
    (hash and prefixMask) == prefix

private fun bitmapSlots(bitmap: Int): List<Int> =
    (0 until 32).filter { slot -> bitmap and (1 shl slot) != 0 }

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
            ChampOperation.UNION -> result += if (rightEntry == null || hamtValuesEqual(leftEntry.value, rightEntry.value)) {
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
        hamtValuesEqual(left[index].value, right[index].value)
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
        expected.hash == actual.hash && policy.equivalent(expected.key, actual.key) && hamtValuesEqual(expected.value, actual.value)
}

private fun <K, V, V2> champTopologyEqual(
    left: Node<K, V>?,
    right: Node<K, V2>?,
    policy: HashPolicy<K>,
): Boolean = when {
    left == null || right == null -> left == null && right == null
    left is Leaf && right is Leaf -> left.hash == right.hash
    left is Collision && right is Collision ->
        left.hash == right.hash &&
            left.entries.size == right.entries.size &&
            left.entries.all { leftEntry ->
                right.entries.any { rightEntry -> policy.equivalent(leftEntry.key, rightEntry.key) }
            }
    left is BitmapNode && right is BitmapNode ->
        left.dataMap == right.dataMap &&
            left.nodeMap == right.nodeMap &&
            left.data.map { it.hash } == right.data.map { it.hash } &&
            left.nodes.size == right.nodes.size &&
            left.nodes.zip(right.nodes).all { (leftChild, rightChild) ->
                champTopologyEqual(leftChild, rightChild, policy)
            }
    else -> false
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
