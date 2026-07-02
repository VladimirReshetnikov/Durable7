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

## Finger-Tree Core And Deque

The finger-tree workspaces provide persistent sequence engines: a tuned catenable deque and a
general monoid-measured tree. They support endpoint operations, concatenation, splitting by
position or measure, indexed access where exposed, and immutable structural sharing.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `FingerTreeDeque<T>`, `FingerTree<TElement, TMeasure, TMeasureOps>` | [Workspace](../../src/CSharp/FingerTree/README.md), [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md), [deque source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs), [measured tree source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/FingerTree.cs) |
| C | `ft_tree`, `ft_tree_policy`, `ft_measure_policy`, `ft_persistent_deque` | [Workspace](../../src/C/FingerTree/README.md), [API notes](../../src/C/FingerTree/docs/api-notes.md), [public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h) |
| C++ | `persistent_deque<T>`, `finger_tree<Element, MeasurePolicy>` | [Workspace](../../src/Cpp/FingerTree/README.md), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [aggregate header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/finger_tree.hpp), [deque header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/persistent_deque.hpp), [measured tree header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_finger_tree.hpp) |

## Reversible Deque

Reversible deques add orientation-aware views over persistent deque storage so reversal can be
represented without eagerly copying the sequence.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `ReversibleDeque<T>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_reversible_deque` | [Public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `reversible_deque<T>` | [Header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/reversible_deque.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |

## Sorted Collections

Sorted collections use order-statistic measures over finger trees. They expose immutable sorted
bags/multisets, sets, and key-value maps with comparer-preserving behavior.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `SortedBag<T>`, `SortedSet<T>`, `SortedDictionary<TKey, TValue>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [bag](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedBag.cs), [set](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedSet.cs), [dictionary](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/SortedDictionary.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_sorted_multiset`, `ft_sorted_set`, `ft_sorted_map` | [Public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `sorted_bag<T, Less>`, `sorted_set<T, Less>`, `sorted_map<Key, T, Less>` | [bag header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_bag.hpp), [set header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_set.hpp), [map header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/sorted_map.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |

## Priority Queue

Priority queues are measured finger-tree facades that locate and remove the highest-priority entry
according to each language's comparison policy.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `PriorityQueue<TElement, TPriority>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/PriorityQueue.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_priority_queue` | [Public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `priority_queue<Element, Priority, Comparison>` | [Header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/priority_queue.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |

## Interval Tree

Interval trees store interval annotations in finger-tree measures so overlap queries can skip
subtrees whose summary cannot intersect the probe.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IntervalTree<T>`, `Interval<T>`, `IntervalMeasure<T>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [source](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/IntervalTree.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_interval_tree`, `ft_interval_tree_i64`, `ft_interval_i64` | [Public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `interval_tree<T, Comparison>` | [Header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/interval_tree.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |

## Ropes And Text

Ropes provide persistent chunked sequences, measured ropes add custom split/locate measures, and
text ropes specialize the same machinery for newline-aware text navigation.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `Rope<T>`, `MeasuredRope<T, TMeasure, TMeasureOps>`, `RopeText`, `RopeBuilder`, `NewlineMeasure`, `NewlineStyle` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [rope](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Rope.cs), [measured rope](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/MeasuredRope.cs), [text](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/RopeText.cs), [builder](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/RopeBuilder.cs), [API spec](../../src/CSharp/FingerTree/docs/api-specification.md) |
| C | `ft_rope`, `ft_measured_rope`, `ft_text_rope`, `ft_line_column` | [Public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | `rope<T>`, `measured_rope<T, MeasurePolicy>`, `text_rope`, `rope_builder`, `newline_measure`, `line_column` | [rope header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope.hpp), [measured rope header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_rope.hpp), [text header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/rope_text.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |

## Measures, Comparisons, And Predicates

Measures are the connective tissue for finger-tree-derived collections. The C# and C++ workspaces
expose typed measure/comparison/predicate abstractions; the C workspace exposes equivalent policy
callbacks and context pointers.

| Language | Public entry points | Primary references |
| --- | --- | --- |
| C# | `IMonoid<TMeasure>`, `IMeasure<TElement, TMeasure>`, `IMeasurePredicate<TMeasure>`, `IComparison<T>`, `SizeMeasure<TElement>`, `SumMeasure<T>`, `ProductMeasure<...>`, `MaxMeasure<T>`, `MinMeasure<T>`, `KeyMeasure<T>`, `OrderStatisticMeasure<T>` | [usage guide](../../src/CSharp/FingerTree/docs/usage.md), [measures](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Measures.cs), [predicates](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/MeasurePredicate.cs), [comparisons](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Comparisons.cs), [built-ins](../../src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs) |
| C | `ft_measure_policy`, `ft_measure_predicate_fn`, `ft_compare_fn`, `ft_value_type` | [Public header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h), [API notes](../../src/C/FingerTree/docs/api-notes.md) |
| C++ | measure policies and predicates in `measures.hpp`, `built_in_measures.hpp`, `product_measure.hpp`, `sum_measure.hpp`, `comparisons.hpp`, and `measure_predicates.hpp` | [measure headers](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measures.hpp), [built-ins](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/built_in_measures.hpp), [predicates](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measure_predicates.hpp), [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) |

## Navigation Rules

- Start with this catalog when comparing data-structure availability across languages.
- Use the [workspace map](workspace-map.md) when choosing the correct language/data-structure directory.
- Use the [porting and semantic parity guide](../guides/porting-and-semantic-parity.md) when changing behavior that may cross language workspaces.
- Use workspace API specs and public headers for normative contracts.
- Keep new public data structures visible here when they become part of a long-lived workspace surface.
