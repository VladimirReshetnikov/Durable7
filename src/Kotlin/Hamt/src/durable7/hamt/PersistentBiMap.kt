/*
 * Strict persistent bidirectional map.
 *
 * Maintains a one-to-one correspondence by composing a forward map with an inverse one, each under
 * its own hash policy. Strict: an insertion whose key or value is already represented is rejected
 * rather than silently displacing the existing pair.
 */
package durable7.hamt

/**
 * Which side of the bijection blocked an insertion. The key domain is reported in preference to the value domain when
 * both conflict.
 */
public enum class BiMapConflict { KEY, VALUE }

/** Raised when a strict bimap insertion would break the one-to-one correspondence. */
public class BiMapConflictException(public val conflict: BiMapConflict) :
    IllegalArgumentException("An equivalent ${conflict.name.lowercase()} is already present.")

/** A presence-safe lookup result, so a stored null stays distinct from absence. */
public sealed interface BiMapLookup<out T> {
    /** The lookup matched, carrying the stored counterpart. */
    public data class Found<T>(public val value: T) : BiMapLookup<T>

    /** The lookup did not match. */
    public data object Missing : BiMapLookup<Nothing>
}

/** The outcome of a non-throwing strict insertion, naming the blocking domain when rejected. */
public data class BiMapAddResult<K, V>(
    public val map: PersistentBiMap<K, V>,
    public val added: Boolean,
    public val conflict: BiMapConflict?,
)

/** The bimap remaining after a removal, together with the opposite-domain representative removed. */
public data class BiMapRemoveResult<K, V, T>(
    public val map: PersistentBiMap<K, V>,
    public val removed: Boolean,
    public val opposite: T?,
)

/** Strict immutable bijection backed by independent forward and inverse CHAMP maps. */
public class PersistentBiMap<K, V> private constructor(
    private val forward: PersistentHashMap<K, V>,
    private val backward: PersistentHashMap<V, K>,
) : Iterable<HamtEntry<K, V>> {
    @Volatile
    private var inverseView: PersistentBiMap<V, K>? = null

    public companion object {
        /**
         * An empty bidirectional map. The two policies are independent - keys and values are hashed and compared
         * separately - and both are retained by identity.
         */
        public fun <K, V> empty(
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentBiMap<K, V> = PersistentBiMap(
            PersistentHashMap.empty(keyPolicy),
            PersistentHashMap.empty(valuePolicy),
        )

        /**
         * A bidirectional map holding every pair.
         *
         * @throws BiMapConflictException if the entries do not form a bijection, that is, if a key or a value
         * repeats.
         */
        public fun <K, V> from(
            entries: Iterable<Pair<K, V>>,
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentBiMap<K, V> {
            var result = empty<K, V>(keyPolicy, valuePolicy)
            for ((key, value) in entries) result = result.add(key, value)
            return result
        }
    }

    /** Number of pairs. Both directions always hold the same count. */
    public val size: Int get() = forward.size
    /** Whether the map holds no pairs. */
    public val isEmpty: Boolean get() = forward.isEmpty
    /** The hash policy used for keys. */
    public val keyPolicy: HashPolicy<K> get() = forward.policy
    /** The hash policy used for values, independent of the key policy. */
    public val valuePolicy: HashPolicy<V> get() = backward.policy

    /** Cached O(1) inverse facade whose own inverse is this exact object. */
    public val inverse: PersistentBiMap<V, K>
        get() {
            inverseView?.let { return it }
            return synchronized(this) {
                inverseView ?: PersistentBiMap(backward, forward).also { created ->
                    created.inverseView = this
                    inverseView = created
                }
            }
        }

    /** Whether [key] is present in the forward direction. */
    public fun containsKey(key: K): Boolean = forward.containsKey(key)
    /** Whether [value] is present in the inverse direction. */
    public fun containsValue(value: V): Boolean = backward.containsKey(value)

    /**
     * The value paired with [key], as a presence-discriminated result. Unlike a nullable return this keeps a stored
     * `null` value distinct from a miss.
     */
    public fun lookup(key: K): BiMapLookup<V> =
        forward.getEntry(key)?.let { BiMapLookup.Found(it.value) } ?: BiMapLookup.Missing

    /**
     * The key paired with [value], as a presence-discriminated result. The inverse lookup costs the same as the
     * forward one, since both directions are indexed.
     */
    public fun lookupKey(value: V): BiMapLookup<K> =
        backward.getEntry(value)?.let { BiMapLookup.Found(it.value) } ?: BiMapLookup.Missing

    /**
     * A map with the pair added.
     *
     * @throws BiMapConflictException if the key or the value is already present. A bijection cannot absorb the pair
     * by displacing an existing one, so this is strict rather than overwriting; use [tryAdd] to detect the conflict
     * without an exception.
     */
    public fun add(key: K, value: V): PersistentBiMap<K, V> {
        val result = tryAdd(key, value)
        if (!result.added) throw BiMapConflictException(checkNotNull(result.conflict))
        return result.map
    }

    /** Key conflict has precedence when both equivalence classes are occupied. */
    public fun tryAdd(key: K, value: V): BiMapAddResult<K, V> {
        if (forward.containsKey(key)) return BiMapAddResult(this, false, BiMapConflict.KEY)
        if (backward.containsKey(value)) return BiMapAddResult(this, false, BiMapConflict.VALUE)
        return BiMapAddResult(
            PersistentBiMap(forward.add(key, value), backward.add(value, key)),
            true,
            null,
        )
    }

    /** Adds or replaces one key's value without displacing a different key. */
    public fun set(key: K, value: V): PersistentBiMap<K, V> {
        val previous = forward.getEntry(key)
        if (previous == null) {
            if (backward.containsKey(value)) throw BiMapConflictException(BiMapConflict.VALUE)
            return PersistentBiMap(forward.add(key, value), backward.add(value, key))
        }
        if (valuePolicy.equivalent(previous.value, value)) return this
        if (backward.containsKey(value)) throw BiMapConflictException(BiMapConflict.VALUE)
        checkNotNull(backward.getEntry(previous.value)) { "PersistentBiMap invariant failure." }
        return PersistentBiMap(
            forward.remove(previous.key).add(previous.key, value),
            backward.remove(previous.value).add(value, previous.key),
        )
    }

    /** A map without the pair keyed by [key]. Returns the receiver unchanged when absent. */
    public fun removeKey(key: K): PersistentBiMap<K, V> = tryRemoveKey(key).map

    /**
     * Remove the pair keyed by [key], reporting whether anything was removed and, if so, the value that went with
     * it.
     */
    public fun tryRemoveKey(key: K): BiMapRemoveResult<K, V, V> {
        val entry = forward.getEntry(key) ?: return BiMapRemoveResult(this, false, null)
        checkNotNull(backward.getEntry(entry.value)) { "PersistentBiMap invariant failure." }
        return BiMapRemoveResult(
            PersistentBiMap(forward.remove(entry.key), backward.remove(entry.value)),
            true,
            entry.value,
        )
    }

    /** A map without the pair whose value is [value]. Returns the receiver unchanged when absent. */
    public fun removeValue(value: V): PersistentBiMap<K, V> = tryRemoveValue(value).map

    /**
     * Remove the pair whose value is [value], reporting whether anything was removed and, if so, the key that went
     * with it.
     */
    public fun tryRemoveValue(value: V): BiMapRemoveResult<K, V, K> {
        val entry = backward.getEntry(value) ?: return BiMapRemoveResult(this, false, null)
        checkNotNull(forward.getEntry(entry.value)) { "PersistentBiMap invariant failure." }
        return BiMapRemoveResult(
            PersistentBiMap(forward.remove(entry.value), backward.remove(entry.key)),
            true,
            entry.value,
        )
    }

    /** An empty map retaining both policies. Returns the receiver when it is already empty. */
    public fun clear(): PersistentBiMap<K, V> =
        if (isEmpty) this else empty(keyPolicy, valuePolicy)

    override fun iterator(): Iterator<HamtEntry<K, V>> = forward.iterator()
    /** The keys, in the forward index's iteration order. */
    public fun keys(): Sequence<K> = forward.entries().map { it.key }
    /** The values, in the same order as [keys]. */
    public fun values(): Sequence<V> = forward.entries().map { it.value }

    internal fun sharesRootsWith(other: PersistentBiMap<K, V>): Boolean =
        forward.sharesRootWith(other.forward) && backward.sharesRootWith(other.backward)

    /**
     * Walk both indexes and check that they agree - equal sizes, and every forward pair present in the inverse. A
     * defensive audit for tests and for data arriving from outside the process, not a routine operation: it visits
     * every entry.
     */
    public fun validateStructure(): Boolean {
        if (forward.size != backward.size) return false
        for (entry in forward) {
            val inverseEntry = backward.getEntry(entry.value) ?: return false
            if (!keyPolicy.equivalent(entry.key, inverseEntry.value)) return false
        }
        for (entry in backward) {
            val forwardEntry = forward.getEntry(entry.value) ?: return false
            if (!valuePolicy.equivalent(entry.key, forwardEntry.value)) return false
        }
        return true
    }
}
