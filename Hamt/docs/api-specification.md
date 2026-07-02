# HAMT API Specification

- Status: Implemented
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers and reviewers of `Tools.DataStructures.Hamt`
- Scope: Public API, persistence semantics, and complexity guarantees

## Overview

`PersistentHashMap<TKey, TValue>` is an immutable unordered dictionary backed by a hash-array mapped
trie. Each update returns a new map version and preserves the old version unchanged. Untouched
subtrees are shared by reference, so retaining many versions costs only the changed search paths plus
new or replaced leaves.

`PersistentHashSet<T>` is a value set built on the same HAMT core. It preserves the same comparer and
structural-sharing semantics as the map.

## Hash Trie Shape

The implementation uses 32-way logical branching with 5 hash bits consumed per level. Sparse branch
nodes are represented by a 32-bit bitmap and a compact child array; the array index is the population
count below the selected bit. Unequal keys with identical full hash codes are stored in immutable
collision buckets and compared linearly with the configured equality comparer.

Enumeration order is an implementation detail of the trie shape and comparer hash codes. It is stable
for an unchanged version, but callers must not treat it as insertion order or sorted order.

## Map Contract

`PersistentHashMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`.

- `Empty` returns the shared empty map using `EqualityComparer<TKey>.Default`.
- `Create(comparer)` returns an empty map using a supplied comparer.
- `CreateRange(items, comparer)` adds entries in enumeration order with last-wins semantics.
- `SetItem(key, value)` adds or replaces a key.
- `Add(key, value)` adds a key and throws `ArgumentException` when the key already exists.
- `TryAdd(key, value, out result)` reports duplicate keys without throwing.
- `Remove(key)` removes a key if present and returns the original map when absent.
- `TryRemove(key, out result, out value)` reports whether a key existed and returns the removed value.
- `Clear()` returns an empty map preserving the current comparer.

The configured comparer defines hash and equality semantics, including any behavior for null keys.
The comparer must honor the usual hash contract: equivalent keys must produce equal hash codes.
Changing comparer behavior after a map has been built has the same undefined practical effect as
mutating a comparer used by `Dictionary<TKey, TValue>`.

## Set Contract

`PersistentHashSet<T>` implements `IReadOnlyCollection<T>`.

- `Empty`, `Create`, and `CreateRange` mirror the map factories.
- `Add`, `TryAdd`, `Remove`, `TryRemove`, `Contains`, and `Clear` mirror map behavior.
- `Union`, `Intersect`, `Except`, and `SymmetricExcept` return new persistent sets.
- `IsSubsetOf`, `IsSupersetOf`, `Overlaps`, and `SetEquals` interpret equality through the set's
  comparer.

## Complexity

Let `w` be the hash width (32 bits), `b` be the branch factor (32), and `c` be the length of an
equal-hash collision bucket.

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes.
- Enumeration: O(n) time and O(branch-factor * depth) auxiliary stack space, bounded by the 32-way
  trie shape for normal 32-bit hashes.
- Map `CreateRange` / set `CreateRange`: O(n * update-cost) with structural sharing during the build.
- Set algebra implemented from public operations: O((n + m) * update-cost) unless the operation only
  probes membership.

Update allocation is O(depth + c) for the changed path and any touched collision bucket. Unchanged
subtrees remain shared and are safe for concurrent readers because all node arrays are privately
created before publication and never mutated afterward.
