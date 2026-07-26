/**
 * Persistent CHAMP hash map and hash set, with bulk builders and one-way editing sessions.
 *
 * Branching is 32-way and bitmap-indexed, with immutable collision buckets, so lookup, insertion,
 * and removal are constant time in expectation while versions share every unchanged node. Key
 * equivalence comes from a retained hash policy, and a no-op returns a collection sharing the
 * receiver's root.
 */
import { getBulkBuilderCombiner } from "./bulk-builder-internals.js";
import { defaultHashPolicy, sameValueZero, type HashPolicy } from "./hash-policy.js";

const bitsPerLevel = 5;
const branchMask = 0x1f;

type Node<K, V> = Leaf<K, V> | Collision<K, V> | BitmapNode<K, V>;

type MutableNode<K, V> = MutableLeaf<K, V> | MutableCollision<K, V> | MutableBitmapNode<K, V>;

interface Leaf<K, V> {
    readonly kind: "leaf";
    readonly hash: number;
    readonly key: K;
    readonly value: V;
    readonly entryCount: 1;
}

interface Collision<K, V> {
    readonly kind: "collision";
    readonly hash: number;
    readonly entries: readonly Leaf<K, V>[];
    readonly entryCount: number;
}

interface BitmapNode<K, V> {
    readonly kind: "bitmap";
    readonly dataMap: number;
    readonly nodeMap: number;
    readonly data: readonly Leaf<K, V>[];
    readonly nodes: readonly Node<K, V>[];
    readonly entryCount: number;
}

interface MutableLeaf<K, V> {
    readonly kind: "leaf";
    readonly hash: number;
    readonly key: K;
    value: V;
}

interface MutableCollision<K, V> {
    readonly kind: "collision";
    readonly hash: number;
    readonly entries: MutableLeaf<K, V>[];
}

interface MutableBitmapNode<K, V> {
    readonly kind: "bitmap";
    dataMap: number;
    nodeMap: number;
    readonly data: MutableLeaf<K, V>[];
    readonly nodes: MutableNode<K, V>[];
}

interface MutableInsertResult<K, V> {
    readonly node: MutableNode<K, V>;
    readonly added: boolean;
}

interface InsertResult<K, V> {
    readonly node: Node<K, V>;
    readonly added: boolean;
    readonly changed: boolean;
    readonly duplicate: boolean;
}

interface FactoryUpdateNodeResult<K, V> {
    readonly node: Node<K, V>;
    readonly value: V;
    readonly added: boolean;
    readonly changed: boolean;
}

interface RemoveResult<K, V> {
    readonly node: Node<K, V> | undefined;
    readonly removed: HamtEntry<K, V> | undefined;
    readonly changed: boolean;
}

/** A concrete stored key/value representative. */
export interface HamtEntry<K, V> {
    /** The key. */
    readonly key: K;
    /** The value. */
    readonly value: V;
}

/** Result of a non-throwing insertion. */
export interface AddResult<T> {
    /** The value. */
    readonly value: T;
    /** Whether a new entry was published, as opposed to an equivalent key already being present. */
    readonly added: boolean;
}

/** Result of removing a map key. */
export interface MapRemoveResult<K, V> {
    /** The resulting collection. */
    readonly map: PersistentHashMap<K, V>;
    /** The value. */
    readonly value: V;
}

/** Result of removing a map entry, including its stored key representative. */
export interface MapRemoveEntryResult<K, V> {
    /** The resulting collection. */
    readonly map: PersistentHashMap<K, V>;
    /** The stored key representative and value that were removed. */
    readonly entry: HamtEntry<K, V>;
}

/** Result of a factory-driven persistent map update. */
export interface MapUpdateResult<K, V> {
    /** The resulting collection. */
    readonly map: PersistentHashMap<K, V>;
    /** The value. */
    readonly value: V;
}

/** Result of removing a set element, including its stored representative. */
export interface SetRemoveResult<T> {
    /** A map with the key bound to the value, adding or replacing as needed. */
    readonly set: PersistentHashSet<T>;
    /** The value. */
    readonly value: T;
}

/** Which side of a diff an entry falls on. */
export type MapDifferenceKind = "added" | "removed" | "changed";

/** Typed semantic difference between two maps. */
export interface MapDifference<K, V> {
    /** Which side the difference falls on. */
    readonly kind: MapDifferenceKind;
    /** The key. */
    readonly key: K;
    /** The value on the source side, absent when the key was added. */
    readonly before: V | undefined;
    /** The value on the target side, absent when the key was removed. */
    readonly after: V | undefined;
}

/** Thrown by duplicate-rejecting insertion. */
export class DuplicateKeyError extends Error {
    public constructor(message = "An equivalent key is already present.") {
        super(message);
        this.name = "DuplicateKeyError";
    }
}

function leaf<K, V>(hash: number, key: K, value: V): Leaf<K, V> {
    return { kind: "leaf", hash: hash | 0, key, value, entryCount: 1 };
}

function collision<K, V>(hash: number, entries: readonly Leaf<K, V>[]): Collision<K, V> {
    return { kind: "collision", hash: hash | 0, entries, entryCount: entries.length };
}

function bitmap<K, V>(
    dataMap: number,
    nodeMap: number,
    data: readonly Leaf<K, V>[],
    nodes: readonly Node<K, V>[],
): BitmapNode<K, V> {
    return {
        kind: "bitmap",
        dataMap: dataMap | 0,
        nodeMap: nodeMap | 0,
        data,
        nodes,
        entryCount: data.length + nodes.reduce((count, node) => count + node.entryCount, 0),
    };
}

function valuesEqual<T>(left: T, right: T): boolean {
    return sameValueZero(left, right);
}

function hashFragment(hash: number, shift: number): number {
    return (hash >>> shift) & branchMask;
}

function bitPosition(fragment: number): number {
    return 1 << fragment;
}

function popCount(value: number): number {
    value >>>= 0;
    value -= (value >>> 1) & 0x55555555;
    value = (value & 0x33333333) + ((value >>> 2) & 0x33333333);
    return Math.imul((value + (value >>> 4)) & 0x0f0f0f0f, 0x01010101) >>> 24;
}

function sparseIndex(bitmapValue: number, bit: number): number {
    return popCount(bitmapValue & (bit - 1));
}

function replaceAt<T>(values: readonly T[], index: number, value: T): readonly T[] {
    const result = values.slice();
    result[index] = value;
    return result;
}

function insertAt<T>(values: readonly T[], index: number, value: T): readonly T[] {
    const result = values.slice();
    result.splice(index, 0, value);
    return result;
}

function removeAt<T>(values: readonly T[], index: number): readonly T[] {
    const result = values.slice();
    result.splice(index, 1);
    return result;
}

function getInNode<K, V>(
    node: Node<K, V>,
    hash: number,
    key: K,
    shift: number,
    policy: HashPolicy<K>,
): HamtEntry<K, V> | undefined {
    if (node.kind === "leaf") {
        return node.hash === hash && policy.equivalent(node.key, key) ? { key: node.key, value: node.value } : undefined;
    }
    if (node.kind === "collision") {
        if (node.hash !== hash) return undefined;
        const entry = node.entries.find((candidate) => policy.equivalent(candidate.key, key));
        return entry === undefined ? undefined : { key: entry.key, value: entry.value };
    }
    const bit = bitPosition(hashFragment(hash, shift));
    if ((node.dataMap & bit) !== 0) {
        const entry = node.data[sparseIndex(node.dataMap, bit)];
        return entry !== undefined && entry.hash === hash && policy.equivalent(entry.key, key)
            ? { key: entry.key, value: entry.value }
            : undefined;
    }
    if ((node.nodeMap & bit) !== 0) {
        const child = node.nodes[sparseIndex(node.nodeMap, bit)];
        return child === undefined ? undefined : getInNode(child, hash, key, shift + bitsPerLevel, policy);
    }
    return undefined;
}

function collectLeaves<K, V>(node: Node<K, V>): readonly Leaf<K, V>[] {
    if (node.kind === "leaf") return [node];
    if (node.kind === "collision") return node.entries;
    return [...node.data, ...node.nodes.flatMap(collectLeaves)];
}

function mutableLeaf<K, V>(hash: number, key: K, value: V): MutableLeaf<K, V> {
    return { kind: "leaf", hash: hash | 0, key, value };
}

function mutableBitmap<K, V>(
    dataMap: number,
    nodeMap: number,
    data: MutableLeaf<K, V>[],
    nodes: MutableNode<K, V>[],
): MutableBitmapNode<K, V> {
    return { kind: "bitmap", dataMap: dataMap | 0, nodeMap: nodeMap | 0, data, nodes };
}

function collectMutableLeaves<K, V>(node: MutableNode<K, V>): MutableLeaf<K, V>[] {
    if (node.kind === "leaf") return [node];
    if (node.kind === "collision") return [...node.entries];
    return [...node.data, ...node.nodes.flatMap(collectMutableLeaves)];
}

function mergeMutableNodes<K, V>(
    left: MutableNode<K, V>,
    leftHash: number,
    right: MutableNode<K, V>,
    rightHash: number,
    shift: number,
): MutableNode<K, V> {
    if (leftHash === rightHash) {
        return {
            kind: "collision",
            hash: leftHash | 0,
            entries: [...collectMutableLeaves(left), ...collectMutableLeaves(right)],
        };
    }
    if (shift > 30) throw new Error("Distinct 32-bit hashes exhausted the CHAMP hash width.");
    const leftBit = bitPosition(hashFragment(leftHash, shift));
    const rightBit = bitPosition(hashFragment(rightHash, shift));
    if (leftBit === rightBit) {
        if (shift === 30) throw new Error("Distinct 32-bit hashes collided at the final CHAMP level.");
        return mutableBitmap(
            0,
            leftBit,
            [],
            [mergeMutableNodes(left, leftHash, right, rightHash, shift + bitsPerLevel)],
        );
    }
    const ordered = (leftBit >>> 0) < (rightBit >>> 0)
        ? [[leftBit, left], [rightBit, right]] as const
        : [[rightBit, right], [leftBit, left]] as const;
    const data: MutableLeaf<K, V>[] = [];
    const nodes: MutableNode<K, V>[] = [];
    let dataMap = 0;
    let nodeMap = 0;
    for (const [bit, node] of ordered) {
        if (node.kind === "leaf") {
            dataMap |= bit;
            data.push(node);
        } else {
            nodeMap |= bit;
            nodes.push(node);
        }
    }
    return mutableBitmap(dataMap, nodeMap, data, nodes);
}

function insertMutableNode<K, V>(
    node: MutableNode<K, V>,
    hash: number,
    key: K,
    value: V,
    shift: number,
    policy: HashPolicy<K>,
    combine: ((existing: V, incoming: V) => V) | undefined,
): MutableInsertResult<K, V> {
    if (node.kind === "leaf") {
        if (node.hash === hash && policy.equivalent(node.key, key)) {
            const selected = combine === undefined ? value : combine(node.value, value);
            if (!valuesEqual(node.value, selected)) node.value = selected;
            return { node, added: false };
        }
        return {
            node: mergeMutableNodes(node, node.hash, mutableLeaf(hash, key, value), hash, shift),
            added: true,
        };
    }
    if (node.kind === "collision") {
        if (node.hash !== hash) {
            return {
                node: mergeMutableNodes(node, node.hash, mutableLeaf(hash, key, value), hash, shift),
                added: true,
            };
        }
        const existing = node.entries.find((entry) => policy.equivalent(entry.key, key));
        if (existing === undefined) {
            node.entries.push(mutableLeaf(hash, key, value));
            return { node, added: true };
        }
        const selected = combine === undefined ? value : combine(existing.value, value);
        if (!valuesEqual(existing.value, selected)) existing.value = selected;
        return { node, added: false };
    }

    const bit = bitPosition(hashFragment(hash, shift));
    if ((node.dataMap & bit) !== 0) {
        const dataIndex = sparseIndex(node.dataMap, bit);
        const existing = node.data[dataIndex];
        if (existing === undefined) throw new Error("Corrupt mutable CHAMP data bitmap.");
        if (existing.hash === hash && policy.equivalent(existing.key, key)) {
            const selected = combine === undefined ? value : combine(existing.value, value);
            if (!valuesEqual(existing.value, selected)) existing.value = selected;
            return { node, added: false };
        }
        const child = mergeMutableNodes(
            existing,
            existing.hash,
            mutableLeaf(hash, key, value),
            hash,
            shift + bitsPerLevel,
        );
        node.data.splice(dataIndex, 1);
        node.nodes.splice(sparseIndex(node.nodeMap, bit), 0, child);
        node.dataMap = (node.dataMap & ~bit) | 0;
        node.nodeMap = (node.nodeMap | bit) | 0;
        return { node, added: true };
    }
    if ((node.nodeMap & bit) !== 0) {
        const nodeIndex = sparseIndex(node.nodeMap, bit);
        const existing = node.nodes[nodeIndex];
        if (existing === undefined) throw new Error("Corrupt mutable CHAMP node bitmap.");
        const result = insertMutableNode(
            existing,
            hash,
            key,
            value,
            shift + bitsPerLevel,
            policy,
            combine,
        );
        node.nodes[nodeIndex] = result.node;
        return { node, added: result.added };
    }
    node.data.splice(
        sparseIndex(node.dataMap, bit),
        0,
        mutableLeaf(hash, key, value),
    );
    node.dataMap = (node.dataMap | bit) | 0;
    return { node, added: true };
}

function freezeMutableNode<K, V>(node: MutableNode<K, V>): Node<K, V> {
    if (node.kind === "leaf") return leaf(node.hash, node.key, node.value);
    if (node.kind === "collision") {
        return collision(
            node.hash,
            node.entries.map((entry) => leaf(entry.hash, entry.key, entry.value)),
        );
    }
    return bitmap(
        node.dataMap,
        node.nodeMap,
        node.data.map((entry) => leaf(entry.hash, entry.key, entry.value)),
        node.nodes.map(freezeMutableNode),
    );
}

function mergeNodes<K, V>(
    left: Node<K, V>,
    leftHash: number,
    right: Node<K, V>,
    rightHash: number,
    shift: number,
): Node<K, V> {
    if (leftHash === rightHash) return collision(leftHash, [...collectLeaves(left), ...collectLeaves(right)]);
    const leftBit = bitPosition(hashFragment(leftHash, shift));
    const rightBit = bitPosition(hashFragment(rightHash, shift));
    if (leftBit === rightBit) {
        return bitmap(0, leftBit, [], [mergeNodes(left, leftHash, right, rightHash, shift + bitsPerLevel)]);
    }
    const ordered = (leftBit >>> 0) < (rightBit >>> 0)
        ? [[leftBit, left], [rightBit, right]] as const
        : [[rightBit, right], [leftBit, left]] as const;
    const data: Leaf<K, V>[] = [];
    const nodes: Node<K, V>[] = [];
    let dataMap = 0;
    let nodeMap = 0;
    for (const [bit, node] of ordered) {
        if (node.kind === "leaf") {
            dataMap |= bit;
            data.push(node);
        } else {
            nodeMap |= bit;
            nodes.push(node);
        }
    }
    return bitmap(dataMap, nodeMap, data, nodes);
}

function insertNode<K, V>(
    node: Node<K, V>,
    hash: number,
    key: K,
    value: V,
    shift: number,
    overwrite: boolean,
    policy: HashPolicy<K>,
): InsertResult<K, V> {
    if (node.kind === "leaf") {
        if (node.hash === hash && policy.equivalent(node.key, key)) {
            if (!overwrite) return { node, added: false, changed: false, duplicate: true };
            if (valuesEqual(node.value, value)) return { node, added: false, changed: false, duplicate: false };
            return { node: leaf(hash, node.key, value), added: false, changed: true, duplicate: false };
        }
        return { node: mergeNodes(node, node.hash, leaf(hash, key, value), hash, shift), added: true, changed: true, duplicate: false };
    }
    if (node.kind === "collision") {
        if (node.hash !== hash) {
            return { node: mergeNodes(node, node.hash, leaf(hash, key, value), hash, shift), added: true, changed: true, duplicate: false };
        }
        const index = node.entries.findIndex((entry) => policy.equivalent(entry.key, key));
        if (index < 0) return { node: collision(hash, [...node.entries, leaf(hash, key, value)]), added: true, changed: true, duplicate: false };
        if (!overwrite) return { node, added: false, changed: false, duplicate: true };
        const current = node.entries[index];
        if (current === undefined || valuesEqual(current.value, value)) return { node, added: false, changed: false, duplicate: false };
        return {
            node: collision(hash, replaceAt(node.entries, index, leaf(hash, current.key, value))),
            added: false,
            changed: true,
            duplicate: false,
        };
    }

    const bit = bitPosition(hashFragment(hash, shift));
    if ((node.dataMap & bit) !== 0) {
        const dataIndex = sparseIndex(node.dataMap, bit);
        const current = node.data[dataIndex];
        if (current === undefined) throw new Error("Corrupt CHAMP data bitmap.");
        if (current.hash === hash && policy.equivalent(current.key, key)) {
            if (!overwrite) return { node, added: false, changed: false, duplicate: true };
            if (valuesEqual(current.value, value)) return { node, added: false, changed: false, duplicate: false };
            return {
                node: bitmap(node.dataMap, node.nodeMap, replaceAt(node.data, dataIndex, leaf(hash, current.key, value)), node.nodes),
                added: false,
                changed: true,
                duplicate: false,
            };
        }
        const child = mergeNodes(current, current.hash, leaf(hash, key, value), hash, shift + bitsPerLevel);
        return {
            node: bitmap(
                node.dataMap & ~bit,
                node.nodeMap | bit,
                removeAt(node.data, dataIndex),
                insertAt(node.nodes, sparseIndex(node.nodeMap, bit), child),
            ),
            added: true,
            changed: true,
            duplicate: false,
        };
    }
    if ((node.nodeMap & bit) !== 0) {
        const index = sparseIndex(node.nodeMap, bit);
        const current = node.nodes[index];
        if (current === undefined) throw new Error("Corrupt CHAMP node bitmap.");
        const child = insertNode(current, hash, key, value, shift + bitsPerLevel, overwrite, policy);
        if (!child.changed) return { node, added: false, changed: false, duplicate: child.duplicate };
        return {
            node: bitmap(node.dataMap, node.nodeMap, node.data, replaceAt(node.nodes, index, child.node)),
            added: child.added,
            changed: true,
            duplicate: false,
        };
    }
    return {
        node: bitmap(node.dataMap | bit, node.nodeMap, insertAt(node.data, sparseIndex(node.dataMap, bit), leaf(hash, key, value)), node.nodes),
        added: true,
        changed: true,
        duplicate: false,
    };
}

function factoryUpdateNode<K, V>(
    node: Node<K, V>,
    hash: number,
    key: K,
    shift: number,
    policy: HashPolicy<K>,
    addFactory: (key: K) => V,
    updateFactory: ((key: K, value: V) => V) | undefined,
): FactoryUpdateNodeResult<K, V> {
    if (node.kind === "leaf") {
        if (node.hash === hash && policy.equivalent(node.key, key)) {
            if (updateFactory === undefined) {
                return { node, value: node.value, added: false, changed: false };
            }
            const selected = updateFactory(key, node.value);
            if (valuesEqual(node.value, selected)) {
                return { node, value: node.value, added: false, changed: false };
            }
            return {
                node: leaf(hash, node.key, selected),
                value: selected,
                added: false,
                changed: true,
            };
        }
        const selected = addFactory(key);
        return {
            node: mergeNodes(node, node.hash, leaf(hash, key, selected), hash, shift),
            value: selected,
            added: true,
            changed: true,
        };
    }

    if (node.kind === "collision") {
        if (node.hash !== hash) {
            const selected = addFactory(key);
            return {
                node: mergeNodes(node, node.hash, leaf(hash, key, selected), hash, shift),
                value: selected,
                added: true,
                changed: true,
            };
        }
        const index = node.entries.findIndex((entry) => policy.equivalent(entry.key, key));
        if (index < 0) {
            const selected = addFactory(key);
            return {
                node: collision(hash, [...node.entries, leaf(hash, key, selected)]),
                value: selected,
                added: true,
                changed: true,
            };
        }
        const current = node.entries[index];
        if (current === undefined) throw new Error("Corrupt CHAMP collision bucket.");
        if (updateFactory === undefined) {
            return { node, value: current.value, added: false, changed: false };
        }
        const selected = updateFactory(key, current.value);
        if (valuesEqual(current.value, selected)) {
            return { node, value: current.value, added: false, changed: false };
        }
        return {
            node: collision(hash, replaceAt(node.entries, index, leaf(hash, current.key, selected))),
            value: selected,
            added: false,
            changed: true,
        };
    }

    const bit = bitPosition(hashFragment(hash, shift));
    if ((node.dataMap & bit) !== 0) {
        const dataIndex = sparseIndex(node.dataMap, bit);
        const current = node.data[dataIndex];
        if (current === undefined) throw new Error("Corrupt CHAMP data bitmap.");
        if (current.hash === hash && policy.equivalent(current.key, key)) {
            if (updateFactory === undefined) {
                return { node, value: current.value, added: false, changed: false };
            }
            const selected = updateFactory(key, current.value);
            if (valuesEqual(current.value, selected)) {
                return { node, value: current.value, added: false, changed: false };
            }
            return {
                node: bitmap(
                    node.dataMap,
                    node.nodeMap,
                    replaceAt(node.data, dataIndex, leaf(hash, current.key, selected)),
                    node.nodes,
                ),
                value: selected,
                added: false,
                changed: true,
            };
        }
        const selected = addFactory(key);
        const child = mergeNodes(
            current,
            current.hash,
            leaf(hash, key, selected),
            hash,
            shift + bitsPerLevel,
        );
        return {
            node: bitmap(
                node.dataMap & ~bit,
                node.nodeMap | bit,
                removeAt(node.data, dataIndex),
                insertAt(node.nodes, sparseIndex(node.nodeMap, bit), child),
            ),
            value: selected,
            added: true,
            changed: true,
        };
    }
    if ((node.nodeMap & bit) !== 0) {
        const index = sparseIndex(node.nodeMap, bit);
        const current = node.nodes[index];
        if (current === undefined) throw new Error("Corrupt CHAMP node bitmap.");
        const child = factoryUpdateNode(
            current,
            hash,
            key,
            shift + bitsPerLevel,
            policy,
            addFactory,
            updateFactory,
        );
        if (!child.changed) {
            return { node, value: child.value, added: false, changed: false };
        }
        return {
            node: bitmap(
                node.dataMap,
                node.nodeMap,
                node.data,
                replaceAt(node.nodes, index, child.node),
            ),
            value: child.value,
            added: child.added,
            changed: true,
        };
    }

    const selected = addFactory(key);
    return {
        node: bitmap(
            node.dataMap | bit,
            node.nodeMap,
            insertAt(node.data, sparseIndex(node.dataMap, bit), leaf(hash, key, selected)),
            node.nodes,
        ),
        value: selected,
        added: true,
        changed: true,
    };
}

function singletonLeaf<K, V>(node: Node<K, V>): Leaf<K, V> | undefined {
    if (node.kind === "leaf") return node;
    if (node.kind === "collision") return node.entries.length === 1 ? node.entries[0] : undefined;
    return node.data.length === 1 && node.nodes.length === 0 ? node.data[0] : undefined;
}

function normalize<K, V>(node: BitmapNode<K, V>): Node<K, V> | undefined {
    if (node.data.length === 0 && node.nodes.length === 0) return undefined;
    if (node.data.length === 1 && node.nodes.length === 0) return node.data[0];
    if (node.data.length === 0 && node.nodes.length === 1 && node.nodes[0]?.kind !== "bitmap") return node.nodes[0];
    return node;
}

function removeNode<K, V>(
    node: Node<K, V>,
    hash: number,
    key: K,
    shift: number,
    policy: HashPolicy<K>,
): RemoveResult<K, V> {
    if (node.kind === "leaf") {
        return node.hash === hash && policy.equivalent(node.key, key)
            ? { node: undefined, removed: { key: node.key, value: node.value }, changed: true }
            : { node, removed: undefined, changed: false };
    }
    if (node.kind === "collision") {
        if (node.hash !== hash) return { node, removed: undefined, changed: false };
        const index = node.entries.findIndex((entry) => policy.equivalent(entry.key, key));
        if (index < 0) return { node, removed: undefined, changed: false };
        const removed = node.entries[index];
        if (removed === undefined) return { node, removed: undefined, changed: false };
        const next = removeAt(node.entries, index);
        const replacement = next.length === 0 ? undefined : next.length === 1 ? next[0] : collision(hash, next);
        return { node: replacement, removed: { key: removed.key, value: removed.value }, changed: true };
    }
    const bit = bitPosition(hashFragment(hash, shift));
    if ((node.dataMap & bit) !== 0) {
        const index = sparseIndex(node.dataMap, bit);
        const current = node.data[index];
        if (current === undefined || current.hash !== hash || !policy.equivalent(current.key, key)) {
            return { node, removed: undefined, changed: false };
        }
        const replacement = bitmap(node.dataMap & ~bit, node.nodeMap, removeAt(node.data, index), node.nodes);
        return { node: normalize(replacement), removed: { key: current.key, value: current.value }, changed: true };
    }
    if ((node.nodeMap & bit) === 0) return { node, removed: undefined, changed: false };
    const index = sparseIndex(node.nodeMap, bit);
    const current = node.nodes[index];
    if (current === undefined) throw new Error("Corrupt CHAMP node bitmap.");
    const child = removeNode(current, hash, key, shift + bitsPerLevel, policy);
    if (!child.changed) return { node, removed: undefined, changed: false };
    const promoted = child.node === undefined ? undefined : singletonLeaf(child.node);
    const replacement = child.node === undefined
        ? bitmap(node.dataMap, node.nodeMap & ~bit, node.data, removeAt(node.nodes, index))
        : promoted !== undefined
            ? bitmap(
                node.dataMap | bit,
                node.nodeMap & ~bit,
                insertAt(node.data, sparseIndex(node.dataMap, bit), promoted),
                removeAt(node.nodes, index),
            )
            : bitmap(node.dataMap, node.nodeMap, node.data, replaceAt(node.nodes, index, child.node));
    return { node: normalize(replacement), removed: child.removed, changed: true };
}

function* entriesOfNode<K, V>(root: Node<K, V>): Generator<HamtEntry<K, V>, void> {
    const stack: Node<K, V>[] = [root];
    while (stack.length !== 0) {
        const node = stack.pop();
        if (node === undefined) continue;
        if (node.kind === "leaf") yield { key: node.key, value: node.value };
        else if (node.kind === "collision") {
            for (const entry of node.entries) yield { key: entry.key, value: entry.value };
        } else {
            for (let index = node.nodes.length - 1; index >= 0; index--) {
                const child = node.nodes[index];
                if (child !== undefined) stack.push(child);
            }
            for (const entry of node.data) yield { key: entry.key, value: entry.value };
        }
    }
}

/** Immutable 32-way CHAMP hash map with collision buckets and structural sharing. */
export class PersistentHashMap<K, V> implements Iterable<HamtEntry<K, V>> {
    readonly #root: Node<K, V> | undefined;
    /** Number of entries. */
    public readonly size: number;
    /** The retained policy defining equivalence. */
    public readonly policy: HashPolicy<K>;

    private constructor(root: Node<K, V> | undefined, size: number, policy: HashPolicy<K>) {
        this.#root = root;
        this.size = size;
        this.policy = policy;
    }

    /** The empty map, retaining the supplied policy objects. */
    public static empty<K, V>(policy: HashPolicy<K> = defaultHashPolicy<K>()): PersistentHashMap<K, V> {
        return new PersistentHashMap<K, V>(undefined, 0, policy);
    }

    /** Build a map from the given entries. */
    public static from<K, V>(
        items: Iterable<readonly [K, V]>,
        policy: HashPolicy<K> = defaultHashPolicy<K>(),
    ): PersistentHashMap<K, V> {
        return new HashMapBulkBuilder<K, V>(policy).setItems(items).toImmutable();
    }

    /** Creates an empty single-owner mutable editing session. */
    public static createTransient<K, V>(policy: HashPolicy<K> = defaultHashPolicy<K>()): TransientHashMap<K, V> {
        return new TransientHashMap(PersistentHashMap.empty<K, V>(policy));
    }

    /** Creates an empty reusable construction-only bulk builder. */
    public static createBulkBuilder<K, V>(
        policy: HashPolicy<K> = defaultHashPolicy<K>(),
    ): HashMapBulkBuilder<K, V> {
        return new HashMapBulkBuilder(policy);
    }

    /** Adopts this immutable map in O(1); a clean session republishes this exact object. */
    public toTransient(): TransientHashMap<K, V> { return new TransientHashMap(this); }

    /** Whether the map holds no entries. */
    public get isEmpty(): boolean { return this.size === 0; }

    /** Whether both maps reference the same root. A representation test, not an equality test. */
    public sharesRootWith(other: PersistentHashMap<K, V>): boolean { return this.#root === other.#root; }

    /** Whether the key is present. */
    public containsKey(key: K): boolean { return this.getEntry(key) !== undefined; }

    /** The value stored for the key, or `undefined` when absent. */
    public get(key: K): V | undefined { return this.getEntry(key)?.value; }

    /** The stored key representative and value, or `undefined` when absent. */
    public getEntry(key: K): HamtEntry<K, V> | undefined {
        return this.#root === undefined ? undefined : getInNode(this.#root, this.policy.hash(key) | 0, key, 0, this.policy);
    }

    /** A map containing the given entry, keeping the stored representative when present. */
    public put(key: K, value: V): PersistentHashMap<K, V> {
        const hash = this.policy.hash(key) | 0;
        if (this.#root === undefined) return new PersistentHashMap(leaf(hash, key, value), 1, this.policy);
        const result = insertNode(this.#root, hash, key, value, 0, true, this.policy);
        return result.changed ? new PersistentHashMap(result.node, this.size + (result.added ? 1 : 0), this.policy) : this;
    }

    /** A map containing the given entry; returns the receiver when it is already present. */
    public add(key: K, value: V): PersistentHashMap<K, V> {
        const result = this.tryAdd(key, value);
        if (!result.added) throw new DuplicateKeyError();
        return result.value;
    }

    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
    public tryAdd(key: K, value: V): AddResult<PersistentHashMap<K, V>> {
        const hash = this.policy.hash(key) | 0;
        if (this.#root === undefined) return { value: new PersistentHashMap(leaf(hash, key, value), 1, this.policy), added: true };
        const result = insertNode(this.#root, hash, key, value, 0, false, this.policy);
        return result.duplicate
            ? { value: this, added: false }
            : { value: new PersistentHashMap(result.node, this.size + (result.added ? 1 : 0), this.policy), added: result.added };
    }

    /** Returns a stored value or adds one selected by the factory in one trie descent. */
    public getOrAdd(key: K, addFactory: (key: K) => V): MapUpdateResult<K, V> {
        if (typeof addFactory !== "function") throw new TypeError("addFactory must be a function.");
        return this.applyFactoryUpdate(key, addFactory, undefined);
    }

    /** Adds or updates one value, invoking exactly one selected factory in one trie descent. */
    public addOrUpdate(
        key: K,
        addFactory: (key: K) => V,
        updateFactory: (key: K, value: V) => V,
    ): MapUpdateResult<K, V> {
        if (typeof addFactory !== "function") throw new TypeError("addFactory must be a function.");
        if (typeof updateFactory !== "function") throw new TypeError("updateFactory must be a function.");
        return this.applyFactoryUpdate(key, addFactory, updateFactory);
    }

    /**
     * Apply `set` for each pair in turn, so later pairs overwrite earlier ones. Only the final map
     * is observable; for building from scratch a bulk builder avoids the per-item path copies.
     */
    public setItems(items: Iterable<readonly [K, V]>): PersistentHashMap<K, V> {
        let result: PersistentHashMap<K, V> = this;
        for (const [key, value] of items) result = result.put(key, value);
        return result;
    }

    /** A map without that entry; returns the receiver when it is absent. */
    public remove(key: K): PersistentHashMap<K, V> { return this.tryRemove(key)?.map ?? this; }

    /** Remove the entry and report what was removed, or `undefined` when absent. */
    public tryRemove(key: K): MapRemoveResult<K, V> | undefined {
        const result = this.tryRemoveEntry(key);
        return result === undefined ? undefined : { map: result.map, value: result.entry.value };
    }

    /** Remove the key and report the stored representative and value, or nothing when absent. */
    public tryRemoveEntry(key: K): MapRemoveEntryResult<K, V> | undefined {
        if (this.#root === undefined) return undefined;
        const result = removeNode(this.#root, this.policy.hash(key) | 0, key, 0, this.policy);
        if (!result.changed || result.removed === undefined) return undefined;
        return { map: new PersistentHashMap(result.node, this.size - 1, this.policy), entry: result.removed };
    }

    /** An empty map retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentHashMap<K, V> {
        return this.isEmpty ? this : new PersistentHashMap(undefined, 0, this.policy);
    }

    /** The entries of both maps. */
    public union(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> {
        this.requireSamePolicy(other);
        if (this.#root === other.#root) return this;
        return this.setItems(Array.from(other, (entry): readonly [K, V] => [entry.key, entry.value]));
    }

    /** The entries present in both maps. */
    public intersect(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> {
        this.requireSamePolicy(other);
        if (this.#root === other.#root) return this;
        let result = PersistentHashMap.empty<K, V>(this.policy);
        for (const entry of this) if (other.containsKey(entry.key)) result = result.put(entry.key, entry.value);
        return result.mapEquals(this) ? this : result;
    }

    /** This map's entries that are absent from the other. */
    public except(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> {
        this.requireSamePolicy(other);
        if (this.#root === other.#root) return this.clear();
        let result: PersistentHashMap<K, V> = this;
        for (const entry of other) result = result.remove(entry.key);
        return result;
    }

    /** The entries present in exactly one of the two maps. */
    public symmetricExcept(other: PersistentHashMap<K, V>): PersistentHashMap<K, V> {
        this.requireSamePolicy(other);
        if (this.#root === other.#root) return this.clear();
        let result: PersistentHashMap<K, V> = this;
        for (const entry of other) result = result.containsKey(entry.key) ? result.remove(entry.key) : result.put(entry.key, entry.value);
        return result;
    }

    /** Whether both hold the same entries, comparing values with the supplied predicate. */
    public mapEquals(other: PersistentHashMap<K, V>, valueEquals: (left: V, right: V) => boolean = valuesEqual): boolean {
        this.requireSamePolicy(other);
        if (this.#root === other.#root) return true;
        if (this.size !== other.size) return false;
        for (const entry of this) {
            const candidate = other.getEntry(entry.key);
            if (candidate === undefined || !valueEquals(entry.value, candidate.value)) return false;
        }
        return true;
    }

    public *diff(other: PersistentHashMap<K, V>, valueEquals: (left: V, right: V) => boolean = valuesEqual): Generator<MapDifference<K, V>, void> {
        this.requireSamePolicy(other);
        if (this.#root === other.#root) return;
        for (const entry of this) {
            const after = other.getEntry(entry.key);
            if (after === undefined) yield { kind: "removed", key: entry.key, before: entry.value, after: undefined };
            else if (!valueEquals(entry.value, after.value)) yield { kind: "changed", key: entry.key, before: entry.value, after: after.value };
        }
        for (const entry of other) {
            if (!this.containsKey(entry.key)) yield { kind: "added", key: entry.key, before: undefined, after: entry.value };
        }
    }

    public *keys(): Generator<K, void> { for (const entry of this) yield entry.key; }
    public *values(): Generator<V, void> { for (const entry of this) yield entry.value; }
    /** Iterate the entries. */
    public entries(): IterableIterator<HamtEntry<K, V>> { return this[Symbol.iterator](); }

    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> {
        return this.#root === undefined ? [][Symbol.iterator]() : entriesOfNode(this.#root);
    }

    private applyFactoryUpdate(
        key: K,
        addFactory: (key: K) => V,
        updateFactory: ((key: K, value: V) => V) | undefined,
    ): MapUpdateResult<K, V> {
        const hash = this.policy.hash(key) | 0;
        if (this.#root === undefined) {
            const value = addFactory(key);
            return { map: new PersistentHashMap(leaf(hash, key, value), 1, this.policy), value };
        }
        const result = factoryUpdateNode(
            this.#root,
            hash,
            key,
            0,
            this.policy,
            addFactory,
            updateFactory,
        );
        const map = result.changed
            ? new PersistentHashMap(result.node, this.size + (result.added ? 1 : 0), this.policy)
            : this;
        return { map, value: result.value };
    }

    private requireSamePolicy(other: PersistentHashMap<K, V>): void {
        if (this.policy !== other.policy) throw new TypeError("Maps must retain the same hash policy object.");
    }
}

function persistentMapFromBulkRoot<K, V>(
    root: Node<K, V> | undefined,
    size: number,
    policy: HashPolicy<K>,
): PersistentHashMap<K, V> {
    const constructor = PersistentHashMap as unknown as new (
        root: Node<K, V> | undefined,
        size: number,
        policy: HashPolicy<K>,
    ) => PersistentHashMap<K, V>;
    return new constructor(root, size, policy);
}

/** Reusable construction-only map builder whose freezes are detached immutable snapshots. */
export class HashMapBulkBuilder<K, V> {
    #root: MutableNode<K, V> | undefined;
    #size = 0;
    readonly #policy: HashPolicy<K>;

    public constructor(policy: HashPolicy<K> = defaultHashPolicy<K>()) {
        this.#policy = policy;
    }

    /** Number of entries. */
    public get size(): number { return this.#size; }
    /** Whether the builder holds no entries. */
    public get isEmpty(): boolean { return this.#size === 0; }
    /** The retained policy defining equivalence. */
    public get policy(): HashPolicy<K> { return this.#policy; }

    /** A map with the key bound to the value, adding or replacing as needed. */
    public setItem(key: K, value: V): HashMapBulkBuilder<K, V> {
        this.applyItem(key, value);
        return this;
    }

    /** Add or replace every pair in turn, mutating the builder's unpublished nodes. */
    public setItems(items: Iterable<readonly [K, V]>): HashMapBulkBuilder<K, V> {
        if (items === null || items === undefined) throw new TypeError("items must be iterable.");
        for (const [key, value] of items) this.setItem(key, value);
        return this;
    }

    /** Copies all CHAMP nodes into a detached persistent snapshot and keeps the builder active. */
    public toImmutable(): PersistentHashMap<K, V> {
        return persistentMapFromBulkRoot(
            this.#root === undefined ? undefined : freezeMutableNode(this.#root),
            this.#size,
            this.#policy,
        );
    }

    private applyItem(
        key: K,
        value: V,
        combine: ((existing: V, incoming: V) => V) | undefined = getBulkBuilderCombiner<V>(this),
    ): void {
        const hash = this.#policy.hash(key) | 0;
        if (this.#root === undefined) {
            this.#root = mutableLeaf(hash, key, value);
            this.#size = 1;
            return;
        }
        const result = insertMutableNode(
            this.#root,
            hash,
            key,
            value,
            0,
            this.#policy,
            combine,
        );
        this.#root = result.node;
        if (result.added) this.#size++;
    }
}

/** Immutable CHAMP hash set preserving stored representatives and policy identity. */
export class PersistentHashSet<T> implements Iterable<T> {
    readonly #map: PersistentHashMap<T, true>;

    private constructor(map: PersistentHashMap<T, true>) { this.#map = map; }

    /** The empty set, retaining the supplied policy objects. */
    public static empty<T>(policy: HashPolicy<T> = defaultHashPolicy<T>()): PersistentHashSet<T> {
        return new PersistentHashSet(PersistentHashMap.empty<T, true>(policy));
    }

    /** Build a set from the given elements. */
    public static from<T>(values: Iterable<T>, policy: HashPolicy<T> = defaultHashPolicy<T>()): PersistentHashSet<T> {
        if (values === null || values === undefined) throw new TypeError("values must be iterable.");
        const builder = PersistentHashMap.createBulkBuilder<T, true>(policy);
        for (const value of values) builder.setItem(value, true);
        return new PersistentHashSet(builder.toImmutable());
    }

    /** An empty single-owner editing session under the given policy. */
    public static createTransient<T>(policy: HashPolicy<T> = defaultHashPolicy<T>()): TransientHashSet<T> {
        return new TransientHashSet(PersistentHashSet.empty<T>(policy));
    }

    /**
     * A single-owner editing session starting from this value, which is itself unaffected by the
     * session's edits.
     */
    public toTransient(): TransientHashSet<T> { return new TransientHashSet(this); }

    /** Number of elements. */
    public get size(): number { return this.#map.size; }
    /** Whether the set holds no elements. */
    public get isEmpty(): boolean { return this.#map.isEmpty; }
    /** The retained policy defining equivalence. */
    public get policy(): HashPolicy<T> { return this.#map.policy; }
    /** Whether both sets reference the same root. A representation test, not an equality test. */
    public sharesRootWith(other: PersistentHashSet<T>): boolean { return this.#map.sharesRootWith(other.#map); }
    /** Whether the element is present. */
    public contains(value: T): boolean { return this.#map.containsKey(value); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(value: T): T | undefined { return this.#map.getEntry(value)?.key; }
    /** A set containing the given element; returns the receiver when it is already present. */
    public add(value: T): PersistentHashSet<T> { return this.withMap(this.#map.add(value, true)); }

    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
    public tryAdd(value: T): AddResult<PersistentHashSet<T>> {
        const result = this.#map.tryAdd(value, true);
        return { value: this.withMap(result.value), added: result.added };
    }

    /** A set containing the given element, keeping the stored representative when present. */
    public put(value: T): PersistentHashSet<T> { return this.withMap(this.#map.put(value, true)); }
    /** A set without that element; returns the receiver when it is absent. */
    public remove(value: T): PersistentHashSet<T> { return this.withMap(this.#map.remove(value)); }

    /** Remove the element and report what was removed, or `undefined` when absent. */
    public tryRemove(value: T): SetRemoveResult<T> | undefined {
        const result = this.#map.tryRemoveEntry(value);
        return result === undefined ? undefined : { set: this.withMap(result.map), value: result.entry.key };
    }

    /** An empty set retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentHashSet<T> { return this.withMap(this.#map.clear()); }

    /** The elements of both sets. */
    public union(values: Iterable<T> | PersistentHashSet<T>): PersistentHashSet<T> {
        if (values instanceof PersistentHashSet) return this.withMap(this.#map.union(values.#map));
        let result: PersistentHashSet<T> = this;
        for (const value of values) result = result.put(value);
        return result;
    }

    /** The elements present in both sets. */
    public intersect(values: Iterable<T> | PersistentHashSet<T>): PersistentHashSet<T> {
        if (values instanceof PersistentHashSet && values.policy === this.policy) return this.withMap(this.#map.intersect(values.#map));
        const probe = PersistentHashSet.from(values, this.policy);
        const builder = PersistentHashMap.createBulkBuilder<T, true>(this.policy);
        for (const value of this) if (probe.contains(value)) builder.setItem(value, true);
        const result = new PersistentHashSet(builder.toImmutable());
        return result.size === this.size ? this : result;
    }

    /** This set's elements that are absent from the other. */
    public except(values: Iterable<T> | PersistentHashSet<T>): PersistentHashSet<T> {
        if (values instanceof PersistentHashSet && values.policy === this.policy) return this.withMap(this.#map.except(values.#map));
        let result: PersistentHashSet<T> = this;
        for (const value of values) result = result.remove(value);
        return result;
    }

    /** The elements present in exactly one of the two sets. */
    public symmetricExcept(values: Iterable<T> | PersistentHashSet<T>): PersistentHashSet<T> {
        if (values instanceof PersistentHashSet && values.policy === this.policy) return this.withMap(this.#map.symmetricExcept(values.#map));
        const distinct = PersistentHashSet.from(values, this.policy);
        let result: PersistentHashSet<T> = this;
        for (const value of distinct) result = result.contains(value) ? result.remove(value) : result.put(value);
        return result;
    }

    /** Whether every element of this set also occurs in the other. */
    public isSubsetOf(values: Iterable<T>): boolean {
        const probe = values instanceof PersistentHashSet && values.policy === this.policy ? values : PersistentHashSet.from(values, this.policy);
        if (this.size > probe.size) return false;
        for (const value of this) if (!probe.contains(value)) return false;
        return true;
    }

    /** Whether this set is a subset of the other and the other holds an element it lacks. */
    public isProperSubsetOf(values: Iterable<T>): boolean {
        const probe = values instanceof PersistentHashSet && values.policy === this.policy ? values : PersistentHashSet.from(values, this.policy);
        return this.size < probe.size && this.isSubsetOf(probe);
    }

    /** Whether every element of the other occurs in this set. */
    public isSupersetOf(values: Iterable<T>): boolean {
        for (const value of values) if (!this.contains(value)) return false;
        return true;
    }

    /** Whether this set is a superset of the other and holds an element the other lacks. */
    public isProperSupersetOf(values: Iterable<T>): boolean {
        const probe = values instanceof PersistentHashSet && values.policy === this.policy ? values : PersistentHashSet.from(values, this.policy);
        return this.size > probe.size && this.isSupersetOf(probe);
    }

    /** Whether the two sets share at least one element. */
    public overlaps(values: Iterable<T>): boolean {
        for (const value of values) if (this.contains(value)) return true;
        return false;
    }

    /** Whether both sets hold the same elements. */
    public setEquals(values: Iterable<T>): boolean {
        const probe = values instanceof PersistentHashSet && values.policy === this.policy ? values : PersistentHashSet.from(values, this.policy);
        return this.size === probe.size && this.isSubsetOf(probe);
    }

    public [Symbol.iterator](): IterableIterator<T> { return this.#map.keys(); }

    private withMap(value: PersistentHashMap<T, true>): PersistentHashSet<T> {
        return value === this.#map ? this : new PersistentHashSet(value);
    }
}

/** Raised when a one-way transient session is accessed after publication. */
export class TransientConsumedError extends Error {
    public constructor() { super("The transient session has already been published."); this.name = "TransientConsumedError"; }
}

/**
 * Unsynchronized, single-owner CHAMP editing session.
 *
 * JavaScript cannot expose interior owner-token mutation without compromising the immutable public
 * representation, so edits use the persistent CHAMP kernel. Adoption and publication remain O(1),
 * no-op sessions preserve exact source identity, enumeration is version-bound, and publication is
 * one-way exactly like the sibling-language transient contract.
 */
export class TransientHashMap<K, V> implements Iterable<HamtEntry<K, V>> {
    #current: PersistentHashMap<K, V>;
    #active = true;
    #version = 0;

    public constructor(source: PersistentHashMap<K, V>) { this.#current = source; }
    /** Number of entries. */
    public get size(): number { this.ensureActive(); return this.#current.size; }
    /** Whether the session holds no entries. */
    public get isEmpty(): boolean { return this.size === 0; }
    /** The retained policy defining equivalence. */
    public get policy(): HashPolicy<K> { this.ensureActive(); return this.#current.policy; }
    /** Whether the key is present. */
    public containsKey(key: K): boolean { this.ensureActive(); return this.#current.containsKey(key); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(key: K): V | undefined { this.ensureActive(); return this.#current.get(key); }
    /** The stored key representative and value, or `undefined` when absent. */
    public getEntry(key: K): HamtEntry<K, V> | undefined { this.ensureActive(); return this.#current.getEntry(key); }

    /** A map with the key bound to the value, adding or replacing as needed. */
    public set(key: K, value: V): void { this.publishMutation(this.#current.put(key, value)); }
    /** Add the entry, reporting whether it was added rather than throwing on a duplicate. */
    public tryAdd(key: K, value: V): boolean {
        this.ensureActive(); const result = this.#current.tryAdd(key, value);
        if (result.added) this.publishMutation(result.value); return result.added;
    }
    /** A session containing the given entry; returns the receiver when it is already present. */
    public add(key: K, value: V): void { if (!this.tryAdd(key, value)) throw new DuplicateKeyError(); }
    /** A session without that entry; returns the receiver when it is absent. */
    public remove(key: K): boolean {
        this.ensureActive(); const result = this.#current.tryRemoveEntry(key);
        if (result === undefined) return false; this.publishMutation(result.map); return true;
    }
    /** An empty session retaining the same policies; returns the receiver when already empty. */
    public clear(): void { this.publishMutation(this.#current.clear()); }

    /** Consumes the session. Every later operation, including enumeration, throws. */
    public persist(): PersistentHashMap<K, V> {
        this.ensureActive(); this.#active = false; return this.#current;
    }

    /** Iterate the keys. */
    public keys(): IterableIterator<K> {
        const entries = this.versionedEntries();
        return (function* (): IterableIterator<K> { for (const entry of entries) yield entry.key; })();
    }
    /** Iterate the values. */
    public values(): IterableIterator<V> {
        const entries = this.versionedEntries();
        return (function* (): IterableIterator<V> { for (const entry of entries) yield entry.value; })();
    }
    public [Symbol.iterator](): IterableIterator<HamtEntry<K, V>> { return this.versionedEntries(); }

    private versionedEntries(): IterableIterator<HamtEntry<K, V>> {
        this.ensureActive(); const expected = this.#version, owner = this, iterator = this.#current[Symbol.iterator]();
        return (function* (): IterableIterator<HamtEntry<K, V>> {
            while (true) {
                owner.validateVersion(expected); const item = iterator.next();
                if (item.done) return; yield item.value;
            }
        })();
    }
    private publishMutation(next: PersistentHashMap<K, V>): void {
        this.ensureActive(); if (next !== this.#current) { this.#current = next; this.#version++; }
    }
    private validateVersion(expected: number): void {
        this.ensureActive(); if (this.#version !== expected) throw new Error("The transient was modified during enumeration.");
    }
    private ensureActive(): void { if (!this.#active) throw new TransientConsumedError(); }
}

/** Single-owner mutable wrapper for a persistent CHAMP set. */
export class TransientHashSet<T> implements Iterable<T> {
    #current: PersistentHashSet<T>;
    #active = true;
    #version = 0;
    public constructor(source: PersistentHashSet<T>) { this.#current = source; }
    /** Number of elements. */
    public get size(): number { this.ensureActive(); return this.#current.size; }
    /** Whether the session holds no elements. */
    public get isEmpty(): boolean { return this.size === 0; }
    /** The retained policy defining equivalence. */
    public get policy(): HashPolicy<T> { this.ensureActive(); return this.#current.policy; }
    /** Whether the element is present. */
    public contains(value: T): boolean { this.ensureActive(); return this.#current.contains(value); }
    /** The value stored for the key, or `undefined` when absent. */
    public get(value: T): T | undefined { this.ensureActive(); return this.#current.get(value); }
    /** A session containing the given element; returns the receiver when it is already present. */
    public add(value: T): boolean { this.ensureActive(); const result = this.#current.tryAdd(value); if (result.added) this.publishMutation(result.value); return result.added; }
    /** A session containing the given element, keeping the stored representative when present. */
    public put(value: T): void { this.publishMutation(this.#current.put(value)); }
    /** A session without that element; returns the receiver when it is absent. */
    public remove(value: T): boolean { this.ensureActive(); const result = this.#current.tryRemove(value); if (result === undefined) return false; this.publishMutation(result.set); return true; }
    /** An empty session retaining the same policies; returns the receiver when already empty. */
    public clear(): void { this.publishMutation(this.#current.clear()); }
    /** Whether every element of this session also occurs in the other. */
    public isSubsetOf(values: Iterable<T>): boolean { this.ensureActive(); return this.#current.isSubsetOf(values); }
    /** Whether this session is a subset of the other and the other holds an element it lacks. */
    public isProperSubsetOf(values: Iterable<T>): boolean { this.ensureActive(); return this.#current.isProperSubsetOf(values); }
    /** Whether every element of the other occurs in this session. */
    public isSupersetOf(values: Iterable<T>): boolean { this.ensureActive(); return this.#current.isSupersetOf(values); }
    /** Whether this session is a superset of the other and holds an element the other lacks. */
    public isProperSupersetOf(values: Iterable<T>): boolean { this.ensureActive(); return this.#current.isProperSupersetOf(values); }
    /** Whether the two sessions share at least one element. */
    public overlaps(values: Iterable<T>): boolean { this.ensureActive(); return this.#current.overlaps(values); }
    /** Whether both sessions hold the same elements. */
    public setEquals(values: Iterable<T>): boolean { this.ensureActive(); return this.#current.setEquals(values); }
    /**
     * End the session and return its current value. The session is closed afterwards, which is what
     * makes the published value safe to share.
     */
    public persist(): PersistentHashSet<T> { this.ensureActive(); this.#active = false; return this.#current; }
    public [Symbol.iterator](): IterableIterator<T> {
        this.ensureActive(); const expected = this.#version, owner = this, iterator = this.#current[Symbol.iterator]();
        return (function* (): IterableIterator<T> { while (true) { owner.validateVersion(expected); const item = iterator.next(); if (item.done) return; yield item.value; } })();
    }
    private publishMutation(next: PersistentHashSet<T>): void { this.ensureActive(); if (next !== this.#current) { this.#current = next; this.#version++; } }
    private validateVersion(expected: number): void { this.ensureActive(); if (this.#version !== expected) throw new Error("The transient was modified during enumeration."); }
    private ensureActive(): void { if (!this.#active) throw new TransientConsumedError(); }
}
