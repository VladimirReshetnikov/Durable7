/**
 * The persistent sequence core: deques, a reversible deque, and the measured finger tree.
 *
 * Every collection here returns a new version and leaves the previous one valid, sharing unchanged
 * structure. The substrate is a lazy Hinze-Paterson finger tree with memoized suspensions, so
 * endpoint reads are O(1) worst-case, endpoint edits are O(1) amortized under persistent
 * branching, concatenation is O(log(min(n, m))) amortized, and whole-value reversal is an O(1)
 * lazy structural mirror. The tree caches a monoidal measure at every node, so one generic split
 * answers any monotone question the measure can express without visiting the elements it skips.
 */
import { MeasuredSequence } from "./measured-sequence.js";
import { SizeMeasure, type MeasurePolicy } from "./measures.js";

const sharedSizeMeasure = new SizeMeasure<unknown>();
function sizePolicy<T>(): SizeMeasure<T> { return sharedSizeMeasure as SizeMeasure<T>; }

/** The two deques produced by a positional split; both share structure with the original. */
export interface DequeSplit<T> { readonly left: PersistentDeque<T>; readonly right: PersistentDeque<T> }
/**
 * The pieces produced by splitting around one element: what precedes it, the element, and what
 * follows it.
 */
export interface DequeItemSplit<T> { readonly left: PersistentDeque<T>; readonly item: T; readonly right: PersistentDeque<T> }
/**
 * The three deques produced by splitting out a range: before it, the range itself, and after it.
 */
export interface DequeRangeSplit<T> { readonly before: PersistentDeque<T>; readonly range: PersistentDeque<T>; readonly after: PersistentDeque<T> }
/** An endpoint element together with the deque that remains after removing it. */
export interface DequePop<T> { readonly value: T; readonly rest: PersistentDeque<T> }
/**
 * An element found next to a cursor gap, wrapped so a stored `undefined` stays distinct from
 * "nothing there".
 */
export interface SequenceCursorPeek<T> { readonly value: T }

/** Persistent catenable sequence facade over the measured finger tree. */
export class PersistentDeque<T> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, number>;
    private constructor(items: MeasuredSequence<T, number>) { this.#items = items; }
    /** The empty deque, retaining the supplied policy objects. */
    public static empty<T>(): PersistentDeque<T> { return new PersistentDeque(MeasuredSequence.empty(sizePolicy<T>())); }
    /** Build a deque from the given elements. */
    public static from<T>(values: Iterable<T>): PersistentDeque<T> { return new PersistentDeque(MeasuredSequence.from(values, sizePolicy<T>())); }
    /** A cursor at the given gap of the deque. */
    public getCursor(position = 0): PersistentDequeCursor<T> { return new PersistentDequeCursor(this, position); }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the deque holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.#items.front(); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(index: number): T | undefined { return this.#items.at(index); }
    /** A deque with the value added at the front. */
    public prepend(value: T): PersistentDeque<T> { return new PersistentDeque(this.#items.prepend(value)); }
    /** A deque with the value added at the back. */
    public append(value: T): PersistentDeque<T> { return new PersistentDeque(this.#items.append(value)); }
    /**
     * This deque's elements followed by the other's, joining the two trees rather than copying
     * either.
     */
    public concat(other: PersistentDeque<T>): PersistentDeque<T> {
        if (other.isEmpty) return this;
        if (this.isEmpty) return other;
        return new PersistentDeque(this.#items.concat(other.#items));
    }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver.
     */
    public splitAt(index: number): DequeSplit<T> | undefined {
        const split = this.#items.splitAt(index);
        return split === undefined ? undefined : { left: new PersistentDeque(split.left), right: new PersistentDeque(split.right) };
    }
    /**
     * Split around the element at the given index, yielding it with the elements on either side.
     */
    public splitItemAt(index: number): DequeItemSplit<T> | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        const first = this.splitAt(index)!;
        const second = first.right.splitAt(1)!;
        return { left: first.left, item: first.right.front()!, right: second.right };
    }
    /** Split into the elements before the range, the range itself, and the rest. */
    public splitRange(start: number, count: number): DequeRangeSplit<T> | undefined {
        if (!Number.isInteger(start) || !Number.isInteger(count) || start < 0 || count < 0 || start + count > this.size) return undefined;
        const first = this.splitAt(start)!;
        const second = first.right.splitAt(count)!;
        return { before: first.left, range: second.left, after: second.right };
    }
    /** A deque with the value inserted so that it ends up at the given index. */
    public insertAt(index: number, value: T): PersistentDeque<T> | undefined {
        const next = this.#items.insertAt(index, value);
        return next === undefined ? undefined : new PersistentDeque(next);
    }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(index: number, value: T): PersistentDeque<T> | undefined {
        const next = this.#items.setAt(index, value);
        return next === undefined ? undefined : next === this.#items ? this : new PersistentDeque(next);
    }
    /** A deque without the element at the given index. */
    public removeAt(index: number): PersistentDeque<T> | undefined {
        const next = this.#items.removeAt(index);
        return next === undefined ? undefined : new PersistentDeque(next);
    }
    /** The first element together with the remaining deque, or `undefined` when empty. */
    public tryViewLeft(): DequePop<T> | undefined {
        if (this.isEmpty) return undefined;
        return { value: this.front()!, rest: this.splitAt(1)!.right };
    }
    /** The last element together with the remaining deque, or `undefined` when empty. */
    public tryViewRight(): DequePop<T> | undefined {
        if (this.isEmpty) return undefined;
        return { value: this.back()!, rest: this.splitAt(this.size - 1)!.left };
    }
    /**
     * A deque with the order reversed. O(1): a lazy structural mirror sharing storage with the
     * receiver - digits mirror eagerly and the middle's reversal rides a suspension - not a
     * rebuild. (This was previously a Theta(n) materialization.)
     */
    public reverse(): PersistentDeque<T> {
        if (this.size < 2) return this;
        return new PersistentDeque(this.#items.reversedView());
    }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return this.#items.toArray(); }
    /**
     * Whether both deques share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: PersistentDeque<T>): boolean { return this.#items.sharesStructureWith(other.#items); }
    public [Symbol.iterator](): IterableIterator<T> { return this.#items[Symbol.iterator](); }
}

/** Orientation-aware immutable deque; whole-value reversal is O(1). */
export class ReversibleDeque<T> implements Iterable<T> {
    readonly #items: PersistentDeque<T>;
    readonly #reversed: boolean;
    private constructor(items: PersistentDeque<T>, reversed: boolean) { this.#items = items; this.#reversed = reversed; }
    /** The empty deque, retaining the supplied policy objects. */
    public static empty<T>(): ReversibleDeque<T> { return new ReversibleDeque(PersistentDeque.empty<T>(), false); }
    /** Build a deque from the given elements. */
    public static from<T>(values: Iterable<T>): ReversibleDeque<T> { return new ReversibleDeque(PersistentDeque.from(values), false); }
    /** A cursor at the given gap of the deque. */
    public getCursor(position = 0): ReversibleDequeCursor<T> { return new ReversibleDequeCursor(this, position); }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the deque holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.#reversed ? this.#items.back() : this.#items.front(); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.#reversed ? this.#items.front() : this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(index: number): T | undefined { return this.#items.get(this.#reversed ? this.size - index - 1 : index); }
    /** A deque with the order reversed. */
    public reverse(): ReversibleDeque<T> { return new ReversibleDeque(this.#items, !this.#reversed); }
    /** A deque with the value added at the front. */
    public prepend(value: T): ReversibleDeque<T> {
        return new ReversibleDeque(this.#reversed ? this.#items.append(value) : this.#items.prepend(value), this.#reversed);
    }
    /** A deque with the value added at the back. */
    public append(value: T): ReversibleDeque<T> {
        return new ReversibleDeque(this.#reversed ? this.#items.prepend(value) : this.#items.append(value), this.#reversed);
    }
    /**
     * This deque's elements followed by the other's. O(log(min(n, m))) amortized in every
     * orientation combination. Same-orientation operands join structurally; opposite orientations
     * lazily mirror the smaller operand into the larger one's orientation first - an O(1)
     * structural reversal whose work rides the suspensions - and then join structurally. (This
     * previously fell back to a Theta(n + m) materialization.)
     */
    public concat(other: ReversibleDeque<T>): ReversibleDeque<T> {
        if (this.isEmpty) return other;
        if (other.isEmpty) return this;
        if (this.#reversed === other.#reversed) {
            const joined = this.#reversed ? other.#items.concat(this.#items) : this.#items.concat(other.#items);
            return new ReversibleDeque(joined, this.#reversed);
        }
        if (this.size >= other.size) {
            // Keep the receiver's orientation; mirror the smaller right operand into it.
            const joined = this.#reversed
                ? other.#items.reverse().concat(this.#items)
                : this.#items.concat(other.#items.reverse());
            return new ReversibleDeque(joined, this.#reversed);
        }
        // Keep the larger right operand's orientation; mirror the receiver into it.
        const joined = other.#reversed
            ? other.#items.concat(this.#items.reverse())
            : this.#items.reverse().concat(other.#items);
        return new ReversibleDeque(joined, other.#reversed);
    }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver.
     */
    public splitAt(index: number): readonly [ReversibleDeque<T>, ReversibleDeque<T>] | undefined {
        if (!Number.isInteger(index) || index < 0 || index > this.size) return undefined;
        if (!this.#reversed) {
            const split = this.#items.splitAt(index)!;
            return [new ReversibleDeque(split.left, false), new ReversibleDeque(split.right, false)];
        }
        const split = this.#items.splitAt(this.size - index)!;
        return [new ReversibleDeque(split.right, true), new ReversibleDeque(split.left, true)];
    }
    /** The first element together with the remaining deque, or `undefined` when empty. */
    public tryViewLeft(): readonly [T, ReversibleDeque<T>] | undefined {
        if (this.isEmpty) return undefined;
        return [this.front()!, this.splitAt(1)![1]];
    }
    /** The last element together with the remaining deque, or `undefined` when empty. */
    public tryViewRight(): readonly [T, ReversibleDeque<T>] | undefined {
        if (this.isEmpty) return undefined;
        return [this.back()!, this.splitAt(this.size - 1)![0]];
    }
    /**
     * Builds a deque whose logical order is `values` in *this* deque's physical orientation.
     *
     * {@link from} always produces forward-oriented storage. Splicing such a range into a reversed
     * receiver would make both {@link concat} calls see mismatched orientations and interpose a
     * lazy mirror of the range. Aligning the middle first keeps the splice on the direct
     * same-orientation path in both orientations: when this deque is reversed the range is stored
     * physically reversed, which is exactly the design's statement that inserting the logical
     * range inserts the reversed range physically. `values` is enumerated once.
     *
     * @internal Ownership detail of the cursor splice path; not part of the published surface.
     */
    public alignedRange(values: Iterable<T>): ReversibleDeque<T> {
        const materialized = Array.from(values);
        if (this.#reversed) materialized.reverse();
        return new ReversibleDeque(PersistentDeque.from(materialized), this.#reversed);
    }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return Array.from(this); }
    /**
     * Whether both deques share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: ReversibleDeque<T>): boolean { return this.#items.sharesStorageWith(other.#items); }
    public *[Symbol.iterator](): IterableIterator<T> {
        if (!this.#reversed) yield* this.#items;
        else yield* this.#items.reverse();
    }
}

/** The two trees produced by a split; both carry their own recomputed measure. */
export interface MeasuredSplit<T, M> { readonly left: FingerTree<T, M>; readonly right: FingerTree<T, M> }
/** The pieces produced by splitting around one element, with the element itself. */
export interface MeasuredItemSplit<T, M> { readonly left: FingerTree<T, M>; readonly item: T; readonly right: FingerTree<T, M> }
/**
 * Where a measure-directed search landed, reported without splitting the tree. The found flag
 * distinguishes a real hit from the end position.
 */
export interface LocateResult<T, M> { readonly index: number; readonly measureBefore: M; readonly item: T | undefined; readonly found: boolean }
/**
 * The outcome of a measure-directed cursor seek; on a miss the cursor sits at the end and remains
 * usable.
 */
export interface FingerTreeCursorSearch<T, M> { readonly cursor: FingerTreeCursor<T, M>; readonly found: boolean }

/** General persistent monoid-measured sequence. */
export class FingerTree<T, M> implements Iterable<T> {
    readonly #items: MeasuredSequence<T, M>;
    /** The retained policy defining equivalence. */
    public readonly policy: MeasurePolicy<T, M>;
    private constructor(items: MeasuredSequence<T, M>, policy: MeasurePolicy<T, M>) { this.#items = items; this.policy = policy; }
    /** The empty tree, retaining the supplied policy objects. */
    public static empty<T, M>(policy: MeasurePolicy<T, M>): FingerTree<T, M> { return new FingerTree(MeasuredSequence.empty(policy), policy); }
    /** Build a tree from the given elements. */
    public static from<T, M>(values: Iterable<T>, policy: MeasurePolicy<T, M>): FingerTree<T, M> { return new FingerTree(MeasuredSequence.from(values, policy), policy); }
    /** A cursor before the first element. */
    public getCursorAtStart(): FingerTreeCursor<T, M> { return new FingerTreeCursor(this, 0); }
    /** A cursor after the last element. */
    public getCursorAtEnd(): FingerTreeCursor<T, M> { return new FingerTreeCursor(this, this.size); }
    /** Seek by measure and report whether any prefix satisfied the predicate. */
    public cursorByMeasure(predicate: (measure: M) => boolean): FingerTreeCursorSearch<T, M> {
        const located = this.tryLocate(predicate);
        return { cursor: new FingerTreeCursor(this, located.found ? located.index : this.size), found: located.found };
    }
    /** Number of elements. */
    public get size(): number { return this.#items.size; }
    /** Whether the tree holds no elements. */
    public get isEmpty(): boolean { return this.#items.isEmpty; }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(): M { return this.#items.measure; }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.#items.front(); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.#items.back(); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(index: number): T | undefined { return this.#items.at(index); }
    /** A tree with the value added at the front. */
    public prepend(value: T): FingerTree<T, M> { return new FingerTree(this.#items.prepend(value), this.policy); }
    /** A tree with the value added at the back. */
    public append(value: T): FingerTree<T, M> { return new FingerTree(this.#items.append(value), this.policy); }
    /**
     * This tree's elements followed by the other's, joining the two trees rather than copying
     * either.
     */
    public concat(other: FingerTree<T, M>): FingerTree<T, M> {
        if (this.policy !== other.policy) throw new TypeError("Trees must retain the same measure policy object.");
        if (this.isEmpty) return other;
        if (other.isEmpty) return this;
        return new FingerTree(this.#items.concat(other.#items), this.policy);
    }
    /**
     * Split at the first position whose inclusive prefix measure satisfies the predicate. The
     * predicate is expected to be monotone, which is what makes "the first satisfying position"
     * well defined.
     */
    public split(predicate: (measure: M) => boolean): MeasuredSplit<T, M> {
        const located = this.#items.locate(predicate);
        return this.splitAtIndex(located.found ? located.index : this.size)!;
    }
    /** Split at a positional boundary. */
    public splitAtIndex(index: number): MeasuredSplit<T, M> | undefined {
        const split = this.#items.splitAt(index);
        return split === undefined ? undefined : { left: new FingerTree(split.left, this.policy), right: new FingerTree(split.right, this.policy) };
    }
    /**
     * The first element whose inclusive prefix measure satisfies the predicate together with the
     * elements on either side, distinguishing "found here" from "not found".
     */
    public trySplitFind(predicate: (measure: M) => boolean): MeasuredItemSplit<T, M> | undefined {
        const located = this.#items.locate(predicate);
        if (!located.found) return undefined;
        const first = this.splitAtIndex(located.index)!;
        const second = first.right.splitAtIndex(1)!;
        return { left: first.left, item: located.value!, right: second.right };
    }
    /**
     * The combined measure of the first `count` elements, summed from cached node measures rather
     * than element by element.
     */
    public prefixMeasure(count: number): M | undefined { return this.#items.prefixMeasure(count); }
    /** Where the first element satisfying the predicate sits, reported without splitting. */
    public tryLocate(predicate: (measure: M) => boolean): LocateResult<T, M> {
        const result = this.#items.locate(predicate);
        return { index: result.index, measureBefore: result.measureBefore, item: result.value, found: result.found };
    }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(index: number, value: T): FingerTree<T, M> | undefined {
        const next = this.#items.setAt(index, value);
        return next === undefined ? undefined : new FingerTree(next, this.policy);
    }
    /** A tree with the value inserted so that it ends up at the given index. */
    public insertAt(index: number, value: T): FingerTree<T, M> | undefined {
        const next = this.#items.insertAt(index, value);
        return next === undefined ? undefined : new FingerTree(next, this.policy);
    }
    /** A tree without the element at the given index. */
    public removeAt(index: number): FingerTree<T, M> | undefined {
        const next = this.#items.removeAt(index);
        return next === undefined ? undefined : new FingerTree(next, this.policy);
    }
    /** The first element together with the remaining tree, or `undefined` when empty. */
    public tryViewLeft(): readonly [T, FingerTree<T, M>] | undefined { return this.isEmpty ? undefined : [this.front()!, this.splitAtIndex(1)!.right]; }
    /** The last element together with the remaining tree, or `undefined` when empty. */
    public tryViewRight(): readonly [T, FingerTree<T, M>] | undefined { return this.isEmpty ? undefined : [this.back()!, this.splitAtIndex(this.size - 1)!.left]; }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return this.#items.toArray(); }
    /**
     * Whether both trees share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
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
    /** Number of elements. */
    public get size(): number { return this.deque.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): SequenceCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.deque.get(this.position - 1)! };
    }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): SequenceCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.deque.get(this.position)! };
    }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): PersistentDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new PersistentDequeCursor(this.deque, this.position - 1);
    }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): PersistentDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new PersistentDequeCursor(this.deque, this.position + 1);
    }
    /** A cursor at the given position within the same deque version. */
    public seek(position: number): PersistentDequeCursor<T> {
        return position === this.position ? this : new PersistentDequeCursor(this.deque, position);
    }
    /** Insert at the gap and return a cursor positioned after the new element. */
    public insert(value: T): PersistentDequeCursor<T> {
        return new PersistentDequeCursor(this.deque.insertAt(this.position, value)!, this.position + 1);
    }
    /** Insert every value at the gap, in order, returning a cursor after the last. */
    public insertRange(values: Iterable<T>): PersistentDequeCursor<T> {
        const middle = PersistentDeque.from(values);
        if (middle.isEmpty) return this;
        const split = this.deque.splitAt(this.position)!;
        return new PersistentDequeCursor(split.left.concat(middle).concat(split.right), this.position + middle.size);
    }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): PersistentDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        return new PersistentDequeCursor(this.deque.removeAt(this.position - 1)!, this.position - 1);
    }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): PersistentDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new PersistentDequeCursor(this.deque.removeAt(this.position)!, this.position);
    }
    /** Replace the element after the gap, keeping the gap where it is. */
    public replaceNext(value: T): PersistentDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new PersistentDequeCursor(this.deque.setItem(this.position, value)!, this.position);
    }
    /** The deque version this cursor is positioned in. */
    public snapshot(): PersistentDeque<T> { return this.deque; }
}

/** Immutable logical-order cursor over a persistent O(1)-reversible deque. */
export class ReversibleDequeCursor<T> {
    public constructor(public readonly deque: ReversibleDeque<T>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > deque.size) {
            throw new RangeError("Cursor position is outside the reversible deque.");
        }
    }
    /** Number of elements. */
    public get size(): number { return this.deque.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): SequenceCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.deque.get(this.position - 1)! };
    }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): SequenceCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.deque.get(this.position)! };
    }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): ReversibleDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new ReversibleDequeCursor(this.deque, this.position - 1);
    }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): ReversibleDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new ReversibleDequeCursor(this.deque, this.position + 1);
    }
    /** A cursor at the given position within the same deque version. */
    public seek(position: number): ReversibleDequeCursor<T> {
        return position === this.position ? this : new ReversibleDequeCursor(this.deque, position);
    }
    /** Insert at the gap and return a cursor positioned after the new element. */
    public insert(value: T): ReversibleDequeCursor<T> { return this.insertRange([value]); }
    /** Insert every value at the gap, in order, returning a cursor after the last. */
    public insertRange(values: Iterable<T>): ReversibleDequeCursor<T> {
        const middle = this.deque.alignedRange(values);
        if (middle.isEmpty) return this;
        const split = this.deque.splitAt(this.position)!;
        return new ReversibleDequeCursor(split[0].concat(middle).concat(split[1]), this.position + middle.size);
    }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): ReversibleDequeCursor<T> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        const split = this.deque.splitAt(this.position - 1)!;
        return new ReversibleDequeCursor(split[0].concat(split[1].splitAt(1)![1]), this.position - 1);
    }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): ReversibleDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const split = this.deque.splitAt(this.position)!;
        return new ReversibleDequeCursor(split[0].concat(split[1].splitAt(1)![1]), this.position);
    }
    /** Replace the element after the gap, keeping the gap where it is. */
    public replaceNext(value: T): ReversibleDequeCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        const split = this.deque.splitAt(this.position)!;
        return new ReversibleDequeCursor(split[0].append(value).concat(split[1].splitAt(1)![1]), this.position);
    }
    /** A deque with the order reversed. */
    public reverse(): ReversibleDequeCursor<T> { return new ReversibleDequeCursor(this.deque.reverse(), this.size - this.position); }
    /** The deque version this cursor is positioned in. */
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
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.#position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.#position === this.tree.size; }
    /** The combined measure of every element before the gap. */
    public get measureBefore(): M { return this.tree.prefixMeasure(this.#position)!; }
    /** The combined measure of every element at or after the gap. */
    public get measureAfter(): M { return this.tree.splitAtIndex(this.#position)!.right.measure(); }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): SequenceCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.tree.get(this.#position - 1)! };
    }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): SequenceCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.tree.get(this.#position)! };
    }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): FingerTreeCursor<T, M> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new FingerTreeCursor(this.tree, this.#position - 1);
    }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): FingerTreeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new FingerTreeCursor(this.tree, this.#position + 1);
    }
    /** A cursor at the first position whose inclusive prefix measure satisfies the predicate. */
    public seekByMeasure(predicate: (measure: M) => boolean): FingerTreeCursor<T, M> {
        return this.tree.cursorByMeasure(predicate).cursor;
    }
    /** Seek by measure and report whether any prefix satisfied the predicate. */
    public searchByMeasure(predicate: (measure: M) => boolean): FingerTreeCursorSearch<T, M> {
        return this.tree.cursorByMeasure(predicate);
    }
    /** Insert at the gap and return a cursor positioned after the new element. */
    public insert(value: T): FingerTreeCursor<T, M> {
        return new FingerTreeCursor(this.tree.insertAt(this.#position, value)!, this.#position + 1);
    }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): FingerTreeCursor<T, M> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        return new FingerTreeCursor(this.tree.removeAt(this.#position - 1)!, this.#position - 1);
    }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): FingerTreeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new FingerTreeCursor(this.tree.removeAt(this.#position)!, this.#position);
    }
    /** Replace the element after the gap, keeping the gap where it is. */
    public replaceNext(value: T): FingerTreeCursor<T, M> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new FingerTreeCursor(this.tree.setItem(this.#position, value)!, this.#position);
    }
    /** The tree version this cursor is positioned in. */
    public snapshot(): FingerTree<T, M> { return this.tree; }
}
