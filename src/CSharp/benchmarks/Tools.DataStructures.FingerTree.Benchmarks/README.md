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

```powershell
cd C:\DataStructures\src\CSharp\benchmarks\Tools.DataStructures.FingerTree.Benchmarks

# Everything, full (default) job — the trustworthy but slow run.
dotnet run -c Release -- --filter *

# A quick pass (ShortRun: 3 warmups, 3 iterations) — good enough to see the asymptotic shape.
dotnet run -c Release -- --filter * --job short

# One class or one method (the filter matches the full namespace.type.method name).
dotnet run -c Release -- --filter *ReverseBenchmarks* --job short
dotnet run -c Release -- --filter *PriorityQueueBenchmarks.Ours_Meld*
```

Release configuration is mandatory for meaningful numbers; BenchmarkDotNet refuses to trust a Debug build.
Results are written under `BenchmarkDotNet.Artifacts/` (git-ignored); curated tables live in
[`../../docs/FingerTree/benchmarks.md`](../../docs/FingerTree/benchmarks.md).

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
| `RopeCursorCarryTuningBenchmarks` | Axis 2 C0 focus/flush tuning under actual left/right carry publication, backspace, and forward-delete pressure | the common zipper engine through its readonly-struct wrapper |
| `MeasuredRopeCursorBenchmarks` | Axis 2 measured-text edit, absolute measure-seek, and line/column gate (baseline skeleton in P0; cursor lanes arrive in C2) | indexed `MeasuredRope<char, int, NewlineMeasure>` |
| `SortedBuilderBenchmarks` | sorted builder batch-edit freeze constants and allocation | repeated immutable edits, caller-side BCL staging, BCL immutable builders |
| `RopeBuilderBenchmarks` | append-only rope builder construction and snapshot constants | `Create`, `AddLast` loop, text `StringBuilder` materialization, `ImmutableList<T>.Builder` |
| `ChampBenchmarks` | CHAMP lookup, payload-dense iteration, shared-single-change diff, and independent-history equality/diff | `Dictionary` and `ImmutableDictionary` |
| `CtrieBenchmarks` | lock-free lookup and O(1) immutable snapshot publication | `ConcurrentDictionary` lookup and O(n) immutable copy |
| `TransientLifecycleBenchmarks` | Axis 2 edit-locality/publication matrix, structural path-copy counters, and the direct-separate T1 gate build: first-edit ordinary deferral, later reusable-path promotion, exact-type transient-editable branch/collision nodes, O(1) adoption/seal, and actual ownership/copy/retained-size evidence | direct persistent edits and canonical `BulkBuilder` construction |
| `FrozenLookupBenchmarks`, `FrozenClusteredLookupBenchmarks`, `FrozenCollisionLookupBenchmarks`, `FrozenNullLookupBenchmarks` | Axis 2 F1 fixed-layout bake-off across lookup mixes, enumeration, construction, retained arrays, null/stored representatives, collision shapes, and break-even | persistent CHAMP, linear/Robin-Hood/quadratic repository prototypes, `Dictionary`, `ImmutableDictionary`, and BCL `FrozenDictionary` where semantically representable |
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

Comparable lanes receive the same generated entries, stored comparer object, hit/miss probes,
warmup/job configuration, and result-consumption shape. A control that cannot represent a semantic
lane—most notably a null-key lane—is omitted and called out rather than given different input.
BenchmarkDotNet's `MemoryDiagnoser` supplies allocation data; retained graph bytes come from the
internal structural estimators and are never relabeled as allocation bytes.

On branch `codex/axis2-t1-direct-separate-gate`, the `SeparateNodeKernelHistory` method is the
filterable T1 measurement lane. Ordinary collision and bitmap nodes retain the b590 sealed,
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
`codex/axis2-t1-separate-gate`. This branch intentionally exposes only the direct-separate lane,
and no advance/defer result is claimed before its locked measurements complete.

The F1 frozen-layout bake-off keeps one packed source-order entry array and compares three fixed
offline indexes: simple linear probing, Robin-Hood linear probing, and power-of-two triangular
quadratic probing. Global setup validates comparer identity, lookup/content parity, exact repository
prototype enumeration order, stored representatives, nulls, and equal-full-hash buckets before a
timed method can run. Setup also emits `AXIS2_F1_RETAINED_V1` rows for each prototype and for arrays
reachable through the current runtime's BCL Frozen implementation. These are retained-array
estimates, not construction allocation. The exact single-worker evidence protocol and pending gate
are recorded in the [F1 decision document](../../docs/Hamt/frozen-f1-layout-decision.md).
