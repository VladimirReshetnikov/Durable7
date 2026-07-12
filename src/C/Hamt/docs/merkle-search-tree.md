# C Merkle Search Tree

- Status: Current core and wire specification
- Created (UTC): 2026-07-12T05:30:00Z
- Repository HEAD: 2a2c92d10d308a18793067106b1ef10d3748f0ba
- Audience: C consumers, maintainers, and cross-language port reviewers
- Scope: `mst-sha256-b16-v2` policy, canonical MST2 blocks, persistent core API, and ownership

The C17 Merkle search tree is an immutable ordered content-addressed map. It is the type-erased C
port of the C# reference and implements the exact `mst-sha256-b16-v2` hashing and `MST2` block
contract. Include [`merkle_search_tree.h`](../include/Tools/DataStructures/Hamt/merkle_search_tree.h)
and compile [`merkle_search_tree.c`](../src/merkle_search_tree.c). On Windows link `bcrypt`; on
non-Windows hosts link OpenSSL Crypto.

This document covers the core/wire milestone. Content-store persistence, proof exchange, sync, and
three-way merge are intentionally a later layer over the stable block visitor and policy contract.

## Deterministic Policy

A `tds_merkle_policy` binds these four byte strings into one SHA-256 domain:

1. the algorithm identifier `mst-sha256-b16-v2`;
2. the caller's semantic policy identifier;
3. the key codec identifier;
4. the value codec identifier.

The domain preimage is byte `0x50`, followed by each field as an unsigned big-endian 32-bit length
and its exact bytes. A policy identifier must be non-empty well-formed UTF-8 containing at least one
non-whitespace code point. Every codec identifier must be well-formed UTF-8 with no Unicode
whitespace at either edge and end in `-v` plus one or more ASCII decimal digits. Identifiers are
copied into the policy and therefore need not outlive `tds_merkle_policy_create`.

The empty-tree preimage is ASCII `MST2`, byte `0`, and the 32-byte domain digest. A key's layer hash
uses byte `0x4b` with two framed fields: the domain digest and canonical key bytes. Its layer is the
number of leading zero hexadecimal nibbles in that hash, from 0 through 64. This geometric B=16
assignment makes shape depend only on policy-bound contents, never update history.

## Exact MST2 Node Block

Every non-empty node is serialized as one canonical block and named by SHA-256 of the complete
block:

| Field | Encoding |
| --- | --- |
| Magic | four bytes, ASCII `MST2` |
| Kind | byte `1` for a node block |
| Domain | 32-byte policy-domain digest |
| Layer | one byte, 0 through 64 |
| Subtree count | unsigned big-endian 32-bit integer |
| Entry count | unsigned big-endian 32-bit integer |
| Entries | for each entry: key length, key bytes, value length, value bytes |
| Children | exactly `entry count + 1` digests; an absent child uses the policy's empty digest |

Lengths are unsigned big-endian 32-bit integers. Entries in a block share its layer and are strictly
ordered by the configured comparator. Child layers are strictly lower and their keys stay between
the adjacent separators. `tds_merkle_search_tree_visit_blocks` exposes borrowed `(digest, bytes)`
pairs in preorder without transferring ownership.

The cross-language single-entry golden policy is `golden-int-string-v1`, with `i32-be-v1` keys and
`nullable-utf8-v1` values. Its domain, empty digest, and `{42: "forty-two"}` root are respectively:

```text
fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2
98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3
1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94
```

The native tests compare every byte of that block with the C# and Rust golden vector.

## Type And Codec Hooks

The policy owns no caller context; allocator, type, comparator, equality, and codec contexts must
remain valid until the last policy/tree handle is disposed. A type policy supplies object size and
a stable, non-null `type_identity` tag. A null copy callback means byte-copy the fixed-size object.
If a destroy callback is present, copy is required and must construct an independently owned value
or fail without leaving ownership in the destination.

Encoding is fallible and deliberately two-pass. The first call receives `(destination == NULL,
destination_size == 0)` and reports the exact size. The second receives exactly that many
library-owned bytes and must report the same size. A mismatch is
`TDS_MERKLE_INCONSISTENT_POLICY`; no node is published. Decode constructs a value in uninitialized
storage and is used by later untrusted-wire ingestion; a successful decode will be re-encoded and
must reproduce the original bytes before publication.

Built-in canonical codecs are:

- `i32-be-v1`: exactly four two's-complement big-endian bytes;
- `i64-be-v1`: exactly eight two's-complement big-endian bytes;
- `nullable-utf8-v1`: byte `0` for null, or byte `1` plus strict well-formed UTF-8;
- `nullable-bytes-v1`: byte `0` for null, or byte `1` plus arbitrary bytes;
- `guid-rfc4122-v1`: exactly 16 RFC 4122 network-order bytes.

The nullable type helpers deep-copy their payload through the configured allocator. Null and a
present empty payload are distinct (`00` versus `01`). UTF-8 rejects overlong forms, surrogates,
truncation, and values beyond U+10FFFF.

## Handle Ownership And Publication

`tds_merkle_policy` and `tds_merkle_search_tree` are owning handles, not trivially copyable values.
Use their `copy`, `move`, and `dispose` functions. Stored keys, values, canonical byte arrays,
entries, nodes, and policies are immutable reference-counted objects. Reference counts are atomic;
node release is intrusive and iterative, so disposal allocates no memory and does not recurse.

Producing operations accept exact source/result aliasing:

```c
tds_merkle_status status =
    tds_merkle_search_tree_set(&tree, &key, &value, &tree);
```

Every allocation, callback, encoding, hash, and node construction completes before publication.
On failure an aliased source is unchanged and a distinct, zero-initialized result remains
untouched. A distinct successful result owns a policy reference and root reference and must be
disposed. Replacing a comparator-equivalent key preserves the first stored key representative;
bulk creation is stable, retaining the first representative and last value.

## Core Operations And Relations

The core provides bulk construction, lookup, persistent set/remove/clear, ordered visitation,
inclusive range visitation, semantic map equality, digest-pruned streaming diff, and structural
diagnostics. A fixed 65-frame iterator bounds ordered traversal storage. Range traversal prunes
intervals using the comparator.

Compatibility is intentionally layered:

- `content_equals` compares only policy-domain and root digests in O(1), treating SHA-256 collision
  resistance as the content-addressing assumption;
- `map_equals` and `diff` require equal domains and identical key/value `type_identity` tags before
  invoking typed callbacks;
- `shared_node_count` requires the exact same policy object, because pointer sharing exists only
  within one ownership lineage.

Diff prunes identical node pointers and equal block digests. When separators align it recurses only
into changed children; otherwise it performs an ordered merge. Visitors observe additions,
removals, and changes in key order. Visitor failure stops immediately; effects of earlier calls are
not rolled back.

`tds_merkle_search_tree_validate` re-encodes every stored key and value, recomputes key layers,
checks strict ordering and child bounds, recomputes cached count/height/block metadata, reconstructs
every canonical block, and checks its digest. Outputs are published only on a successful validation
call. Shape, block, root-identity, node-identity, and shared-node APIs are diagnostics and expose
borrowed identities only.

## Complexity And Concurrency

Let `h` be tree height and `n` entry count. Lookup and persistent point updates visit O(h) blocks;
updates allocate a new changed path and share every untouched subtree. Ordered visitation is O(n),
range visitation is O(h + reported entries) under ordinary interval pruning, and bulk construction
is O(n log n). Block metadata, root digest, size, height, and block count are O(1).

Independent handles may be read concurrently. Atomic reference counts also allow retained snapshots
to be copied and disposed across threads, provided caller callbacks, contexts, and represented
objects obey their own concurrency requirements. No operation mutates published content.
