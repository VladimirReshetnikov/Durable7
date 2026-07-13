# Axis 2 Lifecycle And Sequence-Cursor Plan

- Created (UTC): 2026-07-13T03:22:20Z
- Repository HEAD: b600a208395fd16e73b620cfda4eca20d372bd75
- Audience: Maintainers designing the next C# persistent-collection wave
- Scope: Detailed C#-first plan for owner-token transients, persistent collections,
  read-optimized frozen collections, and version-bound rope cursors

## Decision

The active Axis 2 work is two independent C#-first tracks:

1. an explicit **transient -> persistent -> frozen** lifecycle, beginning with CHAMP map/set; and
2. a **version-bound gap cursor** over `Rope<T>`, followed by `MeasuredRope` and only then any
   deque/general-measured surface.

Automatic size-tier switching and key-type dispatch are intentionally postponed. The first frozen
hash implementation uses one general layout for every count and key type. The first cursor does not
depend on the planned range-update sequence. Neither track is a commitment to port a public surface
until the C# contracts and measurements stabilize.

This document refines the survey-level entries in the
[frontier structure catalog](../reference/frontier-structure-catalog.md) and supersedes the API and
complexity sketch in item A3 of the
[2026-07-09 proposal](new-data-structures-2026-07-09.md). It does not describe shipped APIs.

## Why The Vocabulary Must Be Exact

The repository already has several mutation-shaped construction paths, but they are not one
lifecycle mechanism:

| Existing mechanism | What it is | What it is not |
| --- | --- | --- |
| C# HAMT `BulkBuilder` | Internal list/bucket staging followed by canonical CHAMP construction | An owner-token transient; `ToImmutable` rebuilds |
| Sorted set/dictionary builders | Public BCL-backed staging with reusable snapshots | O(1) persistent-node adoption or publication |
| Rope/RRB builders | Append-oriented frozen-prefix builders with cheap incremental snapshots | Arbitrary local editing over adopted tree nodes |
| C# and Kotlin Ctrie | Concurrent mutable ingestion with an O(1) generation snapshot | A single-owner transient or a terminal frozen lookup layout |
| Persistent collections | Immutable, branchable, structurally shared versions | Read-only layouts selected for one terminal hot version |

The [mutable-builder proposal](../../src/CSharp/docs/FingerTree/mutable-builder-proposal.md) already
records most of these distinctions. Axis 2 gives the missing states distinct names:

- **Builder**: mutable staging outside the persistent node graph, optimized for construction or a
  restricted pattern such as append. A builder may publish reusable snapshots.
- **Transient**: single-owner mutable access to persistent-capable nodes under an edit token. The
  first C# contract is one-way: publication consumes the transient.
- **Persistent**: immutable structural sharing optimized for branching version histories.
- **Frozen**: a separate immutable type optimized for repeated reads of one version. It has no
  persistent update surface.
- **Snapshot view**: the Ctrie's immutable view of one concurrent generation. It is neither a
  canonical CHAMP nor a frozen lookup layout.

“Frozen prefix,” “frozen Ctrie generation,” and “frozen collection” remain three unrelated terms.
Documentation must qualify the first two and reserve an unqualified type name such as
`FrozenHashMap` for the read-optimized terminal tier.

## Lifecycle State Machine

```text
                    O(1) adopt                  O(1) consume/publish
PersistentHashMap --------------> TransientHashMap -----------------> PersistentHashMap
         |                              |
         | O(n) Freeze                  | no direct Freeze in v1
         v                              v
 FrozenHashMap <---------------- PersistentHashMap
         |
         | O(n) ToPersistent
         v
 PersistentHashMap

staging Builder -- family-specific construction/publication cost --> Persistent

Ctrie -- O(1) Snapshot --> SnapshotView -- O(n) --> Persistent or Frozen
```

The cycle is asymmetric on purpose:

- persistent -> transient and transient -> persistent can be O(1) because the nodes are designed
  for token-governed copy-on-write;
- persistent -> frozen and frozen -> persistent are O(n) representation changes;
- frozen -> transient therefore routes through persistent and is O(n);
- v1 does not add `Transient.Freeze()`: the persistent phase boundary stays visible;
- `Freeze()` on a frozen value, if exposed, returns the same object.

Builders remain useful. They are construction APIs with family-specific costs, not aliases for the
transient state.

## Track L: CHAMP Transient -> Persistent -> Frozen

### L0. Contract lock

Before node changes, add benchmark skeletons and executable contract tests for the current
`PersistentHashMap` and `PersistentHashSet`. The tests pin comparer identity, first stored key
representative, collision behavior, null handling, stable enumeration, no-op identity, retained old
versions, and callback exception behavior. These become the oracle for both conversions.

### L1. One-way C# CHAMP transient

The initial surface is intentionally smaller than a reusable BCL-style builder:

```csharp
public sealed partial class PersistentHashMap<TKey, TValue>
{
    public static Transient CreateTransient(
        IEqualityComparer<TKey>? comparer = null);

    public Transient ToTransient();               // O(1)

    public sealed class Transient : IReadOnlyDictionary<TKey, TValue>
    {
        public int Count { get; }
        public IEqualityComparer<TKey> Comparer { get; }
        public TValue this[TKey key] { get; }
        public IEnumerable<TKey> Keys { get; }
        public IEnumerable<TValue> Values { get; }

        public bool ContainsKey(TKey key);
        public bool TryGetValue(TKey key, out TValue value);
        public bool TryGetKey(TKey equalKey, out TKey actualKey);

        public void Add(TKey key, TValue value);
        public bool TryAdd(TKey key, TValue value);
        public void SetItem(TKey key, TValue value);
        public bool Remove(TKey key);
        public void Clear();

        public PersistentHashMap<TKey, TValue> Persist(); // O(1), consumes this object
    }
}
```

`PersistentHashSet<T>.Transient` is a thin map-transient facade with mutable-set verbs and the same
publication contract.

`Persist()` atomically changes the transient to an inactive state. Every subsequent read,
mutation, enumeration request, or second publication throws `ObjectDisposedException`. The C# type
system cannot consume an object statically, so runtime invalidation makes the one-way transition
unambiguous. A future reusable owner-token builder would be a separate API with repeated-snapshot
semantics; v1 does not blur those two models.

The transient is single-owner and unsynchronized. Sequential ownership transfer between threads is
permitted only under caller-provided synchronization; concurrent access is unsupported. Enumerator
mutation checks are fail-fast through one version counter.

### Edit-token mechanics

Each mutable-capable CHAMP node carries an optional owner token. The token has a small active/sealed
state and no back-reference to the transient.

1. `ToTransient()` creates a transient over the persistent root and a fresh active token. It does
   not traverse the graph.
2. A mutation may change a node in place only when the node's token is the active token.
3. An unowned node or a node owned by another/sealed token is copied along the edited path; the copy
   receives the active token.
4. Newly allocated payload and child arrays are private to the token. The implementation performs
   all failure-prone hashing, equality, allocation, and callback work before publishing an array
   reference into an owned node.
5. `Persist()` seals the token with an interlocked/volatile transition, constructs the immutable
   wrapper, invalidates the transient, and returns. It never clears tags by traversing the graph.
6. A later transient from the published root gets a different token and therefore path-copies on
   its first write.

The desired bounds are:

| Operation | Contract target |
| --- | --- |
| Persistent -> transient | O(1), one transient/token allocation |
| Lookup in transient | Same trie depth as persistent lookup |
| Edit already-owned path | O(w + c), with `w` trie depth and `c` collision work |
| First edit of shared path | O(w + c), copying only that path |
| Transient -> persistent | O(1), no graph walk |
| Persistent base retained during edits | Unchanged and concurrently readable |

The token reference retained by edited nodes is a real memory cost. Retained-byte benchmarks decide
whether the O(1) transition is worth it; the implementation must not disguise that cost by calling
an O(n) tag-clearing pass “publication.”

### Transient semantic and failure contracts

The transient preserves the current CHAMP rules exactly:

- comparer identity survives every transition by reference;
- equivalent key replacement retains the first stored key representative;
- the last distinct value wins, while a semantically equal replacement is a logical no-op;
- stable trie-order enumeration is preserved;
- `Clear()` preserves comparer identity;
- default-policy empty results may use the canonical empty, while a custom comparer is retained;
- collision nodes remain canonical and branch contraction matches persistent deletion;
- a single-item edit has the strong exception guarantee;
- a range operation, if added later, is incrementally committed and documents the item at which a
  throwing source/callback leaves the transient;
- no-op edits do not advance the enumerator version or allocate a new path.

The validation suite must inject throwing hash/equality/value callbacks and allocation failures at
every publication boundary that can be made deterministic. In-place shifting followed by
failure-prone work is prohibited.

### L2. Frozen-layout bake-off

`System.Collections.Frozen` is a performance baseline, not the repository type's implementation or
contract. The BCL explicitly treats frozen collections as expensive-to-construct, read-optimized
snapshots. Axis 2 needs the same lifecycle boundary while retaining repository-specific comparer,
stored-key, enumeration, and conversion contracts.

The bake-off compares fixed general layouts only:

- packed entries plus a Robin-Hood open-addressed index;
- packed entries plus a simpler linear/quadratic-probe index; and
- a BCL `FrozenDictionary` adapter used only as a measurement/control implementation.

Counts and key datasets vary in the benchmarks, but v1 may not select a representation from count,
runtime key type, or comparer type. That restriction keeps the lifecycle experiment independent of
the postponed specialization tracks.

### L3. Fixed-layout frozen hash map/set

The provisional public surface is:

```csharp
public sealed class FrozenHashMap<TKey, TValue> :
    IReadOnlyDictionary<TKey, TValue>
{
    public static FrozenHashMap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? comparer = null);

    public int Count { get; }
    public IEqualityComparer<TKey> Comparer { get; }
    public TValue this[TKey key] { get; }
    public IEnumerable<TKey> Keys { get; }
    public IEnumerable<TValue> Values { get; }

    public bool ContainsKey(TKey key);
    public bool TryGetValue(TKey key, out TValue value);
    public bool TryGetKey(TKey equalKey, out TKey actualKey);

    public FrozenHashMap<TKey, TValue> Freeze();            // identity
    public PersistentHashMap<TKey, TValue> ToPersistent();  // O(n)
}

public sealed partial class PersistentHashMap<TKey, TValue>
{
    public FrozenHashMap<TKey, TValue> Freeze();             // O(n)
}
```

`FrozenHashSet<T>` mirrors the set surface and shares the internal frozen hash-table core.

Version 1 has one representation:

- a packed entry array in documented source-enumeration order;
- each entry stores the already computed 32-bit hash, stored key, and value;
- an offline-built integer slot table maps probes to packed-entry indexes;
- one benchmark-selected load factor applies to all nonempty instances;
- enumeration scans packed entries and therefore does not expose probe-table order;
- lookups allocate nothing and never mutate/memoize shared state.

`CreateRange` and `Freeze` preserve first stored key/last value behavior under the chosen comparer.
`ToPersistent` uses the canonical bulk-construction path and preserves comparer identity. Empty
singleton reuse is allowed because it is an identity policy, not adaptive layout selection.

The frozen surface has no `SetItem`, `Add`, `Remove`, builder, or update-shaped convenience. Editing
is explicitly expensive:

```csharp
var persistent = frozen.ToPersistent(); // O(n)
var transient = persistent.ToTransient();
```

### L4. Ctrie snapshot conversion

After the fixed frozen core ships, add `SnapshotView.Freeze()`, not `Freeze()` on the live Ctrie.
The receiver name must make clear that the concurrent structure continues to accept writes.
`SnapshotView.Freeze()` and `SnapshotView.ToPersistentHashMap()` each enumerate exactly the captured
generation once; later live-trie writes cannot affect either result.

### Lifecycle benchmark gates

`TransientLifecycleBenchmarks` covers counts 0, 1, 8, 32, 1K, and 100K, with:

- fresh construction;
- persistent `SetItem` loops;
- the current canonical bulk builder;
- transient construction;
- persistent -> transient with edit counts ranging from one to the full count;
- edit/publish cycles and retained-base branching;
- collision-heavy and custom-comparer datasets.

Record throughput, allocated bytes, copied/owned node counts, retained bytes, and publication
latency. Structural counters must demonstrate that adoption and `Persist()` do not traverse.

`FrozenLookupBenchmarks` records:

- positive, negative, and mixed hit ratios;
- enumeration throughput;
- construction cost and retained bytes per entry;
- thaw cost;
- random, colliding, null-capable, string, and custom-comparer datasets; and
- the read count at which construction cost breaks even.

Compare `PersistentHashMap`, mutable `Dictionary`, `ImmutableDictionary`, BCL
`FrozenDictionary`, and each fixed-layout prototype. A key-type dataset is a workload, not
permission to dispatch on key type.

### Lifecycle shipment gate

The transient ships only when it demonstrates all semantic rules, failure atomicity, base-version
isolation, post-publication invalidation, and O(1) adoption/publication counters. The frozen map/set
ships only with a documented lookup/enumeration/memory win in a named read-heavy regime, or a
separately documented semantic reason strong enough to justify a new public family.

## Track C: Version-Bound Rope Cursor/Zipper

### The complexity correction

A root-to-position path stack gives O(log n) initial descent, O(1) movement inside a chunk, and
amortized O(1) sequential movement. It does **not** make a canonical immutable `Rope<T>` appear in
O(1) after every edit. Rebuilding the changed root spine costs O(log n) work and allocation.

A `readonly struct` also does not make an arbitrary-depth context allocation-free. The path needs
heap-backed storage or persistent context nodes, and snapshot caching naturally needs reference
state. The public representation is therefore not locked until the C0 spike measures it.

The baseline contract treats the cursor/zipper itself as a persistent working version:

- movement and local edits return another cursor;
- old cursors remain valid and can be edited to create branches;
- `Snapshot()` materializes a canonical `Rope<T>` in O(log n) when dirty and may cache the result;
- repeated clean `Snapshot()` is O(1) and reference-identical;
- if a caller materializes a rope after every keystroke, the operation is still O(log n).

An optional hybrid can make the `Rope<T>` facade accept a focused root (left tree + bounded active
window + right tree) and canonicalize lazily. That design could return `(Rope, Cursor)` after local
edits in O(1) amortized time, but it broadens every rope operation's representation contract. It is
an experiment in C0, not a pre-written complexity claim.

### C0. Persistent-zipper and focused-root spike

Build test-only prototypes for both alternatives:

1. **zipper-as-version**: split state remains in the cursor; `Snapshot()` rebuilds a canonical root;
2. **focused Rope root**: `Rope<T>` internally holds either a canonical root or a focused root and
   memoizes canonicalization for nonlocal operations.

Instrument node visits, spine allocations, forced suspensions, focus-array copies, and retained
memory across long local-edit histories. The API/complexity review chooses one before public types
ship. Zipper-as-version is the conservative default because it does not tax unrelated rope users.

### Gap semantics

The cursor denotes a boundary in `0 .. Count`, not an element. At position `p`, elements
`[0, p)` are left of the gap and `[p, Count)` are right of it.

- `PeekPrevious` reads `p - 1`; `PeekNext` reads `p`.
- `MovePrevious` changes `p` to `p - 1`; `MoveNext` changes it to `p + 1`.
- `Insert` places content at `p` and returns a gap after the inserted content.
- `DeletePrevious` is backspace: it removes `p - 1` and returns `p - 1`.
- `DeleteNext` removes `p` and keeps the position.
- `ReplaceNext` replaces `p` and keeps the position.
- empty, start, and end positions are first-class states.

Boundary-invalid non-`Try` operations throw `InvalidOperationException`. `default` handling depends
on the C0 class/struct decision and must be explicit rather than accidentally dereferencing null
context storage.

### Provisional C# surface

The conservative zipper-as-version API is:

```csharp
public sealed partial class Rope<T>
{
    public RopeCursor<T> GetCursor(int position = 0); // O(log n)
}

public sealed class RopeCursor<T>
{
    public int Count { get; }
    public int Position { get; }
    public bool IsAtStart { get; }
    public bool IsAtEnd { get; }

    public bool TryPeekPrevious(out T value);
    public bool TryPeekNext(out T value);

    public RopeCursor<T> MovePrevious();
    public RopeCursor<T> MoveNext();
    public RopeCursor<T> Seek(int position);

    public RopeCursor<T> Insert(T value);
    public RopeCursor<T> InsertRange(ReadOnlySpan<T> values);
    public RopeCursor<T> DeletePrevious();
    public RopeCursor<T> DeleteNext();
    public RopeCursor<T> ReplaceNext(T value);

    public Rope<T> Snapshot();
}
```

The class is logically immutable: every operation returns a new cursor state. The sketch uses a
class because the arbitrary-depth path/context and cached snapshot are reference-shaped; C0 may
justify a small struct holding an immutable context reference, but no allocation claim follows from
that spelling.

Name the public abstraction `Cursor`; use “zipper” for the internal decomposition. Do not add a
separate `Rope` argument to cursor operations. The cursor owns its source/context, so it cannot be
silently applied to the wrong version.

Bookmarks and rebasing are not required for C1. If added later, a bookmark is a version-bound
boundary plus left/right insertion affinity. It may rebase only through an explicit change record
whose source identity matches; there is no meaningful automatic rebase onto an arbitrary unrelated
rope with duplicate elements.

### Internal zipper model

The working state is:

```text
logical sequence = flatten(left chunks)
                 ++ active[0 .. gap)
                 ++ active[gap .. length)
                 ++ flatten(right chunks)

Position = measure(left chunks) + gap
```

The active edit window is bounded independently of the normal rope chunk maximum. Candidate caps
16, 32, 64, and 128 are benchmark inputs; copying a 2,048-element ordinary chunk on every keystroke
would satisfy Big-O notation while failing the feature's purpose.

- Cursor creation splits to the containing chunk in O(log n).
- Movement inside the active window changes only the gap.
- Crossing an edge pulls a bounded slice from the adjacent tree through endpoint views.
- An edit copies only the bounded active window.
- Overflow flushes a bounded far-side slice into the adjacent tree.
- Underflow/empty focus pulls a bounded slice from a neighbor.
- `Snapshot()` concatenates left, active, and right, coalescing immediate seams; this is O(log n)
  amortized and is memoized per cursor state.

If C0 selects the focused-root alternative, the same decomposition becomes an internal `Rope<T>`
root variant. `Count`, cursor edits, peeks, and sequential enumeration operate directly over it;
nonlocal indexed operations may force thread-safe memoized canonicalization. The general measured
finger-tree engine itself remains unchanged.

### Honest cursor complexity table

| Operation | Zipper-as-version target |
| --- | --- |
| Create cursor or arbitrary `Seek` | O(log n), with path/context allocation |
| Position/start/end/peek in focus | O(1) |
| Move within active window | O(1), no new tree spine |
| Cross focus/chunk boundary | O(1) amortized; O(log n) worst case |
| Local insert/delete/replace | O(1) amortized with bounded focus copy; O(log n) worst case on boundary repair |
| Dirty `Snapshot()` | O(log n) amortized and allocates rebuilt spine |
| Repeated clean `Snapshot()` | O(1), same `Rope<T>` reference |
| Sequential traversal of k elements | O(k + log n) |

Do not state worst-case O(1) for local editing: the underlying finger-tree endpoint operations are
amortized and can force a suspended spine.

### C1. Positional rope cursor

Ship the gap contract, movement, local edit surface, snapshot, model tests, and benchmarks only after
C0 settles representation and bounds. The first public version omits bookmarks, arbitrary rebase,
selection objects, text line/column helpers, and a raw `FingerTree` cursor.

### C2. Measured and text cursors

After positional semantics stabilize, add `MeasuredRopeCursor<T, TMeasure, TMeasureOps>` with:

- `MeasureBefore`: measure of `[0, Position)`;
- `MeasureAfter`: measure of `[Position, Count)`; and
- seek by the existing closure-free monotone predicate contract.

For a noninvertible monoid, moving left cannot subtract one element's measure. The active window
therefore caches prefix and suffix measure arrays; rebuilding them performs at most the bounded
focus-cap number of callbacks. Across tree boundaries, combination order must remain correct for a
noncommutative monoid.

The current newline-count measure yields a line number but not a column. An O(1) line-and-column
claim requires a richer composable text extent, for example line-break count plus trailing-column
length (and total length where needed). C2 either adds and law-tests that monoid or documents column
as O(log n); it does not overclaim the current measure.

### C3. Samples

Update the Editor sample to demonstrate localized insertion, deletion, movement, Unicode, and
branching from an old cursor. Update the Tour's undo/redo text-buffer path to consume committed rope
snapshots and rename its current “cursor” history index to “history position,” preventing two
unrelated cursor meanings.

### C4. Later sequence families

`FingerTreeDeque<T>` is the next plausible adapter: a gap zipper is a left deque plus a right deque,
and movement transfers one endpoint. Returning a materialized deque after every edit again requires
either O(log n) concatenation or a focused deque representation, so it waits for the rope decision.

Defer these surfaces until a consumer and benchmark justify them:

- editable RRB cursor: random access is already its strength, while persistent edits still copy a
  radix path;
- `ReversibleDeque`, raw `FingerTree`, and Tungsten List cursors;
- element-identity handles or arbitrary-version bookmark rebasing; and
- any cursor over the unimplemented range-update sequence.

### Cursor validation and benchmark gates

The command-model suite compares against `List<T>` plus an integer gap and covers:

- movement, seek, peeks, insertion/range insertion, both deletes, and replacement;
- retained old cursors and branches from stale-but-valid versions;
- empty/start/end and focus-cap boundaries;
- rope boundaries 255/256/257 and 2,047/2,048/2,049;
- repeated seam oscillation and insert/delete churn;
- deep digit/middle transitions and lazy-spine forcing;
- source/snapshot identity, clean snapshot caching, and structural sharing;
- overflow before allocation and exception-safe measure callbacks;
- concurrent reads and snapshot-cache races; and
- a noncommutative measured model proving
  `MeasureBefore combine MeasureAfter == total` in order.

`RopeCursorBenchmarks` uses documents of 1K, 64K, and 1M elements; locality windows 1, 8, 256,
and random; typing/backspace bursts; alternating move/edit; absolute seek; and snapshot cadences 1,
16, and 256. Compare indexed `Rope` edits, the cursor prototypes, and appropriate mutable
`StringBuilder`/gap-buffer baselines. Record time, p50/p99 latency, bytes per edit, Gen0 pressure,
node visits, spine allocations, retained memory, and normalization cost. Sweep focus caps rather
than choosing one by intuition.

Porting begins only after the C# cursor materially beats indexed rope edits on named local histories
without regressing non-cursor rope workloads.

## Dependency And Rollout Order

The tracks share contract review and benchmark infrastructure but no implementation dependency:

```text
P0 contract lock
├── L1 CHAMP transient
│   └── L2 frozen bake-off ──> L3 frozen map/set ──> L4 Ctrie SnapshotView conversion
└── C0 zipper/focused-root spike ──> C1 Rope cursor ──> C2 measured/text cursor ──> C3 samples
                                         └───────────> C4 deque evaluation
```

Frozen conversion does not depend on a transient implementation; both meet at persistent CHAMP.
The cursor does not depend on range updates. Styled text depends on both a settled cursor and a
separately designed/law-tested range-update action, so it remains later work.

The recommended execution order is:

1. **P0 Axis 2 contract lock**: this proposal, catalog corrections, benchmark skeleton design, and
   API/complexity review.
2. **L1 C# CHAMP one-way transient**.
3. **C0 persistent-zipper/focused-root spike**, in parallel with frozen measurements once L1
   contracts are stable.
4. **L2/L3 fixed-layout frozen hash map/set** if the measurements meet the shipment gate.
5. **C1 C# positional rope cursor** and **C3 Editor/Tour integration**.
6. **L4 Ctrie snapshot conversion** and **C2 measured/text cursor**.
7. Evaluate C# RRB transient, packed sorted frozen types, and deque cursor separately; none is
   automatically implied by the first families.
8. Consider sibling-language promotion only after a C# compatibility round and explicit scope
   decision.

## Cross-Language Posture

This plan is C#-first, not a blanket parity promise. If later promoted:

- Kotlin/JVM can express edit tokens naturally.
- Rust must preserve its no-unsafe rule; consuming ownership may be idiomatic while O(1) adoption
  over shared `Arc` nodes may not be.
- C++ must preserve move-only representatives, which can make copy-on-first-write impossible for a
  shared payload without a different ownership representation.
- C needs a separate failure-atomic design integrating edit ownership, reference counts, fallible
  callbacks, and fallible allocation.
- Haskell should prefer an `ST`/`runST` construction boundary over a nominally identical imperative
  public transient.
- Kotlin and Rust sequence checkpoints do not share every C# finger-tree complexity bound; cursor
  semantics may port while documented costs remain backend-specific.

Parity means result semantics, policy preservation, representative retention, version isolation,
and failure behavior. Conversion and locality complexity may be an explicit language boundary.

## Explicitly Postponed Tracks

The following work is outside the active Axis 2 wave:

- automatic flat/tree size tiers and threshold hysteresis;
- runtime key-type dispatch, string-specific frozen layouts, ART routing, and generic factories that
  choose Patricia or CHAMP;
- tiny frozen layouts, fuse/ribbon filters, PGM indexes, and per-count strategy selectors;
- sorted, vector, and rope frozen families without their own read-heavy evidence;
- owner-token mutation in the lazy raw FingerTree core;
- composite Tungsten transients;
- RRB/ReversibleDeque/Tungsten cursor surfaces without a consumer; and
- the styled-text sample until both cursor and range-update foundations exist.

Re-entry requires a named consumer or benchmark showing that the fixed general representation is
the bottleneck. Merely observing a different strategy in another library is not sufficient.

## References

- [Clojure transients](https://clojure.org/reference/transients) - owner-isolated mutation and the
  one-way persistent publication precedent.
- [.NET frozen collections](https://learn.microsoft.com/en-us/dotnet/api/system.collections.frozen?view=net-10.0) - the separate, construction-heavy read-optimized tier used as a benchmark baseline.
- [Hinze and Paterson, *Finger trees: a simple general-purpose data structure*](https://www.cs.tufts.edu/comp/150FP/archive/ralf-hinze/finger-trees.pdf) - endpoint, concatenation, split, and measured-search bounds underlying the cursor analysis.
- [Stucki et al., *RRB Vector: a Practical General Purpose Immutable Sequence*](https://infoscience.epfl.ch/record/213452) - transient/vector background and the reason RRB remains a separate evaluation.
