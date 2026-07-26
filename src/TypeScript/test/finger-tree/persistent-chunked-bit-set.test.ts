/**
 * Tests for the sparse chunked bit set: the nonnegative signed 32-bit index domain, no-op identity,
 * inclusive rank and zero-based select, and chunkwise set algebra.
 */
import { describe, expect, it } from "vitest";

import { PersistentChunkedBitSet } from "../../src/finger-tree/index.js";

describe("PersistentChunkedBitSet", () => {
    it("sorts, deduplicates, and crosses 64-bit word boundaries", () => {
        const set = PersistentChunkedBitSet.from([130, 0, 64, 63, 64, 129]);
        expect([...set]).toEqual([0, 63, 64, 129, 130]);
        expect(set.count).toBe(5);
        expect(set.chunkCount).toBe(3);
    });

    it("enforces the signed-32-bit point-update domain", () => {
        const largest = 0x7fff_ffff;
        const set = PersistentChunkedBitSet.empty().add(largest);
        expect(set.contains(largest)).toBe(true);
        expect(set.contains(-1)).toBe(false);
        expect(set.remove(-1)).toBe(set);
        expect(() => set.add(-1)).toThrow(RangeError);
        expect(() => set.add(largest + 1)).toThrow(RangeError);
    });

    it("returns exact receiver identities for point no-ops", () => {
        const set = PersistentChunkedBitSet.from([1, 65]);
        expect(set.add(1)).toBe(set);
        expect(set.remove(2)).toBe(set);
        expect(set.tryAdd(1)).toEqual({ changed: false, set });
        expect(set.tryRemove(2)).toEqual({ changed: false, set });
    });

    it("computes inclusive rank at and between represented words", () => {
        const set = PersistentChunkedBitSet.from([0, 2, 63, 64, 130]);
        expect([-1, 0, 1, 2, 63, 64, 129, 130].map((index) => set.rank(index)))
            .toEqual([0, 1, 1, 2, 3, 4, 4, 5]);
    });

    it("selects by zero-based population rank", () => {
        const set = PersistentChunkedBitSet.from([1, 64, 66, 200]);
        expect([0, 1, 2, 3].map((rank) => set.select(rank))).toEqual([1, 64, 66, 200]);
        expect(set.trySelect(4)).toBeUndefined();
        expect(() => set.select(-1)).toThrow(RangeError);
    });

    it("implements all four persistent set algebra operations", () => {
        const left = PersistentChunkedBitSet.from([1, 2, 64, 130]);
        const right = PersistentChunkedBitSet.from([2, 3, 64, 200]);
        expect([...left.union(right)]).toEqual([1, 2, 3, 64, 130, 200]);
        expect([...left.intersect(right)]).toEqual([2, 64]);
        expect([...left.except(right)]).toEqual([1, 130]);
        expect([...left.symmetricExcept(right)]).toEqual([1, 3, 130, 200]);
        expect(left.union(PersistentChunkedBitSet.empty())).toBe(left);
    });

    it("contracts empty words, preserves snapshots, and validates annotations", () => {
        const source = PersistentChunkedBitSet.from([63, 64]);
        const branch = source.remove(63);
        expect(source.contains(63)).toBe(true);
        expect(branch.chunkCount).toBe(1);
        expect(source.validateStructure()).toEqual({ chunkCount: 2, popCount: 2 });
        expect(branch.validateStructure()).toEqual({ chunkCount: 1, popCount: 1 });
    });
});
