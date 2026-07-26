/**
 * Tests that the single-pass factory updates hash once, descend once, and invoke exactly one
 * caller-supplied factory, with no retry loop.
 */
import fc from "fast-check";
import { describe, expect, test } from "vitest";
import { PersistentHashMap, createHashPolicy } from "../../src/hamt/index.js";

describe("PersistentHashMap factory updates", () => {
    test("validates both factories before hashing or selecting a branch", () => {
        let hashCalls = 0;
        const policy = createHashPolicy<string>(
            (key) => {
                hashCalls++;
                return key.length;
            },
            (left, right) => left === right,
        );
        const empty = PersistentHashMap.empty<string, number>(policy);
        const present = empty.put("stored", 1);
        hashCalls = 0;

        expect(() => empty.getOrAdd("missing", null as unknown as (key: string) => number))
            .toThrow(TypeError);
        expect(() => present.getOrAdd("stored", undefined as unknown as (key: string) => number))
            .toThrow(TypeError);
        expect(() => present.addOrUpdate(
            "stored",
            null as unknown as (key: string) => number,
            (_key, value) => value,
        )).toThrow(TypeError);
        expect(() => present.addOrUpdate(
            "stored",
            () => 2,
            undefined as unknown as (key: string, value: number) => number,
        )).toThrow(TypeError);
        expect(hashCalls).toBe(0);
    });

    test("selects exactly one factory and retains key and equal-value representatives", () => {
        interface Key { readonly text: string; readonly identity: number }
        let hashCalls = 0;
        let equalityCalls = 0;
        const policy = createHashPolicy<Key>(
            (key) => {
                hashCalls++;
                return key.text.toLowerCase().charCodeAt(0);
            },
            (left, right) => {
                equalityCalls++;
                return left.text.toLowerCase() === right.text.toLowerCase();
            },
        );
        const storedKey: Key = { text: "Alpha", identity: 1 };
        const lookupKey: Key = { text: "ALPHA", identity: 2 };
        const storedValue = { identity: 1 };
        const source = PersistentHashMap.empty<Key, object>(policy).put(storedKey, storedValue);

        hashCalls = 0;
        equalityCalls = 0;
        let getFactoryCalls = 0;
        const hit = source.getOrAdd(lookupKey, () => {
            getFactoryCalls++;
            return { identity: 99 };
        });
        expect(hit).toEqual({ map: source, value: storedValue });
        expect(getFactoryCalls).toBe(0);
        expect(hashCalls).toBe(1);
        expect(equalityCalls).toBe(1);

        const replacement = { identity: 2 };
        let addCalls = 0;
        let updateCalls = 0;
        let observedKey: Key | undefined;
        let observedValue: object | undefined;
        const updated = source.addOrUpdate(
            lookupKey,
            () => {
                addCalls++;
                return { identity: 3 };
            },
            (key, value) => {
                updateCalls++;
                observedKey = key;
                observedValue = value;
                return replacement;
            },
        );
        expect(addCalls).toBe(0);
        expect(updateCalls).toBe(1);
        expect(observedKey).toBe(lookupKey);
        expect(observedValue).toBe(storedValue);
        expect(updated.value).toBe(replacement);
        expect(updated.map.getEntry(lookupKey)).toEqual({ key: storedKey, value: replacement });
        expect(source.getEntry(lookupKey)).toEqual({ key: storedKey, value: storedValue });

        const equal = source.addOrUpdate(lookupKey, () => replacement, (_key, value) => value);
        expect(equal.map).toBe(source);
        expect(equal.value).toBe(storedValue);

        const callerKey: Key = { text: "Beta", identity: 3 };
        let factoryKey: Key | undefined;
        const added = source.getOrAdd(callerKey, (key) => {
            factoryKey = key;
            return replacement;
        });
        expect(factoryKey).toBe(callerKey);
        expect(added.value).toBe(replacement);
        expect(added.map.getEntry(callerKey)).toEqual({ key: callerKey, value: replacement });
        expect(source.containsKey(callerKey)).toBe(false);
    });

    test("distinguishes present undefined and preserves SameValueZero value no-ops", () => {
        const presentUndefined = PersistentHashMap.empty<string, string | undefined>()
            .put("key", undefined);
        let addCalls = 0;
        const hit = presentUndefined.getOrAdd("key", () => {
            addCalls++;
            return "added";
        });
        expect(hit.map).toBe(presentUndefined);
        expect(hit.value).toBeUndefined();
        expect(addCalls).toBe(0);

        let updateCalls = 0;
        const updated = presentUndefined.addOrUpdate(
            "key",
            () => "added",
            (_key, value) => {
                updateCalls++;
                expect(value).toBeUndefined();
                return "updated";
            },
        );
        expect(updateCalls).toBe(1);
        expect(updated.value).toBe("updated");
        expect(updated.map.get("key")).toBe("updated");

        const negativeZero = PersistentHashMap.empty<string, number>().put("zero", -0);
        const sameZero = negativeZero.addOrUpdate("zero", () => 1, () => +0);
        expect(sameZero.map).toBe(negativeZero);
        expect(Object.is(sameZero.value, -0)).toBe(true);
        const nan = PersistentHashMap.empty<string, number>().put("nan", Number.NaN);
        const sameNan = nan.addOrUpdate("nan", () => 1, () => Number.NaN);
        expect(sameNan.map).toBe(nan);
        expect(Number.isNaN(sameNan.value)).toBe(true);
    });

    test("hashes once and scans one equal-hash collision bucket once", () => {
        let hashCalls = 0;
        let equalityCalls = 0;
        const policy = createHashPolicy<number>(
            () => {
                hashCalls++;
                return 0;
            },
            (left, right) => {
                equalityCalls++;
                return left === right;
            },
        );
        let source = PersistentHashMap.empty<number, number>(policy);
        for (let key = 0; key < 32; key++) source = source.put(key, key);

        hashCalls = 0;
        equalityCalls = 0;
        const hit = source.addOrUpdate(31, () => -1, (_key, value) => value + 1);
        expect(hit.value).toBe(32);
        expect(hashCalls).toBe(1);
        expect(equalityCalls).toBe(32);

        hashCalls = 0;
        equalityCalls = 0;
        const miss = source.getOrAdd(32, () => 32);
        expect(miss.value).toBe(32);
        expect(miss.map.size).toBe(33);
        expect(hashCalls).toBe(1);
        expect(equalityCalls).toBe(32);
    });

    test("factory and policy failures publish no successor", () => {
        let throwFromHash = false;
        let throwFromEquality = false;
        const failure = new Error("callback failed");
        const policy = createHashPolicy<number>(
            (key) => {
                if (throwFromHash) throw failure;
                return key & 1;
            },
            (left, right) => {
                if (throwFromEquality) throw failure;
                return left === right;
            },
        );
        const source = PersistentHashMap.empty<number, number>(policy).put(1, 10).put(2, 20);
        const expected = Array.from(source);

        expect(() => source.getOrAdd(3, () => { throw failure; })).toThrow(failure);
        expect(() => source.addOrUpdate(1, () => 1, () => { throw failure; })).toThrow(failure);
        throwFromHash = true;
        expect(() => source.getOrAdd(3, () => 30)).toThrow(failure);
        throwFromHash = false;
        throwFromEquality = true;
        expect(() => source.getOrAdd(1, () => 30)).toThrow(failure);
        throwFromEquality = false;

        expect(Array.from(source)).toEqual(expected);
        expect(source.get(1)).toBe(10);
        expect(source.get(2)).toBe(20);
    });

    test("generated histories match a mutable model and retain snapshots", () => {
        fc.assert(fc.property(
            fc.array(fc.record({
                key: fc.integer({ min: -40, max: 40 }),
                addValue: fc.integer(),
                delta: fc.integer({ min: -20, max: 20 }),
                update: fc.boolean(),
            }), { maxLength: 250 }),
            (operations) => {
                let actual = PersistentHashMap.empty<number, number>();
                const expected = new Map<number, number>();
                const snapshots: Array<readonly [PersistentHashMap<number, number>, Map<number, number>]> = [];
                for (let index = 0; index < operations.length; index++) {
                    const operation = operations[index];
                    if (operation === undefined) continue;
                    if (index % 47 === 0) snapshots.push([actual, new Map(expected)]);
                    if (operation.update) {
                        const result = actual.addOrUpdate(
                            operation.key,
                            () => operation.addValue,
                            (_key, value) => value + operation.delta,
                        );
                        actual = result.map;
                        expected.set(
                            operation.key,
                            expected.has(operation.key)
                                ? (expected.get(operation.key) as number) + operation.delta
                                : operation.addValue,
                        );
                        expect(result.value).toBe(expected.get(operation.key));
                    } else {
                        const result = actual.getOrAdd(operation.key, () => operation.addValue);
                        actual = result.map;
                        if (!expected.has(operation.key)) expected.set(operation.key, operation.addValue);
                        expect(result.value).toBe(expected.get(operation.key));
                    }
                }
                expect(actual.size).toBe(expected.size);
                for (const [key, value] of expected) expect(actual.get(key)).toBe(value);
                for (const [snapshot, model] of snapshots) {
                    expect(snapshot.size).toBe(model.size);
                    for (const [key, value] of model) expect(snapshot.get(key)).toBe(value);
                }
            },
        ), { numRuns: 150 });
    });
});
