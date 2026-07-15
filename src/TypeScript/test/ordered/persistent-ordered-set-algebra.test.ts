import { describe, expect, test } from "vitest";
import { PersistentOrderedSet } from "../../src/ordered/index.js";
import {
    createHashPolicy,
    defaultHash,
    type HashPolicy,
} from "../../src/hamt/index.js";

interface Representative {
    readonly equivalenceClass: number;
    readonly name: string;
}

function representative(equivalenceClass: number, name: string): Representative {
    return { equivalenceClass, name };
}

function classPolicy(): HashPolicy<Representative> {
    return createHashPolicy(
        (item) => item.equivalenceClass,
        (left, right) => left.equivalenceClass === right.equivalenceClass,
    );
}

function identityPolicy(): HashPolicy<Representative> {
    return createHashPolicy(defaultHash, Object.is);
}

function expectExact<T>(expected: readonly T[], actual: PersistentOrderedSet<T>): void {
    expect(actual.toArray()).toHaveLength(expected.length);
    for (let index = 0; index < expected.length; index++) {
        expect(Object.is(actual.getAt(index), expected[index])).toBe(true);
    }
    actual.validateStructure();
}

describe("PersistentOrderedSet algebra", () => {
    test("uses receiver order, receiver representatives, and argument first representatives", () => {
        const policy = classPolicy();
        const receiverA = representative(1, "receiver-a");
        const receiverB = representative(2, "receiver-b");
        const receiverC = representative(3, "receiver-c");
        const argumentA = representative(1, "argument-a");
        const argumentD = representative(4, "argument-d");
        const receiver = PersistentOrderedSet.from([receiverA, receiverB, receiverC], policy);
        const argument = PersistentOrderedSet.from([argumentA, argumentD], policy);

        for (const values of [argument, [argumentA, argumentD]] as const) {
            expectExact([receiverA, receiverB, receiverC, argumentD], receiver.union(values));
            expectExact([receiverA], receiver.intersect(values));
            expectExact([receiverB, receiverC], receiver.except(values));
            expectExact([receiverB, receiverC, argumentD], receiver.symmetricExcept(values));
        }
        expectExact([receiverA, receiverB, receiverC], receiver);
        expectExact([argumentA, argumentD], argument);
    });

    test("normalizes same-type foreign-policy arguments under the receiver policy only", () => {
        const receiverPolicy = classPolicy();
        let foreignHashFails = false;
        let foreignEqualityFails = false;
        const foreignPolicy = createHashPolicy<Representative>(
            (item) => { if (foreignHashFails) throw new Error("foreign hash"); return defaultHash(item); },
            (left, right) => { if (foreignEqualityFails) throw new Error("foreign equality"); return left === right; },
        );
        const receiverItem = representative(1, "receiver");
        const equalArgument = representative(1, "equal-argument");
        const laterEqualArgument = representative(1, "later-equal-argument");
        const newArgument = representative(2, "new-argument");
        const receiver = PersistentOrderedSet.from([receiverItem], receiverPolicy);
        const argument = PersistentOrderedSet.from(
            [equalArgument, laterEqualArgument, newArgument],
            foreignPolicy,
        );
        foreignHashFails = true;
        foreignEqualityFails = true;

        expectExact([receiverItem, newArgument], receiver.union(argument));
        expectExact([receiverItem], receiver.intersect(argument));
        expectExact([], receiver.except(argument));
        expectExact([newArgument], receiver.symmetricExcept(argument));
        expect(receiver.isSubsetOf(argument)).toBe(true);
        expect(receiver.isProperSubsetOf(argument)).toBe(true);
        expect(receiver.isSupersetOf(argument)).toBe(false);
        expect(receiver.isProperSupersetOf(argument)).toBe(false);
        expect(receiver.overlaps(argument)).toBe(true);
        expect(receiver.setEquals(argument)).toBe(false);

        const receiverOnly = representative(9, "receiver-only");
        const union = PersistentOrderedSet.from([receiverOnly], receiverPolicy).union(argument);
        expectExact([receiverOnly, equalArgument, newArgument], union);
        expect(union.tryGetValue(laterEqualArgument)).toEqual({ found: true, value: equalArgument });
        expect(union.policy).toBe(receiverPolicy);
    });

    test("all logical algebra no-ops preserve exact receiver identity after normalization", () => {
        const policy = classPolicy();
        const alpha = representative(1, "alpha");
        const beta = representative(2, "beta");
        const set = PersistentOrderedSet.from([alpha, beta], policy);
        const empty = PersistentOrderedSet.empty(policy);
        const subset = PersistentOrderedSet.from([representative(1, "subset-alpha")], policy);
        const superset = PersistentOrderedSet.from([
            representative(1, "other-alpha"),
            representative(2, "other-beta"),
            representative(3, "gamma"),
        ], policy);
        const disjoint = PersistentOrderedSet.from([representative(9, "disjoint")], policy);

        expect(set.union(set)).toBe(set);
        expect(set.intersect(set)).toBe(set);
        expect(set.union(empty)).toBe(set);
        expect(set.symmetricExcept(empty)).toBe(set);
        expect(set.union(subset)).toBe(set);
        expect(set.intersect(superset)).toBe(set);
        expect(set.except(disjoint)).toBe(set);
        const selfDifference = set.except(set);
        const selfSymmetric = set.symmetricExcept(set);
        expect(selfDifference.isEmpty).toBe(true);
        expect(selfSymmetric.isEmpty).toBe(true);
        expect(selfDifference.policy).toBe(policy);
        expect(selfSymmetric.policy).toBe(policy);
        expect(selfDifference).not.toBe(PersistentOrderedSet.empty<Representative>());
    });

    test("enumerable normalization collapses duplicates and preserves undefined and null", () => {
        const set = PersistentOrderedSet.from<undefined | null | string>(["receiver"]);
        const argument: Array<undefined | null | string> = ["new", "new", undefined, undefined, null, null];
        expectExact(["receiver", "new", undefined, null], set.union(argument));
        expectExact(["receiver", "new", undefined, null], set.symmetricExcept(argument));
    });

    test("every operation normalizes the whole argument before applying a shortcut", () => {
        const source = PersistentOrderedSet.from([1, 2]);
        const failure = new Error("late enumeration failure");
        function* throwing(): Generator<number, void> {
            yield 1;
            throw failure;
        }
        const operations: Array<() => unknown> = [
            () => source.union(throwing()),
            () => source.intersect(throwing()),
            () => source.except(throwing()),
            () => source.symmetricExcept(throwing()),
            () => source.isSubsetOf(throwing()),
            () => source.isProperSubsetOf(throwing()),
            () => source.isSupersetOf(throwing()),
            () => source.isProperSupersetOf(throwing()),
            () => source.overlaps(throwing()),
            () => source.setEquals(throwing()),
        ];
        for (const operation of operations) expect(operation).toThrow(failure);
        expect(source.toArray()).toEqual([1, 2]);
    });

    test("receiver hash and equality failures during normalization are eager and atomic", () => {
        let failHash = false;
        let failEquality = false;
        const policy = createHashPolicy<Representative>(
            (_item) => { if (failHash) throw new Error("receiver hash"); return 0; },
            (left, right) => {
                if (failEquality) throw new Error("receiver equality");
                return left.equivalenceClass === right.equivalenceClass;
            },
        );
        const item = representative(1, "source");
        const source = PersistentOrderedSet.from([item], policy);
        const argument = [representative(2, "argument"), representative(3, "later")];
        const operationFactories = (values: Iterable<Representative>): Array<() => unknown> => [
            () => source.union(values),
            () => source.intersect(values),
            () => source.except(values),
            () => source.symmetricExcept(values),
            () => source.isSubsetOf(values),
            () => source.isProperSubsetOf(values),
            () => source.isSupersetOf(values),
            () => source.isProperSupersetOf(values),
            () => source.overlaps(values),
            () => source.setEquals(values),
        ];

        failHash = true;
        for (const operation of operationFactories(argument)) expect(operation).toThrow("receiver hash");
        failHash = false;
        failEquality = true;
        for (const operation of operationFactories(argument)) expect(operation).toThrow("receiver equality");
        failEquality = false;
        expectExact([item], source);
    });

    test("rejects null operands before doing any work", () => {
        const source = PersistentOrderedSet.from([1]);
        const invalid = null as unknown as Iterable<number>;
        expect(() => source.union(invalid)).toThrow(TypeError);
        expect(() => source.intersect(invalid)).toThrow(TypeError);
        expect(() => source.except(invalid)).toThrow(TypeError);
        expect(() => source.symmetricExcept(invalid)).toThrow(TypeError);
        expect(() => source.isSubsetOf(invalid)).toThrow(TypeError);
        expect(() => source.isProperSubsetOf(invalid)).toThrow(TypeError);
        expect(() => source.isSupersetOf(invalid)).toThrow(TypeError);
        expect(() => source.isProperSupersetOf(invalid)).toThrow(TypeError);
        expect(() => source.overlaps(invalid)).toThrow(TypeError);
        expect(() => source.setEquals(invalid)).toThrow(TypeError);
    });
});

describe("PersistentOrderedSet relations", () => {
    test("all small truth tables match mathematical distinct sets despite duplicates", () => {
        const universe = 5;
        for (let receiverMask = 0; receiverMask < 1 << universe; receiverMask++) {
            const receiverItems = Array.from(
                { length: universe },
                (_, item) => item,
            ).filter((item) => (receiverMask & (1 << item)) !== 0);
            const receiver = PersistentOrderedSet.from(receiverItems);
            for (let argumentMask = 0; argumentMask < 1 << universe; argumentMask++) {
                const distinctArgument = Array.from(
                    { length: universe },
                    (_, item) => item,
                ).filter((item) => (argumentMask & (1 << item)) !== 0);
                const argument = distinctArgument.flatMap((item) => [item, item]);
                const receiverOnly = receiverMask & ~argumentMask;
                const argumentOnly = argumentMask & ~receiverMask;
                expect(receiver.isSubsetOf(argument)).toBe(receiverOnly === 0);
                expect(receiver.isProperSubsetOf(argument)).toBe(receiverOnly === 0 && argumentOnly !== 0);
                expect(receiver.isSupersetOf(argument)).toBe(argumentOnly === 0);
                expect(receiver.isProperSupersetOf(argument)).toBe(argumentOnly === 0 && receiverOnly !== 0);
                expect(receiver.overlaps(argument)).toBe((receiverMask & argumentMask) !== 0);
                expect(receiver.setEquals(argument)).toBe(receiverMask === argumentMask);
            }
        }
    });

    test("proper cardinalities collapse foreign reference classes under the receiver policy", () => {
        const receiverPolicy = classPolicy();
        const receiverRepresentative = representative(1, "receiver");
        const firstForeign = representative(1, "first-foreign");
        const secondForeign = representative(1, "second-foreign");
        const receiver = PersistentOrderedSet.from([receiverRepresentative], receiverPolicy);
        const foreign = PersistentOrderedSet.from([firstForeign, secondForeign], identityPolicy());

        expect(receiver.isSubsetOf(foreign)).toBe(true);
        expect(receiver.isProperSubsetOf(foreign)).toBe(false);
        expect(receiver.isSupersetOf(foreign)).toBe(true);
        expect(receiver.isProperSupersetOf(foreign)).toBe(false);
        expect(receiver.overlaps(foreign)).toBe(true);
        expect(receiver.setEquals(foreign)).toBe(true);
    });
});
