import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { BrodalOkasakiHeap } from "../../src/finger-tree/index.js";

describe("BrodalOkasakiHeap", () => {
    test("drains adversarial inputs in sorted order", () => {
        let heap = BrodalOkasakiHeap.from(Array.from({ length: 4096 }, (_, index) => 4095 - index));
        const output: number[] = [];
        while (!heap.isEmpty) { output.push(heap.minimum); heap = heap.deleteMinimum(); }
        expect(output).toEqual(Array.from({ length: 4096 }, (_, index) => index));
    });

    test("meld uses comparator identity and retains prior versions", () => {
        const comparator = (left: number, right: number): number => left - right;
        const left = BrodalOkasakiHeap.from([1, 3, 5], comparator);
        const right = BrodalOkasakiHeap.from([2, 4, 6], comparator);
        const merged = left.meld(right);
        expect(left.count).toBe(3);
        expect(right.count).toBe(3);
        expect(merged.count).toBe(6);
        expect(merged.minimum).toBe(1);
        expect(merged.sharedTreeCount(left)).toBeGreaterThan(0);
        expect(() => left.meld(BrodalOkasakiHeap.from([0], (a, b) => a - b))).toThrow(TypeError);
    });

    test("random histories agree with sorted multiset models", () => {
        fc.assert(fc.property(fc.array(fc.integer(), { maxLength: 500 }), (values) => {
            let heap = BrodalOkasakiHeap.from(values);
            expect(heap.validateStructure().count).toBe(values.length);
            const output: number[] = [];
            while (!heap.isEmpty) { const view = heap.minimumView()!; output.push(view.minimum); heap = view.remainder; }
            expect(output).toEqual(values.toSorted((a, b) => a - b));
        }), { numRuns: 100 });
    });
});
