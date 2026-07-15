import { MerkleDigest, MerkleSearchTreePolicy } from "./merkle-encoding.js";
import { MerkleSearchTree, type MerkleNode, type MerkleStoredEntry } from "./merkle-search-tree.js";

function bytesEqual(left: Uint8Array, right: Uint8Array): boolean {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

export type MerklePersistenceFailure =
  | "conflicting-block" | "missing-block" | "digest-mismatch" | "invalid-block"
  | "policy-mismatch" | "budget-exceeded" | "cycle" | "invalid-structure";

export class MerklePersistenceError extends Error {
  constructor(readonly kind: MerklePersistenceFailure, message: string) { super(message); this.name = "MerklePersistenceError"; }
}

export class MerkleBlock {
  readonly #content: Uint8Array;
  constructor(readonly digest: MerkleDigest, content: Uint8Array) { this.#content = content.slice(); }
  get content(): Uint8Array { return this.#content.slice(); }
  equals(other: MerkleBlock): boolean { return this.digest.equals(other.digest) && bytesEqual(this.#content, other.#content); }
}

export interface MerkleBlockStore {
  get(digest: MerkleDigest): MerkleBlock | undefined;
  put(block: MerkleBlock): boolean;
  has(digest: MerkleDigest): boolean;
  digests(): readonly MerkleDigest[];
}

export class InMemoryMerkleBlockStore implements MerkleBlockStore {
  readonly #blocks = new Map<string, MerkleBlock>();
  get count(): number { return this.#blocks.size; }
  get(digest: MerkleDigest): MerkleBlock | undefined {
    const block = this.#blocks.get(digest.toString());
    return block === undefined ? undefined : new MerkleBlock(block.digest, block.content);
  }
  has(digest: MerkleDigest): boolean { return this.#blocks.has(digest.toString()); }
  put(block: MerkleBlock): boolean {
    const key = block.digest.toString(), existing = this.#blocks.get(key);
    if (existing !== undefined) {
      if (!existing.equals(block)) throw new MerklePersistenceError("conflicting-block", `Different bytes already occupy digest ${key}.`);
      return false;
    }
    this.#blocks.set(key, new MerkleBlock(block.digest, block.content));
    return true;
  }
  digests(): readonly MerkleDigest[] { return [...this.#blocks.values()].map(block => block.digest).sort((a, b) => a.compareTo(b)); }
}

export class MerkleBlockPack {
  readonly blocks: readonly MerkleBlock[];
  constructor(
    readonly algorithmId: string,
    readonly domainDigest: MerkleDigest,
    readonly rootHash: MerkleDigest,
    blocks: Iterable<MerkleBlock>,
  ) {
    const unique = new Map<string, MerkleBlock>();
    for (const block of blocks) {
      const key = block.digest.toString(), previous = unique.get(key);
      if (previous !== undefined && !previous.equals(block)) throw new MerklePersistenceError("conflicting-block", `Pack repeats ${key} with different content.`);
      if (previous === undefined) unique.set(key, new MerkleBlock(block.digest, block.content));
    }
    this.blocks = [...unique.values()];
  }
  get blockCount(): number { return this.blocks.length; }
}

export interface MerkleVerificationBudgetOptions {
  readonly maximumBlocks?: number;
  readonly maximumTotalBytes?: number;
  readonly maximumBlockBytes?: number;
  readonly maximumDepth?: number;
  readonly maximumEntries?: number;
  readonly maximumChildrenPerBlock?: number;
  readonly maximumQueryBytes?: number;
}

export class MerkleVerificationBudget {
  readonly maximumBlocks: number;
  readonly maximumTotalBytes: number;
  readonly maximumBlockBytes: number;
  readonly maximumDepth: number;
  readonly maximumEntries: number;
  readonly maximumChildrenPerBlock: number;
  readonly maximumQueryBytes: number;
  constructor(options: MerkleVerificationBudgetOptions = {}) {
    this.maximumBlocks = options.maximumBlocks ?? 1_000_000;
    this.maximumTotalBytes = options.maximumTotalBytes ?? 1_073_741_824;
    this.maximumBlockBytes = options.maximumBlockBytes ?? 16_777_216;
    this.maximumDepth = options.maximumDepth ?? 256;
    this.maximumEntries = options.maximumEntries ?? 100_000_000;
    this.maximumChildrenPerBlock = options.maximumChildrenPerBlock ?? 65_536;
    this.maximumQueryBytes = options.maximumQueryBytes ?? this.maximumBlockBytes;
    for (const value of Object.values(this)) if (!Number.isSafeInteger(value) || value <= 0) throw new RangeError("Merkle verification limits must be positive finite safe integers.");
    if (this.maximumBlockBytes > this.maximumTotalBytes || this.maximumQueryBytes > this.maximumTotalBytes) throw new RangeError("Per-item byte limits cannot exceed total bytes.");
  }
}

/** @internal */
export interface DecodedMerkleBlock<K, V> {
  readonly level: number;
  readonly count: number;
  readonly entries: readonly MerkleStoredEntry<K, V>[];
  readonly children: readonly MerkleDigest[];
}

class Reader {
  offset = 0;
  constructor(readonly bytes: Uint8Array) {}
  take(length: number): Uint8Array {
    if (!Number.isSafeInteger(length) || length < 0 || this.offset + length > this.bytes.length) throw new MerklePersistenceError("invalid-block", "Truncated or invalid-length MST2 block.");
    const result = this.bytes.slice(this.offset, this.offset + length); this.offset += length; return result;
  }
  byte(): number { return this.take(1)[0]!; }
  int32(): number { const bytes = this.take(4); return new DataView(bytes.buffer, bytes.byteOffset, 4).getInt32(0, false); }
}

/** @internal */
export class MerkleVerificationContext {
  readonly seen: Set<string> = new Set<string>();
  blocks = 0; bytes = 0; entries = 0;
  constructor(readonly budget: MerkleVerificationBudget) {}
  account(block: MerkleBlock, depth: number): boolean {
    const key = block.digest.toString();
    if (depth > this.budget.maximumDepth) throw new MerklePersistenceError("budget-exceeded", "Maximum Merkle depth exceeded.");
    if (block.content.length > this.budget.maximumBlockBytes) throw new MerklePersistenceError("budget-exceeded", "Maximum Merkle block size exceeded.");
    if (this.seen.has(key)) return false;
    this.seen.add(key); this.blocks++; this.bytes += block.content.length;
    if (this.blocks > this.budget.maximumBlocks || this.bytes > this.budget.maximumTotalBytes) throw new MerklePersistenceError("budget-exceeded", "Merkle closure budget exceeded.");
    return true;
  }
}

/** @internal */
export function decodeMerkleBlock<K, V>(block: MerkleBlock, tree: MerkleSearchTree<K, V>, context: MerkleVerificationContext, depth: number): DecodedMerkleBlock<K, V> {
  context.account(block, depth);
  const content = block.content;
  if (!MerkleDigest.hash(content).equals(block.digest)) throw new MerklePersistenceError("digest-mismatch", `Block ${block.digest} does not hash to its claimed address.`);
  const reader = new Reader(content);
  if (!bytesEqual(reader.take(4), Uint8Array.from([0x4d, 0x53, 0x54, 0x32])) || reader.byte() !== 1) throw new MerklePersistenceError("invalid-block", "Invalid MST2 node magic or tag.");
  if (!MerkleDigest.fromBytes(reader.take(32)).equals(tree.policy.domainDigest)) throw new MerklePersistenceError("policy-mismatch", "MST2 block belongs to another policy domain.");
  const level = reader.byte(), count = reader.int32(), entryCount = reader.int32();
  if (count <= 0 || entryCount <= 0 || entryCount > count) throw new MerklePersistenceError("invalid-block", "Invalid MST2 counts.");
  const childCount = entryCount + 1;
  if (childCount > context.budget.maximumChildrenPerBlock) throw new MerklePersistenceError("budget-exceeded", "Maximum child references per block exceeded.");
  context.entries += entryCount;
  if (context.entries > context.budget.maximumEntries) throw new MerklePersistenceError("budget-exceeded", "Maximum decoded entries exceeded.");
  const entries: MerkleStoredEntry<K, V>[] = [];
  for (let index = 0; index < entryCount; index++) {
    const keyLength = reader.int32(); if (keyLength < 0 || keyLength > context.budget.maximumBlockBytes) throw new MerklePersistenceError("invalid-block", "Invalid MST2 key length.");
    const keyBytes = reader.take(keyLength), valueLength = reader.int32();
    if (valueLength < 0 || valueLength > context.budget.maximumBlockBytes) throw new MerklePersistenceError("invalid-block", "Invalid MST2 value length.");
    const valueBytes = reader.take(valueLength);
    let key: K, value: V;
    try { key = tree.policy.keyCodec.decode(keyBytes); value = tree.policy.valueCodec.decode(valueBytes); }
    catch (cause) { throw new MerklePersistenceError("invalid-block", `A canonical codec rejected MST2 content: ${String(cause)}`); }
    if (!bytesEqual(tree.policy.keyCodec.encode(key), keyBytes) || !bytesEqual(tree.policy.valueCodec.encode(value), valueBytes)) throw new MerklePersistenceError("invalid-block", "MST2 codec content is not canonical under re-encoding.");
    const entry = tree.createVerifiedEntry(key, value, keyBytes, valueBytes);
    if (entry.level !== level) throw new MerklePersistenceError("invalid-block", "MST2 separator does not hash to the declared level.");
    if (index > 0 && tree.policy.comparer(entries[index - 1]!.key, entry.key) >= 0) throw new MerklePersistenceError("invalid-block", "MST2 separators are not strictly ordered.");
    entries.push(entry);
  }
  const children = Array.from({ length: childCount }, () => MerkleDigest.fromBytes(reader.take(32)));
  if (reader.offset !== content.length) throw new MerklePersistenceError("invalid-block", "Trailing bytes follow the MST2 block.");
  return { level, count, entries, children };
}

function ensureEnvelope<K, V>(algorithmId: string, domain: MerkleDigest, policy: MerkleSearchTreePolicy<K, V>): void {
  if (algorithmId !== MerkleSearchTreePolicy.algorithmId || !domain.equals(policy.domainDigest)) throw new MerklePersistenceError("policy-mismatch", "Merkle envelope algorithm or domain differs from the policy.");
}

function loadWithStore<K, V>(rootHash: MerkleDigest, policy: MerkleSearchTreePolicy<K, V>, store: MerkleBlockStore, budget: MerkleVerificationBudget, context = new MerkleVerificationContext(budget)): MerkleSearchTree<K, V> {
  const shell = MerkleSearchTree.empty(policy);
  if (rootHash.equals(policy.emptyDigest)) return shell;
  const memo = new Map<string, MerkleNode<K, V>>(), active = new Set<string>();
  const loadNode = (digest: MerkleDigest, depth: number, minimum?: K, maximum?: K, parentLevel = 65): MerkleNode<K, V> | undefined => {
    if (digest.equals(policy.emptyDigest)) return undefined;
    const key = digest.toString(), cached = memo.get(key); if (cached !== undefined) return cached;
    if (active.has(key)) throw new MerklePersistenceError("cycle", `Cycle detected through ${key}.`);
    const block = store.get(digest); if (block === undefined) throw new MerklePersistenceError("missing-block", `Missing Merkle block ${key}.`);
    active.add(key);
    try {
      const decoded = decodeMerkleBlock(block, shell, context, depth);
      if (decoded.level >= parentLevel) throw new MerklePersistenceError("invalid-structure", "Merkle child levels must strictly decrease.");
      for (const entry of decoded.entries) {
        if (minimum !== undefined && policy.comparer(entry.key, minimum) <= 0 || maximum !== undefined && policy.comparer(entry.key, maximum) >= 0) throw new MerklePersistenceError("invalid-structure", "Merkle entry violates its parent interval.");
      }
      const children = decoded.children.map((child, index) => loadNode(child, depth + 1, index === 0 ? minimum : decoded.entries[index - 1]!.key, index === decoded.entries.length ? maximum : decoded.entries[index]!.key, decoded.level));
      const node = shell.createVerifiedNode(decoded.level, decoded.entries, children);
      if (node.count !== decoded.count || !node.digest.equals(digest) || !bytesEqual(node.blockBytes, block.content)) throw new MerklePersistenceError("invalid-structure", "MST2 count or exact reserialization differs.");
      memo.set(key, node); return node;
    } finally { active.delete(key); }
  };
  const tree = MerkleSearchTree.fromVerifiedRoot(policy, loadNode(rootHash, 1));
  try { tree.validateStructure(); } catch (cause) { throw new MerklePersistenceError("invalid-structure", String(cause)); }
  return tree;
}

export function exportMerklePack<K, V>(tree: MerkleSearchTree<K, V>, digests?: Iterable<MerkleDigest>): MerkleBlockPack {
  const blocks = [...tree.blocksPreorder()].map(block => new MerkleBlock(block.digest, block.bytes));
  if (digests === undefined) return new MerkleBlockPack(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, blocks);
  const byDigest = new Map(blocks.map(block => [block.digest.toString(), block]));
  const requested: MerkleBlock[] = [];
  for (const digest of digests) { const block = byDigest.get(digest.toString()); if (block === undefined) throw new MerklePersistenceError("missing-block", `Tree does not contain block ${digest}.`); requested.push(block); }
  return new MerkleBlockPack(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, requested);
}

export function saveMerkleTree<K, V>(tree: MerkleSearchTree<K, V>, store: MerkleBlockStore): number {
  const blocks = exportMerklePack(tree).blocks;
  for (const block of blocks) { const previous = store.get(block.digest); if (previous !== undefined && !previous.equals(block)) throw new MerklePersistenceError("conflicting-block", `Store conflicts at ${block.digest}.`); }
  let written = 0; for (const block of blocks) if (store.put(block)) written++; return written;
}

export function loadMerkleTree<K, V>(rootHash: MerkleDigest, policy: MerkleSearchTreePolicy<K, V>, store: MerkleBlockStore, budget: MerkleVerificationBudget = new MerkleVerificationBudget()): MerkleSearchTree<K, V> {
  return loadWithStore(rootHash, policy, store, budget);
}

export function importMerklePack<K, V>(pack: MerkleBlockPack, policy: MerkleSearchTreePolicy<K, V>, destination: MerkleBlockStore = new InMemoryMerkleBlockStore(), budget: MerkleVerificationBudget = new MerkleVerificationBudget()): MerkleSearchTree<K, V> {
  ensureEnvelope(pack.algorithmId, pack.domainDigest, policy);
  const staged = new Map<string, MerkleBlock>();
  const shell = MerkleSearchTree.empty(policy), context = new MerkleVerificationContext(budget);
  for (const block of pack.blocks) { decodeMerkleBlock(block, shell, context, 1); staged.set(block.digest.toString(), block); }
  const overlay: MerkleBlockStore = {
    get: digest => staged.get(digest.toString()) ?? destination.get(digest),
    has: digest => staged.has(digest.toString()) || destination.has(digest),
    put: () => { throw new Error("Read-only verification overlay."); },
    digests: () => [],
  };
  const tree = loadWithStore(pack.rootHash, policy, overlay, budget, context);
  for (const block of pack.blocks) { const previous = destination.get(block.digest); if (previous !== undefined && !previous.equals(block)) throw new MerklePersistenceError("conflicting-block", `Destination conflicts at ${block.digest}.`); }
  for (const block of pack.blocks) destination.put(block);
  return tree;
}

export interface MerkleSyncPlan {
  readonly targetRoot: MerkleDigest;
  readonly rootsMatch: boolean;
  readonly requestedDigests: readonly MerkleDigest[];
  readonly examinedBlocks: number;
  readonly examinedBytes: number;
  readonly requiresBlocks: boolean;
}

export function createMerkleSyncPack<K, V>(tree: MerkleSearchTree<K, V>, receiver: MerkleBlockStore): MerkleBlockPack {
  const missing: MerkleBlock[] = [];
  const walk = (node: MerkleNode<K, V> | undefined): void => {
    if (node === undefined || receiver.has(node.digest)) return;
    missing.push(new MerkleBlock(node.digest, node.blockBytes)); for (const child of node.children) walk(child);
  };
  walk(tree.getRootForPersistence());
  return new MerkleBlockPack(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, missing);
}

export function planMerkleSync<K, V>(target: MerkleSearchTree<K, V>, publishedLocal: MerkleSearchTree<K, V>, receiver: MerkleBlockStore): MerkleSyncPlan {
  if (!target.policy.compatibleWith(publishedLocal.policy)) throw new MerklePersistenceError("policy-mismatch", "Cannot synchronize different Merkle domains.");
  const requested: MerkleDigest[] = []; let examinedBlocks = 0, examinedBytes = 0;
  const walk = (node: MerkleNode<K, V> | undefined): void => {
    if (node === undefined) return; examinedBlocks++; examinedBytes += node.blockBytes.length;
    if (!receiver.has(node.digest)) { requested.push(node.digest); return; }
    for (const child of node.children) walk(child);
  };
  walk(target.getRootForPersistence());
  return { targetRoot: target.rootHash, rootsMatch: target.rootHash.equals(publishedLocal.rootHash), requestedDigests: requested, examinedBlocks, examinedBytes, requiresBlocks: requested.length > 0 };
}
