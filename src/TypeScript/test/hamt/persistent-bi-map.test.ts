/**
 * Tests for the strict bidirectional map: rejection on either domain with the key checked first,
 * representative retention, and constant-time inversion.
 */
import fc from "fast-check";
import { describe, expect, test } from "vitest";
import {
    BiMapConflictError,
    PersistentBiMap,
    createHashPolicy,
} from "../../src/hamt/index.js";

describe("PersistentBiMap", () => {
    test("retains independent policies and exposes a cached inverse facade", () => {
        const keys = createHashPolicy<string>(() => 0, (a, b) => a.toLowerCase() === b.toLowerCase());
        const values = createHashPolicy<number>((value) => value, (a, b) => a === b);
        const map = PersistentBiMap.empty(keys, values).add("One", 1).add("Two", 2);

        expect(map.keyPolicy).toBe(keys);
        expect(map.valuePolicy).toBe(values);
        expect(map.get("ONE")).toEqual({ found: true, value: 1 });
        expect(map.getKey(2)).toEqual({ found: true, value: "Two" });
        expect(map.inverse).toBe(map.inverse);
        expect(map.inverse.inverse).toBe(map);
        expect(map.inverse.get(1)).toEqual({ found: true, value: "One" });
        expect(map.validateStructure()).toBe(true);
    });

    test("strictly rejects duplicate classes on either domain", () => {
        const policy = createHashPolicy<string>(() => 0, (a, b) => a.toLowerCase() === b.toLowerCase());
        const map = PersistentBiMap.empty(policy, policy).add("Key", "Value");

        expect(() => map.add("KEY", "other")).toThrowError(BiMapConflictError);
        expect(map.tryAdd("KEY", "other")).toEqual({ added: false, conflict: "key", map });
        expect(map.tryAdd("other", "VALUE")).toEqual({ added: false, conflict: "value", map });
        expect(map.size).toBe(1);
    });

    test("set retains representatives, uses value policy for no-ops, and rejects displacement", () => {
        interface Token { readonly text: string; readonly identity: number }
        const policy = createHashPolicy<Token>(
            () => 0,
            (a, b) => a.text.toLowerCase() === b.text.toLowerCase(),
        );
        const key: Token = { text: "Key", identity: 1 };
        const value: Token = { text: "Value", identity: 2 };
        const map = PersistentBiMap.empty(policy, policy)
            .add(key, value)
            .add({ text: "Other", identity: 3 }, { text: "Claimed", identity: 4 });

        const same = map.set(
            { text: "KEY", identity: 5 },
            { text: "VALUE", identity: 6 },
        );
        expect(same).toBe(map);
        expect(same.getKey({ text: "value", identity: 7 })).toEqual({ found: true, value: key });
        expect(same.get({ text: "key", identity: 8 })).toEqual({ found: true, value });

        expect(() => map.set(key, { text: "CLAIMED", identity: 9 })).toThrowError(BiMapConflictError);
        const replacement: Token = { text: "Replacement", identity: 10 };
        const changed = map.set({ text: "key", identity: 11 }, replacement);
        expect(changed.getKey(replacement)).toEqual({ found: true, value: key });
        expect(changed.containsValue(value)).toBe(false);
        expect(map.get(key)).toEqual({ found: true, value });
        expect(changed.validateStructure()).toBe(true);
    });

    test("supports presence-safe undefined representatives", () => {
        const map = PersistentBiMap.empty<undefined | string, undefined | number>()
            .add(undefined, 1)
            .add("undefined-value", undefined);

        expect(map.get(undefined)).toEqual({ found: true, value: 1 });
        expect(map.getKey(undefined)).toEqual({ found: true, value: "undefined-value" });
        expect(map.get("missing")).toEqual({ found: false });
        expect(map.validateStructure()).toBe(true);
    });

    test("removes symmetrically and preserves absent and clear identity", () => {
        const map = PersistentBiMap.from<number, string>([[1, "one"], [2, "two"]]);
        expect(map.tryRemoveKey(9)).toEqual({ removed: false, map });
        expect(map.tryRemoveValue("nine")).toEqual({ removed: false, map });
        expect(map.removeKey(9)).toBe(map);
        expect(map.removeValue("nine")).toBe(map);

        const first = map.tryRemoveKey(1);
        expect(first.removed).toBe(true);
        if (!first.removed) throw new Error("unreachable");
        expect(first.value).toBe("one");
        const second = first.map.tryRemoveValue("two");
        expect(second.removed).toBe(true);
        expect(second.map.isEmpty).toBe(true);
        expect(second.map.clear()).toBe(second.map);
        expect(map.size).toBe(2);
    });

    test("enumerates forward pairs with aligned key and value views", () => {
        const map = PersistentBiMap.from<number, string>([[1, "one"], [2, "two"], [3, "three"]]);
        const entries = Array.from(map);
        expect(Array.from(map.keys())).toEqual(entries.map((entry) => entry.key));
        expect(Array.from(map.values())).toEqual(entries.map((entry) => entry.value));
        expect(Array.from(map.entries())).toEqual(entries);
    });

    test("policy failure leaves the source and inverse cache unchanged", () => {
        let fail = false;
        const values = createHashPolicy<string>(
            (value) => {
                if (fail) throw new Error("injected");
                return value.length;
            },
            (a, b) => a === b,
        );
        const map = PersistentBiMap.empty<number, string>(undefined, values).add(1, "one");
        const inverse = map.inverse;
        fail = true;
        expect(() => map.add(2, "two")).toThrowError("injected");
        fail = false;
        expect(map.size).toBe(1);
        expect(map.inverse).toBe(inverse);
        expect(inverse.inverse).toBe(map);
        expect(map.validateStructure()).toBe(true);
    });

    test("matches a retained bidirectional model", () => {
        fc.assert(fc.property(
            fc.array(fc.record({
                kind: fc.constantFrom("add", "set", "removeKey", "removeValue"),
                key: fc.integer({ min: 0, max: 15 }),
                value: fc.integer({ min: 0, max: 15 }),
            }), { maxLength: 300 }),
            (commands) => {
                let actual = PersistentBiMap.empty<number, number>();
                const forward = new Map<number, number>();
                const inverse = new Map<number, number>();
                const retained: readonly [PersistentBiMap<number, number>, ReadonlyMap<number, number>][] = [];
                const snapshots: [PersistentBiMap<number, number>, ReadonlyMap<number, number>][] = [...retained];

                for (const command of commands) {
                    if (snapshots.length < 8 && (command.key + command.value) % 11 === 0) {
                        snapshots.push([actual, new Map(forward)]);
                    }
                    if (command.kind === "add") {
                        const canAdd = !forward.has(command.key) && !inverse.has(command.value);
                        const result = actual.tryAdd(command.key, command.value);
                        expect(result.added).toBe(canAdd);
                        actual = result.map;
                        if (canAdd) {
                            forward.set(command.key, command.value);
                            inverse.set(command.value, command.key);
                        }
                    } else if (command.kind === "set") {
                        const owner = inverse.get(command.value);
                        if (owner !== undefined && owner !== command.key) {
                            expect(() => actual.set(command.key, command.value)).toThrowError(BiMapConflictError);
                        } else {
                            const previous = forward.get(command.key);
                            if (previous !== undefined) inverse.delete(previous);
                            forward.set(command.key, command.value);
                            inverse.set(command.value, command.key);
                            actual = actual.set(command.key, command.value);
                        }
                    } else if (command.kind === "removeKey") {
                        const previous = forward.get(command.key);
                        if (previous !== undefined) inverse.delete(previous);
                        forward.delete(command.key);
                        actual = actual.removeKey(command.key);
                    } else {
                        const key = inverse.get(command.value);
                        if (key !== undefined) forward.delete(key);
                        inverse.delete(command.value);
                        actual = actual.removeValue(command.value);
                    }
                    assertModel(actual, forward);
                }
                for (const [snapshot, model] of snapshots) assertModel(snapshot, model);
            },
        ), { seed: 0xB1A4, numRuns: 80 });
    });
});

function assertModel(map: PersistentBiMap<number, number>, expected: ReadonlyMap<number, number>): void {
    expect(map.size).toBe(expected.size);
    expect(map.validateStructure()).toBe(true);
    expect(Array.from(map, (entry) => [entry.key, entry.value] as const).sort((a, b) => a[0] - b[0]))
        .toEqual(Array.from(expected).sort((a, b) => a[0] - b[0]));
    for (const [key, value] of expected) {
        expect(map.get(key)).toEqual({ found: true, value });
        expect(map.getKey(value)).toEqual({ found: true, value: key });
    }
}
