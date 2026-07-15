# Data Structure Catalog

- Created (UTC): 2026-07-02T19:53:11Z
- Repository HEAD: 1d90612aed11f273521046015c9d63bb7c993bba
- Audience: Maintainers and AI agents comparing data-structure and numerics surfaces across languages
- Scope: Repository-owned data-structure families, adjacent numerics libraries, public entry points, and primary reference links

This catalog is the cross-workspace orientation layer. It answers "which public library surfaces exist in
which language, and where do I start?" The workspace API specifications and headers remain the
authoritative source for contracts, complexity, allocation behavior, and validation details.

Use this together with the [workspace map](workspace-map.md): the map explains the language-first
layout, while this catalog maps each public library family across that layout. Use the
[semantic contracts reference](semantic-contracts.md) when reviewing behavior that should remain
recognizable across language ports.

## Fixed-Width Integer Numerics

`Tools.Numerics` is the C# reference workspace for fixed-width and sparse integer values; TypeScript
and Python port the same semantic family over native arbitrary-precision integer substrates. The family provides
deterministic two's-complement arithmetic, parse/format behavior, binary conversion APIs, and declaration-parity
guardrails for the wide-integer family.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `UInt256`, `Int256`, `UInt512`, `Int512`, `UInt1024`, `Int1024`, `SparseInteger`, `BitConverterEx` | [Workspace](../../src/CSharp/docs/Numerics/overview.md), [API and behavior reference](../../src/CSharp/docs/Numerics/api-and-behavior-reference.md), [validation](../../src/CSharp/docs/Numerics/validation.md), [wide-integer guidance](../../src/CSharp/docs/Numerics/wide-integer-maintainer-guidance.md), [tests](../../src/CSharp/tests/Tools.Numerics.Tests/README.md) |
| TypeScript | `UInt256`, `Int256`, `UInt512`, `Int512`, `UInt1024`, `Int1024`, `SparseInteger`, `BitConverterEx` | [Workspace](../../src/TypeScript/README.md), [API notes](../../src/TypeScript/docs/api-notes.md), [tests](../../src/TypeScript/test/README.md) |
| Python | `UInt256`, `Int256`, `UInt512`, `Int512`, `UInt1024`, `Int1024`, `FixedWidthInteger`, `SparseInteger`, `BitConverterEx` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/numerics), [tests](../../src/Python/tests/README.md) |

## HAMT Map And Set

The HAMT workspaces implement persistent hash-array mapped trie maps and sets with 32-way
bitmap-indexed branching, immutable equal-hash collision buckets, structural sharing between
versions, and comparer/hash-policy preservation. All eight languages expose one-way CHAMP map/set
editing sessions with O(1)-in-trie adoption and publication. C# implements the optimized owner-token
kernel, including in-place edits of token-owned nodes. C, C++, Haskell, Kotlin, Rust, TypeScript, and
Python preserve the same observable lifecycle through semantic facades whose changed point edits
remain ordinary persistent path copies; those sibling sessions make no edit-performance claim. The
seven established ports align same-policy equality/diff through canonical logical slots and prune
shared descendants; Python exposes the same results and exact-root short circuit through
lookup-based traversal rather than claiming that structural optimization. Independently created
hash policies retain semantic fallback or explicit rejection according to the local contract. All
eight languages expose persistent map `GetOrAdd`/`AddOrUpdate` counterparts that hash once, descend
once, and invoke exactly one selected factory without a retry loop. All eight also ship persistent
hash bags with positive 32-bit per-class multiplicities, widened expanded counts, receiver-policy
multiset algebra, and stored-representative recovery, plus strict persistent bidirectional maps over
independent key/value policies. C# uses a checked `long` bag total, TypeScript uses `bigint`, and
Python uses `int`. C++ and Rust expose construction-only
bulk builders over unpublished mutable CHAMP nodes; TypeScript and Python now port that reusable,
detached-freeze surface as well. All eight languages expose explicit-width Patricia
maps/sets; C# and Kotlin/JVM intentionally own the managed-only
Ctrie, whose snapshots enumerate in canonical CHAMP order for exact sequence-preserving conversion;
TypeScript supplies the synchronous isolate-local snapshot facade without a cross-worker progress
claim, and Python supplies a thread-safe, lock-coordinated facade over persistent roots. All eight
languages own complete wire-compatible policy-bound Merkle search trees.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentHashMap<TKey, TValue>` with persistent `GetOrAdd`/`AddOrUpdate`, `PersistentHashMap<TKey, TValue>.Transient`, `PersistentHashSet<T>`, `PersistentHashSet<T>.Transient`, `PersistentHashBag<T>`, `PersistentBiMap<TKey, TValue>`, `ConcurrentHashTrie<TKey, TValue>`, `PersistentIntMap<TValue>`, `PersistentIntSet`, `PersistentLongMap<TValue>`, `PersistentLongSet`, `MerkleSearchTree<TKey, TValue>` | [Workspace](../../src/CSharp/docs/Hamt/overview.md), [usage guide](../../src/CSharp/docs/Hamt/usage.md), [API spec](../../src/CSharp/docs/Hamt/api-specification.md), [T2 shipment decision](../../src/CSharp/docs/Hamt/transient-t2-decision.md), [CHAMP map](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashMap.cs), [single-pass persistent updates](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashMap.SinglePassUpdates.cs), [hash bag](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashBag.cs), [bimap](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentBiMap.cs), [map transient](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashMap.Transient.cs), [set transient](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentHashSet.Transient.cs), [concurrent trie](../../src/CSharp/src/Tools.DataStructures.Hamt/ConcurrentHashTrie.cs), [int map](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentIntMap.cs), [Merkle search tree](../../src/CSharp/src/Tools.DataStructures.Hamt/MerkleSearchTree.cs), [canonical codecs](../../src/CSharp/src/Tools.DataStructures.Hamt/MerkleEncoding.cs), [persistence vocabulary](../../src/CSharp/src/Tools.DataStructures.Hamt/MerklePersistence.cs), [verification, proofs, sync, and merge](../../src/CSharp/src/Tools.DataStructures.Hamt/MerkleSearchTree.PersistenceAlgorithms.cs) |
| C | `tds_hamt_map`, `tds_hamt_map_transient`, `tds_hamt_set`, `tds_hamt_set_transient`, `tds_hamt_bag`, `tds_hamt_bi_map`, hash/set policies, `tds_int_map`, `tds_int_set`, `tds_long_map`, `tds_long_set`, `tds_merkle_search_tree`, `tds_merkle_block_store`, `tds_merkle_verification_budget`, `tds_merkle_proof`, and sync/merge handles | [Workspace](../../src/C/Hamt/README.md), [usage guide](../../src/C/Hamt/docs/usage.md), [API spec](../../src/C/Hamt/docs/api-specification.md), [bimap header](../../src/C/Hamt/include/Tools/DataStructures/Hamt/persistent_bi_map.h), [Merkle specification](../../src/C/Hamt/docs/merkle-search-tree.md), [CHAMP and transient header](../../src/C/Hamt/include/Tools/DataStructures/Hamt/hamt.h), [Patricia header](../../src/C/Hamt/include/Tools/DataStructures/Hamt/patricia.h), [Merkle header](../../src/C/Hamt/include/Tools/DataStructures/Hamt/merkle_search_tree.h) |
| C++ | `persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>`, its construction-only `bulk_builder`, and nested `transient`; `persistent_hash_set<T, Hash, KeyEqual>` and nested `transient`; `persistent_hash_bag`; `persistent_bi_map`; Patricia and Merkle families | [Workspace](../../src/Cpp/Hamt/README.md), [usage guide](../../src/Cpp/Hamt/docs/usage.md), [API spec](../../src/Cpp/Hamt/docs/api-specification.md), [bimap header](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/persistent_bi_map.hpp), [Merkle core](../../src/Cpp/Hamt/docs/merkle-search-tree.md), [persistence specification](../../src/Cpp/Hamt/docs/merkle-persistence.md), [aggregate header](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/hamt.hpp), [persistence](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/merkle_persistence.hpp), [proofs and merge](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/merkle_proofs.hpp) |
| Haskell | `HashMap k v`, `MapTransient k v`, `HashSet a`, `SetTransient a`, `HashBag a`, `BiMap k v`, `HashPolicy k`, `Hashable`, `IntMap32 v`, `IntMap64 v`, `IntSet32`, `IntSet64`, `MerkleSearchTree k v`, `MerkleBlockStore`, `MerkleProof`, `MerkleVerificationBudget` | [Workspace](../../src/Haskell/Hamt/README.md), [map source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/HashMap.hs), [bimap source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/BiMap.hs), [transient source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/Transient.hs), [Patricia source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/Patricia.hs), [Merkle guide](../../src/Haskell/Hamt/docs/merkle-search-tree.md), [encoding](../../src/Haskell/Hamt/src/Data/Structures/Hamt/MerkleEncoding.hs), [Merkle core](../../src/Haskell/Hamt/src/Data/Structures/Hamt/MerkleSearchTree.hs), [persistence, proofs, sync, and merge](../../src/Haskell/Hamt/src/Data/Structures/Hamt/MerklePersistence.hs), [tests](../../src/Haskell/Hamt/test/README.md) |
| Kotlin | `PersistentHashMap<K, V>` and nested `Transient<K, V>`, `PersistentHashSet<T>` and nested `Transient<T>`, `PersistentHashBag<T>`, `PersistentBiMap<K, V>`, `HashPolicy<K>`, `ConcurrentHashTrie<K, V>`, `PersistentIntMap<V>`, `PersistentIntSet`, `PersistentLongMap<V>`, `PersistentLongSet`, `MerkleSearchTree<K, V>`, `MerkleBlockStore`, `MerkleProof`, `MerkleVerificationBudget` | [Workspace](../../src/Kotlin/Hamt/README.md), [API notes](../../src/Kotlin/Hamt/docs/api-notes.md), [bimap source](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/PersistentBiMap.kt), [Merkle guide](../../src/Kotlin/Hamt/docs/merkle-search-tree.md), [validation](../../src/Kotlin/Hamt/docs/validation.md), [CHAMP and transient source](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/PersistentHamt.kt), [Ctrie source](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/ConcurrentHashTrie.kt), [Patricia source](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/PersistentPatricia.kt), [Merkle encoding](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/MerkleEncoding.kt), [Merkle core](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/MerkleSearchTree.kt), [persistence vocabulary](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/MerklePersistence.kt), [verification, proofs, sync, and merge](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/MerkleSearchTreePersistence.kt), [tests](../../src/Kotlin/Hamt/tests/README.md) |
| Rust | `PersistentHashMap<K, V, S>`, `BulkBuilder<K, V, S>`, `TransientHashMap<K, V, S>`, `PersistentHashSet<T, S>`, `TransientHashSet<T, S>`, `PersistentHashBag<T, S>`, `PersistentBiMap<K, V, SK, SV>`, Patricia and Merkle families | [Workspace](../../src/Rust/Hamt/README.md), [API notes](../../src/Rust/Hamt/docs/api-notes.md), [CHAMP, builder, and transient source](../../src/Rust/Hamt/src/lib.rs), [bimap source](../../src/Rust/Hamt/src/bi_map.rs), [Merkle search tree](../../src/Rust/Hamt/docs/merkle-search-tree.md), [validation](../../src/Rust/Hamt/docs/validation.md), [Merkle core](../../src/Rust/Hamt/src/merkle_search_tree.rs), [encoding](../../src/Rust/Hamt/src/merkle_encoding.rs), [persistence, proofs, sync, and merge](../../src/Rust/Hamt/src/merkle_persistence.rs) |
| TypeScript | `PersistentHashMap<K, V>` with `getOrAdd`/`addOrUpdate`, `HashMapBulkBuilder<K, V>`, `TransientHashMap<K, V>`, `PersistentHashSet<T>`, `TransientHashSet<T>`, `PersistentHashBag<T>`, `PersistentBiMap<K, V>`, `ConcurrentHashTrie<K, V>`, Patricia and complete Merkle families | [Workspace](../../src/TypeScript/README.md), [API notes](../../src/TypeScript/docs/api-notes.md), [HAMT source](../../src/TypeScript/src/hamt), [tests](../../src/TypeScript/test/README.md) |
| Python | `PersistentHashMap` with `get_or_add`/`add_or_update`, `HashMapBulkBuilder`, `TransientHashMap`, `PersistentHashSet`, `TransientHashSet`, `PersistentHashBag`, `PersistentBiMap`, `HashPolicy`, `ConcurrentHashTrie`, Patricia and complete Merkle families | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [HAMT source](../../src/Python/src/vladimir_reshetnikov/data_structures/hamt), [tests](../../src/Python/tests/README.md) |

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
| C | `tds_hamt_bi_map` | [API specification](../../src/C/Hamt/docs/api-specification.md#persistent-bidirectional-map-contract), [usage](../../src/C/Hamt/docs/usage.md#persistent-bidirectional-map), [validation](../../src/C/Hamt/docs/validation.md#persistent-bidirectional-map-shipment-evidence) |
| C++ | `persistent_bi_map<Key, T, KeyHash, KeyEqual, ValueHash, ValueEqual>` | [API specification](../../src/Cpp/Hamt/docs/api-specification.md), [usage](../../src/Cpp/Hamt/docs/usage.md#persistent-bidirectional-map), [validation](../../src/Cpp/Hamt/docs/validation.md) |
| Haskell | `Data.Structures.Hamt.BiMap.BiMap k v` | [Workspace contract](../../src/Haskell/Hamt/README.md#persistent-bidirectional-map), [tests](../../src/Haskell/Hamt/test/README.md) |
| Kotlin | `PersistentBiMap<K, V>` | [API notes](../../src/Kotlin/Hamt/docs/api-notes.md#persistent-bidirectional-map), [validation](../../src/Kotlin/Hamt/docs/validation.md) |
| Rust | `PersistentBiMap<K, V, SK, SV>` | [API notes](../../src/Rust/Hamt/docs/api-notes.md#persistent-bidirectional-map), [validation](../../src/Rust/Hamt/docs/validation.md) |
| TypeScript | `PersistentBiMap<K, V>` | [API notes](../../src/TypeScript/docs/api-notes.md#hamt-maps-bags-bimaps-builders-and-sessions), [validation](../../src/TypeScript/docs/validation.md) |
| Python | `PersistentBiMap[K, V]` | [API notes](../../src/Python/docs/api-notes.md), [validation](../../src/Python/docs/validation.md) |

## Finger-Tree Core And Deque

The finger-tree workspaces provide persistent sequence engines: a tuned catenable deque and a
general monoid-measured tree. They support endpoint operations, concatenation, splitting by
position or measure, indexed access where exposed, and immutable structural sharing.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `FingerTreeDeque<T>`, `FingerTree<TElement, TMeasure, TMeasureOps>`, `RrbVector<T>`, `RrbVector<T>.Builder` | [Workspace](../../src/CSharp/docs/FingerTree/overview.md), [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md), [deque source](../../src/CSharp/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs), [measured tree source](../../src/CSharp/src/Tools.DataStructures.FingerTree/FingerTree.cs), [RRB vector and builder](../../src/CSharp/src/Tools.DataStructures.FingerTree/RrbVector.cs) |
| C | `ft_tree`, `ft_tree_policy`, `ft_measure_policy`, `ft_persistent_deque`, `ft_rrb_vector`, `ft_rrb_builder` | [Workspace](../../src/C/FingerTree/README.md), [usage guide](../../src/C/FingerTree/docs/usage.md), [API notes](../../src/C/FingerTree/docs/api-notes.md), [finger-tree header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [RRB header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/rrb_vector.h) |
| C++ | `persistent_deque<T>`, `finger_tree<Element, MeasurePolicy>`, `rrb_vector<T>`, `rrb_vector_builder<T>` | [Workspace](../../src/Cpp/FingerTree/README.md), [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [aggregate header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/finger_tree.hpp), [deque header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/persistent_deque.hpp), [measured tree header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_finger_tree.hpp), [RRB vector header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rrb_vector.hpp) |
| Haskell | `Deque a`, `FingerTree v a`, `Measured v a`, `RrbVector a` | [Workspace](../../src/Haskell/FingerTree/README.md), [deque source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Deque.hs), [measured tree source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measured.hs), [RRB source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/RrbVector.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `PersistentDeque<T>`, `FingerTree<T, M>`, `MeasurePolicy<T, M>`, `RrbVector<T>`, `RrbVector.Builder<T>` | [Workspace](../../src/Kotlin/FingerTree/README.md), [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [public facades](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Core.kt), [measured AVL engine](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/PersistentMeasuredTree.kt), [RRB source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/RrbVector.kt), [tests](../../src/Kotlin/FingerTree/tests/README.md) |
| Rust | `PersistentDeque<T>`, `FingerTree<T, P>`, `MeasurePolicy<T>`, `RrbVector<T>`, `RrbVectorBuilder<T>` | [Workspace](../../src/Rust/FingerTree/README.md), [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [deque source](../../src/Rust/FingerTree/src/deque.rs), [measured source](../../src/Rust/FingerTree/src/measured.rs), [RRB source](../../src/Rust/FingerTree/src/rrb_vector.rs) |
| TypeScript | `PersistentDeque<T>`, `FingerTree<T, M>`, `MeasurePolicy<T, M>`, `RrbVector<T>`, `RrbVectorBuilder<T>` | [Workspace](../../src/TypeScript/README.md), [core](../../src/TypeScript/src/finger-tree/core.ts), [RRB vector](../../src/TypeScript/src/finger-tree/rrb-vector.ts) |
| Python | `PersistentDeque`, `FingerTree`, `MeasuredSequence`, `MeasurePolicy`, `RrbVector`, `RrbVectorBuilder` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [measured AVL/core](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/measured_sequence.py), [RRB vector](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/rrb_vector.py), [tests](../../src/Python/tests/README.md) |

## Range-Update Sequence

All eight languages ship a language-local `RangeUpdateSequence` as an immutable indexed sequence
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
| C# | `IRangeUpdateAlgebra<TElement, TMeasure, TTag>`, `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` | [Contract, algebra, and invariants](../../src/CSharp/docs/FingerTree/range-update-sequence.md), [API specification](../../src/CSharp/docs/FingerTree/api-specification.md#range-update-sequence), [source](../../src/CSharp/src/Tools.DataStructures.FingerTree/RangeUpdateSequence.cs), [algebra source](../../src/CSharp/src/Tools.DataStructures.FingerTree/IRangeUpdateAlgebra.cs), [validation](../../src/CSharp/docs/FingerTree/validation.md#range-update-sequence-integration-gate), [tests](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md) |
| C | `ft_range_update_policy`, `ft_range_update_sequence` | [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/range_update_sequence.h), [implementation](../../src/C/FingerTree/src/range_update_sequence.c), [tests](../../src/C/FingerTree/tests/range_update_sequence_tests.c) |
| C++ | `range_update_sequence<Element, Algebra>` | [header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/range_update_sequence.hpp), [tests](../../src/Cpp/FingerTree/tests/range_update_sequence_tests.cpp), [validation](../../src/Cpp/FingerTree/docs/validation.md) |
| Haskell | `RangeUpdateAlgebra`, `RangeUpdateSequence` | [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/RangeUpdateSequence.hs), [tests](../../src/Haskell/FingerTree/test/RangeUpdateSequenceTests.hs) |
| Kotlin | `RangeUpdateAlgebra<T, M, Tag>`, `RangeUpdateSequence<T, M, Tag>` | [source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/RangeUpdateSequence.kt), [contract](../../src/Kotlin/FingerTree/docs/range-update-sequence.md), [tests](../../src/Kotlin/FingerTree/test/tools/datastructures/fingertree/RangeUpdateSequenceTests.kt) |
| Rust | `RangeUpdateAlgebra`, `RangeUpdateSequence` | [crate](../../src/Rust/RangeUpdate/README.md), [source](../../src/Rust/RangeUpdate/src/lib.rs), [tests](../../src/Rust/RangeUpdate/tests/range_update_sequence.rs) |
| TypeScript | `RangeUpdateAlgebra<T, M, Tag>`, `RangeUpdateSequence<T, M, Tag>` | [contract](../../src/TypeScript/docs/range-update-sequence.md), [source](../../src/TypeScript/src/finger-tree/range-update-sequence.ts), [tests](../../src/TypeScript/test/finger-tree/range-update-sequence.test.ts) |
| Python | `RangeUpdateAlgebra`, `RangeUpdateSequence` | [contract](../../src/Python/docs/range-update-sequence.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/range_update_sequence.py), [tests](../../src/Python/tests/finger_tree/test_range_update_sequence.py) |

At the pre-bimap Range shipment checkpoint, both full serialized C# Debug and Release solution
builds completed with zero warnings and zero errors, and both configurations passed 1,417/1,417
tests. No benchmark was run; measurement remains
postponed until an isolated session. The single-pass HAMT updates, hash bag, ordered set, and
range-update sequence now ship across all eight languages; the detailed evidence is in the
[cross-language completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md).

## Reversible Deque

Reversible deques add orientation-aware views over persistent deque storage so reversal can be
represented without eagerly copying the sequence.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `ReversibleDeque<T>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [source](../../src/CSharp/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_reversible_deque` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `reversible_deque<T>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/reversible_deque.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `ReversibleDeque a` | [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/ReversibleDeque.hs), [tests](../../src/Haskell/FingerTree/test/README.md), [complexity audit](reversible-deque-complexity-audit.md) |
| Kotlin | `ReversibleDeque<T>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Core.kt), [complexity audit](reversible-deque-complexity-audit.md) |
| Rust | `ReversibleDeque<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/deque.rs) |
| TypeScript | `ReversibleDeque<T>` | [API notes](../../src/TypeScript/docs/api-notes.md), [source](../../src/TypeScript/src/finger-tree/core.ts) |
| Python | `ReversibleDeque` | [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/core.py), [tests](../../src/Python/tests/README.md) |

## Sorted Collections

Sorted collections expose immutable sorted bags/multisets, sets, and key-value maps with
comparer-preserving behavior. The mature C#/C++/C ports use order-statistic measures over finger
trees; the Rust checkpoint now uses cached count plus last-key order-statistic measures over its
measured tree while its API notes track the remaining lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `SortedBag<T>`, `SortedSet<T>`, sorted builders, `SortedDictionary<TKey, TValue>`, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [bag](../../src/CSharp/src/Tools.DataStructures.FingerTree/SortedBag.cs), [set](../../src/CSharp/src/Tools.DataStructures.FingerTree/SortedSet.cs), [dictionary](../../src/CSharp/src/Tools.DataStructures.FingerTree/SortedDictionary.cs), [canonical set](../../src/CSharp/src/Tools.DataStructures.FingerTree/CanonicalSortedSet.cs), [rank policy](../../src/CSharp/src/Tools.DataStructures.FingerTree/ZipTreeRankPolicy.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_sorted_multiset`, `ft_sorted_set`, `ft_sorted_map`, `ft_canonical_sorted_set`, `ft_canonical_policy` | [usage guide](../../src/C/FingerTree/docs/usage.md), [API notes](../../src/C/FingerTree/docs/api-notes.md), [canonical set and policy header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/canonical_sorted_set.h), [core public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [tests](../../src/C/FingerTree/tests/README.md) |
| C++ | `sorted_bag<T, Less>`, `sorted_set<T, Less>`, `sorted_map<Key, T, Less>`, `canonical_sorted_set<T>`, `zip_tree_rank_policy<T>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [canonical set and policy](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/canonical_sorted_set.hpp), [bag header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_bag.hpp), [set header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_set.hpp), [map header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_map.hpp), [tests](../../src/Cpp/FingerTree/tests/README.md) |
| Haskell | `SortedBag a`, `SortedSet a`, `SortedMap k v`, `CanonicalSortedSet a`, `ZipTreeRankPolicy a` | [canonical-set guide](../../src/Haskell/FingerTree/docs/canonical-sorted-set.md), [canonical source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/CanonicalSortedSet.hs), [bag source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/SortedBag.hs), [set source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/SortedSet.hs), [map source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/SortedMap.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>`, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [measured sorted collections](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Sorted.kt), [canonical set and rank policy](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/CanonicalSortedSet.kt), [tests](../../src/Kotlin/FingerTree/tests/README.md) |
| Rust | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>`, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [measured sorted collections](../../src/Rust/FingerTree/src/sorted.rs), [canonical set and rank policy](../../src/Rust/FingerTree/src/canonical_sorted_set.rs), [tests](../../src/Rust/FingerTree/tests/README.md) |
| TypeScript | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>`, builders, `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>` | [sorted facades](../../src/TypeScript/src/finger-tree/sorted.ts), [canonical set](../../src/TypeScript/src/finger-tree/canonical-sorted-set.ts), [tests](../../src/TypeScript/test/README.md) |
| Python | `SortedBag`, `SortedSet`, `SortedMap`, `SortedSetBuilder`, `SortedMapBuilder`, `CanonicalSortedSet`, `ZipTreeRankPolicy` | [API notes](../../src/Python/docs/api-notes.md), [sorted facades](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/sorted.py), [canonical set](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/canonical_sorted_set.py), [tests](../../src/Python/tests/README.md) |

## Priority Queue

Priority queues locate and remove the front priority entry according to each language's comparison
policy. The mature C#/C++/C ports use measured finger-tree facades; the Rust checkpoint now uses
cached minimum-priority measures over its measured tree while its API notes track the remaining
lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PriorityQueue<TElement, TPriority>`, `BrodalOkasakiHeap<T>`, `PrioritySearchQueue<TKey, TPriority, TValue>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [measured priority queue](../../src/CSharp/src/Tools.DataStructures.FingerTree/PriorityQueue.cs), [Brodal–Okasaki heap](../../src/CSharp/src/Tools.DataStructures.FingerTree/BrodalOkasakiHeap.cs), [priority-search queue](../../src/CSharp/src/Tools.DataStructures.FingerTree/PrioritySearchQueue.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_priority_queue`, `ft_brodal_policy`, `ft_brodal_heap`, `ft_priority_search_queue`, `ft_priority_search_entry` | [usage guide](../../src/C/FingerTree/docs/usage.md), [measured priority-queue header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [Brodal-Okasaki header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/brodal_okasaki_heap.h), [priority-search header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/priority_search_queue.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `priority_queue<Element, Priority, Comparison>`, `brodal_okasaki_heap<T, Less>`, `brodal_okasaki_heap_statistics`, `priority_search_queue<K, P, V, KeyLess, PriorityLess>`, `priority_search_entry<K, P, V>`, and typed add/remove/minimum results | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [measured priority-queue header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/priority_queue.hpp), [Brodal-Okasaki header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/brodal_okasaki_heap.hpp), [priority-search header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/priority_search_queue.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `PriorityQueue p a`, `BrodalOkasakiHeap a`, `PrioritySearchQueue k p v`, `PrioritySearchEntry k p v` | [measured priority queue](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/PriorityQueue.hs), [Brodal-Okasaki heap](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/BrodalOkasakiHeap.hs), [priority-search queue](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/PrioritySearchQueue.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `PriorityQueue<T, P>`, `PriorityEntry<T, P>`, `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [measured priority queue](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/PriorityAndInterval.kt), [Brodal-Okasaki heap](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/BrodalOkasakiHeap.kt), [priority-search queue](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/PrioritySearchQueue.kt), [priority-core notes](../../src/Kotlin/FingerTree/docs/priority-cores.md) |
| Rust | `PriorityQueue<T, P>`, `PriorityEntry<T, P>`, `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>`, `OrderPolicy<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [measured priority queue](../../src/Rust/FingerTree/src/priority_queue.rs), [Brodal-Okasaki heap](../../src/Rust/FingerTree/src/brodal_okasaki_heap.rs), [priority-search queue](../../src/Rust/FingerTree/src/priority_search_queue.rs), [ordering policy](../../src/Rust/FingerTree/src/ordering.rs), [Brodal notes](../../src/Rust/FingerTree/docs/brodal-okasaki-heap.md), [PSQ notes](../../src/Rust/FingerTree/docs/priority-search-queue.md) |
| TypeScript | `PriorityQueue<T, P>`, `BrodalOkasakiHeap<T>`, `PrioritySearchQueue<K, P, V>` and typed view/result values | [measured queue](../../src/TypeScript/src/finger-tree/priority-interval.ts), [Brodal heap](../../src/TypeScript/src/finger-tree/brodal-okasaki-heap.ts), [priority-search queue](../../src/TypeScript/src/finger-tree/priority-search-queue.ts) |
| Python | `PriorityQueue`, `PriorityEntry`, `BrodalOkasakiHeap`, `BrodalMinimumView`, `PrioritySearchQueue`, `PrioritySearchEntry`, and typed result/statistics values | [API notes](../../src/Python/docs/api-notes.md), [measured queue](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/priority_interval.py), [Brodal heap](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/brodal_okasaki_heap.py), [priority-search queue](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/priority_search_queue.py), [tests](../../src/Python/tests/README.md) |

## Interval Tree

Interval trees store ordered interval collections for overlap and containment queries. The mature
C#/C++/C ports use interval annotations in finger-tree measures so queries can skip subtrees whose
summary cannot intersect the probe; the Rust checkpoint now uses cached maximum-high measures over
its measured tree while its API notes track the remaining lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IntervalTree<T>`, `Interval<T>`, `IntervalMeasure<T>` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [source](../../src/CSharp/src/Tools.DataStructures.FingerTree/IntervalTree.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md) |
| C | `ft_interval_tree`, `ft_interval_tree_i64`, `ft_interval_i64` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `interval_tree<T, Comparison>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/interval_tree.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `IntervalTree a`, `Interval a` | [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/IntervalTree.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `IntervalTree<T>`, `Interval<T>` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/PriorityAndInterval.kt) |
| Rust | `IntervalTree<T>`, `Interval<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/interval_tree.rs) |
| TypeScript | `IntervalTree<T>`, `Interval<T>` | [source](../../src/TypeScript/src/finger-tree/priority-interval.ts), [tests](../../src/TypeScript/test/finger-tree/core.test.ts) |
| Python | `IntervalTree`, `Interval` | [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/priority_interval.py), [tests](../../src/Python/tests/README.md) |

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
claiming the C# zipper or its focus-local complexity. C text positions count byte-sized `char` elements and use
the existing LF-only zero-based line/column rules.
The Rust checkpoint uses chunked measured storage for both positional `Rope<T>` and custom-measured
`MeasuredRope<T, P>` and stores `TextRope` content in a newline-measured rope while its API notes track
the remaining lazy-spine parity boundary.
Python uses the same snapshot-plus-gap semantics over its persistent measured-AVL checkpoint;
`TextRope` and `TextRopeCursor` count Python Unicode code points rather than UTF-16 units or
grapheme clusters.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `Rope<T>`, `RopeCursor<T>`, `Rope<T>.Builder`, `MeasuredRope<T, TMeasure, TMeasureOps>`, `MeasuredRopeCursor<T, TMeasure, TMeasureOps>`, `MeasuredRope<T, TMeasure, TMeasureOps>.Builder`, `RopeText`, `RopeBuilder`, `NewlineMeasure`, `NewlineStyle` | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [rope](../../src/CSharp/src/Tools.DataStructures.FingerTree/Rope.cs), [positional cursor](../../src/CSharp/src/Tools.DataStructures.FingerTree/Rope.Cursor.cs), [cursor C0 decision and complexity scope](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md), [rope builder](../../src/CSharp/src/Tools.DataStructures.FingerTree/Rope.Builder.cs), [measured rope](../../src/CSharp/src/Tools.DataStructures.FingerTree/MeasuredRope.cs), [measured cursor](../../src/CSharp/src/Tools.DataStructures.FingerTree/MeasuredRope.Cursor.cs), [cursor C2 decision and evidence](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md), [measured rope builder](../../src/CSharp/src/Tools.DataStructures.FingerTree/MeasuredRope.Builder.cs), [text](../../src/CSharp/src/Tools.DataStructures.FingerTree/RopeText.cs), [text builder](../../src/CSharp/src/Tools.DataStructures.FingerTree/RopeBuilder.cs), [API spec](../../src/CSharp/docs/FingerTree/api-specification.md), [validation](../../src/CSharp/docs/FingerTree/validation.md) |
| C | `ft_rope`, `ft_rope_cursor`, `ft_measured_rope`, `ft_measured_rope_cursor`, `ft_text_rope`, `ft_text_rope_cursor`, `ft_line_column` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `rope<T>`, `rope_cursor<T>`, `measured_rope<T, MeasurePolicy>`, `measured_rope_cursor<T, MeasurePolicy>`, `measured_rope_cursor_search_result<T, MeasurePolicy>`, `text_rope`, `text_rope_cursor`, `text_rope_cursor_search_result`, `rope_builder`, `newline_measure`, `line_column` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [rope header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope.hpp), [measured rope/cursor header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_rope.hpp), [text header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope_text.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `Rope a`, `RopeCursor a`, `MeasuredRope v a`, `MeasuredRopeCursor v a`, `MeasuredRopeCursorSearch v a`, `TextRope`, `TextRopeCursor`, `TextRopeCursorSearch`, `NewlineMeasure` | [workspace and cursor contract](../../src/Haskell/FingerTree/README.md), [rope/cursor source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Rope.hs), [measured rope/cursor source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/MeasuredRope.hs), [text cursor source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Rope/Text.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `Rope<T>`, `RopeCursor<T>`, `RopeCursorPeek<T>`, `MeasuredRope<T, M>`, `MeasuredRopeCursor<T, M>`, `MeasuredRopeCursorSearch<T, M>`, `TextRope`, `TextRopeCursor`, `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [rope source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Rope.kt), [measured cursor](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/MeasuredRopeCursor.kt), [text cursor](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/TextRopeCursor.kt) |
| Rust | `Rope<T>`, `RopeCursor<T>`, `MeasuredRope<T, P>`, `MeasuredRopeCursor<T, P>`, `MeasuredRopeCursorSearch<T, P>`, `TextRope`, `TextRopeCursor`, `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/rope.rs) |
| TypeScript | `Rope<T>`, `RopeCursor<T>`, `RopeCursorPeek<T>`, `MeasuredRope<T, M>`, `MeasuredRopeCursor<T, M>`, `TextRope`, `TextRopeCursor`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/TypeScript/docs/api-notes.md), [source](../../src/TypeScript/src/finger-tree/rope.ts), [tests](../../src/TypeScript/test/finger-tree/rope-daba.test.ts) |
| Python | `Rope`, `RopeCursor`, `RopeCursorPeek`, `MeasuredRope`, `MeasuredRopeCursor`, `TextRope`, `TextRopeCursor`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/rope.py), [tests](../../src/Python/tests/README.md) |

## Measures, Comparisons, And Predicates

Measures are the connective tissue for finger-tree-derived collections. The C#, C++, Kotlin, and Python
workspaces expose typed measure abstractions (with comparison and predicate abstractions where their
surfaces require them); the C workspace exposes equivalent policy callbacks and context pointers.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IMonoid<TMeasure>`, `IMeasure<TElement, TMeasure>`, `IMeasurePredicate<TMeasure>`, `IComparison<T>`, `DabaLite<T, TMonoid>`, built-in and product measures | [usage guide](../../src/CSharp/docs/FingerTree/usage.md), [measures](../../src/CSharp/src/Tools.DataStructures.FingerTree/Measures.cs), [DABA Lite](../../src/CSharp/src/Tools.DataStructures.FingerTree/DabaLite.cs), [predicates](../../src/CSharp/src/Tools.DataStructures.FingerTree/MeasurePredicate.cs), [comparisons](../../src/CSharp/src/Tools.DataStructures.FingerTree/Comparisons.cs), [built-ins](../../src/CSharp/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs) |
| C | `ft_daba_policy`, `ft_daba_lite`, `ft_daba_lite_statistics`, `ft_measure_policy`, `ft_measure_predicate_fn`, `ft_compare_fn`, `ft_value_type` | [usage guide](../../src/C/FingerTree/docs/usage.md), [DABA Lite header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/daba_lite.h), [core public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `daba_lite_value<T>`, `daba_lite_monoid_policy<P, T>`, `daba_lite<T, P>`, `daba_lite_statistics`, plus measure policies and predicates in `measures.hpp`, `built_in_measures.hpp`, `product_measure.hpp`, `sum_measure.hpp`, `comparisons.hpp`, and `measure_predicates.hpp` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [DABA Lite](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/daba_lite.hpp), [measure headers](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measures.hpp), [built-ins](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/built_in_measures.hpp), [predicates](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measure_predicates.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `Measured v a`, `Size`, `Elem`, `MeasurePair`, `Maximum`, `Minimum`; predicates are ordinary pure functions `v -> Bool` | [measured tree source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measured.hs), [measures source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measures.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Kotlin | `Monoid<T>`, `MeasurePolicy<T, M>`, `DabaLite<T>`, `DabaLiteStatistics`, `SizeMeasure<T>`, `IntSumMeasure`, `MaxMeasure<T>`, `MinMeasure<T>`, `ProductMeasure<T, A, B>`, `MeasurePair<A, B>`, `NewlineMeasure` | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [monoids and measures](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Core.kt), [DABA Lite](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/DabaLite.kt), [newline measure](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Rope.kt) |
| Rust | `DabaMonoid<T>`, `DabaLite<T, M>`, `DabaLiteStatistics`, `MeasurePolicy<T>`, `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, `KeyMeasure<T>`, `ProductMeasure<T, PFirst, PSecond>`, `MeasurePair<TFirst, TSecond>`, `SizeAndSumMeasure<T>`, `SizeAndMaxMeasure<T>`, `SizeAndMinMeasure<T>`, `OrderStatisticMeasure<T>`, `RankedKey<T>`, `NewlineMeasure` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [DABA Lite](../../src/Rust/FingerTree/src/daba_lite.rs), [measures](../../src/Rust/FingerTree/src/measured.rs), [newline measure](../../src/Rust/FingerTree/src/rope.rs) |
| TypeScript | `Monoid<M>`, `MeasurePolicy<T, M>`, `DabaLite<T, M>`, size/sum/min/max/product measures, comparators, and `NewlineMeasure` | [measures](../../src/TypeScript/src/finger-tree/measures.ts), [DABA Lite](../../src/TypeScript/src/finger-tree/daba-lite.ts), [API notes](../../src/TypeScript/docs/api-notes.md) |
| Python | `Monoid`, `MeasurePolicy`, `DabaLite`, `DabaLiteStatistics`, size/sum/min/max/product measures, comparators, and `NewlineMeasure` | [API notes](../../src/Python/docs/api-notes.md), [measures](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/measures.py), [DABA Lite](../../src/Python/src/vladimir_reshetnikov/data_structures/finger_tree/daba_lite.py), [tests](../../src/Python/tests/README.md) |

## Insertion-Ordered Persistent Set

The neutral Ordered family composes hashed membership with a persistent ordered sequence without
depending on Tungsten. Equality policy determines membership and first representatives; enumeration
preserves insertion or explicitly requested order. Duplicate additions never move or replace a
representative. Explicit movement, positional ranges, reversal, stable one-shot sorting,
receiver-policy set algebra, and all six set relations preserve immutable versions and documented
identity no-ops. Algebra eagerly normalizes the complete argument under the receiver policy before
applying shortcuts, retaining receiver representatives and first normalized argument representatives.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentOrderedSet<T>` | [Workspace](../../src/CSharp/docs/Ordered/overview.md), [usage guide](../../src/CSharp/docs/Ordered/usage.md), [API specification](../../src/CSharp/docs/Ordered/api-specification.md), [source](../../src/CSharp/src/Tools.DataStructures.Ordered), [validation](../../src/CSharp/docs/Ordered/validation.md), [tests](../../src/CSharp/tests/Tools.DataStructures.Ordered.Tests/README.md) |
| C | `tds_ordered_set` | [workspace](../../src/C/Ordered/README.md), [public header](../../src/C/Ordered/include/tools/data_structures/ordered/ordered_set.h), [tests](../../src/C/Ordered/tests/ordered_set_tests.c) |
| C++ | `persistent_ordered_set<T, Hash, KeyEqual>` | [workspace](../../src/Cpp/Ordered/README.md), [header](../../src/Cpp/Ordered/include/tools/data_structures/ordered/persistent_ordered_set.hpp), [tests](../../src/Cpp/Ordered/tests/persistent_ordered_set_tests.cpp) |
| Haskell | `PersistentOrderedSet` | [workspace](../../src/Haskell/Ordered/README.md), [source](../../src/Haskell/Ordered/src/Data/Structures/Ordered/PersistentOrderedSet.hs) |
| Kotlin | `PersistentOrderedSet<T>` | [workspace](../../src/Kotlin/Ordered/README.md), [source](../../src/Kotlin/Ordered/src/tools/datastructures/ordered/PersistentOrderedSet.kt) |
| Rust | `PersistentOrderedSet<T, S>` | [workspace](../../src/Rust/Ordered/README.md), [source](../../src/Rust/Ordered/src/lib.rs), [tests](../../src/Rust/Ordered/tests/persistent_ordered_set.rs) |
| TypeScript | `PersistentOrderedSet<T>`, idiomatic lookup/removal result values | [Workspace](../../src/TypeScript/README.md), [API notes](../../src/TypeScript/docs/api-notes.md), [source](../../src/TypeScript/src/ordered), [tests](../../src/TypeScript/test/ordered) |
| Python | `PersistentOrderedSet`, `OrderedSetValueResult`, `OrderedSetRemoveResult` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/ordered), [tests](../../src/Python/tests/ordered) |

All eight ports ship. The C# focused single-worker Debug and Release lanes each pass 62 tests. At
the historical pre-Range Ordered shipment checkpoint, the full
serialized C# Release build had zero warnings and zero errors and the complete gate passed
1,355/1,355 tests; the later pre-bimap full-workspace evidence is the 1,417/1,417 Debug and Release
Range checkpoint recorded above. No benchmark was run for either shipment, and measurements remain postponed for an
isolated session.

## Tungsten Application Collections

The Tungsten-collections workspaces are application-specific leaf consumers for the Tungsten
project. They may change with newly discovered or reinterpreted Wolfram-kernel behavior and may
move out of this repository. No general collection may depend on these packages/types or use them
as a semantic baseline. A generally useful mechanism must be forked into an independently owned
implementation with its own API, contracts, tests, and evolution policy.

Within that application boundary, the workspaces compose the HAMT and FingerTree families into persistent
collections shaped for Tungsten Language `List` and `Association` semantics: an ordered-sequence
facade with the Tungsten `List` operation vocabulary, and an insertion-ordered map with keyed and
positional access following the kernel-verified `Association` ordering rules (in-place update,
move-on-`Append`/`Prepend`, first-position/last-value construction, positional slicing, stable
sorts). The primary external client is the Tungsten engine in the Smithereens repository; the C#
implementation is the semantic reference only for sibling Tungsten ports. The
[derived structure catalog](derived-structure-catalog.md) records useful historical composition
evidence, not permission to make a general structure depend on Tungsten.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentList<T>`, `PersistentAssociation<TKey, TValue>` | [Workspace](../../src/CSharp/docs/Tungsten/overview.md), [usage guide](../../src/CSharp/docs/Tungsten/usage.md), [API spec](../../src/CSharp/docs/Tungsten/api-specification.md), [list source](../../src/CSharp/src/Tools.DataStructures.Tungsten/PersistentList.cs), [association source](../../src/CSharp/src/Tools.DataStructures.Tungsten/PersistentAssociation.cs) |
| C | `tds_tungsten_list`, `tds_tungsten_association`, `tds_tungsten_association_policy` | [Workspace](../../src/C/Tungsten/README.md), [public header](../../src/C/Tungsten/include/tools/data_structures/tungsten/tungsten.h), [implementation](../../src/C/Tungsten/src/tungsten.c), [tests](../../src/C/Tungsten/tests/tungsten_c_tests.c) |
| C++ | `persistent_list<T>`, `persistent_association<Key, T, Hash, KeyEqual, ValueEqual>` | [Workspace](../../src/Cpp/Tungsten/README.md), [aggregate header](../../src/Cpp/Tungsten/include/tools/data_structures/tungsten/tungsten.hpp), [list header](../../src/Cpp/Tungsten/include/tools/data_structures/tungsten/persistent_list.hpp), [association header](../../src/Cpp/Tungsten/include/tools/data_structures/tungsten/persistent_association.hpp), [tests](../../src/Cpp/Tungsten/tests/tungsten_tests.cpp) |
| Haskell | `PersistentList a`, `PersistentAssociation k v` | [Workspace](../../src/Haskell/Tungsten/README.md), [list source](../../src/Haskell/Tungsten/src/Data/Structures/Tungsten/List.hs), [association source](../../src/Haskell/Tungsten/src/Data/Structures/Tungsten/Association.hs), [tests](../../src/Haskell/Tungsten/test/Main.hs) |
| Kotlin | `PersistentList<T>`, `PersistentAssociation<K, V>` | [Workspace](../../src/Kotlin/Tungsten/README.md), [source](../../src/Kotlin/Tungsten/src/tools/datastructures/tungsten/PersistentTungsten.kt), [tests](../../src/Kotlin/Tungsten/test/tools/datastructures/tungsten/TungstenTests.kt) |
| Rust | `PersistentList<T>`, `PersistentAssociation<K, V, S>` | [Workspace](../../src/Rust/Tungsten/README.md), [source](../../src/Rust/Tungsten/src/lib.rs) |
| TypeScript | `PersistentList<T>`, `PersistentAssociation<K, V>` | [Workspace](../../src/TypeScript/README.md), [list](../../src/TypeScript/src/tungsten/persistent-list.ts), [association](../../src/TypeScript/src/tungsten/persistent-association.ts), [tests](../../src/TypeScript/test/tungsten/tungsten.test.ts) |
| Python | `PersistentList`, `PersistentAssociation` | [Workspace](../../src/Python/README.md), [API notes](../../src/Python/docs/api-notes.md), [source](../../src/Python/src/vladimir_reshetnikov/data_structures/tungsten), [tests](../../src/Python/tests/README.md) |

## Navigation Rules

- Start with this catalog when comparing data-structure availability across languages.
- Use the [workspace map](workspace-map.md) when choosing the correct language/data-structure directory.
- Use the [semantic contracts reference](semantic-contracts.md) when checking shared persistence,
  ownership, policy, ordering, and failure-behavior obligations.
- Use the [porting and semantic parity guide](../guides/porting-and-semantic-parity.md) when changing behavior that may cross language workspaces.
- Use workspace API specs and public headers for normative contracts.
- Keep new public data structures visible here when they become part of a long-lived workspace surface.
