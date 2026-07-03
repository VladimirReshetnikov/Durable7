# Data Structure Catalog

- Created (UTC): 2026-07-02T19:53:11Z
- Repository HEAD: 1d90612aed11f273521046015c9d63bb7c993bba
- Audience: Maintainers and AI agents comparing data-structure surfaces across languages
- Scope: Repository-owned data-structure families, public entry points, and primary reference links

This catalog is the cross-workspace orientation layer. It answers "which data structures exist in
which language, and where do I start?" The workspace API specifications and headers remain the
authoritative source for contracts, complexity, allocation behavior, and validation details.

Use this together with the [workspace map](workspace-map.md): the map explains the language-first
layout, while this catalog maps each data-structure family across that layout.

## HAMT Map And Set

The HAMT workspaces implement persistent hash-array mapped trie maps and sets with 32-way
bitmap-indexed branching, immutable equal-hash collision buckets, structural sharing between
versions, and comparer/hash-policy preservation.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PersistentHashMap<TKey, TValue>`, `PersistentHashSet<T>` | [Workspace](../../src/CSharp/Hamt/README.md), [usage guide](../../src/CSharp/Hamt/docs/usage.md), [API spec](../../src/CSharp/Hamt/docs/api-specification.md), [map source](../../src/CSharp/Hamt/src/Tools.DataStructures.Hamt/PersistentHashMap.cs), [set source](../../src/CSharp/Hamt/src/Tools.DataStructures.Hamt/PersistentHashSet.cs) |
| C | `tds_hamt_map`, `tds_hamt_set`, `tds_hamt_policy`, `tds_hamt_set_policy` | [Workspace](../../src/C/Hamt/README.md), [usage guide](../../src/C/Hamt/docs/usage.md), [API spec](../../src/C/Hamt/docs/api-specification.md), [public header](../../src/C/Hamt/include/Tools/DataStructures/Hamt/hamt.h) |
| C++ | `persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>`, `persistent_hash_set<T, Hash, KeyEqual>` | [Workspace](../../src/Cpp/Hamt/README.md), [usage guide](../../src/Cpp/Hamt/docs/usage.md), [API spec](../../src/Cpp/Hamt/docs/api-specification.md), [map header](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/persistent_hash_map.hpp), [set header](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/persistent_hash_set.hpp) |
| Haskell | `HashMap k v`, `HashSet a`, `HashPolicy k`, `Hashable` | [Workspace](../../src/Haskell/Hamt/README.md), [map source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/HashMap.hs), [set source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/HashSet.hs), [tests](../../src/Haskell/Hamt/test/README.md) |
| Rust | `PersistentHashMap<K, V, S>`, `PersistentHashSet<T, S>` | [Workspace](../../src/Rust/Hamt/README.md), [API notes](../../src/Rust/Hamt/docs/api-notes.md), [validation](../../src/Rust/Hamt/docs/validation.md), [source](../../src/Rust/Hamt/src/lib.rs) |

## Finger-Tree Core And Deque

The finger-tree workspaces provide persistent sequence engines: a tuned catenable deque and a
general monoid-measured tree. They support endpoint operations, concatenation, splitting by
position or measure, indexed access where exposed, and immutable structural sharing.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `FingerTreeDeque<T>`, `FingerTree<TElement, TMeasure, TMeasureOps>` | [Workspace](../../src/CSharp/FingerTree/README.md), [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md), [deque source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs), [measured tree source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/FingerTree.cs) |
| C | `ft_tree`, `ft_tree_policy`, `ft_measure_policy`, `ft_persistent_deque` | [Workspace](../../src/C/FingerTree/README.md), [usage guide](../../src/C/FingerTree/docs/usage.md), [API notes](../../src/C/FingerTree/docs/api-notes.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h) |
| C++ | `persistent_deque<T>`, `finger_tree<Element, MeasurePolicy>` | [Workspace](../../src/Cpp/FingerTree/README.md), [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [aggregate header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/finger_tree.hpp), [deque header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/persistent_deque.hpp), [measured tree header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_finger_tree.hpp) |
| Haskell | `Deque a`, `FingerTree v a`, `Measured v a` | [Workspace](../../src/Haskell/FingerTree/README.md), [deque source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Deque.hs), [measured tree source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measured.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `PersistentDeque<T>`, `FingerTree<T, P>`, `MeasurePolicy<T>` | [Workspace](../../src/Rust/FingerTree/README.md), [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [deque source](../../src/Rust/FingerTree/src/deque.rs), [measured source](../../src/Rust/FingerTree/src/measured.rs) |

## Reversible Deque

Reversible deques add orientation-aware views over persistent deque storage so reversal can be
represented without eagerly copying the sequence.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `ReversibleDeque<T>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_reversible_deque` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `reversible_deque<T>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/reversible_deque.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `ReversibleDeque a` | [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/ReversibleDeque.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `ReversibleDeque<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/deque.rs) |

## Sorted Collections

Sorted collections expose immutable sorted bags/multisets, sets, and key-value maps with
comparer-preserving behavior. The mature C#/C++/C ports use order-statistic measures over finger
trees; the Rust checkpoint preserves the surface over shared tree storage while its API notes track
the remaining lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `SortedBag<T>`, `SortedSet<T>`, `SortedSet<T>.Builder`, `SortedDictionary<TKey, TValue>`, `SortedDictionary<TKey, TValue>.Builder` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [bag](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedBag.cs), [set](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedSet.cs), [set builder](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedSet.Builder.cs), [dictionary](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedDictionary.cs), [dictionary builder](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedDictionary.Builder.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_sorted_multiset`, `ft_sorted_set`, `ft_sorted_map` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `sorted_bag<T, Less>`, `sorted_set<T, Less>`, `sorted_map<Key, T, Less>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [bag header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_bag.hpp), [set header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_set.hpp), [map header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_map.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `SortedBag a`, `SortedSet a`, `SortedMap k v` | [bag source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/SortedBag.hs), [set source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/SortedSet.hs), [map source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/SortedMap.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `SortedBag<T>`, `SortedSet<T>`, `SortedMap<K, V>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/sorted.rs) |

## Priority Queue

Priority queues locate and remove the front priority entry according to each language's comparison
policy. The mature C#/C++/C ports use measured finger-tree facades; the Rust checkpoint now uses
cached minimum-priority measures over its measured tree while its API notes track the remaining
lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PriorityQueue<TElement, TPriority>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/PriorityQueue.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_priority_queue` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `priority_queue<Element, Priority, Comparison>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/priority_queue.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `PriorityQueue p a` | [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/PriorityQueue.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `PriorityQueue<T, P>`, `PriorityEntry<T, P>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/priority_queue.rs) |

## Interval Tree

Interval trees store ordered interval collections for overlap and containment queries. The mature
C#/C++/C ports use interval annotations in finger-tree measures so queries can skip subtrees whose
summary cannot intersect the probe; the Rust checkpoint preserves the surface over shared tree
storage while its API notes track the remaining lazy-spine parity boundary.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IntervalTree<T>`, `Interval<T>`, `IntervalMeasure<T>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/IntervalTree.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_interval_tree`, `ft_interval_tree_i64`, `ft_interval_i64` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `interval_tree<T, Comparison>` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/interval_tree.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `IntervalTree a`, `Interval a` | [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/IntervalTree.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `IntervalTree<T>`, `Interval<T>` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/interval_tree.rs) |

## Ropes And Text

Ropes provide persistent chunked sequences, measured ropes add custom split/locate measures, and
text ropes specialize the same machinery for newline-aware text navigation.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `Rope<T>`, `Rope<T>.Builder`, `MeasuredRope<T, TMeasure, TMeasureOps>`, `MeasuredRope<T, TMeasure, TMeasureOps>.Builder`, `RopeText`, `RopeBuilder`, `NewlineMeasure`, `NewlineStyle` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [rope](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Rope.cs), [rope builder](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Rope.Builder.cs), [measured rope](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/MeasuredRope.cs), [measured rope builder](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/MeasuredRope.Builder.cs), [text](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/RopeText.cs), [text builder](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/RopeBuilder.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_rope`, `ft_measured_rope`, `ft_text_rope`, `ft_line_column` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `rope<T>`, `measured_rope<T, MeasurePolicy>`, `text_rope`, `rope_builder`, `newline_measure`, `line_column` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [rope header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope.hpp), [measured rope header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_rope.hpp), [text header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope_text.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `Rope a`, `MeasuredRope v a`, `TextRope`, `NewlineMeasure` | [rope source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Rope.hs), [measured rope source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/MeasuredRope.hs), [text source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Rope/Text.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `Rope<T>`, `MeasuredRope<T, P>`, `TextRope`, `RopeBuilder`, `NewlineMeasure`, `LineColumn` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [source](../../src/Rust/FingerTree/src/rope.rs) |

## Measures, Comparisons, And Predicates

Measures are the connective tissue for finger-tree-derived collections. The C# and C++ workspaces
expose typed measure/comparison/predicate abstractions; the C workspace exposes equivalent policy
callbacks and context pointers.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IMonoid<TMeasure>`, `IMeasure<TElement, TMeasure>`, `IMeasurePredicate<TMeasure>`, `IComparison<T>`, `SizeMeasure<TElement>`, `SumMeasure<T>`, `ProductMeasure<...>`, `MaxMeasure<T>`, `MinMeasure<T>`, `KeyMeasure<T>`, `OrderStatisticMeasure<T>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [measures](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Measures.cs), [predicates](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/MeasurePredicate.cs), [comparisons](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Comparisons.cs), [built-ins](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs) |
| C | `ft_measure_policy`, `ft_measure_predicate_fn`, `ft_compare_fn`, `ft_value_type` | [usage guide](../../src/C/FingerTree/docs/usage.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | measure policies and predicates in `measures.hpp`, `built_in_measures.hpp`, `product_measure.hpp`, `sum_measure.hpp`, `comparisons.hpp`, and `measure_predicates.hpp` | [usage guide](../../src/Cpp/FingerTree/docs/usage.md), [measure headers](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measures.hpp), [built-ins](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/built_in_measures.hpp), [predicates](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measure_predicates.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |
| Haskell | `Measured v a`, `Size`, `Elem`, `MeasurePair`, `Maximum`, `Minimum`; predicates are ordinary pure functions `v -> Bool` | [measured tree source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measured.hs), [measures source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measures.hs), [tests](../../src/Haskell/FingerTree/test/README.md) |
| Rust | `MeasurePolicy<T>`, `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, `KeyMeasure<T>`, `ProductMeasure<T, PFirst, PSecond>`, `MeasurePair<TFirst, TSecond>`, `SizeAndSumMeasure<T>`, `SizeAndMaxMeasure<T>`, `SizeAndMinMeasure<T>`, `OrderStatisticMeasure<T>`, `RankedKey<T>`, `NewlineMeasure` | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [measures](../../src/Rust/FingerTree/src/measured.rs), [newline measure](../../src/Rust/FingerTree/src/rope.rs) |

## Navigation Rules

- Start with this catalog when comparing data-structure availability across languages.
- Use the [workspace map](workspace-map.md) when choosing the correct language/data-structure directory.
- Use the [porting and semantic parity guide](../guides/porting-and-semantic-parity.md) when changing behavior that may cross language workspaces.
- Use workspace API specs and public headers for normative contracts.
- Keep new public data structures visible here when they become part of a long-lived workspace surface.
