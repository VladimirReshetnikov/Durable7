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
- built-in policies `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, `KeyMeasure<T>`,
  `ProductMeasure<T, PFirst, PSecond>` with `MeasurePair<TFirst, TSecond>`, and
  `OrderStatisticMeasure<T>` with `RankedKey<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, P>`, `MeasuredRopeBuilder<T, P>`, `TextRope`, `RopeBuilder`,
  `NewlineMeasure`, `NewlineStyle`, and `LineColumn`.

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

This workspace is a semantic checkpoint, not the final lazy finger-tree representation. It preserves immutable
snapshot behavior, stable observable ordering, rank/range semantics, priority stability, closed-interval overlap
semantics, and text line navigation. `PersistentDeque<T>` has moved past the initial vector snapshot and now uses
an `Arc`-shared balanced tree, so nontrivial splits, concatenations, range operations, and point updates share
unchanged subtrees. `ReversibleDeque<T>` is now an O(1) mirrored-tree view over that deque: reverse wraps or
cancels a shared tree view, and reversed/mixed-orientation endpoint operations, splits, and concatenations stay on
the tree path instead of materializing vectors. Split/pop results preserve that wrapper, and `iter`, borrowed
`IntoIterator`, and owned `IntoIterator` enumerate in logical order. Deque nodes cache endpoint leaf signposts;
sorted bounds therefore descend once in O(log n) node visits, and the sorted split/equal-range/insert/remove
operations reuse those bounds while preserving shared subtrees. `Rope<T>` now uses chunked length-measured
storage over the shared measured tree, so chunk construction, `copy_to`, positional edits, slices, splits, and
concatenations share unchanged chunks and measured subtrees; `MeasuredRope<T, P>` now provides the same
positional insert/remove/range/slice vocabulary while preserving cached user measures. Its mutable append builder
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

Future representation work should keep the Rust public names and result shapes stable while replacing the remaining
semantic-checkpoint algorithms with lazy measured-spine equivalents where needed for asymptotic parity.

## External dependency

Extended-grapheme segmentation uses `unicode-segmentation` 1.13.3, licensed `MIT OR Apache-2.0`.
Cargo resolves and pins that crate in the repository's `src/Rust/Cargo.lock`; its source is fetched
from crates.io and is not vendored into this repository.
