import { defaultComparator, type Comparator } from "./ordering.js";

/** Associative operation with an identity. */
export interface Monoid<T> {
    readonly identity: T;
    combine(left: T, right: T): T;
}

/** Monoid-valued measurement of sequence elements. */
export interface MeasurePolicy<T, M> extends Monoid<M> {
    measure(element: T): M;
}

/** Element-count measure. */
export class SizeMeasure<T> implements MeasurePolicy<T, number> {
    public readonly identity = 0;
    public combine(left: number, right: number): number { return left + right; }
    public measure(_element: T): number { return 1; }
}

/** Numeric sum measure. */
export class NumberSumMeasure implements MeasurePolicy<number, number> {
    public readonly identity = 0;
    public combine(left: number, right: number): number { return left + right; }
    public measure(element: number): number { return element; }
}

/** Bigint sum measure. */
export class BigIntSumMeasure implements MeasurePolicy<bigint, bigint> {
    public readonly identity = 0n;
    public combine(left: bigint, right: bigint): bigint { return left + right; }
    public measure(element: bigint): bigint { return element; }
}

/** Optional maximum measure. Undefined denotes the monoid identity. */
export class MaxMeasure<T> implements MeasurePolicy<T, T | undefined> {
    public readonly identity = undefined;
    public readonly comparator: Comparator<T>;
    public constructor(comparator: Comparator<T> = defaultComparator) { this.comparator = comparator; }
    public combine(left: T | undefined, right: T | undefined): T | undefined {
        if (left === undefined) return right;
        if (right === undefined) return left;
        return this.comparator(left, right) >= 0 ? left : right;
    }
    public measure(element: T): T { return element; }
}

/** Optional minimum measure. Undefined denotes the monoid identity. */
export class MinMeasure<T> implements MeasurePolicy<T, T | undefined> {
    public readonly identity = undefined;
    public readonly comparator: Comparator<T>;
    public constructor(comparator: Comparator<T> = defaultComparator) { this.comparator = comparator; }
    public combine(left: T | undefined, right: T | undefined): T | undefined {
        if (left === undefined) return right;
        if (right === undefined) return left;
        return this.comparator(left, right) <= 0 ? left : right;
    }
    public measure(element: T): T { return element; }
}

/** Product of two measures. */
export interface MeasurePair<A, B> {
    readonly first: A;
    readonly second: B;
}

export class ProductMeasure<T, A, B> implements MeasurePolicy<T, MeasurePair<A, B>> {
    public readonly first: MeasurePolicy<T, A>;
    public readonly second: MeasurePolicy<T, B>;
    public constructor(first: MeasurePolicy<T, A>, second: MeasurePolicy<T, B>) {
        this.first = first;
        this.second = second;
    }
    public get identity(): MeasurePair<A, B> { return { first: this.first.identity, second: this.second.identity }; }
    public combine(left: MeasurePair<A, B>, right: MeasurePair<A, B>): MeasurePair<A, B> {
        return {
            first: this.first.combine(left.first, right.first),
            second: this.second.combine(left.second, right.second),
        };
    }
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
