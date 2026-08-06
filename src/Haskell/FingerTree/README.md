# Haskell FingerTree

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell finger-tree family port
- Scope: `durable7-fingertree` package

This package ports the repository finger-tree family to Haskell. It includes a general measured
finger tree, a size-and-rightmost-leaf-measured deque, a reversible deque, a sorted bag/set/map
family on the measured core, a stable meldable priority queue, a worst-case-optimal Brodal-Okasaki heap, a keyed
priority-search queue, interval tree and payload-bearing interval-map helpers, positional ropes, measured ropes, text-rope navigation
helpers, immutable positional/measured/text rope cursors, a persistent RRB vector, a policy-canonical
zip-zip-tree sorted set, a persistent lazy range-update sequence, and
`PersistentChunkedBitSet`, a measured sparse sequence of nonzero 64-bit words with logarithmic
membership, inclusive rank/select, and sparse linear algebra. It also carries six of the seven
research-derived collections described below, together with the append-only level-ancestor seam two
of them share.

`IntervalMap a v` composes the augmented low-sorted `IntervalTree` with an ordered exact-key map.
It provides unique closed-interval keys, strict addition, payload replacement, exact and indexed
lookup, pruned first/all/count overlap queries, snapshot-preserving removal, and cross-index
validation.

`RangeUpdateSequence` is a deterministic implicit-key AVL tree with cached count, height, and
ordered logical measure. A retained `RangeUpdateAlgebra` record defines element measurement,
ordered measure combination, identity recognition, directional `compose newer older`, and tag
actions on elements and cached subtree measures. Whole-sequence nonidentity updates replace one
root in O(1); indexed edits, splits, joins, proper range updates, and proper range queries copy or
visit O(log n) boundary nodes. Reads carry pending tags without publishing pushed trees, while
structural descent pushes tags immutably before rotations. Empty ranges and semantically identity
tags retain the source root. As with `MeasuredRope`, operands passed to `append` must retain
extensionally identical policy functions because Haskell functions have no decidable equality.
The port uses `Maybe` for invalid indices and ranges and the package's checked-`Int` pure exception
for count overflow.

## The seven research-derived collections

`AncestralSliceQueue`, `BilateralAncestralDeque`, `ContextualRankSequence`, `PersistentDeltaMap`,
`PersistentRunDeltaVector`, and `PersistentMonotoneActionHeap` are the finger-tree members of the
seven collections that originated as scoped design studies; the seventh,
`PersistentAncestralConnectionForest`, lives in `durable7-hamt`. Each is reachable only through its
own module: several of them export a `ValidationStatistics` type, so re-exporting them from the
`Durable7.FingerTree` umbrella would be ambiguous, the same reason `RangeUpdateSequence` is not
re-exported there either.

`IncrementalAncestor` is the append-only level-ancestor seam the first two share, and it is where
this port diverges most from the managed and native ones. Those own a mutable arena of integer
handles behind a lock, injected through a `Create(arena)` factory and observed through arena
statistics. Here a `Node a` *is* its own handle: the forest is ordinary immutable heap structure, so
there is no arena object, no lock, no handle-recycling question, no backend-injection seam, and no
way to present a node to the wrong forest. The two managed factories collapse into one `empty`, and
leaf addition improves from O(1) amortized to **O(1) worst case** because there is no block store to
grow. Ancestor queries still follow the Myers two-link scheme in O(log M) parent/jump hops. The one
capability that genuinely does not survive is the arena's retained query counters, which have no
pure analogue; `ancestorAtDepthHops` returns the hop count to the caller that caused it instead, and
`BilateralAncestralDeque` builds its `QueryCost`-returning `indexCost`/`sliceCost`/`splitAtCost`/
`removeFirstCost`/`removeLastCost` siblings on top of that, which is what makes the query ceilings
testable rather than merely asserted.

`AncestralSliceQueue a` is the triple `(tail node, low depth, count)` naming an interval of one
root-to-tail path. Its anchored-empty rule is preserved exactly: an empty value retains the node
immediately before its window, so appending to any empty slice yields exactly the new value, and a
queue drained from the front and one drained from the back keep different anchors. Suffix slices,
`take count`, `drop 0`, and a split at `count` are query-free; a split at `0` is not, because the
anchor one level above the window can only be named by an ancestor query.

`BilateralAncestralDeque a` holds a reversed left interval and a forward right interval, which makes
`reverse` an O(1) exchange. The at-most-two-query ceiling for `slice` and `splitAt` holds including
cross-arm cases, removals on the owning arm are query-free, and the four cached endpoints index
without a query. Its `sharesRootWith` compares the retained interval *nodes* rather than the outer
handle: the handle is a small all-strict record, so GHC's CPR analysis unboxes and rebuilds it
around unchanged endpoints and a facade-level `StableName` comparison fails under `-O2`. Any future
structure whose sharing diagnostic compares a small strict facade record rather than an inner node
inherits that hazard.

`ContextualRankSequence element` lifts a finite deterministic event machine into an all-start-state
summary monoid over the package's measured core. Because that core is a genuine Hinze-Paterson tree
with digits and a lazy middle, this port keeps the reference's *edit* bounds exactly — O(1)
whole-sequence evaluation from the cached root summary, O(s log n) prefix evaluation, indexing,
rank, select and edits, O(s) amortized endpoint updates, and O(s log(min(n, m))) concatenation —
rather than the weaker endpoint and concatenation bounds the Rust port has to state for its
join-tree substrate. Enumeration is the one figure that does not carry over: it is Θ(s·n) here
rather than the reference's O(n), because the substrate's left-view fold rebuilds a spine node per
step and each rebuild reforces one O(s) effect-table composition, where the reference instead walks
a traversal stack and combines no measures. The
machine is a retained `ContextualEventMachine` record instead of a static-abstract interface, so
`stateCount` is instance-level; `append` cannot compare two records for identity, so it checks the
one thing that is observable and would silently corrupt summaries — a declared state-count mismatch
— and documents operand compatibility as a caller precondition. Event counts are `Integer`, so the
reference's overflow contract is vacuous, while its negative-event-count rejection is kept and the
element count remains a checked `Int`. Query results are forced before return.

`PersistentDeltaMap key value` pairs current state with a designated checkpoint and a coalesced
exact net-change index. Every load-bearing rule survives: a policy-equal write is a no-op returning a
version that shares every root, the first effective write captures `before`, repeated writes
coalesce, returning a key to its checkpoint state removes its record, and emptying the change index
snaps the current root back onto the checkpoint root. `Maybe` replaces the presence-safe
`DeltaMapValue` wrapper, which the reference needs only because `null` is a valid value. Enumeration
is a lazy list, which matches the reference's lazy `GetChanges()` more closely than the Rust port's
eager materialization. Range-restricted enumeration goes through `SortedMap.keyRange`, an ordered
inclusive restriction built from two antitone boundary descents, so `changesInRange` and
`entriesInRange` cost O(log(k + 1)) to restrict and Θ(1) amortized per yielded record — the
reference's bound, not a weakened one. The former substrate limit is gone: since `SortedMap` moved
onto the measured finger tree, `minEntry`/`maxEntry` are O(1) digit reads, matching the baseline's
cached extremes.

`PersistentRunDeltaVector a` keeps current and checkpoint RRB roots plus an ordered index of the
maximal runs of differing positions. Accepting or reverting one run is four splits and two concats,
so it costs O(log n) independently of the run's length and performs no value comparisons. The
reference identity that the "a clean position reuses its exact checkpoint cell" invariant depends on
is carried by an internal cell wearing a globally unique token, drawn from an atomic counter through
the same `unsafePerformIO`/`NOINLINE` device `Data.Unique` uses; that makes the identity test
available to the pure `validateStructure` and turns `RrbVector.setAt`'s `Eq` no-op check into
exactly the identity short circuit `Arc::ptr_eq` gives the Rust port. `StableName`-based diagnostics
confirm the same fact independently in the tests. Its `EqualityPolicy` is a retained value because
the value relation decides which writes are semantic no-ops; the caller must supply a true
equivalence relation, and `reflexiveIeeeEquality` is the canonical exit for floating payloads,
because a naive `(==)` on `Double` is not reflexive on `NaN` while `EqualityComparer<double>.Default`
is, and a non-reflexive relation corrupts run accounting in a way no invariant check can see.

`PersistentMonotoneActionHeap element priority action` is the action-tagged sibling of
`BrodalOkasakiHeap`, carrying one immutable pending action per tree and forest spine so a whole-heap
priority transform is O(1) worst case while insert, meld, and minimum stay O(1) and delete-minimum
stays O(log n). `actionCompose outer inner` means `outer (inner p)`, so the newer action is the outer
one at every pushdown site, and the `OrderClamp` algebra pins that direction: a cap composed after a
floor collapses to the cap's boundary. The old root is exposed before a losing child is skew-inserted,
so a later insertion is never retroactively transformed. The action policy is a retained record, so
melding operands built from different policies is a documented caller obligation rather than a
detected error.

[`CanonicalSortedSet`](docs/canonical-sorted-set.md) derives exact HMAC-SHA-256 ranks from a caller's
equivalence-class hash and a retained seeded, keyed, or fresh-random policy. Policy creation is an
explicit `IO` boundary that allocates honest algebra-family identity; all persistent set operations
are pure. Stable bulk construction and incremental histories converge on one topology, first
representatives are retained, cross-policy equality uses the receiver's comparison semantics, and
same-family algebra preserves canonicality. Iterative construction, updates, equality, enumeration,
digesting, and validation remain stack-safe even when a constant rank hash forces a linear tree.

`BrodalOkasakiHeap a` directly implements the bootstrapped skew-binomial representation: the
rank-zero global root stores the minimum while its child list fuses primitive skew-tree children
with the embedded heap forest. Insert, meld, and minimum are O(1) worst-case and minimum deletion is
O(log n). Its validator decodes every fused boundary and checks skew ranks, heap order, count, and
depth without depending on the operation code.

`PrioritySearchQueue k p v` is a direct persistent winner-cached AVL core. Each key has one
priority/payload entry, each node caches its subtree winner, and equal priorities break by key.
Keyed updates and minimum deletion are O(log n), minimum lookup is O(1), and inclusive key-range
plus priority-threshold queries prune subtrees whose cached winner already exceeds the threshold.
`validateStructure` independently checks order, AVL balance, cached count/height, and every winner.

`RrbVector a` uses immutable boxed arrays for 32-element leaves and 32-way branches. Packed branches
omit cumulative sizes and select children from five-bit radix digits; split/append-created relaxed
branches retain exact prefix sizes. The surface provides indexed replacement, endpoints,
boundary-spine append, split, range edits, `unsnoc`, structural validation/statistics, and an IO-only
root-sharing diagnostic for tests. `fromList` is the idiomatic pure bulk-construction path: unlike
the strict-language ports, Haskell does not expose a public mutable transient builder. Append
redistributes only the boundary seam, so the test suite's density ceilings are regression gates and
not a global minimum-occupancy invariant. The validator's height cap is derived from the `Int` bit
width plus one legal boundary-only slack level and is thirteen on the repository's 64-bit targets.

`ReversibleDeque a` uses its own orientation-aware strict finger-tree core. `reverse` is an O(1)
mirror of the root, and `append` glues logical digits through mirrored middle views, so
mixed-orientation appends such as `append (reverse left) right` remain tree-based instead of
reifying either operand through lists.

The core measured tree follows the Hinze-Paterson shape directly. Some derived Haskell facades use
idiomatic `containers` storage where it preserves the same observable contract more naturally than
copying a stricter managed implementation detail.

The deque's rightmost-leaf signpost is comparator-agnostic storage used by the runtime-comparator
sorted helpers. Lower bound, upper bound, and binary search follow one measured root-to-leaf path in
O(log n), rather than binary-searching through O(log n) positional probes.

`SortedBag` uses the measured core rather than `Data.Map`: each distinct-key bucket is a
`Data.Sequence`, and the spine caches total multiplicity, distinct count, and last key. Key counts,
rank lookup, and rank-boundary slicing therefore descend in O(log n); slicing rebuilds at most its
two boundary buckets and shares untouched measured subtrees. This remains logarithmic when all
instances belong to one comparer-equal bucket.

`SortedSet` and `SortedMap` sit on the same measured core under the same (count, last key)
measure, replacing their former `Data.Set`/`Data.Map.Strict` facades. Point writes and keyed
searches stay O(log n) measured descents, while extremes improved to O(1) digit reads and the
set's rank slice improved from a Θ(position + length) list rebuild to two comparison-free
structural splits, O(log n) at any position. The set algebra (`union`, `intersection`,
`difference`, and derivatives) adopts whole runs of one operand between measured boundary splits,
delivering the O(m log(n/m + 1)) class the `containers` hedge algorithms delivered — two disjoint
ranges merge in O(log n) and two fully interleaved operands in O(n + m) — never an
element-by-element Θ(m log(n + m)). `SortedMap.keyRange` remains an O(log n) ordered restriction
built from two boundary splits whose probes are never stored, so the delta map's representative
retention and callback budgets hold unchanged.

`Rope` and `MeasuredRope` are chunked facades over that measured core, with chunks capped at 64
elements. Their spines are measured by element count (and, for `MeasuredRope`, by the caller's
monoidal element measure), so indexing, splitting, prefix measurement, and measure-guided location
navigate cached measures in O(log n), plus at most one bounded chunk scan. Endpoint and positional
edits replace only a boundary chunk and retain untouched tree/chunk storage; `append` joins the two
spines without flattening. Because Haskell functions do not have decidable equality, two
`MeasuredRope` operands passed to `append` must have extensionally identical element-measure
functions.

`RopeCursor a` is an opaque immutable snapshot-plus-gap cursor over `Rope a`. `cursor` and
`cursorAt` create gaps in `0 .. count`; the query, peek, movement, seek, insertion, deletion,
replacement, and `snapshot` functions preserve retained ancestors and let edits branch freely.
`Maybe` represents an absent neighbor or invalid operation in the usual Haskell way: when the
element type is itself `Maybe a`, `Just Nothing` is a stored `Nothing` and outer `Nothing` is a
boundary. Same-position seek and empty range insertion preserve the exact retained version state,
and unconditional replacement has no `Eq` constraint and stores the supplied representative.
Construction, movement, and snapshot are O(1); peeks and point edits are O(log n) plus bounded
64-element chunk work, and inserting `m` values is O(m + log n). This is deliberately a persistent
path-copy checkpoint, not the C# focused cursor representation, so it makes no focus-local or benchmark-parity claim.

`MeasuredRopeCursor v a` applies the same opaque immutable gap model to the exact retained
`MeasuredRope`. It adds ordered `measureBefore` and `measureAfter` partitions plus absolute
measure-guided factory and seek operations. `MeasuredRopeCursorSearch` always carries a usable
cursor: a lawful monotone-prefix hit selects the gap before the first satisfying element, while an
empty rope or miss returns the end gap with `searchFound == False`. Measure partitions preserve
left-to-right monoid order without assuming an inverse, commutativity, or element equality. Peeks
retain Haskell's nested-`Maybe` distinction, replacement has no `Eq` constraint, same-gap seek and
empty insertion preserve the exact source, and edits branch through the existing measured rope.
Creation, movement, positional seek, and snapshot are O(1); peeks, ordered measure reads, point
edits, and absolute measure search are O(log n) plus one bounded 64-element chunk scan, and range
insertion is O(m + log n). Element-measure and monoid work may occur during measure reads and edits.

`TextRopeCursor` and `TextRopeCursorSearch` are zero-wrapper type aliases over that measured cursor,
with the full gap/edit/search surface and `lineColumnOfCursor` re-exported by the text module. A
snapshot is therefore immediately usable by every existing text helper. Positions and columns count
Haskell `Char` elements, not UTF-16 code units or grapheme clusters: a supplementary-plane character
normally occupies one `Char`, a combining mark occupies another, and a surrogate-valued `Char` also
occupies one. Because `TextRope` itself is a type alias, callers can construct
one through the generic measured API with a non-newline measure; the text cursor and helper contract
requires values created by `fromString`/`fromText` or an extensionally identical newline measure.

Positional- and measured-rope growth checks the cached `Int` count before constructing a result.
`append`, point/range insertion, and their cursor forms raise the family-specific pure length-
overflow exception if the result would exceed `maxBound`; the receiver and every retained source
remain reusable. Measured range insertion validates the gap, consumes only enough list spine to
prove the count fits, then constructs its measured middle once, so overflow wins before the new
elements or their measure/monoid callbacks are forced. As usual for pure Haskell, forcing an
unevaluated operand or input spine is outside that callback-order guarantee. The text facade also
checks the derived `newlineCount + 1` line count instead of wrapping at `maxBound`.

The text helpers use the measured rope's cached newline counts for offset/line navigation rather
than rescanning the entire text. The interval tree is a low-sorted finger tree annotated with both
maximum-high and last-low values: lower-bound insertion is O(log n), `IntervalTree.findOverlap` is
O(log n), and `IntervalTree.findOverlaps` repeatedly prunes prefixes whose maximum high cannot reach
the probe. That pruning belongs to those tree-level queries; the rank-oriented overlap cursor
factories described below reach the augmentation only through indexed lookup and do not share it.
`compact`, full enumeration, and text/string conversion remain intentionally linear in the number
of returned elements.

## Persistent cursor tier

Every applicable family in this package ships an immutable public cursor. All of them are opaque
values with hidden constructors, all navigation and editing returns new cursor values, retained
ancestors stay valid and branchable, and `snapshot` never consumes its receiver. With one named
exception below they are Profile R snapshot-plus-position or snapshot-plus-rank checkpoints in the
sense of the repository-wide persistent cursor design: the cursor is a retained collection plus a
validated integer, it holds no path frames, and every edit delegates to the ordinary persistent
operation. None of them inherits the C# rope tier's focused representation, memoized snapshot,
callback ceiling, allocation bound, or amortized-locality claims.

Because a cursor is a pure value, no uninitialized, moved-from, or disposed state is representable.
The invalid-default contract that the C, C++, C#, and Rust ports must enforce at run time is
discharged here by the type system — a consequence of immutability rather than an omitted check.

The `Durable7.FingerTree` facade re-exports `RopeCursor` and every ordered-search cursor
type. The remaining cursor types — the five sequence cursors below plus `MeasuredRopeCursor` and
`TextRopeCursor` — are reachable only through their own modules.

### General measured tree: the one true split cursor

`Durable7.FingerTree.Measured` exports `Cursor v a` and `CursorSearch v a` (fields
`cursorSearchCursor`, `cursorSearchFound`). This is the one family that is **not** a root-plus-index
checkpoint: a cursor retains the snapshot together with the left and right trees of an actual split,
so the ordered measure partition is already materialized.

The surface is measure- and neighbor-oriented and deliberately invents no `count` or `position`,
because an arbitrary monoid cannot be read as an index. Factories are `cursorAtStart`, `cursorAtEnd`,
and `cursorByMeasure`, whose monotone-prefix predicate selects the gap before the first satisfying
element and whose miss returns an end cursor with `cursorSearchFound == False`. `cursorIsAtStart`,
`cursorIsAtEnd`, `cursorMeasureBefore`, and `cursorMeasureAfter` query it; `cursorPeekPrevious`,
`cursorPeekNext`, `cursorMovePrevious`, `cursorMoveNext`, and `cursorSeekByMeasure` navigate it;
`cursorInsert`, `cursorDeletePrevious`, `cursorDeleteNext`, and `cursorReplaceNext` edit it, and
`cursorSnapshot` publishes it. Insertion returns the gap after the new element, `cursorDeletePrevious`
moves the gap left, and forward deletion and replacement keep it fixed.

`cursorMeasureBefore` and `cursorMeasureAfter` combine in that order to the snapshot measure, with no
inverse, commutativity, or element equality assumed. Both are **O(1)**: each reads the cached root
annotation of a retained half rather than performing a split, so this family alone does not pay a
split to answer an ordered measure question. Peeks and unit movement are O(1) amortized along one
cursor lineage and O(log n) worst at a spine crossing; branching immediately before a crossing can
repeat that work independently, since deferred work in one branch is not paid for by a sibling. Edits
reassemble the two sides and cost O(log(min(|left|, |right|))). `cursorSeekByMeasure` re-splits the
retained snapshot from the root in O(log n) and does not reuse the open path. `cursorSnapshot` is
O(1). Only `Show` is derived: a derived `Read` could fabricate a cursor whose retained snapshot
disagrees with its two halves, and a derived structural `Eq` would contradict the extensional
equality the deque snapshots use.

### Positional sequence cursors

`Deque.Cursor a`, `ReversibleDeque.Cursor a`, `RrbVector.Cursor a`, and
`RangeUpdateSequence.Cursor element measureValue tag` share one positional gap protocol: `cursor`
opens at gap zero, `cursorAt` validates a position in `0 .. count` and returns `Nothing` outside it,
and `cursorPosition`, `cursorCount`, `cursorIsAtStart`, `cursorIsAtEnd`, `cursorPeekPrevious`,
`cursorPeekNext`, `cursorMovePrevious`, `cursorMoveNext`, `cursorSeek`, `cursorInsert`,
`cursorDeletePrevious`, `cursorDeleteNext`, `cursorReplaceNext`, and `cursorSnapshot` behave
uniformly. Insertion returns the gap after the inserted values, `cursorDeletePrevious` moves the gap
left, and forward deletion and replacement keep it fixed. `cursorCount` is an O(1) cached read and
movement is O(1) in all four, because it rewrites only an integer.

`Deque.Cursor` adds `cursorInsertList`, which returns the receiver for an empty list. Replacement is
unconditional and carries no `Eq a`, because the deque has no element-equality policy. Peeks and
point edits are O(log n) measured splits, and inserting `m` values is O(m + log n). Element counts
are checked as the measure monoid combines them, so a length overflow raises rather than publishing
a wrapped negative count that would make every cursor bound check meaningless.

`ReversibleDeque.Cursor` adds `cursorReverse`, which maps gap `p` to `count - p`, swaps the logical
sides, and is O(1) because the underlying reversal is a root mirror. Peeks are O(log n). Its edits,
however, are the honest exception in this package: `cursorInsert`, `cursorInsertList`,
`cursorDeletePrevious`, `cursorDeleteNext`, and `cursorReplaceNext` all round-trip the whole deque
through a list and rebuild it, so each is **Θ(n)** and shares no structure with the source. The
source and every retained snapshot remain valid and the results are correct; only the cost is
linear. No locality claim is made for this family.

`RrbVector.Cursor` adds `cursorInsertList` and `cursorInsertVector`, the latter splicing an existing
vector through split-and-append with structural sharing. Its `cursorReplaceNext` requires `Eq a` and
applies the vector's element-equality no-op rule, returning the source vector when the element is
unchanged. Peeks and point edits are O(log32 n), `cursorInsertList` of `m` values is
O(m + log32 n), and `cursorInsertVector` adds the vector's concat/rebalance bound. Count growth is
checked before a result is constructed.

`RangeUpdateSequence.Cursor` adds ordered measures and lazy-tag range operations:
`cursorMeasureBefore`, `cursorMeasureAfter`, `cursorMeasurePrevious`, `cursorMeasureNext`,
`cursorApplyPrevious`, and `cursorApplyNext`. `cursorApplyPrevious k tag` targets
`[position - k, position)` and `cursorApplyNext k tag` targets `[position, position + k)`; both keep
the gap fixed and validate the complete range before any identity test, tag action, or measure
callback. A zero-length range or a semantically identity tag returns the source sequence unchanged.
`cursorReplaceNext` carries no `Eq element`, so it always rebuilds, and earlier range tags do not
transform the supplied replacement.

Unlike the general measured tree above, this cursor is root-plus-position, so its ordered measure
reads are **not** O(1): `cursorMeasureBefore` and `cursorMeasureAfter` call `measureRange`, which is
O(1) only in its two boundary cases — an empty range returns the algebra's identity measure without
invoking element or tag callbacks, and a whole-sequence range returns the cached root measure — and
O(log n) otherwise. Peeks, point edits, range measures, and proper range updates are O(log n); a
whole-sequence nonidentity tag remains the sequence's O(1) root update.

Rope, measured-rope, and text cursors are described above and are unchanged by this tier.

### Ordered-search cursors

`Durable7.FingerTree.OrderedSearchCursor` holds one ordered gap cursor for each of seven
families: `SortedBagCursor a`, `SortedSetCursor a`, `SortedMapCursor k v`,
`CanonicalSortedSetCursor a`, `PrioritySearchQueueCursor k p v`, `IntervalTreeCursor a`,
`IntervalMapCursor a v`, and `ChunkedBitSetCursor`. Two shared carriers appear throughout:
`CursorSearch` with fields `cursorFound` and `searchCursor`, and `CursorInsert` with fields
`cursorAdded` and `insertionCursor`.

Each family follows one naming shape — `…CursorAt` for a validated rank, `…LowerBoundCursor` and
`…UpperBoundCursor` for order bounds, `find…Cursor` for an exact search returning `CursorSearch`,
then `…CursorPosition`, `…PeekPrevious`, `…PeekNext`, `…MovePrevious`, `…MoveNext`, `…SeekRank`, the
family-appropriate edits, and `…Snapshot`. Every cursor denotes a gap whose *next* entry is the
search candidate, so a miss is a usable insertion gap rather than an invalid state, and every
factory retains the exact comparator, rank policy, or endpoint policy of its source.

Family-specific rules:

- `sortedBagAdd` always inserts at the **upper** bound, after every existing comparer-equal
  occurrence, preserving the bag's stable equal-item insertion rule, and returns the gap after the
  new occurrence. `sortedBagDeletePrevious` and `sortedBagDeleteNext` remove the exact occurrence at
  that rank. There is deliberately no arbitrary in-run insertion and no replacement, because
  changing an occurrence can move its sort position.
- `sortedSetAdd` inserts an absent item at its lower bound and returns the gap after it; an
  equivalent present class leaves the set unchanged, retains its first stored representative, and
  still reports the gap after that class. Deletions remove the exact comparer class, and there is no
  replacement.
- `sortedMapInsert` is strict and returns `Nothing` for a duplicate key; `sortedMapTryInsert` reports
  the same outcome through `CursorInsert`. `sortedMapSetItem` updates an exact hit or inserts at a
  miss, and `sortedMapSetNextValue` rewrites the next value while retaining its stored key and gap.
- `canonicalAdd` returns `Either CanonicalSetError`, because deriving a zip-zip rank is itself
  fallible. The cursor retains the exact `ZipTreeRankPolicy`, and a closed result has the same
  topology as the ordinary canonical operation for the same policy and contents.
- The priority-search queue cursor is a **key-order** cursor; priority is cached augmentation, not a
  second navigation order. `prioritySearchMinimumCursor` reads the cached winner and then performs
  an ordinary key seek rather than walking a priority sequence that does not exist.
  `prioritySearchSetNext` retains the stored key representative.
- The interval-tree cursor is ordered by nondecreasing low endpoint. `findIntervalCursor` matches an
  exact stored interval on both endpoints, `findOverlapCursor` and `findContainingCursor` locate a
  first overlap, and `intervalTreeSeekNextOverlap` continues strictly after the focused occurrence
  so a factory's hit cannot be rediscovered forever. `intervalTreeInsert` uses the facade's low-bound
  placement, and deletions remove the exact occurrence at the cursor's rank rather than an ambiguous
  equal interval.
- The interval-map cursor is ordered by the unique complete `(low, high)` interval key.
  `intervalMapTryInsert` reports whether a complete key was added, `intervalMapSetNextValue` retains
  the stored interval representative, and the overlap factories and `intervalMapSeekNextOverlap`
  mirror the interval-tree spellings.
- The chunked-bit-set cursor traverses present set bits, not a dense Boolean sequence.
  `chunkedBitSetCursorAt` takes an `Int64` population rank in `0 .. size`, while
  `chunkedBitSetCursorAtOrAfter` and `findChunkedBitSetCursor` take a bit index; the latter adds the
  found flag. `chunkedBitSetInsert` is an exact no-op for a bit already present — receiver and gap
  both preserved — and otherwise returns the gap after the new bit. Deletions clear the exact
  neighbouring bit.

Every edit in this module calls the owning collection's ordinary persistent operation, so
comparators, canonical ranks and rotations, winner caches, maximum-high summaries, and sparse-word
contraction behave exactly as they do for a direct call.

Complexity is uniform where the substrate is: `…CursorPosition` and `…Snapshot` are O(1), rank
validation reads a cached count, and `…MovePrevious`/`…MoveNext`/`…SeekRank` are O(1) because they
rewrite only an integer. Beyond that:

- Sorted-bag, sorted-set, and sorted-map peeks, bounds, additions, and deletions are O(log n)
  measured descents in the shared finger-tree substrate.
- `cursorBoundRank` and `cursorIndex` for the canonical sorted set and the priority-search queue are
  single O(h) descents through cached subtree counts. For the AVL-backed queue that is O(log n); for
  the canonical set, expected logarithmic height depends on the documented coherent pseudorandom
  rank assumptions, and a degenerate rank policy makes `h = n`.
- Interval-tree and interval-map indexed lookup and lower-bound ranks are O(log n).
  `intervalTreeUpperBoundCursor` additionally probes forward once per equal-low occurrence, so an
  equal-low run of length `d` costs O((1 + d) log n).
- The overlap factories are the honest exception. `findOverlapCursor`, `findContainingCursor`,
  `intervalTreeSeekNextOverlap`, and their interval-map counterparts probe candidate ranks one at a
  time from the start rank, each with an O(log n) indexed lookup, and stop at the first overlap. A
  hit at rank `r` from start `s` costs O((r - s + 1) log n), and a miss scans to the end at
  Θ(n log n). They do **not** use the maximum-high pruning that `IntervalTree.findOverlap` and
  `IntervalTree.findOverlaps` apply. Callers who need the pruned query should run it on the snapshot
  and build a cursor from its result.
- Chunked-bit-set membership, insertion, deletion, rank, and select are logarithmic in the number of
  represented nonzero 64-bit words, plus bounded constant work inside one word.

```powershell
cd src\Haskell
.\test.ps1 -Workspace FingerTree
```

The wrapper forces one Cabal build job. The local [test README](test/README.md) lists the
deterministic coverage areas.
