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
- `BrodalOkasakiHeap<T>` over a retained `OrderPolicy<T>`, with owned minimum views and
  structural validation statistics;
- `PrioritySearchQueue<K, P, V>` and its owned entry/result handles over retained key and priority
  policies;
- `FingerTree<T, P>` over a `MeasurePolicy<T>`, including built-in size, sum, min/max, key,
  order-statistic, and product-measure policies;
- `ReversibleDeque<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>`;
- `Interval<T>`, `IntervalTree<T>`, and payload-bearing `PersistentIntervalMap<T, V>`;
- `PersistentChunkedBitSet` over the shared measured tree;
- `Rope<T>` and its immutable positional `RopeCursor<T>`, `MeasuredRope<T, P>` and
  `MeasuredRopeCursor<T, P>`, `MeasuredRopeBuilder<T, P>`, `TextRope` and `TextRopeCursor`, and
  `RopeBuilder`, including Unicode text extras and newline-style classification.

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
`BrodalOkasakiHeap<T>` directly implements the bootstrapped skew-binomial representation. Its
rank-zero global minimum fuses primitive tree children with an embedded heap forest; insert and
meld perform worst-case O(1) comparisons and structural work, while delete-min performs worst-case
O(log n) work. Trees and values use `Arc`, so persistent operations and owned minimum views do not
require `T: Clone`. Canonical natural-order policies interoperate across construction; custom heaps
may meld only when they retain clones of the same `OrderPolicy<T>` identity or clones of the same
caller-owned comparer `Arc` passed to `with_shared_comparer`. Full validation decodes
every fused boundary and audits ranks, heap order, count, and depth.
`PrioritySearchQueue<K, P, V>` is a separate persistent winner-cached AVL map. It retains the first
concrete representative of each key-order equivalence class and replaces priority/payload last;
ties select the retained key that comes first under the key policy. Every node caches its subtree
winner for O(1) global minimum and priority-threshold pruning. `Arc`-backed entry components make
reads, removal, and owned result handles available without `Clone` bounds; only exact no-op
detection requires ordinary priority and payload equality.
`ReversibleDeque<T>` uses O(1) mirrored tree views over that shared deque, including tree-based
mixed-orientation concat, split, and endpoint operations after reverse. Its split/pop results retain the
reversible facade, and borrowed or owned iteration follows logical orientation. `Rope<T>` now uses chunked
length-measured storage over the shared measured tree. `RopeCursor<T>` is the low-risk positional
cursor checkpoint: an immutable root-sharing rope snapshot plus a validated gap in `0..=len`.
Movement, seek, and snapshot are O(1) and require no element cloning; peeks and point edits retain
the rope substrate's O(log n) plus bounded-chunk work, and range insertion is O(m + log n). Retained
cursors branch independently. Cached rope lengths use checked `usize` addition: a result that cannot
fit panics before publication while every input snapshot remains valid. This is deliberately not the
C# focused zipper and carries no amortized-locality claim. `MeasuredRopeCursor<T, P>` applies the
same immutable snapshot-plus-gap model to the exact measured version, adds ordered prefix/suffix
measures and absolute monotone prefix search, and returns a usable end cursor on a search miss.
`TextRopeCursor` is the nominal newline-specialized facade: it retains `TextRope`, reports scalar-
offset line/column positions, and preserves existing LF-measure semantics. Measured/text navigation
does not require `T: Clone`; edits inherit the measured rope's bounded-chunk clone requirement.
Checked count preflights make unrepresentable measured growth fail before element-measure callbacks.
`TextRope` stores the same character content in a newline-measured rope for cached line navigation.
`FingerTree<T, P>` now
uses structurally shared measured tree storage with cached monoid measures. The measured core now includes
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
`PersistentIntervalMap<T, V>` adds unique lexicographic interval keys and payload updates over a
separate full-key/maximum-high measure, validates every interval argument, retains the first key
representative, and deliberately omits payload-ambiguous coalescing.
`PersistentChunkedBitSet` stores only nonzero 64-bit words over the shared measured tree. Its cached
word-order and population summaries provide logarithmic membership, point edits, inclusive rank,
and zero-based select in the number of represented words. It accepts the shared nonnegative signed
32-bit index domain, shares unchanged storage, drops empty words, and supplies persistent union,
intersection, difference, and symmetric difference.
`SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>` now use
order-statistic measured tree storage with cached count plus last-key measures. The crate still does
not claim the C#/C++ lazy finger-tree asymptotic profile overall; derived algorithms remain
semantic-checkpoint implementations until the lazy measured spine is ported through the whole family.

See the [Brodal-Okasaki heap notes](docs/brodal-okasaki-heap.md), the
[priority-search queue notes](docs/priority-search-queue.md), [API notes](docs/api-notes.md),
[validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract, checkpoint boundary, and evidence entry points.

Validate from `src/Rust`:

```powershell
.\test.ps1 -Workspace FingerTree
```

Unicode extended-grapheme segmentation uses the non-vendored `unicode-segmentation` 1.13.3 crate.
Canonical zip-tree policy uses RustCrypto `hmac` 0.12.1 and `sha2` 0.10.9 plus `getrandom` 0.3.4 for
fresh OS-random keys. These dependencies are permissively licensed and pinned by the workspace
`Cargo.lock`.
