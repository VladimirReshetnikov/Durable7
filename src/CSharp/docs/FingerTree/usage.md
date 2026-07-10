# FingerTree C# Usage Guide

- Created (UTC): 2026-07-02T20:15:41Z
- Repository HEAD: 6a3f4d2b64b7a6f367557a45a1d0549c232f9eed
- Audience: .NET consumers and maintainers using `Tools.DataStructures.FingerTree`
- Scope: Namespace, construction, persistence, public facade selection, common update/query patterns, and samples

This guide is the practical entry point for the C# FingerTree workspace. The
[API specification](api-specification.md) is the normative contract for complexity and edge cases;
the [design notes](FingerTree-Design-Notes.pdf) explain the architecture; this guide focuses on the
code shapes callers use first.

## Namespace, Build, And Samples

The public types live in the `Tools.DataStructures.FingerTree` namespace:

```csharp
using Tools.DataStructures.FingerTree;
```

Some names intentionally match BCL collection names. Use namespace aliases when a file also imports
`System.Collections.Generic` broadly:

```csharp
using FtPriorityQueue = Tools.DataStructures.FingerTree.PriorityQueue<string, int>;
using FtSortedSet = Tools.DataStructures.FingerTree.SortedSet<int>;
```

Validate the workspace through the solution:

```powershell
.\test.ps1
```

Runnable tours live under [`samples`](../../samples/README.md):

```powershell
dotnet run --project samples/Tools.DataStructures.FingerTree.Tour -c Release
dotnet run --project samples/Tools.DataStructures.FingerTree.Showcase -c Release
dotnet run --project samples/Tools.DataStructures.FingerTree.Editor -c Release
```

## Persistent Values

Every public collection is immutable and persistent. Update-shaped members return a new version and
leave the source version valid:

```csharp
var empty = FingerTreeDeque<int>.Empty;
var one = empty.AddLast(10);
var two = one.AddLast(20);
var three = two.AddFirst(5);

// empty == []
// one   == [10]
// two   == [10, 20]
// three == [5, 10, 20]
```

Keeping an old version is just keeping a reference. Edits share untouched structure with the source
version, so the same pattern supports snapshots, undo/redo, and lock-free read sharing. The worked
patterns are in [persistence and concurrency](persistence-and-concurrency.md).

## Persistent Deque

Use `FingerTreeDeque<T>` for a persistent indexed sequence with efficient endpoint operations,
concatenation, splitting, indexed edits, and sorted-sequence helpers:

```csharp
var deque = FingerTreeDeque<int>.Create(1, 2, 3);
var extended = deque.AddFirst(0).AddLast(4);

var first = extended.PopFirst();
var rest = first.Rest;                    // [1, 2, 3, 4]

var split = rest.SplitAt(2);
var joined = split.Left.Concat(split.Right);
var replaced = joined.SetItem(1, 20);     // [1, 20, 3, 4]
```

Use `TryPeekFirst`, `TryPeekLast`, `PopFirst`, and `PopLast` when endpoint access is the main
workflow. Use `InsertAt`, `InsertRange`, `RemoveAt`, `RemoveRange`, `GetRange`, `SplitAt`,
`SplitItemAt`, and `SplitRange` when the operation is positional.

The deque also has helpers for already sorted deques:

```csharp
var sorted = FingerTreeDeque<int>.Create(1, 3, 3, 7, 9);
var lower = sorted.SortedLowerBound(3);             // 1
var upper = sorted.SortedUpperBound(3);             // 3
var equalRange = sorted.SplitAtSortedEqualRange(3); // Range == [3, 3]
var inserted = sorted.InsertSorted(5);              // [1, 3, 3, 5, 7, 9]
```

Sorted helpers assume the receiver is sorted under the supplied comparer. If sortedness is a
long-lived invariant, prefer `SortedBag<T>`, `SortedSet<T>`, or `SortedDictionary<TKey, TValue>`.

## Reversible Deque

Use `ReversibleDeque<T>` when logical reversal is a primary operation. `Reverse()` is O(1) and
returns a new orientation over shared storage:

```csharp
var deque = ReversibleDeque<int>.Create(1, 2, 3);
var reversed = deque.Reverse();

var first = reversed[0];        // 3
var restored = reversed.Reverse();
```

Endpoint, index, split, and concat operations respect the current logical orientation. Use
`FingerTreeDeque<T>` when reversal is not part of the contract and the tuned deque is enough.

## Sorted Collections

Use the sorted wrappers when order is an invariant owned by the collection.

`SortedBag<T>` is a multiset that preserves duplicates:

```csharp
var bag = SortedBag<int>.CreateRange(new[] { 5, 1, 3, 3, 1 });

var countOfThree = bag.CountOf(3);     // 2
var rankOfThree = bag.CountLessThan(3);
var secondItem = bag[1];
var middle = bag.GetRange(2, 5);
```

`SortedSet<T>` keeps one value per comparer-equal item and supports navigable-set queries and
set algebra:

```csharp
var set = Tools.DataStructures.FingerTree.SortedSet<int>
    .CreateRange(new[] { 5, 1, 3, 3 });

var withFour = set.Add(4);
if (withFour.TryFloor(4, out var floor))
{
    // floor == 4
}

var other = Tools.DataStructures.FingerTree.SortedSet<int>.CreateRange(new[] { 3, 7 });
var union = withFour.Union(other);
var intersection = withFour.Intersect(other);
```

`SortedDictionary<TKey, TValue>` is a key-ordered persistent map:

```csharp
var map = Tools.DataStructures.FingerTree.SortedDictionary<int, string>.Empty
    .SetItem(2, "two")
    .SetItem(1, "one");

if (map.TryGetValue(2, out var value))
{
    // value == "two"
}

var firstEntry = map.EntryAt(0);
var range = map.GetRange(1, 2);
```

Pass an `IComparer<T>` or `IComparer<TKey>` through `Create` or `CreateRange` when the default order
is not the desired order.

For large batches of sorted-set or sorted-dictionary edits followed by a snapshot, use the nested builders:

```csharp
var builder = Tools.DataStructures.FingerTree.SortedSet<int>.CreateBuilder();
builder.UnionWith(new[] { 5, 1, 3 });
builder.Remove(1);
var frozenSet = builder.ToImmutable();

var mapBuilder = map.ToBuilder();
mapBuilder.SetItem(3, "three");
mapBuilder.Remove(1);
var frozenMap = mapBuilder.ToImmutable();
```

The sorted builders are staging builders: a dirty `ToImmutable()` rebuilds the immutable value. For small
batches into a large existing collection, persistent `Add`/`Remove`/`SetItem` remains the better default.

## Priority Queue

`PriorityQueue<TElement, TPriority>` is a persistent minimum-priority queue. Equal priorities drain
in insertion order, and two queues can be melded efficiently:

```csharp
var queue = Tools.DataStructures.FingerTree.PriorityQueue<string, int>.Empty
    .Enqueue("slow", 10)
    .Enqueue("fast", 1)
    .Enqueue("normal", 5);

if (queue.TryPeek(out var element, out var priority))
{
    // element == "fast"; priority == 1
}

while (queue.TryDequeue(out element, out priority, out var rest))
{
    // Process element.
    queue = rest;
}
```

Use `Meld` when two persistent queues should be combined without rebuilding a heap from scratch.
The queue orders priorities by `Comparer<TPriority>.Default`.

## Interval Tree

`IntervalTree<T>` stores closed intervals and supports overlap and point-stabbing queries:

```csharp
var intervals = IntervalTree<int>.Empty
    .Insert(1, 5)
    .Insert(10, 15)
    .Insert(3, 8);

if (intervals.TryFindOverlap(4, 11, out var firstOverlap))
{
    // firstOverlap overlaps [4, 11].
}

var all = intervals.FindOverlaps(new Interval<int>(4, 11));
var count = intervals.CountOverlaps(new Interval<int>(4, 11));
var coalesced = intervals.Coalesce();
```

Intervals are ordered by low endpoint, and overlap queries use the cached maximum high endpoint
measure to skip subtrees.

## Ropes And Text

Use `Rope<T>` for a persistent chunked positional sequence:

```csharp
var rope = Rope<int>.Create(1, 2, 3, 4);
var edited = rope.Insert(2, 99).RemoveAt(0);
var slice = edited.Slice(1, 2);
var joined = slice.Concat(Rope<int>.Create(7, 8));
```

Use `MeasuredRope<T, TMeasure, TMeasureOps>` when the sequence also needs cumulative-measure
navigation:

```csharp
var weights = MeasuredRope<int, int, SumMeasure<int>>.Create(5, 1, 4);
var prefix = weights.PrefixMeasure(2); // 6

if (weights.TryLocateByMeasure(sum => sum > 5, out var index, out var before, out var element))
{
    // index == 1, before == 5, element == 1
}
```

For text, use `Rope<char>` for raw character storage and
`MeasuredRope<char, int, NewlineMeasure>` for newline-aware navigation:

```csharp
var text = "hello\nworld\n".ToTextRope();

var lineCount = text.LineCount();          // 3, including the trailing empty line
var location = text.LineColumnOf(8);
var secondLine = text.GetLine(1);

var inserted = text.InsertRange(5, ",\nthere");
var materialized = inserted.AsString();
```

`RopeBuilder` is the convenient path for incremental text construction:

```csharp
var builder = new RopeBuilder();
builder.Append("hello").Append(' ').AppendLine("world");

var charRope = builder.ToRope();
var textRope = builder.ToTextRope();
```

For element-generic append staging, use the nested rope builders. They adopt an existing rope as a frozen
prefix, freeze only the newly appended tail, and cache clean snapshots:

```csharp
var ints = Rope<int>.Create(1, 2).ToBuilder();
ints.Add(3);
ints.AddRange(new[] { 4, 5 });
var frozenInts = ints.ToImmutable();

var measured = MeasuredRope<int, int, SumMeasure<int>>.CreateBuilder();
measured.AddRange(new[] { 5, 1, 4 });
var total = measured.Measure; // 10
var frozenMeasured = measured.ToImmutable();
```

The editor-grade extras cover code-point and grapheme addressing, newline-style detection, and
CRLF-aware line text:

```csharp
var style = text.DetectNewlineStyle();
var codePoints = text.CodePointCount();
var graphemes = text.GraphemeCount();
var lineText = text.GetLineText(0);
```

## Generic Measured Tree

Use `FingerTree<TElement, TMeasure, TMeasureOps>` directly when the algorithm is naturally driven by
a monoid measure. Built-in measures cover common cases:

```csharp
var counted = FingerTree<int, int, SizeMeasure<int>>.Create(10, 20, 30, 40);
var split = counted.Split(measure => measure > 2);

// split.Left  == [10, 20]
// split.Right == [30, 40]
```

Ready-made named operations express common measure choices without hand-written predicates:

```csharp
var maxTree = FingerTree<int, Optional<int>, MaxMeasure<int>>.Create(3, 9, 1);
maxTree.TryExtractMax(out var max, out var withoutMax);

var weights = FingerTree<int, int, SumMeasure<int>>.Create(5, 1, 4);
weights.TrySelectByCumulativeWeight(5, out var selected, out var weightBefore);

var sizeAndSum = ProductMeasures.CreateSizeAndSum(5, 1, 4, 2);
var prefix = sizeAndSum.SplitAtIndex(2);
var selectedWithIndex = sizeAndSum.TrySelectByCumulativeWeight(6, out var picked, out var index);
```

Implement `IMeasure<TElement, TMeasure>` for custom measures and `IMeasurePredicate<TMeasure>` for
zero-allocation custom locate/split predicates. Prefer the typed facades above when they match the
problem; use the raw measured tree when your measure is the primary design.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Persistent indexed sequence with endpoint edits | `FingerTreeDeque<T>` |
| O(1) logical reverse over a persistent sequence | `ReversibleDeque<T>` |
| Custom monoid measure, measure-guided locate, or split | `FingerTree<TElement, TMeasure, TMeasureOps>` |
| Sorted values with duplicates | `SortedBag<T>` |
| Unique sorted values and set algebra | `SortedSet<T>` |
| Batched sorted-set edits before one snapshot | `SortedSet<T>.Builder` |
| Sorted key/value lookup and rank access | `SortedDictionary<TKey, TValue>` |
| Batched sorted-dictionary edits before one snapshot | `SortedDictionary<TKey, TValue>.Builder` |
| Minimum-priority draining and meld | `PriorityQueue<TElement, TPriority>` |
| Closed-interval overlap and containment queries | `IntervalTree<T>` |
| Chunked persistent positional sequence | `Rope<T>` |
| Incremental generic append construction | `Rope<T>.Builder` |
| Chunked sequence with cumulative measure navigation | `MeasuredRope<T, TMeasure, TMeasureOps>` |
| Incremental measured append construction | `MeasuredRope<T, TMeasure, TMeasureOps>.Builder` |
| Newline-aware text content | `MeasuredRope<char, int, NewlineMeasure>` and `RopeText` helpers |
| Incremental text construction | `RopeBuilder` |

For performance evidence, see [benchmarks](benchmarks.md). For cross-language contract alignment,
see the repository [porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
