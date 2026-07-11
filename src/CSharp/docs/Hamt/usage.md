# HAMT C# Usage Guide

- Created (UTC): 2026-07-02T20:12:28Z
- Repository HEAD: f448af2c7626e4f3b06f74701c3f9f9383db7446
- Audience: .NET consumers and maintainers using `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>`
- Scope: Namespace, construction, persistent update patterns, comparer behavior, iteration, set algebra, and concurrency

This guide is the practical companion to the [C# API specification](api-specification.md). It shows
the common usage patterns for the canonical C# HAMT implementation; the API specification remains
the normative contract for complexity, allocation behavior, and edge cases.

## Namespace And Build

The public types live in the `Tools.DataStructures.Hamt` namespace:

```csharp
using Tools.DataStructures.Hamt;
```

Validate the workspace through the solution:

```powershell
.\test.ps1
```

The repository currently builds the library from source under
[`src/Tools.DataStructures.Hamt`](../../src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj).

## Persistent Values

Maps and sets are immutable persistent values. Update-shaped members return a new version and leave
the source version usable:

```csharp
var empty = PersistentHashMap<int, string>.Empty;
var one = empty.SetItem(1, "one");
var two = one.SetItem(2, "two");
var replaced = two.SetItem(1, "uno");

// empty has no keys.
// one has { 1 -> "one" }.
// two has { 1 -> "one", 2 -> "two" }.
// replaced has { 1 -> "uno", 2 -> "two" }.
```

No-op updates return the current instance. Examples include replacing a value with one that compares
equal under `EqualityComparer<TValue>.Default`, removing an absent key, clearing an empty map, and
adding an already present item to a set.

## Persistent Map

Use `SetItem` for add-or-replace:

```csharp
var map = PersistentHashMap<int, string>.Empty
    .SetItem(1, "one")
    .SetItem(2, "two");

if (map.TryGetValue(1, out var value))
{
    // value == "one"
}

var twoValue = map[2];
```

Use `Add` when duplicates are programmer errors, and `TryAdd` when duplicate rejection is part of
normal control flow:

```csharp
var unique = PersistentHashMap<int, string>.Empty.Add(1, "one");

if (!unique.TryAdd(1, "duplicate", out var same))
{
    // same is the original map.
}

if (unique.TryAdd(2, "two", out var withTwo))
{
    // withTwo has both keys; unique is unchanged.
}
```

`TryRemove` reports whether the key existed and returns the removed value:

```csharp
if (withTwo.TryRemove(1, out var withoutOne, out var removedValue))
{
    // removedValue == "one"
    // withoutOne no longer contains key 1.
}
```

Use `CreateRange` to build from scratch and `SetItems` to add or replace a sequence on an existing
map. Both apply entries in enumeration order with last-wins value semantics for equivalent keys:

```csharp
var built = PersistentHashMap<int, string>.CreateRange(new[]
{
    KeyValuePair.Create(1, "one"),
    KeyValuePair.Create(2, "two"),
    KeyValuePair.Create(1, "uno"),
});
```

## Comparers And Equivalent Keys

The default factories use `EqualityComparer<TKey>.Default`. Pass an `IEqualityComparer<TKey>` when
keys need custom hash/equality semantics:

```csharp
var map = PersistentHashMap<string, int>
    .Create(StringComparer.OrdinalIgnoreCase)
    .SetItem("Alpha", 1)
    .SetItem("beta", 2);

var found = map.TryGetValue("ALPHA", out var alphaValue);
```

Equivalent keys must produce equal hash codes. Comparer behavior should remain stable for the
lifetime of every map version created with that comparer.

When an update uses an equivalent key, the map keeps the originally stored key object. Use
`TryGetKey` to recover it:

```csharp
if (map.TryGetKey("alpha", out var actualKey))
{
    // actualKey is the stored equivalent key, for example "Alpha".
}
```

The same rule applies to `CreateRange`: later equivalent keys replace the value, while the first
stored key object remains the enumerated key. If the replacement value compares equal under
`EqualityComparer<TValue>.Default`, the earlier stored value object is retained as well.

Null-key behavior is entirely comparer-defined. The default comparer supports null for nullable
reference keys; a custom comparer must do whatever its own contract promises.

## Iteration

`PersistentHashMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`, and its `Keys` and
`Values` views follow pair enumeration order:

```csharp
foreach (var (key, value) in map)
{
    // Inspect key and value.
}

foreach (var key in map.Keys)
{
    // Inspect keys in the same trie order as pair enumeration.
}
```

Enumeration follows the HAMT bitmap/collision shape. It is stable for an unchanged version, but it
is not insertion order or sorted order. The public struct enumerators are copy-safe and keep
traversal state inline; `foreach` through the concrete collection avoids boxing the enumerator.

## Persistent Set

`PersistentHashSet<T>` is the set facade over the map core and implements `IReadOnlySet<T>`:

```csharp
var empty = PersistentHashSet<int>.Empty;
var one = empty.Add(1);
var two = one.Add(2);
var removed = two.Remove(1);

var hasTwo = removed.Contains(2);
```

Use try-pattern operations when membership changes matter:

```csharp
if (!one.TryAdd(1, out var duplicate))
{
    // duplicate is one.
}

if (one.TryAdd(2, out var withTwo) &&
    withTwo.TryRemove(1, out var withoutOne))
{
    // withoutOne contains 2 and no longer contains 1.
}
```

Pass a comparer through `Create` or `CreateRange` for custom item equality. Use `TryGetValue` to
recover the originally stored item object when an equivalent item is present:

```csharp
var set = PersistentHashSet<string>
    .CreateRange(new[] { "Alpha", "beta" }, StringComparer.OrdinalIgnoreCase);

if (set.TryGetValue("ALPHA", out var actualValue))
{
    // actualValue is "Alpha".
}
```

## Set Algebra

Set algebra methods accept `IEnumerable<T>` inputs and interpret membership through the receiver's
comparer:

```csharp
var left = PersistentHashSet<int>.CreateRange(new[] { 1, 2, 3 });
var right = new[] { 3, 4 };

var union = left.Union(right);
var intersection = left.Intersect(right);
var difference = left.Except(right);
var symmetric = left.SymmetricExcept(right);

var subset = left.IsSubsetOf(new[] { 1, 2, 3, 4 });
var overlaps = left.Overlaps(right);
var equal = left.SetEquals(new[] { 3, 2, 1 });
```

`Intersect`, `SymmetricExcept`, `IsSubsetOf`, `IsProperSubsetOf`, `IsProperSupersetOf`, and
`SetEquals` materialize the input into a temporary `HashSet<T>` using the receiver's comparer.
`IsSupersetOf` and `Overlaps` stream their inputs and can exit early.

## Concurrency And Lifetime

Map and set instances are immutable after construction. Independent snapshots can be read and
enumerated concurrently while other threads compute new versions from the same snapshot. Normal .NET
variable ownership still applies: do not race on the same mutable variable while another thread
reassigns it to a newer version.

For a shared mutable map, use `ConcurrentHashTrie<TKey, TValue>`. Its updates are atomic and its
snapshot is an O(1) persistent value:

```csharp
var live = new ConcurrentHashTrie<string, int>(StringComparer.OrdinalIgnoreCase);
live.AddOrUpdate("requests", _ => 1, (_, count) => count + 1);

PersistentHashMap<string, int> published = live.Snapshot();
live["requests"] = 100;

// The snapshot remains stable at 1 while the live trie advances.
Console.WriteLine(published["REQUESTS"]);
```

Factories passed to `GetOrAdd` and `AddOrUpdate` may be invoked more than once under contention;
keep them repeatable and free of non-repeatable side effects.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Immutable unordered key/value collection | `PersistentHashMap<TKey, TValue>` |
| Add-or-replace update | `SetItem` or `SetItems` |
| Duplicate-rejecting insert | `Add` or `TryAdd` |
| Stored equivalent key recovery | `TryGetKey` |
| Immutable unordered value set | `PersistentHashSet<T>` |
| Stored equivalent item recovery | `TryGetValue` |
| Union/intersection/difference | `Union`, `Intersect`, `Except`, `SymmetricExcept` |
| Custom value semantics | `Create(comparer)` or `CreateRange(items, comparer)` |
| Shared mutable map with O(1) immutable snapshots | `ConcurrentHashTrie<TKey, TValue>` |

For cross-language contract alignment, see the repository
[porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
