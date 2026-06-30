# C++ FingerTree Implementation Notes

- Status: Living implementation notes
- Created (UTC): 2026-06-30T17:20:17Z
- Repository HEAD: d140fb07d8ae21726e96b9ad916154c3bf87411d
- Audience: Maintainers and AI agents implementing or reviewing the C++ port
- Scope: Implementation decisions, C# parity checks, justified divergences, validation observations, and defect-report links

This document records concrete implementation notes for the C++ port as it is built. It is intentionally more
operational than the port plan: each entry should say what C# source or tests were compared, what C++ choice was
made, why that choice preserves or improves semantics/performance/quality, and what validation covered it.

If a C# defect, flaw, or improvement opportunity is found during the port, write a separate `*.md` report or
proposal and link it from this document. Do not bury those findings in commit messages or chat.

## Checkpoint: Workspace Skeleton

Compared material:

- `Cpp/FingerTree/docs/port-plan.md`
- `Cpp/FingerTree/docs/port-plan-editorial-notes.md`
- `README.md`
- `docs/agent-workflows.md`

Implemented:

- Header-first CMake workspace rooted at `Cpp/FingerTree`.
- Public aggregate include `tools/data_structures/finger_tree/finger_tree.hpp`.
- CTest smoke executable with a tiny local runner.
- Test-support allocation counter, operation/comparison counter, deterministic RNG, and command-sequence scaffold.
- Visual Studio/Ninja presets using absolute paths for bundled CMake/Ninja tools.

Justified divergences:

- The plan originally described CMake `CXX_STANDARD 26` for MSVC `/std:c++latest`. The bundled CMake 4.2 rejects
  `CXX_STANDARD 26` for the installed MSVC 19.50 compiler, while the compiler itself supports `/std:c++latest`.
  The workspace therefore models targets as `CXX_STANDARD 23` in CMake and adds `/std:c++latest` explicitly for
  MSVC targets. This is a build-system compatibility choice, not a library semantic downgrade.
- The bootstrap test layer does not introduce Catch2 yet. That keeps the initial workspace buildable before
  vcpkg is intentionally bootstrapped. The local runner is deliberately small and can coexist with Catch2 later.

Validation:

- Configured with the bundled Visual Studio CMake after initializing the shell through `VsDevCmd.bat`.
- Built `msvc-debug`.
- Ran `ctest --preset msvc-debug`; the smoke target passed.

## Checkpoint: Measure Infrastructure

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/Measures.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Comparisons.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/SumMeasure.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/ProductMeasure.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/MeasurePredicate.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/MeasurePredicates.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/PriorityQueue.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/IntervalTree.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/BuiltInMeasureTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/SumMeasureTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ProductMeasureTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/CustomComparisonMeasureTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/PriorityQueueTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/IntervalTreeTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ZeroClosureNamedOpTests.cs`

Implemented:

- `monoid_policy` and `measure_policy` concepts.
- Static comparison policies: natural/default order, reverse order, and adapter support for `std::less`-style
  comparators.
- Structural measure carriers: `optional_measure`, `ranked_key`, `measure_pair`, `priority_entry`,
  `priority_aggregate`, `interval`, and `interval_annotation`.
- Measure policies: size, sum, max, min, last-key, order-statistic, product, priority, and interval.
- Value-type predicate objects corresponding to the C# zero-closure predicates: rank/count predicates,
  lower/upper-bound key predicates, optional extremum predicates, priority-front, interval signpost predicates,
  sum-threshold predicates, and product-component projectors.

Parity notes:

- `optional_measure<T>` mirrors C# `Optional<T>`: absence is the monoid identity; `some` carries a value;
  structural equality is defined for the carrier, not for containers.
- Max/min measures keep the earlier element when the comparison reports equality. This preserves the C# stable
  extremum behavior and is required for FIFO equal-priority extraction once the tree engine arrives.
- Key and order-statistic measures are right-biased for their key component, matching C# lower/upper-bound
  semantics over sorted sequences.
- Product measures combine component-wise and inherit monoid laws from their operands.
- Priority and interval measures bake the comparison into the measure policy, matching the C# distinction between
  runtime comparators for sorted wrappers and measure-time comparison for priority/interval signposts.
- The predicate tests emulate prefix accumulation directly, so lower/upper-bound strictness and cumulative-weight
  threshold behavior are validated before the tree engine exists.

Justified divergences:

- Counts and ranks use `std::size_t` rather than C# `int`. This follows the port plan and avoids C#'s mixed
  wrap/throw count-overflow behavior. The C++ contract treats count overflow as a precondition violation; helper
  `checked_add` guards measure-combine arithmetic for unsigned counts.
- `sum_measure<T>` uses `T{}` as the additive identity and requires `T + T -> T`. That covers built-in arithmetic
  and user numeric types without taking a dependency. C# `decimal` and `BigInteger` are not specially modeled in
  the core; Boost.Multiprecision coverage remains a later test-only option if vcpkg is introduced.
- Runtime predicate comparators use `std::less`-style callables, while static measure comparators expose a
  three-way-sign `compare` member. This gives C++ call sites idiomatic runtime comparator storage without losing
  the static comparison regime needed by max/min/priority/interval measures.

Validation:

- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug`; the smoke-and-measure test executable passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release`; the same smoke-and-measure test executable passed.
- Allocation bracketing confirmed product-component predicate evaluation performs no heap allocation.

Findings:

- No runtime C# defect was found in the measure and predicate layer.
- A narrow C# documentation wording defect was recorded in
  [`FingerTree/docs/interval-tree-strict-core-wording-defect-report.md`](../../../FingerTree/docs/interval-tree-strict-core-wording-defect-report.md):
  `IntervalTree<T>` refers to the underlying measured core as "strict" where the repository docs and implementation
  describe the lazy-memoized measured finger tree.

## Checkpoint: Atomic Lazy Cell

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/MiddleTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs`
- `Cpp/FingerTree/docs/port-plan.md`
- `Cpp/FingerTree/docs/port-plan-editorial-notes.md`

Compared C# tests and examples:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/MeasuredFingerTreePersistenceTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/PersistenceConcurrencyExamplesTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/TearableConcurrencyStressTests.cs`

Implemented:

- `detail::lazy_cell<T>`, a copyable shared handle around an atomic `std::shared_ptr<const state_base>`.
- Eager/computed construction from a value or a prebuilt `std::shared_ptr<const T>`.
- Deferred construction from a pure factory returning either `T` or `std::shared_ptr<const T>`.
- First-force publication through compare-exchange. Racing first readers may duplicate bounded factory work, but
  all converge on the single published value state.
- Retry-after-exception behavior: a throwing factory leaves the cell pending, matching the C# lazy middle's strong
  exception-safety shape.
- Pending-state release after publication: replacing the pending state with the computed state releases captured
  source structures once the forcing call returns, avoiding the `std::call_once` retention problem called out in
  the port plan.

Parity notes:

- This primitive mirrors the C# `LazyMiddle<T, TChild>._state` and `LazyMeasuredMiddle<...>._state` shape: one
  shared cell holds either a pending operation object or a computed result, and compare-exchange publishes the
  computed result.
- The cell deliberately does not use `std::once_flag`; the pending operation object is dropped after successful
  publication, preserving the C# space behavior where `_state` no longer references the pending operation or the
  already-forced source tree it captured.
- `std::atomic<std::shared_ptr<...>>` is used for the concurrently read/written state pointer. This is the C++
  analogue of C# `Volatile.Read` plus `Interlocked.CompareExchange` on an object reference; plain `shared_ptr`
  reassignment would be a data race.

Remaining engine work:

- The general measured tree also needs a separate atomic pointer-published measure box for deep-node measures.
  `lazy_cell<T>` can inform that implementation, but the measure box has slightly different construction timing
  because push middles can report a measure without forcing while pop middles cannot.
- The lazy cell is an internal primitive only. It does not by itself implement the one-operation-deep discipline;
  the measured-tree and deque constructors must still force captured source middles before creating pending
  push/pop operations, just as the C# code does.

Validation:

- Added tests for eager/computed cells, null computed-pointer rejection, quiescent one-shot forcing, exception
  retry, copied-cell sharing, release of captured pending state after publication, and concurrent first-force
  convergence.
- Built `msvc-debug` and ran `ctest --preset msvc-debug`.
- Built `msvc-release` and ran `ctest --preset msvc-release`.

## Checkpoint: Atomic Measure Box

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/TearableConcurrencyStressTests.cs`
- `Cpp/FingerTree/docs/port-plan-editorial-notes.md`

Implemented:

- `detail::atomic_box<T>`, a one-shot atomic publication cell for lazily computed values.
- Null `std::shared_ptr<const T>` is the "not computed" sentinel, matching C#'s `object? _measureBox` null state.
- `get_or_compute` first loads the published pointer, computes a fully initialized value outside the atomic slot,
  then publishes the pointer with compare-exchange.
- The factory may return either `T` or `std::shared_ptr<const T>`.
- Exceptions and null factory results do not publish anything; a later read can retry from the empty state.

Parity notes:

- This is the C++ analogue of the boxed deep-node measure in `DeepMeasuredTree`: the measure is published as a
  pointer, not as a racing non-atomic `T` write and not as `std::atomic<T>`.
- Publishing a pointer preserves the C# tear-free property for large measure values. The tests use a four-word
  `wide_value` whose fields must agree, so concurrent first publication validates that readers observe a fully
  constructed value.
- The primitive is separate from `lazy_cell<T>` because the measured tree has two independent cells per deep node:
  one for the middle suspension and one for the combined measure.

Validation:

- Added tests for empty/quiescent publication, prebuilt pointer publication, null result rejection, exception
  retry, and concurrent publication of intact wide values.
- Built `msvc-debug` and ran `ctest --preset msvc-debug`.
- Built `msvc-release` and ran `ctest --preset msvc-release`.

## Checkpoint: Measured Lazy Cell

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs`
- `Cpp/FingerTree/docs/port-plan.md`
- `Cpp/FingerTree/docs/port-plan-editorial-notes.md`

Implemented:

- `detail::measured_lazy_cell<Tree>`, a measured-middle publication primitive with the same computed-or-pending
  atomic state shape as `detail::lazy_cell`.
- `measure()` checks the computed state first, then asks the pending operation for an optional measure without
  forcing, and only forces the tree if the pending operation cannot answer.
- Pending factories may return either a `Tree` value or `std::shared_ptr<const Tree>`.
- `defer_force_only` models pop-like pending operations whose measure cannot be recovered without forcing.

Parity notes:

- This captures the C# push-versus-pop asymmetry directly. `PendingMeasuredPushFront` and
  `PendingMeasuredPushBack` can report their resulting middle measure arithmetically from the forced source and
  pushed node, while `PendingMeasuredPopFront` and `PendingMeasuredPopBack` return false and force.
- A successful force replaces the pending state with a computed tree state, so pending operations and captured
  sources are released just as in the C# compare-exchange cells.
- The primitive is still deliberately lower-level than the final measured tree. It enforces publication semantics
  and the measure-probe protocol; the tree constructors still have to preserve the one-operation-deep suspension
  discipline.

Validation:

- Added tests for computed measure reads, null computed tree rejection, push-like measure probes that avoid
  forcing, force-only measure reads that publish the tree, retry after force-time exceptions, and captured-state
  release after force.
- Built `msvc-debug` and ran `ctest --preset msvc-debug`.
- Built `msvc-release` and ran `ctest --preset msvc-release`.

## Checkpoint: Persistent Deque Core

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/FingerTreeDequeResults.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/TreeElement.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Digit.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Node.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Tree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/MiddleTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/TreeOperations.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeEndpointOperationTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeBranchingPersistenceTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeIndexingAndSplitTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeEnumerationAndCopyTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeSortedSearchTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeSortedSearchEdgeTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeComplexityGuardTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeRandomizedModelTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ModelBasedCommandTests.cs`

Implemented:

- `persistent_deque<T>`, a public immutable deque facade with C++ names for the C# operations:
  endpoint reads and updates, indexed reads/replacement/update, insertion/removal, range extraction,
  split-at/index/range results, concatenation, sorted lower/upper-bound search, deterministic duplicate
  binary search, sorted insertion/removal, vector materialization, iterator traversal, and invariant validation.
- A tuned internal 2-3 finger-tree engine in `detail/deque_tree.hpp`:
  zero-through-three transient digits, one-through-three stored digits, two/three-child nodes, cached leaf counts,
  cached rightmost-leaf signposts, empty/single/deep tree levels, and lazy memoized middle subtrees.
- Endpoint overflow and repair rules matching the C# implementation:
  `Cons`/`Snoc` over a three-child digit leaves a safe two-child digit and suspends a `Node2` push into a forced
  middle; removal from a one-child digit pulls from the middle, using the `Node3` chop fast path and deferring
  only the recursive `Node2` pop.
- Level-spanning construction and concatenation helpers matching `TreeOperations`:
  `FromDigit`, `FromPartialDigit`, `DeepLeft`, `DeepRight`, `PullLeft`, `PullRight`, and `Concat`/`Glue` with the
  same two-through-nine element chunking rule where two- and four-element remainders become `Node2` chunks.
- A stack iterator that traverses tree and node blocks left-to-right without flattening the collection first.

Parity notes:

- Leaves are stored inline inside digits and nodes through a value wrapper, not as separately allocated heap
  objects. That preserves the important C# `Leaf<T>` property: endpoint-heavy workloads do not allocate one
  object per element merely to represent leaves.
- Internal `Node2`/`Node3` and tree-level reps are heap objects shared by `std::shared_ptr<const ...>`, matching
  the C# design where nodes and tree levels are immutable reference objects shared by persistent versions.
- The middle subtree uses the previously implemented `detail::lazy_cell<deque_tree<T>>`, so pending operations
  are atomically published, memoized, and released after first force. This is the C++ analogue of C# `LazyMiddle`
  with `Volatile.Read` and `Interlocked.CompareExchange`.
- The C# implementation encodes element height with polymorphic recursion (`Tree<T, TChild>` and
  `Node<T, TChild>`). C++ cannot express that infinite type family directly in one public template, so the port
  uses type-erased `deque_element<T>` values whose node case points to a node containing children one level closer
  to the leaves. Construction sites preserve the same height discipline; invariant validation recomputes sizes and
  signposts.
- Sorted lower/upper-bound search uses the cached rightmost-leaf signpost exactly like C# `BoundIndex`. The C++
  API accepts `std::less`-style callables rather than `IComparer<T>`.

Justified divergences:

- Counts and indices use `std::size_t`; C# uses `int` and throws before exceeding `Int32.MaxValue`. This follows the
  port-wide count policy already chosen for measures and avoids baking a 32-bit limit into the native container.
  Arithmetic still uses checked unsigned addition on structural cached sizes.
- Public names follow C++ container conventions (`size`, `empty`, `front`, `back`, `at`, `push_front`,
  `push_back`) while retaining C#-recognizable operation families (`set_item`, `insert_at`, `split_at`,
  `sorted_lower_bound`, etc.). The semantic operations and result shapes are the same.
- `try_front`, `try_back`, and `try_get` return pointers rather than copying into out parameters. This avoids
  unnecessary copies for large `T` and is idiomatic for nullable read probes in C++.
- `sorted_binary_search` returns `std::ptrdiff_t` so it can preserve the C# bitwise-complement insertion-index
  convention without narrowing every native index to `int`.
- Concat uses small `std::vector<deque_element<T>>` buffers for the two-through-nine combined elements and up to
  three carried nodes. The C# code allocates short arrays for the same buffers; this is the same asymptotic shape
  and limited to the logarithmic concat descent.

Validation:

- Added deterministic tests for construction, endpoint persistence, empty behavior, indexing, replacement,
  updater calls, insertion/removal, range extraction, split reconstruction, concat, range insertion/appending,
  iterator/copy order, sorted lower/upper-bound semantics, deterministic duplicate binary search, sorted equal
  range splitting, sorted insertion/removal, and a comparer-call guard over signpost-guided search.
- Added a bounded randomized model replay that interleaves endpoint operations, indexed updates, insertion,
  removal, split reconstruction, retained-version branching, and concat while validating invariants after each
  operation and rechecking retained versions at the end.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- No C# defect, flaw, or improvement proposal was found in this deque-core pass. The C# implementation comments for
  overflow, pull/chop, one-operation-deep suspension, cached size, cached rightmost signposts, split, concat, and
  enumeration matched the implementation behavior reviewed for this checkpoint.

## Checkpoint: General Measured Finger Tree Core

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/FingerTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/MeasurePredicate.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredElements.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/MeasuredFingerTreeTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/MeasuredFingerTreePersistenceTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/TryLocateTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/AllocationFreeReadTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ModelBasedCommandTests.cs`

Implemented:

- `finger_tree<T, MeasurePolicy>`, the public general measured finger tree facade using the C++ static measure
  policy shape already implemented in the measure checkpoint.
- `detail::measured_tree<T, MeasurePolicy>`, a type-erased 1-through-4 digit measured finger tree following the
  Hinze-Paterson general core: empty/single/deep levels, two/three-child measured nodes, measured leaves, cached
  element/node measures, lazy middle subtrees, and lazy deep-node measure boxes.
- Endpoint operations matching the C# general measured core:
  prefix/suffix room extends the digit; a four-child prefix/suffix overflow forces the source middle and suspends
  a `Node3` push; a one-child underflow pulls a node from a forced middle and suspends the corresponding pop.
- General concat (`app3`/`Glue`) with the same 1..4 digit and 2/3-node chunking rule as C#:
  chunk triples while more than four elements remain, then use Node2 for a two-element remainder, Node3 for three,
  and two Node2 chunks for four.
- Measure-guided `split`, `try_split_find`, and `try_locate`, including the allocation-free locate shape that
  descends without reconstructing result trees.
- Lazy deep measure publication through `detail::atomic_box<TMeasure>` and lazy measured middle publication through
  `detail::measured_lazy_cell<tree>`.

Parity notes:

- The general measured tree uses one-through-four digits, unlike the tuned deque's simplified one-through-three
  digits. The C++ measured core follows the C# measured source, not the deque source.
- Push suspensions capture a forced source middle plus the pushed node and answer `measure()` from the monoidal
  combination of source measure and node measure without forcing the resulting structure. Pop suspensions use
  `defer_force_only`, matching C#'s non-group monoid behavior where a pop measure cannot be recovered by
  subtraction.
- Deep node measure is computed lazily and published as a pointer through `atomic_box`, preserving the C# boxed
  measure cell's tear-free publication behavior for large measure values.
- Leaves are value-stored with their measure cached once at construction. Nodes are shared immutable heap objects
  with cached combined measures, matching the C# `MeasuredLeaf`/`MeasuredNode` responsibilities.
- `split` and `try_locate` use the same accumulator order as C#: test the prefix measure, then the middle measure,
  then the suffix; when descending into the middle, combine the left-middle measure before splitting the hit node's
  children.

Justified divergences:

- C# exposes `FingerTree<TElement, TMeasure, TMeasureOps>` with separate measure and operation type parameters.
  The C++ API uses `finger_tree<T, MeasurePolicy>` because every C++ measure policy already names its
  `measure_type`; this removes a redundant template argument without changing semantics.
- C# value-type predicates avoid delegate closure allocation. C++ templates over ordinary predicate callables give
  the same monomorphized descent for lambdas and predicate objects, so no separate public predicate interface is
  needed for the raw tree.
- The C++ public view/search methods return `std::optional` result structs instead of C# `bool` plus out
  parameters. This is idiomatic C++ and avoids default-output sentinel states.
- Internal prefix/suffix/node buffers use small `std::vector` values. The C# measured core uses short arrays for
  the same buffers, so this preserves the same asymptotic allocation pattern. The tuned deque remains separately
  optimized with inline digit storage because its C# source also uses inline digit structs.

Validation:

- Added tests for empty behavior, endpoint construction, positional split across every index for sizes up to 96,
  concat order/measure/associativity, max-priority extraction, count-plus-last-key indexing/lower-bound search,
  locate versus split-find across thresholds, and a bounded randomized branching history with retained versions,
  endpoint views, split/concat round-trips, and concat with retained versions.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- A C# improvement proposal was recorded in
  [`FingerTree/docs/measured-fingertree-enumerator-allocation-improvement-proposal.md`](../../../FingerTree/docs/measured-fingertree-enumerator-allocation-improvement-proposal.md).
  The C# public general measured tree enumerator materializes a `List<TElement>` before yielding; this is documented
  behavior rather than a correctness bug, but it is an allocation/performance shortcoming relative to the tuned
  deque's stack enumerator.

## Checkpoint: Priority Queue Wrapper

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/PriorityQueue.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/MeasurePredicates.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/PriorityQueueTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/BuiltInMeasureTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/CustomComparisonMeasureTests.cs`

Implemented:

- `priority_queue<T, Priority, Comparison>`, a persistent meldable min-priority queue backed by
  `finger_tree<priority_entry<T, Priority>, priority_measure<T, Priority, Comparison>>`.
- O(1) count/min-priority observation from the measured tree aggregate.
- Enqueue as append, preserving insertion order among equal priorities.
- Peek/dequeue by locating or splitting at the first prefix whose aggregate minimum reaches the whole-tree minimum.
- Meld by measured-tree concatenation.
- Reversed-priority behavior through `reverse_comparison<Priority>`.

Parity notes:

- The C# queue uses `Comparer<TPriority>.Default` inside `PriorityMeasure`; the C++ queue uses the existing static
  comparison policy parameter so priority ordering remains part of the measure type. This is the same design choice
  already used by C++ max/min/priority measures.
- Equal-priority FIFO behavior follows from two places, matching C#: `priority_measure::combine` keeps the left
  minimum on equality, and dequeue splits at the first subtree whose minimum is at most the whole-tree minimum.
- Enumeration/materialization order is insertion/tree order and intentionally not priority order, matching the C#
  documentation's unspecified enumeration order.

Justified divergences:

- Empty peek/dequeue returns `std::optional` rather than C# `bool` plus out parameters.
- `size()` returns `std::size_t`, continuing the C++ count policy chosen for the rest of the port.
- Runtime priority comparers are not accepted because the priority measure itself depends on the ordering. This
  matches the C# queue's design note that a different priority order needs a different priority type; C++ exposes
  the equivalent through a different static comparison policy.

Validation:

- Added tests for empty behavior, priority peek without removal, nondecreasing drain order against a sorted model,
  FIFO stability among equal priorities, meld, persistence after dequeue, and reversed-comparison max-queue
  behavior.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- No C# defect, flaw, or improvement proposal was found in the priority queue pass. The C# source and tests matched
  the measured-core mechanics used by the C++ wrapper: append for enqueue, stable minimum signposts, split/locate at
  the front minimum, and concat for meld.

## Checkpoint: Interval Tree Wrapper

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/IntervalTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/MeasurePredicates.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/IntervalTreeTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/BuiltInMeasureTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/CustomComparisonMeasureTests.cs`

Implemented:

- `interval_tree<T, Comparison>`, a persistent closed-interval tree backed by
  `finger_tree<interval<T>, interval_measure<T, Comparison>>`.
- Insertion by splitting at the first low endpoint greater than or equal to the inserted interval's low, appending
  the interval, and concatenating the suffix. This preserves nondecreasing low-endpoint order and mirrors the C#
  `LastLowAtLeastPredicate` path.
- Single-overlap and point-stabbing queries by locating the first prefix whose maximum high endpoint reaches the
  query low, then checking the candidate low against the query high.
- Full overlap enumeration by first discarding intervals whose low is above the query high, then repeatedly
  splitting at the next candidate whose maximum high reaches the query low. This is the C# `FindOverlaps`
  algorithm in C++ result-value form.
- Exact membership and one-item removal over duplicate low endpoints. Endpoint matching uses comparison-policy
  equality for both low and high endpoints, not structural value equality.
- Coalescing of overlapping/touching closed intervals into maximal disjoint intervals in low-endpoint order.
- Public aggregate-header and CMake/test-harness integration for the new wrapper.

Parity notes:

- The C++ interval measure carries the same three pieces of information as C#: count, right-biased last low
  endpoint, and maximum high endpoint.
- `try_find_overlap`, `try_find_containing`, `find_overlaps`, `count_overlaps`, `contains`, `try_remove`, `remove`,
  `coalesce`, and `to_vector` follow the same algorithms and observable contracts as the C# implementation.
- `contains` and `try_remove` were checked against the C# comparer-equality regression: an endpoint type can order
  by one field while value equality distinguishes another field, and the tree still finds/removes by comparer
  equality.
- `coalesce` currently materializes the interval order into a `std::vector` before the sweep. This does not regress
  relative to the current C# public interval tree, because C# `foreach` over the public general measured tree also
  materializes before yielding, as recorded in the measured-tree enumerator improvement proposal. Once the C++
  measured tree gains a streaming iterator, `coalesce` can switch to it without changing semantics.

Justified divergences:

- C# `IntervalTree<T>` uses `Comparer<T>.Default`; C++ exposes a static `Comparison` policy parameter. This is the
  same compile-time comparison regime already chosen for C++ max/min/priority/interval measures and avoids storing
  a runtime comparer in every persistent version while supporting custom endpoint orderings.
- Empty query/remove operations return `std::optional` instead of C# `bool` plus out parameters.
- Counts use `std::size_t`, continuing the port-wide count policy. The underlying interval measure uses
  `checked_add` for count accumulation.
- C++ construction supports initializer-list, iterator, and range inputs rather than C# `IEnumerable<T>` factory
  methods.
- `interval<T>::contains` and `interval<T>::overlaps` are generic over a comparison policy and default to natural
  ordering. The tree methods use the tree's `Comparison` policy internally, so custom tree orderings do not depend
  on structural equality.

Validation:

- Added tests for closed-interval containment/overlap semantics, empty-tree behavior, insertion order, single
  overlap queries versus brute force, full overlap enumeration and counts versus brute force, point stabbing,
  duplicate membership/removal, coalescing versus a sweep model, and comparer-equality behavior where value
  equality differs from endpoint ordering.
- The debug assertion reported while developing the interval wrapper was traced to a C++ test loop that used
  `index != actual.size()` starting from one. Empty overlap results made the loop enter on an empty vector. The
  loop was corrected to use `index < actual.size()`.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- No new C# defect, flaw, or improvement proposal was found in the interval tree pass. The C# implementation and
  tests matched the measured interval-tree algorithms ported here.
- The previously recorded C# wording defect for the interval tree still applies:
  [`FingerTree/docs/interval-tree-strict-core-wording-defect-report.md`](../../../FingerTree/docs/interval-tree-strict-core-wording-defect-report.md).

## Checkpoint: Non-Interactive MSVC Test Failures

Implemented:

- The native smoke-test executable now configures the MSVC debug CRT at startup so warnings, errors, asserts, and
  invalid-parameter failures report through stderr rather than modal Visual C++ Runtime Library dialogs.
- The test harness also disables the relevant Windows fault/open-file critical-error boxes and unit-buffers stdout
  and stderr, so CTest output records the last completed test before an abort.

Validation:

- Applied the staged harness-only diff to a detached temporary worktree and ran both MSVC debug and release CTest
  presets successfully before committing that checkpoint.

## Checkpoint: Sorted Collection Wrappers

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/SortedBag.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/SortedSet.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/SortedDictionary.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/MeasurePredicates.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/SortedBagTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/SortedSetTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/SortedDictionaryTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/SortedCollectionPropertyTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/AllocationFreeReadTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/DerivedCollectionPersistenceTests.cs`
- The sorted-set section of `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ModelBasedCommandTests.cs`

Implemented:

- `sorted_bag<T, Less>`, a persistent sorted multiset backed by
  `finger_tree<T, order_statistic_measure<T>>`.
- `sorted_set<T, Less>`, a persistent sorted set backed by the same order-statistic measured tree.
- `sorted_map<Key, T, Less>`, the C++ analogue of C# `SortedDictionary<TKey, TValue>`, backed by
  `finger_tree<std::pair<Key, T>, sorted_map_entry_measure<Key, T>>`.
- Runtime comparator storage through a `Less` object in each wrapper. The order-statistic measures remain
  comparison-independent and the comparator is applied only in split/locate predicates, matching the C# design.
- Sorted construction through `std::stable_sort` followed by measured-tree appends. Bag construction preserves
  comparer-equal input order; set construction keeps the first comparer-equal value; map construction keeps the
  last duplicate-key entry.
- Bag rank/count/index/range/multiplicity/removal operations.
- Set rank/index/navigation/range/update operations plus linear-merge union, intersection, difference,
  symmetric difference, subset/superset predicates, overlap, and equality checks.
- Map lookup, rank/index, set/insert/try-insert/remove, floor/ceiling/lower/higher entry navigation, key ranges,
  and key/value/entry materialization.

Parity notes:

- The bag `add` path splits at the first key above the inserted item, so new comparer-equal items are appended
  after existing equals just as in C#.
- The set `add` path splits at the first key at least equal to the inserted item and returns the unchanged set
  when that first suffix item is comparer-equal, preserving C# uniqueness by comparer equality.
- The map `set_item` path replaces a comparer-equal key in place relative to the surrounding key order; `insert`
  throws on duplicate keys and `try_insert` reports absence with `std::optional`.
- Rank and neighbor queries use `try_locate` rather than rebuilding split results, preserving the allocation-free
  read shape that motivated the C# zero-closure predicate tests.
- Set algebra currently materializes both operands into vectors before the linear merge. This is not worse than
  the current C# sorted-wrapper behavior over the public general measured tree, whose enumerator materializes
  before yielding as recorded in the measured-tree enumerator improvement proposal. Once C++ `finger_tree` has a
  streaming iterator, these merges can be switched to iterator traversal without changing public semantics.

Justified divergences:

- C# stores an `IComparer<T>` object. C++ stores a `Less` object whose type is part of the wrapper type. This gives
  the same runtime-comparator behavior for ordinary function objects while keeping the wrapper allocation-free and
  avoiding type erasure. Capturing comparers remain available by instantiating the wrapper with the comparer type.
- `sorted_map` uses C++ naming rather than `sorted_dictionary`.
- Missing ranks and navigation/lookup/update misses use `std::optional`; C# uses `-1`, `bool` plus out parameters,
  or throwing indexers depending on the member.
- Counts and ranks use `std::size_t`, continuing the C++ count policy.
- C++ exposes `to_vector`, `keys_to_vector`, and `values_to_vector` in this checkpoint instead of C# collection
  interfaces. Lazy iterators remain a follow-up on the measured-tree iterator work.

Validation:

- Added tests for bag construction/order/stable equal-key behavior, ranks, counts, indexing, one-vs-all removal,
  range extraction, descending order, and randomized multiset histories against a sorted vector model.
- Added tests for set uniqueness, indexing, optional rank results, floor/ceiling/lower/higher navigation, range
  extraction, set algebra and relation predicates against `std::set`, descending order, and randomized histories.
- Added tests for map last-wins construction, lookup, set/insert/try-insert/remove semantics, rank/index,
  floor/ceiling/lower/higher entry navigation, range extraction, descending key order, and randomized histories
  against `std::map`.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- No new C# defect, flaw, or improvement proposal was found in the sorted collection pass. The C# source and tests
  matched the measured-tree wrapper algorithms ported here.

## Checkpoint: Reversible Deque

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleElements.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ReversibleDequeTests.cs`

Implemented:

- `reversible_deque<T>`, a persistent catenable deque with O(1) whole-sequence reversal.
- A strict internal reversible finger tree in `detail::rev_tree<T>` with empty/single/deep levels, one-through-three
  prefix and suffix digits, two/three-child internal nodes, cached leaf counts, cached endpoint values, and a
  reversal bit on deep levels and grouping nodes.
- Reversal-aware endpoint reads, endpoint updates, indexed reads, indexed replacement, insertion, removal, split,
  concatenation, vector materialization, and invariant validation.
- Mixed-orientation concatenation through the same C# glue algorithm: concatenate logical suffix/middle/prefix
  bridge digits, chunk the bridge into two/three-child nodes, and recurse over the logical middles without
  materializing either operand.
- Public aggregate-header and CMake/test-harness integration for the new wrapper.

Parity notes:

- The C++ reversible core follows the C# reversible source rather than layering a mode bit on
  `persistent_deque<T>`. That preserves the deliberate strict representation and keeps `reverse()` O(1).
- `mirror()` on a deep level or node allocates only a new shallow wrapper with the reversal bit toggled. It reuses
  the same child structures, so reversal is independent of the number of stored leaves just like C# `Mirror`.
- Endpoint reads use cached first/last values and return references when the public operation can safely bind to
  storage owned by the deque. Indexed descent returns values because reversed logical child/digit access may create
  temporary mirrored views; this matches the C# indexer returning `T` and avoids unsafe C++ references.
- The concat implementation uses the representation kind tag followed by `static_cast` for the checked cases
  instead of RTTI. This is a C++ quality improvement over a literal dynamic-cast translation and does not change
  the C# algorithm or asymptotic behavior.
- Node construction validates the two/three-child arity before reading cached endpoint values. The C# source uses
  `Debug.Assert` for this invariant; the C++ internal constructor throws before any invalid vector access so debug
  builds fail through the non-interactive test path.

Justified divergences:

- Counts and indices use `std::size_t`, continuing the C++ count policy. Structural size arithmetic uses checked
  unsigned addition.
- Public names follow the C++ deque spellings already used by `persistent_deque<T>` (`push_front`, `push_back`,
  `front`, `back`, `at`) while retaining C#-recognizable operation families (`set_item`, `insert_at`, `split_at`,
  `concat`, `reverse`).
- `try_pop_front` and `try_pop_back` return `std::optional<reversible_deque_pop<T>>` instead of C# `bool` plus out
  parameters.
- The public C++ checkpoint exposes `to_vector` rather than a lazy iterator. This is not a regression relative to
  C# `ReversibleDeque<T>`, whose enumerator explicitly materializes `ToArray()` before yielding.

Validation:

- Added tests for endpoint operation persistence, reverse correctness and double-reverse identity, operations after
  reverse, all four concat orientation combinations, split/reconcat at every index, large reversed indexing,
  reversed-deep-middle mutation, randomized histories that interleave reverse with every operation, and empty
  behavior.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- No new C# defect, flaw, or improvement proposal was found in the reversible deque pass. The C# source and tests
  matched the strict reversible-tree algorithms ported here.

## Checkpoint: Positional Rope

Compared C# source:

- `FingerTree/src/Tools.DataStructures.FingerTree/Rope.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/RopeChunk.cs`

Compared C# tests:

- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/RopeTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/RopeModelTests.cs`
- `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/RopePropertyTests.cs`
- The rope section of `FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ModelBasedCommandTests.cs`

Implemented:

- `rope<T>`, a persistent chunked positional sequence backed by the C++ general measured tree.
- `detail::rope_chunk<T>`, a bounded immutable chunk handle over `std::shared_ptr<const std::vector<T>>` plus
  offset/length. Chunk slices share backing storage; chunk edits allocate fresh vectors.
- `detail::rope_chunk_length_measure<T>` and `detail::rope_index_predicate`, giving the measured tree the same
  cumulative-length addressing model as C# `ChunkLengthMeasure<T>` and `ElementIndexPredicate`.
- Construction from initializer lists, iterator pairs, ranges, spans, and owned shared chunks.
- Endpoint reads/updates, indexed reads/replacement, insertion, removal, range insertion/removal, split, slice,
  concat with boundary coalescing, copying/materialization, `compact`, and invariant validation.
- Public aggregate-header and CMake/test-harness integration for the new wrapper.

Parity notes:

- The C++ rope follows the C# structure: a measured tree of chunks, a maximum chunk size of 2048, a minimum
  coalescing threshold of 256, split/locate by cumulative chunk length, and bounded chunk copying on edits.
- `insert_at`, `remove_at`, `set_item`, `split_at`, and `slice` isolate the touched chunk with
  `finger_tree::try_split_find`/`try_locate`, then rebuild only that chunk plus the tree path.
- `concat` peeks at the rightmost and leftmost boundary chunks and merges them when they fit, preserving the C#
  behavior that repeated concatenation does not leave a seam of tiny chunks.
- `compact()` materializes the sequence and rebuilds fresh chunks, releasing oversized backing vectors retained by
  slices. Tests cover the shared-storage use-count drop after the slice is dropped.
- `from_chunks` splits oversized source blocks into chunk-sized views without copying, preserving the C# zero-copy
  import behavior while making ownership explicit through `shared_ptr<const std::vector<T>>`.

Justified divergences:

- Counts and indices use `std::size_t`, continuing the C++ count policy. Chunk and tree length arithmetic use
  checked unsigned addition.
- The C++ zero-copy `from_chunks` entry point does not accept `std::span`, raw pointers, or mutable vector
  references for retained storage. A persistent rope can outlive such views, and a mutable backing array/vector
  would undermine immutable-snapshot semantics.
- The safe default construction paths copy input ranges into fresh immutable chunk vectors. This is the C++ analogue
  of using `Create`/`CreateRange` rather than C# `FromChunks` for ordinary mutable arrays.
- Public names follow the C++ spellings already used by the deque wrappers (`size`, `empty`, `front`, `back`,
  `insert_at`, `remove_at`, `split_at`, `to_vector`) while preserving the C# operation families.
- The first checkpoint exposes vector materialization rather than a chunk-aware iterator. This temporarily trails
  the C# `IReadOnlyList<T>`/enumerator surface, but does not affect the indexed/editing semantics or asymptotic
  update behavior. A streaming iterator should be added with the measured-tree traversal follow-up.

Validation:

- Added tests for construction from ranges, initializer lists, and owned chunks including oversized imported
  blocks; endpoint operation persistence; indexed mutation across chunk boundaries; range insertion/removal;
  split/slice/copy reconstruction; concat boundary coalescing; `compact()` releasing borrowed storage after slices;
  randomized histories against a vector model; large splice/indexing; and empty/error behavior.
- Built `msvc-debug` with `/W4 /WX`.
- Ran `ctest --preset msvc-debug --output-on-failure`; all tests passed.
- Built `msvc-release` with `/W4 /WX`.
- Ran `ctest --preset msvc-release --output-on-failure`; all tests passed.

Findings:

- A C# improvement proposal was recorded in
  [`FingerTree/docs/rope-fromchunks-immutable-storage-improvement-proposal.md`](../../../FingerTree/docs/rope-fromchunks-immutable-storage-improvement-proposal.md).
  `Rope<T>.FromChunks` imports `ReadOnlyMemory<T>` without copying; that is useful, but the backing storage may be
  externally mutable, so the immutable-storage precondition should be documented or reflected in the API name.
