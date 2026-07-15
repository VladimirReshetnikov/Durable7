import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    ConcurrentHashTrie,
    DuplicateKeyError,
    PersistentHashMap,
    PersistentHashSet,
    TransientConsumedError,
    createHashPolicy,
} from "../../src/hamt/index.js";

describe("PersistentHashMap", () => {
    test("updates preserve old versions and no-op roots", () => {
        const empty = PersistentHashMap.empty<string, number>();
        const one = empty.put("a", 1);
        const two = one.put("b", 2);
        const replaced = two.put("a", 3);

        expect(empty.get("a")).toBeUndefined();
        expect(one.get("a")).toBe(1);
        expect(two.get("a")).toBe(1);
        expect(replaced.get("a")).toBe(3);
        expect(two.put("a", 1)).toBe(two);
        expect(two.remove("missing")).toBe(two);
    });

    test("duplicate insertion rejects without allocation", () => {
        const map = PersistentHashMap.empty<string, number>().put("a", 1);
        expect(map.tryAdd("a", 2)).toEqual({ value: map, added: false });
        expect(() => map.add("a", 2)).toThrow(DuplicateKeyError);
    });

    test("collision buckets retain entries and remove exact representatives", () => {
        const policy = createHashPolicy<number>(() => 0, (left, right) => left === right);
        const map = PersistentHashMap.empty<number, number>(policy).put(1, 10).put(2, 20).put(3, 30);
        expect(map.get(1)).toBe(10);
        expect(map.get(2)).toBe(20);
        const removed = map.tryRemove(2)!;
        expect(removed.value).toBe(20);
        expect(Array.from(removed.map.keys())).toEqual([1, 3]);
    });

    test("equivalent replacement retains the original key", () => {
        interface Key { readonly text: string; readonly identity: number }
        const policy = createHashPolicy<Key>(
            (key) => key.text.charCodeAt(0),
            (left, right) => left.text === right.text,
        );
        const original: Key = { text: "x", identity: 1 };
        const replacement: Key = { text: "x", identity: 2 };
        const map = PersistentHashMap.from([[original, 10], [replacement, 20]], policy);
        expect(map.size).toBe(1);
        expect(map.getEntry(replacement)).toEqual({ key: original, value: 20 });
    });

    test("typed diff distinguishes absent and undefined values", () => {
        const left = PersistentHashMap.from<string, number | undefined>([["a", undefined], ["b", 2]]);
        const right = left.remove("a").put("b", 3).put("c", undefined);
        expect(Array.from(left.diff(right)).sort((a, b) => String(a.key).localeCompare(String(b.key)))).toEqual([
            { kind: "removed", key: "a", before: undefined, after: undefined },
            { kind: "changed", key: "b", before: 2, after: 3 },
            { kind: "added", key: "c", before: undefined, after: undefined },
        ]);
    });

    test("random histories agree with a mutable model", () => {
        fc.assert(fc.property(
            fc.array(fc.tuple(fc.integer({ min: -100, max: 100 }), fc.option(fc.integer()), fc.boolean()), { maxLength: 500 }),
            (operations) => {
                let actual = PersistentHashMap.empty<number, number>();
                const expected = new Map<number, number>();
                for (const [key, value, remove] of operations) {
                    if (remove) {
                        actual = actual.remove(key);
                        expected.delete(key);
                    } else {
                        const concrete = value ?? 0;
                        actual = actual.put(key, concrete);
                        expected.set(key, concrete);
                    }
                }
                expect(actual.size).toBe(expected.size);
                for (const [key, value] of expected) expect(actual.get(key)).toBe(value);
            },
        ), { numRuns: 200 });
    });
});

describe("PersistentHashSet", () => {
    test("supports algebra and relation operations", () => {
        const left = PersistentHashSet.from([1, 2, 3]);
        expect(Array.from(left.union([3, 4, 5])).sort()).toEqual([1, 2, 3, 4, 5]);
        expect(Array.from(left.intersect([2, 3, 9])).sort()).toEqual([2, 3]);
        expect(Array.from(left.except([1, 3]))).toEqual([2]);
        expect(Array.from(left.symmetricExcept([3, 4])).sort()).toEqual([1, 2, 4]);
        expect(left.isProperSupersetOf([1, 3])).toBe(true);
        expect(left.isProperSubsetOf([1, 2, 3, 4])).toBe(true);
        expect(left.setEquals([3, 2, 1, 1])).toBe(true);
    });

    test("stored undefined representative remains removable", () => {
        const set = PersistentHashSet.from<undefined | string>([undefined, "a"]);
        expect(set.contains(undefined)).toBe(true);
        const removed = set.tryRemove(undefined)!;
        expect(removed.value).toBeUndefined();
        expect(removed.set.contains(undefined)).toBe(false);
    });
});

describe("ConcurrentHashTrie", () => {
    test("snapshot remains stable and generation counts publications", () => {
        const trie = new ConcurrentHashTrie<string, number>();
        expect(trie.tryAdd("alpha", 1)).toBe(true);
        expect(trie.tryAdd("alpha", 2)).toBe(false);
        const snapshot = trie.snapshot();
        expect(trie.compute("alpha", () => 0, (_key, value) => value + 1)).toBe(2);
        trie.set("beta", 2);
        expect(snapshot.get("alpha")).toBe(1);
        expect(snapshot.get("beta")).toBeUndefined();
        expect(snapshot.toPersistentHashMap().size).toBe(1);
        expect(trie.generation).toBe(3);
    });
});

describe("single-owner transients", () => {
    test("clean and no-op sessions republish the exact persistent source", () => {
        const source = PersistentHashMap.from([["a", 1], ["b", 2]]);
        expect(source.toTransient().persist()).toBe(source);
        const transient = source.toTransient();
        transient.set("a", 1);
        expect(transient.remove("missing")).toBe(false);
        expect(transient.tryAdd("a", 9)).toBe(false);
        expect(transient.persist()).toBe(source);
    });

    test("publishes once and invalidates version-bound enumeration", () => {
        const transient = PersistentHashMap.createTransient<number, number>();
        for (let key = 0; key < 100; key++) transient.set(key, key * 2);
        const iterator = transient[Symbol.iterator]();
        expect(iterator.next().done).toBe(false);
        transient.set(100, 200);
        expect(() => iterator.next()).toThrow(/modified/u);
        const persistent = transient.persist();
        expect(persistent.size).toBe(101);
        expect(() => transient.get(1)).toThrow(TransientConsumedError);
        expect(() => transient.persist()).toThrow(TransientConsumedError);
    });

    test("set transients preserve representatives and immutable snapshots", () => {
        const source = PersistentHashSet.from([1, 2, 3]);
        const transient = source.toTransient();
        expect(transient.add(3)).toBe(false);
        expect(transient.add(4)).toBe(true);
        expect(transient.remove(1)).toBe(true);
        const published = transient.persist();
        expect([...source].sort()).toEqual([1, 2, 3]);
        expect([...published].sort()).toEqual([2, 3, 4]);
    });
});
