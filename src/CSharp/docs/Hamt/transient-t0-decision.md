# CHAMP transient T0 workload-qualification decision

- Status: Evidence collection pending
- Created (UTC): 2026-07-13T06:24:55Z
- Repository HEAD: 098ceb6fed880edcbd4902c2b5940f43d005e3da
- Audience: Maintainers deciding whether an owner-token CHAMP experiment is justified
- Scope: C# persistent-map and `BulkBuilder` baselines for the Axis 2 T0 opportunity gate

## Decision status

**Evidence collection pending.** T0 has not established that a transient is faster, allocates less,
or should be implemented. The committed matrix and counters only qualify whether the shipped
persistent map creates enough repeated wrapper and path-copy pressure in a named workload to justify
T1, the production-representative private owner-token kernel.

The distinction is load-bearing:

- an **opportunity signal** is a persistent baseline with enough non-publication wrappers and
  repeated copied nodes/arrays that removing some of them could plausibly clear the predeclared
  materiality threshold; and
- an **actual win** requires T1 to preserve every semantic contract, pay its adoption/token/sealing
  costs, and still beat the identical persistent workload by more than the larger of the measured
  noise floor and the 10% practical latency/allocation margin.

T0 may advance only on the first claim. It cannot make the second claim because no transient kernel
exists in this phase. If the counter and timing artifacts do not identify a credible named regime,
Track T is deferred and direct persistent operations plus `BulkBuilder` remain authoritative.

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

Run from this worktree. These settings disable NuGet parallel restore, MSBuild project parallelism,
compiler-server sharing, MSBuild node reuse, and .NET build servers. BenchmarkDotNet executes cases
sequentially; do not launch the commands concurrently.

```powershell
Set-Location C:\Users\vresh\.codex\worktrees\5cd5\DataStructures\src\CSharp
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore DataStructures.sln --disable-parallel
dotnet build DataStructures.sln -c Release --no-restore --disable-build-servers -m:1 `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

Set-Location benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release --no-build -- `
    --filter '*TransientLifecycleBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t0\short'
```

Collect the full persistent and `BulkBuilder` matrix in separate sequential processes so an
interrupted builder control does not invalidate persistent evidence:

```powershell
dotnet run -c Release --no-build -- `
    --filter '*TransientLifecycleBenchmarks.PersistentHistory*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t0\full-persistent'
dotnet run -c Release --no-build -- `
    --filter '*TransientLifecycleBenchmarks.BulkBuilderHistory*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t0\full-bulk-builder'
```

After the counter report names a candidate workload, collect its unchanged persistent control in
five independent processes. Preserve the selected benchmark filter verbatim in the curated record;
`<candidate-filter>` is replaced once, before any T1 result is inspected.

```powershell
1..5 | ForEach-Object {
    dotnet run -c Release --no-build -- `
        --filter '<candidate-filter>' `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-t0\noise-persistent-$($_)"
}
```

## Artifact contract

| Evidence | Required location | Current state |
| --- | --- | --- |
| Short matrix smoke run | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/short/` | Pending |
| Full persistent timings, allocations, and `AXIS2_T0_COUNTER_V1` log rows | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/full-persistent/` | Pending |
| Full `BulkBuilder` control | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/full-bulk-builder/` | Pending |
| Five independent selected-control noise runs | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-t0/noise-persistent-*/` | Pending |
| Curated matrix, counter table, environment, and threshold calculation | This document, section `Curated evidence` | Pending |
| T0 advance/defer result | This document, replacing the pending status without rewriting provenance | Pending |

Raw BenchmarkDotNet artifacts are git-ignored. The curated evidence must retain the executed commit,
runtime/SDK, CPU, GC mode, exact filters, all selected parameter values, confidence intervals,
measured noise floor, threshold calculation, and the distinction between allocation bytes and the
untimed copied-graph counters.

## Curated evidence

Pending. No timings, allocation results, counter rows, or threshold comparisons have been collected
for this record.

## Exit outcomes

Once every required artifact exists, replace the pending status with exactly one conclusion:

1. **Advance to T1**, naming the base size, edit history, edit count, publication cadence, and gross
   wrapper/path-copy opportunity that plausibly clears the locked threshold. This authorizes only a
   private production-representative kernel and does not claim a transient win.
2. **Defer Track T** because no named baseline regime has sufficient opportunity or because the
   credible regimes are already better served by direct persistent operations or `BulkBuilder`.

Only a later T1 record can establish an actual owner-token performance win or authorize T2 public
API design.
