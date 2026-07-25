# Derived Structure Catalog

- Status: Historical composition survey with current disposition notes; consult the data-structure catalog for shipped surface
- Created (UTC): 2026-07-03T17:01:58Z
- Repository HEAD: ef450fdd2651ca7d1862ad8eda2f2a9ae7eda722
- Audience: Maintainers and AI agents planning new repository-owned structures or API extensions
- Scope: Adversarially verified catalog of data structures buildable on the HAMT and FingerTree
  families, the enabling API gaps they share, and consumer-driven substrate requirements

This document records what can usefully be built *on top of* the repository's two shipped families,
which repository API additions those candidates keep asking for, and the composition rules that
survived adversarial review. It complements the
[data-structure catalog](data-structure-catalog.md), which describes shipped surface only.

Several survey items have since shipped, as called out locally. Tungsten `PersistentAssociation`
was an application-specific realization of one composition idea; the later general
`PersistentOrderedMap` shipment is an independently owned neutral implementation and does not use
Tungsten as a substrate or semantic baseline.
The normative
Tungsten application-leaf dependency boundary requires an
independently owned fork for any general reuse.

The benchmark-independent implementation tranches now ship persistent one-descent HAMT updates,
`PersistentHashBag`, strict `PersistentBiMap`, neutral `PersistentOrderedSet` and
`PersistentOrderedMap`, set-valued
`PersistentHashMultimap`, bidirectional `PersistentRelation`, payload-bearing
`PersistentIntervalMap`, `PersistentOrderedMultimap`, `PersistentMapPatch`,
`PersistentDirectedGraph`, `PersistentIndexedMap`, `PersistentChunkedBitSet`, and the genuinely new
non-composite `RangeUpdateSequence` core across all nine languages. C# owns the detailed managed
contracts; siblings preserve the same logical semantics through language-local policies and
ownership idioms. Both full serialized C# Debug and Release gates pass 1,158/1,158 tests after
builds with zero warnings and zero errors. No benchmark
was run; see the
[frontier catalog](frontier-structure-catalog.md#range-update-sequence-persistent-lazy-propagation)
and the [four-surface cross-language completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md).
The [PersistentBiMap completion audit](../reviews/persistent-bimap-cross-language-completion-2026-07-15.md)
owns the strict two-domain tranche's language mappings and validation evidence.

## Provenance And Method

Findings come from two multi-agent design surveys run on 2026-07-02 and 2026-07-03:

1. A derived-structures survey: five independent ideation passes produced 48 candidates,
   consolidated to 18, each adversarially verified against the actual C# workspace sources
   (claims were checked at file/line granularity against `PersistentHashMap.cs`, `FingerTree.cs`,
   `BuiltInMeasures.cs`, `ProductMeasure.cs`, and related files).
2. A consumer case study: replacement designs for Tungsten Language `List` and `Association` for
   the Tungsten engine (`C:\Smithereens\src\Tungsten`), with the target semantics verified against
   a local Tungsten Engine 14.3 kernel and the designs verified against `Rope.cs`,
   `FingerTreeDeque.cs`, `ReversibleDeque.cs`, and `SortedDictionary.cs`.

Complexity notation: HAMT point operations are written `O(w + c)` where `w` is the trie depth
(at most 7 levels for 32-bit hashes with 32-way branching) and `c` the equal-hash collision-bucket
scan. Finger-tree bounds are the documented amortized bounds, which hold under branching
persistence via memoized suspensions.

Verdicts:

- **Strong** - clearly buildable on current APIs and genuinely useful; survived every objection.
- **Plausible** - buildable, with real caveats or moderate API gaps that shape the design.
- **Weak** - feasible but flawed, redundant, or disproportionate to its value as a family;
  usually better shipped as an API addition, sample, or documentation pattern.

## Enabling API Gaps

These gaps recurred across independent candidates and across the external consumer study. They are
the highest-leverage repository work because each unblocks several candidates at once. Every gap is
portable to the C model (callback + context pointer) unless noted.

| Surveyed gap / current disposition | Surface | What it unblocks |
| --- | --- | --- |
| Persistent `GetOrAdd` / `AddOrUpdate` — **shipped across all nine languages** | HAMT map | One hash, one trie descent, eager factory validation, and exactly one selected factory invocation support bag increments, multimap inner updates, graph edge operations, interning, and aggregation without a probe followed by `SetItem`. Hit/no-op identity, stored representatives, nullable values, and callback-failure atomicity are locked in every port. |
| Bulk construction — **shipped**; public editing sessions are a separate lifecycle | HAMT map and set | Canonical range construction stages mutable unpublished CHAMP nodes and freezes once. C# keeps that builder internal; C++ and Rust expose public construction-only builders, and TypeScript and Python expose equivalent reusable builders whose frozen snapshots are detached. Those builders are not general update sessions. OCaml adds a detached reusable staging facade whose edits remain persistent path copies and therefore makes no construction-throughput claim. |
| Structural diff / equality / set-vs-set algebra — **shipped**; 3-way merge remains consumer-gated | HAMT node layer (not composable from outside) | Equality, typed diff, and same-type algebra now ship across all nine languages. The established structural ports use reference-pruned traversal; Python and OCaml preserve the semantics through their documented checkpoint traversal. TypeScript, Python, and OCaml editing sessions expose receiver-policy set relations. A general 3-way merge still needs a conflict matrix. |
| Value-comparer parameter for no-op identity | HAMT factories | `SetItem`'s equal-value no-op check hardcodes `EqualityComparer<TValue>.Default`; a factory-supplied value comparer would let structural value equality trigger the identity short-circuit. |
| Reverse support | `Rope<T>` and `FingerTreeDeque<T>` | A reversal bit or reverse enumerator. `ReversibleDeque` exists but lacks the sorted adapter and range operations, materializes an `O(n)` array per enumeration, and its amortized bounds are documented for single-threaded linear use only - facades keep rejecting it. |
| Struct enumerator for `Rope<T>` / `MeasuredRope` | Rope family | Both use compiler-generated yield iterators; `FingerTreeDeque` already has a public struct enumerator, and the general measured tree gained one on 2026-07-01. Iteration-hot consumers (evaluators) notice the difference. |
| Sorted-search signposts on `Rope<T>` | Rope family | `SortedLowerBound`/`SortedUpperBound`/`InsertSorted` exist only on `FingerTreeDeque`; rope-backed sorted workloads fall back to `O(log^2 n)` binary search via the indexer. |

Smaller items surfaced once each: a persistent staged-tail appender for `Rope<T>` (raw `AddLast` is
already amortized `O(1)`; a bounded immutable tail is a constant-factor tune), a public
`FromChunks` on `MeasuredRope`, remove-at-rank / split-at-rank on `SortedDictionary` (rank reads
exist, rank writes do not), and a floor/ceiling lookup in the C sorted-map port (parity item).

## Candidate Catalog

Each disposition below names the languages in which a candidate has actually shipped; shipment in
one workspace does not silently imply all nine. Cross-language families carry local docs and tests
in every named port, plus benchmark evidence for complexity and allocation claims where the parity
guide requires it. That parity bill - not feasibility -
is what separates many "plausible" verdicts from "strong": thin facades are often better shipped as
API additions plus samples than as families.

### Strong

| Candidate | Composition | Key caveat |
| --- | --- | --- |
| `PersistentOrderedSet<T>` | Independently owned HAMT `item -> stamp` + persistent stamp-ordered sequence | **Shipped across all nine languages** in neutral Ordered packages. The ports own first-representative retention, explicit movement, positional ranges, stable one-shot sorting, receiver-policy algebra, sparse-label/relabel behavior, models, and tests without a Tungsten dependency or oracle. The hardened C# reference additionally locks deterministic relabel fallback and failure atomicity. |
| `PersistentOrderedMap<TKey, TValue>` | Independently owned HAMT keyed index + neutral persistent ordered sequence | **Shipped across all nine languages.** The general map retains first key representatives, keeps payload replacement position-stable, separates explicit movement from setting, and owns its contract, model, invariants, and tests without a Tungsten dependency or oracle. Ports may store entries in both indexes or keep only labels in the HAMT; that representation choice is not public semantics. |
| HAMT structural diff / merge / set algebra | Feature inside the Hamt family node layer, phased: (1) `MapEquals` + `Diff` enumerator, (2) structural set-vs-set ops, (3) 3-way `Merge` with a specified conflict matrix | Bound is `O(divergent region)` and history-dependent, not content-diff-dependent; collision buckets are insertion-ordered so equal buckets need key-matched (unordered) comparison; comparer mismatch must be gated by reference equality on the comparer. |
| `PersistentHashBag<T>` | Facade over HAMT `T -> int` + cached wide total count | **Shipped across all nine languages** with checked positive per-class multiplicities, separate distinct/total cardinalities, first-representative retention, eager receiver-policy normalization, conventional multiset algebra, and expanded/distinct enumeration. TypeScript uses `bigint`, Python uses an unbounded `int`, and OCaml uses a checked native `int` total while retaining the shared positive per-class contract. |
| `PersistentBiMap<TKey, TValue>` | Forward `K -> V` + inverse `V -> K` HAMTs behind a bijection-enforcing facade | **Shipped across all nine languages.** The strict contract retains independent policies, rejects either occupied domain, performs configured-value-policy no-op checks before replacement, removes and reinserts both directions when changing a pair, and constructs O(1) inverse facades over the same roots. Honest 2x memory: every pair is stored in both tries. |

The shipped `PersistentOrderedSet` and `PersistentOrderedMap` address ordered unique membership and
ordered key/value lookup respectively. The generic map owns a representation-neutral contract;
the Tungsten case study independently specialized the broad composition idea for `Association`.
*Application-specific shipment 2026-07-07*: the Tungsten workspaces own a values-in-both
`PersistentAssociation` (plus the `PersistentList` sequence facade), with the C# workspace
(`Durable7.Tungsten`) as the semantic
reference only for C, C++, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml Tungsten ports linked from the
[data-structure catalog](data-structure-catalog.md#tungsten-application-collections). This did not
ship the generic ordered-map candidate at that time; the later neutral shipment did. Other
unmarked candidates below remain unshipped. The
structural diff feature is the one candidate that cannot be built by composition - the node layer
is internal - and the one that upgrades the most other candidates from "store versions" to "reason
about versions".

### Plausible

| Candidate | Composition | Key caveat |
| --- | --- | --- |
| `AddressablePriorityQueue<TKey, TPriority>` | HAMT `key -> (stamp, priority)` + `SortedSet<(priority, stamp, key)>` | The plain composition dominates a bespoke `ProductMeasure` design on every op except `Enqueue`; `TrySplitFind` + `Concat` already excises located elements, so no new core API is needed. Its use cases cover the delete-by-handle timer/interval niche. |
| `PersistentHashMultimap<TKey, TValue>` (set-valued) | HAMT `K -> PersistentHashSet<V>` | **Shipped across all nine languages**, together with a bidirectional `PersistentRelation` that owns exact forward/reverse multimap indexes. Whole-multimap no-op identity composes from the nested contracts. The deque-valued (event-stream) variant remains weaker: every append pays the outer update walk and adds a cross-family dependency. |
| `VersionedKvStore<TKey, TValue>` | `SortedDictionary<revision, HAMT>` snapshot index + optional finger-tree journal with checkpoints | Decompose into two small layered types, not one modal store. Per-key temporal queries are `O(journal)` without an opt-in `key -> revision-list` secondary index, which doubles write cost. |
| `OverlayMap` / layered config | Base HAMT + small overlay HAMT with tombstones | `O(1)` effective `Count` and overlay-only writes are mutually exclusive; effective enumeration pays a per-key suppression probe. |
| `IndexedHashSet<T>` / `WeightedKeyedSampler<TKey>` | HAMT `key -> stamp` + size x sum x key product-measured tree | One parameterized family (uniform = weight 1). `TryLocate` already returns the full product measure, so cross-component projection is a convenience extension, not a core change. Weighted variant needs the size component for rank ops and non-negative-weight enforcement. |
| `PersistentHashGraph<TVertex, TEdgeData>` | HAMT `V -> (out-edge HAMT, in-neighbor set)` + cached edge count | Reverse index is mandatory or `RemoveVertex` is `O(E)`. Version diff/merge degrade to `O(n + m)` until structural diff lands. Nested no-op identity composes to graph-level instance identity - test-lock it. |
| Persistent union-find | HAMT `T -> (parent, rank)` with opt-in compress-on-write | Inverse-Ackermann bound holds only along linear threaded histories; branching workloads see `Theta(log n)` hops per find. Honest contract required. |
| `InternPool<T>` / scoped symbol table | `GetOrAdd` on the hash set + snapshot-stack scoping | Right-sized as an API addition plus a sample, not a family. `TryGetValue` canonical recovery was visibly designed for interning. Strong-reference retention caps it at arena interning; epoch rotation is `O(n)`. |
| `PersistentTable<TKey, TRow>` | HAMT primary + `SortedDictionary` unique indexes + custom-measured non-unique indexes | Row-count-in-range needs a custom `(entryCount, rowCount, lastKey)` measure (stock rank counts distinct column values). Stage as a single-language reference composition before committing to parity. |
| `PersistentWorkspace` (trie-of-HAMTs VFS) | Directory nodes as HAMT `name -> node`, files as `Rope<byte>` | The genuine differentiator is `O(depth)` subtree rename; a flat sorted map also snapshots in `O(1)` and gives prefix ranges free. Node-value `ReferenceEquals` pruning already enables an `O(changed paths)` diff. |
| `MerkleHamt<TKey, TValue>` | Node-type fork with per-node memoized digest (CAS-published, finger-tree precedent) | Largest-effort candidate. Requires a pinned deterministic key hash (default .NET string hashing is per-process randomized - fatal for cross-process addressing), an encoder constant on comparer-equality classes, and a serialization story the repository lacks. Defer until structural diff and serialization exist. |

The later `PersistentIntervalMap` was selected from the broader planning work rather than this
original composition survey. It now ships across all nine languages as an exact interval-key map
plus an augmented overlap-search index; consult the
[shipped catalog](data-structure-catalog.md#derived-persistent-maps-relations-and-sparse-bit-sets) rather than
retrofitting its contract into this historical candidate ranking.

### Weak

| Candidate | Why it stays weak |
| --- | --- |
| `GetIn`/`UpdateIn` path ops + map cursor | Buildable as extension methods, but `HAMT<string, object>` nesting fights static typing, and the C++ analog (`std::any`) has no default equality, breaking the no-op identity the feature depends on. Ship as a recipe; the production answer is a typed variant-node document type. |
| `Atom<T>` (CAS root swap) | Not a data structure - a mutable cell the repository's design constraints exclude. Trivial in C#; the C port is an unsolved memory-reclamation problem (reader refcount increments race writer swap+destroy; needs hazard pointers or epochs). Ship as a usage-guide pattern. |
| `SnapshotCache` (persistent bounded LRU/TTL) | Lazy-tombstone amortization breaks under branching persistence (re-branching from a pre-compaction version repays `O(n)` per branch); persistent LRU self-cancels because touch-on-read allocates, so read-heavy workloads want TTL/FIFO, at which point the LRU machinery is dead weight. |

A sparse bitset (HAMT `int -> ulong` chunks + popcount-sum-measured chunk tree for rank/select)
came up in follow-up discussion rather than the verified surveys; it half-fails the load-bearing
test below - dense integer keys are the HAMT's least differentiated case, and the measured chunk
tree alone answers membership at `O(log chunks)`. If pursued, prototype the tree-only form first.

## Composition Design Rules

Cross-cutting findings that adversarial review kept re-deriving:

1. **Stable-stamp discipline.** Never store finger-tree positions or ranks in the HAMT - positions
   shift. Use monotone stamps as the rendezvous key between the hash side and the order side.
   Strict excision (`TrySplitFind` + `Concat`, `O(log n)`) stays honest under branching
   persistence; lazy stale-entry skipping does not (its compaction amortization assumes linear
   histories).
2. **Amortization vs branching persistence.** The core tree bounds hold under branching via
   memoized suspensions. App-level amortizations layered on top - staged append tails,
   gapped-label relabeling, compaction thresholds - do not automatically inherit that robustness.
   Document linear-history bounds honestly and give the worst case.
3. **Values-in-both for dual-access structures.** When both `by-key` and `by-position` reads are
   hot (caches with recency order, Tungsten-style associations where `Keys`/`Values`/`Normal`
   dominate), storing one shared entry in both indexes buys allocation-free key reads and hash-free
   ordered reads, at the price of updates touching both structures. A label-only HAMT avoids
   duplicated payload storage but makes keyed lookup locate the ordered entry. The shipped generic
   `PersistentOrderedMap` contract permits either language-local representation and tests the
   logical dual-index invariant instead of prescribing storage placement.
4. **The load-bearing test.** Reject a candidate when a sorted finger tree alone suffices (keys
   already ordered, no hashed lookup on the hot path) or when keys are dense integers. The HAMT
   earns its place through near-constant persistent keyed lookup over arbitrary hashable keys.
5. **Measure placement.** A monoid measure taxes every structural operation that rebuilds a
   boundary chunk or node - split-heavy workloads pay measure refolds on transients that never
   read the measure. Derived data that is only read at identity points (structural hashes, counts
   for equality) can instead be memoized on the facade or element wrapper. Benchmark both
   placements before committing.
6. **No-op identity composes.** Equal-value `SetItem` returning the same instance propagates
   through nested structures (inner map unchanged implies outer map unchanged) and gives consumers
   a reference-equality "nothing changed" signal. Facades should preserve and test-lock this
   property; it currently depends on default value equality (see the value-comparer gap).
7. **Parity economics decide tier, not feasibility.** Feasibility was almost never the blocker.
   When a candidate is a thin facade, prefer an API addition on the existing family plus a sample
   over a new four-language family.

## Consumer Case Study: Tungsten

This section records application requirements and algorithmic provenance. It does not define a
general collection contract. Tungsten may consume general structures, but a general structure must
not depend on Tungsten code or behavior; fork any reusable mechanism independently under the
application-leaf boundary.

The Tungsten engine (`C:\Smithereens\src\Tungsten`, a kernel-free Tungsten Language engine)
provided an external requirements source: replacement designs for `List` (the argument sequence of
every expression) and `Association` (an insertion-ordered map with both key and positional
access).

Both map onto this repository's structures directly - `List` onto a size-adaptive
array / `Rope<Expr>` / packed `Rope<double>` tiering, `Association` onto the
`PersistentOrderedMap` pattern with a stamp-sorted `FingerTreeDeque` and values in both
structures - and in most operations beat the reference Tungsten implementation asymptotically
(append loops, functional single-element replacement, slicing, `O(1)` reverse). The study
kernel-verified the target semantics against Tungsten Engine 14.3 and adversarially verified the
designs against this repository's sources; its corrections are folded into the gaps and rules
above (measure-refold costs, reversal-vs-directional-hash soundness, staged-tail amortization
limits, `ReversibleDeque` rejection reasons).

The full study, including the operation-by-operation mapping tables and the kernel-verified
Tungsten semantics, lives in the Smithereens repository at
`src/Tungsten/docs/reports/2026-07-03-list-association-persistent-backends.md`.

The library-side outcome shipped on 2026-07-07 as the
C# Tungsten-collections workspace: the
`Association` composite as specified here (stamp-sorted deque + HAMT, values in both, gapped
labels with honest relabel contract), and the `List` facade over the finger-tree deque without
the engine-level small/packed tiers (those stay behind the client's expression surface).

## Relationship To Other Documents

- [Data structure catalog](data-structure-catalog.md) - shipped families only; when a candidate
  from this document ships, move its authoritative description there.
- [Frontier structure catalog](frontier-structure-catalog.md) - independently implemented cores
  rather than compositions, including the shipped C# `RangeUpdateSequence` reference and its
  remaining seven-language parity bill.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) - the parity workflow a
  shipped candidate must satisfy.
- Workspace improvement proposals (for example the C# FingerTree enumerator-allocation and
  rope-storage proposals under `src/CSharp/docs/FingerTree/`) - single-workspace API gaps listed
  here should graduate into such proposals when scheduled.
