# FingerTree C# Usage Guide

- Created (UTC): 2026-07-02T20:15:41Z
- Repository HEAD: 6a3f4d2b64b7a6f367557a45a1d0549c232f9eed
- Audience: .NET consumers and maintainers using `Durable7.FingerTree`
- Scope: Namespace, construction, persistence, public facade selection, common update/query patterns, and samples

This guide is the practical entry point for the C# FingerTree workspace. The
[API specification](api-specification.md) is the normative contract for complexity and edge cases;
the [design notes](FingerTree-Design-Notes.pdf) explain the architecture; this guide focuses on the
code shapes callers use first.

## Namespace, Build, And Samples

The public types live in the `Durable7.FingerTree` namespace:

```csharp
using Durable7.FingerTree;
```

Some names intentionally match BCL collection names. Use namespace aliases when a file also imports
`System.Collections.Generic` broadly:

```csharp
using FtPriorityQueue = Durable7.FingerTree.PriorityQueue<string, int>;
using FtSortedSet = Durable7.FingerTree.SortedSet<int>;
```

Validate the workspace through the solution:

```powershell
.\test.ps1
```

Runnable tours live under [`samples`](../../samples/README.md):

```powershell
dotnet run --project samples/Durable7.FingerTree.Tour -c Release
dotnet run --project samples/Durable7.FingerTree.Showcase -c Release
dotnet run --project samples/Durable7.FingerTree.Editor -c Release
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
var set = Durable7.FingerTree.SortedSet<int>
    .CreateRange(new[] { 5, 1, 3, 3 });

var withFour = set.Add(4);
if (withFour.TryFloor(4, out var floor))
{
    // floor == 4
}

var other = Durable7.FingerTree.SortedSet<int>.CreateRange(new[] { 3, 7 });
var union = withFour.Union(other);
var intersection = withFour.Intersect(other);
```

`SortedDictionary<TKey, TValue>` is a key-ordered persistent map:

```csharp
var map = Durable7.FingerTree.SortedDictionary<int, string>.Empty
    .SetItem(2, "two")
    .SetItem(1, "one");

if (map.TryGetValue(2, out var value))
{
    // value == "two"
}

var firstEntry = map.EntryAt(0);
var range = map.GetRange(1, 2);
```

Use `PersistentDeltaMap<TKey, TValue>` when the recurring question is “what is the exact net change
from this accepted checkpoint?” rather than an arbitrary-pair diff:

```csharp
var tracked = PersistentDeltaMap<int, string>.CreateRange(new[]
{
    KeyValuePair.Create(1, "one"),
    KeyValuePair.Create(2, "two"),
})
    .SetItem(1, "ONE")
    .Remove(2)
    .SetItem(3, "three");

foreach (var change in tracked.GetChanges()) // keys 1, 2, 3
    Console.WriteLine($"{change.Key}: {change.Kind}");

var reverted = tracked.Rollback();   // { 1: "one", 2: "two" }, O(1)
var accepted = tracked.Checkpoint(); // current state becomes the checkpoint, O(1)
```

Repeated writes coalesce to the checkpoint's first-before/final-after endpoints; restoring an
endpoint cancels its record. With N keys across checkpoint and current state and k net-changed keys,
point operations remain O(log(N + 1)) and fully consuming ordered `GetChanges()` is `Θ(k + 1)`.
This research surface is
fully persistent but deliberately limited to a designated checkpoint and point updates; it does not
promise arbitrary-version diff, tracked bulk clear, or tracked range extraction. The
[proposal and prior-art audit](../../../../docs/proposals/persistent-delta-map-2026-07-25.md) state the
precise comparison model and novelty boundary.

Pass an `IComparer<T>` or `IComparer<TKey>` through `Create` or `CreateRange` when the default order
is not the desired order.

For large batches of sorted-set or sorted-dictionary edits followed by a snapshot, use the nested builders:

```csharp
var builder = Durable7.FingerTree.SortedSet<int>.CreateBuilder();
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

## Policy-Canonical Sorted Set

Use `CanonicalSortedSet<T>` when equal contents should converge on one binary-tree shape within a
retained rank policy. The shared default is convenient for process-local persistent versions; its
HMAC key is generated randomly once and is not exposed:

```csharp
var local = CanonicalSortedSet<int>.Empty
    .Add(3)
    .Add(1)
    .Add(2);

var withoutTwo = local.Remove(2);
CanonicalSortedSetStatistics statistics = local.ValidateStructure();
Console.WriteLine($"{statistics.Count} items, height {statistics.Height}");
```

All versions derived from `local` retain the same `ZipTreeRankPolicy<int>`. Another process, or a
fresh `ZipTreeRankPolicy<int>.Create()` call without a seed, receives a different random key and
generally a different shape.

Use a public seed for reproducible tests, artifacts, or cooperative processes where adversarial key
selection is not a concern. An explicit comparer must be paired with a rank hash that is constant on
its equivalence classes. This example treats integers with equal magnitude as equivalent:

```csharp
static ulong MagnitudeRankHash(int value) => (ulong)Math.Abs((long)value);

var byMagnitude = Comparer<int>.Create(static (left, right) =>
    Math.Abs((long)left).CompareTo(Math.Abs((long)right)));

var policy = ZipTreeRankPolicy<int>.Create(
    comparer: byMagnitude,
    rankHash: MagnitudeRankHash,
    seed: 0x1234_5678_9abc_def0UL);

var first = CanonicalSortedSet<int>.CreateRange([-3, 2, 1, 3, -2], policy);
first.TryGetValue(3, out var representative); // representative == -3

var secondPolicy = ZipTreeRankPolicy<int>.Create(
    comparer: byMagnitude,
    rankHash: MagnitudeRankHash,
    seed: 0x1234_5678_9abc_def0UL);
var second = CanonicalSortedSet<int>.CreateRange([-1, -2, 3], secondPolicy);

Console.WriteLine(first.SetEquals(second));                 // True: semantic equality.
Console.WriteLine(first.ContentHash == second.ContentHash); // True for this reproduced rank policy.
```

The seed is public and deterministically derives the HMAC key; it is not a password or a denial-of-
service defense. For ranks that should remain unpredictable to an input chooser, create a policy
from caller-managed secret material:

```csharp
byte[] rankKey = System.Security.Cryptography.RandomNumberGenerator.GetBytes(32);
var keyedPolicy = ZipTreeRankPolicy<int>.CreateKeyed(
    rankKey,
    rankHash: static value => unchecked((ulong)(uint)value));

var keyed = CanonicalSortedSet<int>.CreateRange([5, 1, 9, 3], keyedPolicy);
```

`CreateKeyed` requires at least 32 bytes and copies the key. Retain it securely if another policy or
process must reproduce ranks; otherwise clear the caller-owned array after construction. A secret
key cannot compensate for rank-hash collisions: two equal 64-bit rank-hash inputs receive the same
complete HMAC-derived rank.

Set equality and algebra intentionally have different compatibility rules. `SetEquals` and the
`IReadOnlySet<T>` relation members use the receiver's comparer and interoperate semantically with
different canonical policies or ordinary sets. `Union`, `Intersect`, and `Except` require the exact
same policy object because their results must stay in one rank space:

```csharp
var left = CanonicalSortedSet<int>.CreateRange([1, 2, 4], policy);
var right = CanonicalSortedSet<int>.CreateRange([2, 3, 5], policy);
var union = left.Union(right);

var differentPolicy = CanonicalSortedSet<int>.CreateRange(
    [1, 2, 4],
    ZipTreeRankPolicy<int>.Create(seed: 99));
bool semanticallyEqual = left.SetEquals(differentPolicy);
```

`ContentHash` is a memoized non-cryptographic shape/content digest. Treat it as a same-policy
inequality fast path, never as authentication or as a cross-policy content address; call
`SetEquals` for semantic equality.

Expected O(log n) lookup and updates assume sparse rank-hash collisions and pseudorandom HMAC ranks
for the actual key set. `Add` and `Remove` copy O(h) path nodes. A constant rank hash can force
h = n, but all search, update, build, enumeration, digest, equality, and validation traversals use
explicit stacks and remain stack-safe. `CreateRange` always sorts in O(n log n), then builds the
Cartesian tree in O(n).

## Priority Queue

`PriorityQueue<TElement, TPriority>` is a persistent minimum-priority queue. Equal priorities drain
in insertion order, and two queues can be melded efficiently:

```csharp
var queue = Durable7.FingerTree.PriorityQueue<string, int>.Empty
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

## Worst-Case Meldable Heap

`BrodalOkasakiHeap<T>` is the alternative for a persistent workload that needs worst-case rather
than amortized latency for insert and meld. Reuse the same comparer object for every heap that may
be melded:

```csharp
var order = Comparer<int>.Default;
var left = BrodalOkasakiHeap<int>.CreateRange([7, 2, 9], order);
var right = BrodalOkasakiHeap<int>.CreateRange([6, 1, 8], order);
var heap = left.Meld(right).Insert(3);

var statistics = heap.ValidateStructure();

while (heap.TryDeleteMinimum(out var minimum, out var rest))
{
    // minimum values arrive as 1, 2, 3, 6, 7, 8, 9.
    heap = rest;
}
```

`Minimum`, `Insert`, and `Meld` are O(1) worst-case; `DeleteMinimum` is O(log n) worst-case.
`Meld` requires comparer object identity even when one operand is empty. Ordinary enumeration visits
each element in unspecified structural order, so repeatedly delete the minimum when sorted order is
required. Equal elements have no stable tie order. `ValidateStructure` is an O(n) diagnostic pass.

## Priority Search Queue

`PrioritySearchQueue<TKey, TPriority, TValue>` is one persistent structure for keyed lookup,
minimum-priority removal, and a combined key-range/priority-threshold query:

```csharp
var jobs = PrioritySearchQueue<string, int, string>.Create(
        StringComparer.Ordinal,
        Comparer<int>.Default)
    .SetItem("compile", 3, "Build the solution")
    .SetItem("deploy", 8, "Publish artifacts")
    .SetItem("audit", 1, "Check invariants");

var next = jobs.Minimum; // key "audit", priority 1
var urgent = jobs.EnumerateAtMost("a", "z", maximumPriority: 3).ToArray();
jobs = jobs.DeleteMinimum(out var removed);

var statistics = jobs.ValidateStructure();
```

The AVL is ordered by the retained key comparer and caches one minimum-priority winner per subtree.
Keyed lookup/update/removal and `DeleteMinimum` are O(log n); `Minimum` is O(1). Priority ties use
key order, and full enumeration and `EnumerateAtMost` both return entries in key order. A range query
costs O(log n + v), where v is the number of nodes not excluded by key or priority pruning; v can be
n for a dense result. Equivalent-key replacement keeps the originally stored key representative.
An update is an exact no-op only when its priority is equal under both the retained priority comparer
and default equality and its value is equal under default equality. `CreateRange` applies entries in
order in O(n log n), so the last priority/value for an equivalent key wins while the first key
representative remains stored.

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

Use `PersistentIntervalMap<TEndpoint, TValue>` when each distinct interval carries one payload:

```csharp
var bookings = PersistentIntervalMap<int, string>.Empty
    .Add(new Interval<int>(9, 11), "design review")
    .Add(new Interval<int>(10, 12), "release window")
    .SetItem(new Interval<int>(14, 15), "retrospective");

if (bookings.TryFindContaining(10, out var booking))
    Console.WriteLine($"{booking.Key}: {booking.Value}");

var morning = bookings.FindOverlaps(new Interval<int>(8, 12));
```

The map validates every interval argument, orders keys lexicographically by `(Low, High)`, and
allows distinct keys to overlap. `Add` rejects an equivalent interval; `SetItem` replaces its
payload while retaining the original interval representative. A configured value comparer defines
when replacement is an identity-preserving no-op. Endpoint ordering is fixed to
`Comparer<TEndpoint>.Default`, while value equality is independently configurable. Unlike
`IntervalTree<T>`, the map has no `Coalesce` because it cannot infer how payloads should merge.

## Ropes And Text

Use `Rope<T>` for a persistent chunked positional sequence:

```csharp
var rope = Rope<int>.Create(1, 2, 3, 4);
var edited = rope.Insert(2, 99).RemoveAt(0);
var slice = edited.Slice(1, 2);
var joined = slice.Concat(Rope<int>.Create(7, 8));
```

For a run of localized edits, retain an immutable `RopeCursor<T>` and materialize a rope only at the
checkpoint cadence the application actually needs. The cursor denotes a gap: insertion leaves the gap
after the inserted values, backspace (`DeletePrevious`) moves it left, while `DeleteNext` and
`ReplaceNext` keep it fixed.

```csharp
var source = Rope<int>.Create(1, 2, 3, 4);
var atMiddle = source.GetCursor(2);                 // 1, 2 | 3, 4
var typed = atMiddle.InsertRange([90, 91]);         // 1, 2, 90, 91 | 3, 4
var erased = typed.DeletePrevious();                // 1, 2, 90 | 3, 4
var checkpoint = erased.Snapshot();                 // [1, 2, 90, 3, 4]

// Every cursor is a persistent version. Branching never changes the retained cursor.
var alternate = atMiddle.ReplaceNext(30).Snapshot(); // [1, 2, 30, 4]
var unchanged = atMiddle.Snapshot();                  // the original source object
```

`TryPeekPrevious` and `TryPeekNext` inspect either side without moving. `MovePrevious`, `MoveNext`,
`DeletePrevious`, `DeleteNext`, and `ReplaceNext` throw `InvalidOperationException` when the required
neighbor does not exist; `Seek` and `GetCursor` reject positions outside `0 .. Count`.
`Seek(cursor.Position)` and empty `InsertRange` preserve the exact cursor state. A dirty first
`Snapshot()` performs the canonical tree join, while every later snapshot of that edit version—including
from a differently positioned navigation cursor—returns the same rope reference in O(1). The default
`RopeCursor<T>` value is invalid; use `Rope<T>.Empty.GetCursor()` for a real empty cursor.

The cursor is tuned for a linear lineage of local edits: those operations are O(1) amortized and O(log n)
worst-case. Retaining and editing `b` independent boundary branches has the conservative O(b log n)
bound. Callers that need a canonical rope after every edit should include that O(log n) snapshot work in
their workload model.

The [API specification](api-specification.md#positional-edit-cursor) is normative for the shipped
surface; the [C0 decision](rope-cursor-c0-decision.md) records the benchmark selection and the exact
linear-lineage/branched-history proof boundary.

The measured sibling preserves the same gap and edit vocabulary while carrying the aggregate on
both sides of the gap. Absolute measure seek accepts either a delegate or a closure-free struct
predicate and returns the gap immediately before the first element whose inclusive prefix makes the
predicate true:

```csharp
var text = "alpha\nbeta\ngamma".ToTextRope();
var cursor = text.GetCursor(6);               // immediately before 'b'
var edited = cursor.InsertRange("new ");       // alpha\nnew |beta\ngamma

Console.WriteLine(edited.MeasureBefore);      // one newline before the gap
Console.WriteLine(edited.MeasureAfter);       // two newlines at/after the gap

if (edited.TrySeekByMeasure(newlines => newlines >= 2, out var beforeSecondNewline))
    Console.WriteLine(beforeSecondNewline.Position);

var checkpoint = edited.Snapshot();
```

`MeasureBefore` and `MeasureAfter` compose in source order to the whole-version measure; no inverse
or commutativity is required. A measure seek from an existing cursor lineage prepares the selected
bounded fragment once and shares its element measures with descendant versions. A one-shot
`MeasuredRope.TryGetCursorByMeasure` defers focus construction without retaining a full
element-measure array. Failed callbacks publish no partial cache or snapshot. See the
[measured cursor API](api-specification.md#measured-edit-cursor) and
[C2 decision](measured-rope-cursor-c2-decision.md) for the exact semantics, complexity scope, and
locked benchmark evidence.

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

## FIFO Sliding-Window Aggregation

Use `DabaLite<T, TMonoid>` for one mutable FIFO window whose ordered aggregate must be available
with a bounded amount of work on every operation. The same monoid types used by the measured tree
work directly; they need not be commutative or invertible.

```csharp
var window = new DabaLite<long, SumMeasure<long>>();
window.Insert(5);
window.Insert(8);
window.Insert(13);

long total = window.Aggregate; // 26
window.Evict();                // removes the oldest contribution, 5
total = window.Aggregate;      // 21

var state = window.ValidateStructure(); // state.Count == 2
window.Clear();                // O(1), with no Combine call
```

Insert, eviction, and query invoke `Combine` at most three, two, and one times respectively. Their
total worst-case O(1) bound assumes both `Combine` and `Empty` are O(1). If either callback throws
during a mutating operation, the window remains unchanged. `Clear` is O(1), but a nonempty clear
still obtains `Empty` once before committing. The type intentionally exposes neither the oldest raw
value nor enumeration: DABA Lite overwrites queue slots with partial aggregates while a flip is in
progress. Externally serialize all access to an instance.

## Relaxed Radix-Balanced Vectors

Use `RrbVector<T>` for a persistent sequence whose dominant operation is uniform random indexing:

```csharp
var left = RrbVector<int>.CreateRange(Enumerable.Range(0, 10_000));
var right = RrbVector<int>.CreateRange(Enumerable.Range(10_000, 10_000));
var vector = left.Concat(right);

int middle = vector[10_000];
var edited = vector.SetItem(10_000, -1);
var (prefix, suffix) = edited.SplitAt(5_000);

var builder = vector.ToBuilder(); // adopts vector as an O(1) immutable prefix
builder.AddRange(Enumerable.Range(20_000, 10_000));
var extended = builder.ToImmutable();
```

Choose `FingerTreeDeque<T>` when endpoints and concat dominate, `Rope<T>` when scan density and
middle edits dominate, and `RrbVector<T>` when consistent indexing constants dominate. Packed RRB
nodes use radix indexing without size tables; split/concat introduces size tables only where child
spans become irregular. The immutable type has no dedicated tail buffer, so prefer its builder over
an `AddLast` loop for bulk append construction.

## Experimental Ancestral Slice Queue

Use `AncestralSliceQueue<T>` when versions form branching append histories and every retained
contiguous slice must remain appendable. It is intentionally narrower than a general persistent
sequence: it has no prepend, point update, middle insertion, or concatenation of unrelated histories.

```csharp
var source = AncestralSliceQueue<int>.CreateRange(Enumerable.Range(0, 100));
var window = source.Slice(25, 50);

var leftBranch = window.AddLast(1000);
var rightBranch = window.RemoveFirst().AddLast(2000);
var (prefix, suffix) = window.SplitAt(20);

int item = suffix[5];
// source, window, prefix, and both branches remain unchanged and appendable.
```

Every handle shares an append-only arena. The shipped Myers arena makes `AddLast` O(1) amortized;
`RemoveFirst`, `RemoveLast`, and `Drop` are O(1) worst case; and `First`, indexing, `Take`, `Slice`,
and nontrivial `SplitAt` are O(log M) worst case for M historical appends in that arena. The arena
retains every appended payload until the arena itself is collectible. An optimal dynamic
level-ancestor backend would make all scalar operations O(1) worst case, but that backend is a
theoretical instantiation rather than shipped code. See the
[research proposal](../../../../docs/proposals/ancestral-slice-queue-2026-07-25.md) for the invariant,
proof, scoped novelty claim, and comparison table.

## Range-Update Sequence

Use `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` when a persistent indexed sequence must
apply one algebraic transformation to a contiguous range and query that range's aggregate without
visiting every covered element. `TOps` defines both the ordinary ordered element measure and how a
tag acts on an element and on a combined subtree measure. The central rule is
`Compose(newer, older)`: it means apply `older` first and `newer` second.

For example, the affine sum policy in the
[range-update sequence contract](range-update-sequence.md#affine-assignment-and-addition-example)
defines tags that assign or add `long` values while measuring each range by its sum:

```csharp
using Sequence = Durable7.FingerTree.RangeUpdateSequence<
    long, long, AffineTag, AffineSumAlgebra>;

var source = Sequence.Create([1, 2, 3, 4]);
var added = source.ApplyRange(1, 2, AffineTag.Add(10));
// [1, 12, 13, 4], Measure == 30

var assigned = added.ApplyRange(2, 2, AffineTag.Assign(7));
// [1, 12, 7, 7], Measure == 27

long middleSum = assigned.MeasureRange(1, 2); // 19
var (prefix, suffix) = assigned.SplitAt(2);
var reconstructed = prefix.Concat(suffix);

// source remains [1, 2, 3, 4].
```

A whole-sequence nonidentity update performs O(1) structural work. An arbitrary contiguous update,
range measure, indexed edit, split, or concat performs O(log n) structural work in the worst case;
elapsed time also includes the policy calls. Empty-range and `IsIdentity`-recognized updates return
the source instance. Range validation happens before policy callbacks, and a throwing policy leaves
every input snapshot unchanged. The generic core deliberately has no `AddRange` or `AssignRange`
method because those verbs belong to a particular tag algebra.

Concrete `foreach` uses the public struct enumerator without boxing; interface enumeration boxes.
Separate enumerators are independent and safe for concurrent reads. Do not copy an in-progress
enumerator to fork a traversal: copies share traversal state, and the stale copy fails fast after
the other advances.

## Persistent Chunked Bit Set

Use `PersistentChunkedBitSet` for a sparse nonnegative integer set when population rank/select and
word-wise algebra matter:

```csharp
var occupied = PersistentChunkedBitSet.CreateRange([0, 63, 64, 10_000]);
occupied = occupied.Add(65).Remove(0);

var upTo64 = occupied.Rank(64); // inclusive
var second = occupied.Select(1); // zero-based population order
var common = occupied.Intersect(otherBits);
```

Storage is proportional to nonzero 64-bit chunks, not the largest bit index. See the
[complete contract](persistent-chunked-bit-set.md).

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
| Exact sorted net changes from one designated persistent-map checkpoint | `PersistentDeltaMap<TKey, TValue>` |
| Minimum-priority draining and meld | `PriorityQueue<TElement, TPriority>` |
| Closed-interval overlap and containment queries | `IntervalTree<T>` |
| Closed-interval keys with payload lookup and overlap queries | `PersistentIntervalMap<TEndpoint, TValue>` |
| Chunked persistent positional sequence | `Rope<T>` |
| Localized persistent positional editing with retained branches | `RopeCursor<T>` from `Rope<T>.GetCursor()` |
| Uniform random-access persistent sequence | `RrbVector<T>` |
| Branching append history with appendable persistent slices | `AncestralSliceQueue<T>` (experimental) |
| Persistent indexed sequence with logarithmic range actions and aggregate queries | `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` |
| Sparse nonnegative integer set with population rank/select | `PersistentChunkedBitSet` |
| Mutable FIFO window aggregate with worst-case O(1) operations | `DabaLite<T, TMonoid>` |
| Policy-scoped canonical sorted shape and memoized digest | `CanonicalSortedSet<T>` |
| Worst-case O(1) persistent insert and meld | `BrodalOkasakiHeap<T>` |
| Keyed priority updates and range-bounded priority queries | `PrioritySearchQueue<TKey, TPriority, TValue>` |
| Incremental generic append construction | `Rope<T>.Builder` |
| Chunked sequence with cumulative measure navigation | `MeasuredRope<T, TMeasure, TMeasureOps>` |
| Incremental measured append construction | `MeasuredRope<T, TMeasure, TMeasureOps>.Builder` |
| Newline-aware text content | `MeasuredRope<char, int, NewlineMeasure>` and `RopeText` helpers |
| Incremental text construction | `RopeBuilder` |

For performance evidence, see [benchmarks](benchmarks.md). For cross-language contract alignment,
see the repository [porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
