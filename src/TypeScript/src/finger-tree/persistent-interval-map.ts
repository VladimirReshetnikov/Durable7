import { Interval } from "./priority-interval.js";
import { MeasuredSequence } from "./measured-sequence.js";
import type { MeasurePolicy } from "./measures.js";
import { defaultComparator, type Comparator } from "./ordering.js";

/** One interval key and its associated payload. */
export interface IntervalMapEntry<T, V> {
    readonly interval: Interval<T>;
    readonly value: V;
}

/** Presence-discriminated exact-key lookup, including stored representatives. */
export type IntervalMapLookup<T, V> =
    | { readonly found: true; readonly entry: IntervalMapEntry<T, V> }
    | { readonly found: false };

/** Result of a nonthrowing strict insertion. */
export interface IntervalMapAddResult<T, V> {
    readonly added: boolean;
    readonly map: PersistentIntervalMap<T, V>;
}

/** Raised when strict insertion names an existing interval key. */
export class DuplicateIntervalError extends Error {
    public constructor() {
        super("An equivalent interval key is already present.");
        this.name = "DuplicateIntervalError";
    }
}

interface Present<T> { readonly value: T }
interface IntervalMapSummary<T> {
    readonly lastInterval: Interval<T> | undefined;
    readonly maximumHigh: Present<T> | undefined;
}

function sameValueZero<T>(left: T, right: T): boolean {
    return left === right || (left !== left && right !== right);
}

function compareIntervals<T>(
    left: Interval<T>,
    right: Interval<T>,
    comparator: Comparator<T>,
): number {
    const low = comparator(left.low, right.low);
    return low !== 0 ? low : comparator(left.high, right.high);
}

class IntervalMapMeasure<T, V> implements MeasurePolicy<IntervalMapEntry<T, V>, IntervalMapSummary<T>> {
    public readonly identity: IntervalMapSummary<T> = {
        lastInterval: undefined,
        maximumHigh: undefined,
    };

    public constructor(public readonly comparator: Comparator<T>) {}

    public measure(entry: IntervalMapEntry<T, V>): IntervalMapSummary<T> {
        return { lastInterval: entry.interval, maximumHigh: { value: entry.interval.high } };
    }

    public combine(left: IntervalMapSummary<T>, right: IntervalMapSummary<T>): IntervalMapSummary<T> {
        let maximumHigh: Present<T> | undefined;
        if (left.maximumHigh === undefined) maximumHigh = right.maximumHigh;
        else if (right.maximumHigh === undefined) maximumHigh = left.maximumHigh;
        else maximumHigh = this.comparator(left.maximumHigh.value, right.maximumHigh.value) >= 0
            ? left.maximumHigh
            : right.maximumHigh;
        return { lastInterval: right.lastInterval ?? left.lastInterval, maximumHigh };
    }
}

/**
 * Immutable map from comparer-distinct closed intervals to payload values.
 *
 * Keys are ordered lexicographically by `(low, high)`. The measured sequence caches both its
 * rightmost complete key and maximum high endpoint, supporting logarithmic exact-key positioning
 * and pruned overlap searches without a second index. Distinct overlapping keys remain distinct.
 */
export class PersistentIntervalMap<T, V> implements Iterable<IntervalMapEntry<T, V>> {
    readonly #entries: MeasuredSequence<IntervalMapEntry<T, V>, IntervalMapSummary<T>>;
    readonly #measure: IntervalMapMeasure<T, V>;

    private constructor(
        entries: MeasuredSequence<IntervalMapEntry<T, V>, IntervalMapSummary<T>>,
        public readonly comparator: Comparator<T>,
        public readonly valueEquals: (left: V, right: V) => boolean,
        measure: IntervalMapMeasure<T, V>,
    ) {
        this.#entries = entries;
        this.#measure = measure;
    }

    public static empty<T, V>(
        comparator: Comparator<T> = defaultComparator,
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentIntervalMap<T, V> {
        const measure = new IntervalMapMeasure<T, V>(comparator);
        return new PersistentIntervalMap(MeasuredSequence.empty(measure), comparator, valueEquals, measure);
    }

    /** Constructs with later comparer-distinct payloads winning while retaining first keys. */
    public static from<T, V>(
        entries: Iterable<readonly [Interval<T>, V]>,
        comparator: Comparator<T> = defaultComparator,
        valueEquals: (left: V, right: V) => boolean = sameValueZero,
    ): PersistentIntervalMap<T, V> {
        if (entries === null || entries === undefined) throw new TypeError("entries must be iterable.");
        let result = PersistentIntervalMap.empty<T, V>(comparator, valueEquals);
        for (const [interval, value] of entries) result = result.set(interval, value);
        return result;
    }

    public get size(): number { return this.#entries.size; }
    public get isEmpty(): boolean { return this.#entries.isEmpty; }

    public get(interval: Interval<T>): V | undefined {
        const lookup = this.tryGetEntry(interval);
        return lookup.found ? lookup.entry.value : undefined;
    }

    public tryGetEntry(interval: Interval<T>): IntervalMapLookup<T, V> {
        this.requireValid(interval);
        const index = this.lowerBound(interval);
        const stored = this.#entries.at(index);
        return stored !== undefined && compareIntervals(stored.interval, interval, this.comparator) === 0
            ? { found: true, entry: stored }
            : { found: false };
    }

    public containsKey(interval: Interval<T>): boolean { return this.tryGetEntry(interval).found; }

    /** Strictly inserts an absent interval key. */
    public add(interval: Interval<T>, value: V): PersistentIntervalMap<T, V> {
        const result = this.tryAdd(interval, value);
        if (!result.added) throw new DuplicateIntervalError();
        return result.map;
    }

    public tryAdd(interval: Interval<T>, value: V): IntervalMapAddResult<T, V> {
        this.requireValid(interval);
        const index = this.lowerBound(interval);
        const stored = this.#entries.at(index);
        if (stored !== undefined && compareIntervals(stored.interval, interval, this.comparator) === 0) {
            return { added: false, map: this };
        }
        return { added: true, map: this.wrap(this.#entries.insertAt(index, { interval, value })!) };
    }

    /** Adds or replaces a payload while retaining the first interval representative. */
    public set(interval: Interval<T>, value: V): PersistentIntervalMap<T, V> {
        this.requireValid(interval);
        const index = this.lowerBound(interval);
        const stored = this.#entries.at(index);
        if (stored === undefined || compareIntervals(stored.interval, interval, this.comparator) !== 0) {
            return this.wrap(this.#entries.insertAt(index, { interval, value })!);
        }
        if (this.valueEquals(stored.value, value)) return this;
        return this.wrap(this.#entries.setAt(index, { interval: stored.interval, value })!);
    }

    public remove(interval: Interval<T>): PersistentIntervalMap<T, V> {
        this.requireValid(interval);
        const index = this.lowerBound(interval);
        const stored = this.#entries.at(index);
        return stored === undefined || compareIntervals(stored.interval, interval, this.comparator) !== 0
            ? this
            : this.wrap(this.#entries.removeAt(index)!);
    }

    public clear(): PersistentIntervalMap<T, V> {
        return this.isEmpty ? this : PersistentIntervalMap.empty(this.comparator, this.valueEquals);
    }

    public findOverlap(probe: Interval<T>): IntervalMapEntry<T, V> | undefined {
        this.requireValid(probe);
        const candidates = this.candidatePrefix(probe.high);
        const located = candidates.locate((summary) => summary.maximumHigh !== undefined
            && this.comparator(summary.maximumHigh.value, probe.low) >= 0);
        return located.found ? located.value : undefined;
    }

    public findContaining(point: T): IntervalMapEntry<T, V> | undefined {
        const candidates = this.candidatePrefix(point);
        const located = candidates.locate((summary) => summary.maximumHigh !== undefined
            && this.comparator(summary.maximumHigh.value, point) >= 0);
        return located.found ? located.value : undefined;
    }

    /** Returns all overlaps in lexicographic key order using repeated measured pruning. */
    public findOverlaps(probe: Interval<T>): IntervalMapEntry<T, V>[] {
        this.requireValid(probe);
        const result: IntervalMapEntry<T, V>[] = [];
        let remaining = this.candidatePrefix(probe.high);
        while (!remaining.isEmpty) {
            const located = remaining.locate((summary) => summary.maximumHigh !== undefined
                && this.comparator(summary.maximumHigh.value, probe.low) >= 0);
            if (!located.found || located.value === undefined) break;
            result.push(located.value);
            remaining = remaining.splitAt(located.index + 1)!.right;
        }
        return result;
    }

    public countOverlaps(probe: Interval<T>): number { return this.findOverlaps(probe).length; }

    public *keys(): Generator<Interval<T>, void> { for (const entry of this.#entries) yield entry.interval; }
    public *values(): Generator<V, void> { for (const entry of this.#entries) yield entry.value; }
    public [Symbol.iterator](): IterableIterator<IntervalMapEntry<T, V>> { return this.#entries[Symbol.iterator](); }

    public sharesStorageWith(other: PersistentIntervalMap<T, V>): boolean {
        return this.#entries.sharesStructureWith(other.#entries);
    }

    /** Checks key ordering, interval validity, and both cached annotations. */
    public validateStructure(): boolean {
        let previous: Interval<T> | undefined;
        let maximumHigh: Present<T> | undefined;
        for (const entry of this.#entries) {
            if (this.comparator(entry.interval.low, entry.interval.high) > 0) return false;
            if (previous !== undefined && compareIntervals(previous, entry.interval, this.comparator) >= 0) return false;
            previous = entry.interval;
            if (maximumHigh === undefined || this.comparator(entry.interval.high, maximumHigh.value) > 0) {
                maximumHigh = { value: entry.interval.high };
            }
        }
        const summary = this.#entries.measure;
        return summary.lastInterval === previous
            && (summary.maximumHigh === undefined) === (maximumHigh === undefined)
            && (summary.maximumHigh === undefined || maximumHigh === undefined
                || this.comparator(summary.maximumHigh.value, maximumHigh.value) === 0);
    }

    private requireValid(interval: Interval<T>): void {
        if (interval === null || interval === undefined) throw new TypeError("interval must be provided.");
        if (this.comparator(interval.low, interval.high) > 0) {
            throw new RangeError("Interval low endpoint must not exceed high endpoint.");
        }
    }

    private lowerBound(interval: Interval<T>): number {
        return this.#entries.locate((summary) => summary.lastInterval !== undefined
            && compareIntervals(summary.lastInterval, interval, this.comparator) >= 0).index;
    }

    private candidatePrefix(high: T): MeasuredSequence<IntervalMapEntry<T, V>, IntervalMapSummary<T>> {
        const end = this.#entries.locate((summary) => summary.lastInterval !== undefined
            && this.comparator(summary.lastInterval.low, high) > 0).index;
        return this.#entries.splitAt(end)!.left;
    }

    private wrap(
        entries: MeasuredSequence<IntervalMapEntry<T, V>, IntervalMapSummary<T>>,
    ): PersistentIntervalMap<T, V> {
        return new PersistentIntervalMap(entries, this.comparator, this.valueEquals, this.#measure);
    }
}
