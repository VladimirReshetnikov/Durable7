/**
 * Tests for the persistent monotone-action heap: the clamp algebra's collapse cases, the direction
 * of tag composition at both pushdown sites, the temporal meaning of a whole-heap transform, the
 * three tie-breaking rules, self-meld duplication, structural sharing with negative controls, and
 * agreement with a direct model over randomized histories.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import type { Comparator } from "../../src/finger-tree/ordering.js";
import {
    OrderClamp,
    OrderClampPolicy,
    PersistentMonotoneActionHeap,
    type MonotoneHeapAction,
    type MonotoneHeapEntry,
} from "../../src/finger-tree/persistent-monotone-action-heap.js";

type ClampAction = OrderClamp<number>;
type ClampHeap = PersistentMonotoneActionHeap<string, number, ClampAction>;

function clampHeap(policy: OrderClampPolicy<number>, entries: ReadonlyArray<readonly [string, number]>): ClampHeap {
    return PersistentMonotoneActionHeap.from<string, number, ClampAction>(entries, policy);
}

function emptyClampHeap(policy: OrderClampPolicy<number>): ClampHeap {
    return PersistentMonotoneActionHeap.empty<string, number, ClampAction>(policy);
}

function drain<E, P, A>(heap: PersistentMonotoneActionHeap<E, P, A>): Array<MonotoneHeapEntry<E, P>> {
    const result: Array<MonotoneHeapEntry<E, P>> = [];
    let current = heap;
    for (;;) {
        const view = current.minimumView();
        if (view === undefined) break;
        result.push(view.minimum);
        current = view.remainder;
    }
    return result;
}

function drainPriorities<E, A>(heap: PersistentMonotoneActionHeap<E, number, A>): number[] {
    return drain(heap).map((entry) => entry.priority);
}

function priorityMap<A>(heap: PersistentMonotoneActionHeap<string, number, A>): Record<string, number> {
    const map: Record<string, number> = {};
    for (const entry of heap) map[entry.element] = entry.priority;
    return map;
}

/** A priority whose comparator ignores `tag`, so equal-comparing distinct representatives exist. */
interface Ranked { readonly key: number; readonly tag: string }
const byKey: Comparator<Ranked> = (left, right) => left.key - right.key;

/** A second, deliberately non-commutative action family, proving the policy seam is not clamp-only. */
interface Affine { readonly scale: number; readonly offset: number }
class AffinePolicy implements MonotoneHeapAction<number, Affine> {
    public readonly comparator: Comparator<number> = (left, right) => left - right;
    public readonly identity: Affine = { scale: 1, offset: 0 };
    public isIdentity(action: Affine): boolean { return action.scale === 1 && action.offset === 0; }
    public compose(outer: Affine, inner: Affine): Affine { return { scale: outer.scale * inner.scale, offset: outer.scale * inner.offset + outer.offset }; }
    public apply(action: Affine, priority: number): number { return action.scale * priority + action.offset; }
}

describe("OrderClampPolicy", () => {
    const policy = new OrderClampPolicy<number>();

    test("recognizes its identity and hands the other operand back untouched", () => {
        const floor = policy.atLeast(3);
        expect(policy.isIdentity(policy.identity)).toBe(true);
        expect(policy.isIdentity(floor)).toBe(false);
        expect(policy.isIdentity(policy.constant(0))).toBe(false);
        expect(policy.compose(policy.identity, floor)).toBe(floor);
        expect(policy.compose(floor, policy.identity)).toBe(floor);
        expect(policy.apply(policy.identity, 42)).toBe(42);
    });

    test("an outer constant wins outright and an inner constant receives the outer clamp", () => {
        const outerConstant = policy.constant(7);
        expect(policy.compose(outerConstant, policy.atLeast(3))).toBe(outerConstant);
        expect(policy.compose(outerConstant, policy.between(-5, 5))).toBe(outerConstant);
        const innerConstant = policy.compose(policy.atMost(4), policy.constant(9));
        expect(innerConstant.isConstant).toBe(true);
        expect(innerConstant.constantValue).toBe(4);
        const untouched = policy.compose(policy.atMost(40), policy.constant(9));
        expect(untouched.constantValue).toBe(9);
    });

    test("disjoint bounds collapse to a constant carrying the outer boundary", () => {
        // The canonical non-commutative pair: a cap composed after a floor collapses to the cap's
        // boundary, and the opposite order collapses to the floor's.
        const capAfterFloor = policy.compose(policy.atMost(10), policy.atLeast(50));
        expect(capAfterFloor.isConstant).toBe(true);
        expect(capAfterFloor.constantValue).toBe(10);
        const floorAfterCap = policy.compose(policy.atLeast(50), policy.atMost(10));
        expect(floorAfterCap.isConstant).toBe(true);
        expect(floorAfterCap.constantValue).toBe(50);
        for (const sample of [-100, 0, 10, 30, 50, 100]) {
            expect(policy.apply(capAfterFloor, sample)).toBe(10);
            expect(policy.apply(floorAfterCap, sample)).toBe(50);
        }
    });

    test("overlapping bounds intersect and keep the inner representative on an equal boundary", () => {
        const ranked = new OrderClampPolicy<Ranked>(byKey);
        const innerLower: Ranked = { key: 5, tag: "inner" };
        const outerLower: Ranked = { key: 5, tag: "outer" };
        // Distinct objects that compare equal, so the retained representative is observable by ===.
        const lower = ranked.compose(ranked.atLeast(outerLower), ranked.atLeast(innerLower));
        expect(lower.lowerBound).toBe(innerLower);
        expect(lower.hasUpperBound).toBe(false);
        const innerUpper: Ranked = { key: 5, tag: "inner" };
        const outerUpper: Ranked = { key: 5, tag: "outer" };
        const upper = ranked.compose(ranked.atMost(outerUpper), ranked.atMost(innerUpper));
        expect(upper.upperBound).toBe(innerUpper);
        // A strictly tighter outer bound still wins; the tie rule only settles equal boundaries.
        expect(ranked.compose(ranked.atLeast({ key: 9, tag: "outer" }), ranked.atLeast(innerLower)).lowerBound.tag).toBe("outer");
        // Disjoint keeps the outer (newer) representative instead.
        const outerFloor: Ranked = { key: 9, tag: "outer" };
        const collapsed = ranked.compose(ranked.atLeast(outerFloor), ranked.atMost({ key: 1, tag: "inner" }));
        expect(collapsed.isConstant).toBe(true);
        expect(collapsed.constantValue).toBe(outerFloor);
        // The intersection of two overlapping two-sided clamps keeps both tightest bounds.
        const intersected = policy.compose(policy.between(0, 8), policy.between(4, 12));
        expect(intersected.lowerBound).toBe(4);
        expect(intersected.upperBound).toBe(8);
    });

    test("application tests the constant, then the lower bound, then the upper bound", () => {
        const crossed = new OrderClamp<number>({ lowerBound: 5, upperBound: 1 });
        // 3 is below the lower bound and above the upper bound; lower is consulted first.
        expect(policy.apply(crossed, 3)).toBe(5);
        const constantAndBounded = new OrderClamp<number>({ constantValue: 7, lowerBound: 5, upperBound: 1 });
        expect(policy.apply(constantAndBounded, 3)).toBe(7);
        expect(policy.apply(policy.between(2, 6), 0)).toBe(2);
        expect(policy.apply(policy.between(2, 6), 9)).toBe(6);
        expect(policy.apply(policy.between(2, 6), 4)).toBe(4);
    });

    test("clamp construction validates bounds and absent components are unreadable", () => {
        expect(() => policy.between(5, 1)).toThrow(RangeError);
        expect(policy.between(4, 4).lowerBound).toBe(4);
        const floor = policy.atLeast(3);
        expect(() => floor.upperBound).toThrow(RangeError);
        expect(() => floor.constantValue).toThrow(RangeError);
        expect(() => policy.identity.lowerBound).toThrow(RangeError);
        expect(policy.atLeast(3).equals(policy.atLeast(3))).toBe(true);
        expect(policy.atLeast(3).equals(policy.atMost(3))).toBe(false);
        expect(policy.constant(3).equals(policy.atLeast(3))).toBe(false);
        expect(OrderClamp.identity<number>().equals(policy.identity)).toBe(true);
    });
});

describe("PersistentMonotoneActionHeap composition direction", () => {
    test("the tree pushdown site composes the newer action outermost", () => {
        const policy = new OrderClampPolicy<number>();
        const base = clampHeap(policy, [["a", 1], ["b", 2], ["c", 3]]);
        const floored = base.transformAll(policy.atLeast(50));
        expect(floored.minimum?.priority).toBe(50);
        // The second transform stacks a cap over the retained floor on the root tree itself. Reading
        // only the minimum touches nothing but the root's own tag, so this isolates the tree site.
        const capped = floored.transformAll(policy.atMost(10));
        expect(capped.minimum?.priority).toBe(10);
        // compose(floor 50, cap 10) - the reversal - would be the constant 50 instead.
        expect(capped.minimum?.priority).not.toBe(50);
        expect(drainPriorities(capped)).toEqual([10, 10, 10]);
    });

    test("the forest-spine pushdown site composes the newer action outermost", () => {
        const policy = new OrderClampPolicy<number>();
        // Two entries only, so the later skew-insert takes the non-linking branch and keeps the
        // floor-tagged spine cell as the tail of a fresh identity-tagged cell. Exposing the capped
        // root then pushes the cap onto a spine cell that already carries the floor. Every tagTree
        // call in this history composes against the identity and is therefore blind to argument
        // order, so only the forest site can fail here.
        const base = clampHeap(policy, [["a", 1], ["b", 2]]);
        const floored = base.transformAll(policy.atLeast(50));
        const grown = floored.insert("c", 100);
        expect(priorityMap(grown)).toEqual({ a: 50, b: 50, c: 100 });
        const capped = grown.transformAll(policy.atMost(10));
        expect(priorityMap(capped)).toEqual({ a: 10, b: 10, c: 10 });
        // compose(cap 10, floor 50) is the constant 10. The reversal would leave b at 50, and losing
        // the stacked floor altogether would leave b at its raw 2.
        expect(priorityMap(capped)["b"]).not.toBe(50);
        expect(priorityMap(capped)["b"]).not.toBe(2);
        expect(capped.validateStructure().count).toBe(3);
    });

    test("a non-clamp action family sees the same composition direction", () => {
        const affine = new AffinePolicy();
        const heap = PersistentMonotoneActionHeap.from<string, number, Affine>([["x", 1], ["y", 4]], affine);
        const composed = heap.transformAll({ scale: 1, offset: 3 }).transformAll({ scale: 2, offset: 0 });
        // 2 * (p + 3), not 2 * p + 3.
        expect(priorityMap(composed)).toEqual({ x: 8, y: 14 });
        expect(composed.minimum?.priority).toBe(8);
        expect(composed.validateStructure().count).toBe(2);
        expect(drainPriorities(composed)).toEqual([8, 14]);
    });
});

describe("PersistentMonotoneActionHeap temporal semantics", () => {
    test("a later insertion is never retroactively transformed", () => {
        const policy = new OrderClampPolicy<number>();
        const base = clampHeap(policy, [["a", 1], ["b", 2], ["c", 3]]);
        const capped = base.transformAll(policy.atMost(0));
        expect(drainPriorities(capped)).toEqual([0, 0, 0]);
        const later = capped.insert("late", 100);
        expect(priorityMap(later)["late"]).toBe(100);
        expect(drainPriorities(later)).toEqual([0, 0, 0, 100]);
        // The source version is untouched, and its own priorities never saw the cap.
        expect(base.count).toBe(3);
        expect(drainPriorities(base)).toEqual([1, 2, 3]);
        expect(capped.count).toBe(3);
    });

    test("a meld leaves each operand's action history to its own entries", () => {
        const policy = new OrderClampPolicy<number>();
        const left = clampHeap(policy, [["l1", 5], ["l2", 7]]).transformAll(policy.constant(1));
        const right = clampHeap(policy, [["r1", 9], ["r2", 11]]);
        const merged = left.meld(right);
        expect(priorityMap(merged)).toEqual({ l1: 1, l2: 1, r1: 9, r2: 11 });
        expect(merged.count).toBe(4);
        expect(merged.validateStructure().count).toBe(4);
        // Transforming the merged version reaches everything present at that moment.
        const cappedMerged = merged.transformAll(policy.atMost(3));
        expect(priorityMap(cappedMerged)).toEqual({ l1: 1, l2: 1, r1: 3, r2: 3 });
        expect(priorityMap(merged)).toEqual({ l1: 1, l2: 1, r1: 9, r2: 11 });
    });
});

describe("PersistentMonotoneActionHeap tie-breaking", () => {
    test("an insert root tie favours the new entry", () => {
        const policy = new OrderClampPolicy<number>();
        const heap = clampHeap(policy, [["old", 5]]).insert("new", 5);
        expect(heap.minimum?.element).toBe("new");
        expect(heap.minimum?.priority).toBe(5);
        expect(heap.count).toBe(2);
    });

    test("a meld root tie favours the receiver, in both directions", () => {
        const policy = new OrderClampPolicy<number>();
        const left = clampHeap(policy, [["L", 5], ["L2", 8]]);
        const right = clampHeap(policy, [["R", 5], ["R2", 9]]);
        expect(left.meld(right).minimum?.element).toBe("L");
        expect(right.meld(left).minimum?.element).toBe("R");
        expect(left.meld(right).count).toBe(4);
        expect(right.meld(left).count).toBe(4);
    });

    test("a forest minimum tie favours the earliest spine occurrence", () => {
        const policy = new OrderClampPolicy<number>();
        // Skew insertion conses at the spine head, so "c" precedes "b" on the root's forest.
        const heap = clampHeap(policy, [["a", 0], ["b", 5], ["c", 5]]);
        const remainder = heap.deleteMinimum();
        expect(remainder?.minimum?.element).toBe("c");
        expect(remainder?.minimum?.element).not.toBe("b");
        expect(remainder?.count).toBe(2);
    });
});

describe("PersistentMonotoneActionHeap structure", () => {
    test("melding a heap with itself duplicates every logical occurrence", () => {
        const policy = new OrderClampPolicy<number>();
        const heap = clampHeap(policy, [["a", 3], ["b", 1], ["c", 2], ["d", 5]]);
        const doubled = heap.meld(heap);
        expect(doubled.count).toBe(8);
        expect(doubled.validateStructure().count).toBe(8);
        expect(drainPriorities(doubled)).toEqual([1, 1, 2, 2, 3, 3, 5, 5]);
        expect(heap.count).toBe(4);
        // A transform applied before the self-meld reaches both copies, since both are the same
        // retained root.
        const transformed = heap.transformAll(policy.atLeast(4));
        expect(drainPriorities(transformed.meld(transformed))).toEqual([4, 4, 4, 4, 4, 4, 5, 5]);
    });

    test("a whole-heap transform reuses the entire child forest", () => {
        const policy = new OrderClampPolicy<number>();
        const heap = clampHeap(policy, [["a", 1], ["b", 2], ["c", 3], ["d", 4]]);
        const transformed = heap.transformAll(policy.atMost(2));
        expect(transformed).not.toBe(heap);
        expect(transformed.sharesRootWith(heap)).toBe(false);
        expect(transformed.sharesRootChildrenWith(heap)).toBe(true);
        // Every node but the freshly tagged root is shared, which is the O(1)-allocation claim.
        expect(transformed.sharedTreeCount(heap)).toBe(heap.count - 1);
        // Negative controls: an insertion rebuilds the root's spine, and an unrelated heap shares
        // nothing at all.
        expect(heap.insert("e", 99).sharesRootChildrenWith(heap)).toBe(false);
        expect(clampHeap(policy, [["z", 1], ["y", 2]]).sharedTreeCount(heap)).toBe(0);
        // An identity action is defined to return the receiver, so a sharing assertion there would
        // be a self-comparison; assert the receiver identity itself instead.
        expect(heap.transformAll(policy.identity)).toBe(heap);
    });

    test("a meld shares structure with both operands", () => {
        const policy = new OrderClampPolicy<number>();
        const left = clampHeap(policy, [["a", 1], ["b", 2], ["c", 3]]);
        const right = clampHeap(policy, [["d", 4], ["e", 5], ["f", 6]]);
        const merged = left.meld(right);
        expect(merged).not.toBe(left);
        expect(merged).not.toBe(right);
        expect(merged.sharedTreeCount(left)).toBeGreaterThan(0);
        expect(merged.sharedTreeCount(right)).toBeGreaterThan(0);
        expect(left.sharedTreeCount(right)).toBe(0);
    });

    test("payload objects survive by identity through transforms and rebuilds", () => {
        const policy = new OrderClampPolicy<number>();
        const payload = { name: "shared" };
        const heap = PersistentMonotoneActionHeap.from<{ readonly name: string }, number, ClampAction>([[payload, 5]], policy);
        const later = heap.insert({ name: "other" }, 1).transformAll(policy.atMost(0));
        const found = Array.from(later).find((entry) => entry.element.name === "shared");
        expect(found?.element).toBe(payload);
        expect(found?.priority).toBe(0);
    });

    test("empty heaps stay usable and report the absent result", () => {
        const policy = new OrderClampPolicy<number>();
        const empty = emptyClampHeap(policy);
        expect(empty.isEmpty).toBe(true);
        expect(empty.count).toBe(0);
        expect(empty.minimum).toBeUndefined();
        expect(empty.deleteMinimum()).toBeUndefined();
        expect(empty.minimumView()).toBeUndefined();
        expect(empty.transformAll(policy.atMost(3))).toBe(empty);
        expect(Array.from(empty)).toEqual([]);
        expect(empty.validateStructure()).toEqual({ count: 0, rootForestLength: 0, maximumRank: 0, maximumDepth: 0, taggedComponentCount: 0 });
        expect(empty.comparator).toBe(policy.comparator);
        expect(empty.actionPolicy).toBe(policy);
        const single = clampHeap(policy, [["a", 1]]);
        expect(empty.meld(single)).toBe(single);
        expect(single.meld(empty)).toBe(single);
        const drained = single.deleteMinimum();
        expect(drained?.isEmpty).toBe(true);
        expect(drained?.insert("b", 2).minimum?.element).toBe("b");
        expect(drained?.actionPolicy).toBe(policy);
    });

    test("melding heaps that retain different policies is rejected", () => {
        const left = clampHeap(new OrderClampPolicy<number>(), [["a", 1]]);
        const right = clampHeap(new OrderClampPolicy<number>(), [["b", 2]]);
        expect(() => left.meld(right)).toThrow(TypeError);
        expect(() => right.meld(left)).toThrow(TypeError);
        // Sharing one policy object is what makes two heaps meldable.
        const shared = new OrderClampPolicy<number>();
        expect(clampHeap(shared, [["a", 1]]).meld(clampHeap(shared, [["b", 2]])).count).toBe(2);
    });

    test("validation counts tags only where a pending action really sits", () => {
        const policy = new OrderClampPolicy<number>();
        const heap = clampHeap(policy, [["a", 1], ["b", 2], ["c", 3], ["d", 4], ["e", 5]]);
        const clean = heap.validateStructure();
        expect(clean.count).toBe(5);
        expect(clean.taggedComponentCount).toBe(0);
        expect(clean.rootForestLength).toBeGreaterThan(0);
        expect(clean.maximumDepth).toBeGreaterThan(1);
        expect(clean.maximumRank).toBeGreaterThan(0);
        const transformed = heap.transformAll(policy.atMost(2));
        const tagged = transformed.validateStructure();
        expect(tagged.count).toBe(5);
        expect(tagged.taggedComponentCount).toBeGreaterThan(0);
        expect(tagged.rootForestLength).toBe(clean.rootForestLength);
        expect(tagged.maximumRank).toBe(clean.maximumRank);
        expect(tagged.maximumDepth).toBe(clean.maximumDepth);
    });
});

describe("PersistentMonotoneActionHeap bulk behaviour", () => {
    test("drains adversarial inputs in sorted order", () => {
        const policy = new OrderClampPolicy<number>();
        const size = 4096;
        let heap = PersistentMonotoneActionHeap.from<number, number, ClampAction>(
            Array.from({ length: size }, (_, index): readonly [number, number] => [size - 1 - index, size - 1 - index]),
            policy,
        );
        expect(heap.validateStructure().count).toBe(size);
        const output: number[] = [];
        for (;;) {
            const view = heap.minimumView();
            if (view === undefined) break;
            output.push(view.minimum.priority);
            heap = view.remainder;
        }
        expect(output).toEqual(Array.from({ length: size }, (_, index) => index));
    });

    test("the split-forest decomposition re-inserts zeros in first-encountered order", () => {
        const policy = new OrderClampPolicy<number>();
        // A fixed shape whose delete-minimum path splits off two rank-zero trees at once. The
        // decomposition prepends each zero as it is encountered, so the re-insertion walks the
        // collected list backwards; re-inserting them forwards instead swaps the order of the two
        // priority-2 entries "e7" and "e6" below while leaving every rank and depth unchanged, so
        // only the drained element order can see it.
        const priorities = [2, 1, 2, 2, 2, 2, 2, 2, 1, 0, 2, 0, 0, 1, 2, 0];
        const heap = clampHeap(policy, priorities.map((priority, index): readonly [string, number] => [`e${index}`, priority]));
        expect(heap.validateStructure().count).toBe(16);
        expect(drain(heap).map((entry) => entry.element)).toEqual([
            "e15", "e12", "e11", "e9", "e13", "e8", "e1", "e10",
            "e14", "e5", "e7", "e6", "e4", "e2", "e3", "e0",
        ]);
    });

    test("interleaved transforms and deletions keep the split-forest decomposition sound", () => {
        const policy = new OrderClampPolicy<number>();
        let heap = emptyClampHeap(policy);
        const model = new Map<string, number>();
        for (let step = 0; step < 400; step++) {
            const element = `e${step}`;
            const priority = (step * 37) % 101;
            heap = heap.insert(element, priority);
            model.set(element, priority);
            if (step % 17 === 16) {
                const bound = (step * 13) % 101;
                heap = heap.transformAll(policy.atLeast(bound));
                for (const [key, value] of model) model.set(key, Math.max(value, bound));
            }
            if (step % 23 === 22) {
                const view = heap.minimumView();
                expect(view).toBeDefined();
                expect(view!.minimum.priority).toBe(Math.min(...model.values()));
                model.delete(view!.minimum.element);
                heap = view!.remainder;
            }
            if (step % 41 === 40) expect(heap.validateStructure().count).toBe(model.size);
        }
        expect(heap.count).toBe(model.size);
        expect(drainPriorities(heap)).toEqual([...model.values()].sort((left, right) => left - right));
    });

    test("random operation histories agree with a direct model", () => {
        fc.assert(
            fc.property(
                fc.array(fc.tuple(fc.integer({ min: 0, max: 99 }), fc.integer({ min: -30, max: 30 })), { maxLength: 120 }),
                (operations) => {
                    const policy = new OrderClampPolicy<number>();
                    let heap = emptyClampHeap(policy);
                    let model: Array<{ readonly element: string; readonly priority: number }> = [];
                    let serial = 0;
                    for (const [code, argument] of operations) {
                        switch (code % 5) {
                            case 0: {
                                const element = `e${serial++}`;
                                heap = heap.insert(element, argument);
                                model = [...model, { element, priority: argument }];
                                break;
                            }
                            case 1: {
                                heap = heap.transformAll(policy.atLeast(argument));
                                model = model.map((entry) => ({ element: entry.element, priority: Math.max(entry.priority, argument) }));
                                break;
                            }
                            case 2: {
                                heap = heap.transformAll(policy.atMost(argument));
                                model = model.map((entry) => ({ element: entry.element, priority: Math.min(entry.priority, argument) }));
                                break;
                            }
                            case 3: {
                                heap = heap.transformAll(policy.constant(argument));
                                model = model.map((entry) => ({ element: entry.element, priority: argument }));
                                break;
                            }
                            default: {
                                const view = heap.minimumView();
                                if (view === undefined) {
                                    expect(model).toHaveLength(0);
                                    break;
                                }
                                expect(view.minimum.priority).toBe(Math.min(...model.map((entry) => entry.priority)));
                                const index = model.findIndex((entry) => entry.element === view.minimum.element);
                                expect(index).toBeGreaterThanOrEqual(0);
                                expect(model[index]!.priority).toBe(view.minimum.priority);
                                model = [...model.slice(0, index), ...model.slice(index + 1)];
                                heap = view.remainder;
                                break;
                            }
                        }
                        expect(heap.count).toBe(model.length);
                        expect(heap.validateStructure().count).toBe(model.length);
                        const observed = Array.from(heap).map((entry) => `${entry.element}=${entry.priority}`).sort();
                        expect(observed).toEqual(model.map((entry) => `${entry.element}=${entry.priority}`).sort());
                    }
                    expect(drainPriorities(heap)).toEqual(model.map((entry) => entry.priority).sort((left, right) => left - right));
                    return true;
                },
            ),
            { numRuns: 60 },
        );
    });

    test("random meld histories preserve every entry and stay meldable after transforms", () => {
        fc.assert(
            fc.property(
                fc.array(fc.array(fc.integer({ min: -50, max: 50 }), { maxLength: 30 }), { minLength: 2, maxLength: 8 }),
                fc.integer({ min: -50, max: 50 }),
                (groups, bound) => {
                    const policy = new OrderClampPolicy<number>();
                    let merged = emptyClampHeap(policy);
                    const expected: number[] = [];
                    groups.forEach((group, groupIndex) => {
                        let piece = clampHeap(policy, group.map((value, index): readonly [string, number] => [`g${groupIndex}i${index}`, value]));
                        if (groupIndex % 2 === 0) {
                            piece = piece.transformAll(policy.atLeast(bound));
                            expected.push(...group.map((value) => Math.max(value, bound)));
                        } else {
                            expected.push(...group);
                        }
                        merged = merged.meld(piece);
                    });
                    expect(merged.count).toBe(expected.length);
                    expect(merged.validateStructure().count).toBe(expected.length);
                    expect(drainPriorities(merged)).toEqual([...expected].sort((left, right) => left - right));
                    return true;
                },
            ),
            { numRuns: 60 },
        );
    });
});
