/**
 * Canonical persistent sorted set built on a zip-zip tree.
 *
 * History independent: two sets holding the same elements under the same policy have the same
 * shape, whatever sequence of edits produced them. Shape follows per-element ranks derived by keyed
 * hashing rather than insertion order, which is what makes the set structurally comparable across
 * versions and across ports.
 */
import { createHash, createHmac, randomBytes } from "node:crypto";
import { defaultHash } from "../hamt/hash-policy.js";
import { defaultComparator, type Comparator } from "./ordering.js";

const mask64 = (1n << 64n) - 1n;
interface ZipRank { readonly geometric: number; readonly secondary: bigint; readonly content: bigint }

function leadingZeroBits64(value: bigint): number { return value === 0n ? 64 : 64 - value.toString(2).length; }
function rotateLeft64(value: bigint, count: bigint): bigint { const normalized = value & mask64; return ((normalized << count) | (normalized >> (64n - count))) & mask64; }
function mix64(value: bigint): bigint {
    let mixed = value & mask64;
    mixed ^= mixed >> 30n; mixed = (mixed * 0xbf58476d1ce4e5b9n) & mask64;
    mixed ^= mixed >> 27n; mixed = (mixed * 0x94d049bb133111ebn) & mask64;
    return (mixed ^ (mixed >> 31n)) & mask64;
}

/** Comparator and keyed deterministic rank policy for canonical zip-zip sets. */
export class ZipTreeRankPolicy<T> {
    /** The retained comparator defining the ordering. */
    public readonly comparator: Comparator<T>;
    /** The seed fixing this policy's rank key, or none when the key was generated randomly. */
    public readonly seed: bigint | undefined;
    readonly #rankHash: (value: T) => bigint;
    readonly #rankKey: Uint8Array;

    private constructor(comparator: Comparator<T>, rankHash: (value: T) => bigint, rankKey: Uint8Array, seed: bigint | undefined) {
        this.comparator = comparator; this.#rankHash = rankHash; this.#rankKey = rankKey.slice(); this.seed = seed;
    }

    /**
     * A policy with a randomly generated rank key unless a seed fixes one. Supplying a comparator
     * obliges the caller to supply a matching rank hash, since the default hash only knows the
     * default ordering.
     */
    public static create<T>(options: {
        readonly comparator?: Comparator<T>;
        readonly rankHash?: (value: T) => bigint | number;
        readonly seed?: bigint | number;
    } = {}): ZipTreeRankPolicy<T> {
        if (options.comparator !== undefined && options.rankHash === undefined) throw new TypeError("An explicit comparator requires an equivalence-coherent rank hash.");
        const comparator = options.comparator ?? defaultComparator<T>;
        const rankHash = options.rankHash === undefined
            ? (value: T): bigint => BigInt(defaultHash(value) >>> 0)
            : (value: T): bigint => BigInt(options.rankHash!(value));
        const seed = options.seed === undefined ? undefined : BigInt(options.seed);
        const rankKey = seed === undefined ? randomBytes(32) : deriveSeedKey(seed);
        return new ZipTreeRankPolicy(comparator, rankHash, rankKey, seed);
    }

    /**
     * A policy from an explicit rank key. This is how two processes agree on one canonical shape:
     * the same key and comparator yield identical trees for identical contents.
     */
    public static createKeyed<T>(rankKey: Uint8Array, options: {
        readonly comparator?: Comparator<T>;
        readonly rankHash?: (value: T) => bigint | number;
    } = {}): ZipTreeRankPolicy<T> {
        if (rankKey.byteLength < 32) throw new RangeError("A zip-zip rank key must contain at least 32 bytes.");
        if (options.comparator !== undefined && options.rankHash === undefined) throw new TypeError("An explicit comparator requires an equivalence-coherent rank hash.");
        const rankHash = options.rankHash === undefined
            ? (value: T): bigint => BigInt(defaultHash(value) >>> 0)
            : (value: T): bigint => BigInt(options.rankHash!(value));
        return new ZipTreeRankPolicy(options.comparator ?? defaultComparator<T>, rankHash, rankKey, undefined);
    }

    /** Returns the deterministic three-part rank for diagnostics and cross-language vectors. */
    public rank(value: T): { readonly geometric: number; readonly secondary: bigint; readonly content: bigint } {
        const source = Buffer.alloc(8); source.writeBigInt64BE(BigInt.asIntN(64, this.#rankHash(value)));
        const digest = createHmac("sha256", this.#rankKey).update(source).digest();
        const primary = digest.readBigUInt64BE(0);
        return { geometric: leadingZeroBits64(primary), secondary: digest.readBigUInt64BE(8), content: digest.readBigUInt64BE(16) };
    }
}

function deriveSeedKey(seed: bigint): Uint8Array {
    const material = Buffer.alloc(12); material.write("ZZT2", 0, "ascii"); material.writeBigInt64BE(BigInt.asIntN(64, seed), 4);
    return createHash("sha256").update(material).digest();
}

class Node<T> {
    public readonly item: T;
    public readonly rank: ZipRank;
    public readonly left: Node<T> | undefined;
    public readonly right: Node<T> | undefined;
    public readonly count: number;
    public readonly height: number;
    #digest: bigint | undefined;
    public constructor(item: T, rank: ZipRank, left: Node<T> | undefined, right: Node<T> | undefined) {
        this.item = item; this.rank = rank; this.left = left; this.right = right;
        this.count = 1 + (left?.count ?? 0) + (right?.count ?? 0);
        this.height = 1 + Math.max(left?.height ?? 0, right?.height ?? 0);
    }
    public digest(): bigint {
        if (this.#digest !== undefined) return this.#digest;
        const pending: Array<readonly [Node<T>, boolean]> = [[this, false]];
        while (pending.length !== 0) {
            const [node, expanded] = pending.pop()!;
            if (node.#digest !== undefined) continue;
            if (!expanded) {
                pending.push([node, true]);
                if (node.right !== undefined && node.right.#digest === undefined) pending.push([node.right, false]);
                if (node.left !== undefined && node.left.#digest === undefined) pending.push([node.left, false]);
            } else {
                const left = node.left === undefined ? 0x243f6a8885a308d3n : node.left.#digest!;
                const right = node.right === undefined ? 0x13198a2e03707344n : node.right.#digest!;
                node.#digest = mix64(node.rank.content ^ rotateLeft64(left, 17n) ^ rotateLeft64(right, 43n));
            }
        }
        return this.#digest!;
    }
}

function rankEqual(left: ZipRank, right: ZipRank): boolean { return left.geometric === right.geometric && left.secondary === right.secondary && left.content === right.content; }
function higher<T>(leftItem: T, left: ZipRank, rightItem: T, right: ZipRank, comparator: Comparator<T>): boolean {
    if (left.geometric !== right.geometric) return left.geometric > right.geometric;
    if (left.secondary !== right.secondary) return left.secondary > right.secondary;
    return comparator(leftItem, rightItem) < 0;
}

function split<T>(root: Node<T> | undefined, item: T, comparator: Comparator<T>): readonly [Node<T> | undefined, Node<T> | undefined] {
    const path: Array<readonly [Node<T>, boolean]> = [];
    let cursor = root;
    while (cursor !== undefined) { const wentLeft: boolean = comparator(item, cursor.item) < 0; path.push([cursor, wentLeft]); cursor = wentLeft ? cursor.left : cursor.right; }
    let left: Node<T> | undefined; let right: Node<T> | undefined;
    while (path.length !== 0) {
        const [node, wentLeft] = path.pop()!;
        if (wentLeft) right = new Node(node.item, node.rank, right, node.right);
        else left = new Node(node.item, node.rank, node.left, left);
    }
    return [left, right];
}

function insert<T>(root: Node<T> | undefined, item: Node<T>, comparator: Comparator<T>): Node<T> {
    if (root === undefined) return item;
    const path: Array<readonly [Node<T>, boolean]> = [];
    let cursor: Node<T> | undefined = root;
    while (cursor !== undefined && !higher(item.item, item.rank, cursor.item, cursor.rank, comparator)) {
        const wentLeft: boolean = comparator(item.item, cursor.item) < 0; path.push([cursor, wentLeft]); cursor = wentLeft ? cursor.left : cursor.right;
    }
    const [left, right] = split(cursor, item.item, comparator);
    let result = new Node(item.item, item.rank, left, right);
    while (path.length !== 0) { const [node, wentLeft] = path.pop()!; result = wentLeft ? new Node(node.item, node.rank, result, node.right) : new Node(node.item, node.rank, node.left, result); }
    return result;
}

function merge<T>(initialLeft: Node<T> | undefined, initialRight: Node<T> | undefined, comparator: Comparator<T>): Node<T> | undefined {
    if (initialLeft === undefined) return initialRight;
    if (initialRight === undefined) return initialLeft;
    let left: Node<T> | undefined = initialLeft; let right: Node<T> | undefined = initialRight;
    const path: Array<readonly [Node<T>, boolean]> = [];
    while (left !== undefined && right !== undefined) {
        if (higher(left.item, left.rank, right.item, right.rank, comparator)) { path.push([left, true]); left = left.right; }
        else { path.push([right, false]); right = right.left; }
    }
    let result = left ?? right;
    while (path.length !== 0) { const [node, choseLeft] = path.pop()!; result = choseLeft ? new Node(node.item, node.rank, node.left, result) : new Node(node.item, node.rank, result, node.right); }
    return result;
}

function remove<T>(root: Node<T> | undefined, item: T, comparator: Comparator<T>): { readonly root: Node<T> | undefined; readonly removed: boolean } {
    const path: Array<readonly [Node<T>, boolean]> = [];
    let cursor = root;
    while (cursor !== undefined) { const comparison = comparator(item, cursor.item); if (comparison === 0) break; const wentLeft: boolean = comparison < 0; path.push([cursor, wentLeft]); cursor = wentLeft ? cursor.left : cursor.right; }
    if (cursor === undefined) return { root, removed: false };
    let result = merge(cursor.left, cursor.right, comparator);
    while (path.length !== 0) { const [node, wentLeft] = path.pop()!; result = wentLeft ? new Node(node.item, node.rank, result, node.right) : new Node(node.item, node.rank, node.left, result); }
    return { root: result, removed: true };
}

function* iterate<T>(root: Node<T>): Generator<T, void> {
    const pending: Node<T>[] = []; let cursor: Node<T> | undefined = root;
    while (cursor !== undefined || pending.length !== 0) {
        while (cursor !== undefined) { pending.push(cursor); cursor = cursor.left; }
        const node = pending.pop()!; yield node.item; cursor = node.right;
    }
}

/**
 * A membership result carrying the stored representative; on a miss it echoes the probe, so callers
 * always have a value to work with.
 */
export interface CanonicalSetLookup<T> { readonly found: boolean; readonly value: T }
/**
 * Shape measurements returned by a successful structural audit, including how often the geometric
 * rank tied and secondary ranks had to decide.
 */
export interface CanonicalSortedSetStatistics { readonly count: number; readonly height: number; readonly maximumGeometricRank: number; readonly priorityCollisionCount: number }
/**
 * The outcome of seeking a cursor. On a miss the cursor sits where the element would be inserted.
 */
export interface CanonicalCursorSearch<T> { readonly found: boolean; readonly cursor: CanonicalSortedSetCursor<T> }

/** Immutable policy-canonical Cartesian search tree. */
export class CanonicalSortedSet<T> implements Iterable<T> {
    readonly #root: Node<T> | undefined;
    /** The retained policy defining equivalence. */
    public readonly policy: ZipTreeRankPolicy<T>;
    private constructor(root: Node<T> | undefined, policy: ZipTreeRankPolicy<T>) { this.#root = root; this.policy = policy; }
    /** The empty set, retaining the supplied policy objects. */
    public static empty<T>(policy: ZipTreeRankPolicy<T>): CanonicalSortedSet<T> { return new CanonicalSortedSet(undefined, policy); }
    /** Build a set from the given elements. */
    public static from<T>(values: Iterable<T>, policy: ZipTreeRankPolicy<T>): CanonicalSortedSet<T> { let result = CanonicalSortedSet.empty(policy); for (const value of values) result = result.add(value); return result; }
    /** Number of elements. */
    public get size(): number { return this.#root?.count ?? 0; }
    /** Number of elements. */
    public get count(): number { return this.size; }
    /** Whether the set holds no elements. */
    public get isEmpty(): boolean { return this.#root === undefined; }
    /** Tree height, exposed for structural diagnostics. */
    public get height(): number { return this.#root?.height ?? 0; }
    /**
     * A digest identifying this set's contents. Because the shape is canonical, two sets under the
     * same policy hold the same elements exactly when their digests agree, so this compares whole
     * sets in constant time.
     */
    public get contentHash(): bigint { return this.#root?.digest() ?? 0n; }
    #find(value: T): Node<T> | undefined { let cursor = this.#root; while (cursor !== undefined) { const comparison = this.policy.comparator(value, cursor.item); if (comparison === 0) return cursor; cursor = comparison < 0 ? cursor.left : cursor.right; } return undefined; }
    /** Whether the element is present. */
    public contains(value: T): boolean { return this.#find(value) !== undefined; }
    /** The stored value representative, reported presence-safely. */
    public tryGetValue(value: T): CanonicalSetLookup<T> { const found = this.#find(value); return found === undefined ? { found: false, value } : { found: true, value: found.item }; }
    /** A set containing the given element; returns the receiver when it is already present. */
    public add(value: T): CanonicalSortedSet<T> {
        const rank = this.policy.rank(value);
        const existing = this.#find(value);
        if (existing !== undefined) { if (!rankEqual(existing.rank, rank)) throw new Error("The rank hash is not constant on comparator equivalence classes."); return this; }
        return new CanonicalSortedSet(insert(this.#root, new Node(value, rank, undefined, undefined), this.policy.comparator), this.policy);
    }
    /** A set without that element; returns the receiver when it is absent. */
    public remove(value: T): CanonicalSortedSet<T> { const result = remove(this.#root, value, this.policy.comparator); return result.removed ? new CanonicalSortedSet(result.root, this.policy) : this; }
    /** An empty set retaining the same policies; returns the receiver when already empty. */
    public clear(): CanonicalSortedSet<T> { return this.isEmpty ? this : CanonicalSortedSet.empty(this.policy); }
    #compatible(other: CanonicalSortedSet<T>): void { if (this.policy !== other.policy) throw new TypeError("Canonical set algebra requires the same rank-policy object."); }
    /** The elements of both sets. */
    public union(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> { this.#compatible(other); if (this.#root === other.#root) return this; let result: CanonicalSortedSet<T> = this; for (const value of other) result = result.add(value); return result; }
    /** The elements present in both sets. */
    public intersect(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> { this.#compatible(other); if (this.#root === other.#root) return this; let result = CanonicalSortedSet.empty<T>(this.policy); for (const value of this) if (other.contains(value)) result = result.add(value); return result.setEquals(this) ? this : result; }
    /** This set's elements that are absent from the other. */
    public except(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> { this.#compatible(other); if (this.#root === other.#root) return this.clear(); let result: CanonicalSortedSet<T> = this; for (const value of other) result = result.remove(value); return result; }
    /** Whether both sets hold the same elements. */
    public setEquals(values: Iterable<T>): boolean {
        if (values instanceof CanonicalSortedSet && values.policy === this.policy) {
            if (this === values) return true;
            if (this.size !== values.size || this.contentHash !== values.contentHash) return false;
        }
        const other = CanonicalSortedSet.from(values, this.policy); if (this.size !== other.size) return false;
        const left = this[Symbol.iterator](); const right = other[Symbol.iterator]();
        while (true) { const a = left.next(); const b = right.next(); if (a.done || b.done) return a.done === b.done; if (this.policy.comparator(a.value, b.value) !== 0) return false; }
    }
    /** Whether every element of this set also occurs in the other. */
    public isSubsetOf(values: Iterable<T>): boolean { const other = CanonicalSortedSet.from(values, this.policy); if (this.size > other.size) return false; for (const value of this) if (!other.contains(value)) return false; return true; }
    /** Whether this set is a subset of the other and the other holds an element it lacks. */
    public isProperSubsetOf(values: Iterable<T>): boolean { const other = CanonicalSortedSet.from(values, this.policy); return this.size < other.size && this.isSubsetOf(other); }
    /** Whether every element of the other occurs in this set. */
    public isSupersetOf(values: Iterable<T>): boolean { for (const value of values) if (!this.contains(value)) return false; return true; }
    /** Whether this set is a superset of the other and holds an element the other lacks. */
    public isProperSupersetOf(values: Iterable<T>): boolean { const other = CanonicalSortedSet.from(values, this.policy); return this.size > other.size && this.isSupersetOf(other); }
    /** Whether the two sets share at least one element. */
    public overlaps(values: Iterable<T>): boolean { for (const value of values) if (this.contains(value)) return true; return false; }
    /** A cursor at the given gap of the set. */
    public cursorAt(position = 0): CanonicalSortedSetCursor<T> { return new CanonicalSortedSetCursor(this, position); }
    /** A cursor before the first entry not below the probe, where it would be inserted. */
    public cursorAtLowerBound(value: T): CanonicalSortedSetCursor<T> { return this.cursorAt(this.#boundRank(value, false)); }
    /** A cursor after any entry equivalent to the probe. */
    public cursorAtUpperBound(value: T): CanonicalSortedSetCursor<T> { return this.cursorAt(this.#boundRank(value, true)); }
    /**
     * Seek to the probe and report whether it is present. On a miss the cursor sits where the probe
     * would be inserted and remains usable.
     */
    public findCursor(value: T): CanonicalCursorSearch<T> {
        const cursor = this.cursorAtLowerBound(value);
        const next = cursor.peekNext();
        return { found: next !== undefined && this.policy.comparator(next.value, value) === 0, cursor };
    }
    #boundRank(value: T, upper: boolean): number {
        let rank = 0; let node = this.#root;
        while (node !== undefined) {
            const comparison = this.policy.comparator(node.item, value);
            if (comparison < 0 || upper && comparison === 0) { rank += (node.left?.count ?? 0) + 1; node = node.right; }
            else node = node.left;
        }
        return rank;
    }
    /**
     * Whether both sets share a node by identity. A representation test used to confirm a no-op
     * avoided copying, not an equality test.
     */
    public sharesStorageWith(other: CanonicalSortedSet<T>): boolean {
        if (this.#root === undefined || other.#root === undefined) return false; if (this.#root === other.#root) return true;
        const nodes = new Set<Node<T>>(); const first = [this.#root];
        while (first.length !== 0) { const node = first.pop()!; nodes.add(node); if (node.left !== undefined) first.push(node.left); if (node.right !== undefined) first.push(node.right); }
        const second = [other.#root]; while (second.length !== 0) { const node = second.pop()!; if (nodes.has(node)) return true; if (node.left !== undefined) second.push(node.left); if (node.right !== undefined) second.push(node.right); }
        return false;
    }
    /**
     * Walk the whole set and check its invariants, throwing on the first violation. A defensive
     * audit, not part of normal use.
     */
    public validateStructure(): CanonicalSortedSetStatistics {
        if (this.#root === undefined) return { count: 0, height: 0, maximumGeometricRank: 0, priorityCollisionCount: 0 };
        const pending: Array<readonly [Node<T>, T | undefined, T | undefined, number]> = [[this.#root, undefined, undefined, 1]];
        const visited = new Set<Node<T>>(); const priorities = new Set<string>(); let count = 0; let height = 0; let maxRank = 0; let collisions = 0;
        while (pending.length !== 0) {
            const [node, lower, upper, depth] = pending.pop()!;
            if (visited.has(node)) throw new Error("Canonical set contains a cycle or shared child."); visited.add(node);
            if (lower !== undefined && this.policy.comparator(node.item, lower) <= 0 || upper !== undefined && this.policy.comparator(node.item, upper) >= 0) throw new Error("Canonical set order invariant failed.");
            if (!rankEqual(node.rank, this.policy.rank(node.item))) throw new Error("Canonical set rank is not reproducible.");
            if (node.left !== undefined && !higher(node.item, node.rank, node.left.item, node.left.rank, this.policy.comparator) || node.right !== undefined && !higher(node.item, node.rank, node.right.item, node.right.rank, this.policy.comparator)) throw new Error("Canonical set heap invariant failed.");
            if (node.count !== 1 + (node.left?.count ?? 0) + (node.right?.count ?? 0) || node.height !== 1 + Math.max(node.left?.height ?? 0, node.right?.height ?? 0)) throw new Error("Canonical set metadata invariant failed.");
            count++; height = Math.max(height, depth); maxRank = Math.max(maxRank, node.rank.geometric);
            const priority = `${node.rank.geometric}:${node.rank.secondary}`; if (priorities.has(priority)) collisions++; else priorities.add(priority);
            if (node.right !== undefined) pending.push([node.right, node.item, upper, depth + 1]);
            if (node.left !== undefined) pending.push([node.left, lower, node.item, depth + 1]);
        }
        if (count !== this.size || height !== this.height) throw new Error("Canonical set root metadata invariant failed.");
        return { count, height, maximumGeometricRank: maxRank, priorityCollisionCount: collisions };
    }
    public [Symbol.iterator](): IterableIterator<T> { return this.#root === undefined ? [][Symbol.iterator]() : iterate(this.#root); }
}

/** Immutable policy-preserving root-plus-rank cursor over a canonical sorted set. */
export class CanonicalSortedSetCursor<T> {
    public constructor(public readonly set: CanonicalSortedSet<T>, public readonly position = 0) {
        if (!Number.isInteger(position) || position < 0 || position > set.size) throw new RangeError("Cursor position is outside the canonical sorted set.");
    }
    /** Number of elements. */
    public get size(): number { return this.set.size; }
    /** Whether the gap precedes the first element. */
    public get isAtStart(): boolean { return this.position === 0; }
    /** Whether the gap follows the last element. */
    public get isAtEnd(): boolean { return this.position === this.size; }
    /** The element immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): { readonly value: T } | undefined { return this.isAtStart ? undefined : { value: Array.from(this.set)[this.position - 1]! }; }
    /** The element immediately after the gap, or `undefined` at the end. */
    public peekNext(): { readonly value: T } | undefined { return this.isAtEnd ? undefined : { value: Array.from(this.set)[this.position]! }; }
    /**
     * A cursor one position earlier. The receiver is unchanged; movement produces a new cursor over
     * the same version.
     */
    public movePrevious(): CanonicalSortedSetCursor<T> { if (this.isAtStart) throw new RangeError("Cursor is already at the start."); return new CanonicalSortedSetCursor(this.set, this.position - 1); }
    /** A cursor one position later. The receiver is unchanged. */
    public moveNext(): CanonicalSortedSetCursor<T> { if (this.isAtEnd) throw new RangeError("Cursor is already at the end."); return new CanonicalSortedSetCursor(this.set, this.position + 1); }
    /** A cursor at the given rank within the same set version. */
    public seekRank(position: number): CanonicalSortedSetCursor<T> { return position === this.position ? this : new CanonicalSortedSetCursor(this.set, position); }
    /** A set containing the given element; returns the receiver when it is already present. */
    public add(value: T): CanonicalSortedSetCursor<T> { const location = this.set.cursorAtLowerBound(value); return new CanonicalSortedSetCursor(this.set.add(value), location.position + 1); }
    /** Remove the element before the gap and return a cursor in its place. */
    public deletePrevious(): CanonicalSortedSetCursor<T> { const item = this.peekPrevious(); if (item === undefined) throw new RangeError("No item precedes the cursor."); return new CanonicalSortedSetCursor(this.set.remove(item.value), this.position - 1); }
    /** Remove the element after the gap and return a cursor in its place. */
    public deleteNext(): CanonicalSortedSetCursor<T> { const item = this.peekNext(); if (item === undefined) throw new RangeError("No item follows the cursor."); return new CanonicalSortedSetCursor(this.set.remove(item.value), this.position); }
    /** The set version this cursor is positioned in. */
    public snapshot(): CanonicalSortedSet<T> { return this.set; }
}
