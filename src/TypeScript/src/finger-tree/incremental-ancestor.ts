/**
 * Append-only incremental level-ancestor arenas shared by the ancestry-backed collections.
 *
 * An arena is a monotone forest that grows one leaf at a time and answers "which ancestor of this
 * node sits at absolute depth d?". Handles are stable integers owned by one arena; the
 * distinguished bottom node has depth -1 and no value. A leaf may be added below any published
 * node, not only the most recent one, so old consumer versions grow into independent branches.
 */

/** The value stored in each labelled node is opaque to the arena. */
export interface IncrementalAncestorArena<T> {
    /** The distinguished unlabelled node at depth -1. */
    readonly bottom: number;
    /** The number of labelled nodes this arena has published. */
    readonly publishedNodeCount: number;
    /** Adds a labelled leaf below a published node and returns its stable handle. */
    addLeaf(parent: number, value: T): number;
    /** A node's immutable absolute depth; the bottom node has depth -1. */
    depthOf(node: number): number;
    /** A labelled node's parent, or bottom when the node has depth zero. */
    parentOf(node: number): number;
    /** The unique ancestor of a node at an absolute depth. */
    ancestorAtDepth(node: number, depth: number): number;
    /** The immutable value associated with a labelled node. */
    valueAt(node: number): T;
}

/**
 * Representation and traversal counters from an arena snapshot.
 *
 * `blockCount` and `allocatedSlotCount` describe the odd-sized block store; the hop counters
 * describe the parent and jump links that ancestor queries actually followed.
 */
export interface MyersIncrementalAncestorStatistics {
    readonly publishedNodeCount: number;
    readonly blockCount: number;
    readonly allocatedSlotCount: number;
    readonly addLeafCount: number;
    readonly ancestorQueryCount: number;
    readonly lastAncestorHopCount: number;
    readonly maximumAncestorHopCount: number;
    readonly totalAncestorHopCount: number;
}

class Node<T> {
    public constructor(
        public readonly value: T,
        public readonly parent: number,
        public readonly depth: number,
        public readonly skip: number,
        public readonly skipDistance: number,
    ) {}
}

/** Exact floor of the square root, so the block boundary is right at every perfect square. */
function integerSquareRoot(value: number): number {
    if (value <= 0) return 0;
    let root = Math.floor(Math.sqrt(value));
    while (root > 0 && root * root > value) root -= 1;
    while ((root + 1) * (root + 1) <= value) root += 1;
    return root;
}

/**
 * An incremental ancestor arena using the two-link applicative-stack scheme of Myers.
 *
 * Each immutable node keeps one parent link and one coalesced jump link, so adding a leaf performs
 * O(1) link work. Nodes are retained in blocks of sizes 1, 3, 5, ..., whose square boundaries give
 * O(1) addressing and O(sqrt(M)) unused slots after M published nodes. Block allocation is what
 * makes insertion O(1) amortized rather than worst case: a single addition at a square boundary
 * pays Theta(sqrt(M)) to allocate the next odd block, the accepted contract of the shared
 * odd-block representation (deamortizing it was considered and declined; Haskell's arena-free port
 * exceeds this profile at O(1) worst case). An ancestor query follows O(log M) parent/jump links in
 * the worst case. Total storage is O(M), and every published value stays reachable until the arena
 * itself is collected.
 *
 * The managed baseline serializes every operation under a private lock and raises on three
 * arithmetic ceilings. Neither applies here: JavaScript has no shared-memory concurrency for
 * ordinary objects, so no lock exists to document, and depth, node count, and coalesced jump
 * distance are all bounded by memory long before they approach `Number.MAX_SAFE_INTEGER`. This port
 * therefore synthesizes no artificial limit, and its counters are exact rather than saturating.
 */
export class MyersIncrementalAncestorArena<T> implements IncrementalAncestorArena<T> {
    readonly #blocks: Node<T>[][] = [];
    #count = 0;
    #allocatedSlotCount = 0;
    #addLeafCount = 0;
    #ancestorQueryCount = 0;
    #lastAncestorHopCount = 0;
    #maximumAncestorHopCount = 0;
    #totalAncestorHopCount = 0;

    /** An arena containing only its unlabelled bottom node. */
    public constructor() {
        // The bottom node carries no value. Every read of it is rejected before the field is
        // touched, which is what lets the field stay typed as T rather than infecting the public
        // signature with an optional that callers would have to unwrap.
        this.#append(new Node(undefined as T, 0, -1, 0, 0));
    }

    /** The distinguished unlabelled node at depth -1. O(1). */
    public get bottom(): number { return 0; }

    /** The number of labelled nodes published by this arena. O(1). */
    public get publishedNodeCount(): number { return this.#count - 1; }

    /**
     * Adds a labelled leaf below `parent` and returns its stable handle. O(1) amortized over the
     * block allocations; a block-boundary call pays Theta(sqrt(M)) for the new odd block.
     */
    public addLeaf(parent: number, value: T): number {
        const parentNode = this.#nodeAt(parent);
        let skip: number;
        let skipDistance: number;
        if (parent === 0 || parentNode.skip === 0) {
            skip = parent;
            skipDistance = 1;
        } else {
            const skipped = this.#blockRead(parentNode.skip);
            if (skipped.skipDistance <= parentNode.skipDistance) {
                skip = skipped.skip;
                skipDistance = 1 + parentNode.skipDistance + skipped.skipDistance;
            } else {
                skip = parent;
                skipDistance = 1;
            }
        }
        const handle = this.#append(new Node(value, parent, parentNode.depth + 1, skip, skipDistance));
        this.#addLeafCount += 1;
        return handle;
    }

    /** A node's immutable absolute depth; the bottom node has depth -1. O(1). */
    public depthOf(node: number): number { return this.#nodeAt(node).depth; }

    /** A labelled node's parent, or bottom when the node has depth zero. O(1). */
    public parentOf(node: number): number {
        if (node === 0) throw new RangeError("The bottom node has no parent.");
        return this.#nodeAt(node).parent;
    }

    /** The immutable value associated with a labelled node. O(1). */
    public valueAt(node: number): T {
        if (node === 0) throw new RangeError("The bottom node has no value.");
        return this.#nodeAt(node).value;
    }

    /**
     * The unique ancestor of `node` at absolute `depth`, following O(log M) parent/jump links in
     * the worst case after M published nodes.
     */
    public ancestorAtDepth(node: number, depth: number): number {
        let current = this.#nodeAt(node);
        if (!Number.isInteger(depth) || depth < -1 || depth > current.depth) {
            throw new RangeError("The depth is outside the node's ancestry.");
        }
        let hops = 0;
        let handle = node;
        while (current.depth > depth) {
            const remaining = current.depth - depth;
            // Take the jump only when it lands at or below the target, so the walk never
            // overshoots and never needs to back up.
            handle = current.skipDistance > 0 && current.skipDistance <= remaining ? current.skip : current.parent;
            current = this.#blockRead(handle);
            hops += 1;
        }
        this.#ancestorQueryCount += 1;
        this.#lastAncestorHopCount = hops;
        this.#totalAncestorHopCount += hops;
        if (hops > this.#maximumAncestorHopCount) this.#maximumAncestorHopCount = hops;
        return handle;
    }

    /** Deterministic representation and traversal counters. O(1). */
    public statistics(): MyersIncrementalAncestorStatistics {
        return {
            publishedNodeCount: this.#count - 1,
            blockCount: this.#blocks.length,
            allocatedSlotCount: this.#allocatedSlotCount,
            addLeafCount: this.#addLeafCount,
            ancestorQueryCount: this.#ancestorQueryCount,
            lastAncestorHopCount: this.#lastAncestorHopCount,
            maximumAncestorHopCount: this.#maximumAncestorHopCount,
            totalAncestorHopCount: this.#totalAncestorHopCount,
        };
    }

    /**
     * Audits one node's whole root path against the Myers invariants: depths decrease by exactly
     * one per parent link, the bottom node terminates the path at depth -1, and every jump link
     * lands exactly its recorded distance above its owner. O(d) for a node at depth d.
     */
    public validateAncestry(node: number): void {
        let current = this.#nodeAt(node);
        while (current.depth >= 0) {
            const parentNode = this.#blockRead(current.parent);
            if (parentNode.depth !== current.depth - 1) throw new Error("A parent link does not descend exactly one level.");
            if (current.skipDistance < 1) throw new Error("A jump link has a non-positive distance.");
            const skipped = this.#blockRead(current.skip);
            if (skipped.depth !== current.depth - current.skipDistance) throw new Error("A jump link does not match its recorded distance.");
            current = parentNode;
        }
        if (current.parent !== 0 || current.skip !== 0 || current.depth !== -1) throw new Error("The bottom node is malformed.");
    }

    #nodeAt(node: number): Node<T> {
        if (!Number.isInteger(node) || node < 0 || node >= this.#count) throw new RangeError("The node was never published by this arena.");
        return this.#blockRead(node);
    }

    /** Reads an already-validated slot from the odd-sized block store in O(1). */
    #blockRead(index: number): Node<T> {
        const block = integerSquareRoot(index);
        const slots = this.#blocks[block];
        if (slots === undefined) throw new Error("The arena's block store is malformed.");
        const found = slots[index - block * block];
        if (found === undefined) throw new Error("The arena's block store is malformed.");
        return found;
    }

    /** Publishes one node and returns its handle, allocating a block only at a square boundary. */
    #append(item: Node<T>): number {
        const index = this.#count;
        const block = integerSquareRoot(index);
        if (block === this.#blocks.length) {
            const blockLength = 2 * block + 1;
            // Seed the block so every slot holds a well-formed value; the tail slots are
            // overwritten before any handle addressing them is ever issued.
            this.#blocks.push(new Array<Node<T>>(blockLength).fill(item));
            this.#allocatedSlotCount += blockLength;
        }
        const slots = this.#blocks[block];
        if (slots === undefined) throw new Error("The arena's block store is malformed.");
        slots[index - block * block] = item;
        this.#count = index + 1;
        return index;
    }
}
