import { readFileSync } from "node:fs";
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { PersistentOrderedSet } from "../../src/ordered/index.js";
import { createHashPolicy } from "../../src/hamt/index.js";

interface Item {
    readonly equivalenceClass: number;
    readonly identity: number;
}

function modulo(value: number, modulus: number): number {
    return ((value % modulus) + modulus) % modulus;
}

function findIndex(items: readonly Item[], probe: Item): number {
    return items.findIndex((item) => item.equivalenceClass === probe.equivalenceClass);
}

function normalize(items: readonly Item[]): Item[] {
    const result: Item[] = [];
    for (const item of items) if (findIndex(result, item) < 0) result.push(item);
    return result;
}

function assertState(
    expected: readonly Item[],
    actual: PersistentOrderedSet<Item>,
): void {
    expect(actual.size).toBe(expected.length);
    expect(actual.toArray()).toHaveLength(expected.length);
    for (let index = 0; index < expected.length; index++) {
        const item = expected[index]!;
        const probe: Item = { equivalenceClass: item.equivalenceClass, identity: -1 };
        expect(actual.getAt(index)).toBe(item);
        expect(actual.indexOf(probe)).toBe(index);
        expect(actual.tryGetValue(probe)).toEqual({ found: true, value: item });
    }
    const missing: Item = { equivalenceClass: 100_000, identity: -1 };
    expect(actual.contains(missing)).toBe(false);
    expect(actual.indexOf(missing)).toBe(-1);
    expect(actual.tryGetValue(missing)).toEqual({ found: false, value: missing });
    expect(actual.validateStructure().count).toBe(expected.length);
}

describe("PersistentOrderedSet generated histories", () => {
    test("branching commands agree with an independent comparer-aware list model", () => {
        const command = fc.record({
            operation: fc.integer({ min: 0, max: 15 }),
            equivalenceClass: fc.integer({ min: -20, max: 20 }),
            identity: fc.integer(),
            position: fc.integer(),
        });

        fc.assert(fc.property(fc.array(command, { maxLength: 70 }), (history) => {
            const policy = createHashPolicy<Item>(
                () => 0,
                (left, right) => left.equivalenceClass === right.equivalenceClass,
            );
            const empty = PersistentOrderedSet.empty(policy);
            const versions: Array<{
                readonly actual: PersistentOrderedSet<Item>;
                readonly expected: readonly Item[];
            }> = [{ actual: empty, expected: [] }];

            for (const commandValue of history) {
                const branch = versions[modulo(commandValue.identity, versions.length)]!;
                const source = branch.actual;
                let expected = branch.expected.slice();
                const item: Item = {
                    equivalenceClass: commandValue.equivalenceClass,
                    identity: commandValue.identity,
                };
                let actual: PersistentOrderedSet<Item>;
                let identityExpected = false;

                switch (commandValue.operation) {
                    case 0: {
                        identityExpected = findIndex(expected, item) >= 0;
                        if (!identityExpected) expected.push(item);
                        actual = source.add(item);
                        break;
                    }
                    case 1: {
                        identityExpected = findIndex(expected, item) >= 0;
                        if (!identityExpected) expected.unshift(item);
                        actual = source.addFirst(item);
                        break;
                    }
                    case 2: {
                        const index = modulo(commandValue.position, expected.length + 1);
                        identityExpected = findIndex(expected, item) >= 0;
                        if (!identityExpected) expected.splice(index, 0, item);
                        actual = source.insert(index, item);
                        break;
                    }
                    case 3: {
                        if (expected.length === 0) {
                            identityExpected = true;
                            actual = source;
                            break;
                        }
                        const oldIndex = modulo(commandValue.equivalenceClass, expected.length);
                        const finalIndex = modulo(commandValue.position, expected.length);
                        const stored = expected[oldIndex]!;
                        identityExpected = oldIndex === finalIndex;
                        if (!identityExpected) {
                            expected.splice(oldIndex, 1);
                            expected.splice(finalIndex, 0, stored);
                        }
                        actual = source.moveTo(finalIndex, {
                            equivalenceClass: stored.equivalenceClass,
                            identity: -1,
                        });
                        break;
                    }
                    case 4: {
                        const index = findIndex(expected, item);
                        identityExpected = index < 0;
                        if (index >= 0) expected.splice(index, 1);
                        actual = source.remove(item);
                        break;
                    }
                    case 5: {
                        if (expected.length === 0) {
                            identityExpected = true;
                            actual = source;
                            break;
                        }
                        const index = modulo(commandValue.position, expected.length);
                        expected.splice(index, 1);
                        actual = source.removeAt(index);
                        break;
                    }
                    case 6: {
                        const index = modulo(commandValue.position, expected.length + 1);
                        const count = modulo(commandValue.identity, expected.length - index + 1);
                        identityExpected = index === 0 && count === expected.length;
                        expected = expected.slice(index, index + count);
                        actual = source.getRange(index, count);
                        break;
                    }
                    case 7: {
                        identityExpected = expected.length <= 1;
                        expected.reverse();
                        actual = source.reverse();
                        break;
                    }
                    case 8: {
                        const original = expected.slice();
                        expected = expected
                            .map((value, index) => ({ value, index }))
                            .toSorted((left, right) =>
                                modulo(left.value.equivalenceClass, 5) - modulo(right.value.equivalenceClass, 5)
                                || left.index - right.index)
                            .map(({ value }) => value);
                        identityExpected = expected.every((value, index) => value === original[index]);
                        actual = source.sort((left, right) =>
                            modulo(left.equivalenceClass, 5) - modulo(right.equivalenceClass, 5));
                        break;
                    }
                    case 9: {
                        const argument: Item[] = [
                            item,
                            { equivalenceClass: item.equivalenceClass, identity: item.identity + 1 },
                            { equivalenceClass: item.equivalenceClass + 1_000, identity: item.identity },
                        ];
                        const before = expected.length;
                        for (const candidate of normalize(argument)) {
                            if (findIndex(expected, candidate) < 0) expected.push(candidate);
                        }
                        identityExpected = before === expected.length;
                        actual = source.union(argument);
                        break;
                    }
                    case 10: {
                        const argument = normalize([
                            item,
                            { equivalenceClass: item.equivalenceClass, identity: item.identity + 1 },
                        ]);
                        const before = expected.length;
                        expected = expected.filter((value) => findIndex(argument, value) < 0);
                        identityExpected = before === expected.length;
                        actual = source.except(argument);
                        break;
                    }
                    case 11: {
                        const argument = normalize([
                            item,
                            { equivalenceClass: item.equivalenceClass + 1, identity: item.identity },
                        ]);
                        const original = expected;
                        expected = expected.filter((value) => findIndex(argument, value) >= 0);
                        identityExpected = expected.length === original.length;
                        actual = source.intersect(argument);
                        break;
                    }
                    case 12: {
                        const argument = normalize([
                            item,
                            { equivalenceClass: item.equivalenceClass, identity: item.identity + 1 },
                            { equivalenceClass: item.equivalenceClass + 1, identity: item.identity },
                        ]);
                        const receiver = expected;
                        expected = [
                            ...receiver.filter((value) => findIndex(argument, value) < 0),
                            ...argument.filter((value) => findIndex(receiver, value) < 0),
                        ];
                        actual = source.symmetricExcept(argument);
                        break;
                    }
                    case 13: {
                        const count = modulo(commandValue.position, expected.length + 1);
                        identityExpected = count === expected.length;
                        expected = expected.slice(0, count);
                        actual = source.take(count);
                        break;
                    }
                    case 14: {
                        const count = modulo(commandValue.position, expected.length + 1);
                        identityExpected = count === 0;
                        expected = expected.slice(count);
                        actual = source.drop(count);
                        break;
                    }
                    default: {
                        identityExpected = expected.length === 0;
                        expected = [];
                        actual = source.clear();
                        break;
                    }
                }

                expect(actual === source).toBe(identityExpected);
                expect(actual.policy).toBe(policy);
                assertState(expected, actual);
                versions.push({ actual, expected });
            }

            for (const version of versions) {
                expect(version.actual.policy).toBe(policy);
                assertState(version.expected, version.actual);
            }
        }), { numRuns: 100 });
    });
});

describe("Ordered package boundary and surface", () => {
    test("production imports only neutral HAMT and FingerTree foundations", () => {
        const source = readFileSync(
            new URL("../../src/ordered/persistent-ordered-set.ts", import.meta.url),
            "utf8",
        );
        const imports = Array.from(
            source.matchAll(/^import .* from "([^"]+)";/gmu),
            (match) => match[1],
        );
        expect(imports).toEqual([
            "../finger-tree/core.js",
            "../finger-tree/measures.js",
            "../finger-tree/ordering.js",
            "../hamt/hash-policy.js",
            "../hamt/persistent-hamt.js",
        ]);
    });

    test("package manifest exposes the neutral ordered subpath", () => {
        const manifest = JSON.parse(readFileSync(
            new URL("../../package.json", import.meta.url),
            "utf8",
        )) as { readonly exports: Record<string, unknown> };
        expect(manifest.exports["./ordered"]).toEqual({
            types: "./dist/ordered/index.d.ts",
            import: "./dist/ordered/index.js",
        });
    });

    test("surface contains explicit order operations without sorted-set vocabulary", () => {
        const names = new Set(Object.getOwnPropertyNames(PersistentOrderedSet.prototype));
        for (const expected of [
            "add", "addFirst", "insert", "moveToFirst", "moveToLast", "moveTo",
            "remove", "tryRemove", "removeAt", "removeFirst", "removeLast", "clear",
            "getRange", "take", "drop", "reverse", "sort", "union", "intersect",
            "except", "symmetricExcept", "isSubsetOf", "isProperSubsetOf", "isSupersetOf",
            "isProperSupersetOf", "overlaps", "setEquals", "toArray",
        ]) expect(names.has(expected)).toBe(true);
        for (const forbidden of ["minimum", "maximum", "lowerBound", "upperBound", "floor", "ceiling"]) {
            expect(names.has(forbidden)).toBe(false);
        }
    });
});
