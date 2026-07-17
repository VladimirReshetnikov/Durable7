package tools.datastructures.hamt

public fun interface EqualityPolicy<T> {
    public fun equivalent(left: T, right: T): Boolean
}

private object DefaultEqualityPolicy : EqualityPolicy<Any?> {
    override fun equivalent(left: Any?, right: Any?): Boolean = left === right || left == right
}

@Suppress("UNCHECKED_CAST")
public fun <T> defaultEqualityPolicy(): EqualityPolicy<T> = DefaultEqualityPolicy as EqualityPolicy<T>

/** Presence-safe patch value; [value] may itself be null while [isPresent] remains true. */
public class MapPatchValue<V> private constructor(
    public val isPresent: Boolean,
    public val value: V?,
) {
    public companion object {
        public fun <V> absent(): MapPatchValue<V> = MapPatchValue(false, null)
        public fun <V> present(value: V): MapPatchValue<V> = MapPatchValue(true, value)
    }
}

public data class MapPatchEntry<K, V>(
    public val key: K,
    public val before: MapPatchValue<V>,
    public val after: MapPatchValue<V>,
)

public data class MapPatchAddResult<K, V>(
    public val patch: PersistentMapPatch<K, V>,
    public val added: Boolean,
)

/** Immutable invertible strict changes between policy-compatible persistent hash maps. */
public class PersistentMapPatch<K, V> private constructor(
    private val changes: PersistentHashMap<K, Change<V>>,
    public val valuePolicy: EqualityPolicy<V>,
) : Iterable<MapPatchEntry<K, V>> {
    private data class Change<V>(val before: MapPatchValue<V>, val after: MapPatchValue<V>)

    public companion object {
        public fun <K, V> empty(
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: EqualityPolicy<V> = defaultEqualityPolicy(),
        ): PersistentMapPatch<K, V> = PersistentMapPatch(PersistentHashMap.empty(keyPolicy), valuePolicy)

        public fun <K, V> from(
            entries: Iterable<MapPatchEntry<K, V>>,
            keyPolicy: HashPolicy<K> = defaultHashPolicy(),
            valuePolicy: EqualityPolicy<V> = defaultEqualityPolicy(),
        ): PersistentMapPatch<K, V> {
            var result = empty<K, V>(keyPolicy, valuePolicy)
            for (entry in entries) result = result.add(entry)
            return result
        }

        public fun <K, V> between(
            source: PersistentHashMap<K, V>,
            target: PersistentHashMap<K, V>,
            valuePolicy: EqualityPolicy<V> = defaultEqualityPolicy(),
        ): PersistentMapPatch<K, V> {
            require(source.policy === target.policy) { "Maps must retain the same hash policy object." }
            var result = empty<K, V>(source.policy, valuePolicy)
            for (entry in source) {
                val targetEntry = target.getEntry(entry.key)
                result = when {
                    targetEntry == null -> result.add(
                        MapPatchEntry(entry.key, MapPatchValue.present(entry.value), MapPatchValue.absent()),
                    )
                    !valuePolicy.equivalent(entry.value, targetEntry.value) -> result.add(
                        MapPatchEntry(entry.key, MapPatchValue.present(entry.value), MapPatchValue.present(targetEntry.value)),
                    )
                    else -> result
                }
            }
            for (entry in target) {
                if (!source.containsKey(entry.key)) {
                    result = result.add(
                        MapPatchEntry(entry.key, MapPatchValue.absent(), MapPatchValue.present(entry.value)),
                    )
                }
            }
            return result
        }
    }

    public val size: Int get() = changes.size
    public val isEmpty: Boolean get() = changes.isEmpty
    public val keyPolicy: HashPolicy<K> get() = changes.policy

    public fun get(key: K): MapPatchEntry<K, V>? {
        val indexed = changes.getEntry(key) ?: return null
        return MapPatchEntry(indexed.key, indexed.value.before, indexed.value.after)
    }

    public fun add(entry: MapPatchEntry<K, V>): PersistentMapPatch<K, V> {
        if (statesEqual(entry.before, entry.after)) return this
        if (changes.containsKey(entry.key)) throw DuplicateKeyException("An equivalent patch key already exists.")
        return PersistentMapPatch(changes.add(entry.key, Change(entry.before, entry.after)), valuePolicy)
    }

    public fun tryAdd(entry: MapPatchEntry<K, V>): MapPatchAddResult<K, V> {
        if (statesEqual(entry.before, entry.after)) return MapPatchAddResult(this, false)
        if (changes.containsKey(entry.key)) return MapPatchAddResult(this, false)
        return MapPatchAddResult(add(entry), true)
    }

    public fun remove(key: K): PersistentMapPatch<K, V> =
        if (!changes.containsKey(key)) this else PersistentMapPatch(changes.remove(key), valuePolicy)

    /** Validates every expected state before publishing any edit. */
    public fun apply(source: PersistentHashMap<K, V>): PersistentHashMap<K, V> {
        require(source.policy === keyPolicy) { "The source map must retain the patch's hash policy object." }
        for (entry in this) {
            if (!stateMatches(entry.before, source.getEntry(entry.key))) {
                throw IllegalStateException("The source map conflicts with the patch at key '${entry.key}'.")
            }
        }
        var result = source
        for (entry in this) {
            result = if (entry.after.isPresent) {
                @Suppress("UNCHECKED_CAST")
                result.put(entry.key, entry.after.value as V)
            } else {
                result.remove(entry.key)
            }
        }
        return result
    }

    public fun invert(): PersistentMapPatch<K, V> {
        var result = empty<K, V>(keyPolicy, valuePolicy)
        for (entry in this) result = result.add(MapPatchEntry(entry.key, entry.after, entry.before))
        return result
    }

    /** Composes this patch with [next], rejecting incompatible intermediate states. */
    public fun compose(next: PersistentMapPatch<K, V>): PersistentMapPatch<K, V> {
        require(keyPolicy === next.keyPolicy) { "Patches must retain the same hash policy object." }
        require(valuePolicy === next.valuePolicy) { "Patches must retain the same value policy object." }
        var result = this
        for (entry in next) {
            val prior = result.get(entry.key)
            if (prior == null) {
                result = result.add(entry)
                continue
            }
            require(statesEqual(prior.after, entry.before)) {
                "The patches have incompatible intermediate states at key '${entry.key}'."
            }
            result = if (statesEqual(prior.before, entry.after)) {
                result.remove(prior.key)
            } else {
                PersistentMapPatch(
                    result.changes.put(prior.key, Change(prior.before, entry.after)),
                    valuePolicy,
                )
            }
        }
        return result
    }

    public fun validateStructure(): Int {
        for (entry in this) check(!statesEqual(entry.before, entry.after)) { "Patch stores a semantic no-op." }
        return size
    }

    override fun iterator(): Iterator<MapPatchEntry<K, V>> =
        changes.asSequence().map { MapPatchEntry(it.key, it.value.before, it.value.after) }.iterator()

    private fun statesEqual(left: MapPatchValue<V>, right: MapPatchValue<V>): Boolean =
        left.isPresent == right.isPresent && (!left.isPresent || valuesEqual(left.value, right.value))

    private fun stateMatches(expected: MapPatchValue<V>, actual: HamtEntry<K, V>?): Boolean =
        expected.isPresent == (actual != null) && (!expected.isPresent || valuesEqual(expected.value, actual?.value))

    @Suppress("UNCHECKED_CAST")
    private fun valuesEqual(left: V?, right: V?): Boolean = valuePolicy.equivalent(left as V, right as V)
}
