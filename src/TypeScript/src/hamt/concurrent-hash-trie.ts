import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import { PersistentHashMap, type HamtEntry } from "./persistent-hamt.js";

/** Immutable point-in-time view returned by {@link ConcurrentHashTrie.snapshot}. */
export class ConcurrentHashTrieSnapshot<K, V> implements Iterable<HamtEntry<K, V>> {
    readonly #map: PersistentHashMap<K, V>;

    public constructor(map: PersistentHashMap<K, V>) { this.#map = map; }
    public get size(): number { return this.#map.size; }
    public get isEmpty(): boolean { return this.#map.isEmpty; }
    public get(key: K): V | undefined { return this.#map.get(key); }
    public getEntry(key: K): HamtEntry<K, V> | undefined { return this.#map.getEntry(key); }
    public containsKey(key: K): boolean { return this.#map.containsKey(key); }
    public toPersistentHashMap(): PersistentHashMap<K, V> { return this.#map; }
    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> { return this.#map[Symbol.iterator](); }
}

/**
 * Mutable snapshotting hash trie for JavaScript's single-agent object model.
 *
 * Updates publish persistent CHAMP roots and snapshots capture the current root in O(1). JavaScript
 * objects cannot be shared between worker isolates, so this class intentionally does not claim the
 * lock-free multi-threaded GCAS/RDCSS progress guarantee of the C#/Kotlin Ctries.
 */
export class ConcurrentHashTrie<K, V> implements Iterable<HamtEntry<K, V>> {
    #map: PersistentHashMap<K, V>;
    #generation = 0;
    public readonly policy: HashPolicy<K>;

    public constructor(policy: HashPolicy<K> = defaultHashPolicy<K>()) {
        this.policy = policy;
        this.#map = PersistentHashMap.empty<K, V>(policy);
    }

    public get generation(): number { return this.#generation; }
    public get size(): number { return this.#map.size; }
    public get isEmpty(): boolean { return this.#map.isEmpty; }
    public get(key: K): V | undefined { return this.#map.get(key); }
    public getEntry(key: K): HamtEntry<K, V> | undefined { return this.#map.getEntry(key); }
    public containsKey(key: K): boolean { return this.#map.containsKey(key); }

    public set(key: K, value: V): void {
        const next = this.#map.put(key, value);
        this.publish(next);
    }

    public tryAdd(key: K, value: V): boolean {
        const result = this.#map.tryAdd(key, value);
        if (result.added) this.publish(result.value);
        return result.added;
    }

    public getOrPut(key: K, factory: (key: K) => V): V {
        const current = this.#map.getEntry(key);
        if (current !== undefined) return current.value;
        const value = factory(key);
        this.publish(this.#map.put(key, value));
        return value;
    }

    public compute(key: K, add: (key: K) => V, update: (key: K, value: V) => V): V {
        const current = this.#map.getEntry(key);
        const nextValue = current === undefined ? add(key) : update(current.key, current.value);
        this.publish(this.#map.put(key, nextValue));
        return this.#map.getEntry(key)!.value;
    }

    public remove(key: K): HamtEntry<K, V> | undefined {
        const result = this.#map.tryRemoveEntry(key);
        if (result === undefined) return undefined;
        this.publish(result.map);
        return result.entry;
    }

    public clear(): void { this.publish(this.#map.clear()); }

    public snapshot(): ConcurrentHashTrieSnapshot<K, V> {
        return new ConcurrentHashTrieSnapshot(this.#map);
    }

    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> {
        return this.snapshot()[Symbol.iterator]();
    }

    private publish(next: PersistentHashMap<K, V>): void {
        if (next === this.#map) return;
        this.#map = next;
        this.#generation++;
    }
}
