import { defaultHashPolicy, sameValueZero, type HashPolicy } from "./hash-policy.js";
import { DuplicateKeyError, PersistentHashMap } from "./persistent-hamt.js";

/** Presence-safe patch state; a present `undefined` remains distinct from absence. */
export type MapPatchValue<V> =
    | { readonly present: false }
    | { readonly present: true; readonly value: V };

/** One strict before/after change in a persistent map patch. */
export interface MapPatchEntry<K, V> {
    readonly key: K;
    readonly before: MapPatchValue<V>;
    readonly after: MapPatchValue<V>;
}

interface MapPatchChange<V> {
    readonly before: MapPatchValue<V>;
    readonly after: MapPatchValue<V>;
}

/** Raised when strict application observes a state other than the recorded `before` state. */
export class MapPatchConflictError<K, V> extends Error {
    public constructor(
        public readonly key: K,
        public readonly expected: MapPatchValue<V>,
        public readonly actual: MapPatchValue<V>,
    ) {
        super("The map does not match the patch's expected state.");
        this.name = "MapPatchConflictError";
    }
}

/** Raised when adjacent patches disagree about their intermediate state. */
export class MapPatchCompositionError<K, V> extends Error {
    public constructor(
        public readonly key: K,
        public readonly leftAfter: MapPatchValue<V>,
        public readonly rightBefore: MapPatchValue<V>,
    ) {
        super("The patches disagree about their intermediate map state.");
        this.name = "MapPatchCompositionError";
    }
}

/** Creates a presence-safe absent patch value. */
export function absentMapPatchValue<V>(): MapPatchValue<V> { return { present: false }; }

/** Creates a presence-safe present patch value, including a present `undefined`. */
export function presentMapPatchValue<V>(value: V): MapPatchValue<V> { return { present: true, value }; }

/** Immutable, invertible set of strict changes between compatible persistent hash maps. */
export class PersistentMapPatch<K, V> implements Iterable<MapPatchEntry<K, V>> {
    readonly #changes: PersistentHashMap<K, MapPatchChange<V>>;

    private constructor(
        changes: PersistentHashMap<K, MapPatchChange<V>>,
        public readonly valueEquals: (left: V, right: V) => boolean,
    ) {
        this.#changes = changes;
    }

    public static empty<K, V>(
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentMapPatch<K, V> {
        return new PersistentMapPatch(PersistentHashMap.empty(keyPolicy), valueEquals);
    }

    public static from<K, V>(
        entries: Iterable<MapPatchEntry<K, V>>,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentMapPatch<K, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let changes = PersistentHashMap.empty<K, MapPatchChange<V>>(keyPolicy);
        for (const entry of entries) {
            if (PersistentMapPatch.statesEqual(entry.before, entry.after, valueEquals)) continue;
            const added = changes.tryAdd(entry.key, { before: entry.before, after: entry.after });
            if (!added.added) throw new DuplicateKeyError("An equivalent changed key is already present.");
            changes = added.value;
        }
        return new PersistentMapPatch(changes, valueEquals);
    }

    public static between<K, V>(
        before: PersistentHashMap<K, V>,
        after: PersistentHashMap<K, V>,
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentMapPatch<K, V> {
        PersistentMapPatch.requireMapPolicy(before, after);
        if (before.sharesRootWith(after)) return PersistentMapPatch.empty(before.policy, valueEquals);
        let changes = PersistentHashMap.empty<K, MapPatchChange<V>>(before.policy);
        for (const entry of before) {
            const next = after.getEntry(entry.key);
            if (next === undefined) {
                changes = changes.add(entry.key, {
                    before: presentMapPatchValue(entry.value),
                    after: absentMapPatchValue(),
                });
            } else if (!valueEquals(entry.value, next.value)) {
                changes = changes.add(entry.key, {
                    before: presentMapPatchValue(entry.value),
                    after: presentMapPatchValue(next.value),
                });
            }
        }
        for (const entry of after) {
            if (!before.containsKey(entry.key)) {
                changes = changes.add(entry.key, {
                    before: absentMapPatchValue(),
                    after: presentMapPatchValue(entry.value),
                });
            }
        }
        return new PersistentMapPatch(changes, valueEquals);
    }

    public get size(): number { return this.#changes.size; }
    public get count(): number { return this.size; }
    public get isEmpty(): boolean { return this.#changes.isEmpty; }
    public get keyPolicy(): HashPolicy<K> { return this.#changes.policy; }
    public keys(): IterableIterator<K> { return this.#changes.keys(); }

    public apply(source: PersistentHashMap<K, V>): PersistentHashMap<K, V> {
        this.requireCompatibleMap(source);
        for (const entry of this.#changes) {
            const actualEntry = source.getEntry(entry.key);
            const actual = actualEntry === undefined
                ? absentMapPatchValue<V>()
                : presentMapPatchValue(actualEntry.value);
            if (!PersistentMapPatch.statesEqual(entry.value.before, actual, this.valueEquals)) {
                throw new MapPatchConflictError(entry.key, entry.value.before, actual);
            }
        }

        let result = source;
        for (const entry of this.#changes) {
            result = entry.value.after.present
                ? result.put(entry.key, entry.value.after.value)
                : result.remove(entry.key);
        }
        return result;
    }

    public invert(): PersistentMapPatch<K, V> {
        if (this.isEmpty) return this;
        let changes = PersistentHashMap.empty<K, MapPatchChange<V>>(this.keyPolicy);
        for (const entry of this.#changes) {
            changes = changes.add(entry.key, { before: entry.value.after, after: entry.value.before });
        }
        return new PersistentMapPatch(changes, this.valueEquals);
    }

    public compose(next: PersistentMapPatch<K, V>): PersistentMapPatch<K, V> {
        this.requireCompatiblePatch(next);
        if (this.isEmpty) return next;
        if (next.isEmpty) return this;
        let changes = PersistentHashMap.empty<K, MapPatchChange<V>>(this.keyPolicy);
        for (const entry of this.#changes) {
            const right = next.#changes.getEntry(entry.key);
            if (right === undefined) {
                changes = changes.add(entry.key, entry.value);
                continue;
            }
            if (!PersistentMapPatch.statesEqual(entry.value.after, right.value.before, this.valueEquals)) {
                throw new MapPatchCompositionError(entry.key, entry.value.after, right.value.before);
            }
            if (!PersistentMapPatch.statesEqual(entry.value.before, right.value.after, this.valueEquals)) {
                changes = changes.add(entry.key, { before: entry.value.before, after: right.value.after });
            }
        }
        for (const entry of next.#changes) {
            if (!this.#changes.containsKey(entry.key)) changes = changes.add(entry.key, entry.value);
        }
        return new PersistentMapPatch(changes, this.valueEquals);
    }

    public clear(): PersistentMapPatch<K, V> {
        return this.isEmpty ? this : PersistentMapPatch.empty(this.keyPolicy, this.valueEquals);
    }

    public sharesRootWith(other: PersistentMapPatch<K, V>): boolean {
        return this.#changes.sharesRootWith(other.#changes);
    }

    public *[Symbol.iterator](): IterableIterator<MapPatchEntry<K, V>> {
        for (const entry of this.#changes) {
            yield { key: entry.key, before: entry.value.before, after: entry.value.after };
        }
    }

    public validateStructure(): { readonly count: number } {
        for (const entry of this.#changes) {
            if (PersistentMapPatch.statesEqual(entry.value.before, entry.value.after, this.valueEquals)) {
                throw new Error("A map patch stores a no-op change.");
            }
        }
        return { count: this.size };
    }

    private requireCompatibleMap(map: PersistentHashMap<K, V>): void {
        if (map.policy !== this.keyPolicy) throw new TypeError("The map and patch must retain the same key policy object.");
    }

    private requireCompatiblePatch(other: PersistentMapPatch<K, V>): void {
        if (other.keyPolicy !== this.keyPolicy || other.valueEquals !== this.valueEquals) {
            throw new TypeError("Composed patches must retain the same key and value policy objects.");
        }
    }

    private static requireMapPolicy<K, V>(left: PersistentHashMap<K, V>, right: PersistentHashMap<K, V>): void {
        if (left.policy !== right.policy) throw new TypeError("The maps must retain the same key policy object.");
    }

    private static statesEqual<V>(
        left: MapPatchValue<V>,
        right: MapPatchValue<V>,
        valueEquals: (left: V, right: V) => boolean,
    ): boolean {
        return left.present === right.present
            && (!left.present || (right.present && valueEquals(left.value, right.value)));
    }
}
