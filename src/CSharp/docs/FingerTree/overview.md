# FingerTree

- Status: Implemented workspace
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers implementing and reviewing the finger-tree deque
- Scope: Project layout and validation entry points for `src/CSharp/src/Tools.DataStructures.FingerTree`

`src/CSharp/src/Tools.DataStructures.FingerTree` contains the .NET 10 C# preview workspace for `Tools.DataStructures.FingerTree`, a persistent collection library built around tuned and general-purpose finger trees.

This workspace provides two public finger-tree types. `FingerTree<TElement, TMeasure, TMeasureOps>` is the general Hinze–Paterson **measured** finger tree — a persistent sequence annotated by an arbitrary monoidal measure (supplied through the static-abstract `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` interfaces), whose monotone-predicate `Split` specializes it into a positional sequence (`SizeMeasure<T>`), a mergeable priority queue (`MaxMeasure<T>`/`MinMeasure<T>`), an ordered search tree (`KeyMeasure<T>`), an order-statistic tree (`OrderStatisticMeasure<T>`), or a persistent Fenwick-style cumulative-weight structure (`SumMeasure<T>`, for weighted selection and sampling) — all shipped ready-made with named operations in `FingerTreeMeasureExtensions` and `FingerTreeSumExtensions`, with any two of them composable through the `ProductMeasure<…>` combinator (e.g. size+sum for a tree that is positional *and* Fenwick, or size+max for a priority queue with positional access), and any custom measure expressible by implementing the interfaces. A full Hinze–Paterson interval tree (`IntervalTree<T>`) with logarithmic stabbing/overlap queries a sorted multiset (`SortedBag<T>`) with ranking and order-statistic indexing, a sorted set (`SortedSet<T>`) with navigable-set queries and set algebra, a sorted dictionary (`SortedDictionary<TKey, TValue>`) with navigable-map queries and order-statistic access, and a meldable `PriorityQueue<TElement, TPriority>` are built on the same core. Each deep node holds its middle subtree behind a memoized suspension and computes its measure lazily — Hinze–Paterson's lazy finger tree realized in a strict language — so the amortized bounds hold under fully persistent (branching) histories; the only consequence versus the deque is that `Measure` is O(1) amortized rather than worst-case (a general monoid has no inverse, so a popped subtree's measure is recovered by forcing, not subtraction). See [docs/api-specification.md](api-specification.md#the-general-measured-finger-tree) for its contract.

`ReversibleDeque<T>` is a sibling deque that adds **O(1) `Reverse`** while preserving all of the deque's bounds (including mixed-orientation `Concat` at O(log min)), via per-node reversal bits read through orientation-aware accessors — at a constant-factor cost, so it is a separate opt-in type rather than a change to the tuned deque.

`RrbVector<T>` is the random-access sibling: a persistent relaxed radix-balanced vector with
32-element leaves, 32-way regular radix branches, and cumulative size tables only on relaxed
branches. It provides uniform O(log32 n) indexing and path-copying updates, O(log32(n + m))
concatenation by merging and repartitioning only the two boundary spines, and an append-only builder
with immutable cached snapshots.

`DabaLite<T, TMonoid>` is the family's deliberately mutable streaming member. It reuses
`IMonoid<T>` to maintain a FIFO window aggregate through the VLDB Journal 2021 six-cursor DABA Lite
schedule. Insert, eviction, and query make at most three, two, and one `Combine` calls respectively;
their total worst-case O(1) bound assumes `Combine` and `Empty` are themselves O(1). A linked
64-slot chunk queue gives worst-case-O(1) cursor movement and growth without retaining retired
prefixes, while `ValidateStructure` audits the callback-free structural state and reports region and
chunk statistics. Mutators give the strong callback exception guarantee, `Clear` is O(1) with zero
`Combine` calls, and callers must externally serialize access to this mutable type.

`CanonicalSortedSet<T>` is the policy-canonical sorted sibling. A retained
`ZipTreeRankPolicy<T>` applies HMAC-SHA256 to a comparer-equivalence-class rank hash, using a
geometric primary rank, a fixed 64-bit secondary rank, and comparer order as the final tie-break.
Versions retaining one policy therefore converge on the same shape for equal contents regardless
of insertion/deletion history. Public seeds reproduce ranks but are not secrets; random policies
use unexposed keys, while `CreateKeyed` accepts caller-retained secret material. Lookup and updates
are expected O(log n) only when rank-hash collisions and key selection preserve pseudorandom-rank
behavior. Every traversal and O(h)-path-copying update is iterative and remains stack-safe when a
colliding policy forces height h = n.

`BrodalOkasakiHeap<T>` is the latency-oriented heap sibling: a persistent bootstrapped
skew-binomial priority queue with O(1) worst-case insert, minimum, and meld and O(log n) worst-case
delete-min. It complements the measured finger-tree priority queue when per-operation worst-case
bounds matter more than constants or stable priority/payload separation. Its public structural
validator audits the fused bootstrapped/skew-binomial representation and reports rank/depth statistics.

`PrioritySearchQueue<TKey, TPriority, TValue>` combines an ordered key map with a min-priority
queue in one persistent AVL core. This is a winner-cached AVL, not Hinze's loser-tree priority-search
pennant. Each node caches its subtree winner, enabling O(log n) keyed updates/deletion, O(1)
minimum, O(log n) delete-min, and key-range plus priority-threshold queries that prune irrelevant key
intervals and subtrees whose minimum priority already exceeds the bound. A query costs O(log n + v)
for v visited nodes, with v ≤ n and therefore an O(n) worst case.

`FingerTreeDeque<T>` is the individually tuned sequence/deque (the analogue of Haskell's `Data.Sequence`, kept separate from the general core just as Haskell keeps it separate from `Data.FingerTree`): an immutable `IReadOnlyList<T>` with O(1) endpoint reads, O(log n) worst-case / O(1) amortized endpoint insertion and removal, concatenation logarithmic in the smaller operand (amortized), indexed access and splitting logarithmic in the distance from the nearer end (amortized), and comparer-based sorted search over rightmost-element signposts with a worst-case near-bound comparer-call count. The representation follows the simplified finger tree of Claessen's *Finger Trees Explained Anew, and Slightly Simplified* (digits of one through three elements, middle nodes of two or three children), with element height encoded through polymorphic recursion, leaf counts plus rightmost-leaf signposts cached per node, and the middle subtree of every deep node held behind a memoize-on-first-force suspension — the strict-language strategy from Hinze and Paterson's original paper that makes the amortized bounds hold under fully persistent (branching) version use. The normative API and complexity contract is [docs/api-specification.md](api-specification.md); its complexity columns were realigned with the source papers' amortized claims (worst-case O(log n), amortized sharp under branching persistence, O(1) worst-case endpoint reads) after the specification review.

## Layout

- `DataStructures.sln` is the solution entry point.
- `src/Tools.DataStructures.FingerTree/` contains the public library.
  - `Measures.cs` — the `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` static-abstract measure interfaces and the built-in `SizeMeasure<T>`.
  - `Comparisons.cs` — the static-abstract `IComparison<T>` order strategy with `DefaultComparison<T>` and `ReverseComparison<T, TComparison>`, letting the comparison measures use a custom order without a hand-rolled measure.
  - `BuiltInMeasures.cs` — ready-made measures (`MaxMeasure<T>`, `MinMeasure<T>`, `KeyMeasure<T>`, `OrderStatisticMeasure<T>`) with their `Optional<T>` / `RankedKey<T>` carriers, covering priority queues, ordered sets, and order-statistic trees out of the box.
  - `SumMeasure.cs` — a generic-math numeric sum measure (`SumMeasure<T>` over `IAdditionOperators`/`IAdditiveIdentity`) turning the measured tree into a persistent Fenwick-style structure, with `FingerTreeSumExtensions` named operations (`SplitByCumulativeWeight`, `TrySelectByCumulativeWeight`) for O(log n) cumulative-weight split and weighted selection / sampling.
  - `ProductMeasure.cs` — the `ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>` combinator (with its `MeasurePair<TFirst, TSecond>` carrier) composing two existing measures into one tree, plus the `ProductMeasures` factory for the headline size+sum / size+max / size+min pairings.
  - `FingerTreeProductExtensions.cs` — component-projecting splits over any product (`SplitByFirst`/`SplitBySecond`/`TrySplitFindByFirst`/`TrySplitFindBySecond`, all five type arguments inferred from the tree) and named operations for the headline compositions: size+sum (`SplitAtIndex`, `SplitByCumulativeWeight`, `TrySelectByCumulativeWeight` reporting the selected index) and size+max / size+min (`TryPeekMax`/`Min`, `TryExtractMax`/`Min`).
  - `FingerTreeMeasureExtensions.cs` — named operations over the ready-made measures (`TryExtractMax`/`Min`, `SplitByLowerBound`/`UpperBound`, `SplitAtIndex`).
  - `IntervalTree.cs` — a full Hinze–Paterson interval tree (`IntervalTree<T>`, `Interval<T>`, `IntervalMeasure<T>`) with O(log n) insert, stabbing/overlap queries, removal and membership, O(k log n) overlap enumeration/count, and O(n) coalescing of overlapping intervals.
  - `SortedBag.cs` — `SortedBag<T>`, an immutable sorted multiset on the order-statistic measure (runtime `IComparer<T>`): O(log n) add/remove/search/rank, order-statistic indexing, range extraction, and O(1) count/min/max.
  - `SortedSet.cs` / `SortedSet.Builder.cs` — `SortedSet<T>`, the uniqueness-enforcing sibling: navigable-set queries (floor/ceiling/lower/higher), order-statistic indexing and ranking, range extraction, O(n + m) set algebra and relations, plus a nested mutable builder for batched edits and cached snapshots.
  - `SortedDictionary.cs` / `SortedDictionary.Builder.cs` — `SortedDictionary<TKey, TValue>` (an `IReadOnlyDictionary`) on a key-projecting order-statistic measure (`EntryMeasure<TKey, TValue>`): O(log n) lookup/set/add/remove, navigable-map neighbor queries, order-statistic access by rank, key-range extraction, plus a nested mutable builder for batched entry edits.
  - `PriorityQueue.cs` — `PriorityQueue<TElement, TPriority>`, a meldable persistent min-priority queue on a count-plus-min measure (`PriorityMeasure`): O(1) amortized enqueue, O(1) min-priority peek, O(log n) peek/dequeue, and O(log(min(n, m))) melding.
  - `RrbVector.cs` — a 32-way relaxed radix-balanced persistent vector with radix-indexed regular
    nodes, relaxed-node size tables, an append-only builder, structural split/edit, and
    boundary-spine concatenation.
  - `DabaLite.cs` — a six-cursor, chunk-queue-backed FIFO sliding-window aggregator over any monoid,
    with bounded callback counts, strong callback exception safety, and structural statistics.
  - `CanonicalSortedSet.cs` / `ZipTreeRankPolicy.cs` — the policy-canonical, stack-safe
    zip-zip-inspired sorted set and its random, publicly seeded, or caller-keyed HMAC rank policy.
  - `BrodalOkasakiHeap.cs` — the bootstrapped skew-binomial heap with optimal purely functional
    worst-case bounds and a public invariant/statistics audit.
  - `PrioritySearchQueue.cs` — the winner-cached keyed AVL priority-search queue, range/threshold
    query surface, and public AVL/winner validation statistics.
  - `FingerTree.cs` — the public general measured finger tree `FingerTree<TElement, TMeasure, TMeasureOps>`.
  - `Rope.cs` / `Rope.Builder.cs` + `Internal/RopeChunk.cs` — `Rope<T>`, a general-purpose persistent **chunked** sequence (rope): elements live in bounded array chunks (`Chunk<T>`, measured by `ChunkLengthMeasure<T>`) at the leaves of a count-measured finger tree, giving cache-friendly storage and O(log n) indexed insert/remove/split/slice with O(log min) concat and structural-sharing persistence. Element-agnostic (`Rope<char>` is a text buffer, `Rope<byte>` a binary buffer); positional reads/splits use the zero-allocation `ElementIndexPredicate` over `TryLocate`; the nested append-only builder uses frozen-prefix snapshots for incremental construction.
  - `Rope.Cursor.cs` + `Internal/RopeCursorPrototype.cs` — the public immutable `RopeCursor<T>` positional edit cursor and its shared zipper engine. A cursor is a gap in `0 .. Count`; movement and edits return branchable cursor values over a 16-element focus with sub-256-element carries, while a winner-returning memo cell materializes one canonical `Rope<T>` snapshot per logical edit version. Linear local-edit histories are O(1) amortized per operation and O(log n) worst-case; arbitrary fan-out retains the honest O(b log n) bound for b branches. See the [C0 decision](rope-cursor-c0-decision.md) for the selected proof scope and the [API specification](api-specification.md#positional-edit-cursor) for the shipped contract.
  - `MeasuredRope.cs` / `MeasuredRope.Builder.cs` + `Internal/MeasuredRopeChunk.cs` — `MeasuredRope<T, TMeasure, TMeasureOps>`, the measured sibling of `Rope<T>`: each chunk additionally caches an arbitrary monoidal user measure (`MeasuredChunk<…>`, measured by the product `MeasuredChunkMeasure<…>` of count and user measure), so the rope supports O(log n) navigation by the measure as well as by position (`Measure`, `PrefixMeasure`, `SplitByMeasure`, `TryLocateByMeasure`). Its append-only builder maintains a live combined measure. The canonical use is a text buffer with a line-count measure for O(log n) offset↔line navigation; the same machinery serves weighted selection and byte-offset addressing.
  - `RopeText.cs` — text conveniences kept as a companion layer so the rope cores stay element-agnostic: the ready-made `NewlineMeasure` and `RopeText` extension methods over `Rope<char>` and `MeasuredRope<char, int, NewlineMeasure>` for string interop (`ToCharRope`/`ToTextRope`/`AsString`), O(log n) line/column navigation (`LineCount`/`LineOfOffset`/`LineStartOffset`/`LineColumnOf`/`OffsetOf`/`GetLine`/`Lines`), and a `TextReader` adapter (`AsTextReader`).
  - `RopeTextExtras.cs` — editor-grade text extras: Unicode scalar-value (code point) addressing (`CodePointCount`/`EnumerateRunes`, streaming, surrogate-aware) and grapheme-cluster addressing (`GraphemeCount`/`EnumerateGraphemes`); character-offset ↔ code-point/grapheme-index conversions (`CodePointIndexToCharOffset`/`CharOffsetToCodePointIndex`/`GraphemeIndexToCharOffset`/`CharOffsetToGraphemeIndex`, round-tripping on boundaries); and carriage-return-aware line handling (`DetectNewlineStyle` over LF/CRLF/CR/mixed, and `GetLineText`/`LinesText` that strip a trailing `\r`).
  - `RopeBuilder.cs` — a fluent, `StringBuilder`-backed text builder that accumulates `Append`s (string/char/span/`Rune`/`AppendLine`) and materializes a chunked `Rope<char>` (`ToRope`) or text rope (`ToTextRope`) through chunk staging in one O(n) pass.
  - `Internal/Measured/` — the general measured core: `MeasuredElements.cs` (element contract, leaf wrapper, grouping nodes), `MeasuredTree.cs` (the abstract level with all shared operations, including the read-only `LocateTree`), `MeasuredTreeLevels.cs` (empty/single/deep levels with the lazily memoized measure), and `MeasuredMiddle.cs` (the memoized middle-subtree suspensions and their pending push/pop operations that make the amortized bounds persistence-robust).
  - `MeasurePredicate.cs` — the public `IMeasurePredicate<TMeasure>` value-type predicate strategy interface (a sibling of `IMonoid`/`IMeasure`/`IComparison`), so callers can write their own **zero-allocation** queries over a raw tree through the public generic `FingerTree.TryLocate<TPredicate>`.
  - `Internal/MeasurePredicates.cs` — the library's own non-capturing struct predicates (`CountAbovePredicate`, `KeyAtLeast`/`KeyAbovePredicate`, `PriorityFrontPredicate`, `MaxHighAtLeastPredicate`, and the `FuncMeasurePredicate` delegate adapter) that route the sorted/order-statistic/priority/interval collections' hot reads through `TryLocate` with zero allocation.
  - `ReversibleDeque.cs` + `Internal/Reversible/` — `ReversibleDeque<T>`, the reversible sibling of `FingerTreeDeque<T>`: the same deque/index/split/concat bounds plus O(1) `Reverse` (every node carries a reversal bit and an O(1) `Mirror`; operations read through orientation-aware accessors so even mixed-orientation `Concat` stays O(log min)). Strict, with higher constant factors — use it when O(1) reverse is needed.
  - `FingerTreeDeque.cs` — public deque type, argument validation, and the struct enumerator.
  - `FingerTreeDequeResults.cs` — split and pop result record structs.
  - `Internal/` — the deque's tuned finger-tree core: `TreeElement.cs` (measured-element contract and the struct `Leaf<T>`), `Digit.cs`, `Node.cs`, `Tree.cs` (empty/single/deep levels), `MiddleTree.cs` (memoized middle-subtree suspensions and their pending operations), and `TreeOperations.cs` (smart deep constructors, pulls with the paper's `chop`, and concatenation).
- [`tests/Tools.DataStructures.FingerTree.Tests/`](../../tests/Tools.DataStructures.FingerTree.Tests/README.md)
  contains the xUnit/CsCheck suite. Its local README maps the deque, measured-tree, derived-collection, rope,
  sample-smoke, property, model-command, persistence, and tearable-concurrency stress test files.
- `benchmarks/Tools.DataStructures.FingerTree.Benchmarks/` is the shared BenchmarkDotNet harness for
  the C# persistent-collections workspace. Alongside the deque, ropes, measures, sorted collections,
  and measured priority queue, it now contains RRB-vector, DABA Lite, canonical zip-set,
  Brodal-Okasaki heap, priority-search-queue, CHAMP, Ctrie, Patricia, and Merkle search-tree gates;
  see its `README.md` for the class-to-contract matrix and run commands.
- `samples/` holds three runnable, smoke-tested console tours (see `samples/README.md`): `Tour` (a persistent text buffer — undo/redo over O(1) snapshots, O(log n) line/column navigation, and a background thread taking millions of lock-free, never-torn snapshots while a writer publishes versions), `Showcase` (one measured tree, many structures — priority queue, weighted sampling, order-statistic set, interval index, reversible deque, navigable map), and `Editor` (the editor-grade text extras — chars vs code points vs graphemes, newline detection, and offset addressing). Run e.g. `dotnet run --project samples/Tools.DataStructures.FingerTree.Editor -c Release`.
- `docs/` contains usage, API, validation, and algorithm design references, including [docs/usage.md](usage.md) as the practical facade-selection and first-use guide; [docs/validation.md](validation.md) as the local restore/build/test, sample, benchmark, and stress-control guide; [docs/FingerTree-Design-Notes.tex](FingerTree-Design-Notes.tex) / [docs/FingerTree-Design-Notes.pdf](FingerTree-Design-Notes.pdf) — a single navigable design-notes tour of the whole library (the two cores, the lazy-memoized spine, the measure framework, the closure-free predicate API, the collection and rope families, the concurrency/memory-model argument, and the three-tier test strategy) — regenerate the PDF from the source with `pwsh -File docs/build-design-notes.ps1`; [docs/benchmarks.md](benchmarks.md) with curated measured results; and [docs/persistence-and-concurrency.md](persistence-and-concurrency.md) — a worked guide to cheap snapshots, structural-sharing undo/redo, and lock-free multi-threaded access (every pattern backed by a runnable example test).

## Validation

Use the local .NET SDK:

```powershell
.\test.ps1
```

The solution builds warning-free with `CS1591`/`CS1573` as errors and the full test suite is expected to pass.
See [`docs/validation.md`](validation.md) for the restore/build/test split, sample smoke coverage,
benchmark boundary, stress controls, and test-suite coverage map.

## Benchmarks

```powershell
cd benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short   # quick pass; drop --job for a full run
```

The harness is a measurement gate, not itself a stored result. Its
[README](../../benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md) maps each benchmark
class to the contract and baseline it exercises; [benchmarks.md](benchmarks.md) is the authoritative
home for curated measured tables. Run the relevant class in Release before making a new constant-
factor or scaling claim, and do not infer results for newly added Axis 1 classes merely from their
presence in the harness.
