# C# FingerTree API Specification

- Status: Current normative API specification
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers, reviewers, and implementers of the C# FingerTree workspace
- Scope: Public API shape, semantic contracts, complexity targets, and collection roles for the C# FingerTree library
- Related code:
  - `src/CSharp/src/Durable7.FingerTree/`
- Related first-use guide:
  - [C# FingerTree usage guide](usage.md)
- Related range-action contract:
  - [Range-update sequence contract](range-update-sequence.md)

## Summary

This document specifies the public API contract for the C# FingerTree workspace. It opens with the tuned `FingerTreeDeque<T>` contract, then records the sibling measured tree, sorted collections, priority queue, interval tree, reversible deque, rope, and range-update sequence contracts in the same library. For first-use examples and facade selection, start with the [usage guide](usage.md).

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
- The implicit-AVL `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>`, including its static
  action algebra, lazy-tag composition order, range operations, and structural bounds.
- `PersistentChunkedBitSet`, including sparse nonnegative-int membership, inclusive rank,
  zero-based select, chunk contraction, and set algebra. Its detailed normative surface is the
  [persistent chunked bit set contract](persistent-chunked-bit-set.md).
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

The implementation lives under `src/CSharp/src/Durable7.FingerTree/` with this public namespace and primary type:

```csharp
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

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
namespace Durable7.FingerTree;

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
- **Persistent interval map.** `PersistentIntervalMap<TEndpoint, TValue>` is the payload-bearing
  interval sibling. It validates `Low <= High` and stores one entry per endpoint-comparer-equivalent
  interval in lexicographic `(Low, High)` order; distinct overlapping intervals remain independent.
  `Add` is strict, `TryAdd` reports an existing key, and `SetItem` replaces only the payload while
  retaining the first interval representative. An update whose payload is equal under the retained
  `ValueComparer` returns the receiver. `ContainsKey`, the indexer, `TryGetValue`, `TryGetKey`,
  insertion, replacement, and removal are O(log n). `TryFindOverlap` and `TryFindContaining` are
  O(log n); `FindOverlaps` and `CountOverlaps` are O(k log n) for k hits. Enumeration is ascending
  lexicographic interval order. The internal measure combines count, the rightmost full interval
  key, and maximum high endpoint, so exact lookup does not scan same-low runs and overlap pruning
  needs no side index. Endpoint ordering is fixed to `Comparer<TEndpoint>.Default` because maximum-
  high combination occurs inside the static measure; value equality is independently configurable.
  There is intentionally no `Coalesce`: merging interval keys would require an application-specific
  rule for merging their payloads.
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

### Positional Edit Cursor

`Rope<T>.GetCursor(position)` creates a public readonly `RopeCursor<T>` whose position is a gap in
`0 .. Count`. At gap `p`, `[0, p)` lies before the gap and `[p, Count)` lies after it.

- **Gap operations.** `TryPeekPrevious` observes `p - 1`, and `TryPeekNext` observes `p`.
  `MovePrevious` and `MoveNext` move the gap by one. `Insert` and `InsertRange` place values at the gap
  and return the gap after them. `DeletePrevious` is backspace and returns `p - 1`; `DeleteNext`
  removes `p` and keeps the position; `ReplaceNext` replaces `p` and keeps the position. A missing
  neighbor makes a non-`Try` operation throw `InvalidOperationException`. Creation and `Seek` reject
  positions outside `0 .. Count`.
- **Persistence and identity.** The cursor is logically immutable. Navigation shares one logical
  version and its snapshot cache; an edit creates a new version while every retained cursor remains
  valid. `Seek(Position)` and empty `InsertRange` preserve the exact version and context state.
  `ReplaceNext` always creates a new edit version, even for an equal or identical value, and invokes no
  element-equality callback. The default struct value is invalid and every member throws
  `InvalidOperationException`; `Rope<T>.Empty.GetCursor()` is the initialized empty state.
- **Representation.** The cursor holds a bounded 16-element active window between left and right
  ordinary-chunk trees. Each side has at most one partial carry of 0 through 255 elements; overflow
  flushes a 256-element ordinary chunk. The cursor value holds immutable context references plus a
  `CursorVersionState<T>`; copying the struct never aliases mutable edit state.
- **Snapshot.** A dirty `Snapshot()` packs the two bounded carries and focus and joins them to the
  retained trees in O(log n). Each logical edit version owns a thread-safe memo cell. Concurrent first
  callers build candidates, publish with compare/exchange, and all return the winner; a thrown candidate
  publishes nothing. Repeated snapshots, including snapshots through different navigation contexts over
  that version, are O(1) and reference-identical. A cursor created from a rope starts with that rope as
  its clean cached snapshot.
- **Complexity scope.** Creation and arbitrary `Seek` are O(log n). Peeks, focus-local movement, and
  single-element edits are O(1) amortized along a linear lineage and O(log n) worst-case when a boundary
  repair forces a suspended spine. `InsertRange` of m elements is O(m + log n) amortized. Dirty snapshot
  is bounded packing plus an O(log n) join. The C0 evidence does not prove constant-amortized work over
  arbitrary version-DAG fan-out: editing b independently retained boundary descendants has the published
  O(b log n) bound.
- **Failures and concurrency.** Insert count overflow is checked before edit allocation. Initialized
  cursors are immutable and safe for concurrent reads. Snapshot memoization is the only internal
  publication and does not change the cursor's logical sequence or position.

The [C0 decision record](rope-cursor-c0-decision.md) owns the representation selection, measured gate,
and proof boundary; the [validation guide](validation.md#test-coverage) maps the public command model,
identity/sharing, boundary, failure, and concurrency evidence. This section describes the positional
cursor; the measured extension is specified below.

## The Measured Rope

`MeasuredRope<T, TMeasure, TMeasureOps>` is the measured sibling of `Rope<T>`: a persistent chunked sequence that also tracks an arbitrary monoidal user measure, so it navigates by that measure as well as by position. Its headline application is a text buffer with a line measure, giving O(log n) offset↔line navigation; the same machinery serves weighted selection and byte-offset addressing over variable-width elements.

- **Representation.** A `FingerTree<MeasuredChunk<…>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<…>>`: each chunk caches both its length and the combined user measure of its elements (`MeasuredChunk`), and the tree measure is the product of total count and total user measure. Positional operations split on the count component (the zero-allocation `PairCountAbovePredicate`); measure operations on the user component.
- **Positional operations.** The same surface and bounds as `Rope<T>` (O(1) ends, O(log n) indexed edit/split/slice, O(log(min)) concat), with the one addition that an edit that rebuilds a chunk recomputes that chunk's cached measure (an O(chunk-size) bounded cost), and a chunk merge combines measures monoidally without rescanning.
- **Measure navigation.** `Measure` is the whole-sequence user measure (O(1)). `PrefixMeasure(count)` is the user measure of the first `count` elements (O(log n)). `SplitByMeasure(predicate)` splits at the element where a monotone predicate over the accumulated user measure first becomes true, and `TryLocateByMeasure(predicate, …)` locates that element with its index and the measure before it — both O(log n), descending to the boundary chunk on the tree and then scanning within it (a bounded cost). For a newline measure these give "line index at an offset" (`PrefixMeasure`) and "offset of the start of a line" (`TryLocateByMeasure`).
- **Builder.** `MeasuredRope<T, TMeasure, TMeasureOps>.Builder` is the measured analogue of `Rope<T>.Builder`: append-only, frozen-prefix based, and clean-freeze cached. It additionally exposes a live O(1) `Measure` property seeded from `TMeasureOps.Empty` and updated as elements are appended.
- **Choice.** Use `Rope<T>` when no secondary measure is needed; `MeasuredRope<T, TMeasure, TMeasureOps>` when measure-based navigation is required.
- **Text conveniences.** `RopeText` is a companion extension layer (so the rope cores stay element-agnostic) over `Rope<char>` and the line-aware `MeasuredRope<char, int, NewlineMeasure>`: the ready-made `NewlineMeasure`, string interop (`ToCharRope`/`ToTextRope`/`AsString`), zero-based O(log n) line/column navigation (`LineCount`, `LineOfOffset`, `LineStartOffset`, `LineColumnOf`, `OffsetOf`, `GetLine`, `Lines`), and a forward-only `TextReader` adapter (`AsTextReader`). Line numbering follows the editor convention that line count is newline count plus one (an empty buffer is one empty line; a trailing newline yields a trailing empty line).

### Measured Edit Cursor

`MeasuredRope<T, TMeasure, TMeasureOps>.GetCursor(position)` creates a public readonly
`MeasuredRopeCursor<T, TMeasure, TMeasureOps>`. It has the positional cursor's gap, movement, edit,
branching, identity, failure, snapshot, and linear-lineage complexity contracts, specialized to a
measured rope.

- **Ordered gap measures.** `MeasureBefore` is the aggregate of `[0, Position)` and `MeasureAfter`
  is the aggregate of `[Position, Count)`. `Combine(MeasureBefore, MeasureAfter)` therefore equals
  the version's total measure in source order. The contract assumes associativity only: it requires
  no inverse, commutativity, default identity value, or element equality.
- **Absolute measure seek.** `TrySeekByMeasure` searches the current logical version, and
  `MeasuredRope.TryGetCursorByMeasure` combines creation with that search. Delegate and constrained
  `IMeasurePredicate<TMeasure>` overloads select the gap immediately before the first element whose
  inclusive prefix makes a lawful monotone predicate true. A predicate true for the empty measure
  selects zero on a nonempty rope. A miss returns `false` and an end cursor whose `MeasureBefore` is
  the whole measure. Empty ropes also miss. Arbitrary positional `Seek` remains available.
- **Prepared fragments.** A measure seek on an existing lineage prepares at most the selected
  ordinary chunk and shares its element measures with descendant edit versions. Prefix and suffix
  tables are installed lazily and failure-atomically; later seeks through that fragment do no
  element remeasurement. A one-shot source factory deliberately does not retain a full
  element-measure array and defers focused cursor materialization until movement or editing requires it.
- **Snapshot and callbacks.** Clean snapshots return the exact source reference. Dirty first
  snapshots publish one winner-returning canonical measured rope and do not re-invoke the element
  `Measure` callback for already prepared buffers. A callback exception publishes neither partial
  fragment preparation nor a snapshot; racing preparation may duplicate bounded work but cannot
  expose partial arrays.
- **Text specialization.** With `MeasuredRope<char, int, NewlineMeasure>`, the cursor works directly
  with the existing UTF-16 text helpers. `RopeText.LineColumnOf(cursor)` obtains zero-based line and
  column coordinates. It reads the line from `MeasureBefore` without a descent, but resolves the
  column through `Snapshot().LineStartOffset(line)`, so a dirty cursor first normalizes its canonical
  snapshot exactly as the method's own remarks state.

The [C2 shipment decision](measured-rope-cursor-c2-decision.md) owns the exact 16/256 representation,
callback ceilings, source-versus-prepared split policy, benchmark thresholds, and validation
evidence. No bookmark or rebase cursor is implied by this surface.

The deque, RRB, reversible-deque, and raw-finger-tree cursors that later shipped under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md)
are **not** governed by this section. They are Profile R snapshot-plus-gap checkpoints delegating to
their collections' ordinary persistent operations, and they inherit none of the focused
representation, memo cell, callback ceiling, allocation bound, or amortized-locality claims recorded
here for the rope tier.

## The Persistent Cursor Tier

Beyond the two rope cursors, this workspace ships **thirteen** public cursor types under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md).
Every one is a `public readonly struct`, and every one is a **Profile R checkpoint**: the value is a
retained collection reference plus a validated position, rank, or split, and every edit delegates to
an ordinary published persistent operation. None of them claims the rope tier's focused
representation, memo cell, callback ceiling, allocation bound, or amortized locality.

`Rope<T>` and `MeasuredRope<T, …>` remain the only focused cursors. There is no `TextRopeCursor`
type: the text tier is extension methods over `MeasuredRopeCursor<char, int, NewlineMeasure>`.

### Shipped cursor types

| Group | Type | Axis |
| --- | --- | --- |
| sequence | `FingerTreeDequeCursor<T>` | positional gap in `0 .. Count` |
| sequence | `ReversibleDequeCursor<T>` | positional gap in logical orientation |
| sequence | `RrbVectorCursor<T>` | positional gap in `0 .. Count` |
| sequence | `RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps>` | positional gap plus measures and range tags |
| sequence | `FingerTreeCursor<TElement, TMeasure, TMeasureOps>` | measure-and-neighbor split, **no count or position** |
| sorted | `SortedBagCursor<T>` | comparator-order gap with duplicate occurrences |
| sorted | `SortedSetCursor<T>` | comparator-order gap, one representative per class |
| sorted | `SortedDictionaryCursor<TKey, TValue>` | key-order gap |
| sorted | `CanonicalSortedSetCursor<T>` | comparator-order gap over the zip-zip set |
| augmented | `PrioritySearchQueueCursor<TKey, TPriority, TValue>` | key order; priority is a query, not a second axis |
| augmented | `IntervalTreeCursor<T>` | nondecreasing low-endpoint order with duplicate occurrences |
| augmented | `PersistentIntervalMapCursor<TEndpoint, TValue>` | unique complete `(Low, High)` interval key order |
| augmented | `PersistentChunkedBitSetCursor` | population-rank gap over set bits; `long` count and position |

### Factories and the hit discriminator

`GetCursor(int position = 0)` is universal for the countable families — `long` on
`PersistentChunkedBitSetCursor` — and its default argument is the start gap; `GetCursor(Count)` is
the end gap. There is no `GetCursorAtStart` or `GetCursorAtRank`.

Ordered families add `GetCursorAtLowerBound(…)` and `GetCursorAtUpperBound(…)`. Exact search is
always a `bool`-returning `TryGetCursor(…, out … cursor)` overload, distinguished by argument type;
**no cursor type has a `Found` property and no factory returns a result struct**. A miss still
publishes a usable lower-bound cursor, so it is a location rather than an invalid value.

Family-specific factories worth naming exactly, because they are easy to guess wrong:

```csharp
// PrioritySearchQueue<TKey, TPriority, TValue>
public PrioritySearchQueueCursor<TKey, TPriority, TValue> GetCursorAtMinimumPriority();

// IntervalTree<T> and PersistentIntervalMap<TEndpoint, TValue>
public bool TryGetOverlapCursor(Interval<T> query, out IntervalTreeCursor<T> cursor);
public bool TryGetContainingCursor(T point, out IntervalTreeCursor<T> cursor);

// PersistentChunkedBitSet
public PersistentChunkedBitSetCursor GetCursorAtOrAfter(int bitIndex);

// FingerTree<TElement, TMeasure, TMeasureOps>
public FingerTreeCursor<TElement, TMeasure, TMeasureOps> GetCursorAtStart();
public FingerTreeCursor<TElement, TMeasure, TMeasureOps> GetCursorAtEnd();
public bool TryGetCursor<TPredicate>(TPredicate predicate, out FingerTreeCursor<…> cursor)
    where TPredicate : struct, IMeasurePredicate<TMeasure>;
```

`GetCursorAtMinimumPriority` reads the root's cached winner and then performs an ordinary key seek;
it does not walk a priority-ordered sequence, which does not exist.

### Navigation and edit vocabulary

The rank seek verb is **deliberately split** and must not be documented as one name: sequence cursors
use `Seek(int)`, sorted and augmented cursors use `SeekRank(int)` — `SeekRank(long)` on the bit set —
and the raw measured tree uses `SeekByMeasure<TPredicate>(TPredicate)`.

- **Sequence cursors** expose `Count`, `Position`, `IsAtStart`, `IsAtEnd`, `TryPeekPrevious`,
  `TryPeekNext`, `MovePrevious`, `MoveNext`, `Seek`, `Insert`, `DeletePrevious`, `DeleteNext`,
  `ReplaceNext`, and `Snapshot`. `FingerTreeDequeCursor<T>` and `RrbVectorCursor<T>` add
  `InsertRange(IEnumerable<T>)`; `RrbVectorCursor<T>` also overloads `InsertRange(RrbVector<T>)` for
  a structural-sharing splice through `SplitAt`/`Concat`. `ReversibleDequeCursor<T>` adds
  `Reverse()`, which maps gap `p` to `Count - p` and returns a cursor over the reversed logical
  version.
- **`RangeUpdateSequenceCursor`** adds the `MeasureBefore`/`MeasureAfter` properties and four range
  members: `MeasurePrevious(int count)`, `MeasureNext(int count)`, `ApplyPrevious(int count, TTag tag)`,
  and `ApplyNext(int count, TTag tag)`. Note the asymmetry — the whole-side measures are properties,
  the counted forms are methods.
- **`FingerTreeCursor`** has neither `Count` nor `Position`, by design: a maximum, interval, or
  arbitrary application monoid cannot be interpreted as an index, so the type refuses to fabricate
  one. Its state is `(snapshot, left, right)` — two whole subtrees — and its surface is `IsAtStart`,
  `IsAtEnd`, `MeasureBefore`, `MeasureAfter`, the two peeks, `MovePrevious`/`MoveNext`,
  `SeekByMeasure`, `Insert`, `DeletePrevious`, `DeleteNext`, `ReplaceNext`, and `Snapshot`.
  `SeekByMeasure` returns the cursor directly; the `bool` discriminator lives on the collection-level
  `TryGetCursor` factory.
- **Sorted cursors** expose the ordered-gap protocol plus family-appropriate edits. `SortedBagCursor`,
  `SortedSetCursor`, and `CanonicalSortedSetCursor` expose `Add(item)` and the two deletions and
  deliberately have **no `ReplaceNext`**: changing an occurrence can change its sort position, so the
  unambiguous operation is delete-plus-`Add`. `SortedDictionaryCursor` adds `Insert(key, value)`,
  `TryInsert(key, value, out …)`, `SetItem(key, value)`, and `SetNextValue(value)`.
- **Augmented cursors.** `PrioritySearchQueueCursor` exposes `Insert`, `TryInsert`, `SetItem`, and
  `SetNext(priority, value)`. The interval families expose `TrySeekNextOverlap(query, out cursor)` —
  the cursor-relative continuation, which searches strictly after the focused occurrence so a
  factory's gap-before-hit result cannot rediscover the same occurrence indefinitely.
  `PersistentIntervalMapCursor` adds `Insert`, `TryInsert`, and `SetNextValue`; endpoint replacement
  is not a local edit and is expressed as remove-plus-insert. `PersistentChunkedBitSetCursor` exposes
  `Add(int bitIndex)` and the two deletions, which clear the exact neighboring bit.

### Identity, defaults, and the error channel

`Snapshot()` returns the retained field with no reconstruction, so a cursor that has performed no
edit returns the **exact source instance**, and identity survives arbitrary navigation because
movement threads the same reference through. Several edit paths additionally preserve identity on a
semantic no-op by forwarding the collection's own `ReferenceEquals` result — this covers RRB
equal-value replacement, chunked-bit-set present-bit `Add`, and the range sequence's zero-length or
identity-tag `ApplyPrevious`/`ApplyNext`.

The default struct value of every cursor type is explicitly invalid, and **every** member throws
`InvalidOperationException` with a type-specific message. This is enforced structurally: no cursor
declares `Position` as an auto-property. Each routes through a private guarded accessor — named
`Value` in twelve types and `SnapshotValue` in `FingerTreeCursor` — so `Position`, the derived
`IsAtStart`/`IsAtEnd`, and the `Seek`/`SeekRank` identity shortcut all read the guard before
returning, and none can silently report gap zero on a `default` value. `FingerTreeCursor` has no
`Position` at all and guards transitively through its `Left`/`Right` accessors; its `SeekByMeasure`
has no identity shortcut. Inherited `ValueType` members are not overridden and do not throw.

Otherwise the channel is a clean split:

- **`ArgumentOutOfRangeException`** for a bad argument. Every cursor constructor validates its
  position, so every factory and every seek inherits the check; counted range arguments on the range
  sequence validate through a shared negative-and-overflow helper.
- **`InvalidOperationException`** for a boundary violation — moving past an end, deleting across an
  absent neighbor, `SetNextValue` with no next entry.
- **`ArgumentNullException`** from the `InsertRange` overloads.
- **`OverflowException`** at `int.MaxValue`, because every position advance is `checked`.

One exception to the split, worth calling out: `PrioritySearchQueueCursor.Insert` throws
**`ArgumentException`** on a duplicate key, not `InvalidOperationException`. It is the only cursor in
this workspace that throws `ArgumentException`.

### Honest complexity

These are checkpoints, and their costs are the owning collection's costs plus O(1) rank arithmetic.
The gap-cursor shape invites the opposite assumption, so the following are stated explicitly.

| Operation | Cost |
| --- | --- |
| creation at a position, `Seek`/`SeekRank`, `MovePrevious`/`MoveNext` | O(1) — allocates a struct and adjusts an integer; touches no tree |
| `Count`, `Position`, `IsAtStart`, `IsAtEnd`, clean `Snapshot()` | O(1) |
| peek on a positional or ordered cursor | O(log n) — a **full indexed descent from the root** |
| bound and exact seek factories | O(log n) |
| point edit | the delegated operation, normally O(log n) |

**Movement does not amortize a peek.** `MoveNext(); TryPeekNext();` re-descends from the root every
time, because the cursor retains no path. A complete traversal by move-plus-peek is therefore
**O(n log n)**, not O(n). Use the collection's own enumerator for whole-collection walks.

Family-specific costs that differ from the table:

- **`FingerTreeCursor`** is the one family that beats the positional cursors on locality, because it
  carries both subtrees rather than an index. `MeasureBefore` and `MeasureAfter` are **O(1)** cached
  root-measure reads; peeks are O(1); and `MoveNext`/`MovePrevious` move a leaf with its cached
  measure intact, so the user's `IMeasure` callback is **not** re-invoked by navigation. Edits are
  O(log n) through `Concat`.
- **`RangeUpdateSequenceCursor.MeasureBefore`/`MeasureAfter`** look like field reads at the call site
  but are each an O(log n) `MeasureRange` call, and are not cached. `ApplyPrevious`/`ApplyNext`
  delegate to `ApplyRange`, which stamps a single node-level tag, so a range apply is O(log n) rather
  than O(count); zero length and an identity tag both short-circuit to the receiver without callbacks.
  Measure reads thread inherited-tag state down the descent, so the tree is never eagerly pushed down
  to answer a query.
- **`ReversibleDequeCursor.InsertRange`** does not delegate to a bulk operation; it inserts one
  element at a time, costing O(k log n) and producing k intermediate versions. The deque and RRB
  cursors delegate to real bulk `InsertRange`.
- **`PersistentChunkedBitSetCursor`** peeks through `Select`, which is an O(log c) descent over `c`
  nonzero chunks **plus a bounded in-word loop of up to 63 clear-lowest-set-bit iterations**. It is
  O(log c) asymptotically, but the constant is real and every peek pays it. `Rank` is a clean
  O(log c) descent plus one population count.
- **The interval cursors perform a genuine augmented descent.** `TryGetOverlapCursor` and
  `TrySeekNextOverlap` locate through a `MaxHigh`-pruned structural descent with an allocation-free
  predicate, not a rank scan, and stop once low endpoints exceed the query high. The honest caveat is
  that each continuation step first splits off a suffix tree, so iterating all `k` overlaps is
  O(k log n) with O(log n) allocation per step rather than a stateful O(log n + k) walk.
- **`CanonicalSortedSetCursor`** derives bound ranks by an explicit zip-zip node descent over cached
  child counts, so its factories are O(h). As elsewhere in the canonical set, `h` is expected
  logarithmic under the documented coherent pseudorandom rank assumptions and is not unconditionally
  O(log n).

## Relaxed Radix-Balanced Vector

`RrbVector<T>` is an immutable `IReadOnlyList<T>` with 32-element leaf arrays and 32-way internal
branches. Packed branches omit cumulative sizes and select a child from five-bit radix digits;
only branches with irregular child spans retain a cumulative size table. Both representations are
validated by the same cached-count/height/layout invariants.

- `CreateRange` builds packed leaves and bottom-up 32-way levels in O(n). `CreateBuilder` and
  `ToBuilder` provide mutable append-only staging: an existing vector is adopted as an O(1) frozen
  prefix, full staged leaf arrays freeze without another element copy, partial leaves copy on
  freeze, and a clean repeated `ToImmutable` returns the same immutable instance. Joining a staged
  suffix to a nonempty prefix may redistribute the two boundary leaves; all other frozen leaves are
  shared directly.
- Indexed get and `SetItem` are O(log32 n); equal-value replacement returns the current instance.
- `AddFirst`/`AddLast` are boundary concatenations. `Concat` recursively merges the right and left
  boundary spines, coalesces leaf payloads, and partitions at most 64 children into balanced nodes,
  taking O(log32(n + m)) time and storage. This is boundary-only redistribution: it does not enforce
  a global minimum occupancy away from the seam, so density stress bounds are validation evidence
  for tested histories rather than a representation invariant.
- `SplitAt` copies one path and returns structurally shared prefix/suffix vectors. A split exactly on
  a leaf or subtree boundary reuses that leaf/subtree rather than slicing an equal replacement.
  `InsertRange` and `RemoveRange` compose split and concat with O(log n + inserted-elements) /
  O(log n) structure work.
- Enumeration is O(n) and currently allocates an iterator and explicit traversal stack.

All produced nodes are immutable. Old vectors remain valid, empty-side concat and boundary splits
preserve instance identity, and no caller-owned mutable arrays are retained. There is currently no
dedicated persistent tail buffer: repeated immutable `AddLast` remains a boundary-spine operation;
use the builder when append throughput is the dominant construction workload.

## Experimental Ancestral Slice Queue

`AncestralSliceQueue<T>` is an immutable `IReadOnlyList<T>` facade over a mutable, monotone
`IIncrementalAncestorArena<T>`. A handle stores an arena-owned tail node, the absolute depth of its
first visible value, and its count. Its denotation is the contiguous ancestor-depth interval from
that first depth through the tail. An empty result retains the node immediately before its window as
an anchor; appending to any empty slice consequently produces exactly the appended value.

The restricted public algebra is `AddLast`, endpoint reads and removals, indexed lookup, `Take`,
`Drop`, `Slice`, `SplitAt`, and enumeration. Every producing operation leaves all old handles valid,
and every result remains appendable. Prepend, point replacement, arbitrary middle edits, unrelated
concatenation, and cross-arena operations are deliberately unsupported.

Let `U(M)` be `AddLeaf` time and `Q(M)` be level-ancestor-query time after M nodes have been
published in one arena. Parent, depth, and value reads are O(1). These are sequential bounds and do
not include lock waiting or caller payload work. The facade has these bounds:

| Operation | Time |
| --- | --- |
| `AddLast` | `U(M)` |
| `Count`, `IsEmpty`, `Last`, `RemoveFirst`, `RemoveLast`, `Drop` | O(1) |
| `First`, indexer, `Take`, `Slice`, nontrivial `SplitAt` | `Q(M)` |
| `TryRemoveFirst` / `TryRemoveLast` | `Q(M)` / O(1) |
| enumerate n visible values | Theta(n) |

The shipped `MyersIncrementalAncestorArena<T>` uses one parent and one coalesced jump per immutable
node. Its leaf insertion does O(1) link work and is O(1) amortized including odd-block allocation;
ancestor queries are O(log M) worst case; and total manager storage is O(M). All its reads and writes
are serialized by one private lock. An Alstrup--Holm incremental level-ancestor backend supplies
`U(M) = Q(M) = O(1)` worst case with linear manager space, giving the facade O(1) worst-case scalar
operations, but no such backend is included in this prototype.

Space is charged to history, not a single handle: the arena retains every successful append and its
payload until the arena becomes unreachable. The complete invariant, preservation proof, allocator
arithmetic, prior-art audit, and precise shipped/theoretical distinction are normative in the
[research proposal](../../../../docs/proposals/ancestral-slice-queue-2026-07-25.md).

## DABA Lite Sliding-Window Aggregator

`DabaLite<T, TMonoid>` is a mutable FIFO sliding-window aggregator over the existing static
`IMonoid<T>` vocabulary. `Insert` appends, `Evict`/`TryEvict` remove the oldest value, and `Aggregate`
returns the in-order aggregate; the monoid need be associative but need not be commutative or
invertible.

The implementation follows the six-cursor DABA Lite algorithm introduced in Tangwongsan, Hirzel,
and Schneider's 2021 VLDB Journal article. The cursor order is always

```text
F <= L <= R <= A <= B <= E
```

over one logical queue. The half-open front and back regions are `[F, B)` and `[B, E)`; `[L, R)`,
`[R, A)`, and `[A, B)` are the left, right, and accumulator work regions inside the front. Two
additional values hold the `R`-through-`A` and back aggregates. After inserting at `E` or advancing
`F`, one bounded fixup executes these paper cases in order:

1. If `F == B`, it collapses `L`, `R`, `A`, and `B` to `E` and resets both aggregate fields.
2. Otherwise, if `L == B`, it starts the next flip by setting `L = F` and `A = B = E`, moving the
   old back aggregate into the `R`-through-`A` field, and resetting the back aggregate.
3. If `L == R`, it advances `L`, `R`, and `A` once and resets the `R`-through-`A` field.
4. Otherwise, it advances `L`, retreats `A`, and computes one new left and one new right partial
   aggregate.

There is no loop or recursive reversal in a window operation. `Insert`, `Evict`/`TryEvict`, and
`Aggregate` invoke `Combine` at most three, two, and one times respectively. A query of an empty
window returns `Empty` with zero `Combine` calls; a nonempty query combines the partial aggregate at
`F` with the back aggregate exactly once. These callback-count ceilings are worst-case constants
regardless of window size. The complete operations are worst-case O(1) only when both `Combine` and
`Empty` themselves take O(1) time.

### Mutation, Exceptions, And Concurrency

`Insert` throws `OverflowException` before changing a window whose `Count` is already
`int.MaxValue`. `Evict` throws `InvalidOperationException` on an empty window, while `TryEvict`
returns `false`. All mutating operations provide the strong guarantee for monoid callback failures:
if any `Combine` or `Empty` invocation throws, the published count, cursor state, aggregates, and
active chunk chain remain unchanged. Callback side effects outside this object are naturally not
rolled back.

`Clear` is an O(1) reset, not a sequence of evictions. An empty clear makes no callback. A nonempty
clear obtains `Empty` once, invokes `Combine` zero times, and then swaps in one new empty chunk; a
throwing `Empty` leaves the old window intact.

The class is deliberately mutable and provides no internal synchronization. Its contract does not
support overlapping access to one instance; callers must serialize operations or provide external
locking. Independently owned instances can of course be used independently.

### Representation And Lifetime

The logical algorithm stores the paper's `n + 2` values of type `T`: one partial aggregate per
window position and two aggregate fields. The C# queue is physically a doubly linked chain of
64-slot arrays, so a nonempty state with `n` logical positions has allocated queue capacity `n`
plus 1 through 127 slack slots. The lower slack comes from the exclusive end position and the upper
slack additionally includes retired positions before `F` in the current first block. An empty
aggregator owns one 64-slot block. The two aggregate fields, six cursors, and block links are
metadata in addition to those array slots.

Chunk growth and every cursor move are worst-case O(1); no resizing copies the window. After a
successful eviction, a reference-bearing retired slot is cleared promptly, and crossing a chunk
boundary detaches the predecessor in both directions. The queue's `Begin` is derived from its
current first block rather than retaining the block created with the aggregator, so a long-running
sliding window cannot retain an unbounded chain through a stale construction-time root. `Clear`
drops the entire previous chain in O(1).

DABA Lite queue slots are not stable raw-value storage: the incremental reversal overwrites values
with partial aggregates. The API consequently has no `Peek`, value-returning eviction, or
enumeration member. `Count`, emptiness, and the aggregate are the observable window state.

### Structural Validation

`ValidateStructure` invokes neither `Combine` nor `Empty`. It follows the active chunk chain and
checks predecessor/successor links, acyclicity, all six cursor indices and reachability, the cursor
ordering, `Count == E - F`, and the active block/slack calculation. For a nonempty window it also
checks the DABA equations

```text
|L,R| = |R,A|
|L,R| + |R,A| + |A,B| + 1 = |F,B| - |B,E|
|F,L| = |B,E| + 1
```

and for an empty window requires all cursors to coincide. The method takes O(c) time and O(c) space
for `c` active chunks, hence O(n) in the window size. It returns `DabaLiteStatistics` with these
fields: `Count`, `FrontLength`, `BackLength`, `LeftLength`, `RightLength`, `AccumulatorLength`,
`BlockCount`, `AllocatedSlotCapacity`, and `SlackSlotCount`.

Validation is intentionally structural only. Since arbitrary non-invertible monoids do not permit
the overwritten original values to be reconstructed, it cannot independently recompute the
aggregate or inspect an oldest raw value. Content correctness is validated against an external FIFO
model in the test suite.

## Canonical Zip-Zip Sorted Set

`CanonicalSortedSet<T>` is an immutable `IReadOnlySet<T>` implemented as a persistent Cartesian
binary-search tree. Comparer order is the search-tree order. A content-derived priority makes the
shape canonical within a retained `ZipTreeRankPolicy<T>` rather than within the type globally.

### Rank Derivation And Policy Modes

`ZipTreeRankPolicy<T>` retains an `IComparer<T>`, a `Func<T, ulong>` rank hash, and an owned HMAC key.
For an item, the implementation encodes the 64-bit rank-hash result in big-endian order and computes
HMAC-SHA256 under that key. The first three 64-bit big-endian words of the digest supply:

1. a geometric coordinate equal to the leading-zero count of the first word, from 0 through 64;
2. a fixed-width 64-bit secondary coordinate; and
3. a 64-bit content word used by the memoized subtree digest, not by heap ordering.

Nodes form a max-heap under geometric coordinate, then secondary coordinate. If that pair collides,
the comparer-smaller item wins. This final tie-break makes the priority order total and the shape
deterministic even under collisions, but a large collision class can become a linear chain.

Policy construction has three distinct reproducibility and trust modes:

- `ZipTreeRankPolicy<T>.Default` uses the default comparer and equality hash plus one 32-byte
  cryptographically random key generated once for the closed generic type. The key is not exposed;
  the policy and its canonical shapes are process-local.
- `ZipTreeRankPolicy<T>.Create(..., seed: null)` generates a fresh unexposed 32-byte random key on
  every call. `Create(..., seed: value)` deterministically derives a 32-byte HMAC key as SHA-256 of
  ASCII `ZZT2` followed by the seed's eight big-endian bytes. `Seed` exposes that public seed. It is
  a reproducibility salt, not a secret adversarial-security key.
- `ZipTreeRankPolicy<T>.CreateKeyed(rankKey, ...)` requires at least 32 bytes, copies the supplied
  key, and does not expose it. Callers that retain and protect the same key can reproduce ranks in
  another policy or process.

When no comparer is supplied, the fallback rank hash is the zero-extended 32-bit bit pattern of
`EqualityComparer<T>.Default.GetHashCode`. Supplying an explicit comparer requires an explicit rank
hash; the factory rejects omission. In every mode, the rank hash must be constant on the comparer's
equivalence classes. Incremental duplicate insertion and bulk duplicate elimination detect unequal
derived ranks for equivalent values and throw `InvalidOperationException`, but callers remain
responsible for the function's global coherence and stability.

With an unexposed or secret key, HMAC-SHA256 makes ranks for distinct inputs impractical to predict
under the usual HMAC pseudorandom-function assumption. It cannot restore entropy lost before the
HMAC: equal 64-bit rank hashes produce equal complete ranks, and the default fallback has only 32
bits of input. `CreateKeyed` therefore does not protect an incoherent or attacker-collidable rank
hash. A public seed is deterministic mixing rather than a secret PRF boundary: any caller can
calculate ranks and deliberately select an unfavorable set.

This is a practical zip-zip-inspired rank scheme, not the paper's exact metadata construction. The
secondary coordinate is always 64 bits rather than a range sized as a function of n. The
implementation therefore makes no claim to the paper's O(log log n)-bit rank-metadata theorem.

### Canonicality And Set Semantics

For stable comparer and rank-hash behavior, equal mathematical contents under one policy object
produce one shape regardless of insertion, removal, or bulk-construction history. Separate policy
objects also reproduce that shape when they use the same comparer semantics, equivalence-class rank
hash, and public seed or retained key. Policies with independent random keys generally produce
different shapes. Thus "history-independent" means policy-scoped; it does not mean that all
`CanonicalSortedSet<T>` instances share one representation.

`Create(policy)` creates an empty set retaining that exact policy. `CreateRange(items, policy)` keeps
the first item in each comparer-equivalence class. `Add` retains an existing representative and
returns the current instance for a duplicate. `Remove` returns the current instance when absent;
`Clear` does so when already empty. `TryGetValue` recovers the stored representative. Enumeration is
strictly in comparer order. `Policy`, `Count`, `IsEmpty`, and diagnostic `Height` are O(1) cached
properties.

The `IReadOnlySet<T>` relation members apply the receiver's comparer to arbitrary enumerables.
`SetEquals(CanonicalSortedSet<T>)` and the interface `SetEquals(IEnumerable<T>)` compare semantic set
contents even when policy objects differ. With different comparer semantics, the result is defined
by the receiver and need not be symmetric. By contrast, `Union`, `Intersect`, and `Except` accept
only another canonical set retaining the same policy object; they throw `ArgumentException` for a
distinct policy even when its seed and configuration are identical. This identity gate prevents
algebra from silently mixing rank spaces.

Each node lazily memoizes a non-cryptographic 64-bit `ContentHash` from its content word and child
digests, publishing it with compare-and-swap; the empty set reports zero. Under one coherent policy,
digest inequality is a safe semantic-inequality fast path. Digest equality is not proof of equality.
Across policy objects,
neither equality nor inequality of `ContentHash` has semantic meaning. Same-policy `SetEquals` uses
count and digest rejection before an iterative lockstep comparison that prunes reference-equal
subtrees; cross-policy equality falls back to comparer-based set semantics.

### Bulk Build And Validation

`CreateRange` materializes and comparer-sorts all input together with its original sequence index,
so equivalent values retain the first representative. After duplicate/rank-coherence checks, a
monotone-stack Cartesian builder constructs the unique priority tree in O(u) time for u unique
items, and a second explicit-stack pass freezes immutable nodes bottom-up. Including the mandatory
sort, construction is O(n log n) time and O(n) auxiliary storage; it does not become linear merely
because the input was already sorted.

`ValidateStructure` iteratively checks strict comparer order, rank reproducibility, heap order,
cached count and height, and root metadata. It returns `CanonicalSortedSetStatistics` containing
item count, height, largest geometric rank, and the number of repeated geometric/secondary pairs.
Validation costs O(n) expected time and O(n) auxiliary storage because it tracks priority pairs as
well as an explicit traversal stack.

### Complexity, Persistence, And Degeneracy

Let h be the current tree height, n the receiver size, and m the other input size.

- `Contains` and `TryGetValue` take O(h) time and O(1) auxiliary space.
- `Add` and `Remove` take O(h) time and allocate O(h) immutable path nodes plus O(h) temporary
  explicit-stack entries. Untouched subtrees remain shared. They do not copy O(1) nodes.
- Enumeration takes O(n) time and O(h) iterator-stack space.
- The first `ContentHash` access visits uncached descendants in O(n) time and O(h) stack space;
  later root accesses are O(1). Concurrent readers may duplicate benign work but publish one stable
  boxed digest per node.
- Same-policy equality is O(n + m) worst-case after digest computation, with shared subtrees pruned.
  Cross-policy equality materializes the other input in a BCL `SortedSet<T>`, costing O(m log m)
  plus O(mh) receiver membership work and O(m) auxiliary storage.
- Algebra composes public searches and updates rather than performing a fused linear Cartesian
  merge. Under expected logarithmic height, union and difference are O(m log(n + m)); intersection
  additionally probes n receiver items against the other tree and rebuilds retained items. A
  degenerate tree can make these compositions quadratic.

All search/update expected O(log n) bounds require distinct or sufficiently sparse rank-hash
collisions and HMAC outputs behaving like independent pseudorandom ranks for the actual key set.
They are not worst-case or adversarial bounds. Constant rank hashes demonstrably yield h = n.
Contains, updates, split/merge, bulk freeze, enumeration, digest computation, equality, and
validation all use explicit stacks, so even this case does not consume the managed call stack or
raise a recursion-depth failure. It still costs O(n) time and O(n) explicit-stack memory for a
height-n operation.

## Brodal–Okasaki Heap

`BrodalOkasakiHeap<T>` is an immutable min-heap retaining an `IComparer<T>`. It stores one global
minimum above a skew-binomial forest whose elements are themselves bootstrapped heaps. `Insert`
and `Meld` inspect and link only the first two forest ranks, giving O(1) worst-case time and a bounded
number of comparisons; `Minimum` is O(1), and `DeleteMinimum` normalizes and melds O(log n) ranked
trees in O(log n) worst-case time.

The implementation uses the paper's final fused representation. A ranked tree stores its primitive
skew-binomial children `c` immediately followed by the forest `f` of its embedded bootstrapped heap,
as one persistent list `c ++ f`. The rank determines where the structural prefix ends; the permitted
rank-zero ambiguity is resolved in favor of the structural prefix, exactly as in delete-min's split.
This saves an indirection but makes the rank-aware decomposition, rather than raw child-list shape,
the representation invariant.

`Meld` requires comparer object identity, returns either operand for an empty-side meld, and otherwise
shares every untouched ranked tree. `TryGetMinimum` and `TryDeleteMinimum` provide nonthrowing empty
handling; throwing counterparts use `InvalidOperationException`. Enumeration visits structural
heap order and is explicitly not sorted; repeatedly deleting the minimum produces sorted order.
It visits every logical element exactly once in O(n) time with an explicit stack. Equal-priority tie
order is unspecified. `CreateRange` performs one O(1) worst-case insertion per input and therefore
costs O(n) time.

`ValidateStructure` is an O(n) diagnostic traversal using O(n) worst-case explicit-stack storage. It
checks the global root, fused child/embedded-forest decomposition, skew-forest rank discipline, heap
order, and the logical count. It returns `BrodalOkasakiHeapStatistics` with the count, root-forest
length, maximum rank, and maximum traversal depth. Validation never changes the heap or forces a
normalization that an ordinary operation would not perform.

Strict Fibonacci and hollow heaps remain intentionally absent. Their optimal decrease-key machinery
depends on mutable pointer surgery, so path-copying would not preserve the bounds that distinguish
them; this is a recorded rejection, not an implementation gap.

## Priority Search Queue

`PrioritySearchQueue<TKey, TPriority, TValue>` stores at most one entry per key in an immutable AVL
tree ordered by `IComparer<TKey>`. Every node caches the minimum-priority entry in its subtree under
the retained `IComparer<TPriority>`; equal-priority winners use key order as a deterministic tie-break.
The cached full entry is one additional metadata field per node. This is a winner-cached AVL
implementation of the priority-search-queue abstraction, not Hinze's loser-tree priority-search
pennant, so pennant-specific representation and output bounds do not apply.

`TryGetEntry`, `SetItem`, `TryAdd`, `Remove`, and `TryRemove` are O(log n) worst-case. Equivalent-key
replacement retains the original key representative. It preserves instance identity only when the
new priority is equal to the stored priority under both the retained priority comparer and
`EqualityComparer<TPriority>.Default`, and the value is equal under
`EqualityComparer<TValue>.Default`; comparer-equal but representation-distinct priorities are stored,
as are default-equal priorities that the retained comparer distinguishes. Absent removals also
preserve identity. `Minimum`/`TryGetMinimum` are O(1); `DeleteMinimum` is O(log n). Enumeration is in
key order. Policies flow into every version; there is no operation that silently changes them.

`CreateRange` applies entries in enumeration order through `SetItem`, taking O(n log n) time and
O(log n) transient traversal stack per insertion. For equivalent keys, the last priority/value wins
while the first stored key representative remains. This is deliberately not Hinze's linear
ordered-input `from-ord-list` construction.

`EnumerateAtMost(minimumKey, maximumKey, maximumPriority)` returns entries in the inclusive key range
whose priority is no greater than the threshold. Traversal prunes outside BST key bounds and any
subtree whose cached winner exceeds the threshold, costing O(log n + v) where v is the number of
visited nodes that cannot be pruned (including the k reported entries). Since v can equal n, the
worst case is O(n); no O(log n + k) or pennant output-sensitive bound is promised. The iterator uses
an explicit stack and remains stack-safe. This query is the core's differentiator from a
HAMT-plus-sorted-set composition.

`ValidateStructure` checks strict comparer order, cached count and height, AVL balance, and every
cached subtree winner in O(n) time with O(log n) explicit-stack storage. It returns
`PrioritySearchQueueStatistics` containing the entry count, AVL height, and maximum absolute balance
factor. `Height` is also exposed as a constant-time diagnostic property.

## Range-Update Sequence

`RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` is an immutable `IReadOnlyList<TElement>`
implemented by a deterministic implicit-key AVL tree with cached ordered measures and lazily
composed subtree tags. It is a separate sibling core; it does not add tags to the measured finger
tree or tuned deque. `TOps` statically supplies the complete action algebra:

```csharp
public interface IRangeUpdateAlgebra<TElement, TMeasure, TTag>
    : IMeasure<TElement, TMeasure>
{
    static abstract TTag IdentityTag { get; }
    static abstract bool IsIdentity(TTag tag);
    static abstract TTag Compose(TTag newer, TTag older);
    static abstract TElement ApplyElement(TTag tag, TElement element);
    static abstract TMeasure ApplyMeasure(TTag tag, TMeasure measure, int count);
}
```

`Compose(newer, older)` means apply `older` and then apply `newer`. This order is normative. The
tag operations form a monoid with `IdentityTag`; their action on elements and measures preserves
identity and composition; `ApplyMeasure` distributes through ordered `Combine`, agrees with
`ApplyElement` on singleton measures, and maps `Empty` at count zero to `Empty`. More explicitly:

```text
Compose(IdentityTag, p) = p = Compose(p, IdentityTag)
Compose(r, Compose(q, p)) = Compose(Compose(r, q), p)

ApplyElement(Compose(q, p), x)
    = ApplyElement(q, ApplyElement(p, x))

ApplyMeasure(Compose(q, p), a, count)
    = ApplyMeasure(q, ApplyMeasure(p, a, count), count)

ApplyMeasure(p, Measure(x), 1) = Measure(ApplyElement(p, x))
ApplyMeasure(p, Combine(a, b), ca + cb)
    = Combine(ApplyMeasure(p, a, ca), ApplyMeasure(p, b, cb))
ApplyMeasure(p, Empty, 0) = Empty
```

`IsIdentity(IdentityTag)` is true. Any value-distinct tag recognized by `IsIdentity` obeys all the
same identity equations. `Combine` remains ordered and may be noncommutative. The algebra laws,
including the inherited `IMonoid` laws, are policy preconditions.

### Public Surface

```csharp
public sealed class RangeUpdateSequence<TElement, TMeasure, TTag, TOps>
    : IReadOnlyList<TElement>
    where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
{
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Empty { get; }
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Create(
        ReadOnlySpan<TElement> items);
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> CreateRange(
        IEnumerable<TElement> items);

    public int Count { get; }
    public bool IsEmpty { get; }
    public TMeasure Measure { get; }
    public TElement this[int index] { get; }

    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Prepend(TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Append(TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Insert(int index, TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> SetItem(int index, TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> RemoveAt(int index);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Concat(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> other);
    public (
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Left,
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Right) SplitAt(int index);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> GetRange(
        int index,
        int count);

    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> ApplyRange(
        int index,
        int count,
        TTag tag);
    public TMeasure MeasureRange(int index, int count);

    public Enumerator GetEnumerator();

    public struct Enumerator : IEnumerator<TElement>
    {
        public TElement Current { get; }
        public bool MoveNext();
        public void Dispose();
    }
}
```

The generic surface names the universal operation `ApplyRange`; it does not expose assignment- or
addition-specific verbs. Those meanings belong to `TTag` and `TOps`.

### Positional, Range, And Identity Semantics

`Create` and `CreateRange` retain input order. Bulk construction is O(n) and creates a deterministic
balanced tree; `CreateRange` rejects `null`, eagerly enumerates once, and completes enumeration
before element-measure callbacks begin. An empty input returns the canonical `Empty`.

The indexer, `SetItem`, and `RemoveAt` accept indices in `0 .. Count - 1`. `Insert` and `SplitAt`
accept positions in `0 .. Count`. `GetRange`, `ApplyRange`, and `MeasureRange` accept exactly those
`index` and `count` pairs for which both are nonnegative and `count <= Count - index`. This
subtraction-based test avoids overflow. Validation precedes all algebra calls, so an invalid range
throws even if its update tag would be an identity.

Boundary identities are observable:

- an empty `Create`/`CreateRange` or empty `GetRange` returns `Empty`;
- `SplitAt(0)` and `SplitAt(Count)` retain the source as the nonempty result;
- concatenating an empty side returns the nonempty operand;
- `GetRange(0, Count)` returns the source; and
- `ApplyRange` returns the source for an empty range or any tag recognized by `IsIdentity`.

`SetItem` has no equality shortcut because the generic surface owns no element-equality policy.
Values supplied to `Prepend`, `Append`, `Insert`, and `SetItem` are current logical values; pending
tags from earlier updates are pushed off the edit path and do not retroactively transform them.
An empty `ApplyRange` bypasses `IsIdentity`. `Concat` rejects `null` and checks combined-count
overflow before invoking any tag or measure policy.
`MeasureRange` of an empty range returns the cached empty measure without invoking element-measure
or tag callbacks after generic initialization.

### Lazy-Tag And Cache Invariant

Every node stores its element, children, AVL height, count, ordered measure, and an optional pending
tag. Its element and measure already reflect that pending tag, while the child objects do not. The
measure equals the ordered aggregate of the node's complete logical subtree, count is one plus both
child counts, and the child heights differ by at most one.

A full-subtree application transforms the root element and cached measure, composes the newer tag
after the previous pending tag, and retains both child references. Structural descent and rotations
immutably push a pending tag into the child roots first. Rotations therefore never move an untagged
child out from under an action that logically covers it. Split and join copy AVL boundary spines;
when descent crosses a pending tag, immutable push may additionally allocate a replacement wrapper
root for an off-spine child, while the structure below that root remains shared. An arbitrary range
update isolates the middle, applies one tag to its root, and rejoins. A whole-sequence update
bypasses split/join and changes one root in O(1).

Reads do not push by allocating permanent nodes. They carry inherited actions down the traversal.
Because a tag inherited from an ancestor is newer than the current node's pending tag, a child sees
`Compose(inherited, node.PendingTag)`. `MeasureRange` likewise combines fully covered cached
subtrees under inherited tags and visits only O(log n) boundary structure.

### Failure, Enumeration, And Concurrency

All policy calls required by a persistent operation complete before a result facade is published.
If `Measure`, `Combine`, `IsIdentity`, `Compose`, `ApplyElement`, or `ApplyMeasure` throws, every
input version remains unchanged. Count overflow and policy arithmetic overflow likewise publish no
partial result. Callback side effects outside the collection cannot be rolled back.

Enumeration yields the logical sequence after all inherited tags. The concrete `GetEnumerator`
path uses the public struct without boxing; `IEnumerable<TElement>` and `IEnumerable` paths box it.
An empty concrete enumerator has no traversal state. A nonempty enumerator allocates shared
O(log n) state. Copies of an in-progress enumerator share that state; after either copy advances,
the stale copy fails fast with `InvalidOperationException`. Separately created enumerators are
independent. `Reset` throws `NotSupportedException`, `Dispose` is a no-op, and `Current` returns
`default` outside an active element.

The collection has no mutable tag-propagation cache and is safe for concurrent reads, including
independent enumeration, indexing, aggregate reads, and range queries over shared versions. Policy
callbacks can consequently run concurrently; policy-owned mutable state requires external
synchronization.

### Complexity

| Operation | Worst-case structural bound |
| --- | --- |
| `Count`, `IsEmpty`, `Measure` | O(1) |
| `Create`, `CreateRange` | O(n) time and storage |
| Index lookup | O(log n), no permanent allocation |
| Insert, remove, replace | O(log n) time and O(log n) new nodes |
| Split, concat, nontrivial range extraction | O(log n) time and O(log n) new nodes |
| Whole-sequence tag application | O(1) time and one new root |
| Arbitrary range tag application | O(log n) time and O(log n) new nodes |
| Range measure query | O(1) for empty/full; O(log n) for a proper nonempty range, with no permanent allocation |
| Enumeration | O(n) time and O(log n) traversal state |

These are deterministic AVL/path-copying bounds, not comparative performance claims. The
[range-update sequence contract](range-update-sequence.md) gives the complete law table, affine
assign/add example, implementation invariant, deterministic test matrix, and benchmark boundary.
The C# reference is shipped. Focused Debug validation passes all 62 range-update tests, and the
complete 692-test FingerTree project suite passes in Debug and Release. At the pre-bimap Range
shipment checkpoint, the full serialized C# solution built with zero warnings or errors and passed
1,417/1,417 tests in both configurations: 319 Numerics + 292 HAMT + 692 FingerTree + 62 Ordered + 52
Tungsten. No benchmark result is part of the shipment evidence.
