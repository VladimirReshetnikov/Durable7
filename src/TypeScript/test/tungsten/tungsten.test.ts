import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { createHashPolicy } from "../../src/hamt/index.js";
import { PersistentAssociation, PersistentList } from "../../src/tungsten/index.js";

describe("PersistentList", () => {
    test("supports the Tungsten list vocabulary", () => {
        const basis = PersistentList.from([1, 2, 3]);
        expect(basis.prepend(0).append(4).toArray()).toEqual([0, 1, 2, 3, 4]);
        expect(basis.insertRange(1, [8, 9])!.toArray()).toEqual([1, 8, 9, 2, 3]);
        expect(basis.removeRange(1, 1)!.toArray()).toEqual([1, 3]);
        expect(basis.takeLast(2)!.toArray()).toEqual([2, 3]);
        expect(basis.dropLast(2)!.toArray()).toEqual([1]);
        expect(basis.reverse().toArray()).toEqual([3, 2, 1]);
    });
});

describe("PersistentAssociation", () => {
    test("set preserves position; append/prepend move existing keys", () => {
        const basis = PersistentAssociation.from([["a", 1], ["b", 2], ["c", 3]]);
        expect(basis.setItem("b", 20).toArray()).toEqual([["a", 1], ["b", 20], ["c", 3]]);
        expect(basis.append("a", 10).toArray()).toEqual([["b", 2], ["c", 3], ["a", 10]]);
        expect(basis.prepend("c", 30).toArray()).toEqual([["c", 30], ["a", 1], ["b", 2]]);
        expect(basis.insert(2, "a", 10)!.toArray()).toEqual([["b", 2], ["a", 10], ["c", 3]]);
    });

    test("retains stored representatives under custom key policies", () => {
        interface Key { readonly text: string; readonly id: number }
        const policy = createHashPolicy<Key>((key) => key.text.length, (left, right) => left.text === right.text);
        const original: Key = { text: "x", id: 1 };
        const probe: Key = { text: "x", id: 2 };
        const association = PersistentAssociation.empty<Key, number>(policy).setItem(original, 1).setItem(probe, 2);
        expect(association.size).toBe(1);
        expect(association.getStoredKey(probe)).toBe(original);
        expect(association.first()).toEqual([original, 2]);
    });

    test("relabels exhausted midpoint gaps without changing order", () => {
        let association = PersistentAssociation.from([[0, 0], [1, 1]]);
        const model: Array<readonly [number, number]> = [[0, 0], [1, 1]];
        for (let key = 2; key < 500; key++) { association = association.insert(1, key, key)!; model.splice(1, 0, [key, key]); }
        expect(association.toArray()).toEqual(model);
        expect(association.size).toBe(500);
    });

    test("generated histories agree with ordered map models", () => {
        fc.assert(fc.property(fc.array(fc.tuple(fc.integer({ min: 0, max: 30 }), fc.integer(), fc.integer({ min: 0, max: 3 })), { maxLength: 300 }), (commands) => {
            let actual = PersistentAssociation.empty<number, number>();
            const model: Array<[number, number]> = [];
            for (const [key, value, operation] of commands) {
                const existing = model.findIndex((entry) => entry[0] === key);
                if (operation === 0) {
                    actual = actual.setItem(key, value);
                    if (existing < 0) model.push([key, value]); else model[existing] = [key, value];
                } else if (operation === 1) {
                    actual = actual.append(key, value);
                    if (existing >= 0) model.splice(existing, 1);
                    model.push([key, value]);
                } else if (operation === 2) {
                    actual = actual.prepend(key, value);
                    if (existing >= 0) model.splice(existing, 1);
                    model.unshift([key, value]);
                } else {
                    actual = actual.remove(key);
                    if (existing >= 0) model.splice(existing, 1);
                }
            }
            expect(actual.toArray()).toEqual(model);
        }), { numRuns: 100 });
    });
});
