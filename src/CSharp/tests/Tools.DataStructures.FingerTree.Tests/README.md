# C# FingerTree Tests

- Created (UTC): 2026-07-02T21:19:42Z
- Repository HEAD: e375d5f1b031745ac97cf2ae81e0d91cf03ec22e
- Audience: Maintainers validating the C# FingerTree workspace
- Scope: xUnit, CsCheck, sample-smoke, model, and stress tests under `src/CSharp/tests/Tools.DataStructures.FingerTree.Tests`

`Tools.DataStructures.FingerTree.Tests` is the managed test project for the C# FingerTree workspace. It targets the
workspace defaults from `Directory.Build.props`, references the public library, and also references the Tour,
Showcase, and Editor sample projects so ordinary test runs compile and smoke-test the runnable samples.

The project uses xUnit for example/model tests and CsCheck for generated data and model-based command-sequence
tests. Keep new behavior covered by a direct example plus a model, property, or stress check when there is a
reasonable oracle.

## Source Map

- `FingerTreeDeque*Tests.cs` and `FingerTreeDequeAssert.cs` cover endpoint operations, indexing, split/concat,
  sorted search, enumeration/copy, invariants, branching persistence, randomized model histories, and complexity
  guards for the tuned deque, including fused sorted locate-and-edit reconstruction.
  `InternalEngineContractTests.cs` pins shared bounds and invariant-failure behavior
  at the internal enumeration seam.
- `MeasuredFingerTreeTests.cs`, `MeasuredFingerTreePersistenceTests.cs`, `BuiltInMeasureTests.cs`,
  `CustomComparisonMeasureTests.cs`, `ProductMeasureTests.cs`, `SumMeasureTests.cs`, `TryLocateTests.cs`,
  `ZeroClosureNamedOpTests.cs`, and `AllocationFreeReadTests.cs` cover the measured core, measure families,
  closure-free predicates, product/sum operations, locate/split equivalence, and zero-allocation read paths.
- `SortedBagTests.cs`, `SortedSetTests.cs`, `SortedDictionaryTests.cs`, `SortedBuilderTests.cs`,
  `PriorityQueueTests.cs`, `IntervalTreeTests.cs`, `ReversibleDequeTests.cs`, and
  `DerivedCollectionPersistenceTests.cs` cover derived collection facades and mutable sorted builders against
  BCL or brute-force model behavior.
- `ComparerEquivalentFacadeTests.cs` covers canonical-instance behavior when comparer equality is coarser than
  object equality across all sorted facades, plus duplicate-low interval-tree stress.
- `RopeTests.cs`, `RopeModelTests.cs`, `MeasuredRopeTests.cs`, `RopeTextTests.cs`, `RopeTextExtrasTests.cs`, and
  `RopeBuilderTests.cs` / `RopeAppendBuilderTests.cs` cover chunked ropes, measured ropes, text helpers,
  Unicode/newline extras, and builders.
- `RopeBoundaryCoverageTests.cs` covers legal sub-minimum split chunks, concatenation seam re-coalescing, and
  mixed `TextReader` peek/single/buffered reads across chunk boundaries.
- `RrbVectorTests.cs` covers branch-factor boundaries, unequal-height boundary-spine concatenation,
  radix-indexed regular nodes, relaxed size-table invariants, exact-boundary leaf reuse, adversarial
  split/concat density and height, builder snapshot isolation, endpoint contracts, and randomized
  persistent edit histories with retained versions.
- `DabaLiteTests.cs` covers noncommutative FIFO order, randomized variable windows, chunk churn,
  empty/clear contracts, and the proven worst-case monoid invocation limits.
- `CanonicalSortedSetTests.cs` covers permutation/delete-reinsert shape convergence, keyed policy
  semantics, randomized histories, rank collisions, algebra, representative retention, and digest publication.
- `BrodalOkasakiHeapTests.cs` covers large sorted drains, randomized meld trees, persistence,
  comparer-policy gating, empty behavior, and worst-case constant insert/meld comparison counts.
- `PrioritySearchQueueTests.cs` covers keyed priority updates, minimum ties/deletion, AVL balance,
  randomized histories and snapshots, range/threshold pruning, try patterns, and stored-key retention.
- `SampleSmokeTests.cs` captures the Tour, Showcase, and Editor sample output contracts.
- `PersistenceConcurrencyExamplesTests.cs` and `TearableConcurrencyStressTests.cs` cover structural-sharing
  examples, atomic publication, concurrent reads, and tearable-value stress.
- `RopePropertyTests.cs`, `SortedCollectionPropertyTests.cs`, and `ModelBasedCommandTests.cs` hold the CsCheck
  generated-history and model-based command suites.
- `SingleUseEnumerable.cs` is shared test support for enumerable-consumption checks.

## Build And Run

From `src/CSharp`, run the full solution test gate:

```powershell
.\test.ps1
```

Run only this test project when iterating on test code:

```powershell
.\test.ps1 -Project .\tests\Tools.DataStructures.FingerTree.Tests\Tools.DataStructures.FingerTree.Tests.csproj
```

Filter a class while developing a focused change:

```powershell
.\test.ps1 -Filter FullyQualifiedName~RopePropertyTests
```

The launcher suppresses modal Windows loader/crash reporting for the complete `dotnet` child-process tree. The
test assembly repeats the headless process configuration during module initialization, so direct test-runner and
Test Explorer execution is non-interactive after the assembly loads as well.

## Stress Controls

`TearableConcurrencyStressTests` honors `FINGERTREE_STRESS_SECONDS`. The default is short enough for ordinary
`.\test.ps1`; raise it for a local soak run without editing source:

```powershell
$env:FINGERTREE_STRESS_SECONDS = '60'
.\test.ps1 -Filter FullyQualifiedName~TearableConcurrencyStressTests
Remove-Item Env:\FINGERTREE_STRESS_SECONDS
```

Use the workspace [validation guide](../../docs/FingerTree/validation.md) for restore/build split commands, sample and
benchmark validation boundaries, warning policy, and evidence expectations.
