import { describe, expect, it } from "vitest";

import { createHashPolicy, PersistentRelation } from "../../src/hamt/index.js";

interface Box { readonly id: number; readonly name: string }
const boxes = createHashPolicy<Box>((box) => box.id, (left, right) => left.id === right.id);

describe("PersistentRelation", () => {
    it("represents many-to-many adjacency in both directions", () => {
        const relation = PersistentRelation.from([["a", 1], ["a", 2], ["b", 2]]);
        expect(relation.pairCount).toBe(3);
        expect([...relation.getRights("a")].sort()).toEqual([1, 2]);
        expect([...relation.getLefts(2)].sort()).toEqual(["a", "b"]);
        expect(relation.validateStructure()).toBe(true);
    });

    it("retains one global representative per equivalence class", () => {
        const left1 = { id: 1, name: "left-1" };
        const left2 = { id: 2, name: "left-2" };
        const right = { id: 3, name: "right-first" };
        const relation = PersistentRelation.empty<Box, Box>(boxes, boxes)
            .add(left1, right)
            .add(left2, { id: 3, name: "right-later" });

        expect(relation.getRights(left2).get({ id: 3, name: "probe" })).toBe(right);
        expect(relation.getLefts(right).get({ id: 2, name: "probe" })).toBe(left2);
        expect(relation.validateStructure()).toBe(true);
    });

    it("caches an O(1) involutive inverse facade", () => {
        const relation = PersistentRelation.from([["a", 1], ["b", 1]]);
        expect(relation.inverse.inverse).toBe(relation);
        expect(relation.inverse.contains(1, "a")).toBe(true);
        expect(relation.inverse).toBe(relation.inverse);
    });

    it("removes pairs symmetrically and preserves retained branches", () => {
        const source = PersistentRelation.from([["a", 1], ["a", 2], ["b", 2]]);
        const branch = source.remove("a", 2);
        expect(branch.contains("a", 2)).toBe(false);
        expect(branch.getLefts(2).contains("b")).toBe(true);
        expect(source.contains("a", 2)).toBe(true);
        expect(branch.validateStructure()).toBe(true);
    });

    it("removes complete left and right adjacency groups", () => {
        const source = PersistentRelation.from([["a", 1], ["a", 2], ["b", 2], ["c", 3]]);
        const noA = source.removeLeft("a");
        const noTwo = source.removeRight(2);
        expect(noA.containsLeft("a")).toBe(false);
        expect(noA.getLefts(2).contains("b")).toBe(true);
        expect(noTwo.containsRight(2)).toBe(false);
        expect(noTwo.contains("a", 1)).toBe(true);
        expect(noA.validateStructure() && noTwo.validateStructure()).toBe(true);
    });

    it("returns the exact receiver for duplicate additions and removal misses", () => {
        const source = PersistentRelation.empty<string, number>().add("a", 1);
        expect(source.add("a", 1)).toBe(source);
        expect(source.remove("a", 2)).toBe(source);
        expect(source.removeLeft("missing")).toBe(source);
        expect(source.removeRight(9)).toBe(source);
        expect(source.clear().isEmpty).toBe(true);
    });
});
