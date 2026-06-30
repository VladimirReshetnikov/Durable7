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
