# C# FingerTree Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:33:30Z
- Repository HEAD: 7c02f68ae23244d48871317ea90d26c0defd2394
- Audience: Maintainers validating the C# FingerTree workspace
- Scope: Local restore, build, test, sample, benchmark, stress, and warning-policy guidance for `src/CSharp/src/Tools.DataStructures.FingerTree`

Use this guide when changing the C# FingerTree library, tests, samples, benchmark harness, or documentation
that makes build, validation, API, complexity, or performance claims. For semantic contracts and first-use
examples, pair it with the [API specification](api-specification.md) and [usage guide](usage.md).

## Build Model

`DataStructures.sln` contains:

- `src/Tools.DataStructures.FingerTree/Tools.DataStructures.FingerTree.csproj`, the public library.
- `tests/Tools.DataStructures.FingerTree.Tests/Tools.DataStructures.FingerTree.Tests.csproj`, the
  xUnit/CsCheck test project.
- `samples/Tools.DataStructures.FingerTree.Tour`, `Showcase`, and `Editor`, the runnable sample tours.
- `benchmarks/Tools.DataStructures.FingerTree.Benchmarks`, the BenchmarkDotNet harness.

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
[README](../../benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md).

## Commands

From `src/CSharp`:

```powershell
dotnet restore
dotnet build .\DataStructures.sln
.\test.ps1
```

For ordinary behavior changes, `.\test.ps1` is the main gate because it restores and builds as needed before running
the test projects while suppressing modal Windows failure UI throughout the child-process tree. Use the explicit
restore/build steps when validating toolchain, solution membership, XML documentation, sample build, or
benchmark-project build changes.

Run individual sample tours when changing sample text or manual-demo behavior:

```powershell
dotnet run --project samples/Tools.DataStructures.FingerTree.Tour -c Release
dotnet run --project samples/Tools.DataStructures.FingerTree.Showcase -c Release
dotnet run --project samples/Tools.DataStructures.FingerTree.Editor -c Release
```

Run benchmarks only when changing performance-sensitive code, benchmark code, or performance claims:

```powershell
cd benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Test Coverage

`tests/Tools.DataStructures.FingerTree.Tests/` covers the xUnit/CsCheck suite. See the
[tests README](../../tests/Tools.DataStructures.FingerTree.Tests/README.md) for source-file grouping, filter examples,
sample-smoke hooks, and stress controls.

The suite covers:

- `FingerTreeDeque<T>` endpoint, indexing, splitting, concatenation, sorted-search, enumeration, invariant,
  branching-persistence, randomized model, and complexity-guard behavior;
- the general measured tree, built-in measures, custom comparisons, product measures, sum measures, zero-closure
  named operations, and `TryLocate`/`TrySplitFind` equivalence;
- derived sorted bag/set/dictionary, sorted mutable builders, priority queue, interval tree, and reversible deque
  behavior against BCL or brute-force models where appropriate;
- `Rope<T>`, `MeasuredRope<T, TMeasure, TMeasureOps>`, text helpers, editor-grade Unicode/newline helpers,
  `RopeBuilder`, and nested append-only rope builders;
- `RrbVector<T>` radix boundaries, unequal-height concatenation, split round-trips, retained
  snapshots, and randomized mixed-edit histories;
- `DabaLite<T, TMonoid>` noncommutative ordering, 100,000-operation randomized windows, empty/clear
  behavior, and instrumented worst-case combine-count ceilings;
- `CanonicalSortedSet<T>` independent-history shape/digest convergence, 50,000 randomized updates,
  comparer-equivalent representatives, policy gating, adversarial rank collisions, and concurrent digest publication;
- `BrodalOkasakiHeap<T>` sorted drains through 100,000 items, randomized meld forests, retained
  versions, comparer gating, and size-independent insert/meld comparison ceilings;
- runnable sample smoke tests for Tour, Showcase, and Editor;
- persistence/concurrency examples and tearable-struct stress tests;
- CsCheck property tests and model-based command-sequence tests that shrink operation histories rather than only
  data inputs.

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
