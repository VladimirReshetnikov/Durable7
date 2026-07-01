# Proposal: Mutable Builders For FingerTree Collections

- Status: Proposed
- Created (UTC): 2026-07-01T18:14:04Z
- Repository HEAD: 8a0104880d57bcf77daeff761b74f352b14ce906
- Audience: Maintainers designing bulk construction and transient mutation APIs for the C# FingerTree workspace
- Scope: Builder applicability, API shape, internal implementation strategy, validation, and rollout order

## Summary

Mutable builders are applicable to this library, but the useful form is not the same for every structure.

The recommended path is to start with **bulk construction builders**: mutable staging objects that collect edits
privately and then freeze to an immutable persistent value. These builders should reuse new internal bottom-up tree
packing helpers so the immutable result is built once, with no receiver mutation and no exposure of partially built
state.

A deeper `System.Collections.Immutable`-style **transient tree builder** that mutates uniquely owned internal nodes is
possible, but it should be treated as a later optimization. It interacts directly with this library's lazy memoized
middles, cached measures, persistence guarantees, and concurrent-read story. Those are exactly the properties that
make the current implementation valuable, so the first builder wave should avoid invalidating them.

The highest-value builder work is:

1. A generic `Rope<T>.Builder` and measured-rope builder.
2. Public builders for `SortedSet<T>`, `SortedBag<T>`, and `SortedDictionary<TKey, TValue>` that batch many updates
   and freeze to one order-statistic finger tree.
3. Internal bottom-up builders for the tuned deque and general measured tree, used by `CreateRange`, sorted merges,
   and public builders.
4. Only after benchmarks justify it: owner-token transient internals for repeated localized edits.

## Motivation

`System.Collections.Immutable` builders exist because repeatedly producing immutable versions can allocate a fresh
path for every mutation. A builder lets callers perform many related changes while there is still a single owner,
then publish an immutable snapshot at the boundary.

This library has the same use cases:

- build a large rope or text buffer one append at a time;
- import many sorted keys, intervals, or priority entries;
- apply a batch of set/dictionary mutations before publishing a new immutable snapshot;
- construct intermediate results for set algebra, merge, filtering, or range extraction;
- avoid repeated boundary-chunk copies in ropes;
- reduce constant factors in `CreateRange` paths that currently append into a persistent tree.

There is already a specialized `RopeBuilder` for text. It stages characters in `StringBuilder` and materializes a
`Rope<char>` or measured text rope in one pass. That is evidence that the builder idea fits the domain; the proposal
below generalizes it and routes more of the library through shared internal construction machinery.

## Non-Goals

The first builder wave should not:

- make immutable instances mutable internally after publication;
- expose mutable spans or arrays from a builder that can later be observed through an immutable rope;
- weaken concurrent-read guarantees of published immutable values;
- require public collection types to implement mutable BCL interfaces if those interfaces would imply unsupported
  semantics or complexity;
- promise `O(1)` freeze after arbitrary builder edits unless the implementation actually preserves a frozen tree
  incrementally.

## Builder Taxonomy

### Bulk Construction Builder

A bulk construction builder stores mutable staging data outside the immutable tree, for example in arrays, lists, or
BCL mutable sorted containers. `ToImmutable()` builds a fresh immutable structure from that staging data.

This form is simple and safe:

- Immutable values never share mutable nodes with the builder.
- Publication safety is the same as ordinary `CreateRange`.
- `ToImmutable()` is usually `O(n)`, or `O(n log n)` when sorting is required.
- Continuing to mutate the builder after `ToImmutable()` cannot change the produced snapshot.

This is the right default.

### Incremental Freezing Builder

An incremental freezing builder caches the last produced immutable value and a dirty flag. If no mutations occurred
after the last `ToImmutable()`, it returns the cached value. After mutations, it rebuilds from staging data.

This is a small extension of bulk construction and is still safe. It is useful for APIs modeled after
`ImmutableArray<T>.Builder` and `ImmutableSortedSet<T>.Builder`, where callers may ask for a snapshot more than once.

### Owner-Token Transient Tree Builder

An owner-token builder stores mutable internal tree nodes tagged with a builder identity. Edits mutate a node in
place only when the tag matches; otherwise they path-copy. `ToImmutable()` freezes reachable nodes by clearing the
owner or converting to immutable node types.

This can make repeated local edits faster, but it is the riskiest approach here:

- lazy middle suspensions must not point at mutable future state;
- cached measures must be invalidated or updated correctly after every in-place change;
- no immutable value may observe a mutable node after publication;
- concurrent readers of old immutable values must remain lock-free and deterministic;
- freeze must be robust even if `ToImmutable()` is called repeatedly while the builder continues mutating.

This should be investigated only after bulk builders and bottom-up construction are in place and benchmarked.

## Applicability Matrix

| Structure | Builder usefulness | Recommended builder kind | Notes |
| --- | --- | --- | --- |
| `Rope<T>` | High | Bulk chunk builder | Avoids repeated boundary-chunk copies and builds owned chunks directly. |
| `MeasuredRope<T, TMeasure, TMeasureOps>` | High | Bulk chunk builder with cached per-chunk measures | Same as `Rope<T>`, plus measure accumulation while filling chunks. |
| `RopeBuilder` / text helpers | High | Keep as text facade over generic rope builders | Existing char builder can remain ergonomic while sharing generic machinery. |
| `SortedSet<T>` | High | Mutable sorted staging plus freeze | Best for many add/remove operations followed by one snapshot. |
| `SortedBag<T>` | High | Mutable multiset staging plus freeze | Must preserve duplicate multiplicities and documented equal-element stability. |
| `SortedDictionary<TKey, TValue>` | High | Mutable map staging plus freeze | Last-wins semantics naturally match mutable assignment. |
| `IntervalTree<T>` | Medium-high | Mutable interval list or sorted staging plus freeze | Strong for bulk load and coalesce/edit batches. |
| `PriorityQueue<TElement, TPriority>` | Medium-high | Append-only entry staging plus freeze | Useful for bulk enqueue and meld preparation; stable order must be preserved. |
| `FingerTreeDeque<T>` | Medium | Bottom-up sequence builder | Existing append construction is already amortized linear; builder mainly lowers constants and allocations. |
| `FingerTree<TElement, TMeasure, TMeasureOps>` | Medium | Bottom-up measured sequence builder | Useful as internal machinery and for raw measured-tree users. |
| `ReversibleDeque<T>` | Low-medium | Bulk sequence builder only | Reversal is already cheap; strict core makes transient internals possible but not urgent. |

## Proposed Public API

The API sketches below describe the public surface, not the source layout. If a builder is implemented in a separate
file, the owning collection type may need to become `partial`; if the builder lives beside the existing collection
implementation, no `partial` modifier is required. The important contract is the nested builder type and the
`ToBuilder()` / `CreateBuilder()` entry points.

### Generic Rope Builder

Prefer a nested builder to avoid colliding with the existing text-specific `RopeBuilder` name:

```csharp
public sealed class Rope<T>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyList<T>
    {
        public int Count { get; }
        public T this[int index] { get; set; }

        public Builder Append(T item);
        public Builder AppendRange(ReadOnlySpan<T> items);
        public Builder AppendRange(IEnumerable<T> items);
        public Builder Insert(int index, T item);
        public Builder InsertRange(int index, ReadOnlySpan<T> items);
        public Builder RemoveAt(int index);
        public Builder RemoveRange(int index, int count);
        public Builder Clear();

        public Rope<T> ToImmutable();
    }
}
```

The first implementation can optimize append-heavy workloads and implement indexed edits by materializing the
staged chunks into a mutable list. It does not need to match every immutable operation's asymptotic bound inside the
builder. The contract should say that the builder is for private, single-threaded mutation and snapshot production.
`Append` / `AppendRange` should be the primary construction verbs. `Add` aliases are optional convenience members,
not a requirement, because `IReadOnlyList<T>` intentionally does not promise mutable collection semantics.

An even smaller first slice is append-only:

```csharp
public sealed class Rope<T>.Builder
{
    public int Count { get; }
    public Builder Append(T item);
    public Builder AppendRange(ReadOnlySpan<T> items);
    public Builder AppendRange(IEnumerable<T> items);
    public Builder Clear();
    public Rope<T> ToImmutable();
}
```

That append-only version already solves the highest-value rope construction case and can grow later.

### Measured Rope Builder

Measured ropes need the same shape, with measure-aware freeze:

```csharp
public sealed class MeasuredRope<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyList<T>
    {
        public int Count { get; }
        public TMeasure Measure { get; }
        public T this[int index] { get; set; }

        public Builder Append(T item);
        public Builder AppendRange(ReadOnlySpan<T> items);
        public Builder AppendRange(IEnumerable<T> items);
        public Builder Clear();

        public MeasuredRope<T, TMeasure, TMeasureOps> ToImmutable();
    }
}
```

For append-only staging, the builder can maintain a current mutable chunk and its current measure. On freeze, each
chunk is copied or retired into immutable owned storage and wrapped in `MeasuredChunk`.

### Text Rope Builder

The current `RopeBuilder` should remain as the friendly text API. Internally it can either continue using
`StringBuilder` or delegate to `Rope<char>.Builder`.

The likely best split is:

- keep `RopeBuilder` for text-specific overloads (`Append(Rune)`, `AppendLine`, `ToTextRope`);
- add `Rope<char>.Builder` for generic sequence-style char construction;
- have `RopeBuilder.ToRope()` and `ToTextRope()` use the new chunk builder once it exists, avoiding an extra
  full-size intermediate array when practical.

### Sorted Set Builder

```csharp
public sealed class SortedSet<T>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder(IComparer<T>? comparer = null);

    public sealed class Builder : IReadOnlyCollection<T>
    {
        public int Count { get; }
        public IComparer<T> Comparer { get; }
        public T Min { get; }
        public T Max { get; }

        public bool Contains(T item);
        public bool Add(T item);
        public bool Remove(T item);
        public void UnionWith(IEnumerable<T> items);
        public void IntersectWith(IEnumerable<T> items);
        public void ExceptWith(IEnumerable<T> items);
        public void SymmetricExceptWith(IEnumerable<T> items);
        public void Clear();

        public SortedSet<T> ToImmutable();
    }
}
```

Implementation options:

- Stage in `System.Collections.Generic.SortedSet<T>` with the same comparer, then freeze by enumerating it into the
  order-statistic finger tree.
- Cache the last immutable result and a version number so repeated `ToImmutable()` calls with no intervening changes
  are `O(1)`.
- Rebuild on every dirty freeze; do not attempt node-level mutation in the first version.

The BCL staging option preserves one representative for each comparer-equal group. `Add` should keep the existing
representative on duplicate keys, matching `SortedSet<T>.Add`; a remove followed by add may choose a new
representative because that is the mutation sequence the caller requested.

This builder is especially attractive because current `AddRange` performs `m` independent persistent `Add`s, while
the builder can batch into mutable sorted staging and build once.

### Sorted Bag Builder

`SortedBag<T>` needs multiplicity and stability. A BCL `SortedSet<T>` alone is insufficient.

Possible staging representations:

1. `List<T>` plus dirty sorted snapshot. Best for append-heavy then freeze.
2. `SortedDictionary<T, Run>` keyed by comparer order, where each run stores the actual comparer-equal values in
   insertion order.
3. `SortedDictionary<T, int>` when only values matter and equal-element identity does not matter.

Because `SortedBag<T>` currently documents that equal elements keep insertion order, option 2 is the safest general
form:

```csharp
public sealed class SortedBag<T>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder(IComparer<T>? comparer = null);

    public sealed class Builder : IReadOnlyCollection<T>
    {
        public int Count { get; }
        public IComparer<T> Comparer { get; }

        public void Add(T item);
        public void AddRange(IEnumerable<T> items);
        public bool Remove(T item);
        public int RemoveAll(T item);
        public int CountOf(T item);
        public bool Contains(T item);
        public void Clear();

        public SortedBag<T> ToImmutable();
    }
}
```

Freeze enumerates comparer-key runs in order, preserving each run's value order, then builds the measured tree once.
The implementation should model those runs explicitly rather than treating the dictionary key object as the whole
value. A practical `Run` stores the current representative plus the actual comparer-equal values. `CountOf` returns
the run length, `Remove` removes one value from the run, and `RemoveAll` removes the whole run. If the first value in
a run is removed, the next surviving value becomes the representative before the next freeze.

### Sorted Dictionary Builder

```csharp
public sealed class SortedDictionary<TKey, TValue>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder(IComparer<TKey>? comparer = null);

    public sealed class Builder : IReadOnlyDictionary<TKey, TValue>
    {
        public int Count { get; }
        public IComparer<TKey> Comparer { get; }
        public TValue this[TKey key] { get; set; }

        public void SetItem(TKey key, TValue value);
        public void Add(TKey key, TValue value);
        public bool TryAdd(TKey key, TValue value);
        public bool Remove(TKey key);
        public void Clear();

        public SortedDictionary<TKey, TValue> ToImmutable();
    }
}
```

The natural staging type is `System.Collections.Generic.SortedDictionary<TKey, TValue>` with the same comparer. Freeze
enumerates entries in key order and builds the finger-tree map once. This matches the immutable type's last-wins
`CreateRange` semantics when the builder uses assignment.

### Priority Queue Builder

The priority queue is insertion-order stable among equal priorities. A builder should preserve a monotonically
increasing sequence number internally, even if the immutable tree stores only `(element, priority)` entries in
sequence order.

```csharp
public sealed class PriorityQueue<TElement, TPriority>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyCollection<(TElement Element, TPriority Priority)>
    {
        public int Count { get; }

        public Builder Enqueue(TElement element, TPriority priority);
        public Builder EnqueueRange(IEnumerable<(TElement Element, TPriority Priority)> items);
        public bool TryPeek(out TElement element, out TPriority priority);
        public bool TryDequeue(out TElement element, out TPriority priority);
        public void Clear();

        public PriorityQueue<TElement, TPriority> ToImmutable();
    }
}
```

The first implementation can stage in a list and freeze by appending in insertion order. If builder-side dequeue is
important, use a mutable heap for operations and retain a separate insertion-order list or deleted markers for
freezing. `ToImmutable()` must enumerate surviving entries in original enqueue order, not heap order; otherwise
equal-priority FIFO stability is lost. The builder's own enumeration should follow the immutable queue enumeration
contract, which is insertion order today but not priority order. That extra heap-plus-log complexity should be
benchmark-driven.

### Interval Tree Builder

```csharp
public sealed class IntervalTree<T>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyCollection<Interval<T>>
    {
        public int Count { get; }

        public Builder Add(Interval<T> interval);
        public bool Remove(Interval<T> interval);
        public bool Contains(Interval<T> interval);
        public void CoalesceInPlace();
        public void Clear();

        public IntervalTree<T> ToImmutable();
    }
}
```

Bulk load can stage in a list and freeze by sorting by low endpoint, preserving duplicate/value-equal semantics as
the immutable `CreateRange` path requires. If builder-side overlap queries are needed, the builder can lazily freeze
to an internal immutable snapshot for query operations and mark it dirty on mutation.

### Deque And Raw Measured Tree Builders

The deque and raw measured tree should get builders after internal bottom-up construction helpers exist:

```csharp
public sealed class FingerTreeDeque<T>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyList<T>
    {
        public int Count { get; }
        public T this[int index] { get; set; }

        public Builder AddFirst(T item);
        public Builder AddLast(T item);
        public Builder AddRange(IEnumerable<T> items);
        public Builder InsertRange(int index, IEnumerable<T> items);
        public Builder RemoveRange(int index, int count);
        public FingerTreeDeque<T> ToImmutable();
    }
}
```

For the first version, this can be an array/list-backed builder whose freeze calls a bottom-up tree packer. This is
simple, safe, and still useful for many edits before publication. A true transient finger-tree deque can wait.

For the raw measured tree:

```csharp
public sealed class FingerTree<TElement, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<TElement, TMeasure>
{
    public Builder ToBuilder();
    public static Builder CreateBuilder();

    public sealed class Builder : IReadOnlyCollection<TElement>
    {
        public int Count { get; }
        public TMeasure Measure { get; }

        public Builder Append(TElement item);
        public Builder AppendRange(IEnumerable<TElement> items);
        public Builder Clear();
        public FingerTree<TElement, TMeasure, TMeasureOps> ToImmutable();
    }
}
```

This should initially be append-oriented. Arbitrary index edits belong on `Rope<T>` / `FingerTreeDeque<T>` unless a
raw measured-tree use case proves otherwise.

## Internal Construction Helpers

The most important enabling work is not public API; it is reusable internal builders that construct tree shapes
bottom-up.

### Sequence Packer

Add an internal helper for the tuned deque:

```csharp
internal static class TreeBulkBuilder
{
    public static Tree<T, Leaf<T>> Build<T>(ReadOnlySpan<T> items);
    public static Tree<T, Leaf<T>> Build<T>(IEnumerable<T> items);
}
```

Expected behavior:

- consume input once;
- for `IEnumerable<T>` inputs with no cheap count, collect into a `List<T>` or pooled buffer before packing rather
  than mixing enumeration with deep-tree construction state;
- group leaves into digits and nodes with valid invariants;
- use computed middle trees, not suspended pending repairs, because bulk construction has no deferred endpoint
  operation to amortize;
- compute cached sizes and rightmost signposts exactly once;
- avoid repeated persistent `Snoc` allocation where possible.

Do not overfit this helper to one supposedly perfect balanced shape. The required properties are valid digit/node
invariants, preserved sequence order, correct cached metadata, and competitive allocation behavior against repeated
append construction.

This helper can replace `FingerTreeDeque<T>.BuildTree`, support `CreateRange`, and build results of sorted merges.

### General Measured Packer

Add a parallel helper for the general measured tree:

```csharp
internal static class MeasuredTreeBulkBuilder
{
    public static MeasuredTree<TElement, MeasuredLeaf<TElement, TMeasure, TMeasureOps>, TMeasure, TMeasureOps>
        Build<TElement, TMeasure, TMeasureOps>(ReadOnlySpan<TElement> items)
        where TMeasureOps : IMeasure<TElement, TMeasure>;
}
```

Expected behavior:

- wrap each element in `MeasuredLeaf`;
- group leaves and nodes in two-or-three-child nodes and one-through-four digits;
- compute node measures once;
- create deep middles as already-computed middle cells;
- preserve the existing lazy measure memoization of deep nodes where that is still desirable.

This helper would serve raw measured trees, sorted collections, priority queues, interval trees, and ropes whose
chunk sequence is already known.

### Chunk Builders

For ropes, add reusable chunk staging:

```csharp
internal sealed class ChunkBuilder<T>
{
    public int Count { get; }
    public void Append(T item);
    public void Append(ReadOnlySpan<T> items);
    public IEnumerable<Chunk<T>> FreezeChunks();
}
```

Measured ropes need either a second builder or a generic measure-aware option:

```csharp
internal sealed class MeasuredChunkBuilder<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    public int Count { get; }
    public TMeasure Measure { get; }
    public void Append(T item);
    public void Append(ReadOnlySpan<T> items);
    public IEnumerable<MeasuredChunk<T, TMeasure, TMeasureOps>> FreezeChunks();
}
```

The builder owns mutable arrays. `FreezeChunks()` must either copy a chunk or transfer ownership of a fully retired
array to an immutable chunk. After ownership is transferred, the builder must never write to that array again; later
mutations allocate new mutable storage. A final partially filled chunk should usually be copied to exact length unless
it is retired and no longer reachable from the builder.

## Complexity And Allocation Expectations

The first builder wave should make these claims:

| Operation | Expected complexity | Notes |
| --- | ---: | --- |
| Append into rope builder | Amortized O(1) | Fills mutable chunks; no persistent tree edit per element. |
| Freeze rope builder | O(n) | Copies chunks or transfers retired chunk ownership, then builds chunk tree. |
| Sorted builder add/remove | O(log n) or staging-dependent | Depends on BCL sorted staging. |
| Sorted builder freeze | O(n) if already sorted, O(n log n) if list-staged | Builds measured tree once. |
| Deque/raw sequence builder append | Amortized O(1) | List-backed initially. |
| Deque/raw sequence builder freeze | O(n) | Bottom-up pack. |
| Repeated `ToImmutable()` with no mutation | O(1) | Return cached snapshot. |

Important: builders improve constants and batch behavior, but they should not obscure the immutable types' persistent
operation contracts. Documentation should phrase builder complexity separately from immutable operation complexity.

## Builder Semantic Contract

A builder is a mutable workspace, not a persistent value. Intermediate builder states are not preserved unless the
caller explicitly asks for an immutable snapshot with `ToImmutable()`.

Every public builder should document these rules:

- mutations after `ToImmutable()` do not affect any previously produced immutable value;
- repeated `ToImmutable()` with no intervening mutation may return a cached immutable instance;
- any mutation invalidates the cached snapshot and advances the builder version;
- enumeration and indexed reads observe the builder's current mutable state, with invalidation behavior documented
  like ordinary mutable collections;
- builders are intended for single-owner use and should not be passed between threads while they are being mutated.

## Snapshot And Thread-Safety Contract

Builders should be explicitly single-threaded mutable objects. They do not need to be safe for concurrent mutation or
concurrent read while mutating.

Produced immutable values must keep the existing contract:

- immutable snapshots are safe for concurrent reads;
- mutating a builder after `ToImmutable()` never changes any previously produced immutable value;
- builder staging memory is not exposed through immutable chunks unless it has been copied or ownership has been
  transferred away from the builder;
- comparer and element-object caveats remain the same as the immutable collections: mutable reference elements can
  still be mutated by the caller, and comparer order must remain stable for sorted structures.

## Interaction With Lazy Memoized Middles

Bulk builders should usually produce computed middle trees rather than pending-operation suspensions. The purpose of
a suspension is to defer and share the repair work of a persistent endpoint edit. Bulk construction already owns the
whole shape and can build it directly.

That means:

- endpoint operations on the resulting immutable value keep their existing behavior;
- the built tree may have fewer initial suspended repairs than a tree produced by repeated appends;
- tests that depend on lazy repair behavior should continue to build histories through endpoint operations, not only
  through builders;
- benchmark comparisons should separate "bulk-built tree" from "append-history tree" when measuring first-force
  behavior.

For the general measured tree, deep node measures may remain lazily memoized. The builder can compute node measures
for grouping nodes but leave deep-tree aggregate measures lazy, preserving current allocation and concurrency design.

## Validation Plan

### Unit Tests

For every builder:

- empty builder freezes to the canonical empty value when applicable;
- builder add/remove/edit operations match the immutable operation model;
- `ToImmutable()` snapshots are not affected by subsequent builder mutation;
- `ToBuilder()` round-trips all elements, comparer/order, and measures;
- repeated `ToImmutable()` with no mutation returns an equal value and, if caching is specified, the same instance;
- invalid arguments match existing immutable APIs.

### Invariant Tests

Use existing `ValidateInvariants()` hooks after every builder freeze:

- deque digit/node sizes and cached counts;
- measured tree cached measures;
- rope chunk length and maximum-size constraints;
- measured-rope chunk measures;
- sorted set/bag/dictionary order and uniqueness/multiplicity;
- interval-tree low-order and maximum-high annotations.

### Property And Model-Based Tests

Extend model-based command tests with builder commands:

- `ToBuilder`;
- builder mutation commands;
- `ToImmutable`;
- continue mutating builder after snapshot;
- switch back to immutable operations.

The failure shrinker should produce readable scripts such as:

```text
ToBuilder; Add 3; Snapshot; Remove 3; AssertSnapshotContains 3
```

### Concurrency Tests

Builders themselves do not need concurrent mutation tests. They do need publication tests:

- freeze a builder to an immutable value;
- publish that immutable value through `Volatile.Write` or `Interlocked.Exchange`;
- read concurrently from many threads;
- continue mutating the builder privately and verify readers of the old snapshot remain stable.

### Benchmarks

Add benchmark families:

- `RopeBuilderBenchmarks`: append one element at a time, append spans, freeze, compare with repeated `AddLast`,
  `Rope.Create`, `StringBuilder`, and `ImmutableList<T>.Builder` where meaningful.
- `SortedBuilderBenchmarks`: batch `Add`/`Remove` plus freeze, compare with repeated immutable updates and
  `ImmutableSortedSet<T>.Builder` / `ImmutableSortedDictionary<TKey,TValue>.Builder`.
- `BulkBuildBenchmarks`: current `CreateRange` repeated append versus bottom-up packer.
- `SnapshotBenchmarks`: repeated `ToImmutable()` with and without intervening mutations.

Metrics should include allocations as well as time. Timing-only wins are not enough if allocation behavior regresses
snapshot workloads.

## Rollout Plan

### Phase 1: Internal Bottom-Up Builders

Implement construction machinery in two slices:

1. chunk staging for ropes, because it can ship behind `RopeBuilder` and `Rope<T>.Builder` without changing the raw
   tree packers;
2. `TreeBulkBuilder` and `MeasuredTreeBulkBuilder`, once the benchmark harness can compare tree shapes and allocation
   behavior.

Route the packers through:

- `FingerTreeDeque<T>.CreateRange`;
- `FingerTree<TElement, TMeasure, TMeasureOps>.CreateRange`;
- sorted collection merge/build internals where inputs are already sorted;
- priority queue and interval tree bulk creation;
- rope chunk-tree creation.

This phase has no public API risk and should produce immediate allocation wins.

### Phase 2: Generic Rope Builders

Add append-oriented `Rope<T>.Builder` and `MeasuredRope<T, TMeasure, TMeasureOps>.Builder`. Keep indexed mutable
editing out of the first version unless a concrete scenario needs it.

Update `RopeBuilder` to reuse the generic chunk-freeze machinery where it improves allocation.

### Phase 3: Sorted Builders

Add `ToBuilder()` / `CreateBuilder()` for:

- `SortedSet<T>`;
- `SortedBag<T>`;
- `SortedDictionary<TKey, TValue>`.

Use BCL mutable sorted staging first. Freeze to the order-statistic measured tree using the internal measured packer.

### Phase 4: Domain Builders

Add builders for:

- `PriorityQueue<TElement, TPriority>`;
- `IntervalTree<T>`.

These can initially be staging-list builders with efficient freeze. Builder-side query operations can use a cached
immutable snapshot when dirty-state complexity would otherwise grow too large.

### Phase 5: Deque And Raw Measured Builders

Add list-backed builders for `FingerTreeDeque<T>` and append-oriented builders for raw
`FingerTree<TElement, TMeasure, TMeasureOps>`. These primarily expose the internal packers publicly.

### Phase 6: Evaluate Transient Internals

Only after the earlier phases and benchmarks:

- identify workloads where rebuilding-on-freeze is too expensive;
- prototype owner-token mutable nodes in a separate namespace or branch;
- prove snapshot isolation and measure invalidation;
- run the full property/model/concurrency suite plus targeted allocation tests.

This phase should be optional, not assumed.

## Adoption Criteria

Add a public builder when at least one of these is true:

- benchmarks show lower allocation or elapsed time for a documented bulk-edit workload;
- the builder exposes a common construction workflow that is awkward or allocation-heavy with only immutable
  operations;
- the builder lets existing factory or merge APIs share an internal bulk-construction path.

Do not add a builder solely for API symmetry. Each builder should have tests for snapshot isolation, comparer/order
preservation, and freeze invariants before it becomes public.

## Open Questions

- Should public builders be nested types (`Rope<T>.Builder`) or top-level types (`RopeBuilder<T>`)? Nested types avoid
  naming conflicts with the existing text `RopeBuilder`, but top-level names are sometimes easier to discover.
- Should `ToImmutable()` return a cached exact instance after no-op mutation sequences, or only after no mutation at
  all since the previous freeze?
- Should sorted builders expose rank/index operations, or keep only mutation plus enumeration at first?
- Should `Rope<T>.Builder` support efficient random insertion/removal initially, or remain append-oriented until an
  editor workload asks for mutable random editing?
- Should builder freeze build deep-node aggregate measures eagerly to reduce first-read cost, or preserve lazy measure
  memoization exactly as ordinary construction does?

## Recommendation

Proceed, but with discipline:

1. Build internal bottom-up packers first.
2. Ship append-oriented rope builders first; they are the clearest win.
3. Ship sorted/map/bag builders using mutable staging and freeze-to-tree.
4. Keep builders explicitly single-threaded and snapshot-producing.
5. Defer true transient tree mutation until benchmarks show a real need.

This gives the library most of the practical value of `System.Collections.Immutable` builders while preserving the
clean immutable/lazy core that the current design depends on.
