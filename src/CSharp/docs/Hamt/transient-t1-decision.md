# CHAMP transient T1 representation decision

- Status: Selected — direct separate-node kernel; advance to T2
- Created (UTC): 2026-07-13T13:39:29Z
- Repository HEAD: d048b77629a5f07fac94890b64902b479da360f6
- Evidence commit: d048b77629a5f07fac94890b64902b479da360f6
- Decision closed (UTC): 2026-07-13T13:39:29Z
- Audience: Maintainers deciding whether the Axis 2 CHAMP transient may acquire a public API
- Scope: C# T1 ownership representation, correctness evidence, retained layout, and locked benchmark gate

## Decision status

**Select the direct separate-node representation and advance to T2.** The private kernel clears the
locked T0 regime by a material margin after charging its complete timed allocation and reachable
edited-graph retention:

- at `N100000_E512 / ClusteredPrefix / End`, its 99.9% confidence-interval upper bound remains below
  the latency cutoff established by five independent persistent controls;
- allocation falls from 700 KB to 253.66 KB, while published retained bytes rise by only 0.197%;
- `Every64` independently corroborates the result, including under adverse confidence-interval
  endpoints;
- adoption and publication perform zero graph visits, ordinary nodes retain zero owner metadata,
  and the one-edit guard publishes an entirely ordinary graph; and
- the focused 26-test kernel suite, all 190 HAMT tests, and the full Release solution build pass.

This authorizes implementation of the public T2 lifecycle. It does **not** ship a public
`Transient` type, freeze the T2 API shape, or change the recommendation for one-off edits. Direct
persistent operations remain preferable for sparse histories; the selected kernel is justified by
many edits per publication.

## Selected representation

The selected kernel adds exact-type transient-editable branch and collision classes alongside the
ordinary sealed CHAMP hierarchy. It does not add an owner field, ownership flag, or common mutable
base to `LeafNode`, `CollisionNode`, or `BitmapIndexedNode`.

Its lifecycle is deliberately asymmetric:

1. adoption stores the persistent source/root in O(1), without a graph walk or edit token;
2. the first successful edit delegates to the ordinary persistent operation and caches that exact
   immutable wrapper;
3. a later successful edit promotes only reusable branch/collision paths into the separate-node
   hierarchy; leaf-only histories may remain ordinary;
4. a two-bit ownership mask tracks the editable node's `Data` and `Children` arrays independently,
   so copying one array never grants write ownership of the other;
5. the edit token and bounded prepare/commit plan are allocated only when promotion needs them; and
6. publication first prepares the immutable wrapper, applies only non-throwing commits, consumes the
   session, and returns in O(1) without sealing traversal.

The first-edit deferral is load-bearing. A one-edit session retains no token, no commit plan, and no
separate node; its publication returns the cached ordinary wrapper. Long sessions pay the editable
machinery only after there is reusable work.

Diagnostics are fixed at adoption. Timed production kernels cannot later acquire partial counters,
while diagnostic kernels support deterministic allocation/callback/publication failure injection.
Setup replays both modes and requires semantic, canonical, and retained-layout equality before the
timed method may run.

## Rejected ownership layouts

The comparison branches remain historical evidence and are not combined with the selected timings:

- `codex/axis2-t1-owner-fields-gate` places ownership state in mutable-capable persistent nodes. It
  perturbs the ordinary layout even for callers that never create a transient, contrary to the
  ordinary-map retained-size guard.
- `codex/axis2-t1-separate-gate` uses an owner-free but shared abstract branch/collision formulation.
  It avoids per-node owner fields, but changes the ordinary exact-type hierarchy and its hot-path
  assumptions.
- `codex/axis2-t1-direct-separate-gate` keeps the b590 ordinary field layout and monomorphic lookup
  source fingerprints exact, while direct transient node types carry all edit-only state. It is the
  only candidate used by this decision.

The selected representation therefore pays editability only in graphs that actually reuse a path.
Published mixed graphs remain valid persistent CHAMPs and are covered by lookup, enumeration,
update/remove, semantic equality, bidirectional diff, and map-algebra tests.

## Lifecycle, ownership, and failure invariants

The private kernel establishes the invariants required before a public lifecycle is credible:

- a node may be mutated only under the active token, and an array may be written only when its own
  ownership bit is set;
- every hash/equality/value callback and every replacement allocation completes before the first
  in-place write of an operation;
- commit applies only field/reference assignments that cannot throw;
- any failed edit preserves content, root identity, token/plan presence, version, enumerator
  validity, and the identities of the source and cached deferred wrapper;
- a token or commit plan first created by a failed promotion is discarded rather than retained;
- publication allocation/failure leaves the session active and retryable, while successful
  publication consumes it exactly once;
- the base map and every previously published version remain isolated from later edits;
- comparer identity, first equivalent-key representative retention, collision order semantics,
  canonical branch contraction, and recursive counts match ordinary CHAMP behavior; and
- clean adoption/publication and deferred ordinary publication visit no nodes.

## Locked single-worker protocol

All evidence was collected from the evidence commit above. Restore, build, generated
BenchmarkDotNet restore/build, and benchmark execution were serialized. NuGet parallel restore,
MSBuild project parallelism, compiler-server sharing, MSBuild node reuse, and .NET build servers
were disabled. The final benchmark processes were pinned to logical processor 0 (`--affinity 1`).

The host is heterogeneous. An exact-SHA unpinned pilot scheduled across different core classes and
produced persistent-control medians from roughly 417 us through 1 ms. Its complete artifact is
archived under `axis2-t1-unpinned-1befaa2/`, classified as inconclusive, and excluded from every
calculation. The affinity protocol was committed before any pinned candidate run, and the entire
deciding matrix was recollected under that protocol.

Run from the direct-separate worktree:

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

# Prevent BenchmarkDotNet child processes from capturing credentials.
Get-ChildItem Env: | Where-Object {
    $_.Name -match '(?i)(TOKEN|KEY|SECRET|PASSWORD|CREDENTIAL|CONNECTION|COOKIE|AUTH|IGCCSVC)'
} | Remove-Item -ErrorAction SilentlyContinue

Set-Location benchmarks\Tools.DataStructures.FingerTree.Benchmarks
$driver = '.\bin\Release\net10.0\Tools.DataStructures.FingerTree.Benchmarks.dll'
$affinityMask = 1
$persistentEnd = '*TransientLifecycleBenchmarks.PersistentHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512)'
$directEnd = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512)'
$persistent64 = '*TransientLifecycleBenchmarks.PersistentHistory*History: ClusteredPrefix*PublicationCadence: Every64*Workload: N100000_E512)'
$direct64 = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: Every64*Workload: N100000_E512)'
$persistentSparse = '*TransientLifecycleBenchmarks.PersistentHistory*History: ClusteredPrefix*PublicationCadence: EveryEdit*Workload: N100000_E1)'
$directSparse = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: EveryEdit*Workload: N100000_E1)'

1..5 | ForEach-Object {
    dotnet $driver --filter $persistentEnd --affinity $affinityMask `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-t1\noise-persistent-$($_)"
}
dotnet $driver --filter $directEnd --affinity $affinityMask `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\candidate-direct-separate-full'
dotnet $driver --filter $persistent64 --affinity $affinityMask `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\corroboration-persistent-every64-full'
dotnet $driver --filter $direct64 --affinity $affinityMask `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\corroboration-direct-every64-full'
dotnet $driver --filter $persistentSparse --affinity $affinityMask `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\ordinary-update-every-edit-full'
dotnet $driver --filter $directSparse --affinity $affinityMask `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\guard-direct-every-edit-full'
dotnet $driver --filter '*ChampBenchmarks.ChampLookup*' --affinity $affinityMask `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t1\ordinary-lookup-full'
```

The literal closing parenthesis in each parameterized filter is significant. It prevents the E1
guard from also selecting `N100000_E100000`. Every final lifecycle CSV contains exactly one intended
row; the lookup CSV contains exactly its two configured counts.

## Artifact contract

Raw artifacts are git-ignored; this document is the durable curated record.

| Evidence | Artifact under `BenchmarkDotNet.Artifacts/` | State |
| --- | --- | --- |
| Excluded unpinned pilot | `axis2-t1-unpinned-1befaa2/` | Complete; inconclusive and excluded |
| Five independent End controls | `axis2-t1/noise-persistent-1/` through `-5/` | Complete |
| Direct End candidate | `axis2-t1/candidate-direct-separate-full/` | Complete |
| Persistent/direct Every64 pair | `axis2-t1/corroboration-*-every64-full/` | Complete |
| Persistent/direct sparse pair | `axis2-t1/ordinary-update-every-edit-full/`, `guard-direct-every-edit-full/` | Complete |
| Ordinary lookup guard | `axis2-t1/ordinary-lookup-full/` | Complete |

Four unpinned ShortRun tuning directories under `axis2-t1/` are exploratory and excluded. No Short
result contributes to the decision.

## Environment and noise floor

The pinned matrix ran on Windows 11 `10.0.26300.8758`, a 13th Gen Intel Core i7-1355U (10 physical,
12 logical cores), .NET SDK `11.0.100-preview.5.26302.115`, .NET runtime `10.0.5` x64 RyuJIT AVX2,
concurrent workstation GC, and BenchmarkDotNet `0.14.0`. Every generated harness records
`Affinity=000000000001`, `/m:1`, `BuildInParallel=false`, and `UseSharedCompilation=false`.

The five independent persistent End controls were:

| Process | Mean | Median | 99.9% confidence interval | Allocated |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 418.543 us | 404.761 us | 402.184–434.902 us | 700 KB |
| 2 | 426.761 us | 411.834 us | 411.855–441.667 us | 700 KB |
| 3 | 443.293 us | 440.547 us | 427.409–459.177 us | 700 KB |
| 4 | 514.387 us | 486.688 us | 485.440–543.335 us | 700 KB |
| 5 | 545.988 us | 523.498 us | 511.537–580.440 us | 700 KB |

The noise-floor center is the median of process medians, 440.547 us. Their median absolute deviation
is 35.786 us; scaling by 1.4826 and then by three gives 159.1689708 us, or 36.1298501% of that center.
The largest per-process relative 99.9% confidence interval is 6.31%. The locked latency threshold is
therefore `max(10%, 36.1298501%) = 36.1298501%`. The policy's mean-latency point comparison uses the
median of the five process means, 443.293 us; applying the locked threshold gives a deciding upper
cutoff of 283.132 us. All five allocation observations are 700 KB, so the allocation and retained-
byte thresholds remain their predeclared 10% practical margins.

## Timed result

### Deciding End lane

| Lane | Mean | Median | 99.9% confidence interval | Allocated | Reachable retained bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Persistent median-of-process centers | 443.293 us | 440.547 us | five processes above | 700 KB | 4,306,320 |
| Direct separate-node kernel | 227.600 us | 216.741 us | 217.736–237.464 us | 253.66 KB | 4,314,808 |

Relative to the robust mean control center, the candidate mean is 48.66% lower. More conservatively,
its entire confidence interval is below the 283.132 us cutoff; the upper endpoint is still 46.43%
below the mean control center. Allocation falls by about 63.76%. Published reachable retention rises
by 8,488 bytes, or 0.1971%, well below the 10% retained-byte guard.

### Every64 corroboration

| Lane | Mean | Median | 99.9% confidence interval | Allocated | Reachable retained bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Persistent | 522.335 us | 494.918 us | 483.356–561.313 us | 700 KB | 4,306,320 |
| Direct separate-node kernel | 278.876 us | 273.665 us | 266.448–291.304 us | 349.25 KB | 4,314,864 |

The candidate improves mean latency by 46.61% and allocation by 50.11%. Even the deliberately
adverse comparison `1 - 291.304 / 483.356 = 39.73%` clears the 36.13% latency threshold. Reachable
retention rises by 8,544 bytes, or 0.1984%.

### Sparse EveryEdit guard

| Lane | Mean | Median | 99.9% confidence interval | Allocated | Reachable retained bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Persistent | 581.787 ns | 563.411 ns | 563.876–599.698 ns | 1,400 B | 4,306,320 |
| Direct separate-node kernel | 645.411 ns | 618.623 ns | 616.297–674.525 ns | 1,488 B | 4,306,320 |

This is a boundary result, not a win claim. Mean latency increases by 10.94%; median/p50 increases by
9.80%, but mean is the policy statistic and the median is not used to claim an unqualified
“under 10%” pass. Combining the confidence bounds gives an observed slowdown interval of roughly
2.77%–19.62%, so the bare 10% line is inconclusive. The result remains well within the locked 36.13%
noise-qualified regression threshold. Allocation rises by 88 bytes, or 6.29%, and retention is
unchanged.

The structural result explains the bounded cost: the session performs one deferred persistent
mutation and publication, but zero promotions, token allocations, commit-plan allocations, separate
nodes, or owner metadata. Callers with one edit should continue to use the ordinary persistent API.

### Ordinary persistent lookup/layout guard

| Count | Mean | Median | 99.9% confidence interval | Allocated |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 5.732 ns | 5.584 ns | 5.347–6.117 ns | 0 B |
| 100,000 | 11.335 ns | 10.551 ns | 10.457–12.214 ns | 0 B |

These are absolute sanity observations, not a fabricated before/after comparison. The stronger
ordinary-surface evidence is structural: tests pin the exact b590 source fingerprints of the
ordinary field layouts and monomorphic lookup loop, and retained diagnostics report zero ordinary
owner metadata. The selected layout therefore does not tax ordinary nodes for transient capability.

## Structural counters

The three direct runs emitted exactly one `AXIS2_T1_COUNTER_V2` row each. Lifecycle and reuse fields:

| Cadence / edits | Adopt / visits | Publish / visits | Deferred | Promotions | Plans | Prepared | Editable copied nodes/arrays | In-place node/array writes | Wrappers / deferred |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| End / 512 | 1 / 0 | 1 / 0 | 1 | 1 | 1 | 512 | 1,058 / 1,058 | 510 / 510 | 2 / 1 |
| Every64 / 512 | 8 / 0 | 8 / 0 | 8 | 8 | 8 | 512 | 1,296 / 1,296 | 496 / 496 | 16 / 8 |
| EveryEdit / 1 | 1 / 0 | 1 / 0 | 1 | 0 | 0 | 1 | 0 / 0 | 0 / 0 | 1 / 1 |

Every editable copied-node count equals its allocated-node count, and every editable copied-array
count equals its allocated-array count. Retained graph fields:

| Cadence / edits | Ordinary owner bytes | Ordinary retained | Published owner nodes/arrays/tokens | Separate collision/branch nodes | Separate metadata | Published retained | Recursive entries |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| End / 512 | 0 | 4,306,320 | 1,058 / 1,058 / 1 | 0 / 1,058 | 8,464 | 4,314,808 | 100,000 |
| Every64 / 512 | 0 | 4,306,320 | 1,044 / 1,044 / 8 | 0 / 1,044 | 8,352 | 4,314,864 | 100,000 |
| EveryEdit / 1 | 0 | 4,306,320 | 0 / 0 / 0 | 0 / 0 | 0 | 4,306,320 | 100,000 |

`*_layout_adjusted_retained_bytes` are compatibility aliases and equal the corresponding actual
retained-byte fields. No modeled subtraction is used.

## Correctness validation

Validation on the evidence commit used the same single-worker build policy:

- `PersistentHashMapSeparateNodeKernelTests`: 26 passed, 0 failed;
- complete `Tools.DataStructures.Hamt.Tests`: 190 passed, 0 failed; and
- full `DataStructures.sln` Release build: 0 warnings, 0 errors.

The focused suite covers every first-operation verb, no-op deferral, representative identity,
collisions, lazy token/plan state, production/diagnostic layout parity, base/version isolation,
consumed sessions, exact cached-wrapper publication, recursive counts/canonicality, and deterministic
hash/equality/value/allocation/publication failures. Mixed published hierarchies are then exercised
through every persistent consumer that depends on exact node type.

## Exit outcome

T1 exits with **Advance to T2**. T2 may now expose the selected one-way CHAMP transient through a
public map/set lifecycle, but must still satisfy the public-surface API/XML, failure-injection,
consumed-alias, comparer/representative, no-op identity, enumeration, retained-memory, and
ship/defer requirements in the Axis 2 plan. If those requirements fail, the private T1 kernel does
not by itself justify shipping an API.
