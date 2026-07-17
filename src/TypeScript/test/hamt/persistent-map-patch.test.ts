import { describe, expect, it } from "vitest";

import {
    absentMapPatchValue,
    createHashPolicy,
    MapPatchCompositionError,
    MapPatchConflictError,
    PersistentHashMap,
    PersistentMapPatch,
    presentMapPatchValue,
} from "../../src/hamt/index.js";

describe("PersistentMapPatch", () => {
    it("builds between maps and applies every add, removal, and change", () => {
        const policy = createHashPolicy<string>((key) => key.length, (left, right) => left === right);
        const before = PersistentHashMap.from([["a", 1], ["bb", 2]], policy);
        const after = PersistentHashMap.from([["bb", 20], ["ccc", 3]], policy);
        const patch = PersistentMapPatch.between(before, after);
        expect(patch.count).toBe(3);
        expect(patch.apply(before).mapEquals(after)).toBe(true);
        expect(patch.validateStructure()).toEqual({ count: 3 });
    });

    it("distinguishes absence from a present undefined", () => {
        const source = PersistentHashMap.empty<string, number | undefined>().put("present", undefined);
        const target = source.remove("present").put("added", undefined);
        const patch = PersistentMapPatch.between(source, target);
        expect([...patch]).toEqual(expect.arrayContaining([
            { key: "present", before: { present: true, value: undefined }, after: { present: false } },
            { key: "added", before: { present: false }, after: { present: true, value: undefined } },
        ]));
        expect(patch.apply(source).containsKey("added")).toBe(true);
        expect(patch.apply(source).containsKey("present")).toBe(false);
    });

    it("inverts a patch and restores the source", () => {
        const before = PersistentHashMap.from([["a", 1], ["b", 2]]);
        const after = before.remove("a").put("b", 3).put("c", 4);
        const patch = PersistentMapPatch.between(before, after);
        expect(patch.invert().apply(patch.apply(before)).mapEquals(before)).toBe(true);
    });

    it("composes exactly like sequential application and drops round trips", () => {
        const first = PersistentMapPatch.from<string, number>([
            { key: "a", before: absentMapPatchValue(), after: presentMapPatchValue(1) },
            { key: "b", before: presentMapPatchValue(2), after: presentMapPatchValue(3) },
        ]);
        const second = PersistentMapPatch.from<string, number>([
            { key: "a", before: presentMapPatchValue(1), after: absentMapPatchValue() },
            { key: "b", before: presentMapPatchValue(3), after: presentMapPatchValue(4) },
        ]);
        const source = PersistentHashMap.from([["b", 2]]);
        const composed = first.compose(second);
        expect(composed.count).toBe(1);
        expect(composed.apply(source).mapEquals(second.apply(first.apply(source)))).toBe(true);
    });

    it("reports strict apply and composition conflicts without changing inputs", () => {
        const patch = PersistentMapPatch.from<string, number>([
            { key: "a", before: presentMapPatchValue(1), after: presentMapPatchValue(2) },
        ]);
        const source = PersistentHashMap.from([["a", 9]]);
        expect(() => patch.apply(source)).toThrow(MapPatchConflictError);
        expect(source.get("a")).toBe(9);
        const incompatible = PersistentMapPatch.from<string, number>([
            { key: "a", before: presentMapPatchValue(7), after: presentMapPatchValue(8) },
        ], patch.keyPolicy, patch.valueEquals);
        expect(() => patch.compose(incompatible)).toThrow(MapPatchCompositionError);
    });

    it("rejects policy mismatches and stores no no-op entries", () => {
        const policy = createHashPolicy<string>(() => 0, (left, right) => left === right);
        const patch = PersistentMapPatch.from([
            { key: "a", before: presentMapPatchValue(1), after: presentMapPatchValue(1) },
        ], policy);
        expect(patch.isEmpty).toBe(true);
        expect(() => patch.apply(PersistentHashMap.empty<string, number>())).toThrow(TypeError);
        expect(patch.clear()).toBe(patch);
    });
});
