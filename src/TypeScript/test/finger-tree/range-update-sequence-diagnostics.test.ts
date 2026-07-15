import { describe, expect, it } from "vitest";
import * as FingerTree from "../../src/finger-tree/index.js";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import {
    beginRangeUpdateObservation,
    observeRangeUpdateOperation,
} from "../../src/finger-tree/range-update-sequence-internals.js";
import { addTag, SumAffineAlgebra } from "./range-update-test-support.js";

describe("RangeUpdateSequence diagnostics", () => {
    it("reports exact whole-range and no-op operation costs", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from(
            Array.from({ length: 127 }, (_, index) => index),
            algebra,
        );

        const whole = observeRangeUpdateOperation(() =>
            source.applyRange(0, source.size, addTag(5)));
        expect(whole.statistics).toMatchObject({
            nodeVisits: 1,
            nodeAllocations: 1,
            facadeAllocations: 1,
            rotations: 0,
            pushes: 0,
            subtreeApplications: 1,
            identityTestCallbacks: 1,
            tagComposeCallbacks: 0,
            elementApplyCallbacks: 1,
            measureApplyCallbacks: 1,
            elementMeasureCallbacks: 0,
            measureCombineCallbacks: 0,
        });
        expect(whole.result.toArray()).toEqual(source.toArray().map(value => value + 5));

        const empty = observeRangeUpdateOperation(() => source.applyRange(41, 0, addTag(5)));
        expect(empty.result).toBe(source);
        expect(empty.statistics.policyCallbacks).toBe(0);
        expect(empty.statistics.nodeAllocations).toBe(0);
        expect(empty.statistics.facadeAllocations).toBe(0);

        const identity = observeRangeUpdateOperation(() =>
            source.applyRange(0, source.size, { multiply: 1, add: 0 }));
        expect(identity.result).toBe(source);
        expect(identity.statistics.identityTestCallbacks).toBe(1);
        expect(identity.statistics.policyCallbacks).toBe(1);
        expect(identity.statistics.nodeAllocations).toBe(0);
        expect(identity.statistics.facadeAllocations).toBe(0);
    });

    it("erases a composed identity with one root wrapper and visible policy calls", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from([1, 2, 3, 4, 5], algebra)
            .applyRange(0, 5, addTag(10));
        const observed = observeRangeUpdateOperation(() =>
            source.applyRange(0, source.size, addTag(-10)));

        expect(observed.result.toArray()).toEqual([1, 2, 3, 4, 5]);
        expect(observed.statistics).toMatchObject({
            nodeVisits: 1,
            nodeAllocations: 1,
            facadeAllocations: 1,
            pushes: 0,
            subtreeApplications: 1,
            identityTestCallbacks: 2,
            tagComposeCallbacks: 1,
            elementApplyCallbacks: 1,
            measureApplyCallbacks: 1,
        });
        expect(observed.result.validateStructure().pendingTagNodeCount).toBe(0);
    });

    it("allocates no persistent nodes or facades for reads and iteration", () => {
        const algebra = new SumAffineAlgebra();
        const sequence = RangeUpdateSequence.from(
            Array.from({ length: 255 }, (_, index) => index),
            algebra,
        )
            .applyRange(32, 160, addTag(7))
            .applyRange(0, 255, addTag(3));

        const indexed = observeRangeUpdateOperation(() => sequence.get(100));
        expect(indexed.result).toBe(110);
        expect(indexed.statistics.nodeAllocations).toBe(0);
        expect(indexed.statistics.facadeAllocations).toBe(0);
        expect(indexed.statistics.nodeVisits).toBeLessThanOrEqual(sequence.validateStructure().height);

        const measured = observeRangeUpdateOperation(() => sequence.measureRange(47, 121));
        expect(measured.result).toBe(sequence.toArray().slice(47, 168)
            .reduce((sum, value) => sum + value, 0));
        expect(measured.statistics.nodeAllocations).toBe(0);
        expect(measured.statistics.facadeAllocations).toBe(0);

        const enumerated = observeRangeUpdateOperation(() => Array.from(sequence));
        expect(enumerated.result).toEqual(sequence.toArray());
        expect(enumerated.statistics.nodeAllocations).toBe(0);
        expect(enumerated.statistics.facadeAllocations).toBe(0);

        const wholeMeasure = observeRangeUpdateOperation(() => sequence.measureRange(0, sequence.size));
        expect(wholeMeasure.result).toBe(sequence.measure);
        expect(wholeMeasure.statistics.policyCallbacks).toBe(0);
        expect(wholeMeasure.statistics.nodeVisits).toBe(0);
    });

    it("splits a pending root at endpoints without pushing or allocating", () => {
        const algebra = new SumAffineAlgebra();
        const sequence = RangeUpdateSequence.from([1, 2, 3, 4, 5], algebra)
            .applyRange(0, 5, addTag(10));

        const atStart = observeRangeUpdateOperation(() => sequence.splitAt(0));
        expect(atStart.result.left).toBe(RangeUpdateSequence.empty(algebra));
        expect(atStart.result.right).toBe(sequence);
        expect(atStart.statistics.policyCallbacks).toBe(0);
        expect(atStart.statistics.nodeAllocations).toBe(0);
        expect(atStart.statistics.facadeAllocations).toBe(0);

        const atEnd = observeRangeUpdateOperation(() => sequence.splitAt(sequence.size));
        expect(atEnd.result.left).toBe(sequence);
        expect(atEnd.result.right).toBe(RangeUpdateSequence.empty(algebra));
        expect(atEnd.statistics.policyCallbacks).toBe(0);
        expect(atEnd.statistics.nodeAllocations).toBe(0);
        expect(atEnd.statistics.facadeAllocations).toBe(0);
    });

    it("keeps proper range updates height-scaled", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from(
            Array.from({ length: 4_095 }, (_, index) => index),
            algebra,
        );
        const height = source.validateStructure().height;
        const observed = observeRangeUpdateOperation(() => source.applyRange(511, 2_801, addTag(9)));

        expect(observed.statistics.nodeVisits).toBeLessThanOrEqual(height * 20);
        expect(observed.statistics.nodeAllocations).toBeLessThanOrEqual(height * 40);
        expect(observed.statistics.facadeAllocations).toBe(1);
        expect(observed.statistics.subtreeApplications).toBeLessThanOrEqual(height * 6);
        observed.result.validateStructure();
    });

    it("retains extensive structure across point and whole-range versions", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from(
            Array.from({ length: 255 }, (_, index) => index),
            algebra,
        );
        const height = source.validateStructure().height;
        const point = source.setItem(127, -1);
        const whole = source.applyRange(0, source.size, addTag(1));

        expect(source.sharedNodeCount(point)).toBeGreaterThanOrEqual(source.size - height - 1);
        expect(source.sharedNodeCount(whole)).toBe(source.size - 1);
        expect(source.sharedNodeCount(source)).toBe(source.size);
        expect(RangeUpdateSequence.empty(algebra).sharedNodeCount(source)).toBe(0);

        const otherAlgebra = new SumAffineAlgebra();
        expect(() => source.sharedNodeCount(RangeUpdateSequence.from([1], otherAlgebra))).toThrow(TypeError);
    });

    it("maintains AVL and pending-depth bounds across adversarial edits", () => {
        const algebra = new SumAffineAlgebra();
        let sequence = RangeUpdateSequence.empty(algebra);
        for (let index = 0; index < 512; index++) sequence = sequence.append(index);
        sequence = sequence.applyRange(0, sequence.size, addTag(1));
        for (let index = 0; index < 160; index++) {
            const start = (index * 37) % (sequence.size - 3);
            sequence = sequence.applyRange(start, 3, addTag(index % 5 - 2));
            if (index % 3 === 0) sequence = sequence.insert(start, -index);
            if (index % 5 === 0) sequence = sequence.removeAt(sequence.size - 1 - start % sequence.size);
        }

        const statistics = sequence.validateStructure();
        expect(statistics.maximumAbsoluteBalanceFactor).toBeLessThanOrEqual(1);
        expect(statistics.height).toBeLessThanOrEqual(2 * Math.ceil(Math.log2(sequence.size + 1)));
        expect(statistics.maximumPendingTagDepth).toBeLessThanOrEqual(statistics.height);
    });

    it("supports nested synchronous observation scopes and keeps internals off the package index", () => {
        const algebra = new SumAffineAlgebra();
        const sequence = RangeUpdateSequence.from([1, 2, 3], algebra);
        const outer = beginRangeUpdateObservation();
        try {
            const inner = observeRangeUpdateOperation(() => sequence.get(1));
            expect(inner.result).toBe(2);
            expect(inner.statistics.nodeVisits).toBeGreaterThan(0);
            expect(outer.statistics.nodeVisits).toBe(0);
        } finally {
            outer.dispose();
        }

        expect("beginRangeUpdateObservation" in FingerTree).toBe(false);
        expect("observeRangeUpdateOperation" in FingerTree).toBe(false);
    });
});
