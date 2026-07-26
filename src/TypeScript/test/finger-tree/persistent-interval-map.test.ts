/**
 * Tests for the interval-keyed map: lexicographic key ordering, overlap not implying key equality,
 * strict insertion, and stabbing and overlap queries driven by the cached maximum endpoint.
 */
import { describe, expect, it } from "vitest";

import {
    DuplicateIntervalError,
    Interval,
    PersistentIntervalMap,
} from "../../src/finger-tree/index.js";

describe("PersistentIntervalMap", () => {
    it("orders keys lexicographically when lower endpoints match", () => {
        const map = PersistentIntervalMap.empty<number, string>()
            .add(new Interval(1, 5), "wide")
            .add(new Interval(1, 3), "narrow")
            .add(new Interval(0, 9), "first");
        expect([...map].map(({ interval }) => [interval.low, interval.high])).toEqual([[0, 9], [1, 3], [1, 5]]);
        expect(() => map.add(new Interval(1, 3), "duplicate")).toThrow(DuplicateIntervalError);
    });

    it("retains the first key representative and returns itself for equal values", () => {
        const first = new Interval(1, 3);
        const equal = new Interval(1, 3);
        const map = PersistentIntervalMap.empty<number, string>().add(first, "value");
        expect(map.set(equal, "value")).toBe(map);
        const changed = map.set(equal, "changed");
        expect(changed.tryGetEntry(equal)).toEqual({ found: true, entry: { interval: first, value: "changed" } });
    });

    it("rejects intervals invalid under the map comparator", () => {
        const forged = new Interval(5, 1, (left, right) => right - left);
        const map = PersistentIntervalMap.empty<number, string>();
        expect(() => map.add(forged, "invalid")).toThrow(RangeError);
        expect(() => map.findOverlap(forged)).toThrow(RangeError);
    });

    it("finds overlaps and stabbing matches through maximum-high pruning", () => {
        const map = PersistentIntervalMap.from([
            [new Interval(0, 2), "a"],
            [new Interval(4, 9), "b"],
            [new Interval(5, 6), "c"],
            [new Interval(11, 12), "d"],
        ]);
        expect(map.findOverlaps(new Interval(6, 10)).map(({ value }) => value)).toEqual(["b", "c"]);
        expect(map.findContaining(5)?.value).toBe("b");
        expect(map.countOverlaps(new Interval(6, 10))).toBe(2);
    });

    it("removes exact keys without changing retained snapshots", () => {
        const key = new Interval(1, 2);
        const source = PersistentIntervalMap.from([[key, "a"], [new Interval(3, 4), "b"]]);
        const branch = source.remove(new Interval(1, 2));
        expect(branch.containsKey(key)).toBe(false);
        expect(source.containsKey(key)).toBe(true);
        expect(branch.remove(key)).toBe(branch);

        const larger = PersistentIntervalMap.from(
            Array.from({ length: 16 }, (_, index): readonly [Interval<number>, number] => [new Interval(index, index + 1), index]),
        );
        expect(larger.remove(new Interval(7, 8)).sharesStorageWith(larger)).toBe(true);
    });

    it("retains comparator and value policy and validates annotations", () => {
        const descending = (left: number, right: number): number => right - left;
        const caseInsensitive = (left: string, right: string): boolean => left.toLowerCase() === right.toLowerCase();
        const map = PersistentIntervalMap.empty<number, string>(descending, caseInsensitive)
            .add(new Interval(5, 3, descending), "Value");
        expect(map.set(new Interval(5, 3, descending), "value")).toBe(map);
        expect(map.comparator).toBe(descending);
        expect(map.valueEquals).toBe(caseInsensitive);
        expect(map.validateStructure()).toBe(true);
        expect(map.clear().validateStructure()).toBe(true);
    });
});
