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
- `CanonicalSortedSet<T>`, `ZipTreeRankPolicy<T>`, stable comparer/hash traits and built-ins,
  algebra/diff result types, validation statistics, and policy/invariant errors;
- `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, ordering policy/comparer types,
  validation statistics, and meld/invariant errors;
- `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>`, add/remove/minimum result
  handles, borrowing iterators, validation statistics, and range/invariant errors;
- `FingerTree<T, P>` over `MeasurePolicy<T>`;
- built-in policies `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, `MinMeasure`, `KeyMeasure<T>`,
  `ProductMeasure<T, PFirst, PSecond>` with `MeasurePair<TFirst, TSecond>`, and
  `OrderStatisticMeasure<T>` with `RankedKey<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, positional `RopeCursor<T>`, `MeasuredRope<T, P>`, `MeasuredRopeBuilder<T, P>`,
  `TextRope`, `RopeBuilder`, `NewlineMeasure`, `NewlineStyle`, and `LineColumn`.

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

## Positional rope cursor

`Rope::cursor()` creates an immutable cursor at gap zero; `cursor_at(position)` accepts every gap in
`0..=len` and returns `None` outside that range. `RopeCursor<T>` owns a cheap root-sharing `Rope<T>`
snapshot and a gap position. It intentionally has no `Default` and is `Send + Sync` whenever `T`
is `Send + Sync`. `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`, borrowed
`peek_previous`/`peek_next`, `move_previous`/`move_next`, `seek`, and `snapshot` expose navigation
without requiring `T: Clone`. Boundary movement and invalid seek return `None`. Seeking to the
current position and inserting an empty range return unchanged root-sharing cursor values.

Edits return new cursors and never mutate the receiver, so any retained cursor can form an
independent branch. `insert` and `insert_range` leave the new gap after the inserted values;
`delete_previous` implements backspace and moves the gap left; `delete_next` and `replace_next`
keep the gap fixed. `replace_next` is unconditional: it invokes no equality comparison and creates
an edited snapshot even for an equal replacement. Missing previous/next elements return `None`.
These edit methods require `T: Clone` because the current chunked rope substrate clones the affected
chunk; the bound is not imposed on construction, navigation, peeking, seeking, or snapshotting.
Cached positional-rope lengths use checked `usize` addition. Any construction or growth operation
whose resulting length is unrepresentable, including concatenation, ordinary rope insertion, and
cursor insertion, panics before returning; all input ropes and cursors remain valid.

This is a semantic positional checkpoint, not a port of the C# focused zipper. Cursor creation,
cloning, movement, seek, and snapshot are O(1). Peeks and point edits are O(log n) plus bounded chunk
work; inserting `m` values is O(m + log n). No O(1)-amortized local-edit claim is made. There is no
`MeasuredRope` or `TextRope` cursor in this checkpoint.

## Brodal-Okasaki heap

`BrodalOkasakiHeap<T>` is the direct bootstrapped skew-binomial representation, not an adapter over
the measured priority queue. A rank-zero global tree stores the minimum, and each skew-binomial
tree fuses its primitive children with the embedded heap forest from Brodal and Okasaki. Insert,
minimum, and meld are worst-case O(1); delete-min is worst-case O(log n).

`OrderPolicy<T>` retains a `Send + Sync` `OrderComparer<T>`. Natural policies are marked canonical,
so two independently constructed natural heaps can meld. Cloning a custom policy preserves its
identity; `with_shared_comparer` also lets independently constructed heaps retain clones of the same
caller-owned comparer `Arc`, matching comparer-object identity in the managed reference.
Independently constructed custom policies are rejected by `meld` with
`BrodalMeldError`, even when their functions happen to compare identically. This is the Rust form
of the C# comparer-object compatibility contract.

Values and all tree links use `Arc`. Consequently construction, insertion, meld, minimum,
delete-min, iteration, validation, and sharing diagnostics impose no `T: Clone` bound.
`minimum` returns `Option<&T>`. `minimum_view` returns `Option<BrodalMinimumView<T>>`; its
`Arc<T>` minimum is an owned shared handle that remains valid independently of the source and
remainder. In particular, a stored `Option<T>` value of `None` is distinct from an empty heap.

Structural-order iteration is explicit-stack and comparison-free. `validate_structure` uses an
explicit worklist to audit the global rank-zero root, every fused primitive/embedded boundary,
skew-forest rank rules, parent/child order, logical count, maximum rank, and maximum depth.
`shared_tree_count` and `shares_root_with` are read-only identity diagnostics for persistence
audits. See [Brodal-Okasaki heap](brodal-okasaki-heap.md) for the representation and validation
contract in one place.

## Priority-search queue

`PrioritySearchQueue<K, P, V>` is an immutable AVL ordered map with one entry per key-policy
equivalence class. Each node caches count, height, and the minimum-priority entry in its complete
subtree. Keyed lookup, insertion, replacement, removal, and winner deletion are O(log n); global
minimum is O(1). Equal priorities break by retained key order, so the winner is deterministic.

The first concrete key representative is permanent for the life of an equivalence class. Bulk
construction and `set_item` replace its priority and payload last-wins. `set_item` reuses the exact
root only when the priority policy reports equality, ordinary `P: PartialEq` also reports equality,
and `V: PartialEq` reports equal payloads. Those two equality bounds occur only on `set_item`;
bulk last-wins construction, `try_add`, reads, iteration, removal, minimum deletion, validation,
and diagnostics impose neither equality nor `Clone` bounds.

`PrioritySearchEntry<K, P, V>` owns `Arc` handles for its three components. Borrowed accessors avoid
copies, while component-handle accessors and the owned remove/minimum results remain usable after
the source queue is dropped. This makes `Option` components explicit: outer `Option<Entry>` means
presence or absence, independently of `None` stored inside the key, priority, or payload.

`enumerate_at_most` eagerly rejects an inverted key range with `PrioritySearchRangeError`, then
returns a lazy borrowing iterator. Bounds are inclusive, output is in key order, and a subtree is
pruned when its cached winner exceeds the inclusive priority threshold. The traversal uses an
explicit stack and requires no component cloning. Its lifetime deliberately borrows the queue and
three query values instead of cloning generic bounds.

`validate_structure` explicitly audits strict key bounds, AVL balance, cached count/height, and
winner-handle identity at every node. `shared_node_count`, `shares_root_with`, and
`shares_node_for_key` provide read-only persistence diagnostics. See
[Priority-search queue](priority-search-queue.md) for the complete operation and evidence map.

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

## Canonical zip-zip sorted set

`CanonicalSortedSet<T>` is an immutable Cartesian binary-search tree over `Arc` nodes. A retained
`ZipTreeRankPolicy<T>` owns two `Send + Sync` trait objects: `ZipTreeComparer<T>` defines both order
and set equivalence, while `ZipTreeRankHash<T>` supplies a deterministic 64-bit value that must be
constant on every comparer-equivalence class. `NaturalZipTreeComparer` and
`StableZipTreeRankHash` provide an explicit natural-order path for fixed-width integers, `bool`,
`char`, UTF-8 strings, and byte strings. The stable-hash trait deliberately omits platform-sized
integers and `std::hash::Hash`; Rust's randomized or implementation-defined hashers never influence
canonical topology.

The policy encodes the 64-bit rank hash in big-endian order and applies HMAC-SHA-256. The first
three big-endian words supply geometric rank (leading-zero count), secondary rank, and subtree
digest content. Heap order compares the first two coordinates descending and comparer order
ascending as the final tie-break. Consequently rank collisions do not lose correctness or
canonicality, but a constant rank hash makes height equal to count.

Policy construction exposes the C# trust modes without a hash-unstable default:

- `random` / `random_natural` obtain a fresh unexposed 32-byte key from the operating system. Each
  call creates an independent policy; clone the handle to retain identity across versions.
- `seeded` / `seeded_natural` derive the key as SHA-256 of ASCII `ZZT2` followed by the public
  seed's eight big-endian bytes. This reproduces ranks but is not adversarial security.
- `keyed` / `keyed_natural` require at least 32 bytes, copy and hide them, and let callers reproduce
  ranks across processes while retaining responsibility for key protection.

RustCrypto's `hmac` and `sha2` implement the primitive, and `getrandom` supplies fresh keys. HMAC
cannot restore entropy discarded by the 64-bit input hash, make an incoherent hash coherent, or
prevent deliberate degeneration under a public seed. Key bytes are redacted from `Debug`, but the
type does not promise memory locking or zeroization. The 64-bit memoized `content_hash` is a fast
same-policy inequality filter, not a cryptographic commitment or equality proof.

`from_items` comparer-sorts and retains the first concrete representative in each equivalence class
before an O(n) Cartesian freeze; total bulk cost is O(n log n). It takes ownership directly and
does not require `T: Clone`. Neither do construction of an empty set, lookup, containment,
iteration, digesting, topology diagnostics, validation, clear, version-identity checks, or same- and
cross-policy equality. `T: Clone` is required only by operations that must reproduce owned items:
`insert` and `remove` path-copy existing nodes, algebra composes those updates, and `diff` returns
owned vectors.

`insert` returns `CanonicalSetError::IncoherentRankHash` if an equivalent representative derives a
different rank. It otherwise retains the old representative and shared root on duplicates. Absent
removal, empty clear, self algebra, and difference by an empty set likewise preserve
`is_same_version`. Localized edits path-copy O(h) nodes and share untouched subtrees; all
traversals, updates, digest work, and destruction use explicit stacks so height-n collision cases
remain call-stack safe.

`union`, `intersection`, `except`, and `diff` require the exact same retained policy object and
return `IncompatiblePolicy` for independently reconstructed policy handles. This prevents mixing
rank spaces silently. `set_equals` still compares mathematical contents across policies under the
receiver's comparer. It stably sorts and deduplicates borrowed references from the other set, so it
does not clone elements; differing comparer semantics can intentionally make the receiver-defined
result asymmetric. Same-policy equality first rejects count or memoized-digest differences and
then walks canonical topology in lockstep while pruning shared nodes. `diff` returns owned
`only_in_left` and `only_in_right` vectors in comparer order.

`validate_structure` checks rank reproducibility, strict search order, heap priority, cached count
and height, and root metadata. It reports count, height, largest geometric coordinate, and repeated
geometric/secondary priority pairs. Immutable sets and policies are `Send + Sync` when `T` is; the
memoized digest uses `OnceLock` for safe concurrent publication.

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
and regular/relaxed-node statistics while checking every cached layout invariant and the
`floor((usize::BITS - 1) / 5) + 1` height cap (thirteen on supported 64-bit targets). The extra
level admits the legal boundary-only `minimum height + 1` slack in the top count band. Concat
redistributes only the boundary seam; it does not certify
global minimum occupancy, so adversarial density ceilings remain test gates. The append builder
moves staged leaves into immutable nodes, retains an adopted vector as an O(1) prefix, and returns
the same root on repeated clean freezes. As in C#, there is no dedicated persistent tail buffer;
immutable endpoint insertion remains a boundary-spine operation.

Future representation work should keep the Rust public names and result shapes stable while replacing the remaining
semantic-checkpoint algorithms with lazy measured-spine equivalents where needed for asymptotic parity.

## External dependencies

Extended-grapheme segmentation uses `unicode-segmentation` 1.13.3, licensed `MIT OR Apache-2.0`.
Canonical rank derivation uses RustCrypto `hmac` 0.12.1 and `sha2` 0.10.9, also licensed
`MIT OR Apache-2.0`; fresh key generation uses `getrandom` 0.3.4 under `MIT OR Apache-2.0`. Cargo
resolves and pins these crates in the repository's `src/Rust/Cargo.lock`; their sources are fetched
from crates.io and are not vendored into this repository.
