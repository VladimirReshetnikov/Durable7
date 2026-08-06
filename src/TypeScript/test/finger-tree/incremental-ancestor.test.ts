/**
 * Tests for the append-only incremental level-ancestor seam.
 *
 * The load-bearing case is the hop bound. Myers' coalesced jump links are the entire reason this
 * backend exists over a plain parent array, and removing them leaves every ancestor *answer*
 * correct while turning each query into an O(depth) walk. Only a hop-count assertion can catch
 * that, so the bound here is stated so a coalescing-free arena fails it.
 */
import { describe, expect, test } from "vitest";
import { MyersIncrementalAncestorArena } from "../../src/finger-tree/incremental-ancestor.js";

function ceilingLog2(value: number): number {
    let bits = 0;
    let remaining = value;
    while (remaining > 1) { remaining = Math.ceil(remaining / 2); bits += 1; }
    return bits;
}

/** A multiply-only oracle, deliberately not the arena's sqrt-seeded form, so the two disagree if
 *  the block arithmetic is off by one at a perfect square. */
function referenceIntegerSquareRoot(value: number): number {
    let root = 0;
    while ((root + 1) * (root + 1) <= value) root += 1;
    return root;
}

function buildChain(arena: MyersIncrementalAncestorArena<number>, length: number): number {
    let tip = arena.bottom;
    for (let index = 1; index <= length; index += 1) tip = arena.addLeaf(tip, index);
    return tip;
}

describe("MyersIncrementalAncestorArena", () => {
    test("deep chain queries stay logarithmic", () => {
        const nodeCount = 32_768;
        const arena = new MyersIncrementalAncestorArena<number>();
        const tip = buildChain(arena, nodeCount);

        const before = arena.statistics();
        expect(before.ancestorQueryCount).toBe(0);
        expect(before.maximumAncestorHopCount).toBe(0);

        for (let depth = -1; depth < nodeCount; depth += 1) arena.ancestorAtDepth(tip, depth);

        const after = arena.statistics();
        // The factor four is headroom for the constant in Myers' coalescing schedule, whose jumps
        // do not close a full remaining half per hop. It stays Theta(log M), so no linear-hop
        // implementation can satisfy it at this scale: a plain parent walk would need 32768 hops
        // against a bound of 64.
        const bound = 4 * ceilingLog2(nodeCount + 1);
        expect(after.maximumAncestorHopCount).toBeGreaterThanOrEqual(1);
        expect(after.maximumAncestorHopCount).toBeLessThanOrEqual(bound);
        expect(after.ancestorQueryCount).toBe(nodeCount + 1);
    });

    test("the last hop count reports one query rather than accumulating", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const tip = buildChain(arena, 512);
        const depth = arena.depthOf(tip);

        arena.ancestorAtDepth(tip, depth);
        expect(arena.statistics().lastAncestorHopCount).toBe(0);
        arena.ancestorAtDepth(tip, -1);
        expect(arena.statistics().lastAncestorHopCount).toBeGreaterThan(1);
        arena.ancestorAtDepth(tip, depth);
        expect(arena.statistics().lastAncestorHopCount).toBe(0);
    });

    test("the odd block layout tracks the square boundaries", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        let tip = arena.bottom;
        for (let published = 0; published <= 2_048; published += 1) {
            const current = arena.statistics();
            const expectedBlocks = referenceIntegerSquareRoot(published) + 1;
            expect(current.publishedNodeCount).toBe(published);
            expect(current.blockCount).toBe(expectedBlocks);
            expect(current.allocatedSlotCount).toBe(expectedBlocks * expectedBlocks);
            tip = arena.addLeaf(tip, published);
        }
    });

    test("error contracts", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const node = arena.addLeaf(arena.bottom, 1);

        expect(() => arena.depthOf(99)).toThrow(RangeError);
        expect(() => arena.depthOf(-1)).toThrow(RangeError);
        expect(() => arena.parentOf(arena.bottom)).toThrow(RangeError);
        expect(() => arena.valueAt(arena.bottom)).toThrow(RangeError);
        expect(() => arena.valueAt(99)).toThrow(RangeError);
        expect(() => arena.addLeaf(99, 2)).toThrow(RangeError);
        expect(() => arena.ancestorAtDepth(node, 5)).toThrow(RangeError);
        expect(() => arena.ancestorAtDepth(node, -2)).toThrow(RangeError);

        expect(arena.valueAt(node)).toBe(1);
        expect(arena.depthOf(arena.bottom)).toBe(-1);
        arena.validateAncestry(node);
    });

    test("a rejected addition publishes nothing", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const tip = buildChain(arena, 64);
        const before = arena.statistics();

        expect(() => arena.addLeaf(4_096, 7)).toThrow(RangeError);

        const after = arena.statistics();
        expect(after.publishedNodeCount).toBe(before.publishedNodeCount);
        expect(after.addLeafCount).toBe(before.addLeafCount);
        expect(after.blockCount).toBe(before.blockCount);
        expect(after.allocatedSlotCount).toBe(before.allocatedSlotCount);
        // Pin the counter to a value, not merely to being unchanged: comparing two reads of the
        // same counter also holds for a counter that never advances at all.
        expect(before.addLeafCount).toBe(64);

        expect(arena.addLeaf(tip, 65)).toBe(before.publishedNodeCount + 1);
        expect(arena.statistics().addLeafCount).toBe(65);
    });

    test("branches below a shared parent stay independent", () => {
        const arena = new MyersIncrementalAncestorArena<number>();
        const root = arena.addLeaf(arena.bottom, 0);
        const left = arena.addLeaf(root, 1);
        const right = arena.addLeaf(root, 2);

        expect(left).not.toBe(right);
        expect(arena.ancestorAtDepth(left, 0)).toBe(root);
        expect(arena.ancestorAtDepth(right, 0)).toBe(root);
        expect(arena.valueAt(left)).toBe(1);
        expect(arena.valueAt(right)).toBe(2);
        expect(arena.depthOf(left)).toBe(arena.depthOf(right));
    });

    test("every ancestor answer matches the naive parent walk", () => {
        // The shape must be both branching and deep, and the assertions below pin that rather than
        // trusting the construction: a chain cannot distinguish a jump landing on the wrong branch,
        // and a shallow shape never reaches addLeaf's coalescing arm at all.
        const arena = new MyersIncrementalAncestorArena<number>();
        const handles = [arena.bottom];
        const frontiers = [arena.bottom, arena.bottom, arena.bottom, arena.bottom];
        let seed = 12_345;
        for (let index = 1; index < 400; index += 1) {
            seed = (seed * 1_103_515_245 + 12_345) % 2_147_483_648;
            const slot = index % frontiers.length;
            const parent = frontiers[slot];
            if (parent === undefined) throw new Error("frontier missing");
            const node = arena.addLeaf(parent, index);
            frontiers[slot] = node;
            handles.push(node);
            if (index % 97 === 0) frontiers.push(handles[seed % handles.length] ?? arena.bottom);
        }

        const depths = handles.map((handle) => arena.depthOf(handle));
        const deepestDepth = Math.max(...depths);
        // Count FORKS, not distinct parents: a pure chain maximises distinct parents (one per node),
        // so `parents.size > 1` was implied by the depth assertion below and could not fail. A fork
        // is a node that is the parent of more than one child, which is what makes a mis-aimed jump
        // able to land on the wrong branch at all.
        const childCounts = new Map<number, number>();
        for (const handle of handles.slice(1)) {
            const parent = arena.parentOf(handle);
            childCounts.set(parent, (childCounts.get(parent) ?? 0) + 1);
        }
        const forks = [...childCounts.values()].filter((count) => count > 1).length;
        expect(forks).toBeGreaterThanOrEqual(3);
        expect(deepestDepth).toBeGreaterThanOrEqual(20);

        // Coalescing must actually have happened: reaching bottom from the deepest node in fewer
        // hops than its depth is only possible if jump links were built and followed.
        const deepest = handles[depths.indexOf(deepestDepth)];
        if (deepest === undefined) throw new Error("deepest missing");
        expect(arena.ancestorAtDepth(deepest, -1)).toBe(arena.bottom);
        expect(arena.statistics().lastAncestorHopCount).toBeLessThan(deepestDepth);

        for (const handle of handles) {
            arena.validateAncestry(handle);
            const chain: number[] = [];
            let walker = handle;
            while (walker !== arena.bottom) { chain.push(walker); walker = arena.parentOf(walker); }
            chain.reverse();
            expect(arena.ancestorAtDepth(handle, -1)).toBe(arena.bottom);
            for (let depth = 0; depth < chain.length; depth += 1) {
                expect(arena.ancestorAtDepth(handle, depth)).toBe(chain[depth]);
            }
        }
    });
});
