/**
 * Tests for the counted multiset: the per-class and total counting domains, receiver-policy
 * algebra, and stored-representative retention.
 */
import fc from "fast-check";
import { describe, expect, test } from "vitest";
import { PersistentHashBag, createHashPolicy } from "../../src/hamt/index.js";

const maximumMultiplicity = 0x7fff_ffff;

describe("PersistentHashBag", () => {
    test("keeps the deliberately narrow multiset surface", () => {
        const names = new Set(Object.getOwnPropertyNames(PersistentHashBag.prototype));
        for (const forbidden of [
            "size",
            "toTransient",
            "toBuilder",
            "symmetricExcept",
            "setEquals",
            "validateStructure",
        ]) {
            expect(names.has(forbidden)).toBe(false);
        }
    });

    test("aggregates occurrences, retains first representatives, and aligns all views", () => {
        interface Item { readonly text: string; readonly identity: number }
        const policy = createHashPolicy<Item>(
            () => 0,
            (left, right) => left.text.toLowerCase() === right.text.toLowerCase(),
        );
        const alpha: Item = { text: "Alpha", identity: 1 };
        const equivalentAlpha: Item = { text: "ALPHA", identity: 2 };
        const beta: Item = { text: "Beta", identity: 3 };
        const bag = PersistentHashBag.from(
            [alpha, equivalentAlpha, beta, alpha, beta],
            policy,
        );

        expect(bag.distinctCount).toBe(2);
        expect(bag.totalCount).toBe(5n);
        expect(bag.isEmpty).toBe(false);
        expect(bag.policy).toBe(policy);
        expect(bag.countOf(equivalentAlpha)).toBe(3);
        expect(bag.tryGetValue(equivalentAlpha)).toEqual({ found: true, value: alpha });
        expect(bag.tryGetValue({ text: "missing", identity: 9 })).toEqual({
            found: false,
            value: { text: "missing", identity: 9 },
        });

        const entries = Array.from(bag.entries());
        expect(Array.from(bag.distinctItems())).toEqual(entries.map((entry) => entry.key));
        expect(Array.from(bag)).toEqual(entries.flatMap(
            (entry) => Array<Item>(entry.value).fill(entry.key),
        ));
        expect(bag.toArray()).toEqual(Array.from(bag));
    });

    test("presence-discriminated lookup supports an undefined representative", () => {
        const bag = PersistentHashBag.from<undefined | string>([undefined, "x", undefined]);
        const found = bag.tryGetValue(undefined);
        expect(found.found).toBe(true);
        expect(found.value).toBeUndefined();
        expect(bag.countOf(undefined)).toBe(2);
        expect(bag.tryGetValue("missing")).toEqual({ found: false, value: "missing" });
        expect(bag.removeAll(undefined).contains(undefined)).toBe(false);
    });

    test("point updates saturate removal and preserve no-op identity and snapshots", () => {
        const empty = PersistentHashBag.empty<string>();
        const one = empty.add("alpha");
        const four = one.addCopies("alpha", 3);
        const mixed = four.addCopies("beta", 2);
        const reduced = mixed.removeCopies("alpha", 2);

        expect(empty.isEmpty).toBe(true);
        expect(one.countOf("alpha")).toBe(1);
        expect(four.countOf("alpha")).toBe(4);
        expect(mixed.totalCount).toBe(6n);
        expect(reduced.countOf("alpha")).toBe(2);
        expect(reduced.removeCopies("alpha", 99).contains("alpha")).toBe(false);
        expect(mixed.removeCopies("missing", 2)).toBe(mixed);
        expect(mixed.removeAll("missing")).toBe(mixed);
        expect(mixed.addCopies("alpha", 0)).toBe(mixed);
        expect(mixed.removeCopies("alpha", 0)).toBe(mixed);
        expect(empty.clear()).toBe(empty);
        expect(mixed.clear().policy).toBe(mixed.policy);
        expect(mixed.countOf("alpha")).toBe(4);
        expect(mixed.countOf("beta")).toBe(2);
    });

    test("validates copy counts before hashing and enforces C# multiplicity boundaries", () => {
        let hashCalls = 0;
        const policy = createHashPolicy<string>(
            (value) => {
                hashCalls++;
                return value.length;
            },
            (left, right) => left === right,
        );
        const bag = PersistentHashBag.empty<string>(policy);
        for (const invalid of [-1, 0.5, Number.NaN, Number.POSITIVE_INFINITY, 0x8000_0000]) {
            expect(() => bag.addCopies("x", invalid)).toThrow(RangeError);
            expect(() => bag.removeCopies("x", invalid)).toThrow(RangeError);
        }
        expect(hashCalls).toBe(0);
        expect(bag.addCopies("x", 0)).toBe(bag);
        expect(bag.removeCopies("x", 0)).toBe(bag);
        expect(hashCalls).toBe(0);

        const maximum = bag.addCopies("x", maximumMultiplicity);
        expect(maximum.countOf("x")).toBe(maximumMultiplicity);
        expect(maximum.totalCount).toBe(BigInt(maximumMultiplicity));
        expect(() => maximum.add("x")).toThrow(RangeError);
        expect(maximum.countOf("x")).toBe(maximumMultiplicity);
        expect(() => maximum.sum(maximum)).toThrow(RangeError);
        expect(maximum.countOf("x")).toBe(maximumMultiplicity);
    });

    test("toArray rejects oversized expanded totals before allocating", () => {
        const large = PersistentHashBag.empty<string>()
            .addCopies("alpha", maximumMultiplicity)
            .addCopies("beta", maximumMultiplicity)
            .addCopies("gamma", maximumMultiplicity);
        expect(large.totalCount).toBe(3n * BigInt(maximumMultiplicity));
        expect(() => large.toArray()).toThrow(/maximum array length/u);
        expect(Array.from(large.entries()).reduce(
            (total, entry) => total + BigInt(entry.value),
            0n,
        )).toBe(large.totalCount);
    });

    test("same-policy algebra uses conventional multiplicities and identity rules", () => {
        const left = PersistentHashBag.empty<number>()
            .addCopies(1, 2)
            .addCopies(2, 5)
            .addCopies(3, 1);
        const right = PersistentHashBag.empty<number>()
            .addCopies(1, 3)
            .addCopies(2, 1)
            .addCopies(4, 4);

        const union = left.union(right);
        const intersection = left.intersect(right);
        const difference = left.except(right);
        const sum = left.sum(right);
        expect([1, 2, 3, 4].map((value) => union.countOf(value))).toEqual([3, 5, 1, 4]);
        expect([1, 2, 3, 4].map((value) => intersection.countOf(value))).toEqual([2, 1, 0, 0]);
        expect([1, 2, 3, 4].map((value) => difference.countOf(value))).toEqual([0, 4, 1, 0]);
        expect([1, 2, 3, 4].map((value) => sum.countOf(value))).toEqual([5, 6, 1, 4]);
        expect(left.union(left)).toBe(left);
        expect(left.intersect(left)).toBe(left);
        expect(left.except(left).isEmpty).toBe(true);
        expect(left.except(left).policy).toBe(left.policy);
        expect(left.sum(left).countOf(2)).toBe(10);
        expect(left.union(PersistentHashBag.empty(left.policy))).toBe(left);
        expect(left.except(PersistentHashBag.empty(left.policy))).toBe(left);
    });

    test("mismatched policies normalize eagerly and preserve receiver representatives", () => {
        interface Item { readonly text: string; readonly identity: number }
        const receiverPolicy = createHashPolicy<Item>(
            () => 0,
            (left, right) => left.text.toLowerCase() === right.text.toLowerCase(),
        );
        const receiverAlpha: Item = { text: "Alpha", identity: 1 };
        const argumentAlpha: Item = { text: "ALPHA", identity: 2 };
        const argumentAlpha2: Item = { text: "alpha", identity: 3 };
        const argumentBeta: Item = { text: "Beta", identity: 4 };
        const argumentBeta2: Item = { text: "BETA", identity: 5 };
        const receiver = PersistentHashBag.empty<Item>(receiverPolicy)
            .addCopies(receiverAlpha, 4);
        const argument = PersistentHashBag.empty<Item>()
            .addCopies(argumentAlpha, 2)
            .addCopies(argumentAlpha2, 3)
            .addCopies(argumentBeta, 1)
            .addCopies(argumentBeta2, 2);
        const firstObservedBeta = Array.from(argument.entries())
            .find((entry) => entry.key.text.toLowerCase() === "beta")?.key;

        const union = receiver.union(argument);
        const intersection = receiver.intersect(argument);
        const difference = receiver.except(argument);
        const sum = receiver.sum(argument);
        expect(union.policy).toBe(receiverPolicy);
        expect(union.countOf(argumentAlpha)).toBe(5);
        expect(union.tryGetValue(argumentAlpha).value).toBe(receiverAlpha);
        expect(union.countOf(argumentBeta)).toBe(3);
        expect(union.tryGetValue(argumentBeta).value).toBe(firstObservedBeta);
        expect(intersection).toBe(receiver);
        expect(difference.isEmpty).toBe(true);
        expect(difference.policy).toBe(receiverPolicy);
        expect(sum.countOf(argumentAlpha)).toBe(9);
        expect(sum.tryGetValue(argumentAlpha).value).toBe(receiverAlpha);
        expect(sum.countOf(argumentBeta)).toBe(3);
        expect(receiver.countOf(receiverAlpha)).toBe(4);
        expect(argument.totalCount).toBe(8n);
    });

    test("mismatched-policy collapse overflow and policy failures are eager for all algebra", () => {
        interface Item { readonly text: string }
        let throwFromHash = false;
        const receiverPolicy = createHashPolicy<Item>(
            () => {
                if (throwFromHash) throw new Error("hash failed");
                return 0;
            },
            (left, right) => left.text.toLowerCase() === right.text.toLowerCase(),
        );
        const receiver = PersistentHashBag.empty<Item>(receiverPolicy);
        const argument = PersistentHashBag.empty<Item>()
            .addCopies({ text: "Alpha" }, maximumMultiplicity)
            .add({ text: "ALPHA" });
        const operations = [
            (left: PersistentHashBag<Item>, right: PersistentHashBag<Item>) => left.union(right),
            (left: PersistentHashBag<Item>, right: PersistentHashBag<Item>) => left.intersect(right),
            (left: PersistentHashBag<Item>, right: PersistentHashBag<Item>) => left.except(right),
            (left: PersistentHashBag<Item>, right: PersistentHashBag<Item>) => left.sum(right),
        ];
        for (const operation of operations) expect(() => operation(receiver, argument)).toThrow(RangeError);

        const smallArgument = PersistentHashBag.from<Item>([{ text: "Beta" }]);
        throwFromHash = true;
        for (const operation of operations) {
            expect(() => operation(receiver, smallArgument)).toThrow(/hash failed/u);
        }
        throwFromHash = false;
        expect(receiver.isEmpty).toBe(true);
        expect(argument.countOf({ text: "Alpha" })).toBe(0);
        expect(argument.totalCount).toBe(BigInt(maximumMultiplicity) + 1n);
    });

    test("generated update histories match a mutable multiset and retain snapshots", () => {
        fc.assert(fc.property(
            fc.array(fc.record({
                key: fc.integer({ min: -20, max: 20 }),
                copies: fc.integer({ min: 0, max: 5 }),
                action: fc.integer({ min: 0, max: 3 }),
            }), { maxLength: 250 }),
            (operations) => {
                let actual = PersistentHashBag.empty<number>();
                const model = new Map<number, number>();
                const snapshots: Array<readonly [PersistentHashBag<number>, Map<number, number>]> = [];
                for (let index = 0; index < operations.length; index++) {
                    const operation = operations[index];
                    if (operation === undefined) continue;
                    if (index % 53 === 0) snapshots.push([actual, new Map(model)]);
                    const previous = model.get(operation.key) ?? 0;
                    switch (operation.action) {
                        case 0:
                            actual = actual.add(operation.key);
                            model.set(operation.key, previous + 1);
                            break;
                        case 1:
                            actual = actual.addCopies(operation.key, operation.copies);
                            if (operation.copies !== 0) {
                                model.set(operation.key, previous + operation.copies);
                            }
                            break;
                        case 2: {
                            actual = actual.removeCopies(operation.key, operation.copies);
                            const remaining = Math.max(0, previous - operation.copies);
                            if (remaining === 0) model.delete(operation.key);
                            else model.set(operation.key, remaining);
                            break;
                        }
                        case 3:
                            actual = actual.removeAll(operation.key);
                            model.delete(operation.key);
                            break;
                    }
                }
                const assertMatches = (
                    bag: PersistentHashBag<number>,
                    expected: ReadonlyMap<number, number>,
                ): void => {
                    expect(bag.distinctCount).toBe(expected.size);
                    expect(bag.totalCount).toBe(
                        Array.from(expected.values()).reduce((sum, count) => sum + BigInt(count), 0n),
                    );
                    for (let key = -20; key <= 20; key++) {
                        expect(bag.countOf(key)).toBe(expected.get(key) ?? 0);
                    }
                    expect(Array.from(bag).length).toBe(Number(bag.totalCount));
                };
                assertMatches(actual, model);
                for (const [snapshot, expected] of snapshots) assertMatches(snapshot, expected);
            },
        ), { numRuns: 120 });
    });

    test("generated algebra agrees with occurrence-count models", () => {
        fc.assert(fc.property(
            fc.array(fc.integer({ min: -10, max: 10 }), { maxLength: 100 }),
            fc.array(fc.integer({ min: -10, max: 10 }), { maxLength: 100 }),
            (leftValues, rightValues) => {
                const left = PersistentHashBag.from(leftValues);
                const right = PersistentHashBag.from(rightValues);
                const leftCounts = new Map<number, number>();
                const rightCounts = new Map<number, number>();
                for (const value of leftValues) leftCounts.set(value, (leftCounts.get(value) ?? 0) + 1);
                for (const value of rightValues) rightCounts.set(value, (rightCounts.get(value) ?? 0) + 1);
                const union = left.union(right);
                const intersection = left.intersect(right);
                const difference = left.except(right);
                const sum = left.sum(right);
                for (let value = -10; value <= 10; value++) {
                    const leftCount = leftCounts.get(value) ?? 0;
                    const rightCount = rightCounts.get(value) ?? 0;
                    expect(union.countOf(value)).toBe(Math.max(leftCount, rightCount));
                    expect(intersection.countOf(value)).toBe(Math.min(leftCount, rightCount));
                    expect(difference.countOf(value)).toBe(Math.max(0, leftCount - rightCount));
                    expect(sum.countOf(value)).toBe(leftCount + rightCount);
                }
            },
        ), { numRuns: 100 });
    });
});
