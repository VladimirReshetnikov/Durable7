# C# FingerTree Tests

- Created (UTC): 2026-07-02T21:19:42Z
- Repository HEAD: e375d5f1b031745ac97cf2ae81e0d91cf03ec22e
- Audience: Maintainers validating the C# FingerTree workspace
- Scope: xUnit, CsCheck, sample-smoke, model, and stress tests under `src/CSharp/tests/Durable7.FingerTree.Tests`

`Durable7.FingerTree.Tests` is the managed test project for the C# FingerTree workspace. It targets the
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
- `PersistentDeltaMapTests.cs` covers exact checkpoint-relative before/after changes, coalescing and
  cancellation, comparer-equivalent representative episodes, retained branches, O(1) root-sharing
  checkpoint/rollback, randomized model parity, callback failure atomicity, and a baseline-independent
  change-enumeration comparison-count guard.
- `BilateralAncestralDequeTests.cs` covers the experimental two-oriented-ancestry-interval deque and
  Myers reference arena: endpoint contracts, reverse/slice closure, exhaustive and randomized
  retained branches, exact ancestor-query ceilings, irregular-tree ancestry oracles, square block
  seams, enumeration routing, and concurrent branching/reads.
- `PersistentIntervalMapTests.cs` covers strict and replacing updates, lexicographic interval-key
  order, configured payload equality, first interval representatives, reversed-endpoint rejection,
  point and overlap queries against a brute-force model, removal, policy-preserving clear,
  branching persistence, and annotation invariants.
- `PersistentChunkedBitSetTests.cs` covers negative and maximum indexes, 64-bit word boundaries,
  deduplication, inclusive rank, zero-based select, chunk contraction, four-way set algebra,
  identity, retained branches, randomized sorted-set parity, and measured-tree invariants.
- `ComparerEquivalentFacadeTests.cs` covers canonical-instance behavior when comparer equality is coarser than
  object equality across all sorted facades, plus duplicate-low interval-tree stress.
- `RopeTests.cs`, `RopeModelTests.cs`, `MeasuredRopeTests.cs`, `RopeTextTests.cs`, `RopeTextExtrasTests.cs`, and
  `RopeBuilderTests.cs` / `RopeAppendBuilderTests.cs` cover chunked ropes, measured ropes, text helpers,
  Unicode/newline extras, and builders.
- `RopeContractOracleTests.cs` and `MeasuredRopeContractOracleTests.cs` are the Axis 2 executable baseline for
  logical order, chunk bounds and proportionality, backing-store sharing, snapshot identity, noncommutative
  measures, overflow rejection, and strong exception behavior for user measure callbacks.
- `RopeCursorPrototypeContractTests.cs`, `RopeCursorPrototypeModelTests.cs`, and
  `RopeCursorPrototypeBoundaryTests.cs` validate the private Axis 2 C0 cursor representation across all focus/flush candidates,
  retained branches, class/struct/mutable representations, snapshot-cache races, every source chunk length,
  seam oscillation, and long typing/backspace histories retained as the representation/tuning oracle.
- `RopeCursorTests.cs` and `RopeCursorModelTests.cs` validate the public Axis 2 C1 readonly-struct cursor:
  `List<T>` gap-model parity, the selected 16/256 focus/carry bounds, exact named boundaries, all operations,
  retained branches, version/context/snapshot identity, overflow and failure atomicity, cache races, structural
  sharing, and adversarial fan-out under the published conservative branch bound.
- `MeasuredRopeCursorApiShapeTests.cs`, `MeasuredRopeCursorTests.cs`, and
  `MeasuredRopeCursorModelTests.cs` validate the public Axis 2 C2 readonly-struct cursor: API shape,
  ordered before/after measures, positional movement and edits, retained branches, snapshot identity,
  callback failures, and deterministic gap-model histories.
- `MeasuredRopeCursorMeasureSeekTests.cs` and `MeasuredRopeCursorMeasureCacheTests.cs` cover absolute
  measure seek through delegate and closure-free predicates, lawful noncommutative measures, source and
  prepared paths, dirty versions, exact bounded callback counts, same-fragment lineage reuse,
  failure-atomic preparation, and concurrent winner-returning publication.
- `RopeBoundaryCoverageTests.cs` covers legal sub-minimum split chunks, concatenation seam re-coalescing, and
  mixed `TextReader` peek/single/buffered reads across chunk boundaries.
- `RrbVectorTests.cs` covers branch-factor boundaries, unequal-height boundary-spine concatenation,
  radix-indexed regular nodes, relaxed size-table invariants, exact-boundary leaf reuse, adversarial
  split/concat density and height, builder snapshot isolation, endpoint contracts, and randomized
  persistent edit histories with retained versions.
- `AncestralSliceQueueTests.cs` covers exhaustive slice/split boundaries, appendable anchored empties,
  retained and randomized branching histories, direct Myers ancestor-oracle parity, odd-block square
  seams and capacity arithmetic, traversal-hop and operation-routing guardrails, explicit/custom
  factory validation, enumeration, and synchronized concurrent branches/readers.
- `DabaLiteTests.cs` covers noncommutative FIFO order, a 100,000-operation randomized variable
  window model, empty/clear contracts, and the three/two/at-most-one worst-case `Combine` limits.
  `DabaLiteAdversarialTests.cs` exhausts short histories and covers all four fixup phases with a
  non-default identity, 63/64/65 and 127/128/129 chunk boundaries, long-lived steady-window churn,
  every reachable `Combine`/`Empty` failure ordinal with unchanged published state, prompt
  retired-reference and retired-block release, O(1) clear/reuse, and exact structural region and
  chunk/slack statistics without invoking monoid callbacks.
- `CanonicalSortedSetTests.cs` covers public-seed permutation/delete-reinsert convergence, 50,000
  randomized persistent updates, policy-gated algebra, representative retention, rank collisions,
  and concurrent digest publication. `CanonicalSortedSetAdversarialTests.cs` covers required
  comparer-equivalence rank hashes, semantic `IReadOnlySet<T>` equality across policy identities,
  caller-keyed HMAC reproducibility and key ownership, invariant/statistics validation, localized
  sharing, and stack-safe 4,096-node fully colliding histories.
- `BrodalOkasakiHeapTests.cs` covers large sorted drains, randomized meld trees, persistence,
  comparer-policy gating, empty behavior, and worst-case constant insert/meld comparison counts.
  `BrodalOkasakiHeapAuditTests.cs` independently decomposes the fused child/embedded-forest encoding
  and covers public invariant statistics, adversarial shapes, a 30,000-operation branching retained
  history, exact off-path sharing, comparer-equivalent representatives, and operation-cost ceilings.
- `PrioritySearchQueueTests.cs` covers keyed priority updates, minimum ties/deletion, AVL balance,
  randomized histories and snapshots, range/threshold pruning, try patterns, and stored-key retention.
  `PrioritySearchQueueAdversarialTests.cs` covers comparer-versus-default equality during replacement,
  public invariant statistics, every AVL rotation and deletion rebalance, a 20,000-operation retained
  history, exact path sharing, tie order, and comparison-count evidence for query pruning.
- `RangeUpdate*Tests.cs` is the integration test family for the implicit-AVL lazy-tag
  sequence. Its required scope is the exact API, complete static algebra laws and
  `Compose(newer, older)` direction, affine assignment/addition, noncommutative measures, all
  positional/range boundaries, array/list command-model histories with retained branches,
  AVL/tag/cache invariants, deterministic height/node/allocation/callback ceilings, failpoint
  atomicity, concrete/interface and copied-enumerator behavior, and concurrent reads. The focused
  lane passes 62/62 tests; the complete project passes 692/692 in Debug and Release with zero build
  warnings or errors. At the pre-bimap Range shipment checkpoint, the full serialized C# solution
  built with zero warnings or errors and passed 1,417/1,417 tests in both configurations (319
  Numerics + 292 HAMT + 692 FingerTree + 62 Ordered + 52 Tungsten), so the C# reference is shipped.
  `RangeUpdateDiagnosticsAdapter.cs` exposes the
  internal deterministic counters to this test assembly without adding diagnostics to the public
  collection API.
- `ContextualRankSequenceTests.cs` covers the experimental finite-context event sequence: quoted
  delimiters, exhaustive short words from every start state, randomized retained branches against
  a direct scanner, multi-event transitions, split/concat boundaries, cached-query callback counts,
  invalid policies, overflow atomicity, and concurrent readers with independent branch writers.
  The focused lane passes 7/7 in both Debug and Release on the experimental branch.
- `SampleSmokeTests.cs` captures the Tour, Showcase, and Editor sample output contracts, including
  the Axis 2 C3 retained measured-cursor history, cadence-sixteen snapshot policy, localized Unicode
  editing, line/column result, and alternate branch transcript.
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
.\test.ps1 -Project .\tests\Durable7.FingerTree.Tests\Durable7.FingerTree.Tests.csproj
```

Filter a class while developing a focused change:

```powershell
.\test.ps1 -Filter FullyQualifiedName~RopePropertyTests
```

The checkpoint-differential map lane uses:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentDeltaMapTests
```

The research-prototype lane passes 15/15 focused cases in both configurations.

The experimental ancestral-slice queue lane is:

```powershell
.\test.ps1 -Filter FullyQualifiedName~AncestralSliceQueue
```

This lane passes 15/15 tests in Debug and Release. No benchmark result is part of this experimental
gate.

The bilateral ancestral deque research lane is:

```powershell
.\test.ps1 -Filter FullyQualifiedName~BilateralAncestralDequeTests
```

That lane passes 15/15 tests in Debug and Release.

The range-update integration lane uses the same serialized launcher:

```powershell
.\test.ps1 -Filter FullyQualifiedName~RangeUpdate
```

This focused Debug lane passes 62/62 tests. The complete project passes 692/692 tests in Debug and
Release. At the pre-bimap Range shipment checkpoint, the shipped C# reference was additionally
covered by full serialized C# solution builds with zero warnings or errors and 1,417/1,417 passing
tests in both configurations; see the
[range-update sequence contract](../../docs/FingerTree/range-update-sequence.md) and validation guide.

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
