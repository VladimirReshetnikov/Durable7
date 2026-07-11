# HAMT C API Specification

- Status: Current API specification
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C `tds_hamt` API
- Scope: Public C API, ownership semantics, persistence behavior, and complexity guarantees

For practical policy setup and lifetime examples, start with the [usage guide](usage.md).

## Overview

`tds_hamt_map` is an immutable unordered map backed by a hash-array mapped trie. Updates return a new
map value through an out parameter and leave the source map unchanged. Untouched nodes are shared by
intrusive reference count; update operations clone only the search path and any touched equal-hash
collision bucket.

`tds_hamt_set` is a value-set wrapper over the same map core. It stores set items as map keys and
uses a unit value.

The C API is type-erased. Keys, values, and set items are `void *` payloads interpreted by policy
callbacks. A policy may simply store borrowed pointers, or it may retain/release payloads to give
collections ownership of copied or reference-counted objects.

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
