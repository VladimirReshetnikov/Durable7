import { describe, expect, test } from "vitest";
import {
    OrderedSetMissingValueError,
    PersistentOrderedSet,
} from "../../src/ordered/index.js";
import {
    createHashPolicy,
    defaultHashPolicy,
    type HashPolicy,
} from "../../src/hamt/index.js";

interface Representative {
    readonly equivalenceClass: number;
    readonly name: string;
}

function representative(
    equivalenceClass: number,
    name: string,
): Representative {
    return { equivalenceClass, name };
}

function representativePolicy(hashBuckets = 1): HashPolicy<Representative> {
    return createHashPolicy(
        (value) => ((value.equivalenceClass % hashBuckets) + hashBuckets) % hashBuckets,
        (left, right) => left.equivalenceClass === right.equivalenceClass,
    );
}

function expectRepresentatives<T>(
    expected: readonly T[],
    actual: PersistentOrderedSet<T>,
): void {
    expect(actual.size).toBe(expected.length);
    expect(actual.count).toBe(expected.length);
    expect(actual.isEmpty).toBe(expected.length === 0);
    const array = actual.toArray();
    expect(array).toHaveLength(expected.length);
    for (let index = 0; index < expected.length; index++) {
        expect(Object.is(array[index], expected[index])).toBe(true);
        expect(Object.is(actual.getAt(index), expected[index])).toBe(true);
        expect(actual.indexOf(expected[index]!)).toBe(index);
        expect(actual.contains(expected[index]!)).toBe(true);
        const lookup = actual.tryGetValue(expected[index]!);
        expect(lookup.found).toBe(true);
        expect(Object.is(lookup.value, expected[index])).toBe(true);
    }
    if (expected.length === 0) {
        expect(() => actual.first).toThrow(/empty/u);
        expect(() => actual.last).toThrow(/empty/u);
    } else {
        expect(Object.is(actual.first, expected[0])).toBe(true);
        expect(Object.is(actual.last, expected.at(-1))).toBe(true);
    }
    expect(actual.validateStructure()).toEqual({ count: expected.length });
}

describe("PersistentOrderedSet construction and point operations", () => {
    test("canonicalizes only the shared default-policy empty and retains custom policies", () => {
        expect(PersistentOrderedSet.empty<string>()).toBe(PersistentOrderedSet.empty<string>());
        expect(PersistentOrderedSet.create<string>()).toBe(PersistentOrderedSet.empty<string>());
        expect(PersistentOrderedSet.empty(defaultHashPolicy<string>())).toBe(PersistentOrderedSet.empty<string>());
        expect(PersistentOrderedSet.from<string>([])).toBe(PersistentOrderedSet.empty<string>());
        expect(PersistentOrderedSet.createRange<string>([])).toBe(PersistentOrderedSet.empty<string>());

        const policy = representativePolicy();
        const custom = PersistentOrderedSet.empty(policy);
        const customRange = PersistentOrderedSet.from([], policy);
        expect(custom).not.toBe(PersistentOrderedSet.empty<Representative>());
        expect(customRange).not.toBe(PersistentOrderedSet.empty<Representative>());
        expect(custom.policy).toBe(policy);
        expect(customRange.policy).toBe(policy);
        expectRepresentatives([], custom);
        expectRepresentatives([], customRange);
    });

    test("one-pass construction collapses duplicates at first positions and retains first representatives", () => {
        const policy = representativePolicy();
        const firstAlpha = representative(1, "first-alpha");
        const beta = representative(2, "beta");
        const laterAlpha = representative(1, "later-alpha");
        const gamma = representative(3, "gamma");
        const set = PersistentOrderedSet.from(
            [firstAlpha, beta, laterAlpha, gamma, representative(1, "latest-alpha")],
            policy,
        );

        expectRepresentatives([firstAlpha, beta, gamma], set);
        expect(set.tryGetValue(laterAlpha)).toEqual({ found: true, value: firstAlpha });
        expect(set.indexOf(laterAlpha)).toBe(0);
        expect(set.policy).toBe(policy);

        const missing = representative(9, "missing");
        expect(set.tryGetValue(missing)).toEqual({ found: false, value: missing });
        expect(set.indexOf(missing)).toBe(-1);
    });

    test("undefined, null, NaN, and negative zero remain ordinary default-policy representatives", () => {
        const set = PersistentOrderedSet.from<undefined | null | number>([
            undefined,
            null,
            Number.NaN,
            -0,
            undefined,
            Number.NaN,
            0,
        ]);
        expect(set.size).toBe(4);
        expect(set.first).toBeUndefined();
        expect(set.tryGetValue(undefined)).toEqual({ found: true, value: undefined });
        expect(set.tryGetValue(null)).toEqual({ found: true, value: null });
        expect(Number.isNaN(set.tryGetValue(Number.NaN).value)).toBe(true);
        expect(Object.is(set.tryGetValue(0).value, -0)).toBe(true);
        expect(set.moveToLast(undefined).last).toBeUndefined();
        expect(set.remove(undefined).contains(undefined)).toBe(false);
        set.validateStructure();
    });

    test("duplicate addition never hides movement or representative replacement", () => {
        const policy = representativePolicy();
        const first = representative(1, "first");
        const middle = representative(2, "middle");
        const last = representative(3, "last");
        const source = PersistentOrderedSet.from([first, middle, last], policy);
        const duplicate = representative(2, "duplicate");

        expect(source.add(duplicate)).toBe(source);
        expect(source.addFirst(duplicate)).toBe(source);
        expect(source.insert(0, duplicate)).toBe(source);
        expect(source.insert(source.size, duplicate)).toBe(source);
        expectRepresentatives([first, middle, last], source);

        const appended = representative(4, "appended");
        const prepended = representative(5, "prepended");
        const inserted = representative(6, "inserted");
        expectRepresentatives([first, middle, last, appended], source.add(appended));
        expectRepresentatives([prepended, first, middle, last], source.addFirst(prepended));
        expectRepresentatives([first, inserted, middle, last], source.insert(1, inserted));
        expectRepresentatives([first, middle, last], source);
    });

    test("removal shapes preserve order, old versions, miss identity, and policy", () => {
        const policy = representativePolicy();
        const first = representative(1, "first");
        const middle = representative(2, "middle");
        const last = representative(3, "last");
        const source = PersistentOrderedSet.from([first, middle, last], policy);
        const missing = representative(9, "missing");

        expect(source.remove(missing)).toBe(source);
        expect(source.tryRemove(missing)).toEqual({ removed: false, set: source });
        const byTry = source.tryRemove(representative(2, "probe"));
        expect(byTry.removed).toBe(true);
        expectRepresentatives([first, last], byTry.set);
        expectRepresentatives([first, last], source.remove(middle));
        expectRepresentatives([first, last], source.removeAt(1));
        expectRepresentatives([middle, last], source.removeFirst());
        expectRepresentatives([first, middle], source.removeLast());
        expectRepresentatives([first, middle, last], source);

        const cleared = source.clear();
        expect(cleared.policy).toBe(policy);
        expect(cleared).not.toBe(PersistentOrderedSet.empty<Representative>());
        expect(cleared.clear()).toBe(cleared);
        expect(PersistentOrderedSet.from([1]).clear()).toBe(PersistentOrderedSet.empty<number>());
    });

    test("empty endpoints and invalid positions throw without consulting the hash policy", () => {
        const empty = PersistentOrderedSet.empty<number>();
        expect(() => empty.first).toThrow(/empty/u);
        expect(() => empty.last).toThrow(/empty/u);
        expect(() => empty.removeFirst()).toThrow(/empty/u);
        expect(() => empty.removeLast()).toThrow(/empty/u);
        expect(() => empty.moveToFirst(1)).toThrow(OrderedSetMissingValueError);
        expect(() => empty.moveToLast(1)).toThrow(OrderedSetMissingValueError);
        expect(() => empty.moveTo(0, 1)).toThrow(RangeError);

        let hashCalls = 0;
        let equalityCalls = 0;
        const value = representative(1, "value");
        const source = PersistentOrderedSet.from([value], representativePolicy());
        const callbacks = createHashPolicy<Representative>(
            () => { hashCalls++; return 0; },
            () => { equalityCalls++; return true; },
        );
        const guarded = PersistentOrderedSet.from([value], callbacks);
        hashCalls = 0;
        equalityCalls = 0;
        expect(() => guarded.insert(-1, value)).toThrow(RangeError);
        expect(() => guarded.insert(2, value)).toThrow(RangeError);
        expect(() => guarded.moveTo(-1, value)).toThrow(RangeError);
        expect(() => guarded.moveTo(1, value)).toThrow(RangeError);
        expect(() => guarded.getRange(-1, 0)).toThrow(RangeError);
        expect(() => guarded.getRange(0, 2)).toThrow(RangeError);
        expect(() => guarded.take(2)).toThrow(RangeError);
        expect(() => guarded.drop(2)).toThrow(RangeError);
        expect(hashCalls).toBe(0);
        expect(equalityCalls).toBe(0);
        expectRepresentatives([value], guarded);
        expectRepresentatives([value], source);
    });

    test("construction rejects null and propagates late enumeration failure", () => {
        expect(() => PersistentOrderedSet.from(null as unknown as Iterable<number>)).toThrow(TypeError);
        let iteratorCreations = 0;
        const oneShot: Iterable<number> = {
            *[Symbol.iterator](): Iterator<number> {
                iteratorCreations++;
                yield 1;
                yield 2;
                yield 1;
            },
        };
        expect(PersistentOrderedSet.from(oneShot).toArray()).toEqual([1, 2]);
        expect(iteratorCreations).toBe(1);

        const failure = new Error("late enumeration");
        function* throwing(): Generator<number, void> {
            yield 1;
            yield 2;
            throw failure;
        }
        expect(() => PersistentOrderedSet.from(throwing())).toThrow(failure);
    });

    test("point-operation hash and collision-equality failures are source-atomic", () => {
        let failHash = false;
        let failEquality = false;
        const failure = new Error("policy failure");
        const policy = createHashPolicy<Representative>(
            (_item) => { if (failHash) throw failure; return 0; },
            (left, right) => {
                if (failEquality) throw failure;
                return left.equivalenceClass === right.equivalenceClass;
            },
        );
        const first = representative(1, "first");
        const second = representative(2, "second");
        const source = PersistentOrderedSet.from([first, second], policy);
        const missing = representative(99, "missing");
        const pointOperations: Array<() => unknown> = [
            () => source.contains(missing),
            () => source.indexOf(missing),
            () => source.add(missing),
            () => source.addFirst(missing),
            () => source.insert(1, missing),
            () => source.remove(missing),
            () => source.moveToFirst(missing),
            () => source.moveToLast(missing),
        ];

        failHash = true;
        for (const operation of pointOperations) expect(operation).toThrow(failure);
        failHash = false;
        failEquality = true;
        for (const operation of pointOperations) expect(operation).toThrow(failure);
        failEquality = false;
        expectRepresentatives([first, second], source);

        const empty = PersistentOrderedSet.empty(policy);
        const single = PersistentOrderedSet.from([first], policy);
        failHash = true;
        expect(empty.clear()).toBe(empty);
        expect(empty.reverse()).toBe(empty);
        expect(single.reverse()).toBe(single);
        expect(empty.sort(() => { throw failure; })).toBe(empty);
        expect(single.sort(() => { throw failure; })).toBe(single);
        failHash = false;
    });
});

describe("PersistentOrderedSet movement, ranges, and ordering", () => {
    test("all small movement pairs interpret the destination as a final result index", () => {
        for (let size = 1; size <= 9; size++) {
            const source = PersistentOrderedSet.from(Array.from({ length: size }, (_, index) => index));
            for (let oldIndex = 0; oldIndex < size; oldIndex++) {
                for (let finalIndex = 0; finalIndex < size; finalIndex++) {
                    const expected = Array.from({ length: size }, (_, index) => index);
                    const [item] = expected.splice(oldIndex, 1);
                    expected.splice(finalIndex, 0, item!);
                    const actual = source.moveTo(finalIndex, oldIndex);
                    expect(actual.toArray()).toEqual(expected);
                    expect(oldIndex === finalIndex ? actual === source : actual !== source).toBe(true);
                    actual.validateStructure();
                }
            }
        }
    });

    test("end movement uses stored representatives and identity fast paths", () => {
        const policy = representativePolicy();
        const first = representative(1, "first");
        const middle = representative(2, "middle");
        const last = representative(3, "last");
        const source = PersistentOrderedSet.from([first, middle, last], policy);
        expect(source.moveToFirst(representative(1, "probe"))).toBe(source);
        expect(source.moveToLast(representative(3, "probe"))).toBe(source);
        expectRepresentatives([middle, first, last], source.moveToFirst(representative(2, "probe")));
        expectRepresentatives([first, last, middle], source.moveToLast(representative(2, "probe")));
        expect(() => source.moveToFirst(representative(99, "missing"))).toThrow(OrderedSetMissingValueError);
    });

    test("repeated same-position insertions and sibling relabel histories preserve snapshots", () => {
        let source = PersistentOrderedSet.from([-2, -1]);
        for (let value = 0; value < 70; value++) {
            source = source.insert(1, value);
            source.validateStructure();
        }
        const sourceExpected = source.toArray();
        let left = source;
        let right = source;
        for (let value = 1_000; value < 1_080; value++) left = left.insert(1, value);
        for (let value = 2_000; value < 2_080; value++) right = right.insert(1, value);
        expect(source.toArray()).toEqual(sourceExpected);
        expect(left.size).toBe(152);
        expect(right.size).toBe(152);
        expect(left.getAt(1)).toBe(1_079);
        expect(right.getAt(1)).toBe(2_079);
        expect(left.contains(2_079)).toBe(false);
        expect(right.contains(1_079)).toBe(false);
        left.validateStructure();
        right.validateStructure();
    });

    test("every small valid range matches the model and boundary identities", () => {
        const policy = representativePolicy();
        const items = Array.from({ length: 12 }, (_, index) => representative(index, `item-${String(index)}`));
        const source = PersistentOrderedSet.from(items, policy);
        for (let index = 0; index <= source.size; index++) {
            for (let count = 0; count <= source.size - index; count++) {
                const actual = source.getRange(index, count);
                expectRepresentatives(items.slice(index, index + count), actual);
                expect(actual.policy).toBe(policy);
                if (index === 0 && count === source.size) expect(actual).toBe(source);
            }
        }
        expect(source.take(source.size)).toBe(source);
        expect(source.drop(0)).toBe(source);
        expectRepresentatives(items.slice(0, 3), source.take(3));
        expectRepresentatives(items.slice(2), source.drop(2));
        expect(source.take(0).policy).toBe(policy);
        expect(source.drop(source.size).policy).toBe(policy);
        expect(() => source.getRange(1, Number.MAX_SAFE_INTEGER)).toThrow(RangeError);
        expect(() => source.take(-1)).toThrow(RangeError);
        expect(() => source.drop(source.size + 1)).toThrow(RangeError);
    });

    test("reverse and stable one-shot sort preserve representatives, policy, and no-op identity", () => {
        expect(PersistentOrderedSet.empty<number>().reverse()).toBe(PersistentOrderedSet.empty<number>());
        const single = PersistentOrderedSet.from([1]);
        expect(single.reverse()).toBe(single);
        const policy = representativePolicy();
        const items = [
            representative(4, "four"),
            representative(1, "one"),
            representative(7, "seven"),
            representative(2, "two"),
            representative(5, "five"),
        ];
        const source = PersistentOrderedSet.from(items, policy);
        expectRepresentatives(items.toReversed(), source.reverse());
        const byRemainder = (left: Representative, right: Representative): number =>
            left.equivalenceClass % 3 - right.equivalenceClass % 3;
        const expected = items
            .map((item, index) => ({ item, index }))
            .toSorted((left, right) => byRemainder(left.item, right.item) || left.index - right.index)
            .map(({ item }) => item);
        const sorted = source.sort(byRemainder);
        expectRepresentatives(expected, sorted);
        expect(sorted.policy).toBe(policy);
        expect(sorted.sort(byRemainder)).toBe(sorted);
        const appended = representative(10, "later");
        expectRepresentatives([...expected, appended], sorted.add(appended));
        expect(source.sort(() => 0)).toBe(source);
        expectRepresentatives(items, source);
    });

    test("sort and derived-index failures never mutate the source", () => {
        let failHash = false;
        const policy = createHashPolicy<Representative>(
            (value) => { if (failHash) throw new Error("hash failure"); return value.equivalenceClass & 3; },
            (left, right) => left.equivalenceClass === right.equivalenceClass,
        );
        const items = [representative(3, "three"), representative(1, "one"), representative(2, "two")];
        const source = PersistentOrderedSet.from(items, policy);
        const wideItems = Array.from(
            { length: 12 },
            (_, index) => representative(index + 100, `wide-${String(index)}`),
        );
        const wideSource = PersistentOrderedSet.from(wideItems, policy);
        const orderingFailure = new Error("ordering failure");
        expect(() => source.sort(() => { throw orderingFailure; })).toThrow(orderingFailure);
        expectRepresentatives(items, source);

        failHash = true;
        expect(() => source.sort((left, right) => left.equivalenceClass - right.equivalenceClass)).toThrow("hash failure");
        expect(() => source.reverse()).toThrow("hash failure");
        expect(() => source.getRange(1, 1)).toThrow("hash failure");
        expect(() => wideSource.getRange(1, 10)).toThrow("hash failure");
        failHash = false;
        expectRepresentatives(items, source);
        expectRepresentatives(wideItems, wideSource);
    });

    test("iteration and array copies are ordered, independent, and version-bound", async () => {
        const source = PersistentOrderedSet.from(Array.from({ length: 1_000 }, (_, index) => index));
        const iterator = source[Symbol.iterator]();
        expect(iterator.next()).toEqual({ done: false, value: 0 });
        const successor = source.moveToFirst(999).add(1_000);
        expect(Array.from(iterator).slice(0, 3)).toEqual([1, 2, 3]);
        const firstCopy = source.toArray();
        const secondCopy = source.toArray();
        expect(firstCopy).not.toBe(secondCopy);
        firstCopy[0] = -1;
        expect(source.first).toBe(0);

        await Promise.all(Array.from({ length: 16 }, async (): Promise<void> => {
            expect(source.toArray()).toEqual(Array.from({ length: 1_000 }, (_, index) => index));
            expect(successor.first).toBe(999);
            expect(successor.last).toBe(1_000);
        }));
    });
});
