/**
 * Persistent set-valued hash multimap.
 *
 * Maps each key to a nonempty value set, so adding a present pair is a no-op and removing a key's
 * last value removes the key itself. Distinct-key and pair cardinalities are tracked separately,
 * because neither can be derived from the other in constant time.
 */
import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import { PersistentHashMap, PersistentHashSet } from "./persistent-hamt.js";

/** One stored representative pair from a set-valued hash multimap. */
export interface HashMultimapEntry<K, V> {
    /** The key. */
    readonly key: K;
    /** The value. */
    readonly value: V;
}

/** Presence-discriminated lookup of a key's non-empty value set. */
export type HashMultimapLookup<V> =
    | { readonly found: true; readonly values: PersistentHashSet<V> }
    | { readonly found: false; readonly values: PersistentHashSet<V> };

function incrementPairCount(count: number): number {
    if (count === Number.MAX_SAFE_INTEGER) {
        throw new RangeError("The operation would exceed the exact multimap pair-count limit.");
    }
    return count + 1;
}

/**
 * Immutable set-valued hash multimap composed from the public CHAMP map and set.
 *
 * Both domains retain their first stored representative. Empty value sets are contracted out of
 * the outer map, so `keyCount` and `pairCount` always describe the same flattened relation.
 */
export class PersistentHashMultimap<K, V> implements Iterable<HashMultimapEntry<K, V>> {
    readonly #groups: PersistentHashMap<K, PersistentHashSet<V>>;
    readonly #valuePolicy: HashPolicy<V>;
    readonly #pairCount: number;

    private constructor(
        groups: PersistentHashMap<K, PersistentHashSet<V>>,
        valuePolicy: HashPolicy<V>,
        pairCount: number,
    ) {
        this.#groups = groups;
        this.#valuePolicy = valuePolicy;
        this.#pairCount = pairCount;
    }

    /** The empty multimap, retaining the supplied policy objects. */
    public static empty<K, V>(
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentHashMultimap<K, V> {
        return new PersistentHashMultimap(
            PersistentHashMap.empty<K, PersistentHashSet<V>>(keyPolicy),
            valuePolicy,
            0,
        );
    }

    /** Build a multimap from the given pairs. */
    public static from<K, V>(
        entries: Iterable<readonly [K, V]>,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentHashMultimap<K, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentHashMultimap.empty<K, V>(keyPolicy, valuePolicy);
        for (const [key, value] of entries) result = result.add(key, value);
        return result;
    }

    /** Number of distinct keys. Every key has at least one value. */
    public get keyCount(): number { return this.#groups.size; }
    /** Number of pairs, maintained incrementally rather than derived by summing groups. */
    public get pairCount(): number { return this.#pairCount; }
    /** Whether the multimap holds no pairs. */
    public get isEmpty(): boolean { return this.#pairCount === 0; }
    /** The retained hash policy defining key equivalence. */
    public get keyPolicy(): HashPolicy<K> { return this.#groups.policy; }
    /** The retained hash policy defining value equivalence. */
    public get valuePolicy(): HashPolicy<V> { return this.#valuePolicy; }

    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.#groups.containsKey(key); }

    /** Whether the pair is present. */
    public contains(key: K, value: V): boolean {
        return this.#groups.getEntry(key)?.value.contains(value) ?? false;
    }

    /** Returns the first stored representative for a represented key class. */
    public getStoredKey(key: K): K | undefined {
        return this.#groups.getEntry(key)?.key;
    }

    /** Returns a represented non-empty group, or an empty set retaining the value policy. */
    public getValues(key: K): PersistentHashSet<V> {
        return this.#groups.getEntry(key)?.value ?? PersistentHashSet.empty(this.#valuePolicy);
    }

    /**
     * The value group for that key, reported presence-safely so absence stays distinct from an
     * empty group.
     */
    public tryGetValues(key: K): HashMultimapLookup<V> {
        const group = this.#groups.getEntry(key);
        return group === undefined
            ? { found: false, values: PersistentHashSet.empty(this.#valuePolicy) }
            : { found: true, values: group.value };
    }

    /** Adds a pair; an existing pair is an exact receiver-identity no-op. */
    public add(key: K, value: V): PersistentHashMultimap<K, V> {
        const group = this.#groups.getEntry(key);
        if (group === undefined) {
            const values = PersistentHashSet.empty(this.#valuePolicy).add(value);
            return new PersistentHashMultimap(
                this.#groups.add(key, values),
                this.#valuePolicy,
                incrementPairCount(this.#pairCount),
            );
        }

        const added = group.value.tryAdd(value);
        if (!added.added) return this;
        return new PersistentHashMultimap(
            this.#groups.put(group.key, added.value),
            this.#valuePolicy,
            incrementPairCount(this.#pairCount),
        );
    }

    /** Removes one pair and contracts its outer key when the last value disappears. */
    public remove(key: K, value: V): PersistentHashMultimap<K, V> {
        const group = this.#groups.getEntry(key);
        if (group === undefined) return this;
        const removed = group.value.tryRemove(value);
        if (removed === undefined) return this;

        const groups = removed.set.isEmpty
            ? this.#groups.remove(group.key)
            : this.#groups.put(group.key, removed.set);
        return new PersistentHashMultimap(groups, this.#valuePolicy, this.#pairCount - 1);
    }

    /** Removes an entire key class and all of its pairs. */
    public removeKey(key: K): PersistentHashMultimap<K, V> {
        const removed = this.#groups.tryRemoveEntry(key);
        if (removed === undefined) return this;
        return new PersistentHashMultimap(
            removed.map,
            this.#valuePolicy,
            this.#pairCount - removed.entry.value.size,
        );
    }

    /** An empty multimap retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentHashMultimap<K, V> {
        return this.isEmpty ? this : PersistentHashMultimap.empty(this.keyPolicy, this.#valuePolicy);
    }

    /** Iterate the keys. */
    public keys(): IterableIterator<K> { return this.#groups.keys(); }

    public *entries(): Generator<HashMultimapEntry<K, V>, void> {
        for (const group of this.#groups) {
            for (const value of group.value) yield { key: group.key, value };
        }
    }

    public [Symbol.iterator](): IterableIterator<HashMultimapEntry<K, V>> { return this.entries(); }

    /** True when both facades reference the same outer CHAMP root. */
    public sharesRootWith(other: PersistentHashMultimap<K, V>): boolean {
        return this.#groups.sharesRootWith(other.#groups);
    }

    /** Checks counts, policy propagation, and the no-empty-group invariant. */
    public validateStructure(): boolean {
        let pairs = 0;
        for (const group of this.#groups) {
            if (group.value.isEmpty || group.value.policy !== this.#valuePolicy) return false;
            pairs += group.value.size;
            if (!Number.isSafeInteger(pairs)) return false;
        }
        return pairs === this.#pairCount && (pairs === 0) === (this.#groups.size === 0);
    }
}
