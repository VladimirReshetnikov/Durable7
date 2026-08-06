/**
 * Tests for the bilateral ancestral deque.
 *
 * Covers the two-interval representation and the O(1) reversal that only exchanges the intervals;
 * the *exact* level-ancestor query profile the query discipline promises - full tables rather than
 * ceilings, including the cross-arm ranges and the four cached endpoints that index with no query at
 * all; the two-value shortcut and the `depth(tail) - count + 2` anchor selection inside a
 * centre-crossing removal; the no-op operations that return a value sharing the source storage, each
 * paired with a negative control so the assertion can fail; endpoint and boundary errors; branch
 * independence across retained versions; the structural audit and its own query cost; and agreement
 * with an array model under exhaustive endpoint-construction words and randomized branching
 * histories.
 *
 * The profile tests construct their own `MyersIncrementalAncestorArena` and read counters off that
 * reference, because `statistics()` belongs to one backend rather than to the arena seam.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
    BilateralAncestralDeque,
    type BilateralAncestralDequeInterval,
} from "../../src/finger-tree/bilateral-ancestral-deque.js";
import {
    MyersIncrementalAncestorArena,
    type IncrementalAncestorArena,
} from "../../src/finger-tree/incremental-ancestor.js";

/**
 * The reference deque used by every query-profile assertion: four values added at the front and four
 * at the back, so both arms hold exactly four values and every cross-arm case is reachable.
 */
const PROFILE_VALUES = [1, 2, 3, 4, 5, 6, 7, 8];

/**
 * Queries spent by `getAt` at each visible index. The four zeros are exactly the cached endpoints:
 * index 0 is the left tail, index 3 the left base, index 4 the right base, index 7 the right tail.
 */
const GET_AT_QUERIES = [0, 1, 1, 0, 0, 1, 1, 0];

/** Indices of the four cached endpoints, which the zeros of `GET_AT_QUERIES` must reproduce. */
const CACHED_ENDPOINT_INDICES = [0, 3, 4, 7];

/**
 * Queries spent by `take(k)` and `drop(k)` for k in 0..8. A prefix or suffix stops inside at most one
 * arm, so neither ever spends more than one query.
 */
const TAKE_QUERIES = [0, 0, 1, 1, 0, 0, 1, 1, 0];
const DROP_QUERIES = [0, 1, 1, 1, 0, 1, 1, 1, 0];

/**
 * Queries spent by `splitAt(k)`; the pair is a prefix plus its complementary suffix, so the
 * documented ceiling of two is reached but never exceeded.
 */
const SPLIT_QUERIES = [0, 1, 2, 2, 0, 1, 2, 2, 0];

/**
 * Queries spent by `getRange(start, count)`; row `start`, column `count`. The ceiling of two holds
 * for the cross-arm rows too, because a crossing range takes a suffix of the left arm and a prefix of
 * the right arm, and each of those spends at most one.
 */
const RANGE_QUERIES = [
    [0, 0, 1, 1, 0, 0, 1, 1, 0],
    [0, 1, 2, 1, 1, 2, 2, 1],
    [0, 1, 1, 1, 2, 2, 1],
    [0, 1, 1, 2, 2, 1],
    [0, 0, 1, 1, 0],
    [0, 1, 2, 1],
    [0, 1, 1],
    [0, 1],
    [0],
];

/** Grows a deque inside a caller-supplied arena so both arms are populated as requested. */
function build<T>(arena: IncrementalAncestorArena<T>, front: readonly T[], back: readonly T[]): BilateralAncestralDeque<T> {
    let deque = BilateralAncestralDeque.empty<T>(arena);
    for (const value of front) deque = deque.addFirst(value);
    for (const value of back) deque = deque.addLast(value);
    return deque;
}

/** Runs an action and reports the level-ancestor queries it caused together with its result. */
function measure<T, R>(arena: MyersIncrementalAncestorArena<T>, action: () => R): { queries: number; result: R } {
    const before = arena.statistics().ancestorQueryCount;
    const result = action();
    return { queries: arena.statistics().ancestorQueryCount - before, result };
}

/** Runs an action and asserts the exact arena additions and ancestor queries it caused. */
function expectArenaDelta<T>(arena: MyersIncrementalAncestorArena<T>, adds: number, queries: number, action: () => unknown): void {
    const publishedBefore = arena.publishedNodeCount;
    const measured = measure(arena, action);
    expect(arena.publishedNodeCount - publishedBefore).toBe(adds);
    expect(measured.queries).toBe(queries);
}

/**
 * Checks the bulk reads and the structural audit against one expected model.
 *
 * The whole comparison is one assertion because the exhaustive word test calls this tens of
 * thousands of times, and per-value assertions dominate its runtime.
 */
function expectValues<T>(deque: BilateralAncestralDeque<T>, expected: readonly T[]): void {
    const statistics = deque.validateStructure();
    expect({
        count: deque.count,
        isEmpty: deque.isEmpty,
        values: deque.toArray(),
        iterated: [...deque],
        indexed: expected.map((_, index) => deque.getAt(index)),
        auditedCount: statistics.count,
        auditedIntervalTotal: statistics.leftCount + statistics.rightCount,
    }).toEqual({
        count: expected.length,
        isEmpty: expected.length === 0,
        values: expected,
        iterated: expected,
        indexed: expected,
        auditedCount: expected.length,
        auditedIntervalTotal: expected.length,
    });
}

/** Checks every read the deque offers against one expected model, and audits the intervals. */
function expectDeque<T>(deque: BilateralAncestralDeque<T>, expected: readonly T[]): void {
    expectValues(deque, expected);
    expect(() => deque.getAt(expected.length)).toThrow(RangeError);
    expect(() => deque.getAt(-1)).toThrow(RangeError);

    if (expected.length === 0) {
        expect(() => deque.first).toThrow(RangeError);
        expect(() => deque.last).toThrow(RangeError);
        expect(() => deque.removeFirst()).toThrow(RangeError);
        expect(() => deque.removeLast()).toThrow(RangeError);
        expect(deque.tryRemoveFirst()).toBeUndefined();
        expect(deque.tryRemoveLast()).toBeUndefined();
    } else {
        expect(deque.first).toEqual(expected[0]);
        expect(deque.last).toEqual(expected[expected.length - 1]);
        const front = deque.tryRemoveFirst()!;
        expect(front.value).toEqual(expected[0]);
        expect(front.rest.toArray()).toEqual(expected.slice(1));
        const back = deque.tryRemoveLast()!;
        expect(back.value).toEqual(expected[expected.length - 1]);
        expect(back.rest.toArray()).toEqual(expected.slice(0, -1));
    }
}

/**
 * An arena whose bottom node reports depth zero, used to prove eager rejection. Every navigating
 * member fails loudly, so a rejected arena that was nevertheless consulted surfaces as an error
 * rather than as a silently wrong deque.
 */
class MisplacedBottomArena implements IncrementalAncestorArena<number> {
    public touched = 0;
    public get bottom(): number { return 0; }
    public get publishedNodeCount(): number { return 0; }
    public addLeaf(): number { this.touched += 1; throw new Error("A rejected arena is never asked to publish."); }
    public depthOf(): number { return 0; }
    public parentOf(): number { throw new Error("A rejected arena is never navigated."); }
    public ancestorAtDepth(): number { throw new Error("A rejected arena is never queried."); }
    public valueAt(): number { throw new Error("A rejected arena holds no values."); }
}

describe("BilateralAncestralDeque", () => {
    test("an empty deque fails its reads and answers every no-op with shared storage", () => {
        const empty = BilateralAncestralDeque.emptyMyers<number>();
        expectDeque(empty, []);

        for (const produced of [empty.clear(), empty.take(0), empty.drop(0), empty.getRange(0, 0), empty.reverse()]) {
            expect(produced).toBe(empty);
        }
        const split = empty.splitAt(0);
        expect(split.left).toBe(empty);
        expect(split.right).toBe(empty);

        expect(() => empty.take(1)).toThrow(RangeError);
        expect(() => empty.drop(1)).toThrow(RangeError);
        expect(() => empty.splitAt(1)).toThrow(RangeError);
        expect(() => empty.getRange(0, 1)).toThrow(RangeError);
        expect(() => empty.getRange(1, 0)).toThrow(RangeError);
    });

    test("endpoint growth and removal retain every prior branch", () => {
        const root = BilateralAncestralDeque.emptyMyers<number>();
        const one = root.addLast(10);
        const two = one.addLast(20);
        const three = two.addFirst(5);
        const four = three.addFirst(1);
        const five = four.addLast(30);

        expectDeque(root, []);
        expectDeque(one, [10]);
        expectDeque(two, [10, 20]);
        expectDeque(three, [5, 10, 20]);
        expectDeque(four, [1, 5, 10, 20]);
        expectDeque(five, [1, 5, 10, 20, 30]);

        expectDeque(five.removeFirst(), [5, 10, 20, 30]);
        expectDeque(five.removeLast(), [1, 5, 10, 20]);
        expectDeque(five, [1, 5, 10, 20, 30]);

        const rightOnly = root.addLast(1).addLast(2).addLast(3);
        expectDeque(rightOnly.removeFirst(), [2, 3]);
        expectDeque(rightOnly.removeFirst().removeFirst(), [3]);
        expectDeque(rightOnly.removeFirst().removeFirst().removeFirst(), []);
        expectDeque(rightOnly, [1, 2, 3]);

        const leftOnly = root.addFirst(3).addFirst(2).addFirst(1);
        expectDeque(leftOnly.removeLast(), [1, 2]);
        expectDeque(leftOnly.removeLast().removeLast(), [1]);
        expectDeque(leftOnly.removeLast().removeLast().removeLast(), []);

        // Two independent branches grow from one retained version.
        expectDeque(two.addFirst(-1), [-1, 10, 20]);
        expectDeque(two.addLast(99), [10, 20, 99]);
        expectDeque(two, [10, 20]);

        const cleared = five.clear();
        expectDeque(cleared, []);
        expectDeque(cleared.addFirst(-7).addLast(8), [-7, 8]);
        expectDeque(five, [1, 5, 10, 20, 30]);
    });

    test("reverse exchanges the two intervals and continues from either orientation", () => {
        const deque = BilateralAncestralDeque.emptyMyers<number>()
            .addFirst(3).addFirst(2).addFirst(1)
            .addLast(4).addLast(5).addLast(6).addLast(7);
        const reversed = deque.reverse();
        const restored = reversed.reverse();

        expectDeque(deque, [1, 2, 3, 4, 5, 6, 7]);
        expectDeque(reversed, [7, 6, 5, 4, 3, 2, 1]);
        expectDeque(restored, [1, 2, 3, 4, 5, 6, 7]);
        expectDeque(reversed.addFirst(0).addLast(8), [0, 7, 6, 5, 4, 3, 2, 1, 8]);
        expectDeque(reversed.removeFirst().removeLast(), [6, 5, 4, 3, 2]);
        expectDeque(deque, [1, 2, 3, 4, 5, 6, 7]);

        // Reversal is the exchange itself, not a rebuild: the intervals are swapped by identity.
        expect(reversed.leftInterval).toBe(deque.rightInterval);
        expect(reversed.rightInterval).toBe(deque.leftInterval);
        expect(restored.sharesStorageWith(deque)).toBe(true);
        expect(reversed.sharesStorageWith(deque)).toBe(false);

        // At most one value, reversal returns the receiver; two values, it does not.
        const singleton = BilateralAncestralDeque.from([42]);
        expect(singleton.reverse()).toBe(singleton);
        const pair = singleton.addLast(43);
        expect(pair.reverse()).not.toBe(pair);
        expectDeque(pair.reverse(), [43, 42]);
    });

    test("every slice and split boundary crossing both arms remains growable", () => {
        const original = BilateralAncestralDeque.emptyMyers<number>()
            .addFirst(3).addFirst(2).addFirst(1)
            .addLast(4).addLast(5).addLast(6).addLast(7);
        const expected = [1, 2, 3, 4, 5, 6, 7];

        for (let start = 0; start <= expected.length; start += 1) {
            for (let count = 0; count <= expected.length - start; count += 1) {
                const window = original.getRange(start, count);
                const model = expected.slice(start, start + count);
                expectDeque(window, model);
                expectDeque(window.addFirst(-100 - start).addLast(100 + count), [-100 - start, ...model, 100 + count]);
                expectDeque(window, model);
                expectDeque(window.reverse().addFirst(-1).addLast(-2), [-1, ...[...model].reverse(), -2]);
            }
        }

        for (let boundary = 0; boundary <= expected.length; boundary += 1) {
            const split = original.splitAt(boundary);
            expectDeque(split.left, expected.slice(0, boundary));
            expectDeque(split.right, expected.slice(boundary));
            expectDeque(split.left.addLast(88), [...expected.slice(0, boundary), 88]);
            expectDeque(split.right.addFirst(77), [77, ...expected.slice(boundary)]);
        }

        expectDeque(original, expected);
    });

    test("every endpoint-construction word through length eight matches the sequence oracle", () => {
        for (let length = 0; length <= 8; length += 1) {
            for (let word = 0; word < 1 << length; word += 1) {
                let deque = BilateralAncestralDeque.emptyMyers<number>();
                const model: number[] = [];
                for (let value = 0; value < length; value += 1) {
                    if ((word & (1 << value)) === 0) { deque = deque.addFirst(value); model.unshift(value); }
                    else { deque = deque.addLast(value); model.push(value); }
                }

                expectDeque(deque, model);
                for (let start = 0; start <= length; start += 1) {
                    for (let count = 0; count <= length - start; count += 1) {
                        const expected = model.slice(start, start + count);
                        const window = deque.getRange(start, count);
                        expectValues(window, expected);
                        expectValues(window.addFirst(-1).addLast(-2), [-1, ...expected, -2]);
                        expectValues(window.reverse(), [...expected].reverse());
                    }
                }

                for (let boundary = 0; boundary <= length; boundary += 1) {
                    const split = deque.splitAt(boundary);
                    expectValues(split.left, model.slice(0, boundary));
                    expectValues(split.right, model.slice(boundary));
                    expectValues(deque.take(boundary), model.slice(0, boundary));
                    expectValues(deque.drop(boundary), model.slice(boundary));
                }
            }
        }
    });

    test("the query profile pins exact ancestor query counts rather than ceilings", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const deque = build(arena, [4, 3, 2, 1], [5, 6, 7, 8]);
        expect(deque.toArray()).toEqual(PROFILE_VALUES);
        expect(deque.leftInterval.count).toBe(4);
        expect(deque.rightInterval.count).toBe(4);

        // Growth, both endpoint reads, reversal, clearing, and whole-sequence enumeration are free.
        expectArenaDelta(arena, 0, 0, () => deque.count);
        expectArenaDelta(arena, 0, 0, () => deque.isEmpty);
        expectArenaDelta(arena, 0, 0, () => deque.first);
        expectArenaDelta(arena, 0, 0, () => deque.last);
        expectArenaDelta(arena, 0, 0, () => deque.reverse());
        expectArenaDelta(arena, 0, 0, () => deque.clear());
        expectArenaDelta(arena, 0, 0, () => deque.toArray());
        expectArenaDelta(arena, 0, 0, () => [...deque]);
        expectArenaDelta(arena, 1, 0, () => deque.addFirst(0));
        expectArenaDelta(arena, 1, 0, () => deque.addLast(9));

        // Indexing: the exact profile, not merely the ceiling of one.
        const getAtProfile: number[] = [];
        for (let index = 0; index < PROFILE_VALUES.length; index += 1) {
            const measured = measure(arena, () => deque.getAt(index));
            expect(measured.result).toBe(PROFILE_VALUES[index]);
            getAtProfile.push(measured.queries);
        }
        expect(getAtProfile).toEqual(GET_AT_QUERIES);
        expect(Math.max(...GET_AT_QUERIES)).toBe(1);

        // The four zeros of that profile are exactly the four cached endpoints, and each names the
        // retained handle the representation caches for it.
        expect(getAtProfile.flatMap((queries, index) => (queries === 0 ? [index] : []))).toEqual(CACHED_ENDPOINT_INDICES);
        expect(deque.getAt(0)).toBe(arena.valueAt(deque.leftInterval.tail));
        expect(deque.getAt(deque.leftInterval.count - 1)).toBe(arena.valueAt(deque.leftInterval.base));
        expect(deque.getAt(deque.leftInterval.count)).toBe(arena.valueAt(deque.rightInterval.base));
        expect(deque.getAt(deque.count - 1)).toBe(arena.valueAt(deque.rightInterval.tail));

        const takeProfile: number[] = [];
        const dropProfile: number[] = [];
        const splitProfile: number[] = [];
        for (let boundary = 0; boundary <= PROFILE_VALUES.length; boundary += 1) {
            const prefix = measure(arena, () => deque.take(boundary));
            expect(prefix.result.toArray()).toEqual(PROFILE_VALUES.slice(0, boundary));
            takeProfile.push(prefix.queries);

            const suffix = measure(arena, () => deque.drop(boundary));
            expect(suffix.result.toArray()).toEqual(PROFILE_VALUES.slice(boundary));
            dropProfile.push(suffix.queries);

            const split = measure(arena, () => deque.splitAt(boundary));
            expect(split.result.left.toArray()).toEqual(PROFILE_VALUES.slice(0, boundary));
            expect(split.result.right.toArray()).toEqual(PROFILE_VALUES.slice(boundary));
            splitProfile.push(split.queries);
        }
        expect(takeProfile).toEqual(TAKE_QUERIES);
        expect(dropProfile).toEqual(DROP_QUERIES);
        expect(splitProfile).toEqual(SPLIT_QUERIES);
        expect(Math.max(...SPLIT_QUERIES)).toBe(2);

        const rangeProfile: number[][] = [];
        for (let start = 0; start <= PROFILE_VALUES.length; start += 1) {
            const row: number[] = [];
            for (let count = 0; count <= PROFILE_VALUES.length - start; count += 1) {
                const measured = measure(arena, () => deque.getRange(start, count));
                expect(measured.result.toArray()).toEqual(PROFILE_VALUES.slice(start, start + count));
                row.push(measured.queries);
            }
            rangeProfile.push(row);
        }
        expect(rangeProfile).toEqual(RANGE_QUERIES);
        expect(Math.max(...RANGE_QUERIES.map((row) => Math.max(...row)))).toBe(2);

        // The cross-arm rows really are cross-arm, and they still respect the ceiling of two: a
        // five-value window from index one, and a three-value window from index three, both span the
        // centre of a four-plus-four deque.
        expect(RANGE_QUERIES[1]![5]).toBe(2);
        expect(RANGE_QUERIES[3]![3]).toBe(2);

        // Removal on its own nonempty arm makes no query at all.
        expectArenaDelta(arena, 0, 0, () => deque.removeFirst());
        expectArenaDelta(arena, 0, 0, () => deque.removeLast());
    });

    test("the removal profile pins the two-value shortcut and the anchor selection", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const rightOnly = build(arena, [], [10, 11, 12, 13]);
        const leftOnly = build(arena, [13, 12, 11, 10], []);

        // A drain that always crosses the centre spends one query per removal until the owning
        // interval is down to two values: at two the surviving value is already the retained tail,
        // and at one the result is an empty handle. Both endpoints of the profile are exactly zero.
        const frontProfile: number[] = [];
        let current = rightOnly;
        while (current.count !== 0) {
            const source = current;
            const measured = measure(arena, () => source.removeFirst());
            frontProfile.push(measured.queries);
            current = measured.result;
        }
        expect(frontProfile).toEqual([1, 1, 0, 0]);

        const backProfile: number[] = [];
        current = leftOnly;
        while (current.count !== 0) {
            const source = current;
            const measured = measure(arena, () => source.removeLast());
            backProfile.push(measured.queries);
            current = measured.result;
        }
        expect(backProfile).toEqual([1, 1, 0, 0]);

        // The single query selects the ancestor at depth(tail) - count + 2, which is the *next*
        // cached base. An off-by-one there would either resurrect the removed value or skip one, and
        // both the endpoint read and the structural audit would see it.
        const shortened = rightOnly.removeFirst();
        expect(shortened.toArray()).toEqual([11, 12, 13]);
        expect(shortened.first).toBe(11);
        expect(shortened.rightInterval.base).toBe(rightOnly.rightInterval.base + 1);
        expect(shortened.rightInterval.anchor).toBe(rightOnly.rightInterval.base);
        expect(shortened.validateStructure().rightCount).toBe(3);
        expect(shortened.validateStructure().rightAnchorDepth).toBe(0);

        const twice = shortened.removeFirst();
        expect(twice.toArray()).toEqual([12, 13]);
        expect(twice.first).toBe(12);
        expect(twice.rightInterval.anchor).toBe(shortened.rightInterval.base);
        expect(twice.validateStructure().rightAnchorDepth).toBe(1);

        // The third removal is the one that sees a two-value interval, and its shortcut names the
        // retained tail directly instead of querying for it.
        const thrice = twice.removeFirst();
        expect(thrice.toArray()).toEqual([13]);
        expect(thrice.first).toBe(13);
        expect(thrice.rightInterval.base).toBe(twice.rightInterval.tail);
        expect(thrice.rightInterval.tail).toBe(twice.rightInterval.tail);
        expect(thrice.rightInterval.anchor).toBe(twice.rightInterval.base);
        expect(thrice.validateStructure().rightAnchorDepth).toBe(2);

        expectDeque(rightOnly, [10, 11, 12, 13]);
        expectDeque(leftOnly, [10, 11, 12, 13]);
    });

    test("no-op operations return a value sharing the source storage", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const deque = build(arena, [3, 2, 1], [4, 5, 6]);
        const canonicalEmpty = deque.clear();

        for (const whole of [deque.getRange(0, deque.count), deque.take(deque.count), deque.drop(0)]) {
            expect(whole).toBe(deque);
        }

        // An endpoint split keeps one side as the receiver itself and anchors the other at the arena
        // bottom, so neither side is rebuilt.
        const frontSplit = deque.splitAt(0);
        const backSplit = deque.splitAt(deque.count);
        expect(frontSplit.right).toBe(deque);
        expect(backSplit.left).toBe(deque);

        // These are cross-object assertions, not self-comparisons: each empty result is a distinct
        // handle that nonetheless denotes the very same anchored storage.
        for (const producedEmpty of [frontSplit.left, backSplit.right, deque.getRange(3, 0), deque.take(0), deque.drop(deque.count)]) {
            expect(producedEmpty).not.toBe(canonicalEmpty);
            expect(producedEmpty.sharesStorageWith(canonicalEmpty)).toBe(true);
            expect(producedEmpty.sharesArenaWith(deque)).toBe(true);
        }
        expect(canonicalEmpty.clear()).toBe(canonicalEmpty);

        // Negative controls: a slice that stops inside an arm genuinely retains different endpoints,
        // and a deque built by the Myers convenience factory owns a different arena entirely.
        expect(deque.take(2)).not.toBe(deque);
        expect(deque.take(2).sharesStorageWith(deque)).toBe(false);
        expect(deque.take(2).sharesArenaWith(deque)).toBe(true);
        expect(canonicalEmpty.sharesStorageWith(deque)).toBe(false);
        expect(BilateralAncestralDeque.from([1, 2, 3, 4, 5, 6]).sharesArenaWith(deque)).toBe(false);

        // Storage sharing is a representation test, so two handles enumerating equal values across
        // different arenas must not be mistaken for it.
        const twin = build(new MyersIncrementalAncestorArena<number>(), [3, 2, 1], [4, 5, 6]);
        expect(twin.toArray()).toEqual(deque.toArray());
        expect(twin.sharesStorageWith(deque)).toBe(false);
    });

    test("values are retained by identity rather than copied", () => {
        // Small integers and interned strings compare equal under ===, so the payload here is an
        // object: only then is "the same value came back" an identity observation.
        const first = { label: "first" };
        const middle = { label: "middle" };
        const last = { label: "last" };
        const deque = BilateralAncestralDeque.from([first, middle, last]);

        expect(deque.first).toBe(first);
        expect(deque.last).toBe(last);
        expect(deque.getAt(1)).toBe(middle);
        expect(deque.toArray()[1]).toBe(middle);
        expect([...deque][2]).toBe(last);
        expect(deque.getRange(1, 2).first).toBe(middle);
        expect(deque.reverse().first).toBe(last);
        expect(deque.tryRemoveFirst()!.value).toBe(first);
        // A real negative control: a structurally equal but distinct object, held in a variable so
        // the assertion can actually fail. Allocating it inside `expect(...)` instead would make the
        // line unfalsifiable, since no value the deque could return is `Object.is`-equal to a fresh
        // literal.
        const impostor = { label: "first" };
        expect(deque.getAt(0)).toBe(first);
        expect(deque.getAt(0)).not.toBe(impostor);
    });

    test("the structural audit reports counts and rejects a corrupted interval", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const deque = build(arena, [2, 1], [3]);
        const statistics = deque.validateStructure();
        expect(statistics.count).toBe(3);
        expect(statistics.leftCount).toBe(2);
        expect(statistics.rightCount).toBe(1);
        expect(statistics.leftAnchorDepth).toBe(-1);
        expect(statistics.rightAnchorDepth).toBe(-1);
        expect(statistics.arenaPublishedNodeCount).toBe(3);

        const empty = deque.clear();
        const corrupt = (left: BilateralAncestralDequeInterval, right: BilateralAncestralDequeInterval, count: number): BilateralAncestralDeque<number> =>
            BilateralAncestralDeque.fromRepresentation(arena, left, right, count);

        expect(() => corrupt(deque.leftInterval, deque.rightInterval, deque.count + 1).validateStructure())
            .toThrow(/total count disagrees/);
        expect(() => corrupt({ ...empty.leftInterval, tail: empty.leftInterval.tail + 1 }, empty.rightInterval, 0).validateStructure())
            .toThrow(/one shared endpoint/);
        expect(() => corrupt({ ...deque.leftInterval, count: 1 }, deque.rightInterval, 2).validateStructure())
            .toThrow(/endpoint depths/);
        expect(() => corrupt({ ...deque.leftInterval, base: deque.leftInterval.tail }, deque.rightInterval, 3).validateStructure())
            .toThrow(/first node after the anchor/);
        expect(() => corrupt({ ...deque.leftInterval, count: -1 }, deque.rightInterval, 0).validateStructure())
            .toThrow(/count is negative/);

        // Two sibling branches of one arena let the ancestry-path checks be provoked without also
        // breaking the cheaper checks that run before them.
        const branchArena = new MyersIncrementalAncestorArena<number>();
        const firstChild = branchArena.addLeaf(branchArena.bottom, 1);
        const grandchild = branchArena.addLeaf(firstChild, 2);
        const sibling = branchArena.addLeaf(branchArena.bottom, 10);
        const siblingChild = branchArena.addLeaf(sibling, 20);
        const siblingTail = branchArena.addLeaf(siblingChild, 30);
        const branchEmpty = BilateralAncestralDeque.empty<number>(branchArena);

        expect(() => BilateralAncestralDeque.fromRepresentation(
            branchArena,
            { anchor: firstChild, base: grandchild, tail: siblingTail, count: 2 },
            branchEmpty.rightInterval,
            2,
        ).validateStructure()).toThrow(/anchor is not an ancestor/);
        expect(() => BilateralAncestralDeque.fromRepresentation(
            branchArena,
            { anchor: branchArena.bottom, base: firstChild, tail: siblingTail, count: 3 },
            branchEmpty.rightInterval,
            3,
        ).validateStructure()).toThrow(/not on the tail's ancestry path/);

        expectDeque(deque, [1, 2, 3]);
    });

    test("the structural audit spends exactly two queries per nonempty interval", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const empty = BilateralAncestralDeque.empty<number>(arena);
        const rightOnly = build(arena, [], [1, 2, 3]);
        const leftOnly = build(arena, [3, 2, 1], []);
        const bilateral = build(arena, [2, 1], [3, 4]);

        expectArenaDelta(arena, 0, 0, () => empty.validateStructure());
        expectArenaDelta(arena, 0, 2, () => rightOnly.validateStructure());
        expectArenaDelta(arena, 0, 2, () => leftOnly.validateStructure());
        // Both intervals nonempty is the documented ceiling of four, not O(1) work.
        expectArenaDelta(arena, 0, 4, () => bilateral.validateStructure());
    });

    test("out-of-range arguments are rejected without publishing or querying", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const deque = build(arena, [2, 1], [3]);
        const before = arena.statistics();

        for (const rejected of [
            (): unknown => deque.getAt(3),
            (): unknown => deque.getAt(-1),
            (): unknown => deque.getAt(0.5),
            (): unknown => deque.getAt(Number.NaN),
            (): unknown => deque.take(-1),
            (): unknown => deque.take(4),
            (): unknown => deque.take(1.5),
            (): unknown => deque.drop(-1),
            (): unknown => deque.drop(4),
            (): unknown => deque.splitAt(-1),
            (): unknown => deque.splitAt(4),
            (): unknown => deque.getRange(-1, 0),
            (): unknown => deque.getRange(0, -1),
            (): unknown => deque.getRange(0, 0.5),
            (): unknown => deque.getRange(4, 0),
            (): unknown => deque.getRange(2, 2),
            (): unknown => deque.getRange(Number.MAX_SAFE_INTEGER, 1),
            (): unknown => deque.getRange(1, Number.MAX_SAFE_INTEGER),
        ]) {
            expect(rejected).toThrow(RangeError);
        }

        const after = arena.statistics();
        expect(after.publishedNodeCount).toBe(before.publishedNodeCount);
        expect(after.addLeafCount).toBe(before.addLeafCount);
        expect(after.ancestorQueryCount).toBe(before.ancestorQueryCount);
        // Pin the counter to a value, not merely to being unchanged: comparing two reads of the same
        // counter also holds for a counter that never advances at all.
        expect(before.publishedNodeCount).toBe(3);
        expectDeque(deque, [1, 2, 3]);
    });

    test("an arena whose bottom is not at depth -1 is rejected before publication", () => {
        const arena = new MisplacedBottomArena();
        expect(() => BilateralAncestralDeque.empty<number>(arena)).toThrow(RangeError);
        expect(() => BilateralAncestralDeque.empty<number>(arena)).toThrow(/depth -1 but reports depth 0/);
        expect(arena.touched).toBe(0);
    });

    test("a stored undefined is a present value and absence uses the result shape", () => {
        const deque = BilateralAncestralDeque.emptyMyers<string | undefined>().addFirst(undefined).addLast("tail");

        expect(deque.count).toBe(2);
        expect(deque.first).toBeUndefined();
        expect(deque.last).toBe("tail");
        expect(deque.getAt(0)).toBeUndefined();
        expect(deque.toArray()).toEqual([undefined, "tail"]);

        const popped = deque.tryRemoveFirst();
        expect(popped).not.toBeUndefined();
        expect(popped!.value).toBeUndefined();
        expect(popped!.rest.toArray()).toEqual(["tail"]);
        expect(BilateralAncestralDeque.emptyMyers<string | undefined>().tryRemoveFirst()).toBeUndefined();
        deque.validateStructure();
    });

    test("one shared arena serves independent deque families and the factories stay separate", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const root = BilateralAncestralDeque.empty<number>(arena);
        const firstFamily = root.addLast(1).addLast(2);
        const secondFamily = root.addFirst(9).addFirst(8);

        expectDeque(firstFamily, [1, 2]);
        expectDeque(secondFamily, [8, 9]);
        expect(arena.statistics().publishedNodeCount).toBe(4);
        expect(root.arena).toBe(arena);
        expect(firstFamily.sharesArenaWith(secondFamily)).toBe(true);

        // A deque rebuilt through `from` owns a private arena rather than reusing its source's.
        const copied = BilateralAncestralDeque.from(firstFamily);
        expectDeque(copied, [1, 2]);
        expect(copied.sharesArenaWith(firstFamily)).toBe(false);
        expect(arena.statistics().publishedNodeCount).toBe(4);
        expect(BilateralAncestralDeque.emptyMyers<number>().sharesArenaWith(BilateralAncestralDeque.emptyMyers<number>())).toBe(false);
    });

    test("iteration is repeatable and never observes a later branch", () => {
        const deque = BilateralAncestralDeque.emptyMyers<number>().addFirst(2).addFirst(1).addLast(3);
        const iterator = deque[Symbol.iterator]();
        expect(iterator.next().value).toBe(1);

        // Grow both arms after the iterator was created; the captured paths must not see the leaves.
        const grown = deque.addFirst(0).addLast(4);
        expect([...iterator]).toEqual([2, 3]);
        expect([...deque]).toEqual([1, 2, 3]);
        expect([...deque]).toEqual([1, 2, 3]);
        expect([...grown]).toEqual([0, 1, 2, 3, 4]);
    });

    test("randomized branching histories agree with array models", () => {
        const command = fc.record({
            op: fc.integer({ min: 0, max: 9 }),
            source: fc.nat({ max: 4096 }),
            amount: fc.nat({ max: 4096 }),
            value: fc.integer({ min: -1000, max: 1000 }),
        });

        fc.assert(fc.property(fc.array(command, { minLength: 1, maxLength: 60 }), (commands) => {
            const root = BilateralAncestralDeque.emptyMyers<number>();
            const versions: Array<{ deque: BilateralAncestralDeque<number>; model: number[] }> = [{ deque: root, model: [] }];

            for (const step of commands) {
                const source = versions[step.source % versions.length]!;
                const { deque, model } = source;
                const boundary = model.length === 0 ? 0 : step.amount % (model.length + 1);
                let produced: BilateralAncestralDeque<number>;
                let expected: number[];
                switch (step.op) {
                    case 0: produced = deque.addFirst(step.value); expected = [step.value, ...model]; break;
                    case 1: produced = deque.addLast(step.value); expected = [...model, step.value]; break;
                    case 2: if (model.length === 0) continue; produced = deque.removeFirst(); expected = model.slice(1); break;
                    case 3: if (model.length === 0) continue; produced = deque.removeLast(); expected = model.slice(0, -1); break;
                    case 4: produced = deque.reverse(); expected = [...model].reverse(); break;
                    case 5: {
                        const count = model.length === boundary ? 0 : step.value % (model.length - boundary + 1);
                        const width = Math.abs(count);
                        produced = deque.getRange(boundary, width);
                        expected = model.slice(boundary, boundary + width);
                        break;
                    }
                    case 6: {
                        const split = deque.splitAt(boundary);
                        const takeLeft = step.value % 2 === 0;
                        produced = takeLeft ? split.left : split.right;
                        expected = takeLeft ? model.slice(0, boundary) : model.slice(boundary);
                        break;
                    }
                    case 7: produced = deque.take(boundary); expected = model.slice(0, boundary); break;
                    case 8: produced = deque.drop(boundary); expected = model.slice(boundary); break;
                    default: produced = deque.clear(); expected = []; break;
                }

                expect(produced.toArray()).toEqual(expected);
                expect(produced.count).toBe(expected.length);
                expect(produced.validateStructure().count).toBe(expected.length);
                expect(deque.toArray()).toEqual(model);
                versions.push({ deque: produced, model: expected });
            }

            // Every retained version still denotes exactly what it did when it was produced.
            for (const retained of versions) {
                expect(retained.deque.toArray()).toEqual(retained.model);
                expect([...retained.deque]).toEqual(retained.model);
                retained.deque.validateStructure();
                for (let index = 0; index < retained.model.length; index += 1) {
                    expect(retained.deque.getAt(index)).toBe(retained.model[index]);
                }
            }
            expect(root.toArray()).toEqual([]);
        }), { numRuns: 100 });
    });
});
