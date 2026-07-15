package tools.datastructures.hamt

public enum class BiMapConflict { KEY, VALUE }

public class BiMapConflictException(public val conflict: BiMapConflict) :
    IllegalArgumentException("An equivalent ${conflict.name.lowercase()} is already present.")

public sealed interface BiMapLookup<out T> {
    public data class Found<T>(public val value: T) : BiMapLookup<T>
    public data object Missing : BiMapLookup<Nothing>
}

public data class BiMapAddResult<K, V>(
    public val map: PersistentBiMap<K, V>,
    public val added: Boolean,
    public val conflict: BiMapConflict?,
)

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
        public fun <K, V> empty(
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentBiMap<K, V> = PersistentBiMap(
            PersistentHashMap.empty(keyPolicy),
            PersistentHashMap.empty(valuePolicy),
        )

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

    public val size: Int get() = forward.size
    public val isEmpty: Boolean get() = forward.isEmpty
    public val keyPolicy: HashPolicy<K> get() = forward.policy
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

    public fun containsKey(key: K): Boolean = forward.containsKey(key)
    public fun containsValue(value: V): Boolean = backward.containsKey(value)

    public fun lookup(key: K): BiMapLookup<V> =
        forward.getEntry(key)?.let { BiMapLookup.Found(it.value) } ?: BiMapLookup.Missing

    public fun lookupKey(value: V): BiMapLookup<K> =
        backward.getEntry(value)?.let { BiMapLookup.Found(it.value) } ?: BiMapLookup.Missing

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

    public fun removeKey(key: K): PersistentBiMap<K, V> = tryRemoveKey(key).map

    public fun tryRemoveKey(key: K): BiMapRemoveResult<K, V, V> {
        val entry = forward.getEntry(key) ?: return BiMapRemoveResult(this, false, null)
        checkNotNull(backward.getEntry(entry.value)) { "PersistentBiMap invariant failure." }
        return BiMapRemoveResult(
            PersistentBiMap(forward.remove(entry.key), backward.remove(entry.value)),
            true,
            entry.value,
        )
    }

    public fun removeValue(value: V): PersistentBiMap<K, V> = tryRemoveValue(value).map

    public fun tryRemoveValue(value: V): BiMapRemoveResult<K, V, K> {
        val entry = backward.getEntry(value) ?: return BiMapRemoveResult(this, false, null)
        checkNotNull(forward.getEntry(entry.value)) { "PersistentBiMap invariant failure." }
        return BiMapRemoveResult(
            PersistentBiMap(forward.remove(entry.value), backward.remove(entry.key)),
            true,
            entry.value,
        )
    }

    public fun clear(): PersistentBiMap<K, V> =
        if (isEmpty) this else empty(keyPolicy, valuePolicy)

    override fun iterator(): Iterator<HamtEntry<K, V>> = forward.iterator()
    public fun keys(): Sequence<K> = forward.entries().map { it.key }
    public fun values(): Sequence<V> = forward.entries().map { it.value }

    internal fun sharesRootsWith(other: PersistentBiMap<K, V>): Boolean =
        forward.sharesRootWith(other.forward) && backward.sharesRootWith(other.backward)

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
