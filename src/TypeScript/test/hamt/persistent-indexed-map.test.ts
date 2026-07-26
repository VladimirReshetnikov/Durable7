/**
 * Tests that the secondary index cannot drift from the primary map, counting index-selector
 * invocations across adds, no-op writes, changes, and removals.
 */
import { describe, expect, it } from "vitest";

import { createHashPolicy, DuplicateKeyError, PersistentIndexedMap } from "../../src/hamt/index.js";

interface Key { readonly id: number; readonly name: string }
const keys = createHashPolicy<Key>((key) => key.id, (left, right) => left.id === right.id);

describe("PersistentIndexedMap", () => {
    it("populates nonunique secondary groups", () => {
        const map = PersistentIndexedMap.from(
            [["a", 1], ["b", 3], ["c", 2]],
            (_key, value) => value % 2,
        );
        expect(map.getKeys(1).setEquals(["a", "b"])).toBe(true);
        expect(map.getKeys(0).setEquals(["c"])).toBe(true);
        expect(map.validateStructure()).toEqual({ count: 3, indexKeyCount: 2 });
    });

    it("does not invoke the selector for duplicate adds or equal updates", () => {
        let calls = 0;
        const selector = (_key: string, value: number): number => { calls++; return value % 2; };
        const source = PersistentIndexedMap.empty(selector).add("a", 1);
        expect(() => source.add("a", 9)).toThrow(DuplicateKeyError);
        expect(source.tryAdd("a", 9)).toEqual({ added: false, map: source });
        expect(source.set("a", 1)).toBe(source);
        expect(calls).toBe(1);
    });

    it("moves a changed value between secondary groups", () => {
        const source = PersistentIndexedMap.empty<string, number, number>((_key, value) => value % 2).add("a", 1);
        const changed = source.set("a", 2);
        expect(changed.getKeys(1).isEmpty).toBe(true);
        expect(changed.getKeys(0).contains("a")).toBe(true);
        expect(source.getKeys(1).contains("a")).toBe(true);
    });

    it("retains primary and secondary representatives", () => {
        const primary = { id: 1, name: "primary" };
        const index = { id: 2, name: "index" };
        const map = PersistentIndexedMap.empty<Key, string, Key>(
            () => index, keys, undefined, keys,
        ).add(primary, "value");
        expect(map.getStoredKey({ id: 1, name: "probe" })).toBe(primary);
        expect(map.getIndexKey(primary)).toBe(index);
        expect(map.getKeys({ id: 2, name: "probe" }).get({ id: 1, name: "probe" })).toBe(primary);
    });

    it("removes without calling the selector and contracts empty groups", () => {
        let calls = 0;
        const source = PersistentIndexedMap.empty<string, number, number>((_key, value) => { calls++; return value; })
            .add("a", 1);
        const removed = source.remove("a");
        expect(calls).toBe(1);
        expect(removed.isEmpty).toBe(true);
        expect(removed.indexKeyCount).toBe(0);
        expect(source.remove("missing")).toBe(source);
    });

    it("leaves the source reusable when the selector throws", () => {
        const selector = (_key: string, value: number): number => {
            if (value === 9) throw new Error("selector failure");
            return value;
        };
        const source = PersistentIndexedMap.empty(selector).add("a", 1);
        expect(() => source.set("a", 9)).toThrow("selector failure");
        expect(source.get("a")).toBe(1);
        expect(source.validateStructure()).toEqual({ count: 1, indexKeyCount: 1 });
    });
});
