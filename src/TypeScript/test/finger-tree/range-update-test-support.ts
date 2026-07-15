import type { RangeUpdateAlgebra } from "../../src/finger-tree/range-update-algebra.js";

export interface AffineTag {
    readonly multiply: number;
    readonly add: number;
}

export const identityAffineTag: AffineTag = { multiply: 1, add: 0 };

export function addTag(add: number): AffineTag { return { multiply: 1, add }; }
export function assignTag(value: number): AffineTag { return { multiply: 0, add: value }; }
export function scaleTag(multiply: number): AffineTag { return { multiply, add: 0 }; }

function canonicalNumber(value: number): number { return value === 0 ? 0 : value; }

export function applyAffine(tag: AffineTag, value: number): number {
    return canonicalNumber(tag.multiply * value + tag.add);
}

export class SumAffineAlgebra implements RangeUpdateAlgebra<number, number, AffineTag> {
    public readonly identity = 0;
    public readonly identityTag: AffineTag = identityAffineTag;

    public combine(left: number, right: number): number { return canonicalNumber(left + right); }
    public measure(element: number): number { return canonicalNumber(element); }
    public isIdentity(tag: AffineTag): boolean { return tag.multiply === 1 && tag.add === 0; }
    public compose(newer: AffineTag, older: AffineTag): AffineTag {
        return {
            multiply: canonicalNumber(newer.multiply * older.multiply),
            add: canonicalNumber(newer.multiply * older.add + newer.add),
        };
    }
    public applyElement(tag: AffineTag, element: number): number { return applyAffine(tag, element); }
    public applyMeasure(tag: AffineTag, measure: number, count: number): number {
        return canonicalNumber(tag.multiply * measure + tag.add * count);
    }
}

export interface WeightedMeasure {
    readonly length: number;
    readonly sum: number;
    readonly weightedSum: number;
}

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

export class FailingAffineAlgebra extends SumAffineAlgebra {
    #remaining = Number.POSITIVE_INFINITY;
    #calls = 0;

    public get calls(): number { return this.#calls; }

    public arm(ordinal: number): void {
        if (!Number.isInteger(ordinal) || ordinal <= 0) throw new RangeError("Ordinal must be positive.");
        this.#remaining = ordinal;
        this.#calls = 0;
    }

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

export function expectedAfterTag(
    values: readonly number[],
    index: number,
    count: number,
    tag: AffineTag,
): number[] {
    return values.map((value, current) =>
        current >= index && current < index + count ? applyAffine(tag, value) : value);
}
