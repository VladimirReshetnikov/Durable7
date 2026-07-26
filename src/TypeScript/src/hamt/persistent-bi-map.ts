/**
 * Strict persistent bidirectional map.
 *
 * Maintains a one-to-one correspondence by composing a forward map with an inverse one, each under
 * its own hash policy. Strict: an insertion whose key or value is already represented is rejected
 * rather than silently displacing the existing pair.
 */
import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import { type HamtEntry, PersistentHashMap } from "./persistent-hamt.js";

/** Presence-discriminated bidirectional lookup result. */
export type BiMapLookup<T> =
    | { readonly found: true; readonly value: T }
    | { readonly found: false };

/** Nonthrowing strict-add result. */
export type BiMapAddResult<K, V> =
    | { readonly added: true; readonly map: PersistentBiMap<K, V> }
    | { readonly added: false; readonly conflict: "key" | "value"; readonly map: PersistentBiMap<K, V> };

/** Nonthrowing removal result. */
export type BiMapRemoveResult<K, V, T> =
    | { readonly removed: true; readonly map: PersistentBiMap<K, V>; readonly value: T }
    | { readonly removed: false; readonly map: PersistentBiMap<K, V> };

/** Raised when strict addition or replacement violates one side of the bijection. */
export class BiMapConflictError extends Error {
    /**
     * Which side of the bijection was already represented. The key domain is reported in preference
     * to the value domain when both conflict.
     */
    public readonly conflict: "key" | "value";

    public constructor(conflict: "key" | "value") {
        super(`An equivalent ${conflict} is already present.`);
        this.name = "BiMapConflictError";
        this.conflict = conflict;
    }
}

/** Immutable strict bijection backed by independent forward and inverse CHAMP maps. */
export class PersistentBiMap<K, V> implements Iterable<HamtEntry<K, V>> {
    readonly #forward: PersistentHashMap<K, V>;
    readonly #inverse: PersistentHashMap<V, K>;
    #inverseView: PersistentBiMap<V, K> | undefined;

    private constructor(forward: PersistentHashMap<K, V>, inverse: PersistentHashMap<V, K>) {
        this.#forward = forward;
        this.#inverse = inverse;
    }

    /** Returns an empty bimap retaining the exact independent policy objects. */
    public static empty<K, V>(
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentBiMap<K, V> {
        return new PersistentBiMap(
            PersistentHashMap.empty<K, V>(keyPolicy),
            PersistentHashMap.empty<V, K>(valuePolicy),
        );
    }

    /** Constructs a strict bimap, rejecting repeated classes in either domain. */
    public static from<K, V>(
        entries: Iterable<readonly [K, V]>,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valuePolicy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentBiMap<K, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentBiMap.empty<K, V>(keyPolicy, valuePolicy);
        for (const [key, value] of entries) result = result.add(key, value);
        return result;
    }

    /** Number of pairs. */
    public get size(): number { return this.#forward.size; }
    /** Whether the bimap holds no pairs. */
    public get isEmpty(): boolean { return this.#forward.isEmpty; }
    /** The retained hash policy defining key equivalence. */
    public get keyPolicy(): HashPolicy<K> { return this.#forward.policy; }
    /** The retained hash policy defining value equivalence. */
    public get valuePolicy(): HashPolicy<V> { return this.#inverse.policy; }

    /** Returns a cached O(1) facade over the existing inverse and forward roots. */
    public get inverse(): PersistentBiMap<V, K> {
        if (this.#inverseView !== undefined) return this.#inverseView;
        const inverse = new PersistentBiMap<V, K>(this.#inverse, this.#forward);
        inverse.#inverseView = this;
        this.#inverseView = inverse;
        return inverse;
    }

    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.#forward.containsKey(key); }
    /**
     * Whether the value is represented. Constant time, because the inverse map answers it directly.
     */
    public containsValue(value: V): boolean { return this.#inverse.containsKey(value); }

    /** Returns the stored value representative for a key class. */
    public get(key: K): BiMapLookup<V> {
        const entry = this.#forward.getEntry(key);
        return entry === undefined ? { found: false } : { found: true, value: entry.value };
    }

    /** Returns the stored key representative for a value class. */
    public getKey(value: V): BiMapLookup<K> {
        const entry = this.#inverse.getEntry(value);
        return entry === undefined ? { found: false } : { found: true, value: entry.value };
    }

    /** Strictly adds a pair when both equivalence classes are absent. */
    public add(key: K, value: V): PersistentBiMap<K, V> {
        const result = this.tryAdd(key, value);
        if (!result.added) throw new BiMapConflictError(result.conflict);
        return result.map;
    }

    /** Attempts strict addition without throwing for a represented key or value class. */
    public tryAdd(key: K, value: V): BiMapAddResult<K, V> {
        if (this.#forward.containsKey(key)) return { added: false, conflict: "key", map: this };
        if (this.#inverse.containsKey(value)) return { added: false, conflict: "value", map: this };
        return {
            added: true,
            map: new PersistentBiMap(this.#forward.add(key, value), this.#inverse.add(value, key)),
        };
    }

    /** Adds or replaces one key's value without displacing a different key. */
    public set(key: K, value: V): PersistentBiMap<K, V> {
        const previous = this.#forward.getEntry(key);
        if (previous === undefined) {
            if (this.#inverse.containsKey(value)) throw new BiMapConflictError("value");
            return new PersistentBiMap(this.#forward.add(key, value), this.#inverse.add(value, key));
        }
        if (this.valuePolicy.equivalent(previous.value, value)) return this;
        if (this.#inverse.containsKey(value)) throw new BiMapConflictError("value");

        const storedKey = this.#inverse.getEntry(previous.value);
        if (storedKey === undefined) throw new Error("PersistentBiMap invariant failure.");
        return new PersistentBiMap(
            this.#forward.remove(storedKey.value).add(storedKey.value, value),
            this.#inverse.remove(previous.value).add(value, storedKey.value),
        );
    }

    /** Remove the pair holding that key from both directions. */
    public removeKey(key: K): PersistentBiMap<K, V> { return this.tryRemoveKey(key).map; }

    /** Attempts removal through the key domain and returns the opposite stored representative. */
    public tryRemoveKey(key: K): BiMapRemoveResult<K, V, V> {
        const forwardEntry = this.#forward.getEntry(key);
        if (forwardEntry === undefined) return { removed: false, map: this };
        const inverseEntry = this.#inverse.getEntry(forwardEntry.value);
        if (inverseEntry === undefined) throw new Error("PersistentBiMap invariant failure.");
        return {
            removed: true,
            value: forwardEntry.value,
            map: new PersistentBiMap(
                this.#forward.remove(inverseEntry.value),
                this.#inverse.remove(forwardEntry.value),
            ),
        };
    }

    /** Remove the pair holding that value from both directions. */
    public removeValue(value: V): PersistentBiMap<K, V> { return this.tryRemoveValue(value).map; }

    /** Attempts removal through the value domain and returns the opposite stored representative. */
    public tryRemoveValue(value: V): BiMapRemoveResult<K, V, K> {
        const inverseEntry = this.#inverse.getEntry(value);
        if (inverseEntry === undefined) return { removed: false, map: this };
        const forwardEntry = this.#forward.getEntry(inverseEntry.value);
        if (forwardEntry === undefined) throw new Error("PersistentBiMap invariant failure.");
        return {
            removed: true,
            value: inverseEntry.value,
            map: new PersistentBiMap(
                this.#forward.remove(inverseEntry.value),
                this.#inverse.remove(forwardEntry.value),
            ),
        };
    }

    /** An empty bimap retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentBiMap<K, V> {
        return this.isEmpty ? this : PersistentBiMap.empty(this.keyPolicy, this.valuePolicy);
    }

    /** Iterate the keys. */
    public keys(): IterableIterator<K> { return this.#forward.keys(); }
    /** Iterate the values. */
    public values(): IterableIterator<V> { return this.#forward.values(); }
    /** Iterate the pairs. */
    public entries(): IterableIterator<HamtEntry<K, V>> { return this.#forward.entries(); }
    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> { return this.entries(); }

    /** Checks both directions of the bijection invariant. */
    public validateStructure(): boolean {
        if (this.#forward.size !== this.#inverse.size) return false;
        for (const entry of this.#forward) {
            const inverse = this.#inverse.getEntry(entry.value);
            if (inverse === undefined || !this.keyPolicy.equivalent(entry.key, inverse.value)) return false;
        }
        for (const entry of this.#inverse) {
            const forward = this.#forward.getEntry(entry.value);
            if (forward === undefined || !this.valuePolicy.equivalent(entry.key, forward.value)) return false;
        }
        return true;
    }
}
