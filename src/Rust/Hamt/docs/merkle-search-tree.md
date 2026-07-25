# Rust Merkle Search Tree

- Created (UTC): 2026-07-12T04:30:09Z
- Repository HEAD: 8a926e3bdb0cc37da0c8a15c4c32352c2ebcb1f5
- Audience: Maintainers implementing or reviewing the Rust Merkle search tree
- Scope: Core API, codecs, `MST2` blocks, the ordered cursor, persistence, proofs, synchronization,
  and merge

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

## Ordered persistent cursor

`MerkleSearchTreeCursor<K, V>` owns a retained `MerkleSearchTree<K, V>` snapshot plus a
comparer-order rank gap in `0..=len`. It is a **Profile R snapshot-plus-rank semantic checkpoint**:
every edit delegates to the ordinary canonical tree operation, and the cursor claims none of the C#
rope tier's focused representation, snapshot memo, callback ceiling, allocation bound, or amortized
locality. No frames are retained, so context space is O(1).

Create one with `cursor()` at the start gap, `cursor_at(position)` for a rank gap, `cursor_at_end()`
after the final entry, `lower_bound_cursor(key)`, `upper_bound_cursor(key)`, or `cursor_at_key(key)`.
`cursor_at` returns `Option` and rejects a position above `len`; every other factory is total.
`cursor_at_key` returns a bare `(MerkleSearchTreeCursor<K, V>, bool)` tuple whose flag reports
whether the gap's next entry is an exact comparer match, so a miss still returns a usable insertion
gap rather than an invalid state. `upper_bound_cursor` is the lower bound advanced past an exact
hit. Every factory retains the exact `MerkleSearchTreePolicy<K, V>` handle, including on an empty
tree.

Navigation is immutable and never consumes the receiver: `len`, `is_empty`, `position`,
`is_at_start`, `is_at_end`, borrowed `peek_previous` / `peek_next` returning
`Option<&MerkleEntry<K, V>>`, `move_previous` / `move_next`, and `seek(position)`. Boundary movement
and an out-of-range seek return `None`. `snapshot()` returns the cursor version's canonical
immutable tree by root sharing and leaves the cursor usable. Navigation and snapshotting require
neither `K: Clone` nor `V: Clone`, matching the core.

Edits use two channels, deliberately separated by what can fail:

- `insert(key, value)` and `set_item(key, value)` return `Result<Self, MerkleCursorEditError>`.
  `insert` rejects an equivalent existing key with `DuplicateKey`; both reject a key whose
  lower-bound rank is not the cursor's current gap with `WrongGap { expected, actual }`. Canonical
  encoding or tree-construction failure surfaces as `Tree(MerkleTreeError)`, and
  `MerkleCursorEditError` implements `Display`, `Error`, and `From<MerkleTreeError>`.
- `set_next_value(value)` returns `Result<Option<Self>, MerkleTreeError>`: the outer `Result`
  carries codec failure, and the inner `Option` is `None` only when the gap is at the end.
  `delete_previous()` and `delete_next()` return `Option<Self>` and cannot fail otherwise, because
  removing a key that the cursor just borrowed invokes no encoder.

Gap conventions after each edit are:

| Operation | Resulting gap |
| --- | --- |
| `insert` | `position + 1`, immediately after the new entry |
| `set_item` on a miss | `position + 1` |
| `set_item` on an exact hit | `position`, unchanged |
| `set_item` recognized as an encoded-value no-op | `position`, unchanged, receiver root retained |
| `set_next_value` | `position`, unchanged |
| `delete_previous` | `position - 1` |
| `delete_next` | `position`, unchanged |

`set_item` and `set_next_value` detect the ordinary encoded-value no-op through `shares_root_with`
and return the receiver cursor rather than an equal new version. `set_next_value` names its target
by the stored key handle recovered from `peek_next`, so the first stored key representative survives
a value update exactly as it does under `set_item`. Because every cursor edit calls the ordinary
canonical `set_item` or `remove`, the resulting policy binding, representative retention, canonical
encoding, `MST2` bytes, block digests, root digest, and failure atomicity are identical to a direct
tree edit. Cursor state is local navigation state and never appears in `MST2`, `MSP2`, a pack, a
proof, a sync plan, or a store.

The trust boundary is the tree's, unchanged. Build a cursor only from a tree constructed normally or
obtained through a fully verified `load` / `import`; the cursor weakens no codec round-trip check
and consumes no verification budget of its own. `MerkleBlock`, `MerkleBlockPack`, `MerkleProof`, and
store content never become editable through it.

Let `h` be block height and `e_i` the entry occupancy of visited block `i`. `len` and `position` are
O(1) reads. Creating a cursor at start, end, or a rank is O(1). `find_position` inside one block is
a binary search, so key seek costs O(sum log(e_i + 1)) comparator calls, but the surrounding rank
accumulation in `lower_bound_rank_for_cursor` sums the cached child counts preceding the selected
interval, and `entry_at_rank_for_cursor` scans a block's entries while subtracting child counts.
Blocks cache each child's total count rather than a cumulative child-prefix table, so a bound
factory, a rank seek, the initial `position`, **and every peek** cost O(sum (e_i + 1)). Moving the
gap is O(1) because it only rewrites an integer and clones two handles, so a complete traversal by
move-plus-peek is O(n · sum (e_i + 1)) rather than O(n). An edit plus its snapshot is expected
O(16 log16 n) block visits plus the changed encoded bytes under uniform layers, and O(n) plus those
bytes for a degenerate block distribution, because the edit delegates to the canonical operation.
`snapshot` is O(1) in both the clean and the dirty case, since the retained tree is already
canonical.

## Blocks, stores, and transfer packs

`MerkleBlock` owns immutable serialized bytes and their claimed address. Its constructor does not
authenticate that pair because storage is deliberately format agnostic. `MerkleBlockStore` defines
concurrent-safe lookup and shared-reference mutation; a repeated identical write is a no-op and a
different byte sequence under an existing digest is a `ConflictingBlock` failure.
`InMemoryMerkleBlockStore` implements that contract with an `RwLock` and returns sorted digest
snapshots.

`export_pack` emits the complete closure in deterministic preorder. `export_pack_for` preserves an
explicit unique request order for partial transfer. `MerkleBlockPack` records the algorithm,
domain, target root, ordered unique blocks, total byte count, and whether the root block is directly
included. `save` and destination-backed `import` preflight every transferred address before their
first store write, so a late conflict does not partially publish earlier blocks.

`load` follows only the closure reachable from the requested root. `import` first verifies every
supplied block, overlays it on an optional existing store, verifies the complete reachable closure,
then commits supplied blocks only after successful destination preflight. A partial pack is valid
only when its fallback store completes the root closure. Empty roots require no block.

For every unique block the loader authenticates the claimed SHA-256 digest, `MST2` magic/tag,
policy domain, signed counts, exact codec decode-and-reencode bytes, key-derived level, comparer
ordering, child digest arity, absence of trailing bytes, and full canonical re-encoding. Closure
construction additionally rejects missing blocks, active-reference cycles, child levels that do
not descend, keys crossing separator intervals, declared/actual subtree-count differences, and a
reconstructed root mismatch. Trusted `Arc` nodes are created only after those checks.

## Verification budgets

`MerkleVerificationBudget` has seven positive finite limits:

| Limit | Accounted resource |
| --- | --- |
| `max_block_count` | distinct decoded block addresses |
| `max_total_byte_count` | proof query plus distinct serialized block bytes |
| `max_block_byte_count` | one serialized block |
| `max_depth` | root-to-leaf closure or proof expansion |
| `max_entry_count` | entries decoded across distinct blocks |
| `max_child_references_per_block` | child digest slots in one block |
| `max_proof_query_byte_count` | one canonical `MSP2` query descriptor |

Limits are checked before entry/child allocation and before following a reference. Proof-query
accounting is the first verification action; an oversized query invokes no codec, hashing, or block
decode work and reports zero verified bytes/blocks. `MerkleVerificationError` exposes a stable
`MerkleVerificationFailureKind`, diagnostic text, and the offending/missing digest when known.

## `MSP2` proofs

`create_proof` produces membership or nonmembership steps along one search path.
`create_range_proof` expands every nonempty child interval intersecting an inclusive range. The
query descriptor is ASCII `MSP2`, one kind byte (`0` membership, `1` nonmembership, `2` range),
then canonical key/value fields with signed big-endian 32-bit lengths. A `MerkleProofStep` contains
one complete `MST2` block and sorted unique indexes of children expanded by other steps; all other
child hashes remain authenticated opaque boundaries.

Verification budgets and strictly decodes every supplied block, checks expanded indexes against
the canonical query path/frontier, validates each expanded child reference, rejects repeated,
missing, extra, or unreachable steps, decodes/re-encodes query keys and membership values, and
requires the declared root block. Empty roots prove only nonmembership or ranges and contain no
steps. The immutable result reports validity, failure kind/message, root, verified blocks, and
accounted bytes.

## Synchronization

`create_sync_pack` walks the target tree and prunes any block already present in a receiver whose
stored blocks are understood to have verified descendant closure. `plan_sync` is the repair-safe
alternative for a possibly partial store: it requests the first absent block on each reachable
target path and stops below that frontier. After fetching those exact addresses with
`export_pack_for`, the receiver repeats planning until no addresses remain, then publishes only a
successfully loaded target root. Plan diagnostics include local/target roots and examined blocks
and bytes.

## Typed three-way merge

`merge` compares base, left, and right in key order. A side unchanged from base yields the other
side; identical descendant edits collapse without a callback. True conflicts carry shared key and
base/left/right values. `MerkleMergeValue::Absent` differs from `Present(Arc<V>)`, so deletion is
not confused with a present `None` when `V = Option<T>`. A resolver may choose base/left/right,
delete, provide a new value, or leave the conflict unresolved.

Existing entry records are reused through `Arc`, and a custom value replaces only its value record,
so neither persistence nor merge requires `K: Clone` or `V: Clone`. If any conflict remains, the
result exposes all typed conflicts and intentionally no partial tree. A successful output is rebuilt
through the same canonical wide-tree constructor and therefore has history-independent `MST2`
bytes.

## Validation

From `src/Rust`:

```powershell
cargo fmt --all -- --check
cargo clippy --jobs 1 -p durable7-hamt --all-targets -- -D warnings
cargo test -p durable7-hamt --jobs 1 -- --test-threads=1
cargo test -p durable7-hamt --release --jobs 1 -- --test-threads=1
cargo doc --jobs 1 -p durable7-hamt --no-deps
```

The broader workspace wrapper remains `./test.ps1 -Workspace Hamt`.
