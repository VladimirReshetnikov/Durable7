import { describe, expect, it } from "vitest";

import { createHashPolicy, PersistentOrderedMultimap } from "../../src/index.js";

interface Box { readonly id: number; readonly name: string }
const boxes = createHashPolicy<Box>((box) => box.id, (left, right) => left.id === right.id);

describe("PersistentOrderedMultimap", () => {
    it("enumerates in grouped key and value insertion order", () => {
        const map = PersistentOrderedMultimap.from([
            ["a", 1], ["b", 2], ["a", 3], ["b", 4],
        ]);
        expect([...map]).toEqual([
            { key: "a", value: 1 }, { key: "a", value: 3 },
            { key: "b", value: 2 }, { key: "b", value: 4 },
        ]);
        expect(map.keyCount).toBe(2);
        expect(map.pairCount).toBe(4);
    });

    it("retains first representatives in both independent domains", () => {
        const key = { id: 1, name: "key-first" };
        const value = { id: 2, name: "value-first" };
        const map = PersistentOrderedMultimap.empty<Box, Box>(boxes, boxes)
            .add(key, value)
            .add({ id: 1, name: "key-later" }, { id: 3, name: "other" })
            .add({ id: 1, name: "key-later" }, { id: 2, name: "value-later" });
        expect(map.getStoredKey({ id: 1, name: "probe" })).toBe(key);
        expect(map.tryGetValue(key, { id: 2, name: "probe" })).toEqual({ found: true, value });
        expect(map.keyPolicy).toBe(boxes);
        expect(map.valuePolicy).toBe(boxes);
    });

    it("returns the exact receiver for duplicate additions and removal misses", () => {
        const source = PersistentOrderedMultimap.empty<string, number>().add("a", 1);
        expect(source.add("a", 1)).toBe(source);
        expect(source.tryAdd("a", 1)).toEqual({ added: false, map: source });
        expect(source.remove("a", 2)).toBe(source);
        expect(source.removeKey("missing")).toBe(source);
        expect(source.sharesGroupsRootsWith(source)).toBe(true);
    });

    it("contracts empty groups and appends a reintroduced group", () => {
        const source = PersistentOrderedMultimap.from([["a", 1], ["b", 2]]);
        const removed = source.remove("a", 1);
        const readded = removed.add("a", 3);
        expect([...removed.keys()]).toEqual(["b"]);
        expect([...readded.keys()]).toEqual(["b", "a"]);
        expect(source.contains("a", 1)).toBe(true);
    });

    it("removes complete groups and returns policy-bound empty groups", () => {
        const map = PersistentOrderedMultimap.empty<Box, Box>(boxes, boxes)
            .add({ id: 1, name: "a" }, { id: 2, name: "x" })
            .add({ id: 1, name: "a2" }, { id: 3, name: "y" });
        expect(map.removeKey({ id: 1, name: "probe" }).isEmpty).toBe(true);
        expect(map.getValues({ id: 9, name: "missing" }).policy).toBe(boxes);
    });

    it("validates nested indexes and preserves retained branches", () => {
        const source = PersistentOrderedMultimap.from([["a", 1], ["a", 2], ["b", 3]]);
        const branch = source.remove("a", 1);
        expect(source.contains("a", 1)).toBe(true);
        expect(branch.contains("a", 1)).toBe(false);
        expect(source.validateStructure()).toEqual({ keyCount: 2, pairCount: 3 });
        expect(branch.validateStructure()).toEqual({ keyCount: 2, pairCount: 2 });
    });
});
