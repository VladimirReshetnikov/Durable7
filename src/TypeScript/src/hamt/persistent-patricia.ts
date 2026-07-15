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
    private withCore(core: PatriciaCore<bigint, V>): PersistentLongMap<V> { return core === this.#core ? this : new PersistentLongMap(core); }
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
    public add(value: number): PersistentIntSet { return this.withMap(this.#map.put(value, true)); }
    public remove(value: number): PersistentIntSet { return this.withMap(this.#map.remove(value)); }
    public union(other: PersistentIntSet): PersistentIntSet { return this.withMap(this.#map.union(other.#map)); }
    public intersect(other: PersistentIntSet): PersistentIntSet { return this.withMap(this.#map.intersect(other.#map)); }
    public except(other: PersistentIntSet): PersistentIntSet { return this.withMap(this.#map.except(other.#map)); }
    public *[Symbol.iterator](): IterableIterator<number> { for (const [key] of this.#map) yield key; }
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
    public add(value: bigint): PersistentLongSet { return this.withMap(this.#map.put(value, true)); }
    public remove(value: bigint): PersistentLongSet { return this.withMap(this.#map.remove(value)); }
    public union(other: PersistentLongSet): PersistentLongSet { return this.withMap(this.#map.union(other.#map)); }
    public intersect(other: PersistentLongSet): PersistentLongSet { return this.withMap(this.#map.intersect(other.#map)); }
    public except(other: PersistentLongSet): PersistentLongSet { return this.withMap(this.#map.except(other.#map)); }
    public *[Symbol.iterator](): IterableIterator<bigint> { for (const [key] of this.#map) yield key; }
    private withMap(map: PersistentLongMap<true>): PersistentLongSet { return map === this.#map ? this : new PersistentLongSet(map); }
}
