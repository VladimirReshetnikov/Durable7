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

describe("Patricia cursors", () => {
    test("32-bit map factories expose every ordered gap and distinguish stored undefined", () => {
        const keys = [-0x8000_0000, -1, 0, 17, 0x7fff_ffff];
        const map = PersistentIntMap.from<string | undefined>(
            keys.map((key) => [key, key === 0 ? undefined : String(key)] as const),
        );

        for (let position = 0; position <= keys.length; position++) {
            const cursor = map.cursor(position);
            expect(cursor.position).toBe(position);
            expect(cursor.size).toBe(keys.length);
            expect(cursor.isAtStart).toBe(position === 0);
            expect(cursor.isAtEnd).toBe(position === keys.length);
            expect(cursor.snapshot()).toBe(map);
            expect(cursor.peekPrevious()?.[0]).toBe(keys[position - 1]);
            expect(cursor.peekNext()?.[0]).toBe(keys[position]);
        }

        expect(map.lowerBoundCursor(-2).position).toBe(1);
        expect(map.upperBoundCursor(-1).position).toBe(2);
        expect(map.lowerBoundCursor(18).position).toBe(4);
        expect(map.upperBoundCursor(0x7fff_ffff).position).toBe(keys.length);
        const exact = map.cursorAtKey(0);
        expect(exact.found).toBe(true);
        const entry = exact.cursor.peekNext();
        expect(entry).toBeDefined();
        expect(entry?.[0]).toBe(0);
        expect(entry?.[1]).toBeUndefined();
        const miss = map.cursorAtKey(1);
        expect(miss.found).toBe(false);
        expect(miss.cursor.position).toBe(3);
        expect(miss.cursor.peekNext()?.[0]).toBe(17);

        expect(map.cursorAtEnd().snapshot()).toBe(map);
        expect(() => map.cursor(-1)).toThrow(RangeError);
        expect(() => map.cursor(map.size + 1)).toThrow(RangeError);
        expect(() => map.cursor().movePrevious()).toThrow(RangeError);
        expect(() => map.cursorAtEnd().moveNext()).toThrow(RangeError);
    });

    test("map edits preserve gap continuity, identity no-ops, and retained branches", () => {
        const source = PersistentIntMap.from<number | undefined>([[-10, 1], [0, undefined], [10, 3]]);
        const atZero = source.cursorAtKey(0);
        expect(atZero.found).toBe(true);
        expect(atZero.cursor.setNextValue(undefined).snapshot()).toBe(source);

        const updated = atZero.cursor.setNextValue(2);
        expect(updated.position).toBe(1);
        expect(updated.snapshot().get(0)).toBe(2);
        expect(source.get(0)).toBeUndefined();
        expect(Array.from(atZero.cursor.deleteNext().snapshot(), ([key]) => key)).toEqual([-10, 10]);
        expect(Array.from(atZero.cursor.deletePrevious().snapshot(), ([key]) => key)).toEqual([0, 10]);

        const inserted = source.cursorAtKey(5).cursor.insert(5, 5);
        expect(inserted.position).toBe(3);
        expect(Array.from(inserted.snapshot(), ([key]) => key)).toEqual([-10, 0, 5, 10]);
        expect(Array.from(source, ([key]) => key)).toEqual([-10, 0, 10]);
        expect(source.lowerBoundCursor(-5).put(-5, -5).position).toBe(2);
        expect(atZero.cursor.put(0, 20).position).toBe(1);

        expect(() => atZero.cursor.insert(0, 4)).toThrow("already present");
        expect(() => source.cursor().insert(5, 5)).toThrow("belongs at gap");
        expect(() => source.cursorAtEnd().setNextValue(4)).toThrow(RangeError);
        expect(() => source.cursor().deletePrevious()).toThrow(RangeError);
        expect(() => source.cursorAtEnd().deleteNext()).toThrow(RangeError);
    });

    test("64-bit maps and both set widths provide equivalent cursor edits", () => {
        const min = -(1n << 63n);
        const max = (1n << 63n) - 1n;
        const longMap = PersistentLongMap.from([[min, min], [-1n, -1n], [0n, 0n], [1n << 40n, 1n], [max, max]]);
        expect(longMap.lowerBoundCursor(min).position).toBe(0);
        expect(longMap.upperBoundCursor(min).position).toBe(1);
        expect(longMap.lowerBoundCursor(1n).position).toBe(3);
        expect(longMap.upperBoundCursor(max).position).toBe(5);
        const longExact = longMap.cursorAtKey(1n << 40n);
        expect(longExact.found).toBe(true);
        expect(longExact.cursor.setNextValue(42n).snapshot().get(1n << 40n)).toBe(42n);
        const longInserted = longMap.cursorAtKey(-2n).cursor.insert(-2n, 99n);
        expect(longInserted.position).toBe(2);
        expect(Array.from(longInserted.snapshot(), ([key]) => key)).toEqual([min, -2n, -1n, 0n, 1n << 40n, max]);

        const intSet = PersistentIntSet.from([-0x8000_0000, -1, 0, 0x7fff_ffff]);
        const intMiss = intSet.cursorAtItem(-2);
        expect(intMiss.found).toBe(false);
        const intAdded = intMiss.cursor.add(-2);
        expect(intAdded.position).toBe(2);
        expect(Array.from(intAdded.snapshot())).toEqual([-0x8000_0000, -2, -1, 0, 0x7fff_ffff]);
        expect(intSet.cursorAtItem(0).cursor.add(0).snapshot()).toBe(intSet);
        expect(Array.from(intSet.cursorAtItem(0).cursor.deleteNext().snapshot())).toEqual([-0x8000_0000, -1, 0x7fff_ffff]);

        const longSet = PersistentLongSet.from([min, -1n, 0n, max]);
        expect(longSet.upperBoundCursor(min).position).toBe(1);
        const longAdded = longSet.cursorAtItem(1n).cursor.add(1n);
        expect(longAdded.position).toBe(4);
        expect(Array.from(longAdded.snapshot())).toEqual([min, -1n, 0n, 1n, max]);
        expect(() => longSet.cursor().add(1n)).toThrow("belongs at gap");
    });

    test("random lower, upper, and exact ranks agree with a sorted model", () => {
        fc.assert(fc.property(
            fc.uniqueArray(fc.integer({ min: -500, max: 500 }), { maxLength: 100 }),
            fc.integer({ min: -550, max: 550 }),
            (generated, probe) => {
                const keys = generated.toSorted((left, right) => left - right);
                const map = PersistentIntMap.from(keys.map((key) => [key, key] as const));
                const lower = keys.findIndex((key) => key >= probe);
                const upper = keys.findIndex((key) => key > probe);
                const expectedLower = lower < 0 ? keys.length : lower;
                const expectedUpper = upper < 0 ? keys.length : upper;
                expect(map.lowerBoundCursor(probe).position).toBe(expectedLower);
                expect(map.upperBoundCursor(probe).position).toBe(expectedUpper);
                const exact = map.cursorAtKey(probe);
                expect(exact.cursor.position).toBe(expectedLower);
                expect(exact.found).toBe(keys.includes(probe));
            },
        ), { numRuns: 300 });
    });
});
