# Rust Merkle Search Tree

- Created (UTC): 2026-07-12T04:30:09Z
- Repository HEAD: 8a926e3bdb0cc37da0c8a15c4c32352c2ebcb1f5
- Audience: Maintainers implementing or reviewing the Rust Merkle search tree
- Scope: Core persistent API, canonical codecs, SHA-256 domain, and `MST2` block bytes

`MerkleSearchTree<K, V>` is a safe-Rust semantic and wire port of the C# canonical Merkle search
tree. It is a deterministic ordered map and content-addressed block tree, not a probabilistic skip
list and not a binary Merkle tree. The algorithm identifier is exactly
`mst-sha256-b16-v2`.

## Policy and codecs

Every tree retains one `MerkleSearchTreePolicy<K, V>` containing:

- an application policy/version ID;
- a `MerkleKeyComparer<K>` defining order and key equivalence;
- injective, versioned `MerkleCodec<K>` and canonical `MerkleCodec<V>` implementations;
- the derived domain digest and canonical empty-root digest.

The SHA-256 domain is the byte `0x50` followed by each of these UTF-8 fields framed with a signed
big-endian 32-bit byte length: algorithm ID, policy ID, key codec ID, and value codec ID. A codec ID
must end in `-v` and one or more ASCII decimal digits. `hash_key_bytes` uses the same framing with
tag `0x4b`, the 32-byte domain digest, and canonical key bytes. The number of leading zero
base-16 digits in that digest is the entry level, from zero through 64.

The crate supplies these strict codecs:

| Rust value | Encoding ID | Canonical representation |
| --- | --- | --- |
| `i32` | `i32-be-v1` | exactly four big-endian two's-complement bytes |
| `i64` | `i64-be-v1` | exactly eight big-endian two's-complement bytes |
| `Option<String>` | `nullable-utf8-v1` | tag 0 alone for `None`; tag 1 plus well-formed UTF-8 otherwise |
| `Option<Vec<u8>>` | `nullable-bytes-v1` | tag 0 alone for `None`; tag 1 plus payload otherwise |
| `Rfc4122Guid` | `guid-rfc4122-v1` | exactly 16 RFC-4122/network-order bytes |

Decoding consumes the complete byte slice. Noncanonical widths, tags, null trailing bytes, and
malformed UTF-8 are errors. `MerkleDigest` similarly accepts exactly 32 binary bytes or 64
hexadecimal characters; writing to a short destination fails before changing it.

## Canonical wide tree

In comparer order, every consecutive run at the greatest level in a subtree becomes the separator
array of one wide block; the intervals between those separators recurse at lower levels. This is
the B=16 paper construction because each SHA-256 nibble is zero with probability 1/16. Bulk
construction sorts by comparer order, retains the first representative of an equivalent-key run,
and uses its last value. Incremental insert, replacement, and removal preserve exactly that shape:
deleting a separator joins adjacent intervals and collapses an empty block shell.

Nodes, entry records, encoded bytes, keys, and values use `Arc` sharing. Updates allocate only the
affected block path, and cloning a tree clones two retained handles. Core operations do not require
`K: Clone` or `V: Clone`. An encoded-value no-op, absent removal, and clearing an already empty tree
retain the root. `shared_block_count`, `shares_root_with`, `shape`, and `blocks_preorder` expose
diagnostics without weakening immutability.

Lookup follows one comparer-selected interval per block. Iteration and inclusive range traversal
use explicit stacks. `content_equals` compares policy domain and root address in O(1);
`map_equals_by` provides semantic comparison; `diff_by` prunes equal-address subtrees and falls back
to a sorted merge only when separator topology differs. `validate_structure` independently checks
ordering, level placement, child bounds, cached counts/heights/block counts, exact re-encoding, and
every block digest.

## Exact `MST2` block

The canonical empty address is SHA-256 over `MST2`, byte tag 0, and the 32-byte domain digest. Each
nonempty block is encoded in this order:

1. ASCII `MST2` and byte tag 1;
2. the 32-byte policy-domain digest;
3. one-byte level;
4. signed big-endian 32-bit complete subtree count;
5. signed big-endian 32-bit entry count;
6. each entry's key length/key bytes/value length/value bytes, with signed big-endian 32-bit lengths;
7. exactly `entry count + 1` child addresses, using the canonical empty address for an absent child.

The block address is SHA-256 over those complete bytes. The integration suite locks the same
single-entry domain, block, and root golden vector as C# and also proves independent insertion
histories produce identical preorder block bytes.

## Validation

From `src/Rust`:

```powershell
cargo fmt --all -- --check
cargo clippy -p tools-data-structures-hamt --all-targets -- -D warnings
cargo test -p tools-data-structures-hamt
cargo test -p tools-data-structures-hamt --release
cargo doc -p tools-data-structures-hamt --no-deps
```

The broader workspace wrapper remains `./test.ps1 -Workspace Hamt`.
