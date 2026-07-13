# Rope cursor C0 representation and proof decision

- Status: Evidence collection pending
- Created (UTC): 2026-07-13T06:10:29Z
- Repository HEAD: 439b83e7738907c2ff01e66da1c232ab70a80388
- Audience: Maintainers deciding whether the Axis 2 rope cursor can advance to C1
- Scope: C# positional-rope zipper prototypes, their proof boundary, and the locked C0 evidence protocol

## Decision status

**Evidence collection pending.** This record deliberately makes no selection among the immutable
class cursor, readonly-struct cursor, mutable-session control, focused-root escalation, and deferral.
The semantic tests, structural counters, retained-version experiment, five-process noise floor, and
Release benchmark artifacts named below must exist before this status changes.

C0 advances only when one persistent representation clears the predeclared materiality rule in the
named workload without contradicting the complexity scope proved here. The rule is the larger of the
measured noise floor and the practical margin in the benchmark README: 10% for mean latency,
allocated bytes, and retained bytes, and 15% for p99 latency. An interval that crosses the threshold
is inconclusive. Mutable-session results are a control, not a candidate for the C1 persistent API.

## Prototype representations

All three prototypes execute the same zipper engine and differ only in the caller-visible carrier:

- the **immutable class cursor** allocates a wrapper for each navigation or edit result;
- the **readonly-struct cursor** copies a value containing references to the immutable version state
  and zipper context, avoiding the class wrapper but not the arrays and tree nodes created by edits;
- the **mutable-session control** mutates one private wrapper's version/context fields while the
  engine continues to create immutable edit state. It measures the upper bound available by removing
  result-wrapper churn; it is not an owner-token transient and must not be described as one.

The persistent version state and navigation context are intentionally separate. A
`CursorVersionState<T>` holds the logical count, the exact zipper context that can normalize that
version, and a nullable snapshot cache. Movement and seek return a new navigation context over the
same version state. An edit creates a new version state seeded by the edited context. Consequently,
two navigation positions in one version share snapshot identity, while retained edited branches own
independent version/cache cells.

The source version starts with the source `Rope<T>` already installed as its cached snapshot. A dirty
version's first `Snapshot()` constructs a candidate and publishes it with
`Interlocked.CompareExchange`; every caller returns the winner. A race may perform normalization more
than once, but only the winning rope is retained by the version and observed by later callers. A
normalization exception occurs before publication and leaves the cache empty, so a later call may
retry. The proof and measurements must therefore distinguish **one published winner** from the false
claim of **exactly one normalization attempt**.

## Zipper decomposition and invariants

A context represents the sequence in this exact order:

```text
left ordinary tree · left partial carry · active focus · right partial carry · right ordinary tree
                                             ^
                                          gap index
```

For configuration `F = FocusCapacity` and `K = FlushChunkSize`, every reachable context must satisfy:

1. `0 <= Gap <= Active.Length <= F` and `0 <= Position <= Count`.
2. `LeftCarry.Length < K` and `RightCarry.Length < K`; there is at most one partial carry on each
   side.
3. `Position == LeftTree.Measure + LeftCarry.Length + Gap`.
4. `Count == LeftTree.Measure + LeftCarry.Length + Active.Length + RightCarry.Length +
   RightTree.Measure`.
5. Ordinary-tree chunks remain in logical order. A full `K`-element carry segment is flushed as an
   ordinary chunk; a partial segment stays in its side carry. Pulling or spilling may copy a focus or
   carry but must not reverse either side.
6. An edited version's snapshot seed is never mutated after publication. Every retained cursor and
   snapshot therefore remains isolated from later edits and branches.

The C0 contract suite must exercise empty/start/end gaps, focus and carry boundaries, 255/256/257 and
2,047/2,048/2,049 rope boundaries, seam oscillation, retained ancestors, fan-out at a pending carry
flush, and concurrent cache publication. Counters must independently report node visits, spine
allocations, forced suspensions, focus/carry copies and elements copied, wrapper allocations, and
snapshot normalizations. Counter collection is untimed; it must not run inside any comparable
latency lane.

## Honest complexity and potential boundary

Treat `F <= 128` and `K <= 2,048` as locked configuration bounds, not as functions of sequence size
`n`. An edit copies at most the bounded focus and may copy or flush a bounded partial carry. These
operations are O(1) with respect to `n`, but that notation does not erase their potentially material
`F`/`K` constants; the tuning cross-product and copy counters decide whether those constants are
acceptable.

On one **linear version lineage**, a unit movement stays in the focus until a boundary pull is needed.
The pending focus/carry work can be charged to elements moved through that lineage, and ordinary
finger-tree endpoint work has the usual amortized bound. A boundary operation may still force a
spine path and cost O(log n) in the worst case. Normalizing a dirty snapshot copies the bounded
middle state and joins it to the two ordinary trees, costing O(F + K + log n) in the current
representation while sharing the untouched tree portions.

That amortization does **not** compose over an arbitrary version DAG. If `b` children branch from the
same high-potential cursor immediately before a carry/chunk or lazy-spine transition, each child can
repeat the deferred work. The honest aggregate bound for those fan-out boundary histories is
O(b log n), plus bounded focus/carry copying per branch; it is not O(b) unless adversarial branching
counters establish a stronger result. More generally, linear work may be amortized within each
branch, but potential consumed on one child cannot pay for a sibling. C1 documentation may claim
linear-lineage amortization and this O(b log n) branch bound only; an unqualified version-DAG
amortized O(1) claim requires new proof and evidence.

Absolute seek is O(log n) after a canonical snapshot exists. Seeking a dirty version first invokes
snapshot normalization, so the random-locality benchmark deliberately measures that cost. A racing
snapshot has bounded latency per caller but may perform the normalization work once per racer before
one cache value wins; the concurrency test and counter artifact must record that distinction.

## Locked benchmark histories

`RopeCursorBenchmarks` uses seed `0x5eed2026` and crosses:

- document sizes 1,024, 65,536, and 1,048,576 UTF-16 elements;
- locality windows 1, 8, and 256, plus deterministic random absolute positions;
- snapshot cadences 1, 16, and 256 edits; and
- focus capacities 16, 32, 64, and 128, with flush size fixed at 2,048.

Every invocation performs exactly 256 replacements, ensuring that every cadence publishes at least
one snapshot and that the returned last snapshot consumes the result. The three local windows use
unit `MovePrevious`/`MoveNext` navigation; the random lane uses absolute `Seek` for all cursor
representations so it does not accidentally allocate millions of wrappers merely to reach a random
position. Indexed `Rope<char>` applies the identical replacement positions and captures an immutable
version reference at each cadence. `StringBuilder` applies the same replacements and calls
`ToString()` at the same cadence. Class, struct, and mutable-session cursor lanes use the same edit
values, navigation rule, focus/flush configuration, and snapshot timing.

`RopeCursorTuningBenchmarks` fixes the representative workload at 65,536 elements, locality window
8, cadence 16, and 256 replacements, then independently crosses all four focus capacities with flush
sizes 256, 512, 1,024, and 2,048 for all three prototype carriers. This preserves the complete tuning
obligation without multiplying the full document/locality/cadence matrix by another factor of four.

The timed methods do not start diagnostic sessions, traverse retained object graphs, or validate
invariants. Retained bytes, adversarial branch counts, and operation counters belong in separate
artifacts and cannot be inferred from `MemoryDiagnoser`'s allocation columns.

## Exact single-worker commands

Run from this worktree. The environment settings flow into BenchmarkDotNet's generated MSBuild
projects as well as the explicit build. `-m:1`, disabled project parallelism, disabled shared
compilation, disabled build servers, and BenchmarkDotNet's sequential case execution are mandatory;
do not remove them for either the short or full collection.

```powershell
Set-Location C:\Users\vresh\.codex\worktrees\5cd5\DataStructures\src\CSharp
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'

dotnet restore DataStructures.sln --disable-parallel
dotnet build DataStructures.sln -c Release --no-restore --disable-build-servers -m:1 `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

Set-Location benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release --no-build -- `
    --filter '*RopeCursorBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\short-main'
dotnet run -c Release --no-build -- `
    --filter '*RopeCursorTuningBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\short-tuning'
```

The full evidence collection uses the default BenchmarkDotNet job:

```powershell
Set-Location C:\Users\vresh\.codex\worktrees\5cd5\DataStructures\src\CSharp\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'

dotnet run -c Release --no-build -- `
    --filter '*RopeCursorBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\full-main'
dotnet run -c Release --no-build -- `
    --filter '*RopeCursorTuningBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\full-tuning'
```

Before comparing prototype/control differences, collect the control noise floor in five independent
processes. These runs retain the same single-worker environment:

```powershell
1..5 | ForEach-Object {
    dotnet run -c Release --no-build -- `
        --filter '*RopeCursorBenchmarks.IndexedRopeEditBurst*' `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-c0\noise-rope-$($_)"
}
```

## Artifact contract

| Evidence | Required location | Current state |
| --- | --- | --- |
| Short timing/allocation smoke run | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/short-*` | Pending |
| Full comparison and tuning runs | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/full-*` | Pending |
| Five independent control runs | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/noise-rope-*` | Pending |
| Untimed counters and adversarial branch traces | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/counters/` | Pending |
| Retained-version graph measurements | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/retained/` | Pending |
| Curated tables and environment summary | `src/CSharp/docs/FingerTree/benchmarks.md`, section `Axis 2 C0 rope cursor` | Pending |
| Final select/escalate/defer rationale | This document, replacing the pending status without rewriting the original provenance | Pending |

Raw BenchmarkDotNet directories are git-ignored working artifacts. The curated table must record the
exact runtime/SDK, CPU, GC mode, commands, selected parameter rows, confidence intervals, measured
noise floor, and the threshold calculation. Counter and retained-memory collectors must identify the
commit they executed and serialize their inputs; neither may be folded into the timed benchmark
methods.

## Exit outcomes

After every artifact above is populated, this record must choose exactly one result:

1. **Select class zipper-as-version** or **select readonly-struct zipper-as-version**, with the named
   workload/cadence and the proven branch scope carried verbatim into C1.
2. **Escalate to a focused-root spike** only if dirty snapshot normalization is measured as the
   blocker and the ordinary-rope regression matrix is funded.
3. **Defer C1** if neither persistent carrier clears the locked gate or if retained branching
   invalidates the proof/space bound.

The mutable-session control can explain wrapper overhead, but it cannot by itself justify selection.
Until one of these outcomes is supported by committed evidence, the status remains **evidence
collection pending**.
