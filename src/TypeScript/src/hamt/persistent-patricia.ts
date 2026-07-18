import { sameValueZero } from "./hash-policy.js";

type PatriciaNode<K, V> = PatriciaLeaf<K, V> | PatriciaBranch<K, V>;

interface PatriciaLeaf<K, V> {
    readonly kind: "leaf";
    readonly path: bigint;
    readonly key: K;
    readonly value: V;
    readonly count: 1;
}

interface PatriciaBranch<K, V> {
    readonly kind: "branch";
    readonly prefix: bigint;
    readonly mask: bigint;
    readonly left: PatriciaNode<K, V>;
    readonly right: PatriciaNode<K, V>;
    readonly count: number;
}

interface PatriciaChange<K, V> {
    readonly node: PatriciaNode<K, V> | undefined;
    readonly changed: boolean;
    readonly added: boolean;
}

function makeLeaf<K, V>(path: bigint, key: K, value: V): PatriciaLeaf<K, V> {
    return { kind: "leaf", path, key, value, count: 1 };
}

function makeBranch<K, V>(
    prefix: bigint,
    mask: bigint,
    left: PatriciaNode<K, V>,
    right: PatriciaNode<K, V>,
): PatriciaBranch<K, V> {
    const count = left.count + right.count;
    if (!Number.isSafeInteger(count)) throw new RangeError("Patricia map cardinality overflow.");
    return { kind: "branch", prefix, mask, left, right, count };
}

function prefixOf(path: bigint, mask: bigint): bigint {
    return path & ~((mask << 1n) - 1n);
}

function highestBit(value: bigint): bigint {
    if (value <= 0n) throw new RangeError("A nonzero path difference is required.");
    return 1n << BigInt(value.toString(2).length - 1);
}

function join<K, V>(
    leftPath: bigint,
    left: PatriciaNode<K, V>,
    rightPath: bigint,
    right: PatriciaNode<K, V>,
): PatriciaNode<K, V> {
    const mask = highestBit(leftPath ^ rightPath);
    const prefix = prefixOf(leftPath, mask);
    return (leftPath & mask) === 0n
        ? makeBranch(prefix, mask, left, right)
        : makeBranch(prefix, mask, right, left);
}

function findLeaf<K, V>(node: PatriciaNode<K, V>, path: bigint): PatriciaLeaf<K, V> | undefined {
    let current = node;
    while (current.kind === "branch") {
        if (prefixOf(path, current.mask) !== current.prefix) return undefined;
        current = (path & current.mask) === 0n ? current.left : current.right;
    }
    return current.path === path ? current : undefined;
}

function putNode<K, V>(
    node: PatriciaNode<K, V> | undefined,
    path: bigint,
    key: K,
    value: V,
): PatriciaChange<K, V> {
    if (node === undefined) return { node: makeLeaf(path, key, value), changed: true, added: true };
    if (node.kind === "leaf") {
        if (node.path === path) {
            if (sameValueZero(node.value, value)) return { node, changed: false, added: false };
            return { node: makeLeaf(path, node.key, value), changed: true, added: false };
        }
        return { node: join(node.path, node, path, makeLeaf(path, key, value)), changed: true, added: true };
    }
    if (prefixOf(path, node.mask) !== node.prefix) {
        return { node: join(node.prefix, node, path, makeLeaf(path, key, value)), changed: true, added: true };
    }
    if ((path & node.mask) === 0n) {
        const child = putNode(node.left, path, key, value);
        return child.changed
            ? { node: makeBranch(node.prefix, node.mask, child.node!, node.right), changed: true, added: child.added }
            : { node, changed: false, added: false };
    }
    const child = putNode(node.right, path, key, value);
    return child.changed
        ? { node: makeBranch(node.prefix, node.mask, node.left, child.node!), changed: true, added: child.added }
        : { node, changed: false, added: false };
}

function removeNode<K, V>(node: PatriciaNode<K, V> | undefined, path: bigint): PatriciaChange<K, V> {
    if (node === undefined) return { node: undefined, changed: false, added: false };
    if (node.kind === "leaf") {
        return node.path === path
            ? { node: undefined, changed: true, added: false }
            : { node, changed: false, added: false };
    }
    if (prefixOf(path, node.mask) !== node.prefix) return { node, changed: false, added: false };
    if ((path & node.mask) === 0n) {
        const child = removeNode(node.left, path);
        if (!child.changed) return { node, changed: false, added: false };
        return {
            node: child.node === undefined ? node.right : makeBranch(node.prefix, node.mask, child.node, node.right),
            changed: true,
            added: false,
        };
    }
    const child = removeNode(node.right, path);
    if (!child.changed) return { node, changed: false, added: false };
    return {
        node: child.node === undefined ? node.left : makeBranch(node.prefix, node.mask, node.left, child.node),
        changed: true,
        added: false,
    };
}

function* iterate<K, V>(root: PatriciaNode<K, V>): Generator<readonly [K, V], void> {
    const stack: PatriciaNode<K, V>[] = [root];
    while (stack.length !== 0) {
        const node = stack.pop();
        if (node === undefined) continue;
        if (node.kind === "leaf") yield [node.key, node.value];
        else {
            stack.push(node.right);
            stack.push(node.left);
        }
    }
}

class PatriciaCore<K, V> implements Iterable<readonly [K, V]> {
    readonly #root: PatriciaNode<K, V> | undefined;
    readonly #encode: (key: K) => bigint;
    public readonly size: number;

    public constructor(root: PatriciaNode<K, V> | undefined, size: number, encode: (key: K) => bigint) {
        this.#root = root;
        this.size = size;
        this.#encode = encode;
    }

    public get(key: K): V | undefined {
        return this.#root === undefined ? undefined : findLeaf(this.#root, this.#encode(key))?.value;
    }

    public containsKey(key: K): boolean {
        return this.#root !== undefined && findLeaf(this.#root, this.#encode(key)) !== undefined;
    }

    public entryAt(index: number): readonly [K, V] | undefined {
        if (!Number.isSafeInteger(index) || index < 0 || index >= this.size) return undefined;
        let current = this.#root!;
        let remaining = index;
        while (current.kind === "branch") {
            if (remaining < current.left.count) current = current.left;
            else {
                remaining -= current.left.count;
                current = current.right;
            }
        }
        return [current.key, current.value];
    }

    public lowerBoundRank(key: K): readonly [rank: number, found: boolean] {
        const path = this.#encode(key);
        let current = this.#root;
        let rank = 0;
        while (current?.kind === "branch") {
            if (prefixOf(path, current.mask) !== current.prefix) {
                return path < current.prefix ? [rank, false] : [rank + current.count, false];
            }
            if ((path & current.mask) === 0n) current = current.left;
            else {
                rank += current.left.count;
                current = current.right;
            }
        }
        if (current === undefined) return [0, false];
        return current.path < path ? [rank + 1, false] : [rank, current.path === path];
    }

    public put(key: K, value: V): PatriciaCore<K, V> {
        const change = putNode(this.#root, this.#encode(key), key, value);
        return change.changed ? new PatriciaCore(change.node, this.size + (change.added ? 1 : 0), this.#encode) : this;
    }

    public remove(key: K): PatriciaCore<K, V> {
        const change = removeNode(this.#root, this.#encode(key));
        return change.changed ? new PatriciaCore(change.node, this.size - 1, this.#encode) : this;
    }

    public union(other: PatriciaCore<K, V>, combine?: (key: K, left: V, right: V) => V): PatriciaCore<K, V> {
        let result: PatriciaCore<K, V> = this;
        for (const [key, right] of other) {
            const found = result.#root === undefined ? undefined : findLeaf(result.#root, result.#encode(key));
            result = result.put(key, found === undefined || combine === undefined ? right : combine(found.key, found.value, right));
        }
        return result;
    }

    public intersect(other: PatriciaCore<K, V>, combine?: (key: K, left: V, right: V) => V): PatriciaCore<K, V> {
        let result = new PatriciaCore<K, V>(undefined, 0, this.#encode);
        for (const [key, left] of this) {
            const right = other.#root === undefined ? undefined : findLeaf(other.#root, other.#encode(key));
            if (right !== undefined) result = result.put(key, combine === undefined ? left : combine(key, left, right.value));
        }
        return result.contentEquals(this) ? this : result;
    }

    public except<W>(other: PatriciaCore<K, W>): PatriciaCore<K, V> {
        let result: PatriciaCore<K, V> = this;
        for (const [key] of other) result = result.remove(key);
        return result;
    }

    public contentEquals(other: PatriciaCore<K, V>): boolean {
        if (this === other) return true;
        if (this.size !== other.size) return false;
        for (const [key, value] of this) {
            const found = other.#root === undefined ? undefined : findLeaf(other.#root, other.#encode(key));
            if (found === undefined || !sameValueZero(value, found.value)) return false;
        }
        return true;
    }

    public [Symbol.iterator](): IterableIterator<readonly [K, V]> {
        return this.#root === undefined ? [][Symbol.iterator]() : iterate(this.#root);
    }
}

function encodeInt32(key: number): bigint {
    if (!Number.isInteger(key) || key < -0x8000_0000 || key > 0x7fff_ffff) {
        throw new RangeError("PersistentIntMap keys must be signed 32-bit integers.");
    }
    return BigInt((key ^ -0x8000_0000) >>> 0);
}

const sign64 = 1n << 63n;
const minInt64 = -sign64;
const maxInt64 = sign64 - 1n;

function encodeInt64(key: bigint): bigint {
    if (key < minInt64 || key > maxInt64) throw new RangeError("PersistentLongMap keys must be signed 64-bit integers.");
    return BigInt.asUintN(64, key ^ minInt64);
}

/** Persistent big-endian Patricia map over signed 32-bit number keys. */
export class PersistentIntMap<V> implements Iterable<readonly [number, V]> {
    readonly #core: PatriciaCore<number, V>;
    private constructor(core: PatriciaCore<number, V>) { this.#core = core; }
    public static empty<V>(): PersistentIntMap<V> { return new PersistentIntMap(new PatriciaCore(undefined, 0, encodeInt32)); }
    public static from<V>(items: Iterable<readonly [number, V]>): PersistentIntMap<V> {
        let result = PersistentIntMap.empty<V>();
        for (const [key, value] of items) result = result.put(key, value);
        return result;
    }
    public get size(): number { return this.#core.size; }
    public get isEmpty(): boolean { return this.size === 0; }
    public get(key: number): V | undefined { return this.#core.get(key); }
    public containsKey(key: number): boolean { return this.#core.containsKey(key); }
    /** Creates an immutable ordered cursor at a rank gap. */
    public cursor(position = 0): PersistentIntMapCursor<V> {
        validateCursorPosition(position, this.size);
        return new PersistentIntMapCursor(this, position);
    }
    /** Creates an immutable ordered cursor after the final entry. */
    public cursorAtEnd(): PersistentIntMapCursor<V> { return new PersistentIntMapCursor(this, this.size); }
    /** Creates a cursor before the first key not less than `key`. */
    public lowerBoundCursor(key: number): PersistentIntMapCursor<V> {
        return new PersistentIntMapCursor(this, this.#core.lowerBoundRank(key)[0]);
    }
    /** Creates a cursor before the first key greater than `key`. */
    public upperBoundCursor(key: number): PersistentIntMapCursor<V> {
        const [rank, found] = this.#core.lowerBoundRank(key);
        return new PersistentIntMapCursor(this, found ? rank + 1 : rank);
    }
    /** Creates a usable lower-bound cursor and reports whether its next entry has the exact key. */
    public cursorAtKey(key: number): { readonly cursor: PersistentIntMapCursor<V>; readonly found: boolean } {
        const [rank, found] = this.#core.lowerBoundRank(key);
        return { cursor: new PersistentIntMapCursor(this, rank), found };
    }
    public put(key: number, value: V): PersistentIntMap<V> { return this.withCore(this.#core.put(key, value)); }
    public remove(key: number): PersistentIntMap<V> { return this.withCore(this.#core.remove(key)); }
    public clear(): PersistentIntMap<V> { return this.isEmpty ? this : PersistentIntMap.empty<V>(); }
    public union(other: PersistentIntMap<V>, combine?: (key: number, left: V, right: V) => V): PersistentIntMap<V> {
        return this.withCore(this.#core.union(other.#core, combine));
    }
    public intersect(other: PersistentIntMap<V>, combine?: (key: number, left: V, right: V) => V): PersistentIntMap<V> {
        return this.withCore(this.#core.intersect(other.#core, combine));
    }
    public except<W>(other: PersistentIntMap<W>): PersistentIntMap<V> { return this.withCore(this.#core.except(other.#core)); }
    public [Symbol.iterator](): IterableIterator<readonly [number, V]> { return this.#core[Symbol.iterator](); }
    /** @internal Cursor rank selection. */
    public entryAtForCursor(index: number): readonly [number, V] | undefined { return this.#core.entryAt(index); }
    /** @internal Cursor lower-bound rank. */
    public lowerBoundRankForCursor(key: number): readonly [rank: number, found: boolean] { return this.#core.lowerBoundRank(key); }
    private withCore(core: PatriciaCore<number, V>): PersistentIntMap<V> { return core === this.#core ? this : new PersistentIntMap(core); }
}

/** Persistent big-endian Patricia map over signed 64-bit bigint keys. */
export class PersistentLongMap<V> implements Iterable<readonly [bigint, V]> {
    readonly #core: PatriciaCore<bigint, V>;
    private constructor(core: PatriciaCore<bigint, V>) { this.#core = core; }
    public static empty<V>(): PersistentLongMap<V> { return new PersistentLongMap(new PatriciaCore(undefined, 0, encodeInt64)); }
    public static from<V>(items: Iterable<readonly [bigint, V]>): PersistentLongMap<V> {
        let result = PersistentLongMap.empty<V>();
        for (const [key, value] of items) result = result.put(key, value);
        return result;
    }
    public get size(): number { return this.#core.size; }
    public get isEmpty(): boolean { return this.size === 0; }
    public get(key: bigint): V | undefined { return this.#core.get(key); }
    public containsKey(key: bigint): boolean { return this.#core.containsKey(key); }
    /** Creates an immutable ordered cursor at a rank gap. */
    public cursor(position = 0): PersistentLongMapCursor<V> {
        validateCursorPosition(position, this.size);
        return new PersistentLongMapCursor(this, position);
    }
    /** Creates an immutable ordered cursor after the final entry. */
    public cursorAtEnd(): PersistentLongMapCursor<V> { return new PersistentLongMapCursor(this, this.size); }
    /** Creates a cursor before the first key not less than `key`. */
    public lowerBoundCursor(key: bigint): PersistentLongMapCursor<V> {
        return new PersistentLongMapCursor(this, this.#core.lowerBoundRank(key)[0]);
    }
    /** Creates a cursor before the first key greater than `key`. */
    public upperBoundCursor(key: bigint): PersistentLongMapCursor<V> {
        const [rank, found] = this.#core.lowerBoundRank(key);
        return new PersistentLongMapCursor(this, found ? rank + 1 : rank);
    }
    /** Creates a usable lower-bound cursor and reports whether its next entry has the exact key. */
    public cursorAtKey(key: bigint): { readonly cursor: PersistentLongMapCursor<V>; readonly found: boolean } {
        const [rank, found] = this.#core.lowerBoundRank(key);
        return { cursor: new PersistentLongMapCursor(this, rank), found };
    }
    public put(key: bigint, value: V): PersistentLongMap<V> { return this.withCore(this.#core.put(key, value)); }
    public remove(key: bigint): PersistentLongMap<V> { return this.withCore(this.#core.remove(key)); }
    public clear(): PersistentLongMap<V> { return this.isEmpty ? this : PersistentLongMap.empty<V>(); }
    public union(other: PersistentLongMap<V>, combine?: (key: bigint, left: V, right: V) => V): PersistentLongMap<V> {
        return this.withCore(this.#core.union(other.#core, combine));
    }
    public intersect(other: PersistentLongMap<V>, combine?: (key: bigint, left: V, right: V) => V): PersistentLongMap<V> {
        return this.withCore(this.#core.intersect(other.#core, combine));
    }
    public except<W>(other: PersistentLongMap<W>): PersistentLongMap<V> { return this.withCore(this.#core.except(other.#core)); }
    public [Symbol.iterator](): IterableIterator<readonly [bigint, V]> { return this.#core[Symbol.iterator](); }
    /** @internal Cursor rank selection. */
    public entryAtForCursor(index: number): readonly [bigint, V] | undefined { return this.#core.entryAt(index); }
    /** @internal Cursor lower-bound rank. */
    public lowerBoundRankForCursor(key: bigint): readonly [rank: number, found: boolean] { return this.#core.lowerBoundRank(key); }
    private withCore(core: PatriciaCore<bigint, V>): PersistentLongMap<V> { return core === this.#core ? this : new PersistentLongMap(core); }
}

/** Immutable persistent gap cursor over a `PersistentIntMap` in ascending signed-key order. */
export class PersistentIntMapCursor<V> {
    readonly #source: PersistentIntMap<V>;
    readonly #position: number;

    /** Creates a root-plus-rank checkpoint; prefer the map's cursor factories. */
    public constructor(source: PersistentIntMap<V>, position: number) {
        validateCursorPosition(position, source.size);
        this.#source = source;
        this.#position = position;
    }

    /** Number of entries in this cursor version. */
    public get size(): number { return this.#source.size; }
    /** Number of entries before the gap. */
    public get position(): number { return this.#position; }
    /** Whether the gap precedes every entry. */
    public get isAtStart(): boolean { return this.#position === 0; }
    /** Whether the gap follows every entry. */
    public get isAtEnd(): boolean { return this.#position === this.size; }
    /** Entry immediately before the gap, or `undefined` at the start. */
    public peekPrevious(): readonly [number, V] | undefined {
        return this.#position === 0 ? undefined : this.#source.entryAtForCursor(this.#position - 1);
    }
    /** Entry immediately after the gap, or `undefined` at the end. The tuple distinguishes a stored undefined value. */
    public peekNext(): readonly [number, V] | undefined { return this.#source.entryAtForCursor(this.#position); }
    /** Returns a cursor one entry toward the start. */
    public movePrevious(): PersistentIntMapCursor<V> {
        if (this.isAtStart) throw new RangeError("The cursor is already at the start.");
        return new PersistentIntMapCursor(this.#source, this.#position - 1);
    }
    /** Returns a cursor one entry toward the end. */
    public moveNext(): PersistentIntMapCursor<V> {
        if (this.isAtEnd) throw new RangeError("The cursor is already at the end.");
        return new PersistentIntMapCursor(this.#source, this.#position + 1);
    }
    /** Returns a cursor at a rank gap in `0 .. size`. */
    public seek(position: number): PersistentIntMapCursor<V> {
        validateCursorPosition(position, this.size);
        return position === this.#position ? this : new PersistentIntMapCursor(this.#source, position);
    }
    /** Strictly inserts a missing key at the current lower-bound gap and returns the gap after it. */
    public insert(key: number, value: V): PersistentIntMapCursor<V> {
        const [rank, found] = this.#source.lowerBoundRankForCursor(key);
        if (found) throw new Error(`The key '${key}' is already present.`);
        this.ensureCurrentGap(rank, key);
        return new PersistentIntMapCursor(this.#source.put(key, value), this.#position + 1);
    }
    /** Updates an exact next entry or inserts at a missing lower-bound gap. */
    public put(key: number, value: V): PersistentIntMapCursor<V> {
        const [rank, found] = this.#source.lowerBoundRankForCursor(key);
        this.ensureCurrentGap(rank, key);
        const edited = this.#source.put(key, value);
        if (edited === this.#source) return this;
        return new PersistentIntMapCursor(edited, found ? this.#position : this.#position + 1);
    }
    /** Replaces the next entry's value while retaining its key and the current gap. */
    public setNextValue(value: V): PersistentIntMapCursor<V> {
        const next = this.peekNext();
        if (next === undefined) throw new RangeError("No entry follows the cursor gap.");
        const edited = this.#source.put(next[0], value);
        return edited === this.#source ? this : new PersistentIntMapCursor(edited, this.#position);
    }
    /** Deletes the entry before the gap and moves the gap left. */
    public deletePrevious(): PersistentIntMapCursor<V> {
        const previous = this.peekPrevious();
        if (previous === undefined) throw new RangeError("No entry precedes the cursor gap.");
        return new PersistentIntMapCursor(this.#source.remove(previous[0]), this.#position - 1);
    }
    /** Deletes the entry after the gap and keeps the gap fixed. */
    public deleteNext(): PersistentIntMapCursor<V> {
        const next = this.peekNext();
        if (next === undefined) throw new RangeError("No entry follows the cursor gap.");
        return new PersistentIntMapCursor(this.#source.remove(next[0]), this.#position);
    }
    /** Returns this cursor version's canonical immutable map. */
    public snapshot(): PersistentIntMap<V> { return this.#source; }

    private ensureCurrentGap(rank: number, key: number): void {
        if (rank !== this.#position) {
            throw new RangeError(`Key '${key}' belongs at gap ${rank}, not at the current gap ${this.#position}.`);
        }
    }
}

/** Immutable persistent gap cursor over a `PersistentLongMap` in ascending signed-key order. */
export class PersistentLongMapCursor<V> {
    readonly #source: PersistentLongMap<V>;
    readonly #position: number;

    /** Creates a root-plus-rank checkpoint; prefer the map's cursor factories. */
    public constructor(source: PersistentLongMap<V>, position: number) {
        validateCursorPosition(position, source.size);
        this.#source = source;
        this.#position = position;
    }

    public get size(): number { return this.#source.size; }
    public get position(): number { return this.#position; }
    public get isAtStart(): boolean { return this.#position === 0; }
    public get isAtEnd(): boolean { return this.#position === this.size; }
    public peekPrevious(): readonly [bigint, V] | undefined {
        return this.#position === 0 ? undefined : this.#source.entryAtForCursor(this.#position - 1);
    }
    public peekNext(): readonly [bigint, V] | undefined { return this.#source.entryAtForCursor(this.#position); }
    public movePrevious(): PersistentLongMapCursor<V> {
        if (this.isAtStart) throw new RangeError("The cursor is already at the start.");
        return new PersistentLongMapCursor(this.#source, this.#position - 1);
    }
    public moveNext(): PersistentLongMapCursor<V> {
        if (this.isAtEnd) throw new RangeError("The cursor is already at the end.");
        return new PersistentLongMapCursor(this.#source, this.#position + 1);
    }
    public seek(position: number): PersistentLongMapCursor<V> {
        validateCursorPosition(position, this.size);
        return position === this.#position ? this : new PersistentLongMapCursor(this.#source, position);
    }
    public insert(key: bigint, value: V): PersistentLongMapCursor<V> {
        const [rank, found] = this.#source.lowerBoundRankForCursor(key);
        if (found) throw new Error(`The key '${key}' is already present.`);
        this.ensureCurrentGap(rank, key);
        return new PersistentLongMapCursor(this.#source.put(key, value), this.#position + 1);
    }
    public put(key: bigint, value: V): PersistentLongMapCursor<V> {
        const [rank, found] = this.#source.lowerBoundRankForCursor(key);
        this.ensureCurrentGap(rank, key);
        const edited = this.#source.put(key, value);
        if (edited === this.#source) return this;
        return new PersistentLongMapCursor(edited, found ? this.#position : this.#position + 1);
    }
    public setNextValue(value: V): PersistentLongMapCursor<V> {
        const next = this.peekNext();
        if (next === undefined) throw new RangeError("No entry follows the cursor gap.");
        const edited = this.#source.put(next[0], value);
        return edited === this.#source ? this : new PersistentLongMapCursor(edited, this.#position);
    }
    public deletePrevious(): PersistentLongMapCursor<V> {
        const previous = this.peekPrevious();
        if (previous === undefined) throw new RangeError("No entry precedes the cursor gap.");
        return new PersistentLongMapCursor(this.#source.remove(previous[0]), this.#position - 1);
    }
    public deleteNext(): PersistentLongMapCursor<V> {
        const next = this.peekNext();
        if (next === undefined) throw new RangeError("No entry follows the cursor gap.");
        return new PersistentLongMapCursor(this.#source.remove(next[0]), this.#position);
    }
    public snapshot(): PersistentLongMap<V> { return this.#source; }

    private ensureCurrentGap(rank: number, key: bigint): void {
        if (rank !== this.#position) {
            throw new RangeError(`Key '${key}' belongs at gap ${rank}, not at the current gap ${this.#position}.`);
        }
    }
}

/** Persistent Patricia set over signed 32-bit numbers. */
export class PersistentIntSet implements Iterable<number> {
    readonly #map: PersistentIntMap<true>;
    private constructor(map: PersistentIntMap<true>) { this.#map = map; }
    public static empty(): PersistentIntSet { return new PersistentIntSet(PersistentIntMap.empty<true>()); }
    public static from(items: Iterable<number>): PersistentIntSet { let result = this.empty(); for (const item of items) result = result.add(item); return result; }
    public get size(): number { return this.#map.size; }
    public get isEmpty(): boolean { return this.size === 0; }
    public contains(value: number): boolean { return this.#map.containsKey(value); }
    /** Creates an immutable ordered cursor at a population-rank gap. */
    public cursor(position = 0): PersistentIntSetCursor {
        validateCursorPosition(position, this.size);
        return new PersistentIntSetCursor(this, position);
    }
    public cursorAtEnd(): PersistentIntSetCursor { return new PersistentIntSetCursor(this, this.size); }
    public lowerBoundCursor(value: number): PersistentIntSetCursor {
        return new PersistentIntSetCursor(this, this.#map.lowerBoundRankForCursor(value)[0]);
    }
    public upperBoundCursor(value: number): PersistentIntSetCursor {
        const [rank, found] = this.#map.lowerBoundRankForCursor(value);
        return new PersistentIntSetCursor(this, found ? rank + 1 : rank);
    }
    public cursorAtItem(value: number): { readonly cursor: PersistentIntSetCursor; readonly found: boolean } {
        const [rank, found] = this.#map.lowerBoundRankForCursor(value);
        return { cursor: new PersistentIntSetCursor(this, rank), found };
    }
    public add(value: number): PersistentIntSet { return this.withMap(this.#map.put(value, true)); }
    public remove(value: number): PersistentIntSet { return this.withMap(this.#map.remove(value)); }
    public union(other: PersistentIntSet): PersistentIntSet { return this.withMap(this.#map.union(other.#map)); }
    public intersect(other: PersistentIntSet): PersistentIntSet { return this.withMap(this.#map.intersect(other.#map)); }
    public except(other: PersistentIntSet): PersistentIntSet { return this.withMap(this.#map.except(other.#map)); }
    public *[Symbol.iterator](): IterableIterator<number> { for (const [key] of this.#map) yield key; }
    /** @internal Cursor rank selection. */
    public itemAtForCursor(index: number): number | undefined { return this.#map.entryAtForCursor(index)?.[0]; }
    /** @internal Cursor lower-bound rank. */
    public lowerBoundRankForCursor(value: number): readonly [rank: number, found: boolean] {
        return this.#map.lowerBoundRankForCursor(value);
    }
    private withMap(map: PersistentIntMap<true>): PersistentIntSet { return map === this.#map ? this : new PersistentIntSet(map); }
}

/** Persistent Patricia set over signed 64-bit bigints. */
export class PersistentLongSet implements Iterable<bigint> {
    readonly #map: PersistentLongMap<true>;
    private constructor(map: PersistentLongMap<true>) { this.#map = map; }
    public static empty(): PersistentLongSet { return new PersistentLongSet(PersistentLongMap.empty<true>()); }
    public static from(items: Iterable<bigint>): PersistentLongSet { let result = this.empty(); for (const item of items) result = result.add(item); return result; }
    public get size(): number { return this.#map.size; }
    public get isEmpty(): boolean { return this.size === 0; }
    public contains(value: bigint): boolean { return this.#map.containsKey(value); }
    /** Creates an immutable ordered cursor at a population-rank gap. */
    public cursor(position = 0): PersistentLongSetCursor {
        validateCursorPosition(position, this.size);
        return new PersistentLongSetCursor(this, position);
    }
    public cursorAtEnd(): PersistentLongSetCursor { return new PersistentLongSetCursor(this, this.size); }
    public lowerBoundCursor(value: bigint): PersistentLongSetCursor {
        return new PersistentLongSetCursor(this, this.#map.lowerBoundRankForCursor(value)[0]);
    }
    public upperBoundCursor(value: bigint): PersistentLongSetCursor {
        const [rank, found] = this.#map.lowerBoundRankForCursor(value);
        return new PersistentLongSetCursor(this, found ? rank + 1 : rank);
    }
    public cursorAtItem(value: bigint): { readonly cursor: PersistentLongSetCursor; readonly found: boolean } {
        const [rank, found] = this.#map.lowerBoundRankForCursor(value);
        return { cursor: new PersistentLongSetCursor(this, rank), found };
    }
    public add(value: bigint): PersistentLongSet { return this.withMap(this.#map.put(value, true)); }
    public remove(value: bigint): PersistentLongSet { return this.withMap(this.#map.remove(value)); }
    public union(other: PersistentLongSet): PersistentLongSet { return this.withMap(this.#map.union(other.#map)); }
    public intersect(other: PersistentLongSet): PersistentLongSet { return this.withMap(this.#map.intersect(other.#map)); }
    public except(other: PersistentLongSet): PersistentLongSet { return this.withMap(this.#map.except(other.#map)); }
    public *[Symbol.iterator](): IterableIterator<bigint> { for (const [key] of this.#map) yield key; }
    /** @internal Cursor rank selection. */
    public itemAtForCursor(index: number): bigint | undefined { return this.#map.entryAtForCursor(index)?.[0]; }
    /** @internal Cursor lower-bound rank. */
    public lowerBoundRankForCursor(value: bigint): readonly [rank: number, found: boolean] {
        return this.#map.lowerBoundRankForCursor(value);
    }
    private withMap(map: PersistentLongMap<true>): PersistentLongSet { return map === this.#map ? this : new PersistentLongSet(map); }
}

/** Immutable persistent gap cursor over a `PersistentIntSet`. */
export class PersistentIntSetCursor {
    readonly #source: PersistentIntSet;
    readonly #position: number;

    public constructor(source: PersistentIntSet, position: number) {
        validateCursorPosition(position, source.size);
        this.#source = source;
        this.#position = position;
    }

    public get size(): number { return this.#source.size; }
    public get position(): number { return this.#position; }
    public get isAtStart(): boolean { return this.#position === 0; }
    public get isAtEnd(): boolean { return this.#position === this.size; }
    public peekPrevious(): number | undefined {
        return this.#position === 0 ? undefined : this.#source.itemAtForCursor(this.#position - 1);
    }
    public peekNext(): number | undefined { return this.#source.itemAtForCursor(this.#position); }
    public movePrevious(): PersistentIntSetCursor {
        if (this.isAtStart) throw new RangeError("The cursor is already at the start.");
        return new PersistentIntSetCursor(this.#source, this.#position - 1);
    }
    public moveNext(): PersistentIntSetCursor {
        if (this.isAtEnd) throw new RangeError("The cursor is already at the end.");
        return new PersistentIntSetCursor(this.#source, this.#position + 1);
    }
    public seek(position: number): PersistentIntSetCursor {
        validateCursorPosition(position, this.size);
        return position === this.#position ? this : new PersistentIntSetCursor(this.#source, position);
    }
    public add(value: number): PersistentIntSetCursor {
        const [rank, found] = this.#source.lowerBoundRankForCursor(value);
        this.ensureCurrentGap(rank, value);
        return found ? this : new PersistentIntSetCursor(this.#source.add(value), this.#position + 1);
    }
    public deletePrevious(): PersistentIntSetCursor {
        const previous = this.peekPrevious();
        if (previous === undefined) throw new RangeError("No item precedes the cursor gap.");
        return new PersistentIntSetCursor(this.#source.remove(previous), this.#position - 1);
    }
    public deleteNext(): PersistentIntSetCursor {
        const next = this.peekNext();
        if (next === undefined) throw new RangeError("No item follows the cursor gap.");
        return new PersistentIntSetCursor(this.#source.remove(next), this.#position);
    }
    public snapshot(): PersistentIntSet { return this.#source; }

    private ensureCurrentGap(rank: number, value: number): void {
        if (rank !== this.#position) {
            throw new RangeError(`Item '${value}' belongs at gap ${rank}, not at the current gap ${this.#position}.`);
        }
    }
}

/** Immutable persistent gap cursor over a `PersistentLongSet`. */
export class PersistentLongSetCursor {
    readonly #source: PersistentLongSet;
    readonly #position: number;

    public constructor(source: PersistentLongSet, position: number) {
        validateCursorPosition(position, source.size);
        this.#source = source;
        this.#position = position;
    }

    public get size(): number { return this.#source.size; }
    public get position(): number { return this.#position; }
    public get isAtStart(): boolean { return this.#position === 0; }
    public get isAtEnd(): boolean { return this.#position === this.size; }
    public peekPrevious(): bigint | undefined {
        return this.#position === 0 ? undefined : this.#source.itemAtForCursor(this.#position - 1);
    }
    public peekNext(): bigint | undefined { return this.#source.itemAtForCursor(this.#position); }
    public movePrevious(): PersistentLongSetCursor {
        if (this.isAtStart) throw new RangeError("The cursor is already at the start.");
        return new PersistentLongSetCursor(this.#source, this.#position - 1);
    }
    public moveNext(): PersistentLongSetCursor {
        if (this.isAtEnd) throw new RangeError("The cursor is already at the end.");
        return new PersistentLongSetCursor(this.#source, this.#position + 1);
    }
    public seek(position: number): PersistentLongSetCursor {
        validateCursorPosition(position, this.size);
        return position === this.#position ? this : new PersistentLongSetCursor(this.#source, position);
    }
    public add(value: bigint): PersistentLongSetCursor {
        const [rank, found] = this.#source.lowerBoundRankForCursor(value);
        this.ensureCurrentGap(rank, value);
        return found ? this : new PersistentLongSetCursor(this.#source.add(value), this.#position + 1);
    }
    public deletePrevious(): PersistentLongSetCursor {
        const previous = this.peekPrevious();
        if (previous === undefined) throw new RangeError("No item precedes the cursor gap.");
        return new PersistentLongSetCursor(this.#source.remove(previous), this.#position - 1);
    }
    public deleteNext(): PersistentLongSetCursor {
        const next = this.peekNext();
        if (next === undefined) throw new RangeError("No item follows the cursor gap.");
        return new PersistentLongSetCursor(this.#source.remove(next), this.#position);
    }
    public snapshot(): PersistentLongSet { return this.#source; }

    private ensureCurrentGap(rank: number, value: bigint): void {
        if (rank !== this.#position) {
            throw new RangeError(`Item '${value}' belongs at gap ${rank}, not at the current gap ${this.#position}.`);
        }
    }
}

function validateCursorPosition(position: number, size: number): void {
    if (!Number.isSafeInteger(position) || position < 0 || position > size) {
        throw new RangeError(`Cursor position must be a safe integer in 0 .. ${size}.`);
    }
}
