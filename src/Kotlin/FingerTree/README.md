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
- `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, and `BrodalOkasakiHeapStatistics`;
- `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>`, and result/statistics types;
- `Interval<T>`, `IntervalTree<T>`, and `PersistentIntervalMap<T, V>`;
- `PersistentChunkedBitSet`, a sparse measured nonzero-word set with rank/select and algebra;
- `RrbVector<T>` and its append-only `RrbVector.Builder<T>`;
- `ZipTreeRankPolicy<T>`, `CanonicalSortedSet<T>`, and `CanonicalSortedSetStatistics`;
- `Monoid<T>`, `DabaLite<T>`, and `DabaLiteStatistics` for mutable FIFO window aggregation;
- `RangeUpdateAlgebra<T, M, Tag>`, `RangeUpdateSequence<T, M, Tag>`, and its split/validation
  result types;
- `Rope<T>`, its immutable positional `RopeCursor<T>` and nullable-safe `RopeCursorPeek<T>`,
  `MeasuredRope<T, M>`, `MeasuredRopeCursor<T, M>`, `MeasuredRopeCursorSearch<T, M>`, `TextRope`,
  `TextRopeCursor`, `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

The family is backed by a shared immutable measured AVL sequence. Every node caches subtree size,
height, and the active monoidal measure; joins, splits, indexed edits, and concatenation copy only
the affected paths and retain untouched JVM nodes. The same substrate drives `PersistentDeque`, the
general `FingerTree`, sorted facades, stable priority selection, max-high interval pruning,
positional/measured ropes, and newline-measured text. `ReversibleDeque` keeps its specialized
orientation-aware balanced tree so whole-value reversal remains O(1).

`PersistentIntervalMap<T, V>` composes the maximum-high-augmented interval tree with the persistent
sorted map. It provides unique complete interval keys, exact and indexed payload lookup, strict and
conditional add, replacement, removal, pruned first/all/count overlap queries, component-sharing
diagnostics, and cross-index validation.

`RangeUpdateSequence` is a separate implicit-key AVL sibling whose nodes additionally cache an
optional algebraic lazy tag. Its runtime algebra extends `MeasurePolicy`, with directional
`compose(newer, older)` and actions on elements and combined subtree measures. Full-range updates
replace one root in O(1); proper updates and queries visit O(log n) boundary paths. Structural
descent immutably pushes tags before rotations, while reads carry inherited actions without
publishing pushed nodes. See the [range-update contract](docs/range-update-sequence.md).

`RopeCursor<T>` and `MeasuredRopeCursor<T, M>` are immutable semantic checkpoints retaining an exact
rope snapshot plus a validated gap in `0..size`. Both preserve branchable edits, nullable-safe wrapped
peeks, identity-preserving same-position seek and empty insertion, and equality-free replacement. The
measured cursor additionally exposes ordered `measureBefore`/`measureAfter` partitions and absolute
prefix search with an end cursor on a miss. `TextRopeCursor` keeps the exact text facade around the
newline-measured cursor and wraps edited measured snapshots in O(1), retaining access to string,
line, and UTF-16 column helpers without materialization.
Creation, movement, seek, and snapshot are O(1); peeks, measured reads, measure search, and point edits
are O(log n), while inserting `m` elements is O(m + log n). Rope growth uses checked `Int` arithmetic
and fails before measure-policy callbacks or publication. These are not the C# focused cursor representation and make
no memo-cell, allocation, or amortized-locality claim.

`RrbVector<T>` is the family's random-access-optimized sequence. It stores up to 32 elements per
leaf and uses 32-way branches: packed branches navigate by five-bit radix arithmetic without size
tables, while split/concatenation-induced relaxed branches cache `IntArray` cumulative sizes.
Indexing and replacement copy one O(log32 n) path; concatenation and splitting rebuild only boundary
spines and retain untouched leaves. Its append builder owns mutable tail arrays, freezes full leaves,
copies partial tails, and caches isolated immutable snapshots.

`CanonicalSortedSet<T>` is the policy-canonical sorted sibling. It is an immutable Cartesian binary
search tree whose geometric and secondary priorities come from keyed HMAC-SHA-256 ranks. Natural-
order and explicit-comparator `ZipTreeRankPolicy<T>` factories support fresh hidden keys, public
reproducibility seeds, and caller-retained keys; an explicit comparator requires a rank hash that is
constant on its equivalence classes. Bulk and incremental histories with the same coherent policy
converge on one topology, and path-copying updates retain untouched nodes. A lazily published 64-bit
tree digest provides same-policy inequality rejection, while semantic equality remains comparer-
based and canonical algebra requires the same policy object. Fully colliding ranks can produce a
linear tree, so all construction, editing, validation, iteration, equality, and digest traversals use
explicit stacks.

`DabaLite<T>` is the family's deliberately mutable streaming member. It accepts a runtime
`Monoid<T>` (and every `MeasurePolicy<*, T>` is one), and implements the VLDB Journal 2021 DABA Lite
six-cursor schedule over linked 64-slot chunks. Insert, eviction, and aggregate query invoke the
monoid combine callback at most three, two, and one times respectively; their complete worst-case
O(1) bound assumes the callbacks are O(1). Mutators are callback-atomic, `clear()` replaces the
whole active chain in O(1), retired slots/chunks are released promptly, and `validateStructure()`
audits links, cursor/region invariants, and capacity statistics without invoking the monoid. One
instance must not be accessed concurrently without external serialization.

`BrodalOkasakiHeap<T>` directly implements the bootstrapped skew-binomial heap: its global
rank-zero root stores the minimum and its child list fuses primitive skew-tree children with an
embedded heap forest. Minimum, insert, and meld are O(1) worst-case; minimum deletion is O(log n).
Custom heaps meld only when they retain the same comparator object. The validator decodes every
fused boundary and checks ranks, heap order, logical count, and depth.

`PrioritySearchQueue<K, P, V>` is a separate winner-cached persistent AVL core. It retains one
entry per ordered key, preserves the first equivalent key representative, breaks priority ties by
key order, and provides O(1) minimum plus O(log n) keyed updates and minimum deletion. Its inclusive
key-range/priority-threshold sequence prunes subtrees by cached winner and remains key ordered.

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

See [API notes](docs/api-notes.md), [range-update contract](docs/range-update-sequence.md),
[priority-core notes](docs/priority-cores.md),
[validation](docs/validation.md), and the
[test map](tests/README.md).
