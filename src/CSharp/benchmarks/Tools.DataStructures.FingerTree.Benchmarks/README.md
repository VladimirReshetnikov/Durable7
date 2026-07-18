# Tools.DataStructures.FingerTree.Benchmarks

- Created (UTC): 2026-06-13T00:00:00Z
- Repository HEAD: 49863a2aadab97d9ace092b65949af5f094237ca
- Audience: Maintainers validating the library's complexity and constant-factor claims
- Scope: BenchmarkDotNet harness for the C# persistent collection cores

A [BenchmarkDotNet](https://benchmarkdotnet.org/) harness that turns the library's asymptotic and
constant-factor claims into measured evidence, comparing against the BCL's persistent collections
(`System.Collections.Immutable.ImmutableList<T>` / `ImmutableSortedSet<T>`) and `System.Collections.Generic.PriorityQueue<,>`.

## Running

The project is an executable that uses `BenchmarkSwitcher`, so the command line selects what runs.
Prepare it with one restore/build worker, then invoke the already-built driver directly. Do not
overlap restore, build, or benchmark processes.

```powershell
cd C:\DataStructures\src\CSharp
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore .\DataStructures.sln --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\benchmarks\Tools.DataStructures.FingerTree.Benchmarks\Tools.DataStructures.FingerTree.Benchmarks.csproj `
    -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

# Prevent BenchmarkDotNet child processes from inheriting credentials.
Get-ChildItem Env: | Where-Object {
    $_.Name -match '(?i)(TOKEN|KEY|SECRET|PASSWORD|CREDENTIAL|CONNECTION|COOKIE|AUTH|IGCCSVC)'
} | Remove-Item -ErrorAction SilentlyContinue

cd .\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
$driver = '.\bin\Release\net10.0\Tools.DataStructures.FingerTree.Benchmarks.dll'

# Correctness-only frozen-layout matrix. This does not invoke BenchmarkDotNet or collect timings.
dotnet $driver --verify-axis2-frozen-layouts

# Everything, full (default) job — the trustworthy but slow run.
dotnet $driver --filter '*'

# A quick pass (ShortRun: 3 warmups, 3 iterations) — good enough to see the asymptotic shape.
dotnet $driver --filter '*' --job short

# One class or one method (the filter matches the full namespace.type.method name).
dotnet $driver --filter '*ReverseBenchmarks*' --job short
dotnet $driver --filter '*PriorityQueueBenchmarks.Ours_Meld*'
```

Release configuration is mandatory for meaningful numbers; BenchmarkDotNet refuses to trust a Debug build.
For an Axis 2 decision matrix on a heterogeneous-core host, pass the same predeclared affinity mask
to every control and candidate process (the locked T1/T2 evidence used `--affinity 1`). The
environment and build properties above also constrain BenchmarkDotNet's generated-harness builds
to one MSBuild node without compiler/build-server reuse.
Results are written under `BenchmarkDotNet.Artifacts/` (git-ignored); curated tables live in
the owning workspace's decision or benchmark document, including
[`../../docs/FingerTree/benchmarks.md`](../../docs/FingerTree/benchmarks.md) and the
[CHAMP T1](../../docs/Hamt/transient-t1-decision.md) and
[T2](../../docs/Hamt/transient-t2-decision.md) decisions.

On a memory-constrained machine already hosting unrelated .NET work, set
`TDS_BENCHMARK_IN_PROCESS=1` to select BenchmarkDotNet's no-emit in-process toolchain. This mode
creates no generated MSBuild project or benchmark child process and is appropriate for paired
guardrail ratios after the benchmark executable itself has been built in Release with one MSBuild
node. Do not combine its absolute timings with normal out-of-process evidence; record the mode in
the decision artifact.

For `MeasuredRopeCursorQueryBenchmarks`, set `TDS_AXIS2_C2_GATE_ONLY=1` to restrict the document-size
parameter to the predeclared 65,536-element C2 gate while retaining both sparse and dense newline
distributions. The default remains the complete 1,024 / 65,536 / 1,048,576 matrix.

## Benchmark classes

| Class | Validates | Baseline |
|-------|-----------|----------|
| `ReverseBenchmarks` | `ReversibleDeque.Reverse` is O(1) (flat across sizes) | `ImmutableList.Reverse` (O(n)) |
| `DequeEndpointBenchmarks` | O(1) amortized endpoint add/remove, O(1) endpoint read | `ImmutableList` Add / Insert(0) |
| `DequeIndexingBenchmarks` | indexing is O(log(distance from nearer end)) — U-shaped by position | `ImmutableList` indexer (flat O(log n)) |
| `DequeConcatBenchmarks` | `Concat` is O(log(min(n, m))) | `ImmutableList.AddRange` (O(m log n)) |
| `ReversibleOverheadBenchmarks` | the constant-factor cost of reversibility vs the plain deque | `FingerTreeDeque` |
| `SortedSetBenchmarks` | order-statistic rank-select in O(log n) | `ImmutableSortedSet` (no indexer → O(k) skip) |
| `PriorityQueueBenchmarks` | meldable `Meld` in O(log(min(n, m))) | BCL `PriorityQueue` (rebuild, O(n + m)) |
| `WeightedSelectBenchmarks` | Fenwick cumulative-weight select in O(log n) | naive prefix scan (O(n)) |
| `PersistenceBenchmarks` | branching ops on a retained version stay O(1)/O(log n) (flat across sizes) | `ImmutableSortedSet` (derived-collection parity) |
| `RopeBenchmarks` | `Rope<char>` large-buffer editing in O(log n) (insert/remove/split mid-buffer) | `string` (O(n) copy) and `ImmutableList<char>` |
| `MeasuredRopeBenchmarks` | `MeasuredRope<char,…>` line navigation in O(log n) (offset↔line) | `string` newline scan (O(n)) |
| `RopeCursorBenchmarks` | Axis 2 positional local-edit, locality, branch, and snapshot-cadence gate (baseline skeleton in P0; cursor lanes arrive in C0/C1) | indexed persistent `Rope<char>` and `StringBuilder` mutable control |
| `RopeCursorCarryTuningBenchmarks` | Axis 2 C0 focus/flush tuning under actual left/right carry publication, backspace, and forward-delete pressure | the common focused cursor engine through its readonly-struct wrapper |
| `MeasuredRopeCursorBenchmarks`, `MeasuredRopeCursorGateBenchmarks`, `MeasuredRopeCursorQueryBenchmarks`, `MeasuredRopeCursorDirtyQueryBenchmarks` | Axis 2 C2 public measured-text cursor: cadence-matched local-edit matrix and compact shipment gate, positional seek, delegate/struct absolute measure seek, clean and freshly dirty line/column/query paths, and untimed callback diagnostics across sparse/dense newlines | indexed `MeasuredRope<char, int, NewlineMeasure>` edits, explicit snapshot queries, and the existing locate/line-column helpers |
| `SortedBuilderBenchmarks` | sorted builder batch-edit freeze constants and allocation | repeated immutable edits, caller-side BCL staging, BCL immutable builders |
| `RopeBuilderBenchmarks` | append-only rope builder construction and snapshot constants | `Create`, `AddLast` loop, text `StringBuilder` materialization, `ImmutableList<T>.Builder` |
| `ChampBenchmarks` | CHAMP lookup, payload-dense iteration, shared-single-change diff, and independent-history equality/diff | `Dictionary` and `ImmutableDictionary` |
| `CtrieBenchmarks` | lock-free lookup and O(1) immutable snapshot publication | `ConcurrentDictionary` lookup and O(n) immutable copy |
| `TransientLifecycleBenchmarks` | Axis 2 edit-locality/publication matrix and the shipped public C# transient path: O(1) `ToTransient`/`Persist`, first-edit ordinary deferral, later reusable-path promotion, exact-type transient-editable branch/collision nodes, plus untimed structural copy/ownership/retained-size diagnostics | direct persistent edits and canonical `BulkBuilder` construction |
| `FrozenLookupBenchmarks`, `FrozenClusteredLookupBenchmarks`, `FrozenCollisionLookupBenchmarks`, `FrozenNullLookupBenchmarks` | Axis 2 F1 fixed-layout bake-off across lookup mixes, enumeration, construction, conversion back to canonical CHAMP, retained arrays, null/stored representatives, collision shapes, and break-even | persistent CHAMP, linear/Robin-Hood/quadratic repository prototypes, `Dictionary`, `ImmutableDictionary`, and BCL `FrozenDictionary` where semantically representable |
| `PatriciaMapBenchmarks` | integer-key lookup and prefix-aware structural union | CHAMP and `ImmutableDictionary` lookup |
| `RrbVectorBenchmarks` | uniform middle indexing and boundary-spine concatenation | `Rope<T>` indexing/concat and `ImmutableList<T>` indexing/concat |
| `DabaLiteBenchmarks` | worst-case O(1) FIFO slide-and-query aggregation, callback-free structure validation, and 63/64/65 chunk-boundary behavior | `Queue<T>` plus O(n) reaggregation |
| `CanonicalSortedSetBenchmarks` | publicly seeded HMAC-rank lookup, O(h) persistent insert/remove, same-policy independent-history equality, memoized digest rejection, O(n log n) sort plus O(n) Cartesian bulk build (including a fully colliding rank hash), and structure validation | `ImmutableSortedSet<T>` |
| `BrodalOkasakiHeapBenchmarks` | worst-case O(1) persistent insert/meld, O(log n) delete-min, full build/drain scaling, and fused-representation validation | measured finger-tree priority queue equivalents |
| `PrioritySearchQueueBenchmarks` | keyed persistent updates, delete-min, structure validation, and fully pruned/sparse/dense range-bounded priority queries | `SortedDictionary` lookup and range-aware filtered scans |
| `MerkleSearchTreeBenchmarks` | content-addressed lookup/update, O(1) digest equality, and digest-pruned diff | `SortedDictionary` lookup |

## Fairness methodology

Every benchmark obeys the same rules so the numbers do not lie:

- **One operation per method.** The structure of size N is built once in `[GlobalSetup]`; the measured method
  performs a single persistent operation on it. Construction is never folded into the timed region.
- **Results escape dead-code elimination.** Each method returns its result (a new persistent instance, or a
  read value), which BenchmarkDotNet consumes so the JIT cannot delete the work.
- **Allocations are measured.** `[MemoryDiagnoser]` reports bytes-per-operation, which is where the O(1) vs
  O(n) story is clearest (e.g. `ReversibleDeque.Reverse` allocates a constant 72 bytes at any size).
- **Baselines are the fair idiomatic alternative.** Where the BCL lacks an operation (immutable concat, heap
  meld, rank index), the baseline is the standard way a user would otherwise achieve the same result, and the
  comment on the method says so.
- **Sizes span orders of magnitude** so a flat line (O(1)), a gentle climb (O(log n)), and a straight climb
  (O(n)) are visually distinct in the `Ratio` and `Mean` columns.

## Axis 2 predeclared measurement policy

The Axis 2 gates use this policy, committed before any prototype result is collected. A lane is
**materially better** only when its improvement exceeds the larger of its measured relative noise
floor and the relevant practical margin. The practical margins are 10% for mean latency, allocated
bytes per operation, and separately measured retained bytes. A claimed tail-latency win must also
improve p99 by at least 15%. A regression in another load-bearing metric is acceptable only when it
is below that metric's same threshold and the decision record explains the trade.

Measure the noise floor by running the unchanged control lane in five independent BenchmarkDotNet
processes. For each reported metric, use the larger of BenchmarkDotNet's relative 99.9% confidence
interval and three scaled median absolute deviations of the five process medians. Do not substitute
the prototype/control difference itself for the noise estimate. A result whose confidence interval
crosses the required threshold is inconclusive, not a win.

On a heterogeneous-core processor, predeclare one logical-processor affinity mask and pass it to
every control and candidate process. Record the mask with the hardware summary. If an unpinned pilot
reveals cross-core-class scheduling noise, archive that complete pilot as inconclusive and rerun the
entire deciding matrix at the fixed affinity; never combine pinned and unpinned observations.

Every advance or defer record must name the exact workload, dataset, snapshot/publication cadence,
and metric that decided the gate; include the exact command, environment summary, raw artifact path,
and curated table. Correctness tests and structural-counter runs are separate from timed runs.
Diagnostics must not execute inside a latency benchmark unless every compared lane performs the same
diagnostic work.

## Axis 2 locked datasets and lanes

`Axis2BenchmarkPolicy.cs` is the executable source of truth. It fixes seed `0x5eed2026`, comparer
instances, input generation, and these matrices:

- hash counts: 0, 1, 8, 32, 1,024, and 100,000; uniform, shared-prefix, and full-collision hashes;
- edit/publication cadences: 1, 8, 64, and end of batch, with repeated-key, clustered-prefix,
  disjoint-key, collision-heavy, removal, and mixed-operation lanes;
- lookup hit ratios: 0%, 50%, and 100%, using one shuffled probe array per comparable lane;
- cursor documents: 1,024, 65,536, and 1,048,576 UTF-16 elements; locality windows 1, 8, 256,
  and random; snapshot cadences 1, 16, and 256;
- cursor tuning candidates: focus capacities 16, 32, 64, and 128 independently crossed with
  flush sizes 256, 512, 1,024, and 2,048; and
- measured-text data: the same deterministic documents with newline-sparse and newline-dense variants.

The C0 carry-tuning sublane fixes the representative point at 65,536 UTF-16 elements, locality
window 8, and snapshot cadence 16. Each invocation types 2,304 elements and backspaces them, then
types another 2,304 immediately to the right of a fixed gap and forward-deletes them. The run length
is divisible by the cadence and exceeds the largest flush-size-plus-focus candidate, so every one of
the sixteen focus/flush pairs must publish an ordinary chunk on both sides. Global setup verifies
those transitions and exact sequence restoration outside the timed region.

The C2 edit gate is locked at 65,536 UTF-16 elements, 256 replacements, locality window 8, and
snapshot cadence 16—the C1 representative point and the predeclared target cadence for C3 sample
integration. The public
measured cursor must improve both mean latency and allocation over indexed measured-rope edits by
at least the larger of the measured noise floor and 10%. Sparse text places one newline every 256
elements; dense text places one every eight. The exploratory matrix also crosses 1K/64K/1M
documents, locality 1/8/256, and cadences 1/16/256.

Query guardrails are independent of the edit win and were fixed before results were collected.
Clean positional seek must remain at or below 1.25 times direct `GetCursor`, with no more than a
1.10 allocation ratio. Source-to-cursor and prepared-cursor measure seek must each remain at or
below 2.0 times the matching existing locate mean and allocate no more than 16 KiB; the untimed
diagnostic may invoke `Measure` at most 2,048 times for the located ordinary chunk plus 16 times for
focus preparation. Prepared-cursor `LineColumnOf` must remain at or below 1.25 times the existing
rope helper mean and allocate no more than 64 bytes. Delegate and constrained-struct predicate lanes
are measured separately. Freshly dirty position, measure, and line/column queries compare direct
cursor operations with the equivalent explicit-snapshot path; each must remain at or below a 1.25
mean ratio and a 1.10 allocation ratio. Global setup proves result parity and records `Measure`/`Combine` callback
counts; those diagnostics never execute in a timed method. Global cleanup writes one stable
`AXIS2_C2_CALLBACK_V1` line into the run log, including edit, snapshot, source-seek, and
prepared-seek callback totals. Snapshot diagnostics fail setup if publication remeasures an element.

Comparable lanes receive the same generated entries, stored comparer object, hit/miss probes,
warmup/job configuration, and result-consumption shape. A control that cannot represent a semantic
lane—most notably a null-key lane—is omitted and called out rather than given different input.
BenchmarkDotNet's `MemoryDiagnoser` supplies allocation data; retained graph bytes come from the
internal structural estimators and are never relabeled as allocation bytes.

The `SeparateNodeKernelHistory` method retains its historical compatibility name so the locked T1
filters and archived artifacts stay reproducible. Its timed body is now the shipped public T2 path:
each publication batch calls `map.ToTransient()`, applies edits through the public point verbs, and
calls `Persist()`. It carries the `Axis2T2`, `EditPublication`, and `PublicTransient` categories. The
public map type is the selected engine itself, so this timed path does not add a facade/session
wrapper beyond the object users receive.

Setup replays a diagnostics-enabled instance of the same engine solely to prove semantic,
canonical, counter, and retained-layout parity with production. That untimed replay emits
`AXIS2_T1_COUNTER_V2`; diagnostic work is never folded into the public latency method. Ordinary
collision and bitmap nodes retain the b590 sealed,
readonly source shape and physically contain no owner token or ownership flags. Direct
`SeparateTransientCollisionNode : HashNode` and `SeparateTransientBranchNode : Node` types carry
the token; a two-bit mask gives data and child arrays independent write ownership without a shared
abstract node-property hierarchy. The first changed edit stays ordinary, with reusable branch or
collision paths promoted only by a later changed edit; tokens, commit plans, and diagnostics are
lazy or optional. `AXIS2_T1_COUNTER_V2` therefore reports
`ordinary_owner_metadata_bytes=0`, explicit deferred-persistent and editable-promotion counts, and
`editable_*` node/array counters that do not mislabel the ordinary first-edit path. The paired T0
row contextualizes ordinary persistent path-copy costs, while MemoryDiagnoser charges the complete
timed history. Both retained-byte fields estimate the actual reachable layout. The
`*_layout_adjusted_retained_bytes` names remain as CSV-compatibility aliases and equal
their corresponding actual retained-byte fields; no subtraction or modeled adjustment occurs.
Adoption and publication node-visit counters must remain zero.
The owner-field lane and evidence recorded in the T0 decision document are historical and run on
`codex/axis2-t1-owner-fields-gate`; the earlier abstract-base separate formulation runs on
`codex/axis2-t1-separate-gate`. Only the direct-separate lane contributes to the completed gate. The
[T1 decision](../../docs/Hamt/transient-t1-decision.md) records the pinned noise calculation,
material End/Every64 wins, bounded sparse cost, retained graph, and advance to T2. The full public-
path confirmation used this exact compatibility filter and artifact directory:

```powershell
$publicEnd = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512)'
dotnet $driver --filter $publicEnd --affinity 1 `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t2-public-end-full'
```

Across 100 samples, the public path measured 236.700 us mean, 220.608 us median, a
222.285–251.115 us 99.9% confidence interval, and 253.67 KB allocated. Its entire interval is below
the locked 283.132 us cutoff; the mean is 46.60% below the 443.293 us persistent-control center and
the upper confidence endpoint is 43.35% below it. Adoption/publication node visits remain zero, and
the actual retained graph is 4,314,808 bytes versus 4,306,320 bytes for the ordinary result
(+8,488 bytes, +0.1971%). See the
[T2 decision](../../docs/Hamt/transient-t2-decision.md) for shipment context.

An earlier pinned one-case BenchmarkDotNet Dry-job run was only a public-path harness smoke. It
proved setup/replay completion and zero adoption/publication node visits, but supplies no statistical
performance evidence and is not combined with the full public-path result.

The F1 frozen-layout bake-off keeps one packed source-order entry array and compares three fixed
offline indexes: simple linear probing, Robin-Hood linear probing, and power-of-two triangular
quadratic probing. Global setup validates comparer identity, lookup/content parity, exact repository
prototype enumeration order, stored representatives, nulls, and equal-full-hash buckets before a
timed method can run. Setup also emits `AXIS2_F1_RETAINED_V1` rows for each prototype and for arrays
reachable through the current runtime's BCL Frozen implementation. These are retained-array
estimates, not construction allocation. Separate `ToPersistent` groups rebuild canonical CHAMP
from each candidate's packed source-order entries, including the null lane. The exact single-worker
evidence protocol and pending gate are recorded in the
[F1 decision document](../../docs/Hamt/frozen-f1-layout-decision.md). The standalone
`--verify-axis2-frozen-layouts` path emits `AXIS2_FROZEN_VERIFY_V2` after the complete 39-case
matrix plus a reference-representative case, exact candidate -> persistent -> candidate round trips,
layout diagnostics, exact comparer callback counts, invariant retained-record formatting,
break-even arithmetic, and throwing-comparer checks pass. It invokes neither BenchmarkDotNet nor
retained-size output and is a correctness prerequisite only. The
[F0 decision](../../docs/Hamt/frozen-f0-signal-decision.md) must record an isolated linear-layout
advance before F1 results may be interpreted.
