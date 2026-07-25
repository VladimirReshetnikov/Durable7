# C Merkle Search Tree

- Status: Current core, ordered cursor, persistence, proof, synchronization, and merge specification
- Created (UTC): 2026-07-12T05:30:00Z
- Repository HEAD: 2a2c92d10d308a18793067106b1ef10d3748f0ba
- Audience: C consumers, maintainers, and cross-language port reviewers
- Scope: `mst-sha256-b16-v2`, MST2/MSP2, persistent core/cursor/store API, verification, sync, and merge

The C17 Merkle search tree is an immutable ordered content-addressed map. It is the type-erased C
port of the C# reference and implements the exact `mst-sha256-b16-v2` hashing and `MST2` block
contract. Include [`merkle_search_tree.h`](../include/durable7/hamt/merkle_search_tree.h)
and compile [`merkle_search_tree.c`](../src/merkle_search_tree.c). On Windows link `bcrypt`; on
non-Windows hosts link OpenSSL Crypto.

The public C17 surface includes the complete persistence tier: immutable block and pack handles,
generic and in-memory stores, bounded verified load/import, point and range proofs, closure-pruned
and iterative synchronization, and present-null-safe typed three-way merge.

## Deterministic Policy

A `d7_merkle_policy` binds these four byte strings into one SHA-256 domain:

1. the algorithm identifier `mst-sha256-b16-v2`;
2. the caller's semantic policy identifier;
3. the key codec identifier;
4. the value codec identifier.

The domain preimage is byte `0x50`, followed by each field as an unsigned big-endian 32-bit length
and its exact bytes. A policy identifier must be non-empty well-formed UTF-8 containing at least one
non-whitespace code point. Every codec identifier must be well-formed UTF-8 with no Unicode
whitespace at either edge and end in `-v` plus one or more ASCII decimal digits. Identifiers are
copied into the policy and therefore need not outlive `d7_merkle_policy_create`.

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
the adjacent separators. `d7_merkle_search_tree_visit_blocks` exposes borrowed `(digest, bytes)`
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
`D7_MERKLE_INCONSISTENT_POLICY`; no node is published. Decode constructs a value in uninitialized
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

`d7_merkle_policy` and `d7_merkle_search_tree` are owning handles, not trivially copyable values.
Use their `copy`, `move`, and `dispose` functions. Stored keys, values, canonical byte arrays,
entries, nodes, and policies are immutable reference-counted objects. Reference counts are atomic;
node release is intrusive and iterative, so disposal allocates no memory and does not recurse.

Producing operations accept exact source/result aliasing:

```c
d7_merkle_status status =
    d7_merkle_search_tree_set(&tree, &key, &value, &tree);
```

Every allocation, callback, encoding, hash, and node construction completes before publication.
On failure an aliased source is unchanged and a distinct, zero-initialized result remains
untouched. A distinct successful result owns a policy reference and root reference and must be
disposed. Replacing a comparator-equivalent key preserves the first stored key representative;
bulk creation is stable, retaining the first representative and last value.

The same ownership discipline applies to `d7_merkle_block`, `d7_merkle_block_pack`,
`d7_merkle_sync_plan`, `d7_merkle_proof`, `d7_merkle_memory_block_store`, and
`d7_merkle_three_way_merge_result`: use the matching `copy`, `move`, and `dispose` operations.
Handles produced from a tree retain its exact policy representation, including allocator and typed
object callback lifetime. Public constructors that instead accept a standalone allocator copy that
allocator value; its context must remain valid until the final handle copy is disposed.

`d7_merkle_memory_block_store_as_store` returns a borrowed callback adapter. At least one owning
memory-store handle must outlive every adapter call. `try_get` returns an owned block snapshot, not
a borrowed slot; the snapshot remains valid across concurrent removal, clear, and store disposal.
The memory store serializes count/contains/get/put/remove/clear, keeps digests sorted, and makes
same-digest insertion a race-safe three-state operation: `ADDED`, `PRESENT_IDENTICAL`, or
`CONFLICT`. A conflict also returns `D7_MERKLE_VERIFICATION_FAILURE`. Its lock is non-recursive,
but allocator/deallocator callbacks, block destruction, and digest visitors all run outside that
lock and may reenter live adapter reads. Final owning-handle disposal ends adapter lifetime before
the allocator tears down the store itself. On non-Windows C11 threads, lock acquisition failure is
reported as `D7_MERKLE_CALLBACK_FAILURE`; an impossible unlock failure is a process-fatal internal
synchronization invariant rather than permission to continue with an unknown lock state.

Generic store wrappers use private scratch outputs. Count/contains/remove outputs are published only
on `OK`. `try_get` accepts `OK + found` only with a valid owning block and accepts `OK + not found`
only with an empty block; it disposes callback scratch on failure. `put` accepts only `OK + ADDED`,
`OK + PRESENT_IDENTICAL`, or `VERIFICATION_FAILURE + CONFLICT`. Inconsistent callback combinations
become `D7_MERKLE_CALLBACK_FAILURE`, and caller outputs remain untouched. Callback contexts are
borrowed for the lifetime of adapter use.

## Ordered Persistent Cursor

`d7_merkle_search_tree_cursor` owns a retained tree snapshot plus a comparer-order rank gap. Create
one at a rank, start, end, lower bound, upper bound, or exact-key lower-bound result. `at_key`
publishes a separate `found` flag, so a miss still returns a usable insertion gap. Peeks return
borrowed `d7_merkle_search_entry_ref` values that remain valid until the cursor is destroyed.

Navigation and edits are immutable. `move_previous`, `move_next`, and `seek` return a new cursor over
the same snapshot. `insert` rejects an existing key with `D7_MERKLE_DUPLICATE_KEY` and also rejects
a key whose lower-bound rank is not the current gap. `put` updates the exact next entry or inserts
at a missing lower-bound gap. `set_next_value` retains the stored key representative;
`delete_previous` moves the gap left and `delete_next` keeps it fixed. `snapshot` returns an owning
tree copy and never consumes the cursor.

Use `d7_merkle_search_tree_cursor_copy` for a second live owner and
`d7_merkle_search_tree_cursor_destroy` for every initialized cursor. A zeroed or destroyed cursor
is invalid but safely destroyable. Producing operations support exact source/result aliasing and
otherwise require a distinct output to be zeroed or destroyed; failure leaves both source and
output unchanged. Rank and neighbor lookup use cached subtree counts. Cursor edits call the
ordinary canonical set/remove operations, so policy, representative, encoding, digest,
failure-atomicity, and block-persistence behavior are identical to direct tree edits. Cursor state
is local navigation state and is never part of `MST2`, `MSP2`, a pack, proof, or store.

## Verified Persistence

`d7_merkle_search_tree_export_pack` emits the complete closure in deterministic preorder.
`export_blocks` emits exactly the requested unique digests in caller order and rejects missing or
duplicate requests. `save` preflights every destination digest before its first put, then reports
how many blocks were newly added. A generic external store is not transactional: an unexpected
callback failure during the subsequent put sequence can leave a verified prefix stored, while the
count output remains untouched. Predictable same-digest conflicts are detected by preflight and
therefore cause zero puts.

`load` accepts a root digest, policy, store, and optional seven-field verification budget. It hashes
each unique block before decoding; checks MST2 magic, domain, lengths, canonical codec round trips,
key layers and ordering; validates child levels and full subtree bounds; reconstructs subtree
counts and canonical bytes; and publishes only a fully verified immutable tree. The empty digest
loads as an empty tree but still requires a valid store adapter.

`import_pack` first rejects unsupported algorithms and foreign domains, then authenticates and
canonically decodes every supplied block. It overlays supplied blocks over the optional destination
store and requires the declared root's complete closure to verify before any destination put or
tree publication. Supplied authenticated blocks that are not reachable from the declared root are
legal and are committed as useful partial-sync state. The declared root closure itself may not be
partial. Destination conflicts are preflighted across the entire pack before its first put.

Verification failures return `D7_MERKLE_VERIFICATION_FAILURE` plus a structured kind and optional
block digest. Operational allocator, crypto, codec, comparator, and store failures retain their
ordinary status. `verified_block_count` and `verified_byte_count` count unique authenticated input
work and are published on both success and verification failure.

The seven budgets are independent:

| Field | Bound |
| --- | --- |
| `max_block_count` | unique blocks |
| `max_total_byte_count` | unique block bytes plus proof query bytes |
| `max_block_byte_count` | one block |
| `max_depth` | root-to-leaf verification depth |
| `max_entry_count` | total entries in unique decoded blocks |
| `max_child_references_per_block` | one block's exact `entry_count + 1` references |
| `max_proof_query_byte_count` | canonical MSP2 query bytes |

All fields must be nonzero; the per-block and query byte limits may not exceed the total-byte limit.
Block count/size/depth gates run before verification-table allocation, hashing, or decoding for that
block.

## Exact MSP2 Proofs

A proof envelope owns the algorithm identifier, domain digest, root digest, proof kind, canonical
query bytes, and an ordered list of authenticated MST2 step blocks. Each step also owns a sorted,
duplicate-free list of child indexes expanded by later steps. Point proofs expand at most the one
search-path child. Range proofs expand exactly the non-empty children whose open separator interval
intersects the inclusive requested range; all other child digests remain authenticated closures.

The query byte grammar is:

| Field | Encoding |
| --- | --- |
| Magic | four bytes, ASCII `MSP2` |
| Kind | byte `0` membership, `1` nonmembership, or `2` range |
| First length/value | unsigned big-endian 32-bit length plus canonical key bytes |
| Second length/value | membership: canonical value; range: canonical maximum key; absent for nonmembership |

Membership creation includes the canonical stored value in the query. Verification therefore proves
the exact key/value pair, not merely key presence. Nonmembership proves the canonical search stops
without the key. Range verification checks the complete canonical expansion frontier, so omitting,
adding, reordering, or redirecting a step/expansion invalidates the proof.

Proof verification returns `D7_MERKLE_OK` with `is_valid == false` for an untrusted invalid proof;
operational failures remain non-OK statuses. Its security-sensitive preflight order is fixed:

1. reject an oversized query with zero verified blocks and zero verified bytes;
2. account accepted query bytes;
3. reject excessive step count;
4. reject any excessive expansion count;
5. check algorithm and domain;
6. only then allocate, hash, decode, or invoke codecs/comparators.

Thus query-limit failure outranks even a foreign envelope, while step/expansion-limit failure reports
zero blocks and exactly the accepted query byte count.

## Synchronization

`create_sync_pack` walks a target tree in preorder and prunes an entire subtree whenever the receiver
reports its root digest present. Use it when receiver presence means a previously verified complete
closure. The resulting pack is deterministic and contains no redundant descendants.

For a genuinely partial receiver, use `plan_sync`. It compares target and local roots and walks only
through receiver-present target blocks. The first absent digest on every reachable branch becomes a
request; descendants below that digest wait for a later round. Export those requested blocks, insert
them, and repeat until `requires_blocks` is false. The plan records requested digests plus examined
block/byte counts and owns the exact target policy lifetime. This iterative frontier converges in at
most one round per target height when every response is delivered.

## Typed Three-Way Merge

`d7_merkle_search_tree_merge(base, left, right, ...)` performs an ordered three-way merge. All
three operands must retain the exact same policy representation—not merely equal domains and type
tags—because merged entries reuse allocator-owned typed objects. Per key it applies the usual rules:

- equal left/right states win directly;
- when left equals base, right wins;
- when right equals base, left wins;
- otherwise the resolver chooses base, left, right, a replacement value, deletion, or unresolved.

Presence is explicit. `present == true` with a nullable wrapper whose `has_value == false` is a
semantic null value; `present == false` is deletion/absence. An ordinary unresolved merge returns
`D7_MERKLE_OK` with `success == false`, one or more owned conflict records, and no tree. It is not an
operational error status. Resolver or allocator failure leaves the result untouched. If every
conflict resolves, the result owns one canonical tree and no conflicts. A replacement retains the
chosen key representative and encodes the new value under the shared policy.

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

`d7_merkle_search_tree_validate` re-encodes every stored key and value, recomputes key layers,
checks strict ordering and child bounds, recomputes cached count/height/block metadata, reconstructs
every canonical block, and checks its digest. Outputs are published only on a successful validation
call. Shape, block, root-identity, node-identity, and shared-node APIs are diagnostics and expose
borrowed identities only.

## Complexity And Concurrency

Let `h` be tree height and `n` entry count. Lookup and persistent point updates visit O(h) blocks;
updates allocate a new changed path and share every untouched subtree. Ordered visitation is O(n),
range visitation is O(h + reported entries) under ordinary interval pruning, and bulk construction
is O(n log n). Block metadata, root digest, size, height, and block count are O(1).

Complete export/save/load/import are O(b + n + z), where `b` is block count and `z` is canonical
byte volume; store lookup costs are store-defined. Point proof creation/verification is O(h) blocks.
Range proof work is proportional to its canonical expanded frontier and reported range. Sync-pack
construction and one frontier-plan round visit only unpruned target blocks. Three-way merge is
O(nb + nl + nr) ordered entries plus canonical result construction.

Independent handles may be read concurrently. Atomic reference counts also allow retained snapshots,
blocks, packs, proofs, plans, and merge results to be copied and disposed across threads, provided
caller callbacks, contexts, and represented objects obey their own concurrency requirements. The
in-memory store serializes its own operations; its borrowed adapter still requires an owning handle.
No operation mutates published tree/proof/pack content. As with any finite reference count, callers
must not create more simultaneously live copies than the platform counter can represent; retaining
a dead object or overflowing/underflowing that counter is a process-fatal ownership invariant.
