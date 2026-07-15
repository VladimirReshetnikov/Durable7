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
- `ConcurrentHashTrie` is a thread-safe, lock-coordinated Python facade with constant-time immutable
  snapshots. It is not represented as the lock-free GCAS Ctrie of the managed reference workspace.
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
`PersistentHashMap` with `PersistentDeque`; neither its implementation nor its semantic baseline
depends on Tungsten. The HAMT maps each receiver-policy equivalence class to a private signed
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

## Tungsten and numerics

Tungsten collections remain an application-specific dependency leaf. `PersistentAssociation`
uses a CHAMP key index plus measured order sequence with sparse `2^20` stamps, midpoint insertion,
and deterministic relabeling. Position-preserving `set_item` keeps the first stored key
representative; append/prepend/insert move an existing key using the incoming representative.

The six fixed-width integer classes wrap construction and ordinary operators modulo their width,
while `parse` and `checked_*` methods reject out-of-range results. Signed division truncates toward
zero and remainder has the dividend's sign, including explicit minimum-value divided by `-1`
overflow. Exact-width byte conversion is deterministic in either byte order. Python `int` is also
the direct arbitrary-precision substrate for non-negative `SparseInteger`.

## Merkle interoperability

`MerkleSearchTree` uses policy identity `mst-sha256-b16-v2`, SHA-256, strict canonical codecs, the
byte-exact `MST2` block format, seven independent verification budgets, and canonical `MSP2`
membership, absence, and inclusive-range proof envelopes. Persistence and synchronization publish
authenticated closures only; merge values distinguish absence from a present `None`.

The bundled `InMemoryMerkleBlockStore` provides synchronized, atomic multi-block publication.
Third-party protocol stores receive complete verification and conflict preflight before sequential
publication, but cannot acquire transactional guarantees the protocol does not expose.
