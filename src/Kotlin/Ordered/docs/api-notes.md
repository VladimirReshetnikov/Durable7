# Kotlin Persistent Ordered Collections API And Behavior

- Created (UTC): 2026-07-15T09:10:22Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Kotlin/JVM API users, maintainers, reviewers, and sibling-port authors
- Scope: neutral persistent ordered set, map, and multimap

## Ownership And Type

```kotlin
public class PersistentOrderedSet<T> : Iterable<T>
```

`PersistentOrderedMap<K, V>` retains a `PersistentOrderedSet<K>` for explicit key order and a
`PersistentHashMap<K, V>` for payload lookup. Existing-key `set` retains the first representative
and position; movement/reversal/sort share the payload root, while value-only replacement retains
the exact ordered-set object. Range extraction rebuilds precisely the selected payload index, and
`validateStructure()` checks both directions of the composite invariant.

`PersistentOrderedMultimap<K, V>` retains an ordered map of nonempty ordered value sets under
independent key/value `HashPolicy` objects. First representatives and positions win in both levels;
iteration is grouped, pair addition is idempotent, final-value removal contracts the group, and
`pairCount` is a checked `Long` distinct from `keyCount`.

The type is a general-purpose Ordered-owned collection. Production code imports only public
`durable7.hamt` and `durable7.fingertree` APIs. It has no Tungsten
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
| Cursors | `cursorAt`, `findCursor`; the multimap adds `findGroupCursor` |

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

## Persistent Cursors

`PersistentOrderedSetCursor<T>`, `PersistentOrderedMapCursor<K, V>`, and
`PersistentOrderedMultimapCursor<K, V>` navigate the collections' explicit positional order. All
three are **Profile R snapshot-plus-position semantic checkpoints** in the sense of the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md):
each retains one exact immutable snapshot plus a validated gap, and every edit delegates to the
ordinary persistent operation documented above. None of them inherits the C# rope tier's focused
representation, prepared-measure fragments, snapshot memo, callback ceiling, allocation bound, or
amortized-locality claim. Private sparse stamps never enter the cursor contract.

Each cursor is an ordinary immutable class with a private constructor and no public `copy`, so there
is no uninitialized, default, or consumed state; the only failure a factory can express is `null`.
`snapshot()` returns the exact retained version in O(1) and never consumes the cursor. Every cursor
retains the exact `HashPolicy` objects of its source version, and published cursors are safe for
concurrent read-only use to the same extent as their snapshots.

`OrderedCursorPeek<T>` wraps a present neighbor so a stored `null` stays distinguishable from a
missing one. `OrderedCursorSearch<C>(found, cursor)` reports an equality-class hit while always
carrying a usable cursor, and `OrderedCursorInsert<C>(added, cursor)` reports a non-overwriting
insertion.

### Set and map cursors

```text
collection.cursorAt(position = 0)      // null outside 0..size
collection.findCursor(equalValue|key)  // OrderedCursorSearch
size, count, isAtStart, isAtEnd, position
peekPrevious / peekNext
movePrevious / moveNext / seek
deletePrevious / deleteNext
snapshot
```

The set adds `insert(item)` and `tryInsert(item)`; the map adds `insert(key, item)`,
`tryInsert(key, item)`, and `setNextValue(item)`.

`findCursor` places the gap **before** the stored representative or entry on a hit, and returns
`found = false` with the end (append-position) cursor on a miss. There is no key-sorted lower bound
to infer for an insertion-ordered collection, so the miss form is documented as the append position
rather than an insertion point.

Gap conventions and no-op rules:

- set `insert(item)` adds an absent class at the gap and returns the gap after it. An equivalent
  stored class is an exact no-op that returns **this exact cursor** with its snapshot and position
  unchanged; `tryInsert` derives `added` from that reference comparison. There is no `replaceNext`,
  because replacement conflicts with first-representative retention.
- map `insert(key, item)` is strict and returns `position + 1`. A present key raises
  `IllegalArgumentException` from the underlying map, so use `tryInsert`, which returns
  `added = false` together with a cursor positioned at the stored entry's index — the gap before
  it — over the unchanged snapshot.
- `setNextValue(item)` changes only the next entry's payload, retaining its stored key
  representative, stamp, and position. A payload the CHAMP value policy already considers equivalent
  is a no-op that returns this exact cursor.
- `deletePrevious` removes the neighbor before the gap and moves the gap left; `deleteNext` removes
  the neighbor after the gap and keeps it fixed. Both update the ordered sequence and the hashed
  index atomically.
- there is no key rename; express one as removal plus insertion.

Boundary reads and moves return `null`, and position growth uses `Math.addExact`, raising
`ArithmeticException` before publication. `seek` validates against `0..size` and returns `null`
outside it.

### Navigation hashes in the map cursor

`PersistentOrderedMap` splits storage into a `PersistentOrderedSet<K>` for order and a separate
`PersistentHashMap<K, V>` for payloads, so `getAt(index)` reads the key positionally and then
performs a **CHAMP probe** to attach its payload. `PersistentOrderedMapCursor.peekPrevious` and
`peekNext` therefore invoke the retained `HashPolicy<K>`'s hash and equality on a navigation
operation, costing O(log n + w + c) rather than O(log n).

The repository cursor contract asks that navigation invoke no hash unless one is inherently required
to locate or materialize the requested boundary. Here it is required only because this port's two
indexes do not co-locate the payload with the ordered key, unlike sibling ports that store the value
directly in the ordered sequence. This is recorded as an implementation deviation, not as promised
behavior; a later representation change may remove the probe without altering the public contract.
`PersistentOrderedSetCursor` has no such probe — its peeks read the stamp sequence positionally and
perform no hashing.

### Multimap cursor

`PersistentOrderedMultimapCursor<K, V>` uses a flattened **key-grouped pair** rank. Its `position`
and `pairCount` are `Long`, and the valid range is `0..pairCount`. Factories are
`cursorAt(position = 0L)`, `findCursor(key, value)`, and `findGroupCursor(key)`; the last focuses the
first pair of an equivalent key group and returns the end cursor on a miss. Peeks return
`OrderedCursorPeek<OrderedMultimapEntry<K, V>>`.

`add(key, item)` uses grouped collection semantics — appending into an existing group, otherwise
creating a new group at the end — and then returns the gap after the resulting pair. Insertion is
deliberately **not** forced into the receiver's gap, so the returned position is generally not
`position + 1`. A present equivalent pair is an exact cursor no-op; `tryAdd` reports that through
`added`. `deletePrevious` and `deleteNext` remove the focused pair and contract a group that becomes
empty, so no published version contains an empty group.

The multimap caches no per-group pair-prefix count. `peekPrevious`, `peekNext`, `findCursor`,
`findGroupCursor`, and the index recovery inside `add` each enumerate the grouped iteration from the
start, and that enumeration walks the outer ordered map, which performs one CHAMP probe per key
group. Every one of those operations is therefore **O(p)** in the pair count, with O(k) hash probes
for `k` groups. This is the weakest bound in the Kotlin cursor tier; it is stated plainly rather than
approximated as logarithmic. Movement and `seek` remain O(1) on the rank itself.

### Cursor complexity

Let `w <= 7` be the CHAMP depth, `c` an equal-full-hash collision scan, `n` the entry count, and `p`
the multimap pair count.

| Operation | Set | Map | Multimap |
| --- | --- | --- | --- |
| create, move, `seek`, `snapshot` | O(1) | O(1) | O(1) |
| `peekPrevious`/`peekNext` | O(log n) | O(log n + w + c) | O(p) plus one probe per group |
| `findCursor` | O(w + c + log n) | O(w + c + log n) | O(p) plus one probe per group |
| `insert`/`add` with a stamp gap available | O(w + c + log n) | O(w + c + log n) | O(p) plus the grouped add |
| `insert`/`add` forcing a stamp relabel | O(n (w + c)) | O(n (w + c)) | O(p) plus the grouped add |
| `deletePrevious`/`deleteNext` | O(w + c + log n) | O(w + c + log n) | O(p) plus the grouped removal |
| `setNextValue` | — | O(log n + w + c) | — |

The stamp tier is what makes the ordered positional bound logarithmic rather than linear: order is a
`FingerTree<OrderedEntry<T>, Long?>` whose prefix measure is its last stamp, so a positional read is
one measured-AVL descent and `indexOf` is one CHAMP lookup plus one logarithmic measure search. When
an endpoint cannot advance by the private stride, or adjacent stamps leave no interior integer, the
insertion instead rebuilds one unpublished candidate with canonical stamps at O(n (w + c)). No
relabel amortization crosses retained branches.

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
