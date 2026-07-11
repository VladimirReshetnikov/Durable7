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

`ConcurrentHashTrie<TKey, TValue>` is a lock-free mutable map built from bitmap C-nodes, singleton
leaves, collision nodes, and generation-stamped indirection nodes. Its public mutation surface is
linearizable, and `Snapshot()` captures the current Ctrie generation in O(1).

## Hash Trie Shape

The implementation uses CHAMP's 32-way logical branching with 5 hash bits consumed per level. Sparse
branch nodes carry separate 32-bit data and node bitmaps, an inline key/value payload array, and a
compact child-node array; each array index is the population count below the selected bit. Canonical
deletion pulls singleton leaf children into their parent payload run. Unequal keys with identical full hash codes are stored in immutable
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
- `MapEquals(other, valueComparer)` uses canonical trie topology for lockstep equality with
  reference-equal subtree pruning. The two maps must retain the same comparer object;
  collision-bucket order is ignored.
- `Diff(other, valueComparer)` structurally aligns logical bitmap slots, skips reference-equal
  subtries, and returns materialized `MapDifference<TKey, TValue>` values classified as `Added`,
  `Removed`, or `Changed`. Null and comparer-mismatch failures occur eagerly. Removed/changed results
  expose the source's stored key representative; added results expose the target's. Result order is
  deterministic for unchanged operands but remains an implementation detail.

For maps derived from a shared version, equality and diff visit only non-shared trie regions (plus
reported output). Independently built equal maps still require O(n) comparison because canonical
topology does not make separately allocated nodes reference-equal. Equal-hash collision runs are
matched without regard to order and can require O(c²) key comparisons for a bucket of size c.

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
- Map `CreateRange` / set `CreateRange`: O(n (w + c)) through hash-bucket staging followed by one
  canonical freeze; unlike repeated persistent updates, the build does not clone every traversed path.
- `MapEquals`: O(divergent canonical nodes + collision comparisons), with reference-equal subtrees
  skipped in O(1).
- `Diff`: O(n + m) in the current public implementation, with an O(1) shared-root fast path.
- Set algebra implemented from public operations: O((n + m) * update-cost) unless the operation only
  probes membership.

Update allocation is O(b * depth + c) array storage — O(depth + c) allocated node objects — for the
changed path and any touched collision bucket. Unchanged subtrees remain shared and are safe for
concurrent readers because all node arrays are privately created before publication and never
mutated afterward.

## Concurrent Hash Trie Contract

`ConcurrentHashTrie<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>` while exposing a
mutable, thread-safe update surface:

- `SetItem` and the indexer setter atomically add or replace; equal-value no-ops publish no generation.
- `TryAdd`, `TryUpdate`, and `TryRemove` are single-key conditional atomic operations.
- `GetOrAdd` and `AddOrUpdate` use retryable factories. A factory may run more than once when a CAS
  loses contention, matching the repeatability requirement of `ConcurrentDictionary` factories.
- `Clear` atomically publishes an empty root.
- `Snapshot` and `GetEnumerator` capture one immutable generation. Later writes cannot change their
  contents, and snapshots retain the trie's comparer.
- `SnapshotView.ToPersistentHashMap` copies a captured generation into canonical CHAMP form in O(n).

The managed implementation installs GCAS descriptors on the indirection node owning a change.
Readers help complete encountered descriptors. Root-generation identity decides whether a descriptor
commits, preventing a write that raced with `Snapshot` from entering the frozen generation. The new
root initially shares the old C-node graph; a later writer renews an old-generation child indirection
node only when its path reaches that child. Reads and snapshots take no locks. A successful write
copies one compact C-node array per changed level and performs node-local GCAS; collision-node work is
linear in the equal-hash bucket. Contended operations can retry, so progress is lock-free, not wait-free.

## Integer Patricia Map And Set Contract

The integer-specialized family consists of:

- `PersistentIntMap<TValue>` and `PersistentIntSet` for `int` keys/items;
- `PersistentLongMap<TValue>` and `PersistentLongSet` for `long` keys/items.

The concrete facades share a static-policy big-endian Patricia engine; no generic-math or virtual
key conversion occurs in the hot path. A sign-bit transform maps signed order to unsigned trie
order, so enumeration is ascending signed order including the minimum/maximum boundary. Keys are
their own identities and hashes, so collision buckets and comparer policies do not exist.

Map `SetItem`, `Add`/`TryAdd`, `Remove`/`TryRemove`, `Clear`, and no-op instance identity mirror the
persistent CHAMP vocabulary. `Union` is right-biased by default; `Union` and `Intersect` overloads
accept `(key, left, right)` combining functions. Default `Union`, `Intersect`, and `Except` recurse
over prefixes, prune reference-equal subtrees, and reuse untouched nodes. Set facades expose the
same three structural operations and implement `IReadOnlySet<T>`.

Lookup and update visit at most W path-compressed branches, where W is 32 or 64. Structural algebra
is proportional to the prefixes where the inputs overlap or diverge and returns immediately for
reference-equal roots. Combining overloads currently enumerate one side and therefore cost
O(m * W). Enumeration is O(n) in ascending order and currently allocates an iterator plus a traversal
stack; unlike the CHAMP enumerator, it is not an allocation-free struct enumerator.

## Merkle Search Tree Contract

`MerkleSearchTree<TKey, TValue>` is an immutable ordered content-addressed map. Shape priority is a
geometric layer derived from `SHA-256(domain || canonical-key)` plus the full digest and key order as
tie-breaks. Consequently, the same policy and logical entries produce the same Cartesian/Merkle
search-tree shape regardless of insertion/deletion history.

There is deliberately no unsafe default policy. `MerkleSearchTreePolicy<TKey, TValue>` requires:

- a stable application `PolicyId` naming comparer semantics and their version;
- an `IComparer<TKey>` defining key equivalence/order;
- an injective `IMerkleCodec<TKey>` encoding comparer-equivalence classes;
- a canonical `IMerkleCodec<TValue>` value encoding.

The domain digest covers `mst-sha256-v1`, the application policy id, and both codec ids. Built-in
codecs provide big-endian `int`/`long`, tagged nullable UTF-8 strings and byte arrays, and RFC-4122
GUID bytes. Every node hash uses domain-separated, 32-bit-big-endian length-framed fields plus child
digests. Default randomized .NET string hashing is never used. A caller whose comparer equates
multiple representations must encode that equivalence class identically; distinct classes must not
share an encoding.

Lookup and ordered ranges are expected O(log n + k). `SetItem`/`Remove` path-copy expected O(log n)
nodes and eagerly recompute their hashes; equal values and absent removals preserve identity.
`RootHash` and `ContentEquals` are O(1); the latter adopts the ordinary SHA-256 collision-resistance
assumption of content-addressed systems. `MapEquals` follows the digest fast path with semantic
lockstep verification. `Diff` prunes equal-digest subtrees and returns typed added/removed/changed
records; when one edit changes a canonical ancestor, it merge-scans that divergent region.

Keys and values should be semantically immutable after insertion, just as dictionary keys must not
change comparer behavior. The tree owns encoded byte arrays, so later mutation of caller-provided
codec buffers cannot alter a published root address.
