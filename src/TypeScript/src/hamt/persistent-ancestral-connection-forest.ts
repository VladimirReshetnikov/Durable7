/**
 * Fully branching persistent insertion-only connectivity forest with a first-connection query.
 *
 * `PersistentAncestralConnectionForest` is a persistent union-find over the fixed integer vertex
 * universe `0 .. vertexCount - 1`. Each successful `link` performs union by size *without* path
 * compression and labels the one new root-parent edge with the child `AncestralConnectionVersion`.
 * A redundant (already connected) link still publishes a distinct child history version, but shares
 * the complete connectivity index, which `sharesConnectivityRootWith` can confirm.
 *
 * The parent cells live sparsely in the CHAMP `PersistentHashMap`. An absent cell denotes a
 * singleton root, so `create` is O(1) rather than O(`vertexCount`). Union by size limits every
 * parent path to O(log n) cells, where n is the component size. `firstConnected` answers "at which
 * ancestor version did these two vertices first become connected?" by comparing the two current
 * parent paths and taking the latest union-edge version strictly below their forest lowest common
 * ancestor. It never searches the version history, so its cost is independent of history depth.
 *
 * Instances and version tokens are immutable snapshots. Deletion, confluent merging, retroactive
 * updates, and path compression are deliberately outside this module's contract. The O(log n)
 * parent-path factor is worst-case logarithmic in component size rather than the inverse-Ackermann
 * amortized bound of an ephemeral linear-history union-find.
 *
 * ## Why the CHAMP factor is unconditional here
 *
 * Throughout this module w denotes the cost of one parent-cell lookup or insertion in the backing
 * CHAMP map. That substrate narrows every hash-policy result to a signed 32-bit word, consumes five
 * hash bits per trie level, and parks keys sharing a full 32-bit hash in a collision node whose
 * bucket is scanned linearly. A collision bucket can therefore hold two distinct keys, and under a
 * truncating or colliding hash w would be bounded only *in expectation*.
 *
 * This forest removes that caveat by pinning `vertexHashPolicy` rather than accepting the workspace
 * default. The pinned function is the MurmurHash3 32-bit finalizer `fmix32` applied to the vertex's
 * two's-complement pattern. Every step of it is a bijection of the 32-bit word — `h ^= h >>> k` with
 * `k > 0` leaves the top `k` bits fixed and is undone by the same shape of expression, and
 * multiplication by an odd constant is invertible modulo 2**32 (both constants are odd) — and the
 * substrate's own `| 0` narrowing is a bijection too. Vertices are validated into
 * `0 .. vertexCount - 1` before any cell is touched and `maximumVertexCount` caps that universe at
 * 2**31 - 1, so the whole composition is *injective* over every key the map can ever see. No
 * collision bucket can hold two distinct vertices, and two distinct vertices must diverge within 32
 * significant hash bits, that is within `ceil(32 / 5) = 7` levels of 32-way nodes. One cell read or
 * write is therefore O(1) worst case, and every bound stated below is unconditional in both factors.
 * The accompanying test proves the injectivity by exhibiting an explicitly constructed inverse
 * rather than by sampling for absent collisions.
 *
 * ## Recursion
 *
 * Every walk here — version chains, parent paths, and the audit — is an explicit loop, so a history
 * hundreds of thousands of links deep costs no call stack. The CHAMP map's own descent recurses,
 * but over at most seven levels.
 */
import { createHashPolicy, type HashPolicy } from "./hash-policy.js";
import { PersistentHashMap } from "./persistent-hamt.js";

/**
 * Largest permitted vertex universe.
 *
 * The reference implementation's universe is a 32-bit integer, and this bound is also what keeps
 * the pinned vertex hash injective after the CHAMP map narrows it to a signed 32-bit word.
 */
export const maximumVertexCount: number = 0x7fff_ffff;

/** The MurmurHash3 32-bit finalizer, a bijection of the 32-bit word. */
function vertexHash(vertex: number): number {
    let bits = vertex | 0;
    bits ^= bits >>> 16;
    bits = Math.imul(bits, 0x85ebca6b);
    bits ^= bits >>> 13;
    bits = Math.imul(bits, 0xc2b2ae35);
    return (bits ^ (bits >>> 16)) | 0;
}

function vertexEquivalent(left: number, right: number): boolean { return left === right; }

/**
 * The hash policy this forest pins into its parent-cell map.
 *
 * Part of the type's complexity contract rather than a tuning detail: its injectivity over the
 * vertex universe is what makes the CHAMP factor w a worst-case rather than an expected constant.
 * Only injectivity is promised; the particular value is not part of the contract.
 */
export const vertexHashPolicy: HashPolicy<number> = createHashPolicy(vertexHash, vertexEquivalent);

let publishRootVersion: () => AncestralConnectionVersion;
let publishChildVersion: (parent: AncestralConnectionVersion) => AncestralConnectionVersion;

/**
 * Identifies one immutable version in a `PersistentAncestralConnectionForest` history tree.
 *
 * Tokens use *reference* identity: they deliberately have no `equals`, no `hashCode`, and no
 * `valueOf`, so `===`, `Object.is`, `Set`, and `Map` all compare identity. A token records only a
 * depth, a parent, and a root, so a structural comparison would make two sibling branches that
 * split at the same depth compare equal and let a witness from one branch masquerade as a witness
 * from the other. Use `isSameVersion` where the intent should be explicit in the reading.
 *
 * Tokens are published only by `PersistentAncestralConnectionForest.create` and `link`; the
 * constructor is private. The parent chain contains history identities, not mutable connectivity
 * state. Every accessor is O(1).
 */
export class AncestralConnectionVersion {
    readonly #parent: AncestralConnectionVersion | undefined;
    readonly #depth: number;
    readonly #root: AncestralConnectionVersion;

    static {
        publishRootVersion = (): AncestralConnectionVersion => new AncestralConnectionVersion(undefined);
        publishChildVersion = (parent: AncestralConnectionVersion): AncestralConnectionVersion =>
            new AncestralConnectionVersion(parent);
    }

    private constructor(parent: AncestralConnectionVersion | undefined) {
        if (parent !== undefined && parent.#depth >= Number.MAX_SAFE_INTEGER) {
            throw new RangeError("The connection-forest history depth would exceed the exact-integer range.");
        }
        this.#parent = parent;
        this.#depth = parent === undefined ? 0 : parent.#depth + 1;
        this.#root = parent === undefined ? this : parent.#root;
    }

    /** This version's parent, or `undefined` at the history root. O(1). */
    public get parent(): AncestralConnectionVersion | undefined { return this.#parent; }

    /** The number of link events from the history root. O(1). */
    public get depth(): number { return this.#depth; }

    /** The root identity of this version tree, cached rather than walked. O(1). */
    public get root(): AncestralConnectionVersion { return this.#root; }

    /** Whether two handles denote the very same version. O(1), and identical to `===`. */
    public isSameVersion(other: AncestralConnectionVersion): boolean { return this === other; }
}

/** Shape measurements returned by a successful structural audit. */
export interface AncestralConnectionForestStatistics {
    /** The fixed vertex-universe size. */
    readonly vertexCount: number;
    /** The number of connected components, recounted from the parent forest. */
    readonly componentCount: number;
    /** The number of explicitly stored parent cells; absent cells are singleton roots. */
    readonly storedCellCount: number;
    /** The longest parent path found, in edges. Bounded by `floor(log2(vertexCount))`. */
    readonly maximumParentPathLength: number;
    /** The number of link events from the history root to the audited version. */
    readonly historyDepth: number;
}

/**
 * One sparse parent record. `size` is meaningful only for a root, whose `parent` is the vertex
 * itself, and `joinedAt` is present only on a non-root edge. An absent cell canonically means
 * "singleton root of size 1".
 */
interface Cell {
    readonly parent: number;
    readonly size: number;
    readonly joinedAt: AncestralConnectionVersion | undefined;
}

/** One vertex on a parent path together with the union-edge tag leaving it. */
interface PathStep {
    readonly vertex: number;
    readonly joinedAt: AncestralConnectionVersion | undefined;
}

function rootCell(vertex: number, size: number): Cell { return { parent: vertex, size, joinedAt: undefined }; }

function childCell(parent: number, joinedAt: AncestralConnectionVersion): Cell {
    return { parent, size: 0, joinedAt };
}

/**
 * The deeper of two candidate versions, keeping `left` on an equal depth.
 *
 * Every tag reachable from one version's cells lies on that version's own ancestor chain, so the
 * candidates are totally ordered by depth and an actual tie cannot arise through the public API;
 * the rule is a determinism guarantee that keeps the comparison independent of accumulation order.
 */
function later(
    left: AncestralConnectionVersion | undefined,
    right: AncestralConnectionVersion | undefined,
): AncestralConnectionVersion | undefined {
    return right !== undefined && (left === undefined || right.depth > left.depth) ? right : left;
}

/**
 * A fully branching persistent insertion-only connectivity forest that reports the first ancestor
 * version in which two vertices became connected, without searching the version history.
 *
 * See the module documentation for the cost model; w below is the CHAMP path cost, which the pinned
 * `vertexHashPolicy` makes an unconditional O(1).
 */
export class PersistentAncestralConnectionForest {
    readonly #cells: PersistentHashMap<number, Cell>;
    readonly #vertexCount: number;
    readonly #componentCount: number;
    readonly #version: AncestralConnectionVersion;

    private constructor(
        vertexCount: number,
        componentCount: number,
        cells: PersistentHashMap<number, Cell>,
        version: AncestralConnectionVersion,
    ) {
        this.#vertexCount = vertexCount;
        this.#componentCount = componentCount;
        this.#cells = cells;
        this.#version = version;
    }

    /**
     * An edgeless root version over vertices `0 .. vertexCount - 1`. O(1).
     *
     * Every vertex starts as its own component. Because a singleton is represented by the *absence*
     * of a cell, construction costs the same for a universe of two vertices and for one of
     * `maximumVertexCount`. Throws `RangeError` when `vertexCount` is not an integer in
     * `0 .. maximumVertexCount`.
     */
    public static create(vertexCount: number): PersistentAncestralConnectionForest {
        if (!Number.isInteger(vertexCount) || vertexCount < 0) {
            throw new RangeError("Vertex count must be a non-negative integer.");
        }
        if (vertexCount > maximumVertexCount) {
            throw new RangeError(`Vertex count cannot exceed ${maximumVertexCount}.`);
        }
        return new PersistentAncestralConnectionForest(
            vertexCount,
            vertexCount,
            PersistentHashMap.empty<number, Cell>(vertexHashPolicy),
            publishRootVersion(),
        );
    }

    /** The fixed number of vertices in this forest. O(1). */
    public get vertexCount(): number { return this.#vertexCount; }

    /** The number of connected components in this version. O(1). */
    public get componentCount(): number { return this.#componentCount; }

    /** The identity token for this history version. O(1). */
    public get version(): AncestralConnectionVersion { return this.#version; }

    /**
     * The current union-find representative of a vertex. O(w log n) worst case.
     *
     * The representative is selected by deterministic union-by-size history and is an implementation
     * artifact: sibling versions need not agree on it. Throws `RangeError` when the vertex is
     * outside the fixed universe.
     */
    public find(vertex: number): number {
        this.#checkVertex(vertex);
        return this.#findRoot(vertex);
    }

    /**
     * Whether two vertices have the same current root. O(w log n) worst case. Throws `RangeError`
     * when either vertex is outside the fixed universe.
     */
    public connected(left: number, right: number): boolean {
        this.#checkVertices(left, right);
        return this.#findRoot(left) === this.#findRoot(right);
    }

    /**
     * The number of vertices in a vertex's connected component. O(w log n) worst case.
     *
     * An isolated vertex is still a singleton root and reports 1. The answer is the union-by-size
     * count cached at the component's current root, so the walk reads the structure without
     * compressing it; this forest deliberately has no path compression. Throws `RangeError` when
     * the vertex is outside the fixed universe.
     */
    public componentSize(vertex: number): number {
        this.#checkVertex(vertex);
        return this.#sizeOf(this.#findRoot(vertex));
    }

    /**
     * A child history version containing an undirected connection between two vertices.
     *
     * The returned version is always distinct, and is never the receiver. When the endpoints were
     * already connected — a self link included — its connectivity cells are shared unchanged, which
     * `sharesConnectivityRootWith` can confirm; otherwise exactly one union-by-size root edge is
     * added and labelled with the child version. On a size tie the first endpoint's root stays the
     * representative.
     *
     * O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1)
     * space when redundant. The receiver is left untouched, including on failure: `RangeError` for
     * an endpoint outside the fixed universe is raised before any version is published.
     */
    public link(left: number, right: number): PersistentAncestralConnectionForest {
        this.#checkVertices(left, right);
        let leftRoot = this.#findRoot(left);
        let rightRoot = this.#findRoot(right);
        const childVersion = publishChildVersion(this.#version);

        if (leftRoot === rightRoot) {
            return new PersistentAncestralConnectionForest(
                this.#vertexCount, this.#componentCount, this.#cells, childVersion);
        }

        let leftSize = this.#sizeOf(leftRoot);
        let rightSize = this.#sizeOf(rightRoot);
        if (leftSize < rightSize) {
            [leftRoot, rightRoot] = [rightRoot, leftRoot];
            [leftSize, rightSize] = [rightSize, leftSize];
        }

        // On a size tie the first endpoint's root remains the representative, making the result
        // deterministic without changing the logarithmic-height proof. The combined size cannot
        // overflow: the two components are disjoint, so their sum is at most the vertex universe.
        const cells = this.#cells
            .put(leftRoot, rootCell(leftRoot, leftSize + rightSize))
            .put(rightRoot, childCell(leftRoot, childVersion));
        return new PersistentAncestralConnectionForest(
            this.#vertexCount, this.#componentCount - 1, cells, childVersion);
    }

    /**
     * The earliest version on this version's root-to-current history in which the pair was
     * connected, or `undefined` when they are disconnected in this version.
     *
     * A vertex is connected to itself at the history root. The answer is computed from the two
     * current parent paths as the latest union-edge version strictly below their forest lowest
     * common ancestor; the version history is never searched. O(w log n) worst-case time and
     * O(log n) temporary path space, independent of history depth.
     *
     * The result is never a version on another branch, and a later merge of the pair's component
     * into a bigger one never replaces the pair's earlier witness. The two failure modes stay
     * distinguishable: a vertex outside the fixed universe throws `RangeError`, while a pair that is
     * merely disconnected in this version returns `undefined`.
     */
    public firstConnected(left: number, right: number): AncestralConnectionVersion | undefined {
        this.#checkVertices(left, right);
        if (left === right) return this.#version.root;

        const leftPath = this.#buildPath(left);
        const rightPath = this.#buildPath(right);
        if (leftPath[leftPath.length - 1]?.vertex !== rightPath[rightPath.length - 1]?.vertex) return undefined;

        // Delete the common rootward suffix. The remaining steps are precisely the two paths
        // strictly below the forest LCA, and every one of those steps owns a successful-union tag.
        let leftLast = leftPath.length - 1;
        let rightLast = rightPath.length - 1;
        while (leftLast >= 0 && rightLast >= 0 && leftPath[leftLast]?.vertex === rightPath[rightLast]?.vertex) {
            leftLast--;
            rightLast--;
        }

        let latest: AncestralConnectionVersion | undefined;
        for (let index = 0; index <= leftLast; index++) latest = later(latest, leftPath[index]?.joinedAt);
        for (let index = 0; index <= rightLast; index++) latest = later(latest, rightPath[index]?.joinedAt);

        // Distinct connected vertices have at least one edge strictly below their LCA.
        if (latest === undefined) throw new Error("A connected pair has no union-edge witness.");
        return latest;
    }

    /**
     * Whether two versions retain the exact same sparse connectivity index, so neither can observe a
     * union recorded in the other. A representation test, not an equality test. O(1).
     */
    public sharesConnectivityRootWith(other: PersistentAncestralConnectionForest): boolean {
        return this.#cells.sharesRootWith(other.#cells);
    }

    /**
     * Walk the whole representation and check its invariants, throwing `Error` on the first
     * violation, then return its shape measurements. A defensive audit, not part of normal use.
     *
     * Checks the cached counts, the history-token parent chain and root, sparse-cell canonicality,
     * union-tag ancestry and order, the union-by-size height ceiling of `floor(log2 n)`, the cached
     * component count, and the cached root sizes. O(H + m w log n), where H is the history depth and
     * m is the explicit-cell count.
     */
    public validateStructure(): AncestralConnectionForestStatistics {
        if (this.#vertexCount < 0 || this.#componentCount < 0 || this.#componentCount > this.#vertexCount) {
            throw new Error("Connection-forest counts are invalid.");
        }

        const historyRoot = this.#version.root;
        const ancestors = new Set<AncestralConnectionVersion>();
        let expectedDepth = this.#version.depth;
        let version: AncestralConnectionVersion | undefined = this.#version;
        while (version !== undefined) {
            if (version.depth !== expectedDepth || version.root !== historyRoot) {
                throw new Error("The history-token parent chain is inconsistent.");
            }
            expectedDepth--;
            ancestors.add(version);
            version = version.parent;
        }
        if (expectedDepth !== -1 || !ancestors.has(historyRoot)
            || historyRoot.parent !== undefined || historyRoot.depth !== 0) {
            throw new Error("The history root is inconsistent.");
        }

        for (const entry of this.#cells) {
            const cell = entry.value;
            if (!this.#isVertex(entry.key) || !this.#isVertex(cell.parent)) {
                throw new Error("A sparse parent cell addresses a vertex outside the universe.");
            }
            if (cell.parent === entry.key) {
                if (cell.size <= 1 || cell.joinedAt !== undefined) {
                    throw new Error("A stored root cell is not a canonical non-singleton root.");
                }
            } else if (cell.size !== 0 || cell.joinedAt === undefined || !ancestors.has(cell.joinedAt)
                || !this.#cells.containsKey(cell.parent)) {
                throw new Error("A non-root cell has an invalid size or union-version tag.");
            }
        }

        const componentSizes = new Map<number, number>();
        let maximumParentPathLength = 0;
        const maximumHeight = this.#vertexCount <= 1 ? 0 : 31 - Math.clz32(this.#vertexCount);
        for (const entry of this.#cells) {
            let current = entry.key;
            let height = 0;
            let previousDepth = -1;
            for (;;) {
                const cell = this.#cell(current);
                if (cell.parent === current) break;
                const joinedAt = cell.joinedAt;
                if (joinedAt === undefined || joinedAt.depth <= previousDepth || !ancestors.has(joinedAt)) {
                    throw new Error("Union-edge versions do not strictly increase toward the root.");
                }
                previousDepth = joinedAt.depth;
                current = cell.parent;
                if (++height > maximumHeight) {
                    throw new Error("A parent path exceeds the union-by-size height bound.");
                }
            }
            maximumParentPathLength = Math.max(maximumParentPathLength, height);
            componentSizes.set(current, (componentSizes.get(current) ?? 0) + 1);
        }

        const storedCellCount = this.#cells.size;
        const absentSingletons = this.#vertexCount - storedCellCount;
        if (absentSingletons < 0 || absentSingletons + componentSizes.size !== this.#componentCount) {
            throw new Error("The cached component count disagrees with the parent forest.");
        }
        for (const [root, size] of componentSizes) {
            if (this.#sizeOf(root) !== size) throw new Error("A root's cached union size is incorrect.");
        }

        return {
            vertexCount: this.#vertexCount,
            componentCount: this.#componentCount,
            storedCellCount,
            maximumParentPathLength,
            historyDepth: this.#version.depth,
        };
    }

    #findRoot(vertex: number): number {
        let current = vertex;
        for (;;) {
            const parent = this.#parentOf(current);
            if (parent === current) return current;
            current = parent;
        }
    }

    #buildPath(vertex: number): PathStep[] {
        const path: PathStep[] = [];
        let current = vertex;
        for (;;) {
            const cell = this.#cells.get(current);
            const parent = cell === undefined ? current : cell.parent;
            path.push({ vertex: current, joinedAt: cell?.joinedAt });
            if (parent === current) return path;
            current = parent;
        }
    }

    /** The cell of a vertex; an absent cell is canonically its own singleton root. */
    #cell(vertex: number): Cell { return this.#cells.get(vertex) ?? rootCell(vertex, 1); }

    #parentOf(vertex: number): number { return this.#cells.get(vertex)?.parent ?? vertex; }

    #sizeOf(vertex: number): number { return this.#cells.get(vertex)?.size ?? 1; }

    #isVertex(vertex: number): boolean {
        return Number.isInteger(vertex) && vertex >= 0 && vertex < this.#vertexCount;
    }

    #checkVertex(vertex: number): void {
        if (!this.#isVertex(vertex)) {
            throw new RangeError(
                `Vertex ${vertex} must lie within the forest's fixed universe of ${this.#vertexCount} vertices.`);
        }
    }

    #checkVertices(left: number, right: number): void {
        this.#checkVertex(left);
        this.#checkVertex(right);
    }
}
