# HAMT C API Specification

- Status: Current API specification
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C `tds_hamt` API
- Scope: Public C API, ownership semantics, persistence behavior, and complexity guarantees

For practical policy setup and lifetime examples, start with the [usage guide](usage.md).
The ordered content-addressed family has its own exact
[Merkle search tree specification](merkle-search-tree.md), including the MST2 wire bytes and
fallible ownership hooks, verified persistence, MSP2 proofs, synchronization, and three-way merge.

## Overview

`tds_hamt_map` is an immutable unordered map backed by a hash-array mapped trie. Updates return a new
map value through an out parameter and leave the source map unchanged. Untouched nodes are shared by
intrusive reference count; update operations clone only the search path and any touched equal-hash
collision bucket.

`tds_hamt_set` is a value-set wrapper over the same map core. It stores set items as map keys and
uses a unit value.

`tds_int_map` / `tds_long_map` and their set wrappers are a separate explicit-width family backed
by one big-endian Patricia engine. They use integer keys directly rather than hashing and compress
every unary prefix path into a branch prefix plus its highest differing bit.

`tds_merkle_search_tree` is a separate persistent ordered family. It assigns keys deterministic
SHA-256 layers, groups same-layer separators into canonical wide blocks, and preserves exact
cross-language content addresses. Unlike the borrowed-pointer HAMT defaults, its type-erased policy
always materializes owned stored representatives and canonical encoded bytes. Its persistence API
uses atomic owning handles for blocks, packs, proofs, sync plans, and merge results, plus a generic
callback block-store interface and synchronized in-memory implementation. Store wrappers publish
outputs only for valid owning/status combinations; the memory implementation never invokes user
allocator, deallocator, block-destruction, or visitor callbacks while holding its lock.

The C API is type-erased. Keys, values, and set items are `void *` payloads interpreted by policy
callbacks. A policy may simply store borrowed pointers, or it may retain/release payloads to give
collections ownership of copied or reference-counted objects.

## Merkle Persistence Contract

The Merkle status family distinguishes operational failure from untrusted verification failure.
`TDS_MERKLE_VERIFICATION_FAILURE` is accompanied by a structured failure kind and verified
block/byte counters. Load/import enforce seven caller-configurable limits before publication and
validate digest, domain, canonical codec round trips, key layers/order, child bounds, subtree counts,
and exact reconstructed MST2 bytes. Import verifies the declared root closure before destination
preflight and writes; authenticated unreachable supplied blocks remain legal partial-sync state.

Point/nonmembership/range proofs bind canonical `MSP2` query bytes to an ordered set of MST2 blocks
and exact expanded-child indexes. Query, step, and expansion limits precede allocator, hash, codec,
and comparator work. Invalid untrusted proofs return `TDS_MERKLE_OK` with `is_valid == false`;
allocator or callback failure remains a non-OK operational status.

Three-way merge requires the exact same policy representation across base/left/right because it
reuses typed entry objects. Absence is represented by `present == false`; a nullable value whose
wrapper is present but contains semantic null remains distinct. Unresolved conflicts are a normal
owned result (`TDS_MERKLE_OK`, `success == false`, no tree), not an error status. See the dedicated
[Merkle specification](merkle-search-tree.md) for the full store, ownership, sync, and complexity
contracts.

## Patricia Integer Contract

Include `patricia.h` for `tds_int_map`, `tds_long_map`, `tds_int_set`, and `tds_long_set`. The two
map widths accept type-erased values governed by `tds_patricia_value_policy`; the set wrappers store
a unit value. Sign-bit transforms map signed keys to lexicographically sortable unsigned paths, so
visitor traversal is ascending signed order and covers the full `INT32_MIN`/`INT32_MAX` or
`INT64_MIN`/`INT64_MAX` domain.

Point updates path-copy only the compressed search path. Setting an equal value and removing an
absent key reuse the source root. Nodes cache subtree cardinality, and `union`, `intersect`, and
`except` align prefixes before descending, share untouched subtrees, and preserve a source root when
the algebra is a semantic no-op. Union is right-biased for unequal overlapping values; intersection
keeps the left value. The `*_union_with` and `*_intersect_with` variants invoke a typed combining
callback only for keys present on both sides.

Combining callbacks receive `(key, left, right, context)` and return a borrowed value pointer. The
map retains that value through its policy before publishing it, so an allocating retain callback can
still report `TDS_HAMT_OUT_OF_MEMORY`. Callback execution is synchronous; its context need remain
valid only for the call, while any pointed-to returned value must obey the configured retain policy.
Both operands of structural algebra must have identical equality/retain/release callback and context
identities. A result may alias either operand.

## Ownership

Maps and sets are small value structs. Copying them with assignment does not retain the root. Use
`tds_hamt_map_clone` / `tds_hamt_set_clone` when two live values should share the same version, and
call `tds_hamt_map_destroy` / `tds_hamt_set_destroy` for every initialized value.

The default policy hashes and compares pointer identity and stores borrowed pointers. Custom
policies can provide:

- hash and equality callbacks for keys or set items;
- a value equality callback for no-op replacement detection;
- retain and release callbacks for keys, values, or set items;
- one opaque context pointer passed to every callback.

Retain callbacks are value-returning: an allocating retain callback reports failure by returning
`NULL` for a non-`NULL` input, which the library maps to `TDS_HAMT_OUT_OF_MEMORY` and unwinds
(already-retained payloads on the failed path are released; no partially retained node is
published). Retaining a `NULL` key or value yields `NULL` and is not an error.

`try_remove` reports the removed value pointer as stored in the source map. The pointer is valid
only while the source map value itself remains alive (not destroyed): the call performs no retain
on the caller's behalf, and with a copying retain policy the payload is owned by the source's
nodes, so destroying the source frees it even when the result map still contains sibling entries.
When `result` aliases the source map no such lifetime exists — the previous version's root is
released inside the call — so an aliased `try_remove` always reports a `NULL` removed value
pointer (the removed flag stays accurate). Pass a distinct `result` when the removed value pointer
is needed.

Every operation's `result` may alias the source map or set: the library releases the overwritten
version's root before publishing the new one, so `tds_hamt_map_set(&map, k, v, &map)`-style
in-place updates are safe (the previous version is no longer reachable afterwards). On a rejected
duplicate, `tds_hamt_map_add` leaves an aliased `result` holding the unchanged source version,
while a distinct `result` is left destroyed (empty, not a live handle).

## Hash Trie Shape

The trie uses 32-way logical branching and consumes five hash bits per level. CHAMP branch nodes
store separate data and node maps, a compact inline `(hash,key,value)` payload run, and a child-only
subtrie run in one flexible allocation; each slot is the population count below the selected bit.
Deletion promotes a singleton leaf child into its parent payload run. Unequal keys with identical
full 32-bit hashes are stored in immutable collision buckets and
compared linearly with the configured equality callback.

Enumeration order follows trie bitmap order and collision-bucket order. It is stable for an
unchanged version but is not insertion order or sorted order. Iterators keep at most seven inline
branch frames and no heap traversal stack. A copied iterator advances independently as long as the
source map or set remains alive.

## Map Contract

- `tds_hamt_map_create(policy)` returns an empty map using the supplied policy or pointer-identity
  defaults.
- `tds_hamt_map_create_range(policy, entries, count, result)` adds entries in array order with
  last-wins values.
- `tds_hamt_map_set` adds or replaces a key.
- `tds_hamt_map_set_many` adds or replaces entries in array order.
- `tds_hamt_map_add` adds a key and returns `TDS_HAMT_DUPLICATE_KEY` when an equivalent key already
  exists.
- `tds_hamt_map_try_add` returns an added flag and rejects duplicate keys without reporting an
  error.
- `tds_hamt_map_remove` removes a key if present.
- `tds_hamt_map_try_remove` returns removed flag and removed value pointer; when `result` aliases
  the source map the removed value pointer is reported as `NULL` (see [Ownership](#ownership)).
- `tds_hamt_map_try_get` returns the stored value pointer, if present.
- `tds_hamt_map_try_get_key` returns the stored equivalent key pointer, or echoes the query pointer
  on miss.
- `tds_hamt_map_clear` returns an empty map preserving the current policy.
- `tds_hamt_map_equals` compares contents when the callback/context policy identities match.
- `tds_hamt_map_diff` calls a visitor with `ADDED`, `REMOVED`, and `CHANGED` records. It performs no
  result allocation and returns `TDS_HAMT_INVALID_ARGUMENT` for incompatible policies.

When replacing an existing key, the originally stored key is re-retained through the policy: with
identity or reference-counting retain callbacks the stored key *pointer* is preserved (matching the
C# reference's key-instance retention), while a copying retain callback necessarily produces a
fresh copy. When the existing value compares equal under the value equality callback, the root is
reused and the stored value pointer is retained.

## Set Contract

- `tds_hamt_set_create` and `tds_hamt_set_create_range` mirror the map factories.
- `add`, `try_add`, `remove`, `try_remove`, `contains`, `try_get_value`, and `clear` mirror map
  behavior.
- `union_many`, `intersect_many`, `except_many`, and `symmetric_except_many` return new persistent
  sets.
- `is_subset_of_many`, `is_proper_subset_of_many`, `is_superset_of_many`,
  `is_proper_superset_of_many`, `overlaps_many`, and `equals_many` interpret equality through the
  set's policy callbacks. They report the relation through a `bool *result` out-parameter and
  return `tds_hamt_status`, so an allocation failure while materializing the internal probe set is
  distinguishable from a genuine negative answer.

Set operations that need distinct right-side membership materialize their argument into a temporary
`tds_hamt_set` using the receiver's policy. Superset and overlap checks stream their argument.

## Complexity

Let `w` be the hash width (32 bits), `b` be the branch factor (32), and `c` be the length of an
equal-hash collision bucket.

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes.
- Enumeration: O(n) time with at most seven inline branch frames.
- Map `create_range` / set `create_range`: O(n * update-cost), with structural sharing during the
  build.
- Set algebra implemented from public operations: O((n + m) * update-cost), except for streaming
  predicates.
- Patricia lookup, insert, and remove: O(W), with `W` fixed at 32 or 64 and usually far fewer hops
  because unary paths are compressed.
- Patricia structural algebra: O(v), where `v` is the prefix structure visited after shared-root and
  disjoint-prefix pruning; O(n + m) in the worst case. Result cardinality is read from cached subtree
  metadata rather than recomputed by traversal.

Update allocation is O(b * depth + c) array storage and O(depth + c) allocated node objects for the
changed path and any touched collision bucket. Published nodes are immutable apart from reference counts.

## Concurrency

Reference counts are non-atomic. Concurrent read-only access to already-retained map/set handles is safe when
the configured hash/equality callbacks and pointed-to payloads are themselves safe to read concurrently. A
handle must remain alive for the full read; do not copy or destroy that handle concurrently.

Copying, updating, clearing, set algebra, and destroying versions retain or release nodes, including untouched
nodes shared with sibling snapshots. Serialize those operations across every structurally shared lineage:
derive versions single-threaded (or under one external lock), publish already-retained snapshots to readers,
then join/quiesce those readers before releasing their handles. Independent collections proven not to share
nodes may be updated concurrently.
