# Kotlin Merkle Search Tree

- Created (UTC): 2026-07-12T05:34:01Z
- Repository HEAD: 2a2c92d10d308a18793067106b1ef10d3748f0ba
- Audience: Maintainers and reviewers of the Kotlin Merkle search-tree core
- Scope: Canonical policy, immutable tree, `MST2` blocks, persistence, proofs, synchronization, and merge

The Kotlin HAMT workspace includes the safe-JVM port of the repository's paper-style wide Merkle
search tree. It is an immutable ordered map whose topology and content addresses are functions of
the final comparator-ordered contents, not the update history. This milestone owns the in-memory
core, exact block encoding, verified persistence, succinct proofs, synchronization, and merge.

## Policy and Hash Domain

Every tree has an explicit `MerkleSearchTreePolicy<K, V>` containing:

- a nonblank semantic policy ID;
- a comparator whose equivalence relation defines map keys;
- injective, explicitly versioned key and value codecs; and
- the algorithm ID `mst-sha256-b16-v2`.

Codec IDs must be unpadded strings ending in `-v` followed by ASCII decimal digits. Policy IDs,
algorithm IDs, and codec IDs must be valid Unicode and are encoded as strict UTF-8. The domain
digest is SHA-256 over this exact sequence:

```text
50
  int32be(length(utf8("mst-sha256-b16-v2")))) utf8("mst-sha256-b16-v2")
  int32be(length(utf8(policyId)))               utf8(policyId)
  int32be(length(utf8(keyCodec.encodingId)))   utf8(keyCodec.encodingId)
  int32be(length(utf8(valueCodec.encodingId))) utf8(valueCodec.encodingId)
```

`50` is the single-byte policy tag `0x50`. A canonical encoded key is bound to the policy by hashing
the following framed sequence:

```text
4b int32be(32) domainDigest int32be(length(keyBytes)) keyBytes
```

The key's level is the number of leading zero base-16 digits in that digest, from 0 through 64.
Thus the B=16 geometric distribution is deterministic and policy-separated.

## Canonical Codecs and Digests

`MerkleCodec<T>` owns newly allocated canonical bytes from `encode` and consumes exactly one complete
encoding in `decode`. A codec must be injective over the comparator's key-equivalence classes. The
built-in codecs are:

| Codec | Encoding ID | Canonical representation |
| --- | --- | --- |
| `Int32MerkleCodec` | `i32-be-v1` | Exactly four signed two's-complement bytes, big-endian |
| `Int64MerkleCodec` | `i64-be-v1` | Exactly eight signed two's-complement bytes, big-endian |
| `NullableUtf8MerkleCodec` | `nullable-utf8-v1` | `00` for null; otherwise `01` plus strict UTF-8 |
| `NullableBytesMerkleCodec` | `nullable-bytes-v1` | `00` for null; otherwise `01` plus exact bytes |
| `Rfc4122UuidMerkleCodec` | `guid-rfc4122-v1` | Exactly 16 RFC-4122/network-order UUID bytes |

Nullable decoders reject missing or unknown tags and trailing bytes after the null tag. UTF-8
encoding rejects unpaired UTF-16 surrogates; decoding rejects malformed or unmappable byte
sequences. Fixed-width decoders reject every other length.

`MerkleDigest` is an immutable 32-byte SHA-256 address. Construction and byte access use defensive
copies, hexadecimal parsing requires exactly 64 digits, formatting is lowercase, comparison is
unsigned lexicographic byte order, and `tryWriteBytes` leaves its destination unchanged when the
complete digest does not fit.

## Canonical Wide Tree

Within any key interval, entries at the interval's maximum level become the ordered separators of
one wide block. The intervals between and around those separators are recursively represented by
lower-level child blocks. Consecutive keys at the same level therefore occupy the same block instead
of forming a binary chain. Bulk construction, incremental insertion, deletion and contraction all
produce this same canonical geometry.

`MerkleSearchTree.from` sorts input with the policy comparator. Among equivalent keys it retains the
first key object and the last supplied value. `setItem` likewise retains the stored key object;
replacing a value with identical canonical encoded bytes is an identity-preserving no-op. Removing
an absent key and clearing an already empty tree also return the receiver. Real changes copy only
the affected immutable path and retain all untouched block references.

The primary read and diagnostic APIs are:

- `getEntry`, which distinguishes an absent key from a present nullable value;
- in-order `Iterable`/`Sequence` traversal and inclusive `enumerateRange` traversal;
- `contentEquals`, `mapEquals`, and digest-pruned typed `diff`;
- `rootHash`, `height`, `blockCount`, `shape`, and exact `blocksPreorder` output; and
- `sharesRootWith` and `sharedBlockCount` for reference-identity diagnostics.

The tree retains caller key and value references exactly as JVM objects. It separately retains the
canonical bytes captured at insertion time. Public encoded-byte APIs return fresh arrays, so callers
cannot mutate tree content addresses through returned buffers.

## Ordered Persistent Cursor

`MerkleSearchTreeCursor<K, V>` is the specialized ordered cursor for this family. It retains one
exact immutable tree snapshot plus a comparator-order rank gap in `0..size`, and it is a **Profile R
root-plus-rank semantic checkpoint** in the sense of the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md).
It deliberately inherits none of the C# rope tier's focused representation, prepared-measure
fragments, snapshot memo, callback ceiling, allocation bound, or amortized-locality claim. Block
topology, levels, digests, and canonical bytes stay private; ordered key navigation is the only
exposed axis.

Create a cursor from the tree:

```text
cursor()                 // gap zero
cursorAt(position)       // null outside 0..size
cursorAtEnd()            // gap size
lowerBoundCursor(key)    // gap before the first key >= the argument
upperBoundCursor(key)    // gap after an exact hit, otherwise the lower bound
cursorAtKey(key)         // MerkleCursorSearch(cursor, found)
```

`MerkleCursorSearch<K, V>` publishes a separate `found` flag, so an exact-key miss still returns a
usable insertion gap rather than an invalid cursor. The bound factories preserve the exact policy
object even on an empty tree.

Cursor members are `size`, `position`, `isAtStart`, `isAtEnd`, `peekPrevious`, `peekNext`,
`movePrevious`, `moveNext`, `seek`, `insert`, `setItem`, `setNextValue`, `deletePrevious`,
`deleteNext`, and `snapshot`. Peeks return `MerkleEntry<K, V>?`, so a present entry whose value is
`null` remains distinguishable from a missing neighbor.

Navigation and edits are immutable and return new cursor values. `insert` strictly rejects a key that
is already present, and both `insert` and `setItem` additionally reject a key whose lower-bound rank
is not the current gap; both failures raise `IllegalArgumentException`. Gap conventions after each
edit are:

| Operation | Resulting gap |
| --- | --- |
| `insert(key, value)` | `position + 1`, immediately after the new entry |
| `setItem(key, value)` on an exact hit | `position`, unchanged |
| `setItem(key, value)` on a miss | `position + 1` |
| `setNextValue(value)` | `position`, unchanged; the stored key representative is retained |
| `deleteNext()` | `position`, unchanged |
| `deletePrevious()` | `position - 1` |

The identity rules of the owning tree carry through unchanged. `setItem` and `setNextValue` return
this exact cursor when the replacement encodes to identical canonical value bytes, because the
ordinary `MerkleSearchTree.setItem` returns the same tree object. `seek` to the current position
returns the same cursor. Boundary operations return `null`: `peekPrevious` and `movePrevious` at the
start, `moveNext` at the end, `seek` outside `0..size`, and `setNextValue`, `deleteNext`, or
`deletePrevious` with no neighbor. Position growth uses `Math.addExact` and raises
`ArithmeticException` before publication.

Every cursor edit calls the ordinary canonical `setItem` or `remove`. Key-derived geometric levels,
canonical block geometry, `MST2` bytes, root digests, first-key-representative retention, codec round
trips, and failure atomicity are therefore identical to direct tree edits — the cursor adds no second
publication path. Cursor state is purely local navigation state and never appears in an `MST2` block,
an `MSP2` proof, a pack, or a store.

The cursor is an ordinary immutable class with a private constructor and no public `copy`, so there
is no uninitialized, default, moved-from, or disposed state; the only invalid outcome is a factory
returning `null`. `snapshot()` returns this cursor version's canonical tree in O(1) and never
consumes the cursor, and every retained cursor and snapshot stays valid and branchable. Published
cursors are safe for concurrent read-only use to the same extent as the retained tree and its
comparator and codec callbacks.

Let `h` be the block height and `b` the entry count of a visited block. Rank lookup descends using
cached subtree counts, and lower-bound search binary-searches each block's separators; both visit
O(h) blocks, performing O(log b) comparator calls plus an O(b) child-count summation per block. Unit
movement, `seek`, and `snapshot` are O(1), while the next peek pays the rank descent, so no
O(1)-amortized traversal is claimed. Cursor edits carry the ordinary O(h)-block path copy and its
re-encoding cost.

## Exact `MST2` Blocks

The empty-tree address is SHA-256 over this 37-byte manifest:

```text
"MST2" 00 domainDigest
```

Each nonempty node is addressed by SHA-256 over its complete canonical block:

```text
"MST2"
01
domainDigest[32]
level:u8
subtreeCount:int32be
entryCount:int32be
repeat entryCount times:
  keyLength:int32be keyBytes[keyLength]
  valueLength:int32be valueBytes[valueLength]
repeat entryCount + 1 times:
  childDigest[32]
```

A missing child interval uses the policy's empty-tree digest. Entries and child intervals are in
comparator order. All counts and lengths are signed 32-bit big-endian values constrained by the
in-memory representation. `MerkleEncodedBlock` returns an owned byte copy and pairs it with the
SHA-256 address used by its parent.

## Blocks, Stores, and Packs

`MerkleBlock` is the persistence value: a claimed `MerkleDigest` plus defensively copied complete
bytes. Construction deliberately does not authenticate the pair because a store is format-agnostic;
verified load/import/proof paths perform that work before constructing trusted tree state.

`MerkleBlockStore` requires concurrent-safe immutable snapshots, idempotent identical writes, and
typed `CONFLICTING_BLOCK` rejection when different bytes already occupy an address.
`InMemoryMerkleBlockStore` implements this contract with `ConcurrentHashMap.putIfAbsent`; digest
enumeration is a sorted point-in-time snapshot. `MerkleBlockPack` is an immutable logical envelope
containing algorithm ID, domain, target root, and unique ordered blocks. Packs may be complete or
partial, and there is intentionally no additional pack serialization format.

The main persistence API is:

- `tree.save(store)` after complete conflict preflight;
- `tree.exportPack()` for deterministic preorder closure and `tree.exportPack(digests)` for unique
  explicitly ordered addresses;
- `MerkleSearchTree.load(root, policy, store, budget)` for a store closure; and
- `MerkleSearchTree.importPack(pack, policy, destination, budget)` for a complete pack or a partial
  pack whose missing closure is present in `destination`.

Import authenticates every supplied block, verifies the declared reachable closure through a
read-only staged overlay, runs final deep structure validation, then preflights destination address
conflicts before its first write. Thus malformed input and known conflicts publish neither a tree
nor a block prefix. As in the C# and Rust contracts, preflight is not a transaction against a
concurrent second writer or an arbitrary custom store failure during later `put` calls.

The requested root closure, not every unrelated pack address, is the publication boundary. A
canonical supplied block outside that closure is authenticated as one block and may be committed to
the destination as partial synchronization state; its descendants need not yet be present. Such a
partial store is suitable for `planSync`, while `createSyncPack` may prune at a present address only
when the receiver knows that address already represents a verified complete descendant closure.

Untrusted decoding recomputes every digest and requires exact magic, tag, domain, unsigned level,
positive counts, bounded lengths, strict comparator order, canonical codec round trips, key-derived
levels, child arity, no trailing bytes, and exact block reserialization. Closure loading additionally
checks cycles, decreasing child levels, complete child subtree bounds, declared subtree counts,
requested root equality, and final `validateStructure` success.

## Finite Verification Budgets

`MerkleVerificationBudget` has seven positive finite limits. Defaults are one million unique blocks,
one GiB total query-plus-block bytes, 16 MiB per block, depth 256, 100 million decoded entries,
65,536 child references per block, and 16 MiB per proof query. Per-block and proof-query byte limits
cannot exceed the total. When the query limit is omitted it follows the supplied per-block limit,
matching the six-limit C#/Rust convenience contract.

Verification applies limits before corresponding allocation or codec work. A proof accounts and
checks its query before envelope validation, hashing, block decoding, or codec invocation. Each
distinct block is depth/per-block/block-count/total-byte checked before parsing; declared entries
and child references are checked before their arrays are allocated. Import reuses one accounting
context across supplied-block predecode and closure traversal, so repeated addresses are not charged
twice.

## Exact `MSP2` Proof Queries

`createProof` emits the canonical comparator-selected path for membership or nonmembership.
`createRangeProof` emits every nonempty child interval intersecting an inclusive range. Each proof
step carries a complete authenticated `MST2` block plus sorted unique indexes of children expanded
by other steps; opaque child digests authenticate pruned boundaries.

The query descriptor is the only additional canonical proof wire:

```text
"MSP2" kind:u8

kind = 0 membership:
  keyLength:int32be keyBytes
  valueLength:int32be valueBytes

kind = 1 nonmembership:
  keyLength:int32be keyBytes

kind = 2 inclusive range:
  minimumLength:int32be minimumKeyBytes
  maximumLength:int32be maximumKeyBytes
```

`MerkleSearchTree.verifyProof` authenticates every supplied step, validates exact query codec round
trips and canonical expansion, checks expanded parent/child levels and separators, requires the
declared nonempty root, and rejects duplicate, omitted, unreachable, or extra steps. Empty roots can
prove only nonmembership or ranges and carry zero steps. `MerkleProofVerificationResult` reports a
stable failure kind plus exactly accounted blocks and bytes without throwing for hostile proof data.

## Synchronization

`createSyncPack(receiverStore)` assumes each receiver-present address names a previously verified
complete descendant closure and prunes below it. `planSync(localTree, receiverStore)` instead supports
iterative repair of a partial store: it traverses the in-memory target, requests the first absent
address on each reachable path, and stops below each request until the next round reveals that block's
children. Examined block/byte counts describe target work. A caller stages successive explicit packs,
then calls verified `load` or `importPack` before publishing the target tree.

`requiresBlocks == false` does not by itself mean published roots already match: a staged target may
be complete but not yet loaded, and an empty target needs no blocks. `rootsMatch` specifically compares
the local published root with the target.

## Typed Three-Way Merge

`MerkleSearchTree.merge(base, left, right, resolver, valuesEqual)` requires one policy domain and uses
root equality for whole-tree fast paths. Its ordered merge combines one-sided and equal descendant
edits without invoking the resolver. Only keys changed differently on both sides become
`MerkleThreeWayMergeConflict` values.

`MerkleMergeValue.Absent` is distinct from `MerkleMergeValue.Present(null)`. Resolvers may leave a
conflict unresolved, select base/left/right, delete, or supply a typed replacement value. Existing
choices retain the exact entry objects and encoded byte snapshots; a custom replacement encodes only
its new value while retaining the selected key representative and key bytes. Representative
preference is left, then right, then base. If any conflicts remain, the result exposes every conflict
and no partial tree; only a conflict-free result is publishable.

The executable tests pin the full policy-domain, empty-tree, root, and node-block bytes to the exact
C# and Rust golden vectors. This is byte-for-byte interoperability, not merely semantic equality.

## Validation

`validateStructure` eagerly checks strict separator ordering, key-derived levels, child bounds,
entry/child arity, cached counts, height and block count, exact canonical re-encoding, and every
block digest. It returns `MerkleSearchTreeStatistics` only after the complete structure validates.

Run the focused workspace from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Hamt
```

Run all Kotlin workspaces with:

```powershell
.\build.ps1
```

See [validation](validation.md) and the [test map](../tests/README.md) for the model, adversarial,
history-convergence, structural-sharing, exact persistence/proof, seven-budget, synchronization,
present-null merge, concurrent-reader, and concurrent-store coverage.
