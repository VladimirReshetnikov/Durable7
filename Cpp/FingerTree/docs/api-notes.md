# C++ FingerTree API Notes

- Status: Initial notes
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers implementing and reviewing public C++ APIs
- Scope: C++ naming, contracts, and intentional differences from the C# workspace

The public namespace is `tools::data_structures::finger_tree`.

The C++ port follows the repository's C# semantics, but it uses idiomatic C++ spelling:

- collection observers use `empty`, `size`, `front`, `back`, `at`, and `operator[]`;
- persistent updates return new values and do not mutate existing snapshots;
- absent ranks use `std::optional<std::size_t>` rather than a `-1` sentinel;
- multi-value returns use named result structs with structural equality;
- container types deliberately do not define pointer-based default equality;
- runtime comparators are stored by the sorted collection wrappers, while priority and interval measures use
  compile-time comparison policy state;
- concurrently published structure, lazy-state, and measure-box pointers use atomic `std::shared_ptr` publication.

The first wave targets MSVC `/std:c++latest`, while keeping the implementation to stable C++20/23-era facilities
where practical. CMake currently models the target as `CXX_STANDARD 23` and adds `/std:c++latest` explicitly for
MSVC because this bundled CMake rejects `CXX_STANDARD 26` for the installed compiler.

## `persistent_deque<T>`

`persistent_deque<T>` is the C++ port of C# `FingerTreeDeque<T>`. It is immutable: every update returns a new deque
value and existing snapshots remain valid. The implementation is the same tuned simplified finger tree, not a
vector-backed compatibility layer.

Primary C++ spellings:

- observers: `empty`, `size`, `front`, `back`, `try_front`, `try_back`, `at`, `operator[]`, `try_get`;
- endpoint updates: `push_front`, `push_back`, `remove_first`, `remove_last`, `pop_first`, `pop_last`;
- indexed updates: `set_item`, `set_at`, `update_at`, `insert_at`, `insert_range`, `remove_at`, `remove_range`;
- slicing and catenation: `get_range`, `split_at`, `split_item_at`, `split_range`, `concat`, `add_range`;
- sorted-sequence helpers: `sorted_lower_bound`, `sorted_upper_bound`, `sorted_binary_search`, `sorted_contains`,
  `split_at_sorted_lower_bound`, `split_at_sorted_upper_bound`, `split_at_sorted_equal_range`, `insert_sorted`,
  and `remove_all_sorted`;
- traversal/materialization: `begin`, `end`, `copy_to`, `to_vector`.

Notable C++ differences from C#:

- index and count types are `std::size_t`; `sorted_binary_search` returns `std::ptrdiff_t` to retain the C#
  bitwise-complement insertion-index convention;
- try-peek/get operations return nullable pointers instead of using out parameters;
- construction uses initializer-list, iterator, and range APIs rather than C# `params ReadOnlySpan<T>` and
  `IEnumerable<T>` overloads;
- runtime sorted-search comparers are `std::less`-style callables where `compare(a, b)` means `a < b`.

## `finger_tree<T, MeasurePolicy>`

`finger_tree<T, MeasurePolicy>` is the C++ port of the public C# general measured
`FingerTree<TElement, TMeasure, TMeasureOps>`. `MeasurePolicy` supplies the monoid identity, monoid combine, and
element measure through the same static-policy shape used by the measure infrastructure.

Primary operations:

- observers: `empty`, `measure`, `front`, `back`;
- endpoint updates/views: `prepend`, `append`, `try_view_left`, `try_view_right`;
- catenation: `concat`;
- measure-guided search: `split`, `try_split_find`, `try_locate`;
- materialization/copy: `to_vector`, `copy_to`.

Notable C++ differences from C#:

- the measure policy is a single C++ type parameter whose nested `measure_type` names the measure, rather than
  separate `TMeasure` and `TMeasureOps` generic parameters;
- split and locate predicates are ordinary C++ callables; non-capturing predicate objects and lambdas both
  instantiate the same templated descent path;
- result values use `std::optional` for absent views/searches instead of C# `bool` plus out parameters;
- enumeration is exposed through `to_vector`/`copy_to` in this checkpoint. A streaming iterator can be added on top
  of the same tree/node block traversal used by the deque without changing tree semantics.

## `sorted_bag<T, Less>`, `sorted_set<T, Less>`, And `sorted_map<Key, T, Less>`

The sorted collection wrappers are the C++ ports of C# `SortedBag<T>`, `SortedSet<T>`, and
`SortedDictionary<TKey, TValue>`. They are persistent wrappers over the general measured tree with
order-statistic measures.

Primary bag operations:

- observers: `empty`, `size`, `comparison`, `min`, `max`, `at`, `operator[]`;
- updates: `add`, `add_range`, `try_remove`, `remove`, `remove_all`;
- queries: `contains`, `count_less_than`, `count_at_most`, `count_of`, `get_range`;
- materialization: `to_vector`.

Primary set operations:

- observers and rank access: `empty`, `size`, `comparison`, `min`, `max`, `at`, `operator[]`, `index_of`;
- updates: `add`, `add_range`, `try_remove`, `remove`;
- navigation: `try_floor`, `try_ceiling`, `try_lower`, `try_higher`, `get_range`;
- algebra and relations: `union_with`, `intersect`, `except`, `symmetric_except`, subset/superset predicates,
  `overlaps`, and `set_equals`;
- materialization: `to_vector`.

Primary map operations:

- observers and key/rank access: `empty`, `size`, `comparison`, `min_entry`, `max_entry`, `at`, `entry_at`,
  `index_of_key`;
- lookup and updates: `contains_key`, `try_get`, `set_item`, `insert`, `try_insert`, `try_remove`, `remove`;
- navigation: `try_floor_entry`, `try_ceiling_entry`, `try_lower_entry`, `try_higher_entry`, `get_range`;
- materialization: `to_vector`, `keys_to_vector`, `values_to_vector`.

Notable C++ differences from C#:

- sorted wrappers store a runtime `Less` object, defaulting to `std::less<>`, because their order-statistic
  measures are comparison-independent just like the C# sorted wrappers' measures;
- absent ranks use `std::optional<std::size_t>` instead of C#'s `-1` sentinel;
- absent lookup, navigation, insertion, and removal results use `std::optional`;
- `sorted_map` is the C++ name for C# `SortedDictionary`;
- `sorted_bag` preserves comparer-equal insertion order, `sorted_set` keeps the first comparer-equal value during
  range construction, and `sorted_map` keeps the last duplicate-key entry;
- traversal is exposed through vector materialization in this checkpoint. Lazy sorted-wrapper iterators can be
  added after the general measured tree grows a streaming iterator.

## `priority_queue<T, Priority, Comparison>`

`priority_queue<T, Priority, Comparison>` is the C++ port of C# `PriorityQueue<TElement, TPriority>`. It is a
persistent meldable minimum-priority queue backed by `finger_tree<priority_entry<T, Priority>, priority_measure<...>>`.

Primary operations:

- observers: `empty`, `size`, `try_peek_priority`, `try_peek`;
- updates: `enqueue`, `try_dequeue`, `meld`;
- materialization: `to_vector`.

Notable C++ differences from C#:

- priority ordering is a compile-time static comparison policy, defaulting to `default_comparison<Priority>`. This
  matches the measure layer's compile-time comparison regime and still supports max-queue behavior through
  `reverse_comparison<Priority>`;
- absent peek/dequeue results use `std::optional`;
- `to_vector` returns entries in insertion/tree order, matching the C# queue's unspecified enumeration order.

## `interval_tree<T, Comparison>`

`interval_tree<T, Comparison>` is the C++ port of C# `IntervalTree<T>`. It stores closed `interval<T>` values in
nondecreasing low-endpoint order and is backed by
`finger_tree<interval<T>, interval_measure<T, Comparison>>`.

Primary operations:

- observers: `empty`, `size`;
- construction: default construction, initializer-list construction, iterator construction, and `from_range`;
- updates: `insert`, `try_remove`, `remove`, `coalesce`;
- queries: `try_find_overlap`, `try_find_containing`, `find_overlaps`, `count_overlaps`, `contains`;
- materialization: `to_vector`.

Notable C++ differences from C#:

- endpoint ordering is a compile-time static comparison policy, defaulting to `default_comparison<T>`. C# uses
  `Comparer<T>.Default`; the C++ policy shape avoids per-node comparer storage and also supports custom endpoint
  orderings such as projections;
- absent query/remove results use `std::optional` result values rather than C# `bool` plus out parameters;
- `size()` returns `std::size_t`;
- `find_overlaps` returns `std::vector<interval<T>>` in nondecreasing low-endpoint order;
- `contains` and `try_remove` match endpoints by the configured comparison policy, not by `operator==`, matching
  the C# comparer-equality contract.
