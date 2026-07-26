/**
 * Wire-format conformance tests for the Merkle search tree, plus proofs, storage, synchronization,
 * and merge. These are the tests that keep this port byte-identical to its siblings.
 */
import { describe, expect, test } from "vitest";
import fc from "fast-check";
import {
  InMemoryMerkleBlockStore,
  Int32MerkleCodec,
  MerkleBlock,
  MerkleCodecs,
  MerkleDigest,
  MerklePersistenceError,
  MerkleSearchTree,
  MerkleSearchTreePolicy,
  createMerkleProof,
  createMerkleRangeProof,
  createMerkleSyncPack,
  exportMerklePack,
  importMerklePack,
  loadMerkleTree,
  mergeMerkleTrees,
  planMerkleSync,
  saveMerkleTree,
  verifyMerkleProof,
} from "../../src/hamt/index.js";

const numberOrder = (left: number, right: number): number => left - right;

describe("Merkle codecs and exact MST2 wire", () => {
  test("matches the cross-language int32/string golden block", () => {
    const policy = new MerkleSearchTreePolicy("golden-int-string-v1", numberOrder, MerkleCodecs.int32, MerkleCodecs.utf8String);
    const tree = MerkleSearchTree.empty(policy).set(42, "forty-two");
    const block = [...tree.blocksPreorder()][0]!;
    expect(policy.domainDigest.toString()).toBe("fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2");
    expect(tree.rootHash.toString()).toBe("1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94");
    expect(Buffer.from(block.bytes).toString("hex")).toBe("4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3");
  });

  test("matches the shared wide multi-level vector", () => {
    const policy = new MerkleSearchTreePolicy("golden-wide-i32-i32-v1", numberOrder, Int32MerkleCodec, Int32MerkleCodec);
    const keys = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 38, 44, 59, 464];
    const tree = MerkleSearchTree.from(keys.map(key => [key, -key - 1] as const), policy);
    expect(policy.domainDigest.toString()).toBe("eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917");
    expect(tree.rootHash.toString()).toBe("9afd7ba98ec91f72074c5f2c272ca1334244fb43a631e0fb440e02799eee8755");
    expect([tree.count, tree.blockCount, tree.height]).toEqual([14, 4, 3]);
    expect(tree.validateStructure().count).toBe(14);
  });

  test("strict codecs reject noncanonical representations", () => {
    expect(() => MerkleCodecs.int32.decode(Uint8Array.of(1))).toThrow();
    expect(() => MerkleCodecs.utf8String.decode(Uint8Array.of(0, 1))).toThrow();
    expect(() => MerkleCodecs.utf8String.encode("\ud800")).toThrow();
    expect(() => MerkleDigest.parse("0".repeat(63))).toThrow();
    expect(MerkleCodecs.uuid.decode(MerkleCodecs.uuid.encode("00112233-4455-6677-8899-aabbccddeeff"))).toBe("00112233-4455-6677-8899-aabbccddeeff");
  });
});

describe("Merkle search tree and persistence", () => {
  const policy = new MerkleSearchTreePolicy("typescript-test-i32-string-v1", numberOrder, MerkleCodecs.int32, MerkleCodecs.utf8String);

  test("cursor navigates ranks and publishes canonical persistent edits", () => {
    const source = MerkleSearchTree.from([[-10, "a"], [0, null], [10, "c"]] as const, policy);
    for (let position = 0; position <= source.count; position++) {
      const cursor = source.cursor(position);
      expect(cursor.position).toBe(position);
      expect(cursor.isAtStart).toBe(position === 0);
      expect(cursor.isAtEnd).toBe(position === source.count);
      expect(cursor.snapshot()).toBe(source);
      expect(cursor.peekPrevious()?.key).toBe(position === 0 ? undefined : [...source][position - 1]!.key);
      expect(cursor.peekNext()?.key).toBe(position === source.count ? undefined : [...source][position]!.key);
    }

    expect(source.lowerBoundCursor(-5).position).toBe(1);
    expect(source.upperBoundCursor(0).position).toBe(2);
    expect(source.cursorAtKey(0)).toMatchObject({ found: true, cursor: { position: 1 } });
    expect(source.cursorAtKey(5)).toMatchObject({ found: false, cursor: { position: 2 } });

    const exact = source.cursorAtKey(0).cursor;
    expect(exact.setNextValue(null)).toBe(exact);
    const changed = exact.setNextValue("b").snapshot();
    expect(changed.get(0)).toBe("b");
    expect(changed.rootHash.equals(source.rootHash)).toBe(false);
    expect(changed.policy).toBe(source.policy);
    expect(source.getEntry(0)).toEqual({ key: 0, value: null });

    const inserted = source.lowerBoundCursor(5).insert(5, "five");
    expect(inserted.position).toBe(3);
    expect([...inserted.snapshot().keys]).toEqual([-10, 0, 5, 10]);
    expect(inserted.deletePrevious().snapshot().rootHash.equals(source.rootHash)).toBe(true);
    expect([...source.cursorAtEnd().deletePrevious().snapshot().keys]).toEqual([-10, 0]);
    expect([...source.cursor().deleteNext().snapshot().keys]).toEqual([0, 10]);

    expect(() => source.cursor(-1)).toThrow(RangeError);
    expect(() => source.cursor().movePrevious()).toThrow(RangeError);
    expect(() => source.cursorAtEnd().moveNext()).toThrow(RangeError);
    expect(() => exact.insert(0, "duplicate")).toThrow();
    expect(() => source.cursor().insert(5, "wrong gap")).toThrow(RangeError);
  });

  test("cursor cached ranks and bounds match a sorted randomized model", () => {
    fc.assert(fc.property(fc.uniqueArray(fc.integer({ min: -2000, max: 2000 }), { maxLength: 300 }), values => {
      const keys = [...values].sort(numberOrder);
      const tree = MerkleSearchTree.from(keys.map(key => [key, String(key)] as const), policy);
      for (let position = 0; position <= keys.length; position++) {
        const cursor = tree.cursor(position);
        expect(cursor.peekPrevious()?.key).toBe(position === 0 ? undefined : keys[position - 1]);
        expect(cursor.peekNext()?.key).toBe(keys[position]);
      }
      for (let probe = -2100; probe <= 2100; probe += 97) {
        const lower = keys.findIndex(key => key >= probe);
        const rank = lower < 0 ? keys.length : lower;
        const found = keys[rank] === probe;
        expect(tree.lowerBoundCursor(probe).position).toBe(rank);
        expect(tree.upperBoundCursor(probe).position).toBe(found ? rank + 1 : rank);
        expect(tree.cursorAtKey(probe)).toMatchObject({ found, cursor: { position: rank } });
      }
    }), { numRuns: 50 });
  });

  test("incremental histories converge and preserve snapshots", () => {
    fc.assert(fc.property(fc.uniqueArray(fc.integer({ min: -20_000, max: 20_000 }), { maxLength: 300 }), keys => {
      let incremental = MerkleSearchTree.empty(policy);
      const snapshots = [incremental];
      for (const key of keys) { incremental = incremental.set(key, String(key)); snapshots.push(incremental); }
      const bulk = MerkleSearchTree.from([...keys].reverse().map(key => [key, String(key)] as const), policy);
      expect(incremental.rootHash.equals(bulk.rootHash)).toBe(true);
      expect([...incremental]).toEqual([...bulk]);
      expect(snapshots[0]!.isEmpty).toBe(true);
      expect(incremental.validateStructure().count).toBe(keys.length);
    }), { numRuns: 40 });
  });

  test("save, load, import and iterative sync authenticate exact closures", () => {
    const tree = MerkleSearchTree.from(Array.from({ length: 513 }, (_, key) => [key, key % 7 === 0 ? null : `v${key}`] as const), policy);
    const store = new InMemoryMerkleBlockStore();
    expect(saveMerkleTree(tree, store)).toBe(tree.blockCount);
    expect(loadMerkleTree(tree.rootHash, policy, store).contentEquals(tree)).toBe(true);
    const pack = exportMerklePack(tree);
    expect(importMerklePack(pack, policy).contentEquals(tree)).toBe(true);

    const partial = new InMemoryMerkleBlockStore();
    const local = MerkleSearchTree.empty(policy);
    let rounds = 0;
    while (true) {
      const plan = planMerkleSync(tree, local, partial);
      if (!plan.requiresBlocks) break;
      const round = exportMerklePack(tree, plan.requestedDigests);
      for (const block of round.blocks) partial.put(block);
      if (++rounds > tree.height + 1) throw new Error("Sync did not converge by tree height.");
    }
    expect(loadMerkleTree(tree.rootHash, policy, partial).contentEquals(tree)).toBe(true);
    expect(createMerkleSyncPack(tree, store).blockCount).toBe(0);
  });

  test("tampering and conflicting publication are rejected", () => {
    const tree = MerkleSearchTree.empty(policy).set(1, "one").set(2, null);
    const block = exportMerklePack(tree).blocks[0]!;
    const bytes = block.content; bytes[bytes.length - 1] = bytes[bytes.length - 1]! ^ 1;
    const store = new InMemoryMerkleBlockStore();
    store.put(new MerkleBlock(block.digest, bytes));
    expect(() => loadMerkleTree(tree.rootHash, policy, store)).toThrow(MerklePersistenceError);
    expect(() => store.put(block)).toThrow(MerklePersistenceError);
  });

  test("MSP2 membership, absence and range proofs authenticate canonical paths", () => {
    const tree = MerkleSearchTree.from(Array.from({ length: 1000 }, (_, key) => [key * 2, key % 11 === 0 ? null : `v${key}`] as const), policy);
    const member = verifyMerkleProof(createMerkleProof(tree, 400), policy);
    expect(member.valid).toBe(true);
    expect(member.entries).toEqual([{ key: 400, value: "v200" }]);
    const absent = verifyMerkleProof(createMerkleProof(tree, 401), policy);
    expect(absent.valid).toBe(true);
    expect(absent.entries).toEqual([]);
    const range = verifyMerkleProof(createMerkleRangeProof(tree, 391, 413), policy);
    expect(range.valid).toBe(true);
    expect(range.entries.map(entry => entry.key)).toEqual([392, 394, 396, 398, 400, 402, 404, 406, 408, 410, 412]);
    expect(range.accountedBlocks).toBeLessThan(tree.blockCount);
  });

  test("typed merge distinguishes absent from present null and reports bilateral conflicts", () => {
    const base = MerkleSearchTree.from([[1, "one"], [2, null], [3, "three"]] as const, policy);
    const left = base.set(1, "left").remove(2).set(4, "left-only");
    const right = base.set(1, "right").set(2, "present").set(5, "right-only");
    const unresolved = mergeMerkleTrees(base, left, right);
    expect(unresolved.succeeded).toBe(false);
    expect(unresolved.conflicts.map(conflict => conflict.key)).toEqual([1, 2]);
    expect(unresolved.conflicts[1]!.left).toEqual({ present: false });
    expect(unresolved.conflicts[1]!.base).toEqual({ present: true, value: null });
    const resolved = mergeMerkleTrees(base, left, right, conflict => conflict.key === 1 ? { kind: "value", value: "combined" } : { kind: "right" });
    expect(resolved.succeeded).toBe(true);
    expect([...resolved.tree!]).toEqual([
      { key: 1, value: "combined" }, { key: 2, value: "present" }, { key: 3, value: "three" },
      { key: 4, value: "left-only" }, { key: 5, value: "right-only" },
    ]);
  });
});
