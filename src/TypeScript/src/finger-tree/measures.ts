/**
 * The monoid and element-measure policies the measured sequence is parameterized by.
 *
 * A policy must form a monoid - an associative combine with a two-sided identity - because measured
 * search relies on those laws. The module supplies the standard policies plus a product that runs
 * two of them side by side.
 */
import { defaultComparator, type Comparator } from "./ordering.js";

/** Associative operation with an identity. */
export interface Monoid<T> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    readonly identity: T;
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    combine(left: T, right: T): T;
}

/** Monoid-valued measurement of sequence elements. */
export interface MeasurePolicy<T, M> extends Monoid<M> {
    /** The combined measure of every element, read from the cached root measure. */
    measure(element: T): M;
}

/** Element-count measure. */
export class SizeMeasure<T> implements MeasurePolicy<T, number> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public readonly identity = 0;
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: number, right: number): number { return left + right; }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(_element: T): number { return 1; }
}

/** Numeric sum measure. */
export class NumberSumMeasure implements MeasurePolicy<number, number> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public readonly identity = 0;
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: number, right: number): number { return left + right; }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(element: number): number { return element; }
}

/** Bigint sum measure. */
export class BigIntSumMeasure implements MeasurePolicy<bigint, bigint> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public readonly identity = 0n;
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: bigint, right: bigint): bigint { return left + right; }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(element: bigint): bigint { return element; }
}

/** Optional maximum measure. Undefined denotes the monoid identity. */
export class MaxMeasure<T> implements MeasurePolicy<T, T | undefined> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public readonly identity = undefined;
    /** The retained comparator defining the ordering. */
    public readonly comparator: Comparator<T>;
    public constructor(comparator: Comparator<T> = defaultComparator) { this.comparator = comparator; }
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: T | undefined, right: T | undefined): T | undefined {
        if (left === undefined) return right;
        if (right === undefined) return left;
        return this.comparator(left, right) >= 0 ? left : right;
    }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(element: T): T { return element; }
}

/** Optional minimum measure. Undefined denotes the monoid identity. */
export class MinMeasure<T> implements MeasurePolicy<T, T | undefined> {
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public readonly identity = undefined;
    /** The retained comparator defining the ordering. */
    public readonly comparator: Comparator<T>;
    public constructor(comparator: Comparator<T> = defaultComparator) { this.comparator = comparator; }
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: T | undefined, right: T | undefined): T | undefined {
        if (left === undefined) return right;
        if (right === undefined) return left;
        return this.comparator(left, right) <= 0 ? left : right;
    }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(element: T): T { return element; }
}

/** Product of two measures. */
export interface MeasurePair<A, B> {
    /** The first component's measure. */
    readonly first: A;
    /** The second component's measure. */
    readonly second: B;
}

/**
 * Runs two measure policies side by side over the same elements. The product of two monoids is a
 * monoid, so one sequence can be searched by either component - counting elements and summing
 * weights at once, for instance.
 */
export class ProductMeasure<T, A, B> implements MeasurePolicy<T, MeasurePair<A, B>> {
    /** The first component policy. */
    public readonly first: MeasurePolicy<T, A>;
    /** The second component policy. */
    public readonly second: MeasurePolicy<T, B>;
    public constructor(first: MeasurePolicy<T, A>, second: MeasurePolicy<T, B>) {
        this.first = first;
        this.second = second;
    }
    /** The two-sided identity for `combine`, and the measure of the empty sequence. */
    public get identity(): MeasurePair<A, B> { return { first: this.first.identity, second: this.second.identity }; }
    /** Combine two measures in order. Must be associative; it need not be commutative. */
    public combine(left: MeasurePair<A, B>, right: MeasurePair<A, B>): MeasurePair<A, B> {
        return {
            first: this.first.combine(left.first, right.first),
            second: this.second.combine(left.second, right.second),
        };
    }
    /** The combined measure of every element, read from the cached root measure. */
    public measure(element: T): MeasurePair<A, B> {
        return { first: this.first.measure(element), second: this.second.measure(element) };
    }
}

/** Adapts separate monoid and element-measure functions into a policy. */
export function createMeasurePolicy<T, M>(
    identity: M,
    combine: (left: M, right: M) => M,
    measure: (element: T) => M,
): MeasurePolicy<T, M> {
    return { identity, combine, measure };
}
