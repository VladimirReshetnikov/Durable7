# Wolfram Collections API Specification

- Created (UTC): 2026-07-07T15:05:40Z
- Repository HEAD: 754f2e474caf2419bfabd5f88565341ddadbf449
- Audience: Maintainers, reviewers, and porters of `Tools.DataStructures.Wolfram`
- Scope: Public contracts, ordering rules, complexity, and allocation behavior

The XML documentation in
[`PersistentList.cs`](../../src/Tools.DataStructures.Wolfram/PersistentList.cs) and
[`PersistentAssociation.cs`](../../src/Tools.DataStructures.Wolfram/PersistentAssociation.cs) is
the member-level source of truth. This document specifies the cross-cutting contracts and the
tables ports must reproduce.

Complexity notation: `n` is the collection size; `w ≤ 7` is the HAMT trie depth and `c` the
equal-hash collision-bucket scan; finger-tree bounds are the substrate's documented amortized
bounds and hold under branching persistence via memoized suspensions.

## Shared Contracts

- **Immutability and persistence.** No operation observably changes an existing instance. Every
  edit returns a new version sharing structure with its source; any retained version remains
  fully usable under branching histories.
- **Zero-based indexing.** All positional parameters are .NET zero-based; the documented Wolfram
  correspondences are one-based.
- **Canonical empties.** `Empty` is a single shared instance per constructed type (for the
  association, per default key comparer); operations that produce an empty result with the
  default comparer return it.
- **No-op identity.** Operations whose result is observably identical to the receiver return the
  receiver instance: full-range slices, empty-argument bulk edits, `Remove` of an absent key,
  `SetItem` with a default-equal value, `Append`/`Prepend` of an already-terminal equal entry,
  trivial `Reverse`. Test-locked.
- **Thread safety.** Instances are safe for concurrent readers without synchronization. Struct
  enumerators must not be shared across threads.
- **Null policy.** Sequence/function arguments are null-checked (`ArgumentNullException`).
  Association keys follow the HAMT family contract: `TKey : notnull`, nullability enforced by
  annotations rather than runtime checks.

## PersistentList&lt;T&gt;

`sealed class PersistentList<T> : IReadOnlyList<T>`, a facade over `FingerTreeDeque<T>`.

| Member | Wolfram | Complexity | Notes |
| --- | --- | --- | --- |
| `Empty`, `Count`, `IsEmpty` | - / `Length` / - | O(1) | |
| `First`, `Last` | `First`, `Last` | O(1) worst-case | `InvalidOperationException` when empty |
| `this[int]` | `Part` | O(log min(i + 1, n - i)) | |
| `Create(ReadOnlySpan<T>)`, `CreateRange` | list literal | O(n) | `CreateRange` returns an existing `PersistentList<T>` argument unchanged |
| `Append`, `Prepend` | `Append`, `Prepend` | O(1) amortized, O(log n) worst | |
| `AddRange` | `Join` | O(k); O(log min) for a persistent list argument | |
| `Join` | `Join` | O(log min(n1, n2)) | empty operand returns the other instance |
| `Insert`, `InsertRange` | `Insert` | O(log n), O(log n + k) | index `Count` appends |
| `RemoveAt`, `RemoveRange` | `Delete` | O(log n) | |
| `RemoveFirst`, `RemoveLast` | `Rest`, `Most` | O(1) amortized | `InvalidOperationException` when empty |
| `SetItem`, `UpdateAt` | `ReplacePart`, `MapAt` | O(log n) | `UpdateAt` invokes the updater exactly once |
| `GetRange`, `Take`, `TakeLast`, `Drop`, `DropLast` | `Part` span, `Take`, `Drop` | O(log n) | results share structure |
| `SplitAt` | - | O(log n) | both halves share structure |
| `Reverse` | `Reverse` | O(n) | deliberate: no direction dispatch on other operations |
| `Map<TResult>` | `Map` | O(n) | selector called once per element, front to back |
| `IndexOf`, `Contains` | `FirstPosition`, `MemberQ` | O(n) | optional `IEqualityComparer<T>` |
| `ToArray`, `CopyTo` | `Normal`-ish | O(n) | |
| `GetEnumerator` | - | O(1) amortized per step | non-allocating struct enumerator (the substrate's) |

## PersistentAssociation&lt;TKey, TValue&gt;

`sealed class PersistentAssociation<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
where TKey : notnull`.

Composition: `PersistentHashMap<TKey, (long Stamp, TValue Value)>` keyed index plus
`FingerTreeDeque<(long Stamp, TKey Key, TValue Value)>` order sequence sorted by strictly
ascending stamp; values are stored in both. Stamps are gapped order-maintenance labels
(gap `G = 2^20`): append `last + G`, prepend `first - G`, positional insert the neighbor
midpoint, wholesale relabel (`O(n (w + log n))`) when the local gap - or, after ~2^43 end
insertions, the `long` label range - is exhausted. Relabel cost is per produced version and is
not amortized under branching persistence. Only `Insert` (and astronomically remote end-insertion
overflow) can relabel.

### Invariants

1. The entry sequence and the keyed index always contain the same key set with identical stamps
   and values (`entries.Count == index.Count`).
2. Entry stamps strictly ascend in sequence order; association order *is* sequence order.
3. A key's position is recovered from its stamp via sorted search in O(log n); positions and
   ranks are never stored in the hash side.

### Ordering rules (kernel-verified fidelity spec)

| # | Rule |
| --- | --- |
| 1 | Construction (`CreateRange`, `SetItems`): a duplicate key keeps its first occurrence's position with its last value |
| 2 | `Append`/`Prepend` on an existing key remove the old entry and re-add at the end/front (with the supplied key instance) |
| 3 | `SetItem` updates an existing key in place (keeping the stored key instance); new keys append at the end |
| 4 | `GetAt`, `Take`, `Drop`, `GetRange`, `RemoveAt`, `RemoveFirst`, `RemoveLast`, `Reverse` act on association order |
| 5 | `Insert` of an existing key wins position and value; the index is interpreted against the entries before the old occurrence is removed |
| 6 | `Join(other)` = `SetItems(other)`: existing keys keep positions and take the argument's values; new keys append in argument order; key equality uses this association's comparer |
| 7 | `Sort` orders by values, `KeySort` by keys; both stable (equal elements keep association order); results are ordinary associations |
| 8 | `KeyTake` follows requested key order, skips absents, first request wins for duplicates; `RemoveRange` (KeyDrop) preserves surviving order |

### Member table

| Member | Wolfram | Complexity | Notes |
| --- | --- | --- | --- |
| `Empty`, `Create(comparer)` | `<\|\|>` | O(1) | default comparer collapses to shared `Empty` |
| `CreateRange(pairs, comparer)` | association literal | O(n (w + log n)) | rule 1 |
| `Count`, `IsEmpty`, `Comparer` | `Length` / - / - | O(1) | |
| `this[TKey]`, `TryGetValue` | `assoc[k]`, `Lookup` | O(w + c), allocation-free | indexer throws `KeyNotFoundException`; Wolfram would return `Missing["KeyAbsent", k]` |
| `ContainsKey`, `TryGetKey` | `KeyExistsQ` / - | O(w + c) | `TryGetKey` recovers the stored key instance |
| `First`, `Last` | `First`, `Last` (values there) | O(1) worst-case | return pairs; `InvalidOperationException` when empty |
| `GetAt` | `assoc[[n]]` (value there) | O(log min(i + 1, n - i)) | returns the pair |
| `IndexOfKey` | - | O(w + c + log n) | -1 when absent |
| `Keys`, `Values`, enumeration, `ToArray` | `Keys`, `Values`, `Normal` | O(n) | association order, no hashing; struct enumerator |
| `SetItem` | `AssociateTo`, `a[k] = v` | O(w + c + log n); new-key append O(w + c) amortized | rule 3; no-op identity on default-equal value |
| `SetItems`, `Join` | `Join` | O(m (w + log n)) | rules 1/6; small-into-large never rebuilds the large side |
| `Append`, `Prepend` | `Append`, `Prepend` | O(w + c + log n) | rule 2 |
| `Insert` | `Insert` | O(w + c + log n); O(n (w + log n)) on relabel | rule 5 |
| `Remove`, `TryRemove`, `RemoveRange` | `KeyDrop` | O(w + c + log n) per key | absent keys are no-ops |
| `KeyTake` | `KeyTake` | O(m (w + log m)) | rule 8 |
| `RemoveAt`, `RemoveFirst`, `RemoveLast` | `Delete`, `Rest`, `Most` | O(w + c + log n) | surviving entries shared: `Rest`-recursion is linearithmic |
| `GetRange`, `Take`, `Drop` | `Part` span, `Take`, `Drop` | O(log n + min(kept, removed) (w + c)) | index reconciled from the smaller side |
| `Reverse` | `Reverse` | O(n (w + log n)) | fresh labels; no reversal bit by design |
| `KeySort(comparer?)`, `Sort(comparer?)` | `KeySort`, `Sort` | O(n (w + log n)) | rule 7; stable via stamp tiebreak |

### Divergences from Wolfram, by design

- Absent-key reads throw or return `false` instead of producing `Missing[...]`.
- `First`/`Last`/`GetAt` return key/value pairs; Wolfram's return the bare values.
- No `RuleDelayed` distinction: model it in `TValue` if needed.
- Key equality is the factory comparer, not structural `SameQ`; an engine supplies a
  structural-hash comparer to recover Wolfram key semantics (including normalizing machine-real
  keys by numeric value rather than stored text, per the design study's correction).
