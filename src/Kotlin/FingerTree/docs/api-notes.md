# Kotlin FingerTree API Notes

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers implementing and reviewing the Kotlin FingerTree-family port
- Scope: Kotlin naming, contracts, measured-tree representation, complexity, and intentional differences

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, M>` over `MeasurePolicy<T, M>`;
- built-in policies `SizeMeasure<T>`, `IntSumMeasure`, `MaxMeasure<T>`, `MinMeasure<T>`, and
  `ProductMeasure<T, A, B>` with `MeasurePair<A, B>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, and `BrodalOkasakiHeapStatistics`;
- `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>`,
  `PrioritySearchAddResult<K, P, V>`, `PrioritySearchRemoveResult<K, P, V>`,
  `PrioritySearchMinimumView<K, P, V>`, and `PrioritySearchQueueStatistics`;
- `Interval<T>` and `IntervalTree<T>`;
- `PersistentIntervalMap<T, V>` and `IntervalMapEntry<T, V>`;
- `PersistentChunkedBitSet` and `PersistentChunkedBitSetStatistics`;
- `RrbVector<T>` and `RrbVector.Builder<T>`;
- `ZipTreeRankPolicy<T>`, `CanonicalSortedSet<T>`, `CanonicalSetLookup<T>`, and
  `CanonicalSortedSetStatistics`;
- `Monoid<T>`, `DabaLite<T>`, and `DabaLiteStatistics`;
- the append-only level-ancestor seam `IncrementalAncestorArena<T>` with its shipped
  `MyersIncrementalAncestorArena<T>` and `MyersIncrementalAncestorStatistics`, plus the six
  finger-tree members of the seven research-derived collections that build on it and on the measured
  substrate: `AncestralSliceQueue<T>`, `BilateralAncestralDeque<T>`, `ContextualRankSequence<T>`,
  `PersistentDeltaMap<K, V>`, `PersistentRunDeltaVector<T>`, and
  `PersistentMonotoneActionHeap<E, P, A>`, together with their result carriers and the shared
  `ValueEqualityComparer<T>`/`ValueEqualityPolicy<T>`;
- `RangeUpdateAlgebra<T, M, Tag>`, `RangeUpdateSequence<T, M, Tag>`,
  `RangeUpdateSplit<T, M, Tag>`, and `RangeUpdateValidationStatistics`;
- `Rope<T>`, positional `RopeCursor<T>`, nullable-safe `RopeCursorPeek<T>`, `MeasuredRope<T, M>`,
  `MeasuredRopeCursor<T, M>`, `MeasuredRopeCursorSearch<T, M>`, `TextRope`, `TextRopeCursor`,
  `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`;
- sequence cursors `PersistentDequeCursor<T>`, `ReversibleDequeCursor<T>`, `FingerTreeCursor<T, M>`,
  `RrbVectorCursor<T>`, and `RangeUpdateSequenceCursor<T, M, Tag>` with the shared
  `SequenceCursorPeek<T>` and `FingerTreeCursorSearch<T, M>` carriers;
- ordered-search cursors `SortedBagCursor<T>`, `SortedSetCursor<T>`, `SortedMapCursor<K, V>`,
  `CanonicalSortedSetCursor<T>`, `PrioritySearchQueueCursor<K, P, V>`, `IntervalTreeCursor<T>`,
  `PersistentIntervalMapCursor<T, V>`, and `PersistentChunkedBitSetCursor` with the shared
  `OrderedCursorSearch<C>` and `OrderedCursorInsert<C>` carriers.

The Kotlin surface follows Kotlin/JVM conventions:

`PersistentChunkedBitSet` stores only ascending nonzero 64-bit words in the shared measured tree.
Its cached population annotation drives logarithmic membership, inclusive `rank`, and zero-based
`select`; algebra merges sparse word streams over the nonnegative signed-32-bit domain.

- fallible indexed operations return `null`;
- duplicate sorted-map insertion throws `SortedDuplicateKeyException` for `insert` and returns
  `SortedAddResult` for `tryInsert`;
- monoids are runtime objects with identity and associative combine operations; measure policies
  refine `Monoid<M>` with an element-to-measure operation;
- sorted and priority facades accept JVM `Comparator` values where natural ordering is not enough;
- `SortedMap.from(values, comparator)` provides comparator-aware bulk construction and keeps the last supplied
  entry, including its key instance, from every comparator-equal run;
- text offsets are Kotlin `Char` offsets, matching the repository's `Rope<char>` interpretation.

`RangeUpdateSequence` retains a runtime algebra extending `MeasurePolicy`. Its
`compose(newer, older)` direction matches the C# reference exactly, and a wrapper distinguishes an
absent pending action from nullable/default-like/value-distinct identity tags. Indexed edits and
ranges follow the workspace's nullable invalid-result convention; count growth uses checked `Int`
arithmetic. Concatenation requires identical or value-equal algebra policies, empty/no-op paths
retain exact facades, and independent iterators own their immutable traversal state. The full
algebra, lazy-node, complexity, and validation contract is documented in
[range-update-sequence.md](range-update-sequence.md).

`RrbVector` uses `append`/`prepend`, `concat`, `splitAt`, `setItem`, `insertAt`/`insertRange`,
`removeAt`/`removeRange`, and `tryRemoveLast`. Invalid indexed edits and boundaries return `null`,
matching the rest of this workspace. `RrbPop<T>` keeps successful removal distinct from failure even
when a vector stores nullable elements. Equal-value replacement, empty insertion/removal, boundary
splits, and concatenation with empty preserve the receiver or existing root where applicable.

## Positional rope cursor

`Rope.cursor()` creates an immutable cursor at gap zero; `cursorAt(position)` accepts every gap in
`0..size` and returns `null` outside that range. `RopeCursor<T>` is an ordinary non-data class with no
public constructor or default instance, so callers cannot forge a gap through generated `copy`.
It retains the exact source `Rope<T>` reference and its validated gap. `size`, `isEmpty`, `position`,
`isAtStart`, `isAtEnd`, `movePrevious`,
`moveNext`, `seek`, and `snapshot` expose navigation without copying elements. Boundary movement and
invalid seek return `null`; same-position seek returns the same cursor by identity.

`peekPrevious` and `peekNext` return `RopeCursorPeek<T>?`. A null result means that no neighbor exists,
while a non-null wrapper may contain a stored null value, so nullable element types remain
unambiguous. Edits return new cursors and never mutate the receiver. `insert` and `insertRange` leave
the new gap after the inserted values; `deletePrevious` implements backspace and moves left;
`deleteNext` and `replaceNext` keep the gap fixed. Replacement is unconditional, performs no equality
call, and stores the supplied representative. A range source is captured exactly once, and an empty
range returns the same cursor and rope by identity.

Positional `Rope` growth uses checked `Int` arithmetic for prepend, append, point/range insertion, and
concatenation. An unrepresentable result throws `ArithmeticException` before publication; all source
ropes and cursors remain reusable. Cursor creation, movement, seek, and snapshot are O(1). Peeks and
point edits are O(log n) over the measured AVL substrate, and inserting `m` values is O(m + log n).
Immutable cursors are safe for structurally concurrent reads subject to the same caller-owned element
mutability caveat as ropes. This is a semantic checkpoint, not the C# focused cursor representation, and it makes no
O(1)-amortized local-edit claim.

## Measured and text rope cursors

`MeasuredRope.cursor()` and `cursorAt(position)` create an opaque `MeasuredRopeCursor<T, M>` over the
exact source rope. The cursor mirrors the positional gap/edit surface and adds `measureBefore` for
`[0, position)` plus `measureAfter` for `[position, size)`. Those measures are combined in logical
left-to-right order; the contract assumes neither an inverse nor commutativity. A nullable `M` is
supported: a cached null aggregate is a value and is never substituted with `policy.empty`. The same
nullable-safe `RopeCursorPeek<T>` distinguishes a stored null from a missing neighbor. Navigation
retains the exact rope; edits path-copy an independent measured rope; same-position seek and empty
range insertion return the same cursor object.

`cursorByMeasure(predicate)` and `seekByMeasure(predicate)` evaluate a lawful monotone predicate over
absolute prefixes of the whole retained version. `MeasuredRopeCursorSearch<T, M>` always contains a
cursor plus `found`: a hit is the gap immediately before the first element whose inclusive prefix
satisfies the predicate; a miss, including an empty rope, is the end cursor with the whole measure
before it and the identity after it. A predicate already true at the identity therefore selects gap
zero only when an element exists. Cursor search is absolute rather than relative to the receiver's
current gap, and a hit at the current gap preserves cursor identity.

Measured rope prepend, append, point/range insertion, concatenation, and all cursor growth paths use
checked `Int` arithmetic. After an iterable has necessarily been captured once, overflow is rejected
before any measure-policy callback (including compatibility equality) or tree publication. Policy
exceptions during measure reads, searches, or edits propagate while every immutable input remains
exact and retryable. Cursor creation, movement, positional seek, and `snapshot` are O(1). Peeks,
ordered measure reads, point edits, and
absolute measure search are O(log n); insertion of `m` elements is O(m + log n). Unlike C#, Kotlin
does not prepare element-fragment tables: `measureBefore` and `measureAfter` may invoke O(log n)
policy operations on each read. Callers sharing cursors across threads must therefore make reachable
elements, measure values, and policy callbacks safe for those concurrent reads.

`TextRope.cursor()`, `cursorAt`, and `cursorByMeasure` return an opaque `TextRopeCursor` specialized by
`NewlineMeasure`. It delegates the measured cursor but retains the exact `TextRope` facade; navigation
keeps the original facade, and each edit wraps its new measured snapshot in O(1), so `asString`, line
enumeration, and other text helpers remain available without materialization. `lineColumn()` reports
the zero-based line and UTF-16 `Char` column at the gap. The generic
`MeasuredRopeCursor<Char, Int>.lineColumn()` form checks `NewlineMeasure` identity at runtime because
Kotlin's measure-policy type is not part of the cursor's static type.

Both cursor types are snapshot-plus-gap semantic ports. They deliberately do not port C#'s 16/256
focus/carry cursor representation, 2,048-element fragment cache, winner-returning snapshot memo, allocation ceilings,
callback-count gates, or linear-lineage O(1)-amortized local-edit claim.

## Shared cursor contract

Every cursor in this workspace — the rope cursors above and the sequence and ordered-search cursors
below — is a **Profile R root-plus-position (or root-plus-rank) semantic checkpoint** in the sense of
the [repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md).
A cursor retains one exact immutable collection version plus a validated gap, and every edit
delegates to the owning collection's ordinary persistent operation. No cursor in this workspace
inherits the C# rope tier's focused representation, prepared-measure fragments, snapshot memo,
callback ceiling, allocation bound, or amortized-locality claim; the complexity tables below are the
whole promise.

Each cursor is an ordinary immutable class with a private constructor and no public `copy`. There is
no uninitialized, default, moved-from, or disposed state: a cursor either exists and denotes a valid
gap, or a factory returned `null`. Navigation and editing return new cursor values, every retained
cursor stays valid and branchable, and `snapshot()` returns the exact retained collection in O(1)
without consuming the cursor. Cursors retain the exact comparator, measure policy, algebra, rank
policy, or hash policy object of their source version. Published cursors are safe for concurrent
read-only use to the same extent as their source collections and the caller-supplied callbacks they
reach.

The error channel is deliberately split, and the split is by operation kind rather than by type:

| Channel | Operations |
| --- | --- |
| `null` result | every collection cursor factory taking an out-of-range position or rank; `peekPrevious`/`peekNext` at a boundary; `movePrevious`/`moveNext` at a boundary; `seek` outside `0..size`; `deletePrevious`/`deleteNext` at a boundary; `replaceNext`, `setNextValue`, and `setNext` at the end gap |
| Thrown exception | keyed strict insertion (`IllegalArgumentException`, or `SortedDuplicateKeyException` for `SortedMap`); `PersistentChunkedBitSetCursor.add` on a negative bit index; `RangeUpdateSequenceCursor.measurePrevious`/`measureNext`/`applyPrevious`/`applyNext` range preconditions (`IllegalArgumentException` from `require`); position or count growth past `Int.MAX_VALUE`/`Long.MAX_VALUE` (`ArithmeticException` from `Math.addExact`); any comparator, measure, algebra, or hash-policy callback that itself throws |

A miss is never an invalid cursor. Comparator and measure callbacks may throw during a factory or an
edit; the receiver cursor and every retained snapshot remain unchanged and reusable, because each
operation constructs its complete successor before returning it.

## Sequence-family persistent cursors

`PersistentDequeCursor<T>`, `ReversibleDequeCursor<T>`, `RrbVectorCursor<T>`, and
`RangeUpdateSequenceCursor<T, M, Tag>` are positional gap cursors over `0..size`;
`FingerTreeCursor<T, M>` is measure-and-neighbor only. `SequenceCursorPeek<T>` wraps a present
neighbor so a stored `null` element stays distinguishable from a missing one.

Common surface, where the owning collection supports the operation:

```text
collection.cursor()                     // gap zero
collection.cursorAt(position)           // null outside 0..size
size, position, isAtStart, isAtEnd
peekPrevious / peekNext
movePrevious / moveNext / seek
insert / insertRange
deletePrevious / deleteNext / replaceNext
snapshot
```

Gap conventions are uniform. `insert` and `insertRange` leave the gap after the inserted values;
`deletePrevious` is backspace and moves the gap left; `deleteNext` and `replaceNext` address the next
element and keep the gap fixed. `seek` to the current position returns the same cursor by identity,
and an empty `insertRange` returns the same cursor and snapshot by identity. Replacement is
unconditional for the deque and reversible deque — those facades have no element-equality policy and
store the supplied representative without an equality call.

`ReversibleDequeCursor` adds `reverse()`, which maps gap `p` to `size - p` over `value.reverse()`.
The underlying root wrap is O(1), so logical reversal costs the same. Its `replaceNext` is expressed
as `deleteNext` followed by `insert` and `movePrevious`, so it performs several splits and joins
rather than one indexed write; the asymptotic bound is unchanged but the constant is larger than
`PersistentDequeCursor.replaceNext`.

`RrbVectorCursor` overloads `insertRange` for both `Iterable<T>` and `RrbVector<T>`; the vector
overload splices an existing vector while retaining shared subtrees. Its `replaceNext` inherits
`RrbVector.setItem`'s element-equality rule, so an equal value preserves the vector root; the cursor
method still allocates a fresh cursor object, because cursor reference identity is promised only for
same-position `seek` and empty range insertion.

`RangeUpdateSequenceCursor<T, M, Tag>` adds ordered `measureBefore`/`measureAfter` plus the relative
`measurePrevious(count)`, `measureNext(count)`, `applyPrevious(count, tag)`, and
`applyNext(count, tag)` operations. It has no `insertRange`. Its lazy-tag, range-validation, and
measure contracts are owned by [range-update-sequence.md](range-update-sequence.md).

### General measured finger tree

`FingerTreeCursor<T, M>` deliberately exposes **no public `position` and no `size`**, and has no
positional `seek`. A general monoid — a maximum, an interval summary, or an arbitrary application
measure — cannot be read as an index, so the cursor publishes only `isAtStart`, `isAtEnd`,
`measureBefore`, `measureAfter`, neighbor peeks, unit movement, measure seek, point edits, and
`snapshot`. Factories are `FingerTree.cursorAtStart()`, `FingerTree.cursorAtEnd()`, and
`FingerTree.cursorByMeasure(predicate)`.

`cursorByMeasure` and the cursor's own `seekByMeasure` both return `FingerTreeCursorSearch<T, M>`,
which always carries a usable cursor plus `found`. The search is absolute over the whole retained
version rather than relative to the receiver's gap: a hit is the gap immediately before the first
element whose inclusive prefix satisfies the lawful monotone predicate, and a miss — including an
empty tree — is the end gap with `found == false`. A hit at the current gap preserves cursor
identity.

`measureBefore` is the ordered measure of `[0, position)` and `measureAfter` the measure of
`[position, size)`, so `combine(measureBefore, measureAfter)` equals `snapshot().measure()` in that
order; no inverse, commutativity, or identity-as-default is assumed. **Both accessors are total and
both are read-only descents.** They call `PersistentMeasuredTree.measurePrefix`/`measureSuffix`,
which walk one root-to-leaf path, consume a fully covered subtree's cached measure directly, and
allocate no persistent nodes — neither performs a structural split, and neither uses a non-null
assertion. This matters because two shipped policies, `MaxMeasure<T>` and `MinMeasure<T>`, have
`empty = null`: at the start gap `measureBefore` correctly reports that `null` monoid identity
instead of throwing. The boundary is validated at construction, so an out-of-range count can never be
conflated with the identity.

### Complexity

Let `n` be the element count. All bounds treat measure-policy and algebra callbacks as O(1).

| Operation | `PersistentDeque` | `ReversibleDeque` | `FingerTree` | `RrbVector` |
| --- | --- | --- | --- | --- |
| create, move, seek, `snapshot` | O(1) | O(1) | O(1) | O(1) |
| `peekPrevious`/`peekNext` | O(log n) | O(log n) | O(log n) | O(log32 n) |
| `measureBefore`/`measureAfter` | — | — | O(log n) reads, no allocation | — |
| `seekByMeasure` | — | — | O(log n) predicate calls | — |
| `insert` | O(log n) | O(log n) | O(log n) | O(log32(n + 1)) |
| `insertRange` of `m` values | O(m + log n) | O(m + log n) | — | O(m + log32(n + m)) |
| `deletePrevious`/`deleteNext` | O(log n) | O(log n) | O(log n) | O(log32(n + 1)) |
| `replaceNext` | O(log n) | O(log n), several joins | O(log n) | O(log32 n) |
| `reverse` | — | O(1) | — | — |

A linear scan after one seek costs one indexed lookup per step, not O(k + log n): these are
checkpoints, not focused cursors with an open path.

## Ordered-search persistent cursors

The sorted, canonical, priority-search, interval, and sparse-bit families ship root-plus-rank gap
cursors that share one protocol:

```text
entries < boundary | entries >= boundary
                     ^ position; the next entry is the search candidate
```

`position` counts entries before the gap, before-first is zero, and after-last is `size` (or `count`
for the bit set). `OrderedCursorSearch<C>(found, cursor)` reports an exact hit while always carrying
a usable cursor, and `OrderedCursorInsert<C>(added, cursor)` reports a non-overwriting insertion. The
factories are Kotlin extension functions on the owning collection — `cursorAt(rank)`,
`cursorAtLowerBound(...)`, `cursorAtUpperBound(...)`, `findCursor(...)`, and the family-local overlap
factories — plus the cursor-side `seekRank` and `seekNextOverlap`. `cursorAt` returns `null` outside
the valid rank range; every bound factory succeeds and preserves the exact comparator or policy even
on an empty collection.

Two families validate the gap and the rest relocate, and the difference is intentional. The
`SortedBag`, `SortedSet`, `SortedMap`, `CanonicalSortedSet`, `PrioritySearchQueue`, `IntervalTree`,
and `PersistentIntervalMap` cursors compute the key's own ordered location and return a cursor there
regardless of the receiver's current gap; the sibling Patricia and Merkle cursors instead reject a
key whose lower-bound gap is not the current gap. Callers porting between the two tiers must not
assume the stricter contract here.

### Sorted bag, set, and map

`SortedBagCursor<T>` has no `insert` and no `replaceNext`, exactly as the design requires: an
unconstrained insertion inside an equal run, or an in-place replacement that could move an
occurrence, is not obtainable from the ordinary bag API. `add(item)` seeks the **upper bound**, so a
new occurrence follows every existing comparator-equal one and the returned gap is immediately after
it. `deleteNext` and `deletePrevious` remove the exact occurrence at the rank, keeping duplicates
unambiguous. `findCursor(item)` reports `countOf(item) != 0` and returns the lower-bound cursor.

`SortedSetCursor<T>` and `CanonicalSortedSetCursor<T>` expose `add`, adjacent deletion, and no
`replaceNext` — replacing a representative could collide with another equivalence class. `add`
returns the gap **after** the representative in both the miss and the hit case. On a hit the snapshot
is preserved by object identity, because `SortedSet.add` and `CanonicalSortedSet.add` return the
receiver, but the cursor relocates to just after the stored representative rather than preserving the
receiver's gap. `CanonicalSortedSetCursor` retains the exact `ZipTreeRankPolicy<T>` and uses
`policy.comparator` for its exact-search discriminator.

`SortedMapCursor<K, V>` exposes `insert` (throwing `SortedDuplicateKeyException` on a present key),
`tryInsert`, `setItem`, `setNextValue`, and adjacent deletion. Peeks return
`SequenceCursorPeek<SortedMapEntry<K, V>>`. **Key representatives differ by member**: `SortedMap`
follows the C# `SortedDictionary.SetItem` reference and stores the *supplied* key instance for a
comparator-equal key, so `setItem(key, value)` replaces the stored key representative, while
`setNextValue(value)` passes the stored key back and retains it together with the gap. Use
`setNextValue` when representative retention matters. `tryInsert` on a duplicate reports
`added = false` with the lower-bound cursor over the unchanged map.

### Priority-search queue

`PrioritySearchQueueCursor<K, P, V>` navigates **key order only**; priority is cached augmentation,
not a second cursor axis. `cursorAtMinimumPriority()` reads the root's cached winner in O(1) and then
performs an ordinary key seek, returning gap zero for an empty queue — it does not walk a
priority-ordered sequence, which does not exist. `insert` throws `IllegalArgumentException` for an
equivalent key, `tryInsert` reports `added`, `setItem(key, priority, value)` inserts at a miss or
updates the exact hit, and `setNext(priority, value)` retains the stored key representative and the
gap. Every edit rebuilds cached winners through the ordinary AVL constructors.
`enumerateAtMost` remains the winner-pruned query; a cursor scan neither replaces it nor claims its
output-sensitive bound.

### Interval tree and interval map

`IntervalTreeCursor<T>` is ordered by low endpoint. Its peeks return `Interval<T>?` directly, since a
stored interval is never null. `insert` uses the facade's low-bound placement — a newly inserted
equal-low interval precedes the older equal-low run, matching the C# reference — and returns the gap
after it. `deleteNext`/`deletePrevious` remove the exact occurrence at the rank. Endpoint replacement
is deliberately absent: it can move an interval, so express it as removal plus insertion.

`PersistentIntervalMapCursor<T, V>` is ordered by the unique complete `(low, high)` interval key.
`insert` throws for a present complete key and `tryInsert` reports `added`. `setNextValue` reuses the
stored `entry.interval`, so the stored interval representative survives; the map applies **no**
value-comparer no-op, so an equal payload still publishes a new snapshot.

The interval map retains two indexes with different physical orders — a low-endpoint-only
`IntervalTree<T>` for augmented queries and a lexicographic `SortedMap<Interval<T>, V>` for exact
keys and payloads. Every cursor rank, peek, bound, and edit position is taken from the lexicographic
index, so the cursor's declared order is `(low, high)`. `PersistentIntervalMap.findOverlap`
reconciles its augmented result with that declared order by rescanning the equal-low run in the
payload index, so `findOverlap` and `findOverlapCursor` report the same first overlap;
`validateStructure` checks the two indexes agree as an ordered sequence rather than merely as sets.

`seekNextOverlap(probe)` searches strictly after the focused occurrence — from `position + 1`, or
from `size` at the end gap — so a factory's gap-before-hit result cannot rediscover itself. It
returns a usable end cursor on a miss.

**The overlap factories are rank scans, not augmented descents.** `findOverlapCursor`,
`findContainingCursor`, and `seekNextOverlap` walk consecutive ranks from their start position,
stopping once a stored low endpoint exceeds `probe.high`, and each step costs one O(log n) indexed
read. Their honest bound is therefore O(k log n), where `k` is the number of stored intervals whose
low endpoint is at most `probe.high` — up to O(n log n) for a wide probe. `IntervalTree.findOverlap`
remains the `maximumHigh`-pruned O(log n) query; the cursor factories trade that pruning for an exact
rank and a resumable position.

### Persistent chunked bit set

`PersistentChunkedBitSetCursor` traverses present set bits, not a dense Boolean sequence. Its
`position` and `count` are `Long`, matching the population count, and the valid rank range is
`0..count` — one wider than `select`, whose domain is `0 until count`. Factories are `cursorAt(rank)`,
`cursorAtOrAfter(bitIndex)`, and `findCursor(bitIndex)`. A negative `bitIndex` selects the start gap
for search, following the nonthrowing lookup convention, while `add` keeps the collection's
nonnegative validation and throws `IllegalArgumentException`.

`peekPrevious`/`peekNext` return `Int?` bit indexes. `add(bitIndex)` is an exact identity no-op for a
present bit — returning this cursor and this snapshot — and otherwise returns the gap after the new
bit. `deletePrevious`/`deleteNext` clear the exact neighboring bit, and the collection removes a word
that becomes zero, so no publishable version stores a zero word. Set algebra remains a sparse
word-stream operation and is not a cursor primitive.

### Complexity

Let `n` be the entry count, `h` the tree height, `r` an equal-key run length, and `w` the number of
stored nonzero 64-bit words.

| Family | Bound/rank factory | Peek | Insert or add | Delete |
| --- | --- | --- | --- | --- |
| `SortedBag`, `SortedSet`, `SortedMap` | O(log n) comparisons, one guided descent | O(log n) | O(log n) with one extra bound descent for the returned gap | O(log n) |
| `CanonicalSortedSet` | O(h): expected O(log n), O(n) for a degenerate rank policy | O(h) | O(h) | O(h) |
| `PrioritySearchQueue` | O(log n) over cached AVL counts; `cursorAtMinimumPriority` adds an O(1) winner read | O(log n) | O(log n) plus winner repair | O(log n) plus winner repair |
| `IntervalTree` lower bound | O(log n) | O(log n) | O(log n) | O(log n) |
| `IntervalTree` upper bound, `findCursor` | O(log n + r log n): the equal-low run is scanned | — | — | — |
| Overlap factories and `seekNextOverlap` | O(k log n) rank scan, `k` = entries with low ≤ `probe.high` | — | — | — |
| `PersistentIntervalMap` bounds and ranks | O(log n) in the lexicographic index | O(log n) | O(log n) in both indexes | O(log n) in both indexes |
| `PersistentChunkedBitSet` | O(log w) plus O(1) word arithmetic | O(log w) plus up to 63 in-word bit clears | O(log w) | O(log w) |

Unit movement and `seekRank` on an already-built cursor are O(1) on the rank itself; the following
peek pays the table's lookup cost. No family claims O(1)-amortized traversal, because no open path is
retained between steps.

## Representation and complexity

`PersistentMeasuredTree<T, M>` is the shared internal engine: an immutable AVL sequence whose nodes
cache subtree size, height, and the order-sensitive monoidal measure. Construction builds a balanced
tree in O(n). Indexing, path-copying updates, insertion/removal, prefix measurement, measure-guided
location, and splits are O(log n); concatenation joins trees by height and retains unchanged nodes.
The public `sharesStorageWith` diagnostics report shared node identity, and executable tests validate
the AVL bound after generated histories and 100,000-element construction.

`PersistentDeque<T>` and `FingerTree<T, M>` use that engine directly. `MeasuredRope` exposes front/back,
endpoint and positional insertion, range insertion/removal, replacement, slicing, splitting, concatenation,
copying, and compaction over the measured engine; every result retains the supplied measure policy and cached
aggregate. `SortedBag`, `SortedSet`, and
`SortedMap` use `PersistentDeque`; comparator-guided bounds (bag counting, set navigation and
membership, and keyed map lookup) each descend one root-to-leaf path of the sorted tree, so they
cost O(log n) comparisons rather than binary search's O(log² n) indexed probes, and the resulting
edit is also O(log n). `Rope` uses the
same positional tree. `MeasuredRope` caches its supplied measure, and `TextRope` is newline-measured
rather than string-backed. Full enumeration, conversion, sorting/filter rebuilding, and
`PersistentDeque.reverse()` are O(n).

`RrbVector<T>` is a separate 32-way RRB core. Leaves contain 1 through 32 elements. A regular branch
has full-capacity children except possibly its final child and navigates by radix shifts without a
size table; only a relaxed branch stores cumulative child sizes. Lookup and `setItem` are
O(log32 n), and split, insertion, removal, append/prepend, and boundary-spine concatenation are
O(log32(n + m)) with fixed-arity array copying. Exact leaf-boundary splits and full-leaf
concatenations retain original leaves. Counts and cumulative sizes use checked `Int` arithmetic;
the size-derived maximum valid height is `floor((Int.SIZE_BITS - 1) / 5) + 1`, or seven. The extra
level admits the legal boundary-only `minimum height + 1` slack in the top count band. Concatenation performs
boundary-only redistribution and does not promise global minimum occupancy away from the seam;
the adversarial density bounds are test gates, not validator invariants.

`RrbVector.Builder` is append-only between freezes. It stages 32-element tail arrays, transfers a
full tail only after abandoning mutable access to that array, copies a partial tail on freeze, and
adopts an existing vector as an O(1) frozen prefix. `toImmutable()` caches clean snapshots, and
later builder mutation cannot change an earlier vector. Builder iteration freezes once and is
fail-fast if the builder is subsequently modified. The builder is not thread-safe; published
vectors are immutable and safe for concurrent readers.

## Canonical zip-zip sorted set

`CanonicalSortedSet<T>` is an immutable Cartesian binary-search tree. Comparator order is the search
order; a content-derived priority makes topology canonical inside a retained
`ZipTreeRankPolicy<T>`, not globally for the JVM type. `empty(policy)` and `from(values, policy)` are
the idiomatic factories; `create` and `createRange` are aliases for callers porting C# code.

`ZipTreeRankPolicy<T>.create()` uses natural order, the zero-extended 32-bit JVM `hashCode`, and a
fresh unexposed 32-byte key. Passing a `Long` seed derives the key as SHA-256 of ASCII `ZZT2` followed
by the seed's eight big-endian bits. Every factory call creates a distinct policy; unlike the C#
surface, Kotlin does not expose one closed-generic process-wide default object. The explicit-
comparator overload requires a `(T) -> Long` rank hash and rejects its omission. `createKeyed`
requires at least 32 caller-owned bytes, copies them, and offers both natural-order and explicit-
comparator overloads.

For each item, the policy writes the rank hash's 64 bits in big-endian order and computes
HMAC-SHA-256. The first three big-endian 64-bit digest words supply a leading-zero geometric
coordinate, an unsigned secondary coordinate, and a content word for the subtree digest. Heap order
uses geometric rank, unsigned secondary rank, then the comparator-smaller item. Kotlin `Long`
carries the same raw 64-bit seed/hash words as C# `ulong`; negative values are not sign-extended or
re-encoded. The implementation uses only JCA (`SecureRandom`, SHA-256, and HmacSHA256).

The rank hash must be stable and constant on comparator-equivalence classes. Bulk duplicate
elimination and duplicate `add` dynamically reject unequal derived ranks, but no finite check can
prove global coherence. Natural JVM `hashCode` values are not generally cross-language encodings;
cross-process or cross-language reproduction requires the caller to pin equivalent comparator,
rank-hash, key/seed, and item semantics. A public seed permits reproduction but not adversarial rank
secrecy. A protected caller key makes ranks hard to predict only to the extent that the pre-HMAC
64-bit rank hash itself is collision-resistant for the workload. The fixed 64-bit secondary
coordinate is a practical zip-zip-inspired policy and does not claim the paper's compact-rank
metadata theorem.

`from` sorts by comparator plus original sequence index, retains the first representative in each
equivalence class, checks duplicate rank coherence, and freezes a monotone-stack Cartesian build.
`add`, `remove`, and `clear` preserve the receiver for no-ops; `tryGetValue` returns an explicit
`CanonicalSetLookup<T>` so a stored nullable representative remains distinguishable from failure.
`union`, `intersect`, and `except` require the exact same policy object and return canonical results.
`setEquals` is semantic across policy objects under the receiver's comparator; relation methods
likewise deduplicate arbitrary iterables under that comparator.

Every node caches count and height and lazily publishes a non-cryptographic 64-bit `contentHash`.
Within one coherent policy, count or digest inequality proves set inequality; digest equality is
only a filter and is followed by iterative lockstep comparison, with reference-equal subtrees
pruned. Digests from distinct policy objects have no equality meaning. `sharesStorageWith` is an
O(n + m) diagnostic over node identity. `validateStructure` checks strict comparator bounds, rank
reproduction, heap order, node uniqueness/acyclicity, cached metadata, and root metadata, and reports
count, height, largest geometric rank, and repeated geometric/secondary priorities.

For height h, lookup is O(h) time and O(1) space; persistent insertion/removal take O(h) time,
allocate O(h) path nodes, and use O(h) temporary explicit-stack entries. Bulk build is O(n log n)
for sorting plus O(n) Cartesian construction. Validation and the first digest evaluation are O(n);
later root digest reads are O(1). Expected coherent pseudorandom height is O(log n), but colliding or
publicly predictable ranks can force h = n. Every traversal and update is iterative and therefore
remains JVM-stack-safe in that case. Immutable sets and policies are safe for concurrent readers
provided their comparator and rank-hash callbacks are themselves stable and thread-safe.

## DABA Lite sliding-window aggregation

`DabaLite<T>` maintains the FIFO-ordered aggregate of one dynamically sized window without requiring
a commutative operation or an inverse. It takes a runtime `Monoid<T>`; because `MeasurePolicy<E, M>`
refines `Monoid<M>`, a policy such as `IntSumMeasure` can be passed directly when its measure type is
also the window value type:

```kotlin
val window = DabaLite(IntSumMeasure)
window.insert(5)
window.insert(8)
window.insert(13)

val total = window.aggregate // 26
window.evict()                // removes the oldest contribution, 5
val remaining = window.aggregate // 21
```

The implementation follows Tangwongsan, Hirzel, and Schneider's 2021 DABA Lite schedule. Six
cursors remain ordered `F <= L <= R <= A <= B <= E` over one logical queue. `[F, B)` and `[B, E)`
are the front and back; `[L, R)`, `[R, A)`, and `[A, B)` are the incremental-reversal work regions.
Two fields retain the `R`-through-`A` and back products. Every successful insertion or eviction
executes one of four bounded fixups: collapse an exhausted front, begin the next flip, advance the
three equal work cursors, or rewrite one left and one right partial aggregate. There is no loop or
recursive reversal in a window operation.

`insert`, `evict`/`tryEvict`, and a nonempty `aggregate` query invoke `Monoid.combine` at most three,
two, and exactly one times respectively. An empty query obtains `empty` and invokes no combine.
These callback ceilings are independent of window length; full worst-case O(1) time additionally
requires `empty` and `combine` themselves to be O(1). `evict` throws `IllegalStateException` on an
empty window, while `tryEvict` returns `false`.

All mutators give the strong guarantee for monoid callback failures. Publication of counts, cursors,
aggregate fields, slot rewrites, and chunk links is ordered so a throwing `empty` or `combine` leaves
the previous window intact. External side effects performed by a callback cannot be rolled back.
`clear()` is an O(1) reset: an empty clear makes no callback, while a nonempty clear obtains `empty`
once, invokes combine zero times, and swaps in one fresh chunk only after the callback succeeds.

The queue is a doubly linked chain of 64-slot JVM reference arrays. Cursor movement and growth are
worst-case O(1), and no growth copies the window. A successful eviction nulls its retired slot;
crossing a chunk boundary severs both links to the predecessor. `clear()` drops the old chain in
O(1). A state with `n` live positions has queue capacity `n` plus 1 through 127 slack slots; an empty
instance retains one 64-slot chunk. Queue slots are not a stable raw-value sequence because DABA
Lite overwrites values with partial aggregates, so the API deliberately exposes neither peek,
value-returning eviction, nor iteration.

`validateStructure()` invokes neither monoid callback. In O(c) time and space for `c` active chunks,
it checks bidirectional links and acyclicity, cursor reachability/order, `count == E - F`, the DABA
region equations, and the chunk/slack bound. It returns `DabaLiteStatistics` containing count, the
five region lengths, block count, allocated capacity, and slack. Aggregate correctness cannot be
reconstructed from overwritten slots for an arbitrary non-invertible monoid, so executable tests
also compare every operation with an external FIFO model.

`DabaLite` is mutable and unsynchronized. Calls on one instance must not overlap unless the caller
provides external serialization. This concurrency boundary is intentionally different from the
immutable FingerTree and RRB values in the same package.

## Direct priority cores

`BrodalOkasakiHeap<T>` is a direct immutable bootstrapped skew-binomial heap. Its rank-zero global
root caches the minimum and its forest uses the paper's fused primitive-child/embedded-forest
encoding. `minimum`, `insert`, and `meld` are O(1) worst-case; `deleteMinimum` is O(log n). Every
comparator-equivalent element remains a distinct multiset representative. `meld` requires the exact
same comparator object, with natural-order factories sharing the standard singleton. Iteration is
structural, stack-safe, and comparator-free. `validateStructure` checks fused boundaries, skew
ranks, heap order, count, and depth.

`PrioritySearchQueue<K, P, V>` is an immutable winner-cached AVL map. It keeps the first concrete
key in each comparator-equivalence class and updates its priority/payload last-wins. Priority ties
break by key order. Minimum is O(1); keyed operations and minimum deletion are O(log n). The
inclusive `enumerateAtMost` query returns key-ordered results and prunes any subtree whose cached
winner exceeds the threshold, with O(log n + v) work for `v` unpruned visits and O(n) worst case.
The validator checks strict key order, AVL metadata/balance, and every cached winner.

See [priority-core notes](priority-cores.md) for the complete policy, no-op, persistence, complexity,
and JVM comparison-audit contracts.

`PriorityQueue` caches the stable leftmost minimum entry, making peek O(1), enqueue/meld O(log n), and
dequeue O(log n). `IntervalTree` caches last-low and maximum-high summaries: lower-bound insertion and
the first overlap query are O(log n), while overlap enumeration repeatedly prunes unreachable
prefixes. Measure or comparator policies on concatenated values must compare equal. `meld` compares
comparators by identity (JVM `Comparator` has no structural equality): natural-order queues share one
stdlib singleton and always meld, but queues built with custom comparators must be constructed from a
single shared comparator instance.

`ReversibleDeque<T>` retains its specialized orientation-aware balanced storage: `reverse()` wraps or
unwraps a root in O(1), `concat` joins logical roots without materializing either operand, and endpoint
views, indexing, and splits navigate logical orientation. The general engine is a strict measured AVL
sequence rather than the C#/C++ lazy Hinze–Paterson digit spine; consequently its endpoint operations
are O(log n), not amortized O(1). No public facade retains the former flat-`List` representation.

## The seven research-derived collections

Six of the seven live here; the seventh, `PersistentAncestralConnectionForest`, is in
`durable7-hamt`. They are reachable through the ordinary `durable7.fingertree` package alongside the
rest of the family.

`IncrementalAncestorArena<T>` is the append-only level-ancestor seam that `AncestralSliceQueue<T>`
and `BilateralAncestralDeque<T>` share, with `MyersIncrementalAncestorArena<T>` as the shipped
two-link jump-pointer backend and `MyersIncrementalAncestorStatistics` as its counter snapshot. This
is a **faithful** port rather than a reinterpretation: the JVM supplies the mutable state, the
integer handles, and the monitor the managed reference assumes, so the arena keeps its object
identity, its `synchronized` serialization, its odd-block store with square-boundary addressing and
O(sqrt(M)) slack, and its saturating hop counters. Leaf addition is O(1) amortized and an ancestor
query follows O(log M) parent/jump links. The Haskell port is the one that dissolves this seam into
immutable heap structure; Kotlin does not need to. C#'s `GetDepth`/`GetParent`/`GetValue` are spelled
`depthOf`/`parentOf`/`valueAt`, because Kotlin reserves `get`-prefixed names for property accessors.

`AncestralSliceQueue<T>` is a queue whose every version is one interval of a root-to-tail ancestry
path, carried as `(tail, low depth, count)`. Its anchored-empty rule is preserved exactly: an empty
value retains the node immediately before its window, so appending to any empty slice yields exactly
the new value, and a queue drained from the front and one drained from the back keep different
anchors. Suffix slices, `take(size)`, `drop(0)`, and a split at `size` are query-free; a split at `0`
of a non-empty queue is not, because the anchor one level above the window can only be named by an
ancestor query, while on an empty queue the two boundaries coincide and no query happens.

`BilateralAncestralDeque<T>` holds a reversed left interval and a forward right interval, making
`reverse()` an O(1) exchange. The at-most-two-query ceiling for `slice` and `splitAt` holds including
cross-arm cases, removals on the owning arm are query-free, and the four cached endpoints index with
no query. The suites assert exact query profiles rather than ceilings, so a regression that stayed
under the ceiling would still fail.

`ContextualRankSequence<T>` lifts a finite deterministic event machine into an all-start-state
summary monoid. The machine is a retained `ContextualEventMachine<T>` policy object rather than C#'s
static-abstract type parameter, so `stateCount` is instance-level and is read once per lineage.
Each element is stored wrapped with its own cached effect table, which is what keeps rank, select,
and indexed descent free of machine invocations on this node-oriented substrate. **Two bounds are
weaker than the managed reference and are stated as such:** endpoint updates are Θ(s log n) rather
than O(s) amortized, and concatenation is Θ(s·(log m + |h − h′|)) — that is O(s log(n + m)) — rather
than O(s log(min(n, m))). Both follow from the strict measured AVL engine: it has no digits to give
amortized endpoints, and `concatNodes` walks the right operand's left spine unconditionally before
joining by height difference, so the `log(min(n, m))` form fails in both directions here rather than
only for asymmetric operands as in the Rust port.

`PersistentDeltaMap<K, V>` pairs current state with a designated checkpoint and a coalesced exact
net-change index. Every load-bearing rule survives: a policy-equal write is a no-op returning a
version that shares every root, the first effective write captures `before`, repeated writes
coalesce, returning a key to its checkpoint state removes its record, and emptying the change index
snaps the current root back onto the checkpoint root. Presence-safe endpoints are `DeltaMapValue<V>?`
rather than a bare `V?`, so an absent entry and a stored `null` stay distinguishable. Range-restricted
change enumeration uses `SortedMap.getKeyRange`, a genuine ordered restriction, so it never filters
over all changes. One bound is honestly weaker than the reference: `minEntry`/`maxEntry` are
Θ(log N) rather than O(1), because the substrate caches subtree sizes but no extremes.

`PersistentRunDeltaVector<T>` keeps current and checkpoint RRB roots plus an ordered index of the
maximal runs of differing positions. Accepting or reverting one run is a structural splice costing
O(log n) independently of the run's length and performing no value comparisons. Values are boxed in a
private identity-only cell, which is what makes `RrbVector.setItem`'s `l === r || l == r`
short-circuit collapse to the identity test the "a clean position reuses its exact checkpoint cell"
invariant needs; storing raw payloads would let an equal-but-distinct value satisfy that short
circuit and silently retain the wrong object.

`PersistentMonotoneActionHeap<E, P, A>` is the action-tagged sibling of `BrodalOkasakiHeap<T>`,
carrying one immutable pending action per tree and forest spine so a whole-heap priority transform is
O(1) while insert, meld, and minimum stay O(1) and delete-minimum stays O(log n). `compose(outer,
inner)` means `outer` applied after `inner`, so the newer action is the outer one at every pushdown
site, and the `OrderClamp` algebra pins that direction. The old root is exposed before a losing child
is skew-inserted, so a later insertion is never retroactively transformed.

Both checkpoint-differential structures take value equality from one shared retained
`ValueEqualityPolicy<T>` over a `ValueEqualityComparer<in T>`, rather than each shipping its own
abstraction — the same single-policy shape the Rust port uses, because the value relation is what
decides which writes are semantic no-ops and when a recorded change cancels. Canonical policies
(`natural`, `reflexiveIeeeDouble`, `reflexiveIeeeFloat`) are shared singletons, so independently
obtained instances of one canonical kind are compatible, while a `custom` policy carries its own
identity. The floating-point situation differs from every sibling port and is worth stating: Kotlin's
`==` on statically typed `Double`/`Float` is the primitive IEEE comparison and is **not** reflexive
on `NaN`, but the natural policy is generic, so it boxes and reaches `java.lang.Double.equals`, which
compares raw bits and **is** reflexive on `NaN`. Kotlin therefore needs no equivalent of Rust's `Eq`
bound excluding raw floats. The residual difference from .NET is signed zero: the boxed comparison
separates `-0.0` from `+0.0` where `EqualityComparer<double>.Default` does not, and
`reflexiveIeeeDouble`/`reflexiveIeeeFloat` are the canonical .NET-matching relations.

Melding heaps or concatenating sequences whose retained policies differ is gated on the policy being
the same object or value-equal, following `RangeUpdateSequence.concat`'s existing precedent rather
than C#'s reference-identity-only check.
