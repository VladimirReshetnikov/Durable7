import { MeasuredSequence } from "./measured-sequence.js";
import { SizeMeasure, type MeasurePolicy } from "./measures.js";

const sharedSizeMeasure = new SizeMeasure<unknown>();
function sizePolicy<T>(): SizeMeasure<T> { return sharedSizeMeasure as SizeMeasure<T>; }

export interface DequeSplit<T> { readonly left: PersistentDeque<T>; readonly right: PersistentDeque<T> }
export interface DequeItemSplit<T> { readonly left: PersistentDeque<T>; readonly item: T; readonly right: PersistentDeque<T> }
export interface DequeRangeSplit<T> { readonly before: PersistentDeque<T>; readonly range: PersistentDeque<T>; readonly after: PersistentDeque<T> }
export interface DequePop<T> { readonly value: T; readonly rest: PersistentDeque<T> }

/** Persistent catenable sequence facade over a measured balanced tree. */
export class PersistentDeque<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    private constructor(items: MeasuredSequence<T, number>) { this.#items = items; }
    public static empty<T>(): PersistentDeque<T> { return new PersistentDeque(MeasuredSequence.empty(sizePolicy<T>())); }
    public static from<T>(values: Iterable<T>): PersistentDeque<T> { return new PersistentDeque(MeasuredSequence.from(values, sizePolicy<T>())); }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public front(): T | undefined { return this.#items.front(); }
    public back(): T | undefined { return this.#items.back(); }
    public get(index: number): T | undefined { return this.#items.at(index); }
    public prepend(value: T): PersistentDeque<T> { return new PersistentDeque(this.#items.prepend(value)); }
    public append(value: T): PersistentDeque<T> { return new PersistentDeque(this.#items.append(value)); }
    public concat(other: PersistentDeque<T>): PersistentDeque<T> {
        if (other.isEmpty) return this;
        if (this.isEmpty) return other;
        return new PersistentDeque(this.#items.concat(other.#items));
    }
    public splitAt(index: number): DequeSplit<T> | undefined {
        const split = this.#items.splitAt(index);
        return split === undefined ? undefined : { left: new PersistentDeque(split.left), right: new PersistentDeque(split.right) };
    }
    public splitItemAt(index: number): DequeItemSplit<T> | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        const first = this.splitAt(index)!;
        const second = first.right.splitAt(1)!;
        return { left: first.left, item: first.right.front()!, right: second.right };
    }
    public splitRange(start: number, count: number): DequeRangeSplit<T> | undefined {
        if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined;
        const first = this.splitAt(start)!;
        const second = first.right.splitAt(count)!;
        return { before: first.left, range: second.left, after: second.right };
    }
    public insertAt(index: number, value: T): PersistentDeque<T> | undefined {
        const next = this.#items.insertAt(index, value);
        return next === undefined ? undefined : new PersistentDeque(next);
    }
    public setItem(index: number, value: T): PersistentDeque<T> | undefined {
        const next = this.#items.setAt(index, value);
        return next === undefined ? undefined : next === this.#items ? this : new PersistentDeque(next);
    }
    public removeAt(index: number): PersistentDeque<T> | undefined {
        const next = this.#items.removeAt(index);
        return next === undefined ? undefined : new PersistentDeque(next);
    }
    public tryViewLeft(): DequePop<T> | undefined {
        if (this.isEmpty) return undefined;
        return { value: this.front()!, rest: this.splitAt(1)!.right };
    }
    public tryViewRight(): DequePop<T> | undefined {
        if (this.isEmpty) return undefined;
        return { value: this.back()!, rest: this.splitAt(this.size - 1)!.left };
    }
    public reverse(): PersistentDeque<T> { return PersistentDeque.from(this.toArray().reverse()); }
    public toArray(): T[] { return this.#items.toArray(); }
    public sharesStorageWith(other: PersistentDeque<T>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Orientation-aware immutable deque; whole-value reversal is O(1). */
export class ReversibleDeque<T> implements Iterable<T> {
    readonly #items: PersistentDeque<T>;
    readonly #reversed: boolean;
    private constructor(items: PersistentDeque<T>, reversed: boolean) { this.#items = items; this.#reversed = reversed; }
    public static empty<T>(): ReversibleDeque<T> { return new ReversibleDeque(PersistentDeque.empty<T>(), false); }
    public static from<T>(values: Iterable<T>): ReversibleDeque<T> { return new ReversibleDeque(PersistentDeque.from(values), false); }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public front(): T | undefined { return this.#reversed ? this.#items.back() : this.#items.front(); }
    public back(): T | undefined { return this.#reversed ? this.#items.front() : this.#items.back(); }
    public get(index: number): T | undefined { return this.#items.get(this.#reversed ? this.size - index - 1 : index); }
    public reverse(): ReversibleDeque<T> { return new ReversibleDeque(this.#items, !this.#reversed); }
    public prepend(value: T): ReversibleDeque<T> {
        return new ReversibleDeque(this.#reversed ? this.#items.append(value) : this.#items.prepend(value), this.#reversed);
    }
    public append(value: T): ReversibleDeque<T> {
        return new ReversibleDeque(this.#reversed ? this.#items.prepend(value) : this.#items.append(value), this.#reversed);
    }
    public concat(other: ReversibleDeque<T>): ReversibleDeque<T> {
        if (this.isEmpty) return other;
        if (other.isEmpty) return this;
        if (this.#reversed === other.#reversed) {
            const joined = this.#reversed ? other.#items.concat(this.#items) : this.#items.concat(other.#items);
            return new ReversibleDeque(joined, this.#reversed);
        }
        return ReversibleDeque.from([...this, ...other]);
    }
    public splitAt(index: number): readonly [ReversibleDeque<T>, ReversibleDeque<T>] | undefined {
        if (!Number.isInteger(index) || index < 0 || index > this.size) return undefined;
        if (!this.#reversed) {
            const split = this.#items.splitAt(index)!;
            return [new ReversibleDeque(split.left, false), new ReversibleDeque(split.right, false)];
        }
        const split = this.#items.splitAt(this.size - index)!;
        return [new ReversibleDeque(split.right, true), new ReversibleDeque(split.left, true)];
    }
    public tryViewLeft(): readonly [T, ReversibleDeque<T>] | undefined {
        if (this.isEmpty) return undefined;
        return [this.front()!, this.splitAt(1)![1]];
    }
    public tryViewRight(): readonly [T, ReversibleDeque<T>] | undefined {
        if (this.isEmpty) return undefined;
        return [this.back()!, this.splitAt(this.size - 1)![0]];
    }
    public toArray(): T[] { return Array.from(this); }
    public sharesStorageWith(other: ReversibleDeque<T>): boolean { return this.#items.sharesStorageWith(other.#items); }
    public *[Symbol.iterator](): IterableIterator<T> {
        if (!this.#reversed) yield* this.#items;
        else for (let index = this.size - 1; index >= 0; index--) yield this.#items.get(index)!;
    }
}

export interface MeasuredSplit<T, M> { readonly left: FingerTree<T, M>; readonly right: FingerTree<T, M> }
export interface MeasuredItemSplit<T, M> { readonly left: FingerTree<T, M>; readonly item: T; readonly right: FingerTree<T, M> }
export interface LocateResult<T, M> { readonly index: number; readonly measureBefore: M; readonly item: T | undefined; readonly found: boolean }

/** General persistent monoid-measured sequence. */
export class FingerTree<T, M> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, M>;
    public readonly policy: MeasurePolicy<T, M>;
    private constructor(items: MeasuredSequence<T, M>, policy: MeasurePolicy<T, M>) { this.#items = items; this.policy = policy; }
    public static empty<T, M>(policy: MeasurePolicy<T, M>): FingerTree<T, M> { return new FingerTree(MeasuredSequence.empty(policy), policy); }
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): FingerTree<T, M> { return new FingerTree(MeasuredSequence.from(values, policy), policy); }
    public get size(): number { return this.#items.size; }
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    public measure(): M { return this.#items.measure; }
    public front(): T | undefined { return this.#items.front(); }
    public back(): T | undefined { return this.#items.back(); }
    public get(index: number): T | undefined { return this.#items.at(index); }
    public prepend(value: T): FingerTree<T, M> { return new FingerTree(this.#items.prepend(value), this.policy); }
    public append(value: T): FingerTree<T, M> { return new FingerTree(this.#items.append(value), this.policy); }
    public concat(other: FingerTree<T, M>): FingerTree<T, M> {
        if (this.policy !== other.policy) throw new TypeError("Trees must retain the same measure policy object.");
        if (this.isEmpty) return other;
        if (other.isEmpty) return this;
        return new FingerTree(this.#items.concat(other.#items), this.policy);
    }
    public split(predicate: (measure: M) => boolean): MeasuredSplit<T, M> {
        const located = this.#items.locate(predicate);
        return this.splitAtIndex(located.found ? located.index : this.size)!;
    }
    public splitAtIndex(index: number): MeasuredSplit<T, M> | undefined {
        const split = this.#items.splitAt(index);
        return split === undefined ? undefined : { left: new FingerTree(split.left, this.policy), right: new FingerTree(split.right, this.policy) };
    }
    public trySplitFind(predicate: (measure: M) => boolean): MeasuredItemSplit<T, M> | undefined {
        const located = this.#items.locate(predicate);
        if (!located.found) return undefined;
        const first = this.splitAtIndex(located.index)!;
        const second = first.right.splitAtIndex(1)!;
        return { left: first.left, item: located.value!, right: second.right };
    }
    public prefixMeasure(count: number): M | undefined { return this.#items.prefixMeasure(count); }
    public tryLocate(predicate: (measure: M) => boolean): LocateResult<T, M> {
        const result = this.#items.locate(predicate);
        return { index: result.index, measureBefore: result.measureBefore, item: result.value, found: result.found };
    }
    public setItem(index: number, value: T): FingerTree<T, M> | undefined {
        const next = this.#items.setAt(index, value);
        return next === undefined ? undefined : new FingerTree(next, this.policy);
    }
    public insertAt(index: number, value: T): FingerTree<T, M> | undefined {
        const next = this.#items.insertAt(index, value);
        return next === undefined ? undefined : new FingerTree(next, this.policy);
    }
    public removeAt(index: number): FingerTree<T, M> | undefined {
        const next = this.#items.removeAt(index);
        return next === undefined ? undefined : new FingerTree(next, this.policy);
    }
    public tryViewLeft(): readonly [T, FingerTree<T, M>] | undefined { return this.isEmpty ? undefined : [this.front()!, this.splitAtIndex(1)!.right]; }
    public tryViewRight(): readonly [T, FingerTree<T, M>] | undefined { return this.isEmpty ? undefined : [this.back()!, this.splitAtIndex(this.size - 1)!.left]; }
    public toArray(): T[] { return this.#items.toArray(); }
    public sharesStorageWith(other: FingerTree<T, M>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}
