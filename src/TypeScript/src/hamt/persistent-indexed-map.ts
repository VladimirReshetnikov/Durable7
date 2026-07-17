import { defaultHashPolicy, sameValueZero, type HashPolicy } from "./hash-policy.js";
import { PersistentHashMultimap } from "./persistent-hash-multimap.js";
import { DuplicateKeyError, PersistentHashMap, PersistentHashSet } from "./persistent-hamt.js";

interface IndexedValue<V, I> {
    readonly value: V;
    readonly indexKey: I;
}

/** One primary-key/value row from a persistent indexed map. */
export interface IndexedMapEntry<K, V> {
    readonly key: K;
    readonly value: V;
}

/** Presence-discriminated primary lookup. */
export type IndexedMapLookup<K, V> =
    | { readonly found: true; readonly entry: IndexedMapEntry<K, V> }
    | { readonly found: false };

/** Nonthrowing strict-add result. */
export interface IndexedMapAddResult<K, V, I> {
    readonly added: boolean;
    readonly map: PersistentIndexedMap<K, V, I>;
}

/** Immutable primary map with one maintained nonunique secondary index. */
export class PersistentIndexedMap<K, V, I> implements Iterable<IndexedMapEntry<K, V>> {
    readonly #primary: PersistentHashMap<K, IndexedValue<V, I>>;
    readonly #index: PersistentHashMultimap<I, K>;

    private constructor(
        primary: PersistentHashMap<K, IndexedValue<V, I>>,
        index: PersistentHashMultimap<I, K>,
        public readonly indexSelector: (key: K, value: V) => I,
        public readonly valueEquals: (left: V, right: V) => boolean,
    ) {
        this.#primary = primary;
        this.#index = index;
    }

    public static empty<K, V, I>(
        indexSelector: (key: K, value: V) => I,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
        indexPolicy: HashPolicy<I> = defaultHashPolicy<I>(),
    ): PersistentIndexedMap<K, V, I> {
        if (typeof indexSelector !== "function") throw new TypeError("indexSelector must be a function.");
        return new PersistentIndexedMap(
            PersistentHashMap.empty(keyPolicy),
            PersistentHashMultimap.empty(indexPolicy, keyPolicy),
            indexSelector,
            valueEquals,
        );
    }

    public static from<K, V, I>(
        entries: Iterable<readonly [K, V]>,
        indexSelector: (key: K, value: V) => I,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
        indexPolicy: HashPolicy<I> = defaultHashPolicy<I>(),
    ): PersistentIndexedMap<K, V, I> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentIndexedMap.empty(indexSelector, keyPolicy, valueEquals, indexPolicy);
        for (const [key, value] of entries) result = result.set(key, value);
        return result;
    }

    public get size(): number { return this.#primary.size; }
    public get count(): number { return this.size; }
    public get indexKeyCount(): number { return this.#index.keyCount; }
    public get isEmpty(): boolean { return this.#primary.isEmpty; }
    public get keyPolicy(): HashPolicy<K> { return this.#primary.policy; }
    public get indexPolicy(): HashPolicy<I> { return this.#index.keyPolicy; }

    public containsKey(key: K): boolean { return this.#primary.containsKey(key); }
    public get(key: K): V | undefined { return this.#primary.getEntry(key)?.value.value; }

    public tryGetEntry(key: K): IndexedMapLookup<K, V> {
        const entry = this.#primary.getEntry(key);
        return entry === undefined
            ? { found: false }
            : { found: true, entry: { key: entry.key, value: entry.value.value } };
    }

    public getStoredKey(key: K): K | undefined { return this.#primary.getEntry(key)?.key; }
    public getIndexKey(key: K): I | undefined { return this.#primary.getEntry(key)?.value.indexKey; }
    public getKeys(indexKey: I): PersistentHashSet<K> { return this.#index.getValues(indexKey); }
    public containsIndexKey(indexKey: I): boolean { return this.#index.containsKey(indexKey); }

    public add(key: K, value: V): PersistentIndexedMap<K, V, I> {
        if (this.#primary.containsKey(key)) throw new DuplicateKeyError();
        const selected = this.indexSelector(key, value);
        const index = this.#index.add(selected, key);
        const actualIndex = index.getStoredKey(selected) as I;
        return new PersistentIndexedMap(
            this.#primary.add(key, { value, indexKey: actualIndex }),
            index,
            this.indexSelector,
            this.valueEquals,
        );
    }

    public tryAdd(key: K, value: V): IndexedMapAddResult<K, V, I> {
        return this.#primary.containsKey(key) ? { added: false, map: this } : { added: true, map: this.add(key, value) };
    }

    public set(key: K, value: V): PersistentIndexedMap<K, V, I> {
        const current = this.#primary.getEntry(key);
        if (current === undefined) return this.add(key, value);
        if (this.valueEquals(current.value.value, value)) return this;

        const selected = this.indexSelector(current.key, value);
        let index = this.#index;
        let actualIndex = current.value.indexKey;
        if (!this.indexPolicy.equivalent(current.value.indexKey, selected)) {
            index = index.remove(current.value.indexKey, current.key).add(selected, current.key);
            actualIndex = index.getStoredKey(selected) as I;
        }
        return new PersistentIndexedMap(
            this.#primary.put(current.key, { value, indexKey: actualIndex }),
            index,
            this.indexSelector,
            this.valueEquals,
        );
    }

    public remove(key: K): PersistentIndexedMap<K, V, I> {
        const current = this.#primary.getEntry(key);
        if (current === undefined) return this;
        return new PersistentIndexedMap(
            this.#primary.remove(current.key),
            this.#index.remove(current.value.indexKey, current.key),
            this.indexSelector,
            this.valueEquals,
        );
    }

    public clear(): PersistentIndexedMap<K, V, I> {
        return this.isEmpty
            ? this
            : PersistentIndexedMap.empty(this.indexSelector, this.keyPolicy, this.valueEquals, this.indexPolicy);
    }

    public keys(): IterableIterator<K> { return this.#primary.keys(); }
    public *values(): Generator<V, void> { for (const entry of this.#primary) yield entry.value.value; }
    public *[Symbol.iterator](): IterableIterator<IndexedMapEntry<K, V>> {
        for (const entry of this.#primary) yield { key: entry.key, value: entry.value.value };
    }

    public sharesRootsWith(other: PersistentIndexedMap<K, V, I>): boolean {
        return this.#primary.sharesRootWith(other.#primary) && this.#index.sharesRootWith(other.#index);
    }

    public validateStructure(): { readonly count: number; readonly indexKeyCount: number } {
        if (!this.#index.validateStructure() || this.#primary.size !== this.#index.pairCount) {
            throw new Error("The indexed map's primary and secondary counts disagree.");
        }
        if (this.#index.valuePolicy !== this.keyPolicy) throw new Error("The indexed map's primary-key policies disagree.");
        for (const entry of this.#primary) {
            if (!this.#index.contains(entry.value.indexKey, entry.key)) {
                throw new Error("A primary row is absent from its secondary index group.");
            }
        }
        for (const entry of this.#index) {
            const primary = this.#primary.getEntry(entry.value);
            if (primary === undefined || !this.indexPolicy.equivalent(entry.key, primary.value.indexKey)
                || !Object.is(primary.key, entry.value) || !Object.is(primary.value.indexKey, entry.key)) {
                throw new Error("A secondary membership disagrees with its primary row representatives.");
            }
        }
        return { count: this.size, indexKeyCount: this.indexKeyCount };
    }
}
