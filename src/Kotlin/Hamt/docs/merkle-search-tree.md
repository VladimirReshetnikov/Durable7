# Kotlin Merkle Search Tree

- Created (UTC): 2026-07-12T05:34:01Z
- Repository HEAD: 2a2c92d10d308a18793067106b1ef10d3748f0ba
- Audience: Maintainers and reviewers of the Kotlin Merkle search-tree core
- Scope: Canonical policy, codec, immutable tree, and `MST2` wire contracts

The Kotlin HAMT workspace includes the safe-JVM port of the repository's paper-style wide Merkle
search tree. It is an immutable ordered map whose topology and content addresses are functions of
the final comparator-ordered contents, not the update history. This milestone owns the in-memory
core and exact block encoding; repository-backed block persistence and loading are separate work.

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
history-convergence, structural-sharing, nullable-value, and concurrent-reader coverage.
