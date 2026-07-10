# Rust FingerTree Family

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of the Rust FingerTree-family port
- Scope: Public crate shape, checkpoint semantics, and validation entry point

`tools-data-structures-fingertree` is the Rust checkpoint port for the repository's FingerTree
family. It exposes Rust-native names for the same public families:

- `PersistentDeque<T>`;
- `FingerTree<T, P>` over a `MeasurePolicy<T>`, including built-in size, sum, min/max, key,
  order-statistic, and product-measure policies;
- `ReversibleDeque<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, P>`, `MeasuredRopeBuilder<T, P>`, `TextRope`, and `RopeBuilder`,
  including Unicode text extras and newline-style classification.

This checkpoint preserves immutable snapshot semantics and the observable behavior covered by the
crate tests. `PersistentDeque<T>` uses structurally shared balanced tree storage and caches first/last
leaf signposts at every node, so sorted lower/upper bounds visit O(log n) nodes and feed the full sorted
split/equal-range/insert/remove vocabulary.
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
now composes the measured core with cached minimum-priority measures, and `IntervalTree<T>` uses cached maximum-high interval measures for
overlap and containment entry points. `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>` now use
order-statistic measured tree storage with cached count plus last-key measures. The crate still does
not claim the C#/C++ lazy finger-tree asymptotic profile overall; derived algorithms remain
semantic-checkpoint implementations until the lazy measured spine is ported through the whole family.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract, checkpoint boundary, and evidence entry points.

Validate from `src/Rust`:

```powershell
cargo test -p tools-data-structures-fingertree
```

Unicode extended-grapheme segmentation uses the non-vendored `unicode-segmentation` 1.13.3 crate
(`MIT OR Apache-2.0`), pinned by the workspace `Cargo.lock`.
