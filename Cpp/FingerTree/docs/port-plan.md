# C++ Finger Tree Port Plan

- Status: Planning
- Created (UTC): 2026-06-30T02:52:50Z
- Repository HEAD: 8f20c4d9e49550ad8aa8de6fc177bdd8cc41f9fd
- Audience: Maintainers and AI agents implementing the C++ port of the FingerTree workspace
- Scope: Directory layout, toolchain choice, architecture, implementation sequence, and validation strategy

## Summary

The C++ port should live under `Cpp/FingerTree/` and should preserve the C# workspace's central design
decision: keep the tuned catenable deque separate from the fully general measured finger tree. The tuned deque
and the general measured tree share the same intellectual lineage, but they optimize different things and use
different digit shapes. Collapsing them into one implementation would erase useful engineering choices from the
C# codebase.

The port should use the latest C++ dialect supported by the local toolchain, currently MSVC 19.50 in
`/std:c++latest` mode through CMake `CXX_STANDARD 26`. The implementation should use modern C++ library and
language facilities where they improve clarity or performance, but it should avoid fragile draft-only features
such as modules in the first implementation wave. The initial target is a stable, header-first C++ library with
tests, samples, and benchmarks.

Editor-grade text extras are intentionally out of scope for the first C++ port. This excludes Unicode scalar
indexing, grapheme-cluster indexing, newline-style detection, and text-reader adapters. The positional rope and
measured rope remain in scope because they are repository-owned data structures rather than merely text
conveniences.

## Local Toolchain Baseline

The current machine has:

- MSVC `cl.exe` 19.50.35728 from Visual Studio 18 Insiders.
- CMake 4.2.3 bundled with Visual Studio.
- Ninja 1.12.1 bundled with Visual Studio.
- MSBuild available under the Visual Studio installation.
- No `clang++`, `g++`, `vcpkg`, or Conan on the current `PATH` at planning time.

The MSVC compiler must be run from an initialized Visual Studio developer environment, or through CMake presets
that locate the Visual Studio toolchain. A direct PowerShell invocation of `cl` without `VsDevCmd.bat` does not
have the standard library include path configured.

Recommended build entry points:

```powershell
cd C:\Users\vresh\.codex\worktrees\23eb\DataStructures\Cpp\FingerTree
& "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

The repository may install additional tooling. If dependency management is needed, prefer bootstrapping `vcpkg`
for stable open-source C++ libraries. Keep the core implementation dependency-light; use third-party packages
primarily for tests and benchmarks.

## Workspace Layout

Use `Cpp/FingerTree/` as the C++ workspace root:

```text
Cpp/
└── FingerTree/
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── vcpkg.json
    ├── README.md
    ├── docs/
    │   ├── README.md
    │   ├── api-notes.md
    │   ├── port-plan.md
    │   └── validation.md
    ├── include/
    │   └── tools/data_structures/finger_tree/
    │       ├── finger_tree.hpp
    │       ├── persistent_deque.hpp
    │       ├── reversible_deque.hpp
    │       ├── measures.hpp
    │       ├── comparisons.hpp
    │       ├── measure_predicates.hpp
    │       ├── built_in_measures.hpp
    │       ├── product_measure.hpp
    │       ├── sum_measure.hpp
    │       ├── sorted_bag.hpp
    │       ├── sorted_set.hpp
    │       ├── sorted_map.hpp
    │       ├── priority_queue.hpp
    │       ├── interval_tree.hpp
    │       ├── rope.hpp
    │       ├── measured_rope.hpp
    │       ├── rope_builder.hpp
    │       └── detail/
    │           ├── common.hpp
    │           ├── small_digit.hpp
    │           ├── node.hpp
    │           ├── lazy_cell.hpp
    │           ├── deque_elements.hpp
    │           ├── deque_tree.hpp
    │           ├── deque_middle.hpp
    │           ├── deque_operations.hpp
    │           ├── measured_elements.hpp
    │           ├── measured_tree.hpp
    │           ├── measured_tree_levels.hpp
    │           ├── measured_middle.hpp
    │           ├── reversible_elements.hpp
    │           ├── reversible_tree.hpp
    │           ├── rope_chunk.hpp
    │           └── measured_rope_chunk.hpp
    ├── tests/
    │   ├── CMakeLists.txt
    │   ├── persistent_deque_tests.cpp
    │   ├── measured_tree_tests.cpp
    │   ├── sorted_collection_tests.cpp
    │   ├── interval_tree_tests.cpp
    │   ├── priority_queue_tests.cpp
    │   ├── reversible_deque_tests.cpp
    │   ├── rope_tests.cpp
    │   ├── measured_rope_tests.cpp
    │   └── persistence_concurrency_tests.cpp
    ├── benchmarks/
    │   ├── CMakeLists.txt
    │   ├── deque_benchmarks.cpp
    │   ├── measured_tree_benchmarks.cpp
    │   ├── sorted_collection_benchmarks.cpp
    │   ├── priority_queue_benchmarks.cpp
    │   └── rope_benchmarks.cpp
    └── samples/
        ├── CMakeLists.txt
        ├── showcase.cpp
        └── persistent_snapshots.cpp
```

The public include namespace should be `tools::data_structures::finger_tree`. Internal implementation should
live under `tools::data_structures::finger_tree::detail`.

## Dependency Plan

Core library:

- Prefer the C++ standard library.
- Use `std::shared_ptr<const node>` or equivalent internal reference-counted ownership for persistent structure
  sharing.
- Use `std::atomic`, `std::once_flag`, or a small custom atomic lazy cell for memoized suspensions.
- Use concepts to encode measure, comparison, and predicate contracts.
- Use `std::span`, `std::array`, `std::vector`, `std::optional`, and simple result structs.

Tests:

- Prefer Catch2 through `vcpkg`.
- Use deterministic randomized model tests against `std::vector`, `std::multiset`, `std::set`, and `std::map`.
- Add property-style loops directly first; bring in RapidCheck or another property library only if it pays for
  itself.

Benchmarks:

- Use Google Benchmark through `vcpkg`.
- Compare against `std::vector`, `std::deque`, `std::set`, `std::map`, and `std::priority_queue` where those are
  sensible baselines, while documenting mutability and persistence differences.

Optional:

- Use Boost.Multiprecision in tests for generic sum-measure coverage if `vcpkg` is available.
- Avoid Boost as a core dependency unless a concrete piece of functionality beats a small local implementation.

## Dialect And Build Policy

Set the project to C++26 draft mode because it is the latest mode supported by the installed MSVC toolchain:

```cmake
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

For MSVC targets:

```cmake
target_compile_options(target PRIVATE
    /permissive-
    /Zc:__cplusplus
    /W4
    /WX
)
```

Keep C++26 usage deliberately modest at first. Concepts, ranges, and modern constexpr are welcome. Avoid modules,
coroutines, experimental reflection, and library features whose MSVC behavior is still visibly volatile. The
port should be easy to retarget to C++23 if a later CI environment needs a less aggressive standard mode.

## API Mapping Principles

C# API names should not be copied mechanically. Prefer idiomatic C++:

| C# concept | C++ shape |
| --- | --- |
| `FingerTree<TElement, TMeasure, TMeasureOps>` | `finger_tree<T, MeasurePolicy>` |
| `FingerTreeDeque<T>` | `persistent_deque<T>` |
| `ReversibleDeque<T>` | `reversible_deque<T>` |
| `SortedBag<T>` | `sorted_bag<T, Compare = std::less<T>>` |
| `SortedSet<T>` | `sorted_set<T, Compare = std::less<T>>` |
| `SortedDictionary<TKey, TValue>` | `sorted_map<Key, T, Compare = std::less<Key>>` |
| `PriorityQueue<TElement, TPriority>` | `priority_queue<T, Priority, Compare = std::less<Priority>>` |
| `IntervalTree<T>` | `interval_tree<T, Compare = std::less<T>>` |
| `Rope<T>` | `rope<T>` |
| `MeasuredRope<T, TMeasure, TMeasureOps>` | `measured_rope<T, MeasurePolicy>` |

Naming guidelines:

- Use `push_front`, `push_back`, `pop_front`, `pop_back`, `front`, `back`, `empty`, `size`, `begin`, and `end`
  for collection-like operations.
- Use `concat`, `split_at`, `split`, `locate`, `try_locate`, and `try_split_find` for finger-tree-specific
  operations.
- Return new values for every mutation-shaped operation; do not mutate existing snapshots.
- Use `std::optional<T>` for absent values when the value is small or move-friendly.
- Use named result structs for operations returning multiple values, such as `pop_result<T>`, `split_result<T>`,
  and `item_split_result<T>`.
- Prefer `std::size_t` for public sizes and indices. Add overflow checks where constructing result sizes could
  exceed `std::size_t`.

## Measure And Predicate Concepts

The C# code uses static abstract interfaces. C++ can express the same idea naturally with policy types and
concepts:

```cpp
template<class Policy, class T>
concept measure_policy =
    requires(const T& value,
             typename Policy::measure_type left,
             typename Policy::measure_type right) {
        typename Policy::measure_type;
        { Policy::empty() } -> std::same_as<typename Policy::measure_type>;
        { Policy::combine(left, right) } -> std::same_as<typename Policy::measure_type>;
        { Policy::measure(value) } -> std::same_as<typename Policy::measure_type>;
    };
```

Split and locate predicates should be generic callables:

```cpp
template<class Predicate>
auto split(Predicate predicate) const -> split_result<finger_tree>;

template<class Predicate>
auto try_locate(Predicate predicate) const -> std::optional<locate_result<T, measure_type>>;
```

Non-capturing struct predicates from the C# code should map to small C++ function objects. Capturing lambdas are
also fine for user code; hot library paths should use concrete predicate types so the compiler can inline and
devirtualize.

## Architecture

### General Measured Tree

`finger_tree<T, MeasurePolicy>` should port the Hinze-Paterson general measured tree:

- Digits hold one through four children.
- Internal nodes hold two or three children.
- Deep levels hold prefix digit, lazy middle tree, suffix digit.
- Each child exposes a cached measure.
- Each deep tree memoizes its full measure lazily.
- Endpoint reads use prefix/suffix directly and do not force middles.
- Endpoint updates and views use memoized middle suspensions to preserve persistent amortized bounds.
- `concat` implements the paper's `app3`/glue operation.
- `split`, `try_split_find`, and `try_locate` route by accumulated measure.

The internal representation should not expose virtual dispatch on public operations. A practical initial design is
to keep type-erased internal node states behind `std::shared_ptr<const impl>` and recover performance by keeping
node fanout tiny, operations monomorphic at the public template boundary, and measures/predicates statically
bound. If profiling shows virtual dispatch dominating, revisit with a `std::variant` representation.

### Tuned Persistent Deque

`persistent_deque<T>` should port the separate simplified deque core:

- Digits hold one through three elements.
- Internal nodes hold two or three children.
- Cached leaf counts make `size`, endpoint reads, and routing O(1) at each level.
- Cached rightmost-leaf signposts support sorted lower/upper-bound search by skipping subtrees.
- Middle trees are memoized suspensions, preserving the C# implementation's persistent amortized guarantees.
- Split and indexing should preserve the distance-from-nearer-end behavior described in the C# API spec.

This should not be a thin alias over `finger_tree<T, size_measure<T>>`. The C# code intentionally has a tuned
deque because the general measured tree cannot reuse all of the size-specific arithmetic and signpost shortcuts.

### Reversible Deque

`reversible_deque<T>` should be a sibling, not a mode bit on `persistent_deque<T>`:

- Store reversal bits on nodes and deep levels.
- Provide O(1) `reverse()`.
- Interpret children through orientation-aware accessors.
- Preserve O(log(min(n, m))) concat even when operands have mixed orientations.
- Keep forward-only use reasonably fast by checking the common orientation path first.

### Derived Sorted Collections

`sorted_bag`, `sorted_set`, and `sorted_map` should be wrappers around the general measured tree with
order-statistic measures:

- Store the runtime comparer as part of the wrapper.
- Enforce sortedness by construction.
- Use locate/split predicates for membership, rank, neighbor, add, and remove.
- Implement set algebra through linear sorted merges rather than repeated O(log n) operations when both operands
share an order.
- Preserve structural sharing when operations can reuse large unchanged tree ranges.

For C++, comparator equality is not generally decidable. Operations combining two sorted collections should assume
the same comparator type and semantically equivalent comparator state, matching normal ordered-container practice.
Where comparator mismatch is possible, document it as a precondition.

### Priority Queue

`priority_queue<T, Priority, Compare>` should use a count-plus-min measure:

- Entries store element, priority, and a monotonic insertion ordinal if needed for stable equal-priority behavior.
- `push` appends to the underlying measured tree.
- `peek_priority` reads the measure in O(1) amortized.
- `try_peek` and `try_pop` locate the first entry matching the minimum priority.
- `meld` is tree concatenation, preserving O(log(min(n, m))) behavior.

Unlike `std::priority_queue`, this collection is persistent and meldable. Benchmarks should call that out rather
than pretending the standard heap has the same contract.

### Interval Tree

`interval_tree<T, Compare>` should port the full Hinze-Paterson interval tree:

- Store closed intervals in low-endpoint order.
- Measure count, right-biased last low endpoint, and maximum high endpoint.
- Use maximum-high predicates for logarithmic overlap/stabbing queries.
- Preserve duplicate interval values unless an operation explicitly removes one occurrence.
- Implement overlap enumeration as repeated logarithmic extraction first, then consider a more specialized
  traversal if benchmarks justify it.

### Rope And Measured Rope

`rope<T>` should be a chunked persistent sequence over `finger_tree<chunk<T>, chunk_length_measure<T>>`:

- Chunks own or share immutable contiguous storage.
- Reads and splits locate chunks by cumulative length.
- Edits copy only the touched bounded-size chunk plus the tree path.
- Slices may share backing chunk storage.
- `compact()` can rebuild chunks to release retained oversized backing buffers.

`measured_rope<T, MeasurePolicy>` should store chunks with both length and user measure:

- Tree measure is `measure_pair<std::size_t, user_measure>`.
- Positional operations split on the count component.
- Measure navigation splits on the user-measure component.
- `prefix_measure(count)` computes the user measure of a positional prefix.

Text extras are a non-goal for the initial port. A future layer may add UTF and editor conveniences if a C++ user
needs them, but the core rope should stay element-agnostic.

## Lazy Memoization And Thread Safety

The C# implementation's key strict-language move is a shared memoized suspension for each deep middle. The C++
port needs the same idea to keep amortized bounds meaningful under branching persistence.

Recommended initial design:

- Represent a lazy middle as a small shared cell.
- The cell contains either a pending operation object or a computed tree.
- Forcing computes the result outside a lock where practical, then publishes it with a compare-exchange or
  `std::call_once`.
- Concurrent readers may duplicate bounded pending work, but all must converge on one published result.
- Pending operations must be immutable and pure.
- A new suspension should capture an already-forced source and defer exactly one push or pop operation, matching
  the C# discipline that prevents unbounded chains of nested suspensions.

If `std::call_once` makes it awkward to discard the pending operation after force, use an atomic state machine with
`std::shared_ptr<const state_base>` and `compare_exchange`. Keep the API surface immutable regardless of the
internal memoization.

## Implementation Milestones

### Milestone 1: Build Skeleton

Create the C++ workspace:

- `Cpp/FingerTree/CMakeLists.txt`
- `CMakePresets.json`
- `vcpkg.json`
- basic README and docs index
- one empty library target
- one smoke-test target

Validation:

- Configure and build with the Visual Studio CMake and Ninja tools.
- Run an empty or trivial test target through CTest.

### Milestone 2: Measure Infrastructure

Implement:

- measure and monoid concepts
- comparison policies
- optional measure carrier
- size, min, max, key, order-statistic, sum, and product measures
- predicate helpers for count, key, optional extremum, sum threshold, and product projection

Validation:

- Compile-time concept tests.
- Monoid law tests for representative values.
- Predicate behavior tests against simple vectors.

### Milestone 3: General Measured Tree

Implement `finger_tree<T, MeasurePolicy>`:

- empty, single, deep levels
- measured leaves and measured nodes
- lazy middles
- append/prepend/view-left/view-right
- concat
- split/try-split-find/try-locate
- iteration and materialization helpers
- invariant validation in test-only hooks

Validation:

- Positional tests through `size_measure`.
- Priority, key, order-statistic, sum, and product-measure tests.
- Randomized split/concat laws against `std::vector`.
- Branching persistence tests that reuse retained versions.
- Concurrent first-force read tests.

### Milestone 4: Tuned Persistent Deque

Implement `persistent_deque<T>`:

- simplified digits and 2/3 nodes
- cached size and rightmost signposts
- lazy middles and tree operations
- endpoint operations
- indexing, update, insert, remove
- split and range operations
- sorted lower/upper-bound helpers
- iteration

Validation:

- API contract tests against `std::vector`.
- Sorted-search tests against `std::lower_bound`/`std::upper_bound`.
- Randomized model histories.
- Invariant validation after every randomized operation.
- Complexity guard tests focused on comparer-call counts and endpoint allocation shape where practical.

### Milestone 5: Derived Collections

Implement:

- `sorted_bag`
- `sorted_set`
- `sorted_map`
- `priority_queue`
- `interval_tree`

Validation:

- Compare content and query results with `std::multiset`, `std::set`, `std::map`, vector-backed interval models,
  and `std::priority_queue` where contracts overlap.
- Add persistence tests over retained versions.
- Add allocation or operation-count guards for hot locate paths where the test framework can observe them
  robustly.

### Milestone 6: Reversible Deque

Implement `reversible_deque<T>`:

- reversible leaves/nodes/deep levels
- orientation-aware digit access
- O(1) reverse
- mixed-orientation concat
- split/index/edit operations

Validation:

- Double-reverse identity.
- Operation-after-reverse model tests.
- All four mixed-orientation concat combinations.
- Randomized histories interleaving reverse with every operation.

### Milestone 7: Rope Family

Implement:

- `rope<T>`
- `measured_rope<T, MeasurePolicy>`
- `rope_builder<T>` or a char-specialized builder if generic builder ergonomics are poor

Validation:

- Construction from spans, ranges, chunks.
- Indexed reads and edits.
- Split/slice/concat laws.
- Chunk invariant checks.
- Large mid-splice tests.
- Measured-rope prefix and measure-split tests against brute-force models.

### Milestone 8: Samples And Benchmarks

Add samples:

- `showcase.cpp`: priority queue, sorted collections, interval queries, weighted selection, reversible deque.
- `persistent_snapshots.cpp`: cheap snapshots and lock-free publication patterns.

Add benchmarks:

- deque endpoint and index operations
- concat
- sorted search
- priority-queue meld
- weighted selection
- rope insert/split/slice
- reversible reverse

Benchmarks must describe the contract difference between persistent structures and mutable standard-library
baselines.

## Validation Policy

Every milestone should include:

- `cmake --preset msvc-debug`
- `cmake --build --preset msvc-debug`
- `ctest --preset msvc-debug`

Before treating the C++ port as usable:

- Build debug and release presets.
- Run all deterministic and randomized tests.
- Run benchmarks at least in short mode.
- Run concurrency tests under a duration environment variable similar to `FINGERTREE_STRESS_SECONDS`.
- Run static analysis available from MSVC where practical.

## Documentation Policy

Keep the C++ docs current-state oriented:

- `README.md` should explain the workspace and build commands.
- `docs/api-notes.md` should document the C++ API shape and differences from C#.
- `docs/validation.md` should record how to run tests, stress tests, and benchmarks.
- Public headers should document contracts, persistence, complexity, exception behavior, and iterator invalidation
  semantics.

Avoid promising hard worst-case endpoint updates where the implementation only proves persistent amortized bounds.
The C# documentation is unusually careful here; the C++ docs should be just as explicit.

## Open Design Questions

- Internal representation: start with polymorphic implementation nodes for clarity, then profile. A variant-based
  representation may reduce dispatch overhead but could make recursive types harder to maintain.
- Lazy cell implementation: choose between `std::call_once` simplicity and compare-exchange cells that can release
  pending state more eagerly.
- Iterator category: start with forward iterators. Random-access iterators would imply stronger performance
  expectations than this structure naturally provides.
- Public allocator support: defer until the core invariants are stable. Allocator-aware persistent trees are
  possible, but the interaction with shared immutable nodes and lazy cells deserves a separate design pass.
- Exception guarantees: document and test the intended guarantee for comparers, copy/move construction, and
  allocation failure. The natural target is strong exception safety for public operations when element operations
  meet the standard container requirements.
