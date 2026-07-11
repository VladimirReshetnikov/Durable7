# Frontier Structure Catalog

- Status: Candidate catalog - nothing in this document is shipped surface
- Created (UTC): 2026-07-11T03:31:23Z
- Repository HEAD: f40e301e8faf26d748f33d8546d7d9216657301e
- Audience: Maintainers and AI agents planning new repository-owned cores, representation tiers, and specialized sibling collections
- Scope: Surveyed candidates beyond composition of the shipped families: new structure cores (including recent inventions), hybrid/adaptive representations, and niche-specialized collections

This document catalogs candidate work that the [derived structure catalog](derived-structure-catalog.md)
deliberately does not cover. That catalog records what can be built *by composing* the shipped HAMT
and FingerTree families; this one records three complementary axes:

1. **New cores** - structures that need their own node layer, including several invented or refined
   in the last decade.
2. **Hybrid / adaptive representations** - collections that switch internal representation by size,
   lifecycle phase, or (with sharp limits) usage pattern.
3. **Niche specializations** - opt-in sibling types that trade a constant factor for a specific
   workload, in the mold of the shipped `ReversibleDeque<T>` (O(1) reverse at ~2x forward-path cost).

## Provenance And Method

Findings come from a single-pass design survey conducted 2026-07-10/11, grounded against the shipped
C# workspaces ([HAMT](../../src/CSharp/docs/Hamt/overview.md),
[FingerTree](../../src/CSharp/docs/FingerTree/overview.md),
[Tungsten](../../src/CSharp/docs/Tungsten/overview.md), and the
[measured benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md)) and against the derived
structure catalog's verified composition rules. Unlike the derived catalog, the candidates here have
**not** been adversarially verified by independent passes, and the literature references come from
assistant knowledge (cutoff 2026-01) rather than re-read primary sources. Before implementing any
candidate, verify the cited paper's actual claims and bounds; the [references](#references) section
lists what to pull.

**Division of labor with the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md).** That proposal selects a
committed, prioritized slate from the derived catalog plus review-observed gaps; this catalog maps
the *frontier* candidate space beyond it. Three items overlap - the Patricia trie family
(proposal Tier C1), the cursor/zipper (proposal A3), and the RRB vector (considered and deferred
there on the same benchmark-first grounds as here). For those, the proposal's scheduling governs
and the entries below add design detail; everything else in this catalog is new relative to both
documents.

Verdicts use the derived catalog's scale:

- **Strong** - clearly worth building; survived every objection raised in this survey.
- **Plausible** - worth building under conditions named in the caveat (usually a benchmark gate or a
  prerequisite feature).
- **Weak / Reject** - feasible but flawed, redundant, or disproportionate; the better alternative is
  named.

Complexity notation follows the derived catalog: HAMT point operations are `O(w + c)` (trie depth at
most 7 for 32-bit hashes with 5-bit levels, plus collision-bucket scan); finger-tree bounds are the
documented amortized bounds, persistence-robust via memoized suspensions.

## Summary Matrix

| Candidate | Axis | Verdict | Depends on | Rough size |
| --- | --- | --- | --- | --- |
| CHAMP canonicalization + structural equality/diff | 1 | Strong | Evaluate with proposal item A2 (HAMT diff) | Node-layer rewrite + 2 public ops + equality benchmark suite |
| `PersistentIntMap` / `PersistentIntSet` (Patricia) | 1 | Strong (= proposal Tier C1) | - (new self-contained core) | 1 new core, ~2 public types, ~30 members, structural set ops |
| DABA Lite sliding-window aggregator | 1, 3 | Strong | Reuses `IMonoid<T>` | 1 small type, ~8 members |
| Merkle search tree | 1 | Strong direction, gated | Deterministic hashing + serialization story | Largest single item in this catalog |
| RRB vector | 1 | Plausible (benchmark-gated; deferred by proposal) | Benchmark vs `Rope<T>` random access | 1 new core, transient tier |
| Zip tree (canonical sorted set) | 1, 3 | Plausible | Keyed hash for rank derivation | 1 new core, ~2 facades |
| Brodal-Okasaki heap | 1 | Plausible (real-time niche) | - | 1 new core, small surface |
| Priority search queue (Hinze) | 1 | Plausible | Only if `AddressablePriorityQueue` composition constants disappoint | 1 new core |
| Ctrie (concurrent, O(1) snapshot) | 1 | C# + Kotlin/JVM only | Tracing GC; native ports require reclamation design | 1 new core, concurrency test tier |
| Hollow heap / strict Fibonacci heap | 1 | Reject | - | Decrease-key via mutation fights persistence; PSQ covers the niche |
| Size-tiered small representations | 2 | Strong | Benchmark gate at tier boundary | Internal tier per facade + representation-forcing tests |
| `Freeze()` read-optimized tier | 2 | Strong | Optional: fuse filter, PGM for sorted | 1 frozen type per family + strategy selection |
| Cursor / zipper over rope and deque | 2, 3 | Strong (= proposal A3) | - | 1 cursor type per sequence family |
| Key-type-specialized map factories | 2 | Plausible | `PersistentIntMap`, optionally ART | Factory layer, no new core beyond the above |
| Self-adjusting (splay-style) structures | 2 | Reject | - | Reads allocate under path copying; cursors + freeze substitute |
| Range-update sequence (lazy propagation) | 3 | Strong | Measure action interface | 1 sibling core + tag algebra + property tests |
| Order-maintenance list | 3 | Plausible | - | 1 public type; Tungsten stamps could layer on it |
| Persistent chunked bitset | 3 | Plausible | - (tree-only form per derived catalog follow-up) | 1 facade over measured tree |
| Styled-text rope | 3 | Sample, not family | Range-update sequence (or interval runs) | Composition sample + docs |
| Kaplan-Tarjan real-time deque | 3 | Reject | - | Document the memoized deque's spike profile instead |

## Axis 1: New Cores

### CHAMP canonicalization (upgrade to the shipped HAMT)

**C# status (2026-07-11): Implemented.** The managed HAMT uses separate data/node bitmaps, inline
payload runs, deletion-time leaf promotion, canonical one-freeze bulk construction, lockstep
reference-pruned `MapEquals`, and bitmap-aligned structural `Diff`. Its executable gate covers every
leaf/collision/branch transition, eager argument validation, stored-key representatives, randomized
node invariants, independent-history topology, reference-pruning bounds, and dedicated benchmarks.

**Kotlin status (2026-07-10): Implemented.** The JVM port now has the same split bitmap/inline
payload representation and canonical deletion promotion, plus policy-compatible `mapEquals` and
typed `diff`. Its executable suite checks independent insertion histories, CHAMP node invariants,
collisions, persistence, and concurrent readers.

**Rust status (2026-07-10): Implemented.** Both persistent path copying and `BulkBuilder` freezing
now produce split data/node maps with inline payload runs and canonical deletion promotion. The safe
`Arc` implementation adds owned typed diff and representation-invariant coverage.

**Haskell status (2026-07-10): Implemented.** `HashMap` now stores strict split data/node maps,
inline `(hash,key,value)` payload runs, child-only subtrie runs, and deletion promotion. Its
dependency-free API adds `mapEquals` and typed `MapDifference` values, with `validStructure`
checking the CHAMP invariants. Native-port status follows below.

**C++ status (2026-07-10): Implemented.** The header-first C++20 map and its move-only bulk builder
now use split maps, compact inline payload vectors, child-only shared subtries, and canonical removal
promotion. `map_equals` and owned typed `map_difference` results round out the map surface.

**C status (2026-07-10): Implemented.** The C17 core stores split maps, inline type-erased payloads,
and child-only flexible-array runs while preserving retain/release policy balance and allocation-
failure rollback. Visitor-based typed diff avoids imposing an allocator on callers. CHAMP is now
implemented across all six repository languages.

**What it is.** CHAMP (Compressed Hash-Array Mapped Prefix-tree; Steindorfer & Vinju, OOPSLA 2015)
is a refinement of Bagwell's HAMT with two changes that matter here:

1. **Bitmap split.** Each node keeps two bitmaps - a `datamap` for inline key/value payloads and a
   `nodemap` for sub-node pointers - with payloads and sub-nodes stored in separate compact regions
   of one array. Iteration touches payload runs contiguously instead of interleaving pointers and
   payloads, and the node layout removes a class of indirection on lookup.
2. **Canonical deletion topology.** A sub-node that shrinks to a single payload is collapsed into
   its parent, and a child-only wrapper around a leaf or collision run is removed. Bitmap topology is
   therefore determined by the remaining hashes rather than update history. Unary bitmap chains can
   still be necessary when multiple hashes share successive 5-bit prefixes. Collision-run order and
   first-retained stored key representatives remain history-dependent, so "canonical" here means
   canonical trie topology modulo those semantic-equivalence details, not bit-identical object graphs.

**Why it matters here.** Canonical topology lets equality and diff align logical bitmap slots without
whole-map lookup passes. For versions with shared ancestry, reference-equal descendants are skipped,
so work tracks non-shared trie regions plus reported differences. Independently built equal maps
still require O(n) comparison: identical topology is not reference identity. A future trusted
per-node digest or hash-consing layer could change that bound, but CHAMP alone does not. Equal-hash
collision runs require unordered key matching and can take O(c²) comparisons for bucket size c.

**Migration path.** The shipped `PersistentHashMap` is already a bitmap-indexed HAMT with compact
child arrays, collision buckets, struct enumerators, and an internal transient builder - the delta
is the two-bitmap node layout, the canonical-deletion path, and the equality/diff surface. The
enumeration order of an unordered map may change across this migration; the documented contract
already declares enumeration order unspecified, but the test suites and downstream Tungsten
association tests must not accidentally pin the old order.

**Caveats.** Collision buckets remain insertion-ordered, so equality/diff use key-matched unordered
comparison. Equivalent maps can also retain different concrete key objects. Enumeration order is
unspecified, deletion adds a collapse check, and independently allocated maps cannot benefit from
reference pruning. The managed benchmark suite separates shared-single-change diff from
independent-history equality/diff so those two cost profiles stay visible.

**Verdict: Strong.** It compounds with the diff/merge feature already ranked first, and it is an
upgrade in place rather than a new family. Schedule it together with the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md)'s item A2 (HAMT structural
diff/equality): doing A2 phase 1 on a canonicalized node layer gets the guaranteed O(divergence)
bound in one pass instead of retrofitting it later.

### `PersistentIntMap<TValue>` / `PersistentIntSet` (big-endian Patricia trie)

**C# status (2026-07-10): Implemented for both widths.** `PersistentIntMap<TValue>` /
`PersistentIntSet` and `PersistentLongMap<TValue>` / `PersistentLongSet` share a static-policy
big-endian Patricia engine, enumerate in signed order, preserve no-op identity, and provide
prefix-aware structural union/intersection/difference plus map combining overloads.

**Kotlin status (2026-07-10): Implemented for both widths.** `PersistentIntMap`/`Set` and
`PersistentLongMap`/`Set` share an immutable big-endian Patricia engine with sign-bit transforms,
ascending signed iteration, path compression, cached subtree counts, no-op identity, and
prefix-aligned structural union/intersection/difference with map combining overloads. Boundary and
randomized model tests cover both key widths.

**Rust status (2026-07-10): Implemented for both widths.** `PersistentIntMap`/`Set` and
`PersistentLongMap`/`Set` use safe `Arc`-shared compressed-prefix nodes, sign-flipped ordering,
cached subtree cardinalities, and prefix-aligned structural algebra with key/left/right combining
forms. Tests compare randomized histories with `BTreeMap` and cover signed boundaries, fixed and
combining algebra, invariant counts, and no-op root sharing.

**Haskell status (2026-07-11): Implemented for explicit `Int32` and `Int64` widths.**
`Data.Structures.Hamt.Patricia` exposes strict maps and sets over one `Word64` path core, sign-bit
ordering, compressed prefixes, cached subtree counts, keyed/unkeyed combining algebra, and an
invariant validator. The explicit aliases avoid making the width contract depend on platform-sized
`Int`.

**C++ status (2026-07-11): Implemented for both widths.** `persistent_int_map` / `set` and
`persistent_long_map` / `set` use immutable `shared_ptr` nodes with compressed prefixes, cached
subtree counts, signed-order traversal, prefix-aligned fixed and resolver-combining algebra, and
root-preserving semantic no-ops.

**C status (2026-07-11): Implemented for both widths.** The type-erased `tds_int_map` / `set` and
`tds_long_map` / `set` share a reference-counted C17 core with explicit value ownership policy,
cached subtree counts, signed-order visitors, prefix-aware structural algebra, typed combining
callbacks, alias-safe updates, and deterministic model/lifetime tests.

**What it is.** Okasaki & Gill's mergeable integer maps (1998): a binary trie over the bits of an
integer key, path-compressed so each internal node stores a prefix and the single branching bit,
branching on the highest bit where keys differ. Operations are O(min(n, W)) with W the key width;
in practice a handful of node hops.

**Why it matters here.** The derived catalog's "load-bearing test" (rule 4) explicitly names dense
integer keys as the HAMT's least differentiated case. The Patricia trie fills that hole with a
property the HAMT cannot match: **structural merge**. `Union`, `Intersect`, and `Except` recurse on
prefix structure and short-circuit whole subtrees by reference equality, running in O(shared
structure) - typically far below O(n + m) - and returning reference-equal results for no-op merges
(the family's no-op identity contract extends naturally). Merge-heavy workloads (version-set
algebra, sparse indices, ID-set reconciliation) get structurally what the HAMT needs a bespoke diff
layer to approximate. Enumeration is in unsigned key order for free; a sign-flip key transform
yields signed order.

**Design notes.**

- Ship both 32- and 64-bit key widths; C# generic math (`IBinaryInteger<TKey>`) can unify them, but
  two concrete key types keep the hot path free of abstraction.
- Combining-function overloads (`Union(other, (k, l, r) => ...)`) are the API that makes merge
  useful; mirror Haskell `Data.IntMap`'s `unionWith`/`intersectionWith`/`mergeA` vocabulary,
  trimmed to this repository's style.
- Collision buckets do not exist (keys are their own hashes); the structure is strictly simpler
  than the HAMT.

**Verdict: Strong.** Self-contained, port-friendly, 25+ years of production precedent
(`Data.IntMap`), and it covers the acknowledged weak spot of the shipped family. Already selected
as the [2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md)'s Tier C1 (its one
structurally new family); this entry supplies the design detail - structural-merge API vocabulary,
key-width strategy, no-op merge identity - for that scheduled work.

### RRB vector

**C# status (2026-07-11): Implemented and representation-hardened.** `RrbVector<T>` ships with
32-element leaves, radix-indexed regular 32-way branches that allocate no size table, relaxed
branches with cumulative sizes, O(log32 n) indexing/path-copying updates, exact-boundary-sharing
split/edit operations, and O(log32(n + m)) boundary-spine concatenation. Its append-only builder
freezes full leaves safely and caches immutable snapshots. Internal validation and adversarial tests
bound density/height drift; the benchmark gate compares indexing and concat with both `Rope<T>` and
`ImmutableList<T>`. A dedicated persistent tail remains deliberately unimplemented, so immutable
endpoint append is still a boundary-spine operation rather than worst-case O(1).

**Haskell status (2026-07-11): Implemented with an idiomatic pure construction tier.**
`Data.Structures.FingerTree.RrbVector` uses boxed immutable arrays, radix-indexed regular branches,
relaxed cumulative sizes, structural split/append/range edits, cached metadata validation, and
root-sharing diagnostics. `fromList` performs bottom-up bulk construction; no public mutable
transient is exposed because an `ST`-style implementation detail would not be an idiomatic public
Haskell value.

**What it is.** The relaxed radix-balanced tree (Bagwell & Rompf 2011; practical treatment with
transients in Stucki et al., ICFP 2015): a 32-way branching persistent vector where nodes are
normally full (radix-indexable in O(log32 n) with pure bit arithmetic) and carry small size tables
only where concatenation introduced irregularity. This is the persistent vector of Scala 2.13,
Clojure (`core.rrb-vector`), C++ `immer`, and Rust `im`.

**Why it matters here - and why it might not.** The shipped sequence story is
`FingerTreeDeque<T>` (O(1) amortized endpoints, O(log distance-from-nearer-end) indexing - the
measured U-shape) plus `Rope<T>` (chunked leaves, cache-dense, O(log n) edits). The RRB vector's
differentiator is **uniform effectively-O(1) random access** with no U-shape, at the price of
endpoint operations that are merely O(log32 n) amortized-O(1)-ish (tail optimization) rather than
the deque's O(1), and concatenation that is O(log n) with worse constants than the deque's
O(log min).

| Workload | `FingerTreeDeque<T>` | `Rope<T>` | RRB vector |
| --- | --- | --- | --- |
| Endpoint push/pop | O(1) amortized (best) | O(log n), amortized O(1) staged | O(log32 n), O(1) with tail |
| Random index, middle | O(log n), ~310 ns at 100k | O(log n) chunked | O(log32 n), small constant (best) |
| Concat | O(log min) (best) | O(log min) | O(log n), relaxed nodes |
| Cache density | pointer-heavy | chunked (best for scans) | 32-way arrays (good) |
| Bulk build | element-wise | chunk-staged builder (best) | transient tier |

**Verdict: Plausible, benchmark-gated.** Before building it, add a random-access-heavy workload to
the benchmark harness and measure `Rope<T>` against `ImmutableList` there. If the rope's mid-buffer
indexing is the pain point in a real consumer, the RRB vector is the proven answer; otherwise it is
redundant with the two shipped sequences. The
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md) reached the same deferral
independently ("RRB's win is constant factors, which the repository's design notes treat as a
benchmark-first question").

### Merkle search trees / Prolly trees

**C# status (2026-07-10): Implemented with the deterministic-hashing and serialization gates.**
`MerkleSearchTree<TKey, TValue>` uses explicit versioned `IMerkleCodec<T>` policies, domain-separated
SHA-256 geometric key layers, canonical length-framed node serialization, cross-policy fingerprinting,
ordered ranges, content equality, verified equality, and digest-pruned typed diff. Built-in codecs
cover integers, strings, byte arrays, and GUIDs; arbitrary types must supply canonical encodings.

**What they are.** Two convergent designs for *uniquely represented, content-addressed* search
trees. Merkle search trees (Auvolat & Taïani, SRDS 2019) place each key at a layer derived from its
hash (geometric distribution), producing a deterministic B-tree-like shape independent of insertion
order. Prolly trees (probabilistic B-trees; Noms ~2016, now Dolt) reach the same goal by
content-defined chunking: node boundaries fall where a rolling hash of the entry stream crosses a
threshold. Every node carries a digest of its contents; the root digest names the whole collection.

**What they buy.** O(divergence) diff, sync, and three-way merge **across processes and machines**,
not merely across versions sharing pointer ancestry - the property behind AT Protocol repositories
(Bluesky) and Dolt's versioned SQL tables. Unique representation also gives history independence
(the structure provably leaks nothing about insertion order) and digest-based equality.

**Relationship to the derived catalog.** The catalog's `MerkleHamt` (deferred) hashes an
*unordered* trie; the MST is the *ordered* sibling and subsumes several of its uses while adding
range queries and range sync. The same prerequisites bind both, and they are the real work:

1. **Pinned deterministic hashing.** Default .NET string hashing is per-process randomized - fatal
   for cross-process addressing. The family needs an explicit, versioned hash policy (e.g.
   xxHash64/BLAKE3 over a canonical key encoding), with the derived catalog's warning about
   comparer-equality classes needing an encoder constant.
2. **Canonical serialization.** A digest is only as canonical as the byte encoding it hashes. The
   repository currently has no serialization story; the MST forces one.

**Verdict: Strong direction, correctly gated.** Do not start it before deterministic hashing and a
serialization layer exist; once they do, prefer the MST over `MerkleHamt` as the first
content-addressed family.

### Zip trees and zip-zip trees (canonical sorted core)

**C# status (2026-07-10): Implemented.** `CanonicalSortedSet<T>` and
`ZipTreeRankPolicy<T>` provide keyed two-part zip-zip ranks, history-independent shape, persistent
unzip/zip updates, memoized subtree digests, canonical lockstep equality, policy-gated algebra, and
explicit adversarial-rank degradation diagnostics.

**What they are.** Zip trees (Tarjan, Levy & Timmel, 2018/2019) are randomized-rank binary search
trees - a reformulation of treaps where insertion and deletion are single root-to-position *unzip*
/ *zip* passes with O(1) expected restructuring, no rotations. Zip-zip trees (Gila, Goodrich &
Tarjan, 2023) refine the rank distribution to bring expected depth near the information-theoretic
optimum while keeping O(log log n) rank bits.

**Why they matter here.** Deriving each key's rank from a keyed hash of the key makes the tree
**uniquely represented**: same contents, same shape, regardless of history. That yields the
lightweight (no-serialization-needed) version of the canonical-form benefits: memoized subtree
digests give O(1) inequality and O(min divergence) equality, and history independence is a real
security niche (structure cannot leak access or insertion patterns - relevant wherever a persistent
snapshot crosses a trust boundary). Path copying is unusually cheap because updates restructure
O(1) expected nodes.

**Caveats.** All bounds are expected-case; an adversary who can choose keys *and* knows the hash
seed degrades it, so the rank hash must be keyed. The shipped sorted finger tree already covers
ordinary sorted workloads with worst-case bounds and the measure framework; the zip tree earns its
place only where equality/memoization dominates.

**Verdict: Plausible.** Frame it as `CanonicalSortedSet<T>` - a niche sibling (axis 3 framing)
whose selling point is unique representation, not general sorted-set duty.

### Heaps: Brodal-Okasaki yes, hollow/strict-Fibonacci no, PSQ maybe

**C# status (2026-07-10): Brodal–Okasaki and PSQ implemented; mutation-dependent heaps rejected as
specified.** `BrodalOkasakiHeap<T>` directly ports the bootstrapped skew-binomial representation
with bounded-comparison O(1) insert/meld/minimum and O(log n) delete-min.
`PrioritySearchQueue<TKey, TPriority, TValue>` supplies keyed priority updates and the distinctive
key-range/priority-threshold query over one winner-cached AVL core. Strict Fibonacci and hollow
heaps remain explicit non-goals.

- **Brodal-Okasaki heap** (JFP 1996; skew binomial queues + data-structural bootstrapping): purely
  functional with O(1) *worst-case* insert, meld, and findMin, O(log n) deleteMin. The shipped
  finger-tree `PriorityQueue` has the same bounds amortized (meld O(log min)). The niche is
  worst-case latency guarantees. **Plausible** - small, well-documented, pedagogically clean; build
  it when a latency-sensitive consumer appears, and benchmark constants against the shipped queue
  first.
- **Strict Fibonacci heaps** (Brodal, Lagogiannis & Tarjan, STOC 2012) and **hollow heaps**
  (Hansen, Kaplan, Tarjan & Zwick, ICALP 2015) achieve optimal decrease-key bounds through
  aggressive pointer mutation. Persistent path-copying destroys exactly the surgery they rely on.
  **Reject** for this repository.
- **Priority search queues** (Hinze, ICFP 2001; Haskell `psqueues`): a purely functional structure
  that is simultaneously a search tree on keys and a heap on priorities - keyed decrease-key and
  delete in O(log n), min access in O(1)/O(log n), plus the distinctive query "all keys in range
  with priority at most p" in O(r + k). The derived catalog already rates the composition
  alternative (`AddressablePriorityQueue` = HAMT + `SortedSet<(priority, stamp, key)>`) plausible
  and notes it dominates a bespoke design on most operations. **Plausible, second in line:** build
  the composition first; reach for the PSQ core only if its constants (two structures touched per
  update) disappoint or the range-bounded priority query becomes a real requirement.

### DABA Lite sliding-window aggregator

**C# status (2026-07-10): Implemented.** `DabaLite<T, TMonoid>` reuses `IMonoid<T>`, implements
the paper's six-cursor incremental-reversal schedule over a worst-case-O(1) chunked queue, and has
instrumented tests locking down the three/two/one combine ceilings for insert/evict/query.

**What it is.** De-Amortized Banker's Aggregator (Tangwongsan, Hirzel & Schneider; DEBS 2015,
VLDB J 2021 for the Lite variant): FIFO sliding-window aggregation over any monoid with O(1)
*worst-case* time per insert/evict/query and two words of overhead. The finger tree answers the
same query in O(log n); DABA Lite removes the log for the FIFO-window special case.

**Why it fits.** It reuses the family's `IMonoid<TMeasure>` vocabulary verbatim - the aggregator is
generic over the same static-abstract measure operations the trees use, so `SumMeasure`,
`MaxMeasure`, `MinMeasure`, and any user monoid work unchanged. The type is small (two logical
stacks in one buffer plus running aggregates), easy to test exhaustively against a naive
re-aggregation model, and genuinely recent.

**Caveats.** It is ephemeral (a mutable window), not persistent - which is fine, because the
window itself is transient state; document it as the family's first deliberately mutable member, or
ship a persistent facade over `FingerTreeDeque` with the DABA fast path inside a builder.

**Verdict: Strong (small).** High polish-to-effort ratio; also serves as the measure framework's
first consumer outside the trees.

### Ctrie (concurrent hash trie with O(1) snapshots)

**C# status (2026-07-11): Implemented with node GCAS, root/main RDCSS, and tomb cleanup.**
`ConcurrentHashTrie<TKey, TValue>` provides lock-free mutable operations over bitmap C-nodes,
singleton leaves, true equal-hash collision nodes, and empty/singleton tomb nodes. Readers help
node-local GCAS; `Snapshot()` uses a GCAS-priority root/main RDCSS transition so a writer cannot be
lost between main observation and root publication. Later writers lazily renew old-generation child
indirection nodes, while deletion promotes tombs through parents to prevent path-skeleton buildup.
The gate includes deterministic descriptor schedules and an exhaustive serialization oracle for 400
mixed short histories under ordinary, shared-prefix, and all-equal-hash policies. Immutable snapshot
views convert explicitly to canonical `PersistentHashMap` form in O(n).

**Kotlin/JVM status (2026-07-10): Implemented.** `ConcurrentHashTrie<K,V>` mirrors the C# node
protocol with JVM atomics: bitmap C-nodes, singleton/collision nodes, helping GCAS descriptors,
O(1) root-generation snapshots, and lazy child renewal. Its contention suite covers unique-key
publication, a contended atomic counter, equal-hash collision nodes, retained generations, and
explicit snapshot-to-CHAMP conversion.

**Other-language status: Not applicable without a separate reclamation project.** C and C++ need
epochs or hazard pointers before indirection-node CAS can reclaim safely. Rust's `Arc` ownership
alone does not make an atomic child-pointer protocol safe; an `ArcSwap`/epoch design would be a new
dependency and core. Haskell has GC, but this imperative GCAS/generation algorithm is not a useful
port of the repository's pure persistent API; STM or an atomic root wrapper is a different structure.

**What it is.** Prokopec, Bronson, Bagwell & Odersky (PPoPP 2012): a lock-free *mutable* hash trie
(CAS on indirection nodes, generation-stamped GCAS) whose `Snapshot()` is O(1) - subsequent writers
copy-on-write lazily against the frozen generation. It bridges mutable-map throughput and
persistent snapshotting: `ConcurrentDictionary` performance with `PersistentHashMap` snapshots.

**Why managed runtimes only.** In .NET and on the JVM the garbage collector solves the memory-reclamation problem that
makes lock-free tries hard in native code; the C port would be an epoch/hazard-pointer project (the
derived catalog's `Atom<T>` entry records the same conclusion for a far simpler cell). Parity
economics therefore cap this at a C#-only (or C#+JVM) tier, which the porting guide would need to
acknowledge explicitly.

**Verdict: Implemented for the managed tier.** The use case is hot shared caches that periodically
publish immutable snapshots into the persistent world. Deterministic descriptor interleavings,
model-checked short histories, retained-generation tests, and larger contention stress form the
required concurrency-validation tier; the native reclamation exception remains explicit above.

## Axis 2: Hybrid And Adaptive Representations

### Size-tiered small representations

**The pattern.** Below a threshold, represent the collection as a flat array; promote to the tree
representation at the threshold; demote on shrink with hysteresis. Precedents: Clojure's
`PersistentArrayMap` (flat pair array through 8 entries, then HAMT), Rust `im::Vector` (inline
chunk, then tree), and .NET's own `FrozenDictionary` strategy selection.

**Why it is safe here - two arguments worth recording.**

1. **Unobservability is the library's to guarantee.** The Tungsten consumer study kept its
   SmallList/PackedList tiers engine-side because the *engine's* `SameQ`/`FullForm` observability
   rules can only be enforced there. That objection does not transfer: a general-purpose library
   defines its own equality, enumeration contract, and no-op identity, so it can guarantee
   representation-blindness itself. The tier must preserve, and test-lock, all three - plus the
   struct-enumerator shape, which must not box when the backing representation changes.
2. **Bounded tiers are amortization-safe under branching persistence.** The derived catalog's rule
   2 warns that threshold amortizations layered on the trees (relabeling, compaction) do not
   inherit branching robustness. Promotion at a *constant* threshold k costs O(k) = O(1) worst
   case per operation, so re-branching from a pre-promotion version re-pays only a constant. Size
   tiers dodge the hazard entirely; record this distinction so future reviews do not conflate the
   two.

**Where it pays.** Exactly where persistent structures lose worst today: the small-collection
regime in which `Dictionary<TKey, TValue>` embarrasses every persistent map. A flat 8-16 entry pair
array beats the HAMT walk on every operation, allocates one object per version, and enumerates at
array speed. Most real programs hold *many small* collections and *few large* ones.

**Design gates.**

- Thresholds are benchmark-derived, not guessed: add a small-size suite (n in 1..64) to the
  benchmark harness against `Dictionary`, `ImmutableDictionary`, and the current HAMT before fixing
  k.
- Hysteresis (promote at 16, demote at 8) so a size oscillating at the boundary does not flap.
- Representation dispatch must not tax the large case: a sealed internal root-node hierarchy (the
  flat tier is just another node kind) keeps the check to the existing virtual dispatch rather than
  adding a branch per operation.

**Verdict: Strong.** Apply first to `PersistentHashMap`/`PersistentHashSet`; the sequence facades
already have a partial answer in the rope's chunking.

### The transient -> persistent -> frozen lifecycle

**The idea.** Hybridize across *time* rather than size. The repository already ships two-thirds of
the lifecycle: internal transient builders (HAMT bulk builder; rope and sorted-set/dictionary
builders) cover the write-optimized unpublished phase, and the persistent structures cover the
many-versions phase. The missing tier is **`Freeze()`**: an O(n) conversion to a read-optimized
immutable snapshot for the "this exact version will be read millions of times" phase.

**Strategy selection at freeze time** (the `FrozenDictionary` precedent - .NET 8 selects among
specialized layouts by key type and count):

| Input | Frozen layout |
| --- | --- |
| Tiny (n <= ~10) | Flat array, linear or binary scan; no hashing at all |
| General hashed | Dense open-addressed table sized offline; optional binary fuse filter (Graf & Lemire 2022) in front so negative lookups cost one filter probe |
| String keys | Length/prefix bucketing before hashing (the `FrozenDictionary` trick) |
| Sorted | Packed sorted array with Eytzinger layout; optionally a PGM-index (Ferragina & Vinciguerra 2020) for very large n |

**Contract decisions.** Frozen types are *separate types*, not a hidden tier: their persistent
update operations either do not exist or are documented O(n) (thaw + edit). Conversions both ways
are O(n); `Freeze()` on an already-frozen instance is identity. This keeps the persistent types'
complexity table honest and makes the lifecycle explicit in signatures.

**Verdict: Strong.** Together with size tiers, this attacks both regimes where persistent
collections currently concede to mutable ones - tiny collections and read-forever snapshots.

### Cursor / zipper over the sequence family

**The idea.** Self-adjustment is impossible under persistence (below), but *per-consumer* locality
is not: a cursor is a value holding the root-to-position path (plus the current chunk for
`Rope<T>`), making reads and edits *near the last touch point* O(1) amortized instead of
O(log n). Sequential typing into a rope-backed text buffer - the dominant editor workload - becomes
O(1) per keystroke; the cursor's edit returns the new version plus a refreshed cursor.

**Why persistence makes this clean.** In a mutable tree, cursor invalidation is the hard problem.
Here a cursor is bound to the version it was created against, which is immutable - so a stale
cursor is never *wrong*, merely a cursor into an older version. The natural API returns
`(NewVersion, NewCursor)` pairs from cursor edits; committing to a `readonly struct` cursor keeps
it allocation-free on the stack.

**Scope.** `Rope<T>` first (text editing is the motivating consumer), then `FingerTreeDeque<T>` and
`MeasuredRope`. The measured variant's cursor can also carry the accumulated left measure, giving
O(1) "current line/column" during sequential edits - a feature editors ask for constantly.

**Verdict: Strong.** No new core, high leverage for the existing rope family, and it is the honest
substitute for self-adjusting structures. Already scheduled as the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md)'s item A3 (C#-first, with the
Editor sample rewritten on it); this entry adds the rope-chunk cursor, the version-bound validity
argument, and the accumulated-left-measure feature to that item's design space.

### Key-type-specialized map construction

**The idea.** A factory-level hybrid: integer keys route to the Patricia trie, byte-comparable keys
could route to an adaptive-radix-flavored ordered trie (ART; Leis, Kemper & Neumann, ICDE 2013 -
its Node4/16/48/256 adaptive node encodings are node-granularity hybridization worth borrowing even
inside a CHAMP node layer), everything else routes to the HAMT. C# static-abstract generics resolve
the choice at construction; no per-operation dispatch.

**Honest counterpoint.** A single facade type unifying three cores costs either virtual dispatch or
a fat discriminated union on every operation. The cheaper design is *separate public types* plus a
documentation-level decision table ("int keys -> `PersistentIntMap`; need ordering + prefix queries
on strings -> ART; otherwise -> `PersistentHashMap`"), reserving the runtime hybrid for a future
`object`-keyed dynamic scenario if one ever appears.

**Verdict: Plausible,** in the separate-types form. The ART core itself is a meaningful project;
gate it on a consumer with real prefix-query or byte-ordered-key needs.

### Self-adjusting structures: the recorded rejection

Splay trees, move-to-front lists, and other read-adaptive structures fundamentally fight
persistence, for two independent reasons worth recording so the idea is not re-litigated:

1. **Reads would allocate.** Adapting on read means path-copying on read - every lookup produces a
   new version or mutates shared state, destroying both the allocation-free read contract and
   thread-safe sharing of versions.
2. **Shared versions have contradictory access patterns.** Two consumers holding the same version
   would each want it shaped for *their* access sequence; the amortized potential arguments assume
   one linear history (the same failure mode as the derived catalog's rule 2).

The substitutes shipped by this catalog: cursors (per-consumer locality, axis 2) and `Freeze()`
(explicit read-phase optimization, axis 2). **Reject.**

## Axis 3: Niche Specializations

### Range-update sequence (persistent lazy propagation)

**What it is.** A measured sequence supporting O(log n) *range-assign* and *range-add* - the
persistent form of the segment-tree "lazy propagation" technique. A pending tag (assign v / add d)
attaches to an internal node and applies logically to its whole subtree; descent pushes tags down;
tags compose (assign absorbs prior tags; add composes additively over assign or add).

**Why this workspace can build it well.** The memoized-suspension machinery that publishes lazy
middles is exactly the mechanism deferred tags need: a tag push-down is a pending operation
resolved on first force, CAS-published once, and the amortized persistence-robustness argument
carries over. Essentially no mainstream persistent-collections library ships this; it is a genuine
differentiator.

**The design's one real requirement: a measure action.** Cached measures must be updatable *without
visiting elements*: applying tag t to a subtree of k elements with cached measure m must compute
the new measure as `Apply(t, m, k)`. That is an algebraic requirement on the measure - an action of
the tag monoid on the measure monoid:

- `SizeMeasure`: unchanged by any tag.
- `SumMeasure`: assign v over k elements -> k*v; add d -> m + k*d.
- `MaxMeasure`: assign v -> v; add d -> m + d.
- Arbitrary user measures: must implement the action interface or the tree rejects the tag kind.

Concretely, a new static-abstract interface alongside `IMonoid`/`IMeasure` (e.g.
`ITagAction<TTag, TMeasure>`) with laws (identity tag acts trivially; action distributes over
measure combine) that the property suite enforces, mirroring how the monoid laws are enforced
today.

**Costs and the sibling-type trade.** Every read pays a pending-tag check on descent - the
`ReversibleDeque` pattern exactly: an opt-in sibling (`RangeUpdateSequence<T, ...>`), not a change
to the shipped trees. Uses: bulk text styling runs, weighted timeline editing, simulation grids,
Fenwick-style structures with range updates *and* range queries.

**Verdict: Strong.** The most differentiating single new structure in this catalog after CHAMP.

### Order-maintenance list

**What it is.** The Dietz-Sleator / Bender et al. structure answering "does A precede B?" in O(1)
with O(1) amortized insertion, via two-level integer labels with local relabeling.

**Why here.** The Tungsten association already embeds a special case: gapped stamps with gap 2^20,
midpoint insertion, and O(n (w + c)) wholesale relabel. Extracting a public
`OrderMaintenanceList` would (a) serve any consumer needing order queries over a mutating sequence
(dependency graphs, document anchors, CRDT position identifiers), and (b) let the association's
stamp discipline sit on a structure with a stronger amortized bound than wholesale relabel.

**Honest caveat.** Relabeling amortization is a linear-history argument; under branching
persistence a version branched before a relabel can re-pay it - the same honest contract the
association already documents (derived catalog rule 2 / Tungsten "honest amortization" rule).
State the worst case per produced version.

**Verdict: Plausible.** Build it when a second consumer beyond Tungsten stamps appears, or when
association relabel cost shows up in a real profile.

### Persistent chunked bitset

The derived catalog's follow-up note already scoped this: the HAMT-backed form half-fails the
load-bearing test, and the recommended prototype is the **tree-only** form - `ulong` chunks at the
leaves of a measured finger tree with a popcount sum measure, giving O(log chunks) membership,
rank, and select, and set algebra as a chunk-wise merge with reference-equality short-circuits.
Niche: dense integer sets with heavy set algebra (column bitmaps, mark sets, visited sets shared
across versions). If `PersistentIntMap` ships first, benchmark against an `IntMap`-of-`ulong`-chunks
composition before building a dedicated facade. **Verdict: Plausible.**

### Styled-text rope

A text rope carrying formatting runs: `MeasuredRope<char, ...>` for content plus a run sequence
(ideally the range-update sequence above, so "bold lines 10-500" is one O(log n) tag) keeping
style spans aligned under edits. This is a composition showcase, and per the derived catalog's
parity-economics rule it should ship as a **sample plus documentation pattern**, not a family -
the editor samples are the natural home. **Verdict: Sample, not family.**

### Canonical sorted set

The zip-tree candidate from axis 1, framed as this axis's specialization: the niche is
"collections compared far more often than modified" (memoization keys, configuration
fingerprints, cache validity stamps), where memoized digests plus unique representation turn
equality into O(1) inequality / O(divergence) equality. See the axis 1 entry for the design and
caveats.

### Kaplan-Tarjan real-time deque: the recorded rejection

Kaplan & Tarjan's purely functional real-time deques (JACM 1999) give O(1) *worst-case* endpoint
operations and catenation - no amortization spikes ever. Rejected here because: the shipped deque's
spikes are already rare and logarithmic (memoized suspensions spread debt across forces); the
implementation is notoriously intricate (redundant counting systems, multiple layers of
bootstrapping) with constants that typically lose to the amortized structure on every real
workload; and no latency-critical consumer exists. The useful deliverable is *measuring and
documenting the shipped deque's worst observed pause* in the benchmark notes, so a future
latency-sensitive consumer can decide with data. **Verdict: Reject.**

## Cross-Cutting Design Rules

New rules this survey adds to the derived catalog's seven:

1. **Bounded tiers are amortization-safe; threshold amortizations are not.** Promotion at a
   constant size threshold costs O(1) worst case and survives branching persistence; relabeling,
   compaction, and other O(n)-at-threshold schemes do not. Classify any proposed adaptive scheme
   into one of these two bins before accepting its amortized claim.
2. **Unobservability is the tier contract.** A representation switch must preserve equality
   semantics, the enumeration-order contract, no-op identity, and enumerator shape; each tier
   boundary gets representation-forcing tests on both sides.
3. **Canonical form is the gateway property.** CHAMP, zip trees with derived ranks, and Merkle
   search trees all buy the same thing at different price points: unique representation makes
   equality O(divergence), enables memoized digests for O(1) inequality, and (with content
   addressing) unlocks cross-process diff/sync/merge. When equality or diffing is hot, prefer a
   canonical core over a diff algorithm layered on a non-canonical one.
4. **Adaptivity must live outside shared state.** Per-consumer cursors: yes. Self-adjusting shared
   structure: never (reads allocate; sharers conflict).
5. **Freeze, don't self-adjust.** Read-pattern optimization is an explicit O(n) phase change to a
   separate read-optimized type, not a hidden mutation of the persistent structure.
6. **Deterministic hashing is a hard gate for content addressing.** Nothing whose correctness
   depends on cross-process digest agreement (MST, `MerkleHamt`, keyed zip ranks shared across
   machines) may start before a pinned, versioned hash-and-encoding policy exists.
7. **Ephemeral members must say so.** DABA Lite and the Ctrie are (in whole or part) mutable;
   admitting deliberately mutable members into a persistence-first library requires the docs to
   segregate them as sharply as the external-material policy segregates licenses.

## Recommended Sequencing

Applying the derived catalog's parity-economics rule (stage as C#-first reference implementations;
promote to six-language families only with proven value). Items shared with the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md) keep that proposal's
scheduling slot; this list sequences the frontier-only items and says where each attaches:

1. **CHAMP canonicalization** - attach to the proposal's A2 slot (HAMT `MapEquals`/`Diff`): do the
   two-bitmap layout and canonical deletion behind the existing public surface *before or with* A2
   phase 1, then reproduce the paper's iteration/equality benchmarks in the harness. Retrofitting
   canonical form after diff ships would mean re-verifying the diff bounds twice.
2. **Size tiers + `Freeze()`** for the HAMT family - frontier-only; can proceed independently of
   the proposal slate. First step: the small-size benchmark suite (n in 1..64 versus `Dictionary`,
   `FrozenDictionary`, `ImmutableDictionary`) to fix thresholds with data.
3. **Range-update sequence** - frontier-only, and the natural companion to the proposal's A3
   cursor work (both are editor-grade; the styled-text sample consumes both). The measure-action
   interface is the design decision to review first.
4. **DABA Lite window aggregator** - frontier-only; small, recent, reuses `IMonoid`, and exercises
   the measure framework outside the trees.
5. **`PersistentIntMap` / `PersistentIntSet`** - already the proposal's C1 slot; this catalog's
   contribution is the structural-merge API detail. No separate slot needed here.
6. **Merkle search tree** - the most strategically interesting item, deliberately last: blocked on
   the deterministic-hashing and serialization prerequisites, which are worthwhile infrastructure
   in their own right.

The remaining plausible entries (zip tree, Brodal-Okasaki, PSQ, Ctrie, order-maintenance list,
chunked bitset, key-type factories) are consumer-gated: schedule none of them until a concrete
consumer or profile names the need, per the consumer-driven bar the Tungsten case study set.

## References

Verify each against the primary source before implementation; these citations are from survey
knowledge, not re-read papers.

- Steindorfer & Vinju, *Optimizing Hash-Array Mapped Tries for Fast and Lean Immutable JVM
  Collections*, OOPSLA 2015. (CHAMP)
- Okasaki & Gill, *Fast Mergeable Integer Maps*, ML Workshop 1998. (Patricia / IntMap)
- Bagwell & Rompf, *RRB-Trees: Efficient Immutable Vectors*, EPFL tech report 2011; Stucki, Rompf,
  Ureche & Bagwell, *RRB Vector: A Practical General Purpose Immutable Sequence*, ICFP 2015.
- Auvolat & Taïani, *Merkle Search Trees: Efficient State-Based CRDTs in Open Networks*, SRDS 2019.
  Prolly trees: Noms (Attic Labs, ~2016) and Dolt (DoltHub) engineering documentation.
- Tarjan, Levy & Timmel, *Zip Trees*, 2018 (WADS 2019 / ACM TALG). Gila, Goodrich & Tarjan,
  *Zip-Zip Trees*, WADS 2023.
- Brodal & Okasaki, *Optimal Purely Functional Priority Queues*, JFP 1996.
- Brodal, Lagogiannis & Tarjan, *Strict Fibonacci Heaps*, STOC 2012. Hansen, Kaplan, Tarjan &
  Zwick, *Hollow Heaps*, ICALP 2015. (Both rejected here.)
- Hinze, *A Simple Implementation Technique for Priority Search Queues*, ICFP 2001.
- Tangwongsan, Hirzel & Schneider, *Low-Latency Sliding-Window Aggregation in Worst-Case Constant
  Time*, DEBS 2015; *In-Order Sliding-Window Aggregation in Worst-Case Constant Time*, VLDB
  Journal 2021. (DABA / DABA Lite)
- Prokopec, Bronson, Bagwell & Odersky, *Concurrent Tries with Efficient Non-Blocking Snapshots*,
  PPoPP 2012. (Ctrie)
- Leis, Kemper & Neumann, *The Adaptive Radix Tree: ARTful Indexing for Main-Memory Databases*,
  ICDE 2013.
- Dietz & Sleator, *Two Algorithms for Maintaining Order in a List*, STOC 1987; Bender, Cole,
  Demaine, Farach-Colton & Zito, *Two Simplified Algorithms for Maintaining Order in a List*,
  ESA 2002.
- Kaplan & Tarjan, *Purely Functional, Real-Time Deques with Catenation*, JACM 1999. (Rejected
  here.)
- Graf & Lemire, *Binary Fuse Filters: Fast and Smaller Than Xor Filters*, ACM JEA 2022. Dillinger
  & Walzer, *Ribbon Filter: Practically Smaller Than Bloom and Xor*, 2021.
- Ferragina & Vinciguerra, *The PGM-Index: A Fully-Dynamic Compressed Learned Index*, VLDB 2020.
- Hinze & Paterson, *Finger Trees: A Simple General-Purpose Data Structure*, JFP 2006 (shipped
  family's source paper, for bounds comparisons).

## Relationship To Other Documents

- [Derived structure catalog](derived-structure-catalog.md) - compositions of the shipped
  families, the shared enabling API gaps, and the composition design rules this document extends.
  CHAMP's equality/diff entry here is the core-level realization of that catalog's top-ranked gap.
- [Next data structures proposal (2026-07-09)](../proposals/new-data-structures-2026-07-09.md) -
  the committed slate this catalog complements; overlapping items (Patricia trie, cursor/zipper,
  RRB deferral) keep that proposal's scheduling, with design detail added here.
- [Data structure catalog](data-structure-catalog.md) - shipped surface only; when a candidate
  from this document ships, move its authoritative description there.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) - the parity workflow a
  shipped candidate must satisfy; note the Ctrie entry's explicit parity exception.
- [Semantic contracts](semantic-contracts.md) - the no-op identity, ordering, and policy
  obligations that tiered representations must preserve across tiers.
- [C# FingerTree benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) - the measured
  baselines that the benchmark gates in this document extend.
