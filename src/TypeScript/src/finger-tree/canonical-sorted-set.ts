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
    public readonly comparator: Comparator<T>;
    public readonly seed: bigint | undefined;
    readonly #rankHash: (value: T) => bigint;
    readonly #rankKey: Uint8Array;

    private constructor(comparator: Comparator<T>, rankHash: (value: T) => bigint, rankKey: Uint8Array, seed: bigint | undefined) {
        this.comparator = comparator; this.#rankHash = rankHash; this.#rankKey = rankKey.slice(); this.seed = seed;
    }

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

export interface CanonicalSetLookup<T> { readonly found: boolean; readonly value: T }
export interface CanonicalSortedSetStatistics { readonly count: number; readonly height: number; readonly maximumGeometricRank: number; readonly priorityCollisionCount: number }

/** Immutable policy-canonical Cartesian search tree. */
export class CanonicalSortedSet<T> implements Iterable<T> {
    readonly #root: Node<T> | undefined;
    public readonly policy: ZipTreeRankPolicy<T>;
    private constructor(root: Node<T> | undefined, policy: ZipTreeRankPolicy<T>) { this.#root = root; this.policy = policy; }
    public static empty<T>(policy: ZipTreeRankPolicy<T>): CanonicalSortedSet<T> { return new CanonicalSortedSet(undefined, policy); }
    public static from<T>(values: Iterable<T>, policy: ZipTreeRankPolicy<T>): CanonicalSortedSet<T> { let result = CanonicalSortedSet.empty(policy); for (const value of values) result = result.add(value); return result; }
    public get size(): number { return this.#root?.count ?? 0; }
    public get count(): number { return this.size; }
    public get isEmpty(): boolean { return this.#root === undefined; }
    public get height(): number { return this.#root?.height ?? 0; }
    public get contentHash(): bigint { return this.#root?.digest() ?? 0n; }
    #find(value: T): Node<T> | undefined { let cursor = this.#root; while (cursor !== undefined) { const comparison = this.policy.comparator(value, cursor.item); if (comparison === 0) return cursor; cursor = comparison < 0 ? cursor.left : cursor.right; } return undefined; }
    public contains(value: T): boolean { return this.#find(value) !== undefined; }
    public tryGetValue(value: T): CanonicalSetLookup<T> { const found = this.#find(value); return found === undefined ? { found: false, value } : { found: true, value: found.item }; }
    public add(value: T): CanonicalSortedSet<T> {
        const rank = this.policy.rank(value);
        const existing = this.#find(value);
        if (existing !== undefined) { if (!rankEqual(existing.rank, rank)) throw new Error("The rank hash is not constant on comparator equivalence classes."); return this; }
        return new CanonicalSortedSet(insert(this.#root, new Node(value, rank, undefined, undefined), this.policy.comparator), this.policy);
    }
    public remove(value: T): CanonicalSortedSet<T> { const result = remove(this.#root, value, this.policy.comparator); return result.removed ? new CanonicalSortedSet(result.root, this.policy) : this; }
    public clear(): CanonicalSortedSet<T> { return this.isEmpty ? this : CanonicalSortedSet.empty(this.policy); }
    #compatible(other: CanonicalSortedSet<T>): void { if (this.policy !== other.policy) throw new TypeError("Canonical set algebra requires the same rank-policy object."); }
    public union(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> { this.#compatible(other); if (this.#root === other.#root) return this; let result: CanonicalSortedSet<T> = this; for (const value of other) result = result.add(value); return result; }
    public intersect(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> { this.#compatible(other); if (this.#root === other.#root) return this; let result = CanonicalSortedSet.empty<T>(this.policy); for (const value of this) if (other.contains(value)) result = result.add(value); return result.setEquals(this) ? this : result; }
    public except(other: CanonicalSortedSet<T>): CanonicalSortedSet<T> { this.#compatible(other); if (this.#root === other.#root) return this.clear(); let result: CanonicalSortedSet<T> = this; for (const value of other) result = result.remove(value); return result; }
    public setEquals(values: Iterable<T>): boolean {
        if (values instanceof CanonicalSortedSet && values.policy === this.policy) {
            if (this === values) return true;
            if (this.size !== values.size || this.contentHash !== values.contentHash) return false;
        }
        const other = CanonicalSortedSet.from(values, this.policy); if (this.size !== other.size) return false;
        const left = this[Symbol.iterator](); const right = other[Symbol.iterator]();
        while (true) { const a = left.next(); const b = right.next(); if (a.done || b.done) return a.done === b.done; if (this.policy.comparator(a.value, b.value) !== 0) return false; }
    }
    public isSubsetOf(values: Iterable<T>): boolean { const other = CanonicalSortedSet.from(values, this.policy); if (this.size > other.size) return false; for (const value of this) if (!other.contains(value)) return false; return true; }
    public isProperSubsetOf(values: Iterable<T>): boolean { const other = CanonicalSortedSet.from(values, this.policy); return this.size < other.size && this.isSubsetOf(other); }
    public isSupersetOf(values: Iterable<T>): boolean { for (const value of values) if (!this.contains(value)) return false; return true; }
    public isProperSupersetOf(values: Iterable<T>): boolean { const other = CanonicalSortedSet.from(values, this.policy); return this.size > other.size && this.isSupersetOf(other); }
    public overlaps(values: Iterable<T>): boolean { for (const value of values) if (this.contains(value)) return true; return false; }
    public sharesStorageWith(other: CanonicalSortedSet<T>): boolean {
        if (this.#root === undefined || other.#root === undefined) return false; if (this.#root === other.#root) return true;
        const nodes = new Set<Node<T>>(); const first = [this.#root];
        while (first.length !== 0) { const node = first.pop()!; nodes.add(node); if (node.left !== undefined) first.push(node.left); if (node.right !== undefined) first.push(node.right); }
        const second = [other.#root]; while (second.length !== 0) { const node = second.pop()!; if (nodes.has(node)) return true; if (node.left !== undefined) second.push(node.left); if (node.right !== undefined) second.push(node.right); }
        return false;
    }
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
