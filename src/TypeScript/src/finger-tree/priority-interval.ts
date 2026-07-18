import { MeasuredSequence } from "./measured-sequence.js";
import type { MeasurePolicy } from "./measures.js";
import { defaultComparator, type Comparator } from "./ordering.js";

export interface PriorityEntry<T, P> { readonly value: T; readonly priority: P }
export interface PriorityDequeue<T, P> { readonly entry: PriorityEntry<T, P>; readonly queue: PriorityQueue<T, P> }
interface PrioritySummary<T, P> { readonly best: PriorityEntry<T, P> | undefined }

class PriorityMeasure<T, P> implements MeasurePolicy<PriorityEntry<T, P>, PrioritySummary<T, P>> {
    public readonly identity: PrioritySummary<T, P> = { best: undefined };
    public readonly comparator: Comparator<P>;
    public constructor(comparator: Comparator<P>) { this.comparator = comparator; }
    public measure(element: PriorityEntry<T, P>): PrioritySummary<T, P> { return { best: element }; }
    public combine(left: PrioritySummary<T, P>, right: PrioritySummary<T, P>): PrioritySummary<T, P> {
        if (left.best === undefined) return right;
        if (right.best === undefined) return left;
        return this.comparator(left.best.priority, right.best.priority) <= 0 ? left : right;
    }
}

/** Stable immutable priority queue with cached O(1) minimum. */
export class PriorityQueue<T, P> implements Iterable<PriorityEntry<T, P>> {
    readonly #entries: MeasuredSequence<PriorityEntry<T, P>, PrioritySummary<T, P>>;
    readonly #measure: PriorityMeasure<T, P>;
    public readonly comparator: Comparator<P>;
    private constructor(entries: MeasuredSequence<PriorityEntry<T, P>, PrioritySummary<T, P>>, comparator: Comparator<P>, measure: PriorityMeasure<T, P>) {
        this.#entries = entries; this.comparator = comparator; this.#measure = measure;
    }
    public static empty<T, P>(comparator: Comparator<P> = defaultComparator): PriorityQueue<T, P> {
        const measure = new PriorityMeasure<T, P>(comparator);
        return new PriorityQueue(MeasuredSequence.empty(measure), comparator, measure);
    }
    public get size(): number { return this.#entries.size; }
    public get isEmpty(): boolean { return this.#entries.isEmpty; }
    public enqueue(value: T, priority: P): PriorityQueue<T, P> { return new PriorityQueue(this.#entries.append({ value, priority }), this.comparator, this.#measure); }
    public meld(other: PriorityQueue<T, P>): PriorityQueue<T, P> {
        if (this.comparator !== other.comparator) throw new TypeError("Cannot meld queues with different comparator objects.");
        if (this.isEmpty) return other;
        if (other.isEmpty) return this;
        return new PriorityQueue(MeasuredSequence.from([...this, ...other], this.#measure), this.comparator, this.#measure);
    }
    public peekEntry(): PriorityEntry<T, P> | undefined { return this.#entries.measure.best; }
    public peek(): readonly [T, P] | undefined { const entry = this.peekEntry(); return entry === undefined ? undefined : [entry.value, entry.priority]; }
    public peekPriority(): P | undefined { return this.peekEntry()?.priority; }
    public dequeue(): PriorityDequeue<T, P> | undefined {
        const entry = this.peekEntry();
        if (entry === undefined) return undefined;
        const located = this.#entries.locate((summary) => summary.best === entry);
        if (!located.found) throw new Error("Cached priority summary did not identify an entry.");
        return { entry, queue: new PriorityQueue(this.#entries.removeAt(located.index)!, this.comparator, this.#measure) };
    }
    public toArray(): PriorityEntry<T, P>[] { return this.#entries.toArray(); }
    public sharesStorageWith(other: PriorityQueue<T, P>): boolean { return this.#entries.sharesStructureWith(other.#entries); }
    public [Symbol.iterator](): IterableIterator<PriorityEntry<T, P>> { return this.#entries[Symbol.iterator](); }
}

/** Closed interval. */
export class Interval<T> {
    public readonly low: T;
    public readonly high: T;
    public constructor(low: T, high: T, comparator: Comparator<T> = defaultComparator) {
        if (comparator(low, high) > 0) throw new RangeError("Interval low endpoint must not exceed high endpoint.");
        this.low = low; this.high = high;
    }
    public overlaps(other: Interval<T>, comparator: Comparator<T> = defaultComparator): boolean {
        return comparator(this.low, other.high) <= 0 && comparator(other.low, this.high) <= 0;
    }
    public containsPoint(point: T, comparator: Comparator<T> = defaultComparator): boolean {
        return comparator(this.low, point) <= 0 && comparator(point, this.high) <= 0;
    }
}

export interface IntervalRemoveResult<T> { readonly tree: IntervalTree<T>; readonly interval: Interval<T> }
export interface IntervalCursorSearch<T> { readonly found: boolean; readonly cursor: IntervalTreeCursor<T> }
interface IntervalSummary<T> { readonly maximumHigh: T | undefined; readonly lastLow: T | undefined }

class IntervalMeasure<T> implements MeasurePolicy<Interval<T>, IntervalSummary<T>> {
    public readonly identity: IntervalSummary<T> = { maximumHigh: undefined, lastLow: undefined };
    public readonly comparator: Comparator<T>;
    public constructor(comparator: Comparator<T>) { this.comparator = comparator; }
    public measure(element: Interval<T>): IntervalSummary<T> { return { maximumHigh: element.high, lastLow: element.low }; }
    public combine(left: IntervalSummary<T>, right: IntervalSummary<T>): IntervalSummary<T> {
        const maximumHigh = left.maximumHigh === undefined ? right.maximumHigh
            : right.maximumHigh === undefined ? left.maximumHigh
                : this.comparator(left.maximumHigh, right.maximumHigh) >= 0 ? left.maximumHigh : right.maximumHigh;
        return { maximumHigh, lastLow: right.lastLow ?? left.lastLow };
    }
}

/** Immutable max-high annotated interval tree ordered by low endpoint. */
export class IntervalTree<T> implements Iterable<Interval<T>> {
    readonly #intervals: MeasuredSequence<Interval<T>, IntervalSummary<T>>;
    readonly #measure: IntervalMeasure<T>;
    public readonly comparator: Comparator<T>;
    private constructor(intervals: MeasuredSequence<Interval<T>, IntervalSummary<T>>, comparator: Comparator<T>, measure: IntervalMeasure<T>) {
        this.#intervals = intervals; this.comparator = comparator; this.#measure = measure;
    }
    public static empty<T>(comparator: Comparator<T> = defaultComparator): IntervalTree<T> {
        const measure = new IntervalMeasure<T>(comparator);
        return new IntervalTree(MeasuredSequence.empty(measure), comparator, measure);
    }
    public static from<T>(values: Iterable<Interval<T>>, comparator: Comparator<T> = defaultComparator): IntervalTree<T> {
        let result = IntervalTree.empty(comparator); for (const value of values) result = result.insert(value); return result;
    }
    public get size(): number { return this.#intervals.size; }
    public get isEmpty(): boolean { return this.#intervals.isEmpty; }
    #lowerBound(low: T): number { return this.#intervals.locate((summary) => summary.lastLow !== undefined && this.comparator(summary.lastLow, low) >= 0).index; }
    #indexOf(interval: Interval<T>): number {
        let index = this.#lowerBound(interval.low);
        while (index < this.size) {
            const current = this.#intervals.at(index)!;
            if (this.comparator(current.low, interval.low) !== 0) return -1;
            if (this.comparator(current.high, interval.high) === 0) return index;
            index++;
        }
        return -1;
    }
    public insert(interval: Interval<T>): IntervalTree<T> {
        if (this.comparator(interval.low, interval.high) > 0) throw new RangeError("Interval low endpoint must not exceed high endpoint.");
        return new IntervalTree(this.#intervals.insertAt(this.#lowerBound(interval.low), interval)!, this.comparator, this.#measure);
    }
    public contains(interval: Interval<T>): boolean { return this.#indexOf(interval) >= 0; }
    public remove(interval: Interval<T>): IntervalTree<T> { return this.tryRemove(interval)?.tree ?? this; }
    public tryRemove(interval: Interval<T>): IntervalRemoveResult<T> | undefined {
        const index = this.#indexOf(interval); if (index < 0) return undefined;
        return { tree: new IntervalTree(this.#intervals.removeAt(index)!, this.comparator, this.#measure), interval: this.#intervals.at(index)! };
    }
    #nextOverlap(probe: Interval<T>, source: MeasuredSequence<Interval<T>, IntervalSummary<T>>): readonly [Interval<T>, MeasuredSequence<Interval<T>, IntervalSummary<T>>] | undefined {
        const located = source.locate((summary) => summary.maximumHigh !== undefined && this.comparator(summary.maximumHigh, probe.low) >= 0);
        if (!located.found || this.comparator(located.value!.low, probe.high) > 0) return undefined;
        return [located.value!, source.splitAt(located.index + 1)!.right];
    }
    public findOverlap(probe: Interval<T>): Interval<T> | undefined { return this.#nextOverlap(probe, this.#intervals)?.[0]; }
    public findContaining(point: T): Interval<T> | undefined { return this.findOverlap(new Interval(point, point, this.comparator)); }
    public findOverlaps(probe: Interval<T>): Interval<T>[] {
        const result: Interval<T>[] = []; let remaining = this.#intervals;
        while (true) { const next = this.#nextOverlap(probe, remaining); if (next === undefined) break; result.push(next[0]); remaining = next[1]; }
        return result;
    }
    public countOverlaps(probe: Interval<T>): number { return this.findOverlaps(probe).length; }
    public cursorAt(position = 0): IntervalTreeCursor<T> { return new IntervalTreeCursor(this, position); }
    public cursorAtLowerBound(low: T): IntervalTreeCursor<T> { return this.cursorAt(this.#lowerBound(low)); }
    public cursorAtUpperBound(low: T): IntervalTreeCursor<T> {
        const values = this.toArray(); let position = this.#lowerBound(low);
        while (position < values.length && this.comparator(values[position]!.low, low) === 0) position++;
        return this.cursorAt(position);
    }
    public findCursor(interval: Interval<T>): IntervalCursorSearch<T> {
        const values = this.toArray(); const lower = this.#lowerBound(interval.low);
        for (let position = lower; position < values.length && this.comparator(values[position]!.low, interval.low) === 0; position++) {
            if (this.comparator(values[position]!.high, interval.high) === 0) return { found: true, cursor: this.cursorAt(position) };
        }
        return { found: false, cursor: this.cursorAt(lower) };
    }
    public findOverlapCursor(probe: Interval<T>): IntervalCursorSearch<T> { return this.findOverlapCursorFrom(0, probe); }
    public findContainingCursor(point: T): IntervalCursorSearch<T> { return this.findOverlapCursor(new Interval(point, point, this.comparator)); }
    public findOverlapCursorFrom(start: number, probe: Interval<T>): IntervalCursorSearch<T> {
        if (!Number.isInteger(start) || start < 0 || start > this.size) throw new RangeError("Cursor start is outside the interval tree.");
        const values = this.toArray();
        for (let position = start; position < values.length; position++) {
            const interval = values[position]!;
            if (this.comparator(interval.low, probe.high) > 0) break;
            if (interval.overlaps(probe, this.comparator)) return { found: true, cursor: this.cursorAt(position) };
        }
        return { found: false, cursor: this.cursorAt(this.size) };
    }
    public coalesce(): IntervalTree<T> {
        if (this.size < 2) return this;
        const result: Interval<T>[] = [];
        for (const next of this) {
            const current = result.at(-1);
            if (current !== undefined && this.comparator(next.low, current.high) <= 0) {
                result[result.length - 1] = new Interval(current.low, this.comparator(current.high, next.high) >= 0 ? current.high : next.high, this.comparator);
            } else result.push(next);
        }
        return IntervalTree.from(result, this.comparator);
    }
    public toArray(): Interval<T>[] { return this.#intervals.toArray(); }
    public sharesStorageWith(other: IntervalTree<T>): boolean { return this.#intervals.sharesStructureWith(other.#intervals); }
    public [Symbol.iterator](): IterableIterator<Interval<T>> { return this.#intervals[Symbol.iterator](); }
}

/** Immutable low-endpoint-order root-plus-rank cursor over an interval tree. */
export class IntervalTreeCursor<T> {
    public constructor(public readonly tree: IntervalTree<T>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > tree.size) throw new RangeError("Cursor position is outside the interval tree.");
    }
    public get size(): number { return this.tree.size; }
    public get isAtStart(): boolean { return this.position === 0; }
    public get isAtEnd(): boolean { return this.position === this.size; }
    public peekPrevious(): { readonly value: Interval<T> } | undefined { return this.isAtStart ? undefined : { value: this.tree.toArray()[this.position - 1]! }; }
    public peekNext(): { readonly value: Interval<T> } | undefined { return this.isAtEnd ? undefined : { value: this.tree.toArray()[this.position]! }; }
    public movePrevious(): IntervalTreeCursor<T> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new IntervalTreeCursor(this.tree, this.position - 1); }
    public moveNext(): IntervalTreeCursor<T> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new IntervalTreeCursor(this.tree, this.position + 1); }
    public seekRank(position: number): IntervalTreeCursor<T> { return position === this.position ? this : new IntervalTreeCursor(this.tree, position); }
    public seekNextOverlap(probe: Interval<T>): IntervalCursorSearch<T> { return this.tree.findOverlapCursorFrom(this.position < this.size ? this.position + 1 : this.size, probe); }
    public insert(interval: Interval<T>): IntervalTreeCursor<T> { const position = this.tree.cursorAtLowerBound(interval.low).position; return new IntervalTreeCursor(this.tree.insert(interval), position + 1); }
    public deletePrevious(): IntervalTreeCursor<T> { if (this.isAtStart) throw new RangeError("No occurrence precedes the cursor."); return this.deleteAt(this.position - 1); }
    public deleteNext(): IntervalTreeCursor<T> { if (this.isAtEnd) throw new RangeError("No occurrence follows the cursor."); return this.deleteAt(this.position); }
    private deleteAt(rank: number): IntervalTreeCursor<T> {
        const values = this.tree.toArray(); values.splice(rank, 1); values.reverse();
        const tree = IntervalTree.from(values, this.tree.comparator);
        return new IntervalTreeCursor(tree, rank < this.position ? this.position - 1 : this.position);
    }
    public snapshot(): IntervalTree<T> { return this.tree; }
}
