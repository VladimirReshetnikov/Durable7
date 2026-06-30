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
