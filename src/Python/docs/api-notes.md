# Python API and semantic notes

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

This document records Python-specific mappings for the repository's shared semantic contracts.
The package source and executable tests are authoritative where an API detail is not repeated here.

## Runtime mappings

- Persistent collections are immutable values. Updates return a new snapshot; identity-preserving
  no-ops return the original object where the contract makes that observable.
- Hash collections accept a runtime `HashPolicy`. The default follows Python equality and hash
  semantics, with an identity fallback for values that do not provide a hash.
- CHAMP edit sessions are explicitly one-way and version-bound. They provide the shared transient
  lifecycle but make no in-place-update performance claim.
- `ConcurrentHashTrie` is a thread-safe, `RLock`-coordinated Python consumer-semantic facade with
  constant-time immutable snapshots. It is not the lock-free GCAS/RDCSS Ctrie of the managed
  reference workspaces and makes no lock-free progress claim.
- Python `int` supplies the storage substrate for Patricia long keys, fixed-width integers, and
  sparse integers. Public boundaries still enforce signed 32-bit keys and fixed-width wrapping,
  checked arithmetic, byte order, and two's-complement behavior.
- Strings are indexed by Python Unicode code point. Consequently `TextRope` positions differ from
  UTF-16-code-unit workspaces for non-BMP characters; all internal Python text APIs use one
  consistent coordinate system.
- Python ordered-set iteration uses ordinary independent, snapshot-bound iterators. It does not
  reproduce the shared mutable-copy state of the C# value-type enumerator.
- `RangeUpdateSequence` likewise exposes ordinary independent, snapshot-bound Python iterators.
  It does not emulate C# `Current`, `Reset`, disposal, or fail-fast copies of a value enumerator.

## Hash collections

`PersistentHashMap` and `PersistentHashSet` use immutable 32-way CHAMP bitmap nodes and full-hash
collision buckets. Point edits path-copy only the affected spine, comparer-equivalent replacement
keeps the stored key representative, and semantic no-ops keep object identity. Algebra between maps
requires the same policy object where structural compatibility matters; set algebra accepts any
iterable and applies the receiver's policy.

`ConcurrentHashTrie` serializes each public operation with one reentrant lock and publishes whole
immutable CHAMP roots. `snapshot()` captures the current root in O(1); its lookup, presence-aware
entry, canonical iteration, and persistent-map conversion remain stable across every later
publication. `generation` advances exactly once for each changed root actually published by this
facade. Duplicate additions, equal-value sets or computes, missing removals, and clearing an empty
facade do not advance it. A stored `None` remains distinct from absence through `contains_key` and
`get_entry` even though the convenience `get` method is nullable.

Every policy-driven mutation captures the root on which it begins its CHAMP traversal. If a
same-thread `HashPolicy.hash` or `HashPolicy.equivalent` callback re-enters the facade and publishes
a different root, `set`, `try_add`, `remove`, and `compute` discard the obsolete successor and retry
against the nested publication. Their result is therefore determined by the latest stable root:
an equivalent nested insertion makes an outer `try_add` return `False`, while `remove` returns the
representative and value from the root it finally removes. Different-key nested updates remain in
the outer successor. Each changed nested and outer publication advances `generation` separately.
This deterministic retry contract assumes, as any reentrant callback contract must, that callbacks
eventually stop publishing new roots so the outer operation can finish.

`get_or_put(key, factory)` skips its factory on a hit and passes the caller's lookup key on a miss.
Because `RLock` allows same-thread reentry, it checks the latest root again after the callback; an
equivalent entry published by a nested operation wins and keeps that nested operation's stored key
and value representatives. Its user factory runs at most once; policy-callback retries reuse the
already computed candidate. `compute(key, add, update)` likewise passes the caller's lookup key on
both branches, never the retained stored key; the update branch also receives the latest stored
value representative. It computes against a captured immutable root and publishes only while that
exact root is still current. If a callback re-enters the facade and changes the root, `compute`
discards its stale successor and retries against the nested publication. Compute factories must
therefore tolerate repeated invocation. A callback or policy failure before the outer publication
leaves the outer operation unpublished; already completed nested publications are independent
operations and are not rolled back. These are consumer semantics, not an emulation of managed
Ctrie internals or their progress guarantee.

`PersistentHashMap.get_or_add(key, add_factory)` and
`add_or_update(key, add_factory, update_factory)` hash and descend once, validate factories before
hashing, and call exactly the selected factory at most once. Both return a frozen `MapUpdateResult`
containing the successor map and concrete selected value. Hits retain the stored key and value
representatives, including a stored `None`; updates receive the caller's key and stored value. A
factory failure leaves the source untouched. Stored-value no-op detection first checks object
identity and otherwise uses ordinary Python `==`; value-equality exceptions propagate without
publishing a successor.

`HashMapBulkBuilder` constructs an independent map by mutating unpublished leaf, collision, and
bitmap nodes. Its public surface is deliberately limited to policy/count state, `set_item`,
`set_items`, and `to_immutable`. Duplicate keys keep their first representative, the last
Python-distinct value wins, and an equal value keeps the earlier object. Each freeze copies the
node topology without policy callbacks, remains detached from earlier snapshots, and leaves the
builder reusable. Value-equality exceptions leave builder state unchanged. Map/set
range construction, foreign-policy set probes, and applicable
intersection results route through this construction path; lookup/removal/adoption remain the
separate transient lifecycle.

`PersistentHashBag[T]` stores one representative and a positive signed-32-bit multiplicity per
policy class. `distinct_count` counts classes and unbounded Python `int` supplies exact
`total_count`; there is intentionally no ambiguous `size` or `len`. Copy counts are checked before
hashing, zero-copy edits are identity no-ops, expanded iteration repeats each representative
contiguously, and `distinct_items`, `entries`, and `get_entry` provide unambiguous distinct views.
Bag algebra accepts another bag: union takes maxima, intersection minima, `except_` saturated
subtraction, and `sum` checked addition. A foreign policy is eagerly normalized under the receiver
policy, receiver representatives win surviving classes, and collapsed argument classes retain the
first representative encountered in that version's CHAMP order. The narrow bag deliberately has
no transient, builder, symmetric difference, arbitrary-iterable algebra, or content equality.

`PersistentBiMap[K, V]` stores every association in forward and inverse CHAMP maps under independent
`key_policy` and `value_policy` objects. `add` rejects a represented class on either side;
`try_add` identifies the conflicting domain; and `set` replaces one key's value only when the new
value is unclaimed. Policy-equivalent sets retain both stored representatives and return the exact
receiver. Replacement removes and re-adds both entries so ordinary Python value equality cannot
override the configured value policy.

`get` and `get_key` return `BiMapLookupResult`, keeping stored `None` distinct from absence.
Key/value removal is symmetric. The lock-coordinated cached `inverse` facade swaps existing roots in
O(1) and points back to the original object under concurrent access. Forward iteration follows
stable-for-one-version, otherwise unspecified CHAMP order. The honest storage cost is approximately
twice one map; no algebra, builder, transient, or displacement surface is exposed.

`PersistentHashMultimap[K, V]` composes the public CHAMP map and set under independent key and
value policies. It stores no empty groups, reports exact signed-64-bit `pair_count` separately from
`key_count`, retains first representatives in both domains, and contracts an outer key when its
final pair is removed. `try_get_key` and `try_get_values` keep a stored `None` distinct from
absence. Flattened iteration follows stable-for-one-version, otherwise unspecified CHAMP order.

`PersistentRelation[L, R]` maintains mutually inverse multimaps. Addition normalizes both arguments
to globally retained representatives before updating either index, so equivalent objects cannot
drift between adjacency groups. Pair and whole-side removals remain symmetric. Its lock-published
cached `inverse` swaps existing roots in O(1), and `inverse.inverse` is the receiver.

`PersistentMapPatch[K, V]` records explicit absent/present before and after states for maps retaining
the same key-policy object, so a present `None` is never confused with absence. Strict application
preflights every expectation before publishing any successor. Inversion swaps states; composition
requires the same key and value-policy objects, validates the complete intermediate state, and drops
round trips. Typed conflict exceptions leave every input reusable.

`PersistentDirectedGraph[V]` composes an explicit vertex set with a bidirectional relation. Edge
insertion installs both endpoints, isolated vertices remain first-class, self-loops are allowed,
and equivalent parallel edges collapse. Edge removal keeps endpoints; vertex removal deletes every
incident edge. The lock-published cached `reversed` facade swaps relation roots in O(1) and points
back to the original graph.

`PersistentIndexedMap[K, V, I]` composes a primary map with a nonunique secondary multimap. Its
selector is skipped for duplicate strict adds, equal-value updates, removals, and reads. A changed
value is selected before publication and atomically moves its retained primary representative
between secondary groups; selector failure leaves the source unchanged. Primary and secondary
domains keep independent `HashPolicy` objects.

The shared default policy follows coherent Python hash/equality behavior. Hashable values use
`hash` and `==`; the identical-object fast path recovers non-reflexive values such as a retained
`NaN`. Unhashable objects use process-local identity hashing and are equivalent only to themselves;
use `create_hash_policy` for structural unhashable keys. Hashes are normalized to signed 32 bits.

Transient sessions retain the cross-language one-way lifecycle, clean-publication identity, and
version-invalidated iterators. Their changed edits remain persistent path copies; Python makes no
owner-token in-place-update claim. `TransientHashSet` exposes all six read-only relations; they do
not change its mutation version and reject access after publication before consuming an operand.

Patricia int maps/sets enforce signed 32-bit or signed 64-bit boundaries and traverse in ascending
signed-key order. Negative keys are masked before sign-bit biasing so Python's infinite-width
negative integers cannot leak into trie prefixes.

## Ordered collections

`PersistentOrderedSet[T]` lives in the neutral `ordered` package and composes only
`PersistentHashMap` with `PersistentDeque`. The HAMT maps each receiver-policy equivalence class to a private signed
64-bit order stamp, while the deque stores `(stamp, representative)` entries. New neighboring
positions use sparse `2^20` labels and deterministically relabel when a gap is exhausted.

The public construction and query surface is:

```text
empty, from_values
size, is_empty, policy, first, last
contains, try_get_value, get, get_at, index_of, to_list
__len__, __bool__, __contains__, __getitem__, __iter__
```

Point and ordered updates are `add`, `add_first`, `insert`, `move_to_first`, `move_to_last`,
`move_to`, `remove`, `try_remove`, `remove_at`, `remove_first`, `remove_last`, and `clear`. Range
and order operations are `get_range`, `take`, `drop`, `reverse`, and `sort`. Algebra comprises
`union`, `intersect`, `except_`, `symmetric_except`, `is_subset_of`, `is_proper_subset_of`,
`is_superset_of`, `is_proper_superset_of`, `overlaps`, and `set_equals`. The package also exports
the frozen `OrderedSetValueResult` and `OrderedSetRemoveResult` carriers in place of C# `out`
parameters. `except_` has a trailing underscore because `except` is a Python keyword.

Construction and every insertion retain the first representative and first position of an
equivalence class. Duplicate `add`, `add_first`, or `insert` operations are identity no-ops and
never imply movement; only the explicit movement methods reorder an existing stored
representative, with `move_to(index, value)` interpreting `index` as the final result position.
Default-policy emptiness is canonical, while custom empty sets retain the exact `HashPolicy`
object. Full-range extraction, already-sorted order, and logical algebra no-ops retain receiver
identity.

Every algebra and relation operation eagerly consumes and deduplicates its entire operand under
the receiver's policy before applying any shortcut. Union emits receiver representatives followed
by argument-only first representatives; intersection and difference retain receiver order;
symmetric difference emits receiver-only values followed by argument-only values. Sorting is
stable and one-shot: it accepts a two-argument Python comparator, uses Python's default ordering
when omitted, and does not retain that comparator for later additions.

Python maps invalid positions and empty endpoints to `IndexError`, invalid counts to `ValueError`,
missing movement targets to `KeyError`, and null iterable operands to `TypeError`. The nullable
`get` convenience method returns `None` on a miss; use `try_get_value` when stored `None` must
remain distinct from absence. `index_of` first performs a HAMT lookup and then binary-searches
through the public deque index API. Since one deque index is O(log n), this language-local
implementation is O(log^2 n) for that second phase rather than the C# workspace's O(log n)
specialized lower bound. All updates remain immutable and failure-atomic.

`PersistentOrderedMap[K, V]` uses the same neutral sparse-label design. Its persistent deque owns
each `(stamp, key, value)` entry while CHAMP stores only key-to-stamp navigation, avoiding a second
retained payload. Construction keeps the first key representative and position while the last
value-distinct payload wins. `set` never moves an existing class; only explicit movement, reversal,
and stable one-shot `sort` change order. Keyed/positional removal, range extraction, sharing probes,
and dual-index validation follow the ordered-set conventions. Strict addition raises
`DuplicateKeyError`; presence-safe lookup uses `OrderedMapLookup`.

`PersistentOrderedMultimap[K, V]` nests one ordered value set per ordered key group. Flattened
iteration is deliberately key-grouped rather than a global pair-arrival history. Independent key
and value policies retain their first representatives; duplicates and misses return the receiver,
removing a final value contracts its group, and reintroducing that key appends a new group.

## Measured and frontier structures

The general sequence substrate is a persistent implicit AVL tree caching caller-supplied ordered
monoid measures. Deques, sorted collections, stable priority queues, max-high interval trees, ropes,
and immutable gap cursors share its path-copied nodes.

`RangeUpdateSequence[T, M, U]` is a separate persistent implicit-AVL core whose nodes cache height,
signed-32-bit-bounded element count, and the ordered logical measure. Its runtime
`RangeUpdateAlgebra[T, M, U]` supplies `identity`, `combine`, `measure`, `identity_tag`,
`is_identity`, `compose`, `apply_element`, and `apply_measure`. The package also exports
`create_range_update_algebra` as the functional-policy adapter. Composition is intentionally
directional: `compose(newer, older)` means apply `older` first and then `newer`. Ordered measure
combination need not commute. The algebra laws in the C# normative specification remain required;
the Python runtime does not attempt to prove them.

Each construction lineage retains the exact algebra object, captures its empty measure once, and
owns one canonical empty facade reused by derived empty results. Concatenation requires identical
algebra objects even when one operand is empty; compatible empty operands retain the nonempty
operand by identity. Independently constructed empty lineages need not be the same Python object.
`from_iterable` eagerly materializes its source completely before reading the algebra identity or
measuring an element, and passing a `RangeUpdateSequence` with the same algebra returns it directly.

A node's own value and cached measure already reflect its pending tag, while its children do not.
The pending marker is a separate bit: neither `identity_tag`, equality, nor `None` denotes absence,
so nullable values and an active `None` tag are supported. Structural descent immutably pushes the
old tag before inserting or replacing an element. Read-only indexing, range measurement, and
iteration instead carry inherited tags as local state; an ancestor tag is newer and child
inheritance therefore composes it as `compose(inherited, node_pending)`. A composed tag recognized
as identity has its marker erased without undoing the already-logical value or measure.

The public sequence operations are `empty`, `from_iterable`, `get_at`, integer-only `__getitem__`,
`prepend`, `append`, `insert`, `set_item`, `remove_at`, `concat`, `split_at`, `get_range`,
`apply_range`, `measure_range`, `to_list`, `__iter__`, `__len__`, `is_empty`, `algebra`, and
`measure`. `split_at` returns frozen `RangeUpdateSplit(left, right)`. There are deliberately no
policy-specific `add_range` or `assign_range` shortcuts. Negative or excessive positions raise
`IndexError`; negative or excessive counts raise `ValueError`; an operation exceeding the
signed-32-bit count ceiling raises `OverflowError`. Range validation occurs before policy calls.

Empty and identity updates retain the receiver, endpoint splits retain the source on the nonempty
side, a full extraction retains the receiver, and `set_item` always creates a successor because no
element-equality policy exists. A nonidentity whole-sequence update touches and allocates one tree
node. Proper updates split twice, tag the isolated middle root, and rejoin. Range queries consume
cached fully covered subtree measures and allocate no persistent nodes. All policy work precedes
facade publication, so a throwing callback leaves every older version unchanged and usable.
The [Python range-update document](range-update-sequence.md) gives the complete runtime and
implementation mapping.

Extremal measures use the explicit `OptionalValue` carrier rather than reserving `None` as their
identity. Consequently, `MaxMeasure` and `MinMeasure` can measure nullable elements without losing
the distinction between an empty tree and a tree whose extremum is `None`; interval summaries use
the same presence-preserving representation for nullable endpoints.

`PersistentIntervalMap[T, V]` adds payloads to validated closed interval keys without changing the
interval set's merge semantics. Keys are unique and lexicographically ordered by `(low, high)`;
the first interval representative is retained and a supplied value equality callback recognizes
replacement no-ops. One measured sequence caches the complete rightmost key and maximum high
endpoint, supporting exact same-low lookup and pruned overlap/stabbing queries. Distinct overlapping
keys remain independent, and no coalescing API is provided because combining payloads requires an
explicit application policy.

`PersistentChunkedBitSet` represents nonnegative signed-32-bit indexes using only nonzero 64-bit
Python-integer words in the shared measured sequence. Cached population and last-word annotations
support logarithmic membership, point edits, inclusive `rank`, and zero-based `select` in represented
words. Iteration is ascending; union, intersection, `except_`, and symmetric difference merge word
streams in linear represented-word time. Python's arbitrary-precision integers are only the word
substrate and do not widen the public index domain.

Sequence, measured-tree, and measured-rope splits and cursor searches return frozen named
dataclasses, so callers can use semantic fields such as `left`, `right`, `item`, `cursor`, and
`found`. A measured-rope concatenation requires the exact same measure-policy object even when one
operand is empty. Positional and measured cursors retain convenient nullable `peek_previous()` and
`peek_next()` reads; their corresponding `peek_previous_entry()` and `peek_next_entry()` methods
wrap a present value in `RopeCursorPeek`, preserving the distinction between a stored `None` and a
cursor boundary. `replace_next()` is unconditional: it produces a fresh rope even for an identical
object and measured replacement invokes the replacement's measure callback before publication.

The frontier structures keep their actual sibling algorithms rather than flattening to tuples or
dicts: 32-way regular/relaxed RRB nodes, HMAC-SHA256 `ZZT2` zip-zip ranks, bootstrapped skew-binomial
Brodal-Okasaki heaps, winner-cached AVL priority-search queues, and the six-cursor chunked DABA Lite
schedule. Canonical built-in ranks use stable integer/UTF-8/byte hashes, never Python's salted
string hash. Custom objects require an equivalence-coherent `rank_hash` policy.

DABA Lite is mutable and intended for single-threaded mutation. Insert and evict stage all monoid
callbacks before publication, so callback failure leaves the queue unchanged. Clearing remains
O(1) in queue-structure work by publishing a fresh one-block chain after obtaining the identity.
The detached bidirectional chunk chain may be reclaimed later by Python's cyclic collector, so the
operation does not provide the prompt deterministic O(n+c) destruction contract of the native ports.

## Persistent cursors

Every public Python cursor is a **Profile R root-plus-position semantic checkpoint** in the sense of
the [repository-wide cursor design][cursor-design]: a frozen, slotted dataclass holding one retained
collection version plus a validated gap, rank, or search position, and nothing else. No Python
cursor retains path frames, a focused representation, a bounded active window, or prepared measure
fragments, and only `TextRopeCursor` memoizes anything — a lazily built `TextRope` facade around a
rope its inner cursor already holds canonically, not a reconstructed root. Consequently the
Python tier inherits **none** of the C# rope cursor's focused representation, memoization, callback
ceiling, allocation bound, or amortized-locality claims — not even for the Python ropes, which are
themselves snapshot-plus-gap checkpoints. Navigation only rewrites an integer and is O(1); every
peek, seek, measure, and edit delegates to the owning collection's ordinary persistent operation and
therefore costs exactly what that operation costs. A move-plus-peek traversal pays one full lookup
per step and is never O(k) in the number of steps.

Cursors are immutable values. Navigation and editing return new cursors, every retained cursor stays
usable and branchable, and `snapshot()` returns the retained version without consuming the cursor.
Each cursor retains the source's exact policy objects — hash policy, comparator, measure policy,
zip-tree rank policy, value-equality callback, Merkle policy, and range-update algebra — so a
snapshot taken through a cursor is policy-identical to the source, including for empty results.
Because Python constructors validate eagerly, there is no uninitialized, default, moved-from, or
disposed cursor state.

Peeks and searches use frozen result dataclasses rather than `out` parameters or sentinel values:
`PatriciaMapEntry`, `PatriciaCursorSearch`, `MerkleCursorSearch`, `SequenceCursorPeek`,
`OrderedCursorPeek`, `OrderedCursorSearch`, `CanonicalCursorPeek`, `CanonicalCursorSearch`,
`PrioritySearchCursorPeek`, `PrioritySearchCursorSearch`, `IntervalCursorPeek`,
`IntervalCursorSearch`, `IntervalMapCursorPeek`, `IntervalMapCursorSearch`,
`ChunkedBitSetCursorPeek`, `ChunkedBitSetCursorSearch`, `OrderedSetCursorPeek`,
`OrderedSetCursorSearch`, `OrderedMapCursorSearch`, and `OrderedMultimapCursorSearch`, alongside the
existing `RopeCursorPeek`, `MeasuredRopeCursorSearch`, and `TextRopeCursorSearch`. A search miss is
never an exception and never an invalid cursor; it is a `found` discriminator beside a usable gap.
All of these names, and every cursor class, are exported from the package root.

### Error channels and accessor naming

The cursor tier is not uniform across its error channels, and the differences are load bearing:

- An out-of-range construction or `seek`/`seek_rank` position raises `IndexError` in every family
  **except** the four Patricia cursors, whose shared `_validate_cursor_position` raises `ValueError`,
  and the positional and measured rope cursors, which also raise `ValueError`. Merkle raises
  `IndexError`.
- Boundary movement (`move_previous` at the start, `move_next` at the end), boundary deletion, and
  an edit with no adjacent entry raise `IndexError` everywhere, including Patricia.
- Directional counts raise `ValueError`: `RangeUpdateSequenceCursor.measure_previous`,
  `measure_next`, `apply_previous`, and `apply_next` validate `count` against the available
  direction, and the underlying `apply_range`/`measure_range` keep the sequence rule that an index
  is an `IndexError` while a count is a `ValueError`. The ordered map's range helpers follow the
  same split.
- Patricia and Merkle cursor insertion additionally rejects a key whose lower-bound rank is not the
  current gap with `ValueError`.
- Strict duplicate insertion is split. `PersistentIntMapCursor.insert`,
  `PersistentLongMapCursor.insert`, and `MerkleSearchTreeCursor.insert` raise `KeyError`. The sorted
  map raises `SortedDuplicateKeyError`, the interval map `DuplicateIntervalError`, the ordered map
  `DuplicateKeyError`, and the priority-search queue a plain `ValueError` — all four are `ValueError`
  subclasses or `ValueError` itself.

Cardinality accessor naming also drifts, and the drift is per family rather than global. The
finger-tree and HAMT cursors expose `count` even when their collections expose `size`: `RrbVector`,
`PersistentIntervalMap`, and the four Patricia collections publish `size` while
`RrbVectorCursor`, `PersistentIntervalMapCursor`, and the Patricia cursors publish `count`.
`CanonicalSortedSet` and `PrioritySearchQueue` publish both `size` and `count`, and their cursors
publish `count`. The deque, reversible deque, sorted bag/set/map, interval tree, and range-update
sequence expose only `len()`, and their cursors expose `count`. The neutral Ordered cursors instead
match their collections: `PersistentOrderedSetCursor` and `PersistentOrderedMapCursor` expose
`size`, and `PersistentOrderedMultimapCursor` exposes `pair_count`. `FingerTreeCursor` exposes no
cardinality accessor at all — only `is_at_start`, `is_at_end`, `measure_before`, and
`measure_after`.

### Patricia and Merkle ordered cursors

`PersistentIntMapCursor[V]`, `PersistentLongMapCursor[V]`, `PersistentIntSetCursor`, and
`PersistentLongSetCursor` are ascending signed-key gap cursors over the retained map or set plus a
rank. Factories are `cursor(position=0)`, `cursor_at_end()`, `lower_bound_cursor(key)`,
`upper_bound_cursor(key)`, and the presence-safe `cursor_at_key(key)` for maps or
`cursor_at_item(value)` for sets, both returning `PatriciaCursorSearch` with `cursor` and `found`.
Navigation is `count`, `is_at_start`, `is_at_end`, `peek_previous()`, `peek_next()`,
`move_previous()`, `move_next()`, `seek(position)`, and `snapshot()`. Map peeks return
`PatriciaMapEntry[int, V] | None`, so a stored `None` value stays distinct from a boundary; set
peeks return `int | None`, which is unambiguous because a set element is always an integer.

Map edits are `insert`, `put`, `set_next_value`, `delete_previous`, and `delete_next`; set edits are
`add`, `delete_previous`, and `delete_next`. `insert`, `put`, and `add` all require the argument's
lower-bound rank to equal the current gap, so a cursor cannot silently jump. `insert` and `put` at a
missing key return the gap after the new entry; `put` at an exact hit and `set_next_value` keep the
gap fixed; `delete_next` keeps the gap fixed while `delete_previous` moves it left. Set `add` of a
present item at the correct gap returns the receiver cursor exactly. Value replacement uses the
map's ordinary `same_value` rule — object identity first, then Python `==`, with a `TypeError` or
`ValueError` from `==` treated as unequal — so an equal replacement returns the receiver cursor and
the receiver map.

Complexity is honest and unglamorous. `count`, the boundary predicates, `move_previous`,
`move_next`, and `seek` are O(1). Every peek is an `entry_at` root descent over cached subtree
counts, so it is O(W) with `W` equal to 32 or 64; bound and exact factories are the same O(W)
`lower_bound_rank` descent; edits pay that descent plus the ordinary O(W) `put`/`remove`. Traversing
a whole map by move-plus-peek is therefore O(n · W), and cursor context is O(1).

`MerkleSearchTreeCursor[K, V]` is the same shape in policy-comparer order over a retained
`MerkleSearchTree`. Factories are `cursor(position=0)`, `cursor_at_end()`, `lower_bound_cursor(key)`,
`upper_bound_cursor(key)`, and `cursor_at_key(key)`, the last returning `MerkleCursorSearch` whose
`found` flag lets a miss keep a usable insertion gap. Peeks return `MerkleEntry[K, V] | None`. Edits
are `insert`, `set_item` (aliased `set`), `set_next_value`, `delete_previous`, and `delete_next`,
with the same gap conventions as Patricia. Every edit calls the ordinary canonical `set_item` or
`remove`, so level derivation, canonical codecs, `MST2` block bytes, root digest, stored key
representatives, and failure atomicity are byte-for-byte identical to a direct tree edit. The value
no-op is the tree's own rule: an encoding-identical value returns the receiver tree, and
`set_next_value` then returns the receiver cursor. Cursor state is purely local navigation state and
never appears in an `MST2` block, an `MSP2` proof, a pack, or a store.

Merkle rank and lower-bound lookup descend one root-to-leaf path but scan the entries of each
visited node — `_entry_at_for_cursor` walks a node's entry array, and `_lower_bound_rank_for_cursor`
binary-searches for the position and then sums the left child counts one at a time. The honest bound
is therefore O(h · b) comparer and count work for a tree of height `h` whose visited nodes hold up
to `b` entries, plus the ordinary edit's re-encoding and re-digesting of every node on the changed
path.

### Sorted, canonical, and priority-search cursors

`SortedBagCursor[T]`, `SortedSetCursor[T]`, and `SortedMapCursor[K, V]` are comparator-order gap
cursors. Each collection offers `cursor_at(position=0)`, `cursor_at_lower_bound(value_or_key)`,
`cursor_at_upper_bound(value_or_key)`, and `find_cursor(value_or_key)` returning
`OrderedCursorSearch` with `found` and `cursor`. Navigation is `count`, `is_at_start`, `is_at_end`,
`peek_previous()`, `peek_next()`, `move_previous()`, `move_next()`, `seek_rank(position)` — note
`seek_rank`, not `seek` — and `snapshot()`. Peeks return `OrderedCursorPeek`, wrapping `T` for the
bag and set and `SortedMapEntry[K, V]` for the map.

The bag's `add(value)` deliberately ignores the current gap: it locates the comparator upper bound,
inserts after every existing equal occurrence, and returns the gap after the new one, preserving the
collection's stable equal-item rule. `delete_previous` and `delete_next` remove the exact stored
occurrence at the adjacent rank, and no replacement is exposed. The set's `add(value)` inserts at
the lower bound and returns that gap plus one; an already-present comparer class is an
identity-preserving no-op on the set, though the cursor object itself is newly allocated. There is
no `replace_next` on either the bag or the set. The map adds `insert(key, value)`,
`try_insert(key, value)` returning `OrderedCursorSearch`, `set_item(key, value)`,
`set_next_value(value)`, `delete_previous`, and `delete_next`. `set_item` and `set_next_value`
retain the stored key representative and apply the map's identity-or-`==` value no-op, returning the
receiver map when the value is unchanged.

The sorted families are backed by the shared measured AVL sequence with order-statistic sizes, so
their bounds are genuinely logarithmic: `lower_bound`/`upper_bound` are O(log n) descents,
positional `get`/`entry_at` are O(log n), and each edit is an O(log n) split, append, and concat.
Movement is O(1) and each peek is O(log n), making a full move-plus-peek traversal O(n log n).

`CanonicalSortedSetCursor[T]` uses the same protocol over the zip-zip Cartesian tree, with
`cursor_at`, `cursor_at_lower_bound`, `cursor_at_upper_bound`, and `find_cursor` returning
`CanonicalCursorSearch`, and peeks returning `CanonicalCursorPeek`. Edits are `add`,
`delete_previous`, and `delete_next`; `add` places the value at its lower bound and returns the gap
after it, and every edit runs the ordinary zip/unzip so the HMAC-SHA256 `ZZT2` ranks, the canonical
topology, and the exact `ZipTreeRankPolicy[T]` object survive unchanged. Both `_bound_rank` and the
positional `_item_at` are O(h) descents over cached subtree counts. `h` is expected O(log n) only
under the documented coherent pseudorandom rank assumptions; a degenerate `rank_hash` can make
`h = n`, so the bound is stated as O(h) rather than O(log n).

`PrioritySearchQueueCursor[K, P, V]` is a **key-order** cursor; priority is cached augmentation, not
a second navigation axis. Factories are `cursor_at`, `cursor_at_lower_bound(key)`,
`cursor_at_upper_bound(key)`, `find_cursor(key)` returning `PrioritySearchCursorSearch`, and
`cursor_at_minimum_priority()`, which reads the root's cached winner key and then performs an
ordinary key lower-bound seek rather than walking a priority order that does not exist. Peeks return
`PrioritySearchCursorPeek` wrapping a `PrioritySearchEntry`. Edits are `insert` (a `ValueError` on
an equivalent key), `try_insert`, `set_item(key, priority, value)`, `set_next(priority, value)`,
`delete_previous`, and `delete_next`. `set_next` retains the stored key representative.

The queue's no-op rule requires all three of a zero `priority_comparator` result, an
identity-or-`==` equal priority, and an identity-or-`==` equal value; only then does `set_item`
return the receiver queue. It is **not** an identity-only rule, so `set_next(3.0 / 2, value)` over
an entry holding `1.5` and the same value is recognized as a no-op and preserves the queue. The
cursor still allocates a fresh cursor object around that unchanged queue. Conversely a
comparator-equal but `!=` priority is a genuine update that rebuilds every affected winner cache.

The priority-search cursor's bounds are asymmetric and should be treated as a known cost. Its rank
factories are cheap — `_bound_rank` is an O(log n) AVL descent over cached counts — but
`peek_previous` and `peek_next` materialize the entire queue with `tuple(self.queue)`, so each peek
is O(n) time and O(n) allocation. Because `find_cursor`, `set_item`, `set_next`, `delete_previous`,
and `delete_next` each go through a peek or `find_cursor`, all of them are O(n) even though the
underlying queue operations are O(log n). `enumerate_at_most` remains the winner-pruned,
output-sensitive query; a cursor scan must not be substituted for it.

### Interval and bit-set cursors

`IntervalTreeCursor[T]` is ordered by nondecreasing low endpoint with duplicate-occurrence
positions. Factories are `cursor_at`, `cursor_at_lower_bound(low)`, `cursor_at_upper_bound(low)`,
`find_cursor(interval)` under the two-endpoint matching rule, `find_overlap_cursor(probe)`, and
`find_containing_cursor(point)`, the last three returning `IntervalCursorSearch`. The instance
`seek_next_overlap(probe)` searches strictly after the focused occurrence, so a factory's
gap-before-hit result cannot rediscover the same occurrence indefinitely; a miss returns a usable
end cursor. Peeks return `IntervalCursorPeek`. Edits are `insert(interval)`, `delete_previous`, and
`delete_next`. Insertion uses the facade's low-bound placement — a newly inserted equal-low interval
therefore precedes the older equal-low run — and deletion removes the exact occurrence at the
adjacent rank, which disambiguates duplicates. Endpoint replacement is not exposed because it can
move the interval; express it as remove plus insert. Interval validity is delegated to the owning
API, so a `low > high` interval raises the collection's `ValueError`.

`PersistentIntervalMapCursor[T, V]` is ordered by the unique complete `(low, high)` key. Factories
are `cursor_at`, `cursor_at_lower_bound(interval)`, `cursor_at_upper_bound(interval)`,
`find_cursor(interval)`, `find_overlap_cursor(probe)`, and `find_containing_cursor(point)`,
returning `IntervalMapCursorSearch`; `seek_next_overlap(probe)` again continues strictly after the
focus. Peeks return `IntervalMapCursorPeek` wrapping an `IntervalMapEntry`. Edits are
`insert(interval, value)` raising `DuplicateIntervalError`, `try_insert`, `set_next_value(value)`,
`delete_previous`, and `delete_next`. `set_next_value` retains the stored interval representative
and applies the configured `value_equals` no-op, returning the receiver map when unchanged. Exact
key state and the max-high augmentation are published together by construction, because both live in
one measured sequence.

Both interval cursors are logarithmic only in their lower-bound factories, which use a measured
`locate` over the cached rightmost key: O(log n). Everything else on the cursor path is linear.
`IntervalTreeCursor` peeks call `self.tree.to_list()` and `PersistentIntervalMapCursor` peeks call
`tuple(self.map)`, so each peek is O(n) time and O(n) allocation. `IntervalTree.cursor_at_upper_bound`
and `IntervalTree.find_cursor` also materialize the list before scanning the equal-low run, and
`PersistentIntervalMap.cursor_at_upper_bound`/`find_cursor` pay one peek. Most importantly, the
overlap cursor path in both families — `_find_overlap_cursor_from`, reached from
`find_overlap_cursor`, `find_containing_cursor`, and `seek_next_overlap` — **materializes the whole
collection and scans it linearly** from the start rank, stopping once low endpoints exceed
`probe.high`. It does **not** use the `max_high` augmented descent. The non-cursor `find_overlap`,
`find_containing`, and `find_overlaps` queries do prune through `locate` and, for the map, a
`_candidate_prefix` split; the cursor equivalents do not, and are O(n).

`PersistentChunkedBitSetCursor` traverses present set bits rather than a dense Boolean sequence. Its
`position` is a population-rank gap in `0 .. count`, so the cursor rank domain includes `count`
(the end gap) while the collection's `select` domain is `0 .. count - 1`. Factories are
`cursor_at(position=0)`, `cursor_at_or_after(bit_index)`, and `find_cursor(bit_index)` returning
`ChunkedBitSetCursorSearch`; peeks return `ChunkedBitSetCursorPeek` holding an `int` bit index.
Edits are `add(bit_index)`, `delete_previous`, and `delete_next`. A present bit passed to `add` is
an identity no-op returning the receiver cursor; a missing bit updates or inserts its 64-bit word
and returns the gap after the new bit; deletion clears the exact neighboring bit and drops a word
that becomes zero, so no publishable cursor ever retains a zero word. A negative or out-of-domain
index passed to `add` raises the collection's `ValueError`, while the nonthrowing lookup convention
maps a negative `cursor_at_or_after` to the start and a negative `find_cursor` to `found=False` at
the start.

Bit-set cursor bounds are the collection's and are logarithmic in the number of **represented
words**, not in the bit domain. `cursor_at_or_after` and `add` use `rank`, an O(log w) measured
`locate` plus one 64-bit mask popcount; peeks use `select`, an O(log w) `locate` plus at most 63
lowest-bit clears within the located word. Movement is O(1). Set algebra remains a sparse word-stream
operation and is not a cursor primitive.

### Neutral Ordered cursors

The neutral `ordered` package ships `PersistentOrderedSetCursor[T]`,
`PersistentOrderedMapCursor[K, V]`, and `PersistentOrderedMultimapCursor[K, V]` over insertion and
explicit-position order. Private sparse stamps never enter the cursor contract; the cursor position
is an ordinary gap. Each edit prepares the ordered-sequence successor and the CHAMP index successor
and publishes them together, so a failed hash, equality, or value-equality callback leaves the
receiver cursor and every retained branch usable.

The set and map expose `cursor_at(position=0)` plus an equality seek — `find_cursor(equal_value)`
returning `OrderedSetCursorSearch` and `find_cursor(key)` returning `OrderedMapCursorSearch`. A miss
returns `found=False` with a usable **append-position** cursor at `size`, which is the documented
collection-factory form: there is no key-sorted lower bound to infer for an insertion-ordered
collection. Set peeks return `OrderedSetCursorPeek`, which carries its own `found` discriminator so a
stored `None` representative stays distinct from a boundary; map peeks return
`OrderedMapEntry[K, V] | None`. Set edits are `insert(value)`, `try_insert(value)` returning
`(inserted, cursor)`, `delete_previous`, and `delete_next`; an already-present equivalence class is
an exact receiver-identical no-op that neither moves the class nor replaces its stored
representative, and there is deliberately no `replace_next`. Map edits are `insert(key, value)`
raising `DuplicateKeyError`, `try_insert` returning `(inserted, cursor)` positioned at the existing
key on a duplicate, `set_next_value(value)`, `delete_previous`, and `delete_next`. `set_next_value`
preserves the stored key, its stamp, and its position, and applies the identity-or-`==` value no-op.
There is no key rename.

`PersistentOrderedMultimapCursor` walks the flattened, key-grouped pair order — grouped enumeration,
not a global pair-arrival history — and reports `pair_count`. Factories are `cursor_at(position=0)`,
`find_cursor(key, value)`, and `find_group_cursor(key)`, all returning
`OrderedMultimapCursorSearch`, with misses returning the pair-end cursor at `pair_count`. Peeks
return `OrderedMultimapEntry[K, V] | None`. Edits are `add(key, value)`, `try_add(key, value)`,
`delete_previous`, and `delete_next`. A duplicate pair returns the receiver cursor; a genuine
addition recomputes the inserted pair's position in the successor's grouped order and returns the
gap after it; deleting a group's final value contracts the group. Independent key and value policies
are retained.

Ordered-cursor costs follow the collection's dual-index design. Peeks are one O(log n) deque index.
The set's and map's equality seek is one CHAMP lookup followed by `_index_of_stamp`, a binary search
whose every probe is an O(log n) public deque index, so the stamp-location tier is genuinely
**O(log^2 n)** — the same bound already documented for `index_of` and `index_of_key`, not O(log n).
Interior insertion is an O(log n) deque insert plus the CHAMP add; when a sparse `2^20` label gap is
exhausted, the operation falls back to a deterministic relabel that rebuilds the entire order,
costing O(n) hash and sequence work for that produced version and sharing nothing with retained
branches. The multimap has no pair-count prefix measure over its outer group order, so its cursor is
linear rather than logarithmic: `_cursor_entry_at` enumerates the grouped pairs to reach a rank,
making each peek O(p) in the pair count, and `find_cursor`, `find_group_cursor`, and the index
recomputation inside `add` are likewise O(p).

### Sequence-cursor specifics

Beyond the shared rope, deque, reversible-deque, RRB, and measured-tree behavior described above,
four Python details are worth stating exactly.

`FingerTreeCursor[T, M]` is measure-addressed rather than count-addressed: it publishes no
cardinality accessor and no integer `Seek`, only `is_at_start`, `is_at_end`, `measure_before`,
`measure_after`, the peeks, unit movement, `seek_by_measure(predicate)`, `insert`,
`delete_previous`, `delete_next`, `replace_next`, and `snapshot`. Its two measure properties are not
symmetric in cost: `measure_before` calls `prefix_measure`, a read-only descent that combines cached
subtree measures and allocates no persistent nodes, while `measure_after` calls `split_at_index` and
reads the right tree's measure, performing a full structural split whose two halves are then
discarded. The contract `combine(measure_before, measure_after) == snapshot().measure` still holds
in that order, but only `measure_before` is allocation-free. `FingerTree.get_cursor(predicate)`
returns a `(found, cursor)` tuple; the cursor's own `seek_by_measure` discards that flag and returns
the end cursor on a miss.

`MeasuredSequence.set_at` is unconditional. It carries no element-equality shortcut, so
`FingerTreeCursor.replace_next` and `PersistentDequeCursor.replace_next` always publish a successor
and always measure the replacement, which is what the design requires of a generic cursor with no
element-equality policy.

`RrbVectorCursor.replace_next` is the one sequence replacement with a no-op rule, and that rule is
**reference identity only**: `RrbVector.set_item` compares the stored leaf slot with `is`, so an
`==`-equal but distinct object still publishes a new vector. `RrbVectorCursor.insert_range` accepts
either an iterable, which it materializes once, or an `RrbVector`, which it splices through
split/concat with structural sharing; an empty range returns the receiver cursor.

`ReversibleDequeCursor` maps the logical gap onto the physical deque and keeps structural sharing in
both orientations. `insert_range` builds its run through the deque's orientation-aware aligned-run
helper, so a reversed receiver splices a physically reversed run and both concatenations stay on the
sharing path rather than falling into the orientation-mismatch materialization. `reverse()` returns
a cursor over the reversed logical version at gap `count - position`.

## Merkle interoperability

`MerkleSearchTree` uses policy identity `mst-sha256-b16-v2`, SHA-256, strict canonical codecs, the
byte-exact `MST2` block format, seven independent verification budgets, and canonical `MSP2`
membership, absence, and inclusive-range proof envelopes. Persistence and synchronization publish
authenticated closures only; merge values distinguish absence from a present `None`.

The bundled `InMemoryMerkleBlockStore` provides synchronized, atomic multi-block publication.
Third-party protocol stores receive complete verification and conflict preflight before sequential
publication, but cannot acquire transactional guarantees the protocol does not expose.

[cursor-design]: ../../../docs/proposals/repository-wide-persistent-cursor-design.md
