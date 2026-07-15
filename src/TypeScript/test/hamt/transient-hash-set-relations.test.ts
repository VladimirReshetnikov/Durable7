import fc from "fast-check";
import { describe, expect, test } from "vitest";
import {
    PersistentHashSet,
    TransientConsumedError,
    createHashPolicy,
} from "../../src/hamt/index.js";

describe("TransientHashSet relations", () => {
    test("exposes all six receiver-policy read-only relations", () => {
        const policy = createHashPolicy<number>(
            (value) => Math.abs(value) % 7,
            (left, right) => Math.abs(left) === Math.abs(right),
        );
        const transient = PersistentHashSet.from([1, 2, 3], policy).toTransient();
        expect(transient.isSubsetOf([-1, -2, -3, 4])).toBe(true);
        expect(transient.isProperSubsetOf([-1, -2, -3, 4])).toBe(true);
        expect(transient.isSupersetOf([-1, -2])).toBe(true);
        expect(transient.isProperSupersetOf([-1, -2])).toBe(true);
        expect(transient.overlaps([99, -3])).toBe(true);
        expect(transient.overlaps([98, 99])).toBe(false);
        expect(transient.setEquals([-3, -2, -1, 1])).toBe(true);
    });

    test("relations do not advance the mutation version and consumed sessions fail eagerly", () => {
        const transient = PersistentHashSet.from([1, 2, 3]).toTransient();
        const iterator = transient[Symbol.iterator]();
        expect(iterator.next().done).toBe(false);
        expect(transient.isSubsetOf([1, 2, 3, 4])).toBe(true);
        expect(transient.isSupersetOf([1])).toBe(true);
        expect(() => iterator.next()).not.toThrow();
        Array.from(iterator);

        transient.persist();
        let enumerated = false;
        const values = (function* (): IterableIterator<number> {
            enumerated = true;
            yield 1;
        })();
        const operations = [
            () => transient.isSubsetOf(values),
            () => transient.isProperSubsetOf(values),
            () => transient.isSupersetOf(values),
            () => transient.isProperSupersetOf(values),
            () => transient.overlaps(values),
            () => transient.setEquals(values),
        ];
        for (const operation of operations) expect(operation).toThrow(TransientConsumedError);
        expect(enumerated).toBe(false);
    });

    test("generated relations agree with JavaScript Set models", () => {
        fc.assert(fc.property(
            fc.uniqueArray(fc.integer({ min: -50, max: 50 }), { maxLength: 40 }),
            fc.uniqueArray(fc.integer({ min: -50, max: 50 }), { maxLength: 40 }),
            (leftValues, rightValues) => {
                const transient = PersistentHashSet.from(leftValues).toTransient();
                const left = new Set(leftValues);
                const right = new Set(rightValues);
                const subset = Array.from(left).every((value) => right.has(value));
                const superset = Array.from(right).every((value) => left.has(value));
                const overlaps = Array.from(left).some((value) => right.has(value));
                expect(transient.isSubsetOf(rightValues)).toBe(subset);
                expect(transient.isProperSubsetOf(rightValues)).toBe(subset && left.size < right.size);
                expect(transient.isSupersetOf(rightValues)).toBe(superset);
                expect(transient.isProperSupersetOf(rightValues)).toBe(superset && left.size > right.size);
                expect(transient.overlaps(rightValues)).toBe(overlaps);
                expect(transient.setEquals(rightValues)).toBe(subset && left.size === right.size);
            },
        ), { numRuns: 120 });
    });
});
