import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    PersistentIntMap,
    PersistentIntSet,
    PersistentLongMap,
    PersistentLongSet,
} from "../../src/hamt/index.js";

describe("Patricia maps", () => {
    test("32-bit and 64-bit keys enumerate in signed order", () => {
        const intKeys = [-0x8000_0000, -1, 0, 1, 0x7fff_ffff];
        const intMap = PersistentIntMap.from(intKeys.toReversed().map((key) => [key, String(key)] as const));
        expect(Array.from(intMap, ([key]) => key)).toEqual(intKeys);

        const longKeys = [-(1n << 63n), -1n, 0n, 1n, (1n << 63n) - 1n];
        const longMap = PersistentLongMap.from(longKeys.toReversed().map((key) => [key, String(key)] as const));
        expect(Array.from(longMap, ([key]) => key)).toEqual(longKeys);
    });

    test("algebra supports combining and identity no-ops", () => {
        const left = PersistentIntMap.from([[-4, 40], [2, 20], [7, 70]]);
        const right = PersistentIntMap.from([[2, 3], [7, 5], [9, 90]]);
        const combine = (key: number, a: number, b: number): number => key + a * 100 + b;
        expect(Array.from(left.union(right, combine))).toEqual([[-4, 40], [2, 2005], [7, 7012], [9, 90]]);
        expect(Array.from(left.intersect(right, combine))).toEqual([[2, 2005], [7, 7012]]);
        expect(left.put(2, 20)).toBe(left);
        expect(left.remove(1000)).toBe(left);
    });

    test("random histories agree with sorted models", () => {
        fc.assert(fc.property(
            fc.array(fc.tuple(fc.integer(), fc.integer(), fc.boolean()), { maxLength: 300 }),
            (operations) => {
                let actual = PersistentIntMap.empty<number>();
                const expected = new Map<number, number>();
                for (const [key, value, remove] of operations) {
                    if (remove) { actual = actual.remove(key); expected.delete(key); }
                    else { actual = actual.put(key, value); expected.set(key, value); }
                }
                expect(Array.from(actual)).toEqual(Array.from(expected).sort((a, b) => a[0] - b[0]));
            },
        ), { numRuns: 150 });
    });
});

describe("Patricia sets", () => {
    test("support structural algebra", () => {
        const left = PersistentIntSet.from([-3, -1, 1, 3]);
        const right = PersistentIntSet.from([-1, 0, 1]);
        expect(Array.from(left.union(right))).toEqual([-3, -1, 0, 1, 3]);
        expect(Array.from(left.intersect(right))).toEqual([-1, 1]);
        expect(Array.from(left.except(right))).toEqual([-3, 3]);

        const long = PersistentLongSet.from([-1n, 0n, 1n]);
        expect(Array.from(long.remove(0n))).toEqual([-1n, 1n]);
    });
});
