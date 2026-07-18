import { describe, expect, it } from "vitest";

import {
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
});
