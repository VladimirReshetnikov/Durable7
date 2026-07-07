# Wolfram Collections Usage Guide

- Created (UTC): 2026-07-07T15:05:40Z
- Repository HEAD: 754f2e474caf2419bfabd5f88565341ddadbf449
- Audience: Consumers of `Tools.DataStructures.Wolfram`
- Scope: Task-oriented examples for `PersistentList<T>` and `PersistentAssociation<TKey, TValue>`

Add a project reference to
[`Tools.DataStructures.Wolfram.csproj`](../../src/Tools.DataStructures.Wolfram/Tools.DataStructures.Wolfram.csproj)
and import the namespace:

```csharp
using Tools.DataStructures.Wolfram;
```

All indexes below are zero-based; the corresponding Wolfram positions are one-based.

## PersistentList

### Construction

```csharp
var empty = PersistentList<int>.Empty;                    // canonical shared empty
var literal = PersistentList.Create(1, 2, 3);             // {1, 2, 3}
var fromSeq = PersistentList.CreateRange(Enumerable.Range(0, 100));
```

### Wolfram-style editing

Every operation returns a new list; the source is never changed.

```csharp
var list = PersistentList.Create("a", "b", "c");

var appended = list.Append("d");            // Append[list, "d"]        O(1) amortized
var prepended = list.Prepend("z");          // Prepend[list, "z"]       O(1) amortized
var inserted = list.Insert(1, "x");         // Insert[list, "x", 2]     O(log n)
var deleted = list.RemoveAt(0);             // Delete[list, 1]          O(log n)
var replaced = list.SetItem(2, "C");        // ReplacePart[list, 3 -> "C"]
var mapped = list.UpdateAt(2, s => s + s);  // MapAt[f, list, 3]
var joined = list.Join(appended);           // Join[list, appended]     O(log min(n1, n2))
var firstTwo = list.Take(2);                // Take[list, 2]            O(log n), shared
var lastTwo = list.TakeLast(2);             // Take[list, -2]
var rest = list.RemoveFirst();              // Rest[list]
var reversed = list.Reverse();              // Reverse[list]            O(n)
var doubled = list.Map(s => s + s);         // Map[f, list]             O(n)
```

The classic Wolfram append-in-a-loop trap is linear here along a linear history:

```csharp
var acc = PersistentList<int>.Empty;
for (var i = 0; i < 1_000_000; i++)
    acc = acc.Append(i);                    // O(n) total, not O(n^2)
```

### Reading

```csharp
var length = list.Count;                    // Length[list]
var head = list.First;                      // First[list]              O(1)
var part = list[1];                         // list[[2]]                O(log n)
var found = list.IndexOf("b");              // FirstPosition (0-based; -1 when absent)
foreach (var item in list) { }              // non-allocating struct enumerator
```

`PersistentList<T>` implements `IReadOnlyList<T>`, so it interoperates with LINQ directly.

## PersistentAssociation

### Construction

```csharp
var assoc = PersistentAssociation.CreateRange([
    KeyValuePair.Create("a", 1),
    KeyValuePair.Create("b", 2),
    KeyValuePair.Create("a", 3),            // duplicate key
]);
// <|a -> 3, b -> 2|>: first-occurrence position, last value (Wolfram rule).

var ci = PersistentAssociation<string, int>.Create(StringComparer.OrdinalIgnoreCase);
```

### Keyed access

```csharp
var value = assoc["a"];                     // assoc["a"]; throws KeyNotFoundException when absent
if (assoc.TryGetValue("b", out var b)) { }  // Lookup-style access; allocation-free O(w + c) probe
var has = assoc.ContainsKey("c");           // KeyExistsQ
```

Wolfram returns `Missing["KeyAbsent", k]` for absent keys; map `TryGetValue`'s `false` to your
engine's missing value.

### Ordering-aware writes

```csharp
var updated = assoc.SetItem("a", 9);        // AssociateTo: in place    <|a -> 9, b -> 2|>
var appended = assoc.Append("a", 9);        // Append: moves to end     <|b -> 2, a -> 9|>
var fronted = assoc.Prepend("b", 9);        // Prepend: moves to front  <|b -> 9, a -> 3|>
var joined = assoc.Join(other);             // Join: keeps this side's positions, takes other's values
var without = assoc.Remove("a");            // KeyDrop
var taken = assoc.KeyTake(["b", "a"]);      // KeyTake: requested order, absents skipped
```

Observably no-op writes return the same instance - use reference equality as a fixed-point
"nothing changed" signal:

```csharp
if (ReferenceEquals(assoc, assoc.SetItem("a", 3))) { /* unchanged */ }
```

### Positional access

```csharp
var pair = assoc.GetAt(0);                  // assoc[[1]] (pair; .Value is the Wolfram part)
var pos = assoc.IndexOfKey("b");            // key -> position, O(w + c + log n)
var firstTwo = assoc.Take(2);               // Take[assoc, 2]
var sliced = assoc.Drop(1);                 // Drop[assoc, 1]
var shorter = assoc.RemoveAt(0);            // Delete[assoc, 1]
var inserted = assoc.Insert(1, "z", 0);     // Insert[assoc, z -> 0, 2]
var rest = assoc.RemoveFirst();             // Rest[assoc]; shares surviving entries
var reversed = assoc.Reverse();             // Reverse[assoc]           O(n)
var byKey = assoc.KeySort();                // KeySort[assoc]           stable
var byValue = assoc.Sort();                 // Sort[assoc] sorts by values, stable
```

### Ordered reads

```csharp
foreach (var pair in assoc) { }             // Normal[assoc]: struct enumerator, no hashing
var keys = assoc.Keys.ToArray();            // Keys[assoc], association order
var values = assoc.Values.ToArray();        // Values[assoc], association order
var normal = assoc.ToArray();               // KeyValuePair<TKey, TValue>[]
```

`PersistentAssociation<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`; its
enumeration order is the association order, unlike the unordered HAMT it is built on.

### Persistence

```csharp
var v1 = PersistentAssociation<string, int>.Empty.SetItem("a", 1);
var v2 = v1.SetItem("b", 2);
var v3 = v1.Append("a", 9);                 // branch from v1
// v1, v2, v3 all remain fully usable and share structure.
```

## Choosing Between This Library And The Substrates

Use `PersistentHashMap` directly when you need a keyed map and do not care about enumeration
order. Use `FingerTreeDeque`/`Rope` directly when you need a sequence and not the Wolfram
operation vocabulary. Reach for this library when insertion order is part of the contract
(association) or when you want the Wolfram operation surface and its ordering rules test-locked
for you (list and association).
