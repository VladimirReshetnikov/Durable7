# HAMT C API Specification

- Status: Initial port
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers and reviewers of the pure C `tds_hamt` API
- Scope: Public C API, ownership semantics, persistence behavior, and complexity guarantees

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

`try_remove` reports the removed value pointer as stored in the source map. That pointer remains
valid according to the source map's lifetime and policy; the call does not transfer ownership.

## Hash Trie Shape

The trie uses 32-way logical branching and consumes five hash bits per level. Branch nodes store a
32-bit bitmap and a compact child array; the child slot is the population count below the selected
bit. Unequal keys with identical full 32-bit hashes are stored in immutable collision buckets and
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
- `tds_hamt_map_try_remove` returns removed flag and removed value pointer.
- `tds_hamt_map_try_get` returns the stored value pointer, if present.
- `tds_hamt_map_try_get_key` returns the stored equivalent key pointer, or echoes the query pointer
  on miss.
- `tds_hamt_map_clear` returns an empty map preserving the current policy.

When replacing an existing key, the originally stored key pointer is retained. When the existing
value compares equal under the value equality callback, the root is reused and the stored value
pointer is retained.

## Set Contract

- `tds_hamt_set_create` and `tds_hamt_set_create_range` mirror the map factories.
- `add`, `try_add`, `remove`, `try_remove`, `contains`, `try_get_value`, and `clear` mirror map
  behavior.
- `union_many`, `intersect_many`, `except_many`, and `symmetric_except_many` return new persistent
  sets.
- `is_subset_of_many`, `is_proper_subset_of_many`, `is_superset_of_many`,
  `is_proper_superset_of_many`, `overlaps_many`, and `equals_many` interpret equality through the
  set's policy callbacks.

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
changed path and any touched collision bucket. Published nodes are immutable apart from root
reference counts; concurrent reads of already-retained versions are safe under ordinary C object
lifetime rules, but the reference count itself is not atomic.
