# Haskell Merkle Search Tree

- Created (UTC): 2026-07-12T07:00:00Z
- Repository HEAD: 2a2c92d10d308a18793067106b1ef10d3748f0ba
- Audience: Maintainers implementing or reviewing the Haskell Merkle search tree
- Scope: Canonical core, exact wire bytes, verified persistence, proofs, synchronization, and merge

`Data.Structures.Hamt.MerkleSearchTree` is the pure-Haskell core/wire port of the repository's
canonical Merkle search tree. It is an ordered persistent map whose shape and root address depend
only on its policy-bound contents, not insertion history. The implementation follows the
paper-style B=16 construction: the number of leading zero SHA-256 nibbles in a canonical key hash
chooses that key's level, and every consecutive run at the maximum level occupies one wide block.

## Policy and codecs

`Data.Structures.Hamt.MerkleEncoding` defines `MerkleCodec a` as a stable versioned identifier plus
total encode/decode functions returning typed errors. Built-ins cover signed 32-bit and 64-bit
big-endian integers, nullable strict UTF-8 strings, nullable strict byte strings, and exact
RFC-4122/network-order GUID bytes. UTF-8 encoding rejects surrogate code points; decoding rejects
overlong, truncated, surrogate, and otherwise malformed sequences. Nullable encodings use one
leading zero/one tag and preserve embedded NUL bytes.

`makeMerkleSearchTreePolicy` binds the comparator, application semantic id, and both codec ids into
the same `mst-sha256-b16-v2` domain as the C# and Rust implementations. The package owns a pure
SHA-256 implementation, so content addressing does not depend on platform hashing or a process-
randomized `Hashable` instance. Comparator equivalence must be coherent with canonical key bytes:
equivalent keys must encode identically.

## Persistent core

Use the module qualified because its collection vocabulary intentionally overlaps the HAMT and
Patricia modules:

```haskell
import Data.Int (Int32)
import Data.Structures.Hamt.MerkleEncoding
import qualified Data.Structures.Hamt.MerkleSearchTree as Merkle

policy <- either fail pure
  (makeMerkleSearchTreePolicy
    "example-int-map-v1"
    compare
    int32MerkleCodec
    int32MerkleCodec)

tree0 <- pure (Merkle.empty policy)
tree1 <- either (fail . show) pure (Merkle.insert 42 7 tree0)
tree2 <- either (fail . show) pure (Merkle.delete 42 tree1)
```

Bulk construction is stable first-equivalent-key/last-value. `insert` retains the first key
representative and returns the original tree when the replacement's canonical value bytes are
unchanged. New levels split only the affected path; deletion joins adjacent child intervals and
contracts empty block shells. Lookup is comparator-based, range traversal prunes cached key
intervals, and diff prunes equal block digests when separator topology remains aligned before
falling back to an ordered merge after topology-changing edits.

`blocksPreorder` exposes immutable `(digest, bytes)` views for the exact current closure. `shape`,
`commonBlockCount`, and `validateStructure` provide topology, shared-content, and deep invariant
diagnostics. Validation re-encodes every key and value, recomputes hash levels, checks separator
intervals and descending child levels, verifies cached count/height/block-count/minimum/maximum
metadata, rebuilds every complete block, and authenticates every digest.

## `MST2` wire

The empty manifest is:

```text
ASCII "MST2" | tag 0 | 32-byte policy domain
```

Every nonempty block is:

```text
ASCII "MST2" | tag 1 | domain | level | subtree-count:i32be | entry-count:i32be
entry* | (entry-count + 1) child digests

entry := key-length:i32be | key bytes | value-length:i32be | value bytes
```

An absent child uses the policy's empty-manifest digest. The Haskell tests pin the shared
`golden-int-string-v1` domain, empty digest, root digest, and complete single-entry block byte for
byte against C# and Rust, then compare every preorder block produced by opposite insertion
histories.

## Pure authenticated persistence

`Data.Structures.Hamt.MerklePersistence` owns the persistence tier. `MerkleBlock` pairs immutable
bytes with a claimed address without authenticating it; format-aware operations authenticate the
pair. `MerkleBlockStore` is an immutable `Map`-backed snapshot. `putBlock` is idempotent for equal
bytes and rejects a different byte string under an existing address. `saveTree` preflights the
entire destination before returning an updated store, so a late conflict cannot expose a partially
published successor.

`exportPack` emits the complete closure in canonical preorder. `exportPackFor` preserves an
explicit caller order while rejecting duplicate or unknown addresses. A `MerkleBlockPack` can be
complete or partial and records its algorithm, policy domain, target root, ordered unique blocks,
and exact transferred byte count. `importPack` first verifies every supplied block, then verifies
the requested root closure through a staged-first/destination-second overlay, then preflights all
destination conflicts, and only then returns the loaded tree and successor store. Supplied blocks
outside the requested root closure must still be individually canonical and authenticated, but may
remain useful partial synchronization state. Only the requested root closure must be complete.

`loadTree` and `importPack` strictly decode and re-encode keys, values, and blocks; authenticate each
claimed digest; reject malformed and noncanonical lengths or trailing bytes; validate descending
levels, separator intervals, declared subtree counts, cycles, and closure completeness; rebuild the
canonical core; and finally check the requested root and full core structure. No decoded tree or
successor store is returned after failure.

## Verification budgets

`MerkleVerificationBudget` is opaque and can only be built by
`makeMerkleVerificationBudget`. Its seven positive finite limits are:

- unique block count;
- total query-plus-block bytes;
- bytes in one block;
- root-to-leaf reference depth;
- cumulative entries in unique blocks;
- child references in one block; and
- bytes in one proof query descriptor.

The cross-language defaults are 1,000,000 blocks, 1 GiB total, 16 MiB per block, depth 256,
100,000,000 entries, 65,536 child references, and 16 MiB per proof query. Construction rejects a
per-block or query limit larger than the total-byte limit. Header counts and minimum possible block
lengths are admitted before entry-list construction or codec callbacks.

Proof verification has a stronger deliberate precedence: query size is checked and accounted
first; proof step count and every expanded-index count are preflighted second; algorithm/domain
envelopes come third; verifier maps, hashing, codec callbacks, and block decoding occur only after
all three admissions. An oversized query reports zero verified blocks and zero bytes. A structural
proof-limit failure reports zero blocks and query-only bytes.

## Exact `MSP2` proofs

`createProof` emits canonical membership or nonmembership paths. `createRangeProof` emits the exact
set of child intervals intersecting an inclusive comparator range. Their query descriptors are:

```text
point := ASCII "MSP2" | kind:byte | key-length:i32be | key bytes
         | (value-length:i32be | value bytes, membership only)
range := ASCII "MSP2" | 2 | min-length:i32be | min bytes
         | max-length:i32be | max bytes
```

Kinds 0, 1, and 2 mean membership, nonmembership, and range. Every proof step carries one exact
`MST2` block and sorted unique child indexes expanded elsewhere in the proof. Verification requires
the declared root, exact canonical query expansion, correct parent/child addresses and separator
intervals, the authenticated membership value or terminal empty interval, and no unreachable extra
step. Empty roots canonically prove nonmembership and empty ranges without blocks.

Local proof and synchronization construction use a trusted read-only topology view from the core;
they neither call codec decoders nor impose network verification budgets on an already-constructed
tree. Untrusted verification continues to use strict codecs and finite limits.

## Synchronization and merge

`createSyncPack` walks the target in preorder and prunes a whole closure when the receiver already
has its root address. `planSync` performs one iterative frontier round: a missing address is
requested without descending through an unavailable block; after that block is transferred, the
next round discovers its missing children. The plan records deterministic requests and exact target
blocks/bytes examined. Matching local and target roots converge without examining blocks.

`mergeThreeWay` and `mergeThreeWayWith` compare the base and two descendants in ordered key space.
One-sided edits and identical descendant edits merge automatically. True conflicts expose typed
`MergeAbsent`/`MergePresent value` states and accept use-base, use-left, use-right, set-value,
delete, or unresolved decisions. `MergePresent Nothing` remains distinct from deletion when the
tree value type is `Maybe a`. The result type is either `MerkleMergeSucceeded tree` or
`MerkleMergeConflicted conflicts`; an unresolved result cannot carry a partial tree.

## Validation

From `src/Haskell`:

```powershell
.\test.ps1 -Workspace Hamt
cabal test hamt-test --jobs=1 --test-show-details=direct --ghc-options=-Werror
```

The focused coverage is summarized in [`../test/README.md`](../test/README.md).
