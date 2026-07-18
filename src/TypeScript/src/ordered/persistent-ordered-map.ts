import { FingerTree } from "../finger-tree/core.js";
import type { MeasurePolicy } from "../finger-tree/measures.js";
import type { Comparator } from "../finger-tree/ordering.js";
import { defaultHashPolicy, type HashPolicy } from "../hamt/hash-policy.js";
import { DuplicateKeyError, PersistentHashMap } from "../hamt/persistent-hamt.js";

const stampStride = 1n << 20n;
const minimumStamp = -(1n << 63n);
const maximumStamp = (1n << 63n) - 1n;
const maximumSize = 0x7fff_ffff;

interface StoredOrderedMapEntry<K, V> {
    readonly stamp: bigint;
    readonly key: K;
    readonly value: V;
}

/** One stored key/value representative in explicit map order. */
export interface OrderedMapEntry<K, V> {
    readonly key: K;
    readonly value: V;
}

/** Presence-discriminated keyed lookup. */
export type OrderedMapLookup<K, V> =
    | { readonly found: true; readonly entry: OrderedMapEntry<K, V> }
    | { readonly found: false };

/** Nonthrowing strict-add result. */
export interface OrderedMapAddResult<K, V> {
    readonly added: boolean;
    readonly map: PersistentOrderedMap<K, V>;
}

/** Keyed removal result; misses retain the exact receiver. */
export type OrderedMapRemoveResult<K, V> =
    | { readonly removed: true; readonly entry: OrderedMapEntry<K, V>; readonly map: PersistentOrderedMap<K, V> }
    | { readonly removed: false; readonly map: PersistentOrderedMap<K, V> };

/** Raised when explicit movement names an absent key class. */
export class OrderedMapMissingKeyError extends Error {
    public constructor() {
        super("The key is absent from the ordered map.");
        this.name = "OrderedMapMissingKeyError";
    }
}

const sharedOrderPolicy: MeasurePolicy<StoredOrderedMapEntry<unknown, unknown>, bigint | undefined> = {
    identity: undefined,
    combine: (left, right): bigint | undefined => right === undefined ? left : right,
    measure: (entry): bigint => entry.stamp,
};

function orderPolicy<K, V>(): MeasurePolicy<StoredOrderedMapEntry<K, V>, bigint | undefined> {
    return sharedOrderPolicy as MeasurePolicy<StoredOrderedMapEntry<K, V>, bigint | undefined>;
}

function sameValueZero<T>(left: T, right: T): boolean {
    return left === right || (left !== left && right !== right);
}

function requireInsertionIndex(index: number, size: number): void {
    if (!Number.isInteger(index) || index < 0 || index > size) {
        throw new RangeError("index must be an integer from 0 through the map size.");
    }
}

function requireElementIndex(index: number, size: number): void {
    if (!Number.isInteger(index) || index < 0 || index >= size) {
        throw new RangeError("index must identify an entry in the map.");
    }
}

/**
 * Immutable insertion-ordered map with explicit positional reordering.
 *
 * Key equivalence and first-representative retention are defined by a {@link HashPolicy}. Values
 * occur only in the positional tree; the CHAMP index stores key-to-stamp navigation so arbitrary
 * payloads are not duplicated. Replacing a value never changes its key representative or position.
 */
export class PersistentOrderedMap<K, V> implements Iterable<OrderedMapEntry<K, V>> {
    readonly #order: FingerTree<StoredOrderedMapEntry<K, V>, bigint | undefined>;
    readonly #stamps: PersistentHashMap<K, bigint>;

    private constructor(
        order: FingerTree<StoredOrderedMapEntry<K, V>, bigint | undefined>,
        stamps: PersistentHashMap<K, bigint>,
        public readonly valueEquals: (left: V, right: V) => boolean,
    ) {
        this.#order = order;
        this.#stamps = stamps;
    }

    public static empty<K, V>(
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentOrderedMap<K, V> {
        return new PersistentOrderedMap(
            FingerTree.empty(orderPolicy<K, V>()),
            PersistentHashMap.empty<K, bigint>(keyPolicy),
            valueEquals,
        );
    }

    /** Builds in one input order: the first key and position and last distinct value win. */
    public static from<K, V>(
        entries: Iterable<readonly [K, V]>,
        keyPolicy: HashPolicy<K> = defaultHashPolicy<K>(),
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentOrderedMap<K, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentOrderedMap.empty<K, V>(keyPolicy, valueEquals);
        for (const [key, value] of entries) result = result.set(key, value);
        return result;
    }

    public get size(): number { return this.#order.size; }
    public get count(): number { return this.size; }
    public get isEmpty(): boolean { return this.#order.isEmpty; }
    public get keyPolicy(): HashPolicy<K> { return this.#stamps.policy; }

    public get first(): OrderedMapEntry<K, V> {
        const entry = this.#order.front();
        if (entry === undefined) throw new Error("The ordered map is empty.");
        return { key: entry.key, value: entry.value };
    }

    public get last(): OrderedMapEntry<K, V> {
        const entry = this.#order.back();
        if (entry === undefined) throw new Error("The ordered map is empty.");
        return { key: entry.key, value: entry.value };
    }

    public containsKey(key: K): boolean { return this.#stamps.containsKey(key); }

    public get(key: K): V | undefined {
        const lookup = this.tryGetEntry(key);
        return lookup.found ? lookup.entry.value : undefined;
    }

    public tryGetEntry(key: K): OrderedMapLookup<K, V> {
        const indexed = this.#stamps.getEntry(key);
        if (indexed === undefined) return { found: false };
        const entry = this.#order.get(this.indexOfStamp(indexed.value));
        if (entry === undefined) throw new Error("The ordered map's indexes disagree.");
        return { found: true, entry: { key: entry.key, value: entry.value } };
    }

    public getStoredKey(key: K): K | undefined { return this.#stamps.getEntry(key)?.key; }

    public entryAt(index: number): OrderedMapEntry<K, V> {
        requireElementIndex(index, this.size);
        const entry = this.#order.get(index)!;
        return { key: entry.key, value: entry.value };
    }

    public indexOfKey(key: K): number {
        const indexed = this.#stamps.getEntry(key);
        return indexed === undefined ? -1 : this.indexOfStamp(indexed.value);
    }

    /** Creates an immutable explicit-order gap cursor at a position from zero through size. */
    public getCursor(position = 0): PersistentOrderedMapCursor<K, V> {
        return new PersistentOrderedMapCursor(this, position);
    }

    /** Locates a key equivalence class; a miss returns the append-position cursor. */
    public getCursorAtKey(key: K): {
        readonly found: boolean;
        readonly cursor: PersistentOrderedMapCursor<K, V>;
    } {
        const position = this.indexOfKey(key);
        return {
            found: position >= 0,
            cursor: new PersistentOrderedMapCursor(this, position < 0 ? this.size : position),
        };
    }

    public add(key: K, value: V): PersistentOrderedMap<K, V> {
        const result = this.tryAdd(key, value);
        if (!result.added) throw new DuplicateKeyError();
        return result.map;
    }

    public tryAdd(key: K, value: V): OrderedMapAddResult<K, V> {
        return this.insertCore(this.size, key, value);
    }

    public addFirst(key: K, value: V): PersistentOrderedMap<K, V> {
        const result = this.insertCore(0, key, value);
        if (!result.added) throw new DuplicateKeyError();
        return result.map;
    }

    public insert(index: number, key: K, value: V): PersistentOrderedMap<K, V> {
        requireInsertionIndex(index, this.size);
        const result = this.insertCore(index, key, value);
        if (!result.added) throw new DuplicateKeyError();
        return result.map;
    }

    /** Adds or replaces a value without changing its stored key or position. */
    public set(key: K, value: V): PersistentOrderedMap<K, V> {
        const indexed = this.#stamps.getEntry(key);
        if (indexed === undefined) return this.add(key, value);
        const index = this.indexOfStamp(indexed.value);
        const stored = this.#order.get(index)!;
        if (this.valueEquals(stored.value, value)) return this;
        return new PersistentOrderedMap(
            this.#order.setItem(index, { stamp: stored.stamp, key: stored.key, value })!,
            this.#stamps,
            this.valueEquals,
        );
    }

    public moveToFirst(key: K): PersistentOrderedMap<K, V> { return this.moveExisting(0, key); }

    public moveToLast(key: K): PersistentOrderedMap<K, V> {
        if (this.isEmpty) throw new OrderedMapMissingKeyError();
        return this.moveExisting(this.size - 1, key);
    }

    public moveTo(index: number, key: K): PersistentOrderedMap<K, V> {
        requireElementIndex(index, this.size);
        return this.moveExisting(index, key);
    }

    public remove(key: K): PersistentOrderedMap<K, V> { return this.tryRemove(key).map; }

    public tryRemove(key: K): OrderedMapRemoveResult<K, V> {
        const removed = this.#stamps.tryRemoveEntry(key);
        if (removed === undefined) return { removed: false, map: this };
        const index = this.indexOfStamp(removed.entry.value);
        const entry = this.#order.get(index)!;
        return {
            removed: true,
            entry: { key: entry.key, value: entry.value },
            map: PersistentOrderedMap.wrap(this.#order.removeAt(index)!, removed.map, this.valueEquals),
        };
    }

    public removeAt(index: number): PersistentOrderedMap<K, V> {
        requireElementIndex(index, this.size);
        const entry = this.#order.get(index)!;
        const removed = this.#stamps.tryRemoveEntry(entry.key);
        if (removed === undefined || removed.entry.value !== entry.stamp) {
            throw new Error("The ordered map's indexes disagree.");
        }
        return PersistentOrderedMap.wrap(this.#order.removeAt(index)!, removed.map, this.valueEquals);
    }

    public removeFirst(): PersistentOrderedMap<K, V> {
        if (this.isEmpty) throw new Error("The ordered map is empty.");
        return this.removeAt(0);
    }

    public removeLast(): PersistentOrderedMap<K, V> {
        if (this.isEmpty) throw new Error("The ordered map is empty.");
        return this.removeAt(this.size - 1);
    }

    public clear(): PersistentOrderedMap<K, V> {
        return this.isEmpty ? this : PersistentOrderedMap.empty(this.keyPolicy, this.valueEquals);
    }

    public getRange(index: number, count: number): PersistentOrderedMap<K, V> {
        requireInsertionIndex(index, this.size);
        if (!Number.isInteger(count) || count < 0 || count > this.size - index) {
            throw new RangeError("count extends past the end of the map.");
        }
        if (count === this.size) return this;
        if (count === 0) return PersistentOrderedMap.empty(this.keyPolicy, this.valueEquals);

        const first = this.#order.splitAtIndex(index)!;
        const second = first.right.splitAtIndex(count)!;
        const kept = second.left;
        const removedCount = this.size - count;
        let stamps: PersistentHashMap<K, bigint>;
        if (count <= removedCount) {
            stamps = PersistentOrderedMap.buildIndex(kept, this.keyPolicy);
        } else {
            stamps = this.#stamps;
            for (const entry of first.left) stamps = stamps.remove(entry.key);
            for (const entry of second.right) stamps = stamps.remove(entry.key);
        }
        return new PersistentOrderedMap(kept, stamps, this.valueEquals);
    }

    public take(count: number): PersistentOrderedMap<K, V> {
        if (!Number.isInteger(count) || count < 0 || count > this.size) throw new RangeError("count is outside the map.");
        return this.getRange(0, count);
    }

    public drop(count: number): PersistentOrderedMap<K, V> {
        if (!Number.isInteger(count) || count < 0 || count > this.size) throw new RangeError("count is outside the map.");
        return this.getRange(count, this.size - count);
    }

    public reverse(): PersistentOrderedMap<K, V> {
        if (this.size <= 1) return this;
        return PersistentOrderedMap.buildFromDistinct(
            this.toArray().reverse(), this.keyPolicy, this.valueEquals,
        );
    }

    /** Performs a stable one-shot positional sort; ties retain the previous explicit order. */
    public sort(comparator: Comparator<OrderedMapEntry<K, V>>): PersistentOrderedMap<K, V> {
        if (typeof comparator !== "function") throw new TypeError("comparator must be a function.");
        if (this.size <= 1) return this;
        const original = this.#order.toArray();
        const sorted = original.slice();
        sorted.sort((left, right): number => {
            const compared = comparator(
                { key: left.key, value: left.value },
                { key: right.key, value: right.value },
            );
            return compared !== 0 ? compared : left.stamp < right.stamp ? -1 : left.stamp > right.stamp ? 1 : 0;
        });
        if (sorted.every((entry, index) => entry.stamp === original[index]!.stamp)) return this;
        return PersistentOrderedMap.buildFromDistinct(
            sorted.map(({ key, value }) => ({ key, value })), this.keyPolicy, this.valueEquals,
        );
    }

    public toArray(): OrderedMapEntry<K, V>[] { return Array.from(this); }
    public *keys(): Generator<K, void> { for (const entry of this.#order) yield entry.key; }
    public *values(): Generator<V, void> { for (const entry of this.#order) yield entry.value; }
    public *[Symbol.iterator](): IterableIterator<OrderedMapEntry<K, V>> {
        for (const entry of this.#order) yield { key: entry.key, value: entry.value };
    }

    public sharesOrderStorageWith(other: PersistentOrderedMap<K, V>): boolean {
        return this.#order.sharesStorageWith(other.#order);
    }

    public sharesMembershipRootWith(other: PersistentOrderedMap<K, V>): boolean {
        return this.#stamps.sharesRootWith(other.#stamps);
    }

    /** Recomputes the independently owned order/index invariants. */
    public validateStructure(): { readonly count: number } {
        if (this.#order.size !== this.#stamps.size) throw new Error("The ordered map indexes have different counts.");
        let previous: bigint | undefined;
        for (const entry of this.#order) {
            if (previous !== undefined && entry.stamp <= previous) throw new Error("Ordered-map stamps are not ascending.");
            previous = entry.stamp;
            const indexed = this.#stamps.getEntry(entry.key);
            if (indexed === undefined || indexed.value !== entry.stamp || !Object.is(indexed.key, entry.key)) {
                throw new Error("The ordered map indexes disagree about a representative or stamp.");
            }
        }
        for (const indexed of this.#stamps) {
            const ordered = this.#order.get(this.indexOfStamp(indexed.value));
            if (ordered === undefined || !Object.is(indexed.key, ordered.key)) {
                throw new Error("An indexed key has no corresponding ordered representative.");
            }
        }
        return { count: this.size };
    }

    private insertCore(index: number, key: K, value: V): OrderedMapAddResult<K, V> {
        const provisional = this.#stamps.tryAdd(key, 0n);
        if (!provisional.added) return { added: false, map: this };
        if (this.size === maximumSize) throw new RangeError("The operation would exceed the ordered-map size limit.");

        const stamp = PersistentOrderedMap.tryPickStamp(this.#order, index);
        if (stamp === undefined) {
            // Only an exhausted sparse-label gap triggers a full deterministic relabel.
            const entries = this.toArray();
            entries.splice(index, 0, { key, value });
            return {
                added: true,
                map: PersistentOrderedMap.buildFromDistinct(entries, this.keyPolicy, this.valueEquals),
            };
        }

        const entry = { stamp, key, value };
        const order = index === 0
            ? this.#order.prepend(entry)
            : index === this.size
                ? this.#order.append(entry)
                : this.#order.insertAt(index, entry)!;
        return {
            added: true,
            map: new PersistentOrderedMap(order, provisional.value.put(key, stamp), this.valueEquals),
        };
    }

    private moveExisting(finalIndex: number, key: K): PersistentOrderedMap<K, V> {
        const indexed = this.#stamps.getEntry(key);
        if (indexed === undefined) throw new OrderedMapMissingKeyError();
        const oldIndex = this.indexOfStamp(indexed.value);
        if (oldIndex === finalIndex) return this;

        const stored = this.#order.get(oldIndex)!;
        const trimmed = this.#order.removeAt(oldIndex)!;
        const stamp = PersistentOrderedMap.tryPickStamp(trimmed, finalIndex);
        if (stamp === undefined) {
            const entries = Array.from(trimmed, ({ key: itemKey, value }) => ({ key: itemKey, value }));
            entries.splice(finalIndex, 0, { key: stored.key, value: stored.value });
            return PersistentOrderedMap.buildFromDistinct(entries, this.keyPolicy, this.valueEquals);
        }

        const moved = { stamp, key: stored.key, value: stored.value };
        const order = finalIndex === 0
            ? trimmed.prepend(moved)
            : finalIndex === trimmed.size
                ? trimmed.append(moved)
                : trimmed.insertAt(finalIndex, moved)!;
        return new PersistentOrderedMap(order, this.#stamps.put(stored.key, stamp), this.valueEquals);
    }

    private indexOfStamp(stamp: bigint): number {
        const located = this.#order.tryLocate((last) => last !== undefined && last >= stamp);
        if (!located.found || located.item?.stamp !== stamp) throw new Error("The ordered map's indexes disagree.");
        return located.index;
    }

    private static tryPickStamp<K, V>(
        order: FingerTree<StoredOrderedMapEntry<K, V>, bigint | undefined>,
        index: number,
    ): bigint | undefined {
        if (order.isEmpty) return 0n;
        if (index === 0) {
            const first = order.front()!.stamp;
            return first < minimumStamp + stampStride ? undefined : first - stampStride;
        }
        if (index === order.size) {
            const last = order.back()!.stamp;
            return last > maximumStamp - stampStride ? undefined : last + stampStride;
        }
        const left = order.get(index - 1)!.stamp;
        const right = order.get(index)!.stamp;
        return right - left < 2n ? undefined : left + (right - left) / 2n;
    }

    private static buildFromDistinct<K, V>(
        entries: readonly OrderedMapEntry<K, V>[],
        keyPolicy: HashPolicy<K>,
        valueEquals: (left: V, right: V) => boolean,
    ): PersistentOrderedMap<K, V> {
        if (entries.length === 0) return PersistentOrderedMap.empty(keyPolicy, valueEquals);
        const ordered: StoredOrderedMapEntry<K, V>[] = new Array(entries.length);
        const indexed: Array<readonly [K, bigint]> = new Array(entries.length);
        for (let index = 0; index < entries.length; index++) {
            const entry = entries[index]!;
            const stamp = BigInt(index) * stampStride;
            ordered[index] = { stamp, key: entry.key, value: entry.value };
            indexed[index] = [entry.key, stamp];
        }
        return new PersistentOrderedMap(
            FingerTree.from(ordered, orderPolicy<K, V>()),
            PersistentHashMap.from(indexed, keyPolicy),
            valueEquals,
        );
    }

    private static buildIndex<K, V>(
        order: FingerTree<StoredOrderedMapEntry<K, V>, bigint | undefined>,
        keyPolicy: HashPolicy<K>,
    ): PersistentHashMap<K, bigint> {
        return PersistentHashMap.from(
            Array.from(order, (entry): readonly [K, bigint] => [entry.key, entry.stamp]),
            keyPolicy,
        );
    }

    private static wrap<K, V>(
        order: FingerTree<StoredOrderedMapEntry<K, V>, bigint | undefined>,
        stamps: PersistentHashMap<K, bigint>,
        valueEquals: (left: V, right: V) => boolean,
    ): PersistentOrderedMap<K, V> {
        return order.isEmpty
            ? PersistentOrderedMap.empty(stamps.policy, valueEquals)
            : new PersistentOrderedMap(order, stamps, valueEquals);
    }
}

/** Immutable root-plus-position gap cursor over a persistent ordered map. */
export class PersistentOrderedMapCursor<K, V> {
    public constructor(
        public readonly snapshot: PersistentOrderedMap<K, V>,
        public readonly position: number,
    ) {
        requireInsertionIndex(position, snapshot.size);
    }

    public get size(): number { return this.snapshot.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.size; }

    public tryPeekPrevious(): OrderedMapEntry<K, V> | undefined {
        return this.isAtStart ? undefined : this.snapshot.entryAt(this.position - 1);
    }

    public tryPeekNext(): OrderedMapEntry<K, V> | undefined {
        return this.isAtEnd ? undefined : this.snapshot.entryAt(this.position);
    }

    public movePrevious(): PersistentOrderedMapCursor<K, V> {
        if (this.isAtStart) throw new RangeError("The ordered-map cursor is already at the start.");
        return new PersistentOrderedMapCursor(this.snapshot, this.position - 1);
    }

    public moveNext(): PersistentOrderedMapCursor<K, V> {
        if (this.isAtEnd) throw new RangeError("The ordered-map cursor is already at the end.");
        return new PersistentOrderedMapCursor(this.snapshot, this.position + 1);
    }

    public seek(position: number): PersistentOrderedMapCursor<K, V> {
        return position === this.position ? this : new PersistentOrderedMapCursor(this.snapshot, position);
    }

    public insert(key: K, value: V): PersistentOrderedMapCursor<K, V> {
        return new PersistentOrderedMapCursor(
            this.snapshot.insert(this.position, key, value),
            this.position + 1,
        );
    }

    public tryInsert(key: K, value: V): {
        readonly inserted: boolean;
        readonly cursor: PersistentOrderedMapCursor<K, V>;
    } {
        const existing = this.snapshot.indexOfKey(key);
        return existing >= 0
            ? { inserted: false, cursor: new PersistentOrderedMapCursor(this.snapshot, existing) }
            : { inserted: true, cursor: this.insert(key, value) };
    }

    public setNextValue(value: V): PersistentOrderedMapCursor<K, V> {
        const entry = this.tryPeekNext();
        if (entry === undefined) throw new RangeError("The ordered-map cursor has no next entry.");
        return new PersistentOrderedMapCursor(this.snapshot.set(entry.key, value), this.position);
    }

    public deletePrevious(): PersistentOrderedMapCursor<K, V> {
        if (this.isAtStart) throw new RangeError("The ordered-map cursor has no previous entry.");
        return new PersistentOrderedMapCursor(
            this.snapshot.removeAt(this.position - 1),
            this.position - 1,
        );
    }

    public deleteNext(): PersistentOrderedMapCursor<K, V> {
        if (this.isAtEnd) throw new RangeError("The ordered-map cursor has no next entry.");
        return new PersistentOrderedMapCursor(this.snapshot.removeAt(this.position), this.position);
    }
}
