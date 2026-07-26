/**
 * Tests for the history-independent canonical sorted set. The defining property under test is
 * convergence: sets built by different edit histories must reach one identical shape.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { CanonicalSortedSet, ZipTreeRankPolicy } from "../../src/finger-tree/index.js";

describe("CanonicalSortedSet", () => {
    test("same policy and contents converge across permutations", () => {
        const policy = ZipTreeRankPolicy.create<number>({ seed: 0x1234_5678n });
        const values = Array.from({ length: 500 }, (_, index) => index - 250);
        const ascending = CanonicalSortedSet.from(values, policy);
        const descending = CanonicalSortedSet.from(values.toReversed(), policy);
        expect(descending.setEquals(ascending)).toBe(true);
        expect(descending.contentHash).toBe(ascending.contentHash);
        expect(descending.height).toBe(ascending.height);
        expect(ascending.validateStructure().count).toBe(500);
    });

    test("updates preserve representatives, sharing, and canonical topology", () => {
        interface Item { readonly key: number; readonly identity: number }
        const comparator = (left: Item, right: Item): number => left.key - right.key;
        const policy = ZipTreeRankPolicy.create<Item>({ comparator, rankHash: (value) => value.key, seed: 42 });
        const first: Item = { key: 1, identity: 1 };
        const duplicate: Item = { key: 1, identity: 2 };
        const basis = CanonicalSortedSet.from([{ key: 0, identity: 0 }, first, { key: 2, identity: 2 }], policy);
        expect(basis.add(duplicate)).toBe(basis);
        expect(basis.tryGetValue(duplicate)).toEqual({ found: true, value: first });
        const edited = basis.add({ key: 10, identity: 10 });
        expect(edited.sharesStorageWith(basis)).toBe(true);
        expect(edited.remove({ key: 10, identity: 99 }).setEquals(basis)).toBe(true);
    });

    test("seed and keyed rank derivations are reproducible", () => {
        const first = ZipTreeRankPolicy.create<number>({ seed: -1n });
        const second = ZipTreeRankPolicy.create<number>({ seed: -1n });
        expect(first.rank(123)).toEqual(second.rank(123));
        const key = Uint8Array.from({ length: 32 }, (_, index) => index);
        expect(ZipTreeRankPolicy.createKeyed<number>(key).rank(7)).toEqual(ZipTreeRankPolicy.createKeyed<number>(key).rank(7));
    });

    test("random histories agree with native Set models", () => {
        const policy = ZipTreeRankPolicy.create<number>({ seed: 99n });
        fc.assert(fc.property(fc.array(fc.tuple(fc.integer({ min: -100, max: 100 }), fc.boolean()), { maxLength: 500 }), (commands) => {
            let actual = CanonicalSortedSet.empty(policy);
            const expected = new Set<number>();
            for (const [value, remove] of commands) {
                if (remove) { actual = actual.remove(value); expected.delete(value); }
                else { actual = actual.add(value); expected.add(value); }
            }
            expect(Array.from(actual)).toEqual(Array.from(expected).sort((a, b) => a - b));
            expect(actual.validateStructure().count).toBe(expected.size);
        }), { numRuns: 100 });
    });
});
