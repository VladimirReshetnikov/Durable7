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
| `SortedBuilderBenchmarks` | sorted builder batch-edit freeze constants and allocation | repeated immutable edits, caller-side BCL staging, BCL immutable builders |
| `RopeBuilderBenchmarks` | append-only rope builder construction and snapshot constants | `Create`, `AddLast` loop, text `StringBuilder` materialization, `ImmutableList<T>.Builder` |
| `ChampBenchmarks` | CHAMP lookup, payload-dense iteration, shared-single-change diff, and independent-history equality/diff | `Dictionary` and `ImmutableDictionary` |
| `CtrieBenchmarks` | lock-free lookup and O(1) immutable snapshot publication | `ConcurrentDictionary` lookup and O(n) immutable copy |
| `PatriciaMapBenchmarks` | integer-key lookup and prefix-aware structural union | CHAMP and `ImmutableDictionary` lookup |
| `RrbVectorBenchmarks` | uniform middle indexing and boundary-spine concatenation | `Rope<T>` indexing/concat and `ImmutableList<T>` indexing/concat |
| `DabaLiteBenchmarks` | worst-case O(1) FIFO slide-and-query aggregation | `Queue<T>` plus O(n) reaggregation |
| `CanonicalSortedSetBenchmarks` | keyed zip-zip lookup, independent-history equality, and memoized digest | `ImmutableSortedSet<T>` |
| `BrodalOkasakiHeapBenchmarks` | worst-case O(1) persistent insert/meld and O(log n) delete-min | measured finger-tree priority queue |
| `PrioritySearchQueueBenchmarks` | keyed priority updates, delete-min, and range-bounded priority queries | `SortedDictionary` lookup and filtered scan |
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
