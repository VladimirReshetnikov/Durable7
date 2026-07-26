/**
 * Tests for the sequence core: deque and reversible-deque operations, measured splitting and
 * locating, and structure sharing between versions.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    FingerTree,
    Interval,
    IntervalTree,
    NumberSumMeasure,
    PersistentDeque,
    PriorityQueue,
    ReversibleDeque,
    SortedBag,
    SortedMap,
    SortedSet,
} from "../../src/finger-tree/index.js";

describe("persistent sequence cores", () => {
    test("deque preserves versions across endpoint and indexed edits", () => {
        const basis = PersistentDeque.from([1, 2, 3, 4]);
        const edited = basis.prepend(0).append(5).insertAt(3, 99)!.removeAt(4)!;
        expect(basis.toArray()).toEqual([1, 2, 3, 4]);
        expect(edited.toArray()).toEqual([0, 1, 2, 99, 4, 5]);
        const split = edited.splitRange(2, 3)!;
        expect(split.before.toArray()).toEqual([0, 1]);
        expect(split.range.toArray()).toEqual([2, 99, 4]);
        expect(split.after.toArray()).toEqual([5]);
        const large = PersistentDeque.from(Array.from({ length: 256 }, (_, index) => index));
        expect(large.sharesStorageWith(large.splitRange(64, 128)!.range)).toBe(true);
    });

    test("generated edit histories agree with arrays", () => {
        fc.assert(fc.property(fc.array(fc.tuple(fc.nat(100), fc.integer(), fc.boolean()), { maxLength: 300 }), (operations) => {
            let actual = PersistentDeque.empty<number>();
            const expected: number[] = [];
            for (const [rawIndex, value, remove] of operations) {
                if (remove && expected.length !== 0) {
                    const index = rawIndex % expected.length;
                    actual = actual.removeAt(index)!;
                    expected.splice(index, 1);
                } else {
                    const index = rawIndex % (expected.length + 1);
                    actual = actual.insertAt(index, value)!;
                    expected.splice(index, 0, value);
                }
            }
            expect(actual.toArray()).toEqual(expected);
        }), { numRuns: 150 });
    });

    test("reversible deque retains O(1) orientation views", () => {
        const value = ReversibleDeque.from([1, 2, 3, 4]);
        const reversed = value.reverse();
        expect(reversed.toArray()).toEqual([4, 3, 2, 1]);
        expect(reversed.reverse().toArray()).toEqual(value.toArray());
        expect(reversed.sharesStorageWith(value)).toBe(true);
        expect(reversed.prepend(5).append(0).toArray()).toEqual([5, 4, 3, 2, 1, 0]);
    });

    test("measured tree splits and locates by prefix", () => {
        const policy = new NumberSumMeasure();
        const tree = FingerTree.from([2, 3, 5, 7], policy);
        expect(tree.measure()).toBe(17);
        expect(tree.prefixMeasure(3)).toBe(10);
        expect(tree.tryLocate((sum) => sum >= 6)).toEqual({ index: 2, measureBefore: 5, item: 5, found: true });
        expect(tree.trySplitFind((sum) => sum >= 6)!.item).toBe(5);
    });
});

describe("derived measured collections", () => {
    test("sorted bag/set/map expose rank and navigation semantics", () => {
        const bag = SortedBag.from([3, 1, 2, 1, 4]);
        expect(bag.toArray()).toEqual([1, 1, 2, 3, 4]);
        expect(bag.countOf(1)).toBe(2);
        expect(bag.getValueRange(2, 3).toArray()).toEqual([2, 3]);

        const set = SortedSet.from([3, 1, 2, 1, 4]);
        expect(set.toArray()).toEqual([1, 2, 3, 4]);
        expect(set.floor(2.5)).toBe(2);
        expect(set.ceiling(2.5)).toBe(3);
        expect(set.lower(1)).toBeUndefined();

        const map = SortedMap.from([[2, "two"], [1, "one"], [2, "TWO"]]);
        expect(map.keys()).toEqual([1, 2]);
        expect(map.get(2)).toBe("TWO");
        expect(map.floorEntry(1.5)?.key).toBe(1);
    });

    test("priority queue keeps stable equal-priority order", () => {
        let queue = PriorityQueue.empty<string, number>();
        queue = queue.enqueue("a", 2).enqueue("b", 1).enqueue("c", 1).enqueue("d", 3);
        const output: string[] = [];
        while (!queue.isEmpty) { const result = queue.dequeue()!; output.push(result.entry.value); queue = result.queue; }
        expect(output).toEqual(["b", "c", "a", "d"]);
    });

    test("interval tree performs closed overlap, removal, and coalescing", () => {
        const tree = IntervalTree.from([new Interval(5, 8), new Interval(1, 3), new Interval(2, 6), new Interval(10, 12)]);
        expect(tree.findOverlaps(new Interval(3, 5)).map((value) => [value.low, value.high])).toEqual([[1, 3], [2, 6], [5, 8]]);
        expect(tree.findContaining(11)).toEqual(new Interval(10, 12));
        expect(tree.coalesce().toArray().map((value) => [value.low, value.high])).toEqual([[1, 8], [10, 12]]);
        expect(tree.remove(new Interval(2, 6)).contains(new Interval(2, 6))).toBe(false);
    });
});
