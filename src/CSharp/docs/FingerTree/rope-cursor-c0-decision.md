# Rope cursor C0 representation and proof decision

- Status: Selected — readonly-struct cursor-as-version, focus 16, flush 256
- Created (UTC): 2026-07-13T06:10:29Z
- Repository HEAD: 439b83e7738907c2ff01e66da1c232ab70a80388
- Evidence commit: db05d492d2027afc44f551f886d5f91b3d42959e
- Decision closed (UTC): 2026-07-13T07:20:50Z
- Audience: Maintainers deciding whether the Axis 2 rope cursor can advance to C1
- Scope: C# positional-rope cursor prototypes, their proof boundary, and the locked C0 evidence protocol

## Decision status

**Select the readonly-struct cursor-as-version with `FocusCapacity = 16` and
`FlushChunkSize = 256`; advance to C1.** The selected named gate is 256 replacements in a 65,536
element document, locality window eight, with a canonical snapshot every sixteen edits. Against
ordinary indexed `Rope<char>` updates, the struct cursor reduced mean latency by 81.2% and allocated
bytes by 86.5%. Even the adverse confidence-interval comparison clears the 11.25% measured latency
noise floor by a wide margin. The run does not claim a p99 improvement; the 15% p99 rule therefore
does not enter the selection.

The struct is selected over the class carrier because it removes the class wrapper while retaining
the same immutable version/context engine: allocated bytes fell from 265.38 KB to 210.84 KB per
256-edit burst (20.6%), with no measured latency regression. The mutable-session result remains a
control and supplied no unique engine benefit. Dirty snapshot normalization was not the blocker, so
focused-root escalation is rejected. The retained-branch artifact preserves the narrower proof
scope: linear-lineage amortization and O(b log n) worst-case aggregate repair for fan-out from a
boundary version. C1 must publish that scope verbatim and must not claim arbitrary-version-DAG
amortized O(1).

## Prototype representations

All three prototypes execute the same focused cursor engine and differ only in the caller-visible carrier:

- the **immutable class cursor** allocates a wrapper for each navigation or edit result;
- the **readonly-struct cursor** copies a value containing references to the immutable version state
  and cursor context, avoiding the class wrapper but not the arrays and tree nodes created by edits;
- the **mutable-session control** mutates one private wrapper's version/context fields while the
  engine continues to create immutable edit state. It measures the upper bound available by removing
  result-wrapper churn; it is not an owner-token transient and must not be described as one.

The persistent version state and navigation context are intentionally separate. A
`CursorVersionState<T>` holds the logical count, the exact cursor context that can normalize that
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

## Cursor decomposition and invariants

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

The exploratory replacement sweep could not select a flush threshold because replacement does not
grow either carry. That omission was found before selection and corrected by
`RopeCursorCarryTuningBenchmarks`: every one of its sixteen focus/flush combinations performs a
2,304-element typing/backspace cycle on the left and the same insert/forward-delete cycle on the
right. Even the 128/2,048 candidate must therefore publish ordinary chunks on both sides.

`RopeCursorGateBenchmarks` is the predeclared compact full-job gate. It fixes the named workload at
65,536 elements, window eight, cadence sixteen, 256 replacements, focus sixteen, and flush 256, then
compares indexed Rope, class cursor, struct cursor, mutable-session control, and `StringBuilder`.
This avoids rerunning the 720-case exploratory matrix merely to obtain tight intervals at the one
shipment point.

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

dotnet restore Durable7.sln --disable-parallel
dotnet build Durable7.sln -c Release --no-restore --disable-build-servers -m:1 `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

Set-Location benchmarks\Durable7.FingerTree.Benchmarks
dotnet run -c Release --no-build -- `
    --filter '*RopeCursorBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\short-main'
dotnet run -c Release --no-build -- `
    --filter '*RopeCursorTuningBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\short-tuning'
dotnet run -c Release --no-build -- `
    --filter '*RopeCursorCarryTuningBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\short-carry'
```

The decision-critical comparison uses the default BenchmarkDotNet job, and the counter/retention
collector executes separately from all timed methods:

```powershell
Set-Location C:\Users\vresh\.codex\worktrees\5cd5\DataStructures\src\CSharp\benchmarks\Durable7.FingerTree.Benchmarks
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'

dotnet run -c Release --no-build -- `
    --filter '*RopeCursorGateBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-c0\full-gate'
dotnet run -c Release --no-build -- `
    --axis2-c0-evidence '.\BenchmarkDotNet.Artifacts\axis2-c0' 16 256 `
    db05d492d2027afc44f551f886d5f91b3d42959e
```

Before comparing prototype/control differences, collect the control noise floor in five independent
processes. These runs retain the same single-worker environment:

```powershell
1..5 | ForEach-Object {
    dotnet run -c Release --no-build -- `
        --filter '*RopeCursorGateBenchmarks.IndexedRope*' `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-c0\noise-rope-$($_)"
}
```

## Artifact contract

| Evidence | Required location | Current state |
| --- | --- | --- |
| Replacement and bilateral-carry tuning | `src/CSharp/benchmarks/Durable7.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/short-*` | Complete |
| Full selected-point comparison | `src/CSharp/benchmarks/Durable7.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/full-gate/` | Complete |
| Five independent control runs | `src/CSharp/benchmarks/Durable7.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/noise-rope-*` | Complete |
| Untimed counters and adversarial branch traces | `src/CSharp/benchmarks/Durable7.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/counters/` | Complete |
| Retained-version graph measurements | `src/CSharp/benchmarks/Durable7.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-c0/retained/` | Complete |
| Curated tables and environment summary | `src/CSharp/docs/FingerTree/benchmarks.md`, section `Axis 2 C0 rope cursor` | Complete |
| Final select/escalate/defer rationale | This document | Complete — select struct cursor |

Raw BenchmarkDotNet directories are git-ignored working artifacts. The curated table records the
exact runtime/SDK, CPU, GC mode, commands, selected parameter rows, confidence intervals, measured
noise floor, and threshold calculation. Counter and retained-memory collectors identify commit
`db05d492d2027afc44f551f886d5f91b3d42959e` and serialize their inputs; neither is folded into a
timed method.

## Curated evidence

The machine was Windows 11 `10.0.26300.8758`, 13th Gen Intel Core i7-1355U, BenchmarkDotNet
0.14.0, .NET SDK `11.0.100-preview.5.26302.115`, and .NET runtime 10.0.5, x64 RyuJIT AVX2 with
concurrent workstation GC. Every generated restore/build printed `/m:1`,
`BuildInParallel=false`, and `UseSharedCompilation=false`.

### Full named gate

| Lane | Mean | 99.9% CI half-width | Allocated | Mean versus indexed | Allocation versus indexed |
| --- | ---: | ---: | ---: | ---: | ---: |
| Indexed `Rope<char>` | 572.3 us | 21.50 us | 1,559.93 KB | baseline | baseline |
| Class cursor | 117.9 us | 5.89 us | 265.38 KB | 79.4% lower | 83.0% lower |
| **Readonly-struct cursor** | **107.6 us** | **4.62 us** | **210.84 KB** | **81.2% lower** | **86.5% lower** |
| Mutable-session control | 116.5 us | 3.62 us | 210.92 KB | 79.6% lower | 86.5% lower |
| `StringBuilder` control | 1,000.7 us | 54.67 us | 2,176.64 KB | 74.8% higher | 39.5% higher |

The struct/class mean difference is not used as a latency claim because their intervals nearly
touch. The representation choice instead uses the exact 54.54 KB allocation reduction (20.6%) and
the counter result: 698 class wrappers versus zero struct wrappers over the same 256 edits. The
struct has no compensating latency or retained-space regression.

### Five-process indexed-control noise floor

| Process | Mean | Median | 99.9% CI half-width | Allocated |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 649.6 us | 655.753 us | 26.10 us | 1.52 MB |
| 2 | 695.5 us | 699.040 us | 11.54 us | 1.52 MB |
| 3 | 650.6 us | 672.770 us | 21.79 us | 1.52 MB |
| 4 | 666.4 us | 677.905 us | 17.21 us | 1.52 MB |
| 5 | 609.1 us | 612.715 us | 23.14 us | 1.52 MB |

The median of process medians is 672.770 us. Their median absolute deviation is 17.017 us;
`3 * 1.4826 * MAD` is 75.687 us, or **11.25%**. The largest relative per-process 99.9% interval is
4.02%, so the locked latency threshold is 11.25%, the larger of measured noise and the 10% practical
margin. Even comparing the struct's upper interval bound (112.204 us) with the gate baseline's lower
bound (550.8 us) leaves a 79.6% improvement. Allocation was identical in all five controls, so its
threshold remains the 10% practical margin; the measured 86.5% reduction clears it. No p99 claim is
made from BenchmarkDotNet iteration means.

### Carry tuning

| Focus | Flush | Mean | Allocated |
| ---: | ---: | ---: | ---: |
| **16** | **256** | **5.474 ms** | **10.92 MB** |
| 16 | 512 | 5.844 ms | 12.50 MB |
| 16 | 1,024 | 5.814 ms | 15.30 MB |
| 16 | 2,048 | 7.188 ms | 20.32 MB |
| 32 | 256 | 5.285 ms | 11.25 MB |
| 64 | 256 | 5.784 ms | 12.15 MB |
| 128 | 256 | 5.710 ms | 14.06 MB |

ShortRun timing intervals overlap, so the decision does not manufacture a latency ranking. Flush
256 has the lowest allocation for every focus and avoids the quadratic carry-copy constant exposed
by larger thresholds. Focus 16 has the lowest allocation; focus 32's 3.5% mean advantage is below
both the practical margin and ShortRun noise. The bounded-window constants are therefore locked at
16/256.

### Counters, branching, and retention

At the named replacement gate all carriers recorded 32 spine allocations, 4,352 focus elements
copied, sixteen snapshot normalizations, and no node visits; only the class recorded 698 wrapper
allocations. Bilateral typing at 16/256 produced two ordinary chunks on the edited side. Forward
typing copied 65,801 carry elements and published four spines; reversing the completed history
produced two right chunks, copied 74,252 carry elements, and published 72 spines.

The adversarial parent had `LeftCarry.Length == 255`. Branch counts 1, 8, 64, and 256 produced
respectively 3, 24, 192, and 768 spine publications and one snapshot normalization per child at all
three document sizes (1,024, 65,536, and 1,048,576). The boundary-child buffer estimate was 88 bytes
per child. In the 65,536-element retention artifact, each materialized child retained all 32 source
backing stores by identity; the conservative parent-plus-child cursor-buffer totals were 1,224,
1,840, 6,768, and 23,664 bytes. Snapshot-storage totals deliberately double-count those shared
source arrays and are not presented as unique retained bytes.

This establishes bounded branch-local state and genuine source sharing, but it does not prove that
potential consumed by one child pays for a sibling. C1 therefore publishes linear-lineage
amortization and an O(b log n) fan-out worst-case aggregate, plus bounded per-child focus/carry
copying. That is the selected proof boundary.

## Exit outcome

**Select the readonly-struct cursor-as-version with focus 16 and flush 256.** C1 is authorized for
the positional Rope surface and the proof scope above. Focused-root escalation and deferral are
closed for C1. Any later attempt to broaden the branch-amortization claim or change the focus/carry
bounds requires a new counter, retention, and benchmark decision rather than silently editing this
record.
