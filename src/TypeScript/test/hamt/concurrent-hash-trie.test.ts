/**
 * Tests for the snapshotting hash-trie facade: snapshot isolation from later mutation, and retry
 * behavior when a reentrant policy callback publishes a new root.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    ConcurrentHashTrie,
    type ConcurrentHashTrieSnapshot,
    createHashPolicy,
    sameValueZero,
} from "../../src/hamt/index.js";

describe("ConcurrentHashTrie facade semantics", () => {
    test("present undefined values and retained key representatives remain unambiguous", () => {
        interface Key { readonly text: string; readonly identity: number }
        const policy = createHashPolicy<Key>(
            () => 0,
            (left, right) => left.text.toLowerCase() === right.text.toLowerCase(),
        );
        const stored: Key = { text: "Alpha", identity: 1 };
        const caller: Key = { text: "ALPHA", identity: 2 };
        const trie = new ConcurrentHashTrie<Key, number | undefined>(policy);

        expect(trie.tryAdd(stored, undefined)).toBe(true);
        const generation = trie.generation;
        let addCalls = 0;
        expect(trie.getOrPut(caller, () => { addCalls++; return 10; })).toBeUndefined();
        expect(addCalls).toBe(0);
        expect(trie.generation).toBe(generation);
        expect(trie.containsKey(caller)).toBe(true);

        let updateKey: Key | undefined;
        expect(trie.compute(
            caller,
            () => 20,
            (key, value) => {
                updateKey = key;
                expect(value).toBeUndefined();
                return 30;
            },
        )).toBe(30);
        expect(updateKey).toBe(caller);
        expect(trie.getEntry(caller)).toEqual({ key: stored, value: 30 });
        expect(trie.getEntry(caller)!.key).toBe(stored);
        expect(trie.generation).toBe(generation + 1);
    });

    test("getOrPut retries instead of overwriting a reentrant same-key insertion", () => {
        const trie = new ConcurrentHashTrie<string, string>();
        let calls = 0;

        const selected = trie.getOrPut("target", (key) => {
            calls++;
            trie.set(key, "nested");
            return "discarded";
        });

        expect(selected).toBe("nested");
        expect(calls).toBe(1);
        expect(trie.get("target")).toBe("nested");
        expect(trie.generation).toBe(1);
    });

    test("getOrPut may reinvoke a factory after an unrelated reentrant publication", () => {
        const trie = new ConcurrentHashTrie<string, string>();
        let calls = 0;

        const selected = trie.getOrPut("target", () => {
            calls++;
            if (calls === 1) trie.set("side", "nested");
            return `candidate-${calls}`;
        });

        expect(selected).toBe("candidate-2");
        expect(calls).toBe(2);
        expect(trie.get("target")).toBe("candidate-2");
        expect(trie.get("side")).toBe("nested");
        expect(trie.generation).toBe(2);
    });

    test("compute reselects its branch and caller key after reentrant publications", () => {
        const trie = new ConcurrentHashTrie<string, number>();
        let addCalls = 0;
        let updateCalls = 0;
        const updateKeys: string[] = [];

        const addedThenUpdated = trie.compute(
            "target",
            (key) => {
                addCalls++;
                trie.set(key, 40);
                return -1;
            },
            (key, value) => {
                updateCalls++;
                updateKeys.push(key);
                return value + 2;
            },
        );

        expect(addedThenUpdated).toBe(42);
        expect(addCalls).toBe(1);
        expect(updateCalls).toBe(1);
        expect(updateKeys).toEqual(["target"]);
        expect(trie.generation).toBe(2);

        let retryCalls = 0;
        const retriedUpdate = trie.compute(
            "target",
            () => 0,
            (key, value) => {
                retryCalls++;
                expect(key).toBe("target");
                if (retryCalls === 1) trie.set("side", 5);
                return value + retryCalls;
            },
        );

        expect(retriedUpdate).toBe(44);
        expect(retryCalls).toBe(2);
        expect(trie.get("target")).toBe(44);
        expect(trie.get("side")).toBe(5);
        expect(trie.generation).toBe(4);
    });

    test("same-key hash-policy reentrancy determines tryAdd and remove results", () => {
        interface Key { readonly id: number; readonly trigger: boolean }
        const stored: Key = { id: 1, trigger: false };
        const caller: Key = { id: 1, trigger: true };
        let trie!: ConcurrentHashTrie<Key, string>;
        let onHash: ((key: Key) => void) | undefined;
        const policy = createHashPolicy<Key>(
            (key) => {
                const action = onHash;
                if (action !== undefined) {
                    onHash = undefined;
                    action(key);
                }
                return key.id;
            },
            (left, right) => left.id === right.id,
        );
        trie = new ConcurrentHashTrie<Key, string>(policy);

        let nestedAdded: boolean | undefined;
        onHash = () => { nestedAdded = trie.tryAdd(stored, "nested"); };
        expect(trie.tryAdd(caller, "outer")).toBe(false);
        expect(nestedAdded).toBe(true);
        expect(trie.getEntry(caller)).toEqual({ key: stored, value: "nested" });
        expect(trie.getEntry(caller)!.key).toBe(stored);
        expect(trie.generation).toBe(1);

        let nestedRemoved: { readonly key: Key; readonly value: string } | undefined;
        onHash = () => { nestedRemoved = trie.remove(stored); };
        expect(trie.remove(caller)).toBeUndefined();
        expect(nestedRemoved).toEqual({ key: stored, value: "nested" });
        expect(trie.isEmpty).toBe(true);
        expect(trie.generation).toBe(2);
    });

    test("different-key equivalence-policy reentrancy cannot lose set or remove publications", () => {
        interface Key { readonly id: number; readonly name: string }
        const side: Key = { id: 1, name: "side" };
        const target: Key = { id: 2, name: "target" };
        const nestedDuringSet: Key = { id: 3, name: "nested-set" };
        const nestedDuringRemove: Key = { id: 4, name: "nested-remove" };
        let trie!: ConcurrentHashTrie<Key, string>;
        let onEquivalent: (() => void) | undefined;
        const policy = createHashPolicy<Key>(
            () => 0,
            (left, right) => {
                const action = onEquivalent;
                if (action !== undefined) {
                    onEquivalent = undefined;
                    action();
                }
                return left.id === right.id;
            },
        );
        trie = new ConcurrentHashTrie<Key, string>(policy);
        trie.set(side, "side");

        onEquivalent = () => { trie.set(nestedDuringSet, "nested-set"); };
        trie.set(target, "target");
        expect(trie.get(side)).toBe("side");
        expect(trie.get(nestedDuringSet)).toBe("nested-set");
        expect(trie.get(target)).toBe("target");
        expect(trie.generation).toBe(3);

        onEquivalent = () => { trie.set(nestedDuringRemove, "nested-remove"); };
        expect(trie.remove(target)).toEqual({ key: target, value: "target" });
        expect(trie.containsKey(target)).toBe(false);
        expect(trie.get(side)).toBe("side");
        expect(trie.get(nestedDuringSet)).toBe("nested-set");
        expect(trie.get(nestedDuringRemove)).toBe("nested-remove");
        expect(trie.generation).toBe(5);
    });

    test("discarded and failing callbacks do not publish candidate roots", () => {
        const trie = new ConcurrentHashTrie<string, number>();
        trie.set("stored", 1);
        const before = trie.snapshot().toPersistentHashMap();
        const generation = trie.generation;
        const failure = new Error("factory failed");

        expect(() => trie.getOrPut("missing", () => { throw failure; })).toThrow(failure);
        expect(() => trie.compute("stored", () => 0, () => { throw failure; })).toThrow(failure);
        expect(trie.snapshot().toPersistentHashMap()).toBe(before);
        expect(trie.generation).toBe(generation);

        expect(() => trie.compute(
            "missing",
            () => {
                trie.set("nested", 2);
                throw failure;
            },
            (_key, value) => value,
        )).toThrow(failure);
        expect(trie.containsKey("missing")).toBe(false);
        expect(trie.get("nested")).toBe(2);
        expect(trie.generation).toBe(generation + 1);
    });

    test("no-op values, argument validation, and actual publications govern generation", () => {
        const trie = new ConcurrentHashTrie<string, object>();
        const value = {};
        trie.set("key", value);
        const generation = trie.generation;

        trie.set("key", value);
        expect(trie.compute("key", () => ({}), (_key, current) => current)).toBe(value);
        expect(trie.generation).toBe(generation);

        const invalid = undefined as unknown as (key: string) => object;
        expect(() => trie.getOrPut("key", invalid)).toThrow(TypeError);
        expect(() => trie.compute("key", invalid, (_key, current) => current)).toThrow(TypeError);
        expect(() => trie.compute("key", () => value, invalid)).toThrow(TypeError);
        expect(trie.generation).toBe(generation);

        const signedZero = new ConcurrentHashTrie<string, number>();
        signedZero.set("zero", -0);
        const zeroGeneration = signedZero.generation;
        const retained = signedZero.compute("zero", () => 1, () => +0);
        expect(Object.is(retained, -0)).toBe(true);
        expect(Object.is(signedZero.get("zero"), -0)).toBe(true);
        expect(signedZero.generation).toBe(zeroGeneration);
    });

    test("snapshots retain canonical CHAMP order, collisions, and representatives", () => {
        const integerPolicy = createHashPolicy<number>((key) => key, (left, right) => left === right);
        const integerTrie = new ConcurrentHashTrie<number, string>(integerPolicy);
        integerTrie.set(0, "zero");
        integerTrie.set(32, "thirty-two");
        integerTrie.set(1, "one");
        expect(Array.from(integerTrie, (entry) => entry.key)).toEqual([1, 0, 32]);

        interface CollisionKey { readonly id: number; readonly representative: string }
        const policy = createHashPolicy<CollisionKey>(
            () => 0,
            (left, right) => left.id === right.id,
        );
        const first: CollisionKey = { id: 1, representative: "first" };
        const equivalent: CollisionKey = { id: 1, representative: "equivalent" };
        const second: CollisionKey = { id: 2, representative: "second" };
        const third: CollisionKey = { id: 3, representative: "third" };
        const trie = new ConcurrentHashTrie<CollisionKey, string>(policy);
        trie.set(first, "one");
        trie.set(second, "two");
        trie.set(third, "three");
        trie.set(equivalent, "updated");
        const snapshot = trie.snapshot();

        expect(Array.from(snapshot, (entry) => entry.key)).toEqual([first, second, third]);
        expect(snapshot.getEntry(equivalent)!.key).toBe(first);
        expect(Array.from(snapshot)).toEqual(Array.from(snapshot.toPersistentHashMap()));

        expect(trie.remove(second)).toEqual({ key: second, value: "two" });
        trie.set({ id: 4, representative: "fourth" }, "four");
        expect(Array.from(snapshot, (entry) => entry.key)).toEqual([first, second, third]);
        expect(snapshot.size).toBe(3);
    });
});

type ModelValue = number | undefined;

type ModelOperation =
    | { readonly kind: "set"; readonly key: number; readonly value: ModelValue }
    | { readonly kind: "tryAdd"; readonly key: number; readonly value: ModelValue }
    | { readonly kind: "remove"; readonly key: number }
    | { readonly kind: "clear" }
    | { readonly kind: "getOrPut"; readonly key: number; readonly value: ModelValue }
    | { readonly kind: "compute"; readonly key: number; readonly add: ModelValue; readonly delta: number }
    | { readonly kind: "snapshot" };

const modelValue: fc.Arbitrary<ModelValue> = fc.oneof(
    fc.integer({ min: -20, max: 20 }),
    fc.constant(undefined),
);

const modelOperation: fc.Arbitrary<ModelOperation> = fc.oneof(
    fc.record({ kind: fc.constant("set" as const), key: fc.integer({ min: -6, max: 6 }), value: modelValue }),
    fc.record({ kind: fc.constant("tryAdd" as const), key: fc.integer({ min: -6, max: 6 }), value: modelValue }),
    fc.record({ kind: fc.constant("remove" as const), key: fc.integer({ min: -6, max: 6 }) }),
    fc.record({ kind: fc.constant("clear" as const) }),
    fc.record({ kind: fc.constant("getOrPut" as const), key: fc.integer({ min: -6, max: 6 }), value: modelValue }),
    fc.record({
        kind: fc.constant("compute" as const),
        key: fc.integer({ min: -6, max: 6 }),
        add: modelValue,
        delta: fc.integer({ min: -5, max: 5 }),
    }),
    fc.record({ kind: fc.constant("snapshot" as const) }),
);

describe("ConcurrentHashTrie model", () => {
    test("random histories agree with Map and retain every captured snapshot", () => {
        fc.assert(fc.property(
            fc.array(modelOperation, { maxLength: 80 }),
            (operations) => {
                const trie = new ConcurrentHashTrie<number, ModelValue>();
                const model = new Map<number, ModelValue>();
                const retained: Array<{
                    readonly snapshot: ConcurrentHashTrieSnapshot<number, ModelValue>;
                    readonly model: Map<number, ModelValue>;
                }> = [];
                let generation = 0;

                for (const operation of operations) {
                    switch (operation.kind) {
                        case "set": {
                            const changed = !model.has(operation.key)
                                || !sameValueZero(model.get(operation.key), operation.value);
                            trie.set(operation.key, operation.value);
                            model.set(operation.key, operation.value);
                            if (changed) generation++;
                            break;
                        }
                        case "tryAdd": {
                            const added = !model.has(operation.key);
                            expect(trie.tryAdd(operation.key, operation.value)).toBe(added);
                            if (added) {
                                model.set(operation.key, operation.value);
                                generation++;
                            }
                            break;
                        }
                        case "remove": {
                            const present = model.has(operation.key);
                            const value = model.get(operation.key);
                            const removed = trie.remove(operation.key);
                            if (present) {
                                expect(removed).toEqual({ key: operation.key, value });
                                model.delete(operation.key);
                                generation++;
                            } else {
                                expect(removed).toBeUndefined();
                            }
                            break;
                        }
                        case "clear": {
                            const changed = model.size !== 0;
                            trie.clear();
                            model.clear();
                            if (changed) generation++;
                            break;
                        }
                        case "getOrPut": {
                            const present = model.has(operation.key);
                            const expected = present ? model.get(operation.key) : operation.value;
                            let calls = 0;
                            expect(trie.getOrPut(operation.key, () => { calls++; return operation.value; })).toBe(expected);
                            expect(calls).toBe(present ? 0 : 1);
                            if (!present) {
                                model.set(operation.key, operation.value);
                                generation++;
                            }
                            break;
                        }
                        case "compute": {
                            const present = model.has(operation.key);
                            const current = model.get(operation.key);
                            const expected = present ? (current ?? 0) + operation.delta : operation.add;
                            let addCalls = 0;
                            let updateCalls = 0;
                            expect(trie.compute(
                                operation.key,
                                () => { addCalls++; return operation.add; },
                                (key, value) => {
                                    updateCalls++;
                                    expect(key).toBe(operation.key);
                                    return (value ?? 0) + operation.delta;
                                },
                            )).toBe(expected);
                            expect(addCalls).toBe(present ? 0 : 1);
                            expect(updateCalls).toBe(present ? 1 : 0);
                            if (!present || !sameValueZero(current, expected)) generation++;
                            model.set(operation.key, expected);
                            break;
                        }
                        case "snapshot":
                            retained.push({ snapshot: trie.snapshot(), model: new Map(model) });
                            break;
                    }

                    assertMatchesModel(trie, model, generation);
                }

                for (const captured of retained) assertSnapshotMatchesModel(captured.snapshot, captured.model);
            },
        ), { numRuns: 100 });
    });
});

function assertMatchesModel(
    trie: ConcurrentHashTrie<number, ModelValue>,
    model: ReadonlyMap<number, ModelValue>,
    generation: number,
): void {
    expect(trie.size).toBe(model.size);
    expect(trie.isEmpty).toBe(model.size === 0);
    expect(trie.generation).toBe(generation);
    assertSnapshotMatchesModel(trie.snapshot(), model);
}

function assertSnapshotMatchesModel(
    snapshot: ConcurrentHashTrieSnapshot<number, ModelValue>,
    model: ReadonlyMap<number, ModelValue>,
): void {
    expect(snapshot.size).toBe(model.size);
    expect(snapshot.isEmpty).toBe(model.size === 0);
    const entries = Array.from(snapshot);
    expect(entries).toHaveLength(model.size);
    expect(new Set(entries.map((entry) => entry.key)).size).toBe(model.size);
    for (let key = -6; key <= 6; key++) {
        expect(snapshot.containsKey(key)).toBe(model.has(key));
        const entry = snapshot.getEntry(key);
        if (model.has(key)) {
            expect(entry).toEqual({ key, value: model.get(key) });
        } else {
            expect(entry).toBeUndefined();
        }
    }
}
