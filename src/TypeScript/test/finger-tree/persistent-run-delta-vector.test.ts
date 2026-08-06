/**
 * Tests for the checkpointed run-delta vector.
 *
 * Three claims here need more than a model comparison. The first is *reference identity*: "a clean
 * position reuses its exact checkpoint representative" is vacuous in JavaScript when stated over
 * primitives, because `Object.is(5, 5)` is true however many distinct writes produced those fives,
 * so the identity assertions below use an object payload under a coarser relation, where a
 * relation-equal replacement is a *different* object and restoring the wrong one is observable. The
 * second is that a run splice is *comparison-free and length-independent*, which a counting
 * relation proves only when paired with a positive control showing the counter is really wired. The
 * third is that a no-op *shares structure*: several no-ops return the receiver itself, and asserting
 * `a.sharesCurrentRootWith(a)` on the receiver would be a self-comparison that cannot fail, so those
 * cases assert receiver identity and every sharing assertion is made across two genuinely distinct
 * versions with a negative control beside it.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    PersistentDirtyRun,
    PersistentRunDeltaVector,
    naturalValueEquality,
    reflexiveIeeeEquality,
    type ValueEquality,
} from "../../src/finger-tree/persistent-run-delta-vector.js";

interface RunShape { readonly start: number; readonly length: number }
interface Tagged { readonly tag: string }
interface CountingRelation<T> { readonly equals: ValueEquality<T>; readonly calls: () => number }

const byTag: ValueEquality<Tagged> = (left: Tagged, right: Tagged): boolean => left.tag === right.tag;

function counting<T>(inner: ValueEquality<T>): CountingRelation<T> {
    let calls = 0;
    return {
        equals: (left: T, right: T): boolean => { calls++; return inner(left, right); },
        calls: (): number => calls,
    };
}

function shapes<T>(vector: PersistentRunDeltaVector<T>): RunShape[] {
    return Array.from(vector.dirtyRuns(), (run) => ({ start: run.start, length: run.length }));
}

function modelRuns<T>(current: readonly T[], checkpoint: readonly T[], equals: ValueEquality<T>): RunShape[] {
    const runs: RunShape[] = [];
    let index = 0;
    while (index < current.length) {
        if (equals(current[index]!, checkpoint[index]!)) { index++; continue; }
        const start = index;
        while (index < current.length && !equals(current[index]!, checkpoint[index]!)) index++;
        runs.push({ start, length: index - start });
    }
    return runs;
}

/** Asserts the published index against an independently recomputed maximal-run decomposition. */
function expectAgreesWithModel<T>(
    vector: PersistentRunDeltaVector<T>,
    current: readonly T[],
    checkpoint: readonly T[],
    equals: ValueEquality<T> = naturalValueEquality,
): void {
    const runs = modelRuns(current, checkpoint, equals);
    expect(vector.size).toBe(current.length);
    expect(vector.toArray()).toEqual(current);
    expect(vector.checkpointToArray()).toEqual(checkpoint);
    expect(shapes(vector)).toEqual(runs);
    expect(vector.dirtyRunCount).toBe(runs.length);
    expect(vector.dirtyCount).toBe(runs.reduce((sum, run) => sum + run.length, 0));
    expect(vector.hasChanges).toBe(runs.length !== 0);
    expect(vector.isClean).toBe(runs.length === 0);
    for (let index = 0; index < current.length; index++) {
        const containing = runs.find((run) => index >= run.start && index < run.start + run.length);
        expect(vector.at(index)).toBe(current[index]);
        expect(vector.checkpointAt(index)).toBe(checkpoint[index]);
        expect(vector.isDirty(index)).toBe(containing !== undefined);
        const published = vector.dirtyRunContaining(index);
        expect(published === undefined ? undefined : { start: published.start, length: published.length }).toEqual(containing);
        expect(vector.sharesRepresentativeAt(index)).toBe(containing === undefined);
    }
    for (let rank = 0; rank < runs.length; rank++) {
        const run = vector.dirtyRunAt(rank);
        expect({ start: run.start, length: run.length }).toEqual(runs[rank]);
    }
    expect(() => vector.dirtyRunAt(runs.length)).toThrow(RangeError);
    const statistics = vector.validateStructure();
    expect(statistics.count).toBe(current.length);
    expect(statistics.dirtyCount).toBe(vector.dirtyCount);
    expect(statistics.dirtyRunCount).toBe(runs.length);
    expect(statistics.isClean).toBe(runs.length === 0);
    expect(statistics.sharedRepresentativeCount).toBe(current.length - vector.dirtyCount);
}

function zeros(length: number): PersistentRunDeltaVector<number> {
    return PersistentRunDeltaVector.from(Array.from({ length }, () => 0));
}

describe("PersistentDirtyRun", () => {
    test("describes a non-empty half-open interval", () => {
        const run = new PersistentDirtyRun(3, 2);
        expect(run.start).toBe(3);
        expect(run.length).toBe(2);
        expect(run.endExclusive).toBe(5);
        expect(run.contains(3)).toBe(true);
        expect(run.contains(4)).toBe(true);
        expect(run.contains(2)).toBe(false);
        expect(run.contains(5)).toBe(false);
        expect(run.toString()).toBe("[3, 5)");
        expect(() => new PersistentDirtyRun(0, 0)).toThrow(RangeError);
        expect(() => new PersistentDirtyRun(-1, 1)).toThrow(RangeError);
        expect(() => new PersistentDirtyRun(0.5, 1)).toThrow(RangeError);
    });
});

describe("PersistentRunDeltaVector", () => {
    test("empty and built vectors start clean", () => {
        const empty = PersistentRunDeltaVector.empty<number>();
        expect(empty.size).toBe(0);
        expect(empty.isEmpty).toBe(true);
        expect(empty.isClean).toBe(true);
        expect(empty.hasChanges).toBe(false);
        expect(empty.dirtyCount).toBe(0);
        expect(empty.dirtyRunCount).toBe(0);
        expect(empty.toArray()).toEqual([]);
        expect(Array.from(empty.dirtyRuns())).toEqual([]);
        expect(empty.checkpoint()).toBe(empty);
        expect(empty.rollback()).toBe(empty);
        expect(empty.validateStructure().count).toBe(0);
        expect(PersistentRunDeltaVector.from<number>([]).isEmpty).toBe(true);

        const built = PersistentRunDeltaVector.from([10, 20, 30]);
        expect(built.valueEquals).toBe(naturalValueEquality);
        expect(Array.from(built)).toEqual([10, 20, 30]);
        expect(built.sharedLeafCount()).toBe(1);
        expectAgreesWithModel(built, [10, 20, 30], [10, 20, 30]);
    });

    test("a relation-equal write is a no-op keeping the current representative", () => {
        const alpha: Tagged = { tag: "alpha" };
        const alphaAgain: Tagged = { tag: "alpha" };
        const gamma: Tagged = { tag: "gamma" };
        const delta: Tagged = { tag: "delta" };
        expect(alpha).not.toBe(alphaAgain);
        expect(byTag(alpha, alphaAgain)).toBe(true);

        const source = PersistentRunDeltaVector.from([alpha, gamma], byTag);
        const noOp = source.setItem(0, alphaAgain);
        // The receiver comes back, so a sharing assertion here would compare a value with itself.
        expect(noOp).toBe(source);
        expect(noOp.at(0)).toBe(alpha);
        expect(noOp.hasChanges).toBe(false);
        expect(noOp.dirtyRunCount).toBe(0);
        expect(source.resetItem(0)).toBe(source);

        // A genuine edit: sharing is now asserted across two distinct versions, with the negative
        // control that the edited root is *not* shared.
        const dirty = source.setItem(0, delta);
        expect(dirty).not.toBe(source);
        expect(dirty.sharesCurrentRootWith(source)).toBe(false);
        expect(dirty.sharesCheckpointRootWith(source)).toBe(true);
        expect(dirty.at(0)).toBe(delta);
        expect(source.at(0)).toBe(alpha);
        expect(shapes(dirty)).toEqual([{ start: 0, length: 1 }]);
    });

    test("cancellation restores the exact checkpoint representative, not an equal one", () => {
        // `Object.is` on primitives is value equality, so this claim can only be observed with a
        // payload that has identity and a relation coarser than that identity.
        const alpha: Tagged = { tag: "alpha" };
        const alphaAgain: Tagged = { tag: "alpha" };
        const gamma: Tagged = { tag: "gamma" };
        const source = PersistentRunDeltaVector.from([alpha, gamma], byTag);

        // A second position stays dirty throughout, so the run index never empties and the
        // canonicalization onto the checkpoint root cannot mask a wrongly rebuilt representative.
        const dirty = source.setItem(0, { tag: "omega" }).setItem(1, { tag: "zeta" });
        expect(shapes(dirty)).toEqual([{ start: 0, length: 2 }]);
        expect(dirty.sharesRepresentativeAt(0)).toBe(false);
        expect(dirty.sharesRepresentativeAt(1)).toBe(false);

        const cancelled = dirty.setItem(0, alphaAgain);
        expect(shapes(cancelled)).toEqual([{ start: 1, length: 1 }]);
        expect(cancelled.at(0)).toBe(alpha);
        expect(cancelled.at(0)).not.toBe(alphaAgain);
        expect(cancelled.sharesRepresentativeAt(0)).toBe(true);
        expect(cancelled.sharesRepresentativeAt(1)).toBe(false);
        cancelled.validateStructure();

        // `resetItem` restores the same exact representative, again with a run still outstanding.
        const reset = dirty.resetItem(0);
        expect(reset.at(0)).toBe(alpha);
        expect(reset.sharesRepresentativeAt(0)).toBe(true);
        expect(shapes(reset)).toEqual([{ start: 1, length: 1 }]);

        // Clearing the *last* dirty position additionally canonicalizes the current root onto the
        // checkpoint root; `emptied` is a distinct version, so this is not a self-comparison.
        const emptied = cancelled.setItem(1, gamma);
        expect(emptied.at(1)).toBe(gamma);
        expect(emptied.isClean).toBe(true);
        expect(emptied).not.toBe(source);
        expect(emptied.sharesCurrentRootWith(source)).toBe(true);
        expect(emptied.sharesCheckpointRootWith(source)).toBe(true);
        expect(dirty.sharesCurrentRootWith(source)).toBe(false);
        expect(emptied.validateStructure().sharedRepresentativeCount).toBe(2);

        // Retained versions are untouched.
        expect(dirty.at(0)).toEqual({ tag: "omega" });
        expect(source.at(0)).toBe(alpha);
    });

    test("the retained relation must be reflexive, and the defaults are", () => {
        // The default is `Object.is`, which unlike `===` is reflexive on NaN.
        const source = PersistentRunDeltaVector.from([Number.NaN, 1, 0]);
        const rewritten = source.setItem(0, Number.NaN);
        expect(rewritten).toBe(source);
        expect(rewritten.hasChanges).toBe(false);
        expect(Number.isNaN(rewritten.at(0))).toBe(true);

        // `Object.is` separates the signed zeros, which JavaScript can observe.
        const signed = source.setItem(2, -0);
        expect(signed.hasChanges).toBe(true);
        expect(shapes(signed)).toEqual([{ start: 2, length: 1 }]);
        expect(Object.is(signed.at(2), -0)).toBe(true);
        expect(signed.validateStructure().dirtyCount).toBe(1);

        // The canonical IEEE-reflexive helper reproduces .NET's default comparer: one NaN class and
        // one zero class.
        const ieee = PersistentRunDeltaVector.from([Number.NaN, 1, 0], reflexiveIeeeEquality);
        expect(ieee.setItem(0, Number.NaN)).toBe(ieee);
        expect(ieee.setItem(2, -0)).toBe(ieee);
        expect(ieee.setItem(2, 5).setItem(2, -0).isClean).toBe(true);
        expect(reflexiveIeeeEquality(Number.NaN, Number.NaN)).toBe(true);
        expect(reflexiveIeeeEquality(-0, 0)).toBe(true);
        expect(naturalValueEquality(Number.NaN, Number.NaN)).toBe(true);
        expect(naturalValueEquality(-0, 0)).toBe(false);

        // A non-reflexive relation is the documented hazard: it records a phantom run that no
        // internal validation can detect, because the phantom agrees with everything the relation
        // says. This is why `===` is not the default.
        const nonReflexive: ValueEquality<number> = (left, right) => left === right;
        const broken = PersistentRunDeltaVector.from([Number.NaN], nonReflexive).setItem(0, Number.NaN);
        expect(broken.dirtyCount).toBe(1);
        expect(shapes(broken)).toEqual([{ start: 0, length: 1 }]);
        expect(() => broken.validateStructure()).not.toThrow();
    });

    test("point edits keep the run index maximal, non-overlapping, and non-adjacent", () => {
        const source = zeros(12);
        const model = Array.from({ length: 12 }, () => 0);
        const clean = model.slice();

        let vector = source.setItem(2, 20).setItem(4, 40);
        model[2] = 20; model[4] = 40;
        expect(shapes(vector)).toEqual([{ start: 2, length: 1 }, { start: 4, length: 1 }]);
        expectAgreesWithModel(vector, model, clean);

        // A new position between two singleton runs merges both.
        vector = vector.setItem(3, 30); model[3] = 30;
        expect(shapes(vector)).toEqual([{ start: 2, length: 3 }]);
        expectAgreesWithModel(vector, model, clean);

        // A detached position (fresh singleton), then the gap that joins it leftwards.
        vector = vector.setItem(6, 60); model[6] = 60;
        expect(shapes(vector)).toEqual([{ start: 2, length: 3 }, { start: 6, length: 1 }]);
        vector = vector.setItem(5, 50); model[5] = 50;
        expect(shapes(vector)).toEqual([{ start: 2, length: 5 }]);

        // Extending leftwards from a run's start, which rekeys the record.
        vector = vector.setItem(1, 10); model[1] = 10;
        expect(shapes(vector)).toEqual([{ start: 1, length: 6 }]);
        expectAgreesWithModel(vector, model, clean);

        // Interior clearing splits one run into two.
        vector = vector.resetItem(4); model[4] = 0;
        expect(shapes(vector)).toEqual([{ start: 1, length: 3 }, { start: 5, length: 2 }]);
        // Left-edge clearing shrinks from the start; right-edge clearing shrinks from the end.
        vector = vector.resetItem(1); model[1] = 0;
        expect(shapes(vector)).toEqual([{ start: 2, length: 2 }, { start: 5, length: 2 }]);
        vector = vector.resetItem(6); model[6] = 0;
        expect(shapes(vector)).toEqual([{ start: 2, length: 2 }, { start: 5, length: 1 }]);
        expectAgreesWithModel(vector, model, clean);

        // Clearing the last dirty position canonicalizes the current root onto the checkpoint root.
        const cleared = vector.resetItem(2).resetItem(3).resetItem(5);
        expect(cleared.hasChanges).toBe(false);
        expect(cleared.dirtyRunCount).toBe(0);
        expect(cleared.isClean).toBe(true);
        expect(cleared.toArray()).toEqual(clean);
        expect(cleared).not.toBe(source);
        expect(cleared.sharesCurrentRootWith(source)).toBe(true);
        expect(cleared.sharedLeafCount()).toBe(source.sharedLeafCount());
        expect(vector.sharesCurrentRootWith(source)).toBe(false);
        expect(cleared.resetItem(3)).toBe(cleared);
        expectAgreesWithModel(cleared, clean, clean);
    });

    test("accepting or reverting one run touches only that run and makes no relation calls", () => {
        const relation = counting(naturalValueEquality<number>);
        const original = PersistentRunDeltaVector.from(Array.from({ length: 20 }, (_, index) => index), relation.equals);
        let edited = original;
        for (const index of [2, 3, 4, 5, 9, 10, 11, 12, 13, 17]) edited = edited.setItem(index, 1000 + index);
        const expected: RunShape[] = [{ start: 2, length: 4 }, { start: 9, length: 5 }, { start: 17, length: 1 }];
        expect(shapes(edited)).toEqual(expected);
        expect(edited.dirtyCount).toBe(10);
        // Positive control: the counter really is wired to the edits above.
        expect(relation.calls()).toBeGreaterThan(0);

        const beforeAccept = relation.calls();
        const acceptedMiddle = edited.acceptDirtyRunAt(1);
        expect(relation.calls()).toBe(beforeAccept);
        expect(shapes(acceptedMiddle)).toEqual([{ start: 2, length: 4 }, { start: 17, length: 1 }]);
        expect(acceptedMiddle.dirtyCount).toBe(5);
        for (let index = 9; index < 14; index++) {
            expect(acceptedMiddle.at(index)).toBe(1000 + index);
            expect(acceptedMiddle.checkpointAt(index)).toBe(1000 + index);
            expect(acceptedMiddle.sharesRepresentativeAt(index)).toBe(true);
        }
        // The current root is carried over untouched; the checkpoint root is a fresh splice.
        expect(acceptedMiddle).not.toBe(edited);
        expect(acceptedMiddle.sharesCurrentRootWith(edited)).toBe(true);
        expect(acceptedMiddle.sharesCheckpointRootWith(edited)).toBe(false);

        const beforeRevert = relation.calls();
        const revertedFirst = acceptedMiddle.revertDirtyRunAt(0);
        expect(relation.calls()).toBe(beforeRevert);
        expect(shapes(revertedFirst)).toEqual([{ start: 17, length: 1 }]);
        for (let index = 2; index < 6; index++) {
            expect(revertedFirst.at(index)).toBe(index);
            expect(revertedFirst.sharesRepresentativeAt(index)).toBe(true);
        }
        // The mirror image: the checkpoint root is carried over and the current root is respliced.
        expect(revertedFirst.sharesCheckpointRootWith(acceptedMiddle)).toBe(true);
        expect(revertedFirst.sharesCurrentRootWith(acceptedMiddle)).toBe(false);

        // The final run collapses to a whole checkpoint, which is the O(1) path.
        const beforeFinal = relation.calls();
        const clean = revertedFirst.acceptDirtyRunAt(0);
        expect(relation.calls()).toBe(beforeFinal);
        expect(clean.hasChanges).toBe(false);
        expect(clean.isClean).toBe(true);
        expect(clean.checkpointAt(17)).toBe(1017);
        expect(clean.sharesCurrentRootWith(revertedFirst)).toBe(true);

        // Every retained version is unchanged and independently valid.
        expect(shapes(edited)).toEqual(expected);
        expect(original.hasChanges).toBe(false);
        expect(original.toArray()).toEqual(Array.from({ length: 20 }, (_, index) => index));
        for (const version of [original, edited, acceptedMiddle, revertedFirst, clean]) version.validateStructure();
        expect(() => edited.acceptDirtyRunAt(3)).toThrow(RangeError);
        expect(() => edited.revertDirtyRunAt(3)).toThrow(RangeError);
    });

    test("a run splice costs no comparisons and no walk however long the run is", () => {
        const length = 4096;
        const relation = counting(naturalValueEquality<number>);
        const original = PersistentRunDeltaVector.from(Array.from({ length }, (_, index) => index), relation.equals);
        let edited = original;
        for (let index = 500; index < 3500; index++) edited = edited.setItem(index, -index);
        edited = edited.setItem(4000, -4000);
        expect(shapes(edited)).toEqual([{ start: 500, length: 3000 }, { start: 4000, length: 1 }]);

        const beforeLong = relation.calls();
        const acceptedLong = edited.acceptDirtyRunAt(0);
        const longCalls = relation.calls() - beforeLong;
        const beforeShort = acceptedLong.dirtyRunCount;
        expect(beforeShort).toBe(1);
        expect(longCalls).toBe(0);
        expect(shapes(acceptedLong)).toEqual([{ start: 4000, length: 1 }]);
        expect(acceptedLong.dirtyCount).toBe(1);
        // Splicing a 3000-position run reused whole subtrees rather than rebuilding them: the two
        // roots still share the overwhelming majority of their leaves.
        expect(acceptedLong.sharedLeafCount()).toBeGreaterThan(100);
        expect(acceptedLong.checkpointAt(500)).toBe(-500);
        expect(acceptedLong.checkpointAt(3499)).toBe(-3499);
        expect(acceptedLong.checkpointAt(499)).toBe(499);
        expect(acceptedLong.checkpointAt(3500)).toBe(3500);
        expect(acceptedLong.checkpointAt(4000)).toBe(4000);
        expect(acceptedLong.at(4000)).toBe(-4000);
        acceptedLong.validateStructure();

        // Reverting the same 3000-position run is equally comparison-free.
        const beforeRevert = relation.calls();
        const revertedLong = edited.revertDirtyRunAt(0);
        expect(relation.calls() - beforeRevert).toBe(0);
        expect(revertedLong.toArray().slice(500, 3500)).toEqual(Array.from({ length: 3000 }, (_, offset) => 500 + offset));
        expect(revertedLong.dirtyCount).toBe(1);
        revertedLong.validateStructure();
    });

    test("runs are addressable by rank and by any position they contain", () => {
        const relation = counting(naturalValueEquality<number>);
        const original = PersistentRunDeltaVector.from(Array.from({ length: 16 }, (_, index) => index), relation.equals);
        let edited = original;
        for (const index of [2, 3, 4, 5, 9, 10, 11]) edited = edited.setItem(index, 1000 + index);
        const expected: RunShape[] = [{ start: 2, length: 4 }, { start: 9, length: 3 }];
        expect(shapes(edited)).toEqual(expected);

        for (let rank = 0; rank < expected.length; rank++) {
            const run = edited.dirtyRunAt(rank);
            for (let index = run.start; index < run.endExclusive; index++) {
                const containing = edited.dirtyRunContaining(index);
                expect(containing?.start).toBe(run.start);
                expect(containing?.length).toBe(run.length);
                expect(edited.acceptDirtyRunContaining(index).toArray()).toEqual(edited.acceptDirtyRunAt(rank).toArray());
                expect(shapes(edited.acceptDirtyRunContaining(index))).toEqual(shapes(edited.acceptDirtyRunAt(rank)));
                expect(shapes(edited.revertDirtyRunContaining(index))).toEqual(shapes(edited.revertDirtyRunAt(rank)));
                expect(edited.revertDirtyRunContaining(index).toArray()).toEqual(edited.revertDirtyRunAt(rank).toArray());
            }
        }

        // A clean position is vacuous rather than an error, and consults no relation.
        const before = relation.calls();
        for (const index of [0, 1, 6, 8, 12, 15]) {
            expect(edited.isDirty(index)).toBe(false);
            expect(edited.dirtyRunContaining(index)).toBeUndefined();
            // Both return the receiver, so receiver identity is the assertion that can fail; the
            // paired sharing assertions live on the distinct versions produced above.
            expect(edited.acceptDirtyRunContaining(index)).toBe(edited);
            expect(edited.revertDirtyRunContaining(index)).toBe(edited);
        }
        expect(relation.calls()).toBe(before);
    });

    test("dirty-run enumeration is output-optimal in the number of runs", () => {
        const relation = counting(naturalValueEquality<number>);
        const sparse = (() => {
            let vector = PersistentRunDeltaVector.from(Array.from({ length: 1024 }, () => 0), relation.equals);
            for (let index = 100; index < 800; index++) vector = vector.setItem(index, 1);
            return vector;
        })();
        expect(sparse.dirtyCount).toBe(700);
        expect(sparse.dirtyRunCount).toBe(1);

        // The walk emits one descriptor per run, never one per dirty position, and consults no
        // relation on the way.
        const before = relation.calls();
        let steps = 0;
        for (const run of sparse.dirtyRuns()) { steps++; expect(run.length).toBe(700); }
        expect(steps).toBe(1);
        expect(relation.calls()).toBe(before);

        // Same run count, an order of magnitude fewer dirty positions: the same amount of work.
        let narrow = PersistentRunDeltaVector.from(Array.from({ length: 1024 }, () => 0));
        for (let index = 100; index < 110; index++) narrow = narrow.setItem(index, 1);
        let narrowSteps = 0;
        for (const _ of narrow.dirtyRuns()) narrowSteps++;
        expect(narrowSteps).toBe(steps);
        expect(narrow.dirtyCount).toBe(10);

        // The walk is lazy, so abandoning it early skips the rest. Pulling two values proves
        // nothing on its own — an iterator over a fully materialised array satisfies that
        // identically — so the laziness itself is observed two ways: the returned object is a
        // generator (an eager `all[Symbol.iterator]()` would be an Array Iterator), and it is its
        // own iterable, which a generator guarantees and an array does not survive being reused.
        let many = PersistentRunDeltaVector.from(Array.from({ length: 64 }, () => 0));
        for (let index = 0; index < 64; index += 2) many = many.setItem(index, 1);
        expect(many.dirtyRunCount).toBe(32);
        const iterator = many.dirtyRuns();
        expect(Object.prototype.toString.call(iterator)).toBe("[object Generator]");
        expect(iterator[Symbol.iterator]()).toBe(iterator);
        expect(iterator.next().value?.start).toBe(0);
        expect(iterator.next().value?.start).toBe(2);
        // Abandoning here must not have produced the remaining 30 descriptors; resuming still
        // yields the next one in order, which a spent array iterator could not do.
        expect(iterator.next().value?.start).toBe(4);
    });

    test("positions and ranks outside the vector throw RangeError", () => {
        const vector = zeros(4).setItem(1, 9);
        for (const index of [-1, 4, 4.5, Number.NaN]) {
            expect(() => vector.at(index)).toThrow(RangeError);
            expect(() => vector.checkpointAt(index)).toThrow(RangeError);
            expect(() => vector.setItem(index, 1)).toThrow(RangeError);
            expect(() => vector.resetItem(index)).toThrow(RangeError);
            expect(() => vector.isDirty(index)).toThrow(RangeError);
            expect(() => vector.dirtyRunContaining(index)).toThrow(RangeError);
            expect(() => vector.sharesRepresentativeAt(index)).toThrow(RangeError);
            expect(() => vector.acceptDirtyRunContaining(index)).toThrow(RangeError);
            expect(() => vector.revertDirtyRunContaining(index)).toThrow(RangeError);
        }
        for (const rank of [-1, 1, 0.5]) {
            expect(() => vector.dirtyRunAt(rank)).toThrow(RangeError);
            expect(() => vector.acceptDirtyRunAt(rank)).toThrow(RangeError);
            expect(() => vector.revertDirtyRunAt(rank)).toThrow(RangeError);
        }
        const empty = PersistentRunDeltaVector.empty<number>();
        expect(() => empty.at(0)).toThrow(RangeError);
        expect(() => empty.dirtyRunAt(0)).toThrow(RangeError);
        expect(() => empty.acceptDirtyRunContaining(0)).toThrow(RangeError);
    });

    test("whole checkpoint and rollback are O(1) root swaps that leave branches usable", () => {
        const source = PersistentRunDeltaVector.from([1, 2, 3, 4]);
        const edited = source.setItem(1, 20).setItem(3, 40);
        expect(shapes(edited)).toEqual([{ start: 1, length: 1 }, { start: 3, length: 1 }]);

        const accepted = edited.checkpoint();
        expect(accepted).not.toBe(edited);
        expect(accepted.isClean).toBe(true);
        expect(accepted.sharesCurrentRootWith(edited)).toBe(true);
        expect(accepted.sharesCheckpointRootWith(edited)).toBe(false);
        expect(accepted.checkpointToArray()).toEqual([1, 20, 3, 40]);
        expect(accepted.checkpoint()).toBe(accepted);

        const rolled = edited.rollback();
        expect(rolled).not.toBe(edited);
        expect(rolled.isClean).toBe(true);
        expect(rolled.toArray()).toEqual([1, 2, 3, 4]);
        expect(rolled.sharesCheckpointRootWith(edited)).toBe(true);
        expect(rolled.sharesCurrentRootWith(edited)).toBe(false);
        expect(rolled.rollback()).toBe(rolled);

        // Two branches derived from one retained parent stay independent.
        const branchA = edited.setItem(0, 100);
        const branchB = edited.setItem(2, 300);
        expect(branchA.toArray()).toEqual([100, 20, 3, 40]);
        expect(branchB.toArray()).toEqual([1, 20, 300, 40]);
        expect(edited.toArray()).toEqual([1, 20, 3, 40]);
        for (const version of [source, edited, accepted, rolled, branchA, branchB]) version.validateStructure();
    });

    test("randomized histories agree with an array model", () => {
        const operation = fc.tuple(fc.nat(11), fc.integer({ min: 0, max: 3 }), fc.nat(99));
        fc.assert(fc.property(fc.array(operation, { maxLength: 120 }), (operations) => {
            let vector = PersistentRunDeltaVector.from(Array.from({ length: 12 }, (_, index) => index % 4));
            let current = vector.toArray();
            let checkpoint = current.slice();
            const retained: Array<readonly [PersistentRunDeltaVector<number>, number[], number[]]> = [];
            for (const [position, value, choice] of operations) {
                if (choice < 55) {
                    vector = vector.setItem(position, value);
                    current[position] = value;
                } else if (choice < 75) {
                    vector = vector.resetItem(position);
                    current[position] = checkpoint[position]!;
                } else if (choice < 82) {
                    vector = vector.checkpoint();
                    checkpoint = current.slice();
                } else if (choice < 89) {
                    vector = vector.rollback();
                    current = checkpoint.slice();
                } else {
                    const runs = modelRuns(current, checkpoint, naturalValueEquality);
                    if (runs.length === 0) continue;
                    const run = runs[choice % runs.length]!;
                    const accept = choice % 2 === 0;
                    vector = accept ? vector.acceptDirtyRunAt(choice % runs.length) : vector.revertDirtyRunAt(choice % runs.length);
                    for (let index = run.start; index < run.start + run.length; index++) {
                        if (accept) checkpoint[index] = current[index]!;
                        else current[index] = checkpoint[index]!;
                    }
                }
                if (choice % 23 === 0) retained.push([vector, current.slice(), checkpoint.slice()]);
            }
            expectAgreesWithModel(vector, current, checkpoint);
            for (const [snapshot, expectedCurrent, expectedCheckpoint] of retained) {
                expect(snapshot.toArray()).toEqual(expectedCurrent);
                expect(snapshot.checkpointToArray()).toEqual(expectedCheckpoint);
                snapshot.validateStructure();
            }
        }), { numRuns: 60 });
    });

    test("randomized edits under a coarse relation keep exact representatives", () => {
        const buckets: ValueEquality<number> = (left, right) => Math.trunc(left / 10) === Math.trunc(right / 10);
        fc.assert(fc.property(fc.array(fc.tuple(fc.nat(7), fc.integer({ min: 0, max: 39 })), { maxLength: 60 }), (operations) => {
            const seed = [0, 10, 20, 30, 0, 10, 20, 30];
            let vector = PersistentRunDeltaVector.from(seed, buckets);
            const current = seed.slice();
            for (const [position, value] of operations) {
                const next = vector.setItem(position, value);
                if (buckets(current[position]!, value)) {
                    // A relation-equal write is a no-op that keeps the existing representative.
                    expect(next).toBe(vector);
                } else {
                    expect(next).not.toBe(vector);
                    // A write landing back in the checkpoint's class cancels the delta and restores
                    // the exact checkpoint representative, so the stored value is the seed's value
                    // and not the merely equivalent one just written.
                    current[position] = buckets(seed[position]!, value) ? seed[position]! : value;
                }
                vector = next;
            }
            expectAgreesWithModel(vector, current, seed, buckets);
            // Every clean position holds the very cell the checkpoint holds.
            for (let index = 0; index < seed.length; index++) {
                expect(vector.sharesRepresentativeAt(index)).toBe(!vector.isDirty(index));
            }
        }), { numRuns: 60 });
    });
});
