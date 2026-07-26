/**
 * Tests for the insertion-ordered map: first key representative and position retained while
 * payloads are replaced, plus explicit movement and its failure cases.
 */
import { describe, expect, it } from "vitest";

import {
    createHashPolicy,
    DuplicateKeyError,
    OrderedMapMissingKeyError,
    PersistentOrderedMap,
} from "../../src/index.js";

interface Key { readonly id: number; readonly name: string }
const keys = createHashPolicy<Key>((key) => key.id, (left, right) => left.id === right.id);

describe("PersistentOrderedMap", () => {
    it("retains the first key and position while the last distinct value wins", () => {
        const first = { id: 1, name: "first" };
        const map = PersistentOrderedMap.from<Key, string>([
            [first, "one"],
            [{ id: 2, name: "second" }, "two"],
            [{ id: 1, name: "later" }, "updated"],
        ], keys);
        expect(map.size).toBe(2);
        expect(map.first.key).toBe(first);
        expect(map.get({ id: 1, name: "probe" })).toBe("updated");
        expect(map.indexOfKey(first)).toBe(0);
    });

    it("supports strict positional insertion and value-only replacement", () => {
        const source = PersistentOrderedMap.empty<string, number>().add("a", 1).add("c", 3);
        const inserted = source.insert(1, "b", 2);
        const replaced = inserted.set("b", 20);
        expect([...inserted.keys()]).toEqual(["a", "b", "c"]);
        expect([...replaced.values()]).toEqual([1, 20, 3]);
        expect(replaced.indexOfKey("b")).toBe(1);
        expect(replaced.sharesMembershipRootWith(inserted)).toBe(true);
        expect(() => source.add("a", 9)).toThrow(DuplicateKeyError);
    });

    it("moves entries explicitly and returns itself for positional no-ops", () => {
        const source = PersistentOrderedMap.from([["a", 1], ["b", 2], ["c", 3]]);
        expect(source.moveTo(1, "b")).toBe(source);
        expect([...source.moveToFirst("c").keys()]).toEqual(["c", "a", "b"]);
        expect([...source.moveToLast("a").keys()]).toEqual(["b", "c", "a"]);
        expect(() => source.moveToFirst("missing")).toThrow(OrderedMapMissingKeyError);
    });

    it("removes by key and position while preserving retained snapshots", () => {
        const source = PersistentOrderedMap.from([["a", 1], ["b", 2], ["c", 3]]);
        const removed = source.tryRemove("b");
        expect(removed.removed && removed.entry).toEqual({ key: "b", value: 2 });
        expect([...removed.map.keys()]).toEqual(["a", "c"]);
        expect(source.containsKey("b")).toBe(true);
        expect(source.remove("missing")).toBe(source);
        expect([...source.removeAt(0).keys()]).toEqual(["b", "c"]);
    });

    it("extracts ranges, reverses, and performs stable one-shot sorting", () => {
        const source = PersistentOrderedMap.from([["a", 2], ["b", 1], ["c", 2], ["d", 1]]);
        expect([...source.getRange(1, 2).keys()]).toEqual(["b", "c"]);
        expect([...source.take(2).keys()]).toEqual(["a", "b"]);
        expect([...source.drop(2).keys()]).toEqual(["c", "d"]);
        expect([...source.reverse().keys()]).toEqual(["d", "c", "b", "a"]);
        expect([...source.sort((left, right) => left.value - right.value).keys()]).toEqual(["b", "d", "a", "c"]);
    });

    it("preserves policies, survives sparse relabel pressure, and validates both indexes", () => {
        const valuesEqual = (left: string, right: string): boolean => left.toLowerCase() === right.toLowerCase();
        let map = PersistentOrderedMap.empty<Key, string>(keys, valuesEqual)
            .add({ id: 0, name: "zero" }, "Value")
            .add({ id: 1, name: "one" }, "one");
        expect(map.set({ id: 0, name: "probe" }, "value")).toBe(map);
        for (let id = 2; id < 32; id++) map = map.insert(1, { id, name: String(id) }, String(id));
        expect(map.keyPolicy).toBe(keys);
        expect(map.valueEquals).toBe(valuesEqual);
        expect(map.validateStructure()).toEqual({ count: 32 });
        expect(map.clear().validateStructure()).toEqual({ count: 0 });
    });
});
