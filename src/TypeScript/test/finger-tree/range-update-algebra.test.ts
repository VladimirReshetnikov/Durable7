import { describe, expect, it } from "vitest";
import {
    addTag,
    applyAffine,
    assignTag,
    identityAffineTag,
    measureWeighted,
    scaleTag,
    SumAffineAlgebra,
    WeightedAffineAlgebra,
    type AffineTag,
} from "./range-update-test-support.js";

const tags: readonly AffineTag[] = [
    identityAffineTag,
    { multiply: 1, add: 0 },
    addTag(-3),
    addTag(4),
    assignTag(7),
    scaleTag(-2),
    { multiply: 3, add: 5 },
];

describe("RangeUpdateAlgebra", () => {
    it("treats every recognized identity representation as a full identity", () => {
        const algebra = new SumAffineAlgebra();
        const alternateIdentity = { multiply: 1, add: 0 };

        expect(alternateIdentity).not.toBe(algebra.identityTag);
        expect(algebra.isIdentity(alternateIdentity)).toBe(true);
        expect(algebra.applyElement(alternateIdentity, 17)).toBe(17);
        expect(algebra.applyMeasure(alternateIdentity, 31, 9)).toBe(31);

        for (const tag of tags) {
            expect(algebra.compose(alternateIdentity, tag)).toEqual(tag);
            expect(algebra.compose(tag, alternateIdentity)).toEqual(tag);
        }
    });

    it("composes in older-then-newer application order", () => {
        const algebra = new SumAffineAlgebra();
        const older = assignTag(10);
        const newer = { multiply: 2, add: 3 };
        const composed = algebra.compose(newer, older);

        expect(algebra.applyElement(composed, 99)).toBe(23);
        expect(algebra.applyElement(newer, algebra.applyElement(older, 99))).toBe(23);
        expect(algebra.compose(older, newer)).not.toEqual(composed);
    });

    it("obeys tag associativity and the element and measure action laws", () => {
        const algebra = new SumAffineAlgebra();
        for (const first of tags) {
            for (const second of tags) {
                for (const third of tags) {
                    expect(algebra.compose(third, algebra.compose(second, first))).toEqual(
                        algebra.compose(algebra.compose(third, second), first),
                    );
                }

                const composed = algebra.compose(second, first);
                for (const value of [-5, 0, 11]) {
                    expect(algebra.applyElement(composed, value)).toBe(
                        algebra.applyElement(second, algebra.applyElement(first, value)),
                    );
                }
                for (const count of [0, 1, 8]) {
                    const measure = 37;
                    expect(algebra.applyMeasure(composed, measure, count)).toBe(
                        algebra.applyMeasure(
                            second,
                            algebra.applyMeasure(first, measure, count),
                            count,
                        ),
                    );
                }
            }
        }
    });

    it("agrees between singleton elements and singleton measures", () => {
        const algebra = new WeightedAffineAlgebra();
        for (const tag of tags) {
            for (const value of [-7, 0, 13]) {
                expect(algebra.applyMeasure(tag, algebra.measure(value), 1)).toEqual(
                    algebra.measure(algebra.applyElement(tag, value)),
                );
            }
        }
    });

    it("distributes over an ordered noncommutative measure", () => {
        const algebra = new WeightedAffineAlgebra();
        const leftValues = [2, -1, 5];
        const rightValues = [7, 3];
        const left = measureWeighted(leftValues);
        const right = measureWeighted(rightValues);

        expect(algebra.combine(left, right)).not.toEqual(algebra.combine(right, left));
        for (const tag of tags) {
            const combined = algebra.combine(left, right);
            const transformed = algebra.applyMeasure(
                tag,
                combined,
                leftValues.length + rightValues.length,
            );
            expect(transformed).toEqual(algebra.combine(
                algebra.applyMeasure(tag, left, leftValues.length),
                algebra.applyMeasure(tag, right, rightValues.length),
            ));

            const taggedValues = [...leftValues, ...rightValues].map(value => applyAffine(tag, value));
            expect(transformed).toEqual(measureWeighted(taggedValues));
        }
    });

    it("maps every tag over the empty measure at count zero", () => {
        const algebra = new WeightedAffineAlgebra();
        for (const tag of tags) {
            expect(algebra.applyMeasure(tag, algebra.identity, 0)).toEqual(algebra.identity);
        }
    });
});
