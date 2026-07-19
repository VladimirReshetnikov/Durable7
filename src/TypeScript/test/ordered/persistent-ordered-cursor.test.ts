import { describe, expect, it } from "vitest";

import {
    createHashPolicy,
    defaultHash,
    defaultHashPolicy,
    PersistentOrderedMap,
    PersistentOrderedMultimap,
    PersistentOrderedSet,
} from "../../src/index.js";

describe("neutral ordered collection cursors", () => {
    it("edits ordered-set gaps while retaining source snapshots and undefined presence", () => {
        const source = PersistentOrderedSet.from<string | undefined>(["a", undefined, "c"]);
        let cursor = source.getCursor(1);
        expect(cursor.tryPeekPrevious()).toEqual({ found: true, value: "a" });
        expect(cursor.tryPeekNext()).toEqual({ found: true, value: undefined });

        cursor = cursor.insert("x");
        expect(cursor.position).toBe(2);
        expect(cursor.snapshot.toArray()).toEqual(["a", "x", undefined, "c"]);
        expect(cursor.insert(undefined)).toBe(cursor);
        cursor = cursor.deletePrevious();
        expect(cursor.position).toBe(1);
        expect(cursor.snapshot.toArray()).toEqual(["a", undefined, "c"]);
        expect(source.toArray()).toEqual(["a", undefined, "c"]);
        expect(source.getCursorAtItem(undefined)).toMatchObject({ found: true, cursor: { position: 1 } });
    });

    it("inserts and updates ordered-map entries at explicit gaps", () => {
        const source = PersistentOrderedMap.from([ ["a", 1], ["b", 2], ["c", 3] ]);
        let cursor = source.getCursor(1).insert("x", 9);
        expect(cursor.position).toBe(2);
        expect([...cursor.snapshot.keys()]).toEqual(["a", "x", "b", "c"]);
        cursor = cursor.setNextValue(20);
        expect(cursor.tryPeekNext()).toEqual({ key: "b", value: 20 });
        expect(cursor.tryInsert("b", 200)).toMatchObject({ inserted: false, cursor: { position: 2 } });
        cursor = cursor.deletePrevious();
        expect([...cursor.snapshot.keys()]).toEqual(["a", "b", "c"]);
        expect(source.get("b")).toBe(2);
    });

    it("uses flattened key-grouped ranks for ordered-multimap cursors", () => {
        const source = PersistentOrderedMultimap.from([
            ["b", 2], ["a", 9], ["b", 1], ["c", 7],
        ]);
        const located = source.getCursorAtPair("b", 1);
        expect(located.found).toBe(true);
        let cursor = located.cursor.add("b", 3);
        expect(cursor.position).toBe(3);
        expect([...cursor.snapshot]).toEqual([
            { key: "b", value: 2 },
            { key: "b", value: 1 },
            { key: "b", value: 3 },
            { key: "a", value: 9 },
            { key: "c", value: 7 },
        ]);
        expect(cursor.add("b", 3)).toBe(cursor);
        cursor = cursor.deletePrevious().deleteNext();
        expect(cursor.position).toBe(2);
        expect(cursor.tryPeekNext()).toEqual({ key: "c", value: 7 });
        expect([...cursor.snapshot]).toEqual([
            { key: "b", value: 2 },
            { key: "b", value: 1 },
            { key: "c", value: 7 },
        ]);
        expect(source.pairCount).toBe(4);
        expect(source.getCursorAtGroup("a")).toMatchObject({ found: true, cursor: { position: 2 } });
    });

    it("tolerates values non-reflexive under the value policy in multimap cursors", () => {
        // A bare === policy is a legitimate strict policy under which NaN is non-reflexive.
        const values = createHashPolicy<number>(defaultHash, (left, right) => left === right);
        expect(values.equivalent(NaN, NaN)).toBe(false);

        const source = PersistentOrderedMultimap.from<string, number>(
            [["a", 1], ["b", 2], ["a", 3]],
            defaultHashPolicy<string>(),
            values,
        );
        expect([...source]).toEqual([
            { key: "a", value: 1 },
            { key: "a", value: 3 },
            { key: "b", value: 2 },
        ]);

        // add() derives the gap from the key group, not by re-scanning for the accepted pair.
        const addedNaN = source.getCursor(0).add("a", NaN);
        expect(addedNaN.snapshot.pairCount).toBe(4);
        expect(addedNaN.position).toBe(3);
        const stored = addedNaN.snapshot.cursorEntryAt(2);
        expect(stored.key).toBe("a");
        expect(Number.isNaN(stored.value)).toBe(true);
        expect([...source]).toEqual([
            { key: "a", value: 1 },
            { key: "a", value: 3 },
            { key: "b", value: 2 },
        ]);

        // A normal reflexive value still lands at the same group boundary.
        const addedReal = source.getCursor(0).add("a", 7);
        expect(addedReal.position).toBe(3);
        expect(addedReal.tryPeekPrevious()).toEqual({ key: "a", value: 7 });

        // delete() must not report a false success when remove-by-content is a no-op for a stored NaN.
        const withNaN = PersistentOrderedMultimap
            .empty<string, number>(defaultHashPolicy<string>(), values)
            .add("a", NaN);
        expect(withNaN.pairCount).toBe(1);
        const afterNaN = withNaN.getCursor(1);
        expect(afterNaN.deletePrevious()).toBe(afterNaN);
        const beforeNaN = withNaN.getCursor(0);
        expect(beforeNaN.deleteNext()).toBe(beforeNaN);

        // A reflexive value deletes normally through the same guarded path.
        const withReal = PersistentOrderedMultimap
            .empty<string, number>(defaultHashPolicy<string>(), values)
            .add("a", 5);
        const deleted = withReal.getCursor(1).deletePrevious();
        expect(deleted.snapshot.pairCount).toBe(0);
        expect(deleted.position).toBe(0);
    });
});
