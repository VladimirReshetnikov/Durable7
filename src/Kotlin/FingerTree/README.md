# Kotlin FingerTree Family

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and reviewers of the Kotlin FingerTree-family port
- Scope: `tools.datastructures.fingertree` package

This workspace ports the repository FingerTree collection family to Kotlin/JVM. It exposes Kotlin
names for the public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, M>` over runtime `MeasurePolicy<T, M>` values, including built-in size, sum,
  min/max, and product policies;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `RrbVector<T>` and its append-only `RrbVector.Builder<T>`;
- `Rope<T>`, `MeasuredRope<T, M>`, `TextRope`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

The family is backed by a shared immutable measured AVL sequence. Every node caches subtree size,
height, and the active monoidal measure; joins, splits, indexed edits, and concatenation copy only
the affected paths and retain untouched JVM nodes. The same substrate drives `PersistentDeque`, the
general `FingerTree`, sorted facades, stable priority selection, max-high interval pruning,
positional/measured ropes, and newline-measured text. `ReversibleDeque` keeps its specialized
orientation-aware balanced tree so whole-value reversal remains O(1).

`RrbVector<T>` is the family's random-access-optimized sequence. It stores up to 32 elements per
leaf and uses 32-way branches: packed branches navigate by five-bit radix arithmetic without size
tables, while split/concatenation-induced relaxed branches cache `IntArray` cumulative sizes.
Indexing and replacement copy one O(log32 n) path; concatenation and splitting rebuild only boundary
spines and retain untouched leaves. Its append builder owns mutable tail arrays, freezes full leaves,
copies partial tails, and caches isolated immutable snapshots.

`MeasuredRope` exposes the same positional editing vocabulary as `Rope`—front/back, endpoint and
indexed insertion, range insertion/removal, slicing, splitting, concatenation, replacement, copy,
and compaction—while maintaining the caller's cached monoidal measure. `SortedMap.from` has both
natural-order and explicit-comparator bulk factories; comparator-equal input runs remain last-entry-wins.

This closes the former flat-`List` semantic checkpoint. Indexed access, updates, inserts, removes,
splits, and measure-guided location are logarithmic in tree size; `PriorityQueue.peekEntry` reads a
cached stable minimum in O(1), and `IntervalTree.findOverlap` uses cached maximum-high summaries.
Materialization, sorting/filter rebuilding, `PersistentDeque.reverse`, and string conversion remain
linear. The internal engine is a strict measured AVL sequence rather than the C#/C++ lazy
Hinze–Paterson digit spine, so endpoint operations are O(log n), not the managed engine's amortized
O(1); this is an explicit engine choice, not a flat-storage checkpoint.

Validate from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace FingerTree
```

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md).
