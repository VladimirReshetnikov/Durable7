import { MerkleDigest, MerkleEncodingInternals, type MerkleSearchTreePolicy } from "./merkle-encoding.js";

const { concat, i32 } = MerkleEncodingInternals;

/** @internal */
export interface MerkleStoredEntry<K, V> {
  readonly key: K;
  readonly value: V;
  readonly keyBytes: Uint8Array;
  readonly valueBytes: Uint8Array;
  readonly level: number;
}

/** @internal */
export interface MerkleNode<K, V> {
  readonly level: number;
  readonly entries: readonly MerkleStoredEntry<K, V>[];
  readonly children: readonly (MerkleNode<K, V> | undefined)[];
  readonly count: number;
  readonly height: number;
  readonly blockCount: number;
  readonly minimumKey: K;
  readonly maximumKey: K;
  readonly blockBytes: Uint8Array;
  readonly digest: MerkleDigest;
}

export interface MerkleEntry<K, V> { readonly key: K; readonly value: V }
export type MerkleMapDifferenceKind = "left-only" | "right-only" | "different-value";
export interface MerkleMapDifference<K, V> {
  readonly kind: MerkleMapDifferenceKind;
  readonly key: K;
  readonly left?: V;
  readonly right?: V;
}
export interface MerkleSearchTreeStatistics {
  readonly count: number;
  readonly height: number;
  readonly blockCount: number;
  readonly maximumEntriesPerBlock: number;
  readonly maximumChildrenPerBlock: number;
  readonly encodedBytes: number;
  readonly maximumLevel: number;
}
export interface MerkleEncodedBlock { readonly digest: MerkleDigest; readonly bytes: Uint8Array }

function equalBytes(left: Uint8Array, right: Uint8Array): boolean {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

function levelOf(digest: MerkleDigest): number {
  let level = 0;
  for (const value of digest.toBytes()) {
    if ((value & 0xf0) === 0) level++; else return level;
    if ((value & 0x0f) === 0) level++; else return level;
  }
  return level;
}

function lowerBound<K, V>(entries: readonly MerkleStoredEntry<K, V>[], key: K, comparer: (left: K, right: K) => number): number {
  let low = 0;
  let high = entries.length;
  while (low < high) {
    const middle = (low + high) >>> 1;
    if (comparer(entries[middle]!.key, key) < 0) low = middle + 1; else high = middle;
  }
  return low;
}

function* enumerateNode<K, V>(node: MerkleNode<K, V> | undefined): IterableIterator<MerkleEntry<K, V>> {
  if (node === undefined) return;
  for (let index = 0; index < node.entries.length; index++) {
    yield* enumerateNode(node.children[index]);
    const entry = node.entries[index]!;
    yield { key: entry.key, value: entry.value };
  }
  yield* enumerateNode(node.children[node.entries.length]);
}

/** Canonical wide, immutable, SHA-256-addressed ordered map with exact `MST2` blocks. */
export class MerkleSearchTree<K, V> implements Iterable<MerkleEntry<K, V>> {
  private constructor(readonly policy: MerkleSearchTreePolicy<K, V>, private readonly root: MerkleNode<K, V> | undefined) {}

  static empty<K, V>(policy: MerkleSearchTreePolicy<K, V>): MerkleSearchTree<K, V> {
    return new MerkleSearchTree(policy, undefined);
  }

  static from<K, V>(entries: Iterable<readonly [K, V] | MerkleEntry<K, V>>, policy: MerkleSearchTreePolicy<K, V>): MerkleSearchTree<K, V> {
    const prepared = [...entries].map(item => {
      const key = "key" in item ? item.key : item[0];
      const value = "value" in item ? item.value : item[1];
      return MerkleSearchTree.createEntry(policy, key, value);
    });
    prepared.sort((left, right) => policy.comparer(left.key, right.key));
    const unique: MerkleStoredEntry<K, V>[] = [];
    for (const item of prepared) {
      const previous = unique.at(-1);
      if (previous !== undefined && policy.comparer(previous.key, item.key) === 0) {
        unique[unique.length - 1] = { ...item, key: previous.key, keyBytes: previous.keyBytes, level: previous.level };
      } else unique.push(item);
    }
    const tree = new MerkleSearchTree<K, V>(policy, undefined);
    return new MerkleSearchTree(policy, tree.buildCanonical(unique, 0, unique.length));
  }

  /** @internal Creates a verified tree from a decoded root. */
  static fromVerifiedRoot<K, V>(policy: MerkleSearchTreePolicy<K, V>, root: MerkleNode<K, V> | undefined): MerkleSearchTree<K, V> {
    return new MerkleSearchTree(policy, root);
  }

  get count(): number { return this.root?.count ?? 0; }
  get isEmpty(): boolean { return this.root === undefined; }
  get height(): number { return this.root?.height ?? 0; }
  get blockCount(): number { return this.root?.blockCount ?? 0; }
  get rootHash(): MerkleDigest { return this.root?.digest ?? this.policy.emptyDigest; }
  get keys(): Iterable<K> { const tree = this; return { *[Symbol.iterator]() { for (const entry of tree) yield entry.key; } }; }
  get values(): Iterable<V> { const tree = this; return { *[Symbol.iterator]() { for (const entry of tree) yield entry.value; } }; }

  getEntry(key: K): MerkleEntry<K, V> | undefined {
    let node = this.root;
    while (node !== undefined) {
      const position = lowerBound(node.entries, key, this.policy.comparer);
      if (position < node.entries.length && this.policy.comparer(node.entries[position]!.key, key) === 0) {
        const found = node.entries[position]!;
        return { key: found.key, value: found.value };
      }
      node = node.children[position];
    }
    return undefined;
  }

  has(key: K): boolean { return this.getEntry(key) !== undefined; }
  get(key: K): V | undefined { return this.getEntry(key)?.value; }

  set(key: K, value: V): MerkleSearchTree<K, V> {
    const existing = this.findRecord(key);
    if (existing !== undefined) {
      const valueBytes = this.policy.valueCodec.encode(value).slice();
      if (equalBytes(existing.valueBytes, valueBytes)) return this;
      const replacement: MerkleStoredEntry<K, V> = { ...existing, value, valueBytes };
      return new MerkleSearchTree(this.policy, this.updateValue(this.root!, key, replacement));
    }
    return new MerkleSearchTree(this.policy, this.insert(this.root, MerkleSearchTree.createEntry(this.policy, key, value)));
  }

  remove(key: K): MerkleSearchTree<K, V> {
    const result = this.removeNode(this.root, key);
    return result.removed ? new MerkleSearchTree(this.policy, result.node) : this;
  }

  clear(): MerkleSearchTree<K, V> { return this.isEmpty ? this : MerkleSearchTree.empty(this.policy); }
  sharesRootWith(other: MerkleSearchTree<K, V>): boolean { return this.root === other.root; }

  sharedBlockCount(other: MerkleSearchTree<K, V>): number {
    const left = new Set<MerkleNode<K, V>>();
    for (const node of this.nodesPreorder()) left.add(node);
    let count = 0;
    for (const node of other.nodesPreorder()) if (left.has(node)) count++;
    return count;
  }

  contentEquals(other: MerkleSearchTree<K, V>): boolean {
    return this.policy.compatibleWith(other.policy) && this.rootHash.equals(other.rootHash);
  }

  mapEquals(other: MerkleSearchTree<K, V>, valuesEqual: (left: V, right: V) => boolean = Object.is): boolean {
    if (this.count !== other.count) return false;
    const left = this[Symbol.iterator]();
    const right = other[Symbol.iterator]();
    while (true) {
      const a = left.next(); const b = right.next();
      if (a.done || b.done) return a.done === b.done;
      if (this.policy.comparer(a.value.key, b.value.key) !== 0 || !valuesEqual(a.value.value, b.value.value)) return false;
    }
  }

  diff(other: MerkleSearchTree<K, V>, valuesEqual: (left: V, right: V) => boolean = Object.is): readonly MerkleMapDifference<K, V>[] {
    if (!this.policy.compatibleWith(other.policy)) throw new RangeError("Merkle policy domains differ.");
    if (this.rootHash.equals(other.rootHash)) return [];
    const result: MerkleMapDifference<K, V>[] = [];
    const left = [...this], right = [...other];
    let i = 0, j = 0;
    while (i < left.length || j < right.length) {
      if (i === left.length) { const b = right[j++]!; result.push({ kind: "right-only", key: b.key, right: b.value }); continue; }
      if (j === right.length) { const a = left[i++]!; result.push({ kind: "left-only", key: a.key, left: a.value }); continue; }
      const a = left[i]!, b = right[j]!, comparison = this.policy.comparer(a.key, b.key);
      if (comparison < 0) { result.push({ kind: "left-only", key: a.key, left: a.value }); i++; }
      else if (comparison > 0) { result.push({ kind: "right-only", key: b.key, right: b.value }); j++; }
      else { if (!valuesEqual(a.value, b.value)) result.push({ kind: "different-value", key: a.key, left: a.value, right: b.value }); i++; j++; }
    }
    return result;
  }

  *range(minimum: K, maximum: K): IterableIterator<MerkleEntry<K, V>> {
    if (this.policy.comparer(minimum, maximum) > 0) throw new RangeError("Minimum key exceeds maximum key.");
    for (const entry of this) {
      if (this.policy.comparer(entry.key, minimum) < 0) continue;
      if (this.policy.comparer(entry.key, maximum) > 0) return;
      yield entry;
    }
  }

  get shape(): string {
    const visit = (node: MerkleNode<K, V> | undefined): string => node === undefined ? "." : `L${node.level}[${node.entries.length}](${node.children.map(visit).join(",")})`;
    return visit(this.root);
  }

  *blocksPreorder(): IterableIterator<MerkleEncodedBlock> {
    for (const node of this.nodesPreorder()) yield { digest: node.digest, bytes: node.blockBytes.slice() };
  }

  validateStructure(): MerkleSearchTreeStatistics {
    let maximumEntriesPerBlock = 0, maximumChildrenPerBlock = 0, encodedBytes = 0, maximumLevel = 0, blocks = 0, count = 0, height = 0;
    const walk = (node: MerkleNode<K, V> | undefined, minimum?: K, maximum?: K): void => {
      if (node === undefined) return;
      blocks++; count += node.entries.length; height = Math.max(height, node.height);
      maximumEntriesPerBlock = Math.max(maximumEntriesPerBlock, node.entries.length);
      maximumChildrenPerBlock = Math.max(maximumChildrenPerBlock, node.children.length);
      encodedBytes += node.blockBytes.length; maximumLevel = Math.max(maximumLevel, node.level);
      if (node.entries.length === 0 || node.children.length !== node.entries.length + 1) throw new Error("Invalid Merkle block arity.");
      for (let index = 0; index < node.entries.length; index++) {
        const entry = node.entries[index]!;
        if (entry.level !== node.level) throw new Error("Separator level differs from block level.");
        if (index > 0 && this.policy.comparer(node.entries[index - 1]!.key, entry.key) >= 0) throw new Error("Merkle separators are not strictly ordered.");
        if (minimum !== undefined && this.policy.comparer(entry.key, minimum) <= 0) throw new Error("Entry violates lower interval bound.");
        if (maximum !== undefined && this.policy.comparer(entry.key, maximum) >= 0) throw new Error("Entry violates upper interval bound.");
        if (levelOf(this.policy.hashKey(entry.keyBytes)) !== entry.level) throw new Error("Key level cache is invalid.");
      }
      for (let index = 0; index < node.children.length; index++) {
        const child = node.children[index];
        if (child !== undefined && child.level >= node.level) throw new Error("Child levels must decrease.");
        walk(child, index === 0 ? minimum : node.entries[index - 1]!.key, index === node.entries.length ? maximum : node.entries[index]!.key);
      }
      const rebuilt = this.newNode(node.level, node.entries, node.children);
      if (rebuilt.count !== node.count || rebuilt.height !== node.height || rebuilt.blockCount !== node.blockCount || !rebuilt.digest.equals(node.digest) || !equalBytes(rebuilt.blockBytes, node.blockBytes)) throw new Error("Merkle node caches are invalid.");
    };
    walk(this.root);
    if (count !== this.count || blocks !== this.blockCount) throw new Error("Merkle tree aggregate caches are invalid.");
    return { count, height, blockCount: blocks, maximumEntriesPerBlock, maximumChildrenPerBlock, encodedBytes, maximumLevel };
  }

  [Symbol.iterator](): Iterator<MerkleEntry<K, V>> { return enumerateNode(this.root); }

  /** @internal */
  getRootForPersistence(): MerkleNode<K, V> | undefined { return this.root; }

  /** @internal Creates a node only after persistence code has authenticated all inputs. */
  createVerifiedNode(level: number, entries: readonly MerkleStoredEntry<K, V>[], children: readonly (MerkleNode<K, V> | undefined)[]): MerkleNode<K, V> {
    return this.newNode(level, entries, children);
  }

  /** @internal */
  createVerifiedEntry(key: K, value: V, keyBytes: Uint8Array, valueBytes: Uint8Array): MerkleStoredEntry<K, V> {
    return { key, value, keyBytes: keyBytes.slice(), valueBytes: valueBytes.slice(), level: levelOf(this.policy.hashKey(keyBytes)) };
  }

  private static createEntry<K, V>(policy: MerkleSearchTreePolicy<K, V>, key: K, value: V): MerkleStoredEntry<K, V> {
    const keyBytes = policy.keyCodec.encode(key).slice(), valueBytes = policy.valueCodec.encode(value).slice();
    return { key, value, keyBytes, valueBytes, level: levelOf(policy.hashKey(keyBytes)) };
  }

  private findRecord(key: K): MerkleStoredEntry<K, V> | undefined {
    let node = this.root;
    while (node !== undefined) {
      const position = lowerBound(node.entries, key, this.policy.comparer);
      if (position < node.entries.length && this.policy.comparer(node.entries[position]!.key, key) === 0) return node.entries[position];
      node = node.children[position];
    }
    return undefined;
  }

  private buildCanonical(items: readonly MerkleStoredEntry<K, V>[], start: number, length: number): MerkleNode<K, V> | undefined {
    if (length === 0) return undefined;
    let maximum = 0;
    for (let index = start; index < start + length; index++) maximum = Math.max(maximum, items[index]!.level);
    const entries: MerkleStoredEntry<K, V>[] = [], children: (MerkleNode<K, V> | undefined)[] = [];
    let segment = start;
    for (let index = start; index < start + length; index++) if (items[index]!.level === maximum) {
      children.push(this.buildCanonical(items, segment, index - segment)); entries.push(items[index]!); segment = index + 1;
    }
    children.push(this.buildCanonical(items, segment, start + length - segment));
    return this.newNode(maximum, entries, children);
  }

  private newNode(level: number, entries: readonly MerkleStoredEntry<K, V>[], children: readonly (MerkleNode<K, V> | undefined)[]): MerkleNode<K, V> {
    if (level < 0 || level > 64 || entries.length === 0 || children.length !== entries.length + 1) throw new Error("Invalid Merkle node.");
    let count = entries.length, height = 1, blockCount = 1;
    for (const child of children) if (child !== undefined) { count += child.count; height = Math.max(height, child.height + 1); blockCount += child.blockCount; }
    const blockBytes = this.encodeBlock(level, count, entries, children), digest = MerkleDigest.hash(blockBytes);
    return {
      level, entries, children, count, height, blockCount,
      minimumKey: children[0]?.minimumKey ?? entries[0]!.key,
      maximumKey: children.at(-1)?.maximumKey ?? entries.at(-1)!.key,
      blockBytes, digest,
    };
  }

  private encodeBlock(level: number, count: number, entries: readonly MerkleStoredEntry<K, V>[], children: readonly (MerkleNode<K, V> | undefined)[]): Uint8Array {
    return concat([
      Uint8Array.from([0x4d, 0x53, 0x54, 0x32, 1]), this.policy.domainDigest.toBytes(), Uint8Array.of(level), i32(count), i32(entries.length),
      ...entries.flatMap(entry => [i32(entry.keyBytes.length), entry.keyBytes, i32(entry.valueBytes.length), entry.valueBytes]),
      ...children.map(child => (child?.digest ?? this.policy.emptyDigest).toBytes()),
    ]);
  }

  private updateValue(node: MerkleNode<K, V>, key: K, replacement: MerkleStoredEntry<K, V>): MerkleNode<K, V> {
    const position = lowerBound(node.entries, key, this.policy.comparer);
    if (position < node.entries.length && this.policy.comparer(node.entries[position]!.key, key) === 0) {
      const entries = [...node.entries]; entries[position] = replacement; return this.newNode(node.level, entries, node.children);
    }
    const children = [...node.children]; children[position] = this.updateValue(children[position]!, key, replacement); return this.newNode(node.level, node.entries, children);
  }

  private insert(node: MerkleNode<K, V> | undefined, item: MerkleStoredEntry<K, V>): MerkleNode<K, V> {
    if (node === undefined) return this.newNode(item.level, [item], [undefined, undefined]);
    if (item.level > node.level) { const split = this.split(node, item.key); return this.newNode(item.level, [item], [split.left, split.right]); }
    const position = lowerBound(node.entries, item.key, this.policy.comparer);
    if (item.level < node.level) { const children = [...node.children]; children[position] = this.insert(children[position], item); return this.newNode(node.level, node.entries, children); }
    const split = this.split(node.children[position], item.key);
    return this.newNode(node.level, [...node.entries.slice(0, position), item, ...node.entries.slice(position)], [...node.children.slice(0, position), split.left, split.right, ...node.children.slice(position + 1)]);
  }

  private split(node: MerkleNode<K, V> | undefined, key: K): { readonly left: MerkleNode<K, V> | undefined; readonly right: MerkleNode<K, V> | undefined } {
    if (node === undefined) return { left: undefined, right: undefined };
    const position = lowerBound(node.entries, key, this.policy.comparer), child = this.split(node.children[position], key);
    return {
      left: this.createOrCollapse(node.level, node.entries.slice(0, position), [...node.children.slice(0, position), child.left]),
      right: this.createOrCollapse(node.level, node.entries.slice(position), [child.right, ...node.children.slice(position + 1)]),
    };
  }

  private removeNode(node: MerkleNode<K, V> | undefined, key: K): { readonly node: MerkleNode<K, V> | undefined; readonly removed: boolean } {
    if (node === undefined) return { node, removed: false };
    const position = lowerBound(node.entries, key, this.policy.comparer), found = position < node.entries.length && this.policy.comparer(node.entries[position]!.key, key) === 0;
    if (!found) {
      const result = this.removeNode(node.children[position], key); if (!result.removed) return { node, removed: false };
      const children = [...node.children]; children[position] = result.node; return { node: this.newNode(node.level, node.entries, children), removed: true };
    }
    const entries = [...node.entries.slice(0, position), ...node.entries.slice(position + 1)];
    const children = [...node.children.slice(0, position), this.join(node.children[position], node.children[position + 1]), ...node.children.slice(position + 2)];
    return { node: this.createOrCollapse(node.level, entries, children), removed: true };
  }

  private join(left: MerkleNode<K, V> | undefined, right: MerkleNode<K, V> | undefined): MerkleNode<K, V> | undefined {
    if (left === undefined) return right; if (right === undefined) return left;
    if (left.level > right.level) { const children = [...left.children]; children[children.length - 1] = this.join(children.at(-1), right); return this.newNode(left.level, left.entries, children); }
    if (left.level < right.level) { const children = [...right.children]; children[0] = this.join(left, children[0]); return this.newNode(right.level, right.entries, children); }
    return this.newNode(left.level, [...left.entries, ...right.entries], [...left.children.slice(0, -1), this.join(left.children.at(-1), right.children[0]), ...right.children.slice(1)]);
  }

  private createOrCollapse(level: number, entries: readonly MerkleStoredEntry<K, V>[], children: readonly (MerkleNode<K, V> | undefined)[]): MerkleNode<K, V> | undefined {
    return entries.length === 0 ? children[0] : this.newNode(level, entries, children);
  }

  private *nodesPreorder(): IterableIterator<MerkleNode<K, V>> {
    if (this.root === undefined) return;
    const pending = [this.root];
    while (pending.length > 0) { const node = pending.pop()!; yield node; for (let index = node.children.length - 1; index >= 0; index--) if (node.children[index] !== undefined) pending.push(node.children[index]!); }
  }
}
