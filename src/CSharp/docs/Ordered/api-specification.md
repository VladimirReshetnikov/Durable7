# C# Persistent Ordered Collections API Specification

- Status: Normative current API and behavior specification
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: API consumers, maintainers, reviewers, and sibling-language port authors
- Scope: `PersistentOrderedMap<TKey, TValue>` and `PersistentOrderedSet<T>`

This document is normative for the C# type. The source XML documentation is its concise executable
surface; this specification fixes interactions among ordering, equality policy, representatives,
identity, failure, and complexity.

## Type And Ownership

```csharp
public sealed class PersistentOrderedSet<T> : IReadOnlySet<T>
```

The type is owned by `Tools.DataStructures.Ordered`, which references public HAMT and FingerTree
projects. There is no `notnull` constraint and no dependency on Tungsten production code, tests,
internals, semantics, or a live Tungsten oracle.

The type does not override object equality or hashing. Collection equality is explicit through
`SetEquals`; ordered sequence equality is explicit through enumeration/`SequenceEqual`.

## Persistent Ordered Map

```csharp
public sealed class PersistentOrderedMap<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
```

The map retains independent `KeyComparer` and `ValueComparer` objects. `CreateRange` processes
entries once in order: the first equivalent key fixes the stored key representative and position,
and the last value wins. `SetItem` updates an existing value in place and returns the receiver when
the configured value comparer considers it unchanged. Strict `Add`, `AddFirst`, and `Insert` reject
an equivalent key; `TryAdd` instead returns `false` and the receiver.

`EntryAt` and ordered enumeration read the finger-tree sequence. `ContainsKey`, the key indexer,
`TryGetValue`, and `TryGetKey` use the CHAMP index. `IndexOfKey` combines a hash lookup with a
stamp lower bound. `MoveToFirst`, `MoveToLast`, and `MoveTo` are the only operations that move an
existing entry. Removal is available by key and by position; `TryRemove` returns the stored value.
`GetRange`, `Take`, `Drop`, and `Reverse` preserve the policies and immutable retained versions.

Every entry is one immutable object referenced by both indexes. Published invariants require equal
index counts, strictly increasing sequence stamps, exactly one entry per key class, and reference
identity between the object stored in the CHAMP and at the corresponding sequence position. Sparse
label exhaustion rebuilds one unpublished result with evenly spaced labels; no relabel amortization
is claimed across branches.

Key operations cost O(w + c), positional operations cost O(log n), and operations needing both cost
O(w + c + log n), where `w <= 7` is CHAMP depth and `c` is an equal-hash collision scan. A relabel or
reverse costs O(n (w + c)). Enumeration is O(n) and performs no key hashing.

## Public Surface

| Area | Members |
| --- | --- |
| Construction | `Empty`, `Create(comparer)`, `CreateRange(items, comparer)` |
| State | `Count`, `IsEmpty`, `Comparer`, `First`, `Last` |
| Lookup | `Contains`, `TryGetValue`, `GetAt`, positional indexer, `IndexOf` |
| Addition | `Add`, `AddFirst`, `Insert` |
| Explicit movement | `MoveToFirst`, `MoveToLast`, `MoveTo` |
| Removal | `Remove`, `TryRemove`, `RemoveAt`, `RemoveFirst`, `RemoveLast`, `Clear` |
| Range/order | `GetRange`, `Take`, `Drop`, `Reverse`, stable one-shot `Sort` |
| Set algebra | Same-type and `IEnumerable<T>` overloads of `Union`, `Intersect`, `Except`, `SymmetricExcept` |
| Relations | `IsSubsetOf`, `IsProperSubsetOf`, `IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, `SetEquals` |
| Enumeration | struct `Enumerator`, `GetEnumerator`, `ToArray` |

No sorted-set vocabulary is present. The positional indexer is `this[int index]`; equality lookup is
never overloaded onto it.

## Equality Policy And Representatives

`Comparer` is the exact effective `IEqualityComparer<T>` object supplied at construction. Null means
`EqualityComparer<T>.Default`. The default empty singleton is reused only when the comparer object is
reference-identical to the default comparer; custom-comparer empty results retain the custom object.

The comparer defines hashing, equality classes, duplicate collapse, algebra normalization, and set
relations. Equal values must have equal hashes under that comparer. Nullable values follow the
comparer's policy.

The first representative installed for an equality class is retained until that class is removed.
No first-version operation implicitly replaces a representative:

- later construction duplicates are discarded;
- `Add`, `AddFirst`, and `Insert` are no-ops for an existing class;
- explicit movement moves the stored representative, not the lookup argument;
- range, reversal, and sort rebuild from stored representatives;
- receiver representatives win surviving receiver classes in algebra; and
- argument-only classes take the first representative observed during receiver-policy normalization.

`TryGetValue(equalValue, out actualValue)` returns the stored representative on a hit. On a miss it
returns `false` and echoes `equalValue` through `actualValue`.

## Construction

`Empty` uses the default comparer. `Create(comparer)` creates an empty set retaining the effective
comparer. `CreateRange(items, comparer)`:

1. rejects a null enumerable;
2. enumerates exactly once in source order;
3. stages equivalence classes under the effective comparer;
4. keeps the first representative and position for each class; and
5. bulk-builds the final ordered sequence and stamp index.

The result count is distinct-class count, never raw input count.

## Lookup And Position

`Contains` and `TryGetValue` use the CHAMP membership index. `GetAt(index)` and `this[index]` return
the same stored representative and accept exactly `0 <= index < Count`. `First` and `Last` are the
endpoint representatives and throw `InvalidOperationException` when empty.

`IndexOf(equalValue)` performs one membership/stamp lookup followed by lower-bound search in the
stamp-ordered sequence. It returns `-1` when absent.

## Addition And Movement

`Add(item)` appends an absent class. `AddFirst(item)` prepends an absent class. `Insert(index, item)`
accepts `0 <= index <= Count`, validates the index before hashing, and inserts an absent class before
that position. For an existing class all three retain its representative and position and return the
receiver instance.

Movement is explicit:

- `MoveToFirst(equalValue)` moves an existing class to index zero.
- `MoveToLast(equalValue)` moves an existing class to index `Count - 1`.
- `MoveTo(index, equalValue)` accepts `0 <= index < Count` and interprets `index` as the class's final
  index in the result, not an index before removal.

Movement validates a positional index before hashing where an index is supplied. An absent class
throws `KeyNotFoundException`. The stored representative is retained. Movement to the current
destination returns the receiver.

## Removal And Clear

`Remove(equalValue)` removes a class or returns the receiver when absent. `TryRemove(equalValue, out
result)` returns `false` plus the receiver on a miss; it has no removed-item output because stored
representative recovery is a separate `TryGetValue` operation.

`RemoveAt(index)` accepts `0 <= index < Count`. `RemoveFirst` and `RemoveLast` throw
`InvalidOperationException` on empty input. `Clear` returns the receiver when empty and otherwise a
comparer-preserving empty set.

## Ranges

`GetRange(index, count)` requires:

```text
0 <= index <= Count
0 <= count
count <= Count - index
```

The final inequality is used rather than `index + count <= Count`, avoiding integer overflow. The
full range returns the receiver. An empty range returns a comparer-preserving empty set.

`Take(count)` retains `[0, count)` and `Drop(count)` retains `[count, Count)`. Both accept exactly
`0 <= count <= Count`. `Take(Count)` and `Drop(0)` return the receiver; `Take(0)` and `Drop(Count)`
return comparer-preserving empties.

Range extraction splits the persistent sequence once. The index is rebuilt from kept entries when
kept is the smaller side; otherwise it removes the two discarded edge sequences from the receiver
index. This fixes the O(min(kept, removed)) reconciliation factor.

## Reverse And Sort

`Reverse` returns the receiver for counts zero and one. Otherwise it reverses stored representatives,
assigns fresh private labels, and rebuilds both indexes.

`Sort(orderComparer)` is stable. Null means `Comparer<T>.Default` for ordering only; the set's
equality comparer is unchanged. Old order breaks ordering-comparer ties. Counts zero and one return
the receiver without invoking the ordering comparer. If the stable output sequence is unchanged,
the receiver is returned. Otherwise both indexes are rebuilt. An ordering-comparer exception leaves
the source unchanged. For a set with more than one element, an exception thrown by the effective
ordering comparer is surfaced as `InvalidOperationException`, with the original exception retained
as `InnerException`; this is the `Array.Sort` wrapping contract used by the implementation. Counts
zero and one do not invoke the comparer and therefore do not produce that wrapper.

The result is an ordinary insertion-ordered set. It does not retain or reapply the ordering comparer.

## Set-Producing Algebra

Every operation has same-type and arbitrary-enumerable overloads. Both use the receiver's equality
comparer and eagerly normalize the entire argument in enumeration order before any operation-specific
shortcut. This includes same-type arguments with a different comparer object.

Normalization collapses receiver-equivalent argument values and retains the first encountered
argument representative. It never delegates equality to the argument's own membership probes or a
default BCL set.

Result ordering is:

| Operation | Result sequence |
| --- | --- |
| `Union` | all receiver representatives in receiver order, then argument-only representatives in normalized argument order |
| `Intersect` | surviving receiver representatives in receiver order |
| `Except` | surviving receiver representatives in receiver order |
| `SymmetricExcept` | receiver-only representatives in receiver order, then argument-only representatives in normalized argument order |

Receiver representatives win every class retained from the receiver. Empty and logical-no-op
results retain the receiver comparer; `Union`/`Intersect`/`Except` return the receiver when their
ordered representative sequence is unchanged, and symmetric difference returns the receiver when
the normalized argument is empty.

## Relations

The six `IReadOnlySet<T>` relation methods accept `IEnumerable<T>` and apply the same eager
receiver-policy normalization:

- `IsSubsetOf`
- `IsProperSubsetOf`
- `IsSupersetOf`
- `IsProperSupersetOf`
- `Overlaps`
- `SetEquals`

Duplicate argument values count once under the receiver comparer. Proper relations compare distinct
receiver-policy class counts. Normalization completes before a relation result short-circuits, so a
late argument-enumeration or comparer failure is not hidden by an earlier decisive element.

## Enumeration And Debugging

Pattern-based enumeration returns a public struct `Enumerator` that projects the persistent ordered
sequence without hashing. Constructing the concrete enumerator for an empty set allocates no traversal
state. Constructing it for a nonempty set allocates one shared traversal-state object and one initial
traversal-stack array; deeper traversal can allocate replacement arrays as that O(log n) stack grows.
Pattern-based enumeration does not box the struct. Generic and non-generic interface enumeration
preserves the corresponding empty/nonempty state behavior and additionally boxes the struct.

Value copies of one in-progress nonempty enumerator share its allocated state and fail fast if
advanced divergently; independently obtained enumerators own independent state and are safe for
concurrent read-only use.

`Current` is unspecified before the first successful `MoveNext` and after completion; the default
enumerator yields no values. `IEnumerator.Reset` throws `NotSupportedException`. `Dispose` is a no-op.

`ToArray` returns a fresh ordered representative array. The debugger proxy presents the same
representatives and does not expose private stamps or duplicate index storage.

## Persistence, Concurrency, And Failure

Every published instance is immutable. Changed operations return a new facade; unchanged operations
return the receiver where specified. Earlier versions and retained branches never observe later
updates. Separate threads may read and enumerate any published version without locks.

All comparer and enumerable callbacks execute while constructing unpublished candidates. Failure
cannot mutate or partially publish a successor. Invalid positions throw `ArgumentOutOfRangeException`;
empty endpoint operations throw `InvalidOperationException`; absent movement throws
`KeyNotFoundException`; count or underlying capacity overflow throws `OverflowException`.

## Complexity

Let `w <= 7` be the 32-bit CHAMP depth and `c` an equal-full-hash collision-bucket scan.

| Member family | Bound |
| --- | --- |
| construction | O(m (w + c) + n) with final O(n) sequence/index builds |
| hashed membership / stored representative | O(w + c) |
| positional read | O(log n) worst; O(1 + log min(index + 1, n - index)) amortized; endpoints O(1) |
| `IndexOf` | O(w + c + log n) |
| ordinary end insertion | O(w + c) amortized on a linear history; O(w + c + log n) ordinary worst case |
| positional insertion/movement with a label gap | O(w + c + log n) |
| relabeling insertion/movement | O(n (w + c)) per produced version |
| successful class removal | O(w + c + log n); miss O(w + c) |
| `RemoveAt` | O(w + c + log n) worst, with near-end finger-tree amortization |
| `Clear` | O(1) |
| ranges | O(log n) plus O(min(kept, removed) (w + c)) |
| reverse | O(n (w + c)) |
| stable sort | O(n log n) ordering comparisons plus O(n (w + c)) changed-result rebuild |
| set-producing algebra | O((n + m) (w + c + log(n + m + 1))) conservative worst case |
| relations | O((n + m) (w + c)) after normalization |
| enumeration / array copy | O(n) |

These are capability/asymptotic contracts, not benchmark claims. The private label spacing is not a
public constant, and no amortization claim spans sibling branches that independently cross a relabel.
