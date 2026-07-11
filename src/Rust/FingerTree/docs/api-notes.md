# Rust FingerTree API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust FingerTree-family port
- Scope: Rust naming, contracts, checkpoint limitations, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-fingertree`, with library name
`tools_data_structures_fingertree`.

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `DabaLite<T, M>`, `DabaMonoid<T>`, `DabaLiteStatistics`, and the empty/invariant error types;
- `RrbVector<T>`, `RrbVectorBuilder<T>`, and the split/pop/statistics result types;
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
- out-of-range RRB indexing, splitting, and range edits return `None`; indexing through `Index`
  retains Rust's ordinary panic-on-invalid-index convention.

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

`RrbVector<T>` ports the hardened C# relaxed radix-balanced representation. Leaves contain at most
32 contiguous elements in shared `Arc<[T]>` backing arrays; leaf slices created by split retain that
backing without cloning elements. Regular 32-way branches omit cumulative tables and select children
from five-bit radix spans. Relaxed branches alone store prefix sizes. Indexed reads, borrowed
iteration, split, range extraction, and endpoint removal therefore impose no `Clone` bound. Point
replacement, concat, payload-redistributing range edits, owned pops, owned iteration, and `to_vec`
require `T: Clone` only where Rust ownership requires copying stored values. Equal point
replacement, empty insertion/removal, empty-side concat, and boundary splits preserve root identity
where their result is unchanged. `validate_structure` reports count, height, leaf density, branching,
and regular/relaxed-node statistics while checking every cached layout invariant. The append builder
moves staged leaves into immutable nodes, retains an adopted vector as an O(1) prefix, and returns
the same root on repeated clean freezes. As in C#, there is no dedicated persistent tail buffer;
immutable endpoint insertion remains a boundary-spine operation.

Future representation work should keep the Rust public names and result shapes stable while replacing the remaining
semantic-checkpoint algorithms with lazy measured-spine equivalents where needed for asymptotic parity.

## External dependency

Extended-grapheme segmentation uses `unicode-segmentation` 1.13.3, licensed `MIT OR Apache-2.0`.
Cargo resolves and pins that crate in the repository's `src/Rust/Cargo.lock`; its source is fetched
from crates.io and is not vendored into this repository.
