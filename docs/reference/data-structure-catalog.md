# Data Structure Catalog

- Created (UTC): 2026-07-02T19:53:11Z
- Repository HEAD: 1d90612aed11f273521046015c9d63bb7c993bba
- Audience: Maintainers and AI agents comparing data-structure surfaces across languages
- Scope: Repository-owned data-structure families, public entry points, and primary reference links

This catalog is the cross-workspace orientation layer. It answers "which public library surfaces exist in
which language, and where do I start?" The workspace API specifications and headers remain the
authoritative source for contracts, complexity, allocation behavior, and validation details.

Use this together with the [workspace map](workspace-map.md): the map explains the language-first
layout, while this catalog maps each public library family across that layout. Use the
[semantic contracts reference](semantic-contracts.md) when reviewing behavior that should remain
recognizable across language ports.

## HAMT Map And Set

The HAMT workspaces implement persistent hash-array mapped trie maps and sets with 32-way
bitmap-indexed branching, immutable equal-hash collision buckets, structural sharing between
versions, and comparer/hash-policy preservation. All nine languages expose one-way CHAMP map/set
editing sessions with O(1)-in-trie adoption and publication. C# implements the optimized owner-token
kernel, including in-place edits of token-owned nodes. C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and
Python preserve the same observable lifecycle through semantic facades whose changed point edits
remain ordinary persistent path copies; those sibling sessions make no edit-performance claim. The
optimized ports align same-policy equality/diff through canonical logical slots and prune shared
descendants; Python and OCaml expose the same results through their documented checkpoint traversal
without claiming that structural optimization. Independently created
hash policies retain semantic fallback or explicit rejection according to the local contract. All
nine languages expose persistent map `GetOrAdd`/`AddOrUpdate` counterparts that hash once, descend
once, and invoke exactly one selected factory without a retry loop. All nine also ship persistent
hash bags with positive 32-bit per-class multiplicities, widened expanded counts, receiver-policy
multiset algebra, and stored-representative recovery, plus strict persistent bidirectional maps over
independent key/value policies. C# uses a checked `long` bag total, TypeScript uses `bigint`, Python
uses an unbounded `int`, and OCaml uses a checked native `int`. C++ and Rust expose construction-only
bulk builders over unpublished mutable CHAMP nodes; TypeScript and Python port that reusable,
detached-freeze surface as well. OCaml's reusable staging builder preserves detached freezes but
uses persistent path copies. All nine languages expose explicit-width Patricia
maps/sets; C# and Kotlin/JVM intentionally own the managed-only
Ctrie, whose snapshots enumerate in canonical CHAMP order for exact sequence-preserving conversion;
TypeScript supplies the synchronous isolate-local snapshot facade without a cross-worker progress
claim, while OCaml and Python supply thread-safe, lock-coordinated facades over persistent roots. All nine
languages own complete wire-compatible policy-bound Merkle search trees.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentHashMap<TKey, TValue>` with persistent `GetOrAdd`/`AddOrUpdate`, `PersistentHashMap<TKey, TValue>.Transient`, `PersistentHashSet<T>`, `PersistentHashSet<T>.Transient`, `PersistentHashBag<T>`, `PersistentBiMap<TKey, TValue>`, `ConcurrentHashTrie<TKey, TValue>`, `PersistentIntMap<TValue>`, `PersistentIntSet`, `PersistentLongMap<TValue>`, `PersistentLongSet`, `MerkleSearchTree<TKey, TValue>` | [Workspace](../../src/CSharp/docs/Hamt/overview.md), [usage guide](../../src/CSharp/docs/Hamt/usage.md), [API spec](../../src/CSharp/docs/Hamt/api-specification.md), [T2 shipment decision](../../src/CSharp/docs/Hamt/transient-t2-decision.md), [CHAMP map](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs), [single-pass persistent updates](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.SinglePassUpdates.cs), [hash bag](../../src/CSharp/src/Durable7.Hamt/PersistentHashBag.cs), [bimap](../../src/CSharp/src/Durable7.Hamt/PersistentBiMap.cs), [map transient](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.Transient.cs), [set transient](../../src/CSharp/src/Durable7.Hamt/PersistentHashSet.Transient.cs), [concurrent trie](../../src/CSharp/src/Durable7.Hamt/ConcurrentHashTrie.cs), [int map](../../src/CSharp/src/Durable7.Hamt/PersistentIntMap.cs), [Merkle search tree](../../src/CSharp/src/Durable7.Hamt/MerkleSearchTree.cs), [canonical codecs](../../src/CSharp/src/Durable7.Hamt/MerkleEncoding.cs), [persistence vocabulary](../../src/CSharp/src/Durable7.Hamt/MerklePersistence.cs), [verification, proofs, sync, and merge](../../src/CSharp/src/Durable7.Hamt/MerkleSearchTree.PersistenceAlgorithms.cs) |
| C | `d7_hamt_map`, `d7_hamt_map_transient`, `d7_hamt_set`, `d7_hamt_set_transient`, `d7_hamt_bag`, `d7_hamt_bi_map`, hash/set policies, `d7_int_map`, `d7_int_set`, `d7_long_map`, `d7_long_set`, `d7_merkle_search_tree`, `d7_merkle_block_store`, `d7_merkle_verification_budget`, `d7_merkle_proof`, and sync/merge handles | [Workspace](../../src/C/Hamt/README.md), [usage guide](../../src/C/Hamt/docs/usage.md), [API spec](../../src/C/Hamt/docs/api-specification.md), [bimap header](../../src/C/Hamt/include/durable7/hamt/persistent_bi_map.h), [Merkle specification](../../src/C/Hamt/docs/merkle-search-tree.md), [CHAMP and transient header](../../src/C/Hamt/include/durable7/hamt/hamt.h), [Patricia header](../../src/C/Hamt/include/durable7/hamt/patricia.h), [Merkle header](../../src/C/Hamt/include/durable7/hamt/merkle_search_tree.h) |
| C++ | `persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>`, its construction-only `bulk_builder`, and nested `transient`; `persistent_hash_set<T, Hash, KeyEqual>` and nested `transient`; `persistent_hash_bag`; `persistent_bi_map`; Patricia and Merkle families | [Workspace](../../src/Cpp/Hamt/README.md), [usage guide](../../src/Cpp/Hamt/docs/usage.md), [API spec](../../src/Cpp/Hamt/docs/api-specification.md), [bimap header](../../src/Cpp/Hamt/include/durable7/hamt/persistent_bi_map.hpp), [Merkle core](../../src/Cpp/Hamt/docs/merkle-search-tree.md), [persistence specification](../../src/Cpp/Hamt/docs/merkle-persistence.md), [aggregate header](../../src/Cpp/Hamt/include/durable7/hamt/hamt.hpp), [persistence](../../src/Cpp/Hamt/include/durable7/hamt/merkle_persistence.hpp), [proofs and merge](../../src/Cpp/Hamt/include/durable7/hamt/merkle_proofs.hpp) |
| Haskell | `HashMap k v`, `MapTransient k v`, `HashSet a`, `SetTransient a`, `HashBag a`, `BiMap k v`, `HashPolicy k`, `Hashable`, `IntMap32 v`, `IntMap64 v`, `IntSet32`, `IntSet64`, `MerkleSearchTree k v`, `MerkleBlockStore`, `MerkleProof`, `MerkleVerificationBudget` | [Workspace](../../src/Haskell/Hamt/README.md), [map source](../../src/Haskell/Hamt/src/Durable7/Hamt/HashMap.hs), [bimap source](../../src/Haskell/Hamt/src/Durable7/Hamt/BiMap.hs), [transient source](../../src/Haskell/Hamt/src/Durable7/Hamt/Transient.hs), [Patricia source](../../src/Haskell/Hamt/src/Durable7/Hamt/Patricia.hs), [Merkle guide](../../src/Haskell/Hamt/docs/merkle-search-tree.md), [encoding](../../src/Haskell/Hamt/src/Durable7/Hamt/MerkleEncoding.hs), [Merkle core](../../src/Haskell/Hamt/src/Durable7/Hamt/MerkleSearchTree.hs), [persistence, proofs, sync, and merge](../../src/Haskell/Hamt/src/Durable7/Hamt/MerklePersistence.hs), [tests](../../src/Haskell/Hamt/test/README.md) |
| Kotlin | `PersistentHashMap<K, V>` and nested `Transient<K, V>`, `PersistentHashSet<T>` and nested `Transient<T>`, `PersistentHashBag<T>`, `PersistentBiMap<K, V>`, `HashPolicy<K>`, `ConcurrentHashTrie<K, V>`, `PersistentIntMap<V>`, `PersistentIntSet`, `PersistentLongMap<V>`, `PersistentLongSet`, `MerkleSearchTree<K, V>`, `MerkleBlockStore`, `MerkleProof`, `MerkleVerificationBudget` | [Workspace](../../src/Kotlin/Hamt/README.md), [API notes](../../src/Kotlin/Hamt/docs/api-notes.md), [bimap source](../../src/Kotlin/Hamt/src/durable7/hamt/PersistentBiMap.kt), [Merkle guide](../../src/Kotlin/Hamt/docs/merkle-search-tree.md), [validation](../../src/Kotlin/Hamt/docs/validation.md), [CHAMP and transient source](../../src/Kotlin/Hamt/src/durable7/hamt/PersistentHamt.kt), [Ctrie source](../../src/Kotlin/Hamt/src/durable7/hamt/ConcurrentHashTrie.kt), [Patricia source](../../src/Kotlin/Hamt/src/durable7/hamt/PersistentPatricia.kt), [Merkle encoding](../../src/Kotlin/Hamt/src/durable7/hamt/MerkleEncoding.kt), [Merkle core](../../src/Kotlin/Hamt/src/durable7/hamt/MerkleSearchTree.kt), [persistence vocabulary](../../src/Kotlin/Hamt/src/durable7/hamt/MerklePersistence.kt), [verification, proofs, sync, and merge](../../src/Kotlin/Hamt/src/durable7/hamt/MerkleSearchTreePersistence.kt), [tests](../../src/Kotlin/Hamt/tests/README.md) |
| Rust | `PersistentHashMap<K, V, S>`, `BulkBuilder<K, V, S>`, `TransientHashMap<K, V, S>`, `PersistentHashSet<T, S>`, `TransientHashSet<T, S>`, `PersistentHashBag<T, S>`, `PersistentBiMap<K, V, SK, SV>`, Patricia and Merkle families | [Workspace](../../src/Rust/Hamt/README.md), [API notes](../../src/Rust/Hamt/docs/api-notes.md), [CHAMP, builder, and transient source](../../src/Rust/Hamt/src/lib.rs), [bimap source](../../src/Rust/Hamt/src/bi_map.rs), [Merkle search tree](../../src/Rust/Hamt/docs/merkle-search-tree.md), [validation](../../src/Rust/Hamt/docs/validation.md), [Merkle core](../../src/Rust/Hamt/src/merkle_search_tree.rs), [encoding](../../src/Rust/Hamt/src/merkle_encoding.rs), [persistence, proofs, sync, and merge](../../src/Rust/Hamt/src/merkle_persistence.rs) |
| OCaml | `Persistent_hamt` with builders/transients/factories, `Persistent_hash_set`, `Persistent_hash_bag`, `Persistent_bi_map`, `Persistent_patricia`, `Concurrent_hash_trie`, and complete `Merkle_*` modules | [Workspace](../../src/OCaml/README.md), [API notes](../../src/OCaml/docs/api-notes.md), [HAMT source](../../src/OCaml/lib/hamt), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `PersistentHashMap<K, V>` with `getOrAdd`/`addOrUpdate`, `HashMapBulkBuilder<K, V>`, `TransientHashMap<K, V>`, `PersistentHashSet<T>`, `TransientHashSet<T>`, `PersistentHashBag<T>`, `PersistentBiMap<K, V>`, `ConcurrentHashTrie<K, V>`, Patricia and complete Merkle families | [Workspace](../../src/TypeScript/README.md), [API notes](../../src/TypeScript/docs/api-notes.md), [HAMT source](../../src/TypeScript/src/hamt), [tests](../../src/TypeScript/test/README.md) |
| Python | `PersistentHashMap` with `get_or_add`/`add_or_update`, `HashMapBulkBuilder`, `TransientHashMap`, `PersistentHashSet`, `TransientHashSet`, `PersistentHashBag`, `PersistentBiMap`, `HashPolicy`, `ConcurrentHashTrie`, Patricia and complete Merkle families | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [HAMT source](../../src/Python/src/durable7/hamt), [tests](../../src/Python/tests/README.md) |

The lifecycle shape is idiomatic rather than textually identical. C explicit clones alias one
ref-counted session and observe shared consumed/modified status; C++ sessions are move-only and
publish through an rvalue, with deterministic terminal invalidation during a throwing session move
and the documented no-retry caveat during throwing publication;
Haskell sessions live in `IO`; Kotlin checks consumption at runtime and binds views to a session
version; Rust consumes the session in the type system when publishing. These differences preserve
the common one-way lifecycle without pretending that the sibling facades implement C#'s owner-token
optimization. TypeScript follows the same path-copy session model and scopes its concurrent facade
to one JavaScript isolate. Python also uses version-bound path-copy sessions, but coordinates its
thread-safe live facade with a lock and captures immutable persistent roots in O(1).

## Persistent Bidirectional Map

Every language ships a strict immutable bimap by composing a forward CHAMP `K -> V` with an inverse
CHAMP `V -> K`. Key and value hash/equality policies are retained independently. An add rejects an
already represented class on either side, with the key domain checked first. Result-rich ports
report key conflict when both domains conflict; C# retains a boolean `TryAdd` result.
Replacement may change one key's value only when the new value class is free; a configured-policy-
equivalent value is a no-op retaining both representatives, and another key is never displaced.
Removal is symmetric and presence-safe for nullable values. Forward iteration uses the forward
CHAMP's stable-for-one-version, otherwise unspecified order.

Inverse construction enumerates no entries and is O(1) in pair count. C#, Kotlin, TypeScript, and
Python cache reciprocal facade objects so `map.Inverse.Inverse` or its local equivalent is the exact
original object. C, C++, Haskell, and Rust expose the value-semantic analogue: double inversion
shares the original two immutable roots. All operations publish only after both successor maps are
complete, and the family intentionally exposes no algebra, builder, transient, or displacing
force-put mode. The honest storage cost is approximately two map entries per logical pair.

| Language | Public type | Contract and validation |
| --- | --- | --- |
| C# | `PersistentBiMap<TKey, TValue>` | [API specification](../../src/CSharp/docs/Hamt/api-specification.md#persistent-bimap-contract), [usage](../../src/CSharp/docs/Hamt/usage.md#persistent-bidirectional-map), [validation](../../src/CSharp/docs/Hamt/validation.md#test-coverage) |
| C | `d7_hamt_bi_map` | [API specification](../../src/C/Hamt/docs/api-specification.md#persistent-bidirectional-map-contract), [usage](../../src/C/Hamt/docs/usage.md#persistent-bidirectional-map), [validation](../../src/C/Hamt/docs/validation.md#persistent-bidirectional-map-shipment-evidence) |
| C++ | `persistent_bi_map<Key, T, KeyHash, KeyEqual, ValueHash, ValueEqual>` | [API specification](../../src/Cpp/Hamt/docs/api-specification.md), [usage](../../src/Cpp/Hamt/docs/usage.md#persistent-bidirectional-map), [validation](../../src/Cpp/Hamt/docs/validation.md) |
| Haskell | `Durable7.Hamt.BiMap.BiMap k v` | [Workspace contract](../../src/Haskell/Hamt/README.md#persistent-bidirectional-map), [tests](../../src/Haskell/Hamt/test/README.md) |
| Kotlin | `PersistentBiMap<K, V>` | [API notes](../../src/Kotlin/Hamt/docs/api-notes.md#persistent-bidirectional-map), [validation](../../src/Kotlin/Hamt/docs/validation.md) |
| Rust | `PersistentBiMap<K, V, SK, SV>` | [API notes](../../src/Rust/Hamt/docs/api-notes.md#persistent-bidirectional-map), [validation](../../src/Rust/Hamt/docs/validation.md) |
| OCaml | `Persistent_bi_map` | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/hamt/persistent_bi_map.mli), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `PersistentBiMap<K, V>` | [API notes](../../src/TypeScript/docs/api-notes.md#hamt-maps-bags-multimaps-relations-bimaps-derived-facades-builders-and-sessions), [validation](../../src/TypeScript/docs/validation.md) |
| Python | `PersistentBiMap[K, V]` | [API notes](../../src/Python/docs/api-notes.md), [validation](../../src/Python/docs/validation.md) |

## Derived Persistent Maps, Relations, And Sparse Bit Sets

Nine composition-first families now ship across all nine language roots. They reuse the public
HAMT, ordered-sequence, measured-tree, and interval-search substrates instead of introducing
unnecessary new core trees. In addition to the established multimap, relation, ordered-map, and
interval-map families, the current tranche adds:

- an ordered multimap whose key groups and distinct values each retain first-insertion order;
- a strict presence-safe map patch with preflight application, inversion, and composition;
- an explicit-vertex directed graph with forward and reverse adjacency;
- a primary map with one automatically maintained nonunique secondary index; and
- a sparse chunked bit set with logarithmic point lookup, inclusive rank, and select over cached
  population measures.

The established composition-first families remain:

- set-valued hash multimaps store only nonempty value sets and expose distinct-key and pair
  cardinalities separately;
- relations maintain exact forward and reverse multimaps, so inverse traversal is persistent and
  does not scan the pair set;
- ordered maps separate equality-defined key identity from insertion and explicit-position order,
  retaining the first key representative while allowing payload replacement in place; and
- interval maps attach one payload to each complete `(low, high)` interval key and combine exact
  lookup with augmented stabbing/overlap navigation.

Every mutation publishes all indexes together or publishes nothing, preserves retained versions,
and returns the receiver for documented logical no-ops. Policy-bearing ports retain independent
key/value, left/right, or equality/ordering policies as appropriate. The neutral ordered map is
independently owned.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentHashMultimap`, `PersistentRelation`, `PersistentOrderedMap`, `PersistentIntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet` | [HAMT API](../../src/CSharp/docs/Hamt/api-specification.md), [derived HAMT contract](../../src/CSharp/docs/Hamt/derived-persistent-structures.md), [Ordered multimap](../../src/CSharp/docs/Ordered/persistent-ordered-multimap.md), [chunked bit set](../../src/CSharp/docs/FingerTree/persistent-chunked-bit-set.md) |
| C | `d7_hamt_multimap`, `d7_hamt_relation`, `d7_ordered_map`, `ft_persistent_interval_map`, `d7_ordered_multimap`, `d7_map_patch`, `d7_directed_graph`, `d7_indexed_map`, `ft_persistent_chunked_bit_set` | [HAMT workspace](../../src/C/Hamt/README.md), [Ordered workspace](../../src/C/Ordered/README.md), [FingerTree workspace](../../src/C/FingerTree/README.md) |
| C++ | `persistent_hash_multimap`, `persistent_relation`, `persistent_ordered_map`, `persistent_interval_map`, `persistent_ordered_multimap`, `persistent_map_patch`, `persistent_directed_graph`, `persistent_indexed_map`, `persistent_chunked_bit_set` | [HAMT workspace](../../src/Cpp/Hamt/README.md), [Ordered workspace](../../src/Cpp/Ordered/README.md), [FingerTree workspace](../../src/Cpp/FingerTree/README.md) |
| Haskell | `HashMultimap`, `Relation`, `PersistentOrderedMap`, `IntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet` | [HAMT workspace](../../src/Haskell/Hamt/README.md), [Ordered workspace](../../src/Haskell/Ordered/README.md), [FingerTree workspace](../../src/Haskell/FingerTree/README.md) |
| Kotlin | `PersistentHashMultimap`, `PersistentRelation`, `PersistentOrderedMap`, `PersistentIntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet` | [HAMT workspace](../../src/Kotlin/Hamt/README.md), [Ordered workspace](../../src/Kotlin/Ordered/README.md), [FingerTree workspace](../../src/Kotlin/FingerTree/README.md) |
| Rust | `PersistentHashMultimap`, `PersistentRelation`, `PersistentOrderedMap`, `PersistentIntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet` | [HAMT workspace](../../src/Rust/Hamt/README.md), [Ordered workspace](../../src/Rust/Ordered/README.md), [FingerTree workspace](../../src/Rust/FingerTree/README.md) |
| OCaml | `Persistent_hash_multimap`, `Persistent_relation`, `Persistent_ordered_map`, `Persistent_interval_map`, `Persistent_ordered_multimap`, `Persistent_map_patch`, `Persistent_directed_graph`, `Persistent_indexed_map`, `Persistent_chunked_bit_set` | [Workspace](../../src/OCaml/README.md), [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `PersistentHashMultimap`, `PersistentRelation`, `PersistentOrderedMap`, `PersistentIntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet` | [Workspace](../../src/TypeScript/README.md), [API notes](../../src/TypeScript/docs/api-notes.md), [sources](../../src/TypeScript/src) |
| Python | `PersistentHashMultimap`, `PersistentRelation`, `PersistentOrderedMap`, `PersistentIntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [sources](../../src/Python/src/durable7) |

## Finger-Tree Core And Deque

The finger-tree workspaces provide persistent sequence engines: a tuned catenable deque and a
general monoid-measured tree. They support endpoint operations, concatenation, splitting by
position or measure, indexed access where exposed, and immutable structural sharing.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `FingerTreeDeque<T>`, `FingerTree<TElement, TMeasure, TMeasureOps>`, `RrbVector<T>`, `RrbVector<T>.Builder` | [Workspace](../../src/CSharp/docs/FingerTree/overview.md), [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md), [deque source](../../src/CSharp/src/Durable7.FingerTree/FingerTreeDeque.cs), [measured tree source](../../src/CSharp/src/Durable7.FingerTree/FingerTree.cs), [RRB vector and builder](../../src/CSharp/src/Durable7.FingerTree/RrbVector.cs) |
| C | `ft_tree`, `ft_tree_policy`, `ft_measure_policy`, `ft_persistent_deque`, `ft_rrb_vector`, `ft_rrb_builder` | [Workspace](../../src/C/FingerTree/README.md), [usage guide](../../src/C/FingerTree/docs/usage.md), [API notes](../../src/C/FingerTree/docs/api-notes.md), [finger-tree header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [RRB header](../../src/C/FingerTree/include/durable7/finger_tree/rrb_vector.h) |
| C++ | `persistent_deque<T>`, `finger_tree<Element, MeasurePolicy>`, `rrb_vector<T>`, `rrb_vector_builder<T>` | [Workspace](../../src/Cpp/FingerTree/README.md), [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [aggregate header](../../src/Cpp/FingerTree/include/durable7/finger_tree/finger_tree.hpp), [deque header](../../src/Cpp/FingerTree/include/durable7/finger_tree/persistent_deque.hpp), [measured tree header](../../src/Cpp/FingerTree/include/durable7/finger_tree/measured_finger_tree.hpp), [RRB vector header](../../src/Cpp/FingerTree/include/durable7/finger_tree/rrb_vector.hpp) |
| Haskell | `Deque a`, `FingerTree v a`, `Measured v a`, `RrbVector a` | [Workspace](../../src/Haskell/FingerTree/README.md), [deque source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/Deque.hs), [measured tree source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/Measured.hs), [RRB source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/RrbVector.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `PersistentDeque<T>`, `FingerTree<T, M>`, `MeasurePolicy<T, M>`, `RrbVector<T>`, `RrbVector.Builder<T>` | [Workspace](../../src/Kotlin/FingerTree/README.md), [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [public facades](../../src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt), [measured AVL engine](../../src/Kotlin/FingerTree/src/durable7/fingertree/PersistentMeasuredTree.kt), [RRB source](../../src/Kotlin/FingerTree/src/durable7/fingertree/RrbVector.kt), [tests](../../src/Kotlin/FingerTree/tests/README.md) |
| Rust | `PersistentDeque<T>`, `FingerTree<T, P>`, `MeasurePolicy<T>`, `RrbVector<T>`, `RrbVectorBuilder<T>` | [Workspace](../../src/Rust/FingerTree/README.md), [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [deque source](../../src/Rust/FingerTree/src/deque.rs), [measured source](../../src/Rust/FingerTree/src/measured.rs), [RRB source](../../src/Rust/FingerTree/src/rrb_vector.rs) |
| OCaml | `Persistent_deque`, `Measured_tree`, `Measured_sequence`, `Measures`, `Rrb_vector` and builder | [Workspace](../../src/OCaml/README.md), [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `PersistentDeque<T>`, `FingerTree<T, M>`, `MeasurePolicy<T, M>`, `RrbVector<T>`, `RrbVectorBuilder<T>` | [Workspace](../../src/TypeScript/README.md), [core](../../src/TypeScript/src/finger-tree/core.ts), [RRB vector](../../src/TypeScript/src/finger-tree/rrb-vector.ts) |
| Python | `PersistentDeque`, `FingerTree`, `MeasuredSequence`, `MeasurePolicy`, `RrbVector`, `RrbVectorBuilder` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [measured AVL/core](../../src/Python/src/durable7/finger_tree/measured_sequence.py), [RRB vector](../../src/Python/src/durable7/finger_tree/rrb_vector.py), [tests](../../src/Python/tests/README.md) |

## Range-Update Sequence

All nine languages ship a language-local `RangeUpdateSequence` as an immutable indexed sequence
with logarithmic persistent point edits, concatenation/splitting, range extraction, lazy range
updates, and ordered range-measure queries. It is an independently implemented path-copied implicit
AVL sibling in the FingerTree assembly, not a tagged modification of either existing finger-tree
engine. `IRangeUpdateAlgebra<TElement, TMeasure, TTag>` combines the ordinary ordered measure
monoid with a tag monoid and actions on elements and cached measures;
`Compose(newer, older)` means apply `older` first and `newer` second.

Each node's logical value and cached measure already include its own pending tag while its children
do not. Structural descent and rotations push tags before rearranging nodes, and read-only indexing,
measurement, and enumeration carry inherited tags without mutating shared storage. The public
contract locks algebra laws, validation precedence, no-op identity, failure atomicity, retained-version
isolation, a fail-fast copied struct enumerator, AVL balance/count/measure invariants, and deterministic
operation-count ceilings. Whole-sequence application of a nonidentity tag allocates one new root;
proper subrange updates and queries perform logarithmic boundary work.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IRangeUpdateAlgebra<TElement, TMeasure, TTag>`, `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` | [Contract, algebra, and invariants](../../src/CSharp/docs/FingerTree/range-update-sequence.md), [API specification](../../src/CSharp/docs/FingerTree/api-specification.md#range-update-sequence), [source](../../src/CSharp/src/Durable7.FingerTree/RangeUpdateSequence.cs), [algebra source](../../src/CSharp/src/Durable7.FingerTree/IRangeUpdateAlgebra.cs), [validation](../../src/CSharp/docs/FingerTree/validation.md#range-update-sequence-integration-gate), [tests](../../src/CSharp/tests/Durable7.FingerTree.Tests/README.md) |
| C | `ft_range_update_policy`, `ft_range_update_sequence` | [public header](../../src/C/FingerTree/include/durable7/finger_tree/range_update_sequence.h), [implementation](../../src/C/FingerTree/src/range_update_sequence.c), [tests](../../src/C/FingerTree/tests/range_update_sequence_tests.c) |
| C++ | `range_update_sequence<Element, Algebra>` | [header](../../src/Cpp/FingerTree/include/durable7/finger_tree/range_update_sequence.hpp), [tests](../../src/Cpp/FingerTree/tests/range_update_sequence_tests.cpp), [validation](../../src/Cpp/FingerTree/docs/validation.md) |
| Haskell | `RangeUpdateAlgebra`, `RangeUpdateSequence` | [source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/RangeUpdateSequence.hs), [tests](../../src/Haskell/FingerTree/test/RangeUpdateSequenceTests.hs) |
| Kotlin | `RangeUpdateAlgebra<T, M, Tag>`, `RangeUpdateSequence<T, M, Tag>` | [source](../../src/Kotlin/FingerTree/src/durable7/fingertree/RangeUpdateSequence.kt), [contract](../../src/Kotlin/FingerTree/docs/range-update-sequence.md), [tests](../../src/Kotlin/FingerTree/test/durable7/fingertree/RangeUpdateSequenceTests.kt) |
| Rust | `RangeUpdateAlgebra`, `RangeUpdateSequence` | [crate](../../src/Rust/RangeUpdate/README.md), [source](../../src/Rust/RangeUpdate/src/lib.rs), [tests](../../src/Rust/RangeUpdate/tests/range_update_sequence.rs) |
| OCaml | `Range_update_sequence` and law-gated algebra | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree/range_update_sequence.mli), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `RangeUpdateAlgebra<T, M, Tag>`, `RangeUpdateSequence<T, M, Tag>` | [contract](../../src/TypeScript/docs/range-update-sequence.md), [source](../../src/TypeScript/src/finger-tree/range-update-sequence.ts), [tests](../../src/TypeScript/test/finger-tree/range-update-sequence.test.ts) |
| Python | `RangeUpdateAlgebra`, `RangeUpdateSequence` | [contract](../../src/Python/docs/range-update-sequence.md), [source](../../src/Python/src/durable7/finger_tree/range_update_sequence.py), [tests](../../src/Python/tests/finger_tree/test_range_update_sequence.py) |

At the Range shipment checkpoint, both full serialized C# Debug and Release solution builds
completed with zero warnings and zero errors and passed 1,417/1,417 tests. The current derived-
structure checkpoint passes 1,158/1,158 in both configurations. No benchmark was run; measurement
remains postponed until an isolated session. The single-pass HAMT updates, hash bag, ordered set,
and range-update sequence ship across all nine languages; the detailed earlier evidence is in the
[cross-language completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md).

## Reversible Deque

Reversible deques add orientation-aware views over persistent deque storage so reversal can be
represented without eagerly copying the sequence.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `ReversibleDeque<T>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [source](../../src/CSharp/src/Durable7.FingerTree/ReversibleDeque.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_reversible_deque` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `reversible_deque<T>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/durable7/finger_tree/reversible_deque.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `ReversibleDeque a` | [source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/ReversibleDeque.hs), [tests](../../src/Haskell/FingerTree/test/README.md), [complexity audit](reversible-deque-complexity-audit.md) |
| Kotlin | `ReversibleDeque<T>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [source](../../src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt), [complexity audit](reversible-deque-complexity-audit.md) |
| Rust | `ReversibleDeque<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/deque.rs) |
| OCaml | `Reversible_deque` | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree/reversible_deque.mli), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `ReversibleDeque<T>` | [API notes](../../src/TypeScript/docs/api-notes.md), [source](../../src/TypeScript/src/finger-tree/core.ts) |
| Python | `ReversibleDeque` | [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/durable7/finger_tree/core.py), [tests](../../src/Python/tests/README.md) |

## Sorted Collections

Sorted collections expose immutable sorted bags/multisets, sets, and key-value maps with
comparer-preserving behavior. The mature C#/C++/C ports use order-statistic measures over finger
trees; the Rust checkpoint now uses cached count plus last-key order-statistic measures over its
measured tree while its API notes track the remaining lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `SortedBag<T>`, `SortedSet<T>`, sorted builders, `SortedDictionary<TKey, TValue>`, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [bag](../../src/CSharp/src/Durable7.FingerTree/SortedBag.cs), [set](../../src/CSharp/src/Durable7.FingerTree/SortedSet.cs), [dictionary](../../src/CSharp/src/Durable7.FingerTree/SortedDictionary.cs), [canonical set](../../src/CSharp/src/Durable7.FingerTree/CanonicalSortedSet.cs), [rank policy](../../src/CSharp/src/Durable7.FingerTree/ZipTreeRankPolicy.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_sorted_multiset`, `ft_sorted_set`, `ft_sorted_map`, `ft_canonical_sorted_set`, `ft_canonical_policy` | [usage guide](../../src/C/FingerTree/docs/usage.md), [API notes](../../src/C/FingerTree/docs/api-notes.md), [canonical set and policy header](../../src/C/FingerTree/include/durable7/finger_tree/canonical_sorted_set.h), [core public header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [tests](../../src/C/FingerTree/tests/README.md) |
| C++ | `sorted_bag<T, Less>`, `sorted_set<T, Less>`, `sorted_map<Key, T, Less>`, `canonical_sorted_set<T>`, `zip_tree_rank_policy<T>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [canonical set and policy](../../src/Cpp/FingerTree/include/durable7/finger_tree/canonical_sorted_set.hpp), [bag header](../../src/Cpp/FingerTree/include/durable7/finger_tree/sorted_bag.hpp), [set header](../../src/Cpp/FingerTree/include/durable7/finger_tree/sorted_set.hpp), [map header](../../src/Cpp/FingerTree/include/durable7/finger_tree/sorted_map.hpp), [tests](../../src/Cpp/FingerTree/tests/README.md) |
| Haskell | `SortedBag a`, `SortedSet a`, `SortedMap k v`, `CanonicalSortedSet a`, `ZipTreeRankPolicy a` | [canonical-set guide](../../src/Haskell/FingerTree/docs/canonical-sorted-set.md), [canonical source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/CanonicalSortedSet.hs), [bag source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/SortedBag.hs), [set source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/SortedSet.hs), [map source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/SortedMap.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>`, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [measured sorted collections](../../src/Kotlin/FingerTree/src/durable7/fingertree/Sorted.kt), [canonical set and rank policy](../../src/Kotlin/FingerTree/src/durable7/fingertree/CanonicalSortedSet.kt), [tests](../../src/Kotlin/FingerTree/tests/README.md) |
| Rust | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>`, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [measured sorted collections](../../src/Rust/FingerTree/src/sorted.rs), [canonical set and rank policy](../../src/Rust/FingerTree/src/canonical_sorted_set.rs), [tests](../../src/Rust/FingerTree/tests/README.md) |
| OCaml | `Sorted_bag`, `Sorted_set` and builder, `Sorted_map` and builder, `Canonical_sorted_set` and rank policy | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>`, builders, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [sorted facades](../../src/TypeScript/src/finger-tree/sorted.ts), [canonical set](../../src/TypeScript/src/finger-tree/canonical-sorted-set.ts), [tests](../../src/TypeScript/test/README.md) |
| Python | `SortedBag`, `SortedSet`, `SortedMap`, `SortedSetBuilder`, `SortedMapBuilder`, `CanonicalSortedSet`, `ZipTreeRankPolicy` | [API notes](../../src/Python/docs/api-notes.md), [sorted facades](../../src/Python/src/durable7/finger_tree/sorted.py), [canonical set](../../src/Python/src/durable7/finger_tree/canonical_sorted_set.py), [tests](../../src/Python/tests/README.md) |

## Priority Queue

Priority queues locate and remove the front priority entry according to each language's comparison
policy. The mature C#/C++/C ports use measured finger-tree facades; the Rust checkpoint now uses
cached minimum-priority measures over its measured tree while its API notes track the remaining
lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PriorityQueue<TElement, TPriority>`, `BrodalOkasakiHeap<T>`, `PrioritySearchQueue<TKey, TPriority, TValue>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [measured priority queue](../../src/CSharp/src/Durable7.FingerTree/PriorityQueue.cs), [Brodal–Okasaki heap](../../src/CSharp/src/Durable7.FingerTree/BrodalOkasakiHeap.cs), [priority-search queue](../../src/CSharp/src/Durable7.FingerTree/PrioritySearchQueue.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_priority_queue`, `ft_brodal_policy`, `ft_brodal_heap`, `ft_priority_search_queue`, `ft_priority_search_entry` | [usage guide](../../src/C/FingerTree/docs/usage.md), [measured priority-queue header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [Brodal-Okasaki header](../../src/C/FingerTree/include/durable7/finger_tree/brodal_okasaki_heap.h), [priority-search header](../../src/C/FingerTree/include/durable7/finger_tree/priority_search_queue.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `priority_queue<Element, Priority, Comparison>`, `brodal_okasaki_heap<T, Less>`, `brodal_okasaki_heap_statistics`, `priority_search_queue<K, P, V, KeyLess, PriorityLess>`, `priority_search_entry<K, P, V>`, and typed add/remove/minimum results | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [measured priority-queue header](../../src/Cpp/FingerTree/include/durable7/finger_tree/priority_queue.hpp), [Brodal-Okasaki header](../../src/Cpp/FingerTree/include/durable7/finger_tree/brodal_okasaki_heap.hpp), [priority-search header](../../src/Cpp/FingerTree/include/durable7/finger_tree/priority_search_queue.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `PriorityQueue p a`, `BrodalOkasakiHeap a`, `PrioritySearchQueue k p v`, `PrioritySearchEntry k p v` | [measured priority queue](../../src/Haskell/FingerTree/src/Durable7/FingerTree/PriorityQueue.hs), [Brodal-Okasaki heap](../../src/Haskell/FingerTree/src/Durable7/FingerTree/BrodalOkasakiHeap.hs), [priority-search queue](../../src/Haskell/FingerTree/src/Durable7/FingerTree/PrioritySearchQueue.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `PriorityQueue<T, P>`, `PriorityEntry<T, P>`, `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [measured priority queue](../../src/Kotlin/FingerTree/src/durable7/fingertree/PriorityAndInterval.kt), [Brodal-Okasaki heap](../../src/Kotlin/FingerTree/src/durable7/fingertree/BrodalOkasakiHeap.kt), [priority-search queue](../../src/Kotlin/FingerTree/src/durable7/fingertree/PrioritySearchQueue.kt), [priority-core notes](../../src/Kotlin/FingerTree/docs/priority-cores.md) |
| Rust | `PriorityQueue<T, P>`, `PriorityEntry<T, P>`, `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>`, `OrderPolicy<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [measured priority queue](../../src/Rust/FingerTree/src/priority_queue.rs), [Brodal-Okasaki heap](../../src/Rust/FingerTree/src/brodal_okasaki_heap.rs), [priority-search queue](../../src/Rust/FingerTree/src/priority_search_queue.rs), [ordering policy](../../src/Rust/FingerTree/src/ordering.rs), [Brodal notes](../../src/Rust/FingerTree/docs/brodal-okasaki-heap.md), [PSQ notes](../../src/Rust/FingerTree/docs/priority-search-queue.md) |
| OCaml | `Priority_queue`, `Brodal_okasaki_heap`, `Priority_search_queue` and typed entry/statistics values | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `PriorityQueue<T, P>`, `BrodalOkasakiHeap<T>`, `PrioritySearchQueue<K, P, V>` and typed view/result values | [measured queue](../../src/TypeScript/src/finger-tree/priority-interval.ts), [Brodal heap](../../src/TypeScript/src/finger-tree/brodal-okasaki-heap.ts), [priority-search queue](../../src/TypeScript/src/finger-tree/priority-search-queue.ts) |
| Python | `PriorityQueue`, `PriorityEntry`, `BrodalOkasakiHeap`, `BrodalMinimumView`, `PrioritySearchQueue`, `PrioritySearchEntry`, and typed result/statistics values | [API notes](../../src/Python/docs/api-notes.md), [measured queue](../../src/Python/src/durable7/finger_tree/priority_interval.py), [Brodal heap](../../src/Python/src/durable7/finger_tree/brodal_okasaki_heap.py), [priority-search queue](../../src/Python/src/durable7/finger_tree/priority_search_queue.py), [tests](../../src/Python/tests/README.md) |

## Interval Tree

Interval trees store ordered interval collections for overlap and containment queries. The mature
C#/C++/C ports use interval annotations in finger-tree measures so queries can skip subtrees whose
summary cannot intersect the probe; the Rust checkpoint now uses cached maximum-high measures over
its measured tree while its API notes track the remaining lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IntervalTree<T>`, `Interval<T>`, `IntervalMeasure<T>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [source](../../src/CSharp/src/Durable7.FingerTree/IntervalTree.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_interval_tree`, `ft_interval_tree_i64`, `ft_interval_i64` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `interval_tree<T, Comparison>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/durable7/finger_tree/interval_tree.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `IntervalTree a`, `Interval a` | [source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/IntervalTree.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `IntervalTree<T>`, `Interval<T>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [source](../../src/Kotlin/FingerTree/src/durable7/fingertree/PriorityAndInterval.kt) |
| Rust | `IntervalTree<T>`, `Interval<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/interval_tree.rs) |
| OCaml | `Interval_tree`, `Persistent_interval_map` | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `IntervalTree<T>`, `Interval<T>` | [source](../../src/TypeScript/src/finger-tree/priority-interval.ts), [tests](../../src/TypeScript/test/finger-tree/core.test.ts) |
| Python | `IntervalTree`, `Interval` | [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/durable7/finger_tree/priority_interval.py), [tests](../../src/Python/tests/README.md) |

## Ropes And Text

Ropes provide persistent chunked sequences, measured ropes add custom split/locate measures, and
text ropes specialize the same machinery for newline-aware text navigation. C# ships version-bound
positional and measured cursors with cached canonical snapshots. C ships an explicit-lifetime snapshot-plus-gap
positional and measured cursor pair plus a nominal text cursor. C++, Kotlin, and Rust ship snapshot-plus-gap
positional and measured cursor checkpoints, plus a thin `TextRopeCursor` facade that preserves its
newline-aware text surface. Both measured APIs expose ordered before/after measures and absolute
measure seek. Haskell likewise ships opaque snapshot-plus-gap `MeasuredRopeCursor v a` and
`TextRopeCursor` aliases with ordered measures, absolute search, pure persistent edits, and Haskell
`Char`-element text positions. Every sibling cursor preserves branching and edit behavior without
claiming the C# focused cursor representation or its focus-local complexity. C text positions count byte-sized `char` elements and use
the existing LF-only zero-based line/column rules.
The Rust checkpoint uses chunked measured storage for both positional `Rope<T>` and custom-measured
`MeasuredRope<T, P>` and stores `TextRope` content in a newline-measured rope while its API notes track
the remaining lazy-spine parity boundary.
Python uses the same snapshot-plus-gap semantics over its persistent measured-AVL checkpoint;
`TextRope` and `TextRopeCursor` count Python Unicode code points rather than UTF-16 units or
grapheme clusters.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `Rope<T>`, `RopeCursor<T>`, `Rope<T>.Builder`, `MeasuredRope<T, TMeasure, TMeasureOps>`, `MeasuredRopeCursor<T, TMeasure, TMeasureOps>`, `MeasuredRope<T, TMeasure, TMeasureOps>.Builder`, `RopeText`, `RopeBuilder`, `NewlineMeasure`, `NewlineStyle` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [rope](../../src/CSharp/src/Durable7.FingerTree/Rope.cs), [positional cursor](../../src/CSharp/src/Durable7.FingerTree/Rope.Cursor.cs), [cursor C0 decision and complexity scope](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md), [rope builder](../../src/CSharp/src/Durable7.FingerTree/Rope.Builder.cs), [measured rope](../../src/CSharp/src/Durable7.FingerTree/MeasuredRope.cs), [measured cursor](../../src/CSharp/src/Durable7.FingerTree/MeasuredRope.Cursor.cs), [cursor C2 decision and evidence](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md), [measured rope builder](../../src/CSharp/src/Durable7.FingerTree/MeasuredRope.Builder.cs), [text](../../src/CSharp/src/Durable7.FingerTree/RopeText.cs), [text builder](../../src/CSharp/src/Durable7.FingerTree/RopeBuilder.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md), [validation](../../src/CSharp/docs/FingerTree/validation.md) |
| C | `ft_rope`, `ft_rope_cursor`, `ft_measured_rope`, `ft_measured_rope_cursor`, `ft_text_rope`, `ft_text_rope_cursor`, `ft_line_column` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `rope<T>`, `rope_cursor<T>`, `measured_rope<T, MeasurePolicy>`, `measured_rope_cursor<T, MeasurePolicy>`, `measured_rope_cursor_search_result<T, MeasurePolicy>`, `text_rope`, `text_rope_cursor`, `text_rope_cursor_search_result`, `rope_builder`, `newline_measure`, `line_column` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [rope header](../../src/Cpp/FingerTree/include/durable7/finger_tree/rope.hpp), [measured rope/cursor header](../../src/Cpp/FingerTree/include/durable7/finger_tree/measured_rope.hpp), [text header](../../src/Cpp/FingerTree/include/durable7/finger_tree/rope_text.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `Rope a`, `RopeCursor a`, `MeasuredRope v a`, `MeasuredRopeCursor v a`, `MeasuredRopeCursorSearch v a`, `TextRope`, `TextRopeCursor`, `TextRopeCursorSearch`, `NewlineMeasure` | [workspace and cursor contract](../../src/Haskell/FingerTree/README.md), [rope/cursor source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/Rope.hs), [measured rope/cursor source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/MeasuredRope.hs), [text cursor source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/Rope/Text.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `Rope<T>`, `RopeCursor<T>`, `RopeCursorPeek<T>`, `MeasuredRope<T, M>`, `MeasuredRopeCursor<T, M>`, `MeasuredRopeCursorSearch<T, M>`, `TextRope`, `TextRopeCursor`, `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [rope source](../../src/Kotlin/FingerTree/src/durable7/fingertree/Rope.kt), [measured cursor](../../src/Kotlin/FingerTree/src/durable7/fingertree/MeasuredRopeCursor.kt), [text cursor](../../src/Kotlin/FingerTree/src/durable7/fingertree/TextRopeCursor.kt) |
| Rust | `Rope<T>`, `RopeCursor<T>`, `MeasuredRope<T, P>`, `MeasuredRopeCursor<T, P>`, `MeasuredRopeCursorSearch<T, P>`, `TextRope`, `TextRopeCursor`, `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/rope.rs) |
| OCaml | `Rope` and builder, `Rope_cursor`, `Measured_rope` and cursor, `Text_rope`, `Text_rope_cursor` | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `Rope<T>`, `RopeCursor<T>`, `RopeCursorPeek<T>`, `MeasuredRope<T, M>`, `MeasuredRopeCursor<T, M>`, `TextRope`, `TextRopeCursor`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/TypeScript/docs/api-notes.md), [source](../../src/TypeScript/src/finger-tree/rope.ts), [tests](../../src/TypeScript/test/finger-tree/rope-daba.test.ts) |
| Python | `Rope`, `RopeCursor`, `RopeCursorPeek`, `MeasuredRope`, `MeasuredRopeCursor`, `TextRope`, `TextRopeCursor`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/durable7/finger_tree/rope.py), [tests](../../src/Python/tests/README.md) |

## Measures, Comparisons, And Predicates

Measures are the connective tissue for finger-tree-derived collections. The C#, C++, Kotlin, and Python
workspaces expose typed measure abstractions (with comparison and predicate abstractions where their
surfaces require them); the C workspace exposes equivalent policy callbacks and context pointers.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IMonoid<TMeasure>`, `IMeasure<TElement, TMeasure>`, `IMeasurePredicate<TMeasure>`, `IComparison<T>`, `DabaLite<T, TMonoid>`, built-in and product measures | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [measures](../../src/CSharp/src/Durable7.FingerTree/Measures.cs), [DABA Lite](../../src/CSharp/src/Durable7.FingerTree/DabaLite.cs), [predicates](../../src/CSharp/src/Durable7.FingerTree/MeasurePredicate.cs), [comparisons](../../src/CSharp/src/Durable7.FingerTree/Comparisons.cs), [built-ins](../../src/CSharp/src/Durable7.FingerTree/BuiltInMeasures.cs) |
| C | `ft_daba_policy`, `ft_daba_lite`, `ft_daba_lite_statistics`, `ft_measure_policy`, `ft_measure_predicate_fn`, `ft_compare_fn`, `ft_value_type` | [usage guide](../../src/C/FingerTree/docs/usage.md), [DABA Lite header](../../src/C/FingerTree/include/durable7/finger_tree/daba_lite.h), [core public header](../../src/C/FingerTree/include/durable7/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `daba_lite_value<T>`, `daba_lite_monoid_policy<P, T>`, `daba_lite<T, P>`, `daba_lite_statistics`, plus measure policies and predicates in `measures.hpp`, `built_in_measures.hpp`, `product_measure.hpp`, `sum_measure.hpp`, `comparisons.hpp`, and `measure_predicates.hpp` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [DABA Lite](../../src/Cpp/FingerTree/include/durable7/finger_tree/daba_lite.hpp), [measure headers](../../src/Cpp/FingerTree/include/durable7/finger_tree/measures.hpp), [built-ins](../../src/Cpp/FingerTree/include/durable7/finger_tree/built_in_measures.hpp), [predicates](../../src/Cpp/FingerTree/include/durable7/finger_tree/measure_predicates.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `Measured v a`, `Size`, `Elem`, `MeasurePair`, `Maximum`, `Minimum`; predicates are ordinary pure functions `v -> Bool` | [measured tree source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/Measured.hs), [measures source](../../src/Haskell/FingerTree/src/Durable7/FingerTree/Measures.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `Monoid<T>`, `MeasurePolicy<T, M>`, `DabaLite<T>`, `DabaLiteStatistics`, `SizeMeasure<T>`, `IntSumMeasure`, `MaxMeasure<T>`, `MinMeasure<T>`, `ProductMeasure<T, A, B>`, `MeasurePair<A, B>`, `NewlineMeasure` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [monoids and measures](../../src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt), [DABA Lite](../../src/Kotlin/FingerTree/src/durable7/fingertree/DabaLite.kt), [newline measure](../../src/Kotlin/FingerTree/src/durable7/fingertree/Rope.kt) |
| Rust | `DabaMonoid<T>`, `DabaLite<T, M>`, `DabaLiteStatistics`, `MeasurePolicy<T>`, `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, `KeyMeasure<T>`, `ProductMeasure<T, PFirst, PSecond>`, `MeasurePair<TFirst, TSecond>`, `SizeAndSumMeasure<T>`, `SizeAndMaxMeasure<T>`, `SizeAndMinMeasure<T>`, `OrderStatisticMeasure<T>`, `RankedKey<T>`, `NewlineMeasure` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [DABA Lite](../../src/Rust/FingerTree/src/daba_lite.rs), [measures](../../src/Rust/FingerTree/src/measured.rs), [newline measure](../../src/Rust/FingerTree/src/rope.rs) |
| OCaml | `Measures` policies and monoids, `Daba_lite` and statistics, `Common.Comparator` | [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/finger_tree), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `Monoid<M>`, `MeasurePolicy<T, M>`, `DabaLite<T, M>`, size/sum/min/max/product measures, comparators, and `NewlineMeasure` | [measures](../../src/TypeScript/src/finger-tree/measures.ts), [DABA Lite](../../src/TypeScript/src/finger-tree/daba-lite.ts), [API notes](../../src/TypeScript/docs/api-notes.md) |
| Python | `Monoid`, `MeasurePolicy`, `DabaLite`, `DabaLiteStatistics`, size/sum/min/max/product measures, comparators, and `NewlineMeasure` | [API notes](../../src/Python/docs/api-notes.md), [measures](../../src/Python/src/durable7/finger_tree/measures.py), [DABA Lite](../../src/Python/src/durable7/finger_tree/daba_lite.py), [tests](../../src/Python/tests/README.md) |

## Insertion-Ordered Persistent Set

The neutral Ordered family composes hashed membership with a persistent ordered sequence without
depending on it. Equality policy determines membership and first representatives; enumeration
preserves insertion or explicitly requested order. Duplicate additions never move or replace a
representative. Explicit movement, positional ranges, reversal, stable one-shot sorting,
receiver-policy set algebra, and all six set relations preserve immutable versions and documented
identity no-ops. Algebra eagerly normalizes the complete argument under the receiver policy before
applying shortcuts, retaining receiver representatives and first normalized argument representatives.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentOrderedSet<T>` | [Workspace](../../src/CSharp/docs/Ordered/overview.md), [usage guide](../../src/CSharp/docs/Ordered/usage.md), [API specification](../../src/CSharp/docs/Ordered/api-specification.md), [source](../../src/CSharp/src/Durable7.Ordered), [validation](../../src/CSharp/docs/Ordered/validation.md), [tests](../../src/CSharp/tests/Durable7.Ordered.Tests/README.md) |
| C | `d7_ordered_set` | [workspace](../../src/C/Ordered/README.md), [public header](../../src/C/Ordered/include/durable7/ordered/ordered_set.h), [tests](../../src/C/Ordered/tests/ordered_set_tests.c) |
| C++ | `persistent_ordered_set<T, Hash, KeyEqual>` | [workspace](../../src/Cpp/Ordered/README.md), [header](../../src/Cpp/Ordered/include/durable7/ordered/persistent_ordered_set.hpp), [tests](../../src/Cpp/Ordered/tests/persistent_ordered_set_tests.cpp) |
| Haskell | `PersistentOrderedSet` | [workspace](../../src/Haskell/Ordered/README.md), [source](../../src/Haskell/Ordered/src/Durable7/Ordered/PersistentOrderedSet.hs) |
| Kotlin | `PersistentOrderedSet<T>` | [workspace](../../src/Kotlin/Ordered/README.md), [source](../../src/Kotlin/Ordered/src/durable7/ordered/PersistentOrderedSet.kt) |
| Rust | `PersistentOrderedSet<T, S>` | [workspace](../../src/Rust/Ordered/README.md), [source](../../src/Rust/Ordered/src/lib.rs), [tests](../../src/Rust/Ordered/tests/persistent_ordered_set.rs) |
| OCaml | `Persistent_ordered_set`, `Persistent_ordered_map`, `Persistent_ordered_multimap` | [workspace](../../src/OCaml/README.md), [API notes](../../src/OCaml/docs/api-notes.md), [source](../../src/OCaml/lib/ordered), [tests](../../src/OCaml/tests/README.md) |
| TypeScript | `PersistentOrderedSet<T>`, idiomatic lookup/removal result values | [Workspace](../../src/TypeScript/README.md), [API notes](../../src/TypeScript/docs/api-notes.md), [source](../../src/TypeScript/src/ordered), [tests](../../src/TypeScript/test/ordered) |
| Python | `PersistentOrderedSet`, `OrderedSetValueResult`, `OrderedSetRemoveResult` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/durable7/ordered), [tests](../../src/Python/tests/ordered) |

All nine ports ship. The C# focused single-worker Debug and Release lanes each pass 62 tests. At
the historical pre-Range Ordered shipment checkpoint, the full
serialized C# Release build had zero warnings and zero errors and the complete gate passed
1,355/1,355 tests; current full-workspace evidence is the 1,158/1,158 Debug and Release derived-
structure gate recorded above. No benchmark was run for either shipment, and measurements remain postponed for an
isolated session.

## Research-Derived Collections

Seven collections originated as scoped design studies, each with a normative proposal under
[`docs/proposals`](../proposals/) recording its claims, prior-art boundary, and deliberate
limitations. They were promoted out of the former `*.Experimental` namespaces into the ordinary
family namespaces and ported to Rust, C, Haskell, Kotlin, and C++. Coverage is **C#, Rust, C,
Haskell, Kotlin, and C++** — the remaining three languages are deliberately unported pending a named
consumer, so absence elsewhere is a scheduling decision rather than a gap. Every one of them separates shipped
bounds from theoretical instantiations; read the proposal before relying on a headline complexity.

| Collection | Role | Proposal |
| --- | --- | --- |
| `AncestralSliceQueue<T>` | Persistent queue whose handle is an appendable interval of one root-to-node path in an append-only arena | [ASQ](../proposals/ancestral-slice-queue-2026-07-25.md) |
| `BilateralAncestralDeque<T>` | Restricted persistent deque as two oppositely oriented ancestry intervals, with O(1) reverse | [BAD](../proposals/bilateral-ancestral-deque-2026-07-25.md) |
| `ContextualRankSequence<TElement, TMachine>` | Measured sequence lifting a finite event machine into an all-start-state summary for contextual rank/select | [CRS](../proposals/contextual-rank-sequence-2026-07-25.md) |
| `PersistentDeltaMap<TKey, TValue>` | Sorted map with a designated checkpoint and a coalesced exact net-change index | [PDM](../proposals/persistent-delta-map-2026-07-25.md) |
| `PersistentRunDeltaVector<T>` | Fixed-length current/checkpoint vector with an exact maximal dirty-run index | [PRDV](../proposals/persistent-run-delta-vector-2026-07-29.md) |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | Brodal-Okasaki heap with lazily composed monotone priority actions | [PMAH](../proposals/persistent-monotone-action-heap-2026-07-29.md) |
| `PersistentAncestralConnectionForest` | Branching insertion-only union-find answering first-connected-at-which-version | [PACF](../proposals/persistent-ancestral-connection-forest-2026-07-29.md) |

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `AncestralSliceQueue<T>`, `BilateralAncestralDeque<T>`, `IIncrementalAncestorArena<T>`, `MyersIncrementalAncestorArena<T>`, `ContextualRankSequence<TElement, TMachine>`, `IContextualEventMachine<TElement>`, `PersistentDeltaMap<TKey, TValue>`, `PersistentRunDeltaVector<T>`, `PersistentMonotoneActionHeap<TElement, TPriority, TAction>`, `IMonotoneHeapAction<TPriority, TAction>`, `OrderClampPolicy<T>` (FingerTree); `PersistentAncestralConnectionForest`, `AncestralConnectionVersion` (Hamt) | [FingerTree API spec](../../src/CSharp/docs/FingerTree/api-specification.md), [FingerTree usage](../../src/CSharp/docs/FingerTree/usage.md), [Hamt API spec](../../src/CSharp/docs/Hamt/api-specification.md), [Hamt usage](../../src/CSharp/docs/Hamt/usage.md) |
| Rust | `AncestralSliceQueue<T>`, `BilateralAncestralDeque<T>`, `IncrementalAncestorArena<T>`, `MyersAncestorArena<T>`, `ContextualRankSequence<T, M>`, `ContextualEventMachine<T>`, `PersistentDeltaMap<K, V>`, `PersistentRunDeltaVector<T>`, `PersistentMonotoneActionHeap<E, P, A>`, `MonotoneHeapAction<P, A>`, `ActionPolicy<P, A>`, `OrderClampPolicy<T>`, `EqualityPolicy<T>` (FingerTree); `PersistentAncestralConnectionForest`, `AncestralConnectionVersion` (Hamt) | [FingerTree API notes](../../src/Rust/FingerTree/docs/api-notes.md), [Hamt API notes](../../src/Rust/Hamt/docs/api-notes.md), [FingerTree README](../../src/Rust/FingerTree/README.md), [Hamt README](../../src/Rust/Hamt/README.md) |

| C | `ft_ancestral_slice_queue_*`, `ft_bilateral_ancestral_deque_*`, `ft_incremental_ancestor_arena_*` (vtable seam plus the shipped Myers arena), `ft_contextual_rank_sequence_*`, `ft_persistent_delta_map_*`, `ft_persistent_run_delta_vector_*`, `ft_monotone_action_heap_*` with its clamp policy family (FingerTree); `d7_hamt_ancestral_connection_forest_*` (Hamt) | [FingerTree API notes](../../src/C/FingerTree/docs/api-notes.md), [FingerTree headers](../../src/C/FingerTree/include/durable7/finger_tree/), [Hamt API specification](../../src/C/Hamt/docs/api-specification.md), [Hamt headers](../../src/C/Hamt/include/durable7/hamt/) |
| C++ | `durable7::finger_tree::ancestral_slice_queue<T, Arena>`, `bilateral_ancestral_deque<T, Arena>`, the `incremental_ancestor_arena` **concept** with the shipped `myers_incremental_ancestor_arena<T>` (the backend seam is a compile-time template parameter, not a runtime interface), `contextual_rank_sequence<T, Machine>`, `persistent_delta_map<K, V, KeyLess, ValueEqual>`, `persistent_run_delta_vector<T, EqualityPolicy>`, `persistent_monotone_action_heap<E, P, Policy>` with its `order_clamp` family (FingerTree); `durable7::hamt::persistent_ancestral_connection_forest` (Hamt). All reachable through the aggregate headers | [FingerTree API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [Hamt API specification](../../src/Cpp/Hamt/docs/api-specification.md), [workspace README](../../src/Cpp/README.md) |
| Kotlin | `durable7.fingertree.AncestralSliceQueue<T>`, `BilateralAncestralDeque<T>`, `IncrementalAncestorArena<T>` with the shipped `MyersIncrementalAncestorArena<T>` (a faithful arena port — the JVM supplies the mutable state and monitor the reference assumes), `ContextualRankSequence<T>`, `PersistentDeltaMap<K, V>`, `PersistentRunDeltaVector<T>`, `PersistentMonotoneActionHeap<E, P, A>` with its `OrderClamp` family (FingerTree); `durable7.hamt.PersistentAncestralConnectionForest` (Hamt) | [FingerTree API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [Hamt API notes](../../src/Kotlin/Hamt/docs/api-notes.md), [workspace README](../../src/Kotlin/README.md) |
| Haskell | `Durable7.FingerTree.AncestralSliceQueue`, `.BilateralAncestralDeque`, `.IncrementalAncestor` (the shared seam, with no arena object), `.ContextualRankSequence`, `.PersistentDeltaMap`, `.PersistentRunDeltaVector`, `.PersistentMonotoneActionHeap` (FingerTree); `Durable7.Hamt.PersistentAncestralConnectionForest` (Hamt). Each is reachable through its own module rather than the family umbrella, because several export a `ValidationStatistics` type | [FingerTree README](../../src/Haskell/FingerTree/README.md), [Hamt README](../../src/Haskell/Hamt/README.md), [workspace README](../../src/Haskell/README.md) |

The Rust port makes three intentional, documented divergences, all recorded in the local API notes:
it consolidates C#'s two duplicate level-ancestor arenas into one shared backend; it replaces the
presence-safe `DeltaMapValue<T>` wrapper with `Option`, which C# needs only because `null` is a
valid present value; and it replaces the run-delta vector's private reference-identity `Cell` class
with `Arc<T>` plus `Arc::ptr_eq`. Value equality is taken from a retained `EqualityPolicy<T>`, the
equality counterpart of `OrderPolicy<T>`, because that policy defines semantic no-ops and change
cancellation and so must be remembered rather than taken as a `PartialEq` bound.

The C port follows the workspace's type-erased conventions — values addressed by size plus a
caller-owned type-identity tag, policies carrying copy/destroy/compare callbacks and an allocator,
`ft_status` returns whose outputs are published only on success, and borrow-versus-own accessor
pairs where the managed ports return a single value. It starts from the consolidated arena seam
rather than reproducing the duplication C# originally shipped, so all three languages agree there.
Its one substantive divergence is concurrency: C# and Rust serialize the arena behind a lock, while
C11 has no portable mutex without `<threads.h>`, so the C arena documents the weaker contract —
single-threaded unless the caller synchronizes, with every operation including reads treated as a
write. Handle reference counts remain atomic, and no port claims lock-free progress.

The Haskell port diverges furthest, and in the direction of removing machinery rather than adding
it. The level-ancestor arena disappears entirely: a node is its own handle, so there is no arena
object, no lock, no backend-injection seam, and no arena statistics, and leaf addition improves from
O(1) amortized to O(1) worst case. The retained query counters that removal costs are recovered by
returning a query's hop count to the caller that caused it, which is what keeps the deque's
at-most-two-query ceiling testable. Comparer and action interfaces become retained records of
functions, so operand compatibility for concatenation and melding is a documented caller obligation
rather than a detected error, with one exception: the contextual sequence still rejects a declared
state-count mismatch, because that alone would silently corrupt summaries. Versions of the
connection forest have structural rather than referential identity. Two honest cost statements
follow the substrate rather than the baseline: the delta map's `minEntry`/`maxEntry` are Θ(log N)
because the Haskell sorted map caches no extremes, and the connection forest's CHAMP path factor is
expected rather than worst-case because the hash is truncated to 32 bits. Against that, the
contextual rank sequence keeps the reference's endpoint and concatenation bounds exactly, which the
Rust port cannot, because the Haskell substrate is a genuine finger tree with digits.

## Persistent Cursor Availability

All nine language ports ship the repository-wide semantic cursor tier. Concrete names follow each
language and owning family, so this table groups the surfaces by contract rather than repeating
every generic spelling. Use the
[repository-wide cursor design](../proposals/repository-wide-persistent-cursor-design.md) for the
exhaustive applicability matrix, API/result rules, complexity boundary, implementation ledger, and
validation design; use the [shared cursor contract](semantic-contracts.md#persistent-collection-cursors)
for the concise normative obligations.

| Family group | Availability | Semantic axis |
| --- | --- | --- |
| Patricia integer maps/sets | C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, Python | Ascending signed 32/64-bit key/value order and rank gaps. |
| Measured sequence, deque, reversible deque, RRB, Range, rope/text | All nine ports | Position or ordered measure boundary; text units remain language-local. |
| Sorted bag/set/map, canonical set, priority-search queue, interval tree/map, chunked bit set | All nine ports | Comparer order, interval order, key order, or population-rank gaps as owned by the family. |
| Neutral Ordered set/map/multimap | All nine ports | Explicit position; multimap additionally exposes nested group/value positions. |
| Merkle search tree | All nine ports | Specialized policy-comparer order and authenticated persistent edits. |
| CHAMP and derived unordered hash facades | No public cursor | Private lookup/edit path only; hash enumeration is not semantic order. |
| Ctrie, graph, heaps without stable order, builders/sessions/support artifacts, DABA Lite | No persistent cursor | No stable semantic neighbor axis or not a persistent aggregate. |

Most newly shipped cursors retain a canonical root plus a validated gap/rank and delegate edits to
ordinary persistent operations. That is a semantic checkpoint, not a claim of C# rope's focused
representation, memoization, allocation/callback ceilings, or local-edit amortization.

## Navigation Rules

- Start with this catalog when comparing data-structure availability across languages.
- Use the [workspace map](workspace-map.md) when choosing the correct language/data-structure directory.
- Use the [semantic contracts reference](semantic-contracts.md) when checking shared persistence,
  ownership, policy, ordering, and failure-behavior obligations.
- Use the implemented [repository-wide persistent cursor design](../proposals/repository-wide-persistent-cursor-design.md)
  when assessing navigation/editing across families or deciding whether a new family has a stable
  cursor axis. Its applicability exclusions are normative, and its focused-representation tier
  remains a separate evidence-gated optimization.
- Use the [porting and semantic parity guide](../guides/porting-and-semantic-parity.md) when changing behavior that may cross language workspaces.
- Use workspace API specs and public headers for normative contracts.
- Keep new public data structures visible here when they become part of a long-lived workspace surface.
