/**
 * Snapshotting hash-trie facade over immutable CHAMP roots.
 *
 * Mutations publish a new immutable root and snapshots capture the current one in constant time, so
 * a snapshot is unaffected by every later mutation. Scoped to one JavaScript isolate; it makes no
 * cross-worker progress claim.
 */
import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import { PersistentHashMap, type HamtEntry } from "./persistent-hamt.js";

/** Immutable point-in-time view returned by {@link ConcurrentHashTrie.snapshot}. */
export class ConcurrentHashTrieSnapshot<K, V> implements Iterable<HamtEntry<K, V>> {
    readonly #map: PersistentHashMap<K, V>;

    public constructor(map: PersistentHashMap<K, V>) { this.#map = map; }
    /** Number of entries. */
    public get size(): number { return this.#map.size; }
    /** Whether the trie holds no entries. */
    public get isEmpty(): boolean { return this.#map.isEmpty; }
    /** The value stored for the key, or `undefined` when absent. */
    public get(key: K): V | undefined { return this.#map.get(key); }
    /** The stored key representative and value, or `undefined` when absent. */
    public getEntry(key: K): HamtEntry<K, V> | undefined { return this.#map.getEntry(key); }
    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.#map.containsKey(key); }
    /**
     * The captured root as an ordinary persistent map, in constant time. This is the bridge out of
     * the mutable facade: the result is a value that never changes.
     */
    public toPersistentHashMap(): PersistentHashMap<K, V> { return this.#map; }
    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> { return this.#map[Symbol.iterator](); }
}

/**
 * Mutable snapshotting hash trie for JavaScript's single-agent object model.
 *
 * Updates publish persistent CHAMP roots through an observed-root retry protocol, including when a
 * user hash, equivalence, or factory callback reenters the trie. Snapshots capture the current root
 * in O(1). JavaScript objects cannot be shared between worker isolates, so this class intentionally
 * does not claim the lock-free multi-threaded GCAS/RDCSS progress guarantee of the C#/Kotlin Ctries.
 */
export class ConcurrentHashTrie<K, V> implements Iterable<HamtEntry<K, V>> {
    #map: PersistentHashMap<K, V>;
    #generation = 0;
    /** The retained policy defining equivalence. */
    public readonly policy: HashPolicy<K>;

    public constructor(policy: HashPolicy<K> = defaultHashPolicy<K>()) {
        this.policy = policy;
        this.#map = PersistentHashMap.empty<K, V>(policy);
    }

    /**
     * Monotone counter incremented on every published root, so a caller can detect that some
     * mutation occurred without comparing contents.
     */
    public get generation(): number { return this.#generation; }
    /** Number of entries. */
    public get size(): number { return this.#map.size; }
    /** Whether the trie holds no entries. */
    public get isEmpty(): boolean { return this.#map.isEmpty; }
    /** The value stored for the key, or `undefined` when absent. */
    public get(key: K): V | undefined { return this.#map.get(key); }
    /** The stored key representative and value, or `undefined` when absent. */
    public getEntry(key: K): HamtEntry<K, V> | undefined { return this.#map.getEntry(key); }
    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.#map.containsKey(key); }

    /** A map with the key bound to the value, adding or replacing as needed. */
    public set(key: K, value: V): void {
        while (true) {
            const observed = this.#map;
            const next = observed.put(key, value);
            if (this.tryPublish(observed, next)) return;
        }
    }

    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
    public tryAdd(key: K, value: V): boolean {
        while (true) {
            const observed = this.#map;
            const result = observed.tryAdd(key, value);
            if (this.tryPublish(observed, result.value)) return result.added;
        }
    }

    /**
     * Returns the stored value or publishes a factory-produced value.
     *
     * A reentrant publication invalidates the observed root and retries the operation. The factory
     * can consequently run more than once, and every invocation receives the caller's lookup key.
     */
    public getOrPut(key: K, factory: (key: K) => V): V {
        if (typeof factory !== "function") throw new TypeError("factory must be a function.");
        while (true) {
            const observed = this.#map;
            const current = observed.getEntry(key);
            if (current !== undefined) {
                if (this.#map === observed) return current.value;
                continue;
            }

            const value = factory(key);
            if (this.#map !== observed) continue;
            const next = observed.put(key, value);
            if (this.tryPublish(observed, next)) return value;
        }
    }

    /**
     * Adds a missing value or updates a stored value against one stable observed root.
     *
     * Both callbacks receive the caller's lookup key. Reentrant publications cause a retry and may
     * select a different callback on the next observation; discarded candidates are never published.
     */
    public compute(key: K, add: (key: K) => V, update: (key: K, value: V) => V): V {
        if (typeof add !== "function") throw new TypeError("add must be a function.");
        if (typeof update !== "function") throw new TypeError("update must be a function.");
        while (true) {
            const observed = this.#map;
            const current = observed.getEntry(key);
            const nextValue = current === undefined ? add(key) : update(key, current.value);
            if (this.#map !== observed) continue;
            const next = observed.put(key, nextValue);
            const selected = next === observed && current !== undefined ? current.value : nextValue;
            if (this.tryPublish(observed, next)) return selected;
        }
    }

    /** A trie without that entry; returns the receiver when it is absent. */
    public remove(key: K): HamtEntry<K, V> | undefined {
        while (true) {
            const observed = this.#map;
            const result = observed.tryRemoveEntry(key);
            if (result === undefined) {
                if (this.#map === observed) return undefined;
                continue;
            }
            if (this.tryPublish(observed, result.map)) return result.entry;
        }
    }

    /** An empty trie retaining the same policies; returns the receiver when already empty. */
    public clear(): void {
        while (true) {
            const observed = this.#map;
            if (this.tryPublish(observed, observed.clear())) return;
        }
    }

    /** The trie version this cursor is positioned in. */
    public snapshot(): ConcurrentHashTrieSnapshot<K, V> {
        return new ConcurrentHashTrieSnapshot(this.#map);
    }

    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> {
        return this.snapshot()[Symbol.iterator]();
    }

    private tryPublish(observed: PersistentHashMap<K, V>, next: PersistentHashMap<K, V>): boolean {
        if (this.#map !== observed) return false;
        if (next !== observed) {
            this.#map = next;
            this.#generation++;
        }
        return true;
    }
}
