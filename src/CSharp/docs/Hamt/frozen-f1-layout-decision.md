# Frozen hash F1 fixed-layout decision

- Status: Evidence collection pending
- Created (UTC): 2026-07-13T07:18:58Z
- Repository HEAD: db05d492d2027afc44f551f886d5f91b3d42959e
- Audience: Maintainers deciding whether the Axis 2 frozen hash tier can advance to F2
- Scope: C# benchmark-local linear, Robin-Hood, and quadratic packed-index layouts

## Decision status

**Evidence collection pending.** F1 has not selected a layout and does not authorize a public
`FrozenHashMap` or `FrozenHashSet`. The three repository candidates live only in the benchmark
assembly. Automatic choice by count, key type, comparer type, hit ratio, or observed hash shape is
deliberately absent: the F2 gate must select one fixed general layout or defer the track.

Selection requires one candidate to clear the predeclared materiality rule in a named read-heavy
regime after lookup, enumeration, construction, retained arrays, and calculated break-even reads
are considered together. The threshold is the larger of the measured noise floor and 10% for mean
latency, allocation, and retained bytes; a p99 claim additionally requires 15%. A win on one hit
ratio cannot conceal a material regression in another load-bearing metric.

## Fixed candidates

Every repository candidate stores real `(hash, key, value)` entries in the exact enumeration order
of the source `PersistentHashMap`. Its separate `int[]` index uses zero for empty and source entry
index plus one for occupied. Index rearrangement therefore cannot reorder enumeration or replace a
stored key representative.

| Candidate | Fixed index rule | Slot sizing | Lookup termination |
| --- | --- | --- | --- |
| Linear | Advance one slot with wraparound | Smallest table at no more than 70% load | First empty slot |
| Robin Hood | Linear probing with lower-displacement residents swapped behind higher-displacement candidates | Same 70% rule as linear | First empty slot or resident displacement below the search displacement |
| Quadratic | Triangular increments `1, 2, 3, ...` | Power of two at no more than 70% load | First empty slot; the power-of-two triangular sequence reaches every slot |

All three retain the exact comparer object and recompute real hashes through it during construction
and lookup. They do not special-case null, collisions, runtime key types, or comparer types. An
equal-full-hash run therefore performs the semantically required equality scan. Comparer exceptions
propagate without lookup-time mutation, and a constructor failure cannot publish a partial value.

`System.Collections.Frozen.FrozenDictionary` is a measurement control, not an implementation
candidate and not a source of repository enumeration or stored-representative contracts.

## Locked matrix and setup oracle

All comparable methods use the same source CHAMP map, entry sequence, comparer object, shuffled
1,024-probe array, hit ratio, and result-consumption shape. The matrix is:

- uniform hashes at counts 1, 8, 32, 1,024, and 100,000;
- hashes sharing weak low prefixes at counts 8, 32, and 1,024;
- one equal-full-hash run at counts 8, 32, and 1,024;
- 0%, 50%, and 100% lookup hits in every non-null class;
- null plus case-insensitive stored representatives in equal-full-hash buckets of 8 and 32 entries;
- lookup, full enumeration, and construction from the same retained source map; and
- the three repository candidates, BCL `FrozenDictionary`, shipped persistent CHAMP, mutable
  `Dictionary`, and `ImmutableDictionary` wherever those controls can represent the lane.

The clustered and full-collision classes stop at 1,024 because the fixed quadratic candidate is
intentionally sensitive to weak low bits. Larger counts would turn setup into an unrelated
multi-billion-probe stress run rather than a usable layout comparison. The uniform class still
covers 100,000 entries.

Every `[GlobalSetup]` validates count and lookup parity against the source map before timing. It also
checks exact comparer identity, every repository prototype's source-order enumeration, and stored
key recovery. The null/collision setup checks every original key object by reference, including
null, after later equivalent spellings replace the values. BCL mutable, immutable, and frozen
controls are omitted from that lane because they reject null keys; a sentinel would change the
semantics.

## Retained-array evidence

Setup emits one machine-readable `AXIS2_F1_RETAINED_V1` row per parameter case. For each repository
prototype it reports entry-array bytes, slot-array bytes, slot count, load, and their total. Those
estimates use the actual closed entry struct size and CoreCLR SZARRAY layout. The BCL control is
reported by walking only arrays reachable through this runtime's
`System.Collections.Frozen` implementation objects. Comparers and key/value payload object graphs
are excluded from every layout. The BCL estimate is consequently runtime-specific evidence, not a
portable contract.

The persistent CHAMP graph estimate is reported alongside the array totals but is named separately;
it must not be relabeled as either retained arrays or allocation bytes. The null lane records the BCL
field as `omitted-null-semantics`. `MemoryDiagnoser` construction allocation remains a different
metric. Break-even reads are calculated independently for each candidate from its construction
mean and its per-read saving relative to the already-existing persistent map; a non-faster lookup
has infinite lookup break-even and may still be considered only if a different named read regime
materially wins.

## Exact single-worker commands

Run from this worktree. The environment settings apply to BenchmarkDotNet's generated projects as
well as the explicit build. NuGet restore, MSBuild node reuse/project parallelism, compiler sharing,
and .NET build servers remain disabled throughout; do not run any command concurrently.

```powershell
Set-Location C:\Users\vresh\.codex\worktrees\5cd5\DataStructures\src\CSharp
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore DataStructures.sln --disable-parallel
dotnet build DataStructures.sln -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

Set-Location benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release --no-build -- `
    --filter '*FrozenLookupBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\short-uniform'
dotnet run -c Release --no-build -- `
    --filter '*FrozenClusteredLookupBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\short-clustered'
dotnet run -c Release --no-build -- `
    --filter '*FrozenCollisionLookupBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\short-collision'
dotnet run -c Release --no-build -- `
    --filter '*FrozenNullLookupBenchmarks*' --job short `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\short-null'
```

Collect decision evidence with the default full job in four sequential processes:

```powershell
dotnet run -c Release --no-build -- `
    --filter '*FrozenLookupBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\full-uniform'
dotnet run -c Release --no-build -- `
    --filter '*FrozenClusteredLookupBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\full-clustered'
dotnet run -c Release --no-build -- `
    --filter '*FrozenCollisionLookupBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\full-collision'
dotnet run -c Release --no-build -- `
    --filter '*FrozenNullLookupBenchmarks*' `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f1\full-null'
```

Measure the unchanged uniform BCL lookup control in five independent processes before comparing a
candidate/control difference:

```powershell
1..5 | ForEach-Object {
    dotnet run -c Release --no-build -- `
        --filter '*FrozenLookupBenchmarks.BclFrozenLookupMix*' `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-f1\noise-bcl-frozen-$($_)"
}
```

## Artifact contract

| Evidence | Required location | Current state |
| --- | --- | --- |
| Short semantic/timing smoke matrix | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-f1/short-*` | Pending |
| Full uniform/clustered/collision/null comparisons | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-f1/full-*` | Pending |
| Five independent BCL control runs | `src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/BenchmarkDotNet.Artifacts/axis2-f1/noise-bcl-frozen-*` | Pending |
| Retained-array `AXIS2_F1_RETAINED_V1` rows | Raw logs from the matching short/full directories | Pending |
| Curated tables, break-even calculations, environment, and threshold calculation | This document, section `Curated evidence` | Pending |
| F1 select/defer result | This document, replacing the pending status without rewriting provenance | Pending |

Raw BenchmarkDotNet directories are git-ignored. Curated evidence must retain the executed commit,
runtime/SDK, CPU, GC mode, exact filters and parameters, confidence intervals, five-process noise
calculation, retained-array rows, and per-candidate break-even reads.

## Curated evidence

Pending. No F1 timing, allocation, retained-array, noise-floor, or break-even result has been
collected for this record.

## Exit outcomes

Once every required artifact exists, replace the pending status with exactly one conclusion:

1. **Select linear**, **select Robin Hood**, or **select quadratic**, naming the read-heavy regime
   and carrying that one fixed layout into F2 without adaptive dispatch.
2. **Defer Track F** because no candidate clears the materiality and break-even gate without an
   unacceptable construction, memory, collision, or enumeration regression.

The BCL control cannot be selected as the repository implementation. F2 remains unauthorized until
one of these evidence-backed outcomes is committed.
