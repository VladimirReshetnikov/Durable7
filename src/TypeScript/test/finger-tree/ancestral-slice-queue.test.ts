/**
 * Tests for the ancestral slice queue: the interval algebra over an append-only ancestry path.
 *
 * Two things here are load-bearing and cannot be checked by value comparison alone. The first is
 * the anchored-empty rule: every empty value retains the node immediately *before* its window, so
 * the test pins the anchor depth and identity, not merely that appending to an empty slice yields a
 * one-element queue. The second is the query discipline: the specializations that keep the boundary
 * operations query-free are invisible in the returned values, so they are pinned with exact
 * `statistics().ancestorQueryCount` deltas — equalities, never ceilings — read off an arena the test
 * owns. A `statistics()` call is deliberately absent from the seam interface, so the queue is handed
 * an arena the test constructed and the counters are read from that reference.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import { AncestralSliceQueue } from "../../src/finger-tree/ancestral-slice-queue.js";
import { MyersIncrementalAncestorArena, type IncrementalAncestorArena } from "../../src/finger-tree/incremental-ancestor.js";

/** An arena whose bottom node is not at depth -1, which the queue must refuse to anchor on. */
class FlatBottomArena implements IncrementalAncestorArena<number> {
    public readonly bottom: number = 0;
    public readonly publishedNodeCount: number = 0;
    public addLeaf(): number { throw new Error("The queue must reject this arena before using it."); }
    public depthOf(): number { return 0; }
    public parentOf(): number { throw new Error("The queue must reject this arena before using it."); }
    public ancestorAtDepth(): number { throw new Error("The queue must reject this arena before using it."); }
    public valueAt(): number { throw new Error("The queue must reject this arena before using it."); }
}

/** Every read the reference `AssertState` helper checks, against one expected model. */
function assertState<T>(queue: AncestralSliceQueue<T>, expected: readonly T[]): void {
    expect(queue.count).toBe(expected.length);
    expect(queue.isEmpty).toBe(expected.length === 0);
    expect(queue.toArray()).toEqual([...expected]);
    expect([...queue]).toEqual([...expected]);
    for (let index = 0; index < expected.length; index += 1) expect(queue.getAt(index)).toEqual(expected[index]);
    expect(() => queue.getAt(expected.length)).toThrow(RangeError);
    expect(() => queue.getAt(-1)).toThrow(RangeError);
    expect(() => queue.getAt(0.5)).toThrow(RangeError);

    if (expected.length === 0) {
        expect(() => queue.first).toThrow(RangeError);
        expect(() => queue.last).toThrow(RangeError);
        expect(() => queue.removeFirst()).toThrow(RangeError);
        expect(() => queue.removeLast()).toThrow(RangeError);
        expect(queue.tryRemoveFirst()).toBeUndefined();
        expect(queue.tryRemoveLast()).toBeUndefined();
    } else {
        expect(queue.first).toEqual(expected[0]);
        expect(queue.last).toEqual(expected[expected.length - 1]);
    }

    // The audit walks parent links only, so it re-derives the window without an ancestor query.
    const statistics = queue.validateStructure();
    expect(statistics.count).toBe(expected.length);
    expect(statistics.tailDepth).toBe(statistics.lowDepth + expected.length - 1);
    expect(statistics.anchorDepth).toBe(statistics.lowDepth - 1);
    expect(statistics.anchoredAtBottom).toBe(statistics.anchorDepth === -1);
}

function counted(length: number): { readonly arena: MyersIncrementalAncestorArena<number>; readonly queue: AncestralSliceQueue<number> } {
    const arena = new MyersIncrementalAncestorArena<number>();
    let queue = AncestralSliceQueue.withArena<number>(arena);
    for (let value = 0; value < length; value += 1) queue = queue.addLast(value);
    return { arena, queue };
}

/** Pins exact leaf-add and ancestor-query deltas around one action. */
function expectDelta(arena: MyersIncrementalAncestorArena<number>, adds: number, queries: number, action: () => void): void {
    const before = arena.statistics();
    action();
    const after = arena.statistics();
    expect(after.addLeafCount - before.addLeafCount).toBe(adds);
    expect(after.publishedNodeCount - before.publishedNodeCount).toBe(adds);
    expect(after.ancestorQueryCount - before.ancestorQueryCount).toBe(queries);
}

function range(length: number): number[] { return Array.from({ length }, (_unused, index) => index); }

interface Version { readonly queue: AncestralSliceQueue<number>; readonly model: readonly number[] }

function appended(source: AncestralSliceQueue<number>, model: readonly number[], marker: number): Version {
    return { queue: source.addLast(marker), model: [...model, marker] };
}

function applyStep(source: AncestralSliceQueue<number>, model: readonly number[], operation: number, first: number, second: number, marker: number): Version {
    const length = model.length;
    switch (operation) {
        case 1: return length === 0 ? appended(source, model, marker) : { queue: source.removeFirst(), model: model.slice(1) };
        case 2: return length === 0 ? appended(source, model, marker) : { queue: source.removeLast(), model: model.slice(0, length - 1) };
        case 3: { const count = first % (length + 1); return { queue: source.take(count), model: model.slice(0, count) }; }
        case 4: { const count = first % (length + 1); return { queue: source.drop(count), model: model.slice(count) }; }
        case 5: {
            const start = first % (length + 1);
            const count = second % (length - start + 1);
            return { queue: source.getRange(start, count), model: model.slice(start, start + count) };
        }
        case 6: {
            const position = first % (length + 1);
            const split = source.splitAt(position);
            return second % 2 === 0
                ? { queue: split.left, model: model.slice(0, position) }
                : { queue: split.right, model: model.slice(position) };
        }
        case 7: {
            if (length === 0) return appended(source, model, marker);
            const popped = source.tryRemoveFirst();
            expect(popped).toBeDefined();
            expect(popped!.value).toBe(model[0]);
            return { queue: popped!.rest, model: model.slice(1) };
        }
        case 8: {
            if (length === 0) return appended(source, model, marker);
            const popped = source.tryRemoveLast();
            expect(popped).toBeDefined();
            expect(popped!.value).toBe(model[length - 1]);
            return { queue: popped!.rest, model: model.slice(0, length - 1) };
        }
        default: return appended(source, model, marker);
    }
}

describe("AncestralSliceQueue", () => {
    test("empty, singleton, and stored undefined values follow the endpoint contracts", () => {
        const empty = AncestralSliceQueue.empty<string | undefined>();
        assertState(empty, []);
        expect(empty.validateStructure()).toEqual({ count: 0, lowDepth: 0, tailDepth: -1, anchorDepth: -1, anchoredAtBottom: true, arenaPublishedNodeCount: 0 });

        // A stored `undefined` is a present value; absence is reported by the outer result only.
        const singleton = empty.addLast(undefined);
        assertState(singleton, [undefined]);
        expect(singleton.first).toBeUndefined();
        expect(singleton.last).toBeUndefined();
        expect(singleton.tryRemoveFirst()).toBeDefined();
        expect(singleton.tryRemoveFirst()!.value).toBeUndefined();

        const pair = singleton.addLast("tail");
        assertState(pair, [undefined, "tail"]);
        assertState(singleton, [undefined]);
        assertState(empty, []);

        const front = pair.tryRemoveFirst()!;
        expect(front.value).toBeUndefined();
        assertState(front.rest, ["tail"]);

        const back = pair.tryRemoveLast()!;
        expect(back.value).toBe("tail");
        assertState(back.rest, [undefined]);
        assertState(pair, [undefined, "tail"]);
    });

    test("values are retained by identity rather than copied", () => {
        // Object payloads, because `===` on small integers and interned strings is value equality
        // and could not tell a retained node from a rebuilt one.
        const payloads = range(8).map((id) => ({ id }));
        const queue = AncestralSliceQueue.from(payloads);
        for (let index = 0; index < payloads.length; index += 1) expect(queue.getAt(index)).toBe(payloads[index]);
        expect(queue.first).toBe(payloads[0]);
        expect(queue.last).toBe(payloads[7]);
        expect(queue.toArray()[3]).toBe(payloads[3]);
        expect([...queue][5]).toBe(payloads[5]);
        expect(queue.getRange(2, 3).toArray()).toEqual([payloads[2], payloads[3], payloads[4]]);
    });

    test("every range matches the model and empty anchors remain appendable", () => {
        const length = 65;
        const values = range(length);
        const source = AncestralSliceQueue.from(values);

        for (let start = 0; start <= length; start += 1) {
            for (let count = 0; count <= length - start; count += 1) {
                const slice = source.getRange(start, count);
                assertState(slice, values.slice(start, start + count));

                if (count === 0) {
                    // The anchored-empty rule: appending to any empty slice yields exactly the new
                    // values and never resurrects the discarded prefix.
                    const marker = start;
                    assertState(slice.addLast(-marker).addLast(10_000 + marker), [-marker, 10_000 + marker]);
                    assertState(slice, []);
                    // The anchor is the node immediately before the window, not the window's start.
                    expect(slice.validateStructure().anchorDepth).toBe(start - 1);
                }
            }
        }

        assertState(source.getRange(0, length), values);
        assertState(source.take(length), values);
        assertState(source.drop(0), values);
        assertState(source, values);
    });

    test("take, drop and splitAt partition the source at every boundary", () => {
        const values = range(129);
        const source = AncestralSliceQueue.from(values);

        for (let position = 0; position <= values.length; position += 1) {
            const prefix = source.take(position);
            const suffix = source.drop(position);
            assertState(prefix, values.slice(0, position));
            assertState(suffix, values.slice(position));

            const split = source.splitAt(position);
            assertState(split.left, values.slice(0, position));
            assertState(split.right, values.slice(position));

            // Both results are real ancestry slices and start independent branches.
            const marker = -position - 1;
            assertState(prefix.addLast(marker), [...values.slice(0, position), marker]);
            assertState(suffix.addLast(marker), [...values.slice(position), marker]);
        }

        assertState(source, values);
    });

    test("square and odd block boundaries support index, range and branching", () => {
        const maximumRoot = 32;
        const values = range(maximumRoot * maximumRoot + 1);
        const source = AncestralSliceQueue.from(values);

        for (let root = 0; root <= maximumRoot; root += 1) {
            const square = root * root;
            expect(source.getAt(square)).toBe(square);

            const start = Math.max(square - 1, 0);
            const count = Math.min(3, source.count - start);
            assertState(source.getRange(start, count), values.slice(start, start + count));

            const marker = -square - 1;
            assertState(source.getRange(square, 0).addLast(marker), [marker]);

            if (square > 0) {
                // The queue length equals the tail handle here, so the versions of length square-1
                // and square end immediately before and at the odd-block seam.
                assertState(source.take(square - 1).addLast(marker - 1), [...values.slice(0, square - 1), marker - 1]);
                assertState(source.take(square).addLast(marker - 2), [...values.slice(0, square), marker - 2]);
            }

            if (root !== maximumRoot) {
                const nextSquare = (root + 1) * (root + 1);
                // Read the arena's real block schedule rather than restating the integer identity
                // (r+1)^2 - r^2 = 2r+1, which holds for every implementation and so proved nothing:
                // block k must hold exactly 2k+1 slots, so the totals are consecutive squares.
                const blocks = (source.arena as MyersIncrementalAncestorArena<number>).statistics();
                expect(blocks.allocatedSlotCount).toBe(blocks.blockCount * blocks.blockCount);
                expect(nextSquare - square).toBe(2 * root + 1);
                assertState(source.getRange(square, nextSquare - square), values.slice(square, nextSquare));
            }
        }

        expect(source.count).toBe(values.length);
        expect(source.toArray()).toEqual(values);
    });

    test("a front-drained and a back-drained queue keep different anchors", () => {
        const values = range(6);
        const source = AncestralSliceQueue.from(values);
        let front = source;
        let back = source;
        for (let step = 0; step < values.length; step += 1) {
            front = front.removeFirst();
            back = back.removeLast();
            assertState(front, values.slice(step + 1));
            assertState(back, values.slice(0, values.length - step - 1));
        }

        assertState(front, []);
        assertState(back, []);
        const frontAnchor = front.validateStructure();
        const backAnchor = back.validateStructure();
        expect(frontAnchor.lowDepth).toBe(values.length);
        expect(frontAnchor.anchorDepth).toBe(values.length - 1);
        expect(frontAnchor.anchoredAtBottom).toBe(false);
        expect(backAnchor.lowDepth).toBe(0);
        expect(backAnchor.anchorDepth).toBe(-1);
        expect(backAnchor.anchoredAtBottom).toBe(true);
        expect(front.tailHandle).not.toBe(back.tailHandle);
        expect(front.sharesTailWith(back)).toBe(false);
        expect(front.sharesArenaWith(back)).toBe(true);

        // Both denote the empty sequence, and neither resurrects the discarded prefix.
        assertState(front.addLast(-1), [-1]);
        assertState(back.addLast(-2), [-2]);
        // The operative half of the rule: the two appends must hang off the two DIFFERENT retained
        // anchors. Comparing the appended tail handles instead would be vacuous, because the arena
        // never recycles a handle, so two separate addLeaf calls always differ regardless of anchor.
        const arena = source.arena as MyersIncrementalAncestorArena<number>;
        expect(arena.parentOf(front.addLast(-1).tailHandle)).toBe(front.tailHandle);
        expect(arena.parentOf(back.addLast(-2).tailHandle)).toBe(back.tailHandle);
        expect(arena.parentOf(front.addLast(-1).tailHandle)).not.toBe(arena.parentOf(back.addLast(-2).tailHandle));
        assertState(source, values);
    });

    test("whole-window and suffix results reuse the source tail; interior ones rebuild it", () => {
        const values = range(12);
        const source = AncestralSliceQueue.from(values);

        // These operations return the receiver, so identity is the honest claim; a sharing
        // predicate applied to them would be a self-comparison that cannot fail.
        expect(source.take(12)).toBe(source);
        expect(source.drop(0)).toBe(source);
        expect(source.getRange(0, 12)).toBe(source);
        expect(source.splitAt(12).left).toBe(source);
        expect(source.splitAt(0).right).toBe(source);

        // Genuinely distinct values that nonetheless retain the very same arena node as their tail.
        const derived = [source.getRange(4, 8), source.drop(4), source.getRange(12, 0), source.splitAt(4).right];
        for (const value of derived) {
            expect(value).not.toBe(source);
            expect(value.sharesArenaWith(source)).toBe(true);
            expect(value.sharesTailWith(source)).toBe(true);
            expect(value.tailHandle).toBe(source.tailHandle);
        }
        assertState(derived[0]!, values.slice(4));
        assertState(derived[2]!, []);

        // Negative controls: an interior range must move its tail, and an independently built twin
        // shares neither the arena nor the tail despite enumerating equal values.
        const interior = source.getRange(4, 5);
        expect(interior.sharesArenaWith(source)).toBe(true);
        expect(interior.sharesTailWith(source)).toBe(false);
        expect(interior.tailHandle).not.toBe(source.tailHandle);
        const twin = AncestralSliceQueue.from(source.toArray());
        expect(twin.toArray()).toEqual(source.toArray());
        expect(twin.sharesArenaWith(source)).toBe(false);
        expect(twin.sharesTailWith(source)).toBe(false);
    });

    test("boundary operations make exactly the documented ancestor queries", () => {
        const { arena, queue } = counted(16);
        expect(arena.statistics().addLeafCount).toBe(16);

        expectDelta(arena, 0, 0, () => {
            expect(queue.count).toBe(16);
            expect(queue.isEmpty).toBe(false);
            expect(queue.last).toBe(15);
            // Indexed access at the last visible index uses the retained tail.
            expect(queue.getAt(15)).toBe(15);
            expect(queue.toArray()).toEqual(range(16));
            expect(queue.validateStructure().count).toBe(16);
        });
        expectDelta(arena, 0, 1, () => { expect(queue.first).toBe(0); });
        expectDelta(arena, 0, 1, () => { expect(queue.getAt(7)).toBe(7); });
        expectDelta(arena, 0, 0, () => { expect(queue.removeFirst().count).toBe(15); });
        expectDelta(arena, 0, 0, () => { expect(queue.removeLast().count).toBe(15); });
        expectDelta(arena, 0, 0, () => { expect(queue.drop(0)).toBe(queue); });
        expectDelta(arena, 0, 0, () => { expect(queue.drop(7).count).toBe(9); });
        expectDelta(arena, 0, 0, () => { expect(queue.take(16)).toBe(queue); });
        expectDelta(arena, 0, 1, () => { expect(queue.take(7).count).toBe(7); });
        expectDelta(arena, 0, 0, () => { expect(queue.getRange(0, 16)).toBe(queue); });
        expectDelta(arena, 0, 0, () => { expect(queue.getRange(3, 13).count).toBe(13); });
        expectDelta(arena, 0, 0, () => { expect(queue.getRange(16, 0).count).toBe(0); });
        expectDelta(arena, 0, 1, () => { expect(queue.getRange(3, 5).count).toBe(5); });
        expectDelta(arena, 0, 1, () => { expect(queue.getRange(3, 0).count).toBe(0); });
        expectDelta(arena, 0, 1, () => {
            const split = queue.splitAt(7);
            expect(split.left.count).toBe(7);
            expect(split.right.count).toBe(9);
        });
        expectDelta(arena, 0, 0, () => {
            // A split at the length reuses the source tail for the prefix and only moves the low
            // boundary for the empty suffix.
            const split = queue.splitAt(16);
            expect(split.left).toBe(queue);
            expect(split.right.count).toBe(0);
        });
        expectDelta(arena, 0, 1, () => {
            // A split at 0 of a non-empty queue costs one: the anchored-empty rule makes the empty
            // prefix retain the node at lowDepth - 1. At lowDepth === 0 that node is the arena's
            // bottom, an O(1) read, so this uniform query is a choice rather than a necessity.
            const split = queue.splitAt(0);
            expect(split.left.count).toBe(0);
            expect(split.right).toBe(queue);
        });
        expectDelta(arena, 0, 1, () => {
            const popped = queue.tryRemoveFirst()!;
            expect(popped.value).toBe(0);
            expect(popped.rest.count).toBe(15);
        });
        expectDelta(arena, 0, 0, () => {
            const popped = queue.tryRemoveLast()!;
            expect(popped.value).toBe(15);
            expect(popped.rest.count).toBe(15);
        });
        expectDelta(arena, 1, 0, () => { expect(queue.addLast(16).last).toBe(16); });

        // On an empty queue both split boundaries coincide, so the split is query-free.
        const emptyArena = new MyersIncrementalAncestorArena<number>();
        const empty = AncestralSliceQueue.withArena<number>(emptyArena);
        expectDelta(emptyArena, 0, 0, () => {
            const split = empty.splitAt(0);
            expect(split.left).toBe(empty);
            expect(split.right).toBe(empty);
            expect(empty.take(0)).toBe(empty);
            expect(empty.drop(0)).toBe(empty);
            expect(empty.getRange(0, 0)).toBe(empty);
        });

        // A singleton reads its front from the retained tail rather than through a query.
        const { arena: oneArena, queue: one } = counted(1);
        expectDelta(oneArena, 0, 0, () => {
            expect(one.first).toBe(0);
            expect(one.last).toBe(0);
            expect(one.getAt(0)).toBe(0);
        });
    });

    test("iteration is repeatable and never observes a later branch", () => {
        const values = range(100);
        const source = AncestralSliceQueue.from(values);
        const slice = source.getRange(20, 40);
        const captured = slice[Symbol.iterator]();

        const branch = slice.addLast(-1).addLast(-2);

        expect(slice.toArray()).toEqual(values.slice(20, 60));
        expect(slice.toArray()).toEqual(values.slice(20, 60));
        expect(branch.toArray()).toEqual([...values.slice(20, 60), -1, -2]);
        expect([...captured]).toEqual(values.slice(20, 60));
        assertState(source, values);
    });

    test("retained branches from whole and sliced versions stay independent", () => {
        const values = range(200);
        const source = AncestralSliceQueue.from(values);
        const middle = source.getRange(50, 100);

        const branches = [
            middle.addLast(-1),
            middle.addLast(-2).addLast(-3),
            middle.removeFirst().addLast(-4),
            middle.removeLast().addLast(-5),
            middle.take(40).addLast(-6),
            middle.drop(60).addLast(-7),
            middle.getRange(30, 0).addLast(-8),
        ];

        assertState(branches[0]!, [...values.slice(50, 150), -1]);
        assertState(branches[1]!, [...values.slice(50, 150), -2, -3]);
        assertState(branches[2]!, [...values.slice(51, 150), -4]);
        assertState(branches[3]!, [...values.slice(50, 149), -5]);
        assertState(branches[4]!, [...values.slice(50, 90), -6]);
        assertState(branches[5]!, [...values.slice(110, 150), -7]);
        assertState(branches[6]!, [-8]);
        assertState(middle, values.slice(50, 150));
        assertState(source, values);
    });

    test("out-of-range positions are rejected without changing the source", () => {
        const source = AncestralSliceQueue.from([10, 20, 30]);

        expect(() => source.getAt(3)).toThrow(RangeError);
        expect(() => source.getAt(-1)).toThrow(RangeError);
        expect(() => source.getRange(4, 0)).toThrow(RangeError);
        expect(() => source.getRange(-1, 0)).toThrow(RangeError);
        expect(() => source.getRange(2, 2)).toThrow(RangeError);
        expect(() => source.getRange(2, -1)).toThrow(RangeError);
        expect(() => source.getRange(0, Number.MAX_SAFE_INTEGER)).toThrow(RangeError);
        expect(() => source.take(4)).toThrow(RangeError);
        expect(() => source.take(-1)).toThrow(RangeError);
        expect(() => source.drop(4)).toThrow(RangeError);
        expect(() => source.drop(-1)).toThrow(RangeError);
        expect(() => source.splitAt(4)).toThrow(RangeError);
        expect(() => source.splitAt(-1)).toThrow(RangeError);
        expect(() => source.splitAt(1.5)).toThrow(RangeError);

        assertState(source, [10, 20, 30]);
    });

    test("factories anchor on the supplied arena and reject an unanchored one", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const explicit = AncestralSliceQueue.withArena<number>(arena).addLast(10).addLast(20);
        assertState(explicit, [10, 20]);
        expect(arena.publishedNodeCount).toBe(2);
        expect(explicit.arena).toBe(arena);

        const built = AncestralSliceQueue.from([1, 2, 3]);
        assertState(built, [1, 2, 3]);
        assertState(built.addLast(4), [1, 2, 3, 4]);
        assertState(built, [1, 2, 3]);

        // Each convenience factory owns a private arena, so unrelated queues share no history.
        const first = AncestralSliceQueue.empty<number>().addLast(1);
        const second = AncestralSliceQueue.empty<number>().addLast(2);
        assertState(first, [1]);
        assertState(second, [2]);
        expect(first.sharesArenaWith(second)).toBe(false);
        expect(AncestralSliceQueue.from(built.toArray()).sharesArenaWith(built)).toBe(false);

        expect(() => AncestralSliceQueue.withArena(new FlatBottomArena())).toThrow(TypeError);
    });

    test("random branching histories agree with array models", () => {
        fc.assert(
            fc.property(
                fc.array(fc.tuple(fc.nat(), fc.nat({ max: 9 }), fc.nat(), fc.nat()), { minLength: 30, maxLength: 120 }),
                (steps) => {
                    const histories: Version[] = [{ queue: AncestralSliceQueue.empty<number>(), model: [] }];
                    steps.forEach(([pick, operation, first, second], index) => {
                        const chosen = histories[pick % histories.length]!;
                        const produced = applyStep(chosen.queue, chosen.model, operation, first, second, index);
                        assertState(chosen.queue, chosen.model);
                        assertState(produced.queue, produced.model);
                        histories.push(produced);
                    });
                    for (const version of histories) assertState(version.queue, version.model);
                },
            ),
            { numRuns: 20 },
        );
    });
});
