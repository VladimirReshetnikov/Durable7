import { describe, expect, test } from "vitest";

import {
    CanonicalSortedSet,
    Interval,
    IntervalTree,
    PersistentChunkedBitSet,
    PersistentIntervalMap,
    PrioritySearchQueue,
    SortedBag,
    SortedMap,
    SortedSet,
    ZipTreeRankPolicy,
} from "../../src/finger-tree/index.js";

describe("ordered search cursors", () => {
    test("sorted bag deletes an exact duplicate occurrence", () => {
        const comparator = (left: { rank: number }, right: { rank: number }): number => left.rank - right.rank;
        const first = { rank: 2, name: "first" };
        const second = { rank: 2, name: "second" };
        const third = { rank: 2, name: "third" };
        const bag = SortedBag.from([{ rank: 1, name: "low" }, first, second, third], comparator);
        const cursor = bag.cursorAt(2);
        const deleted = cursor.deleteNext();
        expect(deleted.snapshot().toArray()).toEqual([{ rank: 1, name: "low" }, first, third]);
        expect(cursor.peekNext()?.value).toBe(second);
        expect(bag.cursorAtUpperBound({ rank: 2, name: "probe" }).position).toBe(4);
    });

    test("set and map cursors preserve representatives and strict insertion", () => {
        const compareText = (left: string, right: string): number => left.toLowerCase().localeCompare(right.toLowerCase());
        const set = SortedSet.from(["a", "c"], compareText);
        const miss = set.findCursor("b");
        expect(miss).toMatchObject({ found: false, cursor: { position: 1 } });
        expect(miss.cursor.add("A").snapshot()).toBe(set);
        expect(miss.cursor.add("b").snapshot().toArray()).toEqual(["a", "b", "c"]);

        const map = SortedMap.from([[1, "one"], [3, "three"]]);
        const inserted = map.cursorAtLowerBound(2).insert(2, "two");
        expect(inserted.position).toBe(2);
        expect(inserted.movePrevious().setNextValue("TWO").snapshot().get(2)).toBe("TWO");
        expect(map.cursorAt().tryInsert(1, "duplicate").added).toBe(false);
    });

    test("canonical and priority-search cursors use ordinary repair paths", () => {
        const policy = ZipTreeRankPolicy.create<number>({ seed: 42 });
        const canonical = CanonicalSortedSet.from([1, 4, 7], policy);
        const edited = canonical.cursorAtLowerBound(4).add(3).deletePrevious().snapshot();
        const expected = canonical.add(3).remove(3);
        expect(edited.contentHash).toBe(expected.contentHash);
        expect([...edited]).toEqual([...expected]);
        expect(edited.policy).toBe(policy);
        edited.validateStructure();

        const queue = PrioritySearchQueue.from([
            { key: 1, priority: 30, value: "one" },
            { key: 2, priority: 10, value: "two" },
            { key: 3, priority: 20, value: "three" },
        ]);
        const winner = queue.cursorAtMinimumPriority();
        expect(winner.peekNext()?.value.key).toBe(2);
        const updated = winner.setNext(40, "TWO");
        expect(updated.snapshot().minimum.key).toBe(3);
        updated.snapshot().validateStructure();
    });

    test("interval cursors preserve occurrences and continue overlaps exclusively", () => {
        const first = new Interval(1, 5);
        const middle = new Interval(3, 8);
        const second = new Interval(1, 5);
        const tree = IntervalTree.empty<number>().insert(first).insert(middle).insert(second);
        expect(tree.toArray()).toEqual([second, first, middle]);
        const deleted = tree.cursorAt(1).deleteNext();
        expect(deleted.snapshot().toArray()).toEqual([second, middle]);

        const overlap = tree.findOverlapCursor(new Interval(4, 4));
        expect(overlap.found).toBe(true);
        const next = overlap.cursor.seekNextOverlap(new Interval(4, 4));
        expect(next.found).toBe(true);
        expect(next.cursor.position).toBeGreaterThan(overlap.cursor.position);

        const map = PersistentIntervalMap.from<number, string>([
            [new Interval(1, 4), "a"],
            [new Interval(3, 7), "b"],
            [new Interval(9, 10), "c"],
        ]);
        const exact = map.findCursor(new Interval(3, 7));
        expect(exact.found).toBe(true);
        expect(exact.cursor.setNextValue("B").snapshot().get(new Interval(3, 7))).toBe("B");
        const mapOverlap = map.findOverlapCursor(new Interval(4, 9));
        expect(mapOverlap.cursor.seekNextOverlap(new Interval(4, 9)).found).toBe(true);
    });

    test("bag and interval cursor deletion shares structure instead of rebuilding", () => {
        const ordinals = Array.from({ length: 200 }, (_, index) => index);
        const bag = SortedBag.from(ordinals);
        const deleted = bag.cursorAt(100).deleteNext();
        expect(deleted.snapshot().toArray()).toEqual(ordinals.filter((value) => value !== 100));
        expect(deleted.snapshot().sharesStorageWith(bag)).toBe(true);
        expect(deleted.snapshot().comparator).toBe(bag.comparator);
        expect(deleted.position).toBe(100);
        expect(bag.cursorAt(100).deletePrevious().position).toBe(99);

        const spans = ordinals.slice(0, 100).map((low) => new Interval(low, low + 5));
        const tree = IntervalTree.from(spans);
        const trimmed = tree.cursorAt(50).deleteNext();
        expect(trimmed.snapshot().toArray()).toEqual(spans.filter((span) => span.low !== 50));
        expect(trimmed.snapshot().sharesStorageWith(tree)).toBe(true);
        expect(trimmed.snapshot().comparator).toBe(tree.comparator);
        expect(trimmed.position).toBe(50);
    });

    test("interval cursor deletion keeps front-of-equal-low placement among an equal run", () => {
        const first = new Interval(1, 5);
        const middle = new Interval(1, 9);
        const last = new Interval(1, 5);
        const tree = IntervalTree.empty<number>().insert(first).insert(middle).insert(last);
        expect(tree.toArray()).toEqual([last, middle, first]);
        expect(tree.cursorAt(0).deleteNext().snapshot().toArray()).toEqual([middle, first]);
        expect(tree.cursorAt(1).deleteNext().snapshot().toArray()).toEqual([last, first]);
        expect(tree.cursorAt(3).deletePrevious().snapshot().toArray()).toEqual([last, middle]);
        expect(tree.removeAt(3)).toBeUndefined();
    });

    test("chunked bit cursor uses population ranks and identity-preserving present adds", () => {
        const bits = PersistentChunkedBitSet.from([1, 63, 64, 130]);
        const cursor = bits.cursorAtOrAfter(62);
        expect(cursor.position).toBe(1);
        expect(cursor.peekNext()?.value).toBe(63);
        expect(bits.findCursor(62)).toMatchObject({ found: false, cursor: { position: 1 } });
        const added = cursor.add(62);
        expect([...added.snapshot()]).toEqual([1, 62, 63, 64, 130]);
        expect([...added.deletePrevious().snapshot()]).toEqual([1, 63, 64, 130]);
        expect(cursor.add(63)).toBe(cursor);
    });
});
