# Rust FingerTree API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust FingerTree-family port
- Scope: Rust naming, contracts, checkpoint limitations, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-fingertree`, with library name
`tools_data_structures_fingertree`.

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`, with `PersistentDequeCursor<T>` and
  `ReversibleDequeCursor<T>`;
- `DabaLite<T, M>`, `DabaMonoid<T>`, `DabaLiteStatistics`, and the empty/invariant error types;
- `RrbVector<T>`, `RrbVectorCursor<T>`, `RrbVectorBuilder<T>`, and the split/pop/statistics result
  types;
- `CanonicalSortedSet<T>`, `CanonicalSortedSetCursor<T>`, `CanonicalCursorSearch<T>`,
  `ZipTreeRankPolicy<T>`, stable comparer/hash traits and built-ins,
  algebra/diff result types, validation statistics, and policy/invariant errors;
- `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, ordering policy/comparer types,
  validation statistics, and meld/invariant errors;
- `PrioritySearchQueue<K, P, V>`, `PrioritySearchQueueCursor<K, P, V>`,
  `PrioritySearchCursorSearch<K, P, V>`, `PrioritySearchEntry<K, P, V>`, add/remove/minimum result
  handles, borrowing iterators, validation statistics, and range/invariant errors;
- `FingerTree<T, P>` over `MeasurePolicy<T>`, with `FingerTreeCursor<T, P>` and
  `FingerTreeCursorSearch<T, P>`;
- built-in policies `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, `KeyMeasure<T>`,
  `ProductMeasure<T, PFirst, PSecond>` with `MeasurePair<TFirst, TSecond>`, and
  `OrderStatisticMeasure<T>` with `RankedKey<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`, with `SortedBagCursor<T>`,
  `SortedSetCursor<T>`, `SortedMapCursor<K, V>`, `OrderedCursorSearch<C>`, and
  `OrderedCursorInsert<C>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>`, `IntervalTree<T>`, `IntervalTreeCursor<T>`, `IntervalCursorSearch<T>`,
  `PersistentIntervalMap<T, V>`, `PersistentIntervalMapCursor<T, V>`,
  `IntervalMapCursorSearch<T, V>`, and interval-map result types;
- `PersistentChunkedBitSet`, `PersistentChunkedBitSetCursor`, `ChunkedBitSetCursorSearch`,
  `ChunkedBitSetStatistics`, `NegativeBitIndex`, and its invariant error;
- `Rope<T>`, positional `RopeCursor<T>`, `MeasuredRope<T, P>`, `MeasuredRopeCursor<T, P>`,
  `MeasuredRopeCursorSearch<T, P>`, `MeasuredRopeBuilder<T, P>`, `TextRope`, `TextRopeCursor`,
  `TextRopeCursorSearch`, `RopeBuilder`, `NewlineMeasure`, `NewlineStyle`, and `LineColumn`.

The Rust surface follows Rust conventions:

- fallible indexed operations return `Option` instead of throwing;
- duplicate sorted-map insertion returns `Result<_, DuplicateKeyError>`;
- binary search returns `Result<usize, usize>`, matching Rust's insertion-index convention;
- sorted deques expose lower/upper/equal-range splits plus stable upper-bound insertion and complete
  equal-range removal, with `_by` variants for custom orderings;
- reversible-deque `split_at` and `pop_front`/`pop_back` return `ReversibleDequeSplit<T>` and
  `ReversibleDequePop<T>`, so subsequent reversal and orientation-aware edits remain available;
- measure policies are ordinary traits with static functions for identity, element measure, and combine;
- product-measured trees expose component-projected split and locate helpers, plus named size+sum and size+min/max
  aliases for cumulative-weight and priority operations that also retain positional measures;
- text offsets are Unicode-scalar `char` offsets, matching the repository's `Rope<char>`
  interpretation rather than UTF-8 byte offsets; code-point indexes therefore map directly to
  character offsets, while grapheme helpers return UAX #29 extended-cluster boundaries in those
  same offsets;
- character-offset-to-grapheme-index conversion returns the number of cluster starts strictly
  before the offset: an exact boundary maps to that cluster's index, an interior offset counts the
  containing cluster's start, and the end boundary maps to the grapheme count, matching the C#
  contract;
- out-of-range positional rope edits and text offset conversions return `None`.
- out-of-range RRB indexing, splitting, and range edits return `None`; indexing through `Index`
  retains Rust's ordinary panic-on-invalid-index convention.

## Persistent interval map

`PersistentIntervalMap<T, V>` stores one payload per lexicographically unique closed interval and
allows distinct keys to overlap. Unlike `Interval::new`, which asserts its constructor precondition,
the map validates even directly constructed public-field intervals and returns `IntervalMapError`
from exact mutations and overlap queries. `add` is strict; `set_item` is last-value-wins while
retaining the first interval representative, and an equal payload is a storage-sharing no-op.

The measured summary combines the rightmost full `(low, high)` key with maximum high endpoint.
Exact lookup/update/removal therefore avoid same-low scans, while first-overlap search is logarithmic
and full overlap enumeration is output-sensitive. Endpoint order is Rust `Ord`; payload no-op
detection is ordinary `PartialEq`. There is no `coalesce`, because no general payload merge rule is
available.

## Persistent chunked bit set

`PersistentChunkedBitSet` represents nonnegative signed 32-bit bit indexes and stores only nonzero
64-bit words in a measured persistent tree. `from_indices` and `insert` reject a negative index with
`NegativeBitIndex`; `contains` returns false, `remove` is a root-sharing no-op, and inclusive `rank`
returns zero for a negative query. Duplicate insertion and absent removal share the original tree.

`len` and `rank` use `u64` population counts. `rank(index)` counts set indexes less than or equal to
`index`; `select(rank)` uses a zero-based rank and returns `None` when it is outside the population.
Iteration is ascending. Union, intersection, difference, and symmetric difference merge sorted
word streams, omit zero results, and preserve immutable inputs. Structural validation checks strict
word order, nonzero words, and exact cached chunk/population summaries.

Lookup, point edits, rank, and select are logarithmic in the number of represented words, plus
constant 64-bit word work. Algebra is linear in the combined represented-word counts. The signed
32-bit domain is intentional cross-language API parity rather than a Rust machine-word limit.

## Persistent cursor tier

Beyond the rope cursors documented below, the crate ships public cursors for the deque, reversible
deque, RRB vector, general measured finger tree, sorted bag/set/map, canonical zip-zip sorted set,
priority-search queue, interval tree, interval map, and chunked bit set. `PriorityQueue<T, P>`,
`BrodalOkasakiHeap<T>`, `DabaLite<T, M>`, and the RRB/rope builders deliberately have no cursor:
the two heaps expose the minimum as their only semantic location, and the DABA window and builders
are mutable lifecycles rather than persistent aggregate values.

Every one of these is a **Profile R semantic checkpoint** — a retained collection value plus a
validated gap or rank, whose edits delegate to the ordinary persistent operation. None of them
retains context frames, and none inherits the C# rope tier's focused representation, bounded focus,
carry flush, prepared-measure fragments, snapshot memo, callback ceiling, allocation bound, or
amortized-locality claims. The sibling checkpoints in this crate make no such claim either.

Rust ownership provides the invalid-default contract that the C# struct cursors have to state
explicitly. No cursor type implements `Default`, no moved-from state is observable, and use after a
move is a compile-time error, so every nameable cursor is fully initialized. A cursor over an empty
collection is an ordinary value at gap zero for which `is_at_start` and `is_at_end` are both true.

The error channel is **not** uniform across the crate, and callers should not assume it is:

- boundary movement, an out-of-range seek, and an absent neighbor always use `Option`;
- `SortedMapCursor::insert` returns `Result<Self, DuplicateKeyError>`;
- `CanonicalSortedSetCursor::insert` returns `Result<Self, CanonicalSetError>` for an incoherent
  rank hash;
- `PrioritySearchQueueCursor::insert` returns `Result<Self, PrioritySearchAddResult<K, P, V>>`,
  carrying the rejected attempt's result record as its error;
- `PersistentChunkedBitSetCursor::insert` returns `Result<Self, NegativeBitIndex>`; and
- the interval-map cursor validates directly constructed public-field intervals, so
  `cursor_at_lower_bound`, `cursor_at_upper_bound`, `find_cursor`, `find_overlap_cursor`,
  `seek_next_overlap`, `insert`, `set_next_value`, `delete_previous`, and `delete_next` all return
  `Result<_, IntervalMapError>`, with the deletions and `set_next_value` nesting an `Option` inside
  that `Result`.

`snapshot` is likewise not uniform. The sequence cursors — deque, reversible deque, RRB vector,
general measured tree, and the rope family — return an owned root-sharing collection clone, while
the ordered and search cursors — sorted bag/set/map, canonical set, priority-search queue, interval
tree, interval map, and chunked bit set — return a borrow of the retained collection. Neither form
consumes the cursor.

Search factories that can miss return a presence-discriminated record rather than an invalid cursor:
`OrderedCursorSearch<C>` for the sorted family, `FingerTreeCursorSearch<T, P>`,
`CanonicalCursorSearch<T>`, `PrioritySearchCursorSearch<K, P, V>`, `IntervalCursorSearch<T>`,
`IntervalMapCursorSearch<T, V>`, and `ChunkedBitSetCursorSearch`. In all of them `found` reports
presence only. The sorted family additionally exposes `OrderedCursorInsert<C>`, whose `added` field
reports publication; the two are separate types so generic code cannot confuse the discriminators,
and a key reported `found: true` is exactly a key reported `added: false`.

## Sequence cursors

`PersistentDeque::cursor()` and `cursor_at(position)` create `PersistentDequeCursor<T>`;
`ReversibleDeque` and `RrbVector` expose the same pair for `ReversibleDequeCursor<T>` and
`RrbVectorCursor<T>`. `cursor_at` accepts every gap in `0..=len` and returns `None` outside it. All
three share the positional vocabulary `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`,
borrowed `peek_previous` / `peek_next`, `move_previous` / `move_next`, `seek`, `insert`,
`insert_range`, `delete_previous`, `delete_next`, `replace_next`, and `snapshot`. Unlike the Ordered
crate's cursors, `snapshot` here returns an owned root-sharing collection clone rather than a
borrow. Empty-range insertion and seeking to the current position return unchanged root-sharing
cursors.

Gap conventions are uniform in this family: `insert` and `insert_range` leave the gap after the
inserted values, `delete_previous` implements backspace and moves the gap left, and `delete_next`
and `replace_next` keep it fixed. Sequences have no element-equality policy, so `replace_next` is
unconditional on the deque and reversible deque; the RRB vector's `replace_next` routes through
`set_item` and therefore applies the vector's existing equal-element root-preserving rule, which is
why it alone requires `T: Clone + PartialEq` while its insertions and deletions require only
`T: Clone`.

The deque and reversible-deque cursors impose **no** `T: Clone` bound at all, including on edits:
the `Arc`-shared balanced tree splits and concatenates by sharing nodes rather than copying stored
values. This is a genuine difference from `RopeCursor<T>`, whose chunked substrate must clone the
affected chunk.

`ReversibleDequeCursor::reverse()` additionally reverses the logical snapshot and maps the gap from
`p` to `len - p`, swapping the logical sides. It returns `Self` unconditionally, since every valid
gap has a valid mirror. Reversal is O(1) because the underlying reversible deque wraps or cancels a
shared mirrored-tree view rather than materializing elements.

`FingerTree<T, P>` uses a measure-aware cursor instead of a positional one, because an arbitrary
monoid cannot be interpreted as an index. `cursor_at_start()` and `cursor_at_end()` are O(1) total
factories, and `cursor_by_measure(predicate)` returns `FingerTreeCursorSearch<T, P>` locating the
gap before the first element whose inclusive prefix satisfies a lawful monotone predicate; a miss
reports `found: false` with a usable end cursor. `FingerTreeCursor<T, P>` therefore exposes
`is_at_start`, `is_at_end`, `measure_before`, `measure_after`, borrowed peeks, unit movement,
`seek_by_measure`, `insert`, `delete_previous`, `delete_next`, `replace_next`, and `snapshot` — and
deliberately **no** `len`, `position`, or positional `seek`, so no fabricated element count leaks
into an arbitrary-monoid API. Combining `measure_before` with `measure_after` in that order yields
the snapshot measure; no inverse or commutativity is assumed. Like the deque cursors, none of its
operations requires `T: Clone`.

Complexity for this family, with `n` the length and `m` an inserted range size:

| Operation | Deque / reversible deque | RRB vector | Measured finger tree |
| --- | --- | --- | --- |
| Create, clone, unit move, `seek`, `snapshot` | O(1) | O(1) | O(1) create/move/snapshot |
| `peek_previous` / `peek_next` | O(log n) | O(log32 n) | O(log n) |
| `insert`, `delete_previous`, `delete_next`, `replace_next` | O(log n) | O(log32 n) plus seam rebalancing | O(log n) split plus concat |
| `insert_range` of `m` values | O(m + log n) | O(m + log32 n); `insert_vector` splices a built vector with sharing | not offered |
| `measure_before` | — | — | O(log n) read-only descent, no allocation |
| `measure_after` | — | — | O(log n), but performs a full structural `split_at_index` and allocates a discarded left tree |
| `reverse` | O(1) | — | — |
| Traverse `k` neighbors after one seek | O(k log n) | O(k log32 n) | O(k log n) |

The asymmetry between `measure_before` and `measure_after` on the finger-tree cursor is real and
deliberate: the prefix measure is a read-only descent that reuses cached node measures, while the
suffix measure is currently obtained by splitting the tree at the gap and reading the right side's
cached root measure. Both are logarithmic, but only the latter allocates. Because no port retains
radix or digit frames, a linear scan after one seek costs one lookup per step; the O(k + log32 n)
and O(1)-amortized figures in the repository cursor design belong to the unimplemented focused tier.

## Ordered and search cursors

The comparator- and rank-ordered families all use the shared gap protocol: `position` counts entries
before the gap, the next entry is the search candidate and the target of forward deletion or value
update, before-first is gap zero, and after-last is gap `len`. A miss is an ordinary usable cursor,
not an invalid one. Every factory retains the source collection's exact comparator, rank policy, or
priority policy, including on an empty result.

### Sorted bag, set, and map

`SortedBag`, `SortedSet`, and `SortedMap` each offer `cursor_at(rank)` returning `Option`, total
`cursor_at_lower_bound` and `cursor_at_upper_bound`, and `find_cursor` returning
`OrderedCursorSearch`. The cursors share `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`,
borrowed peeks, `move_previous` / `move_next`, `seek_rank`, and a borrowing `snapshot`.

`SortedBagCursor::add(value)` finds the **upper** bound and inserts after every existing
comparer-equal occurrence, preserving the bag's stable equal-item rule, and returns the gap after
the new occurrence. It exposes no arbitrary `insert_here` and no replacement, because either could
create an order not obtainable from the ordinary bag API. `delete_previous` and `delete_next` remove
the exact stored occurrence at the gap.

`SortedSetCursor::add(value)` inserts an absent class at its lower bound. A duplicate is the
collection's root-sharing no-op that retains the stored representative, but note that the cursor
still reports the gap as `lower_bound + 1` in both cases, so after adding an already present value
the gap sits immediately **after** the retained representative. There is no `replace_next`, because
representative replacement could collide with another class.

`SortedMapCursor` adds `insert` (strict, `Result<Self, DuplicateKeyError>`), `try_insert` (returning
`OrderedCursorInsert`), `set_item` (insert-or-update by key), `set_next_value` (focus-local payload
update that clones and retains the stored key and keeps the gap fixed), and keyed
`delete_previous` / `delete_next`. `try_insert` on a duplicate reports `added: false` and leaves the
gap at the retained entry's lower bound rather than one past it. No key-rename operation ships.

All three are backed by order-statistic measured trees, so rank selection, lower/upper bounds,
peeks, insertions, and deletions are each O(log n), and a complete post-seek traversal by
move-plus-peek is O(n log n) rather than O(n).

### Canonical zip-zip sorted set

`CanonicalSortedSet` offers `cursor_at(rank)`, `cursor_at_lower_bound`, `cursor_at_upper_bound`, and
`find_cursor` returning `CanonicalCursorSearch<T>`. The cursor retains the exact
`ZipTreeRankPolicy<T>` handle, so a snapshot reproduces canonical topology; `find_cursor` compares
through that policy's comparer rather than `PartialEq`. `insert` returns
`Result<Self, CanonicalSetError>` and propagates `IncoherentRankHash`; a duplicate is the ordinary
root-sharing no-op that retains the old representative, and the resulting gap is one past the
retained equivalent item. `delete_previous` and `delete_next` clone the focused item and route
through the ordinary `remove`, so the closed topology equals what the ordinary operation produces
for the same policy and contents.

Rank selection and bound search descend the Cartesian tree using cached subtree counts, so peeks,
bound factories, and edits are **O(h)** — not unconditionally O(log n). Expected logarithmic height
depends on the documented coherent pseudorandom rank assumptions; a constant or degenerate rank hash
makes `h = n`. Navigation, `position`, and `len` require no `T: Clone`; only the edits do.

### Priority-search queue

`PrioritySearchQueue` offers `cursor_at(rank)`, `cursor_at_lower_bound`, `cursor_at_upper_bound`,
`find_cursor` returning `PrioritySearchCursorSearch<K, P, V>`, and `cursor_at_minimum_priority()`.
The last reads the root's cached O(1) winner and then performs an ordinary key seek; it does not
walk a priority-ordered sequence, because key order is the only navigation order and priority
remains cached augmentation.

`insert` returns `Result<Self, PrioritySearchAddResult<K, P, V>>`, using the rejected attempt's own
result record as the error payload. `set_item` inserts at a miss or updates an exact hit;
`set_next` is the focus-local form that retains the stored key representative and keeps the gap
fixed, applying the queue's existing priority/value no-op rule. `delete_previous` and `delete_next`
name their target through the retained `PrioritySearchEntry` key handle rather than cloning `K`, so
cursor deletion extends the family's non-`Clone` support; only `insert`, `set_item`, and `set_next`
require `K: Clone, P: Clone + PartialEq, V: Clone + PartialEq`.

The queue is an AVL tree caching count, height, and the subtree winner, so bound search and rank
selection descend with cached counts in O(log n), and edits recompute winners along the changed path
in O(log n). `enumerate_at_most` remains the winner-pruned output-sensitive query; a cursor scan is
not a substitute for it and does not claim its pruning bound.

### Interval tree and interval map

`IntervalTree` offers `cursor_at(rank)`, `cursor_at_lower_bound(low)`, `cursor_at_upper_bound(low)`,
`find_cursor(interval)`, `find_overlap_cursor(probe)`, and `find_containing_cursor(point)`;
`IntervalTreeCursor<T>` adds `seek_next_overlap(probe)`, which searches strictly after the currently
focused occurrence so a factory's gap-before-hit result cannot rediscover the same occurrence
forever. Order is nondecreasing low endpoint. `find_cursor` matches both endpoints by `Ord::cmp`,
agreeing with `index_of`, and a miss returns the low-endpoint lower bound. `insert` uses the
facade's defined low-bound placement and returns the gap after it; `delete_previous` and
`delete_next` remove the exact occurrence the cursor addresses, which disambiguates duplicate
intervals. Endpoint replacement is not offered, because changing an endpoint can move the interval.

`PersistentIntervalMap` mirrors that surface over the unique complete `(low, high)` key, adding
`set_next_value`, and wraps every fallible member in `Result<_, IntervalMapError>` so that a
directly constructed invalid public-field `Interval` is rejected exactly as the ordinary map rejects
it. `insert` is strict and `set_next_value` retains the stored interval representative while
applying the payload `PartialEq` no-op rule.

Complexity here must be read carefully, because the cursor paths and the ordinary query paths differ:

- Interval-map `cursor_at_lower_bound`, `cursor_at_upper_bound`, `find_cursor`, and the peeks use
  the measured last-interval summary and are O(log n). Its `insert`, `set_next_value`, and deletions
  are the ordinary O(log n) operations.
- Interval-tree `cursor_at_lower_bound`, `cursor_at_upper_bound`, and the peeks are likewise
  O(log n) through the last-low summary.
- Interval-tree `find_cursor` locates the low-endpoint lower bound logarithmically but then walks
  the in-order iterator from rank zero to that bound and scans the equal-low run linearly, so it is
  O(position + d) for an equal-low run of length `d`, i.e. O(n) worst case.
- **Every overlap cursor path is a linear in-order scan, not an augmented descent.**
  `find_overlap_cursor`, `find_containing_cursor`, and `seek_next_overlap` on both the tree and the
  map advance the iterator to the start rank, then scan forward while low endpoints do not exceed
  the probe's high endpoint. That is O(start + r) for `r` scanned candidates and O(n) worst case.
  The collections' own `overlaps` and overlap-enumeration queries do use the cached maximum-high
  augmentation and keep their O(log n) first-hit and O((k + 1) log n) all-hits bounds; the cursor
  factories currently do not, and no augmented bound is claimed for them.

### Chunked bit set

`PersistentChunkedBitSet` offers `cursor_at(populationRank)` returning `Option` over `0..=len` and
total `cursor_at_or_after(bit_index)`, plus `find_cursor(bit_index)` returning
`ChunkedBitSetCursorSearch`. The cursor traverses present set bits rather than a dense Boolean
sequence: `peek_previous` and `peek_next` return `Option<i32>` bit indexes, and `position` and `len`
use the same `u64` width as the population count. A negative search index clamps to the start gap,
following the collection's nonthrowing lookup convention, while `insert` retains the negative-index
validation and returns `Result<Self, NegativeBitIndex>`.

`insert(bit_index)` on a present bit is an identity no-op returning a root-sharing cursor at the
unchanged gap; on an absent bit it returns the gap immediately after the new bit.
`delete_previous` and `delete_next` clear the exact neighboring bit and follow the family's gap
rules. Set algebra remains a sparse word-stream operation and is deliberately not a cursor
primitive.

Peeks and edits are `select` and `rank` operations: logarithmic in the number of represented nonzero
words plus constant 64-bit word work. Rank movement and `seek_rank` are O(1), so a full traversal by
move-plus-peek pays one logarithmic `select` per step.

## Positional rope cursor

`Rope::cursor()` creates an immutable cursor at gap zero; `cursor_at(position)` accepts every gap in
`0..=len` and returns `None` outside that range. `RopeCursor<T>` owns a cheap root-sharing `Rope<T>`
snapshot and a gap position. It intentionally has no `Default` and is `Send + Sync` whenever `T`
is `Send + Sync`. `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`, borrowed
`peek_previous`/`peek_next`, `move_previous`/`move_next`, `seek`, and `snapshot` expose navigation
without requiring `T: Clone`. Boundary movement and invalid seek return `None`. Seeking to the
current position and inserting an empty range return unchanged root-sharing cursor values.

Edits return new cursors and never mutate the receiver, so any retained cursor can form an
independent branch. `insert` and `insert_range` leave the new gap after the inserted values;
`delete_previous` implements backspace and moves the gap left; `delete_next` and `replace_next`
keep the gap fixed. `replace_next` is unconditional: it invokes no equality comparison and creates
an edited snapshot even for an equal replacement. Missing previous/next elements return `None`.
These edit methods require `T: Clone` because the current chunked rope substrate clones the affected
chunk; the bound is not imposed on construction, navigation, peeking, seeking, or snapshotting.
Cached positional-rope lengths use checked `usize` addition. Any construction or growth operation
whose resulting length is unrepresentable, including concatenation, ordinary rope insertion, and
cursor insertion, panics before returning; all input ropes and cursors remain valid.

This is a semantic positional checkpoint, not a port of the C# focused cursor representation. Cursor creation,
cloning, movement, seek, and snapshot are O(1). Peeks and point edits are O(log n) plus bounded chunk
work; inserting `m` values is O(m + log n). No O(1)-amortized local-edit claim is made.

## Measured and text rope cursors

`MeasuredRope::cursor`, `cursor_at`, and `cursor_by_measure` create an opaque
`MeasuredRopeCursor<T, P>` over the exact retained measured-rope root. It has the positional
cursor's gap, boundary, borrowed peek, movement, seek, edit, branching, and snapshot semantics.
`measure_before` and `measure_after` preserve monoid order; they do not assume commutativity or an
inverse. Construction, movement, positional seek, and snapshot are O(1). Prefix/suffix measures,
peeks, and absolute measure search are O(log n) plus bounded chunk work; edits retain the measured
rope's O(log n) plus bounded chunk copy, and inserting `m` elements is O(m + log n). Navigation,
measurement, search, and snapshotting do not require `T: Clone`; edits do.

`cursor_by_measure` and `MeasuredRopeCursor::seek_by_measure` apply a caller-supplied lawful
monotone predicate to absolute prefixes of the complete retained version, independent of the
receiver's current gap. `MeasuredRopeCursorSearch<T, P>` contains the first matching element's gap
and `found == true`; a miss contains `found == false` and a usable end cursor. A predicate panic
publishes no state, so the source and receiver remain reusable. Count growth uses checked `usize`
preflights before element-measure callbacks; concatenation, builder append, ordinary insertion, and
cursor insertion panic without publishing when the resulting count is unrepresentable.

`TextRope::cursor`, `cursor_at`, and `cursor_by_measure` return the nominal `TextRopeCursor` facade
over `MeasuredRopeCursor<char, NewlineMeasure>`. It preserves the exact `TextRope` facade on
snapshot, exposes the full measured cursor vocabulary, and adds `line_column` at the gap. Offsets
and columns count Unicode scalar values. The measure counts only `\n`, exactly like `TextRope`;
CRLF recognition and grapheme segmentation remain explicit text-extra operations rather than
cursor addressing rules. This sibling checkpoint claims no C# focused cursor representation, allocation, or
amortized-locality claim.

## Brodal-Okasaki heap

`BrodalOkasakiHeap<T>` is the direct bootstrapped skew-binomial representation, not an adapter over
the measured priority queue. A rank-zero global tree stores the minimum, and each skew-binomial
tree fuses its primitive children with the embedded heap forest from Brodal and Okasaki. Insert,
minimum, and meld are worst-case O(1); delete-min is worst-case O(log n).

`OrderPolicy<T>` retains a `Send + Sync` `OrderComparer<T>`. Natural policies are marked canonical,
so two independently constructed natural heaps can meld. Cloning a custom policy preserves its
identity; `with_shared_comparer` also lets independently constructed heaps retain clones of the same
caller-owned comparer `Arc`, matching comparer-object identity in the managed reference.
Independently constructed custom policies are rejected by `meld` with
`BrodalMeldError`, even when their functions happen to compare identically. This is the Rust form
of the C# comparer-object compatibility contract.

Values and all tree links use `Arc`. Consequently construction, insertion, meld, minimum,
delete-min, iteration, validation, and sharing diagnostics impose no `T: Clone` bound.
`minimum` returns `Option<&T>`. `minimum_view` returns `Option<BrodalMinimumView<T>>`; its
`Arc<T>` minimum is an owned shared handle that remains valid independently of the source and
remainder. In particular, a stored `Option<T>` value of `None` is distinct from an empty heap.

Structural-order iteration is explicit-stack and comparison-free. `validate_structure` uses an
explicit worklist to audit the global rank-zero root, every fused primitive/embedded boundary,
skew-forest rank rules, parent/child order, logical count, maximum rank, and maximum depth.
`shared_tree_count` and `shares_root_with` are read-only identity diagnostics for persistence
audits. See [Brodal-Okasaki heap](brodal-okasaki-heap.md) for the representation and validation
contract in one place.

## Priority-search queue

`PrioritySearchQueue<K, P, V>` is an immutable AVL ordered map with one entry per key-policy
equivalence class. Each node caches count, height, and the minimum-priority entry in its complete
subtree. Keyed lookup, insertion, replacement, removal, and winner deletion are O(log n); global
minimum is O(1). Equal priorities break by retained key order, so the winner is deterministic.

The first concrete key representative is permanent for the life of an equivalence class. Bulk
construction and `set_item` replace its priority and payload last-wins. `set_item` reuses the exact
root only when the priority policy reports equality, ordinary `P: PartialEq` also reports equality,
and `V: PartialEq` reports equal payloads. Those two equality bounds occur only on `set_item`;
bulk last-wins construction, `try_add`, reads, iteration, removal, minimum deletion, validation,
and diagnostics impose neither equality nor `Clone` bounds.

`PrioritySearchEntry<K, P, V>` owns `Arc` handles for its three components. Borrowed accessors avoid
copies, while component-handle accessors and the owned remove/minimum results remain usable after
the source queue is dropped. This makes `Option` components explicit: outer `Option<Entry>` means
presence or absence, independently of `None` stored inside the key, priority, or payload.

`enumerate_at_most` eagerly rejects an inverted key range with `PrioritySearchRangeError`, then
returns a lazy borrowing iterator. Bounds are inclusive, output is in key order, and a subtree is
pruned when its cached winner exceeds the inclusive priority threshold. The traversal uses an
explicit stack and requires no component cloning. Its lifetime deliberately borrows the queue and
three query values instead of cloning generic bounds.

`validate_structure` explicitly audits strict key bounds, AVL balance, cached count/height, and
winner-handle identity at every node. `shared_node_count`, `shares_root_with`, and
`shares_node_for_key` provide read-only persistence diagnostics. See
[Priority-search queue](priority-search-queue.md) for the complete operation and evidence map.

## DABA Lite FIFO-window aggregation

`DabaLite<T, M>` is the crate's intentionally mutable streaming core. `M: DabaMonoid<T>` supplies
static `empty` and ordered `combine` callbacks; the built-in `SumMeasure<T>` also implements this
trait when `T` has its existing sum-policy bounds. The monoid must be associative with a two-sided
identity but need not be commutative or invertible.

```rust
use tools_data_structures_fingertree::{DabaLite, SumMeasure};

let mut window = DabaLite::<i64, SumMeasure<i64>>::new();
window.insert(5);
window.insert(8);
window.insert(13);
assert_eq!(window.aggregate(), 26);

window.evict().unwrap();
assert_eq!(window.aggregate(), 21);
assert_eq!(window.validate_structure().unwrap().len, 2);
```

The six cursor order is `F <= L <= R <= A <= B <= E`. Each insertion or eviction executes exactly
one bounded fixup: front exhaustion, flip initialization, the `L == R` shift, or one paired partial-
aggregate rewrite. There is no reversal loop. `insert`, `try_evict`/`evict`, and `aggregate` invoke
`combine` at most three, two, and one times respectively. A nonempty query invokes it exactly once;
an empty query calls `empty` and invokes no combine. These are callback-count bounds independent of
window size, while the complete operation is worst-case O(1) only if the callbacks are O(1).

All callback-derived values and a possible successor chunk are planned before any mutation is
published. If `empty` or `combine` unwinds from an insertion, eviction, or nonempty clear, the exact
length, six cursors, active links, slots, and aggregate fields remain unchanged. Callback side
effects, including effects through interior mutability, are not rolled back. The guarantee is
specifically callback panic safety; a user-defined `T::drop` that itself panics is outside it.

`evict` returns `Err(EmptyDabaLiteError)` for an empty window, while `try_evict` returns `false`.
Successful eviction clears the retired slot immediately and detaches a predecessor chunk as soon as
`F` crosses the boundary. `clear` obtains `empty` once and invokes no combine before replacing the
state with one empty chunk. It then iteratively severs the old chain and drops every slot. This is an
intentional Rust/C# complexity divergence: safe generic Rust cannot both release arbitrary owned
values promptly and perform only O(1) destructor work, so Rust `clear` is O(n + c) for `n` values in
`c` chunks. It neither leaks nor defers an unbounded retired chain.

Queue positions are partial-aggregate storage rather than stable originals, so the API intentionally
has no peek, value-returning eviction, or iterator. `validate_structure` is callback-free and checks
the bidirectional chunk links, acyclicity, cursor reachability/order, count and DABA region equations,
and the one-to-127-slot nonempty slack bound. It returns front/back/work-region lengths and physical
chunk capacity, but cannot reconstruct content for an arbitrary non-invertible monoid; tests compare
the aggregate with an external FIFO model.

For `n` live positions, the linked queue allocates `n` slots plus one through 127 slack slots; an
empty window owns one 64-slot chunk. Each occupied slot and each of the two aggregate fields holds an
`Rc<T>`. That bounded indirection lets transactional planning retain old and candidate aggregates
without imposing `T: Clone`; tests exercise a non-`Clone` monoid value. Six cursors, weak backward
links, and strong forward chunk links are additional metadata.

The safe stable-cursor representation uses `Rc<RefCell<_>>`, making `DabaLite` neither `Send` nor
`Sync`. Keep each instance on one thread and mutate it through exclusive `&mut self`; the type does
not provide snapshots or internal synchronization.

## Canonical zip-zip sorted set

`CanonicalSortedSet<T>` is an immutable Cartesian binary-search tree over `Arc` nodes. A retained
`ZipTreeRankPolicy<T>` owns two `Send + Sync` trait objects: `ZipTreeComparer<T>` defines both order
and set equivalence, while `ZipTreeRankHash<T>` supplies a deterministic 64-bit value that must be
constant on every comparer-equivalence class. `NaturalZipTreeComparer` and
`StableZipTreeRankHash` provide an explicit natural-order path for fixed-width integers, `bool`,
`char`, UTF-8 strings, and byte strings. The stable-hash trait deliberately omits platform-sized
integers and `std::hash::Hash`; Rust's randomized or implementation-defined hashers never influence
canonical topology.

The policy encodes the 64-bit rank hash in big-endian order and applies HMAC-SHA-256. The first
three big-endian words supply geometric rank (leading-zero count), secondary rank, and subtree
digest content. Heap order compares the first two coordinates descending and comparer order
ascending as the final tie-break. Consequently rank collisions do not lose correctness or
canonicality, but a constant rank hash makes height equal to count.

Policy construction exposes the C# trust modes without a hash-unstable default:

- `random` / `random_natural` obtain a fresh unexposed 32-byte key from the operating system. Each
  call creates an independent policy; clone the handle to retain identity across versions.
- `seeded` / `seeded_natural` derive the key as SHA-256 of ASCII `ZZT2` followed by the public
  seed's eight big-endian bytes. This reproduces ranks but is not adversarial security.
- `keyed` / `keyed_natural` require at least 32 bytes, copy and hide them, and let callers reproduce
  ranks across processes while retaining responsibility for key protection.

RustCrypto's `hmac` and `sha2` implement the primitive, and `getrandom` supplies fresh keys. HMAC
cannot restore entropy discarded by the 64-bit input hash, make an incoherent hash coherent, or
prevent deliberate degeneration under a public seed. Key bytes are redacted from `Debug`, but the
type does not promise memory locking or zeroization. The 64-bit memoized `content_hash` is a fast
same-policy inequality filter, not a cryptographic commitment or equality proof.

`from_items` comparer-sorts and retains the first concrete representative in each equivalence class
before an O(n) Cartesian freeze; total bulk cost is O(n log n). It takes ownership directly and
does not require `T: Clone`. Neither do construction of an empty set, lookup, containment,
iteration, digesting, topology diagnostics, validation, clear, version-identity checks, or same- and
cross-policy equality. `T: Clone` is required only by operations that must reproduce owned items:
`insert` and `remove` path-copy existing nodes, algebra composes those updates, and `diff` returns
owned vectors.

`insert` returns `CanonicalSetError::IncoherentRankHash` if an equivalent representative derives a
different rank. It otherwise retains the old representative and shared root on duplicates. Absent
removal, empty clear, self algebra, and difference by an empty set likewise preserve
`is_same_version`. Localized edits path-copy O(h) nodes and share untouched subtrees; all
traversals, updates, digest work, and destruction use explicit stacks so height-n collision cases
remain call-stack safe.

`union`, `intersection`, `except`, and `diff` require the exact same retained policy object and
return `IncompatiblePolicy` for independently reconstructed policy handles. This prevents mixing
rank spaces silently. `set_equals` still compares mathematical contents across policies under the
receiver's comparer. It stably sorts and deduplicates borrowed references from the other set, so it
does not clone elements; differing comparer semantics can intentionally make the receiver-defined
result asymmetric. Same-policy equality first rejects count or memoized-digest differences and
then walks canonical topology in lockstep while pruning shared nodes. `diff` returns owned
`only_in_left` and `only_in_right` vectors in comparer order.

`validate_structure` checks rank reproducibility, strict search order, heap priority, cached count
and height, and root metadata. It reports count, height, largest geometric coordinate, and repeated
geometric/secondary priority pairs. Immutable sets and policies are `Send + Sync` when `T` is; the
memoized digest uses `OnceLock` for safe concurrent publication.

This workspace is a semantic checkpoint, not the final lazy finger-tree representation. Its persistent families
preserve immutable snapshot behavior, stable observable ordering, rank/range semantics, priority stability,
closed-interval overlap semantics, and text line navigation. `PersistentDeque<T>` has moved past the initial
vector snapshot and now uses an `Arc`-shared balanced tree, so nontrivial splits, concatenations, range operations,
and point updates share
unchanged subtrees. `ReversibleDeque<T>` is now an O(1) mirrored-tree view over that deque: reverse wraps or
cancels a shared tree view, and reversed/mixed-orientation endpoint operations, splits, and concatenations stay on
the tree path instead of materializing vectors. Split/pop results preserve that wrapper, and `iter`, borrowed
`IntoIterator`, and owned `IntoIterator` enumerate in logical order. Deque nodes cache endpoint leaf signposts;
sorted bounds therefore descend once in O(log n) node visits, and the sorted split/equal-range/insert/remove
operations reuse those bounds while preserving shared subtrees. `Rope<T>` now uses chunked length-measured
storage over the shared measured tree, so chunk construction, `copy_to`, positional edits, slices, splits, and
concatenations share unchanged chunks and measured subtrees; `MeasuredRope<T, P>` now provides the same
positional insert/remove/range/slice vocabulary while preserving cached user measures. Its immutable
measured cursor adds ordered prefix/suffix measures and absolute prefix search; the text-specialized cursor
retains the nominal text facade and its scalar-offset line semantics. Its mutable append builder
keeps an immutable measured-rope prefix plus one staged chunk: freezing publishes that chunk, and later appends
share rather than mutate earlier snapshots. `TextRope` stores characters in
`MeasuredRope<char, NewlineMeasure>` so line counts, line starts, and line/column navigation use cached newline
measures, with Rust-native string conversion and display helpers. Character ropes and text ropes additionally
classify LF/CRLF/CR/mixed newline input, strip the CR from CRLF-aware line text, stream Unicode scalar values,
and materialize only for standards-compliant extended-grapheme segmentation via `unicode-segmentation`. The
general `FingerTree<T, P>` now
uses an `Arc`-shared measured tree with cached monoid measures at every node, so measure-guided split and locate
operations can skip whole subtrees and split results share unchanged structure. Built-in `KeyMeasure<T>` and
`ProductMeasure<T, PFirst, PSecond>` policies now cover the C# headline measure compositions: lower/upper-bound
splits over sorted key-measured trees; component-projected splits/finds/locates for arbitrary product measures;
size+sum cumulative-weight splits/selection; and size+min/max peek/extract operations that preserve a positional
count component. `MeasuredRope<T, P>` indexed splits, concatenation, point replacement, prefix measurement, and
measure-guided locate share unchanged chunks and measured subtrees. `PriorityQueue<T, P>` now reuses the measured
tree through an internal minimum-priority measure, so peek/dequeue locate the first global-minimum entry by cached
prefix measures while preserving equal-priority stability. `IntervalTree<T>` now reuses the measured tree through
a last-low/maximum-high product summary. Overlap and containment queries structurally restrict the low-sorted
candidate prefix and then descend directly to each hit, taking O(log n) for the first hit and
O((k + 1) log n) for all `k` hits without scanning irrelevant intervals. Sorted bag/set/map facades now reuse
the measured tree through cached order-statistic measures: rank, inclusive value/key range, and key-boundary
operations locate by count plus last-key prefixes, while edits and range extraction preserve unchanged measured
subtrees. Sorted-set algebra merges two streaming tree iterators in O(n + m) traversal work instead of performing
a rank descent per element. These derived facades still do not claim
the C#/C++ lazy measured-spine complexity or allocation profile for every operation.

`RrbVector<T>` ports the hardened C# relaxed radix-balanced representation. Leaves contain at most
32 contiguous elements in shared `Arc<[T]>` backing arrays; leaf slices created by split retain that
backing without cloning elements. Regular 32-way branches omit cumulative tables and select children
from five-bit radix spans. Relaxed branches alone store prefix sizes. Indexed reads, borrowed
iteration, split, range extraction, and endpoint removal therefore impose no `Clone` bound. Point
replacement, concat, payload-redistributing range edits, owned pops, owned iteration, and `to_vec`
require `T: Clone` only where Rust ownership requires copying stored values. Equal point
replacement, empty insertion/removal, empty-side concat, and boundary splits preserve root identity
where their result is unchanged. `validate_structure` reports count, height, leaf density, branching,
and regular/relaxed-node statistics while checking every cached layout invariant and the
`floor((usize::BITS - 1) / 5) + 1` height cap (thirteen on supported 64-bit targets). The extra
level admits the legal boundary-only `minimum height + 1` slack in the top count band. Concat
redistributes only the boundary seam; it does not certify
global minimum occupancy, so adversarial density ceilings remain test gates. The append builder
moves staged leaves into immutable nodes, retains an adopted vector as an O(1) prefix, and returns
the same root on repeated clean freezes. As in C#, there is no dedicated persistent tail buffer;
immutable endpoint insertion remains a boundary-spine operation.

Future representation work should keep the Rust public names and result shapes stable while replacing the remaining
semantic-checkpoint algorithms with lazy measured-spine equivalents where needed for asymptotic parity.

## External dependencies

Extended-grapheme segmentation uses `unicode-segmentation` 1.13.3, licensed `MIT OR Apache-2.0`.
Canonical rank derivation uses RustCrypto `hmac` 0.12.1 and `sha2` 0.10.9, also licensed
`MIT OR Apache-2.0`; fresh key generation uses `getrandom` 0.3.4 under `MIT OR Apache-2.0`. Cargo
resolves and pins these crates in the repository's `src/Rust/Cargo.lock`; their sources are fetched
from crates.io and are not vendored into this repository.
