# Rust Ordered Collections

- Created (UTC): 2026-07-15T00:00:00Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Consumers, maintainers, reviewers, and sibling-language port authors
- Scope: Neutral Rust persistent ordered set/map crate, contracts, and validation entry point

`tools-data-structures-ordered` provides the safe-Rust port of the repository's neutral persistent
insertion-ordered set. `PersistentOrderedSet<T, S = RandomState>` combines the public CHAMP
`PersistentHashMap<T, i64, S>` membership index with the public FingerTree `PersistentDeque`
positional sequence. It is independently owned general-purpose code: the crate has no dependency on
Tungsten production code, tests, internals, or behavior.

`PersistentOrderedMap<K, V, S = RandomState>` uses the same sparse-stamp design. Its CHAMP stores
key-to-stamp navigation while the positional deque owns key/value entries, so arbitrary payloads
are not duplicated across indexes. It retains the first key representative and position while
`set_item` replaces only the value; strict addition, explicit movement, ranges, reversal, stable
one-shot sorting, removal, sharing diagnostics, and full dual-index validation mirror the set.

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
  validation.

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
