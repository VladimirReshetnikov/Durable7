/**
 * Shared algebras and helpers for the range-update sequence tests, including counting and failing
 * doubles used to observe callback behavior.
 */
import type { RangeUpdateAlgebra } from "../../src/finger-tree/range-update-algebra.js";

/**
 * An affine range-update tag: multiply then add. Affine maps compose, which is what makes them a
 * usable tag monoid.
 */
export interface AffineTag {
    /** The multiplicative factor. */
    readonly multiply: number;
    /** The additive offset, applied after the factor. */
    readonly add: number;
}

/** The tag that changes nothing: multiply by one, add zero. */
export const identityAffineTag: AffineTag = { multiply: 1, add: 0 };

/** A tag adding a constant to each element. */
export function addTag(add: number): AffineTag { return { multiply: 1, add }; }
/** A tag replacing each element with a constant, expressed as a zero factor plus an offset. */
export function assignTag(value: number): AffineTag { return { multiply: 0, add: value }; }
/** A tag scaling each element by a constant. */
export function scaleTag(multiply: number): AffineTag { return { multiply, add: 0 }; }

function canonicalNumber(value: number): number { return value === 0 ? 0 : value; }

/** Apply an affine tag to one value. */
export function applyAffine(tag: AffineTag, value: number): number {
    return canonicalNumber(tag.multiply * value + tag.add);
}

/** A summing measure with affine tags, the simplest algebra satisfying the range-update laws. */
export class SumAffineAlgebra implements RangeUpdateAlgebra<number, number, AffineTag> {
    /** The measure identity, zero. */
    public readonly identity = 0;
    /** The tag identity. */
    public readonly identityTag: AffineTag = identityAffineTag;

    /** Add two running sums. */
    public combine(left: number, right: number): number { return canonicalNumber(left + right); }
    /** An element measures as its own value. */
    public measure(element: number): number { return canonicalNumber(element); }
    /**
     * Whether the tag acts as the identity, recognizing any inert affine map rather than only the
     * canonical one.
     */
    public isIdentity(tag: AffineTag): boolean { return tag.multiply === 1 && tag.add === 0; }
    /**
     * Compose two affine tags, the older applied first, so the tests can observe composition order.
     */
    public compose(newer: AffineTag, older: AffineTag): AffineTag {
        return {
            multiply: canonicalNumber(newer.multiply * older.multiply),
            add: canonicalNumber(newer.multiply * older.add + newer.add),
        };
    }
    /** Apply a tag to one element. */
    public applyElement(tag: AffineTag, element: number): number { return applyAffine(tag, element); }
    /**
     * Apply a tag to the cached measure of the given number of elements, which is what keeps a
     * deferred tag indistinguishable from an applied one.
     */
    public applyMeasure(tag: AffineTag, measure: number, count: number): number {
        return canonicalNumber(tag.multiply * measure + tag.add * count);
    }
}

/**
 * A measure carrying length, sum, and weighted sum together, so the tests exercise a tag action
 * over more than one component.
 */
export interface WeightedMeasure {
    /** Number of elements summarized. */
    readonly length: number;
    /** Sum of the elements. */
    readonly sum: number;
    /** Position-weighted sum, which a tag must transform consistently with the plain sum. */
    readonly weightedSum: number;
}

/**
 * An affine algebra over the multi-component weighted measure, exercising the distribution law that
 * a single-component measure would not catch.
 */
export class WeightedAffineAlgebra
implements RangeUpdateAlgebra<number, WeightedMeasure, AffineTag> {
    public readonly identity: WeightedMeasure = { length: 0, sum: 0, weightedSum: 0 };
    public readonly identityTag: AffineTag = identityAffineTag;

    public combine(left: WeightedMeasure, right: WeightedMeasure): WeightedMeasure {
        return {
            length: left.length + right.length,
            sum: canonicalNumber(left.sum + right.sum),
            weightedSum: canonicalNumber(
                left.weightedSum + right.weightedSum + left.length * right.sum,
            ),
        };
    }

    public measure(element: number): WeightedMeasure {
        const value = canonicalNumber(element);
        return { length: 1, sum: value, weightedSum: value };
    }

    public isIdentity(tag: AffineTag): boolean { return tag.multiply === 1 && tag.add === 0; }

    public compose(newer: AffineTag, older: AffineTag): AffineTag {
        return {
            multiply: canonicalNumber(newer.multiply * older.multiply),
            add: canonicalNumber(newer.multiply * older.add + newer.add),
        };
    }

    public applyElement(tag: AffineTag, element: number): number { return applyAffine(tag, element); }

    public applyMeasure(tag: AffineTag, measure: WeightedMeasure, count: number): WeightedMeasure {
        return {
            length: measure.length,
            sum: canonicalNumber(tag.multiply * measure.sum + tag.add * count),
            weightedSum: canonicalNumber(
                tag.multiply * measure.weightedSum
                + tag.add * count * (count + 1) / 2,
            ),
        };
    }
}

/** Compute the weighted measure of a sequence directly, as a reference for the cached value. */
export function measureWeighted(values: readonly number[]): WeightedMeasure {
    let sum = 0;
    let weightedSum = 0;
    for (let index = 0; index < values.length; index++) {
        const value = values[index] as number;
        sum += value;
        weightedSum += (index + 1) * value;
    }
    return {
        length: values.length,
        sum: canonicalNumber(sum),
        weightedSum: canonicalNumber(weightedSum),
    };
}

/**
 * An affine algebra whose callbacks can be armed to throw, so the tests can check that a failed
 * callback leaves the sequence unchanged.
 */
export class FailingAffineAlgebra extends SumAffineAlgebra {
    #remaining = Number.POSITIVE_INFINITY;
    #calls = 0;

    /** How many callbacks have run since the last arming. */
    public get calls(): number { return this.#calls; }

    /** Arrange for the named callback to throw on its next invocation. */
    public arm(ordinal: number): void {
        if (!Number.isInteger(ordinal) || ordinal <= 0) throw new RangeError("Ordinal must be positive.");
        this.#remaining = ordinal;
        this.#calls = 0;
    }

    /** Disarm the failure, so later calls succeed. */
    public disarm(): void {
        this.#remaining = Number.POSITIVE_INFINITY;
        this.#calls = 0;
    }

    private hit(name: string): void {
        this.#calls++;
        if (--this.#remaining === 0) throw new Error(`Injected ${name} failure.`);
    }

    public override combine(left: number, right: number): number {
        this.hit("combine");
        return super.combine(left, right);
    }

    public override measure(element: number): number {
        this.hit("measure");
        return super.measure(element);
    }

    public override isIdentity(tag: AffineTag): boolean {
        this.hit("isIdentity");
        return super.isIdentity(tag);
    }

    public override compose(newer: AffineTag, older: AffineTag): AffineTag {
        this.hit("compose");
        return super.compose(newer, older);
    }

    public override applyElement(tag: AffineTag, element: number): number {
        this.hit("applyElement");
        return super.applyElement(tag, element);
    }

    public override applyMeasure(tag: AffineTag, measure: number, count: number): number {
        this.hit("applyMeasure");
        return super.applyMeasure(tag, measure, count);
    }
}

/**
 * The array a tag should produce over a range, computed eagerly as a reference for the lazy result.
 */
export function expectedAfterTag(
    values: readonly number[],
    index: number,
    count: number,
    tag: AffineTag,
): number[] {
    return values.map((value, current) =>
        current >= index && current < index + count ? applyAffine(tag, value) : value);
}
