# Rust FingerTree Family

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of the Rust FingerTree-family port
- Scope: Public crate shape, checkpoint semantics, and validation entry point

`tools-data-structures-fingertree` is the Rust checkpoint port for the repository's FingerTree
family. It exposes Rust-native names for the same public families:

- `PersistentDeque<T>`;
- `DabaLite<T, M>` over a `DabaMonoid<T>`;
- `RrbVector<T>` and its append-only `RrbVectorBuilder<T>`;
- `CanonicalSortedSet<T>` over a retained `ZipTreeRankPolicy<T>`;
- `FingerTree<T, P>` over a `MeasurePolicy<T>`, including built-in size, sum, min/max, key,
  order-statistic, and product-measure policies;
- `ReversibleDeque<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, P>`, `MeasuredRopeBuilder<T, P>`, `TextRope`, and `RopeBuilder`,
  including Unicode text extras and newline-style classification.

This checkpoint preserves immutable snapshot semantics and the observable behavior covered by the
crate tests for its persistent families. `DabaLite<T, M>` is the deliberate mutable exception: it
ports the six-cursor DABA Lite schedule over linked 64-slot chunks, bounds insertion/eviction/query
to three/two/one monoid combines, plans callback work transactionally before publication, and
promptly releases retired slots and chunks. Its safe `Rc` cursor representation is single-threaded
(`!Send` and `!Sync`). Rust's deterministic destruction makes `clear` O(n + c), because promptly
releasing `n` generic owned values in `c` chunks must run their destructors; insertion, eviction,
and query retain worst-case O(1) work when the callbacks do.
`PersistentDeque<T>` uses structurally shared balanced tree storage and caches first/last
leaf signposts at every node, so sorted lower/upper bounds visit O(log n) nodes and feed the full sorted
split/equal-range/insert/remove vocabulary.
`RrbVector<T>` is the uniform-random-access sibling: immutable 32-slot leaves sit below 32-way
branches, regular branches use five-bit radix indexing without size tables, and relaxed branches
retain cumulative sizes only where split or concatenation made child spans irregular. Safe `Arc`
path copying supports indexing, replacement, endpoint edits, boundary-spine concatenation, splits,
range edits, and iteration. The append builder moves full leaves into persistent storage, adopts an
existing vector as a frozen prefix, and caches clean immutable snapshots.
`CanonicalSortedSet<T>` is the policy-canonical ordered-set sibling: immutable `Arc` nodes form a
Cartesian search tree whose geometric, secondary, and content rank words come from HMAC-SHA-256 of
an explicit stable 64-bit rank hash. Fresh OS-random, publicly seeded, and caller-keyed policy
modes separate process-local, reproducible, and secret-key trust boundaries. Policy identity gates
algebra and diff; independently reconstructed policies with the same key can reproduce topology.
Persistent edits share untouched branches, `OnceLock` memoizes a non-cryptographic subtree digest,
and complete validation checks ranks, ordering, heap priority, and metadata. Expected logarithmic
operations require a coherent, sufficiently collision-resistant rank hash; full collisions remain
correct and deterministic but produce linear height. Bulk construction, reads, iteration,
validation, clear, and same- or cross-policy equality accept non-`Clone` elements; only path-copying
updates, set algebra, and owned diff require `T: Clone`.
`ReversibleDeque<T>` uses O(1) mirrored tree views over that shared deque, including tree-based
mixed-orientation concat, split, and endpoint operations after reverse. Its split/pop results retain the
reversible facade, and borrowed or owned iteration follows logical orientation. `Rope<T>` now uses chunked
length-measured storage over the shared measured tree. `TextRope` stores the same character content
in a newline-measured rope for cached line navigation. `FingerTree<T, P>` now uses structurally
shared measured tree storage with cached monoid measures. The measured core now includes
`ProductMeasure<T, PFirst, PSecond>`, `MeasurePair<TFirst, TSecond>`, `KeyMeasure<T>`, and
size+sum / size+min / size+max aliases with component-projected splits, bound splits,
cumulative-weight selection, and positional priority helpers. `MeasuredRope<T, P>` uses chunked
measured storage with cached count plus user-measure summaries and supports persistent positional
insert, range insertion/removal, slicing, and an append builder whose immutable snapshots share
their frozen prefixes. Both rope flavors expose chunk-copy construction and copy into
caller-supplied slices. `TextRope` and `RopeBuilder` include Rust-native string/display
conveniences; character and text ropes also expose scalar/code-point addressing, Unicode UAX #29
extended-grapheme addressing, newline-style detection, and CRLF-aware line text. `PriorityQueue<T, P>`
now composes the measured core with cached minimum-priority measures, and `IntervalTree<T>` uses a cached
last-low/maximum-high product summary for O(log n) first-hit and O((k + 1) log n) full-overlap search.
`SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>` now use
order-statistic measured tree storage with cached count plus last-key measures. The crate still does
not claim the C#/C++ lazy finger-tree asymptotic profile overall; derived algorithms remain
semantic-checkpoint implementations until the lazy measured spine is ported through the whole family.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract, checkpoint boundary, and evidence entry points.

Validate from `src/Rust`:

```powershell
.\test.ps1 -Workspace FingerTree
```

Unicode extended-grapheme segmentation uses the non-vendored `unicode-segmentation` 1.13.3 crate.
Canonical zip-tree policy uses RustCrypto `hmac` 0.12.1 and `sha2` 0.10.9 plus `getrandom` 0.3.4 for
fresh OS-random keys. These dependencies are permissively licensed and pinned by the workspace
`Cargo.lock`.
