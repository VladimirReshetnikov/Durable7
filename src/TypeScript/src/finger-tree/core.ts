import { MeasuredSequence } from "./measured-sequence.js";
import { SizeMeasure, type MeasurePolicy } from "./measures.js";

const sharedSizeMeasure = new SizeMeasure<unknown>();
function sizePolicy<T>(): SizeMeasure<T> { return sharedSizeMeasure as SizeMeasure<T>; }

export interface DequeSplit<T> { readonly left: PersistentDeque<T>; readonly right: PersistentDeque<T> }
export interface DequeItemSplit<T> { readonly left: PersistentDeque<T>; readonly item: T; readonly right: PersistentDeque<T> }
export interface DequeRangeSplit<T> { readonly before: PersistentDeque<T>; readonly range: PersistentDeque<T>; readonly after: PersistentDeque<T> }
export interface DequePop<T> { readonly value: T; readonly rest: PersistentDeque<T> }
export interface SequenceCursorPeek<T> { readonly value: T }

/** Persistent catenable sequence facade over a measured balanced tree. */
export class PersistentDeque<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    private constructor(items: MeasuredSequence<T, number>) { this.#items = items; }
    public static empty<T>(): PersistentDeque<T> { return new PersistentDeque(MeasuredSequence.empty(sizePolicy<T>())); }
    public static from<T>(values: Iterable<T>): PersistentDeque<T> { return new PersistentDeque(MeasuredSequence.from(values, sizePolicy<T>())); }
    public getCursor(position = 0): PersistentDequeCursor<T> { return new PersistentDequeCursor(this, position); }
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
    public getCursor(position = 0): ReversibleDequeCursor<T> { return new ReversibleDequeCursor(this, position); }
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
    /**
     * Builds a deque whose logical order is `values` in *this* deque's physical orientation.
     *
     * {@link from} always produces forward-oriented storage. Splicing such a range into a reversed
     * receiver would make both {@link concat} calls see mismatched orientations and fall back to
     * re-materializing every element, discarding all structural sharing. Aligning the middle first
     * keeps the splice on the sharing path in both orientations: when this deque is reversed the
     * range is stored physically reversed, which is exactly the design's statement that inserting
     * the logical range inserts the reversed range physically. `values` is enumerated once.
     *
     * @internal Ownership detail of the cursor splice path; not part of the published surface.
     */
    public alignedRange(values: Iterable<T>): ReversibleDeque<T> {
        const materialized = Array.from(values);
        if (this.#reversed) materialized.reverse();
        return new ReversibleDeque(PersistentDeque.from(materialized), this.#reversed);
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
export interface FingerTreeCursorSearch<T, M> { readonly cursor: FingerTreeCursor<T, M>; readonly found: boolean }

/** General persistent monoid-measured sequence. */
export class FingerTree<T, M> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, M>;
    public readonly policy: MeasurePolicy<T, M>;
    private constructor(items: MeasuredSequence<T, M>, policy: MeasurePolicy<T, M>) { this.#items = items; this.policy = policy; }
    public static empty<T, M>(policy: MeasurePolicy<T, M>): FingerTree<T, M> { return new FingerTree(MeasuredSequence.empty(policy), policy); }
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): FingerTree<T, M> { return new FingerTree(MeasuredSequence.from(values, policy), policy); }
    public getCursorAtStart(): FingerTreeCursor<T, M> { return new FingerTreeCursor(this, 0); }
    public getCursorAtEnd(): FingerTreeCursor<T, M> { return new FingerTreeCursor(this, this.size); }
    public cursorByMeasure(predicate: (measure: M) => boolean): FingerTreeCursorSearch<T, M> {
        const located = this.tryLocate(predicate);
        return { cursor: new FingerTreeCursor(this, located.found ? located.index : this.size), found: located.found };
    }
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

/** Immutable snapshot-plus-position cursor over a persistent deque. */
export class PersistentDequeCursor<T> {
    public constructor(public readonly deque: PersistentDeque<T>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > deque.size) {
            throw new RangeError("Cursor position is outside the deque.");
        }
    }
    public get size(): number { return this.deque.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.size; }
    public peekPrevious(): SequenceCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.deque.get(this.position - 1)! };
    }
    public peekNext(): SequenceCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.deque.get(this.position)! };
    }
    public movePrevious(): PersistentDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new PersistentDequeCursor(this.deque, this.position - 1);
    }
    public moveNext(): PersistentDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new PersistentDequeCursor(this.deque, this.position + 1);
    }
    public seek(position: number): PersistentDequeCursor<T> {
        return position === this.position ? this : new PersistentDequeCursor(this.deque, position);
    }
    public insert(value: T): PersistentDequeCursor<T> {
        return new PersistentDequeCursor(this.deque.insertAt(this.position, value)!, this.position + 1);
    }
    public insertRange(values: Iterable<T>): PersistentDequeCursor<T> {
        const middle = PersistentDeque.from(values);
        if (middle.isEmpty) return this;
        const split = this.deque.splitAt(this.position)!;
        return new PersistentDequeCursor(split.left.concat(middle).concat(split.right), this.position + middle.size);
    }
    public deletePrevious(): PersistentDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        return new PersistentDequeCursor(this.deque.removeAt(this.position - 1)!, this.position - 1);
    }
    public deleteNext(): PersistentDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new PersistentDequeCursor(this.deque.removeAt(this.position)!, this.position);
    }
    public replaceNext(value: T): PersistentDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new PersistentDequeCursor(this.deque.setItem(this.position, value)!, this.position);
    }
    public snapshot(): PersistentDeque<T> { return this.deque; }
}

/** Immutable logical-order cursor over a persistent O(1)-reversible deque. */
export class ReversibleDequeCursor<T> {
    public constructor(public readonly deque: ReversibleDeque<T>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > deque.size) {
            throw new RangeError("Cursor position is outside the reversible deque.");
        }
    }
    public get size(): number { return this.deque.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.size; }
    public peekPrevious(): SequenceCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.deque.get(this.position - 1)! };
    }
    public peekNext(): SequenceCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.deque.get(this.position)! };
    }
    public movePrevious(): ReversibleDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new ReversibleDequeCursor(this.deque, this.position - 1);
    }
    public moveNext(): ReversibleDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new ReversibleDequeCursor(this.deque, this.position + 1);
    }
    public seek(position: number): ReversibleDequeCursor<T> {
        return position === this.position ? this : new ReversibleDequeCursor(this.deque, position);
    }
    public insert(value: T): ReversibleDequeCursor<T> { return this.insertRange([value]); }
    public insertRange(values: Iterable<T>): ReversibleDequeCursor<T> {
        const middle = this.deque.alignedRange(values);
        if (middle.isEmpty) return this;
        const split = this.deque.splitAt(this.position)!;
        return new ReversibleDequeCursor(split[0].concat(middle).concat(split[1]), this.position + middle.size);
    }
    public deletePrevious(): ReversibleDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        const split = this.deque.splitAt(this.position - 1)!;
        return new ReversibleDequeCursor(split[0].concat(split[1].splitAt(1)![1]), this.position - 1);
    }
    public deleteNext(): ReversibleDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const split = this.deque.splitAt(this.position)!;
        return new ReversibleDequeCursor(split[0].concat(split[1].splitAt(1)![1]), this.position);
    }
    public replaceNext(value: T): ReversibleDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const split = this.deque.splitAt(this.position)!;
        return new ReversibleDequeCursor(split[0].append(value).concat(split[1].splitAt(1)![1]), this.position);
    }
    public reverse(): ReversibleDequeCursor<T> { return new ReversibleDequeCursor(this.deque.reverse(), this.size - this.position); }
    public snapshot(): ReversibleDeque<T> { return this.deque; }
}

/** Immutable measure-aware cursor over one exact general measured-tree version. */
export class FingerTreeCursor<T, M> {
    readonly #position: number;
    public constructor(public readonly tree: FingerTree<T, M>, position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > tree.size) {
            throw new RangeError("Cursor position is outside the measured tree.");
        }
        this.#position = position;
    }
    public get isAtStart(): boolean { return this.#position === 0; }
    public get isAtEnd(): boolean { return this.#position === this.tree.size; }
    public get measureBefore(): M { return this.tree.prefixMeasure(this.#position)!; }
    public get measureAfter(): M { return this.tree.splitAtIndex(this.#position)!.right.measure(); }
    public peekPrevious(): SequenceCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.tree.get(this.#position - 1)! };
    }
    public peekNext(): SequenceCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.tree.get(this.#position)! };
    }
    public movePrevious(): FingerTreeCursor<T, M> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new FingerTreeCursor(this.tree, this.#position - 1);
    }
    public moveNext(): FingerTreeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new FingerTreeCursor(this.tree, this.#position + 1);
    }
    public seekByMeasure(predicate: (measure: M) => boolean): FingerTreeCursor<T, M> {
        return this.tree.cursorByMeasure(predicate).cursor;
    }
    public searchByMeasure(predicate: (measure: M) => boolean): FingerTreeCursorSearch<T, M> {
        return this.tree.cursorByMeasure(predicate);
    }
    public insert(value: T): FingerTreeCursor<T, M> {
        return new FingerTreeCursor(this.tree.insertAt(this.#position, value)!, this.#position + 1);
    }
    public deletePrevious(): FingerTreeCursor<T, M> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        return new FingerTreeCursor(this.tree.removeAt(this.#position - 1)!, this.#position - 1);
    }
    public deleteNext(): FingerTreeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new FingerTreeCursor(this.tree.removeAt(this.#position)!, this.#position);
    }
    public replaceNext(value: T): FingerTreeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new FingerTreeCursor(this.tree.setItem(this.#position, value)!, this.#position);
    }
    public snapshot(): FingerTree<T, M> { return this.tree; }
}
