/**
 * Tests that a range-update sequence is left unchanged when a caller-supplied algebra callback
 * throws.
 */
import { describe, expect, it } from "vitest";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import {
    addTag,
    assignTag,
    FailingAffineAlgebra,
    type AffineTag,
} from "./range-update-test-support.js";

type Sequence = RangeUpdateSequence<number, number, AffineTag>;

interface FailureCase {
    readonly algebra: FailingAffineAlgebra;
    readonly sources: readonly Sequence[];
    readonly operation: () => unknown;
}

function sweepOperationFailures(createCase: () => FailureCase): void {
    const baseline = createCase();
    baseline.algebra.disarm();
    baseline.operation();
    const callbackCount = baseline.algebra.calls;
    expect(callbackCount).toBeGreaterThan(0);

    for (let ordinal = 1; ordinal <= callbackCount; ordinal++) {
        const trial = createCase();
        const snapshots = trial.sources.map(source => source.toArray());
        trial.algebra.arm(ordinal);
        expect(trial.operation).toThrow(/Injected/);
        expect(trial.algebra.calls).toBe(ordinal);

        trial.algebra.disarm();
        for (let sourceIndex = 0; sourceIndex < trial.sources.length; sourceIndex++) {
            const source = trial.sources[sourceIndex] as Sequence;
            expect(source.toArray()).toEqual(snapshots[sourceIndex]);
            source.validateStructure();
        }
    }
}

function taggedSource(algebra: FailingAffineAlgebra): Sequence {
    return RangeUpdateSequence.from(
        Array.from({ length: 31 }, (_, index) => index - 10),
        algebra,
    )
        .applyRange(0, 31, addTag(5))
        .applyRange(7, 17, assignTag(4));
}

describe("RangeUpdateSequence failure atomicity", () => {
    it("publishes no sequence for any construction callback failure", () => {
        const values = Array.from({ length: 23 }, (_, index) => index);
        const baselineAlgebra = new FailingAffineAlgebra();
        baselineAlgebra.disarm();
        RangeUpdateSequence.create(values, baselineAlgebra);
        const callbackCount = baselineAlgebra.calls;
        expect(callbackCount).toBeGreaterThan(values.length);

        for (let ordinal = 1; ordinal <= callbackCount; ordinal++) {
            const algebra = new FailingAffineAlgebra();
            algebra.arm(ordinal);
            expect(() => RangeUpdateSequence.create(values, algebra)).toThrow(/Injected/);
            expect(algebra.calls).toBe(ordinal);
        }

        const finalAlgebra = new FailingAffineAlgebra();
        const result = RangeUpdateSequence.from(values, finalAlgebra);
        expect(result.toArray()).toEqual(values);
        result.validateStructure();
    });

    it("keeps the source usable through every whole and stacked tag callback failure", () => {
        sweepOperationFailures(() => {
            const algebra = new FailingAffineAlgebra();
            const source = RangeUpdateSequence.from([1, 2, 3, 4, 5, 6, 7], algebra);
            algebra.disarm();
            return {
                algebra,
                sources: [source],
                operation: () => source.applyRange(0, source.size, addTag(9)),
            };
        });

        sweepOperationFailures(() => {
            const algebra = new FailingAffineAlgebra();
            const source = RangeUpdateSequence.from([1, 2, 3, 4, 5, 6, 7], algebra)
                .applyRange(0, 7, addTag(9));
            algebra.disarm();
            return {
                algebra,
                sources: [source],
                operation: () => source.applyRange(0, source.size, addTag(-4)),
            };
        });
    });

    it("keeps tagged sources usable through every point-edit callback failure", () => {
        const cases: readonly ((source: Sequence) => unknown)[] = [
            source => source.insert(13, 500),
            source => source.setItem(13, 501),
            source => source.removeAt(13),
        ];

        for (const operation of cases) {
            sweepOperationFailures(() => {
                const algebra = new FailingAffineAlgebra();
                const source = taggedSource(algebra);
                algebra.disarm();
                return { algebra, sources: [source], operation: () => operation(source) };
            });
        }
    });

    it("keeps sources usable through split, slice, and proper range-update failures", () => {
        const cases: readonly ((source: Sequence) => unknown)[] = [
            source => source.splitAt(14),
            source => source.getRange(5, 19),
            source => source.applyRange(4, 22, { multiply: -2, add: 7 }),
        ];

        for (const operation of cases) {
            sweepOperationFailures(() => {
                const algebra = new FailingAffineAlgebra();
                const source = taggedSource(algebra);
                algebra.disarm();
                return { algebra, sources: [source], operation: () => operation(source) };
            });
        }
    });

    it("keeps both concatenation operands usable through every callback failure", () => {
        sweepOperationFailures(() => {
            const algebra = new FailingAffineAlgebra();
            const left = RangeUpdateSequence.from(
                Array.from({ length: 19 }, (_, index) => index),
                algebra,
            ).applyRange(0, 19, addTag(3));
            const right = RangeUpdateSequence.from(
                Array.from({ length: 27 }, (_, index) => 100 + index),
                algebra,
            ).applyRange(2, 20, assignTag(-5));
            algebra.disarm();
            return { algebra, sources: [left, right], operation: () => left.concat(right) };
        });
    });

    it("keeps a tagged source usable through every read callback failure", () => {
        const cases: readonly ((source: Sequence) => unknown)[] = [
            source => source.get(12),
            source => source.measureRange(3, 23),
            source => Array.from(source),
        ];

        for (const operation of cases) {
            sweepOperationFailures(() => {
                const algebra = new FailingAffineAlgebra();
                const source = taggedSource(algebra).applyRange(0, 31, addTag(2));
                algebra.disarm();
                return { algebra, sources: [source], operation: () => operation(source) };
            });
        }
    });

    it("validates all malformed operations before invoking a policy callback", () => {
        const algebra = new FailingAffineAlgebra();
        const source = RangeUpdateSequence.from([1, 2, 3], algebra);
        algebra.arm(1);

        expect(() => source.applyRange(-1, 1, addTag(2))).toThrow(RangeError);
        expect(() => source.applyRange(0, -1, addTag(2))).toThrow(RangeError);
        expect(() => source.applyRange(4, 0, addTag(2))).toThrow(RangeError);
        expect(() => source.applyRange(2, 2, addTag(2))).toThrow(RangeError);
        expect(() => source.measureRange(2, 2)).toThrow(RangeError);
        expect(() => source.getRange(Number.NaN, 0)).toThrow(RangeError);
        expect(algebra.calls).toBe(0);
    });
});
