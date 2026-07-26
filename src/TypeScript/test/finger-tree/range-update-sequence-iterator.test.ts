/**
 * Tests that iterating a range-update sequence observes the same values as direct access, so
 * pending tags are pushed down correctly along the traversal.
 */
import { describe, expect, it } from "vitest";
import type { RangeUpdateAlgebra } from "../../src/finger-tree/range-update-algebra.js";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import { addTag, assignTag, SumAffineAlgebra } from "./range-update-test-support.js";

class OptionalIdentityAlgebra
implements RangeUpdateAlgebra<string | undefined, number, false> {
    public readonly identity = 0;
    public readonly identityTag = false;
    public combine(left: number, right: number): number { return left + right; }
    public measure(_element: string | undefined): number { return 1; }
    public isIdentity(_tag: false): boolean { return true; }
    public compose(_newer: false, _older: false): false { return false; }
    public applyElement(_tag: false, element: string | undefined): string | undefined { return element; }
    public applyMeasure(_tag: false, measure: number, _count: number): number { return measure; }
}

describe("RangeUpdateSequence iteration", () => {
    it("enumerates logical values under nested lazy tags in index order", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from(
            Array.from({ length: 20 }, (_, index) => index),
            algebra,
        )
            .applyRange(3, 12, assignTag(7))
            .applyRange(0, 20, addTag(5))
            .applyRange(8, 9, addTag(-2));

        const indexed = Array.from({ length: source.size }, (_, index) => source.get(index));
        expect(Array.from(source)).toEqual(indexed);
        expect([...source]).toEqual(indexed);
    });

    it("creates independent iterators that can be advanced in any interleaving", () => {
        const algebra = new SumAffineAlgebra();
        const sequence = RangeUpdateSequence.from([10, 20, 30, 40], algebra)
            .applyRange(0, 4, addTag(1));
        const first = sequence[Symbol.iterator]();
        const second = sequence[Symbol.iterator]();

        expect(first.next()).toEqual({ value: 11, done: false });
        expect(first.next()).toEqual({ value: 21, done: false });
        expect(second.next()).toEqual({ value: 11, done: false });
        expect(first.next()).toEqual({ value: 31, done: false });
        expect(second.next()).toEqual({ value: 21, done: false });
        expect(Array.from(first)).toEqual([41]);
        expect(Array.from(second)).toEqual([31, 41]);
    });

    it("binds an iterator to its immutable source version", () => {
        const algebra = new SumAffineAlgebra();
        const original = RangeUpdateSequence.from([1, 2, 3, 4], algebra);
        const iterator = original[Symbol.iterator]();
        expect(iterator.next()).toEqual({ value: 1, done: false });

        const changed = original.applyRange(0, 4, addTag(100)).append(5);
        expect(Array.from(iterator)).toEqual([2, 3, 4]);
        expect(Array.from(changed)).toEqual([101, 102, 103, 104, 5]);
        expect(Array.from(original)).toEqual([1, 2, 3, 4]);
    });

    it("retains stored undefined values and has ordinary iterator exhaustion semantics", () => {
        const algebra = new OptionalIdentityAlgebra();
        const sequence = RangeUpdateSequence.from<string | undefined, number, false>(
            [undefined, "a", undefined, "b"],
            algebra,
        );
        const iterator = sequence[Symbol.iterator]();

        expect(iterator.next()).toEqual({ value: undefined, done: false });
        expect(iterator.next()).toEqual({ value: "a", done: false });
        expect(iterator.next()).toEqual({ value: undefined, done: false });
        expect(iterator.next()).toEqual({ value: "b", done: false });
        expect(iterator.next()).toEqual({ value: undefined, done: true });
        expect(iterator.next()).toEqual({ value: undefined, done: true });
        expect(sequence.measure).toBe(4);
    });

    it("enumerates the empty canonical sequence without policy calls", () => {
        const algebra = new OptionalIdentityAlgebra();
        const empty = RangeUpdateSequence.empty(algebra);
        expect(Array.from(empty)).toEqual([]);
        const iterator = empty[Symbol.iterator]();
        expect(iterator.next()).toEqual({ value: undefined, done: true });
    });
});
