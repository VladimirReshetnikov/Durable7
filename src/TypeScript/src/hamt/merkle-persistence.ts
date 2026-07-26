/**
 * Block storage and verification for Merkle search trees.
 *
 * A tree decomposes into content-addressed blocks; loading one re-derives every block's digest from
 * its bytes, so a corrupted or substituted block is rejected rather than trusted. Verification runs
 * against an explicit budget so untrusted input cannot force unbounded work.
 */
import { MerkleDigest, MerkleSearchTreePolicy } from "./merkle-encoding.js";
import { MerkleSearchTree, type MerkleNode, type MerkleStoredEntry } from "./merkle-search-tree.js";

function bytesEqual(left: Uint8Array, right: Uint8Array): boolean {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

/**
 * Why untrusted persistence input was rejected. Naming the cause lets a caller tell corrupt or
 * over-budget input from input that merely belongs to a different policy domain.
 */
export type MerklePersistenceFailure =
  | "conflicting-block" | "missing-block" | "digest-mismatch" | "invalid-block"
  | "policy-mismatch" | "budget-exceeded" | "cycle" | "invalid-structure";

/** A typed rejection of untrusted Merkle persistence input. */
export class MerklePersistenceError extends Error {
  constructor(readonly kind: MerklePersistenceFailure, message: string) { super(message); this.name = "MerklePersistenceError"; }
}

/** One content-addressed block: its digest and the exact bytes that hash to it. */
export class MerkleBlock {
  readonly #content: Uint8Array;
  constructor(readonly digest: MerkleDigest, content: Uint8Array) { this.#content = content.slice(); }
  /** The block's exact bytes. */
  get content(): Uint8Array { return this.#content.slice(); }
  /** Whether both blocks have the same digest and bytes. */
  equals(other: MerkleBlock): boolean { return this.digest.equals(other.digest) && bytesEqual(this.#content, other.#content); }
}

/**
 * A content-addressed store holding the blocks a Merkle tree decomposes into. Blocks are immutable
 * and keyed by their own digest, so a store never reconciles two versions of one block: either the
 * bytes match what is stored or the input is bad.
 */
export interface MerkleBlockStore {
  /** The value stored for the key, or `undefined` when absent. */
  get(digest: MerkleDigest): MerkleBlock | undefined;
  /** A collection containing the given element, keeping the stored representative when present. */
  put(block: MerkleBlock): boolean;
  /** Whether the key is present. */
  has(digest: MerkleDigest): boolean;
  /** Every stored digest. */
  digests(): readonly MerkleDigest[];
}

/** An in-memory block store with atomic multi-block publication. */
export class InMemoryMerkleBlockStore implements MerkleBlockStore {
  readonly #blocks = new Map<string, MerkleBlock>();
  /** Number of elements. */
  get count(): number { return this.#blocks.size; }
  /** The value stored for the key, or `undefined` when absent. */
  get(digest: MerkleDigest): MerkleBlock | undefined {
    const block = this.#blocks.get(digest.toString());
    return block === undefined ? undefined : new MerkleBlock(block.digest, block.content);
  }
  /** Whether the key is present. */
  has(digest: MerkleDigest): boolean { return this.#blocks.has(digest.toString()); }
  /** A collection containing the given element, keeping the stored representative when present. */
  put(block: MerkleBlock): boolean {
    const key = block.digest.toString(), existing = this.#blocks.get(key);
    if (existing !== undefined) {
      if (!existing.equals(block)) throw new MerklePersistenceError("conflicting-block", `Different bytes already occupy digest ${key}.`);
      return false;
    }
    this.#blocks.set(key, new MerkleBlock(block.digest, block.content));
    return true;
  }
  /** Every stored digest. */
  digests(): readonly MerkleDigest[] { return [...this.#blocks.values()].map(block => block.digest).sort((a, b) => a.compareTo(b)); }
}

/**
 * A transferable bundle of blocks tagged with the tree they describe. The algorithm and domain
 * identifiers travel with the blocks so a receiver can reject a pack built under a different
 * policy.
 */
export class MerkleBlockPack {
  /** The blocks carried by the pack, deduplicated by digest. */
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
  /** Number of blocks the tree encodes into. */
  get blockCount(): number { return this.blocks.length; }
}

/** Overrides for the verification ceilings; anything omitted keeps its default. */
export interface MerkleVerificationBudgetOptions {
  readonly maximumBlocks?: number;
  readonly maximumTotalBytes?: number;
  readonly maximumBlockBytes?: number;
  readonly maximumDepth?: number;
  readonly maximumEntries?: number;
  readonly maximumChildrenPerBlock?: number;
  readonly maximumQueryBytes?: number;
}

/**
 * Ceilings bounding the work untrusted Merkle input may force. Without them a crafted pack or proof
 * could demand unbounded memory or time, so every limit is charged during verification and
 * exceeding one fails the operation.
 */
export class MerkleVerificationBudget {
  /** Most blocks a single verification may account for. */
  readonly maximumBlocks: number;
  /** Most bytes a single verification may account for. */
  readonly maximumTotalBytes: number;
  /** Largest single block that will be accepted. */
  readonly maximumBlockBytes: number;
  /** Deepest chain of blocks that will be followed, bounding what a crafted input can force. */
  readonly maximumDepth: number;
  /** Most decoded entries a single verification may account for. */
  readonly maximumEntries: number;
  /** Most children a single block may declare. */
  readonly maximumChildrenPerBlock: number;
  /** Largest proof query that will be accepted. */
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
  /** The block's level in the canonical wide tree. */
  readonly level: number;
  /** Number of elements. */
  readonly count: number;
  /** Iterate the elements. */
  readonly entries: readonly MerkleStoredEntry<K, V>[];
  /** The digests of the block's children. */
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
  /** Digests already accounted for, so a shared subtree is not charged twice. */
  readonly seen: Set<string> = new Set<string>();
  /** How many blocks have been charged so far. */
  blocks = 0; bytes = 0; entries = 0;
  constructor(readonly budget: MerkleVerificationBudget) {}
  /** Charge one block against the budget, reporting whether it had not already been counted. */
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

/**
 * Bundle a tree's blocks into a pack, or just the requested digests. Requesting a digest the tree
 * lacks is rejected rather than silently omitted.
 */
export function exportMerklePack<K, V>(tree: MerkleSearchTree<K, V>, digests?: Iterable<MerkleDigest>): MerkleBlockPack {
  const blocks = [...tree.blocksPreorder()].map(block => new MerkleBlock(block.digest, block.bytes));
  if (digests === undefined) return new MerkleBlockPack(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, blocks);
  const byDigest = new Map(blocks.map(block => [block.digest.toString(), block]));
  const requested: MerkleBlock[] = [];
  for (const digest of digests) { const block = byDigest.get(digest.toString()); if (block === undefined) throw new MerklePersistenceError("missing-block", `Tree does not contain block ${digest}.`); requested.push(block); }
  return new MerkleBlockPack(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, requested);
}

/**
 * Write every block of the tree into the store, reporting how many were newly written. Conflicting
 * bytes at an existing digest abort before anything is published.
 */
export function saveMerkleTree<K, V>(tree: MerkleSearchTree<K, V>, store: MerkleBlockStore): number {
  const blocks = exportMerklePack(tree).blocks;
  for (const block of blocks) { const previous = store.get(block.digest); if (previous !== undefined && !previous.equals(block)) throw new MerklePersistenceError("conflicting-block", `Store conflicts at ${block.digest}.`); }
  let written = 0; for (const block of blocks) if (store.put(block)) written++; return written;
}

/**
 * Rebuild the tree with the given root digest from the store, verifying every block. Each digest is
 * recomputed from its bytes, so a corrupted or substituted block is rejected rather than trusted.
 */
export function loadMerkleTree<K, V>(rootHash: MerkleDigest, policy: MerkleSearchTreePolicy<K, V>, store: MerkleBlockStore, budget: MerkleVerificationBudget = new MerkleVerificationBudget()): MerkleSearchTree<K, V> {
  return loadWithStore(rootHash, policy, store, budget);
}

/**
 * Verify a received pack and publish its blocks, returning the tree it describes. The pack is
 * verified in full before anything is written, so a bad pack leaves the destination untouched.
 */
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

/**
 * What a receiver still needs in order to reach a target root. The examined counts report how much
 * of the tree the comparison had to walk, which stays small when the two sides mostly agree.
 */
export interface MerkleSyncPlan {
  /** The root the receiver is being brought to. */
  readonly targetRoot: MerkleDigest;
  /** Whether the receiver already holds the target root, making the transfer empty. */
  readonly rootsMatch: boolean;
  /** The blocks the receiver is missing. */
  readonly requestedDigests: readonly MerkleDigest[];
  /** How many blocks the comparison had to inspect. */
  readonly examinedBlocks: number;
  /** How many bytes the comparison had to inspect. */
  readonly examinedBytes: number;
  /** Whether the receiver is missing anything at all. */
  readonly requiresBlocks: boolean;
}

/**
 * Bundle exactly the blocks the receiver does not already hold. A subtree the receiver already has
 * is skipped whole on one digest lookup, so the pack is proportional to the difference rather than
 * to the tree.
 */
export function createMerkleSyncPack<K, V>(tree: MerkleSearchTree<K, V>, receiver: MerkleBlockStore): MerkleBlockPack {
  const missing: MerkleBlock[] = [];
  const walk = (node: MerkleNode<K, V> | undefined): void => {
    if (node === undefined || receiver.has(node.digest)) return;
    missing.push(new MerkleBlock(node.digest, node.blockBytes)); for (const child of node.children) walk(child);
  };
  walk(tree.getRootForPersistence());
  return new MerkleBlockPack(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, missing);
}

/**
 * Work out which blocks a receiver needs, without sending any. Descends only where the roots
 * disagree, so matching subtrees cost one digest comparison regardless of size.
 */
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
