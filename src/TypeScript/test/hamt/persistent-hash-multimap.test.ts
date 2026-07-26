/**
 * Tests for the set-valued multimap: separate key and pair cardinalities, representative retention
 * in both domains, and the nonempty-group invariant.
 */
import { describe, expect, it } from "vitest";

import {
    createHashPolicy,
    PersistentHashMultimap,
} from "../../src/hamt/index.js";

interface Box { readonly id: number; readonly name: string }

const boxes = createHashPolicy<Box>((box) => box.id, (left, right) => left.id === right.id);

describe("PersistentHashMultimap", () => {
    it("retains the first representative in both domains", () => {
        const key = { id: 1, name: "key-first" };
        const value = { id: 2, name: "value-first" };
        const map = PersistentHashMultimap.empty<Box, Box>(boxes, boxes)
            .add(key, value)
            .add({ id: 1, name: "key-later" }, { id: 2, name: "value-later" });

        expect(map.keyCount).toBe(1);
        expect(map.pairCount).toBe(1);
        expect(map.getStoredKey({ id: 1, name: "probe" })).toBe(key);
        expect(map.getValues(key).get({ id: 2, name: "probe" })).toBe(value);
    });

    it("contracts a group when its final pair is removed", () => {
        const source = PersistentHashMultimap.from([["a", 1], ["a", 2], ["b", 3]]);
        const oneLeft = source.remove("a", 1);
        const contracted = oneLeft.remove("a", 2);

        expect(oneLeft.keyCount).toBe(2);
        expect(contracted.keyCount).toBe(1);
        expect(contracted.pairCount).toBe(1);
        expect(contracted.containsKey("a")).toBe(false);
        expect(source.pairCount).toBe(3);
    });

    it("removes a whole key class", () => {
        const source = PersistentHashMultimap.from([["a", 1], ["a", 2], ["b", 3]]);
        const result = source.removeKey("a");
        expect([...result]).toEqual([{ key: "b", value: 3 }]);
        expect(result.removeKey("missing")).toBe(result);
    });

    it("returns the receiver and shares its root for duplicate addition and misses", () => {
        const source = PersistentHashMultimap.empty<string, number>().add("a", 1);
        const duplicate = source.add("a", 1);
        expect(duplicate).toBe(source);
        expect(duplicate.sharesRootWith(source)).toBe(true);
        expect(source.remove("a", 2)).toBe(source);
    });

    it("preserves independent policies and retained branches", () => {
        const keys = createHashPolicy<string>((value) => value.length, (a, b) => a.toLowerCase() === b.toLowerCase());
        const values = createHashPolicy<number>((value) => value, (a, b) => a === b);
        const source = PersistentHashMultimap.empty(keys, values).add("Alpha", 1);
        const branch = source.add("ALPHA", 2);

        expect(source.keyPolicy).toBe(keys);
        expect(source.valuePolicy).toBe(values);
        expect(source.pairCount).toBe(1);
        expect(branch.pairCount).toBe(2);
    });

    it("reports valid counts and empty lookups", () => {
        const map = PersistentHashMultimap.from([["a", 1], ["a", 2], ["b", 2]]);
        const lookup = map.tryGetValues("missing");
        expect(lookup.found).toBe(false);
        expect(lookup.values.isEmpty).toBe(true);
        expect(map.validateStructure()).toBe(true);
        expect(map.clear().validateStructure()).toBe(true);
    });
});
