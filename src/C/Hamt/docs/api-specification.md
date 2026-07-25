# HAMT C API Specification

- Status: Current API specification
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C `d7_hamt` API
- Scope: Public C API, ownership semantics, persistent and one-way edit-session behavior, and complexity guarantees

For practical policy setup and lifetime examples, start with the [usage guide](usage.md).
The ordered content-addressed family has its own exact
[Merkle search tree specification](merkle-search-tree.md), including the MST2 wire bytes and
fallible ownership hooks, verified persistence, MSP2 proofs, synchronization, and three-way merge.

## Overview

`d7_hamt_map` is an immutable unordered map backed by a hash-array mapped trie. Updates return a new
map value through an out parameter and leave the source map unchanged. Untouched nodes are shared by
intrusive reference count; update operations clone only the search path and any touched equal-hash
collision bucket.

`d7_hamt_set` is a value-set wrapper over the same map core. It stores set items as map keys and
uses a unit value.

The composition-first `d7_hamt_map_patch`, `d7_hamt_directed_graph`, and
`d7_hamt_indexed_map` facades retain the underlying callback policies and explicit ownership.
Patch entries carry a separate presence flag so a present null pointer is not deletion; apply
preflights every expected state. Graph edge insertion installs missing endpoints and vertex removal
removes both incident directions. Indexed-map updates move exactly one primary representative
between secondary groups when the selector changes class. All three use clone/move/destroy handles,
write outputs only after success, and expose structural validation.

`d7_hamt_bag` is an immutable unordered multiset wrapper over the map core. It stores one retained
item representative and one library-owned positive `int32_t` multiplicity per equivalence class,
plus a checked nonnegative `int64_t` expanded total. It intentionally has no public builder or
transient surface: construction and every edit publish ordinary persistent versions.

`d7_hamt_multimap` is an immutable set-valued multimap. A key maps to one nonempty persistent set
of distinct values under an independent value policy; empty groups are never stored. It tracks
distinct-key count and a checked `int64_t` pair count, retains the first representative of every key
and value class, and supports pair/group edits plus receiver-policy union, intersection, and
difference. Group sets are reference-counted values stored by the outer CHAMP, so untouched groups
and outer paths share across versions.

`d7_hamt_relation` maintains the same pair set in forward and inverse multimaps. Pair insertion and
removal build both successors before publishing either, so allocation failure cannot expose a
one-sided relation. Forward and reverse lookup use their respective policies; validation checks
counts, both component invariants, and exact inverse membership.

`d7_hamt_map_transient` and `d7_hamt_set_transient` are one-way, single-owner edit-session
surfaces over those persistent values. They preserve the C# transient's lifecycle and collection
semantics in C ownership terms, but intentionally delegate changed point edits to the established
persistent path-copy operations. They are not an owner-token in-place engine and carry no edit-
throughput claim.

`d7_int_map` / `d7_long_map` and their set wrappers are a separate explicit-width family backed
by one big-endian Patricia engine. They use integer keys directly rather than hashing and compress
every unary prefix path into a branch prefix plus its highest differing bit.

`d7_merkle_search_tree` is a separate persistent ordered family. It assigns keys deterministic
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
`D7_MERKLE_VERIFICATION_FAILURE` is accompanied by a structured failure kind and verified
block/byte counters. Load/import enforce seven caller-configurable limits before publication and
validate digest, domain, canonical codec round trips, key layers/order, child bounds, subtree counts,
and exact reconstructed MST2 bytes. Import verifies the declared root closure before destination
preflight and writes; authenticated unreachable supplied blocks remain legal partial-sync state.

Point/nonmembership/range proofs bind canonical `MSP2` query bytes to an ordered set of MST2 blocks
and exact expanded-child indexes. Query, step, and expansion limits precede allocator, hash, codec,
and comparator work. Invalid untrusted proofs return `D7_MERKLE_OK` with `is_valid == false`;
allocator or callback failure remains a non-OK operational status.

Three-way merge requires the exact same policy representation across base/left/right because it
reuses typed entry objects. Absence is represented by `present == false`; a nullable value whose
wrapper is present but contains semantic null remains distinct. Unresolved conflicts are a normal
owned result (`D7_MERKLE_OK`, `success == false`, no tree), not an error status. See the dedicated
[Merkle specification](merkle-search-tree.md) for the full store, ownership, sync, and complexity
contracts.

The Merkle tree also exposes an explicit-lifetime comparer-order cursor. It retains one tree
snapshot and a rank gap, supports rank/lower/upper/exact factories, borrowed adjacent peeks,
persistent insert/put/value-update/delete edits, and non-consuming snapshot. Strict duplicate
insertion returns `D7_MERKLE_DUPLICATE_KEY`. Exact source/result aliasing is supported and a
distinct output is installed only on success. The dedicated
[Merkle specification](merkle-search-tree.md#ordered-persistent-cursor) owns the full lifetime and
edit contract.

## Patricia Integer Contract

Include `patricia.h` for `d7_int_map`, `d7_long_map`, `d7_int_set`, and `d7_long_set`. The two
map widths accept type-erased values governed by `d7_patricia_value_policy`; the set wrappers store
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
still report `D7_HAMT_OUT_OF_MEMORY`. Callback execution is synchronous; its context need remain
valid only for the call, while any pointed-to returned value must obey the configured retain policy.
Both operands of structural algebra must have identical equality/retain/release callback and context
identities. A result may alias either operand.

Patricia maps and sets expose explicit-lifetime ascending-order cursors. Factories accept a rank,
start/end, lower/upper bound, or exact lookup; a miss retains its usable lower-bound gap. Map
cursors support strict insert, put, next-value replacement, adjacent deletion, and snapshot. Set
cursors support add/insert, adjacent deletion, and snapshot. Peeks borrow stored representatives
from the retained cursor version. Every initialized cursor is cloned/destroyed explicitly, and
producing operations support exact source/result aliasing under the same failure-atomic ownership
rules as the owning Patricia values.

## Ownership

Maps, sets, and bags are small value structs. Copying them with assignment does not retain the root.
Use `d7_hamt_map_clone`, `d7_hamt_set_clone`, or `d7_hamt_bag_clone` when two live values should
share the same version, and call the matching `destroy` function for every initialized value.

The default policy hashes and compares pointer identity and stores borrowed pointers. Custom
policies can provide:

- hash and equality callbacks for keys or set items;
- a value equality callback for no-op replacement detection;
- retain and release callbacks for keys, values, or set items;
- one opaque context pointer passed to every callback.

Retain callbacks are value-returning: an allocating retain callback reports failure by returning
`NULL` for a non-`NULL` input, which the library maps to `D7_HAMT_OUT_OF_MEMORY` and unwinds
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
version's root before publishing the new one, so `d7_hamt_map_set(&map, k, v, &map)`-style
in-place updates are safe (the previous version is no longer reachable afterwards). On a rejected
duplicate, `d7_hamt_map_add` leaves an aliased `result` holding the unchanged source version,
while a distinct `result` is left destroyed (empty, not a live handle).

Bag result parameters may likewise alias either input. A distinct result must not already own a
live bag. On every non-OK status the result bytes, both input handles, retained representatives,
and cached totals remain unchanged. On success the result owns exactly one handle, including when
it root-shares a logical no-op. Bag lookups and iterators return borrowed representative pointers;
keep the source bag alive until the pointer is no longer used. Entry iteration copies the
multiplicity into the public `d7_hamt_bag_entry` and never exposes the internal owned count object.

## One-Way CHAMP Edit Sessions

`d7_hamt_map_transient_create` / `d7_hamt_set_transient_create` start empty sessions under the
normalized supplied policy. `d7_hamt_map_to_transient` / `d7_hamt_set_to_transient` allocate one
small opaque session state and retain the source root without walking or copying the trie. The
source remains an independent immutable snapshot. `*_transient_persist` transfers the session's
own retained persistent handle to the caller, marks the state consumed, and does not walk the trie.
The output passed to a create/adopt/persist operation must not already own a live value.

Transient handles follow the same explicit C ownership discipline as persistent handles. Do not
copy a live transient by assignment. `*_transient_clone` creates another owning handle for the same
logical session without copying collection content; call `*_transient_destroy` for every initialized
handle. A successful publication through any clone consumes the shared session. Every subsequent
read, edit, iterator creation, clone, or publication through any alias returns
`D7_HAMT_TRANSIENT_CONSUMED`. Destroy remains valid and idempotent for zero/destroyed handles.

Map sessions expose policy, count, contains, stored-key lookup, value lookup, `set`, duplicate-
rejecting `add`, `try_add`, `remove`, `try_remove`, `clear`, iteration, and terminal `persist`. Set
sessions expose the corresponding item policy, count, contains, stored-representative lookup,
idempotent `add`, `try_add`, `remove`, `try_remove`, `clear`, all six set relations over both
`*_many` inputs and persistent-set operands, iteration, and `persist`. Relation operations use the
active transient as receiver, and therefore preserve its hash/equality policy and duplicate-
collapsing semantics. Their boolean output is published only on `D7_HAMT_OK`; allocation failure
or a consumed session leaves it untouched. Policies and their context pointer are preserved exactly.
Map replacement and set insertion retain the first equivalent stored key/item; equal-value
replacement, duplicate try-add, absent removal, and clearing an empty session preserve root identity.

Changed point edits call the ordinary persistent operation into a temporary map and commit that
complete result only after every allocation and retaining callback succeeds. Consequently an
`OUT_OF_MEMORY` result leaves session content, root identity, version, policy, output flags, and
captured iterator validity unchanged. Publication has no allocation step: an invalid output pointer
returns `D7_HAMT_INVALID_ARGUMENT` and leaves the session active for retry. Hash/equality callbacks
retain their existing infallible C callback shape; retain callbacks report allocation failure by
returning `NULL` for a non-`NULL` input.

Transient iterators borrow the opaque session state and do not retain it. Keep at least one owning
session handle alive until iteration ends. A changed edit increments the session version, and an
older iterator then returns `D7_HAMT_TRANSIENT_MODIFIED` without touching its output parameters.
Logical no-ops and failed edits do not invalidate it. Publication makes an iterator return
`D7_HAMT_TRANSIENT_CONSUMED`. As with persistent iterators, a copied iterator advances
independently while the session remains active and at the captured version.

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

- `d7_hamt_map_create(policy)` returns an empty map using the supplied policy or pointer-identity
  defaults.
- `d7_hamt_map_create_range(policy, entries, count, result)` adds entries in array order with
  last-wins values.
- `d7_hamt_map_set` adds or replaces a key.
- `d7_hamt_map_set_many` adds or replaces entries in array order.
- `d7_hamt_map_add` adds a key and returns `D7_HAMT_DUPLICATE_KEY` when an equivalent key already
  exists.
- `d7_hamt_map_try_add` returns an added flag and rejects duplicate keys without reporting an
  error.
- `d7_hamt_map_get_or_add` invokes its add factory exactly once on a miss and not at all on a hit.
- `d7_hamt_map_add_or_update` invokes exactly one add/update factory once. Both functions hash
  once, descend once, retain the first equivalent key, and return the concrete stored value
  representative through `selected_value` without a second lookup. Factory outputs are borrowed
  candidates retained through the map policy; eager null-factory validation precedes hashing, and
  callback, retention, or allocation failure publishes no output.
- `d7_hamt_map_remove` removes a key if present.
- `d7_hamt_map_try_remove` returns removed flag and removed value pointer; when `result` aliases
  the source map the removed value pointer is reported as `NULL` (see [Ownership](#ownership)).
- `d7_hamt_map_try_get` returns the stored value pointer, if present.
- `d7_hamt_map_try_get_key` returns the stored equivalent key pointer, or echoes the query pointer
  on miss.
- `d7_hamt_map_clear` returns an empty map preserving the current policy.
- `d7_hamt_map_union`, `d7_hamt_map_intersect`, `d7_hamt_map_except`, and
  `d7_hamt_map_symmetric_except` combine two maps. Union retains receiver key representatives and
  uses right values for unequal overlaps; intersection retains receiver entries.
- `d7_hamt_map_equals` compares contents when the callback/context policy identities match. It
  aligns canonical data/node bitmaps, returns immediately for every pointer-identical descendant,
  and compares only inline payloads and collision runs in the remaining trie regions.
- `d7_hamt_map_diff` performs the same lockstep descendant pruning and calls a visitor with
  `ADDED`, `REMOVED`, and `CHANGED` records. It performs no result allocation or key rehashing and
  returns `D7_HAMT_INVALID_ARGUMENT` for incompatible policies. Equal-hash collision runs retain
  unordered key matching and therefore have the existing quadratic bucket worst case.

When replacing an existing key, the originally stored key is re-retained through the policy: with
identity or reference-counting retain callbacks the stored key *pointer* is preserved (matching the
C# reference's key-instance retention), while a copying retain callback necessarily produces a
fresh copy. When the existing value compares equal under the value equality callback, the root is
reused and the stored value pointer is retained.

## Set Contract

- `d7_hamt_set_create` and `d7_hamt_set_create_range` mirror the map factories.
- `add`, `try_add`, `remove`, `try_remove`, `contains`, `try_get_value`, and `clear` mirror map
  behavior.
- `union_many`, `intersect_many`, `except_many`, and `symmetric_except_many` return new persistent
  sets.
- `d7_hamt_set_union`, `d7_hamt_set_intersect`, `d7_hamt_set_except`, and
  `d7_hamt_set_symmetric_except` accept another set and combine CHAMP slots directly when every
  callback and context pointer matches. Their same-set relation counterparts use the same path.
- `is_subset_of_many`, `is_proper_subset_of_many`, `is_superset_of_many`,
  `is_proper_superset_of_many`, `overlaps_many`, and `equals_many` interpret equality through the
  set's policy callbacks. They report the relation through a `bool *result` out-parameter and
  return `d7_hamt_status`, so an allocation failure while materializing the internal probe set is
  distinguishable from a genuine negative answer.

Compatible structural algebra caches cardinality in every node, retains pointer-identical nodes
without invoking the hash callback, and otherwise performs a failure-atomic O(n + m) slot merge.
When policy callbacks or context differ, the right operand is first normalized under the receiver's
policy so the established receiver-policy semantics remain intact.

Set operations that need distinct right-side membership materialize their argument into a temporary
`d7_hamt_set` using the receiver's policy. Superset and overlap checks stream their argument.

## Persistent Hash Bag Contract

Include `persistent_hash_bag.h` for `d7_hamt_bag`, `d7_hamt_bag_entry`, and the three iterator
types. A bag accepts the item portion of `d7_hamt_set_policy`; the implementation supplies its own
count equality/retain/release callbacks and stores multiplicities as owned heap values in an
underlying `d7_hamt_map`. `d7_hamt_bag_get_policy` recovers the normalized item policy. The same
callback/context identity rules that define map policy compatibility define bag policy
compatibility.

The point and construction contract is:

- `d7_hamt_bag_create` creates an empty policy-preserving bag and cannot fail.
- `d7_hamt_bag_create_range` consumes the item array in order, adding one occurrence at a time.
  Equivalent later items increase the count without replacing the first retained representative.
- `distinct_count`, `total_count`, and `is_empty` report equivalence classes, expanded occurrences,
  and emptiness independently.
- `contains` and `count_of` use the item policy. `try_get_value` returns the stored representative
  when present and echoes the query pointer on a miss. `try_get_entry` returns the representative
  and copied multiplicity together and zeroes its entry output on a miss.
- `add` and positive `add_copies` use `d7_hamt_map_add_or_update`, selecting the next multiplicity
  during the single update descent. They hash once, retain the existing representative on a hit,
  and path-copy only after checked arithmetic succeeds.
- `remove_copies` performs saturated subtraction, `remove_all` drops the complete class, and
  `clear` preserves policy. Zero additions/removals, missing-class removals, and clearing an empty
  bag return a root-sharing version.

An explicit copy request is an `int64_t` API value but must be in `[0, INT32_MAX]`. A negative or
larger request returns `D7_HAMT_INVALID_ARGUMENT`; validation occurs before hash, equality, retain,
or allocation callbacks. Zero returns before callbacks and shares the source root. A positive
request that would make one class exceed `INT32_MAX`, make the expanded total exceed `INT64_MAX`,
or collapse policy-incompatible argument classes beyond `INT32_MAX` returns
`D7_HAMT_OVERFLOW`. Allocation or retaining-callback failure returns
`D7_HAMT_OUT_OF_MEMORY`. No partially changed bag is published for any of these statuses.

Bag algebra is receiver-policy algebra:

- `union` selects the maximum multiplicity in each receiver-policy class;
- `intersect` selects the minimum multiplicity;
- `except` subtracts argument multiplicities from receiver multiplicities with saturation at zero;
- `sum` adds multiplicities with checked per-class and expanded-total arithmetic.

When the policies differ, the complete argument is first normalized under the receiver's hash,
equality, retain/release callbacks, and context. This is deliberately eager even when a later
algebra shortcut might otherwise ignore part or all of the argument: foreign classes can collapse,
retain callbacks can fail, and collapse can overflow. Surviving receiver classes retain receiver
representatives. An argument-only class uses the first representative encountered while
normalizing/iterating the argument. Logical no-op results share the receiver root.

`d7_hamt_bag_iterator` expands each class into `count` consecutive occurrences.
`d7_hamt_bag_distinct_iterator` returns each representative once, and
`d7_hamt_bag_entry_iterator` returns one `{ item, count }` record per class. All three follow the
underlying stable-for-one-version trie/collision order, borrow the source bag, use a fixed inline
traversal stack, and can be copied by value to obtain independently advancing cursors.

## Persistent Bidirectional Map Contract

Include `persistent_bi_map.h` for `d7_hamt_bi_map`. Each handle owns forward and inverse
`d7_hamt_map` values plus a reference-counted policy bridge. The bridge copies independent
`d7_hamt_set_policy` records for the key and value domains and dispatches each hash, equality,
retain, release, and context callback in the correct direction. Callback-owned contexts remain the
caller's lifetime responsibility until the last related bimap is destroyed.

`try_add` returns a cloned two-root source on conflict and reports key conflict before value
conflict. Strict `add` maps those cases to `D7_HAMT_DUPLICATE_KEY` and the bimap-specific
`D7_HAMT_DUPLICATE_VALUE`. `set` adds a missing free pair, preserves both roots and
representatives for a value-policy-equivalent update, replaces a present key only with a free
value, and never displaces another key. Replacement deliberately removes and reinserts both
directions. Both successor maps are complete before publication, and a failure leaves output and
source unchanged.

Lookup and removal are symmetric. A boolean presence output distinguishes stored `NULL` from a
miss. Opposite representatives returned by non-aliased removal borrow the source snapshot; the
aliased form reports `NULL` rather than a possibly released pointer, matching the base map's C
ownership rule. `inverse` is O(1), cloning and swapping two reference-counted roots. Double
inversion shares both source roots. `clear` retains both policy directions, iteration follows the
forward CHAMP order, and `debug_validate` checks both canonical maps and every cross-direction
entry. The bimap exposes no algebra, transient, builder, or displacing force-put surface and stores
approximately two map entries per pair. Handles share the base HAMT's non-atomic reference-count
rule: concurrent reads of an already-retained snapshot are safe, but clone/update/destroy on one
shared lineage must be serialized.

## Complexity

Let `w` be the hash width (32 bits), `b` be the branch factor (32), and `c` be the length of an
equal-hash collision bucket.

- Lookup, insert, replace, remove, and one-descent factory update: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes.
- Bimap lookup: one CHAMP lookup. Bimap point edits: up to two lookups and two persistent CHAMP
  updates; storage is approximately twice a single map. Inverse and handle clone are O(1) in pair
  count.
- Transient create/adoption: O(1) in trie size, with one opaque-state allocation and one root retain.
- Transient publication: O(1) in trie size, transferring the already-retained persistent handle.
- Transient lookup and point edits: the same bounds and allocation behavior as the corresponding
  persistent operations. Changed edits path-copy; no in-place-edit or amortized speedup is promised.
- Enumeration: O(n) time with at most seven inline branch frames.
- Map equality and diff: O(v + r + Σ cᵢ²), where `v` is the unmatched canonical trie region
  visited after pointer-identical descendant pruning, `r` is the number of reported differences,
  and each `cᵢ` is a visited equal-hash collision-run length. The quadratic terms come from
  unordered pairwise key matching; the overall worst case is O((n + m)²).
- Map `create_range` / set `create_range`: O(n * update-cost), with structural sharing during the
  build.
- Set algebra implemented from public operations: O((n + m) * update-cost), except for streaming
  predicates.
- Bag lookup and positive addition: O(w / log2(b) + c); positive addition selects its count in one
  trie descent. Removal may perform a lookup followed by one update.
- Bag range construction and receiver-policy algebra: O((n + m) * update-cost) in the current C
  implementation, including eager normalization when policies differ. Distinct and entry
  iteration are O(d); expanded iteration is O(t), where `d` is distinct count and `t` is total
  count.
- Patricia lookup, insert, and remove: O(W), with `W` fixed at 32 or 64 and usually far fewer hops
  because unary paths are compressed.
- Patricia structural algebra: O(v), where `v` is the prefix structure visited after shared-root and
  disjoint-prefix pruning; O(n + m) in the worst case. Result cardinality is read from cached subtree
  metadata rather than recomputed by traversal.

Update allocation is O(b * depth + c) array storage and O(depth + c) allocated node objects for the
changed path and any touched collision bucket. Published nodes are immutable apart from reference counts.

## Concurrency

Reference counts are non-atomic. Concurrent read-only access to already-retained map/set/bag
handles is safe when the configured hash/equality callbacks and pointed-to payloads are themselves
safe to read concurrently. A handle must remain alive for the full read; do not copy or destroy
that handle concurrently.

Copying, updating, clearing, set algebra, and destroying versions retain or release nodes, including untouched
nodes shared with sibling snapshots. Serialize those operations across every structurally shared lineage:
derive versions single-threaded (or under one external lock), publish already-retained snapshots to readers,
then join/quiesce those readers before releasing their handles. Independent collections proven not to share
nodes may be updated concurrently.

Transient sessions are unsynchronized and have one logical owner. Explicitly cloned transient
handles are aliases for lifecycle transfer, not permission for concurrent access. Serialize every
read, edit, clone, publication, and destroy involving one shared session state, and apply the same
lineage rule to its retained source/published roots. Already-retained persistent snapshots remain
eligible for the read-only publication pattern above.
