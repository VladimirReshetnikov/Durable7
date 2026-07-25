# Rust Ordered Collections

- Created (UTC): 2026-07-15T00:00:00Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Consumers, maintainers, reviewers, and sibling-language port authors
- Scope: Neutral Rust persistent ordered set/map crate, contracts, and validation entry point

`durable7-ordered` provides the safe-Rust port of the repository's neutral persistent
insertion-ordered collections. `PersistentOrderedSet<T, S = RandomState>` combines the public CHAMP
`PersistentHashMap<T, i64, S>` membership index with the public FingerTree `PersistentDeque`
positional sequence. It is independently owned general-purpose code: the crate has no dependency on
Tungsten production code, tests, internals, or behavior.

`PersistentOrderedMap<K, V, S = RandomState>` uses the same sparse-stamp design. Its CHAMP stores
key-to-stamp navigation while the positional deque owns key/value entries, so arbitrary payloads
are not duplicated across indexes. It retains the first key representative and position while
`set_item` replaces only the value; strict addition, explicit movement, ranges, reversal, stable
one-shot sorting, removal, sharing diagnostics, and full dual-index validation mirror the set.

`PersistentOrderedMultimap<K, V, SK = RandomState, SV = RandomState>` composes the ordered map and
ordered set into a set-valued multimap. It retains insertion order for key groups and independently
within each value group, while key and value equality domains retain independent hash builders.
Enumeration is deliberately key-grouped rather than a globally interleaved pair-arrival history.

Rust's `Eq` and `Hash` define membership classes. The retained `BuildHasher` defines hash routing,
and every set-producing algebra operation and relation eagerly normalizes its entire argument under
a clone of the receiver's hash builder. Enumeration follows insertion order unless explicit
movement, reversal, or a stable one-shot sort changes it. The first value installed for an equality
class remains the stored representative until removal; duplicates never replace or implicitly move
it.

The public surface covers:

- empty and iterable construction with default or caller-supplied hash builders;
- membership, stored-representative recovery, positional lookup, endpoints, and index lookup;
- append, prepend, insertion, explicit movement, class/position/endpoint removal, and clear;
- contiguous ranges, take/drop, reverse, natural sort, and comparator-based stable one-shot sort;
- receiver-order union, intersection, difference, and symmetric difference, including same-type
  convenience forms that deliberately ignore the argument's hash-builder state;
- subset, proper-subset, superset, proper-superset, overlap, and set-equality relations;
- ordered iteration, vector conversion, indexing, root-sharing diagnostics, and complete dual-index
  validation;
- grouped ordered-multimap construction, lookup, insertion, pair/key-group removal, clear,
  root-sharing diagnostics, and nested invariant validation; and
- immutable position-gap cursors for all three collections, with rank and equality-search factories,
  neighbor peeks and movement, atomic dual-index edits, and borrowed snapshots.

## Cursors

`PersistentOrderedSetCursor<T, S>`, `PersistentOrderedMapCursor<K, V, S>`, and
`PersistentOrderedMultimapCursor<K, V, SK, SV>` live in the public `cursors` module and are
re-exported from the crate root together with `OrderedCursorSearch<C>` and `OrderedCursorInsert<C>`.
A cursor is an immutable value pairing one retained collection version with a validated gap: the set
and map navigate explicit position order, while the multimap navigates the grouped pair rank of
`iter`. Navigation and edits return new cursors, every retained cursor stays branchable, and
`snapshot()` borrows the version without consuming the cursor.

These are Profile R root-plus-position semantic checkpoints. Each edit delegates to the ordinary
persistent operation so both indexes publish atomically, and none of the C# rope cursor's focused
representation, memoization, allocation, callback-count, or amortized-locality claims are inherited.
Private sparse stamps are never exposed; a cursor position is a gap count.

Two separate result carriers keep the discriminators unambiguous: `OrderedCursorSearch::found`
reports that an equivalent entry already exists and is returned only by the non-editing
`find_cursor` / `find_group_cursor` factories, while `OrderedCursorInsert::added` reports that
`try_insert` actually published a new entry. A key reported `found: true` is exactly a key reported
`added: false`, which is why they are not one field on one type.

The multimap cursor's peeks, searches, and deletions walk the flattening iterator and are linear in
the pair rank rather than logarithmic; the set and map cursors' equality search pays the
squared-logarithmic stamp-location tier. [API notes](docs/api-notes.md) give the complete factory,
gap-convention, error-channel, and complexity contract.

Private signed 64-bit sparse stamps leave a large gap between ordinary neighbors. Positional inserts
and moves choose an endpoint label or midpoint when possible. Exhausted gaps and endpoint overflow
fall back to a complete relabel with deterministic `index * 2^20` stamps. Stamps are never public
collection semantics; they only coordinate the sequence and CHAMP index.

Changed versions share every unaffected persistent subtree. Specified logical no-ops return value
clones that share both roots, which is observable through `shares_roots_with`. Published versions are
immutable and automatically `Send + Sync` when their contents and hasher are. Comparator, hashing,
equality, cloning, allocation, or source-iteration failure cannot mutate an existing snapshot.

Rust-facing differences from C# are deliberate:

- invalid insertion/range/removal positions use `Option`, and movement uses
  `Result<_, OrderedSetMoveError>`;
- empty endpoint reads/removals use `Option`;
- comparator panics propagate directly instead of receiving the .NET `Array.Sort` exception wrapper;
- `BuildHasher` is cloned into successors because Rust value ownership has no comparer-object
  identity contract; and
- representative identity follows `Clone` semantics. Identity-preserving handles such as `Arc<T>`
  retain the same allocation, while ordinary owned values retain the same observable value.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md).

Validate from `src/Rust` with the repository's serialized wrapper:

```powershell
.\test.ps1 -Workspace Ordered
```

Benchmarks are intentionally outside routine validation and were not used to justify this port.
