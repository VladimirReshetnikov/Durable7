/**
 * Persistent relaxed radix-balanced vector.
 *
 * A strict radix vector indexes in effectively constant time but cannot split or concatenate
 * cheaply. The relaxed variant allows irregular nodes carrying a size table, which makes those
 * operations logarithmic while regular nodes keep pure radix arithmetic and store no table at all.
 */
const branchFactor = 32;
const radixBits = 5;

type RrbNode<T> = RrbLeaf<T> | RrbBranch<T>;

class RrbLeaf<T> {
    public readonly kind = "leaf";
    public readonly items: readonly T[];
    public readonly count: number;
    public readonly height = 0;
    public constructor(items: readonly T[]) { if (items.length < 1 || items.length > branchFactor) throw new Error("Invalid RRB leaf."); this.items = items; this.count = items.length; }
}

class RrbBranch<T> {
    public readonly kind = "branch";
    public readonly children: readonly RrbNode<T>[];
    public readonly height: number;
    public readonly count: number;
    public readonly cumulativeSizes: readonly number[] | undefined;
    public constructor(children: readonly RrbNode<T>[]) {
        if (children.length < 1 || children.length > branchFactor) throw new Error("Invalid RRB branch factor.");
        const childHeight = children[0]!.height;
        if (children.some((child) => child.height !== childHeight)) throw new Error("RRB siblings must have equal heights.");
        this.children = children;
        this.height = childHeight + 1;
        this.count = children.reduce((sum, child) => checkedCount(sum, child.count), 0);
        this.cumulativeSizes = hasRegularLayout(children, this.height) ? undefined : buildSizes(children);
    }
    public findChild(index: number): { readonly index: number; readonly before: number } {
        if (index < 0 || index >= this.count) throw new RangeError("RRB child index is outside the branch.");
        if (this.cumulativeSizes === undefined) {
            const capacity = 2 ** (this.height * radixBits);
            const childIndex = Math.floor(index / capacity);
            return { index: childIndex, before: childIndex * capacity };
        }
        let low = 0; let high = this.cumulativeSizes.length - 1;
        while (low < high) { const middle = (low + high) >>> 1; if (index < this.cumulativeSizes[middle]!) high = middle; else low = middle + 1; }
        return { index: low, before: low === 0 ? 0 : this.cumulativeSizes[low - 1]! };
    }
}

function checkedCount(left: number, right: number): number {
    const result = left + right;
    if (!Number.isSafeInteger(result)) throw new RangeError("An RRB vector cannot exceed Number.MAX_SAFE_INTEGER elements.");
    return result;
}

function hasRegularLayout<T>(children: readonly RrbNode<T>[], height: number): boolean {
    const capacity = 2 ** (height * radixBits);
    if (!Number.isSafeInteger(capacity)) return false;
    for (let index = 0; index < children.length - 1; index++) if (children[index]!.count !== capacity) return false;
    return children.at(-1)!.count <= capacity;
}

function buildSizes<T>(children: readonly RrbNode<T>[]): readonly number[] {
    const sizes: number[] = []; let count = 0;
    for (const child of children) { count = checkedCount(count, child.count); sizes.push(count); }
    return sizes;
}

function getNode<T>(root: RrbNode<T>, initialIndex: number): T {
    let node = root; let index = initialIndex;
    while (node.kind === "branch") { const location = node.findChild(index); index -= location.before; node = node.children[location.index]!; }
    return node.items[index]!;
}

function setNode<T>(node: RrbNode<T>, index: number, value: T): RrbNode<T> {
    if (node.kind === "leaf") {
        if (Object.is(node.items[index], value)) return node;
        const items = node.items.slice(); items[index] = value; return new RrbLeaf(items);
    }
    const location = node.findChild(index);
    const current = node.children[location.index]!;
    const child = setNode(current, index - location.before, value);
    if (child === current) return node;
    const children = node.children.slice(); children[location.index] = child; return new RrbBranch(children);
}

function partition<T>(children: readonly RrbNode<T>[]): readonly RrbNode<T>[] {
    if (children.length <= branchFactor) return [new RrbBranch(children)];
    const split = Math.floor(children.length / 2);
    return [new RrbBranch(children.slice(0, split)), new RrbBranch(children.slice(split))];
}

function concatSameHeight<T>(left: RrbNode<T>, right: RrbNode<T>): readonly RrbNode<T>[] {
    if (left.kind === "leaf") {
        if (right.kind !== "leaf") throw new Error("RRB height mismatch.");
        if (left.count === branchFactor && right.count === branchFactor) return [left, right];
        const combined = [...left.items, ...right.items];
        if (combined.length <= branchFactor) return [new RrbLeaf(combined)];
        const split = Math.floor(combined.length / 2);
        return [new RrbLeaf(combined.slice(0, split)), new RrbLeaf(combined.slice(split))];
    }
    if (right.kind !== "branch") throw new Error("RRB height mismatch.");
    const boundary = concatSameHeight(left.children.at(-1)!, right.children[0]!);
    return partition([...left.children.slice(0, -1), ...boundary, ...right.children.slice(1)]);
}

function concatNodes<T>(left: RrbNode<T>, right: RrbNode<T>): readonly RrbNode<T>[] {
    if (left.height === right.height) return concatSameHeight(left, right);
    if (left.height > right.height) {
        if (left.kind !== "branch") throw new Error("RRB height mismatch.");
        const boundary = concatNodes(left.children.at(-1)!, right);
        return partition([...left.children.slice(0, -1), ...boundary]);
    }
    if (right.kind !== "branch") throw new Error("RRB height mismatch.");
    const boundary = concatNodes(left, right.children[0]!);
    return partition([...boundary, ...right.children.slice(1)]);
}

function buildSameHeight<T>(nodes: readonly RrbNode<T>[]): RrbNode<T> | undefined { return nodes.length === 0 ? undefined : new RrbBranch(nodes); }

function splitNode<T>(node: RrbNode<T>, index: number): readonly [RrbNode<T> | undefined, RrbNode<T> | undefined] {
    if (index === 0) return [undefined, node];
    if (index === node.count) return [node, undefined];
    if (node.kind === "leaf") return [new RrbLeaf(node.items.slice(0, index)), new RrbLeaf(node.items.slice(index))];
    const location = node.findChild(index);
    const childSplit = splitNode(node.children[location.index]!, index - location.before);
    const left = [...node.children.slice(0, location.index), ...(childSplit[0] === undefined ? [] : [childSplit[0]])];
    const right = [...(childSplit[1] === undefined ? [] : [childSplit[1]]), ...node.children.slice(location.index + 1)];
    return [buildSameHeight(left), buildSameHeight(right)];
}

function buildLevel<T>(nodes: readonly RrbNode<T>[]): RrbNode<T> {
    if (nodes.length === 0) throw new Error("Cannot build an empty RRB level.");
    let level = nodes.slice();
    while (level.length > 1) {
        const parents: RrbNode<T>[] = [];
        for (let index = 0; index < level.length; index += branchFactor) parents.push(new RrbBranch(level.slice(index, index + branchFactor)));
        level = parents;
    }
    return level[0]!;
}

function normalizeRoot<T>(candidate: RrbNode<T> | undefined): RrbNode<T> | undefined {
    let node = candidate;
    while (node?.kind === "branch" && node.children.length === 1) node = node.children[0];
    return node;
}

function* iterate<T>(root: RrbNode<T>): Generator<T, void> {
    const stack: RrbNode<T>[] = [root];
    while (stack.length !== 0) {
        const node = stack.pop()!;
        if (node.kind === "leaf") yield* node.items;
        else for (let index = node.children.length - 1; index >= 0; index--) stack.push(node.children[index]!);
    }
}

/** An endpoint element together with the vector that remains after removing it. */
export interface RrbPop<T> { readonly value: T; readonly rest: RrbVector<T> }
/** The two vectors produced by a positional split; both share structure with the original. */
export interface RrbVectorSplit<T> { readonly left: RrbVector<T>; readonly right: RrbVector<T> }
/**
 * An element found next to a cursor gap, wrapped so a stored `undefined` stays distinct from
 * "nothing there".
 */
export interface RrbCursorPeek<T> { readonly value: T }
/**
 * Shape measurements returned by a successful structural audit. The regular and relaxed branch
 * counts show how much of the tree still uses pure radix addressing.
 */
export interface RrbVectorStatistics {
    /** Number of elements. */
    readonly count: number; readonly height: number; readonly leafCount: number; readonly branchCount: number;
    /** How many branches use pure radix addressing and store no size table. */
    readonly regularBranchCount: number; readonly relaxedBranchCount: number;
    /** Fewest elements in any leaf. */
    readonly minimumLeafLength: number; readonly maximumLeafLength: number;
    /** Fewest children under any branch. */
    readonly minimumBranchingFactor: number; readonly maximumBranchingFactor: number;
}

/** Immutable relaxed radix-balanced vector with 32-way branches. */
export class RrbVector<T> implements Iterable<T> {
    readonly #root: RrbNode<T> | undefined;
    /** @internal Constructs a vector from an already validated tree root. */
    public constructor(root: RrbNode<T> | undefined) { this.#root = normalizeRoot(root); }
    /** The empty vector, retaining the supplied policy objects. */
    public static empty<T>(): RrbVector<T> { return new RrbVector<T>(undefined); }
    /** Build a vector from the given elements. */
    public static from<T>(values: Iterable<T>): RrbVector<T> {
        if (values instanceof RrbVector) return values;
        return new RrbVectorBuilder<T>().appendAll(values).toImmutable();
    }
    /** An empty append builder. */
    public static builder<T>(): RrbVectorBuilder<T> { return new RrbVectorBuilder<T>(); }
    /** A cursor at the given gap of the vector. */
    public getCursor(position = 0): RrbVectorCursor<T> { return new RrbVectorCursor(this, position); }
    /** Number of elements. */
    public get size(): number { return this.#root?.count ?? 0; }
    /** Whether the vector holds no elements. */
    public get isEmpty(): boolean { return this.#root === undefined; }
    /** Tree height, exposed for structural diagnostics. */
    public get height(): number { return this.#root?.height ?? 0; }
    /** The value stored for the key, or `undefined` when absent. */
    public get(index: number): T | undefined { return !Number.isInteger(index) || index < 0 || index >= this.size ? undefined : getNode(this.#root!, index); }
    /** The first element, or `undefined` when empty. */
    public front(): T | undefined { return this.get(0); }
    /** The last element, or `undefined` when empty. */
    public back(): T | undefined { return this.get(this.size - 1); }
    /** A vector with the value added at the back. */
    public append(value: T): RrbVector<T> { return this.concat(new RrbVector(new RrbLeaf([value]))); }
    /** A vector with the value added at the front. */
    public prepend(value: T): RrbVector<T> { return new RrbVector<T>(new RrbLeaf([value])).concat(this); }
    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(index: number, value: T): RrbVector<T> | undefined {
        if (!Number.isInteger(index) || index < 0 || index >= this.size) return undefined;
        const root = setNode(this.#root!, index, value); return root === this.#root ? this : new RrbVector(root);
    }
    /**
     * This vector's elements followed by the other's, joining the two trees rather than copying
     * either.
     */
    public concat(other: RrbVector<T>): RrbVector<T> {
        if (this.#root === undefined) return other;
        if (other.#root === undefined) return this;
        checkedCount(this.size, other.size);
        const roots = concatNodes(this.#root, other.#root);
        return new RrbVector(roots.length === 1 ? roots[0] : new RrbBranch(roots));
    }
    /**
     * The elements before the index and those from it on; both halves share structure with the
     * receiver.
     */
    public splitAt(index: number): RrbVectorSplit<T> | undefined {
        if (!Number.isInteger(index) || index < 0 || index > this.size) return undefined;
        if (index === 0) return { left: RrbVector.empty(), right: this };
        if (index === this.size) return { left: this, right: RrbVector.empty() };
        const split = splitNode(this.#root!, index);
        return { left: new RrbVector(split[0]), right: new RrbVector(split[1]) };
    }
    /** A vector with the value inserted so that it ends up at the given index. */
    public insertAt(index: number, value: T): RrbVector<T> | undefined { return this.insertRange(index, [value]); }
    /** A vector with every value inserted at the given index, in order. */
    public insertRange(index: number, values: Iterable<T>): RrbVector<T> | undefined {
        const split = this.splitAt(index); if (split === undefined) return undefined;
        const middle = RrbVector.from(values); return middle.isEmpty ? this : split.left.concat(middle).concat(split.right);
    }
    /** A vector without the element at the given index. */
    public removeAt(index: number): RrbVector<T> | undefined { return this.removeRange(index, 1); }
    /** A vector without the given run of elements. */
    public removeRange(index: number, count: number): RrbVector<T> | undefined {
        if (!Number.isInteger(index) || !Number.isInteger(count) || index < 0 || count < 0 || index + count > this.size) return undefined;
        if (count === 0) return this;
        const first = this.splitAt(index)!; const second = first.right.splitAt(count)!; return first.left.concat(second.right);
    }
    /** Remove the last element, returning it with the remaining vector, or nothing when empty. */
    public tryRemoveLast(): RrbPop<T> | undefined { return this.isEmpty ? undefined : { value: this.back()!, rest: this.removeAt(this.size - 1)! }; }
    /** An empty vector retaining the same policies; returns the receiver when already empty. */
    public clear(): RrbVector<T> { return this.isEmpty ? this : RrbVector.empty(); }
    /** A builder seeded with these elements, leaving the receiver untouched. */
    public toBuilder(): RrbVectorBuilder<T> { return new RrbVectorBuilder(this); }
    /** Copy the elements into an array, in order. */
    public toArray(): T[] { return Array.from(this); }
    /**
     * Whether both vectors reference the same root. A representation test, not an equality test.
     */
    public sharesRootWith(other: RrbVector<T>): boolean { return this.#root === other.#root; }
    /**
     * Number of leaves the two vectors have in common by identity, used to show that a derived
     * version really shares structure.
     */
    public sharedLeafCount(other: RrbVector<T>): number {
        const leaves = new Set<RrbLeaf<T>>(); const stack = this.#root === undefined ? [] : [this.#root];
        while (stack.length !== 0) { const node = stack.pop()!; if (node.kind === "leaf") leaves.add(node); else stack.push(...node.children); }
        let count = 0; const probe = other.#root === undefined ? [] : [other.#root];
        while (probe.length !== 0) { const node = probe.pop()!; if (node.kind === "leaf") { if (leaves.has(node)) count++; } else probe.push(...node.children); }
        return count;
    }
    /**
     * Walk the whole vector and check its invariants, throwing on the first violation. A defensive
     * audit, not part of normal use.
     */
    public validateStructure(): RrbVectorStatistics {
        if (this.#root === undefined) return { count: 0, height: 0, leafCount: 0, branchCount: 0, regularBranchCount: 0, relaxedBranchCount: 0, minimumLeafLength: 0, maximumLeafLength: 0, minimumBranchingFactor: 0, maximumBranchingFactor: 0 };
        let leafCount = 0; let branchCount = 0; let regular = 0; let relaxed = 0;
        let minLeaf = branchFactor; let maxLeaf = 0; let minBranch = branchFactor; let maxBranch = 0;
        const visit = (node: RrbNode<T>, isRoot: boolean): readonly [number, number] => {
            if (node.kind === "leaf") { if (node.count < 1 || node.count > branchFactor) throw new Error("Invalid RRB leaf size."); leafCount++; minLeaf = Math.min(minLeaf, node.count); maxLeaf = Math.max(maxLeaf, node.count); return [node.count, 0]; }
            if (node.children.length < 1 || node.children.length > branchFactor || (isRoot && node.children.length === 1)) throw new Error("Invalid RRB branch factor.");
            branchCount++; minBranch = Math.min(minBranch, node.children.length); maxBranch = Math.max(maxBranch, node.children.length);
            if (node.cumulativeSizes === undefined) regular++; else relaxed++;
            const results = node.children.map((child) => visit(child, false));
            if (results.some((result) => result[1] !== results[0]![1])) throw new Error("RRB sibling height mismatch.");
            const count = results.reduce((sum, result) => sum + result[0], 0); const height = results[0]![1] + 1;
            if (count !== node.count || height !== node.height || (node.cumulativeSizes === undefined) !== hasRegularLayout(node.children, height)) throw new Error("Invalid RRB metadata.");
            if (node.cumulativeSizes !== undefined && node.cumulativeSizes.some((value, index) => value !== node.children.slice(0, index + 1).reduce((sum, child) => sum + child.count, 0))) throw new Error("Invalid RRB size table.");
            return [count, height];
        };
        const result = visit(this.#root, true);
        return { count: result[0], height: result[1], leafCount, branchCount, regularBranchCount: regular, relaxedBranchCount: relaxed, minimumLeafLength: minLeaf, maximumLeafLength: maxLeaf, minimumBranchingFactor: branchCount === 0 ? 0 : minBranch, maximumBranchingFactor: maxBranch };
    }
    public [Symbol.iterator](): IterableIterator<T> { return this.#root === undefined ? [][Symbol.iterator]() : iterate(this.#root); }
}

/** Immutable snapshot-plus-position gap cursor over a persistent RRB vector. */
export class RrbVectorCursor<T> {
    public constructor(public readonly vector: RrbVector<T>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > vector.size) {
            throw new RangeError("Cursor position is outside the RRB vector.");
        }
    }
    /** Number of elements. */
    public get size(): number { return this.vector.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): RrbCursorPeek<T> | undefined {
        return this.isAtStart ? undefined : { value: this.vector.get(this.position - 1)! };
    }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): RrbCursorPeek<T> | undefined {
        return this.isAtEnd ? undefined : { value: this.vector.get(this.position)! };
    }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): RrbVectorCursor<T> {
        if (this.isAtStart) throw new RangeError("Cursor is already at the start.");
        return new RrbVectorCursor(this.vector, this.position - 1);
    }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): RrbVectorCursor<T> {
        if (this.isAtEnd) throw new RangeError("Cursor is already at the end.");
        return new RrbVectorCursor(this.vector, this.position + 1);
    }
    /** A cursor at the given position within the same vector version. */
    public seek(position: number): RrbVectorCursor<T> {
        return position === this.position ? this : new RrbVectorCursor(this.vector, position);
    }
    /** Insert at the gap and return a cursor positioned after the new element. */
    public insert(value: T): RrbVectorCursor<T> {
        return new RrbVectorCursor(this.vector.insertAt(this.position, value)!, this.position + 1);
    }
    /** Insert every value at the gap, in order, returning a cursor after the last. */
    public insertRange(values: Iterable<T>): RrbVectorCursor<T> {
        return this.insertVector(RrbVector.from(values));
    }
    /**
     * Insert every element of another vector at the gap, splitting and joining once regardless of
     * length.
     */
    public insertVector(values: RrbVector<T>): RrbVectorCursor<T> {
        if (values.isEmpty) return this;
        const split = this.vector.splitAt(this.position)!;
        return new RrbVectorCursor(split.left.concat(values).concat(split.right), this.position + values.size);
    }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): RrbVectorCursor<T> {
        if (this.isAtStart) throw new RangeError("No element precedes the cursor.");
        return new RrbVectorCursor(this.vector.removeAt(this.position - 1)!, this.position - 1);
    }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): RrbVectorCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new RrbVectorCursor(this.vector.removeAt(this.position)!, this.position);
    }
    /** Replace the element after the gap, keeping the gap where it is. */
    public replaceNext(value: T): RrbVectorCursor<T> {
        if (this.isAtEnd) throw new RangeError("No element follows the cursor.");
        return new RrbVectorCursor(this.vector.setItem(this.position, value)!, this.position);
    }
    /** The vector version this cursor is positioned in. */
    public snapshot(): RrbVector<T> { return this.vector; }
}

/** Mutable append staging surface that publishes immutable RRB snapshots. */
export class RrbVectorBuilder<T> implements Iterable<T> {
    #prefix: RrbVector<T>;
    #leaves: RrbNode<T>[] = [];
    #tail: T[] = [];
    #stagedCount = 0;
    #version = 0;
    public constructor(prefix: RrbVector<T> = RrbVector.empty<T>()) { this.#prefix = prefix; }
    /** Number of elements. */
    public get size(): number { return checkedCount(this.#prefix.size, this.#stagedCount); }
    /** A vector with the value added at the back. */
    public append(value: T): this { this.#tail.push(value); this.#stagedCount++; if (this.#tail.length === branchFactor) { this.#leaves.push(new RrbLeaf(this.#tail)); this.#tail = []; } this.#version++; return this; }
    /** Append every element of the given sequence, in order. */
    public appendAll(values: Iterable<T>): this { for (const value of values) this.append(value); return this; }
    /** An empty vector retaining the same policies; returns the receiver when already empty. */
    public clear(): this { if (this.size !== 0) { this.#prefix = RrbVector.empty(); this.#leaves = []; this.#tail = []; this.#stagedCount = 0; this.#version++; } return this; }
    /** Freeze the accumulated elements into a persistent vector. */
    public toImmutable(): RrbVector<T> {
        if (this.#stagedCount === 0) return this.#prefix;
        const nodes = this.#tail.length === 0 ? this.#leaves : [...this.#leaves, new RrbLeaf(this.#tail.slice())];
        const staged = new RrbVector<T>(buildLevel(nodes));
        this.#prefix = this.#prefix.isEmpty ? staged : this.#prefix.concat(staged);
        this.#leaves = []; this.#tail = []; this.#stagedCount = 0;
        return this.#prefix;
    }
    public *[Symbol.iterator](): IterableIterator<T> { const version = this.#version; for (const value of this.toImmutable()) { if (version !== this.#version) throw new Error("RRB builder modified during iteration."); yield value; } }
}
