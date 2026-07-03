# Rust FingerTree API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust FingerTree-family port
- Scope: Rust naming, contracts, checkpoint limitations, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-fingertree`, with library name
`tools_data_structures_fingertree`.

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, P>` over `MeasurePolicy<T>`;
- built-in policies `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, and
  `OrderStatisticMeasure<T>` with `RankedKey<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, P>`, `TextRope`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

The Rust surface follows Rust conventions:

- fallible indexed operations return `Option` instead of throwing;
- duplicate sorted-map insertion returns `Result<_, DuplicateKeyError>`;
- binary search returns `Result<usize, usize>`, matching Rust's insertion-index convention;
- measure policies are ordinary traits with static functions for identity, element measure, and combine;
- text offsets are character offsets, matching the repository's `Rope<char>` interpretation rather than UTF-8 byte offsets.

This workspace is a semantic checkpoint, not the final lazy finger-tree representation. It preserves immutable
snapshot behavior, stable observable ordering, rank/range semantics, priority stability, closed-interval overlap
semantics, and text line navigation. `PersistentDeque<T>` has moved past the initial vector snapshot and now uses
an `Arc`-shared balanced tree, so nontrivial splits, concatenations, range operations, and point updates share
unchanged subtrees. `ReversibleDeque<T>` is now an orientation layer over that deque, so reversal and
wrapper-preserving logical edits share the same underlying tree. `Rope<T>` now uses chunked length-measured
storage over the shared measured tree, so positional edits, slices, splits, and concatenations share unchanged
chunks and measured subtrees; `TextRope` inherits that storage for character offsets and line helpers. The general
`FingerTree<T, P>` now uses an `Arc`-shared measured
tree with cached monoid
measures at every node, so measure-guided split and locate operations can skip whole subtrees and split results
share unchanged structure. `MeasuredRope<T, P>` now reuses that measured tree through an internal count-plus-user
measure policy, so indexed splits, concatenation, point replacement, prefix measurement, and measure-guided locate
share unchanged measured subtrees. `PriorityQueue<T, P>` now reuses the measured tree through an internal
minimum-priority measure, so peek/dequeue locate the first global-minimum entry by cached prefix measures while
preserving equal-priority stability. `IntervalTree<T>` now reuses the measured tree through an internal maximum-high
endpoint measure, so overlap and containment queries skip prefixes whose cached high endpoint cannot intersect the
probe. Sorted bag/set/map facades now reuse the measured tree through cached order-statistic measures: rank and
key-boundary operations locate by count plus last-key prefixes, while edits and range extraction preserve unchanged
measured subtrees. These derived facades still do not claim the C#/C++ lazy measured-spine complexity or allocation
profile for every operation.

Future representation work should keep the Rust public names and result shapes stable while replacing the remaining
semantic-checkpoint algorithms with lazy measured-spine equivalents where needed for asymptotic parity.
