import { MerkleDigest, MerkleEncodingInternals, MerkleSearchTreePolicy } from "./merkle-encoding.js";
import { MerkleSearchTree, type MerkleEntry, type MerkleNode, type MerkleStoredEntry } from "./merkle-search-tree.js";
import { decodeMerkleBlock, MerkleBlock, MerkleVerificationBudget, MerkleVerificationContext } from "./merkle-persistence.js";

const { concat, i32 } = MerkleEncodingInternals;

export type MerkleProofQuery<K, V> =
  | { readonly kind: "membership"; readonly key: K; readonly value: V }
  | { readonly kind: "nonmembership"; readonly key: K }
  | { readonly kind: "range"; readonly minimum: K; readonly maximum: K };

export class MerkleProofStep {
  readonly expandedChildIndexes: readonly number[];
  constructor(readonly block: MerkleBlock, indexes: Iterable<number>) {
    const values = [...indexes];
    if (values.some((value, index) => !Number.isSafeInteger(value) || value < 0 || index > 0 && values[index - 1]! >= value)) throw new RangeError("Expanded child indexes must be sorted, unique, nonnegative integers.");
    this.expandedChildIndexes = values;
  }
}

export class MerkleProof<K, V> {
  readonly queryBytes: Uint8Array;
  readonly steps: readonly MerkleProofStep[];
  constructor(
    readonly algorithmId: string,
    readonly domainDigest: MerkleDigest,
    readonly rootHash: MerkleDigest,
    readonly query: MerkleProofQuery<K, V>,
    queryBytes: Uint8Array,
    steps: Iterable<MerkleProofStep>,
  ) { this.queryBytes = queryBytes.slice(); this.steps = [...steps]; }
}

export type MerkleProofFailure = "none" | "invalid-envelope" | "budget-exceeded" | "invalid-query" | "invalid-block" | "invalid-expansion" | "wrong-result";
export interface MerkleProofVerificationResult<K, V> {
  readonly valid: boolean;
  readonly failure: MerkleProofFailure;
  readonly computedRootHash: MerkleDigest;
  readonly entries: readonly MerkleEntry<K, V>[];
  readonly accountedBlocks: number;
  readonly accountedBytes: number;
}

function equalBytes(left: Uint8Array, right: Uint8Array): boolean { return left.length === right.length && left.every((value, index) => value === right[index]); }

function encodeQuery<K, V>(query: MerkleProofQuery<K, V>, policy: MerkleSearchTreePolicy<K, V>): Uint8Array {
  const magic = Uint8Array.from([0x4d, 0x53, 0x50, 0x32]);
  if (query.kind === "membership") {
    const key = policy.keyCodec.encode(query.key), value = policy.valueCodec.encode(query.value);
    return concat([magic, Uint8Array.of(0), i32(key.length), key, i32(value.length), value]);
  }
  if (query.kind === "nonmembership") {
    const key = policy.keyCodec.encode(query.key); return concat([magic, Uint8Array.of(1), i32(key.length), key]);
  }
  const minimum = policy.keyCodec.encode(query.minimum), maximum = policy.keyCodec.encode(query.maximum);
  return concat([magic, Uint8Array.of(2), i32(minimum.length), minimum, i32(maximum.length), maximum]);
}

function position<K, V>(entries: readonly MerkleStoredEntry<K, V>[], key: K, comparer: (left: K, right: K) => number): { readonly index: number; readonly found: boolean } {
  let low = 0, high = entries.length;
  while (low < high) { const middle = (low + high) >>> 1; if (comparer(entries[middle]!.key, key) < 0) low = middle + 1; else high = middle; }
  return { index: low, found: low < entries.length && comparer(entries[low]!.key, key) === 0 };
}

function selectedChildren<K, V>(query: MerkleProofQuery<K, V>, entries: readonly MerkleStoredEntry<K, V>[], children: readonly MerkleDigest[], policy: MerkleSearchTreePolicy<K, V>): readonly number[] {
  if (query.kind !== "range") {
    const found = position(entries, query.key, policy.comparer); return !found.found && !children[found.index]!.equals(policy.emptyDigest) ? [found.index] : [];
  }
  const result: number[] = [];
  for (let index = 0; index < children.length; index++) {
    if (children[index]!.equals(policy.emptyDigest)) continue;
    const aboveLower = index === 0 || policy.comparer(query.maximum, entries[index - 1]!.key) > 0;
    const belowUpper = index === entries.length || policy.comparer(query.minimum, entries[index]!.key) < 0;
    if (aboveLower && belowUpper) result.push(index);
  }
  return result;
}

function createProof<K, V>(tree: MerkleSearchTree<K, V>, query: MerkleProofQuery<K, V>): MerkleProof<K, V> {
  const steps: MerkleProofStep[] = [];
  const walk = (node: MerkleNode<K, V> | undefined): void => {
    if (node === undefined) return;
    const digests = node.children.map(child => child?.digest ?? tree.policy.emptyDigest);
    const expanded = selectedChildren(query, node.entries, digests, tree.policy);
    steps.push(new MerkleProofStep(new MerkleBlock(node.digest, node.blockBytes), expanded));
    for (const index of expanded) walk(node.children[index]);
  };
  walk(tree.getRootForPersistence());
  return new MerkleProof(MerkleSearchTreePolicy.algorithmId, tree.policy.domainDigest, tree.rootHash, query, encodeQuery(query, tree.policy), steps);
}

export function createMerkleProof<K, V>(tree: MerkleSearchTree<K, V>, key: K): MerkleProof<K, V> {
  const entry = tree.getEntry(key);
  return createProof(tree, entry === undefined ? { kind: "nonmembership", key } : { kind: "membership", key: entry.key, value: entry.value });
}

export function createMerkleRangeProof<K, V>(tree: MerkleSearchTree<K, V>, minimum: K, maximum: K): MerkleProof<K, V> {
  if (tree.policy.comparer(minimum, maximum) > 0) throw new RangeError("Minimum key exceeds maximum key.");
  return createProof(tree, { kind: "range", minimum, maximum });
}

export function verifyMerkleProof<K, V>(proof: MerkleProof<K, V>, policy: MerkleSearchTreePolicy<K, V>, budget: MerkleVerificationBudget = new MerkleVerificationBudget()): MerkleProofVerificationResult<K, V> {
  let context = new MerkleVerificationContext(budget);
  const fail = (failure: MerkleProofFailure): MerkleProofVerificationResult<K, V> => ({ valid: false, failure, computedRootHash: proof.rootHash, entries: [], accountedBlocks: context.blocks, accountedBytes: context.bytes });
  try {
    if (proof.queryBytes.length > budget.maximumQueryBytes) return fail("budget-exceeded");
    context.bytes += proof.queryBytes.length;
    if (context.bytes > budget.maximumTotalBytes) return fail("budget-exceeded");
    if (proof.algorithmId !== MerkleSearchTreePolicy.algorithmId || !proof.domainDigest.equals(policy.domainDigest)) return fail("invalid-envelope");
    let canonicalQuery: Uint8Array;
    try { canonicalQuery = encodeQuery(proof.query, policy); } catch { return fail("invalid-query"); }
    if (!equalBytes(canonicalQuery, proof.queryBytes)) return fail("invalid-query");
    if (proof.query.kind === "range" && policy.comparer(proof.query.minimum, proof.query.maximum) > 0) return fail("invalid-query");
    if (proof.rootHash.equals(policy.emptyDigest)) {
      if (proof.steps.length !== 0 || proof.query.kind === "membership") return fail("wrong-result");
      return { valid: true, failure: "none", computedRootHash: proof.rootHash, entries: [], accountedBlocks: 0, accountedBytes: context.bytes };
    }
    const shell = MerkleSearchTree.empty(policy), steps = new Map<string, MerkleProofStep>();
    for (const step of proof.steps) {
      const key = step.block.digest.toString(); if (steps.has(key)) return fail("invalid-expansion"); steps.set(key, step);
    }
    const visited = new Set<string>(), result: MerkleEntry<K, V>[] = [];
    let found: MerkleStoredEntry<K, V> | undefined;
    const walk = (digest: MerkleDigest, depth: number, minimum?: K, maximum?: K, parentLevel = 65): void => {
      const key = digest.toString(), step = steps.get(key); if (step === undefined || visited.has(key)) throw new Error("expansion"); visited.add(key);
      const decoded = decodeMerkleBlock(step.block, shell, context, depth);
      if (decoded.level >= parentLevel) throw new Error("expansion");
      for (const entry of decoded.entries) {
        if (minimum !== undefined && policy.comparer(entry.key, minimum) <= 0 || maximum !== undefined && policy.comparer(entry.key, maximum) >= 0) throw new Error("expansion");
        if (proof.query.kind === "range" && policy.comparer(entry.key, proof.query.minimum) >= 0 && policy.comparer(entry.key, proof.query.maximum) <= 0) result.push({ key: entry.key, value: entry.value });
      }
      if (proof.query.kind !== "range") { const selected = position(decoded.entries, proof.query.key, policy.comparer); if (selected.found) found = decoded.entries[selected.index]; }
      const expected = selectedChildren(proof.query, decoded.entries, decoded.children, policy);
      if (expected.length !== step.expandedChildIndexes.length || expected.some((value, index) => value !== step.expandedChildIndexes[index])) throw new Error("expansion");
      for (const index of expected) walk(decoded.children[index]!, depth + 1, index === 0 ? minimum : decoded.entries[index - 1]!.key, index === decoded.entries.length ? maximum : decoded.entries[index]!.key, decoded.level);
    };
    try { walk(proof.rootHash, 1); } catch (cause) { return fail(String(cause).includes("budget") ? "budget-exceeded" : "invalid-expansion"); }
    if (visited.size !== steps.size) return fail("invalid-expansion");
    if (proof.query.kind === "membership") {
      if (found === undefined || policy.comparer(found.key, proof.query.key) !== 0 || !equalBytes(found.valueBytes, policy.valueCodec.encode(proof.query.value))) return fail("wrong-result");
      result.push({ key: found.key, value: found.value });
    } else if (proof.query.kind === "nonmembership" && found !== undefined) return fail("wrong-result");
    return { valid: true, failure: "none", computedRootHash: proof.rootHash, entries: result, accountedBlocks: context.blocks, accountedBytes: context.bytes };
  } catch (cause) {
    return fail(String(cause).toLowerCase().includes("budget") ? "budget-exceeded" : "invalid-block");
  }
}

export type MerkleMergeValue<V> = { readonly present: false } | { readonly present: true; readonly value: V };
export interface MerkleThreeWayMergeConflict<K, V> {
  readonly key: K;
  readonly base: MerkleMergeValue<V>;
  readonly left: MerkleMergeValue<V>;
  readonly right: MerkleMergeValue<V>;
}
export type MerkleMergeResolution<V> =
  | { readonly kind: "unresolved" }
  | { readonly kind: "base" }
  | { readonly kind: "left" }
  | { readonly kind: "right" }
  | { readonly kind: "delete" }
  | { readonly kind: "value"; readonly value: V };
export interface MerkleThreeWayMergeResult<K, V> {
  readonly succeeded: boolean;
  readonly tree?: MerkleSearchTree<K, V>;
  readonly conflicts: readonly MerkleThreeWayMergeConflict<K, V>[];
}

function mergeValue<V>(entry: MerkleEntry<unknown, V> | undefined): MerkleMergeValue<V> { return entry === undefined ? { present: false } : { present: true, value: entry.value }; }
function mergeEqual<V>(left: MerkleMergeValue<V>, right: MerkleMergeValue<V>, valuesEqual: (left: V, right: V) => boolean): boolean { return left.present === right.present && (!left.present || valuesEqual(left.value, (right as { readonly present: true; readonly value: V }).value)); }

export function mergeMerkleTrees<K, V>(
  base: MerkleSearchTree<K, V>, left: MerkleSearchTree<K, V>, right: MerkleSearchTree<K, V>,
  resolver: (conflict: MerkleThreeWayMergeConflict<K, V>) => MerkleMergeResolution<V> = () => ({ kind: "unresolved" }),
  valuesEqual: (left: V, right: V) => boolean = Object.is,
): MerkleThreeWayMergeResult<K, V> {
  if (!base.policy.compatibleWith(left.policy) || !base.policy.compatibleWith(right.policy)) throw new RangeError("Merkle merge policy domains differ.");
  if (left.rootHash.equals(right.rootHash)) return { succeeded: true, tree: left, conflicts: [] };
  if (base.rootHash.equals(left.rootHash)) return { succeeded: true, tree: right, conflicts: [] };
  if (base.rootHash.equals(right.rootHash)) return { succeeded: true, tree: left, conflicts: [] };
  const all = [...base, ...left, ...right].map(entry => entry.key).sort(base.policy.comparer);
  const keys: K[] = []; for (const key of all) if (keys.length === 0 || base.policy.comparer(keys.at(-1)!, key) !== 0) keys.push(key);
  const merged: [K, V][] = [], conflicts: MerkleThreeWayMergeConflict<K, V>[] = [];
  for (const key of keys) {
    const bEntry = base.getEntry(key), lEntry = left.getEntry(key), rEntry = right.getEntry(key);
    const b = mergeValue(bEntry), l = mergeValue(lEntry), r = mergeValue(rEntry);
    let selected: MerkleMergeValue<V>, representative = lEntry?.key ?? rEntry?.key ?? bEntry!.key;
    if (mergeEqual(l, r, valuesEqual)) selected = l;
    else if (mergeEqual(l, b, valuesEqual)) selected = r;
    else if (mergeEqual(r, b, valuesEqual)) selected = l;
    else {
      const conflict = { key: representative, base: b, left: l, right: r } as const, resolution = resolver(conflict);
      if (resolution.kind === "unresolved") { conflicts.push(conflict); continue; }
      if (resolution.kind === "base") selected = b;
      else if (resolution.kind === "left") selected = l;
      else if (resolution.kind === "right") selected = r;
      else if (resolution.kind === "delete") selected = { present: false };
      else selected = { present: true, value: resolution.value };
    }
    if (selected.present) merged.push([representative, selected.value]);
  }
  return conflicts.length > 0 ? { succeeded: false, conflicts } : { succeeded: true, tree: MerkleSearchTree.from(merged, base.policy), conflicts: [] };
}
