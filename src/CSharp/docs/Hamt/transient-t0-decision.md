# CHAMP transient T0 workload-qualification decision

- Status: T0 advanced; T1 deciding tuple locked
- Created (UTC): 2026-07-13T06:24:55Z
- Repository HEAD: 098ceb6fed880edcbd4902c2b5940f43d005e3da
- Evidence commit: a82818e9d2a53f314ad7e89a7b3180d6f5507c0f
- Audience: Maintainers deciding whether an owner-token CHAMP experiment is justified
- Scope: C# persistent-map and `BulkBuilder` baselines for the Axis 2 T0 opportunity gate

## Decision status

**Advance to T1** on one locked deciding tuple: a 100,000-entry base, 512 clustered-prefix
replacements, and one publication at the end. The exhaustive persistent census found 511
non-publication wrappers and upper bounds of 448 repeatedly copied nodes and 448 repeatedly copied
arrays in that tuple. Those counts plausibly clear the predeclared 10% materiality threshold and
justify measuring a production-representative private kernel.

This is an opportunity result, not a transient performance claim. The tuple and exact filter were
selected and committed before any owner-kernel timing was observed. T1 must still compare the two
locked ownership layouts, charge ordinary-map and edited-graph retention, establish failure
atomicity and O(1) adoption/publication, and beat the unchanged persistent control beyond its
measured noise floor before any public API is authorized.

The distinction is load-bearing:

- an **opportunity signal** is a persistent baseline with enough non-publication wrappers and
  repeated copied nodes/arrays that removing some of them could plausibly clear the predeclared
  materiality threshold; and
- an **actual win** requires T1 to preserve every semantic contract, pay its adoption/token/sealing
  costs, and still beat the identical persistent workload by more than the larger of the measured
  noise floor and the 10% practical latency/allocation margin.

T0 advances only on the first claim. Direct persistent operations remain the shipped answer while
T1 is evaluated. The existing `BulkBuilder` is not a competitive editing substitute in the deciding
tuple: its short-run construction control was about 151 times slower and allocated about 107 times
as much as the direct persistent history.

## Locked baseline matrix

The executable source of truth is `Axis2BenchmarkPolicy.cs`. The edit/publication suite crosses:

- base counts 0, 1, 8, 32, 1,024, and 100,000;
- edit counts 1, 8, 64, and successive powers of eight through the exact base count (the empty-base
  case keeps one edit so insertion/removal no-op behavior remains observable);
- publication after every 1, 8, or 64 edits, or only at the end;
- repeated same-key replacements, hashes sharing at least the first three 5-bit CHAMP levels,
  disjoint inserts, equal-full-hash collision-bucket edits, removals, and deterministic mixed
  histories; and
- the same entry arrays, edit arrays, singleton comparer object, operation order, publication
  checksum, and escaping final-map result in every comparable timed lane.

The persistent lane starts from a prebuilt retained base and performs every edit. The
`BulkBuilder` lane is an existing construction/staging control, not a transient surrogate: it
materializes the same semantic state at each requested publication boundary and therefore exposes
the O(n) rebuild cost that a builder pays when adopted for arbitrary editing. Neither lane includes
counter collection, graph walking, callback validation, or result comparison in its timed body.

The full-collision history targets equal full hashes, not merely a shared prefix. At base counts up
to 32 every entry is in that collision bucket; larger otherwise-uniform bases retain a 32-entry
equal-full-hash bucket so the 100,000-entry sweep does not turn setup into an unrelated quadratic
collision-construction test. Every key selected by the collision history is in that bucket, and the
machine-readable record reports its exact size alongside the nominal base count.

## Untimed counter contract

The persistent benchmark setup emits a versioned `AXIS2_T0_COUNTER_V1` CSV record to the raw
BenchmarkDotNet log. It reports requested and sampled edit counts, publications, changed operations,
wrapper allocations, copied nodes, copied arrays, node visits, and the subset occurring before a
publication boundary. Graph-difference diagnostics are capped to the first 64 edits so the 100,000
entry matrix does not turn untimed evidence collection into a quadratic retained-graph scan. Full
wrapper and callback counts still cover every requested edit.

The non-publication copied-node/array fields are an **upper bound on reusable-path opportunity**, not
a prediction of T1 savings: a faithful transient must still copy each shared node/array on first
write in a session. The wrapper field is similarly gross pressure before accounting for the
transient session and published-map wrappers.

Hash/equality callback counts come from a separate counting-comparer replay over the identical
entries and edits. That replay never invokes `GetMutationDiagnostics` or
`CountNodeVisitsForDiagnostics`, because either would hash the edited key again and corrupt the
callback count. The graph-diagnostic replay uses the ordinary locked comparer and may rehash solely
for its untimed node-visit observation. Callback counts and structural counts must not be added as
though they were the same execution.

## Exact single-worker commands

Run from the direct-separate worktree. Resolve its root rather than copying a machine-specific path.
These settings disable NuGet parallel restore, MSBuild project parallelism, compiler-server sharing,
MSBuild node reuse, and .NET build servers. BenchmarkDotNet executes cases sequentially; do not
launch the commands concurrently.

```powershell
$repo = (git rev-parse --show-toplevel).Trim()
Set-Location (Join-Path $repo 'src\CSharp')
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore DataStructures.sln --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build DataStructures.sln -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

# Scrub credentials before any BenchmarkDotNet child process captures its environment.
Get-ChildItem Env: | Where-Object {
    $_.Name -match '(?i)(TOKEN|KEY|SECRET|PASSWORD|CREDENTIAL|CONNECTION|COOKIE|AUTH|IGCCSVC)'
} | Remove-Item -ErrorAction SilentlyContinue

Set-Location benchmarks\Tools.DataStructures.FingerTree.Benchmarks
$driver = '.\bin\Release\net10.0\Tools.DataStructures.FingerTree.Benchmarks.dll'

# Exhaustive opportunity-counter census. Dry timings are not deciding evidence.
dotnet $driver `
    --filter '*TransientLifecycleBenchmarks.PersistentHistory*' --job Dry `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t0\dry-persistent'
```

The class-wide filter used by the initial pending protocol is no longer valid after the private T1
lane was added: it would expose owner-kernel results before the T0 tuple was locked. The census above
therefore selects only `PersistentHistory`. It completed all 456 combinations. Its environment log
was reduced to the 456 versioned counter rows after collection; the exported timing reports remain
alongside that sanitized CSV.

The exact deciding filters, locked before any T1 timing, are:

```powershell
$persistentEndFilter = '*TransientLifecycleBenchmarks.PersistentHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512*'
$builderFilter = '*TransientLifecycleBenchmarks.BulkBuilderHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512*'
$directEndFilter = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512*'

dotnet $driver --filter $persistentEndFilter --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t0\candidate-persistent-short'
dotnet $driver --filter $builderFilter --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t0\candidate-bulk-builder-short'
dotnet $driver --filter $directEndFilter --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\candidate-direct-separate-short'
```

The surviving measurement branch is `codex/axis2-t1-direct-separate-gate`.
`SeparateNodeKernelHistory` uses direct editable branch/collision classes with no shared abstract
branch or collision base. Ordinary `CollisionNode` and `BitmapIndexedNode` retain their b590 source
and physical shape. A two-bit mask records data-array and child-array ownership independently, so
wrapping one newly copied array does not falsely claim or eagerly copy the other shared array. Its
versioned counter row remains `layout=separate-nodes` for artifact compatibility.

The owner-field evidence is historical and belongs to
`codex/axis2-t1-owner-fields-gate`; it is not a second method on this branch. Likewise, the earlier
abstract-base owner-free formulation belongs to `codex/axis2-t1-separate-gate`. Do not combine
their artifacts with the direct-separate candidate or rerun their filters from this worktree. This
document records no direct-separate gate outcome until the locked correctness, ordinary-regression,
and candidate measurements have completed.

The full T1 command matrix below is also locked before direct-separate timing is inspected. Commands
intentionally omit `--job`, selecting BenchmarkDotNet's default full job. The deciding persistent
control runs in five independent processes; every other lane runs in its own process. The Every64
pair corroborates repeated publication, the EveryEdit pair guards the sparse case, and the ordinary
lookup/update controls guard the unchanged persistent surface. Preserve every filter verbatim in the
curated record.

```powershell
$persistentEvery64Filter = '*TransientLifecycleBenchmarks.PersistentHistory*History: ClusteredPrefix*PublicationCadence: Every64*Workload: N100000_E512*'
$directEvery64Filter = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: Every64*Workload: N100000_E512*'
$ordinaryUpdateFilter = '*TransientLifecycleBenchmarks.PersistentHistory*History: ClusteredPrefix*PublicationCadence: EveryEdit*Workload: N100000_E1*'
$directSparseFilter = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: EveryEdit*Workload: N100000_E1*'
$ordinaryLookupFilter = '*ChampBenchmarks.ChampLookup*'

1..5 | ForEach-Object {
    dotnet $driver --filter $persistentEndFilter `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-t1\noise-persistent-$($_)"
}

dotnet $driver --filter $directEndFilter `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\candidate-direct-separate-full'

dotnet $driver --filter $persistentEvery64Filter `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\corroboration-persistent-every64-full'
dotnet $driver --filter $directEvery64Filter `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\corroboration-direct-every64-full'

dotnet $driver --filter $ordinaryUpdateFilter `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\ordinary-update-every-edit-full'
dotnet $driver --filter $directSparseFilter `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\guard-direct-every-edit-full'

dotnet $driver --filter $ordinaryLookupFilter `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\ordinary-lookup-full'
```

## Artifact contract

| Evidence | Required location | Current state |
| --- | --- | --- |
| Exhaustive 456-case persistent counter census | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/dry-persistent/` | Complete; 456 sanitized CSV rows plus reports |
| Selected persistent short control | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/candidate-persistent-short/` | Complete |
| Selected `BulkBuilder` short control | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/candidate-bulk-builder-short/` | Complete |
| Five independent selected-control noise runs | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/noise-persistent-*/` | Required by T1; not part of the opportunity decision |
| Full direct-separate deciding candidate | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/candidate-direct-separate-full/` | Required by T1; pending |
| Full Every64 persistent corroboration | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/corroboration-persistent-every64-full/` | Required by T1; pending |
| Full Every64 direct-separate corroboration | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/corroboration-direct-every64-full/` | Required by T1; pending |
| Full sparse ordinary update guard | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/ordinary-update-every-edit-full/` | Required by T1; pending |
| Full sparse direct-separate guard | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/guard-direct-every-edit-full/` | Required by T1; pending |
| Full ordinary lookup regression control | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t1/ordinary-lookup-full/` | Required by T1; pending |
| Curated matrix and counter table | This document, section `Curated evidence` | Complete |
| T0 advance/defer result | This document | **Advance to T1** |

Raw BenchmarkDotNet artifacts are git-ignored. The curated evidence must retain the executed commit,
runtime/SDK, CPU, GC mode, exact filters, all selected parameter values, confidence intervals,
measured noise floor, threshold calculation, and the distinction between allocation bytes and the
untimed copied-graph counters.

## Curated evidence

The census and short controls ran on Windows 11 `10.0.26300.8758`, a 13th Gen Intel Core i7-1355U
(10 physical, 12 logical cores), .NET SDK `11.0.100-preview.5.26302.115`, and .NET runtime `10.0.5`
with concurrent workstation GC. BenchmarkDotNet `0.14.0` built each generated harness with `/m:1`,
`BuildInParallel=false`, and `UseSharedCompilation=false`; cases ran sequentially.

The three load-bearing counter rows are:

| Role | Base | Edits | History | Cadence | Publications | Wrappers | Non-publication wrappers | Sampled node visits | Copied nodes | Copied arrays | Reusable-node upper bound | Reusable-array upper bound | Hash/equality callbacks |
| --- | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Deciding tuple | 100,000 | 512 | Clustered prefix | End | 1 | 512 | 511 | 448 | 448 | 448 | 448 | 448 | 512 / 512 |
| Publication corroboration | 100,000 | 512 | Clustered prefix | Every 64 | 8 | 512 | 504 | 448 | 448 | 448 | 441 | 441 | 512 / 512 |
| Sparse guardrail | 100,000 | 1 | Clustered prefix | Every edit | 1 | 1 | 0 | 7 | 7 | 7 | 0 | 0 | 1 / 1 |

The contrast is the T0 decision. A sparse edit offers no reusable pre-publication work, while the
deciding tuple makes essentially every sampled path copy and all but one map wrapper intermediate.
The 64-edit cadence retains nearly the same opportunity across eight sessions, so the signal is not
dependent on a single token living for the entire history.

Short-run timings are contextual only because three samples give wide 99.9% intervals:

| Lane | Mean | Error | Median | Allocated |
| --- | ---: | ---: | ---: | ---: |
| Direct persistent | 614.743 us | 302.076 us | 609.845 us | 728 KB |
| `BulkBuilder` rebuild | 92.611 ms | 113.060 ms | 92.169 ms | 76.25 MB |

The builder is approximately 150.65 times slower and allocates 107.25 times as much in this
editing history. It remains the canonical fresh-construction tool, but it does not remove the need
to test T1. These Dry/Short timings do not set the T1 threshold; five independent full persistent
controls do that after this tuple lock.

## Exit outcomes

T0 exits with **Advance to T1** for `N100000_E512 / ClusteredPrefix / End`. This authorizes only the
two private production-representative ownership layouts and their gate. It does not establish an
owner-token win. T1 must still run five full persistent controls, compare both layouts on this exact
tuple, corroborate `Every64`, reject regression at `N100000_E1/EveryEdit`, and charge ordinary and
published retained memory. Only that later decision can authorize T2 public API design.
