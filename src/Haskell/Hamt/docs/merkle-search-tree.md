# Haskell Merkle Search Tree

- Created (UTC): 2026-07-12T07:00:00Z
- Repository HEAD: 2a2c92d10d308a18793067106b1ef10d3748f0ba
- Audience: Maintainers implementing or reviewing the Haskell Merkle search tree
- Scope: Canonical codecs, SHA-256 policy framing, persistent core, and exact `MST2` block bytes

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

The current module is the core and wire checkpoint. Content stores, transfer packs, bounded
untrusted decoding, `MSP2` proofs, synchronization, and typed merge belong to the persistence tier
and are not claimed by this checkpoint.

## Validation

From `src/Haskell`:

```powershell
.\test.ps1 -Workspace Hamt
cabal test hamt-test --test-show-details=direct --ghc-options=-Werror
```

The focused coverage is summarized in [`../test/README.md`](../test/README.md).
