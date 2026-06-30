# C++ Finger Tree Port Plan

- Status: Planning
- Created (UTC): 2026-06-30T02:52:50Z
- Repository HEAD: 8f20c4d9e49550ad8aa8de6fc177bdd8cc41f9fd
- Audience: Maintainers and AI agents implementing the C++ port of the FingerTree workspace
- Scope: Directory layout, toolchain choice, architecture, implementation sequence, and validation strategy
- Companion: [`port-plan-editorial-notes.md`](port-plan-editorial-notes.md) records the non-obvious C#→C++
  porting hazards (with `file:line` evidence) behind the design choices below; consult it before implementing the
  lazy spine, the type-erased internal representation, or the comparator handling.

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
cd Cpp\FingerTree   # repository-relative; the C# workspace builds from C:\DataStructures\FingerTree
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
    │   ├── port-plan-editorial-notes.md
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
    /external:anglebrackets /external:W0   # do not promote STL/vcpkg header warnings to errors under /WX
)
```

`/WX` combined with `/std:c++latest` on an Insiders MSVC is itself a build-stability risk for a header-only,
template- and concept-heavy library: standard-library and third-party header warnings — outside this project's
control and liable to change across toolchain bumps — would otherwise be promoted to hard errors. Keep `/WX`, but
scope it to the project's own code with `/external:anglebrackets /external:W0` (and `/external:I <dir>` for vcpkg
includes). `PRIVATE` scoping keeps the flags off consumers but does not by itself shield STL-header diagnostics.

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
- Use `operator[]` and `at(index)` for the indexable types (`persistent_deque`, `reversible_deque`, the sorted
  collections by rank, `rope`, `measured_rope`). The C# deque is `IReadOnlyList<T>` with an O(log min(i+1, n−i))
  indexer and the sorted collections offer O(log n) order-statistic indexing; the "start with forward iterators"
  decision is about iterator *category* only and does not drop indexed access.
- Return new values for every mutation-shaped operation; do not mutate existing snapshots.
- Map the pervasive C# `bool Try…(out value)` shape carefully. A bool plus one absent-on-failure out-value maps to
  `std::optional<T>` / `std::optional<result_struct>`. The one exception is `try_locate`: on the miss path it
  still returns a meaningful `measure_before` (the whole-tree measure, so a rank query reads the full count), so
  do **not** wrap the whole result in `std::optional`. Use a total result that always carries the measure with the
  boundary element optional: `struct locate_result<T, M> { M measure_before; std::optional<T> found; }`.
- Use named result structs for operations returning multiple values, such as `pop_result<T>`, `split_result<T>`,
  and `item_split_result<T>`.
- Give the pure value carriers (`measure_pair`, `ranked_key`, `interval`, `interval_annotation`,
  `priority_aggregate`, the optional measure carrier, and the deque result structs) a defaulted
  `bool operator==(const X&) const = default;` to preserve the C# `record struct` structural-equality contract
  (add `std::hash` only for carriers a user would key on, e.g. `interval`). Do **not** default `operator==` on the
  container types (`persistent_deque`, etc.): the C# containers deliberately omit value equality, and a defaulted
  C++ `operator==` over `shared_ptr` members would compare pointers, not contents.
- Prefer `std::size_t` for public sizes and indices; treat count overflow as a precondition violation rather than
  promising C#'s wrap-or-throw behavior (C# counts are 32-bit `int`, wrapping silently in the general tree but
  throwing in the deque — neither transfers). This `int → size_t` change is signed→unsigned: audit every
  `count > rank`, `size − index`, `index + 1`, and `count_at_most − count_less_than` for unsigned underflow and
  signed/unsigned comparison surprises. Map absent-rank results (`IndexOf`/`IndexOfKey` returning `-1`) to
  `std::optional<std::size_t>`, never a `-1` sentinel.

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

// locate_result is a TOTAL result: measure_before is always meaningful (the whole-tree measure on a miss, so a
// rank query reads the full count); only the boundary element is optional. Do not wrap the whole thing in
// std::optional — that would discard the miss-path measure (see API Mapping Principles).
template<class Predicate>
auto try_locate(Predicate predicate) const -> locate_result<T, measure_type>;
// struct locate_result<T, M> { M measure_before; std::optional<T> found; };
```

Non-capturing struct predicates from the C# code should map to small C++ function objects. Capturing lambdas are
also fine for user code; hot library paths should use concrete predicate types so the compiler can inline and
devirtualize.

There are **three distinct comparator regimes** in the C# library, and the C++ `Compare` parameter means
something different in each (see editorial notes §3). (1) The sorted collections use a *comparator-independent*
order-statistic measure and consult a *runtime* comparer only inside the query predicate, so they can pick the
order at runtime without changing the measure type. (2) The priority queue and interval tree bake the comparison
*into the measure monoid* (`combine` computes the running min / max-high), so their `Compare` must be a
compile-time, stateless, default-constructible measure-policy ingredient, identical in type on both operands of
`meld`/`concat`. (3) The `max_measure<T, Compare>` / `min_measure<T, Compare>` family maps a static comparison
policy directly. Do not store a per-node comparator; regimes (2) and (3) keep the order static, regime (1) keeps
it in the predicate object.

The library's public ergonomics are a **named-operation layer** of free functions over the raw
`split`/`try_split_find`/`try_locate` primitives — the C# `FingerTreeMeasureExtensions` (peek/extract max/min,
lower/upper-bound split, split-at-index), `FingerTreeProductExtensions` (component-projecting splits/finds), and
`FingerTreeSumExtensions` (`split_by_cumulative_weight`, `try_select_by_cumulative_weight`). Port these as free
functions co-located with their measures in `built_in_measures.hpp`, `product_measure.hpp`, and `sum_measure.hpp`;
they are headline API, not optional sugar (see Milestone 2/3).

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

**The internal node representation must be type-erased — this is mandatory, not a tuning choice.** The C# levels
use *polymorphic recursion*: a level's middle is `MeasuredTree<TElement, MeasuredNode<TElement, TChild, …>, …>`,
so each deeper level is a *distinct, deeper* type while the measure type stays fixed. .NET reifies these lazily at
runtime (only O(log n) levels ever exist for a tree of size n); C++ instantiates templates at compile time, so a
literal port (`node<node<node<…>>>`) is unbounded recursive instantiation and **will not compile**. Below the
public `finger_tree<T, MeasurePolicy>` boundary every level must therefore collapse to a *single erased child
type* — e.g. `std::shared_ptr<const measured_element>` carrying a cached `measure_type` — with the leaf level
wrapping `T` and deeper levels wrapping erased 2–3 nodes. `measure_type` is level-invariant. "Operations
monomorphic at the public template boundary" refers to the measure/predicate *policy* staying statically bound; it
does not license keeping the internal level structure templated. A `std::variant` representation does **not** fix
this — a `variant` whose `deep` alternative names the next level's node type is the same non-terminating chain; it
can only discriminate the empty/single/deep shape at a *fixed* level. Keep public operations free of virtual
dispatch by keeping node fanout tiny and measures/predicates statically bound; profile before adding any further
indirection.

`measure()` on the general tree is **O(1) amortized, never worst-case**: the first read of a fresh deep node may
force an O(log n) chain of suspended spine repairs before memoizing, whereas `front()`/`back()` read the
prefix/suffix directly and stay O(1) worst-case without forcing. The reason is the push-vs-pop asymmetry the port
must preserve: a deep node's measure is `combine(prefix, middle_measure, suffix)`, and the lazy middle can report
its measure without forcing only for a *push* suspension (arithmetically, `combine(pushed, source)`); a *pop*
suspension must force, because a general monoid has no inverse to subtract by. The pending-operation type must
expose a `try_measure_without_forcing(measure&) -> bool` equivalent (true/arithmetic for push, false/force for
pop). This asymmetry is exactly why the tuned deque needs no measure box and is kept separate.

### Tuned Persistent Deque

`persistent_deque<T>` should port the separate simplified deque core:

- Digits hold one through three elements.
- Internal nodes hold two or three children.
- Cached leaf counts make `size`, endpoint reads, and routing O(1) at each level. Unlike the general tree's
  measure, the deque's count is **eagerly computed and stored as a plain immutable field** in the deep-node
  constructor — every construction site derives the new count arithmetically (size has an inverse: subtraction),
  so size never forces the middle and needs **no atomic memoization cell**. In C++ this is a plain immutable
  `std::size_t` (safe to read concurrently because it is written-before-publish and never mutated). The only
  shared-mutable cell in the deque is the lazy middle suspension.
- Cached rightmost-leaf signposts support sorted lower/upper-bound search by skipping subtrees.
- Middle trees are memoized suspensions, preserving the C# implementation's persistent amortized guarantees.
- Split and indexing should preserve the distance-from-nearer-end behavior described in the C# API spec.

This should not be a thin alias over `finger_tree<T, size_measure<T>>`. The C# code intentionally has a tuned
deque because the general measured tree cannot reuse all of the size-specific arithmetic and signpost shortcuts.
The invertible size measure (deque) vs. non-invertible general monoid (measured tree, which needs the lazily
memoized measure box of the previous section) is the core engineering reason the two cores stay separate.

### Reversible Deque

`reversible_deque<T>` should be a sibling, not a mode bit on `persistent_deque<T>`:

- Store reversal bits on nodes and deep levels.
- The reversible core is **strict — no lazy spine, no memoized middle suspensions** (the C# `RevDeepTree` holds an
  eagerly-computed child tree). Do **not** add lazy cells, pending-operation objects, or atomic publication to
  this subsystem; the suspension machinery of the previous sections applies to `finger_tree` and `persistent_deque`
  only.
- Because it is strict, its O(1) amortized endpoint bound holds **only for single-threaded linear use**; under
  branching/persistent reuse of a retained version the endpoint bound is O(log n) worst case. Do not document or
  test O(1)-amortized-under-branching for this type (in contrast to the other two cores).
- Provide O(1) `reverse()`. This depends on `mirror()` **sharing** the existing immutable children (a new node
  reusing the same prefix/middle/suffix handles plus a flipped bit), never deep-copying them. Children must be
  `std::shared_ptr<const …>` handles; storing them by value (`std::array`/`std::vector` of nodes) would deep-copy
  on mirror and silently make `reverse()` O(n) per touched node.
- Interpret children through orientation-aware accessors, but note these are **not free on the reversed path**: a
  faithful port of `LogicalChild`/`LogicalChildren` materializes an orientation-flipped element (and array) per
  reversed access. Prefer a small by-value "oriented element" (handle + orientation bit) over allocating a fresh
  `shared_ptr<node>` per access.
- Implement reversed-orientation `front()`/`back()` via the opposite-digit identity against cached
  forward-first/forward-last scalars (the logical-first leaf of a reversed element is the underlying element's
  logical-last) — never by materializing a mirror — so endpoint reads stay O(1) and allocation-free in either
  orientation.
- Preserve O(log(min(n, m))) concat even when operands have mixed orientations.
- Keep forward-only use reasonably fast by checking the common orientation path first (the forward path must avoid
  the reversed-accessor allocation entirely).

### Derived Sorted Collections

`sorted_bag`, `sorted_set`, and `sorted_map` should be wrappers around the general measured tree with
order-statistic measures:

- The order-statistic measure is **comparator-independent** (its `combine` is `(count₁+count₂, last-key)`), so the
  comparer lives **only in the query predicates**, not in the measure. Store the comparer on the wrapper; a
  template `Compare = std::less<T>` covers the common case, but to match C#'s runtime-chosen order (including a
  closure comparator) allow a runtime comparator captured by the predicate object *without changing the measure
  type* — which keeps structural sharing valid across differently-ordered instances of the same static type.
- Enforce sortedness by construction, and pin the observable tie-break/dedup invariants: `sorted_bag` bulk-build
  must use `std::stable_sort` (not `std::sort`) so equal elements keep input order, matching incremental add
  (which inserts *after* equal elements); `sorted_set` bulk-build keeps the first of each equal run; `sorted_map`
  bulk-build is stable-sort + last-wins per equal-key run. Provide three distinct insert variants: `add` (throw on
  existing key), `try_add` (no-op, returns false), `set_item` (replace).
- Use locate/split predicates for membership, rank, neighbor, add, and remove.
- Implement set algebra through linear sorted merges. Note the C# merge fully *materializes* both operands (its
  enumerator eagerly flattens to a list) and rebuilds the result by repeated append — it shares **nothing**. A
  sharing merge would be a C++ improvement, not a fidelity requirement.
- Preserve structural sharing for the single-element and range operations (add, remove, range), which the C#
  implements via split+concat — not for set algebra (see above).

For C++, comparator equality is not generally decidable. Operations combining two sorted collections should assume
the same comparator type and semantically equivalent comparator state, matching normal ordered-container practice.
Where comparator mismatch is possible, document it as a precondition.

### Priority Queue

`priority_queue<T, Priority, Compare>` should use a count-plus-min measure:

- Entries store **only** the element and its priority — **no insertion ordinal**. The measure is exactly
  `(count, min-priority)`. Stable FIFO ordering among entries of equal least priority is *emergent*: entries are
  kept in insertion order (push is a plain append), and `try_peek`/`try_pop` locate the *leftmost* entry whose
  accumulated prefix-min reaches the tree minimum. Do **not** add an ordinal — folding one into the measure would
  change the monoid and the O(1) `peek_priority`, and an ordinal that merely sits in the entry is dead weight.
- `push` appends to the underlying measured tree.
- `peek_priority` reads the measure in O(1) amortized.
- `try_peek` and `try_pop` locate the first (leftmost) entry matching the minimum priority.
- `meld` is tree concatenation, preserving O(log(min(n, m))) behavior.
- The count-plus-min measure selects the `Compare`-minimum priority as the front, so `std::greater<Priority>`
  yields a max-priority queue. `Compare` here is an *ingredient of the measure monoid* (`combine` computes the
  running min), so it must be a compile-time, stateless, default-constructible policy, identical in type on both
  operands of `meld`. This is a deliberate enhancement over the C#, which hard-wires the default comparer and
  obtains a max-queue only by reversing the priority type's natural order.

Unlike `std::priority_queue`, this collection is persistent and meldable. Benchmarks should call that out rather
than pretending the standard heap has the same contract.

### Interval Tree

`interval_tree<T, Compare>` should port the full Hinze-Paterson interval tree:

- Store closed intervals in low-endpoint order.
- Measure count, right-biased last low endpoint, and maximum high endpoint. The max-high component is computed by
  comparing endpoints, so — like the priority queue — **`Compare` is part of the measure monoid**, not just the
  query predicates. Bake `Compare` into a compile-time `interval_measure<T, Compare>` used identically by
  `combine` and by the max-high / last-low split/stabbing predicates; do not thread a runtime comparator.
- Use maximum-high predicates for logarithmic overlap/stabbing queries.
- Preserve duplicate interval values unless an operation explicitly removes one occurrence. `contains`/`remove`
  match by the comparator (`compare(a,b)==0`) on *both* endpoints — not `operator==` — splitting on the low
  endpoint in O(log n) then scanning the equal-low run left-to-right to remove the *first* comparator-equal match
  (cost adds linear-in-equal-lows). Preserve this leftmost-match semantics so which occurrence is removed matches
  C#.
- Implement overlap enumeration as repeated logarithmic extraction first, then consider a more specialized
  traversal if benchmarks justify it.
- Include `coalesce()`: an O(n) low-to-high merge of overlapping/touching intervals into maximal disjoint
  intervals. Intervals merely adjacent across a gap are **not** merged (an arbitrary `T` has no successor
  operation), so the merge compares `interval.low <= current.high` and must not assume adjacency.

### Rope And Measured Rope

`rope<T>` should be a chunked persistent sequence over `finger_tree<chunk<T>, chunk_length_measure<T>>`:

- Chunk storage is `std::shared_ptr<const T[]>` (or `shared_ptr<const std::vector<T>>`) plus offset/length;
  slicing shares the backing allocation in O(1) via the aliasing constructor (mirroring C#'s `ReadOnlyMemory<T>`).
- Reads and splits locate chunks by cumulative length.
- Edits copy only the touched bounded-size chunk plus the tree path; copy-on-write and `compact()` must allocate a
  **fresh** buffer, never resize or overwrite a buffer that may still be shared (this preserves the tear-free
  invariant — see Lazy Memoization And Thread Safety).
- Slices share backing chunk storage, so a tiny slice **pins its entire backing buffer** until rebuilt — this is
  the reason `compact()` exists. A slice far smaller than its backing buffer may warrant an eager copy.
- `from_chunks` (wrapping caller storage without copying) must take ownership of `shared_ptr<const T[]>` under a
  documented no-external-mutation precondition, and must **not** accept a bare `std::span`/pointer for retained
  storage (the persistent rope outlives the call — dangling — and `span` gives no immutability guarantee). Provide
  a copying `from_span`/`create` overload as the safe default.
- `compact()` can rebuild chunks to release retained oversized backing buffers.

`measured_rope<T, MeasurePolicy>` should store chunks with both length and user measure:

- Tree measure is `measure_pair<std::size_t, user_measure>`.
- Positional operations split on the count component.
- Measure navigation splits on the user-measure component. It is **O(log n) tree descent plus a bounded in-chunk
  linear scan**: a measured chunk's cached *total* user measure is **not** prefix-summable, so after the tree
  split isolates the boundary chunk the implementation must accumulate the user measure element-by-element across
  the chunk prefix to find the exact boundary element and the measure strictly before it. Do not stop the split at
  chunk granularity (correctness, not just constant factor).
- `prefix_measure(count)` computes the user measure of a positional prefix.

Text-navigation extras (Unicode-scalar/grapheme indexing, newline-style detection, text-reader adapters) are a
non-goal for the initial port. But `newline_measure` is a *trivial in-scope monoid policy*, so the char/text
builder (the C# `RopeBuilder` is text-bound, not a generic `rope_builder<T>`) stays in scope: port it with
`to_rope() -> rope<char>` and `to_text_rope() -> measured_rope<char, newline_measure>`. The core rope itself stays
element-agnostic.

## Lazy Memoization And Thread Safety

The C# implementation's key strict-language move is a shared memoized suspension for each deep middle. The C++
port needs the same idea to keep amortized bounds meaningful under branching persistence. This is the **highest-risk
area of the port**, because the C# correctness rests on CLR guarantees that do not exist in C++. The full rationale
(with `file:line` evidence) is in editorial notes §1; the requirements below are load-bearing.

This applies to `finger_tree` and `persistent_deque` only — the reversible core is strict and has no suspensions.

**There are two independent memoization cells per general-tree deep node**, each with its own one-shot publication:
the lazy **middle subtree**, and the deep node's lazily memoized **combined measure**. The tuned deque has only
the middle cell (its size is eager and invertible — no measure box). Port both general-tree cells.

Recommended design for the lazy middle:

- Represent it as a small shared cell holding either a pending operation object or a computed tree.
- Forcing computes the result outside a lock where practical, then publishes it with a compare-exchange.
- Concurrent readers may duplicate bounded pending work, but all must converge on one published result.
- Pending operations must be immutable and pure.
- A new suspension must capture an *already-forced* source and defer *exactly one* push or pop, matching the C#
  discipline that prevents unbounded chains of nested suspensions.
- The recommended cell is a `std::atomic<std::shared_ptr<const state_base>>` (or `shared_ptr<const state_base>`
  accessed only via `std::atomic_load`/`atomic_store`) holding either the pending op or the computed result.
  Prefer this over `std::once_flag` + a stored callable: only the compare-exchange cell can **drop the pending
  operation and the already-forced `source` subtree it captured** on publication, matching the C# property that
  overwriting `_state` makes them collectable. A `once_flag` design pins the captured source for the cell's
  lifetime — a real space leak under branching persistence. `std::call_once` is therefore *rejected* for the
  memoized middle, not a co-equal alternative.

Publishing the memoized **measure** (the second cell) needs special care because a measure is an arbitrary user
value type:

- C# stores it *boxed* (`object?`) so an arbitrarily large measure — e.g. a 32-byte struct used as both element
  and measure in the stress suite — is published by a **single atomic reference write** and can never tear. C++
  has no boxing.
- Do **not** use `std::atomic<measure_type>`: for a non-trivially-lock-free measure it is either ill-formed (not
  trivially copyable) or silently not lock-free (a hidden mutex). Do **not** use a non-atomically-written
  `mutable measure_type` read concurrently: that is a data race and tears for any measure wider than a lock-free
  word.
- Publish the measure as a **pointer**, fully constructing it first:
  `std::atomic<std::shared_ptr<const measure_type>>` with `nullptr` as the "not computed" sentinel (the analogue
  of C#'s `null` box). The push-vs-pop asymmetry from the General Measured Tree section governs *when* this cell is
  populated.

Two more rules that span both cells and the publication sample:

- **A plain `std::shared_ptr` is not safe to race on.** Its refcount is thread-safe, but the `shared_ptr` *object*
  is not atomically readable while another thread reassigns it. Every `shared_ptr` that is concurrently
  published-and-read — the two memoization cells *and* the single publication cell in `persistent_snapshots` /
  the concurrency tests (the C++ analogue of C#'s `Volatile.Write`/`Volatile.Read`) — must be
  `std::atomic<std::shared_ptr<T>>` or accessed only via `std::atomic_load`/`atomic_store`. A plain field is safe
  to copy concurrently only after a happens-before publication, never during reassignment.
- **Memory ordering must be acquire/release (or seq_cst), never relaxed.** The fast-path load and the publishing
  CAS/store must form an acquire/release pair so the fully-constructed pointee is visible before its pointer;
  `std::atomic`'s seq_cst defaults are correct. In particular, do not let the fast-path read degrade to a relaxed
  load — that drops the acquire edge even when the CAS is seq_cst.

**Tear-free invariant (state it in the headers and test it).** All node, chunk, and measure storage is fully
written before its owning `shared_ptr` is observable, and is never mutated after publication; each new version is
published through a single atomic pointer write. This is what makes large-value-type elements and measures
tear-free; `compact()` and copy-on-write must allocate fresh buffers. Keep the public API surface immutable
regardless of the internal memoization.

## Implementation Milestones

### Milestone 1: Build Skeleton

Create the C++ workspace:

- `Cpp/FingerTree/CMakeLists.txt`
- `CMakePresets.json`
- `vcpkg.json`
- basic README and docs index
- one empty library target
- one smoke-test target
- test-support instrumentation used from Milestone 3 on: a counting global `operator new`/`delete` (or an
  instrumented allocator) and an operation/comparison counter, so the complexity-guard tests can observe
  allocation and comparer-call counts

Validation:

- Configure and build with the Visual Studio CMake and Ninja tools.
- Run an empty or trivial test target through CTest.

### Milestone 2: Measure Infrastructure

Implement:

- measure and monoid concepts
- comparison policies (including the static comparison policy and a default/reverse adapter)
- optional measure carrier (pin its contracts: `none` is the monoid identity; equal-extremum tie-break is
  deterministic — max keeps the earlier element, min the earlier — so extremum extraction is stable)
- size, min, max, key, order-statistic, sum, and product measures
- predicate helpers for count, key, optional extremum, sum threshold, and product projection (lower vs upper bound
  are a strict `>` vs non-strict `>=` pair of distinct predicates)
- the named-operation free-function layer over the primitives (peek/extract max/min, lower/upper-bound split,
  split-at-index, cumulative-weight split/select, component-projecting product splits/finds)

The `sum_measure` element-type contract guarantees the built-in arithmetic types via an addable-monoid concept
(plus ordering for split/select). `System.Decimal` has no standard C++ equivalent and is out of scope; `BigInteger`
coverage is test-only via Boost.Multiprecision when vcpkg is available.

Validation:

- Compile-time concept tests.
- Monoid law tests for representative values.
- Predicate behavior tests against simple vectors.
- Named-operation tests (and zero-allocation guards on the struct-predicate fast path).

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
- A `try_locate` vs `try_split_find` equivalence test across **every** boundary threshold (including the
  no-boundary, empty, and single cases) on both a `size_measure` tree and a non-group order-statistic/key-measure
  tree: assert the same boundary element and the same measure-before from both paths.
- Branching persistence tests that reuse retained versions, plus a **size-independent marginal-cost** guard
  (replay an endpoint/near-end op on one retained version across two sizes; assert flat marginal allocation) and an
  **endpoint-no-force** guard (`front`/`back`/endpoint-index reads on an unforced spine allocate nothing).
- A **tearable concurrent-first-read** test (the C++ analogue of the C# `TearableConcurrencyStressTests`): a
  multi-word struct with an `is_intact()` tear detector used as **both element and measure**, with (a) many-thread
  reads of one immutable tree, (b) concurrent *first* reads of a fresh, never-forced tree validating the forced
  spine measure **and** every element are intact (this exercises the atomic measure-box publication), (c) lock-free
  single-producer/multi-consumer publication over an atomic `shared_ptr`, and (d) a branching+reading soak off a
  retained base validated against a model.
- A stateful/command-sequence model test that generates *operation sequences* and shrinks to a minimal failing
  program (the analogue of the C# `ModelBasedCommandTests`), replayed against a `std::vector` model.

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
- Randomized model histories, plus a stateful command-sequence model test that shrinks to a minimal failing
  program (analogue of the C# `ModelBasedCommandTests`).
- Invariant validation after every randomized operation.
- Complexity guard tests (non-optional) for comparer-call counts on near-end sorted search, endpoint allocation
  shape, endpoint-no-force, and size-independent marginal cost on a retained version.

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
- Verify the construction/insert invariants: `sorted_bag` stable order for equal elements (`std::stable_sort`),
  `sorted_set` keeps-first, `sorted_map` last-wins, and the three distinct insert variants; priority-queue stable
  FIFO among equal minima; interval-tree `coalesce` and leftmost-match duplicate removal.
- Add persistence tests over retained versions, plus a stateful command-sequence model test (analogue of the C#
  `ModelBasedCommandTests`, which covers the sorted set).
- Add allocation/operation-count guards for the hot locate paths (the C++ analogue of the C# zero-closure /
  allocation-free read tests).

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
- Randomized histories interleaving reverse with every operation, plus a stateful command-sequence model test.
- An O(1)-`reverse` guard: assert `reverse()` performs O(1) allocations and node touches independent of n (it must
  share, not deep-copy, children).
- Expect O(log n) worst-case endpoints; do **not** add a branching-persistence amortization guard here (the
  reversible core is strict, so O(1)-amortized endpoints hold only for linear use).

### Milestone 7: Rope Family

Implement:

- `rope<T>`
- `measured_rope<T, MeasurePolicy>`
- a char/text builder mirroring the C# `RopeBuilder` (fluent char/string/span appends, `append_line`,
  `to_rope() -> rope<char>`, `to_text_rope() -> measured_rope<char, newline_measure>`) — there is no generic
  `rope_builder<T>` in the C# to port

Validation:

- Construction from spans, ranges, chunks.
- Indexed reads and edits.
- Split/slice/concat laws.
- Chunk invariant checks, including the slice-aliasing/`compact()` buffer-release behavior.
- Large mid-splice tests.
- Measured-rope prefix and measure-split tests against brute-force models, plus a stateful command-sequence model
  test (analogue of the C# `ModelBasedCommandTests`, which covers rope and measured rope).
- A tearable concurrent-read test over a `rope`/`measured_rope` of a multi-word struct.

### Milestone 8: Samples And Benchmarks

Add samples (the C# ships three — Tour, Showcase, Editor; Editor is dropped because the editor-grade text
extras are out of scope):

- `showcase.cpp`: priority queue, sorted collections, interval queries, weighted selection, reversible deque.
- `persistent_snapshots.cpp`: concretely the C# **Tour** — a `rope<char>`/`measured_rope<char, newline_measure>`
  text buffer demonstrating (a) undo/redo as a cursor over O(1) structurally-shared snapshots, (b) lock-free
  single-writer/concurrent-reader snapshot publication over an atomic `shared_ptr`, and (c) O(log n) line/column
  navigation via the newline measure. (Not element-agnostic — rope/measured_rope are in scope.)
- Each sample exposes a testable `run(std::ostream&)` seam with `main()` a thin wrapper over `std::cout`, and is
  deterministic, so the samples can be smoke-tested.

Add benchmarks:

- deque endpoint and index operations
- concat
- sorted search
- priority-queue meld
- weighted selection
- rope insert/split/slice
- reversible reverse
- **persistence/branching-flatness**: repeatedly branch an endpoint op off one retained, fully-forced version and
  assert flat time/allocation across sizes (100 / 10k / 1M) — the empirical guard for the amortized-under-branching
  claim, and the highest-value benchmark for the riskiest part of the port
- **measured-rope navigation**: offset↔line (and measure-split) in O(log n) vs a linear newline scan
- **reversible overhead**: `reversible_deque` endpoint/concat cost vs `persistent_deque`, justifying the separate
  type
- **interval-tree queries**: stabbing / overlap enumeration

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
- Run the sample smoke tests (each sample's `run()` captured into a string and checked against expected
  deterministic transcript markers) so the samples cannot silently rot.
- Run static analysis available from MSVC where practical.

## Documentation Policy

Keep the C++ docs current-state oriented:

- `README.md` should explain the workspace and build commands.
- `docs/api-notes.md` should document the C++ API shape and differences from C#.
- `docs/validation.md` should record how to run tests, stress tests, and benchmarks.
- Public headers should document contracts, persistence, complexity, exception behavior, and iterator invalidation
  semantics.

Avoid promising hard worst-case endpoint updates — and, for the general measured tree, hard worst-case
`measure()` — where the implementation only proves persistent amortized bounds. (`front`/`back` are O(1)
worst-case and never force; the general tree's `measure()` is O(1) *amortized* only, because a pop suspension must
force. The reversible deque's amortized endpoint bound is for single-threaded linear use only.) The C#
documentation is unusually careful here; the C++ docs should be just as explicit.

## Open Design Questions

- Internal representation: **resolved** — the level-deepening must be type-erased behind a single erased child
  type (see General Measured Tree); this is required for compilation, not a tuning choice. A `std::variant` may
  discriminate the empty/single/deep shape at a fixed level but cannot replace the erasure (a `variant` naming the
  next level's node type is the same non-terminating instantiation chain). The remaining open question is only the
  erased base's concrete shape (polymorphic class vs. fixed-arity tagged node), to be settled by profiling.
- Lazy cell implementation: **resolved** — use a compare-exchange `std::shared_ptr<const state_base>` cell, not
  `std::call_once`, because only it releases the pending op and its captured forced source on publication (see
  Lazy Memoization And Thread Safety).
- Iterator category: forward iterators, but keep O(log n) `operator[]`/`at` for the indexable types (iterator
  category is independent of providing an indexer). Decide per family whether `begin()` walks the spine lazily (a
  hand-written O(log n) frame stack — only the deque does this in C#) or returns iterators over a materialized
  buffer (what the general tree and derived collections do in C# today), and document the allocation/complexity
  contract either way.
- Public allocator support: defer until the core invariants are stable. Allocator-aware persistent trees are
  possible, but the interaction with shared immutable nodes and lazy cells deserves a separate design pass.
- Exception guarantees: document and test the intended guarantee for comparers, copy/move construction, and
  allocation failure. The natural target is strong exception safety for public operations when element operations
  meet the standard container requirements.
