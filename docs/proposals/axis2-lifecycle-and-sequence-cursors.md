# Axis 2 Final Plan: Lifecycle And Sequence Cursors

- Created (UTC): 2026-07-13T03:22:20Z
- Repository HEAD: b600a208395fd16e73b620cfda4eca20d372bd75
- Final synthesis (UTC): 2026-07-13T04:31:40Z
- Repository HEAD (final synthesis): 6dbe754bd6166462beae226621511ddbcb24aaad
- Final review closure (UTC): 2026-07-13T05:03:55Z
- Repository HEAD (final review closure): 387e24c9ce07631a6fa331693e34bc25afb459be
- Status: Final authoritative Axis 2 plan
- Audience: Maintainers designing the next C# persistent-collection wave
- Scope: Detailed C#-first plan for owner-token transients, persistent collections,
  read-optimized frozen collections, and version-bound rope cursors

## Decision

Axis 2 has three independent C#-first tracks after one shared contract/instrumentation phase:

1. **Track C leads:** a version-bound gap cursor over `Rope<T>`, immediately followed by the
   `MeasuredRope`/text surface and Editor/Tour integration. This is the first intended public Axis 2
   capability.
2. **Track T is evidence-gated:** a one-way owner-token CHAMP transient, beginning with workload and
   representation gates before any public API is committed.
3. **Track F is evidence-gated:** a fixed-layout read-optimized frozen hash tier, beginning with one
   minimally faithful packed-index signal before a layout bake-off or public API.

“Cursor-first” is a priority, not a synthetic implementation dependency. C0, T0, and F0 may run in
parallel after P0; a track that clears its evidence gate does not wait merely to preserve document
order. The cursor is the lead public target because it adds a new sequence capability and has clear
sample integration targets. The Editor and Tour are not claimed as existing production hot paths:
today they are measured-text demonstrations, and both require C2 before they can exercise the cursor
end to end.

Automatic size-tier switching and key-type dispatch are intentionally postponed. The first frozen
hash implementation uses one general layout for every count and key type. The first cursor does not
depend on the planned range-update sequence. No track is a commitment to port a public surface
until the C# contracts and measurements stabilize.

This document refines the survey-level entries in the
[frontier structure catalog](../reference/frontier-structure-catalog.md) and supersedes the API and
complexity sketch in item A3 of the
[2026-07-09 proposal](new-data-structures-2026-07-09.md). It incorporates the conclusions of the
[2026-07-13 review](../reviews/axis2-lifecycle-and-cursors-review-2026-07-13.md) and the useful parts
of its [cursor-first alternative](axis2-cursor-first-alternative-2026-07-13.md). It does not
describe shipped APIs.

## Disposition Of The Review And Alternative

| Reviewed conclusion | Final decision | Qualification or correction |
| --- | --- | --- |
| Preserve the builder/transient/persistent/frozen/snapshot-view vocabulary and lifecycle designs | **Agree** | There are five distinct mechanisms, not four; none is a synonym for another. |
| Make the cursor the lead Axis 2 deliverable | **Agree** | It is the first intended public target, but C0/T0/F0 remain independent and may proceed in parallel. |
| Treat Editor and Tour as named current consumers | **Qualify** | They are credible integration targets, not measured localized-edit workloads today; both use `MeasuredRope` and therefore wait for C2. |
| Add a frozen pre-gate | **Agree** | F0 measures lookup, enumeration, retained memory, construction, and break-even. One layout failing one lookup mix cannot disprove every frozen benefit. |
| Add a cheap throwaway owner-token pre-gate | **Disagree as stated; replace** | Current CHAMP nodes and arrays are readonly-shaped. A faithful owner-token experiment already entails load-bearing ownership and prepare/commit work. T0 first proves a target workload; T1 then builds a production-representative private kernel before T2 exposes an API. |
| Gate branched-cursor amortization | **Strongly agree** | A potential/invariant argument is required; counters and adversarial histories validate or falsify it but do not prove an asymptotic bound alone. |
| Target “few edits to a large retained base” for transients | **Disagree; correct the workload** | The plausible win is many edits per publication, especially repeated/clustered paths and wrapper/path-allocation pressure. Sparse one-off edits are normally a wash or loss. |
| Promise canonical CHAMP order for frozen enumeration | **Reject** | Equal-full-hash collision order is intentionally history-dependent. Frozen order is stable for an unchanged instance but otherwise unspecified and never promised as insertion, sorted, or serialization order. |
| Treat cursor risk as design-only | **Reject** | Immutable wrapper allocation and snapshot-every-edit O(log n) work can still produce no worthwhile win. C0 has an explicit defer outcome. |
| Use “one-day” spikes | **Reject** | Spikes are bounded by files, prototype surfaces, workloads, and exit evidence, never calendar estimates. |
| Update current-state contracts and catalogs when a surface ships | **Agree** | Each public phase updates its workspace overview/usage/API/validation docs, both repository catalogs, the frontier status, and shared semantic contracts in the same tranche. |

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
PersistentHashMap -- O(1) adopt --> Transient -- O(1) Persist (consumes) --> PersistentHashMap

PersistentHashMap -- O(n) Freeze --> FrozenHashMap -- O(n) ToPersistent --> PersistentHashMap

staging Builder -- family-specific construction/publication cost --> Persistent

Ctrie -- O(1) Snapshot --> SnapshotView -- O(n) --> PersistentHashMap
                                      `-- O(n) --> FrozenHashMap
```

The cycle is asymmetric on purpose:

- persistent -> transient and transient -> persistent can be O(1) because the nodes are designed
  for token-governed copy-on-write;
- persistent -> frozen and frozen -> persistent are O(n) representation changes;
- frozen -> transient, when T2 exists, routes through persistent and is O(n); otherwise ordinary
  persistent editing remains available after `ToPersistent()`;
- v1 does not add `Transient.Freeze()`: the persistent phase boundary stays visible;
- `Freeze()` on a frozen value, if exposed, returns the same object.

Builders remain useful. They are construction APIs with family-specific costs, not aliases for the
transient state.

## P0: Shared Contract And Instrumentation Lock

Before representation changes, add benchmark skeletons and executable contract tests for the current
`PersistentHashMap` and `PersistentHashSet`. The tests pin comparer identity, first stored key
representative, collision behavior, null handling, stable enumeration, no-op identity, retained old
versions, and callback exception behavior. Add corresponding Rope/MeasuredRope oracles for logical
sequence, chunk bounds/counts, sharing, snapshot identity, noncommutative measure order, overflow,
and callback behavior. These become the oracle for all three tracks.

P0 also establishes common instrumentation before any spike is interpreted:

- node visits, array clones, wrapper allocations, focus/carry copies, forced suspensions, and
  retained bytes;
- identical data generation, comparer instances, hit/miss mixes, warmup, and result-consumption
  rules within each comparable lane, with controls that cannot represent a semantic lane explicitly
  omitted rather than fed different data;
- an `InternalsVisibleTo` grant from the HAMT project to the existing
  `Tools.DataStructures.FingerTree.Benchmarks` assembly, plus internal-only diagnostic hooks for roots,
  ownership, copied-node counts, and `BulkBuilder`; benchmark access does not become public API;
- a predeclared materiality rule in the benchmark README, at least the larger of the measured noise
  floor and a maintainer-chosen practical margin, so a threshold is not selected after seeing data;
- separate correctness and performance runs—no prototype may skip semantic checks to obtain a
  favorable number; and
- decision records for every advance/defer result, including the exact command and raw/curated
  benchmark artifact.

## Track T: One-Way CHAMP Transient

### T0. Workload qualification

Do not begin by calling a toy mutable node a transient. Extend `ChampBenchmarks` and structural
counters over the shipped persistent map to identify whether the repository has a credible target
regime. Sweep:

- base sizes 0, 1, 8, 32, 1K, and 100K;
- edits per publication from 1 through the full count;
- repeated same-key edits, clustered hashes/shared prefixes, uniformly disjoint keys, collision
  buckets, removals, and mixed operations; and
- publication cadence 1, 8, 64, and end-of-batch.

The expected win regime is **many edits per publication**, especially repeated or clustered paths
where owned nodes can be reused and intermediate persistent wrappers/path arrays dominate. One or a
few sparse edits normally favor direct persistent operations because the first shared path must be
copied either way and a transient adds session/token overhead.

T0 advances only when counters identify a named regime in which eliminating repeated wrappers/path
copies can plausibly clear the predeclared materiality threshold. Otherwise Track T is deferred and
the existing persistent operations plus `BulkBuilder` remain the answer.

### T1. Production-representative private kernel

A faithful value spike cannot be a disposable parallel toy: current `LeafNode`, `CollisionNode`, and
`BitmapIndexedNode` are readonly-shaped, branch nodes cache counts, and `Data`/`Children` arrays can
be shared independently of their wrapper. Build an internal experimental kernel against the real
node semantics, with no public `Transient` API yet, and compare two ownership layouts:

1. a nullable owner token on every mutable-capable persistent node/array owner; and
2. separate transient-editable branch/collision nodes that publish into the persistent hierarchy.

The kernel must establish array ownership before an in-place element/child write, keep cached counts
correct, define leaf and collision replacement/mutation explicitly, seal publication without a graph
walk, and implement the same operation-wide prepare/commit rule required below. Measure the baseline
retained-size regression on ordinary persistent maps as well as the edited transient graph. A
prototype that omits failure atomicity or mutates an array merely because its wrapper token matches
is not production-representative evidence.

T1 advances only if the real kernel produces a material throughput and/or allocation win in the T0
regime after charging token/editable-node retained memory, without regressing ordinary persistent
lookup/update beyond the locked tolerance. If it fails, remove or quarantine the experiment and do
not expose a public lifecycle merely for API symmetry.

### T2. One-way C# CHAMP transient API

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
mutation, enumeration request, or second publication throws `ObjectDisposedException`. Views and
enumerators obtained before publication capture the transient/version and also fail after
publication; they cannot drain an alias of the newly persistent graph. `Persist()` increments the
version as part of invalidation. The C# type system cannot consume an object statically, so runtime
invalidation makes the one-way transition unambiguous. A future reusable owner-token builder would
be a separate API with repeated-snapshot semantics; v1 does not blur those two models.

The transient is single-owner and unsynchronized. Sequential ownership transfer between threads is
permitted only under caller-provided synchronization; concurrent access is unsupported. Enumerator
mutation checks are fail-fast through one version counter.

### Edit-token mechanics

The selected T1 representation gives every mutable node **and every array that may be written in
place** unambiguous ownership under one edit token. A wrapper token alone is not proof that its
`Data` or `Children` array is private, because persistent branch construction currently reuses
unchanged arrays. The token has a small active/sealed state and no back-reference to the transient.

1. `ToTransient()` creates a transient over the persistent root and a fresh active token. It does
   not traverse the graph.
2. A mutation may change a node in place only when the node's token is the active token.
3. An unowned node or a node owned by another/sealed token is copied along the edited path; the copy
   and any array it may mutate receive the active token. Immutable leaf/collision objects may be
   replaced rather than mutated if T1 shows that policy is faster or safer.
4. A point edit uses an operation-wide prepare/commit protocol. It performs every failure-prone
   hash/equality callback and allocates every replacement array/path before the first in-place
   write. The commit phase consists only of non-throwing field/reference assignments. Per-node
   “allocate then assign” is insufficient if a descendant could change before an ancestor
   allocation fails.
5. `Persist()` first allocates/prepares the unpublished immutable wrapper while the transient is
   still active. It then seals the token and invalidates the transient through non-throwing
   interlocked/volatile state changes, and returns the prepared wrapper. Allocation failure leaves
   the transient active and unchanged. It never clears tags by traversing the graph.
6. A later transient from the published root gets a different token and therefore path-copies on
   its first write.

Every prepare plan computes the resulting subtree counts before commit. Non-throwing commit writes
array slots/references and cached counts in an order that cannot leave a reachable transient with a
mixed old/new count. Publication tests recursively validate counts and canonical CHAMP contraction.

The transient retains its source persistent object and a dirty flag. A clean
`source.ToTransient().Persist()` returns `source` by reference, including after any number of
logical no-op edits. A clean `CreateTransient(comparer).Persist()` returns the appropriate
comparer-preserving empty. In those cases no new wrapper allocation is required before sealing.

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
- collision-node structural invariants and branch contraction match persistent deletion, while
  equal-full-hash bucket order remains stable but unspecified;
- a single-item edit has the strong exception guarantee through the operation-wide prepare/commit
  protocol; no owned node is changed until every potentially throwing step succeeds;
- a range operation, if added later, is incrementally committed and documents the item at which a
  throwing source/callback leaves the transient;
- no-op edits do not advance the enumerator version or allocate a new path.

The validation suite must inject throwing hash/equality/value callbacks and allocation failures at
every publication boundary that can be made deterministic. In-place shifting followed by
failure-prone work is prohibited. Tests must fail each preparation step and prove that content,
root identity, token activity, version, and enumerator validity are unchanged.

## Track F: Fixed-Layout Frozen Hash Tier

### F0. Packed-index signal gate

Before a multi-layout bake-off or public surface, build one minimally faithful internal packed-entry
array plus offline integer slot table in the existing benchmark project. It must use real keys,
hashes, comparer calls, stored representatives, collision/null lanes, and source-map enumeration;
synthetic precomputed probe integers alone are not evidence.

Compare with `PersistentHashMap`, `Dictionary`, `ImmutableDictionary`, and BCL
`FrozenDictionary` over positive, negative, and mixed lookup, full enumeration, retained bytes,
construction, and calculated break-even reads. A credible signal is a predeclared material win in a
**named read-heavy regime** across lookup and/or enumeration/memory, with no unacceptable regression
in the regime's other load-bearing metrics. A single layout losing one hit ratio does not disprove
all frozen representations; conversely, a micro-win without a realistic construction break-even or
memory story does not justify a public family. If no credible regime appears, defer Track F before
the bake-off.

### F1. Frozen-layout bake-off

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

### F2. Fixed-layout frozen hash map/set

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

- a packed entry array preserving the source persistent version's exact enumeration sequence;
- each entry stores the already computed 32-bit hash, stored key, and value;
- an offline-built integer slot table maps probes to packed-entry indexes;
- one benchmark-selected load factor applies to all nonempty instances;
- enumeration scans packed entries and therefore does not expose probe-table order;
- the library performs no lookup-time allocation and never mutates/memoizes shared state; user
  comparer callbacks remain outside that allocation guarantee.

The public ordering contract is **stable but unspecified**: unchanged frozen instances enumerate
repeatably, but callers must not treat the order as insertion, sorted, canonical, or serialization
order. Equal-full-hash collision buckets and retained representatives are intentionally
history-dependent in CHAMP.

`CreateRange` is semantically equivalent to
`PersistentHashMap.CreateRange(items, comparer).Freeze()`: it preserves the first stored key and last
distinct value; an equal incoming value retains the stored value representative. It also preserves
the resulting persistent version's traversal sequence. A fused implementation is allowed only if it
produces that same sequence. `SnapshotView.Freeze()` is semantically equivalent to
`snapshot.ToPersistentHashMap().Freeze()` rather than exposing Ctrie traversal order.
`ToPersistent` uses the canonical-topology bulk-construction path and preserves comparer identity;
freezing the result must reproduce the frozen sequence, including collision-bucket order. Empty
singleton reuse is allowed because it is an identity policy, not adaptive layout selection.

The frozen surface has no `SetItem`, `Add`, `Remove`, builder, or update-shaped convenience. Editing
is explicitly expensive:

```csharp
var edited = frozen.ToPersistent().SetItem(key, value); // O(n) conversion, then persistent edit

// Optional batch-edit path only if Track T2 has shipped:
var transient = frozen.ToPersistent().ToTransient();
```

### F3. Ctrie snapshot conversion

Retain the already shipped `SnapshotView.ToPersistentHashMap()` and add
`SnapshotView.Freeze()`, not `Freeze()` on the live Ctrie. The receiver name must make clear that the
concurrent structure continues to accept writes. Each conversion independently enumerates one
captured generation once; later live-trie writes cannot affect either result. Tests and benchmarks
invoke both conversions on the same snapshot rather than taking two live snapshots.

### T/F benchmark matrices

`TransientLifecycleBenchmarks` covers counts 0, 1, 8, 32, 1K, and 100K, with:

- fresh construction;
- persistent `SetItem` loops;
- the current canonical bulk builder;
- transient construction;
- persistent -> transient with edit counts ranging from one to the full count;
- repeated-key, clustered-prefix, disjoint-key, and collision-heavy batches;
- edit/publish cycles and retained-base branching;
- publication cadence and custom-comparer datasets; and
- direct persistent edits plus the current `BulkBuilder` where its construction semantics apply.

Record throughput, allocated bytes, copied/owned node counts, retained bytes, and publication
latency. Structural counters must demonstrate that adoption and `Persist()` do not traverse.

`FrozenLookupBenchmarks` records:

- positive, negative, and mixed hit ratios;
- enumeration throughput;
- construction cost and retained bytes per entry;
- `ToPersistent` conversion cost;
- random, colliding, null-capable, string, and custom-comparer datasets; and
- the read count at which construction cost breaks even.

Compare `PersistentHashMap`, mutable `Dictionary`, `ImmutableDictionary`, BCL
`FrozenDictionary`, and each fixed-layout prototype. Null-key semantic and benchmark lanes apply to
repository types; omit BCL controls that reject that key domain rather than changing the input or
reporting incomparable failures. A key-type dataset is a workload, not permission to dispatch on
key type.

### T/F shipment gates

The transient ships only when it demonstrates all semantic rules, failure atomicity, base-version
isolation, post-publication invalidation, O(1) adoption/publication counters, and a material net win
in the T0 regime after ordinary-map and edited-node retained memory is charged. The frozen map/set
ships only with a documented lookup/enumeration/memory win and realistic construction break-even in
a named read-heavy regime. A merely hypothetical semantic distinction does not override F0/F1.

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

An optional hybrid can make the `Rope<T>` facade accept a focused root (left ordinary-chunk tree +
bounded partial carries/focus + right ordinary-chunk tree) and canonicalize lazily. That design
could return `(Rope, Cursor)` after local edits in O(1) amortized time, but it broadens every rope
operation's representation contract. It is an experiment in C0, not a pre-written complexity claim.

### C0. Zipper-first representation and proof spike

Start with one minimally complete **zipper-as-version** prototype: split state remains in the cursor
and `Snapshot()` rebuilds a canonical root. Do not build a symmetric full focused-root implementation
up front. Instrument node visits, spine allocations, forced suspensions, focus/carry copies, cursor
wrapper allocations, retained memory, and snapshot cadence 1/16/256 across long local-edit histories.

Separate logical version identity from navigation state:

- `CursorVersionState<T>` identifies one immutable logical sequence, owns the count and a
  thread-safe winner-returning `Snapshot()` memo cell, and stores the decomposition seed needed to
  build the canonical rope;
- `CursorContext<T>` owns the gap position/path/focus decomposition used for navigation;
- pure navigation returns a new context over the same version state and therefore the same canonical
  snapshot reference;
- an edit creates a new version state plus its refreshed context; and
- snapshot memoization publishes with `Interlocked.CompareExchange`; racing losers discard their
  candidate and return the winner rather than exposing two reference-distinct snapshots.

Snapshot construction is failure-atomic: a failing call publishes nothing; absent a concurrent
winner the memo remains empty, and in either case the cursor/version remains reusable. C2 retains
each element measure plus ordered prefix/suffix
aggregates for the active focus, both carries, and every fragment cut from a pulled ordinary chunk;
whole untouched chunks retain their existing cached aggregate. Snapshot repacking can therefore split
a carry or fragment at a new boundary and recombine prepared measures without calling
`TMeasureOps.Measure` again for those elements. Racing snapshot candidates may repeat structural and
`Combine` work, but they do not duplicate element-measure callbacks. A failed edit does not publish a
new version state.

C0 compares the logically immutable class, a small readonly struct over immutable heap state, and a
mutable single-owner session as an explicit throughput control. The mutable form is not silently
substituted for the persistent cursor contract. A class that allocates one wrapper per move is not
marketed as an enumerator replacement.

Branched-history complexity is a proof obligation, not merely a benchmark. Write a potential/invariant
argument over the version DAG in which focus and each partial carry are bounded, but explicitly charge
every branch-specific endpoint repair, rebuilt spine, and forced suspension; memoized work counts as
shared only when the relevant suspension really is shared by those branches. The proposition to prove
is whether editing `b` descendants from one retained boundary cursor costs O(b) amortized bounded work
plus genuinely shared forcing, while each individual operation retains an O(log n) worst case. Validate
or falsify that proposition with fan-out across increasing sizes and branch counts. If distinct branches
instead incur independent logarithmic repair, publish O(b log n) for that history—or retain only the
linear-lineage amortized contract—rather than assuming persistence supplied the stronger bound.

C0 has three outcomes:

1. **Select zipper-as-version** when it materially beats indexed Rope edits in a predeclared named
   local workload at that workload's required snapshot cadence. Cadences 1, 16, and 256 are all
   measured to expose tradeoffs; the zipper need not win every cadence to clear a narrower named gate.
2. **Escalate conditionally to focused-root** only when snapshot-after-every-edit is itself a
   predeclared required workload, cadence 1 misses the gate, and dirty canonicalization is the measured
   blocker that a focused root can address. That escalation must then implement and benchmark every
   ordinary Rope operation over the representation union.
3. **Defer the editable cursor** when neither representation clears the shipment gate without
   unacceptable non-cursor regressions. A read-only retained navigator would be a separate,
   consumer-gated proposal, not a consolation API under the same claims.

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

The cursor is logically immutable: no operation changes the receiver's observable sequence,
position, or version identity in place; navigation and editing return cursor values subject to the
no-op identity rules below. `Snapshot()` may populate the shared version state's internal thread-safe
memo without changing that logical value. The sketch uses a class because the arbitrary-depth path/
context and cached snapshot are reference-shaped; C0 may justify a small struct holding an immutable
context reference, but no allocation claim follows from that spelling.

Identity rules are explicit:

- `Seek(Position)` and empty `InsertRange` return the unchanged cursor value and preserve the exact
  shared version/context state and cached snapshot; if C0 selects a class this additionally means
  `ReferenceEquals` with the receiver, while a struct makes no object-identity promise;
- pure navigation returns a different cursor object under the class prototype but shares the same
  immutable sequence state and therefore the same clean snapshot object;
- moving away and back does not promise the original cursor object by reference;
- `ReplaceNext` follows `Rope<T>.SetItem`: it creates an edited state even when the supplied element
  would compare equal, and invokes no hidden element-equality callback; and
- failed `TryPeek` operations do not change state.

Any later range-move convenience defines a zero-distance move as an unchanged value with the same
shared state, and as exact-object identity only under a class representation. C0 counters and C1 tests
pin representation-appropriate cursor identity, context-root identity, and snapshot identity
separately so one is not inferred from another.

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
logical sequence = flatten(left ordinary chunks)
                 ++ left partial carry
                 ++ active[0 .. gap)
                 ++ active[gap .. length)
                 ++ right partial carry
                 ++ flatten(right ordinary chunks)

Position = measure(left ordinary chunks) + left-carry length + gap
```

The active edit window is bounded independently of the normal rope chunk maximum. Candidate caps
16, 32, 64, and 128 are benchmark inputs; copying a 2,048-element ordinary chunk on every keystroke
would satisfy Big-O notation while failing the feature's purpose.

Focus-sized fragments cannot be appended directly as ordinary rope chunks: repeated typing would
then accumulate arbitrarily many sub-minimum chunks and violate the existing proportional
chunk-count policy. `RopeChunking` currently defines only `MinChunkSize = 256` and
`MaxChunkSize = 2048`; it has no hidden target constant. C0 therefore treats
`FlushChunkSize` as an explicit candidate in `{256, 512, 1024, 2048}`, independent of the focus cap,
and locks one value from measurements before C1.

The zipper maintains at most one partial carry between the active window and each adjacent
ordinary-chunk tree, with `0 <= carry.Length < FlushChunkSize`. Overflow merges into the near carry,
flushes as many full `FlushChunkSize` ordinary chunks as needed, and retains only the sub-threshold
remainder. Pulling an endpoint chunk first takes the bounded near-focus slice; any remaining valid
ordinary chunk is reattached immediately, while only a remainder smaller than `MinChunkSize` can
enter the carry (and is therefore smaller than every flush candidate). `Snapshot()` packs the two
bounded carries plus the active window into chunks within `[MinChunkSize, MaxChunkSize]` except for
the unavoidable final boundary chunk before joining the trees. Thus there are at most two partial
fragments outside the active window, and snapshot repair remains bounded chunk work plus the
documented O(log n) tree join rather than an O(n) global `Compact()`. C0 exhaustively tests every
flush candidate against endpoint chunk lengths 1 through `MaxChunkSize`, including existing chunks
larger than the candidate.

- Cursor creation splits to the containing chunk in O(log n).
- Movement inside the active window changes only the gap.
- Crossing an edge pulls a bounded slice from the adjacent tree through endpoint views.
- An edit copies only the bounded active window.
- Overflow transfers a bounded far-side slice into the adjacent carry and flushes only a normally
  packed chunk into the tree.
- Underflow/empty focus pulls from the carry first, then from a neighboring ordinary chunk.
- `Snapshot()` packs the bounded carries/focus, concatenates left and right, and coalesces the two
  seams; this is bounded chunk work plus O(log n) amortized tree work and is memoized per cursor
  state.

If C0 selects the focused-root alternative, the same decomposition becomes an internal `Rope<T>`
root variant. `Count`, cursor edits, peeks, and sequential enumeration operate directly over it;
nonlocal indexed operations may force thread-safe memoized canonicalization. The general measured
finger-tree engine itself remains unchanged.

### Honest cursor complexity table

| Operation | Work target | Allocation note under provisional class spelling |
| --- | --- | --- |
| Create cursor or arbitrary `Seek` | O(log n) | Path/context plus cursor wrapper |
| Position/start/end/peek in focus | O(1) | None |
| Move within active window | O(1), no new tree spine | One cursor wrapper; a struct spelling may remove it |
| Cross focus/chunk boundary | O(1) amortized in the C0-proven history class; O(log n) worst case | Wrapper plus changed context/carry storage |
| Single local insert/delete/replace | O(1) amortized with bounded focus copy in the C0-proven history class; O(log n) worst case on boundary repair | Wrapper plus bounded focus/state storage |
| `InsertRange` of m elements | O(m + log n) amortized after positioning; necessarily Omega(m) | O(m / `FlushChunkSize`) packed storage plus bounded context |
| Dirty `Snapshot()` | Bounded carry packing plus O(log n) amortized tree join | Packed seam chunks plus rebuilt spine |
| Repeated clean `Snapshot()` | O(1), same `Rope<T>` reference | None |
| Sequential traversal of k elements | O(k + log n) | Theta(k) wrappers for the class prototype |

Do not state worst-case O(1) for local editing: the underlying finger-tree endpoint operations are
amortized and can force a suspended spine. `InsertRange` checks count overflow before allocating or
changing state, and an empty range returns the same cursor. Cursor-wrapper allocation is a C0
selection criterion, not incidental noise: the spike must compare an immutable class, a small
readonly struct over immutable heap context, and any mutable-session alternative without inferring
that a struct makes the path itself allocation-free.

“C0-proven history class” is deliberate. C1 XML/API documentation publishes the selected scope—fully
branched histories, linear lineages only, or a weaker branch bound such as O(b log n)—from the proof
and counters. The unqualified amortized rows do not survive a C0 result that establishes only the
narrower scope.

### C1. Positional rope cursor

Ship the gap contract, movement, local edit surface, snapshot, model tests, and benchmarks only after
C0 settles representation and bounds. The first public version omits bookmarks, arbitrary rebase,
selection objects, text line/column helpers, and a raw `FingerTree` cursor.

### C2. Measured and text cursors

After positional semantics stabilize, mirror the selected C1 representation:

```csharp
public sealed partial class MeasuredRope<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> GetCursor(int position = 0);

    public bool TryGetCursorByMeasure(
        Func<TMeasure, bool> predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor);
    public bool TryGetCursorByMeasure<TPredicate>(
        TPredicate predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure>;
}

public sealed class MeasuredRopeCursor<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    public int Count { get; }
    public int Position { get; }
    public TMeasure MeasureBefore { get; } // [0, Position)
    public TMeasure MeasureAfter { get; }  // [Position, Count)

    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> Seek(int position);
    public bool TrySeekByMeasure(
        Func<TMeasure, bool> predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor);
    public bool TrySeekByMeasure<TPredicate>(
        TPredicate predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure>;

    // Every C1 peek, move, and edit member is mirrored; movement and edits return
    // MeasuredRopeCursor<T, TMeasure, TMeasureOps>.
    public MeasuredRope<T, TMeasure, TMeasureOps> Snapshot();
}
```

The class spelling is provisional and mirrors C1's sketch; C0 may select a readonly struct over
shared immutable state without changing these semantic members.

Measure seek follows the existing `TryLocateByMeasure` boundary exactly: the predicate is evaluated
over accumulated prefixes of the whole version from the monoid identity (it is absolute, not relative
to the current gap) and must be monotone—once true, it remains true for every longer prefix. Success
returns a cursor immediately before the first element whose inclusive prefix satisfies the predicate,
so `MeasureBefore` excludes that element. A predicate already true for `TMeasureOps.Empty` returns
position 0 on a nonempty rope, matching the existing API. When no element satisfies the predicate,
including for an empty rope, the method returns `false` and the out cursor is the end cursor with
`Position == Count` and `MeasureBefore == total measure`. The delegate overload preserves the existing
measured-rope convenience surface; the constrained struct overload is its closure-free hot-path form.
Positional `Seek` remains O(log n). Measure seek is O(log n) plus a bounded scan of the located
ordinary chunk—up to `MaxChunkSize`, currently 2,048 elements—and any separate focus-preparation pass;
`MeasureBefore` and `MeasureAfter` are O(1) after that cursor state is prepared.

For a noninvertible monoid, moving left cannot subtract one element's measure. The active focus,
partial carries, and pulled chunk fragments therefore retain per-element measures and ordered prefix/
suffix aggregates; preparing a newly pulled fragment performs at most `MaxChunkSize` element-measure
callbacks, and rebuilding the focus performs at most the smaller focus-cap count. Across tree
boundaries, combination order must remain correct for a noncommutative monoid.

The text specialization remains `MeasuredRope<char, int, NewlineMeasure>`. C2 deliberately does not
introduce a second `TextExtent` rope family that would require duplicate factories, builders, string,
line, Unicode, and grapheme helpers. For a text cursor, `MeasureBefore` is the zero-based line in O(1);
the UTF-16-code-unit column is `Position` minus the current line's start, located by the same absolute
newline-measure search as `LineStartOffset`, in O(log n). A cursor-local line-start cache may be
measured as a separate optimization, but is not part of the baseline complexity claim. Grapheme
navigation remains the existing text-helper concern and is not conflated with UTF-16 columns.

### C3. Samples

Both text samples remain on `MeasuredRope<char, int, NewlineMeasure>` and adopt its measured cursor
after C2; no parallel text-rope type or helper migration is required. C1 is validated by its dedicated
positional model tests and benchmarks rather than by silently changing a sample away from measured
text. Sample line/column output uses the cursor's newline prefix and O(log n) line-start lookup.

- Update the Tour's measured undo/redo text-buffer path to retain measured cursor versions as history
  and materialize snapshots only at explicit display/commit boundaries. That cadence is predeclared
  and measured; the demonstration does not silently redefine cadence 1 as a required workload.
  Rename its current “cursor” history index to “history position,” preventing two unrelated cursor
  meanings.
- Update the Editor sample to demonstrate localized insertion, deletion, movement, Unicode,
  line/column behavior, and branching from an old cursor.

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
- adversarial fan-out from one retained cursor exactly at focus/carry/chunk boundaries, with branch
  count and sequence size scaled independently and checked against the C0 potential argument;
- empty/start/end and focus-cap boundaries;
- rope boundaries 255/256/257 and 2,047/2,048/2,049;
- repeated seam oscillation and insert/delete churn;
- fragment/carry counters proving at most one partial carry per side and ordinary chunk count remains
  proportional to sequence length under long typing histories;
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

`MeasuredRopeCursorBenchmarks` repeats the selected local-edit and snapshot-cadence lanes against
indexed `MeasuredRope<char, int, NewlineMeasure>`, then adds positional/measure seeks and line/column
queries over newline-sparse and newline-dense text. It records `Measure`/`Combine` callback counts as
well as latency and allocation. C2 advances to sample integration only if it preserves C1's material
local-edit win in a predeclared measured-text workload at that workload's snapshot cadence, while
measure navigation and line/column queries remain within their separately locked acceptable-overhead
tolerance. A positional win does not waive regressions introduced by prefix/suffix measure state.

Porting begins only after C1 and C2 clear their respective named positional and measured-text gates
without regressing non-cursor rope workloads.

## Dependency And Rollout Order

The tracks share P0 contracts/instrumentation but have no implementation dependency. Track C is the
lead public priority; T and F advance independently only when their private gates clear:

```text
P0 contract oracles + benchmark/counter skeletons
│
├── TRACK C (lead public target)
│     C0 zipper-first representation/proof spike
│       ├── select zipper-as-version ─────────────┐
│       ├── measured blocker -> focused-root spike│
│       └── neither clears -> DEFER              │
│                                                v
│     C1 positional Rope cursor -> C2 measured/text cursor -> C3 Editor + Tour
│                       └────────────────────────────> C4 deque evaluation (consumer-gated)
│
├── TRACK T (private evidence before API)
│     T0 workload qualification
│       └── [credible] T1 production-representative internal kernel
│              └── [material net win] T2 public one-way transient
│
└── TRACK F (private evidence before API)
      F0 one-layout packed-index signal
        └── [credible regime] F1 layout bake-off
               └── [select layout] F2 implement + shipment gate -> F3 SnapshotView.Freeze()
```

Frozen conversion does not depend on a transient implementation; both meet at persistent CHAMP.
The cursor does not depend on range updates. Styled text depends on both a settled cursor and a
separately designed/law-tested range-update action, so it remains later work.

The recommended execution order is:

1. Complete **P0** and lock measurement materiality before reading prototype results.
2. Begin **C0**, **T0**, and **F0** independently. C0 is the lead engineering priority; T0/F0 are
   bounded private evidence work, not public-surface commitments.
3. Take C0 through its explicit select/escalate/defer decision. When selected, implement and ship
   **C1**, then complete **C2/C3** as the first end-to-end Axis 2 vertical slice.
4. Advance T0 -> T1 -> T2 only at its two gates. A T1 failure leaves current persistent operations
   and `BulkBuilder` authoritative.
5. Advance F0 -> F1 -> F2 -> F3 only at its gates. An F0/F1 failure leaves
   `PersistentHashMap` and BCL `FrozenDictionary` as the documented choices.
6. Evaluate C4, an RRB transient, packed sorted frozen types, and any sibling-language promotion
   separately; none is implied by success of the first C# families.

Landing order follows evidence, not a ceremonial sequence number. “Cursor-first” means maintainer
attention and first intended public capability, not that a proven T2/F2 change must sit idle behind a
blocked cursor decision.

## Required Phase-Exit Evidence

Every shipping phase—C1, T2, and F2—updates the affected C# workspace overview, usage, API, and
validation documents; `docs/reference/data-structure-catalog.md`; the relevant planned row in the
frontier catalog; and `docs/reference/semantic-contracts.md`. T2 adds clean
`ToTransient().Persist()` reference identity to the no-op contract. These are phase deliverables,
not follow-up documentation debt.

| Phase | Evidence required to advance or ship |
| --- | --- |
| P0 | Contract-oracle tests green; benchmark IVT/internal diagnostics and counter skeletons build; materiality/noise rule and datasets committed before result collection |
| C0 | Zipper benchmark/counter artifact; version-state/cache design; focus/carry invariants; version-DAG potential argument; adversarial branching results; explicit select/escalate/defer decision |
| Focused-root escalation | Evidence that dirty snapshot is the blocker; ordinary Rope operation matrix over both root variants; concurrency-safe normalization; no unacceptable non-cursor regression |
| C1 | Public XML/API and current-state document set including the C0-proven branch-complexity scope; `List<T>` gap command model; boundary/overflow/representation-appropriate identity/sharing/concurrency tests; `RopeCursorBenchmarks` clearing the locked materiality gate in a predeclared named workload/cadence |
| C2 | Noncommutative measure model; exact measure-seek identity/miss/boundary and true-at-empty parity tests; delegate/struct-predicate parity; per-fragment measure-cache ceilings and failed/racing snapshot tests; existing `NewlineMeasure` text-helper compatibility; UTF-16 line O(1)/column O(log n) examples; measured-text edit/seek/line-column benchmark gate at the sample cadence |
| C3 | Editor/Tour smoke tests and transcripts; undo/branch histories distinguish edit cursor from history position; snapshot cadence in samples matches a measured workload |
| T0 | Baseline edit-locality/publication matrix and counter report naming a credible win regime or recording deferral |
| T1 | Production-representative ownership layout; ordinary-map size regression; strong-exception failpoints; base isolation; recursive count/canonicality validation; O(1) adopt/seal counters; net benchmark result |
| T2 | Public API/XML and current-state document set; public-surface strong-exception and publication-allocation failpoints; consumed-alias/post-publication invalidation tests; comparer/representative/clean-persist-identity/no-op/enumeration parity; retained-memory disclosure; explicit ship/defer record |
| F0 | One faithful packed-index result across lookup mixes, enumeration, bytes/entry, construction, and break-even; explicit advance/defer record |
| F1 | Fixed-layout comparison under identical semantics/datasets; selected one-layout rationale; no count/key selector hidden in v1 |
| F2 | Public API/XML and current-state document set; stable-unspecified order, `Freeze()` identity, exact freeze -> persistent -> freeze sequence, policy/representative/null/collision/copy/`ToPersistent`, and concurrent-read tests; API-shape check excluding update methods; library-allocation-free point lookup with enumeration allocation measured and documented separately; named-regime shipment evidence |
| F3 | Each conversion independently enumerates the same captured Ctrie generation once; later-write isolation; semantic parity with `ToPersistentHashMap().Freeze()`; conversion benchmark |

Every implementation tranche runs the C# build and unattended suite from `src/CSharp`:

```powershell
dotnet build --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
.\test.ps1
```

Performance decisions use Release BenchmarkDotNet runs from the existing benchmark project, with
the relevant filters (`RopeCursorBenchmarks`, `MeasuredRopeCursorBenchmarks`,
`TransientLifecycleBenchmarks`, or `FrozenLookupBenchmarks`) and curated results recorded in the
benchmark notes. Documentation-only
decision changes run the repository Markdown link checker, stale-path scan, and `git diff --check`.

## Cross-Language Posture

This plan inherits the repository's
[semantic contracts](../reference/semantic-contracts.md), especially reference-first behavior,
no-op identity, comparer/policy preservation, stored-representative rules, stable ordering, and
snapshot isolation. The planned types are not added to that current-state catalog until a public
surface ships. Promotion follows the
[porting and semantic-parity guide](../guides/porting-and-semantic-parity.md); this section records
planning constraints, not a claim that sibling parity already exists.

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

- [Axis 2 plan review (2026-07-13)](../reviews/axis2-lifecycle-and-cursors-review-2026-07-13.md) -
  source of the cursor-priority, frozen-signal, branched-history, and transient-workload findings
  dispositioned by this final plan.
- [Cursor-first alternative (2026-07-13)](axis2-cursor-first-alternative-2026-07-13.md) - retained
  rationale whose ordering is incorporated here with corrected sample, gate, scope, and estimate
  claims.
- [Clojure transients](https://clojure.org/reference/transients) - owner-isolated mutation and the
  one-way persistent publication precedent.
- [.NET frozen collections](https://learn.microsoft.com/en-us/dotnet/api/system.collections.frozen?view=net-10.0) - the separate, construction-heavy read-optimized tier used as a benchmark baseline.
- [Hinze and Paterson, *Finger trees: a simple general-purpose data structure*](https://www.cs.tufts.edu/comp/150FP/archive/ralf-hinze/finger-trees.pdf) - endpoint, concatenation, split, and measured-search bounds underlying the cursor analysis.
- [Stucki et al., *RRB Vector: a Practical General Purpose Immutable Sequence*](https://infoscience.epfl.ch/record/213452) - transient/vector background and the reason RRB remains a separate evaluation.
- [Repository semantic contracts](../reference/semantic-contracts.md) - current no-op, policy,
  ordering, ownership, and snapshot rules inherited by the planned surfaces.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) - reference-first workflow
  and the place to record justified language-specific complexity boundaries after C# stabilization.
