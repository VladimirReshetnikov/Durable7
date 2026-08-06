/**
 * Tests for the ancestral connection forest: the pinned injective vertex hash that earns the
 * unconditional CHAMP factor, union-by-size with a first-endpoint tie-break, the LCA-based
 * first-connection witness, reference-identity version tokens across sibling branches, connectivity
 * sharing on a redundant link, and the structural audit.
 */
import fc from "fast-check";
import { describe, expect, test } from "vitest";

import {
    AncestralConnectionVersion,
    maximumVertexCount,
    PersistentAncestralConnectionForest,
    vertexHashPolicy,
} from "../../src/hamt/persistent-ancestral-connection-forest.js";

type Forest = PersistentAncestralConnectionForest;

function forest(vertexCount: number): Forest {
    return PersistentAncestralConnectionForest.create(vertexCount);
}

/** Newton-iterated inverse of an odd word modulo 2**32; five doublings cover 32 bits. */
function modularInverse(value: number): number {
    let inverse = value | 0;
    for (let step = 0; step < 5; step++) inverse = Math.imul(inverse, 2 - Math.imul(value, inverse));
    return inverse | 0;
}

describe("vertexHashPolicy", () => {
    test("is injective over the vertex universe by an explicit inverse round-trip", () => {
        // Invert fmix32 step by step: an xor-shift by >= 16 is self-inverse, the 13-bit xor-shift
        // inverts by applying its expansion twice, and each odd multiplier inverts by its
        // Newton-iterated modular inverse. A successful round trip proves injectivity on the tested
        // values by exhibiting the inverse, which no sampling-only test can.
        const firstInverse = modularInverse(0x85ebca6b);
        const secondInverse = modularInverse(0xc2b2ae35);
        expect(Math.imul(firstInverse, 0x85ebca6b) | 0).toBe(1);
        expect(Math.imul(secondInverse, 0xc2b2ae35) | 0).toBe(1);

        const unmix = (hash: number): number => {
            let bits = hash | 0;
            bits ^= bits >>> 16;
            bits = Math.imul(bits, secondInverse);
            bits ^= (bits >>> 13) ^ (bits >>> 26);
            bits = Math.imul(bits, firstInverse);
            return (bits ^ (bits >>> 16)) | 0;
        };

        const samples: number[] = [0, 1, 2, 31, 32, 1023, maximumVertexCount - 1, maximumVertexCount];
        for (let index = 0; index < 8192; index++) samples.push(Math.imul(index, 524_287) & maximumVertexCount);
        // The bijection covers the whole 32-bit word, not only the half the forest can address.
        samples.push(-1, -2, -0x8000_0000, 0x7fff_ffff);

        for (const vertex of samples) {
            expect(unmix(vertexHashPolicy.hash(vertex))).toBe(vertex | 0);
        }

        // A sampling control alongside the constructive proof: no two sampled vertices collide.
        const distinct = new Set(samples.map((vertex) => vertexHashPolicy.hash(vertex)));
        expect(distinct.size).toBe(new Set(samples).size);
        expect(vertexHashPolicy.equivalent(7, 7)).toBe(true);
        expect(vertexHashPolicy.equivalent(7, 8)).toBe(false);
    });
});

describe("PersistentAncestralConnectionForest.create", () => {
    test("publishes singleton components and a root history without storing a cell", () => {
        const root = forest(5);

        expect(root.vertexCount).toBe(5);
        expect(root.componentCount).toBe(5);
        expect(root.version.depth).toBe(0);
        expect(root.version.parent).toBeUndefined();
        expect(root.version.root).toBe(root.version);
        expect(root.version.isSameVersion(root.version.root)).toBe(true);
        for (let vertex = 0; vertex < root.vertexCount; vertex++) {
            expect(root.find(vertex)).toBe(vertex);
            expect(root.connected(vertex, vertex)).toBe(true);
            expect(root.componentSize(vertex)).toBe(1);
            expect(root.firstConnected(vertex, vertex)).toBe(root.version);
        }

        expect(root.connected(0, 1)).toBe(false);
        expect(root.firstConnected(0, 1)).toBeUndefined();
        expect(root.validateStructure()).toEqual({
            vertexCount: 5,
            componentCount: 5,
            storedCellCount: 0,
            maximumParentPathLength: 0,
            historyDepth: 0,
        });
    });

    test("accepts an empty and a singleton universe", () => {
        const empty = forest(0);
        expect(empty.vertexCount).toBe(0);
        expect(empty.componentCount).toBe(0);
        expect(() => empty.find(0)).toThrow(RangeError);
        expect(empty.validateStructure().storedCellCount).toBe(0);

        const single = forest(1);
        expect(single.componentCount).toBe(1);
        expect(single.find(0)).toBe(0);
        expect(single.firstConnected(0, 0)).toBe(single.version);
        expect(single.validateStructure().maximumParentPathLength).toBe(0);
    });

    test("rejects a universe outside the representable range", () => {
        expect(() => forest(-1)).toThrow(RangeError);
        expect(() => forest(1.5)).toThrow(RangeError);
        expect(() => forest(Number.NaN)).toThrow(RangeError);
        expect(() => forest(maximumVertexCount + 1)).toThrow(RangeError);
        expect(forest(maximumVertexCount).vertexCount).toBe(maximumVertexCount);
    });
});

describe("PersistentAncestralConnectionForest.link", () => {
    test("records the version in which each pair first became connected", () => {
        const root = forest(6);
        const first = root.link(0, 1);
        const second = first.link(2, 3);
        const bridge = second.link(0, 2);
        const attach = bridge.link(4, 0);

        expect(attach.componentCount).toBe(2);
        expect(attach.firstConnected(0, 1)).toBe(first.version);
        expect(attach.firstConnected(2, 3)).toBe(second.version);
        expect(attach.firstConnected(0, 3)).toBe(bridge.version);
        expect(attach.firstConnected(1, 2)).toBe(bridge.version);
        expect(attach.firstConnected(4, 1)).toBe(attach.version);
        expect(attach.firstConnected(4, 4)).toBe(root.version);
        expect(attach.firstConnected(0, 5)).toBeUndefined();
        expect(attach.find(0)).toBe(0);
        expect(attach.find(4)).toBe(0);

        // Every retained version keeps its own connectivity, untouched by later descendants.
        expect(root.componentCount).toBe(6);
        expect(root.connected(0, 1)).toBe(false);
        expect(root.firstConnected(0, 1)).toBeUndefined();
        expect(first.connected(0, 2)).toBe(false);
        expect(first.componentCount).toBe(5);
        expect(first.firstConnected(0, 1)).toBe(first.version);
        for (const version of [root, first, second, bridge, attach]) version.validateStructure();
    });

    test("always publishes a distinct child version, sharing cells only when redundant", () => {
        const root = forest(4);
        const linked = root.link(0, 1);
        const reverseDuplicate = linked.link(1, 0);
        const selfDuplicate = reverseDuplicate.link(0, 0);

        // The receiver is never returned, so the sharing assertions below compare distinct values.
        expect(reverseDuplicate).not.toBe(linked);
        expect(selfDuplicate).not.toBe(reverseDuplicate);
        expect(linked.version.depth).toBe(1);
        expect(reverseDuplicate.version.depth).toBe(2);
        expect(selfDuplicate.version.depth).toBe(3);
        expect(reverseDuplicate.version.parent).toBe(linked.version);
        expect(selfDuplicate.version.parent).toBe(reverseDuplicate.version);
        expect(reverseDuplicate.version).not.toBe(linked.version);
        expect(reverseDuplicate.componentCount).toBe(linked.componentCount);
        expect(selfDuplicate.componentCount).toBe(linked.componentCount);

        // Redundant links share the connectivity index; a successful one publishes a new index.
        expect(linked.sharesConnectivityRootWith(reverseDuplicate)).toBe(true);
        expect(reverseDuplicate.sharesConnectivityRootWith(selfDuplicate)).toBe(true);
        expect(root.sharesConnectivityRootWith(linked)).toBe(false);
        expect(linked.sharesConnectivityRootWith(linked.link(2, 3))).toBe(false);

        expect(selfDuplicate.firstConnected(0, 1)).toBe(linked.version);
        expect(selfDuplicate.firstConnected(2, 2)).toBe(root.version);
        expect(selfDuplicate.firstConnected(0, 2)).toBeUndefined();
        selfDuplicate.validateStructure();
    });

    test("unions by size and keeps the first endpoint's root on a tie", () => {
        const root = forest(16);
        const first = root.link(0, 1);
        const second = first.link(2, 3);

        expect(second.find(1)).toBe(0);
        expect(second.find(3)).toBe(2);

        // Equal sizes: the first endpoint's root stays the representative in either direction.
        const merged = second.link(3, 1);
        expect(merged.find(0)).toBe(2);
        expect(second.link(1, 3).find(2)).toBe(0);
        expect(merged.componentCount).toBe(13);

        // Unequal sizes: the larger component's root wins regardless of endpoint order.
        const attachedRight = merged.link(4, 0);
        const attachedLeft = merged.link(0, 4);
        expect(attachedRight.find(4)).toBe(2);
        expect(attachedLeft.find(4)).toBe(2);
        expect(attachedRight.componentSize(4)).toBe(5);
        merged.validateStructure();
        attachedLeft.validateStructure();
    });

    test("rebuilds only the touched cells and leaves the receiver untouched", () => {
        const root = forest(8);
        const first = root.link(0, 1);
        const second = first.link(2, 3);

        expect(first.sharesConnectivityRootWith(second)).toBe(false);
        expect(second.sharesConnectivityRootWith(second.link(0, 1))).toBe(true);
        expect(second.sharesConnectivityRootWith(second.link(3, 2))).toBe(true);
        expect(root.componentCount).toBe(8);
        expect(first.componentCount).toBe(7);
        expect(second.componentCount).toBe(6);
        expect(second.validateStructure().storedCellCount).toBe(4);
        expect(first.validateStructure().storedCellCount).toBe(2);
    });
});

describe("PersistentAncestralConnectionForest.firstConnected", () => {
    test("uses the pair's LCA rather than the latest component birth", () => {
        const root = forest(8);
        const ab = root.link(0, 1);
        const cd = ab.link(2, 3);
        const abcd = cd.link(1, 3);
        const ef = abcd.link(4, 5);
        const gh = ef.link(6, 7);
        const efgh = gh.link(4, 6);
        const all = efgh.link(0, 4);

        expect(all.firstConnected(0, 1)).toBe(ab.version);
        expect(all.firstConnected(2, 3)).toBe(cd.version);
        expect(all.firstConnected(0, 2)).toBe(abcd.version);
        expect(all.firstConnected(4, 5)).toBe(ef.version);
        expect(all.firstConnected(6, 7)).toBe(gh.version);
        expect(all.firstConnected(5, 7)).toBe(efgh.version);
        expect(all.firstConnected(1, 6)).toBe(all.version);
        all.validateStructure();
    });

    test("is independent of endpoint order, so the depth comparison never depends on scan order", () => {
        const edges: ReadonlyArray<readonly [number, number]> =
            [[0, 1], [2, 3], [1, 3], [4, 5], [6, 7], [4, 6], [0, 4]];
        let current = forest(8);
        for (const [left, right] of edges) current = current.link(left, right);
        for (let left = 0; left < current.vertexCount; left++) {
            for (let right = 0; right < current.vertexCount; right++) {
                // The forward scan visits the left path first and the reversed scan the right one,
                // so an order-dependent comparison would disagree here.
                expect(current.firstConnected(left, right)).toBe(current.firstConnected(right, left));
            }
        }
    });

    test("handles one endpoint sitting at the LCA after that LCA becomes a child", () => {
        const root = forest(5);
        const pair = root.link(0, 1);
        const otherPair = pair.link(2, 3);
        const larger = otherPair.link(2, 4);
        const merged = larger.link(2, 0);

        expect(merged.find(0)).toBe(2);
        expect(merged.firstConnected(0, 1)).toBe(pair.version);
        expect(merged.firstConnected(0, 2)).toBe(merged.version);
        expect(merged.firstConnected(1, 4)).toBe(merged.version);
        merged.validateStructure();
    });

    test("returns the history root for a vertex with itself and never a descendant witness", () => {
        const root = forest(3);
        const linked = root.link(0, 1);
        const later = linked.link(1, 2);

        expect(later.firstConnected(2, 2)).toBe(root.version);
        expect(later.firstConnected(2, 2)).toBe(later.version.root);
        expect(later.firstConnected(0, 1)).toBe(linked.version);
        expect(later.firstConnected(0, 2)).toBe(later.version);
    });

    test("keeps a long redundant history from replacing the union witness", () => {
        const root = forest(3);
        const first = root.link(0, 1);
        let current = first;
        for (let step = 0; step < 20_000; step++) current = current.link(2, 2);

        expect(current.version.depth).toBe(20_001);
        expect(current.firstConnected(0, 1)).toBe(first.version);
        expect(current.firstConnected(0, 2)).toBeUndefined();
        expect(first.sharesConnectivityRootWith(current)).toBe(true);
        expect(root.sharesConnectivityRootWith(current)).toBe(false);
        expect(current.validateStructure().historyDepth).toBe(20_001);
    });
});

describe("AncestralConnectionVersion", () => {
    test("keeps sibling branches at equal depth distinct", () => {
        const root = forest(5);
        const common = root.link(0, 1);
        const left = common.link(0, 2);
        const right = common.link(0, 3);
        const leftLater = left.link(2, 4);
        const rightLater = right.link(3, 4);

        expect(left.version.depth).toBe(right.version.depth);
        expect(left.version).not.toBe(right.version);
        expect(left.version.isSameVersion(right.version)).toBe(false);
        expect(left.firstConnected(0, 1)).toBe(common.version);
        expect(right.firstConnected(0, 1)).toBe(common.version);
        expect(leftLater.firstConnected(1, 2)).toBe(left.version);
        expect(rightLater.firstConnected(1, 3)).toBe(right.version);
        expect(leftLater.firstConnected(0, 4)).toBe(leftLater.version);
        expect(rightLater.firstConnected(0, 4)).toBe(rightLater.version);
        expect(leftLater.connected(0, 3)).toBe(false);
        expect(rightLater.connected(0, 2)).toBe(false);
        expect(leftLater.version.root).toBe(root.version);
        expect(rightLater.version.root).toBe(root.version);
        leftLater.validateStructure();
        rightLater.validateStructure();
    });

    test("has no structural equality, so equal-depth siblings never merge in a Set or Map", () => {
        const names = Object.getOwnPropertyNames(AncestralConnectionVersion.prototype);
        for (const forbidden of ["equals", "hashCode", "compareTo", "valueOf", "toJSON"]) {
            expect(names).not.toContain(forbidden);
        }

        // Both tokens record the same depth, the same parent, and the same root: a structural
        // comparison would call them equal and let one branch's witness stand for the other's.
        const common = forest(6).link(0, 1);
        const left = common.link(2, 3);
        const right = common.link(2, 3);
        expect(left.version.depth).toBe(right.version.depth);
        expect(left.version.parent).toBe(right.version.parent);
        expect(left.version.root).toBe(right.version.root);
        expect(left.version).not.toBe(right.version);
        expect(new Set([left.version, right.version]).size).toBe(2);
        expect(new Set([left.version, left.version]).size).toBe(1);
        const labelled = new Map([[left.version, "left"], [right.version, "right"]]);
        expect(labelled.get(right.version)).toBe("right");
        expect(labelled.size).toBe(2);
        expect(left.firstConnected(2, 3)).toBe(left.version);
        expect(right.firstConnected(2, 3)).toBe(right.version);
        expect(left.firstConnected(2, 3)).not.toBe(right.version);
    });

    test("gives separate forests separate version trees", () => {
        const leftRoot = forest(2);
        const rightRoot = forest(2);
        const left = leftRoot.link(0, 1);
        const right = rightRoot.link(0, 1);

        expect(leftRoot.version).not.toBe(rightRoot.version);
        expect(left.version).not.toBe(right.version);
        expect(left.version.depth).toBe(right.version.depth);
        expect(left.firstConnected(0, 1)).toBe(left.version);
        expect(right.firstConnected(0, 1)).toBe(right.version);
        expect(left.firstConnected(0, 1)).not.toBe(right.firstConnected(0, 1));
    });

    test("reaches a caller only through a published forest version", () => {
        const root = forest(2);
        expect(root.version).toBeInstanceOf(AncestralConnectionVersion);
        expect(root.link(0, 1).version).toBeInstanceOf(AncestralConnectionVersion);
        expect(root.version.parent).toBeUndefined();
    });
});

describe("PersistentAncestralConnectionForest.componentSize", () => {
    function totalComponentSize(source: Forest): number {
        const roots = new Map<number, number>();
        for (let vertex = 0; vertex < source.vertexCount; vertex++) {
            roots.set(source.find(vertex), source.componentSize(vertex));
        }
        expect(roots.size).toBe(source.componentCount);
        let total = 0;
        for (const size of roots.values()) total += size;
        return total;
    }

    test("reports the cached union size for every retained version", () => {
        const root = forest(6);
        expect(totalComponentSize(root)).toBe(6);

        const pair = root.link(0, 1);
        const triple = pair.link(1, 2);
        const quad = triple.link(2, 3);

        expect(pair.componentSize(0)).toBe(2);
        expect(pair.componentSize(1)).toBe(2);
        expect(pair.componentSize(5)).toBe(1);
        expect(triple.componentSize(2)).toBe(3);
        for (let vertex = 0; vertex < 4; vertex++) expect(quad.componentSize(vertex)).toBe(4);
        expect(quad.componentSize(4)).toBe(1);
        expect(totalComponentSize(quad)).toBe(6);

        const redundant = quad.link(3, 0);
        for (let vertex = 0; vertex < quad.vertexCount; vertex++) {
            expect(redundant.componentSize(vertex)).toBe(quad.componentSize(vertex));
        }

        const branch = quad.link(4, 5);
        expect(branch.componentSize(4)).toBe(2);
        expect(quad.componentSize(4)).toBe(1);
        expect(pair.componentSize(2)).toBe(1);
        expect(root.componentSize(0)).toBe(1);
        expect(totalComponentSize(branch)).toBe(6);
        expect(totalComponentSize(pair)).toBe(6);

        const merged = branch.link(5, 0);
        expect(merged.componentCount).toBe(1);
        for (let vertex = 0; vertex < merged.vertexCount; vertex++) expect(merged.componentSize(vertex)).toBe(6);
        merged.validateStructure();
    });
});

describe("PersistentAncestralConnectionForest.validateStructure", () => {
    test("holds the union-by-size height ceiling over a balanced doubling history", () => {
        const count = 1024;
        const root = forest(count);
        let current = root;
        let firstPairVersion: AncestralConnectionVersion | undefined;
        let finalBridgeVersion: AncestralConnectionVersion | undefined;

        for (let width = 1; width < count; width *= 2) {
            for (let start = 0; start < count; start += width * 2) {
                current = current.link(start, start + width);
                if (start === 0 && width === 1) firstPairVersion = current.version;
                if (start === 0 && width === count / 2) finalBridgeVersion = current.version;
            }
        }

        expect(current.componentCount).toBe(1);
        expect(current.find(count - 1)).toBe(0);
        expect(current.componentSize(count - 1)).toBe(count);
        expect(current.firstConnected(0, 1)).toBe(firstPairVersion);
        expect(current.firstConnected(0, count - 1)).toBe(finalBridgeVersion);
        expect(root.componentCount).toBe(count);
        expect(current.validateStructure()).toEqual({
            vertexCount: count,
            componentCount: 1,
            storedCellCount: count,
            maximumParentPathLength: 10,
            historyDepth: count - 1,
        });
    });

    test("stores only the vertices a union touched in a huge sparse universe", () => {
        const root = forest(maximumVertexCount);
        expect(root.validateStructure().storedCellCount).toBe(0);

        const linked = root.link(0, maximumVertexCount - 1);
        expect(linked.componentCount).toBe(maximumVertexCount - 1);
        expect(linked.connected(0, maximumVertexCount - 1)).toBe(true);
        expect(linked.connected(0, maximumVertexCount - 2)).toBe(false);
        expect(linked.find(maximumVertexCount - 2)).toBe(maximumVertexCount - 2);
        expect(linked.componentSize(0)).toBe(2);
        expect(linked.componentSize(maximumVertexCount - 2)).toBe(1);
        expect(linked.firstConnected(0, maximumVertexCount - 1)).toBe(linked.version);
        expect(linked.validateStructure()).toEqual({
            vertexCount: maximumVertexCount,
            componentCount: maximumVertexCount - 1,
            storedCellCount: 2,
            maximumParentPathLength: 1,
            historyDepth: 1,
        });
    });
});

describe("PersistentAncestralConnectionForest vertex validation", () => {
    test("rejects vertices outside the universe distinguishably from the disconnected answer", () => {
        const root = forest(3);
        const rejected: Array<() => unknown> = [
            () => root.find(-1),
            () => root.find(3),
            () => root.find(1.5),
            () => root.connected(-1, 0),
            () => root.connected(0, 3),
            () => root.componentSize(-1),
            () => root.componentSize(3),
            () => root.link(-1, 0),
            () => root.link(0, 3),
            () => root.firstConnected(-1, 0),
            () => root.firstConnected(0, 3),
            () => root.firstConnected(Number.NaN, 0),
        ];
        for (const operation of rejected) expect(operation).toThrow(RangeError);

        // Out of universe throws; merely disconnected returns the absent result.
        expect(root.firstConnected(0, 1)).toBeUndefined();
        expect(root.connected(0, 1)).toBe(false);

        // A rejected operation publishes nothing: the receiver is exactly as it was.
        expect(root.version.root).toBe(root.version);
        expect(root.version.depth).toBe(0);
        expect(root.componentCount).toBe(3);
        root.validateStructure();
    });
});

interface ModelVersion {
    readonly actual: Forest;
    readonly classes: readonly number[];
    readonly parent: number | undefined;
}

/** The naive answer: the shallowest ancestor version whose classes already agree. */
function firstConnectedByScan(
    states: readonly ModelVersion[],
    index: number,
    left: number,
    right: number,
): AncestralConnectionVersion | undefined {
    const lineage: number[] = [];
    let current: number | undefined = index;
    while (current !== undefined) {
        lineage.push(current);
        current = states[current]!.parent;
    }
    for (let position = lineage.length - 1; position >= 0; position--) {
        const candidate = states[lineage[position]!]!;
        if (candidate.classes[left] === candidate.classes[right]) return candidate.actual.version;
    }
    return undefined;
}

function extend(states: ModelVersion[], parentIndex: number, left: number, right: number): void {
    const source = states[parentIndex]!;
    const classes = source.classes.slice();
    const leftClass = classes[left]!;
    const rightClass = classes[right]!;
    if (leftClass !== rightClass) {
        for (let vertex = 0; vertex < classes.length; vertex++) {
            if (classes[vertex] === rightClass) classes[vertex] = leftClass;
        }
    }

    const actual = source.actual.link(left, right);
    expect(actual.version.parent).toBe(source.actual.version);
    expect(actual.version.depth).toBe(source.actual.version.depth + 1);
    expect(actual.componentCount).toBe(new Set(classes).size);
    states.push({ actual, classes, parent: parentIndex });
}

function checkAllPairs(states: readonly ModelVersion[], index: number): void {
    const state = states[index]!;
    for (let left = 0; left < state.actual.vertexCount; left++) {
        for (let right = 0; right < state.actual.vertexCount; right++) {
            expect(state.actual.connected(left, right)).toBe(state.classes[left] === state.classes[right]);
            expect(state.actual.firstConnected(left, right))
                .toBe(firstConnectedByScan(states, index, left, right));
        }
    }
}

describe("PersistentAncestralConnectionForest branching histories", () => {
    test("match a naive ancestor scan over a deterministic pseudorandom history", () => {
        const vertexCount = 8;
        let seed = 0x5eed2026;
        const next = (): number => {
            seed = (Math.imul(seed, 1_664_525) + 1_013_904_223) | 0;
            return (seed >>> 8) % 0x1000000;
        };

        const states: ModelVersion[] = [{
            actual: forest(vertexCount),
            classes: Array.from({ length: vertexCount }, (_unused, vertex) => vertex),
            parent: undefined,
        }];

        for (let operation = 0; operation < 160; operation++) {
            const parentIndex = next() % states.length;
            extend(states, parentIndex, next() % vertexCount, next() % vertexCount);
            checkAllPairs(states, states.length - 1);
            if (operation % 25 === 0) states[states.length - 1]!.actual.validateStructure();
        }

        // Recheck retained versions once every sibling branch has been constructed.
        for (let probe = 0; probe < 60; probe++) {
            const index = next() % states.length;
            const left = next() % vertexCount;
            const right = next() % vertexCount;
            expect(states[index]!.actual.firstConnected(left, right))
                .toBe(firstConnectedByScan(states, index, left, right));
        }
    });

    test("match a naive ancestor scan under fast-check branching", () => {
        const vertexCount = 6;
        fc.assert(
            fc.property(
                fc.array(
                    fc.tuple(fc.nat(999), fc.nat(vertexCount - 1), fc.nat(vertexCount - 1)),
                    { minLength: 1, maxLength: 24 },
                ),
                (operations) => {
                    const states: ModelVersion[] = [{
                        actual: forest(vertexCount),
                        classes: Array.from({ length: vertexCount }, (_unused, vertex) => vertex),
                        parent: undefined,
                    }];
                    for (const [parentChoice, left, right] of operations) {
                        extend(states, parentChoice % states.length, left, right);
                        checkAllPairs(states, states.length - 1);
                    }
                    for (let index = 0; index < states.length; index++) checkAllPairs(states, index);
                    states[states.length - 1]!.actual.validateStructure();
                },
            ),
            { numRuns: 30 },
        );
    });
});
