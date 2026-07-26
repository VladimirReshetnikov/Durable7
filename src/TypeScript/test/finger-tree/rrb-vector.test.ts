/**
 * Tests for the relaxed radix-balanced vector, covering sequences that straddle the regular and
 * relaxed layout boundaries and agreement with an array reference model.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { RrbVector } from "../../src/finger-tree/index.js";

describe("RrbVector", () => {
    test("crosses regular and relaxed radix boundaries", () => {
        const basis = RrbVector.from(Array.from({ length: 5000 }, (_, index) => index));
        expect(basis.size).toBe(5000);
        expect(basis.get(0)).toBe(0);
        expect(basis.get(31)).toBe(31);
        expect(basis.get(32)).toBe(32);
        expect(basis.get(1024)).toBe(1024);
        expect(basis.back()).toBe(4999);
        expect(basis.validateStructure().count).toBe(5000);

        const split = basis.splitAt(1777)!;
        expect(split.left.size).toBe(1777);
        expect(split.right.front()).toBe(1777);
        expect(split.left.concat(split.right).toArray()).toEqual(basis.toArray());
        expect(basis.sharedLeafCount(split.left)).toBeGreaterThan(0);
        expect(split.left.concat(split.right).validateStructure().count).toBe(5000);
    });

    test("builder snapshots are cached and isolated", () => {
        const builder = RrbVector.builder<number>();
        builder.appendAll(Array.from({ length: 100 }, (_, index) => index));
        const first = builder.toImmutable();
        expect(builder.toImmutable()).toBe(first);
        builder.append(100);
        const second = builder.toImmutable();
        expect(first.size).toBe(100);
        expect(second.size).toBe(101);
        expect(second.sharedLeafCount(first)).toBeGreaterThan(0);
    });

    test("randomized edits agree with arrays and retain old versions", () => {
        fc.assert(fc.property(fc.array(fc.tuple(fc.nat(100), fc.integer(), fc.boolean()), { maxLength: 200 }), (operations) => {
            let vector = RrbVector.empty<number>();
            const model: number[] = [];
            const retained: Array<readonly [RrbVector<number>, number[]]> = [];
            for (const [rawIndex, value, remove] of operations) {
                if (remove && model.length !== 0) {
                    const index = rawIndex % model.length;
                    vector = vector.removeAt(index)!;
                    model.splice(index, 1);
                } else {
                    const index = rawIndex % (model.length + 1);
                    vector = vector.insertAt(index, value)!;
                    model.splice(index, 0, value);
                }
                if (rawIndex % 17 === 0) retained.push([vector, model.slice()]);
            }
            expect(vector.toArray()).toEqual(model);
            expect(vector.validateStructure().count).toBe(model.length);
            for (const [snapshot, expected] of retained) expect(snapshot.toArray()).toEqual(expected);
        }), { numRuns: 100 });
    });
});
