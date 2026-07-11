# C# FingerTree API Specification

- Status: Current normative API specification
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers, reviewers, and implementers of the C# FingerTree workspace
- Scope: Public API shape, semantic contracts, complexity targets, and collection roles for the C# FingerTree library
- Related code:
  - `src/CSharp/src/Tools.DataStructures.FingerTree/`
- Related first-use guide:
  - [C# FingerTree usage guide](usage.md)
- Related docs (external reference material, segregated under [`external/`](external/README.md)):
  - [Finger Trees Explained Anew, and Slightly Simplified](<external/Finger Trees Explained Anew, and Slightly Simplified.tex>)
  - [Finger trees: a simple general-purpose data structure](<external/Finger trees - a simple general-purpose data structure/Finger trees - a simple general-purpose data structure.tex>)
  - [Haskell containers 0.8 `Data.Sequence.Internal`](external/containers-0.8/src/Data/Sequence/Internal.hs)

## Summary

This document specifies the public API contract for the C# FingerTree workspace. It opens with the tuned `FingerTreeDeque<T>` contract, then records the sibling measured tree, sorted collections, priority queue, interval tree, reversible deque, and rope contracts that now ship from the same library. For first-use examples and facade selection, start with the [usage guide](usage.md).

## Scope And Non-Goals

### In Scope

- The tuned `FingerTreeDeque<T>` persistent immutable deque with structural sharing.
- Constant worst-case `Count`, emptiness, first-element, and last-element queries.
- End insertion and removal with logarithmic worst-case and constant amortized (persistent) complexity.
- End replacement through `SetItem(0, value)` and `SetItem(Count - 1, value)` with constant worst-case complexity.
- Catenation with logarithmic complexity in the smaller input.
- Indexed read, update, insertion, deletion, range extraction, and split.
- Sorted-search helpers on `FingerTreeDeque<T>` for lower bound, upper bound, equal ranges, and comparer-compatible binary-search semantics.
- The measured tree and derived collection contracts in the later sections of this document.
- Exact exception and edge-case behavior for public methods.
- Public complexity guarantees for time and allocation.

### Outside The Deque-Specific Contract

- Generic measured-tree semantics. The public generic measured finger tree now ships as the sibling type `FingerTree<TElement, TMeasure, TMeasureOps>` (with the `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` static-abstract measure interfaces). It is governed by its own contract in [The General Measured Finger Tree](#the-general-measured-finger-tree) rather than by these deque-specific bullets.
- Sorted-set uniqueness semantics. A sorted deque may contain duplicates.
- A mutable builder for `FingerTreeDeque<T>`. A builder can be added later without changing the core immutable API.
- Hard real-time worst-case constant deque-end insertions and removals. Finger-tree pushes and pops are amortized constant but can be logarithmic in an individual operation; this does not apply to replacing an existing end element with `SetItem`; see [Why Endpoint Insertions And Removals Are Not Worst-Case Constant](#why-endpoint-insertions-and-removals-are-not-worst-case-constant).
- Validation that a sequence is sorted before a sorted-search helper runs. Debug-only validation may be added, but release behavior must not pay linear validation cost.

## Terminology And Mental Model

- **Deque**: A double-ended queue. Elements can be read, inserted, and removed at both ends.
- **Catenable**: Two instances can be concatenated without copying either complete input.
- **Persistent**: Operations return new instances and leave all existing instances usable.
- **Structural sharing**: New instances reuse unchanged internal nodes from older instances.
- **Digit**: A bounded prefix or suffix buffer at a finger-tree level. This implementation follows the simplified paper and uses digits of length 1 through 3.
- **Node**: A middle-tree element containing 2 or 3 child elements.
- **Measure**: Cached per-subtree metadata. The public deque needs at least element count and rightmost element signposts.
- **Sorted helper**: A method whose result is specified only when the sequence is sorted according to the same comparer supplied to that method.

## Public Package Shape

The implementation lives under `src/CSharp/src/Tools.DataStructures.FingerTree/` with this public namespace and primary type:

```csharp
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

public sealed class FingerTreeDeque<T> : IReadOnlyList<T>
{
    public static FingerTreeDeque<T> Empty { get; }

    public int Count { get; }
    public bool IsEmpty { get; }

    public T First { get; }
    public T Last { get; }
    public T this[int index] { get; }

    public static FingerTreeDeque<T> Create(params ReadOnlySpan<T> items);
    public static FingerTreeDeque<T> CreateRange(IEnumerable<T> items);

    public FingerTreeDeque<T> AddFirst(T item);
    public FingerTreeDeque<T> AddLast(T item);
    public FingerTreeDeque<T> AddRange(IEnumerable<T> items);
    public FingerTreeDeque<T> AddRange(FingerTreeDeque<T> items);
    public FingerTreeDeque<T> Concat(FingerTreeDeque<T> other);

    public FingerTreeDeque<T> RemoveFirst();
    public FingerTreeDeque<T> RemoveLast();
    public FingerTreeDequePop<T> PopFirst();
    public FingerTreeDequePop<T> PopLast();
    public bool TryPeekFirst([MaybeNullWhen(false)] out T value);
    public bool TryPeekLast([MaybeNullWhen(false)] out T value);
    public bool TryPopFirst([MaybeNullWhen(false)] out T value, out FingerTreeDeque<T> rest);
    public bool TryPopLast([MaybeNullWhen(false)] out T value, out FingerTreeDeque<T> rest);

    public bool TryGetItem(int index, [MaybeNullWhen(false)] out T value);
    public FingerTreeDeque<T> SetItem(int index, T value);
    public FingerTreeDeque<T> UpdateAt(int index, Func<T, T> updater);
    public FingerTreeDeque<T> InsertAt(int index, T item);
    public FingerTreeDeque<T> InsertRange(int index, IEnumerable<T> items);
    public FingerTreeDeque<T> RemoveAt(int index);
    public FingerTreeDeque<T> RemoveRange(int index, int count);
    public FingerTreeDeque<T> GetRange(int index, int count);

    public FingerTreeDequeSplit<T> SplitAt(int index);
    public FingerTreeDequeItemSplit<T> SplitItemAt(int index);
    public FingerTreeDequeRangeSplit<T> SplitRange(int index, int count);

    public int SortedLowerBound(T item, IComparer<T>? comparer = null);
    public int SortedUpperBound(T item, IComparer<T>? comparer = null);
    public int SortedBinarySearch(T item, IComparer<T>? comparer = null);
    public bool SortedContains(T item, IComparer<T>? comparer = null);
    public FingerTreeDequeSplit<T> SplitAtSortedLowerBound(T item, IComparer<T>? comparer = null);
    public FingerTreeDequeSplit<T> SplitAtSortedUpperBound(T item, IComparer<T>? comparer = null);
    public FingerTreeDequeRangeSplit<T> SplitAtSortedEqualRange(T item, IComparer<T>? comparer = null);
    public FingerTreeDeque<T> InsertSorted(T item, IComparer<T>? comparer = null);
    public FingerTreeDeque<T> RemoveAllSorted(T item, IComparer<T>? comparer = null);

    public T[] ToArray();
    public void CopyTo(T[] array, int arrayIndex);
    public Enumerator GetEnumerator();
}
```

The `params ReadOnlySpan<T>` factory requires a target framework that supports span-backed `params`. If the project must target an older framework, the implementation should instead expose these overloads:

```csharp
public static FingerTreeDeque<T> Create(params T[] items);
public static FingerTreeDeque<T> Create(ReadOnlySpan<T> items);
```

The type should not implement `IList<T>`, `ICollection<T>`, or non-read-only collection interfaces. It should not implement `IImmutableList<T>` in the first version, because that interface imposes a broad list-shaped API whose weaker operations can obscure the finger-tree-specific complexity contract.

## Result Types

Result structs make multi-value operations self-documenting and avoid public tuple field names becoming part of call-site folklore.

```csharp
namespace Tools.DataStructures.FingerTree;

public readonly record struct FingerTreeDequeSplit<T>(
    FingerTreeDeque<T> Left,
    FingerTreeDeque<T> Right);

public readonly record struct FingerTreeDequeItemSplit<T>(
    FingerTreeDeque<T> Left,
    T Item,
    FingerTreeDeque<T> Right);

public readonly record struct FingerTreeDequeRangeSplit<T>(
    FingerTreeDeque<T> Before,
    FingerTreeDeque<T> Range,
    FingerTreeDeque<T> After);

public readonly record struct FingerTreeDequePop<T>(
    T Value,
    FingerTreeDeque<T> Rest);
```

The `PopFirst` result stores the removed first value and the remaining suffix. The `PopLast` result stores the removed last value and the remaining prefix.

## Deque Semantics

`Empty` is the canonical empty sequence. Empty instances created through operations should return the canonical empty instance unless doing so would complicate implementation without observable benefit.

`First` and `Last` return the endpoint elements in O(1). On an empty deque they throw `InvalidOperationException`.

`TryPeekFirst` and `TryPeekLast` return `false` on an empty deque and set `value` to `default!`. They return `true` and the endpoint value otherwise.

`AddFirst` and `AddLast` preserve all existing versions. They accept `null` when `T` permits it.

`RemoveFirst` and `RemoveLast` throw `InvalidOperationException` on an empty deque. Their `TryPop*` counterparts return `false`, set `value` to `default!`, and set `rest` to `Empty`.

`Concat` returns a sequence whose elements are all elements of the receiver followed by all elements of `other`. It must preserve the relative order and multiplicity of both inputs. Concatenating with `Empty` should return the non-empty input instance when possible.

`AddRange(FingerTreeDeque<T>)` is equivalent to `Concat`. `AddRange(IEnumerable<T>)` enumerates `items` once, appending elements in enumeration order. Passing `null` as an enumerable throws `ArgumentNullException`.

`Count` is an `int`, matching the .NET collection ecosystem. Any operation that would create a deque with more than `int.MaxValue` elements must throw `InvalidOperationException` or `OverflowException` before publishing an invalid instance.

## Indexed Semantics

All index parameters are zero-based.

`this[int index]` returns the element at `index`. It throws `ArgumentOutOfRangeException` when `index < 0` or `index >= Count`.

`TryGetItem` returns `false` for an out-of-range index and sets `value` to `default!`. It returns `true` and the element otherwise.

`SetItem` returns a new deque with exactly one replaced element. It must not interpret equality as a no-op; callers can rely on the operation preserving length and replacing the stored value even when the new value compares equal to the old value.

`UpdateAt` reads the current element, calls `updater` exactly once, and stores the returned value. It throws `ArgumentNullException` when `updater` is null. If `updater` throws, the original deque remains unchanged.

`InsertAt` accepts `index` in the inclusive range `0..Count`. `index == 0` is `AddFirst`; `index == Count` is `AddLast`. Other values split the deque at `index` and insert between the two parts.

`RemoveAt` accepts `index` in the range `0..Count - 1` and removes exactly that element.

`GetRange(index, count)` returns exactly `count` elements starting at `index`. `RemoveRange(index, count)` removes exactly that range. `SplitRange(index, count)` returns the prefix before the range, the range itself, and the suffix after it. These methods throw `ArgumentOutOfRangeException` when `index < 0`, `count < 0`, `index > Count`, or `count > Count - index`, using the subtraction form to avoid signed-integer overflow.

## Split Semantics

`SplitAt(index)` accepts `index` in the inclusive range `0..Count`.

- `SplitAt(0)` returns `(Empty, this)`.
- `SplitAt(Count)` returns `(this, Empty)`.
- For any valid `index`, `Left.Count == index`, `Right.Count == Count - index`, and `Left.Concat(Right)` is sequence-equal to the original deque.

`SplitItemAt(index)` accepts `index` in the range `0..Count - 1`.

- `Left.Count == index`.
- `Item` is the original element at `index`.
- `Right.Count == Count - index - 1`.
- `Left.AddLast(Item).Concat(Right)` is sequence-equal to the original deque.

`SplitRange(index, count)` accepts the same range as `GetRange`.

- `Before.Count == index`.
- `Range.Count == count`.
- `After.Count == Count - index - count`.
- `Before.Concat(Range).Concat(After)` is sequence-equal to the original deque.

## Sorted Search Semantics

Sorted methods are specified only under this precondition:

The deque is sorted in nondecreasing order according to the comparer used for the call, and comparer results for stored elements are stable during the call.

If `comparer` is null, the method uses `Comparer<T>.Default`. If the default comparer cannot compare the stored values and search item, the comparer exception is part of the method behavior.

Sorted helpers do not store the comparer in the deque. A deque can be searched with any comparer, but the result is specified only when the current element order is sorted by that comparer.

### Lower Bound

`SortedLowerBound(item, comparer)` returns the first index `i` such that `comparer.Compare(this[i], item) >= 0`. If all elements are less than `item`, it returns `Count`.

`SplitAtSortedLowerBound(item, comparer)` returns `(Less, GreaterOrEqual)` where every element of `Less` compares less than `item`, and every element of `GreaterOrEqual` compares greater than or equal to `item`.

### Upper Bound

`SortedUpperBound(item, comparer)` returns the first index `i` such that `comparer.Compare(this[i], item) > 0`. If no element is greater than `item`, it returns `Count`.

`SplitAtSortedUpperBound(item, comparer)` returns `(LessOrEqual, Greater)` where every element of `LessOrEqual` compares less than or equal to `item`, and every element of `Greater` compares greater than `item`.

### Binary Search

`SortedBinarySearch(item, comparer)` returns deterministic .NET-style binary-search output:

- If at least one equal element exists, it returns the first equal element index, which is the lower bound.
- If no equal element exists, it returns the bitwise complement of the insertion index, `~SortedLowerBound(item, comparer)`.

Returning the first equal element is a stronger guarantee than `Array.BinarySearch`, whose matching duplicate is unspecified. The stronger guarantee follows naturally from lower-bound search and is useful for persistent editing.

`SortedContains(item, comparer)` is equivalent to checking whether `SortedBinarySearch` returns a nonnegative index.

### Equal Range

`SplitAtSortedEqualRange(item, comparer)` returns `(Before, Range, After)` where:

- every element of `Before` compares less than `item`;
- every element of `Range` compares equal to `item`;
- every element of `After` compares greater than `item`.

`InsertSorted(item, comparer)` inserts after existing equal elements by using the upper bound. This preserves the relative order of equal elements already present in the deque.

`RemoveAllSorted(item, comparer)` removes the complete equal range.

### Unsorted Inputs

When the sorted precondition is false, sorted helper results are unspecified. The methods must still preserve memory safety and immutable-instance validity, but callers must not rely on any returned index or partition.

## Complexity Model

Let:

- `n` be the receiver count;
- `m` be the count of another deque or inserted range;
- `i` be a valid element index;
- `s` be a split index in `0..n`;
- `r` be a range length;
- `k` be the lower-bound or upper-bound result index;
- `nearIndex(i, n) = min(i + 1, n - i)`;
- `nearSplit(s, n) = min(s, n - s)`;
- `nearBound(k, n) = min(k + 1, n - k + 1)`.

Complexities below assume comparer calls, delegate calls, and element assignment are O(1). If a comparer or updater is more expensive, its own cost is additional.

| Operation | Worst-case time | Amortized time | Additional allocation |
| --- | ---: | ---: | ---: |
| `Empty`, `IsEmpty`, `Count` | O(1) | O(1) | O(1) |
| `First`, `Last`, `TryPeekFirst`, `TryPeekLast` | O(1) ‡ | O(1) | O(1) |
| `CreateRange(IEnumerable<T>)` | O(n) | O(n) | O(n) |
| `ToArray`, `CopyTo` | O(n) | O(n) | O(n) for `ToArray`, O(log n) for `CopyTo` ◇ |
| Enumeration | O(n) total, O(log n) setup | O(1) per yielded element | O(log n) enumerator stack |
| `AddFirst`, `AddLast` | O(log n) | O(1) † | O(log n) worst, O(1) amortized † |
| `RemoveFirst`, `RemoveLast`, `PopFirst`, `PopLast` | O(log n) | O(1) † | O(log n) worst, O(1) amortized † |
| `Concat` / `AddRange(FingerTreeDeque<T>)` | O(log(n + m)) | O(log(min(n, m) + 1)) † | O(log(n + m)) worst, sharp amortized † |
| `AddRange(IEnumerable<T>)` | O(m + log(n + 1)) | same | O(m + log(n + 1)) |
| Indexer get, `TryGetItem` | O(log n); O(1) at i ∈ {0, n − 1} ‡ | O(1 + log nearIndex(i, n)) † | O(log n) worst, O(1) at endpoints ‡ |
| `SetItem`, `UpdateAt` | O(log n); O(1) at i ∈ {0, n − 1} ‡ | O(1 + log nearIndex(i, n)) † | O(log n) worst, sharp amortized; O(1) at endpoints ‡ |
| `InsertAt` | O(log n) | O(1 + log(nearSplit(index, n) + 1)) † | O(log n) worst, sharp amortized † |
| `RemoveAt` | O(log n) | O(1 + log nearIndex(i, n)) † | O(log n) worst, sharp amortized † |
| `SplitAt` | O(log n) | O(1 + log(nearSplit(s, n) + 1)) † | O(log n) worst, sharp amortized † |
| `SplitItemAt` | O(log n) | O(1 + log nearIndex(i, n)) † | O(log n) worst, sharp amortized † |
| `GetRange`, `RemoveRange`, `SplitRange` | O(log n) | same | O(log n) |
| `SortedLowerBound`, `SortedUpperBound` | O(1 + log nearBound(k, n)) § | same | O(1) § |
| `SplitAtSortedLowerBound`, `SplitAtSortedUpperBound` | O(log n) | O(1 + log nearBound(k, n)) † | O(log n) worst, sharp amortized † |
| `SortedBinarySearch`, `SortedContains` | O(1 + log nearBound(k, n)) § | same | O(1) § |
| `SplitAtSortedEqualRange`, `RemoveAllSorted` | O(log n) | same | O(log n) |
| `InsertSorted` | O(log n) | O(1 + log n) † | O(log n) |

Footnotes to the table:

- **†** The amortized bound is the sharp "distance to the nearer end" cost for the split family, and the log-of-smaller-operand cost for concatenation. It holds under fully persistent (branching) version use, because the middle subtree of each deep node is held behind a memoize-on-first-force suspension (the original paper's strict-language strategy; see [Worst-Case Versus Amortized Guarantees](#worst-case-versus-amortized-guarantees)). The worst-case column is the cost of a single call that must force deferred spine work, bounded by O(log n): the *descent* alone is sharp worst-case, but the reconstruction (smart deep constructors pulling from a middle, and the carry pushes at the bottom of `glue`) can cascade the full spine. The cascades telescope (no O(log² n) compounding) and are paid for by the potential released when dangerous `One`/`Three` digits collapse to safe `Two` digits.
- **‡** Endpoint reads never force a suspension, because the first and last elements live in the top prefix and suffix digits and the middle leaf count is cached arithmetically. `First`, `Last`, `TryPeekFirst`, `TryPeekLast`, and the indexer and `TryGetItem` at i ∈ {0, n − 1} are therefore O(1) worst-case. Endpoint replacement (`SetItem`/`UpdateAt` at i ∈ {0, n − 1}) likewise rebuilds only the top digit and is O(1) worst-case.
- **§** For sorted searches the stated bound is the comparer-call count, which is worst-case (forcing performs no comparisons), and is the contract that matters when comparisons dominate. Wall-clock time and allocation additionally include amortized forcing of deferred spine work bounded by O(log n) per call; a search whose result index is at the nearer end may resolve within the top digits and force nothing.
- **◇** `CopyTo` allocates no destination storage; the O(log n) is the one-time forcing of any pending spine work, memoized for later readers. On an already-forced tree it is O(1).

The sharp split and search bounds come from finger-tree cost analysis: the *descent* path length is logarithmic in the smaller side created by the split or in the distance to the nearer search bound, not in the whole sequence. It is still correct to document these operations as O(log n) in XML summaries when a shorter summary is preferable, but the detailed docs should preserve the sharper amortized bound and the endpoint worst-case guarantee.

## Worst-Case Versus Amortized Guarantees

The endpoint insertion and removal guarantee is:

- worst-case O(log n);
- amortized O(1) across any valid persistent usage pattern, including branching version histories;
- O(1) worst-case endpoint reads, and O(1) worst-case endpoint replacement.

The same split-distance amortized bounds (sharp "distance to the nearer end" for the split family, log-of-smaller-operand for concatenation) hold under the same fully persistent usage, with O(log n) worst-case per single call. Accessing — as opposed to inserting or removing — the first and last elements is O(1) worst-case unconditionally.

The simplified paper gives a sequential physicist-method proof and notes that persistent amortized complexity requires the usual finger-tree laziness argument; Hinze and Paterson's original paper obtains the persistent bounds by suspending the middle subtree of each deep node and discharges them with an Okasaki debit analysis. C# is strict by default, so the implementation must not casually inherit the Haskell claim by structural resemblance. This specification permits advertising persistent amortized O(1) only when the implementation has one of the following:

- an internal memoized-suspension strategy with tests that exercise branching persistent histories; or
- a written proof or proof sketch showing that the strict representation preserves the advertised persistent amortized bound; or
- a downgraded public guarantee that says the relevant operations are O(log n) worst-case and O(1) amortized only for single-threaded linear use of versions.

The current implementation satisfies the first option. It holds the middle subtree of every deep node behind a memoize-on-first-force suspension, exactly Hinze and Paterson's strict-language strategy ("we need only suspend the middle subtree of each Deep node, so only Θ(log n) suspensions are required"). Each suspension is created over an already-forced source and defers exactly one operation — the paper's refinement that forces the middle subtree in the recursive case "to avoid building a chain of suspensions" — so a single force never recurses deeper than the tree height, and `AddFirst`/`AddLast` are O(log n) worst-case rather than the O(1) worst-case the bare-suspension variant would give. Forcing publishes its result with a compare-exchange, so the bounds are robust under branching persistence: once any version forces a shared suspension, every version sharing it reads the memoized result. The branching-persistence and deterministic complexity-guard tests required by the first option are part of the suite (they assert, among other things, that replaying an endpoint or near-end operation on a single retained version has size-independent marginal cost, which a fully strict representation cannot achieve).

One trade-off is recorded for honesty: with suspended middles, an indexed read or sorted search that descends into the middle may force pending spine work, so those operations' wall-clock time becomes amortized rather than worst-case sharp (the sorted-search comparer-call counts remain worst-case sharp, since forcing performs no comparisons). This matches `Data.Sequence`, whose every documented bound is amortized for the same reason. Endpoint reads are unaffected and remain O(1) worst-case.

This specification records guarantees the implementation meets and tests, not an aspirational target.

## Why Endpoint Insertions And Removals Are Not Worst-Case Constant

The exclusion of hard real-time worst-case O(1) endpoint insertions and removals is a deliberate scope boundary, not a claim that such data structures are impossible. It follows from the representation chosen for `FingerTreeDeque<T>`.

In the simplified finger tree, `AddFirst` usually modifies only the top prefix digit. When the prefix digit is already full, it must leave a neutral digit at the current level and push a node into the middle tree. If the middle tree is also at an overflow point, the same repair recurses one level down. A specially shaped tree can therefore force one endpoint insertion or removal to traverse the whole recursive spine, which is O(log n). `AddLast`, `RemoveFirst`, and `RemoveLast` have symmetric overflow or underflow cases.

The amortized O(1) result is obtained by arranging that recursive endpoint repairs eliminate "dangerous" digit states. In the simplified paper's potential function, `One` and `Three` digits carry potential and `Two` digits do not. A recursive update pays for itself by turning a dangerous digit into a neutral one, so the average endpoint cost over a well-accounted history is constant. That argument bounds accumulated cost; it does not bound the latency of each individual public call.

Replacing an existing endpoint element is different. `SetItem(0, value)` rewrites the first element in the top prefix digit, and `SetItem(Count - 1, value)` rewrites the last element in the top suffix digit. No middle-tree overflow or underflow repair is needed, and the operation only rebuilds the root-level path that directly stores the endpoint. For a singleton tree, the single stored value is replaced. Therefore endpoint replacement should be specified and implemented as O(1) worst-case.

Guaranteeing worst-case O(1) endpoint insertions and removals would require a different engineering contract. The implementation would need to ensure that no public call ever performs an unbounded cascade, even when the tree contains dangerous states at every level. Plausible routes include:

- A real-time scheduling layer that represents recursive repairs as pending work and advances a bounded amount of that work on each subsequent operation.
- An explicitly lazy or memoized internal representation, adapted carefully to strict C#, so recursive repairs can be shared, forced incrementally, and made thread-safe under persistence.
- A different worst-case catenable-deque representation, such as the more involved segmented-number-system family discussed in the original finger-tree paper's related work, with finger-tree-style measurements added only if they can preserve split and search.
- A hybrid representation with local endpoint buffers plus a measured finger-tree core, together with invariants that prove buffer refills, drains, catenation, splitting, and sorted search cannot accumulate more deferred work than future operations are guaranteed to discharge.

Those routes are substantially more than local tuning. They require new internal state, new invariants, and a proof or at least a mechanically stress-tested accounting discipline for persistent branching histories. For this collection, the main design value is the measured finger-tree operation set: catenation, indexing, splitting, and sorted search with a compact persistent representation. Accepting O(log n) worst-case endpoint insertions and removals keeps that design understandable and testable while still providing the expected O(1) amortized deque behavior once the implementation proves the amortized contract.

If a future use case requires per-operation push/pop latency independent of `Count`, it should be treated as a separate design requirement. At that point the project should either introduce a distinct `RealTimeDeque<T>`-style type with a narrower API, or revise this type's internals only after the worst-case scheduling invariants are documented alongside the existing split and search invariants.

## Internal Design Obligations

The public API does not expose implementation nodes, but the implementation should follow the simplified finger-tree shape unless benchmarks or proof obligations justify changing it:

```text
Tree<T> =
    Empty
  | Single(T)
  | Deep(Digit<T> prefix, Tree<Node<T>> middle, Digit<T> suffix)

Digit<T> = One(T) | Two(T, T) | Three(T, T, T)
Node<T> = Node2(T, T) | Node3(T, T, T)
```

Every tree, node, and deep node must cache enough measure data to implement public operations without scanning subtrees:

- `Count`: number of leaf elements represented by the subtree.
- `Last`: the rightmost leaf element, used as a sorted-search signpost.
- `HasLast`: needed for empty subtrees and nullable element types.

The count measure drives indexed split and lookup. The rightmost-element signpost drives sorted lower-bound and upper-bound search: in a sorted subtree, if the subtree's rightmost element is still less than the target, the whole subtree can be skipped.

The implementation may cache digit measures as well. The asymptotic complexity is unchanged because digits have bounded length, but cached digit summaries may reduce comparer calls in sorted search.

Endpoint insertion and removal must preserve the simplified-paper potential invariant:

- Recursive `AddFirst` overflow from a `Three` prefix must leave a `Two` prefix at the current level and push a `Node2` into the middle tree.
- Recursive `AddLast` overflow is symmetric.
- Removing from a one-element prefix must pull from the middle tree when needed.
- When the pulled middle node is a `Node3`, the implementation should expose one element and push the remaining pair back without recursing, following the paper's `chop` idea.
- Symmetric suffix removal follows the same rule.

These rules are not merely implementation taste. They are what creates a neutral digit state and pays for future endpoint operations in the amortized analysis.

To make the amortized bounds robust under branching persistence, the middle subtree of each deep node is held behind a memoize-on-first-force suspension rather than being computed eagerly, following Hinze and Paterson's strict-language strategy. The cached count measure of a deep node is therefore stored explicitly and derived arithmetically at every construction site, never by forcing the middle, so `Count`, emptiness, index routing, endpoint reads, and endpoint replacement remain force-free and worst-case constant where the table promises it. Each suspension is created over an already-forced source middle and defers exactly one push or pop, so forcing one suspension never recurses deeper than the tree height; this is the paper's refinement of forcing the recursive-case middle "to avoid building a chain of suspensions". Endpoint insertion is consequently O(log n) worst-case (an overflow forces the old middle before suspending its push), not O(1) worst-case.

## Thread Safety And Persistence

Instances are immutable snapshots. Reading the same instance concurrently from multiple threads is safe.

The implementation uses memoized internal suspensions (the middle subtree of each deep node), and forcing them is thread-safe: a forcer reads the suspension state with acquire semantics, runs the pure pending operation over immutable captured state, and publishes the result with a compare-exchange. It is acceptable for two racing readers to duplicate that bounded computation, but a race can never publish a partially initialized node, corrupt a cached measure, or make equal public operations observe different element sequences, because at most one result is ever published per suspension cell and all forcers converge on it.

The collection does not deep-freeze element objects. If `T` is a mutable reference type and callers mutate an object after insertion, the deque still preserves the object reference, but sorted-search results are specified only if the comparer order remains stable.

## Equality And Identity

`FingerTreeDeque<T>` should not override `Equals` or `GetHashCode`. Structural equality is O(n), and making it the default object equality would hide a linear operation behind common APIs.

Callers that need structural equality should use `Enumerable.SequenceEqual`, a future explicit `SequenceEqual` helper, or a comparer-aware method with clear O(n) documentation.

Operations may return an existing instance when the result is observably identical and no element equality test is required. Callers must not depend on reference identity to detect logical equality or no-op behavior.

## Examples

Basic persistent deque usage:

```csharp
var items = FingerTreeDeque<int>.Empty
    .AddLast(10)
    .AddLast(20)
    .AddFirst(5);

var split = items.SplitAt(2);

// split.Left:  [5, 10]
// split.Right: [20]
// items is still [5, 10, 20]
```

Indexed update:

```csharp
var updated = items.UpdateAt(1, static value => value + 1);

// updated: [5, 11, 20]
// items:   [5, 10, 20]
```

Sorted search and insertion:

```csharp
var sorted = FingerTreeDeque<int>.Create(1, 3, 3, 7, 9);

int lower = sorted.SortedLowerBound(3);
int upper = sorted.SortedUpperBound(3);
int binary = sorted.SortedBinarySearch(4);
var equalRange = sorted.SplitAtSortedEqualRange(3);
var inserted = sorted.InsertSorted(5);

// lower == 1
// upper == 3
// binary == ~3
// equalRange.Range is [3, 3]
// inserted is [1, 3, 3, 5, 7, 9]
```

## Validation Requirements For The Implementation

The implementation should include tests for at least these contracts:

- Structural persistence: every operation leaves its input sequences unchanged.
- Endpoint operation equivalence against `List<T>` or `ImmutableList<T>` reference behavior.
- Concatenation associativity at the sequence level.
- `SplitAt`, `SplitItemAt`, and `SplitRange` reconstruction laws.
- Indexed get, set, update, insert, and delete around every endpoint and around tree-level boundaries.
- Sorted lower bound, upper bound, binary search, equal range, insertion, and removal with duplicates.
- Custom comparer behavior, including reversed or projection comparers when the input is sorted accordingly.
- Null element handling for nullable reference types with a comparer that accepts nulls.
- Very small counts 0 through at least 12, because those exercise every empty/single/deep digit transition.
- Large generated sequences sufficient to force multiple nested middle-tree levels.
- Branching persistent histories, especially repeated endpoint insertions and removals from the same older version, before claiming persistent amortized O(1).

Property tests should generate random operation histories and compare the resulting sequences against a simple immutable reference model. Separate invariant tests should inspect internals through an `InternalsVisibleTo` test assembly and assert digit length, node length, count caches, and rightmost-element caches after every operation.

## Notes For Maintainers

The fully generic measured finger tree was initially deferred, but is now provided as the sibling type `FingerTree<TElement, TMeasure, TMeasureOps>` (see below). C# expresses the Haskell `Measured v a` typeclass through static-abstract interface members (`IMonoid<TMeasure>`, `IMeasure<TElement, TMeasure>`), so the general machinery is available without per-call allocation or virtual dispatch. The deque remains a separate, individually tuned type — the same split as Haskell keeps `Data.Sequence` distinct from `Data.FingerTree` — and is not re-platformed onto the general core in this revision.

The sorted-search API deliberately remains available on the deque as a local-invariant helper: callers may keep an ordinary sequence sorted for a short workflow without paying for a wrapper. Long-lived sorted invariants are owned by `SortedBag<T>`, `SortedSet<T>`, and `SortedDictionary<TKey, TValue>`, which store comparers, enforce their ordering contracts by construction, and expose rank, range, and neighbor operations over the measured-tree surface.

The implementation should keep XML documentation aligned with this file. XML summaries may use coarser O(log n) wording, but remarks on the key methods should point to this specification for sharper split-distance complexity.

## The General Measured Finger Tree

`FingerTree<TElement, TMeasure, TMeasureOps>` is the general Hinze–Paterson measured finger tree: a persistent sequence of `TElement` annotated by a monoidal `TMeasure`, where `TMeasureOps` supplies the measure algebra statically.

- **Measure interfaces.** `IMonoid<TMeasure>` provides `Empty` (identity) and `Combine` (associative); `IMeasure<TElement, TMeasure> : IMonoid<TMeasure>` adds `Measure(element)`. Both use static-abstract members, so the measure type argument is monomorphized with no allocation or virtual dispatch. Implementations must obey the monoid laws (two-sided identity, associativity); the tree caches combined measures and relies on them.
- **Ready-made measures.** The library ships the common measures so callers need not hand-roll one: `SizeMeasure<T>` (element count → positional sequence and order statistics); `MaxMeasure<T>` / `MinMeasure<T>` (extremum by `Comparer<T>.Default`, carried as `Optional<T>` so the empty tree has a monoid identity → mergeable priority queues); `KeyMeasure<T>` (right-biased last element → ordered lower/upper-bound search on a sorted sequence); and `OrderStatisticMeasure<T>` (the `RankedKey<T>` product of count and last key → a tree that is positional *and* ordered at once). The non-parameterized forms use `Comparer<T>.Default`. The natural operations are surfaced as extension methods in `FingerTreeMeasureExtensions` — `TryPeekMax`/`TryExtractMax`, `TryPeekMin`/`TryExtractMin`, `SplitByLowerBound`/`SplitByUpperBound`, and `SplitAtIndex` — so priority-queue, ordered-set, and order-statistic intent reads directly instead of as a hand-built split predicate.
- **Custom orders.** A custom order needs only a one-method static-abstract `IComparison<T>` (with `DefaultComparison<T>` and `ReverseComparison<T, TComparison>` provided), not a full hand-rolled measure. `MaxMeasure<T, TComparison>` / `MinMeasure<T, TComparison>` bake the order into the extremum measure; `KeyMeasure<T>` and `OrderStatisticMeasure<T>` keep a comparison-free (right-biased) `Combine` and instead accept the order at query time through the `SplitByLowerBound<T, TComparison>` / `SplitByUpperBound<T, TComparison>` overloads, so the same key-measured tree can be searched under any order it happens to be sorted by. `MaxMeasure<T, DefaultComparison<T>>` is equivalent to `MaxMeasure<T>`.
- **Product (composed) measure.** `ProductMeasure<TElement, TFirst, TSecond, TFirstOps, TSecondOps>` combines two independent measures into one, carried by `MeasurePair<TFirst, TSecond>`. The product of two monoids is a monoid — identity is the pair of identities and `Combine` acts component-wise — so the laws are inherited from the factors and a single tree can be split on either axis. `FingerTreeProductExtensions` supplies the component-projecting splits `SplitByFirst`/`SplitBySecond` and `TrySplitFindByFirst`/`TrySplitFindBySecond` (all five type arguments inferred from the tree argument), plus named operations for the headline pairings: size+sum is positional *and* Fenwick at once (`SplitAtIndex`, `SplitByCumulativeWeight`, and `TrySelectByCumulativeWeight` which — unlike the pure `SumMeasure` selection — reports the selected element's index), and size+max / size+min are priority queues with positional access (`TryPeekMax`/`Min`, `TryExtractMax`/`Min`). The `ProductMeasures` factory builds the headline trees without naming the five type arguments; three or more components nest the pair (`MeasurePair<int, MeasurePair<T, Optional<T>>>`). `OrderStatisticMeasure<T>` is this construction specialized to size + last-key, kept as a dedicated type with the friendlier `RankedKey<T>` carrier.
- **Cumulative-weight (sum) measure.** `SumMeasure<T>` measures each element as itself and combines by addition (identity `T.AdditiveIdentity`), so a tree's measure is the running total of its elements, over any generic-math numeric `T` (`IAdditionOperators` + `IAdditiveIdentity` — `int`, `long`, `double`, `decimal`, `BigInteger`, …). This turns the measured tree into a persistent Fenwick-style structure: O(1) total, and `FingerTreeSumExtensions` adds `SplitByCumulativeWeight` (split at the first point where the running total exceeds a threshold — O(log n)) and `TrySelectByCumulativeWeight` (locate the element whose cumulative-weight interval contains a threshold, reporting the weight accumulated before it — the basis of O(log n) weighted random sampling: draw the threshold uniformly in `[0, total)` and each element is selected in proportion to its weight). Floating-point addition is not exactly associative, so with `double`/`float` weights the split boundaries inherit the usual rounding; the measure carries no count, so positional ("first k") queries need a separate size component.
- **Interval tree.** `IntervalTree<T>` is a full Hinze–Paterson interval tree built on the general core with `IntervalMeasure<T>` (the `IntervalAnnotation<T>` measure carries count, the right-biased last low endpoint, and the maximum high endpoint). It stores closed `Interval<T>` values in low-endpoint order and supports O(log n) `Insert`, O(log n) `TryFindOverlap`/`TryFindContaining` (point stabbing), O(k log n) `FindOverlaps`/`CountOverlaps`, value-equality `Contains`/`TryRemove`/`Remove` (O(log n) when low endpoints are distinct), and an O(n) `Coalesce` that merges overlapping intervals into maximal disjoint ones. The maximum-high signpost is what makes a single overlap query logarithmic: the first interval whose accumulated maximum high reaches the query's low is the candidate, and comparing its low against the query's high decides the overlap.
- **Representation.** The canonical general structure: one-through-four element digits, two-or-three child nodes, polymorphic recursion (`Deep` middles store nodes). Each `Deep` node holds its middle subtree behind a memoized suspension and computes its combined measure lazily (memoized on first read), the strict-language realization of Hinze and Paterson's lazy finger tree.
- **Operations.** `Empty`, `IsEmpty`, `Measure`, `First`, `Last`, `Prepend`, `Append`, `Concat`, `TryViewLeft`, `TryViewRight`, `Create`/`CreateRange`, `ToArray`, enumeration, the headline `Split(predicate)` / `TrySplitFind(predicate, …)` — splitting at the element where a monotone predicate over the accumulated measure first becomes true — and the read-only `TryLocate(predicate, out measureBefore, out found)`, which finds the same boundary element and the measure preceding it *without* reconstructing the surrounding subtrees. `TryLocate` is the allocation-free counterpart to `TrySplitFind` for membership, rank, order-statistic, and neighbor queries. It comes in two public forms: a delegate form, and a generic `TryLocate<TPredicate>` taking a constrained value-type predicate (`where TPredicate : struct, IMeasurePredicate<TMeasure>`) that the runtime monomorphizes and devirtualizes so the descent allocates *nothing*. `IMeasurePredicate<TMeasure>` is a public strategy interface (alongside `IMonoid`/`IMeasure`/`IComparison`), so callers can write their own zero-allocation membership, rank, order-statistic, or neighbor queries over a raw tree. `Split` and `TrySplitFind` accept the same struct predicates through generic overloads, so even the split paths carry no predicate closure (the result-tree allocation a split builds is inherent). The sorted multiset/set/dictionary, the priority-queue peek/dequeue, the interval-tree queries, and every collection mutation route through library-internal struct predicates (`KeyAtLeastPredicate<T>`, `CountAbovePredicate<T>`, etc.): reads drop from the ~1–2 KB of a tree-building split to zero, and mutations shed their predicate closure while keeping only the unavoidable result-tree allocation. Choosing the measure specializes the structure: element count → positional sequence and order statistics; maximum priority → mergeable priority queue; maximum key → ordered search tree; running sum → cumulative-weight selection / Fenwick structure; product measures combine these.
- **Complexity.** With O(1) measure operations: `IsEmpty`/`First`/`Last` are O(1) worst-case (they read the prefix/suffix digits and never force the spine); `Measure` is O(1) amortized, with the first read of a fresh deep node forcing a chain of suspensions O(log n) deep before memoizing; endpoint and view operations are O(1) amortized and O(log n) worst-case; `Concat` is O(log(min(n, m))) amortized; `Split`/`TrySplitFind` are O(log(min(k, n − k))) amortized for a boundary at distance k from the nearer end. Because the spine is a memoized lazy suspension, these amortized bounds hold under fully persistent (branching) histories, not merely ephemeral linear use.
- **Sorted multiset.** `SortedBag<T>` is an immutable sorted multiset built on `OrderStatisticMeasure<T>`. Because that measure's combine is order-independent, the bag stores a runtime `IComparer<T>` and applies it in the split predicates, so it accepts an arbitrary comparer (not just `Comparer<T>.Default`) while keeping O(1) `Count`/`Min`/`Max`, O(log n) `Add`/`Remove`/`RemoveAll`/`Contains`/`CountOf`, rank queries (`CountLessThan`/`CountAtMost`), order-statistic indexing (`this[rank]`), and range extraction (`GetRange`). It enforces the sorted invariant by construction, so its queries are always well-defined.
- **Sorted set.** `SortedSet<T>` is the uniqueness-enforcing sibling of `SortedBag<T>` on the same order-statistic measure and runtime comparer. Beyond the bag's queries it adds navigable-set neighbor lookups (`TryFloor`/`TryCeiling`/`TryLower`/`TryHigher`), `IndexOf`, and the set algebra — `Union`/`Intersect`/`Except`/`SymmetricExcept` plus the relations `IsSubsetOf`/`IsSupersetOf`/`IsProperSubsetOf`/`IsProperSupersetOf`/`Overlaps`/`SetEquals` — implemented as O(n + m) sorted-merge sweeps that assume both operands share the order. `CreateBuilder(comparer)` and `ToBuilder()` expose a nested mutable `Builder` for large batched edits: `Add`, `Remove`, `UnionWith`, `ExceptWith`, `Clear`, and `ToImmutable`. Dirty freezes rebuild from comparer-ordered staging in O(n); repeated clean freezes return the same immutable reference.
- **Sorted dictionary.** `SortedDictionary<TKey, TValue>` is a key-ordered map (an `IReadOnlyDictionary<TKey, TValue>`) on `EntryMeasure<TKey, TValue>`, which projects each entry's key into the order-statistic measure. It mirrors `SortedSet<T>` over keys: O(log n) `SetItem`/`Add`/`TryAdd`/`Remove`/`ContainsKey`/`TryGetValue`/`IndexOfKey`, order-statistic `EntryAt`, navigable-map neighbors (`TryFloorEntry`/`TryCeilingEntry`/`TryLowerEntry`/`TryHigherEntry`), `GetRange`, and O(1) `Count`/`MinEntry`/`MaxEntry`, with a runtime key comparer and last-wins `CreateRange`. Its nested mutable `Builder` stages ordered entries with `Add`, `TryAdd`, `SetItem`, `AddRange`, `Remove`, `Clear`, and `ToImmutable`; dirty freezes are O(n), clean freezes are reference-cached, and `SetItem` is an unconditional cache-invalidating write.
- **Meldable priority queue.** `PriorityQueue<TElement, TPriority>` is a persistent minimum-priority queue on `PriorityMeasure<TElement, TPriority>` (a count plus minimum-priority product measure). Entries are stored in insertion order and the front is found through the measure's minimum, so `Enqueue` is a cheap append (O(1) amortized), `TryPeekPriority` is O(1), `TryPeek`/`TryDequeue` are O(log n), and two queues `Meld` by concatenation in O(log(min(n, m))) — the finger-tree advantage over a heap. It is stable (FIFO among equal least priorities) and orders by `Comparer<TPriority>.Default` (reverse the priority for a maximum-priority queue); because a min measure's `Combine` depends on the order, this is the one collection that cannot take a runtime comparer.
- **Persistence (lazy-memoized spine).** Like `FingerTreeDeque<T>`, this type holds the middle subtree of each deep node behind a memoized suspension, so deferred spine repairs are paid once and shared among every version holding the same suspension — the amortized bounds therefore hold under fully persistent (branching) histories. The deque additionally keeps `Measure` O(1) worst-case because its size measure is a *group*: it recovers a popped subtree's size by subtraction without forcing. A general monoid has no inverse, so this type cannot subtract; instead it memoizes each deep node's measure *lazily* (Hinze and Paterson's approach). The consequence is the one bound that differs from the deque: `Measure` is O(1) amortized rather than O(1) worst-case, since the first read of a fresh node may force an O(log n) chain of suspended measures before memoizing. `First`/`Last` still never force and remain O(1) worst-case. The pre-force-the-source discipline (a new suspension always captures an already-forced source) keeps every suspension one operation deep, bounding the forcing cascade by the tree height. Suspensions publish their forced result by compare-exchange, so concurrent reads are safe and converge.

## The Reversible Deque

`ReversibleDeque<T>` is a sibling of `FingerTreeDeque<T>` that adds O(1) `Reverse` while preserving every deque bound. It is a deliberately *separate* type rather than a change to the tuned deque, so the constant-factor cost of reversibility falls only on callers who need it.

- **Representation.** A size-measured simplified finger tree (one-through-three digits, two-or-three nodes), strict. Every node and deep level carries a **reversal bit** and exposes an O(1) `Mirror` (a reversed view that flips the bit without copying the subtree). All operations are written against *logical*, orientation-aware accessors that interpret the bit, and rebuild results in forward orientation.
- **Reverse.** `Reverse()` mirrors the root — O(1) — and leaves the original unchanged. Double reverse is the identity.
- **Bounds preserved.** O(1) endpoint reads; O(log n) worst / O(1) amortized (linear-use) endpoint insert/remove; indexing, `SetItem`, `InsertAt`, `RemoveAt`, and `SplitAt` logarithmic in the distance from the nearer end; `Concat` O(log(min(n, m))). Crucially, **concatenation stays O(log(min)) even across mixed orientations** (e.g. `a.Reverse().Concat(b)`): `glue` reads each operand through its logical accessors, so a reversed operand never needs reifying. This is the "full-bounds" property that distinguishes this type from a cheaper root-flag design whose mixed-orientation concat would degrade to O(n).
- **Cost.** Relative to `FingerTreeDeque<T>`: a reversal-bit branch on every internal access, and small orientation-adjusted digit-array copies along reversed paths. Forward-only usage takes the fast path. Choose `FingerTreeDeque<T>` unless O(1) reverse is required.

## The Rope

`Rope<T>` is a general-purpose persistent **chunked** sequence — a rope — built on the measured finger tree. It is element-agnostic: `Rope<char>` is a text buffer, `Rope<byte>` a binary buffer, `Rope<T>` a large editable list. It is the canonical application of the measured finger tree.

- **Representation.** A `FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>` whose leaves are bounded array chunks (`Chunk<T>` wraps rope-owned `ReadOnlyMemory<T>` plus a cached length) and whose measure is the total element count (the monoidal sum of chunk lengths). Chunking gives cache-friendly storage and amortizes the per-element tree overhead; the tree's depth is O(log(n / chunk-size)).
- **Addressing.** Element position maps to the measure: the chunk containing index `i` is the first whose cumulative length exceeds `i`, found by the monotone split/locate. Reads (`this[index]`, `First`, `Last`) and splits use the non-capturing `ElementIndexPredicate` through the zero-allocation `TryLocate`/`Split<TPredicate>`; the within-chunk offset is `index − (elements left of the chunk)`.
- **Chunk policy.** Chunks never exceed a maximum size (so within-chunk work is a bounded constant); an edit that grows a chunk past the maximum splits it, and one that shrinks a chunk below the minimum coalesces it with a neighbour when they fit, keeping the chunk count ≈ n / target and the tree O(log n). Bulk construction targets maximum-size chunks.
- **Operations.** O(1) `Count`/`IsEmpty`/`First`/`Last`; O(log n) `this[index]`/`SetItem`/`Insert`/`RemoveAt`/`Split`/`Slice` (plus a bounded O(chunk-size) array copy at the edited chunk); O(log(min(n, m))) `Concat`; O(n)/O(m) bulk `Create`/`CreateRange`/`FromChunks`/`InsertRange`/`RemoveRange`/`ToArray`/`CopyTo`. `FromChunks` copies caller-provided memory into rope-owned chunks so immutable snapshots cannot be changed through mutable source arrays. `AddFirst`/`AddLast` copy the boundary chunk (an O(chunk-size) constant), so bulk construction or the append-only `Rope<T>.Builder` is preferred for building. The builder has O(1) `ToBuilder()` prefix adoption, amortized-O(1) `Add`, O(m) `AddRange`, O(k + log n) dirty `ToImmutable()` for k newly staged elements, and O(1) clean repeated `ToImmutable()`. Implements `IReadOnlyList<T>` with a chunk-by-chunk enumerator.
- **Persistence.** Immutable and structurally shared: every edit returns a new rope cheaply, old versions stay valid, and slices share backing arrays (an explicit `Compact()` rebuilds into fresh chunks when a slice-heavy history retains large buffers). This is what makes a `Rope<char>` a natural editor buffer with free snapshots and undo.
- **Generality.** This rope is positional (general over the element type). For an arbitrary monoidal secondary measure — line counts for text, byte weights, custom navigation — use `MeasuredRope<T, TMeasure, TMeasureOps>` (below), the measured sibling.

## The Measured Rope

`MeasuredRope<T, TMeasure, TMeasureOps>` is the measured sibling of `Rope<T>`: a persistent chunked sequence that also tracks an arbitrary monoidal user measure, so it navigates by that measure as well as by position. Its headline application is a text buffer with a line measure, giving O(log n) offset↔line navigation; the same machinery serves weighted selection and byte-offset addressing over variable-width elements.

- **Representation.** A `FingerTree<MeasuredChunk<…>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<…>>`: each chunk caches both its length and the combined user measure of its elements (`MeasuredChunk`), and the tree measure is the product of total count and total user measure. Positional operations split on the count component (the zero-allocation `PairCountAbovePredicate`); measure operations on the user component.
- **Positional operations.** The same surface and bounds as `Rope<T>` (O(1) ends, O(log n) indexed edit/split/slice, O(log(min)) concat), with the one addition that an edit that rebuilds a chunk recomputes that chunk's cached measure (an O(chunk-size) bounded cost), and a chunk merge combines measures monoidally without rescanning.
- **Measure navigation.** `Measure` is the whole-sequence user measure (O(1)). `PrefixMeasure(count)` is the user measure of the first `count` elements (O(log n)). `SplitByMeasure(predicate)` splits at the element where a monotone predicate over the accumulated user measure first becomes true, and `TryLocateByMeasure(predicate, …)` locates that element with its index and the measure before it — both O(log n), descending to the boundary chunk on the tree and then scanning within it (a bounded cost). For a newline measure these give "line index at an offset" (`PrefixMeasure`) and "offset of the start of a line" (`TryLocateByMeasure`).
- **Builder.** `MeasuredRope<T, TMeasure, TMeasureOps>.Builder` is the measured analogue of `Rope<T>.Builder`: append-only, frozen-prefix based, and clean-freeze cached. It additionally exposes a live O(1) `Measure` property seeded from `TMeasureOps.Empty` and updated as elements are appended.
- **Choice.** Use `Rope<T>` when no secondary measure is needed; `MeasuredRope<T, TMeasure, TMeasureOps>` when measure-based navigation is required.
- **Text conveniences.** `RopeText` is a companion extension layer (so the rope cores stay element-agnostic) over `Rope<char>` and the line-aware `MeasuredRope<char, int, NewlineMeasure>`: the ready-made `NewlineMeasure`, string interop (`ToCharRope`/`ToTextRope`/`AsString`), zero-based O(log n) line/column navigation (`LineCount`, `LineOfOffset`, `LineStartOffset`, `LineColumnOf`, `OffsetOf`, `GetLine`, `Lines`), and a forward-only `TextReader` adapter (`AsTextReader`). Line numbering follows the editor convention that line count is newline count plus one (an empty buffer is one empty line; a trailing newline yields a trailing empty line).

## Relaxed Radix-Balanced Vector

`RrbVector<T>` is an immutable `IReadOnlyList<T>` with 32-element leaf arrays and 32-way internal
branches. Every branch stores cumulative child sizes, including regular branches, so indexing uses
the same code before and after concatenation relaxes the shape.

- `CreateRange` builds packed leaves and bottom-up 32-way levels in O(n).
- Indexed get and `SetItem` are O(log32 n); equal-value replacement returns the current instance.
- `AddFirst`/`AddLast` are boundary concatenations. `Concat` recursively merges the right and left
  boundary spines, coalesces leaf payloads, and partitions at most 64 children into balanced nodes,
  taking O(log32(n + m)) time and storage.
- `SplitAt` copies one path and returns structurally shared prefix/suffix vectors. `InsertRange` and
  `RemoveRange` compose split and concat with O(log n + inserted-elements) / O(log n) structure work.
- Enumeration is O(n) and currently allocates an iterator and explicit traversal stack.

All produced nodes are immutable. Old vectors remain valid, empty-side concat and boundary splits
preserve instance identity, and no caller-owned mutable arrays are retained.

## DABA Lite Sliding-Window Aggregator

`DabaLite<T, TMonoid>` is a mutable FIFO sliding-window aggregator over the existing static
`IMonoid<T>` vocabulary. `Insert` appends, `Evict`/`TryEvict` remove the oldest value, and `Aggregate`
returns the in-order aggregate; the monoid need be associative but need not be commutative or
invertible.

The implementation follows Tangwongsan, Hirzel, and Schneider's DABA Lite pointer/fixup algorithm.
Six cursors partition a chunked queue into front, left, right, accumulator, and back regions; every
insert or eviction performs exactly one singleton/flip/shift/shrink fixup. Consequently, there are
no loops or recursive reversal steps in a window operation: insert invokes `Combine` at most three
times, eviction at most two times, and query exactly once. The linked fixed-size chunk queue grows,
trims, and moves cursors in worst-case O(1), avoiding a ring-buffer resize spike.

The type is deliberately mutable and not safe for unsynchronized concurrent writers. `Clear` is
O(n) because it performs the ordinary bounded eviction operation for every item. Space is O(n),
with one stored value/partial aggregate per window entry, two aggregate fields, and bounded chunk
slack.

## Canonical Zip-Zip Sorted Set

`CanonicalSortedSet<T>` is an immutable `IReadOnlySet<T>` whose binary-search-tree priority is a
keyed content-derived zip-zip rank: a geometrically distributed primary rank, a uniformly mixed
secondary rank, and a comparer tie-break. Under one retained `ZipTreeRankPolicy<T>`, equal contents
therefore produce the same shape independently of update history.

`ZipTreeRankPolicy<T>` retains the `IComparer<T>`, rank-hash function, and seed. The rank hash must
be constant on comparer-equivalence classes. The default policy is process-local and mixes the
default equality hash with a cryptographically generated seed; callers needing repeatability across
processes must supply both a pinned seed and a deterministic equivalence-class hash. Set algebra
requires policy object identity so incompatible rank spaces cannot be silently mixed.

Lookup, add, and remove are expected O(log n) and path-copy the search/unzip/zip path; adversarial or
constant rank hashes can produce O(n) height, which diagnostics and tests expose honestly. Duplicate
adds and absent removes preserve instance identity and the first stored representative. Enumeration
is sorted. `Union`, `Intersect`, and `Except` currently compose public updates and cost O(m log n).

Each node memoizes a non-cryptographic 64-bit subtree digest with compare-and-swap publication.
`ContentHash` is O(n) on first access and O(1) afterward. Digest inequality proves content inequality;
equal digests still require `SetEquals`, which traverses canonical shapes in lockstep and prunes
reference-equal nodes.

## Brodal–Okasaki Heap

`BrodalOkasakiHeap<T>` is an immutable min-heap retaining an `IComparer<T>`. It stores one global
minimum above a skew-binomial forest whose elements are themselves bootstrapped heaps. `Insert`
and `Meld` inspect and link only the first two forest ranks, giving O(1) worst-case time and a bounded
number of comparisons; `Minimum` is O(1), and `DeleteMinimum` normalizes and melds O(log n) ranked
trees in O(log n) worst-case time.

`Meld` requires comparer object identity, returns either operand for an empty-side meld, and otherwise
shares every untouched ranked tree. `TryGetMinimum` and `TryDeleteMinimum` provide nonthrowing empty
handling; throwing counterparts use `InvalidOperationException`. Enumeration visits structural
heap order and is explicitly not sorted; repeatedly deleting the minimum produces sorted order.
Equal-priority tie order is unspecified.

Strict Fibonacci and hollow heaps remain intentionally absent. Their optimal decrease-key machinery
depends on mutable pointer surgery, so path-copying would not preserve the bounds that distinguish
them; this is a recorded rejection, not an implementation gap.

## Priority Search Queue

`PrioritySearchQueue<TKey, TPriority, TValue>` stores at most one entry per key in an immutable AVL
tree ordered by `IComparer<TKey>`. Every node caches the minimum-priority entry in its subtree under
the retained `IComparer<TPriority>`; equal-priority winners use key order as a deterministic tie-break.

`TryGetEntry`, `SetItem`, `TryAdd`, `Remove`, and `TryRemove` are O(log n) worst-case. Equivalent-key
replacement retains the original key representative, and equal entry updates/absent removals preserve
instance identity. `Minimum`/`TryGetMinimum` are O(1); `DeleteMinimum` is O(log n). Enumeration is in
key order. Policies flow into every version; there is no operation that silently changes them.

`EnumerateAtMost(minimumKey, maximumKey, maximumPriority)` returns entries in the inclusive key range
whose priority is no greater than the threshold. Traversal prunes outside BST key bounds and any
subtree whose cached winner exceeds the threshold, costing O(log n + v) where v is the number of
nodes that cannot be pruned (including the k reported entries). This query is the core's differentiator
from a HAMT-plus-sorted-set composition.
