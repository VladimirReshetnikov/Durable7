# HAMT API Specification

- Status: Implemented
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers and reviewers of `Tools.DataStructures.Hamt`
- Scope: Public API, persistence semantics, and complexity guarantees

For practical namespace, comparer, persistent update, and set-algebra examples, start with the
[usage guide](usage.md).

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
for an unchanged version, but callers must not treat it as insertion order or sorted order. Both
collections expose public struct enumerators that keep their entire traversal state inline: obtaining
and draining an enumerator allocates nothing, and a copied enumerator advances independently of the
original.

## Map Contract

`PersistentHashMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`.

- `Empty` returns the shared empty map using `EqualityComparer<TKey>.Default`.
- `Create(comparer)` returns an empty map using a supplied comparer.
- `CreateRange(items, comparer)` adds entries in enumeration order with last-wins semantics.
- `SetItem(key, value)` adds or replaces a key.
- `SetItems(items)` adds or replaces entries in enumeration order with last-wins semantics and
  throws `ArgumentNullException` for a null sequence.
- `Add(key, value)` adds a key and throws `ArgumentException` when the key already exists. This is
  stricter than `ImmutableDictionary<TKey, TValue>.Add`, which tolerates re-adding an equal value;
  here any existing equivalent key throws.
- `TryAdd(key, value, out result)` reports duplicate keys without throwing.
- `Remove(key)` removes a key if present and returns the original map when absent.
- `TryRemove(key, out result, out value)` reports whether a key existed and returns the removed value.
- `TryGetKey(equalKey, out actualKey)` recovers the originally stored key object for an equivalent
  key, or echoes `equalKey` back when absent.
- `Clear()` returns an empty map preserving the current comparer, and returns the current instance
  when the map is already empty.

`Add` and `TryAdd` hash the key once and walk the trie once; a rejected duplicate allocates nothing.
The try-pattern `out` values (`TryGetValue`, `TryRemove`) carry `[MaybeNullWhen(false)]`, matching
the `IReadOnlyDictionary<TKey, TValue>` annotation.

No-op updates preserve instance identity throughout: replacing a value that compares equal under
`EqualityComparer<TValue>.Default`, removing an absent key, and clearing an empty map all return the
current instance. Consequently, "last-wins" value semantics hold up to default value equality: when
an incoming value compares equal to the stored value, the stored value object is retained. When a
present key is replaced or re-set, the originally stored key object is likewise retained; use
`TryGetKey` to observe it.

The configured comparer defines hash and equality semantics, including any behavior for null keys.
The comparer must honor the usual hash contract: equivalent keys must produce equal hash codes.
Changing comparer behavior after a map has been built has the same undefined practical effect as
mutating a comparer used by `Dictionary<TKey, TValue>`.

## Set Contract

`PersistentHashSet<T>` implements `IReadOnlySet<T>` (and therefore `IReadOnlyCollection<T>`).

- `Empty`, `Create`, and `CreateRange` mirror the map factories.
- `Add`, `TryAdd`, `Remove`, `TryRemove`, `Contains`, and `Clear` mirror map behavior, including
  no-op instance identity.
- `TryGetValue(equalValue, out actualValue)` recovers the originally stored item object for an
  equivalent item, or echoes `equalValue` back when absent.
- `Union`, `Intersect`, `Except`, and `SymmetricExcept` return new persistent sets.
- `IsSubsetOf`, `IsProperSubsetOf`, `IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, and
  `SetEquals` interpret equality through the set's comparer.

`SetItems` on the map and `Union`/`Except` on the set are the sanctioned bulk updates; there are no
separate `AddRange`/`RemoveRange` members.

`Intersect`, `SymmetricExcept`, `IsSubsetOf`, `IsProperSubsetOf`, `IsProperSupersetOf`, and
`SetEquals` materialize their argument into a probe `HashSet<T>` using the set's comparer — an O(m)
time and space cost documented on each member. `IsSupersetOf` and `Overlaps` stream their argument
and exit early.

## Complexity

Let `w` be the hash width (32 bits), `b` be the branch factor (32), and `c` be the length of an
equal-hash collision bucket.

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes. Lookups allocate nothing.
- Enumeration: O(n) time. The enumerator holds at most seven inline frames (one per trie level) and
  performs no heap allocation.
- Map `CreateRange` / set `CreateRange`: O(n (w + c)) through a mutable unpublished trie followed by
  one freeze; unlike repeated persistent updates, the build does not clone every traversed path.
- Set algebra implemented from public operations: O((n + m) * update-cost) unless the operation only
  probes membership.

Update allocation is O(b * depth + c) array storage — O(depth + c) allocated node objects — for the
changed path and any touched collision bucket. Unchanged subtrees remain shared and are safe for
concurrent readers because all node arrays are privately created before publication and never
mutated afterward.
