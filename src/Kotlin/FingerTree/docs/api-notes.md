# Kotlin FingerTree API Notes

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers implementing and reviewing the Kotlin FingerTree-family port
- Scope: Kotlin naming, contracts, measured-tree representation, complexity, and intentional differences

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, M>` over `MeasurePolicy<T, M>`;
- built-in policies `SizeMeasure<T>`, `IntSumMeasure`, `MaxMeasure<T>`, `MinMeasure<T>`, and
  `ProductMeasure<T, A, B>` with `MeasurePair<A, B>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `BrodalOkasakiHeap<T>`, `BrodalMinimumView<T>`, and `BrodalOkasakiHeapStatistics`;
- `PrioritySearchQueue<K, P, V>`, `PrioritySearchEntry<K, P, V>`,
  `PrioritySearchAddResult<K, P, V>`, `PrioritySearchRemoveResult<K, P, V>`,
  `PrioritySearchMinimumView<K, P, V>`, and `PrioritySearchQueueStatistics`;
- `Interval<T>` and `IntervalTree<T>`;
- `RrbVector<T>` and `RrbVector.Builder<T>`;
- `ZipTreeRankPolicy<T>`, `CanonicalSortedSet<T>`, `CanonicalSetLookup<T>`, and
  `CanonicalSortedSetStatistics`;
- `Monoid<T>`, `DabaLite<T>`, and `DabaLiteStatistics`;
- `Rope<T>`, positional `RopeCursor<T>`, nullable-safe `RopeCursorPeek<T>`, `MeasuredRope<T, M>`,
  `TextRope`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

The Kotlin surface follows Kotlin/JVM conventions:

- fallible indexed operations return `null`;
- duplicate sorted-map insertion throws `SortedDuplicateKeyException` for `insert` and returns
  `SortedAddResult` for `tryInsert`;
- monoids are runtime objects with identity and associative combine operations; measure policies
  refine `Monoid<M>` with an element-to-measure operation;
- sorted and priority facades accept JVM `Comparator` values where natural ordering is not enough;
- `SortedMap.from(values, comparator)` provides comparator-aware bulk construction and keeps the last supplied
  entry, including its key instance, from every comparator-equal run;
- text offsets are Kotlin `Char` offsets, matching the repository's `Rope<char>` interpretation.

`RrbVector` uses `append`/`prepend`, `concat`, `splitAt`, `setItem`, `insertAt`/`insertRange`,
`removeAt`/`removeRange`, and `tryRemoveLast`. Invalid indexed edits and boundaries return `null`,
matching the rest of this workspace. `RrbPop<T>` keeps successful removal distinct from failure even
when a vector stores nullable elements. Equal-value replacement, empty insertion/removal, boundary
splits, and concatenation with empty preserve the receiver or existing root where applicable.

## Positional rope cursor

`Rope.cursor()` creates an immutable cursor at gap zero; `cursorAt(position)` accepts every gap in
`0..size` and returns `null` outside that range. `RopeCursor<T>` is an ordinary non-data class with no
public constructor or default instance, so callers cannot forge a gap through generated `copy`.
It retains the exact source `Rope<T>` reference and its validated gap. `size`, `isEmpty`, `position`,
`isAtStart`, `isAtEnd`, `movePrevious`,
`moveNext`, `seek`, and `snapshot` expose navigation without copying elements. Boundary movement and
invalid seek return `null`; same-position seek returns the same cursor by identity.

`peekPrevious` and `peekNext` return `RopeCursorPeek<T>?`. A null result means that no neighbor exists,
while a non-null wrapper may contain a stored null value, so nullable element types remain
unambiguous. Edits return new cursors and never mutate the receiver. `insert` and `insertRange` leave
the new gap after the inserted values; `deletePrevious` implements backspace and moves left;
`deleteNext` and `replaceNext` keep the gap fixed. Replacement is unconditional, performs no equality
call, and stores the supplied representative. A range source is captured exactly once, and an empty
range returns the same cursor and rope by identity.

Positional `Rope` growth uses checked `Int` arithmetic for prepend, append, point/range insertion, and
concatenation. An unrepresentable result throws `ArithmeticException` before publication; all source
ropes and cursors remain reusable. Cursor creation, movement, seek, and snapshot are O(1). Peeks and
point edits are O(log n) over the measured AVL substrate, and inserting `m` values is O(m + log n).
Immutable cursors are safe for structurally concurrent reads subject to the same caller-owned element
mutability caveat as ropes. This is a semantic checkpoint, not the C# focused zipper, and it makes no
O(1)-amortized local-edit claim. `MeasuredRope` and `TextRope` cursors remain unported.

## Representation and complexity

`PersistentMeasuredTree<T, M>` is the shared internal engine: an immutable AVL sequence whose nodes
cache subtree size, height, and the order-sensitive monoidal measure. Construction builds a balanced
tree in O(n). Indexing, path-copying updates, insertion/removal, prefix measurement, measure-guided
location, and splits are O(log n); concatenation joins trees by height and retains unchanged nodes.
The public `sharesStorageWith` diagnostics report shared node identity, and executable tests validate
the AVL bound after generated histories and 100,000-element construction.

`PersistentDeque<T>` and `FingerTree<T, M>` use that engine directly. `MeasuredRope` exposes front/back,
endpoint and positional insertion, range insertion/removal, replacement, slicing, splitting, concatenation,
copying, and compaction over the measured engine; every result retains the supplied measure policy and cached
aggregate. `SortedBag`, `SortedSet`, and
`SortedMap` use `PersistentDeque`; comparator-guided bounds (bag counting, set navigation and
membership, and keyed map lookup) each descend one root-to-leaf path of the sorted tree, so they
cost O(log n) comparisons rather than binary search's O(log² n) indexed probes, and the resulting
edit is also O(log n). `Rope` uses the
same positional tree. `MeasuredRope` caches its supplied measure, and `TextRope` is newline-measured
rather than string-backed. Full enumeration, conversion, sorting/filter rebuilding, and
`PersistentDeque.reverse()` are O(n).

`RrbVector<T>` is a separate 32-way RRB core. Leaves contain 1 through 32 elements. A regular branch
has full-capacity children except possibly its final child and navigates by radix shifts without a
size table; only a relaxed branch stores cumulative child sizes. Lookup and `setItem` are
O(log32 n), and split, insertion, removal, append/prepend, and boundary-spine concatenation are
O(log32(n + m)) with fixed-arity array copying. Exact leaf-boundary splits and full-leaf
concatenations retain original leaves. Counts and cumulative sizes use checked `Int` arithmetic;
the size-derived maximum valid height is `floor((Int.SIZE_BITS - 1) / 5) + 1`, or seven. The extra
level admits the legal boundary-only `minimum height + 1` slack in the top count band. Concatenation performs
boundary-only redistribution and does not promise global minimum occupancy away from the seam;
the adversarial density bounds are test gates, not validator invariants.

`RrbVector.Builder` is append-only between freezes. It stages 32-element tail arrays, transfers a
full tail only after abandoning mutable access to that array, copies a partial tail on freeze, and
adopts an existing vector as an O(1) frozen prefix. `toImmutable()` caches clean snapshots, and
later builder mutation cannot change an earlier vector. Builder iteration freezes once and is
fail-fast if the builder is subsequently modified. The builder is not thread-safe; published
vectors are immutable and safe for concurrent readers.

## Canonical zip-zip sorted set

`CanonicalSortedSet<T>` is an immutable Cartesian binary-search tree. Comparator order is the search
order; a content-derived priority makes topology canonical inside a retained
`ZipTreeRankPolicy<T>`, not globally for the JVM type. `empty(policy)` and `from(values, policy)` are
the idiomatic factories; `create` and `createRange` are aliases for callers porting C# code.

`ZipTreeRankPolicy<T>.create()` uses natural order, the zero-extended 32-bit JVM `hashCode`, and a
fresh unexposed 32-byte key. Passing a `Long` seed derives the key as SHA-256 of ASCII `ZZT2` followed
by the seed's eight big-endian bits. Every factory call creates a distinct policy; unlike the C#
surface, Kotlin does not expose one closed-generic process-wide default object. The explicit-
comparator overload requires a `(T) -> Long` rank hash and rejects its omission. `createKeyed`
requires at least 32 caller-owned bytes, copies them, and offers both natural-order and explicit-
comparator overloads.

For each item, the policy writes the rank hash's 64 bits in big-endian order and computes
HMAC-SHA-256. The first three big-endian 64-bit digest words supply a leading-zero geometric
coordinate, an unsigned secondary coordinate, and a content word for the subtree digest. Heap order
uses geometric rank, unsigned secondary rank, then the comparator-smaller item. Kotlin `Long`
carries the same raw 64-bit seed/hash words as C# `ulong`; negative values are not sign-extended or
re-encoded. The implementation uses only JCA (`SecureRandom`, SHA-256, and HmacSHA256).

The rank hash must be stable and constant on comparator-equivalence classes. Bulk duplicate
elimination and duplicate `add` dynamically reject unequal derived ranks, but no finite check can
prove global coherence. Natural JVM `hashCode` values are not generally cross-language encodings;
cross-process or cross-language reproduction requires the caller to pin equivalent comparator,
rank-hash, key/seed, and item semantics. A public seed permits reproduction but not adversarial rank
secrecy. A protected caller key makes ranks hard to predict only to the extent that the pre-HMAC
64-bit rank hash itself is collision-resistant for the workload. The fixed 64-bit secondary
coordinate is a practical zip-zip-inspired policy and does not claim the paper's compact-rank
metadata theorem.

`from` sorts by comparator plus original sequence index, retains the first representative in each
equivalence class, checks duplicate rank coherence, and freezes a monotone-stack Cartesian build.
`add`, `remove`, and `clear` preserve the receiver for no-ops; `tryGetValue` returns an explicit
`CanonicalSetLookup<T>` so a stored nullable representative remains distinguishable from failure.
`union`, `intersect`, and `except` require the exact same policy object and return canonical results.
`setEquals` is semantic across policy objects under the receiver's comparator; relation methods
likewise deduplicate arbitrary iterables under that comparator.

Every node caches count and height and lazily publishes a non-cryptographic 64-bit `contentHash`.
Within one coherent policy, count or digest inequality proves set inequality; digest equality is
only a filter and is followed by iterative lockstep comparison, with reference-equal subtrees
pruned. Digests from distinct policy objects have no equality meaning. `sharesStorageWith` is an
O(n + m) diagnostic over node identity. `validateStructure` checks strict comparator bounds, rank
reproduction, heap order, node uniqueness/acyclicity, cached metadata, and root metadata, and reports
count, height, largest geometric rank, and repeated geometric/secondary priorities.

For height h, lookup is O(h) time and O(1) space; persistent insertion/removal take O(h) time,
allocate O(h) path nodes, and use O(h) temporary explicit-stack entries. Bulk build is O(n log n)
for sorting plus O(n) Cartesian construction. Validation and the first digest evaluation are O(n);
later root digest reads are O(1). Expected coherent pseudorandom height is O(log n), but colliding or
publicly predictable ranks can force h = n. Every traversal and update is iterative and therefore
remains JVM-stack-safe in that case. Immutable sets and policies are safe for concurrent readers
provided their comparator and rank-hash callbacks are themselves stable and thread-safe.

## DABA Lite sliding-window aggregation

`DabaLite<T>` maintains the FIFO-ordered aggregate of one dynamically sized window without requiring
a commutative operation or an inverse. It takes a runtime `Monoid<T>`; because `MeasurePolicy<E, M>`
refines `Monoid<M>`, a policy such as `IntSumMeasure` can be passed directly when its measure type is
also the window value type:

```kotlin
val window = DabaLite(IntSumMeasure)
window.insert(5)
window.insert(8)
window.insert(13)

val total = window.aggregate // 26
window.evict()                // removes the oldest contribution, 5
val remaining = window.aggregate // 21
```

The implementation follows Tangwongsan, Hirzel, and Schneider's 2021 DABA Lite schedule. Six
cursors remain ordered `F <= L <= R <= A <= B <= E` over one logical queue. `[F, B)` and `[B, E)`
are the front and back; `[L, R)`, `[R, A)`, and `[A, B)` are the incremental-reversal work regions.
Two fields retain the `R`-through-`A` and back products. Every successful insertion or eviction
executes one of four bounded fixups: collapse an exhausted front, begin the next flip, advance the
three equal work cursors, or rewrite one left and one right partial aggregate. There is no loop or
recursive reversal in a window operation.

`insert`, `evict`/`tryEvict`, and a nonempty `aggregate` query invoke `Monoid.combine` at most three,
two, and exactly one times respectively. An empty query obtains `empty` and invokes no combine.
These callback ceilings are independent of window length; full worst-case O(1) time additionally
requires `empty` and `combine` themselves to be O(1). `evict` throws `IllegalStateException` on an
empty window, while `tryEvict` returns `false`.

All mutators give the strong guarantee for monoid callback failures. Publication of counts, cursors,
aggregate fields, slot rewrites, and chunk links is ordered so a throwing `empty` or `combine` leaves
the previous window intact. External side effects performed by a callback cannot be rolled back.
`clear()` is an O(1) reset: an empty clear makes no callback, while a nonempty clear obtains `empty`
once, invokes combine zero times, and swaps in one fresh chunk only after the callback succeeds.

The queue is a doubly linked chain of 64-slot JVM reference arrays. Cursor movement and growth are
worst-case O(1), and no growth copies the window. A successful eviction nulls its retired slot;
crossing a chunk boundary severs both links to the predecessor. `clear()` drops the old chain in
O(1). A state with `n` live positions has queue capacity `n` plus 1 through 127 slack slots; an empty
instance retains one 64-slot chunk. Queue slots are not a stable raw-value sequence because DABA
Lite overwrites values with partial aggregates, so the API deliberately exposes neither peek,
value-returning eviction, nor iteration.

`validateStructure()` invokes neither monoid callback. In O(c) time and space for `c` active chunks,
it checks bidirectional links and acyclicity, cursor reachability/order, `count == E - F`, the DABA
region equations, and the chunk/slack bound. It returns `DabaLiteStatistics` containing count, the
five region lengths, block count, allocated capacity, and slack. Aggregate correctness cannot be
reconstructed from overwritten slots for an arbitrary non-invertible monoid, so executable tests
also compare every operation with an external FIFO model.

`DabaLite` is mutable and unsynchronized. Calls on one instance must not overlap unless the caller
provides external serialization. This concurrency boundary is intentionally different from the
immutable FingerTree and RRB values in the same package.

## Direct priority cores

`BrodalOkasakiHeap<T>` is a direct immutable bootstrapped skew-binomial heap. Its rank-zero global
root caches the minimum and its forest uses the paper's fused primitive-child/embedded-forest
encoding. `minimum`, `insert`, and `meld` are O(1) worst-case; `deleteMinimum` is O(log n). Every
comparator-equivalent element remains a distinct multiset representative. `meld` requires the exact
same comparator object, with natural-order factories sharing the standard singleton. Iteration is
structural, stack-safe, and comparator-free. `validateStructure` checks fused boundaries, skew
ranks, heap order, count, and depth.

`PrioritySearchQueue<K, P, V>` is an immutable winner-cached AVL map. It keeps the first concrete
key in each comparator-equivalence class and updates its priority/payload last-wins. Priority ties
break by key order. Minimum is O(1); keyed operations and minimum deletion are O(log n). The
inclusive `enumerateAtMost` query returns key-ordered results and prunes any subtree whose cached
winner exceeds the threshold, with O(log n + v) work for `v` unpruned visits and O(n) worst case.
The validator checks strict key order, AVL metadata/balance, and every cached winner.

See [priority-core notes](priority-cores.md) for the complete policy, no-op, persistence, complexity,
and JVM comparison-audit contracts.

`PriorityQueue` caches the stable leftmost minimum entry, making peek O(1), enqueue/meld O(log n), and
dequeue O(log n). `IntervalTree` caches last-low and maximum-high summaries: lower-bound insertion and
the first overlap query are O(log n), while overlap enumeration repeatedly prunes unreachable
prefixes. Measure or comparator policies on concatenated values must compare equal. `meld` compares
comparators by identity (JVM `Comparator` has no structural equality): natural-order queues share one
stdlib singleton and always meld, but queues built with custom comparators must be constructed from a
single shared comparator instance.

`ReversibleDeque<T>` retains its specialized orientation-aware balanced storage: `reverse()` wraps or
unwraps a root in O(1), `concat` joins logical roots without materializing either operand, and endpoint
views, indexing, and splits navigate logical orientation. The general engine is a strict measured AVL
sequence rather than the C#/C++ lazy Hinze–Paterson digit spine; consequently its endpoint operations
are O(log n), not amortized O(1). No public facade retains the former flat-`List` representation.
