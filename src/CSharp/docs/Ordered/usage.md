# C# Persistent Ordered Collections Usage

- Status: Current usage guide
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: C# callers using the neutral ordered map and set
- Scope: Common map/set construction, update, movement, ranges, algebra, and persistence patterns

Import the neutral namespace:

```csharp
using Tools.DataStructures.Ordered;
```

## Ordered Maps

```csharp
var headers = PersistentOrderedMap<string, string>.Create(
        StringComparer.OrdinalIgnoreCase,
        StringComparer.Ordinal)
    .Add("Accept", "application/json")
    .Add("X-Trace", "first")
    .SetItem("x-trace", "updated");

// The first key representative and position are retained.
Console.WriteLine(headers.EntryAt(1));       // [X-Trace, updated]
Console.WriteLine(headers.IndexOfKey("ACCEPT")); // 0

var reordered = headers.MoveToFirst("x-trace");
// [X-Trace, updated], [Accept, application/json]
```

`Add`, `AddFirst`, and `Insert` reject equivalent keys; `TryAdd` is the nonthrowing alternative.
`SetItem` alone adds-or-replaces, and replacement never moves a key. `GetRange`, `Take`, `Drop`,
`Reverse`, removal by key/position, and enumeration all operate in the explicit map order.

## Construction And First Representatives

`CreateRange` processes values in order. The first representative of an equality class fixes both
its representative and its position.

```csharp
var names = PersistentOrderedSet<string>.CreateRange(
    ["Alpha", "beta", "ALPHA", "gamma", "BETA"],
    StringComparer.OrdinalIgnoreCase);

// ["Alpha", "beta", "gamma"]
Console.WriteLine(names.Count);       // 3
Console.WriteLine(names.First);       // Alpha
Console.WriteLine(names.Last);        // gamma

names.TryGetValue("alpha", out var stored);
Console.WriteLine(stored);            // Alpha, not the lookup spelling
Console.WriteLine(names.IndexOf("BETA")); // 1
```

`Empty` and `Create()` use `EqualityComparer<T>.Default`. Passing a custom comparer preserves that
exact comparer object even when a result becomes empty.

## Addition Does Not Hide Movement

Ordinary add operations affect only absent classes:

```csharp
var source = PersistentOrderedSet<string>.CreateRange(
    ["one", "two", "three"],
    StringComparer.OrdinalIgnoreCase);

var unchanged = source.Add("TWO");
Debug.Assert(ReferenceEquals(source, unchanged));

var withTail = source.Add("four");
// ["one", "two", "three", "four"]

var withHead = source.AddFirst("zero");
// ["zero", "one", "two", "three"]

var inserted = source.Insert(1, "middle");
// ["one", "middle", "two", "three"]
```

`Insert(index, existingEquivalentValue)` validates `index` first, then returns the receiver without
moving or replacing the existing representative.

## Explicit Movement Uses Final Result Positions

Movement retains the stored representative:

```csharp
var values = PersistentOrderedSet<string>.CreateRange(
    ["a", "b", "c", "d"],
    StringComparer.OrdinalIgnoreCase);

var front = values.MoveToFirst("C");
// ["c", "a", "b", "d"]

var back = values.MoveToLast("A");
// ["b", "c", "d", "a"]

var positioned = values.MoveTo(2, "A");
// ["b", "c", "a", "d"] -- 2 is the final index
```

An absent value throws `KeyNotFoundException`. Moving a class already at the requested destination
returns the receiver instance.

## Positional Reads, Removal, And Ranges

```csharp
var values = PersistentOrderedSet<int>.CreateRange([10, 20, 30, 40, 50]);

Console.WriteLine(values[2]);          // 30
Console.WriteLine(values.GetAt(4));    // 50

var withoutThirty = values.Remove(30);
var withoutPositionOne = values.RemoveAt(1);
var middle = values.GetRange(1, 3);    // [20, 30, 40]
var firstTwo = values.Take(2);         // [10, 20]
var afterTwo = values.Drop(2);         // [30, 40, 50]
```

`TryRemove(value, out result)` returns `false` and the receiver through `result` on a miss. `Remove`
also returns the receiver on a miss. Empty `First`, `Last`, `RemoveFirst`, and `RemoveLast` throw
`InvalidOperationException`.

Range validation is overflow-safe. `GetRange(0, Count)`, `Take(Count)`, and `Drop(0)` return the
receiver. Empty ranges retain the receiver's equality comparer.

## Reverse And Stable One-Shot Sort

```csharp
var words = PersistentOrderedSet<string>.CreateRange(["bbb", "a", "cc", "dd"]);

var reversed = words.Reverse();
// ["dd", "cc", "a", "bbb"]

var byLength = words.Sort(Comparer<string>.Create(
    static (left, right) => left.Length.CompareTo(right.Length)));
// ["a", "cc", "dd", "bbb"] -- "cc" remains before "dd"

var later = byLength.Add("z");
// ["a", "cc", "dd", "bbb", "z"] -- the set does not stay sorted
```

Stable sorting uses old order to break ordering-comparer ties. If the stable result is already in the
same order, `Sort` returns the receiver. For a set with more than one element, an exception thrown by
the effective ordering comparer is surfaced as `InvalidOperationException`, with the original
exception in `InnerException`; it cannot modify the source. Counts zero and one do not invoke the
ordering comparer.

## Receiver-Policy Set Algebra

Set-producing algebra has overloads for another `PersistentOrderedSet<T>` and for arbitrary
`IEnumerable<T>` arguments. The receiver's comparer always defines equivalence.

```csharp
var left = PersistentOrderedSet<string>.CreateRange(
    ["Alpha", "beta", "left"],
    StringComparer.OrdinalIgnoreCase);

var right = PersistentOrderedSet<string>.CreateRange(
    ["BETA", "gamma", "ALPHA", "delta"],
    StringComparer.Ordinal);

var union = left.Union(right);
// ["Alpha", "beta", "left", "gamma", "delta"]

var intersection = left.Intersect(right);
// ["Alpha", "beta"] -- receiver order and receiver spellings

var difference = left.Except(right);
// ["left"]

var symmetric = left.SymmetricExcept(right);
// ["left", "gamma", "delta"]
```

The argument is eagerly normalized in enumeration order under the receiver comparer. When several
argument representatives collapse into one receiver class, the first encountered argument
representative wins. Receiver representatives win every class retained from the receiver.

Relations (`IsSubsetOf`, `IsProperSubsetOf`, `IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, and
`SetEquals`) use the same normalization rule. They do not delegate equality to the argument's
comparer or to a default `HashSet<T>`.

## Persistence And Identity

Every update leaves the source usable:

```csharp
var v0 = PersistentOrderedSet<int>.CreateRange([1, 2, 3]);
var v1 = v0.MoveToFirst(3);  // [3, 1, 2]
var v2 = v0.Add(4);          // [1, 2, 3, 4]

// v0 remains [1, 2, 3]; v1 and v2 are independent branches.
```

Separate readers may enumerate any retained version concurrently. A single struct enumerator is not
a cross-thread cursor. An empty concrete enumerator allocates no traversal state. A nonempty concrete
enumerator allocates one shared state object and one initial stack array; deeper traversal may allocate
replacement arrays as the O(log n) stack grows. Pattern-based enumeration avoids boxing, while either
interface path additionally boxes the struct. Copying an in-progress enumerator shares its state, and
advancing divergent copies fails fast, following the underlying finger-tree contract.

## Ordered Multimaps

Use `PersistentOrderedMultimap<TKey, TValue>` when key groups and the distinct values inside each
group each need insertion order:

```csharp
var tags = PersistentOrderedMultimap<string, string>
    .Create(StringComparer.OrdinalIgnoreCase, StringComparer.OrdinalIgnoreCase)
    .Add("article", "csharp")
    .Add("video", "persistent")
    .Add("ARTICLE", "collections");

// Keys: article, video
// Pairs: (article, csharp), (article, collections), (video, persistent)
var articleTags = tags.GetValues("ARTICLE");
var withoutCSharp = tags.Remove("article", "CSHARP");
```

Pair enumeration finishes one group before starting the next. Removing a group's final value
removes that group; re-adding it appends a new group. See the
[complete multimap contract](persistent-ordered-multimap.md).

## Comparer-Defined Null

The type has no `notnull` constraint. With the default comparer, nullable-reference null is an
ordinary set element:

```csharp
var nullable = PersistentOrderedSet<string?>.CreateRange([null, "x", null]);
// [null, "x"]

nullable.TryGetValue(null, out var actual); // true, actual is null
```

As with every hashed collection, callers must not mutate stored state that participates in equality
or hashing while the value is retained.
