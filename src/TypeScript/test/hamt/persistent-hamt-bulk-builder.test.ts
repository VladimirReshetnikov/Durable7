/**
 * Tests for the construction-only bulk builder, including that a published collection is detached
 * from the builder's nodes.
 */
import fc from "fast-check";
import { describe, expect, test } from "vitest";
import {
    HashMapBulkBuilder,
    PersistentHashMap,
    createHashPolicy,
} from "../../src/hamt/index.js";

describe("HashMapBulkBuilder", () => {
    test("retains policy, first key representative, and last distinct value", () => {
        interface Key { readonly text: string; readonly identity: number }
        const policy = createHashPolicy<Key>(
            (key) => key.text.toLowerCase().charCodeAt(0),
            (left, right) => left.text.toLowerCase() === right.text.toLowerCase(),
        );
        const first: Key = { text: "Alpha", identity: 1 };
        const equivalent: Key = { text: "ALPHA", identity: 2 };
        const retainedValue = { identity: 1 };
        const replacementValue = { identity: 2 };
        const builder = PersistentHashMap.createBulkBuilder<Key, object>(policy);

        expect(builder.isEmpty).toBe(true);
        expect(builder.policy).toBe(policy);
        expect("addOrUpdate" in builder).toBe(false);
        expect(builder.setItem(first, retainedValue)).toBe(builder);
        builder.setItem(equivalent, retainedValue);
        expect(builder.size).toBe(1);
        expect(builder.toImmutable().getEntry(equivalent)).toEqual({
            key: first,
            value: retainedValue,
        });
        builder.setItem(equivalent, replacementValue);
        expect(builder.toImmutable().getEntry(equivalent)).toEqual({
            key: first,
            value: replacementValue,
        });
    });

    test("publishes reusable detached snapshots that later edits cannot mutate", () => {
        const builder = new HashMapBulkBuilder<number, object>();
        const values = Array.from({ length: 256 }, (_, key) => ({ key }));
        builder.setItems(values.map((value, key): readonly [number, object] => [key, value]));

        const first = builder.toImmutable();
        const second = builder.toImmutable();
        expect(first).not.toBe(second);
        expect(first.sharesRootWith(second)).toBe(false);
        expect(first.policy).toBe(builder.policy);
        expect(first.get(128)).toBe(values[128]);

        const replacement = { key: -1 };
        builder.setItem(128, replacement).setItem(256, replacement);
        const third = builder.toImmutable();
        expect(first.get(128)).toBe(values[128]);
        expect(first.containsKey(256)).toBe(false);
        expect(third.get(128)).toBe(replacement);
        expect(third.get(256)).toBe(replacement);
        expect(first.sharesRootWith(third)).toBe(false);
        expect(second.sharesRootWith(third)).toBe(false);
    });

    test("empty freezes preserve custom policy and invalid input leaves the builder reusable", () => {
        const policy = createHashPolicy<number>(() => 0, (left, right) => left === right);
        const builder = new HashMapBulkBuilder<number, number>(policy);
        const first = builder.toImmutable();
        const second = builder.toImmutable();
        expect(first.policy).toBe(policy);
        expect(second.policy).toBe(policy);
        expect(first).not.toBe(second);
        expect(() => builder.setItems(null as unknown as Iterable<readonly [number, number]>))
            .toThrow(TypeError);
        builder.setItem(1, 2);
        expect(builder.toImmutable().get(1)).toBe(2);
    });

    test("handles the final two hash bits and full-hash collisions without shift wraparound", () => {
        interface Key { readonly identity: number; readonly hash: number }
        let hashCalls = 0;
        const policy = createHashPolicy<Key>(
            (key) => {
                hashCalls++;
                return key.hash;
            },
            (left, right) => left.identity === right.identity,
        );
        const keys: readonly Key[] = [
            { identity: 0, hash: 0x0000_0000 },
            { identity: 1, hash: 0x4000_0000 },
            { identity: 2, hash: 0x8000_0000 },
            { identity: 3, hash: 0xc000_0000 },
            { identity: 4, hash: 0x0000_0000 },
        ];
        const builder = new HashMapBulkBuilder<Key, number>(policy);
        keys.forEach((key, value) => builder.setItem(key, value));
        expect(hashCalls).toBe(keys.length);

        hashCalls = 0;
        const snapshot = builder.toImmutable();
        expect(hashCalls).toBe(0);
        expect(snapshot.size).toBe(keys.length);
        keys.forEach((key, value) => expect(snapshot.get(key)).toBe(value));

        const replacementProbe: Key = { identity: 3, hash: 0xc000_0000 };
        builder.setItem(replacementProbe, 99);
        const updated = builder.toImmutable();
        expect(updated.get(replacementProbe)).toBe(99);
        expect(updated.getEntry(replacementProbe)?.key).toBe(keys[3]);
        expect(snapshot.get(keys[3] as Key)).toBe(3);
    });

    test("generated staged histories match last-value-wins Map semantics", () => {
        fc.assert(fc.property(
            fc.array(fc.tuple(fc.integer({ min: -100, max: 100 }), fc.integer()), {
                maxLength: 500,
            }),
            (items) => {
                const builder = new HashMapBulkBuilder<number, number>();
                const model = new Map<number, number>();
                for (const [key, value] of items) {
                    builder.setItem(key, value);
                    model.set(key, value);
                }
                const first = builder.toImmutable();
                const second = builder.toImmutable();
                expect(builder.size).toBe(model.size);
                expect(first.size).toBe(model.size);
                expect(first.sharesRootWith(second)).toBe(first.isEmpty);
                for (const [key, value] of model) {
                    expect(first.get(key)).toBe(value);
                    expect(second.get(key)).toBe(value);
                }
            },
        ), { numRuns: 120 });
    });
});
