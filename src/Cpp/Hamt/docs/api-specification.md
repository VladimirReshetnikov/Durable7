# HAMT C++ API Specification

- Status: Current API specification
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers and reviewers of `tools::data_structures::hamt`
- Scope: Public C++ API, persistence semantics, and complexity guarantees

For practical include, value-semantics, and set-algebra examples, start with the
[usage guide](usage.md).

## Overview

`persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>` is an immutable unordered map backed by a
hash-array mapped trie. Every update returns a new map value and leaves the source value unchanged.
Untouched trie nodes are shared by `std::shared_ptr<const node>`; update operations clone only the
search path and any touched equal-hash collision bucket.

`persistent_hash_set<T, Hash, KeyEqual>` is a value-set wrapper over
`persistent_hash_map<T, unit, Hash, KeyEqual>`. It preserves the same hash/equality policy and
structural-sharing behavior as the map.

The port intentionally follows C++ value semantics rather than C# reference identity. No-op updates
return a value that shares the same root node as the source; `shares_root_with` and the debug root
inspection helpers expose that property for tests.

## Hash Trie Shape

The trie uses 32-way logical branching and consumes five hash bits per level. Branch nodes store a
32-bit bitmap and a compact child vector; the child slot is the population count below the selected
bit. Unequal keys with identical 32-bit truncated hash codes are stored in immutable collision
buckets and compared linearly with `KeyEqual`.

`Hash` may return any integral-like hash value accepted by `static_cast<std::uint32_t>`; the port
uses the low 32 bits to match the C# implementation's hash width. Equivalent keys must produce equal
32-bit truncated hash values.

Enumeration order follows trie bitmap order and collision-bucket order. It is stable for an
unchanged version but is not insertion order or sorted order. Iterators keep at most seven inline
branch frames and do not allocate heap storage while traversing; copied iterators advance
independently.

## Map Contract

- `empty()` returns an empty default-policy map.
- `create(hash, equal, values_equal)` returns an empty map using supplied policy objects.
- `create_range(items, hash, equal, values_equal)` adds entries in enumeration order with
  last-wins values.
- `set_item(key, value)` adds or replaces a key.
- `set_items(items)` adds or replaces entries in enumeration order.
- `add(key, value)` adds a key and throws `std::invalid_argument` when an equivalent key already
  exists.
- `try_add(key, value)` returns `{map, added}` and rejects duplicate keys without throwing.
- `remove(key)` removes a key if present and returns a root-sharing value when absent.
- `try_remove(key)` returns `{map, removed, value}`.
- `try_get(key)` returns a pointer to the stored value, or `nullptr` when absent.
- `try_get_key(equal_key)` returns a pointer to the stored equivalent key, or `nullptr` when absent.
- `at(key)` returns the stored value reference or throws `std::out_of_range`.
- `clear()` returns an empty map preserving the current policy objects.

When replacing an existing key, the originally stored key object inside the trie is retained. When
the existing value compares equal under `ValueEqual`, the root is reused and the stored value object
is retained.

## Set Contract

- `empty()`, `create`, and `create_range` mirror the map factories.
- `add`, `try_add`, `remove`, `try_remove`, `contains`, `try_get_value`, and `clear` mirror map
  behavior.
- `union_with`, `intersect_with`, `except_with`, and `symmetric_except_with` return new persistent
  sets.
- `is_subset_of`, `is_proper_subset_of`, `is_superset_of`, `is_proper_superset_of`, `overlaps`, and
  `set_equals` interpret equality through the set's `Hash` and `KeyEqual` policy objects.

`intersect_with`, `symmetric_except_with`, `is_subset_of`, `is_proper_subset_of`,
`is_proper_superset_of`, and `set_equals` materialize their argument into `std::unordered_set` using
the set's policy objects. `is_superset_of` and `overlaps` stream their argument and exit early.

## Complexity

Let `w` be the hash width used by the port (32 bits), `b` be the branch factor (32), and `c` be the
length of an equal-hash collision bucket.

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes.
- Enumeration: O(n) time with at most seven inline branch frames.
- Map `create_range` / set `create_range`: O(n * update-cost), with structural sharing during the
  build.
- Set algebra implemented from public operations: O((n + m) * update-cost) unless the operation
  only probes membership.

Update allocation is O(b * depth + c) vector storage and O(depth + c) allocated node objects for the
changed path and any touched collision bucket. Published nodes are immutable and safe for concurrent
readers as long as ordinary C++ object lifetime rules are respected.
