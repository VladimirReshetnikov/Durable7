# Proposal: Mutable Builders For FingerTree Collections

- Status: Proposed
- Created (UTC): 2026-07-01T18:14:04Z
- Revised (UTC): 2026-07-01T20:14:30Z
- Repository HEAD at creation: 8a0104880d57bcf77daeff761b74f352b14ce906
- Repository HEAD at revision: b1913bf759823ab78b1208ad94a9bd9a9839a8f5
- Audience: Maintainers designing bulk construction and builder APIs for the C# FingerTree workspace
- Scope: Applicability of `System.Collections.Immutable`-style builders; internal bulk-construction machinery;
  public builder API shape, semantic contracts, and complexity; validation, benchmarks, and rollout order

## Summary

The motivating question was: *`System.Collections.Immutable` has mutable builders that construct immutable
structures more efficiently — is that applicable and useful for our data structures?*

The honest answer is **yes, in a specific and bounded way**, and the largest part of the win has no public API
at all. This proposal is therefore structured as two decoupled deliverables:

1. **Part I — internal construction machinery.** Reusable internal helpers that build tree shapes directly
   instead of by repeated persistent appends: a validation hook for the general measured core (a prerequisite,
   and a standing gap), trust-sorted construction entry points for the sorted family, chunk staging for the
   rope family (including rerouting the existing text `RopeBuilder` freeze path), and — as a separately
   benchmark-gated prototype — bottom-up tree packers. Part I is useful on its own: it improves `CreateRange`
   and freeze paths for every structure without adding one public member.

2. **Part II — public builders.** Nested `Builder` types with `ToBuilder()` / `CreateBuilder()` entry points,
   shipped in a strict order and each gated on a benchmark or a demonstrated workload: sorted set and sorted
   dictionary builders first (the one clearly painful, uncovered public workload), then append-only rope
   builders (a design — the frozen-prefix builder — that keeps snapshot loops cheap), and nothing else until a
   concrete caller appears.

Equally important is what this proposal does **not** deliver, stated up front rather than discovered later:

- **The edit–snapshot loop is not accelerated, outside the rope family.** The BCL's
  `ImmutableList<T>.Builder` and `ImmutableSortedSet<T>.Builder` are *transient trees*: they adopt the
  immutable value's own nodes (`ToBuilder()` is O(1)), mutate uniquely owned nodes in place (O(log n) per
  edit), and refreeze in O(1) by flipping ownership. The builders proposed here are *staging* builders: they
  rebuild on a dirty freeze — with one exception, the append-only rope builders, whose frozen-prefix design
  keeps snapshots at O(k + log n). For any other workload that alternates small edit batches with snapshots,
  the persistent operations you already have are asymptotically better than these builders, and the
  complexity tables below say so explicitly. A transient tier over this library's lazy-memoized cores is
  deferred indefinitely and may prove not worth building
  (see [Owner-Token Transients](#owner-token-transients-status-and-feasibility)).

- **No builder is added for API symmetry.** The applicability review found that most of the collection family
  is already served by one-pass `CreateRange` factories and, for text, by the existing `RopeBuilder`. Builders
  for `SortedBag<T>`, `PriorityQueue<TElement, TPriority>`, `IntervalTree<T>`, `FingerTreeDeque<T>`, the raw
  measured tree, and indexed rope editing are all explicitly deferred, each with its recorded reason.

## Motivation: What Construction Costs Today

`System.Collections.Immutable` builders exist because every persistent edit path-copies; a builder amortizes
many private edits before publishing one immutable snapshot. This library's construction paths have the same
constant-factor shape, verified against the current source:

- **Every `CreateRange` builds by repeated persistent operations.** `FingerTreeDeque<T>.CreateRange` calls
  the private `BuildTree`, which loops `root = root.Snoc(new Leaf<T>(item))` per element; each `Snoc`
  overflow allocates a pending-push suspension (`Tree<T, TChild>.Snoc`). The sorted collections sort with
  `OrderBy` and then loop `tree = tree.Append(item)`. `PriorityQueue<TElement, TPriority>.CreateRange` is the
  same append loop without the sort (O(n)). `IntervalTree<T>.CreateRange` is different: it loops the
  persistent `Insert` (a split + append + concat per interval), O(n log n) with no pre-sort — packer routing
  for it must first add a sort-by-low-endpoint step. Apart from the interval tree, all of these are already
  amortized **linear** (after the sort, where one is involved) — the target is per-element allocation
  constants, *not* asymptotics.
- **The sorted `AddRange` family really is m independent persistent adds.** `SortedSet<T>.AddRange` is
  literally `foreach (var item in items) result = result.Add(item)` — O(m log n) with a full persistent-edit
  allocation per element. The same shape appears in `SortedBag<T>.AddRange`.
- **Enumerable-sourced rope construction materializes the whole input first.** `Rope<T>.CreateRange` and
  `MeasuredRope<T, TMeasure, TMeasureOps>.CreateRange` do `elements as T[] ?? [.. elements]` — a full-size
  temporary array — before chunking. For large inputs this is a second n-sized allocation that chunk staging
  eliminates outright.
- **The text `RopeBuilder` pays two full copies and a large-object-heap intermediate.** `RopeBuilder.ToRope()`
  copies the `StringBuilder` contents into one `char[n]` (`BuildArray`) and `Rope<char>.Create` then copies
  each `MaxChunkSize` slice into a chunk array. Above roughly 42K characters the intermediate `char[]` lands
  on the LOH. Chunk staging reduces this to at most one full copy (zero full copies with ownership transfer of
  retired chunks) and no n-sized intermediate.
- **Element-at-a-time rope appends copy the boundary chunk.** `Rope<T>.AddLast` is documented O(log n) "with a
  bounded chunk copy"; building a rope one element at a time pays that O(chunk-size) constant per element.
  (`Create`/`CreateRange` do *not* pay it; this cost bites only incremental construction.)

Two facts sharpen where builders can and cannot help:

- The library's persistent appends are already **amortized O(1)** (`FingerTree<…>.Append`,
  `FingerTreeDeque<T>.AddLast`, `PriorityQueue<…>.Enqueue`), so no builder can improve any bulk-construction
  *asymptotic*. Every claimed win in this document is a constant-factor, allocation, or ergonomics win, and
  the benchmark plan treats it that way.
- The existing `RopeBuilder` (StringBuilder-backed, one-pass materialization) is evidence the staging-builder
  idea fits this domain; this proposal generalizes its shape and reroutes its freeze path through shared
  machinery.

## The BCL Builder Model, Mapped Honestly

The BCL has two distinct builder engines, and the distinction drives everything below:

| BCL builder | Engine | `ToBuilder()` | Per edit | `ToImmutable()` |
| --- | --- | --- | --- | --- |
| `ImmutableArray<T>.Builder` | Mutable array staging | O(n) copy | O(1)/O(n) | O(n) copy (or `MoveToImmutable` transfer) |
| `ImmutableList<T>.Builder`, `ImmutableSortedSet<T>.Builder`, `ImmutableSortedDictionary<…>.Builder` | **Transient tree** (adopts the immutable value's own AVL nodes; mutates uniquely owned nodes in place) | **O(1)** | O(log n) | **O(1)** (ownership flip) |

This proposal adopts the **first** model (staging + rebuild-on-dirty-freeze, with one structural exception for
ropes) and defers the second. That choice is deliberate: the transient model requires mutating the same node
types the immutable values share, and this library's cores keep their middles behind lazy memoized suspensions
with cached measures — exactly the machinery in-place mutation endangers. The staging model has the same
publication safety as `CreateRange`: immutable values never share mutable state with a builder.

The cost of that choice, stated plainly rather than implied:

- **`ToBuilder()` is not O(1) here** (except for the rope builders, whose frozen-prefix design adopts the
  receiver — see below). Thawing a sorted collection into BCL sorted staging is O(n log n); thawing a deque
  into list staging is O(n).
- **A dirty `ToImmutable()` is a rebuild**, O(n) from sorted staging (O(n log n) where a sort is required),
  not an O(1) ownership flip.
- **The thaw–edit–refreeze round trip therefore has a break-even.** For E total edits spread over B snapshots
  on a collection of size n, the sorted builder costs O(E·log n + B·n) while plain persistent edits cost
  O(E·log n) with every intermediate version retained for free. The persistent loop is never asymptotically
  worse; the builder wins only on constants, and only when edits per snapshot are on the order of n/log n or
  more — large batches, few snapshots. The rope builders are the exception: the frozen-prefix design makes an
  append-heavy snapshot loop cost O(E + B·log n).

## Workload Guidance: Where Builders Win And Lose

This section is normative documentation content: the public builder XML docs and the design notes must carry
these recommendations.

| Workload | Use | Why |
| --- | --- | --- |
| Build a large collection from scratch, input available up front | `Create` / `CreateRange` | Already one-pass; Part I improves its constants for free. |
| Build incrementally when the input arrives piecemeal (append-heavy) | Rope/measured-rope builder (`RopeBuilder` for text) | Amortized O(1) staged appends; no boundary-chunk copies; frozen-prefix freeze is O(appended + log n). |
| Many set/map mutations, then one snapshot | Sorted builder | O(log n) staged edits, one O(n) trust-sorted freeze; avoids m persistent-edit allocations and `CreateRange`'s redundant re-sort. |
| Merge a small batch m into a large collection (m ≪ n) | Immutable `AddRange` / `Add` / `SetItem` | O(m log n) beats any rebuild-on-freeze builder, whose dirty freeze alone is O(n) or worse. |
| Alternate small edit batches with frequent snapshots | Persistent operations directly (sorted family); rope builder for append-only loops | Sorted staging builders cost O(E·log n + B·n) vs O(E·log n) persistent with free snapshots — the transient-builder workload the first wave does not serve. The frozen-prefix rope builder is the exception at O(E + B·log n). |
| Repeated snapshot of an unchanged builder | Any builder | O(1): the caching contract returns the same instance. |

## Builder Taxonomy

Three tiers, in increasing order of hazard:

### Bulk construction builder

Mutable staging outside the immutable tree (arrays, lists, BCL sorted containers); `ToImmutable()` builds a
fresh immutable value from staging. Publication safety equals `CreateRange`: no immutable value ever shares
mutable state with the builder. This is the default form for everything in Part II.

### Incremental freezing builder

Bulk construction plus a cached `(version, snapshot)` pair: a clean `ToImmutable()` returns the cached
instance in O(1). This is a two-field extension, not a separate design; the
[shared contract](#shared-builder-contract) makes it mandatory rather than optional.

The rope builders extend this one structural step further: the cache *is* the builder's frozen prefix, so a
freeze does not discard staging work already published (see
[the frozen-prefix design](#ropetbuilder-and-measuredropebuilder-append-only-b2)).

### Owner-token transient tree builder

Mutable internal nodes tagged with a builder identity; edits mutate in place only when the tag matches,
otherwise path-copy; freeze clears tags. This is the `ImmutableList<T>.Builder` model. It is **deferred
indefinitely**, with its hazards and feasibility doubts recorded in
[Owner-Token Transients](#owner-token-transients-status-and-feasibility). No part of Part I or Part II depends
on it.

## Non-Goals

The work proposed here must not:

- make published immutable instances mutable through any observable channel;
- expose builder staging memory through an immutable value unless it has been copied or ownership has been
  irrevocably transferred away from the builder;
- weaken the concurrent-read guarantees of published immutable values;
- implement mutable BCL interfaces (`IList<T>`, `IDictionary<TKey, TValue>`, `ISet<T>`) on builders — the
  read surface is honest read-only interfaces, the mutation surface is class-declared (see the
  [interface rule](#interfaces-read-surface-only));
- promise O(1) freeze after arbitrary edits, O(1) `ToBuilder()` (outside the rope family), or any acceleration
  of the edit–snapshot loop — those belong to the deferred transient tier;
- add a builder solely for API symmetry.

---

## Part I — Internal Construction Machinery (Phase A)

Part I splits into **A1**, unconditionally landable work that is correct and useful regardless of any
benchmark outcome, and **A2**, a benchmark-gated prototype with predefined acceptance thresholds and an
explicit null-result plan.

### A1.1 Measured-core validation hook (prerequisite)

The validation story for everything below relies on structural invariant checks after freezes, and the hook
inventory is currently uneven:

| Core / type | Hook today | Checks |
| --- | --- | --- |
| Tuned deque core (`Internal/Tree.cs`, `Node.cs`) | `ValidateAndCount()` | digit arity 1–3, node arity, cached sizes |
| `FingerTreeDeque<T>` | `internal ValidateInvariants()` | delegates to the core |
| `ReversibleDeque<T>` + reversible core | `ValidateInvariants()` / `ValidateAndCount()` | same, orientation-aware |
| `Rope<T>` / `MeasuredRope<…>` | `internal ValidateInvariants()` | chunk non-empty, chunk ≤ max, summed lengths = count |
| **General measured core (`Internal/Measured/`)** | **none** | — |
| **Raw `FingerTree<…>`, `SortedSet/Bag/Dictionary`, `PriorityQueue`, `IntervalTree`** | **none** | — |

A1 therefore starts by adding `ValidateAndCount()` to the general measured core (digit arity 1–4, node arity
2–3, cached node measures recomputed and compared, deep-level counts) and thin `internal ValidateInvariants()`
wrappers on the raw `FingerTree<…>` facade and the sorted/priority/interval collections (which additionally
assert order, uniqueness/multiplicity, and interval low-order/max-high annotations). Without this, a bottom-up
packer or a trust-sorted freeze could build a silently corrupt measured tree that only misbehaves under later
splits: the deque core's only shape guards are `Debug.Assert`s, compiled out in Release, and the general
measured core has no shape guards at all, in any configuration.

### A1.2 Trust-sorted construction entries

The sorted collections' `CreateRange` unconditionally re-sorts (`items.OrderBy(x => x, order)`) even when the
input is already ordered, and always constructs with the comparer passed to it. Builder freezes (and future
sorted merge internals) need a path that skips the redundant sort **and carries the builder's comparer
instance**:

```csharp
// Internal factories; callers guarantee strict comparer order (set/dictionary) or
// nondecreasing comparer order (bag). DEBUG builds assert the order while appending.
internal static SortedSet<T> FromSortedDistinct(IEnumerable<T> sorted, IComparer<T> comparer);
internal static SortedBag<T> FromSorted(IEnumerable<T> sorted, IComparer<T> comparer);
internal static SortedDictionary<TKey, TValue> FromSortedDistinctKeys(
    IEnumerable<KeyValuePair<TKey, TValue>> sorted, IComparer<TKey> comparer);
```

Each is a loop of amortized-O(1) `Append`s — the same loop `CreateRange` already runs after `OrderBy` — so the
O(n) freeze bound requires **no packer**; A2 only improves its constants. Two contract points these factories
pin down:

- **Comparer propagation.** The frozen value must carry the *same comparer instance* as the staging that
  produced the order. Freezing builder staging through public `CreateRange` would both re-sort (O(n log n))
  and, if the comparer argument were forgotten, silently order the frozen collection by
  `Comparer<T>.Default` — a correctness bug, not a performance bug. The internal factories make the comparer a
  required parameter.
- **Empty-freeze identity.** An empty input freezes exactly as `Create(comparer)` does today: the shared
  canonical empty singleton if and only if the comparer is `null` or reference-equal to
  `Comparer<T>.Default`; otherwise a fresh empty instance carrying the comparer. This matches the existing
  collapse rule in `SortedSet<T>.Create` and the internal wrap helpers.

### A1.3 Chunk staging and the text-builder reroute

Reusable chunk staging for the rope family:

```csharp
internal sealed class ChunkBuilder<T>
{
    public int Count { get; }
    public void Add(T item);
    public void Add(ReadOnlySpan<T> items);
    public void Clear();

    /// Retires ALL staging synchronously, before returning: every full chunk's backing array is
    /// transferred (not copied) into an immutable chunk; the final partial chunk is copied to exact
    /// length. After the call the builder owns no array that any returned chunk references.
    public Chunk<T>[] FreezeChunks();
}

internal sealed class MeasuredChunkBuilder<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    public int Count { get; }
    public TMeasure Measure { get; }                       // running fold, seeded from TMeasureOps.Empty
    public void Add(T item);
    public void Add(ReadOnlySpan<T> items);
    public void Clear();
    public MeasuredChunk<T, TMeasure, TMeasureOps>[] FreezeChunks();
}
```

Design points, each load-bearing:

- **Freeze is eager and materialized.** `FreezeChunks()` returns an array, not a lazy sequence. A deferred
  iterator would retire arrays per `MoveNext`, so an abandoned or repeated enumeration would leave an array
  simultaneously immutable-owned and builder-writable — precisely the aliasing the non-goals forbid. All
  retirement happens synchronously at the call, before any chunk is visible.
- **Ownership transfer needs no new chunk types.** `Chunk<T>`'s constructor already wraps the given array
  without copying under the contract that it is never externally mutated afterwards; transfer is a discipline
  the builder enforces (a retired array is never written again; subsequent `Add`s allocate fresh storage), not
  a new representation.
- **Transferred chunks are always exact-length.** Retired full chunks are exactly `MaxChunkSize`; the final
  partial chunk is copied to its exact length. No chunk with slack capacity ever enters an immutable rope, so
  the rope's chunk-size policy (invariant: 1 ≤ length ≤ `MaxChunkSize`; `MinChunkSize` is a coalescing
  heuristic, not an invariant) is preserved without special cases.
- **A measure-carrying internal chunk factory is required.** The only `MeasuredChunk` constructor accepting a
  precomputed measure is currently private; both public constructors rescan the chunk. A1 promotes the
  measure-carrying constructor to `internal` so `MeasuredChunkBuilder` can attach its accumulated per-chunk
  measure without a second O(chunk) scan. (For law-abiding measures the accumulated and recomputed values are
  equal; DEBUG builds may assert it.)
- **The text `RopeBuilder` reroutes its freeze path with zero public API change.** `ToRope()`/`ToTextRope()`
  switch from `BuildArray()` + `Create` (two full copies, one LOH-sized intermediate) to chunk staging (at
  most the final partial chunk copied). Three implementation requirements keep this from regressing against
  the highly tuned `StringBuilder`: (1) span/string appends must be memcpy paths, never per-char loops;
  (2) retired full chunks transfer ownership, only the final partial chunk is copied; (3) newline measures for
  `ToTextRope()` are computed per retired chunk with a vectorizable scan at freeze — `RopeBuilder` exposes no
  live `Measure`, so nothing forces per-character accumulation on the hot append path. The reroute lands
  together with the new `RopeBuilderBenchmarks` family (defined in the Benchmark Plan) run against the
  `StringBuilder` baseline before merging: the architecture decision is settled by the copy arithmetic, the
  verification is not skipped.
- `Rope<T>.CreateRange` / `MeasuredRope<…>.CreateRange` reroute through chunk staging as well, eliminating
  their full-size `T[]` intermediate for non-array enumerables.

### A1.4 Facade wrap factories

The rope cores and the sorted collections hold their trees behind the *public* facade
`FingerTree<TElement, TMeasure, TMeasureOps>`, whose only constructor is private. Any internal machinery that
constructs a measured tree directly (A2's packer; potentially the chunk-tree assembly) needs an internal
entry point:

```csharp
// On FingerTree<TElement, TMeasure, TMeasureOps>:
internal static FingerTree<TElement, TMeasure, TMeasureOps> WrapRoot(
    MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps> root);
```

Without this, two of the packer's routing targets (rope chunk trees, sorted-collection internals) cannot
consume packer output at all. It is an internal member; Phase A remains free of public API.

### A2 Bottom-up tree packers (benchmark-gated prototype)

The packers construct finished tree shapes in one pass — computed middles, no suspensions, cached metadata
filled during construction — instead of n persistent appends:

```csharp
internal static class TreeBulkBuilder
{
    public static Tree<T, Leaf<T>> Build<T>(ReadOnlySpan<T> items);
    public static Tree<T, Leaf<T>> Build<T>(IEnumerable<T> items, int capacity);   // preserves the
    // OverflowException contract BuildTree enforces today for CreateRange/InsertRange
}

internal static class MeasuredTreeBulkBuilder
{
    public static MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps>
        Build<TElement, TMeasure, TMeasureOps>(ReadOnlySpan<TElement> items)
        where TMeasureOps : IMeasure<TElement, TMeasure>;
}
```

#### Worker shape: polymorphic recursion is mandatory

Element height is encoded in the type system: the middle of a `Tree<T, TChild>` is a
`Tree<T, Node<T, TChild>>`. A packer therefore cannot be a flat loop over one span; it must be a
polymorphically recursive generic worker,

```csharp
private static Tree<T, TChild> Pack<T, TChild>(TChild[] level) where TChild : ITreeElement<T, TChild>
```

which chooses this level's prefix/suffix digits, groups the interior children into `Node2`/`Node3` values in a
fresh `Node<T, TChild>[]`, and recurses as `Pack<T, Node<T, TChild>>`. Precedent exists: `TreeOperations.Glue`
already recurses generically across levels, and the recursion depth (hence runtime generic instantiation
depth) is O(log n). The costs the packer *adds* — roughly n/2 node objects plus geometric per-level arrays
(n/3 + n/9 + …) — are exactly what the benchmark gate weighs against `Snoc`'s per-append allocations and
suspension cells. The `ReadOnlySpan<T>` overload avoids copying at level 0 only; deeper levels always allocate
their node arrays.

The measured packer follows the same shape against the measured core's representation (digits 1–4, nodes as
`TChild[]` arrays, every element wrapped in `MeasuredLeaf`), computing each node's measure once at
construction.

#### The shape rule (correctness-critical)

At each level, after choosing a prefix digit of p children and a suffix digit of s children
(deque: 1 ≤ p, s ≤ 3; measured core: 1 ≤ p, s ≤ 4), the remaining interior count r = c − p − s **must be 0 or
at least 2**, because interior children can only be grouped into nodes of 2 or 3. A naive
"fill digits greedily, group the rest by threes" packer produces an impossible r = 1 — first at c = 7 for the
deque, and recursively at any level where the count lands on that residue. The deque core guards its shapes
only with `Debug.Assert`s, compiled out in Release, and the measured core has no shape guards in any
configuration: a wrong packer would *silently* build a corrupt tree that only the test-only validation hooks
catch.

The rule and the small-count table are therefore normative:

| c (children at level) | Shape (deque, digits 1–3) |
| ---: | --- |
| 0 | empty |
| 1 | single |
| 2 | 1 · — · 1 |
| 3 | 2 · — · 1 |
| 4 | 2 · — · 2 |
| 5 | 3 · — · 2 |
| 6 | 3 · — · 3 |
| 7 | 2 · (Node2) · 3 — shrink a digit; 3 · ? · 3 would leave r = 1 |
| 8 | 3 · (Node2) · 3 |
| 9 | 3 · (Node3) · 3 |
| 10 | 3 · (Node2, Node2) · 3 |

Unit tests must cover every count 0–10 *at every recursion level* (e.g. by validating packed trees for all
n ≤ ~100 plus targeted large sizes) with the A1.1 hooks.

#### Computed middles and eager measures

- The packer constructs middles with the existing computed-tree representation
  (`MiddleTree<T, TChild>` has a constructor taking a computed `Tree<T, TChild>`; the measured core's middle
  cell has the equivalent) — no suspensions, because bulk construction has no deferred endpoint repair to
  amortize, which is the entire purpose of a suspension.
- Deep-node aggregate measures are made **eager** on packer-built trees, realized mechanically: the deep
  measure cells are populated only by the first `Measure` read (compare-exchange into a memo box), so the
  packer simply forces `root.Measure` once after packing — O(n) combines, memoized per deep node, no new
  constructor surface. Rationale: every wrapper's `Count` reads the root measure immediately anyway
  (`SortedSet<T>.Count => _tree.Measure.Count`), the combines are cache-hot at pack time, and pre-computed
  measures shrink the concurrency surface of freshly built values (no first-force races). Persistent edits
  applied *after* construction produce lazily memoized descendants exactly as today.

#### What routing through the packers must preserve

- **Exception contracts.** `CreateRange` documents `OverflowException` beyond `int.MaxValue` elements and
  `InsertRange` relies on a running `capacity` check *during* enumeration (`BuildTree(items, int.MaxValue -
  Count)`). The `IEnumerable` packer overload takes the same capacity parameter and throws the same exception
  at the same point — not an `OutOfMemoryException` from an unbounded `List<T>` after consuming gigabytes.
- **Behavioral test intent.** Packed trees have different shapes than `Snoc`-built trees: different digit
  distributions and no pending suspensions. A preliminary audit indicates the pinned complexity-guard tests
  (comparer-call counts, allocation windows) assert *upper bounds* that shallower packed shapes still satisfy,
  and the lazy-repair tests build their histories through endpoint operations — but the audit is a required
  A2 deliverable, not an assumption. One verified coverage drain must be fixed alongside the reroute:
  `ConcurrentFirstReads_OfAFreshStructure_Converge` builds its "fresh lazy structure" via `CreateRange`, and a
  packer-routed `CreateRange` leaves it nothing to race on; that fixture (and any like it) must be rebuilt
  with endpoint-operation loops so the suspension-racing machinery stays exercised.
- **Routing targets.** `FingerTreeDeque<T>.CreateRange`/`InsertRange`, `FingerTree<…>.CreateRange`, the
  trust-sorted factories of A1.2, priority-queue and interval-tree bulk creation, and rope chunk-tree assembly
  (via `WrapRoot`).

#### Gate, thresholds, and the null-result plan

A2 merges only if `BulkBuildBenchmarks` shows, for representative sizes (10³–10⁶), either ≥ 25% fewer
allocated bytes per built element or ≥ 15% lower build time versus the current append-built path, with no
regression in first-read latency benchmarks. If the thresholds are missed, the packers do **not** merge; the
benchmark harness, the A1 machinery, and the negative result recorded in `docs/benchmarks.md` remain — the
question is settled by data rather than relitigated. (Do not overfit the packer to one "perfect" shape: the
required properties are valid invariants, preserved order, correct cached metadata, and competitive
allocation, not a specific silhouette.)

---

## Part II — Public Builders (Phase B)

### Shared builder contract

Every public builder obeys all of the following. These rules are the contract the XML documentation
(mandatory: `CS1591`/`CS1573` are build errors) is written from.

#### Placement, naming, and entry points

- Builders are **nested** `public sealed class Builder` types on their owning collection, which gains the
  `partial` modifier; each builder lives in a sibling file `<TypeName>.Builder.cs` (e.g. `Rope.Builder.cs`).
  Nested is the `System.Collections.Immutable` pattern this proposal models itself on, appears in IntelliSense
  exactly where users look (on the collection), and adds no top-level name collisions in a namespace that
  already deliberately reuses BCL collection names.
- Entry points: instance `ToBuilder()` and static `CreateBuilder()`
  (`CreateBuilder(IComparer<T>? comparer = null)` on sorted types, mirroring `Create`). `ToBuilder()` carries
  the source's comparer.
- The existing top-level `RopeBuilder` is untouched and re-documented as the **text writer** of the library —
  the `StringBuilder` analogue with `Append(string/char/span/Rune)`/`AppendLine` and terminal
  `ToRope()`/`ToTextRope()`. Three-role rule for character construction: `RopeBuilder` for text ergonomics;
  `Rope<char>.Builder` for code generic over the element type; `MeasuredRope<char, int, NewlineMeasure>.Builder`
  normally reached through `RopeBuilder.ToTextRope()` rather than used directly. `RopeBuilder`'s XML docs gain
  a `<seealso>` pointing generic scenarios at `Rope<T>.Builder`.

#### Return conventions (one rule, no exceptions)

Every builder member takes the return convention of the corresponding member of its closest **mutable BCL
analogue**: `void` for unconditional mutators, `bool`/`int` for conditional mutators that report whether or
how much they changed. Concretely: rope/measured-rope builders follow `List<T>` (`void Add/AddRange/Clear`);
the set builder follows `System.Collections.Generic.SortedSet<T>` (`bool Add`, `bool Remove`,
`void UnionWith/ExceptWith/Clear`); the dictionary builder follows `System.Collections.Generic.SortedDictionary`
for `void Add/Clear` and `bool Remove`, the `IDictionary` `TryAdd` extension shape for `bool TryAdd`, and the
immutable type's own verb for `void SetItem` (the upsert; the BCL type expresses it as a settable indexer,
which the interface rule below excludes). The resulting library-wide invariant:

> On a nested `Builder`, **no member returns the builder type**, and a member returning a collection value is
> always `ToImmutable()`. "Returns a value" can therefore never be misread as "returned a new persistent
> version" — the confusion a fluent builder invites in a library where `Rope<T>.Insert` returns a new rope.

`RopeBuilder` stays fluent and is not an exception to the rule: its mutable analogue is `StringBuilder`, which
is itself fluent.

#### Interfaces: read surface only

A builder implements exactly the read-only interfaces that make its read surface honest, never a mutable BCL
interface, and never an indexer setter:

- Append-only builders (rope, measured rope): `IReadOnlyCollection<T>`.
- Sorted set builder: `IReadOnlyCollection<T>`.
- Sorted dictionary builder: `IReadOnlyDictionary<TKey, TValue>` (get-only indexer, `Keys`, `Values`,
  `ContainsKey`, `TryGetValue`); the write verb is `void SetItem(TKey key, TValue value)`.
- (Deferred) deque builder: `IReadOnlyList<T>` with a get-only indexer plus `void SetItem(int index, T value)`.

Rationale: LINQ needs only `IEnumerable<T>`; the read-only interfaces add honest `Count`/indexer fast paths;
mutable interfaces would force `NotSupportedException` members on append-only slices, `void ICollection<T>.Add`
on a set builder that needs `bool Add`, O(n) `Remove(T)`/`IndexOf` on ropes, and roughly ten explicitly
implemented members of documentation burden per builder. A settable indexer on a class whose only collection
interface is read-only is the shape both review rounds flagged; `SetItem` reuses the immutable types' own
replacement verb instead. (Note `List<T>` itself implements `IReadOnlyList<T>`: a read-only *interface* has
never promised an immutable *object*, and the docs must not claim otherwise.)

#### Verb table (library-wide)

| Builder | Verbs | Source of the verb |
| --- | --- | --- |
| `Rope<T>.Builder`, `MeasuredRope<…>.Builder` | `Add`, `AddRange(span/enumerable)`, `Clear` | `List<T>` / `ImmutableList<T>.Builder` |
| `SortedSet<T>.Builder` | `bool Add`, `bool Remove`, `UnionWith`, `ExceptWith`, `Clear` | BCL `SortedSet<T>` (also `ImmutableSortedSet<T>.Builder`); `IntersectWith`/`SymmetricExceptWith` are compatible later additions |
| `SortedDictionary<…>.Builder` | `Add` (throws on duplicate), `bool TryAdd`, `SetItem` (upsert), `AddRange` (Add semantics), `bool Remove`, `Clear` | BCL `SortedDictionary` + immutable `SetItem` |
| (deferred) deque builder | `AddFirst`, `AddLast`, `AddRange`, `SetItem`, `Clear` | immutable deque's own verbs |
| (deferred) bag builder | `Add`, `AddRange`, `bool Remove`, `int RemoveAll`, `Clear` | immutable bag's verbs |
| (deferred) priority-queue builder | `Enqueue`, `EnqueueRange`, `Clear` | immutable queue's verbs |
| (deferred) interval-tree builder | `Insert` (the immutable type's verb — not `Add`), `bool Remove`, `Clear` | immutable interval tree's verbs |

`Append`/`AppendLine` remain exclusively on the text `RopeBuilder`. No verb aliases anywhere.

#### Version, caching, and invalidation (single mechanism)

Each builder holds a private `int _version` and a cached `(_cachedVersion, _cachedResult)` pair.

- **Caching is a MUST.** `ToImmutable()` returns the *reference-identical* cached instance, in O(1), whenever
  `_version` is unchanged since the previous `ToImmutable()`. (The rope builders satisfy this structurally:
  their cache is the frozen prefix.)
- **The version advances iff the operation structurally changed staged contents, as reported by the
  operation's own result.** `bool Add` returning false, `bool Remove` returning false, `TryAdd` returning
  false, `RemoveAll` returning 0, `Clear` on an already-empty builder, and `UnionWith`/`AddRange` that added
  nothing do **not** advance it. Unconditional writes (`SetItem`, rope `Add`/`AddRange` with non-empty input)
  **always** advance it — no value-equality probing is ever performed, so cache behavior never depends on
  element-type equality quality or cost. Documented consequence: `builder.SetItem(key, sameValue)` invalidates
  the cache and the next freeze rebuilds; callers wanting same-instance stability compare before writing.
- **Enumeration is fail-fast off the same counter.** Every builder enumerator captures `_version` at creation
  and throws `InvalidOperationException` on mismatch, `List<T>`-style, uniformly across staging
  representations. This single mechanism replaces the previous draft's "documented like ordinary mutable
  collections" hand-wave: "the version advanced" has exactly one observable meaning.

#### Snapshot isolation, thread safety, publication

- Mutating a builder after `ToImmutable()` never affects any previously produced immutable value.
- Builders are single-owner, single-threaded mutable objects: no thread safety is provided for concurrent
  mutation or read-while-mutate. (The version counter is for fail-fast detection, not synchronization.)
- Produced immutable values keep the library's full contract: safe for concurrent reads; publishable via
  `Volatile.Write`/`Interlocked.Exchange`; no builder staging memory reachable from them (copy or irrevocable
  ownership transfer only).
- Comparer and element-object caveats are unchanged from the immutable collections: the caller can still
  mutate reference-type elements, and comparer order must remain stable.

#### Capacity and overflow

A builder can stage at most `int.MaxValue` elements (the immutable types' own bound). Staging or freezing
beyond a target type's capacity throws the same exception the corresponding immutable factory documents
(`OverflowException` for the deque family), at the same point — during staging, not after exhausting memory.

#### Empty freeze

`ToImmutable()` on an empty builder is equivalent to `Create(Comparer)`: the shared canonical empty singleton
iff the comparer is `null`/reference-equal to `Comparer<T>.Default` (or the type has no comparer); otherwise a
fresh empty instance carrying the builder's comparer.

### `SortedSet<T>.Builder` and `SortedDictionary<TKey, TValue>.Builder` (B1 — first public builders)

The one clearly painful public workload today is the sorted family's `AddRange`/batch-mutation path
(m independent persistent `Add`s). These two builders ship first.

```csharp
public sealed partial class SortedSet<T>
{
    public Builder ToBuilder();                                        // O(n log n): populates BCL staging
    public static Builder CreateBuilder(IComparer<T>? comparer = null);

    public sealed class Builder : IReadOnlyCollection<T>
    {
        public int Count { get; }                                      // O(1)
        public IComparer<T> Comparer { get; }
        public T Min { get; }                                          // O(log n) (staging), vs O(1) immutable
        public T Max { get; }                                          // O(log n) (staging), vs O(1) immutable

        public bool Contains(T item);                                  // O(log n)
        public bool Add(T item);                                       // O(log n); keeps the existing
                                                                       // representative on a comparer-equal hit
        public bool Remove(T item);                                    // O(log n)
        public void UnionWith(IEnumerable<T> items);
        public void ExceptWith(IEnumerable<T> items);
        public void Clear();

        public SortedSet<T> ToImmutable();                             // clean: O(1) cached instance;
                                                                       // dirty: O(n) via FromSortedDistinct
    }
}

public sealed partial class SortedDictionary<TKey, TValue>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder(IComparer<TKey>? comparer = null);

    public sealed class Builder : IReadOnlyDictionary<TKey, TValue>
    {
        public int Count { get; }
        public IComparer<TKey> Comparer { get; }
        public TValue this[TKey key] { get; }                          // get-only (interface member)

        public bool ContainsKey(TKey key);                             // O(log n)
        public bool TryGetValue(TKey key, out TValue value);           // O(log n)
        public void Add(TKey key, TValue value);                       // O(log n); throws on duplicate key
        public bool TryAdd(TKey key, TValue value);                    // O(log n)
        public void SetItem(TKey key, TValue value);                   // O(log n) upsert (the write verb)
        public void AddRange(IEnumerable<KeyValuePair<TKey, TValue>> items);  // Add semantics: throws on
                                                                       // duplicate; last-wins import is a
                                                                       // SetItem loop or CreateRange
        public bool Remove(TKey key);                                  // O(log n)
        public void Clear();

        public SortedDictionary<TKey, TValue> ToImmutable();
        public IEnumerable<TKey> Keys { get; }
        public IEnumerable<TValue> Values { get; }
    }
}
```

Implementation and contract notes:

- **Staging** is `System.Collections.Generic.SortedSet<T>` / `SortedDictionary<TKey, TValue>` constructed with
  the builder's comparer. Freeze enumerates the staging in order into the A1.2 trust-sorted factories,
  passing the same comparer instance — O(n), no re-sort, comparer preserved by construction. A round-trip test
  pins `ReferenceEquals(frozen.Comparer, builder.Comparer)`.
- **Representative semantics** match the immutable type and the BCL staging identically: `Add` of a
  comparer-equal element is a no-op that keeps the existing element (as `SortedSet<T>.Add` does on the
  immutable side); `Remove` followed by `Add` legitimately installs the new element, because that is the
  mutation sequence the caller requested. A test pins the agreement.
- **No rank/index operations in wave 1** — no `this[int rank]`, no `IndexOf`, no `EntryAt`. The BCL staging
  has no rank access, so those members would silently cost O(n) under names the immutable types document as
  O(log n), violating the library's per-member complexity culture. The documented pattern for mid-batch rank
  queries is **query the snapshot**: call `ToImmutable()` (O(1) and reference-cached when clean) and use the
  immutable value's rank operations. Rank-capable builders are possible later only behind a benchmark-justified
  order-statistic staging structure.
- **Honest comparison, pipeline versus pipeline.** For building from m items: builder total is
  O(m log m) staging + O(n) freeze; `CreateRange` is O(m log m) sort + O(n) appends — the **same asymptotic
  class**. What the builder eliminates is `CreateRange`'s redundant re-sort when the data already lives in
  sorted staging, the per-element persistent-edit allocations of the `AddRange` path, and the possibility of
  comparer mismatch between staging and freeze. The B1 gate is therefore a *constants* benchmark
  (allocations and time versus both `AddRange` and collect-then-`CreateRange`), not an asymptotic claim.
- **When not to use it** (normative doc content): merging a small batch m into a large existing collection is
  asymptotically better through immutable `Add`/`AddRange` (O(m log n)) than through
  `ToBuilder` + edits + freeze (O(n log n) thaw + O(n) rebuild); and the edit–snapshot loop belongs to the
  persistent operations outright.

### `Rope<T>.Builder` and `MeasuredRope<…>.Builder` (append-only, B2)

The definitive first-slice surface — deliberately six members (seven on the measured builder, which adds
`Measure`); every deferred member (`Insert`, `RemoveAt`, indexed writes) is addable later without breaking
anything:

```csharp
public sealed partial class Rope<T>
{
    public Builder ToBuilder();                       // O(1): adopts this rope as the frozen prefix
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyCollection<T>
    {
        public int Count { get; }                     // O(1)
        public void Add(T item);                      // amortized O(1), staged into a mutable chunk
        public void AddRange(ReadOnlySpan<T> items);  // O(items.Length) memcpy
        public void AddRange(IEnumerable<T> items);
        public void Clear();
        public Rope<T> ToImmutable();                 // O(k + log n), k = elements staged since last freeze;
                                                      // O(1) cached when clean
    }
}
```

`MeasuredRope<T, TMeasure, TMeasureOps>.Builder` is identical plus `public TMeasure Measure { get; }` —
**seeded from `TMeasureOps.Empty`**, folded incrementally on every `Add`/`AddRange`, reset by `Clear`, always
equal to the combined measure of the current contents (measures are monoids without inverses, which is exactly
why this builder ships append-only: an overwrite or removal could not maintain `Measure` incrementally).

**The frozen-prefix representation.** The builder holds an immutable `Rope<T>` *prefix* plus chunk staging
(A1.3) for the appended tail:

- `ToBuilder()` adopts the receiver as the prefix — **O(1)**, the one place this design matches the BCL's
  O(1) thaw.
- `ToImmutable()` freezes the staged tail into chunks (retired full chunks transfer ownership; the final
  partial chunk is copied to exact length), concatenates `prefix.Concat(tail)` — `Rope<T>.Concat` already
  coalesces boundary chunks, so snapshot loops do not accumulate seams of undersized chunks — and **adopts the
  result as the new prefix**. A snapshot therefore costs O(k + log n) rather than O(n), and an append-heavy
  edit–snapshot loop costs O(E + B·log n) total: the rope family is the one place the first wave keeps the
  snapshot loop cheap.
- `Clear()` resets the prefix to the empty rope and discards staging.

Shipping order: the internal chunk staging (A1.3) lands first and pays for itself through `RopeBuilder` and
`CreateRange`; the public generic builders ship after B1, gated on `RopeBuilderBenchmarks` results, since the
dominant text workload is already served by `RopeBuilder`.

### Deferred builders, with reasons

Each deferral is a recorded decision, not an omission. Adding any of these later requires the
[adoption criteria](#adoption-criteria) plus the specific evidence named here.

- **`SortedBag<T>.Builder`.** A list-staging + stable-sort builder is `SortedBag<T>.CreateRange` by another
  name (that factory already stable-sorts and preserves the documented equal-element insertion order); the
  Run-keyed staging that would support builder-side `Remove`/`CountOf` (a `SortedDictionary<T, Run>` whose
  runs are plain insertion-ordered lists of *all* comparer-equal values — no privileged "representative"; for
  a bag every equal value is a real element and freeze must emit them all, in order) is real machinery with no
  demonstrated caller. Also note: `ToBuilder` + few adds on a large bag would *pessimize* — re-sorting n + m
  elements at freeze is O((n+m) log(n+m)) versus `AddRange`'s O(m log n).
- **`PriorityQueue<TElement, TPriority>.Builder`.** Immutable `Enqueue` is already amortized O(1)
  (`_tree.Append`; the only comparisons are the amortized-O(1) node-measure combines on digit overflow — no
  per-element search), and `CreateRange` is an O(n) append loop; a bulk-enqueue builder can only re-deliver
  packer constants that Part I already gives `CreateRange` internally. Meld preparation is served by
  `CreateRange` + `Meld`. If one ever ships: list staging, freeze re-appends in enqueue order (never heap
  order — equal-priority FIFO stability depends on it); builder-side `TryPeek`/`TryDequeue` over list staging
  would be O(n) scans, *worse* than the immutable members (`TryPeekPriority` O(1); `TryPeek`/`TryDequeue`
  O(log n)), and must either be disclosed as such or replaced by heap staging with a separate insertion-order
  record. One contract decision is recorded now:
  the immutable type documents enumeration as "unspecified (insertion) order", so a `ToBuilder` round-trip
  must read the internal tree sequence (insertion order by construction) rather than rely on the public
  enumeration contract — or that contract must first be strengthened to guaranteed insertion order.
- **`IntervalTree<T>.Builder`.** Bulk load is `CreateRange`; batch edit plus coalesce has no demonstrated
  caller. If one ships: the verb is `Insert` (the immutable type's verb), `bool Remove`, `void Clear`, freeze
  sorts by low endpoint preserving duplicate/value-equal semantics; list-staged `Contains`/overlap queries are
  O(n) and the documented pattern is query-the-snapshot, as with the sorted builders.
- **`FingerTreeDeque<T>.Builder` and raw `FingerTree<…>.Builder`.** List-backed staging whose freeze calls the
  A2 packers — legitimate, but valuable only after A2 merges; they primarily expose the packers publicly. The
  deque builder's sketched surface (recorded for then): `IReadOnlyList<T>` with get-only indexer,
  `AddFirst`/`AddLast`/`AddRange`/`SetItem`/`Clear`/`ToImmutable`. The raw measured builder is append-only
  with a `Measure` property under the same monoid constraint as the measured rope builder.
- **Indexed-edit rope builder** (`Insert`/`RemoveAt`/`SetItem` on `Rope<T>.Builder`). Its natural first
  implementation (materialize staging into a `List<T>`) would give O(n) edits behind an API that mirrors
  O(log n) immutable members, and — for the measured rope — would discard cached per-chunk measures and force
  a full O(n) re-measure at freeze. It waits for an editor-shaped workload that the immutable rope's own
  O(log n) edits do not already serve.

---

## Complexity And Allocation

The load-bearing table. "Clean" means no version advance since the last `ToImmutable()`.

| Operation | Cost | Notes |
| --- | --- | --- |
| Rope builder `Add` | amortized O(1) | mutable chunk staging; no boundary-chunk copy per element |
| Rope builder `AddRange` (span of m) | O(m) | memcpy into staged chunks |
| Rope builder `ToImmutable`, dirty | **O(k + log n)** | k = staged since last freeze; transfer + bounded copy + `Concat` |
| Rope builder `ToImmutable`, clean | O(1) | frozen prefix returned |
| `Rope<T>.ToBuilder()` | **O(1)** | prefix adoption (the rope-family exception) |
| Measured rope builder `Measure` | O(1) | running monoid fold, seeded from `TMeasureOps.Empty` |
| Sorted builder `Add`/`Remove`/`Contains` | O(log n) | BCL sorted staging |
| Sorted builder `UnionWith` (m items) | O(m log(n + m)) | staging inserts |
| Sorted builder `ToImmutable`, dirty | **O(n)** | in-order enumeration into a trust-sorted factory; no re-sort |
| Sorted builder `ToImmutable`, clean | O(1) | cached instance (reference-equal) |
| Sorted `ToBuilder()` | **O(n log n)** | populates BCL staging; O(n) only if a custom sorted bulk-load into staging is ever built |
| (deferred) deque/raw builder `Add*` | amortized O(1) | list staging |
| (deferred) deque/raw builder `ToImmutable`, dirty | O(n) | bottom-up pack (A2) or append loop |
| (deferred) deque/raw `ToBuilder()` | O(n) | copy into list staging |
| Any builder, repeated `ToImmutable` with no mutation | O(1) | MUST return the cached instance |

**Workload totals** (E edits, B snapshots, size n — the rows that keep the table honest):

| Pattern | Staging builder | Persistent operations |
| --- | --- | --- |
| Edit–snapshot loop, sorted family | O(E·log n + **B·n**) | **O(E·log n)** — dominant; snapshots free |
| Edit–snapshot loop, rope (append-only) | **O(E + B·log n)** | O(E·(log n + chunk)) with boundary copies |
| Small batch m into large n | O(n log n) thaw + O(n) freeze | **O(m log n)** via `AddRange` — dominant |
| Bulk build from m items, sorted | O(m log m + n) | `CreateRange`: O(m log m + n) — same class; builder wins constants only |

Builders improve constants and batch behavior; they must never be documented in a way that obscures the
persistent operations' contracts. Builder complexity is always phrased separately from immutable operation
complexity, per member, in the XML docs.

## Validation Plan

### Prerequisite

A1.1's `ValidateAndCount` on the general measured core plus `ValidateInvariants` wrappers on the raw facade
and the sorted/priority/interval collections. Every freeze-producing test below calls the relevant hook.

### Unit tests (every builder)

- Empty builder freezes per the empty-freeze rule: reference-equal to the canonical singleton for the
  default comparer (`ReferenceEquals(builder.ToImmutable(), SortedSet<T>.Empty)`); a fresh instance with
  `ReferenceEquals(result.Comparer, builder.Comparer)` for a custom comparer.
- Builder operations match the immutable operation model (add/remove/set semantics, duplicate handling,
  representative retention, last-wins upsert).
- Snapshot isolation: mutations after `ToImmutable()` never affect previously produced values.
- `ToBuilder()` round-trips elements, comparer (by reference), and measures.
- Caching: clean repeat `ToImmutable()` is reference-equal; structural no-ops (`Add` of a present element,
  `Remove` of an absent one, `TryAdd` collision, empty `AddRange`) do **not** invalidate; `SetItem` with an
  equal value **does**.
- Fail-fast enumeration: mutation during enumeration throws `InvalidOperationException`; structural no-ops do
  not (they do not advance the version).
- Capacity: staging past the documented bound throws the documented exception type.
- Argument validation matches the immutable APIs.

### Invariant and shape tests

- A1.1 hooks after every builder freeze and packer build.
- Packer shape tests: all counts 0 through ~100 plus targeted large sizes, validated at every level
  (the r = 1 borrow rule is the specific bug class these exist to catch, in Release mode where the cores'
  `Debug.Assert`s vanish).
- Rope freeze: chunk lengths within 1..`MaxChunkSize`, exact-length transferred chunks, boundary coalescing
  after snapshot loops.

### Property and model-based tests

Extend `ModelBasedCommandTests` command alphabets with: `ToBuilder`, builder mutations, `ToImmutable`,
continue-mutating-after-snapshot, and switch-back-to-immutable-operations, so the shrinker can produce minimal
scripts of the form `ToBuilder; Add 3; Snapshot; Remove 3; AssertSnapshotContains 3`. The measured-rope
builder sequences additionally assert `builder.Measure` equals the model fold after every command.

### Concurrency tests

Builders themselves are single-threaded; what needs tests is publication: freeze, publish via
`Volatile.Write`/`Interlocked.Exchange`, read from many threads, keep mutating the builder privately, verify
readers of the old snapshot stay stable — the existing tearable-struct stress harness extends naturally.

### Coverage audit (A2 deliverable)

Audit tests whose *intent* is first-force/suspension behavior and whose fixtures are built via `CreateRange`;
rebuild those fixtures with endpoint-operation loops when `CreateRange` becomes packer-routed
(`ConcurrentFirstReads_OfAFreshStructure_Converge` is the verified instance). Re-baseline any pinned
complexity-guard bounds that a packed shape shifts (preliminary audit says none shift, but verification is
part of the deliverable).

## Benchmark Plan

All families report allocated bytes as first-class results, not just time; a timing win with an allocation
regression on snapshot paths fails the gate.

- `BulkBuildBenchmarks` (A2 gate): current append-built `CreateRange` versus packer, per structure,
  10³–10⁶ elements; first-read latency after build (eager-measure effect); thresholds and the null-result
  plan as specified in A2.
- `RopeBuilderBenchmarks` (A1.3 verification + B2 gate): element-at-a-time and span appends then freeze,
  versus repeated `AddLast`, `Rope.Create`, `StringBuilder` + `Create` (current path), and
  `ImmutableList<T>.Builder` where meaningful; text path (`ToTextRope`) versus the current
  `StringBuilder`-backed path, including large-input LOH behavior.
- `SortedBuilderBenchmarks` (B1 gate): batch add/remove + freeze versus (a) repeated immutable `Add`/`AddRange`
  and (b) caller-side collect-into-BCL-set + `CreateRange`, and versus `ImmutableSortedSet<T>.Builder` /
  `ImmutableSortedDictionary<…>.Builder` for external reference. The gate compares constants; the document
  already concedes the asymptotic class is unchanged.
- `SnapshotBenchmarks`: repeated `ToImmutable()` with and without intervening mutations, including the
  **edit–snapshot loop measured explicitly against the persistent-operations baseline** (mutate k times,
  snapshot, repeat) — the workload the staging builders are predicted to lose; the numbers go in the docs so
  the guidance table is evidence-backed. Rope builder snapshot loops run separately (predicted to win via the
  frozen prefix).

## Rollout And Scope Estimates

Estimates are velocity-independent (files, members, tests), per repository convention.

| Phase | Contents | Gate | Rough scope |
| --- | --- | --- | --- |
| **A1** | Measured-core `ValidateAndCount` + facade/collection `ValidateInvariants`; trust-sorted factories; `ChunkBuilder`/`MeasuredChunkBuilder` + `MeasuredChunk` internal measure ctor; `WrapRoot`; `RopeBuilder`/`CreateRange` reroutes | None (unconditional); the new `RopeBuilderBenchmarks` family verifies the reroute | ~10 files touched, ~15 internal members, ~40–60 tests |
| **A2** | `TreeBulkBuilder`, `MeasuredTreeBulkBuilder`; `CreateRange`/`InsertRange`/bulk-path routing; coverage audit + fixture rebuilds | `BulkBuildBenchmarks` thresholds; null-result plan on miss | ~8 files, ~6 internal members, ~30–50 tests incl. shape tests |
| **B1** | `SortedSet<T>.Builder`, `SortedDictionary<…>.Builder` (+ `partial` on owners, 2 files each) | `SortedBuilderBenchmarks` constants win over both baselines | ~6 files, **~30 public members** (all XML-documented; `CS1591` is an error), ~50–70 tests incl. model-based commands |
| **B2** | `Rope<T>.Builder`, `MeasuredRope<…>.Builder` (frozen-prefix) | `RopeBuilderBenchmarks` | ~5 files, **~19 public members**, ~40 tests |
| **C** | Deferred builders (deque, raw tree, bag, PQ, interval, indexed rope edits) | Adoption criteria + the per-builder evidence recorded above | per builder |
| **D** | Owner-token transients | See below; may never run | — |

## Adoption Criteria

A public builder ships only when at least one of these holds, evidenced by the named benchmark or a concrete
caller:

- benchmarks show lower allocation or elapsed time than *both* the immutable-operations baseline and the best
  caller-side workaround (collect + `CreateRange`; `StringBuilder` + `RopeBuilder`) for a documented workload;
- the builder exposes a construction workflow that is genuinely awkward or allocation-heavy through existing
  APIs (not merely different);
- the builder lets existing factory or merge APIs share an internal bulk-construction path they could not
  otherwise share.

Never for API symmetry. Every builder ships with snapshot-isolation, comparer/order-preservation,
caching/version, fail-fast enumeration, and freeze-invariant tests before becoming public.

## Owner-Token Transients: Status And Feasibility

Recorded for completeness; nothing above depends on it.

The transient tier would tag freshly built internal nodes with a builder identity, mutate in place on tag
match, path-copy otherwise, and freeze by clearing tags. Its hazards in this library are structural, not
incidental:

- adopted nodes (from `ToBuilder`) are shared and may be reachable from **unforced suspensions captured by
  previously published values**; the ownership discipline that keeps every such node outside the mutable
  region is definable but difficult to verify, and a single violation corrupts published snapshots through a
  suspension forced later;
- cached measures and cached sizes must be invalidated or recomputed on every in-place edit — the memoized
  measure boxes are currently write-once by design;
- freeze must be robust under repeated `ToImmutable()` interleaved with further mutation;
- the payoff exists only for the edit–snapshot loop, whose staging-builder gap is quantified in the workload
  table — and whose rope-family instance is already solved by the frozen-prefix design.

Phase D therefore runs only if `SnapshotBenchmarks` demonstrates a workload where the O(B·n) rebuild term is a
measured, user-reported bottleneck that neither the persistent operations nor the frozen-prefix pattern
serves — and it must be treated as potentially infeasible to verify, not merely expensive to build.

## Resolved Design Questions

Decisions previously listed as open, now committed (rationales inline above):

1. **Nested vs top-level builders** — nested `Builder` on `partial` owners; `RopeBuilder` unchanged as the
   text writer; three-role rule for character construction.
2. **Return conventions** — mutable-BCL-analogue rule; no member returns the builder type; `RopeBuilder`
   stays fluent under the same rule (its analogue, `StringBuilder`, is fluent).
3. **`ToImmutable` caching** — MUST return the cached reference when the version is unchanged; version
   advances iff the operation reports a structural change; no value-equality probing; one counter drives both
   caching and fail-fast enumeration.
4. **Interfaces and indexers** — read-only interfaces only; no mutable BCL interfaces; no indexer setters;
   `SetItem` is the replacement verb.
5. **Rank operations on sorted builders** — excluded from wave 1; query-the-snapshot is the documented
   pattern.
6. **Rope builder first slice** — append-only, six members (seven on the measured builder), frozen-prefix
   representation; indexed editing deferred until a workload demands it.
7. **Eager vs lazy measures on packer-built trees** — eager, realized by forcing `root.Measure` once after
   packing; post-construction persistent edits stay lazy as today.
8. **Empty freeze under a custom comparer** — `Create(comparer)` equivalence; singleton only for the
   reference-default comparer.
9. **`ToBuilder` cost honesty** — per-builder rows both directions; the "not the BCL transient model"
   statement; workload guidance table.
10. **Verb scheme** — one library-wide table; verbs from the closest mutable BCL analogue, preferring the
    immutable counterpart's verb where the operations correspond one-to-one (`Insert`, `Enqueue`,
    `AddFirst`/`AddLast`, `SetItem`); the append-only rope builders deliberately use `Add` (the
    `List<T>`/`ImmutableList<T>.Builder` verb) rather than `AddLast`, since they append in a single direction;
    `Append` reserved to the text `RopeBuilder`; no aliases.

## Revision Notes

This revision (2026-07-01) restructured the original proposal after two adversarial review rounds against the
source at the revision HEAD. The substantive corrections, so they are not relitigated:

- Split the work into unconditional internal machinery (A1), a benchmark-gated packer prototype with a
  null-result plan (A2), and benchmark-gated public builders (B), replacing the original six-phase plan that
  presented the packers as self-justifying and nine public builders as mostly "High" value.
- Corrected the sorted-builder claim from an implied asymptotic win to a constants/ergonomics win
  (pipeline-versus-pipeline), and attributed the O(n) freeze to a trivial trust-sorted entry point rather
  than to the packers.
- Stated the edit–snapshot-loop totals (O(E·log n + B·n) staging vs O(E·log n) persistent) and the ~n/log n
  edits-per-snapshot crossover; added the missing dirty-freeze and `ToBuilder` complexity rows (sorted
  `ToBuilder` is O(n log n) with BCL staging, not O(n)).
- Adopted the frozen-prefix rope builder, making rope `ToBuilder` O(1) and rope snapshots O(k + log n).
- Fixed contract defects found in review: comparer propagation through freeze; empty-freeze identity under
  custom comparers; monoid-measure seeding (`TMeasureOps.Empty`) and the measure/indexer conflict (resolved by
  shipping measured builders append-only); eager `FreezeChunks` with materialized results and exact-length
  transferred chunks; capacity/overflow contracts; fail-fast enumeration unified with the version counter;
  caching promoted from MAY to MUST; the settable-indexer-on-read-only-interface and fluent-vs-void
  inconsistencies (resolved by the interface and return-convention rules); the conflicting duplicate
  `Rope<T>.Builder` sketches (replaced by one definitive slice).
- Recorded implementability facts the sketches previously hid: the polymorphically recursive packer worker
  and its per-level allocation profile; the r ≠ 1 shape rule with the n = 0–10 table; the required internal
  `MeasuredChunk` measure constructor and `FingerTree.WrapRoot` factory; the `Debug.Assert`-only guard gap;
  the `PriorityQueue` enumeration-order contract dependency; and the test-coverage drain in suspension-racing
  fixtures under a packer-routed `CreateRange`.
