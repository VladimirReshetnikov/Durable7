import { FingerTree } from "../finger-tree/core.js";
import { type MeasurePolicy } from "../finger-tree/measures.js";
import { defaultComparator, type Comparator } from "../finger-tree/ordering.js";
import { defaultHashPolicy, type HashPolicy } from "../hamt/hash-policy.js";
import { PersistentHashMap } from "../hamt/persistent-hamt.js";

const stampStride = 1n << 20n;
const minimumStamp = -(1n << 63n);
const maximumStamp = (1n << 63n) - 1n;
const maximumSize = 0x7fff_ffff;

interface OrderedEntry<T> {
    readonly stamp: bigint;
    readonly item: T;
}

const sharedOrderPolicy: MeasurePolicy<OrderedEntry<unknown>, bigint | undefined> = {
    identity: undefined,
    combine: (
        left: bigint | undefined,
        right: bigint | undefined,
    ): bigint | undefined => right === undefined ? left : right,
    measure: (entry: OrderedEntry<unknown>): bigint => entry.stamp,
};

function orderPolicy<T>(): MeasurePolicy<OrderedEntry<T>, bigint | undefined> {
    return sharedOrderPolicy as MeasurePolicy<OrderedEntry<T>, bigint | undefined>;
}

function requireIterable<T>(items: Iterable<T>): void {
    if (items === null || items === undefined) throw new TypeError("items must be iterable.");
}

function requireInsertionIndex(index: number, size: number): void {
    if (!Number.isInteger(index) || index < 0 || index > size) {
        throw new RangeError("index must be an integer from 0 through the set size.");
    }
}

function requireElementIndex(index: number, size: number): void {
    if (!Number.isInteger(index) || index < 0 || index >= size) {
        throw new RangeError("index must identify an element in the set.");
    }
}

function requireCount(count: number, size: number): void {
    if (!Number.isInteger(count) || count < 0 || count > size) {
        throw new RangeError("count must be an integer from 0 through the set size.");
    }
}

function addChecked<T>(target: T[], item: T): void {
    if (target.length === maximumSize) {
        throw new RangeError("The operation would exceed the ordered-set size limit.");
    }
    target.push(item);
}

/** Presence-discriminated stored-representative lookup result. */
export type OrderedSetLookup<T> =
    | { readonly found: true; readonly value: T }
    | { readonly found: false; readonly value: T };

/** Presence-discriminated cursor peek that remains unambiguous for stored undefined values. */
export type OrderedSetCursorPeek<T> =
    | { readonly found: true; readonly value: T }
    | { readonly found: false };

/** Result of a removal attempt; misses retain the exact receiver. */
export interface OrderedSetRemoveResult<T> {
    readonly removed: boolean;
    readonly set: PersistentOrderedSet<T>;
}

/** Public diagnostics for the independently owned order/membership invariants. */
export interface PersistentOrderedSetStatistics {
    readonly count: number;
}

/** Raised when an explicit movement operation names an absent equivalence class. */
export class OrderedSetMissingValueError extends Error {
    public constructor() {
        super("The value is absent from the ordered set.");
        this.name = "OrderedSetMissingValueError";
    }
}

/**
 * Immutable insertion-ordered set with explicit positional reordering.
 *
 * Membership and first-representative retention are defined by a {@link HashPolicy}; enumeration
 * follows insertion or explicitly requested order. This general-purpose type depends only on the
 * repository's HAMT and FingerTree families and is independent of Tungsten collections.
 */
export class PersistentOrderedSet<T> implements Iterable<T> {
    static readonly #sharedDefaultEmpty = new PersistentOrderedSet<unknown>(
        FingerTree.empty(orderPolicy<unknown>()),
        PersistentHashMap.empty<unknown, bigint>(),
    );

    readonly #order: FingerTree<OrderedEntry<T>, bigint | undefined>;
    readonly #stamps: PersistentHashMap<T, bigint>;

    private constructor(
        order: FingerTree<OrderedEntry<T>, bigint | undefined>,
        stamps: PersistentHashMap<T, bigint>,
    ) {
        this.#order = order;
        this.#stamps = stamps;
    }

    /** Returns an empty set retaining the exact supplied hash/equality policy object. */
    public static empty<T>(
        policy: HashPolicy<T> = defaultHashPolicy<T>(),
    ): PersistentOrderedSet<T> {
        if (policy === defaultHashPolicy<T>()) {
            return PersistentOrderedSet.#sharedDefaultEmpty as PersistentOrderedSet<T>;
        }
        return new PersistentOrderedSet(
            FingerTree.empty(orderPolicy<T>()),
            PersistentHashMap.empty<T, bigint>(policy),
        );
    }

    /** C#-surface spelling for an empty comparer/policy-preserving set. */
    public static create<T>(
        policy: HashPolicy<T> = defaultHashPolicy<T>(),
    ): PersistentOrderedSet<T> {
        return PersistentOrderedSet.empty(policy);
    }

    /**
     * Builds from one pass over the source, retaining each class's first representative and position.
     */
    public static from<T>(
        items: Iterable<T>,
        policy: HashPolicy<T> = defaultHashPolicy<T>(),
    ): PersistentOrderedSet<T> {
        requireIterable(items);
        let seen = PersistentHashMap.empty<T, true>(policy);
        const distinct: T[] = [];
        for (const item of items) {
            const added = seen.tryAdd(item, true);
            if (!added.added) continue;
            seen = added.value;
            addChecked(distinct, item);
        }
        return PersistentOrderedSet.buildFromItems(distinct, policy);
    }

    /** C#-surface spelling for {@link from}. */
    public static createRange<T>(
        items: Iterable<T>,
        policy: HashPolicy<T> = defaultHashPolicy<T>(),
    ): PersistentOrderedSet<T> {
        return PersistentOrderedSet.from(items, policy);
    }

    public get size(): number { return this.#order.size; }
    public get count(): number { return this.size; }
    public get isEmpty(): boolean { return this.#order.isEmpty; }
    public get policy(): HashPolicy<T> { return this.#stamps.policy; }

    /** Gets the first representative, throwing when the set is empty. */
    public get first(): T {
        const entry = this.#order.front();
        if (entry === undefined) throw new Error("The ordered set is empty.");
        return entry.item;
    }

    /** Gets the last representative, throwing when the set is empty. */
    public get last(): T {
        const entry = this.#order.back();
        if (entry === undefined) throw new Error("The ordered set is empty.");
        return entry.item;
    }

    public contains(item: T): boolean { return this.#stamps.containsKey(item); }

    /** Returns the stored first representative on a hit and echoes the lookup value on a miss. */
    public tryGetValue(equalValue: T): OrderedSetLookup<T> {
        const entry = this.#stamps.getEntry(equalValue);
        return entry === undefined
            ? { found: false, value: equalValue }
            : { found: true, value: entry.key };
    }

    public getAt(index: number): T {
        requireElementIndex(index, this.size);
        return this.#order.get(index)!.item;
    }

    public indexOf(equalValue: T): number {
        const entry = this.#stamps.getEntry(equalValue);
        return entry === undefined ? -1 : this.indexOfStamp(entry.value);
    }

    /** Creates an immutable explicit-order gap cursor at a position from zero through size. */
    public getCursor(position = 0): PersistentOrderedSetCursor<T> {
        return new PersistentOrderedSetCursor(this, position);
    }

    /** Locates an equivalence class; a miss returns the append-position cursor. */
    public getCursorAtItem(equalValue: T): {
        readonly found: boolean;
        readonly cursor: PersistentOrderedSetCursor<T>;
    } {
        const position = this.indexOf(equalValue);
        return {
            found: position >= 0,
            cursor: new PersistentOrderedSetCursor(this, position < 0 ? this.size : position),
        };
    }

    /** Appends an absent class; a duplicate is an exact identity no-op and never moves. */
    public add(item: T): PersistentOrderedSet<T> { return this.insertCore(this.size, item); }

    /** Prepends an absent class; a duplicate is an exact identity no-op and never moves. */
    public addFirst(item: T): PersistentOrderedSet<T> { return this.insertCore(0, item); }

    /** Inserts an absent class before an index, validating the index before hashing. */
    public insert(index: number, item: T): PersistentOrderedSet<T> {
        requireInsertionIndex(index, this.size);
        return this.insertCore(index, item);
    }

    public moveToFirst(equalValue: T): PersistentOrderedSet<T> {
        return this.moveExisting(0, equalValue);
    }

    public moveToLast(equalValue: T): PersistentOrderedSet<T> {
        return this.moveExisting(this.size - 1, equalValue);
    }

    /** Moves an existing class to its final result index, retaining the stored representative. */
    public moveTo(index: number, equalValue: T): PersistentOrderedSet<T> {
        requireElementIndex(index, this.size);
        return this.moveExisting(index, equalValue);
    }

    public remove(equalValue: T): PersistentOrderedSet<T> {
        return this.tryRemove(equalValue).set;
    }

    /** Tries to remove a class; a miss returns `{ removed: false, set: receiver }`. */
    public tryRemove(equalValue: T): OrderedSetRemoveResult<T> {
        const removed = this.#stamps.tryRemoveEntry(equalValue);
        if (removed === undefined) return { removed: false, set: this };

        const index = this.indexOfStamp(removed.entry.value);
        const order = this.#order.removeAt(index);
        if (order === undefined) throw new Error("The ordered set's indexes disagree.");
        return { removed: true, set: PersistentOrderedSet.wrap(order, removed.map) };
    }

    public removeAt(index: number): PersistentOrderedSet<T> {
        requireElementIndex(index, this.size);
        const entry = this.#order.get(index)!;
        const removed = this.#stamps.tryRemoveEntry(entry.item);
        if (removed === undefined || removed.entry.value !== entry.stamp) {
            throw new Error("The ordered set's indexes disagree.");
        }
        return PersistentOrderedSet.wrap(this.#order.removeAt(index)!, removed.map);
    }

    public removeFirst(): PersistentOrderedSet<T> {
        if (this.isEmpty) throw new Error("The ordered set is empty.");
        return this.removeAt(0);
    }

    public removeLast(): PersistentOrderedSet<T> {
        if (this.isEmpty) throw new Error("The ordered set is empty.");
        return this.removeAt(this.size - 1);
    }

    public clear(): PersistentOrderedSet<T> {
        return this.isEmpty ? this : PersistentOrderedSet.empty(this.policy);
    }

    /** Extracts a contiguous range while retaining the receiver policy. */
    public getRange(index: number, count: number): PersistentOrderedSet<T> {
        requireInsertionIndex(index, this.size);
        if (!Number.isInteger(count) || count < 0 || count > this.size - index) {
            throw new RangeError("count extends past the end of the set.");
        }
        if (count === this.size) return this;
        if (count === 0) return PersistentOrderedSet.empty(this.policy);

        const first = this.#order.splitAtIndex(index)!;
        const second = first.right.splitAtIndex(count)!;
        const kept = second.left;
        const removedCount = this.size - count;
        let stamps: PersistentHashMap<T, bigint>;
        if (count <= removedCount) {
            stamps = PersistentOrderedSet.buildIndex(kept, this.policy);
        } else {
            stamps = this.#stamps;
            for (const entry of first.left) stamps = stamps.remove(entry.item);
            for (const entry of second.right) stamps = stamps.remove(entry.item);
        }
        return new PersistentOrderedSet(kept, stamps);
    }

    public take(count: number): PersistentOrderedSet<T> {
        requireCount(count, this.size);
        return this.getRange(0, count);
    }

    public drop(count: number): PersistentOrderedSet<T> {
        requireCount(count, this.size);
        return this.getRange(count, this.size - count);
    }

    public reverse(): PersistentOrderedSet<T> {
        if (this.size <= 1) return this;
        return PersistentOrderedSet.buildFromItems(this.toArray().reverse(), this.policy);
    }

    /** Performs a stable one-shot reorder; later additions still append normally. */
    public sort(comparator: Comparator<T> = defaultComparator): PersistentOrderedSet<T> {
        if (this.size <= 1) return this;
        const original = this.#order.toArray();
        const sorted = original.slice();
        sorted.sort((left, right): number => {
            const byItem = comparator(left.item, right.item);
            if (byItem < 0) return -1;
            if (byItem > 0) return 1;
            return left.stamp < right.stamp ? -1 : left.stamp > right.stamp ? 1 : 0;
        });
        const unchanged = sorted.every((entry, index) => entry.stamp === original[index]!.stamp);
        return unchanged
            ? this
            : PersistentOrderedSet.buildFromItems(sorted.map((entry) => entry.item), this.policy);
    }

    /** Receiver-order union followed by first argument-only representatives in normalized order. */
    public union(other: Iterable<T>): PersistentOrderedSet<T> {
        const argument = this.normalize(other);
        let result: T[] | undefined;
        for (const item of argument.items) {
            if (this.#stamps.containsKey(item)) continue;
            result ??= this.toArray();
            addChecked(result, item);
        }
        return result === undefined ? this : PersistentOrderedSet.buildFromItems(result, this.policy);
    }

    /** Retains receiver representatives and receiver order for classes present in the argument. */
    public intersect(other: Iterable<T>): PersistentOrderedSet<T> {
        const argument = this.normalize(other);
        const result: T[] = [];
        for (const entry of this.#order) {
            if (argument.membership.containsKey(entry.item)) result.push(entry.item);
        }
        return result.length === this.size
            ? this
            : PersistentOrderedSet.buildFromItems(result, this.policy);
    }

    /** Removes normalized argument classes while retaining receiver representatives and order. */
    public except(other: Iterable<T>): PersistentOrderedSet<T> {
        const argument = this.normalize(other);
        const result: T[] = [];
        for (const entry of this.#order) {
            if (!argument.membership.containsKey(entry.item)) result.push(entry.item);
        }
        return result.length === this.size
            ? this
            : PersistentOrderedSet.buildFromItems(result, this.policy);
    }

    /** Receiver-only order followed by first normalized argument-only representatives. */
    public symmetricExcept(other: Iterable<T>): PersistentOrderedSet<T> {
        const argument = this.normalize(other);
        if (argument.items.length === 0) return this;

        const result: T[] = [];
        for (const entry of this.#order) {
            if (!argument.membership.containsKey(entry.item)) result.push(entry.item);
        }
        for (const item of argument.items) {
            if (!this.#stamps.containsKey(item)) addChecked(result, item);
        }
        return PersistentOrderedSet.buildFromItems(result, this.policy);
    }

    public isSubsetOf(other: Iterable<T>): boolean {
        const argument = this.normalize(other);
        return this.size <= argument.items.length && this.everyReceiverOccursIn(argument.membership);
    }

    public isProperSubsetOf(other: Iterable<T>): boolean {
        const argument = this.normalize(other);
        return this.size < argument.items.length && this.everyReceiverOccursIn(argument.membership);
    }

    public isSupersetOf(other: Iterable<T>): boolean {
        const argument = this.normalize(other);
        return this.size >= argument.items.length && this.everyArgumentOccursHere(argument.items);
    }

    public isProperSupersetOf(other: Iterable<T>): boolean {
        const argument = this.normalize(other);
        return this.size > argument.items.length && this.everyArgumentOccursHere(argument.items);
    }

    public overlaps(other: Iterable<T>): boolean {
        const argument = this.normalize(other);
        for (const item of argument.items) if (this.#stamps.containsKey(item)) return true;
        return false;
    }

    public setEquals(other: Iterable<T>): boolean {
        const argument = this.normalize(other);
        return this.size === argument.items.length && this.everyArgumentOccursHere(argument.items);
    }

    public toArray(): T[] { return Array.from(this); }

    public *[Symbol.iterator](): IterableIterator<T> {
        for (const entry of this.#order) yield entry.item;
    }

    /** Recomputes the independently owned dual-index invariants. */
    public validateStructure(): PersistentOrderedSetStatistics {
        if (this.#order.size !== this.#stamps.size) {
            throw new Error("The order and membership indexes have different counts.");
        }

        let previous: bigint | undefined;
        for (const entry of this.#order) {
            if (previous !== undefined && entry.stamp <= previous) {
                throw new Error("Order-maintenance stamps are not strictly ascending.");
            }
            previous = entry.stamp;
            const indexed = this.#stamps.getEntry(entry.item);
            if (indexed === undefined || indexed.value !== entry.stamp) {
                throw new Error("An ordered representative has no matching membership stamp.");
            }
            if (!Object.is(indexed.key, entry.item)) {
                throw new Error("The order and membership indexes retain different representatives.");
            }
        }

        for (const indexed of this.#stamps) {
            const position = this.indexOfStamp(indexed.value);
            const ordered = this.#order.get(position)!;
            if (!this.policy.equivalent(indexed.key, ordered.item) || !Object.is(indexed.key, ordered.item)) {
                throw new Error("A membership entry has no matching ordered representative.");
            }
        }
        return { count: this.size };
    }

    private insertCore(index: number, item: T): PersistentOrderedSet<T> {
        const stamp = PersistentOrderedSet.tryPickStamp(this.#order, index);
        if (stamp !== undefined) {
            const added = this.#stamps.tryAdd(item, stamp);
            if (!added.added) return this;
            if (this.size === maximumSize) {
                throw new RangeError("The operation would exceed the ordered-set size limit.");
            }
            const entry: OrderedEntry<T> = { stamp, item };
            const order = index === 0
                ? this.#order.prepend(entry)
                : index === this.size
                    ? this.#order.append(entry)
                    : this.#order.insertAt(index, entry)!;
            return new PersistentOrderedSet(order, added.value);
        }

        const provisional = this.#stamps.tryAdd(item, 0n);
        if (!provisional.added) return this;
        if (this.size === maximumSize) {
            throw new RangeError("The operation would exceed the ordered-set size limit.");
        }
        const entries = this.#order.toArray();
        entries.splice(index, 0, { stamp: 0n, item });
        return PersistentOrderedSet.buildFromItems(entries.map((entry) => entry.item), this.policy);
    }

    private moveExisting(finalIndex: number, equalValue: T): PersistentOrderedSet<T> {
        const indexed = this.#stamps.getEntry(equalValue);
        if (indexed === undefined) throw new OrderedSetMissingValueError();
        const oldIndex = this.indexOfStamp(indexed.value);
        if (oldIndex === finalIndex) return this;

        const stored = this.#order.get(oldIndex)!.item;
        const trimmed = this.#order.removeAt(oldIndex)!;
        const stamp = PersistentOrderedSet.tryPickStamp(trimmed, finalIndex);
        if (stamp === undefined) {
            const entries = trimmed.toArray();
            entries.splice(finalIndex, 0, { stamp: 0n, item: stored });
            return PersistentOrderedSet.buildFromItems(entries.map((entry) => entry.item), this.policy);
        }

        const entry: OrderedEntry<T> = { stamp, item: stored };
        const order = finalIndex === 0
            ? trimmed.prepend(entry)
            : finalIndex === trimmed.size
                ? trimmed.append(entry)
                : trimmed.insertAt(finalIndex, entry)!;
        return new PersistentOrderedSet(order, this.#stamps.put(stored, stamp));
    }

    private indexOfStamp(stamp: bigint): number {
        const located = this.#order.tryLocate(
            (last): boolean => last !== undefined && last >= stamp,
        );
        if (!located.found || located.item?.stamp !== stamp) {
            throw new Error("The ordered set's indexes disagree.");
        }
        return located.index;
    }

    private normalize(other: Iterable<T>): {
        readonly items: readonly T[];
        readonly membership: PersistentHashMap<T, true>;
    } {
        requireIterable(other);
        let membership = PersistentHashMap.empty<T, true>(this.policy);
        const items: T[] = [];
        for (const item of other) {
            const added = membership.tryAdd(item, true);
            if (!added.added) continue;
            membership = added.value;
            addChecked(items, item);
        }
        return { items, membership };
    }

    private everyReceiverOccursIn(membership: PersistentHashMap<T, true>): boolean {
        for (const entry of this.#order) if (!membership.containsKey(entry.item)) return false;
        return true;
    }

    private everyArgumentOccursHere(items: readonly T[]): boolean {
        for (const item of items) if (!this.#stamps.containsKey(item)) return false;
        return true;
    }

    private static tryPickStamp<T>(
        order: FingerTree<OrderedEntry<T>, bigint | undefined>,
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
        const gap = right - left;
        return gap < 2n ? undefined : left + gap / 2n;
    }

    private static buildFromItems<T>(
        items: readonly T[],
        policy: HashPolicy<T>,
    ): PersistentOrderedSet<T> {
        if (items.length === 0) return PersistentOrderedSet.empty(policy);
        const entries: OrderedEntry<T>[] = new Array(items.length);
        const pairs: Array<readonly [T, bigint]> = new Array(items.length);
        for (let index = 0; index < items.length; index++) {
            const item = items[index]!;
            const stamp = BigInt(index) * stampStride;
            entries[index] = { stamp, item };
            pairs[index] = [item, stamp];
        }
        return new PersistentOrderedSet(
            FingerTree.from(entries, orderPolicy<T>()),
            PersistentHashMap.from(pairs, policy),
        );
    }

    private static buildIndex<T>(
        order: FingerTree<OrderedEntry<T>, bigint | undefined>,
        policy: HashPolicy<T>,
    ): PersistentHashMap<T, bigint> {
        return PersistentHashMap.from(
            Array.from(order, (entry): readonly [T, bigint] => [entry.item, entry.stamp]),
            policy,
        );
    }

    private static wrap<T>(
        order: FingerTree<OrderedEntry<T>, bigint | undefined>,
        stamps: PersistentHashMap<T, bigint>,
    ): PersistentOrderedSet<T> {
        return order.isEmpty
            ? PersistentOrderedSet.empty(stamps.policy)
            : new PersistentOrderedSet(order, stamps);
    }
}

/** Immutable root-plus-position gap cursor over a persistent ordered set. */
export class PersistentOrderedSetCursor<T> {
    public constructor(
        public readonly snapshot: PersistentOrderedSet<T>,
        public readonly position: number,
    ) {
        requireInsertionIndex(position, snapshot.size);
    }

    public get size(): number { return this.snapshot.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.size; }

    public tryPeekPrevious(): OrderedSetCursorPeek<T> {
        return this.isAtStart
            ? { found: false }
            : { found: true, value: this.snapshot.getAt(this.position - 1) };
    }

    public tryPeekNext(): OrderedSetCursorPeek<T> {
        return this.isAtEnd
            ? { found: false }
            : { found: true, value: this.snapshot.getAt(this.position) };
    }

    public movePrevious(): PersistentOrderedSetCursor<T> {
        if (this.isAtStart) throw new RangeError("The ordered-set cursor is already at the start.");
        return new PersistentOrderedSetCursor(this.snapshot, this.position - 1);
    }

    public moveNext(): PersistentOrderedSetCursor<T> {
        if (this.isAtEnd) throw new RangeError("The ordered-set cursor is already at the end.");
        return new PersistentOrderedSetCursor(this.snapshot, this.position + 1);
    }

    public seek(position: number): PersistentOrderedSetCursor<T> {
        return position === this.position ? this : new PersistentOrderedSetCursor(this.snapshot, position);
    }

    /** Inserts at the gap; an equivalent representative preserves this cursor. */
    public insert(item: T): PersistentOrderedSetCursor<T> {
        const snapshot = this.snapshot.insert(this.position, item);
        return snapshot === this.snapshot
            ? this
            : new PersistentOrderedSetCursor(snapshot, this.position + 1);
    }

    public tryInsert(item: T): {
        readonly inserted: boolean;
        readonly cursor: PersistentOrderedSetCursor<T>;
    } {
        const cursor = this.insert(item);
        return { inserted: cursor !== this, cursor };
    }

    public deletePrevious(): PersistentOrderedSetCursor<T> {
        if (this.isAtStart) throw new RangeError("The ordered-set cursor has no previous representative.");
        return new PersistentOrderedSetCursor(
            this.snapshot.removeAt(this.position - 1),
            this.position - 1,
        );
    }

    public deleteNext(): PersistentOrderedSetCursor<T> {
        if (this.isAtEnd) throw new RangeError("The ordered-set cursor has no next representative.");
        return new PersistentOrderedSetCursor(this.snapshot.removeAt(this.position), this.position);
    }
}
