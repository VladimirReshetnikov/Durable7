# FingerTree

- Status: Implemented workspace
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers implementing and reviewing the finger-tree deque
- Scope: Project layout and validation entry points for `src/DataStructures/FingerTree`

`src/DataStructures/FingerTree` contains the .NET 10 C# preview workspace for `Tools.DataStructures.FingerTree`, a persistent catenable deque backed by a simplified finger tree.

This workspace provides two public finger-tree types. `FingerTree<TElement, TMeasure, TMeasureOps>` is the general Hinze–Paterson **measured** finger tree — a persistent sequence annotated by an arbitrary monoidal measure (supplied through the static-abstract `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` interfaces), whose monotone-predicate `Split` specializes it into a positional sequence (`SizeMeasure<T>`), a mergeable priority queue (`MaxMeasure<T>`/`MinMeasure<T>`), an ordered search tree (`KeyMeasure<T>`), or an order-statistic tree (`OrderStatisticMeasure<T>`) — all shipped ready-made with named operations in `FingerTreeMeasureExtensions`, and any custom measure expressible by implementing the interfaces. A full Hinze–Paterson interval tree (`IntervalTree<T>`) with logarithmic stabbing/overlap queries a sorted multiset (`SortedBag<T>`) with ranking and order-statistic indexing, a sorted set (`SortedSet<T>`) with navigable-set queries and set algebra, and a sorted dictionary (`SortedDictionary<TKey, TValue>`) with navigable-map queries and order-statistic access are built on the same core. It is strict (eager cached measures); see [docs/api-specification.md](docs/api-specification.md#the-general-measured-finger-tree) for its contract and the strictness boundary versus the deque.

`FingerTreeDeque<T>` is the individually tuned sequence/deque (the analogue of Haskell's `Data.Sequence`, kept separate from the general core just as Haskell keeps it separate from `Data.FingerTree`): an immutable `IReadOnlyList<T>` with O(1) endpoint reads, O(log n) worst-case / O(1) amortized endpoint insertion and removal, concatenation logarithmic in the smaller operand (amortized), indexed access and splitting logarithmic in the distance from the nearer end (amortized), and comparer-based sorted search over rightmost-element signposts with a worst-case near-bound comparer-call count. The representation follows the simplified finger tree of Claessen's *Finger Trees Explained Anew, and Slightly Simplified* (digits of one through three elements, middle nodes of two or three children), with element height encoded through polymorphic recursion, leaf counts plus rightmost-leaf signposts cached per node, and the middle subtree of every deep node held behind a memoize-on-first-force suspension — the strict-language strategy from Hinze and Paterson's original paper that makes the amortized bounds hold under fully persistent (branching) version use. The normative API and complexity contract is [docs/api-specification.md](docs/api-specification.md); its complexity columns were realigned with the source papers' amortized claims (worst-case O(log n), amortized sharp under branching persistence, O(1) worst-case endpoint reads) following an adjudicated [spec defect report](docs/api-specification-defect-report-complexity-guarantees.md).

## Layout

- `FingerTree.sln` is the solution entry point.
- `src/Tools.DataStructures.FingerTree/` contains the public library.
  - `Measures.cs` — the `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` static-abstract measure interfaces and the built-in `SizeMeasure<T>`.
  - `Comparisons.cs` — the static-abstract `IComparison<T>` order strategy with `DefaultComparison<T>` and `ReverseComparison<T, TComparison>`, letting the comparison measures use a custom order without a hand-rolled measure.
  - `BuiltInMeasures.cs` — ready-made measures (`MaxMeasure<T>`, `MinMeasure<T>`, `KeyMeasure<T>`, `OrderStatisticMeasure<T>`) with their `Optional<T>` / `RankedKey<T>` carriers, covering priority queues, ordered sets, and order-statistic trees out of the box.
  - `FingerTreeMeasureExtensions.cs` — named operations over the ready-made measures (`TryExtractMax`/`Min`, `SplitByLowerBound`/`UpperBound`, `SplitAtIndex`).
  - `IntervalTree.cs` — a full Hinze–Paterson interval tree (`IntervalTree<T>`, `Interval<T>`, `IntervalMeasure<T>`) with O(log n) insert, stabbing/overlap queries, removal and membership, O(k log n) overlap enumeration/count, and O(n) coalescing of overlapping intervals.
  - `SortedBag.cs` — `SortedBag<T>`, an immutable sorted multiset on the order-statistic measure (runtime `IComparer<T>`): O(log n) add/remove/search/rank, order-statistic indexing, range extraction, and O(1) count/min/max.
  - `SortedSet.cs` — `SortedSet<T>`, the uniqueness-enforcing sibling: navigable-set queries (floor/ceiling/lower/higher), order-statistic indexing and ranking, range extraction, and O(n + m) set algebra (union/intersect/except/symmetric-except) and relations (subset/superset/overlaps/set-equals).
  - `SortedDictionary.cs` — `SortedDictionary<TKey, TValue>` (an `IReadOnlyDictionary`) on a key-projecting order-statistic measure (`EntryMeasure<TKey, TValue>`): O(log n) lookup/set/add/remove, navigable-map neighbor queries, order-statistic access by rank, and key-range extraction.
  - `FingerTree.cs` — the public general measured finger tree `FingerTree<TElement, TMeasure, TMeasureOps>`.
  - `Internal/Measured/` — the general measured core: `MeasuredElements.cs` (element contract, leaf wrapper, grouping nodes), `MeasuredTree.cs` (the abstract level with all shared operations), and `MeasuredTreeLevels.cs` (empty/single/deep levels).
  - `FingerTreeDeque.cs` — public deque type, argument validation, and the struct enumerator.
  - `FingerTreeDequeResults.cs` — split and pop result record structs.
  - `Internal/` — the deque's tuned finger-tree core: `TreeElement.cs` (measured-element contract and the struct `Leaf<T>`), `Digit.cs`, `Node.cs`, `Tree.cs` (empty/single/deep levels), `MiddleTree.cs` (memoized middle-subtree suspensions and their pending operations), and `TreeOperations.cs` (smart deep constructors, pulls with the paper's `chop`, and concatenation).
- `tests/Tools.DataStructures.FingerTree.Tests/` contains the xUnit suite: for the deque — API contract tests, invariant tests (through `InternalsVisibleTo`), branching-persistence tests, sorted-search edge tests, enumeration/copy tests, randomized model comparisons that validate internal invariants after every step, and complexity-guard tests that pin the near-bound comparer-call counts, the O(1) endpoint allocation behavior, and the flat marginal cost of replaying operations on retained versions; for the general measured tree (`MeasuredFingerTreeTests.cs`) — positional-split-versus-list-slice laws, concatenation order/measure/associativity, and the priority-queue, ordered-search, and order-statistic specializations driven through `Split`; and for the ready-made measures (`BuiltInMeasureTests.cs`) — priority-queue drain order, key-measured lower/upper-bound splits versus a binary-search model, and the order-statistic tree being positional and ordered at once; and for the interval tree (`IntervalTreeTests.cs`) — insertion low-order, single/all overlap and point-stabbing queries versus a brute-force model, overlap counting, value-equal membership/removal with duplicates, and coalescing versus a sweep-merge model; for custom comparisons (`CustomComparisonMeasureTests.cs`) — projection-ordered and reversed priority queues and ordered splits; for the sorted multiset (`SortedBagTests.cs`) — sorted order with duplicates, ranking and order-statistic indexing, range extraction, one-versus-all removal, stable construction, and custom comparers, against a list model; for the sorted set (`SortedSetTests.cs`) — uniqueness, navigable queries, indexing/ranking, range, and full set algebra/relations against the BCL `SortedSet<T>` as an oracle; and for the sorted dictionary (`SortedDictionaryTests.cs`) — key order with last-wins, set/add/remove/lookup, navigable-map queries, order-statistic access, and range against the BCL `SortedDictionary<TKey, TValue>`.
- `docs/` contains API and algorithm design references.

## Validation

Use the local .NET SDK:

```powershell
dotnet test .\FingerTree.sln
```

The solution builds warning-free with `CS1591`/`CS1573` as errors and the full test suite is expected to pass.
