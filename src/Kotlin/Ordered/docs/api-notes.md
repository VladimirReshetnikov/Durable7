# Kotlin Persistent Ordered Set API And Behavior

- Created (UTC): 2026-07-15T09:10:22Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Kotlin/JVM API users, maintainers, reviewers, and sibling-port authors
- Scope: neutral persistent ordered set and map

## Ownership And Type

```kotlin
public class PersistentOrderedSet<T> : Iterable<T>
```

`PersistentOrderedMap<K, V>` retains a `PersistentOrderedSet<K>` for explicit key order and a
`PersistentHashMap<K, V>` for payload lookup. Existing-key `set` retains the first representative
and position; movement/reversal/sort share the payload root, while value-only replacement retains
the exact ordered-set object. Range extraction rebuilds precisely the selected payload index, and
`validateStructure()` checks both directions of the composite invariant.

The type is a general-purpose Ordered-owned collection. Production code imports only public
`tools.datastructures.hamt` and `tools.datastructures.fingertree` APIs. It has no Tungsten
dependency, source inclusion, wrapper, subtype, oracle, or semantic coupling.

The type does not override `equals` or `hashCode`. Set equality is explicit through `setEquals`;
ordered sequence equality is explicit through iteration or `toList()`.

## Public Surface

| Area | Members |
| --- | --- |
| Construction | `empty`, `create`, `from`, `createRange` |
| State | `size`, `count`, `isEmpty`, `policy`, `first`, `last` |
| Lookup | `contains`, `tryGetValue`, `getAt`, indexed `get`, `indexOf` |
| Addition | `add`, `addFirst`, `insert` |
| Explicit movement | `moveToFirst`, `moveToLast`, `moveTo` |
| Removal | `remove`, `tryRemove`, `removeAt`, `removeFirst`, `removeLast`, `clear` |
| Range/order | `getRange`, `take`, `drop`, `reverse`, stable one-shot `sort` |
| Set algebra | `union`, `intersect`, `except`, `symmetricExcept` |
| Relations | `isSubsetOf`, `isProperSubsetOf`, `isSupersetOf`, `isProperSupersetOf`, `overlaps`, `setEquals` |
| Enumeration/diagnostics | `iterator`, `toList`, `validateStructure` |

Kotlin's `Iterable<T>` surface already accepts another `PersistentOrderedSet<T>` as an algebra or
relation argument, so separate same-type overloads are unnecessary.

## Hash Policy And Representatives

The exact effective `HashPolicy<T>` object supplied at construction is retained as `policy`. The
shared default empty is reused only for the repository default-policy singleton. A custom-policy
empty retains that exact custom object, including after clear, empty ranges, algebra, and rebuilds.

The policy defines hashes, equality classes, duplicate collapse, argument normalization, algebra,
and relations. Equal values must have equal hashes under the policy. Kotlin generic parameters can
be nullable; null is an ordinary class whenever the policy admits it.

The first representative installed for a class is retained until removal:

- construction discards later equivalent inputs;
- `add`, `addFirst`, and `insert` do not replace or move an existing class;
- movement uses the stored representative rather than the lookup argument;
- range, reverse, and sort rebuild from stored representatives;
- receiver representatives win every surviving receiver class in algebra; and
- the first normalized argument representative wins each argument-only class.

`tryGetValue` returns `OrderedSetLookup(found = true, storedRepresentative)` on a hit. A miss returns
`found = false` and echoes the lookup value, which keeps nullable presence unambiguous.

## Representation And Invariants

Each snapshot owns two persistent indexes:

```text
FingerTree<OrderedEntry<T>, Long?> order    // ascending stamp, stored representative
PersistentHashMap<T, Long> stamps          // equality class -> stamp
```

The measured tree's prefix measure is its last stamp, allowing `indexOf` to perform one CHAMP lookup
and one logarithmic measure search. Sparse `Long` stamps are selected before, after, or strictly
between neighboring stamps. When an endpoint cannot advance by the private stride, or adjacent
stamps have no interior integer, an unpublished rebuild assigns canonical sparse stamps to the
whole candidate result.

Every published snapshot maintains:

1. equal order and membership counts;
2. exactly one stored representative per policy equality class;
3. strictly ascending and unique order stamps;
4. a membership entry with the same stamp for every ordered entry;
5. an ordered entry with the same stamp for every membership entry;
6. referentially identical representatives in both indexes, including null; and
7. the exact receiver policy object across every derived result.

`validateStructure()` checks both directions without foundation internals and returns
`PersistentOrderedSetStatistics(count)` on success. It throws `IllegalStateException` if the
indexes disagree.

## Construction, Addition, And Movement

`from`/`createRange` enumerate the source once in order, normalize under the supplied policy, and
bulk-build both final indexes. Distinct-class count, not raw source count, becomes `size`.

`add` appends, `addFirst` prepends, and `insert(index, item)` inserts before an index in
`0..size`. All are identity no-ops for an existing class. `insert` validates its position before
hashing.

Movement is deliberately separate from addition:

- `moveToFirst(value)` moves a present class to index zero;
- `moveToLast(value)` moves it to `size - 1`; and
- `moveTo(index, value)` interprets `index` as the class's final position after movement.

An absent class raises `OrderedSetMissingValueException`. `moveTo` validates the position before
hashing. Movement to the current position returns the receiver. Every movement retains the stored
representative.

## Removal, Ranges, Reverse, And Sort

`remove` and a missed `tryRemove` return the receiver. `tryRemove` reports the changed snapshot in
`OrderedSetRemoveResult`; stored-representative recovery remains a separate lookup operation.
`removeFirst`, `removeLast`, `first`, and `last` raise `NoSuchElementException` on an empty set.
`clear` returns the receiver when already empty and otherwise a policy-preserving empty.

`getRange(index, count)` accepts `0 <= index <= size`, `count >= 0`, and
`count <= size - index`; the subtraction form avoids addition overflow. A full range returns the
receiver, and an empty range returns a policy-preserving empty. `take` and `drop` accept `0..size`.
Range reconciliation rebuilds the membership index when the kept side is smaller and otherwise
removes discarded edge entries from the receiver index.

`reverse` is an identity no-op for counts zero and one. A changed reverse assigns fresh private
stamps and rebuilds both indexes.

`sort(comparator)` is stable: old positional order breaks comparator ties. Passing null uses natural
ordering with nulls first; non-comparable non-null elements require an explicit comparator. Counts
zero and one do not invoke the comparator. An unchanged stable order returns the receiver. The
comparator is not retained, so subsequent additions append normally.

## Receiver-Policy Algebra And Relations

Every algebra or relation operation eagerly enumerates and normalizes its complete argument under
the receiver's policy before applying shortcuts. This remains true when the argument is another
ordered set with a different policy and when an early element already determines a relation result.
Late enumeration and policy failures are therefore observable rather than hidden.

Result order and representatives are deterministic:

| Operation | Result sequence |
| --- | --- |
| `union` | receiver representatives in receiver order, then first normalized argument-only representatives |
| `intersect` | surviving receiver representatives in receiver order |
| `except` | surviving receiver representatives in receiver order |
| `symmetricExcept` | receiver-only representatives, then normalized argument-only representatives |

The six relation methods count normalized equality classes, not raw duplicates. Algebra results
retain the receiver policy. Union/intersection/difference return the receiver when their ordered
representative sequence is unchanged; symmetric difference returns it for an empty normalized
argument.

## Identity, Persistence, Failure, And Concurrency

Every published snapshot is immutable. Changed operations return a new facade; specified logical
no-ops return the receiver. Earlier versions, retained branches, iterators already obtained from a
version, and independent readers never observe later changes.

Policy, comparator, iterable, validation, or allocation failures occur while constructing
unpublished candidates. A failure cannot mutate or partially publish a successor. Invalid
positions/counts raise `IndexOutOfBoundsException`; endpoint operations on empty input raise
`NoSuchElementException`; absent movement raises `OrderedSetMissingValueException`; and size or
stamp arithmetic overflow raises `ArithmeticException`.

Published snapshots support concurrent read-only use without locks. As usual for hashed
collections, callers must not mutate stored state involved in hashing/equality while it remains in
a set, and callbacks supplied by callers must provide any synchronization their own state needs.

## Complexity

Let `w <= 7` be the 32-bit CHAMP depth, `c` an equal-full-hash collision scan, `n` the receiver
size, and `m` the number of argument inputs.

| Operation family | Bound |
| --- | --- |
| construction | O(m (w + c) + n) |
| membership / stored representative | O(w + c) |
| positional read | O(log n), endpoints O(1) |
| `indexOf` | O(w + c + log n) |
| end insertion | O(w + c) amortized on a linear history; O(w + c + log n) ordinary worst case |
| positional insertion/movement with a gap | O(w + c + log n) |
| relabeling insertion/movement | O(n (w + c)) |
| successful removal | O(w + c + log n); miss O(w + c) |
| ranges | O(log n) plus O(min(kept, removed) (w + c)) |
| reverse | O(n (w + c)) |
| stable sort | O(n log n) comparisons plus O(n (w + c)) for a changed rebuild |
| set-producing algebra | O((n + m) (w + c + log(n + m + 1))) conservative worst case |
| relations | O((n + m) (w + c)) after normalization |
| iteration / list copy | O(n) |

These are capability and asymptotic contracts, not benchmark claims.
