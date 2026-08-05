# C# FingerTree Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:33:30Z
- Repository HEAD: 7c02f68ae23244d48871317ea90d26c0defd2394
- Audience: Maintainers validating the C# FingerTree workspace
- Scope: Local restore, build, test, sample, benchmark, stress, and warning-policy guidance for `src/CSharp/src/Durable7.FingerTree`

Use this guide when changing the C# FingerTree library, tests, samples, benchmark harness, or documentation
that makes build, validation, API, complexity, or performance claims. For semantic contracts and first-use
examples, pair it with the [API specification](api-specification.md) and [usage guide](usage.md).
The [range-update sequence contract](range-update-sequence.md) adds the static action laws,
implicit-AVL/tag/cache invariant, and deterministic integration matrix for that sibling core.

## Build Model

`Durable7.sln` contains:

- `src/Durable7.FingerTree/Durable7.FingerTree.csproj`, the public library.
- `tests/Durable7.FingerTree.Tests/Durable7.FingerTree.Tests.csproj`, the
  xUnit/CsCheck test project.
- `samples/Durable7.FingerTree.Tour`, `Showcase`, and `Editor`, the runnable sample tours.
- `benchmarks/Durable7.FingerTree.Benchmarks`, the BenchmarkDotNet harness.

`Directory.Build.props` applies the workspace defaults:

- Target framework: `net10.0`.
- Language version: C# `preview`.
- Nullable annotations and implicit usings enabled.
- XML documentation generation enabled for the public library.
- Public XML documentation warnings `CS1591` and `CS1573` promoted to errors.

The sample and benchmark executable projects disable XML documentation generation where appropriate, because
they are not shipped API surfaces. The test project references the library and all three samples, so the sample
programs are compiled and smoke-tested by the ordinary test suite. Benchmark execution is separate from the
test gate; see [benchmarks.md](benchmarks.md) and the benchmark project
[README](../../benchmarks/Durable7.FingerTree.Benchmarks/README.md).

## Commands

From `src/CSharp`:

```powershell
dotnet restore .\Durable7.sln --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\Durable7.sln --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
.\test.ps1
```

For ordinary behavior changes, `.\test.ps1` is the main gate because it restores and builds as needed before running
the test projects while suppressing modal Windows failure UI throughout the child-process tree. Use the explicit
restore/build steps when validating toolchain, solution membership, XML documentation, sample build, or
benchmark-project build changes.
All three phases are serialized; do not overlap them with another workspace build, test, or benchmark run.

Run individual sample tours when changing sample text or manual-demo behavior:

```powershell
dotnet run --project samples/Durable7.FingerTree.Tour -c Release
dotnet run --project samples/Durable7.FingerTree.Showcase -c Release
dotnet run --project samples/Durable7.FingerTree.Editor -c Release
```

Run benchmarks only when changing performance-sensitive code, benchmark code, or performance claims:

```powershell
cd benchmarks\Durable7.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Test Coverage

### Current Derived-Structure Integration Evidence

On 2026-07-29 UTC, the six consolidated research-derived FingerTree lanes passed 70/70 tests in both
Debug and Release: 15 delta-map + 15 ancestral-slice-queue + 15 bilateral-ancestral-deque + 7
contextual-rank-sequence + 11 monotone-action-heap + 7 run-delta-vector. The complete FingerTree
project passed 794/794 tests, and the serialized full C# solution passed 1,240/1,240 in both
configurations: 366 HAMT + 794 FingerTree + 80 Ordered. The separate
ancestral-connection-forest lane contributed 12 of the HAMT tests. Benchmarks were not run because
no empirical performance claim is part of this integration gate. Solution builds completed in both
configurations; the rebased base currently emits pre-existing XML-documentation warnings.

On 2026-08-04 UTC, after the collections were promoted into `Durable7.FingerTree`/`Durable7.Hamt`,
ported to Rust, and extended with the reviewed enhancement backlog, the complete FingerTree project
passes 807/807 tests and the serialized full C# solution passes 1,254/1,254: 367 HAMT +
807 FingerTree + 80 Ordered. The added coverage is the shared incremental-ancestor seam suite
(including a custom non-Myers arena driving both consumer collections through randomized histories),
position-addressed run accept/revert with a retained-branch interleaving property test,
range-restricted change enumeration, bulk assignment with failure atomicity, and uniform endpoint
overflow reporting for the contextual sequence. Benchmarks were not run.

For historical comparison, the pre-experiment checkpoint passed 1,158/1,158 full-solution
tests, including 724 FingerTree tests. The consolidated totals above supersede the isolated
per-branch snapshots recorded while the prototypes were developed.

`tests/Durable7.FingerTree.Tests/` covers the xUnit/CsCheck suite. See the
[tests README](../../tests/Durable7.FingerTree.Tests/README.md) for source-file grouping, filter examples,
sample-smoke hooks, and stress controls.

During interval-map development, run its focused model/invariant lane with:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentIntervalMapTests
```

For the checkpoint-differential ordered-map research prototype, run:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentDeltaMapTests
```

The focused lane passes 15/15 cases covering endpoint classification, cancellation, representative
policy, retained branches, randomized model parity, callback failures, and the `Θ(k + 1)`
enumeration guard. The broader proposal and scoped novelty audit are recorded in the
[research note](../../../../docs/proposals/persistent-delta-map-2026-07-25.md).

For the sparse chunked bit set, run:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentChunkedBitSetTests
```

The focused lane currently passes 8/8 tests covering domain boundaries, word seams, rank/select,
algebra, receiver identity, retained branches, randomized model parity, and measured invariants.

For the experimental ancestral slice queue, run:

```powershell
.\test.ps1 -Filter FullyQualifiedName~AncestralSliceQueue
```

Its focused lane covers the empty/singleton contract, every subrange of a representative history,
appendable empty anchors, split boundaries, retained branching versions, deterministic randomized
model histories, odd-block square seams, a direct ancestor oracle, traversal-hop guardrails,
explicit/custom factories, per-operation backend-counter deltas, synchronized concurrent
branches/readers, enumeration, and invalid ranges. It passes 15/15 in both Debug and Release.

For the experimental bilateral ancestral deque and its Myers reference arena, run:

```powershell
.\test.ps1 -Filter FullyQualifiedName~BilateralAncestralDequeTests
```

This lane is required to cover the two-arm invariant, all slice/split boundaries, retained branching
models, exact level-ancestor call ceilings, direct irregular-tree arena oracles, odd-block seams, and
concurrent reads. The optimal Alstrup--Holm instantiation remains a proved backend reduction rather
than a shipped implementation, so tests must report Myers bounds separately.

The bilateral lane passes 15/15 tests in Debug and Release. Benchmarks are not evidence for its
asymptotic theorem.

For the experimental persistent run-delta vector, run:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentRunDeltaVectorTests
```

The focused lane contains 7 tests covering factories and equality policies both coarser and finer
than default equality, every local run merge/split/shrink/cancellation case, zero-comparer-call
selected-run acceptance and reversion, 5,000 randomized retained-version operations against an
independent model, the clustered `k >> r` witness, comparer failure atomicity, and concurrent
readers with independent branch writers.

For the experimental persistent monotone-action heap, run:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentMonotoneActionHeapTests
```

The focused lane contains 11 tests covering the clamp-action algebra, ordinary heap semantics,
lazy whole-heap transforms, meld compatibility, retained branching versions, randomized model
parity, structural and callback ceilings, failure atomicity, and concurrent readers.

The suite covers:

- `FingerTreeDeque<T>` endpoint, indexing, splitting, concatenation, sorted-search, enumeration, invariant,
  branching-persistence, randomized model, and complexity-guard behavior;
- the general measured tree, built-in measures, custom comparisons, product measures, sum measures, zero-closure
  named operations, and `TryLocate`/`TrySplitFind` equivalence;
- derived sorted bag/set/dictionary, sorted mutable builders, priority queue, interval tree,
  persistent interval map, persistent chunked bit set, and reversible deque
  behavior against BCL or brute-force models where appropriate;
- `BilateralAncestralDeque<T>` empty/end/reverse/index/slice/split behavior, exhaustive and randomized
  retained-version models, query-routing ceilings, Myers branch ancestry, allocation seams, and
  lock-safe concurrent use;
- `Rope<T>`, its public immutable `RopeCursor<T>` gap editor, `MeasuredRope<T, TMeasure, TMeasureOps>`,
  text helpers, editor-grade Unicode/newline helpers, `RopeBuilder`, and nested append-only rope builders;
- `RopeCursorTests.cs` locks default-value rejection, gap and edit semantics, no-op/version/context/snapshot
  identity, equality-free replacement, retained branches, concurrent winner-returning snapshot publication,
  failure-atomic caching, overflow-before-allocation counters, structural sharing, and independently scaled
  boundary fan-out under the published O(b log n) scope;
- `RopeCursorModelTests.cs` compares 2,100 deterministic mixed commands against `List<T>` plus an integer gap,
  retains and branches old cursor versions, covers positions and lengths 0/1/15/16/255/256/257/2047/2048/2049,
  and exercises long typing, carry flush, seam oscillation, and full backspace histories;
- the [C0 decision record](rope-cursor-c0-decision.md) connects those C1 tests to the selected 16/256
  representation, named benchmark gate, and published linear-lineage/O(b log n) complexity scope;
- `MeasuredRopeCursorApiShapeTests.cs`, `MeasuredRopeCursorTests.cs`, and
  `MeasuredRopeCursorModelTests.cs` lock the public C2 surface, default/boundary behavior, ordered
  before/after measures, edit vocabulary, retained branches, snapshot identity, and deterministic
  gap-model histories;
- `MeasuredRopeCursorMeasureSeekTests.cs` and `MeasuredRopeCursorMeasureCacheTests.cs` cover
  delegate and closure-free absolute measure seek, hit/miss/chunk-boundary behavior,
  noncommutative ordering, source/prepared/dirty parity, fragment reuse, callback ceilings,
  failure atomicity, and racing cache publication;
- the [C2 decision record](measured-rope-cursor-c2-decision.md) connects that coverage to the
  locked local-edit, positional/measure-seek, line/column, callback, allocation, and dirty-query gates;
- `RrbVector<T>` radix boundaries, regular-versus-relaxed representation invariants, exact-boundary
  leaf reuse, unequal-height and adversarial-fragment concatenation, density/height ceilings,
  builder snapshot isolation, retained snapshots, and randomized mixed-edit histories;
- `AncestralSliceQueue<T>` queue/slice semantics, appendable anchored empties, direct Myers ancestor
  correctness, odd-block square boundaries, retained branches, randomized models, factory validation,
  operation-routing/traversal counters, and synchronized concurrent publication/read behavior;
- `PersistentRunDeltaVector<T>` exact equality-relative dirty runs, current/checkpoint parity,
  interval locality and maximality, cancellation, whole and selected-run accept/revert, retained
  branches, comparer failures, clustered descriptors, and concurrent snapshots;
- `DabaLiteTests.cs` covers noncommutative FIFO ordering, a 100,000-operation randomized window
  model, empty/clear behavior, and the three/two/at-most-one worst-case `Combine` ceilings;
- `DabaLiteAdversarialTests.cs` exhausts short histories and covers all four fixup phases with a
  non-default identity, 63/64/65 and 127/128/129 chunk boundaries, long-lived sliding churn,
  callback failure at every reachable `Combine`/`Empty` ordinal with the strong exception
  guarantee, prompt reference and retired-block release, clear/reuse, and `ValidateStructure`
  callback independence plus region, capacity, and slack statistics;
- `CanonicalSortedSet<T>` public-seed and caller-keyed HMAC rank reproducibility, independent-history
  shape/digest convergence, 50,000-operation and invariant-heavy 12,000-operation randomized
  histories with retained versions, comparer-equivalent representative retention, rejection and
  detection of incoherent rank hashes, semantic `IReadOnlySet<T>` relations across policies, and
  identity-gated algebra;
- canonical-set Cartesian bulk building, `ValidateStructure` statistics, localized path sharing,
  no-op identity, concurrent digest publication, and a 4,096-node fully colliding height-n tree that
  exercises stack-safe delete, reinsert, enumeration, digest, equality, and validation paths;
- `BrodalOkasakiHeapTests.cs` covers sorted drains through 100,000 items, randomized meld forests,
  retained versions, comparer gating, and size-independent insert/meld comparison ceilings;
- `BrodalOkasakiHeapAuditTests.cs` adds ascending, descending, equal-key, and melded fused-forest
  shapes; a 30,000-operation branching retained-version model; exact off-path sharing;
  comparer-equivalent representatives; public `ValidateStructure` statistics; an independent
  reflection audit of fused child/embedded-forest decomposition; and comparison/allocation ceilings
  separating O(1) insert/meld from O(log n) delete-min;
- `PrioritySearchQueueTests.cs` covers 50,000-operation keyed histories, retained snapshots,
  complete minimum draining, 1,000 range/threshold model queries, AVL balance, and representative
  retention;
- `PrioritySearchQueueAdversarialTests.cs` covers comparer-equality versus default-equality replacement,
  equivalent-key representative retention, every AVL rotation and deletion rebalance, public
  `ValidateStructure` statistics, a 20,000-operation branching retained history, exact no-op/path
  sharing, priority-tie order, and comparison-count evidence for whole-tree, exact-key, and mixed
  range/threshold pruning;
- runnable sample smoke tests for Tour, Showcase, and Editor, including C3 retained measured-cursor
  undo/redo history, cadence-sixteen edit bursts, cursor line/column output, Unicode edits, and
  branching from an old cursor;
- persistence/concurrency examples and tearable-struct stress tests;
- CsCheck property tests and model-based command-sequence tests that shrink operation histories rather than only
  data inputs.

## Range-Update Sequence Integration Gate

Focused, project-level, and full-solution validation for
`RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` completed serially on
2026-07-15 UTC:

- Debug full-solution build: zero warnings and zero errors;
- Debug focused `FullyQualifiedName~RangeUpdate` lane: 62/62 tests passed;
- Debug complete FingerTree test project: 692/692 tests passed;
- Debug full C# solution: 1,417/1,417 tests passed;
- Release full-solution build: zero warnings and zero errors;
- Release complete FingerTree test project: 692/692 tests passed; and
- Release full C# solution: 1,417/1,417 tests passed.

Each full-solution total is 319 Numerics + 292 HAMT + 692 FingerTree + 62 Ordered + 52 Tungsten.
The C# reference is shipped on this deterministic, benchmark-independent evidence. The gate covers:

- **API shape:** reflect the exact generic constraint, factories, positional operations, range
  update/query members, `IReadOnlyList<TElement>` implementation, concrete `GetEnumerator`, and
  nested public mutable struct enumerator. Assert that no assignment- or addition-specific method
  leaks onto the generic surface.
- **Algebra laws:** execute `IMonoid` identity/associativity plus tag identity/associativity,
  `Compose(newer, older)` action order, `IsIdentity` soundness for value-distinct identities,
  singleton consistency, empty-measure preservation, and ordered distribution. Include a lawful
  noncommutative measure and affine assign-after-add/add-after-assign histories.
- **Boundaries and identity:** cover empty/singleton instances, every valid split boundary, whole,
  empty, prefix, suffix, and one-element ranges, invalid/overflow-prone index-count pairs,
  empty-side concat, whole-range extraction, empty/identity updates, and old-version retention.
  Confirm validation precedes tag callbacks.
- **Reference model:** run deterministic generated histories against a mutable array/list oracle.
  Commands include prepend/append/insert/remove/set, split/concat/extract, point reads, whole and
  range measures, range assignment/addition, retained snapshots, and branches from arbitrary old
  versions. Shrinking must preserve command order and the selected branch.
- **Representation:** after every generated command, audit AVL height and balance, cached count,
  logical cached measure under pending tags, composition direction, in-order logical values,
  immutable sharing, and a logarithmic height ceiling.
- **Structural bounds:** instrument node visits, allocations, rotations, element/measure actions,
  composition, and measure callbacks. Assert operation-specific ceilings as functions of observed
  AVL height, including one-root whole-sequence update and logarithmic boundary work. Do not replace
  these guards with elapsed-time thresholds.
- **Failure atomicity:** use failpoint algebras that throw at every reachable ordinal from
  `Measure`, `Combine`, `IsIdentity`, `Compose`, `ApplyElement`, and `ApplyMeasure`, including
  paths that split, join, and rotate. Every source and retained branch must remain unchanged.
- **Enumeration:** cover empty and nonempty concrete pattern enumeration, boxed generic and
  nongeneric interface paths, logical tag application/order, `Current` outside the active element,
  unsupported `Reset`, no-op `Dispose`, shared-state fail-fast copied enumerators, and independent
  enumerators.
- **Concurrency:** repeatedly enumerate, index, query range measures, and read whole measures from
  independent enumerators over shared retained versions. Include tearable carriers where useful;
  policy callback thread safety remains the caller's responsibility.

The focused run uses the filter documented in the
[tests README](../../tests/Durable7.FingerTree.Tests/README.md#build-and-run). The
recorded shipment then runs the complete project suite and ordinary full serialized workspace gate
in both configurations. The detailed semantic oracle is the
[range-update sequence contract](range-update-sequence.md).

Benchmarks are explicitly outside this integration gate. They remain postponed until the machine
can run the benchmark harness in isolation; no wall-clock result gathered under current CPU,
memory, or I/O contention is acceptable as validation evidence.

## Contextual Rank Sequence Gate

Run the focused finite-context rank/select lane while iterating:

```powershell
.\test.ps1 -Filter FullyQualifiedName~ContextualRankSequence
```

The experimental lane covers an independently scanned semantic model, exhaustive short inputs,
retained branching histories, cached-query callback counts, transition-policy failures, and checked
event-total overflow. Before publishing the branch, also run the complete FingerTree project and
full C# solution in Debug and Release, formatting, Markdown-link validation, and `git diff --check`.
The [research note](../../../../docs/proposals/contextual-rank-sequence-2026-07-25.md) is the
authoritative home for the proof and novelty boundary.

The focused lane passes 7/7 tests in both Debug and Release. Its complete integration totals are
recorded in the consolidated gate above.

## Stress Controls

`TearableConcurrencyStressTests` honors `FINGERTREE_STRESS_SECONDS`. The default is short enough for ordinary
`.\test.ps1`; raise it for a longer soak without editing source:

```powershell
$env:FINGERTREE_STRESS_SECONDS = '60'
.\test.ps1 -Filter FullyQualifiedName~TearableConcurrencyStressTests
Remove-Item Env:\FINGERTREE_STRESS_SECONDS
```

Use longer stress runs when touching lazy memoization, atomic publication, structural sharing under concurrent
reads, or the tearable value/measure pathways.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/CSharp> .\test.ps1
```

If a docs-only change only updates links or wording and does not alter commands, API claims, XML documentation,
sample behavior, or performance claims, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
