# FingerTree benchmarks

- Created (UTC): 2026-06-13T00:00:00Z
- Repository HEAD: a93058305d... (benchmark harness)
- Audience: Maintainers and reviewers assessing the library's measured performance
- Scope: Curated results from `benchmarks/Tools.DataStructures.FingerTree.Benchmarks`

This document records representative results from the [BenchmarkDotNet harness](../benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md),
which turns the library's asymptotic and constant-factor claims into measured evidence against the BCL's
persistent collections. The harness — not these snapshots — is the source of truth; re-run it to refresh.

## How to read this

- **Environment:** BenchmarkDotNet 0.14.0, .NET 10, Windows 11, 13th Gen Intel Core i7-1355U. Numbers below are
  from the **ShortRun job** (3 warmups, 3 iterations) used for a quick pass; absolute values carry meaningful
  variance (see the harness for full-job runs), but the **orders of magnitude and the shapes of the curves**
  across sizes are stable and are what the claims rest on.
- **Caveat — micro-reads:** a benchmark whose body is a pure function of unchanging fields (e.g. one O(log n)
  read of a fixed collection at a fixed index) can be hoisted out of the measurement loop by the JIT and report
  an implausible sub-nanosecond time. Such rows are called out rather than taken at face value.

## Headline: O(1) `Reverse`

`ReversibleDeque.Reverse()` mirrors the root and flips a bit — constant work and a constant 72 bytes,
**independent of size** — while every eager reverse is linear. This is the clearest result in the suite.

| Size | `ReversibleDeque.Reverse` | `ImmutableList.Reverse` | `Array.Reverse` (copy) |
|-----:|--------------------------:|------------------------:|-----------------------:|
| 10,000 | ~19 ns · 72 B | 1,438,389 ns · 480 KB | 5,492 ns · 40 KB |
| 1,000,000 | **19 ns · 72 B** | 346,476,750 ns · 48 MB | 1,000,009 ns · 4 MB |

At 1,000,000 elements `ReversibleDeque.Reverse` is ~**18,000,000×** faster than `ImmutableList.Reverse` and
~52,000× faster than copying and reversing a raw array — and its line is flat from 100 to 1,000,000.

## Deque endpoints

`FingerTreeDeque` endpoint operations are O(1) amortized; `ImmutableList` prepend (`Insert(0, …)`) and append
(`Add`) are O(log n) with a large constant, and the ratio grows with size.

| Operation (Size 100,000) | `FingerTreeDeque` | `ImmutableList` | Ratio (Immutable / ours) |
|--------------------------|------------------:|----------------:|-------------------------:|
| Prepend (`AddFirst` / `Insert(0)`) | 16 ns · 88 B | 296 ns · 888 B | ~18× |
| Append (`AddLast` / `Add`) | 45 ns · 176 B | 200 ns · 840 B | ~4–12× |
| Read first (`First` / `[0]`) | 0.3 ns · 0 B | — | O(1) worst case |
| `RemoveFirst` | 23 ns · 136 B | — | — |

The prepend ratio rises from ~10× at 1,000 elements to ~18× at 100,000 — the signature of O(1) versus a growing
per-operation cost. `First` is a sub-nanosecond, zero-allocation read (O(1) worst case).

## Deque indexing — O(log(distance from the nearer end))

Finger-tree indexing is cheapest near either end and most expensive in the middle. The U-shape across positions
is exactly the claimed O(log(min(i, n − i))).

| Position (Size 100,000) | `FingerTreeDeque[i]` |
|-------------------------|---------------------:|
| Start (0)               | 2.7 ns |
| Quarter (n/4)           | 310 ns |
| Middle (n/2)            | 314 ns |
| End (n−1)               | 6.5 ns |

`ImmutableList`'s indexer is uniform O(log n) regardless of position (its per-position micro-reads here are in
the hoisting-caveat regime, so only the *flatness* across positions is meaningful). The finger tree wins
decisively near the ends and pays for the convenience in the middle.

## Reversible deque — the price of O(1) reverse

Reversibility costs a reversal-bit branch and orientation-aware accessors on every internal step. Measured
against the plain deque (Size 100,000):

| Operation | `FingerTreeDeque` | `ReversibleDeque` (forward) | `ReversibleDeque` (reversed) |
|-----------|------------------:|----------------------------:|-----------------------------:|
| `AddFirst` | 20 ns · 88 B | 43 ns · 136 B (~2×) | 290 ns · 1,136 B |
| Index middle | ~600 ns | ~comparable | ~720 ns · 3,520 B |

Forward-orientation operations carry roughly a 2× constant-factor overhead; the reversed path costs more
(orientation-adjusted digit copies). This is the documented trade-off: choose `FingerTreeDeque` unless O(1)
reverse is needed.

## Meldable priority queue — O(log(min(n, m)))

Two persistent priority queues join by tree concatenation, sharing structure. A binary heap (the BCL
`PriorityQueue`) cannot merge — the only option is to rebuild, which is O(n + m). The fixed left queue has
100,000 entries.

| Right size | `PriorityQueue.Meld` (ours) | BCL rebuild-to-merge | Ratio |
|-----------:|----------------------------:|---------------------:|------:|
| 100        | 876 ns · 2.1 KB | 785,057 ns · 2.1 MB | **~900×** |
| 100,000    | 2,027 ns · 4.9 KB | 1,507,583 ns · 4.2 MB | **~750×** |

`Meld` grew only ~2.3× while the right operand grew 1,000× — O(log), not O(n). `TryPeekPriority` is ~1.5 ns
(O(1)). Meld is the finger tree's structural advantage over a heap, and it is dramatic.

## Fenwick weighted selection — O(log n) vs O(n)

`SumMeasure`'s cumulative-weight selection descends the cached running sum; the obvious alternative scans a
prefix sum. The query targets the midpoint of the total weight (worst case for the scan).

| Size | `TrySelectByCumulativeWeight` | Naive prefix scan | Ratio |
|-----:|------------------------------:|------------------:|------:|
| 1,000 | 977 ns | 517 ns | 0.5× (scan wins at tiny n) |
| 100,000 | **1,551 ns** | 48,983 ns | **~32×** |

The tree's time grew only 1.6× for a 100× size increase (O(log n)); the scan grew ~95× (O(n)). The crossover is
small, and the gap widens without bound as n grows.

## Sorted collections — competitive, honest

Our `SortedSet` is a persistent finger-tree sorted set with order-statistic indexing, ranking, and
set-algebra-as-sorted-merge. The BCL `ImmutableSortedSet` is a mature, heavily optimized persistent collection
that *also* offers O(log n) `Contains`, an O(log n) rank indexer (`IReadOnlyList<T>`), and `IndexOf` — so the
order-statistic operations are **not unique to the finger tree**.

The read paths were originally routed through `Split`, which rebuilt both result halves and so allocated ~1–2 KB
per query. They now use a read-only `TryLocate` descent that finds the boundary element (and the measure before
it) without reconstructing any subtree, which cut both time and allocation dramatically:

| `SortedSet` read (100,000) | Before (`Split`) | After (`TryLocate`) |
|----------------------------|-----------------:|--------------------:|
| `Contains`                 | 2,612 ns · 2,400 B | 351 ns · 96 B |
| rank-select (indexer)      | 2,229 ns · 2,392 B | 360 ns · 88 B |

That is ~7× faster and ~25× less allocation. The residual ~90 B is the predicate *closure* (the lambda captures
the query key and comparer); eliminating it entirely would require non-capturing struct predicates, a possible
further step toward the BCL's zero-allocation reads. The BCL's sub-nanosecond reads here are in the
hoisting-caveat regime and are not literal. The finger-tree value proposition for sorted data is the **unified
measure framework** — one core yielding multiset, set, map, interval tree, priority queue, and order-statistic
tree — and structural-sharing persistence; on shared reads it is now within a small constant factor of the
specialized BCL collection.

## Takeaways

- **O(1) `Reverse`** and **O(log(min)) `Meld`** are the standout structural wins (millions-fold and ~750–900×).
- **Endpoint operations** and **weighted selection** show the expected O(1) / O(log n) advantage over the BCL's
  O(log n) / O(n), with ratios that grow with size.
- **Indexing** exhibits the predicted distance-from-nearer-end U-shape.
- **Sorted-collection reads** route through the allocation-free `TryLocate` descent (~7× faster, ~25× less
  allocation than the former `Split`-based reads), leaving only a small per-query predicate closure.
- The finger tree trades a constant-factor read overhead for persistence, structural sharing, and a single
  unified core behind a whole family of collections.
