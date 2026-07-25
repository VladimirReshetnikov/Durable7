# TypeScript API and semantic notes

- Created (UTC): 2026-07-15T00:12:55Z
- Repository HEAD: 6bf20605073b1750d871d4bd53ef75fcfe25484c
- Scope: TypeScript mappings for repository-wide semantic contracts

## Runtime mapping

TypeScript has no user-defined operators, value types, deterministic finalization, or shared-memory
object graph. Consequently, fixed-width integers expose named arithmetic methods, missing-value APIs
use entries or discriminated unions where `undefined` may be a stored value, and all immutable
collections are reference objects. Signed 64-bit keys and values use `bigint`; 32-bit keys use
`number` with range checks.

`PersistentHashMap` follows JavaScript `Map`-style default key equivalence: SameValueZero for
primitives and identity for objects. Callers can supply a `HashPolicy` for structural keys. Equivalent
replacement retains the stored key representative across the HAMT, Patricia, sorted, Merkle, and families.

## HAMT maps, bags, multimaps, relations, bimaps, derived facades, builders, and sessions

`PersistentHashMap.getOrAdd(key, addFactory)` and
`addOrUpdate(key, addFactory, updateFactory)` return a `MapUpdateResult` containing the selected
value and persistent map. They hash once, descend once, validate all supplied factories before
hashing, and invoke exactly one selected factory at most once. A hit containing `undefined` remains
a hit because branch selection uses the stored entry rather than its value. Updates retain the
first equivalent key representative. SameValueZero is the TypeScript value-equivalence rule, so an
equal update retains and reports the stored value representative and returns the receiver map.
The small `{ map, value }` result carrier is necessarily allocated even on a map no-op; the no-op
claim concerns CHAMP nodes and successor maps rather than the whole method call.

`PersistentHashBag<T>` is an immutable unordered multiset with one stored representative and a
positive `number` multiplicity per policy class. `distinctCount` counts classes; exact expanded
`totalCount` is an uncapped `bigint`, deliberately avoiding an ambiguous or lossy `size`. Per-class counts
preserve the C# `1 .. 2^31 - 1` bound. Copy-count arguments must be integers in
`0 .. 2^31 - 1` and are validated before hashing; zero-copy updates, missing removals, and empty
clears preserve receiver identity. Expanded iteration repeats each representative contiguously;
`distinctItems()` and `entries()` expose the matching distinct order. `tryGetValue` uses a
presence-discriminated result so a stored `undefined` representative is unambiguous. `toArray()`
preflights its exact total against JavaScript's `2^32 - 1` maximum array length.

Bag algebra accepts another bag and is governed by the receiver's exact `HashPolicy` object:
`union` takes maximum counts, `intersect` takes minimum counts, `except` uses saturated subtraction,
and `sum` uses checked addition. Receiver representatives win surviving classes. A mismatched-policy
argument is eagerly normalized under the receiver policy before shortcuts; collapsed classes are
checked-summed and retain the first representative observed in that argument version's stable
HAMT order. The bag intentionally has no transient, builder, symmetric-difference, arbitrary-
iterable algebra, or content-equality surface.

`PersistentBiMap<K, V>` is a strict immutable bijection backed by independent forward and inverse
CHAMP maps. `keyPolicy` and `valuePolicy` define the two equivalence domains. `add` rejects an
existing class on either side; `tryAdd` reports `"key"` or `"value"`; and `set` may replace one
key's value only when the new value is unclaimed. Equivalent `set` operations use `valuePolicy`,
retain both stored representatives, and return the receiver. Replacement removes and re-adds both
entries so no substrate-level SameValueZero shortcut can override the configured value policy.

`get` and `getKey` return presence-discriminated results, including for stored `undefined`.
Key/value removal is symmetric and exposes nonthrowing result carriers. `inverse` swaps the existing
roots and policy roles in O(1), is cached, and points back to the original facade. Forward iteration
follows stable-for-one-version, otherwise unspecified CHAMP order. Every pair is stored twice; the
type deliberately has no algebra, builder, transient, or displacement mode.

`PersistentHashMultimap<K, V>` composes the public CHAMP map and set with independent key and value
policies. It stores no empty value groups, reports exact `keyCount` and flattened `pairCount`, and
retains first representatives in both domains. Duplicate addition and missing removal return the
receiver. Removing a final pair contracts its outer key; `removeKey` removes a complete adjacency
group. Iteration flattens the stable-for-one-version, otherwise unspecified CHAMP order.

`PersistentRelation<L, R>` maintains mutually inverse multimaps. Before inserting, it normalizes
both arguments to the globally retained representatives, so the same right class cannot acquire
different stored objects in different left groups. Pair and whole-side removals update both indexes.
The cached `inverse` facade swaps existing roots in O(1), and `inverse.inverse` is the receiver.

`PersistentMapPatch<K, V>` records presence-discriminated before/after states for changes between
maps retaining the same key-policy object. This preserves the distinction between absence and a
present `undefined`. Strict `apply` validates every expectation before publishing any successor;
conflicts leave the source untouched. Inversion swaps states, and composition checks the complete
intermediate state and removes round trips. Composition also requires the same value-equality
function object so no equality domain is silently mixed.

`PersistentDirectedGraph<V>` composes an explicit CHAMP vertex set with a bidirectional relation.
Edges automatically install both endpoints, isolated vertices remain first-class, self-loops are
allowed, and parallel equivalent edges collapse. Removing an edge retains endpoints; removing a
vertex removes all incident edges. `reversed` swaps the already-built relation roots in O(1) and is
an involution.

`PersistentIndexedMap<K, V, I>` composes a primary CHAMP map with a nonunique secondary multimap.
The selector is not invoked for duplicate strict adds, equal-value updates, removals, or lookups.
Changed values are selected before publication and atomically move their primary representative
between secondary groups; an exception leaves the source reusable. Primary and secondary equality
domains retain independent `HashPolicy` objects.

`HashMapBulkBuilder<K, V>` is a reusable construction-only staging object, also available through
`PersistentHashMap.createBulkBuilder`. It exposes only policy/count state, `setItem`, `setItems`, and
`toImmutable`. First key representatives win, the last SameValueZero-distinct value wins, and equal
values retain the earlier value object. Every freeze copies the reachable CHAMP nodes into a
detached immutable snapshot while leaving the builder reusable; no key-policy callback runs during
that freeze. Staging uses uniquely owned mutable leaf, collision, and bitmap nodes that are never
published directly. The builder remains construction-only and is deliberately separate from the
lookup/removal/adoption lifecycle of a transient session.

`TransientHashSet` exposes the six read-only set relations in addition to lookup and mutation.
Relations use the transient's receiver policy, do not advance its mutation version, and obey the
same one-way lifecycle: every relation throws `TransientConsumedError` after publication before
enumerating its argument.

## Independent insertion-ordered collections

`PersistentOrderedSet<T>` is a neutral general-purpose family exported through the `ordered`
subpath. It composes only the public CHAMP map and FingerTree families. A `HashPolicy<T>` defines equality classes; the set retains the first representative,
insertion or explicitly requested order, private sparse `bigint` labels, positional lookup/removal/
ranges, explicit final-index movement, reversal, and stable one-shot sorting.

Algebra and all six relations eagerly normalize the complete argument under the receiver policy,
including another ordered set with a different policy object. Receiver order and representatives win
shared classes; first normalized argument representatives supply argument-only classes. Logical
no-ops preserve the exact receiver, and empty results preserve the policy. Stored `undefined` is
unambiguous through `tryGetValue`'s `{ found, value }` result. TypeScript iterators retain immutable
snapshots but do not emulate the C# struct-enumerator copy/fail-fast mechanics. The full local contract
and API mapping are in [the ordered-set notes](ordered.md).

`PersistentOrderedMap<K, V>` uses the same neutral dependency boundary and sparse-label order
maintenance. Its positional tree owns each key/value payload; CHAMP stores only key-to-stamp
navigation, avoiding a duplicate retained value. Construction keeps the first key representative
and position while the last value-equivalence-distinct payload wins. `set` never moves an existing
key. Only explicit movement, reversal, and stable one-shot sorting change order; range extraction
reconciles both indexes and all logical no-ops preserve the receiver.

`PersistentOrderedMultimap<K, V>` nests one ordered value set in an ordered map. Key groups follow
first key insertion and each group follows first value insertion, so flattened enumeration is
key-grouped rather than a globally interleaved pair-arrival history. Key and value domains retain
independent `HashPolicy` objects. Duplicate pairs and removal misses return the receiver; removing a
last value contracts the group, and reintroducing that key appends a new group.

## Payload interval map and chunked bit set

`PersistentIntervalMap<T, V>` is exported through the finger-tree subpath. It orders validated
closed interval keys lexicographically by `(low, high)`, retains the first key representative, and
uses a supplied value equality function for replacement no-ops. Its measured sequence caches the
complete rightmost interval and maximum high endpoint, which supports exact same-low positioning
and pruned overlap/stabbing queries in one index. Distinct overlapping intervals remain independent;
the type intentionally does not expose interval coalescing because payload merge semantics require
an explicit application policy.

`PersistentChunkedBitSet` stores only nonzero 64-bit `bigint` words in the shared measured sequence.
Its public domain is nonnegative signed-32-bit `number` indexes; point updates validate this domain,
while negative membership/removal/rank queries behave as empty-prefix queries. Cached population
and last-word summaries provide logarithmic point edits, inclusive rank, and zero-based select in
the number of represented words. Iteration is ascending and the four algebra operations merge word
streams in linear represented-word time.

## Persistence and sharing

The CHAMP, Patricia, measured AVL, lazy range-update AVL, RRB, canonical zip-zip,
Brodal–Okasaki, priority-search, interval, and Merkle cores use immutable nodes and path copying.
No-op operations return the receiver where the corresponding semantic contract defines a no-op.
Builders and transient sessions never mutate an already published persistent version.

TypeScript CHAMP transients preserve O(1) adoption, clean/no-op identity publication, single-owner
semantics, version-bound enumeration, and one-way publication. Their edits call the immutable CHAMP
kernel; they do not claim the C# T2 owner-token in-place mutation bound. Rope cursors likewise preserve
immutable branching, gap semantics, navigation/edit behavior, measures, and text line/column mapping,
but use persistent path-copying edits instead of the C# bounded-window cursor optimization.
Positional and measured cursors expose `peekPreviousEntry`/`peekNextEntry` wrappers so stored
`undefined` is distinct from a boundary. `replaceNext` is an unconditional edit: it publishes a
fresh rope even for the identical object, and measured replacement invokes the supplied element's
measure callback before publication.

Every cursor in the package spells its collection length `size`. `RopeCursor`, `MeasuredRopeCursor`,
and `TextRopeCursor` retain `count` as a documented alias of `size` rather than removing it from the
published surface; the alias idiom matches `PersistentChunkedBitSet`, `CanonicalSortedSet`, and
`RangeUpdateSequence`, which expose both spellings for the same reason.

Cursor edits stay on the path-sharing route of the collection they branch from. `TextRopeCursor`
publishes a text version by wrapping its edited measured rope through `TextRope.fromMeasuredRope`, an
O(1) facade change that allocates no nodes and invokes no measure callback, so an empty insert
returns the receiver cursor and the original `TextRope`. Sorted-bag and interval-tree cursor deletes
call the ordinary logarithmic `removeAt` primitive of their measured sequence. `ReversibleDeque`
cursor range inserts build the spliced middle in the receiver's *physical* orientation, so a reversed
receiver takes the same catenation path as a forward one instead of re-materializing every element;
the logical result is identical in both orientations.

Rope range arguments are UTF-16 code units. `Rope.fromText` and `TextRope.fromText` split on `""`,
whereas spreading a `string` yields code points, so `Rope.insertRange`, `MeasuredRope.insertRange`,
and both rope cursors' `insertRange` reject a bare `string` with a `TypeError` rather than silently
storing an astral character as a single two-unit element. The generic signature cannot split for the
caller because the element type is arbitrary; pass `text.split("")`, or use `TextRopeCursor.insert`,
which is text-specific and splits on the caller's behalf. `TextRopeCursor`'s public constructor also
rejects a `snapshot` argument that is not the version its measured cursor is anchored to, so the
line/column surface cannot answer against a different document.

`ConcurrentHashTrie` is an isolate-local consumer-semantic facade over published persistent CHAMP
roots. It provides synchronous mutable-map operations, generation tracking, canonical CHAMP
enumeration, collision and stored-representative behavior, presence-safe entries for stored
`undefined`, and O(1) immutable snapshots. A snapshot retains exactly one observed root and remains
stable after every later mutation; converting it to `PersistentHashMap` returns that captured map.

Every mutator derives its result from one observed root and publishes only while that root remains
current. If a custom hash/equivalence callback or factory reenters the trie and publishes another
root, `set`, `tryAdd`, `getOrPut`, `compute`, and `remove` discard their stale successor and retry
against the latest root. Their return values therefore describe the stable winning observation: for
example, `tryAdd` returns false if a nested same-key insertion wins, while `remove` returns absent if
a nested same-key removal wins.

A retried factory may run more than once, and `compute` may switch from its add branch to its update
branch. Both branches receive the caller's lookup key rather than the retained stored representative;
the update branch also receives the latest stored value. A retry, an equal-value no-op, and a
discarded candidate do not advance `generation`; each changed root that is actually published
advances it exactly once. A callback exception publishes no candidate from the failing outer
operation. Publications made by explicitly nested trie calls are independent completed operations
and are therefore not rolled back when the outer callback throws.

This retry protocol supplies deterministic reentrancy semantics within one JavaScript agent. It is
not a lock-free Ctrie implementation and deliberately does not claim the multi-threaded GCAS/RDCSS
progress contract of the C# and Kotlin Ctries. JavaScript object graphs cannot be atomically shared
between worker isolates.

## Persistent cursor tier

Every applicable family in the package ships a public cursor. All of them are
**Profile R root-plus-position (or root-plus-rank) semantic checkpoints**: an immutable exported
class retaining one exact collection version plus a validated gap, whose every edit delegates to that
collection's ordinary persistent operation. Consequently **no cursor in this package inherits any part
of the C# rope tier's contract** — not its focused bounded-window representation, its prepared-measure
fragments, its winner-returning snapshot memo, its callback ceiling, its allocation bound, or its
amortized-locality claim. Where a cursor's real cost is worse than the collection's own primitive,
this document states the real cost.

Cursors are ordinary immutable classes with strict ESM typing and public validating constructors, so
**there is no uninitialized, moved-from, or disposed cursor state** to document: construction either
yields a usable value or throws. Navigation and editing return new cursor values, retained cursors and
their snapshots stay valid and branchable, and materialization never consumes a cursor. Each cursor
retains its source collection's exact policy objects — `HashPolicy`, `Comparator`, `MeasurePolicy`,
`ZipTreeRankPolicy`, `RangeUpdateAlgebra`, Merkle policy, value-equality function — under JavaScript
reference identity.

### Shared surface and naming

| Concern | Convention | Exceptions verified in this port |
| --- | --- | --- |
| Length | `size` | `MerkleSearchTreeCursor` exposes only `count`; `PersistentChunkedBitSetCursor` leads with `count` and aliases `size`; `PersistentOrderedMultimapCursor` exposes only `pairCount`; `FingerTreeCursor` exposes neither |
| Gap index | `position` | `FingerTreeCursor` has no public position at all |
| Positional seek | `seek(position)` | the comparator-ordered families spell it `seekRank(position)`; `FingerTreeCursor` has only `seekByMeasure`/`searchByMeasure` |
| Boundary tests | `isAtStart`, `isAtEnd` | — |
| Movement | `movePrevious`, `moveNext` | — |
| Materialization | `snapshot()` method | the three neutral `Ordered` cursors expose `snapshot` as a readonly **property** instead |

The five sequence cursors — `PersistentDequeCursor`, `ReversibleDequeCursor`, `RrbVectorCursor`,
`RangeUpdateSequenceCursor`, and `FingerTreeCursor` — spell length `size` and do not carry a `count`
alias, whereas `RopeCursor`, `MeasuredRopeCursor`, and `TextRopeCursor` retain `count` alongside
`size` as described above. `FingerTreeCursor` is the deliberate outlier: the general measured tree has
no element-count measure, so its cursor exposes `measureBefore`/`measureAfter` and neighbor semantics
rather than a fabricated `size` or integer `position`.

Peek shapes vary with what a boundary must be distinguished from. Most families return a `{ value }`
wrapper or `undefined`; the Patricia maps return a `readonly [key, value]` tuple, the Patricia sets a
bare `number`/`bigint`, the Merkle cursor a `MerkleEntry`, and `PersistentOrderedSetCursor` a
`{ found: true, value } | { found: false }` union. In every case a stored `undefined` value stays
distinguishable from an absent neighbor.

### `RangeError` carries two channels

**`RangeError` is used for both bad-argument and boundary conditions across every cursor family in
this package**, so callers cannot distinguish the two by exception type. An out-of-range constructor
or `seek`/`seekRank` position raises `RangeError`; so do "Cursor is already at the start/end" and "No
element/entry/occurrence precedes/follows the cursor". This applies to the sequence, rope, sorted,
canonical, priority-search, interval, interval-map, bit-set, Patricia, Merkle, and neutral `Ordered`
cursors alike. Test `isAtStart`/`isAtEnd`/`size` before a non-`try` operation, or prefer the peek
members, which report a boundary as `undefined` or `{ found: false }` instead of throwing.

A few conditions do get their own types, and they are not uniform either: strict sorted-map insertion
throws `SortedDuplicateKeyError`, strict interval-map insertion `DuplicateIntervalError`, ordered-map
insertion `DuplicateKeyError`, priority-search insertion a plain `TypeError`, and Patricia and Merkle
strict insertion a plain `Error`. Rope range insertion rejects a bare `string` with `TypeError`.

### Patricia integer cursors

`PersistentIntMapCursor<V>`, `PersistentLongMapCursor<V>`, `PersistentIntSetCursor`, and
`PersistentLongSetCursor` are ordered gap cursors over ascending **signed** key order; the sign-bit
path transform keeps in-order traversal ascending across the minimum, zero, and maximum boundaries.
Factories are `cursor(position)`, `cursorAtEnd()`, `lowerBoundCursor(key)`, `upperBoundCursor(key)`,
and `cursorAtKey(key)`/`cursorAtItem(value)`, the last returning `{ cursor, found }` so a miss still
yields a usable insertion gap.

Map `insert` is strict and additionally requires the key's lower-bound rank to equal the current gap;
`put` updates an exact next entry or inserts at a missing lower-bound gap; `setNextValue` retains the
stored integer key. Set `add` applies the same gap check and is an exact receiver-preserving no-op for
a present item, which leaves the gap *before* that item, while a successful insertion returns the gap
after it. `deletePrevious` moves the gap left and `deleteNext` keeps it fixed. A `sameValueZero` value
no-op returns the receiver cursor.

With key width `W` of 32 or 64: `size`, `position`, and `snapshot()` are O(1); rank and lower-bound
factories, peeks, and every edit are O(W) descents through cached subtree counts. **`movePrevious`,
`moveNext`, and `seek` are O(1) because they only rewrite an integer, but the peek after a move is an
unconditional O(W) root descent**, so a complete in-order traversal by move-plus-peek is O(n · W) and
cursor context is O(1). No retained-frame representation ships here.

### Merkle search-tree cursor

`MerkleSearchTreeCursor<K, V>` is the specialized ordered cursor over an authenticated snapshot.
Factories are `cursor(position)`, `cursorAtEnd()`, `lowerBoundCursor(key)`, `upperBoundCursor(key)`,
and `cursorAtKey(key)`, which publishes a separate `found` flag so a miss returns a usable insertion
gap. `insert` rejects an existing key and also rejects a key whose lower-bound rank is not the current
gap; `set` updates the exact next entry or inserts at a missing lower-bound gap; `setNextValue`
retains the stored key representative; `deletePrevious` moves the gap left and `deleteNext` keeps it
fixed.

Every cursor edit calls the ordinary canonical `set`/`remove`, so the policy domain, stored key
representative, canonical codecs, key levels, `MST2` block bytes, root digest, and failure behavior are
identical to a direct tree edit. **Cursor state is local navigation state and never appears in `MST2`,
`MSP2`, a pack, a proof, or a store.** Rank selection and lower-bound rank descend through the
authenticated cached subtree counts, so with height `h` and node entry count `b` they are O(h · b);
moves are O(1) and the following peek pays that descent again.

### Sequence cursors

`PersistentDeque`, `ReversibleDeque`, `RrbVector`, and `RangeUpdateSequence` expose positional gap
cursors through `getCursor(position = 0)`. `FingerTree` has no positional factory at all: it offers
`getCursorAtStart()`, `getCursorAtEnd()`, and `cursorByMeasure(predicate)`, matching its lack of a
public element count. The gap is a
boundary in `0 .. size`; insertion returns the gap after the inserted values, `deletePrevious` removes
`position - 1` and moves the gap left, and `deleteNext`/`replaceNext` address `position` and keep the
gap fixed. Empty, start, and end gaps are ordinary valid states.

Over the measured AVL and RRB substrates, cursor creation, the state members, and `snapshot()` are
O(1); peeks and every point edit are O(log n); `movePrevious`/`moveNext`/`seek` are O(1) with the next
peek paying a fresh O(log n) descent, so a full traversal by move-plus-peek is O(n log n).
`insertRange` is Ω(m) to capture its argument plus the substrate's split/concat bound;
`RrbVectorCursor.insertVector` accepts an existing vector so an already-built range splices with
structural sharing.

Replacement differs by family and the difference is observable. `PersistentDequeCursor`,
`RrbVectorCursor`, and `RangeUpdateSequenceCursor` route `replaceNext` through the collection's
`setItem`, whose `Object.is` identity check returns the receiver collection when the element is the
identical object; the rope cursors instead remove and reinsert, publishing a fresh rope
unconditionally. `ReversibleDequeCursor` implements `replaceNext` as a split, append, and catenation
rather than an indexed set.

`ReversibleDequeCursor` adds `reverse()`, which maps gap `p` to `size - p` over the O(1)-reversed
deque and swaps the logical sides. Its `insert`/`insertRange` path is orientation-aware as described
under persistence and sharing.

`FingerTreeCursor.measureBefore` and `MeasuredRopeCursor.measureBefore` are read-only prefix descents
that allocate no persistent nodes, but **their `measureAfter` is asymmetric**: it performs a real
structural split (two splits in the measured rope's case, via `slice`) to obtain the suffix measure.
Both sides are O(log n), yet only the suffix side allocates. `RangeUpdateSequenceCursor` is the
exception — both of its sides call the read-only `measureRange`, so it is symmetric. Combining before
and after measures in that order equals the snapshot measure in every case.

`FingerTreeCursor.seekByMeasure(predicate)` returns the gap before the first element whose inclusive
prefix satisfies the monotone predicate and returns a usable end cursor on a miss;
`searchByMeasure(predicate)` returns `{ cursor, found }` for callers that must distinguish the two.

### Rope and text cursors

`RopeCursor<T>`, `MeasuredRopeCursor<T, M>`, and `TextRopeCursor` keep the mature contract described
under persistence and sharing. **The text unit throughout the rope and text tier is the UTF-16 code
unit.** `Rope.fromText` and `TextRope.fromText` split on `""`, `NewlineMeasure` counts `"\n"` code
units, `TextRopeCursor.insert(text)` splits its argument the same way, and
`TextRopeCursor.replaceNext` requires a string of length exactly one. An astral character therefore
occupies two cursor positions, contributes two to `size`, and shifts every later offset by two; the
port does not treat code points, Unicode scalars, or grapheme clusters as the navigation unit
anywhere in this tier.

`TextRopeCursor.line` is the newline measure before the gap — a zero-based line index — and `column`
is the offset from that line's start. `seekLineColumn(line, column)` throws `RangeError` for an
invalid pair while `searchLineColumn(line, column)` returns `{ cursor, found }` with the receiver on a
miss; both resolve through the underlying newline-measure locate, so "position at the start of line
*n*" is reachable as `seekLineColumn(n, 0)`. What the text cursor does **not** expose is a generic
measure-predicate seek: `seekByMeasure`/`searchByMeasure` exist only on `MeasuredRopeCursor`, and
reaching them through the public `cursor` property yields a `MeasuredRopeCursor`, dropping the text
facade. A caller wanting an arbitrary newline-measure predicate must seek on the measured cursor and
rebuild a `TextRopeCursor` from the result.

### Ordered-search cursors

`SortedBagCursor<T>`, `SortedSetCursor<T>`, `SortedMapCursor<K, V>`, `CanonicalSortedSetCursor<T>`,
`PrioritySearchQueueCursor<K, P, V>`, `IntervalTreeCursor<T>`, `PersistentIntervalMapCursor<T, V>`,
and `PersistentChunkedBitSetCursor` all denote a gap in the collection's promised order, reached
through `cursorAt(position)`, `cursorAtLowerBound(...)`, `cursorAtUpperBound(...)`, and a
`findCursor(...)` that returns `{ found, cursor }` with a usable gap on a miss. The bit set instead
uses `cursorAt(rank)`, `cursorAtOrAfter(bitIndex)`, and `findCursor(bitIndex)`. `seekRank(position)`
is the positional seek across this group. Every one of them retains the source comparator, rank
policy, or value-equality function exactly, including on an empty result.

Stored-representative rules follow each collection and are worth stating explicitly because the port
makes a definite choice: **`SortedMap.setItem`, `PrioritySearchQueue.setItem`, and
`PersistentIntervalMap.set` retain the stored key or interval representative and discard the supplied
one**, so `SortedMapCursor.setItem`, `PrioritySearchQueueCursor.setItem`, and
`PersistentIntervalMapCursor.setNextValue` never substitute a comparator-equivalent argument for the
stored key. Value no-ops use `Object.is` for the sorted map, `Object.is` on both priority and value
for the priority-search queue, and the map's supplied equality function for the interval map, each
returning the receiver.

Gap conventions in this group: an update at a hit keeps the gap immediately *before* the updated
entry, while an insertion at a miss returns the gap immediately *after* the new entry.
`SortedBagCursor.add` always inserts after every comparator-equal occurrence and returns the gap after
the new one; `SortedSetCursor.add` and `CanonicalSortedSetCursor.add` return the gap after the
comparator class whether or not the item was newly inserted, and the snapshot is the receiver on a
hit. The bag deliberately exposes no arbitrary `insertHere` and no replacement, because either would
create behavior unreachable from the ordinary bag API; the sorted and canonical sets expose no
replacement, because it could collide with another class.

Complexity in this group is **not** uniformly logarithmic, and the differences are real:

- Sorted bag, set, and map: factories, peeks, `add`/`insert`/`setItem`/`setNextValue`, and both
  deletes are O(log n). Bag deletion routes through the measured sequence's ordinary logarithmic
  `removeAt` with path copying — it does not materialize, splice, and rebuild the bag.
- Canonical zip-zip set: `cursorAtLowerBound`/`cursorAtUpperBound` are O(h) rank descents and `add` is
  O(h), preserving the policy-derived ranks and canonical topology of the ordinary operation. **Peeks
  are O(n)** — they materialize the set with `Array.from` — which makes `findCursor` and both deletes
  O(n) as well. Expected `h` is logarithmic only under the rank policy's documented pseudorandom
  assumptions.
- Priority-search queue: `cursorAt`, `cursorAtLowerBound`, `cursorAtUpperBound`, and
  `cursorAtMinimumPriority` are O(h) — the last reads the root's cached winner key and then performs
  an ordinary key seek, not a walk over a priority order that does not exist. `insert`/`tryInsert` are
  O(h). **Peeks are O(n)** because rank selection materializes the queue with `Array.from`, so
  `findCursor`, `setItem`, `setNext`, and both deletes are O(n). `enumerateAtMost` remains the
  winner-pruned query iterator and a cursor scan must not be presented as a replacement for it.
- Interval tree and interval map: `cursorAtLowerBound` is an O(log n) max-high/last-low measured
  locate, `insert`/`tryInsert` are O(log n), and both cursor deletes call the ordinary logarithmic
  `removeAt`, sharing untouched structure rather than rebuilding. **Peeks are O(n)**, and so are
  `cursorAtUpperBound`, `findCursor`, `findOverlapCursor`, `findContainingCursor`,
  `findOverlapCursorFrom`, and `seekNextOverlap`. **The cursor overlap path is a rank scan over a
  materialized array, not the augmented max-high descent** that the collections' own
  `findOverlap`/`findOverlaps` use; the scan does stop once low endpoints pass the probe's high
  endpoint, and `seekNextOverlap` starts strictly after the focused occurrence so a factory's
  gap-before-hit result cannot rediscover it indefinitely.
- Chunked bit set: honest throughout. With `w` represented words, factories, peeks, `add`, and both
  deletes are O(log w) plus bounded 64-bit `bigint` work, and moves are O(1). Cursor rank spans
  `0 .. count`, where `count` denotes the end gap — unlike `select`, whose domain is `0 .. count - 1`.
  Adding a present bit returns the receiver cursor, and clearing a word's last bit removes that word,
  so no publishable cursor version stores a zero word.

Interval validity is delegated to the owning API: an interval cursor neither normalizes nor rejects an
interval differently from the collection it was built on, and `coalesce` remains a collection-wide
operation with no cursor form.

### Neutral ordered cursors

`PersistentOrderedSetCursor<T>`, `PersistentOrderedMapCursor<K, V>`, and
`PersistentOrderedMultimapCursor<K, V>` are checkpoints over insertion and explicit-position order.
They publish the ordered sequence and the CHAMP index atomically and never expose the private sparse
`bigint` stamps. The multimap cursor is one **flattened grouped pair-rank** axis rather than nested
group and value cursors, and because the outer structure caches no pair-prefix count, its peeks,
pair/group factories, and `add` are O(pairs). Duplicate handling differs across the three: set
insertion is a silent receiver-preserving no-op, map insertion throws `DuplicateKeyError`, and
multimap `add` is a no-op for a duplicate pair. These cursors expose `snapshot` as a readonly property
rather than a method. The complete contract is in [the ordered-collection notes](ordered.md).

The `RangeUpdateSequenceCursor` tag and measure contract — `applyPrevious`/`applyNext`,
`measurePrevious`/`measureNext`, their validation order, and their callback-free zero-length cases —
is documented in the [range-update sequence notes](range-update-sequence.md).

### Deliberately cursor-free

CHAMP maps and sets and every hash composite built on them (bag, bimap, multimap, relation, patch,
directed graph, indexed map), `ConcurrentHashTrie` and its snapshots, the measured priority queue, the
Brodal–Okasaki heap, `DabaLite`, the bulk builders and transient sessions, Merkle blocks, packs,
proofs, and stores expose no cursor. Hash
enumeration has no semantic neighbor, heap topology is private and unstable under meld and
delete-minimum, and the remaining types are mutable lifecycles or scalars rather than persistent
aggregates. Adding any of these surfaces requires a new applicability decision rather than reusing the
APIs above.

## Lazy range-update sequence

`RangeUpdateSequence<Element, Measure, Tag>` is the independent implicit-AVL sequence with cached
ordered measures and algebraic lazy range tags. Each instance retains one exact
`RangeUpdateAlgebra` object. That runtime object replaces the C# static `TOps` parameter and is part
of sequence identity: canonical empties, source factory shortcuts, and concatenation all preserve or
require the exact object.

`compose(newer, older)` represents older-then-newer application. Pending absence uses a separate
boolean and never `undefined`, a default tag, or `identityTag`, so both stored `undefined` elements
and an `undefined` tag remain valid. Structural edits push tags by immutable path copying. Indexed
reads, range queries, and iteration carry inherited tags and allocate no persistent nodes. Empty
updates are callback-free, recognized identities retain the receiver, whole nonidentity updates
replace only the root wrapper, and counts/ranges retain the C# `Int32.MaxValue` boundary and
validation order.

TypeScript iterators are independent snapshot-bound JavaScript iterators rather than C# copyable
struct enumerators. The port consequently makes no struct-copy fail-fast or same-object worker-thread
claim. The complete contract and API mapping are in the
[range-update sequence notes](range-update-sequence.md).

## Exact Merkle interoperability

The Merkle policy domain, canonical codecs, base-16 key levels, wide canonical topology, empty-tree
manifest, and node blocks match `mst-sha256-b16-v2`. `MST2` blocks and `MSP2` query descriptors are
byte-identical to sibling ports. Built-in codecs cover int32, int64, nullable strict UTF-8, nullable
bytes, and RFC-4122 UUIDs. Verification authenticates hashes, domains, codec round trips, ordering,
levels, child intervals, subtree counts, exact reserialization, closure completeness, and seven
finite budgets before publication.

The store API is synchronous because Node's in-memory and common embedded stores are synchronous.
Custom remote stores should stage blocks asynchronously outside the tree, then call verified import
or load against a synchronous snapshot.

## Deliberate non-ports

The repository's frozen CHAMP tier, order-maintenance list, styled-text rope, and other unshipped
frontier/derived-catalog entries remain proposals or explicitly postponed candidates. Benchmark
prototypes are evidence machinery, not package API.
