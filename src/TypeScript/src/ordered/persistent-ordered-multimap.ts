import { defaultHashPolicy, type HashPolicy } from "../hamt/hash-policy.js";
import { PersistentOrderedMap } from "./persistent-ordered-map.js";
import { PersistentOrderedSet } from "./persistent-ordered-set.js";

/** One key/value representative in key-grouped insertion order. */
export interface OrderedMultimapEntry<K, V> {
    readonly key: K;
    readonly value: V;
}

/** Presence-discriminated lookup of one nonempty ordered value group. */
export type OrderedMultimapLookup<V> =
    | { readonly found: true; readonly values: PersistentOrderedSet<V> }
    | { readonly found: false; readonly values: PersistentOrderedSet<V> };

/** Result of attempting to add one ordered-multimap pair. */
export interface OrderedMultimapAddResult<K, V> {
    readonly added: boolean;
    readonly map: PersistentOrderedMultimap<K, V>;
}

/**
 * Immutable set-valued multimap preserving key-group order and order within each value group.
 *
 * Pair iteration is key-grouped; it is not one globally interleaved pair-arrival history. Key and
 * value equivalence retain independent hash policies, and empty groups are never stored.
 */
export class PersistentOrderedMultimap<K, V> implements Iterable<OrderedMultimapEntry<K, V>> {
    readonly #groups: PersistentOrderedMap<K, PersistentOrderedSet<V>>;
    readonly #valuePolicy: HashPolicy<V>;
    readonly #pairCount: number;

    private constructor(
        groups: PersistentOrderedMap<K, PersistentOrderedSet<V>>,
        valuePolicy: HashPolicy<V>,
        pairCount: number,
    ) {
        this.#groups = groups;
        this.#valuePolicy = valuePolicy;
        this.#pairCount = pairCount;
    }

    public static empty<K, V>(
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentOrderedMultimap<K, V> {
        return new PersistentOrderedMultimap(
            PersistentOrderedMap.empty<K, PersistentOrderedSet<V>>(keyPolicy, Object.is),
            valuePolicy,
            0,
        );
    }

    public static from<K, V>(
        entries: Iterable<readonly [K, V]>,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentOrderedMultimap<K, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentOrderedMultimap.empty<K, V>(keyPolicy, valuePolicy);
        for (const [key, value] of entries) result = result.add(key, value);
        return result;
    }

    public get keyCount(): number { return this.#groups.size; }
    public get pairCount(): number { return this.#pairCount; }
    public get isEmpty(): boolean { return this.#pairCount === 0; }
    public get keyPolicy(): HashPolicy<K> { return this.#groups.keyPolicy; }
    public get valuePolicy(): HashPolicy<V> { return this.#valuePolicy; }

    public containsKey(key: K): boolean { return this.#groups.containsKey(key); }

    public contains(key: K, value: V): boolean {
        const group = this.#groups.tryGetEntry(key);
        return group.found && group.entry.value.contains(value);
    }

    public countValues(key: K): number {
        const group = this.#groups.tryGetEntry(key);
        return group.found ? group.entry.value.size : 0;
    }

    /** Returns the represented group or an empty ordered set retaining the value policy. */
    public getValues(key: K): PersistentOrderedSet<V> {
        const group = this.#groups.tryGetEntry(key);
        return group.found ? group.entry.value : PersistentOrderedSet.empty(this.#valuePolicy);
    }

    public tryGetValues(key: K): OrderedMultimapLookup<V> {
        const group = this.#groups.tryGetEntry(key);
        return group.found
            ? { found: true, values: group.entry.value }
            : { found: false, values: PersistentOrderedSet.empty(this.#valuePolicy) };
    }

    public getStoredKey(key: K): K | undefined { return this.#groups.getStoredKey(key); }

    public tryGetValue(key: K, value: V): { readonly found: boolean; readonly value: V } {
        const group = this.#groups.tryGetEntry(key);
        return group.found ? group.entry.value.tryGetValue(value) : { found: false, value };
    }

    public add(key: K, value: V): PersistentOrderedMultimap<K, V> {
        const group = this.#groups.tryGetEntry(key);
        if (group.found) {
            const values = group.entry.value.add(value);
            if (values === group.entry.value) return this;
            return new PersistentOrderedMultimap(
                this.#groups.set(group.entry.key, values),
                this.#valuePolicy,
                this.incrementPairCount(),
            );
        }

        return new PersistentOrderedMultimap(
            this.#groups.add(key, PersistentOrderedSet.empty(this.#valuePolicy).add(value)),
            this.#valuePolicy,
            this.incrementPairCount(),
        );
    }

    public tryAdd(key: K, value: V): OrderedMultimapAddResult<K, V> {
        const map = this.add(key, value);
        return { added: map !== this, map };
    }

    public remove(key: K, value: V): PersistentOrderedMultimap<K, V> {
        const group = this.#groups.tryGetEntry(key);
        if (!group.found) return this;
        const removed = group.entry.value.tryRemove(value);
        if (!removed.removed) return this;
        const groups = removed.set.isEmpty
            ? this.#groups.remove(group.entry.key)
            : this.#groups.set(group.entry.key, removed.set);
        return new PersistentOrderedMultimap(groups, this.#valuePolicy, this.#pairCount - 1);
    }

    public removeKey(key: K): PersistentOrderedMultimap<K, V> {
        const group = this.#groups.tryGetEntry(key);
        return !group.found
            ? this
            : new PersistentOrderedMultimap(
                this.#groups.remove(group.entry.key),
                this.#valuePolicy,
                this.#pairCount - group.entry.value.size,
            );
    }

    public clear(): PersistentOrderedMultimap<K, V> {
        return this.isEmpty ? this : PersistentOrderedMultimap.empty(this.keyPolicy, this.#valuePolicy);
    }

    public keys(): IterableIterator<K> { return this.#groups.keys(); }

    public *groups(): Generator<readonly [K, PersistentOrderedSet<V>], void> {
        for (const group of this.#groups) yield [group.key, group.value];
    }

    public *[Symbol.iterator](): IterableIterator<OrderedMultimapEntry<K, V>> {
        for (const group of this.#groups) {
            for (const value of group.value) yield { key: group.key, value };
        }
    }

    public sharesGroupsRootsWith(other: PersistentOrderedMultimap<K, V>): boolean {
        return this.#groups.sharesOrderStorageWith(other.#groups)
            && this.#groups.sharesMembershipRootWith(other.#groups);
    }

    public validateStructure(): { readonly keyCount: number; readonly pairCount: number } {
        this.#groups.validateStructure();
        let pairCount = 0;
        for (const group of this.#groups) {
            if (group.value.isEmpty || group.value.policy !== this.#valuePolicy) {
                throw new Error("An ordered multimap stores an empty group or the wrong value policy.");
            }
            group.value.validateStructure();
            pairCount += group.value.size;
            if (!Number.isSafeInteger(pairCount)) throw new Error("The ordered multimap pair count is not exact.");
        }
        if (pairCount !== this.#pairCount) throw new Error("The ordered multimap pair count disagrees with its groups.");
        return { keyCount: this.keyCount, pairCount };
    }

    private incrementPairCount(): number {
        if (this.#pairCount === Number.MAX_SAFE_INTEGER) {
            throw new RangeError("The operation would exceed the exact ordered-multimap pair-count limit.");
        }
        return this.#pairCount + 1;
    }
}
