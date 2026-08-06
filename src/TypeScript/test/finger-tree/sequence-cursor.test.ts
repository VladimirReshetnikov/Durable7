/**
 * Cross-structure tests for the positional sequence cursors, asserting one shared gap vocabulary
 * and that each retained cursor keeps its own version.
 */
import { describe, expect, it } from "vitest";
import {
    FingerTree,
    NumberSumMeasure,
    PersistentDeque,
    ReversibleDeque,
    RrbVector,
} from "../../src/finger-tree/index.js";
import { RangeUpdateSequence } from "../../src/finger-tree/range-update-sequence.js";
import { addTag, SumAffineAlgebra } from "./range-update-test-support.js";

describe("persistent sequence cursors", () => {
    it("uses measure-aware general-tree gaps and usable misses", () => {
        const tree = FingerTree.from([2, 3, 5, 7], new NumberSumMeasure());
        const start = tree.getCursorAtStart();
        expect(start.isAtStart).toBe(true);
        expect(start.measureBefore).toBe(0);
        expect(start.measureAfter).toBe(17);
        expect(start.peekPrevious()).toBeUndefined();
        expect(start.peekNext()).toEqual({ value: 2 });
        expect(start.snapshot()).toBe(tree);
        expect(() => start.movePrevious()).toThrow(RangeError);

        const located = tree.cursorByMeasure((sum) => sum >= 6);
        expect(located.found).toBe(true);
        expect(located.cursor.measureBefore).toBe(5);
        expect(located.cursor.measureAfter).toBe(12);
        expect(located.cursor.peekPrevious()).toEqual({ value: 3 });
        expect(located.cursor.peekNext()).toEqual({ value: 5 });
        expect(located.cursor.movePrevious().moveNext().snapshot()).toBe(tree);
        expect(located.cursor.insert(4).snapshot().toArray()).toEqual([2, 3, 4, 5, 7]);
        expect(located.cursor.replaceNext(9).snapshot().toArray()).toEqual([2, 3, 9, 7]);
        expect(located.cursor.deletePrevious().snapshot().toArray()).toEqual([2, 5, 7]);
        expect(located.cursor.deleteNext().snapshot().toArray()).toEqual([2, 3, 7]);
        expect(located.cursor.snapshot()).toBe(tree);

        const miss = tree.cursorByMeasure((sum) => sum > 100);
        expect(miss.found).toBe(false);
        expect(miss.cursor.isAtEnd).toBe(true);
        expect(miss.cursor.snapshot()).toBe(tree);
    });

    it("preserves nullable deque presence and captures inserted ranges once", () => {
        const deque = PersistentDeque.from<string | undefined>(["a", undefined, "c"]);
        const cursor = deque.getCursor(1);
        expect(cursor.peekPrevious()).toEqual({ value: "a" });
        expect(cursor.peekNext()).toEqual({ value: undefined });
        expect(cursor.snapshot()).toBe(deque);

        let enumerations = 0;
        const values: Iterable<string> = {
            *[Symbol.iterator](): Generator<string, void, unknown> {
                enumerations++;
                if (enumerations > 1) throw new Error("enumerated twice");
                yield "x";
                yield "y";
            },
        };
        const inserted = cursor.insertRange(values);
        expect(enumerations).toBe(1);
        expect(inserted.position).toBe(3);
        expect(inserted.snapshot().toArray()).toEqual(["a", "x", "y", undefined, "c"]);
        expect(cursor.replaceNext("z").snapshot().toArray()).toEqual(["a", "z", "c"]);
        expect(cursor.deletePrevious().snapshot().toArray()).toEqual([undefined, "c"]);
        expect(cursor.deleteNext().snapshot().toArray()).toEqual(["a", "c"]);
        expect(cursor.snapshot()).toBe(deque);
    });

    it("maps reversible logical gaps through reversal", () => {
        const deque = ReversibleDeque.from([1, 2, 3, 4]);
        for (let position = 0; position <= deque.size; position++) {
            const reversed = deque.getCursor(position).reverse();
            expect(reversed.position).toBe(deque.size - position);
            expect(reversed.snapshot().toArray()).toEqual([4, 3, 2, 1]);
            const restored = reversed.reverse();
            expect(restored.position).toBe(position);
            expect(restored.snapshot().toArray()).toEqual([1, 2, 3, 4]);
        }
        const edited = deque.getCursor(2).insertRange([8, 9]);
        expect(edited.position).toBe(4);
        expect(edited.snapshot().toArray()).toEqual([1, 2, 8, 9, 3, 4]);
        expect(edited.reverse().snapshot().toArray()).toEqual([4, 3, 9, 8, 2, 1]);
    });

    it("splices reversed ranges in physical orientation without losing sharing", () => {
        // Large enough that the finger tree has a spine of 2-3 nodes to share; a handful of
        // elements lives entirely in the root digits, where an edit legitimately rebuilds all
        // structure. (The old join tree shared subtree nodes even at size four.)
        const values = Array.from({ length: 32 }, (_, index) => index + 1);
        const forward = ReversibleDeque.from(values);
        const reversed = ReversibleDeque.from([...values].reverse()).reverse();
        expect(reversed.toArray()).toEqual(forward.toArray());
        for (const source of [forward, reversed]) {
            for (let gap = 0; gap <= source.size; gap++) {
                const edited = source.getCursor(gap).insertRange([98, 99]);
                expect(edited.snapshot().toArray()).toEqual(values.toSpliced(gap, 0, 98, 99));
                expect(edited.position).toBe(gap + 2);
                expect(edited.snapshot().sharesStorageWith(source)).toBe(true);
                expect(source.toArray()).toEqual(values);
            }
        }
        expect(reversed.getCursor(2).insert(0).snapshot().toArray()).toEqual(values.toSpliced(2, 0, 0));
        const clean = reversed.getCursor(2);
        expect(clean.insertRange([])).toBe(clean);
    });

    it("splices RRB vectors across radix-leaf boundaries", () => {
        const vector = RrbVector.from(Array.from({ length: 70 }, (_, index) => index));
        const cursor = vector.getCursor(32);
        expect(cursor.peekPrevious()).toEqual({ value: 31 });
        expect(cursor.peekNext()).toEqual({ value: 32 });
        expect(cursor.snapshot()).toBe(vector);
        const inserted = cursor.insertVector(RrbVector.from([100, 101]));
        expect(inserted.position).toBe(34);
        expect(inserted.snapshot().toArray().slice(30, 36)).toEqual([30, 31, 100, 101, 32, 33]);
        expect(cursor.replaceNext(999).snapshot().get(32)).toBe(999);
        expect(cursor.deletePrevious().snapshot().get(31)).toBe(32);
        expect(cursor.deleteNext().snapshot().get(32)).toBe(33);
        expect(inserted.snapshot().validateStructure().count).toBe(72);
    });

    it("observes lazy Range values and applies directional measures and tags", () => {
        const algebra = new SumAffineAlgebra();
        const source = RangeUpdateSequence.from([1, 2, 3, 4], algebra).applyRange(0, 4, addTag(10));
        const cursor = source.getCursor(2);
        expect(cursor.peekPrevious()).toEqual({ value: 12 });
        expect(cursor.peekNext()).toEqual({ value: 13 });
        expect(cursor.measureBefore).toBe(23);
        expect(cursor.measureAfter).toBe(27);
        expect(cursor.measurePrevious(2)).toBe(23);
        expect(cursor.measureNext(2)).toBe(27);
        expect(cursor.snapshot()).toBe(source);
        expect(cursor.applyPrevious(1, addTag(100)).snapshot().toArray()).toEqual([11, 112, 13, 14]);
        expect(cursor.applyNext(2, addTag(-6)).snapshot().toArray()).toEqual([11, 12, 7, 8]);
        expect(cursor.applyNext(0, addTag(1))).toBe(cursor);
        expect(() => cursor.applyPrevious(3, addTag(1))).toThrow(RangeError);
        expect(cursor.snapshot().toArray()).toEqual([11, 12, 13, 14]);
    });
});
