import { describe, expect, it } from "vitest";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import {
    addTag,
    assignTag,
    expectedAfterTag,
    measureWeighted,
    scaleTag,
    SumAffineAlgebra,
    WeightedAffineAlgebra,
    type WeightedMeasure,
} from "./range-update-test-support.js";

function weightedEquals(left: WeightedMeasure, right: WeightedMeasure): boolean {
    return left.length === right.length
        && left.sum === right.sum
        && left.weightedSum === right.weightedSum;
}

describe("RangeUpdateSequence lazy propagation", () => {
    it("applies whole-sequence tags in directional older-then-newer order", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from([1, 2, 3, 4], algebra);

        const assignedThenAdded = source
            .applyRange(0, source.size, assignTag(10))
            .applyRange(0, source.size, addTag(3));
        const addedThenAssigned = source
            .applyRange(0, source.size, addTag(3))
            .applyRange(0, source.size, assignTag(10));

        expect(assignedThenAdded.toArray()).toEqual([13, 13, 13, 13]);
        expect(addedThenAssigned.toArray()).toEqual([10, 10, 10, 10]);
        expect(assignedThenAdded.measure).toBe(52);
        expect(addedThenAssigned.measure).toBe(40);
        expect(source.toArray()).toEqual([1, 2, 3, 4]);
    });

    it("composes a newer ancestor tag over older descendant tags during reads", () => {
        const algebra = new SumAffineAlgebra();
        const values = Array.from({ length: 12 }, (_, index) => index);
        const source = RangeUpdateSequence.from(values, algebra);
        const innerAssigned = source.applyRange(2, 7, assignTag(10));
        const ancestorAdded = innerAssigned.applyRange(0, innerAssigned.size, addTag(5));
        const expected = expectedAfterTag(
            expectedAfterTag(values, 2, 7, assignTag(10)),
            0,
            values.length,
            addTag(5),
        );

        expect(ancestorAdded.toArray()).toEqual(expected);
        for (let index = 0; index < expected.length; index++) {
            expect(ancestorAdded.get(index)).toBe(expected[index]);
        }
        ancestorAdded.validateStructure();
        expect(innerAssigned.toArray()).toEqual(expectedAfterTag(values, 2, 7, assignTag(10)));
    });

    it("makes a newer proper-range assignment override an older ancestor update", () => {
        const algebra = new SumAffineAlgebra();
        const values = Array.from({ length: 15 }, (_, index) => index - 4);
        const source = RangeUpdateSequence.from(values, algebra);
        const olderAncestor = source.applyRange(0, values.length, addTag(8));
        const changed = olderAncestor.applyRange(4, 6, assignTag(-3));
        const expected = expectedAfterTag(
            expectedAfterTag(values, 0, values.length, addTag(8)),
            4,
            6,
            assignTag(-3),
        );

        expect(changed.toArray()).toEqual(expected);
        expect(changed.measure).toBe(expected.reduce((sum, value) => sum + value, 0));
        changed.validateStructure();
        expect(olderAncestor.toArray()).toEqual(expectedAfterTag(values, 0, values.length, addTag(8)));
    });

    it("supports deeply overlapping add, assign, and scale updates", () => {
        const algebra = new SumAffineAlgebra();
        let expected = Array.from({ length: 40 }, (_, index) => index - 20);
        let sequence = RangeUpdateSequence.from(expected, algebra);
        const operations = [
            { index: 0, count: 40, tag: addTag(7) },
            { index: 5, count: 28, tag: assignTag(3) },
            { index: 11, count: 19, tag: scaleTag(-2) },
            { index: 8, count: 4, tag: addTag(100) },
            { index: 0, count: 17, tag: { multiply: 3, add: -5 } },
            { index: 16, count: 24, tag: addTag(-9) },
            { index: 20, count: 1, tag: assignTag(44) },
        ] as const;

        for (const operation of operations) {
            const previous = sequence;
            const previousExpected = expected;
            sequence = sequence.applyRange(operation.index, operation.count, operation.tag);
            expected = expectedAfterTag(
                expected,
                operation.index,
                operation.count,
                operation.tag,
            );
            expect(sequence.toArray()).toEqual(expected);
            expect(sequence.measure).toBe(expected.reduce((sum, value) => sum + value, 0));
            sequence.validateStructure();
            expect(previous.toArray()).toEqual(previousExpected);
        }
    });

    it("answers every ordered weighted range measure without materializing a pushed tree", () => {
        const algebra = new WeightedAffineAlgebra();
        let expected = Array.from({ length: 31 }, (_, index) => index % 9 - 4);
        let sequence = RangeUpdateSequence.from(expected, algebra);
        const updates = [
            { index: 3, count: 21, tag: addTag(5) },
            { index: 0, count: 31, tag: scaleTag(2) },
            { index: 9, count: 8, tag: assignTag(-6) },
            { index: 14, count: 17, tag: { multiply: -1, add: 2 } },
        ] as const;

        for (const update of updates) {
            sequence = sequence.applyRange(update.index, update.count, update.tag);
            expected = expectedAfterTag(expected, update.index, update.count, update.tag);
        }

        expect(sequence.measure).toEqual(measureWeighted(expected));
        for (let index = 0; index <= expected.length; index++) {
            for (let count = 0; count <= expected.length - index; count++) {
                expect(sequence.measureRange(index, count)).toEqual(
                    measureWeighted(expected.slice(index, index + count)),
                );
            }
        }
        sequence.validateStructure(weightedEquals);
    });

    it("pushes old tags around point edits, splits, slices, and concatenation", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from(
            Array.from({ length: 24 }, (_, index) => index),
            algebra,
        ).applyRange(0, 24, addTag(100));

        const edited = source.insert(7, -1).setItem(18, -2).removeAt(4);
        const expected = source.toArray();
        expected.splice(7, 0, -1);
        expected[18] = -2;
        expected.splice(4, 1);
        expect(edited.toArray()).toEqual(expected);
        edited.validateStructure();

        const split = source.splitAt(9);
        expect(split.left.toArray()).toEqual(source.toArray().slice(0, 9));
        expect(split.right.toArray()).toEqual(source.toArray().slice(9));
        expect(split.left.concat(split.right).toArray()).toEqual(source.toArray());
        expect(source.getRange(5, 13).toArray()).toEqual(source.toArray().slice(5, 18));
    });

    it("combines independently tagged operands and preserves both inputs", () => {
        const algebra = new SumAffineAlgebra();
        const left = RangeUpdateSequence.from([1, 2, 3, 4], algebra).applyRange(1, 3, addTag(10));
        const right = RangeUpdateSequence.from([5, 6, 7, 8, 9], algebra)
            .applyRange(0, 5, assignTag(2))
            .applyRange(2, 2, scaleTag(4));
        const result = left.concat(right);

        expect(result.toArray()).toEqual([...left.toArray(), ...right.toArray()]);
        expect(result.measure).toBe([...result].reduce((sum, value) => sum + value, 0));
        result.validateStructure();
        expect(left.toArray()).toEqual([1, 12, 13, 14]);
        expect(right.toArray()).toEqual([2, 2, 8, 8, 2]);
    });

    it("canonicalizes composed semantic identity tags", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from([3, 1, 4, 1, 5, 9], algebra);
        const changed = source
            .applyRange(0, source.size, addTag(12))
            .applyRange(0, source.size, addTag(-12));

        expect(changed.toArray()).toEqual(source.toArray());
        expect(changed.measure).toBe(source.measure);
        const statistics = changed.validateStructure();
        expect(statistics.pendingTagNodeCount).toBe(0);
        expect(statistics.maximumPendingTagDepth).toBe(0);
    });
});
