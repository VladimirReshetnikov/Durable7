# Frontier Structure Catalog

- Status: Current-state catalog - shipped Axis 1 cores, shipped C# Axis 2 C1/C2/C3/T2, Haskell/Kotlin measured/text plus C++/Haskell/Kotlin/Rust positional cursor checkpoints, cross-language semantic CHAMP sessions, and remaining frontier candidates
- Created (UTC): 2026-07-11T03:31:23Z
- Repository HEAD: f40e301e8faf26d748f33d8546d7d9216657301e
- Audience: Maintainers and AI agents planning new repository-owned cores, representation tiers, and specialized sibling collections
- Scope: New structure cores beyond composition of the original families, plus candidate hybrid/adaptive representations and niche-specialized collections

This document began as a catalog of candidate work that the
[derived structure catalog](derived-structure-catalog.md) deliberately does not cover. That catalog
records what can be built *by composing* the shipped HAMT and FingerTree families; this one records
three complementary axes. Axis 1 now includes both implemented reference cores and unimplemented
candidates. Axis 2 now includes the shipped C# positional and measured rope cursors, their Tour and
Editor integration, Haskell's and Kotlin's measured/text semantic cursor checkpoints, the C++, Haskell, Kotlin, and
Rust positional semantic cursor checkpoints, the optimized C#
one-way CHAMP transient, and semantic path-copying CHAMP editing sessions in every sibling language;
frozen-hash and later phases remain planning material, as does Axis 3:

1. **New cores** - structures that need their own node layer, including several invented or refined
   in the last decade.
2. **Hybrid / adaptive representations** - collections that switch internal representation by size,
   lifecycle phase, or (with sharp limits) usage pattern.
3. **Niche specializations** - opt-in sibling types that trade a constant factor for a specific
   workload, in the mold of the shipped `ReversibleDeque<T>` (O(1) reverse at ~2x forward-path cost).

## Provenance And Method

The initial findings came from a single-pass design survey conducted 2026-07-10/11, grounded against the shipped
C# workspaces ([HAMT](../../src/CSharp/docs/Hamt/overview.md),
[FingerTree](../../src/CSharp/docs/FingerTree/overview.md),
[Tungsten](../../src/CSharp/docs/Tungsten/overview.md), and the
[measured benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md)) and against the derived
structure catalog's verified composition rules. Implemented entries now record the validation and
primary-source checks performed while they were built. Unimplemented candidates have **not** all
received that treatment: before implementing one, re-read the cited paper and verify its actual
claims and bounds. The [references](#references) section lists what to pull.

**Division of labor with the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md).** That proposal selects a
committed, prioritized slate from the derived catalog plus review-observed gaps; this catalog maps
the *frontier* candidate space beyond it. Three items originally overlapped: the Patricia trie
family (proposal Tier C1), the cursor/zipper (proposal A3), and the RRB vector (then deferred on
benchmark-first grounds). Patricia and RRB have since shipped across the language workspaces, and
the positional cursor, measured/text cursor, sample integration, and CHAMP owner-token transients
have shipped as C# Axis 2 C1, C2, C3, and T2. The one-way lifecycle has since gained semantic
path-copying ports in C, C++, Haskell, Kotlin, and Rust, and C++, Haskell, Kotlin, and Rust now have positional
snapshot-plus-gap cursor checkpoints without zipper or performance parity. Haskell and Kotlin additionally have
measured and text cursor facades over the same checkpoint model; the frozen tier and
later cursor families remain planned. The cursor and the temporal-lifecycle work have a dedicated
[Axis 2 final plan](../proposals/axis2-lifecycle-and-sequence-cursors.md), which is authoritative where
its API, complexity, or sequencing detail differs from the older proposal. The entries below are
the current-state record, while the 2026-07-09 proposal remains useful historical scheduling
context.

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

| Structure or strategy | Axis | Verdict / status | Depends on | Rough size |
| --- | --- | --- | --- | --- |
| CHAMP canonicalization + structural equality/diff | 1 | Strong (implemented across all six languages) | Completed with proposal item A2 (HAMT diff) | Node-layer rewrite + 2 public ops + equality benchmark suite |
| `PersistentIntMap` / `PersistentIntSet` (Patricia) | 1 | Strong (implemented across all six languages) | Completed as proposal Tier C1 | 1 shared core, 4 C# public types, structural map/set algebra |
| DABA Lite sliding-window aggregator | 1, 3 | Strong (implemented in every applicable language: C#, C, C++, Kotlin/JVM, and Rust; pure Haskell is not applicable) | Reuses the language's monoid abstraction | 1 small type, ~8 members |
| Merkle search tree | 1 | Strong (implemented completely across all six languages) | Deterministic wire + bounded verification | Largest single item in this catalog |
| RRB vector | 1 | Plausible (implemented across all six languages; evaluation remains benchmark-gated) | Benchmark vs `Rope<T>` random access | 1 new core, transient tier |
| Zip tree (canonical sorted set) | 1, 3 | Plausible (implemented across all six languages) | Completed: coherent keyed rank policy | 1 new core, set facade |
| Brodal-Okasaki heap | 1 | Plausible (implemented across all six languages for the real-time niche) | Completed: invariant and operation-bound audit | 1 new core, small surface |
| Priority search queue (winner-cached AVL) | 1 | Plausible (implemented across all six languages) | Completed as a direct core rather than the addressable composition | 1 new core |
| Ctrie (concurrent, O(1) snapshot) | 1 | Managed-only (C# + Kotlin/JVM implemented) | Tracing GC; native ports require reclamation design | 1 new core, concurrency test tier |
| Hollow heap / strict Fibonacci heap | 1 | Reject | - | Decrease-key via mutation fights persistence; PSQ covers the niche |
| Size-tiered small representations | 2 | Strong, explicitly postponed | Re-entry benchmark after the Axis 2 fixed-layout evidence decision | Internal tier per selected facade + representation-forcing tests |
| Transient -> persistent -> frozen lifecycle | 2 | C# CHAMP T2 owner-token transient and semantic path-copying sibling sessions implemented; frozen map/set tier remains unshipped and evidence-gated | T0/T1/T2 complete for the optimized transient; sibling lifecycle ports complete; postponed F0 then F1 evidence must precede F2 | Shipped map/set sessions across six languages + planned frozen map/set types |
| Version-bound cursor / zipper | 2, 3 | C1 positional cursor, C2 measured/text cursor, and C3 samples implemented in C#; Haskell/Kotlin measured/text and C++/Haskell/Kotlin/Rust positional semantic checkpoints shipped; C4 consumer-gated | C0 selected the C# readonly-struct zipper-as-version; sibling checkpoints reuse persistent rope path copying without its complexity claim | C# positional/measured cursors and Tour/Editor integration, Haskell/Kotlin measured/text facades, and C++/Haskell/Kotlin/Rust positional facades |
| Key-type-specialized map factories | 2 | Plausible, explicitly postponed | Named consumer after explicit Patricia consideration | Factory layer; ART only if independently justified |
| Self-adjusting (splay-style) structures | 2 | Reject | - | Reads allocate under path copying; cursors + freeze substitute |
| Range-update sequence (lazy propagation) | 3 | Strong | Measure action interface | 1 sibling core + tag algebra + property tests |
| Order-maintenance list | 3 | Plausible | - | 1 public type; Tungsten stamps could layer on it |
| Persistent chunked bitset | 3 | Plausible | - (tree-only form per derived catalog follow-up) | 1 facade over measured tree |
| Styled-text rope | 3 | Sample, not family | Range-update sequence (or interval runs) | Composition sample + docs |
| Kaplan-Tarjan real-time deque | 3 | Reject | - | Document the memoized deque's spike profile instead |

## Axis 1: New Cores

### CHAMP canonicalization (upgrade to the shipped HAMT)

**C# status (2026-07-12): Implemented.** The managed HAMT uses separate data/node bitmaps, inline
payload runs, deletion-time leaf promotion, canonical one-freeze bulk construction, lockstep
reference-pruned `MapEquals`, bitmap-aligned structural `Diff`, and same-type structural map/set
algebra and relations. Its executable gate covers every
leaf/collision/branch transition, eager argument validation, stored-key representatives, randomized
node invariants, independent-history topology, reference-pruning bounds, and dedicated benchmarks.

**Kotlin status (2026-07-12): Implemented.** The JVM port now has the same split bitmap/inline
payload representation and canonical deletion promotion, plus policy-compatible `mapEquals` and
typed `diff` and reference-pruned structural algebra. Its executable suite checks independent insertion histories, CHAMP node invariants,
collisions, persistence, and concurrent readers.

**Rust status (2026-07-12): Implemented.** Both persistent path copying and `BulkBuilder` freezing
now produce split data/node maps with inline payload runs and canonical deletion promotion. The safe
`Arc` implementation adds owned typed diff, policy-identity-gated structural algebra, and
representation-invariant coverage.

**Haskell status (2026-07-12): Implemented.** `HashMap` now stores strict split data/node maps,
inline `(hash,key,value)` payload runs, child-only subtrie runs, and deletion promotion. Its
dependency-free API adds `mapEquals`, typed `MapDifference` values, cached-cardinality structural
algebra, and positive-only GHC pointer pruning, with `validStructure`
checking the CHAMP invariants. Native-port status follows below.

**C++ status (2026-07-12): Implemented.** The header-first C++20 map and its move-only bulk builder
now use split maps, compact inline payload vectors, child-only shared subtries, and canonical removal
promotion. `map_equals`, owned typed `map_difference`, cached counts, and policy-token-gated
structural algebra round out the map surface.

**C status (2026-07-12): Implemented.** The C17 core stores split maps, inline type-erased payloads,
and child-only flexible-array runs while preserving retain/release policy balance and allocation-
failure rollback. Visitor-based typed diff avoids imposing an allocator on callers; two-set/map
structural algebra is failure-atomic and reference-pruned. CHAMP is now
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
so structural work tracks visited non-shared trie regions plus reported differences. Independently
built maps may require a complete O(n + m) traversal: identical topology is not reference identity,
and CHAMP alone does not guarantee work proportional to logical divergence. Equal-hash collision
runs require unordered key matching and can add O(c²) comparisons for a bucket of size c.

**Shipped representation change.** `PersistentHashMap<TKey, TValue>` and
`PersistentHashSet<T>` moved from the predecessor bitmap-indexed HAMT to the two-bitmap node layout,
canonical deletion, and structural equality/diff surface while retaining compact storage,
collision buckets, struct enumerators, and one-freeze bulk construction. Enumeration remains
explicitly unspecified, and the tests protect downstream consumers from depending on a historical
node order.

**Caveats.** Collision buckets remain insertion-ordered, so equality/diff use key-matched unordered
comparison. Equivalent maps can also retain different concrete key objects. Enumeration order is
unspecified, deletion adds a collapse check, and independently allocated maps cannot benefit from
reference pruning. The managed benchmark suite separates shared-single-change diff from
independent-history equality/diff so those two cost profiles stay visible.

**Verdict: Strong (implemented).** The in-place CHAMP upgrade shipped together with the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md)'s item A2. `MapEquals` and
`Diff` now exploit shared identity and aligned bitmap slots while retaining the honest complete-
traversal and collision-bucket bounds above.

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
property the HAMT cannot match: **structural merge**. The default `Union`, `Intersect`, and `Except`
align compressed prefixes and short-circuit reference-equal subtrees. Work is proportional to visited
non-shared prefix regions plus materialized result/output, with O(n + m) worst-case work; retained
sharing lowers the visited region count rather than defining a separate asymptotic bound. No-op
merges preserve reference identity.
Merge-heavy workloads (version-set algebra, sparse indices, ID-set reconciliation) therefore avoid
per-key hash lookup passes. Enumeration is in unsigned key order for free; a sign-flip key transform
yields signed order.

**Design notes.**

- Both 32- and 64-bit key widths ship as concrete C# types, keeping the hot path free of generic-
  math abstraction while sharing a static-policy internal engine.
- Combining-function overloads (`Union(other, (k, l, r) => ...)`) make structural merge useful and
  mirror the essential role of Haskell `Data.IntMap`'s `unionWith`/`intersectionWith` vocabulary in
  this repository's style.
- Collision buckets do not exist (keys are their own hashes); the structure is strictly simpler
  than the HAMT.

**Verdict: Strong (implemented).** The family shipped as the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md)'s Tier C1, with explicit 32-
and 64-bit surfaces, combining algebra, signed ordering, validation, and no-op identity. Its
production precedent remains Haskell `Data.IntMap`.

### RRB vector

**C# status (2026-07-11): Implemented and representation-hardened.** `RrbVector<T>` ships with
32-element leaves, radix-indexed regular 32-way branches that allocate no size table, relaxed
branches with cumulative sizes, O(log32 n) indexing/path-copying updates, exact-boundary-sharing
split/edit operations, and O(log32(n + m)) boundary-spine concatenation. Its append-only builder
freezes full leaves safely and caches immutable snapshots. Internal validation and adversarial tests
bound density/height drift; the benchmark gate compares indexing and concat with both `Rope<T>` and
`ImmutableList<T>`. A dedicated persistent tail remains deliberately unimplemented, so immutable
endpoint append is still a boundary-spine operation rather than worst-case O(1).

**C++ status (2026-07-11): Implemented and representation-hardened.** Header-first `rrb_vector<T>`
uses `shared_ptr<const node>` ownership, 32-element leaves, size-table-free regular branches,
cumulative-size relaxed branches, persistent point/range/endpoint edits, boundary-spine
concatenation, retained forward iteration, and an append builder with immutable snapshot isolation.
Structural diagnostics and adversarial model tests cover exact sharing, density/height drift, copy-
constructible non-assignable values, and injected exceptions; native benchmark pairs compare indexed
reads and concatenation directly with `rope<T>`.

**Haskell status (2026-07-11): Implemented with an idiomatic pure construction tier.**
`Data.Structures.FingerTree.RrbVector` uses boxed immutable arrays, radix-indexed regular branches,
relaxed cumulative sizes, structural split/append/range edits, cached metadata validation, and
root-sharing diagnostics. `fromList` performs bottom-up bulk construction; no public mutable
transient is exposed because an `ST`-style implementation detail would not be an idiomatic public
Haskell value.

**Kotlin/JVM status (2026-07-11): Implemented and representation-hardened.** `RrbVector<T>` uses
32-element object-array leaves, radix-indexed regular branches without size tables, relaxed
cumulative-size branches, boundary-spine concatenation, structural split/range/endpoint edits, and
ordered iteration. Its append builder adopts frozen prefixes, transfers full tails, copies partial
tails, caches clean snapshots, and detects mutation during iteration. Nullable-pop, overflow,
exact-sharing, 10,000-operation model, adversarial density/height, and concurrent-reader gates cover
the JVM representation.

**Rust status (2026-07-11): Implemented and representation-hardened.** `RrbVector<T>` stores
contiguous elements in shared `Arc<[T]>` leaf backing with structural slice offsets, uses
size-table-free regular branches and cumulative-size relaxed branches, and keeps `Clone` bounds local
to operations that copy owned values. Structural splits/ranges, borrowed iteration, endpoint removal,
validation/statistics, exact sharing, and the frozen-prefix append builder work without unsafe code;
the full workspace and warning-clean Clippy gates cover randomized, adversarial, non-`Clone`, and
concurrent-reader cases.

**C status (2026-07-11): Implemented with explicit ownership and failure semantics.** The C11
`ft_rrb_vector` uses atomic-reference-counted immutable nodes, policy-driven value copy/destroy/equal
callbacks, injected allocation, size-table-free regular branches, relaxed cumulative sizes, and
alias-safe persistent operations. The builder transfers full owned tails and copies partial tails on
publication. Validation spans MSVC Debug/Release, warning-strict GCC and Clang, Clang AddressSanitizer,
allocation-failure rollback, tracked lifetimes, retained snapshots, concurrent readers, and release
benchmark smoke coverage.

**Representation boundary.** All six ports deliberately implement boundary-spine concatenation,
not the full Stucki-et-al. redistribution pass. The two seam child arrays are repartitioned, but no
global minimum-fullness invariant is promised for leaves or branches away from that seam. The
adversarial density assertions are regression and benchmark gates for the histories they exercise,
not a validator-certified representation contract. Validators do certify a size-derived height cap:
`floor((count storage bit width - 1) / 5) + 1`. The first term is the greatest minimum height
required anywhere in the count domain; the extra level admits the boundary-only concatenation
contract's legal `minimum height + 1` slack even in the top count band. The resulting caps are seven
for the C#/Kotlin `Int` count domain and thirteen on the repository's 64-bit
`size_t`/`Int`/`usize` C, C++, Haskell, and Rust targets.

**What it is.** The relaxed radix-balanced tree (Bagwell & Rompf 2011; practical treatment with
transients in Stucki et al., ICFP 2015): a 32-way branching persistent vector where nodes are
normally full (radix-indexable in O(log32 n) with pure bit arithmetic) and carry small size tables
only where concatenation introduced irregularity. This is the persistent vector of Scala 2.13,
Clojure (`core.rrb-vector`), C++ `immer`, and Rust `im`.

**Why it matters here - and why it might not.** The shipped sequence story is
`FingerTreeDeque<T>` (O(1) amortized endpoints, O(log distance-from-nearer-end) indexing - the
measured U-shape) plus `Rope<T>` (chunked leaves, cache-dense, O(log n) edits). The RRB vector's
differentiator is **uniform O(log32 n) random access** with no U-shape. The shipped C# persistent
value deliberately has no dedicated tail buffer, so endpoint operations remain O(log32 n); its
separate append-only builder is the bulk-construction path. Concatenation is O(log n) with worse
constants than the deque's O(log min).

| Workload | `FingerTreeDeque<T>` | `Rope<T>` | RRB vector |
| --- | --- | --- | --- |
| Endpoint push/pop | O(1) amortized (best) | O(log n), amortized O(1) staged | O(log32 n); no persistent tail |
| Random index, middle | O(log n), ~310 ns at 100k | O(log n) chunked | O(log32 n), small constant (best) |
| Concat | O(log min) (best) | O(log min) | O(log n), relaxed nodes |
| Cache density | pointer-heavy | chunked (best for scans) | 32-way arrays (good) |
| Bulk build | element-wise | chunk-staged builder (best) | transient tier |

**Verdict: Plausible (implemented; adoption remains benchmark-gated).** The benchmark harness now
contains random-access and concatenation comparisons against `Rope<T>` and `ImmutableList<T>`.
Consumers should choose the vector when uniform indexing wins for their measured workload; the
implementation does not by itself erase the overlap with the two shipped sequence families. The
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md)'s original deferral remains
useful context for that demand-driven adoption rule.

### Merkle search trees / Prolly trees

**C# status (2026-07-11): Implemented with the hashing, wire, verification, and synchronization
gates.** `MerkleSearchTree<TKey, TValue>` is now the paper-style B=16 wide tree: leading zero
SHA-256 nibbles assign geometric layers, one block holds each consecutive same-layer run, and exact
`MST2` bytes include the policy domain, entries, subtree count, and child digests. Strict versioned
`IMerkleCodec<T>` implementations encode *and* decode canonical values; the built-ins cover integers,
nullable strings, nullable byte arrays, and GUIDs. The public persistence tier adds immutable blocks,
a thread-safe content store, complete and partial packs, bounded verified load/import, canonical
membership/non-membership/range proofs, iterative frontier plans and closure-pruned sync packs, and a
typed three-way merge that never publishes a partial result with unresolved conflicts. Golden wire
vectors and adversarial tests cover independent histories, wide blocks, retained sharing, malformed
or noncanonical data, missing/tampered closures, resource budgets, proof tampering, sync repair, and
present-null merge semantics. In-process `Diff` prunes equal aligned blocks but honestly falls back to
ordered subtree comparison when a topology-changing edit moves separators; block synchronization is
the cross-process divergence-oriented path.

**Rust status (2026-07-11): Implemented through the complete persistence tier with exact wire
compatibility.** `MerkleSearchTree<K, V>` shares the C# SHA-256 domain, key framing, empty digest,
and `MST2` block bytes while adapting persistence to immutable `Arc` nodes and a safe-Rust,
non-`Clone` API. `MerkleBlockStore` and its concurrent in-memory implementation support atomic
save/import, complete and requested packs, closure-pruned synchronization, and iterative frontier
repair. Strict load/import and `MSP2` point/range verification enforce seven finite budgets before
publishing trusted nodes. Typed three-way merge distinguishes a deleted key from a present `None`
value and withholds the merged tree until every conflict is resolved. Fifty-eight Debug and Release
tests plus doctests cover the shared golden vector, malformed and noncanonical closures, every
budget, proof tampering, partial-store repair, concurrent stores, retained snapshots, and
non-`Clone` values; clippy and rustdoc are warning-clean.

**Haskell status (2026-07-12): Implemented through the complete pure persistence tier with exact
wire compatibility.** `Data.Structures.Hamt.MerklePersistence` adds immutable block-store
snapshots, deterministic complete/partial packs, whole-result save/import publication, strict
bounded closure reconstruction, exact `MSP2` point/range proofs, closure-pruned and iterative
frontier synchronization, and present/absent-safe typed merge. Its opaque budget admits seven
validated limits; proof verification fixes query, shape, and envelope precedence before decoder or
codec work. Local proof/sync construction consumes a read-only trusted topology view rather than
applying network budgets or decoding already-retained values. Eight persistence scenario groups
with 93 direct assertions cover shared golden bytes, hostile closures, every budget, bomb-codec
admission, proof accounting/tampering, partial repair, `MergePresent Nothing` versus deletion,
retained roots, and a 2,000-operation persistence model. The wrapper and a clean warning-denied
build of all eight library and two test modules pass; pure successor stores are the intentional
language-local replacement for a concurrent mutable store.

**C status (2026-07-12): Implemented through the complete type-erased persistence tier with exact
wire compatibility and failure atomicity.** `tds_merkle_search_tree` binds stable type tags,
fallible copy/destroy/equality/comparison/codec/store hooks, and an injected allocator into atomic
immutable handles. The public tier adds a synchronized three-state block store, complete/requested
packs, seven verification budgets, bounded closure-checked load/import, exact `MSP2` point/range
proofs, closure-pruned and iterative synchronization, and exact-policy present-null-safe merge.
Generic store outputs are shielded; allocator, destructor, and visitor callbacks run outside the
non-recursive store lock; query/shape limits precede proof allocation/hash/codec work; and every
allocation/callback failure leaves result handles unpublished. Twenty-one Merkle groups cover the
shared wire, hostile closures and proofs, all limits, late-conflict preflight, sync, every merge
state, exhaustive allocation unwinds, malicious/reentrant callbacks, and eight-thread store races.
MSVC Debug/Release, strict GCC/Clang, AddressSanitizer, and the Clang analyzer are clean.

**Kotlin/JVM status (2026-07-12): Implemented through the complete persistence tier with managed
reference parity and exact wire compatibility.** `MerkleSearchTree<K, V>` retains caller objects
and immutable canonical byte snapshots in path-copied wide nodes. The persistence surface adds
immutable blocks and packs, a concurrent memory store, seven finite verification limits, bounded
verified load/import, exact `MSP2` point/range proofs, iterative frontier repair and closure-pruned
sync packs, and typed present-null-aware three-way merge that withholds partial conflicted trees.
Query and proof-shape limits run before verifier allocation, hashing, or codec work; import verifies
all supplied blocks and its requested root closure before destination conflict preflight and first
publication. Twenty-four focused Merkle groups cover the shared golden wire, histories, retained
sharing, malformed/noncanonical closures, every budget, proof tampering, partial-store repair,
atomic conflict handling, nullable merge states, and concurrent readers. The complete Kotlin
HAMT/FingerTree/Tungsten gate is warning-clean under Kotlin 2.4 `-Werror`.

**C++ status (2026-07-12): Implemented through the complete header-first persistence tier with
native value semantics and exact wire compatibility.** `merkle_persistence.hpp` adds immutable
blocks/packs/plans, a shared-mutex store, seven finite budgets, strict `MST2` export/save/load/import,
destination preflight, partial-overlay import, and iterative synchronization. `merkle_proofs.hpp`
adds exact `MSP2` point/range proofs and typed three-way merge that preserves present-null values,
withholds unresolved trees, and supports move-only keys and values. Proof structure limits precede
allocation, hashing, and codecs; untrusted blocks are re-decoded, re-encoded, and reconstructed
through the canonical
node factory. Nineteen Merkle groups cover all limits, hostile closures/proofs, authenticated
reference tampering, sync convergence, concurrent stores, and present-null/move-only merge.
MSVC Debug/Release, strict GCC/Clang lanes, both analyzers, and copied aggregate-header consumers are
clean.

**What they are.** Two convergent designs for *uniquely represented, content-addressed* search
trees. Merkle search trees (Auvolat & Taïani, SRDS 2019) place each key at a layer derived from its
hash (geometric distribution), producing a deterministic B-tree-like shape independent of insertion
order. Prolly trees (probabilistic B-trees; Noms ~2016, now Dolt) reach the same goal by
content-defined chunking: node boundaries fall where a rolling hash of the entry stream crosses a
threshold. Every node carries a digest of its contents; the root digest names the whole collection.

**What they buy.** Digest-pruned diff, synchronization, and three-way merge **across processes and
machines**, not merely across versions sharing pointer ancestry - the property behind AT Protocol
repositories (Bluesky) and Dolt's versioned SQL tables. Equal block digests prune complete regions;
work follows the examined and missing block frontier, but a topology-changing edit can move
separators, so the shipped API does not promise O(key divergence) for every diff. Policy-bound
unique representation gives history independence and digest-based equality.

**Relationship to the derived catalog.** The catalog's `MerkleHamt` (deferred) hashes an
*unordered* trie; the MST is the *ordered* sibling and subsumes several of its uses while adding
range queries and range sync. The implementation met the two prerequisites that remain mandatory
for any future content-addressed family:

1. **Pinned deterministic hashing.** Default .NET string hashing is per-process randomized - fatal
   for cross-process addressing. `MerkleSearchTreePolicy<TKey, TValue>` therefore binds a versioned
   semantic id, canonical codecs, comparer semantics, and SHA-256 domain digest.
2. **Canonical serialization.** A digest is only as canonical as the byte encoding it hashes. The
   shipped `MST2` wire and `IMerkleCodec<T>` contract reject malformed, noncanonical, and trailing
   encodings before accepting content.

**Verdict: Strong (implemented after satisfying its gates).** The MST is the repository's first
content-addressed family; the deferred `MerkleHamt` must reuse equally explicit hashing, encoding,
verification-budget, and trust-boundary contracts if it is ever promoted.

### Zip trees and zip-zip trees (canonical sorted core)

**C# status (2026-07-11): Implemented and hardened.** `CanonicalSortedSet<T>` and
`ZipTreeRankPolicy<T>` derive a geometric coordinate and fixed 64-bit secondary coordinate from
HMAC-SHA-256. A policy can use an unexposed random key, a public reproducibility seed, or a
caller-retained key; an explicit comparer must also supply a rank hash constant on its equivalence
classes, and duplicate representatives dynamically check that contract. Persistent zip/unzip is
explicit-stack and remains stack-safe under a fully colliding 4,096-node chain. Sorted bulk build is
O(n log n) plus a linear Cartesian-tree pass; `ValidateStructure` checks order, heap rank,
reproducibility, metadata, depth, and priority collisions. Set equality is semantic across policy
families while canonical algebra remains policy-identity gated. Adversarial tests cover keyed-rank
reproduction, comparer/hash incoherence, ordinary `IReadOnlySet<T>` interoperability, randomized
retained histories, exact sharing, and maximally deep delete/reinsert/digest traversal.

**C status (2026-07-11): Implemented with explicit erased-type and failure contracts.** The C11
port owns values and immutable nodes through atomic reference counts, exposes random/seeded/keyed
identity-bearing policy handles, and uses CNG on Windows or OpenSSL Crypto elsewhere. A required
caller-owned value-type identity prevents distinct same-size erased representations from reaching
receiver callbacks during cross-policy equality/relations. Copy/compare/rank-hash and allocator
failures propagate without publishing partial successors; aliasing an operand as the output is
supported. Atomic lazy digests, allocation-free intrusive release, first-representative bulk build,
validator/statistics, sharing diagnostics, and the complete set-relation surface preserve the
managed contract in an idiomatic status-returning API.

**C++ status (2026-07-11): Implemented and cross-toolchain hardened.** The C++23 header-first port
uses system CNG on Windows and OpenSSL Crypto elsewhere, exports the dependency through its CMake
package, and pins natural rank hashes for integral/string types. Representatives live in
`shared_ptr<const T>`, so moved bulk ranges, rvalue insertion, removal, algebra, and reads support
move-only elements. Atomic digest publication is thread-safe; a fixed `size_t`-bit destruction
worklist releases adversarial chains without allocation or recursion. Fourteen focused groups cover
exact vectors, random/key ownership, policy identity, receiver asymmetry, 20,000 retained updates,
quantified sharing, callback exceptions, and allocation-free destruction of 16,384 nodes.

**Haskell status (2026-07-11): Implemented with an explicit pure/IO boundary.** Seeded, keyed, and
fresh-random factories allocate an opaque `Data.Unique` family identity in `IO`; random creation
also obtains entropy from maintained `crypton`, while every operation on an existing set stays
pure. Callers provide the equivalence-class hash explicitly, avoiding an unstable implicit Haskell
hash. Stable bulk sorting preserves first representatives, nodes cache the sibling-compatible
digest eagerly, and IO-only `StableName` diagnostics quantify structural sharing. Exact vectors,
separate same-seed/keyed policy reproduction, receiver-defined relations, a retained-history model,
a 4,096-node collision chain, and hostile-callback validation are executable gates.

**Kotlin/JVM status (2026-07-11): Implemented and independently reviewed.** The JVM port preserves
the `ZZT2` public-seed derivation, full caller-keyed HMAC-SHA-256 mode, unsigned secondary-rank
ordering, first-representative bulk semantics, policy-identity algebra gate, receiver-comparator
cross-policy equality, and explicit-stack Cartesian-tree algorithms. Its lazily published digest
uses `AtomicReference`; focused adversarial coverage fixes the keyed and public-seed vectors,
exercises nullable representatives and maximally colliding chains, quantifies off-path sharing for
add/remove, and races cold digest publication across readers.

**Rust status (2026-07-11): Implemented and independently reviewed.** The safe Rust port uses
RustCrypto HMAC/SHA-256 and `getrandom`, explicitly pinned stable rank hashes instead of
`DefaultHasher`, `Arc` path sharing, `OnceLock` digest publication, and iterative destruction for
height-n chains. Bulk construction, lookup, iteration, validation, clear, and same- or cross-policy
equality accept non-`Clone` elements; only path-copying edits, algebra, and the owned diff require
`T: Clone`. Its 14 focused tests include stable-hash and C#-compatible `ZZT2` vectors, unsigned
secondary ordering, direct first-representative identity, receiver-comparer asymmetry, a
20,000-operation retained-history model, deep destruction, and barrier-started cold digest readers.

**What they are.** Zip trees (Tarjan, Levy & Timmel, 2018/2019) are randomized-rank binary search
trees - a reformulation of treaps where insertion and deletion are single root-to-position *unzip*
/ *zip* passes with O(1) expected restructuring, no rotations. Zip-zip trees (Gila, Goodrich &
Tarjan, 2023) refine the rank distribution to bring expected depth near the information-theoretic
optimum while keeping O(log log n) rank bits.

**Why they matter here.** Under one retained, coherent rank policy, deriving each comparison-
equivalence class's rank makes topology converge for the same stored representatives regardless of
update order. The memoized root digest rejects most inequality in O(1), shared nodes prune semantic
equality, and independently built equal sets compare in O(n). This is policy-canonical topology,
not a globally unique representation: policy identity gates structural algebra, and the observable
first-representative rule means equivalent values can retain different concrete objects. Ephemeral
zip trees change O(1) expected pointers, but a persistent update still copies its O(h)
search/zip/unzip path: expected O(log n) nodes, O(n) under adversarial ranks.

**Caveats.** This is a practical fixed-width variant, not the paper's compact representation: its
64-bit secondary coordinate does not inherit the paper's O(log log n)-metadata theorem, and its
64-bit caller hash (32-bit for the fallback) can collide before HMAC. Expected-depth claims therefore
require a well-distributed, stable, equivalence-class-coherent rank source. `CreateKeyed` can resist
adaptive rank prediction only while its caller-retained key remains secret; a public seed provides
reproduction, not adversarial security. The shipped sorted finger tree covers ordinary workloads
with worst-case bounds and the measure framework; the zip-zip set earns its place where canonical
topology and memoized inequality dominate.

**Verdict: Plausible (implemented for the policy-canonical niche).**
`CanonicalSortedSet<T>` is a sibling for reproducible same-policy topology and memoized inequality,
not the general sorted-set default and not a claim of policy-independent unique representation.

### Heaps: Brodal-Okasaki and PSQ shipped; hollow/strict-Fibonacci rejected

**C# status (2026-07-11): Brodal–Okasaki and PSQ implemented and adversarially audited;
mutation-dependent heaps rejected as specified.** `BrodalOkasakiHeap<T>` directly ports the
bootstrapped skew-binomial representation with bounded-comparison O(1) insert/meld/minimum and
O(log n) delete-min. `PrioritySearchQueue<TKey, TPriority, TValue>` supplies keyed priority updates
and the distinctive key-range/priority-threshold query over one winner-cached AVL core. Both expose
public structural validators. Strict Fibonacci and hollow heaps remain explicit non-goals.

**Haskell status (2026-07-11): Brodal-Okasaki heap and priority search queue implemented.**
`Data.Structures.FingerTree.BrodalOkasakiHeap` directly implements the fused bootstrapped
skew-binomial representation and validates primitive-child/embedded-forest boundaries, ranks, heap
order, count, and depth. `Data.Structures.FingerTree.PrioritySearchQueue` is a direct strict winner-
cached AVL rather than a `Map`/heap composition. It retains one entry per ordered key, breaks
priority ties by key, supports O(1) minimum and O(log n) keyed update/delete-min, validates all
AVL/winner metadata, and preserves retained snapshots through a 10,000-operation model.

**Kotlin/JVM status (2026-07-11): Brodal-Okasaki heap and priority search queue implemented and
adversarially audited.** `BrodalOkasakiHeap<T>` directly implements the bootstrapped skew-binomial
representation with comparator-identity-gated meld, nullable-safe minimum views, explicit-stack
iteration and validation, and measured comparison ceilings through 65,536 elements. Its retained-
snapshot model exercises 20,000 branching operations and its adversarial drains cover monotone,
equal-priority, and melded 4,096-element shapes. `PrioritySearchQueue<K, P, V>` is a persistent
winner-cached AVL with first-key-representative and last-value semantics, exact no-op reuse,
nullable-safe result wrappers, deterministic priority/key tie ordering, and an inclusive key-range/
priority-threshold traversal. Its audit covers all rotation/deletion shapes, 50,000 ascending keys,
a 20,000-operation retained-history model, pruning comparison equations, structural sharing, and
concurrent readers.

**C++ status (2026-07-11): Brodal-Okasaki heap and priority search queue implemented with native
value-policy parity.**
`brodal_okasaki_heap<T, Less>` retains immutable trees, forests, comparator policy, and concrete
element representatives through `shared_ptr`; this permits move-only elements while keeping old
versions and removed-minimum handles alive. Comparator identity gates meld, independently created
default heaps share a canonical policy, and the direct fused bootstrapped skew-binomial core keeps
minimum/insert/meld worst-case O(1) and delete-min O(log n). Explicit-stack iteration, validation,
and destruction cover self-meld DAGs without silently collapsing logical occurrences. Native audits
pin comparison and allocation growth, off-path sharing, throwing-comparator publication, retained
models, adversarial drains, and concurrent readers.

`priority_search_queue<K, P, V, KeyLess, PriorityLess>` is the direct persistent winner-cached AVL
counterpart. Shared component records preserve exact move-only key, priority, and payload
representatives; key-order equivalence retains the first key and replacement retains the last
priority/payload, with exact equal replacements reusing the instance. It provides O(1) minimum,
O(log n) keyed update/removal/delete-minimum, and eager-bound-validated inclusive key-range/
priority-threshold enumeration with cached-winner pruning. Twelve focused groups cover every
rotation and deletion shape, 50,000 ascending keys, a 20,000-operation retained model, exact pruning
equations, policy/callback exceptions, allocation and sharing, move-only components, and concurrent
readers. The aggregate header and installed-consumer package both exercise the public surface under
MSVC, GCC, and Clang in Debug and Release.

**C status (2026-07-11): Brodal-Okasaki heap implemented with explicit ownership and failure
atomicity.** `ft_brodal_heap` uses an identity-bearing, reference-counted policy with a stable erased-
type tag and fallible value-copy, comparator, and allocator hooks. Values, trees, and forest cells
are immutable reference-counted objects; an intrusive nonallocating release worklist keeps deep and
self-melded DAG reclamation stack-safe. Point operations support result/source aliasing, and every
allocation or callback failure withholds the successor; try-delete additionally withholds both the
copied removed representative and remainder if either cannot be published. Exhaustive failpoint and
lifetime sweeps accompany retained models, comparison ceilings, structural validation, and concurrent
distinct-handle readers under MSVC, GCC, Clang, and ASan/UBSan.

The C and C++ `skew_meld` implementations consolidate the child forest by rank buckets with carry,
whereas C#, Haskell, Kotlin, and Rust spell the equivalent step as `uniquify` followed by
`unionUnique`. This is an intentional native implementation-shape difference: both produce the same
valid ranked-tree multiset, minimum, and public complexity bounds.

**Rust status (2026-07-11): Brodal-Okasaki heap implemented without an element-cloning bound.**
`BrodalOkasakiHeap<T>` stores representatives and the fused bootstrapped skew-binomial graph behind
`Arc`. Owned minimum views retain the exact representative; independently constructed natural-order
policies interoperate, while custom heaps must clone the same `OrderPolicy<T>` identity to meld.
Typed invariant and policy errors, explicit-stack iteration/validation, `Option`-valued and non-
`Clone` elements, a 20,000-operation retained-history model, comparison ceilings through 65,536
elements, sharing probes, and concurrent readers pass the full 103-test debug/release gate plus
strict clippy, rustdoc, and doctests.

**Rust priority-search status (2026-07-11): winner-cached AVL implemented without cloning stored
components.** `PrioritySearchQueue<K, P, V>` retains each key, priority, and payload behind `Arc`,
so lookup, removal, minimum views, and iteration borrow or return exact representative handles.
Only exact replacement reuse adds ordinary priority/payload equality bounds. Its borrowing
`enumerate_at_most` iterator eagerly rejects inverted bounds, emits in key order, and prunes by the
cached winner. Eight focused groups cover every rotation/deletion direction, 50,000 ascending keys,
a 20,000-operation retained model, `Option`-valued and non-`Clone` components, exact comparison
equations, path sharing, custom-policy ties, and concurrent readers; the full debug/release gate is
111/111 with warnings-denied clippy and rustdoc.

**C priority-search status (2026-07-11): implemented with erased-type ownership and exhaustive
failure atomicity.** `ft_priority_search_queue` uses separate stable key, priority, and value type
tags, callback contexts, and immutable reference-counted representatives. Key ordering alone defines
the equivalence class and the first stored key; exact no-op reuse requires priority-order equivalence,
priority equality, and value equality, while `key.equals` is deliberately never invoked. Minimum,
removal, and delete-minimum return owned exact-representative handles; lookup and visits borrow.
Iterative AVL updates, traversal, validation, and intrusive reclamation remain stack-safe. Six focused
groups cover 50,000 ascending keys, every rotation/deletion, 20,000 retained operations, exact pruning
equations, exhaustive allocation/copy/equality/comparator failpoints, alias publication, lifetimes,
nullable representations, sharing, and concurrent readers under the full five-lane C matrix.

- **Brodal-Okasaki heap** (JFP 1996; skew binomial queues + data-structural bootstrapping): purely
  functional with O(1) *worst-case* insert, meld, and findMin, O(log n) deleteMin. The shipped
  finger-tree `PriorityQueue` has the same bounds amortized (meld O(log min)). The niche is
  worst-case latency guarantees. The C# implementation occupies that niche as a small, explicit
  sibling, with comparison-count tests for the operation bounds and a benchmark harness for its
  constants against the measured finger-tree queue.
- **Strict Fibonacci heaps** (Brodal, Lagogiannis & Tarjan, STOC 2012) and **hollow heaps**
  (Hansen, Kaplan, Tarjan & Zwick, ICALP 2015) achieve optimal decrease-key bounds through
  aggressive pointer mutation. Persistent path-copying destroys exactly the surgery they rely on.
  **Reject** for this repository.
- **Priority search queues** combine a finite map ordered by key with minimum-priority access.
  Hinze's ICFP 2001 implementation uses priority-search pennants: a loser/semi-heap over a
  weight-balanced search tree. Its threshold-only `at-most` operation has the output-sensitive
  bound Θ(r(log n - log r + 1)) for r results; the full paper adds a key range. The shipped C# core
  deliberately uses a different representation, a persistent AVL node carrying its own entry and
  a cached subtree winner. It provides O(log n) keyed updates and deletion, O(1) `Minimum`, and
  O(log n) `DeleteMinimum`. Its inclusive key-range/priority-threshold traversal costs O(log n + v),
  where v is the number of visited nodes that pruning cannot exclude and v ≤ n; the worst case is
  therefore O(n), and no pennant-specific output bound is claimed. The direct core complements the
  plausible `AddressablePriorityQueue` composition (HAMT plus a sorted priority index) when this
  combined query is required.

### DABA Lite sliding-window aggregator

**C# status (2026-07-11): Implemented and adversarially hardened.** `DabaLite<T, TMonoid>` reuses
`IMonoid<T>` and implements the VLDB Journal 2021 DABA Lite schedule over a linked queue of 64-slot
chunks. Its six cursors remain ordered `F <= L <= R <= A <= B <= E`; they delimit the front, left,
right, accumulator, and back regions while the two fields `aggregateRA` and `aggregateB` carry the
products that do not live in queue slots. Every insertion or eviction performs one bounded fixup:
front exhaustion collapses the cursor state, `L == B` initializes the next flip, `L == R` advances
the three work cursors, and the remaining case rewrites one left and one right partial aggregate.
There is no unbounded reversal loop.

**Kotlin/JVM status (2026-07-11): Implemented with managed-runtime parity.** `DabaLite<T>` accepts a
runtime `Monoid<T>`; `MeasurePolicy<E, M>` now refines `Monoid<M>`, so existing measure policies can
be reused directly. The implementation preserves the same six-cursor schedule, 64-slot chunk
geometry, callback ceilings, strong callback-failure guarantee, callback-free structural audit,
O(1) clear, and mutable/external-serialization boundary as the C# reference. Its adversarial gate
adds exhaustive noncommutative histories, a 100,000-operation FIFO model, every reachable throwing
callback ordinal, boundary-allocation rollback, and weak-reference checks for retired slots and
chunks.

**C++ status (2026-07-11): Implemented with a nonthrowing publication phase.**
`daba_lite<T, MonoidPolicy>` reuses the static `monoid_policy` vocabulary and a public
`daba_lite_value` constraint: values are copyable but must be nothrow move-constructible and
nothrow move-assignable. Callbacks, allocation, and potentially throwing copies build a private
fixup plan; the final optional-slot rewrites, cursor update, and aggregate replacement then commit
without throwing. Failed policy calls, value copies, and boundary allocation therefore preserve
the exact prior representation. Unique ownership of 64-slot blocks and `std::optional<T>` slots
gives prompt deterministic destruction. The mutable type is deliberately noncopyable, nonmovable,
and unsynchronized.

The C++ gate exhausts noncommutative short histories, runs a 100,000-operation FIFO model, reaches
all fixups and callback ceilings, injects every reachable callback and value-copy failure, rejects
throwing-move values at constraint checking, crosses and churns both block-boundary tiers, proves
provisional-successor rollback and prompt reference release, exercises the installed aggregate
header, and benchmarks DABA slide/query against `std::deque` reaggregation.

**C status (2026-07-11): Implemented with explicit type-erased ownership.** `ft_daba_lite` combines
the existing `ft_value_type` and identity/combine portion of `ft_measure_policy` in
`ft_daba_policy`, adding an injectable allocator. Callback-produced values are staged in seven
fixed, suitably aligned temporary slots and ownership-transferred by byte move into 64-slot blocks
or aggregate fields; values must therefore obey the workspace's relocatable C-object contract.
`ft_daba_lite_move` transfers the unique mutable handle. Callbacks are infallible by the existing C
signature, must return normally, and must not reenter the same handle; every library allocation
failure is status-returning and state-atomic.

The C gate covers all 1,024 short noncommutative histories, a 100,000-operation model, every fixup
and 3/2/1 ceiling, both block-boundary tiers and churn, all four create/two growth/two clear
allocation failpoints, exact ownership reclamation, maximum-alignment storage, clear/reuse, and
populated handle relocation. Debug and Release compile under `/W4 /WX`, and the benchmark harness
includes a DABA slide/aggregate workload.

**Rust status (2026-07-11): Implemented with explicit deterministic-drop semantics.**
`DabaLite<T, M>` over `DabaMonoid<T>` preserves the six-cursor schedule, 64-slot chunk geometry,
three/two/one combine ceilings, callback-free validation, and strong callback-panic guarantee.
Candidate aggregates, slot rewrites, and a possible successor chunk are planned before publication,
so a callback unwind cannot expose a partial mutation. Occupied positions and aggregate fields use
`Rc<T>` to avoid imposing `T: Clone`; the `Rc<RefCell<_>>` cursor substrate deliberately makes the
mutable type `!Send` and `!Sync`. Successful eviction promptly clears its retired slot and severs a
retired predecessor block.

**Native clear divergence.** C, C++, and Rust `clear` are intentionally O(n + c), unlike the
managed O(1) reset: deterministic destruction must release `n` generic owned values across `c`
chunks to satisfy prompt reclamation. All three implementations iteratively break the chain,
neither leaking nor deferring an unbounded retired representation. Insert, eviction, and query
retain worst-case O(1) structural and callback work when value operations and monoid callbacks are
O(1). The Rust gate additionally includes a non-`Clone` value and a compile-fail `!Send`/`!Sync`
contract.

**What it is.** The De-Amortized Banker's Aggregator was introduced by Tangwongsan, Hirzel, and
Schneider at DEBS 2017; the 2021 journal article introduced the Lite representation. It maintains
the FIFO-ordered aggregate of a dynamically sized window for any associative monoid, without
requiring commutativity or an inverse. `Insert`, `Evict`/`TryEvict`, and `Aggregate` invoke
`Combine` at most three, two, and one times respectively; an empty query invokes no `Combine`.
These invocation ceilings are unconditional, but the complete operations are worst-case O(1) only
when both `IMonoid<T>.Combine` and `Empty` are O(1). A finger tree answers the same FIFO-window
query in O(log n); DABA Lite removes that logarithm for this deliberately mutable special case.

**Managed-runtime contract and ownership.** In both managed implementations, a throwing monoid
callback leaves the published window unchanged for every mutating operation. `Clear`/`clear` swaps
in one fresh empty chunk in O(1), invokes combine zero times, and likewise commits only after
obtaining the identity. Successful eviction promptly clears a retired reference-bearing slot and
severs an obsolete predecessor chunk. The begin cursor is derived from the current first chunk
rather than a construction-time root, preventing an unbounded retained prefix. Neither class has
an oldest-value or enumeration API: the Lite schedule intentionally overwrites raw values with
partial aggregates. Neither provides synchronization; callers must not overlap access to one
instance without external serialization.

**Space and validation.** The paper's logical accounting is `n + 2` values of type `T`: `n` queue
partial aggregates plus the two aggregate fields. The chunked managed representations instead
allocate queue capacity for `n` live positions plus 1 through 127 slack slots; an empty instance
retains one 64-slot block. Two aggregate fields, six cursors, and block links are additional
metadata. `ValidateStructure`/`validateStructure` is deliberately callback-free and content-blind:
in O(c) time and space for `c` active chunks it checks links, cursor reachability and order,
count/distance equations, DABA region-size equations, and the chunk/slack bound. It returns
`DabaLiteStatistics` with the count, five region lengths, active block count, allocated capacity,
and slack count. Because original values may already have been
overwritten, aggregate correctness is tested against an external FIFO model rather than reconstructed
by the validator.

**Why it fits.** It reuses each language's existing monoid vocabulary, is compact, and has a direct
naive re-aggregation oracle. Its adversarial gate exhausts short histories, covers all
four fixup phases over the six-cursor state, crosses the 63/64/65 and 127/128/129 chunk boundaries,
proves retained-reference release and clear/reuse, injects failures at every bounded callback
position, checks structural statistics, and observes the three/two/at-most-one `Combine` ceilings.

**Caveat.** It is ephemeral, not persistent. That is an intentional fit for transient streaming
state, not a persistent replacement for `FingerTreeDeque<T>`.

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

**Kotlin/JVM status (2026-07-12): Implemented with node GCAS, root/main RDCSS, and tomb cleanup.**
`ConcurrentHashTrie<K,V>` mirrors the C# protocol with JVM atomics: bitmap C-nodes,
singleton/collision/tomb nodes, helping node-local descriptors, linearizable O(1) generation
snapshots, deletion-path contraction, and lazy child renewal. Its gate includes the deterministic
committed-writer/snapshot schedule, deep and equal-hash contraction churn, contended same-key
updates, a 250-round short-history serialization oracle under three hash policies, structural
validation, retained generations, and explicit snapshot-to-CHAMP conversion.

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
economics therefore cap this at the implemented C# + Kotlin/JVM managed tier, which the porting
guide acknowledges explicitly.

**Verdict: Implemented for the managed tier.** The use case is hot shared caches that periodically
publish immutable snapshots into the persistent world. Deterministic descriptor interleavings,
model-checked short histories, retained-generation tests, and larger contention stress form the
required concurrency-validation tier; the native reclamation exception remains explicit above.

## Axis 2: Hybrid And Adaptive Representations

### Size-tiered small representations

**Status: Explicitly postponed; the current persistent facades do not switch to flat small-size
tiers. Axis 2 has independently shipped the C1 positional cursor, C2 measured/text cursor, the T2
C# owner-token CHAMP transient, and semantic sibling-language editing sessions, while the
fixed-layout evidence gate still precedes any size-tier re-entry.**

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

**Verdict: Strong but postponed.** If reactivated, apply first to
`PersistentHashMap`/`PersistentHashSet`; the sequence facades already have a partial answer in the
rope's chunking. A re-entry benchmark must distinguish the gain from size selection from the gain
independently demonstrated by the Axis 2 fixed-layout tier, if that tier ships.

### The transient -> persistent -> frozen lifecycle

**Status (2026-07-13): T2 owner-token transients are shipped for the C# CHAMP map and set, and C,
C++, Haskell, Kotlin, and Rust now ship semantic one-way editing sessions over their persistent
path-copying kernels. The repository-owned frozen collection tier is not shipped: the faithful F0
prototype exists, but its advance/defer evidence and the dependent F1 layout evidence are postponed
until isolated benchmark runs, and F2 is not authorized. No frozen parity is committed. The
[T2 shipment decision](../../src/CSharp/docs/Hamt/transient-t2-decision.md) records the public
boundary; the [Axis 2 final plan](../proposals/axis2-lifecycle-and-sequence-cursors.md) remains
normative for the unshipped frozen phases.**

**The correction.** Existing HAMT bulk construction, sorted builders, and rope/RRB frozen-prefix
builders are staging or pattern-specific builders, not transients in the persistent-collection
sense. They do not adopt an arbitrary persistent root, mutate uniquely owned paths, and publish the
same nodes in O(1). The Ctrie's generation snapshot is a third mechanism again. The C# reference
keeps five terms distinct: staging builder, owner-token transient, persistent version, Ctrie
snapshot view, and read-optimized frozen collection. Sibling semantic editing sessions preserve the
transient lifecycle contract but do not claim the owner-token representation.

**First lifecycle.** C# CHAMP map/set is the reference family:

```text
Persistent --O(1) ToTransient--> single-owner Transient --O(1) Persist--> Persistent  [C# optimized T2]
Persistent --O(1) adopt-------> single-owner session ---O(1) publish--> Persistent  [C/C++/Haskell/Kotlin/Rust; path-copy edits]
Persistent --O(n) Freeze-------> fixed-layout Frozen --O(n) ToPersistent--> Persistent [planned]
```

The shipped C# transient is one-way. `Persist()` consumes the session, seals any allocated edit token,
publishes without traversing the graph, and invalidates the mutable object and every previously
obtained view or enumerator. Edits mutate only nodes and arrays proven owned by the active token and
path-copy shared/sealed storage.
A clean `source.ToTransient().Persist()` returns the exact source object, including after logical
no-op edits. Track T did not jump directly to the public API: T0 qualified the many-edits-per-
publication workload, T1 selected the production-representative separate-node layout after failure,
retained-memory, and material-win gates, and T2 added the public map/set surfaces plus their consumed-
alias and failpoint tests. A later reusable owner-token builder, if justified, remains a separate
surface rather than a semantic change to the existing staging builders. See the
[T0](../../src/CSharp/docs/Hamt/transient-t0-decision.md),
[T1](../../src/CSharp/docs/Hamt/transient-t1-decision.md), and
[T2](../../src/CSharp/docs/Hamt/transient-t2-decision.md) decisions for the successive boundaries.

The sibling sessions port the observable one-way lifecycle, not the optimized kernel. They preserve
policy identity, stored representatives, exact clean/no-op root identity, retained sources,
receiver-policy set relations, and failure-atomic edits, but every changed point edit computes an
ordinary persistent path-copy successor before replacing the current session value. Consequently
they make no transient-throughput or allocation-win claim. C explicit clones alias one ref-counted
consumed state and expose lifecycle/iterator failures through status codes; C++ sessions are
move-only, terminally invalidate affected sessions after a throwing policy move, and document the
separate publication caveat; Haskell sessions
live in `IO`; Kotlin checks consumption dynamically and binds views to session versions; Rust uses
consuming ownership for publication.

The first frozen types are separate `FrozenHashMap<TKey, TValue>` and `FrozenHashSet<T>` types with
no update-shaped API. They use one offline-built general hash layout for every count and key type,
preserve comparer identity and stored representatives, preserve the source persistent version's
exact traversal sequence, and perform no library-controlled allocation on point lookup (user
comparer callbacks remain outside that guarantee). Enumeration is stable for an unchanged instance
but otherwise unspecified—never insertion, sorted, canonical, or serialization order. Frozen-to-
persistent conversion is visibly O(n); `Freeze()` on an already-frozen value is identity. Track F
starts with one faithful packed-index signal (F0), then compares fixed layouts only after a credible
read-heavy regime appears (F1); F2 ships only behind lookup/enumeration/memory and realistic
construction break-even evidence. F1's fixed-layout evidence collection is postponed for an
isolated machine run, but the [F0 signal record](../../src/CSharp/docs/Hamt/frozen-f0-signal-decision.md)
must first advance the track; no
F2 public type or API is authorized.

The benchmark-local candidates already cover the low-risk layout-independent reverse conversion:
each packed source-order entry sequence rebuilds canonical CHAMP through the existing bulk builder.
An untimed verifier locks comparer identity, exact traversal sequence, first key and last distinct
value representatives, null/collision behavior, empty maps, throwing callbacks, fixed slot/load
diagnostics, and candidate -> persistent -> same-candidate reconstruction. These artifacts prepare
the F1/F2 semantic boundary without selecting a probe layout or publishing a frozen type.

**Deliberate non-adaptivity in v1.** Tiny flat layouts, string prefix/length dispatch, integer-key
routing, binary-fuse/ribbon filters, Eytzinger/PGM sorted layouts, and count-derived selectors are
all postponed. Benchmarks may contain tiny and string datasets, but those are workloads, not
permission to select a representation. This isolates the value of the temporal lifecycle before
combining it with size or key specialization.

**Verdict: CHAMP editing sessions are implemented across all six languages; only C# claims the
owner-token optimization, and the frozen tier remains a strong evidence-gated candidate.** The
independent C1 cursor, T2 transient, and sibling semantic-session shipments do not clear Track F.
Advance F only through
the final plan's semantic, failure-atomicity, retained-memory, lookup, enumeration, construction,
and break-even gates. Evaluate RRB transients, Ctrie snapshot-to-frozen conversion, and other frozen
families only after the corresponding C# reference contract settles. Sibling lifecycle parity does
not authorize sibling frozen types or a claim of owner-token edit performance.

### Cursor / zipper over the sequence family

**Status (2026-07-14): C# C1, C2, and C3 are shipped; Haskell and Kotlin have measured/text and
positional semantic checkpoints; C++ and Rust have positional checkpoints.**
`Rope<T>.GetCursor(position)` and the public
readonly `RopeCursor<T>` implement the positional version-bound gap cursor.
`MeasuredRope<T, TMeasure, TMeasureOps>.GetCursor(position)` and
`TryGetCursorByMeasure` add the measured/text specialization through the public readonly
`MeasuredRopeCursor<T, TMeasure, TMeasureOps>`. The Tour retains measured cursor versions for
undo/redo, and the Editor demonstrates a sixteen-edit local Unicode/line/branch history. C4 cursor
adapters remain consumer-gated. C++ `rope_cursor<T>`, Haskell `RopeCursor a`, plus Kotlin and Rust `RopeCursor<T>` preserve
the positional gap, immutable branching, unconditional replacement, and retained-snapshot semantics
through root-sharing snapshot-plus-position facades. Haskell's `MeasuredRopeCursor v a` and Kotlin's
`MeasuredRopeCursor<T, M>` add ordered before/after measures and absolute prefix search over their
existing measured checkpoint cores. Their `TextRopeCursor` surfaces retain the existing text helpers
with `Char`-element Haskell and UTF-16 Kotlin positions. They deliberately do not claim the C# zipper
representation or its focus-local complexity; measured/text cursors remain unported in C, C++, and
Rust, and the C positional cursor remains unported. The
[Axis 2 final cursor plan](../proposals/axis2-lifecycle-and-sequence-cursors.md) remains normative for
the unshipped phases, while the [C0 decision record](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md)
records the selected representation and proof boundary for C1.

**Selected representation.** C0 selected the zipper-as-version representation as a readonly struct
over immutable version and navigation-context references; it did not select the focused-root
alternative. The shipped zipper has a bounded 16-element active focus and at most one partial carry
smaller than 256 elements on each side. Movement and edits return cursor values. Navigation shares
the logical sequence version and snapshot memo; an edit creates a new version state. The public
[source](../../src/CSharp/src/Tools.DataStructures.FingerTree/Rope.Cursor.cs),
[usage guide](../../src/CSharp/docs/FingerTree/usage.md), and
[API specification](../../src/CSharp/docs/FingerTree/api-specification.md) describe that current
surface.

**Measured extension.** C2 preserves the same gap and version model while exposing exact ordered
`MeasureBefore` and `MeasureAfter`. Absolute measure seek selects the gap before the first element
whose inclusive prefix satisfies a delegate or closure-free struct predicate. Existing cursor
lineages prepare at most the selected ordinary fragment and share its element measures with
descendants; one-shot source seeks retain no full element-measure array. Lazy prefix/suffix tables,
fragment preparation, and measured snapshot publication are failure-atomic and safe under racing
readers. With `NewlineMeasure`, the cursor uses the existing UTF-16 text representation and
line/column helpers rather than introducing a second text core. The
[C2 decision record](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md) owns the
locked local-edit/query gates and callback ceilings.

**Kotlin measured/text checkpoint.** Kotlin retains an already-canonical `MeasuredRope<T, M>` plus
its gap rather than porting the focus/carry zipper. Creation, navigation, positional seek, and
snapshot are O(1); ordered measure reads, peeks, point edits, and absolute measure search are
O(log n), and range insertion is O(m + log n). `MeasuredRopeCursorSearch<T, M>` keeps a usable end
cursor on a miss. The thin `TextRopeCursor` preserves exact text-facade identity across navigation
and O(1)-wraps edited measured snapshots so line/string helpers remain available. Checked `Int`
growth, noncommutative measure partitions, callback failure retry, exact-maximum shared DAGs, UTF-16
text behavior, retained branches, deterministic models, and concurrent readers are correctness
gates; none is benchmark or C# allocation/locality evidence. The
[Kotlin API notes](../../src/Kotlin/FingerTree/docs/api-notes.md) own this checkpoint's contracts and
complexity boundary.

**Haskell measured/text checkpoint.** Haskell retains its existing chunked `MeasuredRope v a` plus
a strict validated gap. `measureBefore` and `measureAfter` preserve left-to-right monoid order;
absolute measure search returns an opaque result containing either the first satisfying gap or a
usable end cursor on a miss. Creation, navigation, positional seek, and snapshot are O(1); ordered
measure reads, peeks, point edits, and search are O(log n) plus one bounded 64-element chunk scan,
and range insertion is O(m + log n). The `TextRopeCursor` alias preserves every existing text helper
without wrapping or materialization and counts Haskell `Char` elements. Its contract
assumes the aliased text rope came from `fromString`/`fromText` or the extensionally identical newline
measure; the alias does not nominally enforce that policy. Checked `Int` counts, bounded lazy-list
preflight, noncommutative measures/search, element and monoid callback failure retry, exact-maximum
shared DAGs, checked derived line count, text edits, retained versions, deterministic models, and
concurrent readers are correctness gates. They provide no zipper, memo, allocation, callback-count,
locality, or benchmark evidence. The [Haskell workspace README](../../src/Haskell/FingerTree/README.md)
owns this checkpoint's pure-evaluation and complexity boundary.

**Gap and version semantics.** A cursor denotes a boundary `0 .. Count`, not an element. Previous
peek/movement/backspace address `p - 1`; next peek/movement/delete/replace address `p`. Insertion
returns the gap after the inserted values, backspace returns `p - 1`, and forward deletion or
replacement keeps `p`. Empty, start, and end gaps are first-class. `Seek(Position)` and empty
`InsertRange` preserve the exact shared version/context state; `ReplaceNext` deliberately creates an
edit version without an element-equality check. Every retained cursor remains valid and may be
edited to create an independent branch. There is no implicit redirection to a later rope, bookmark
rebase, or cross-version application. The default struct is invalid; the initialized empty cursor
comes from `Rope<T>.Empty.GetCursor()`.

**Snapshots, sharing, and reads.** A cursor created from a rope starts with that exact rope as its
clean cached snapshot. A dirty first `Snapshot()` performs bounded focus/carry packing plus a
canonical tree join and publishes one winner through `Interlocked.CompareExchange`; racing callers
return that same rope reference. A failed candidate publishes nothing, so the cursor remains
reusable. Later snapshots from any navigation cursor over the logical version are O(1) and
reference-identical. Edited snapshots retain untouched immutable chunk storage; ancestors and
sibling branches remain independently readable. Initialized cursor values are immutable and safe
for concurrent reads, including concurrent first-snapshot publication.

**Proven complexity scope.** Creation and arbitrary `Seek` are O(log n); seeking a dirty edit
version may first materialize its canonical snapshot. Focus-local peeks and movement and local
single-element edits are O(1) amortized along one **linear version lineage**, with O(log n)
worst-case boundary repair. `InsertRange` of `m` elements is O(m + log n) amortized. Dirty
`Snapshot()` is bounded focus/carry packing plus an O(log n) tree join; clean repeated snapshots are
O(1). C0 did not prove an arbitrary-version-DAG amortized bound: editing `b` independently retained
cursors at a carry/chunk or lazy-spine boundary has the conservative O(b log n) aggregate bound,
plus bounded focus/carry copying per branch. Potential consumed by one child cannot pay for a
sibling.

**Remaining phases.** C4 separately evaluates `FingerTreeDeque<T>` and leaves editable RRB,
`ReversibleDeque`, raw `FingerTree`, Tungsten, bookmark/rebase, and range-update cursors deferred
until a consumer and benchmark justify them.

**Verdict: C1, C2, and C3 implemented; C4 remains consumer-gated.** The
positional and measured cursors separately cleared their named local-edit, query, allocation,
callback, and validation gates; the samples lock retained-history, branch, coordinate, and
cadence-sixteen transcripts. The C++, Haskell, Kotlin, and Rust positional checkpoints plus the
Haskell/Kotlin measured/text checkpoints add semantic behavior parity without asserting zipper or
benchmark parity. Those results do not
pre-approve later sequence adapters or a broader branched-history complexity claim.

### Key-type-specialized map construction

**Status: Explicitly postponed.** The Patricia types remain explicit and no general factory chooses
a core from runtime key type.

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

**Verdict: Plausible but postponed,** in the separate-types form. The ART core itself is a
meaningful project; gate it on a consumer with real prefix-query or byte-ordered-key needs after the
existing Patricia option has been considered explicitly.

### Self-adjusting structures: the recorded rejection

Splay trees, move-to-front lists, and other read-adaptive structures fundamentally fight
persistence, for two independent reasons worth recording so the idea is not re-litigated:

1. **Reads would allocate.** Adapting on read means path-copying on read - every lookup produces a
   new version or mutates shared state, destroying both the allocation-free read contract and
   thread-safe sharing of versions.
2. **Shared versions have contradictory access patterns.** Two consumers holding the same version
   would each want it shaped for *their* access sequence; the amortized potential arguments assume
   one linear history (the same failure mode as the derived catalog's rule 2).

The substitutes in this catalog are the shipped C# positional rope cursor (per-consumer locality,
Axis 2 C1), later consumer-gated cursors, and the planned `Freeze()` read-phase optimization.
Self-adjusting shared state remains unnecessary. **Reject.**

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

The shipped zip-tree core from axis 1 is also this axis's specialization: the niche is "collections
compared far more often than modified" (memoization keys, configuration fingerprints, cache
validity stamps). Under one coherent retained policy, memoized digests give O(1) inequality in the
usual non-collision case and shared identity prunes equality; independently allocated equal sets
still require O(n) semantic comparison. See the axis 1 entry for the policy and representative
qualifications.

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
3. **Say which canonicality you mean.** CHAMP canonicalizes hash-trie topology but leaves collision
   order and stored representatives history-dependent; its structural work benefits from shared
   identity and is O(n + m) without it. The zip set converges only under one coherent retained rank
   policy and the same representatives. The MST alone defines a policy-bound canonical wire and
   content address suitable for cross-process pruning, proofs, and synchronization. These are
   related optimization opportunities, not one blanket O(divergence) or unique-representation
   guarantee.
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

## Implementation Status And Remaining Sequencing

### Shipped reference cores and Axis 2 surfaces

The implementation wave described by this catalog has already landed these reference and port surfaces:

- CHAMP canonical nodes plus structural equality/diff;
- the C# `PersistentHashMap<TKey, TValue>.Transient` and `PersistentHashSet<T>.Transient` one-way
  owner-token editing sessions;
- semantic one-way CHAMP map/set editing sessions in C, C++, Haskell, Kotlin, and Rust, retaining
  persistent path-copy costs;
- 32-bit and 64-bit Patricia maps and sets;
- `RrbVector<T>`;
- the Merkle search tree, including its deterministic wire, bounded verification, proofs, sync,
  and merge infrastructure;
- `CanonicalSortedSet<T>` with keyed zip-zip ranks;
- `BrodalOkasakiHeap<T>` and `PrioritySearchQueue<TKey, TPriority, TValue>`;
- `DabaLite<T, TMonoid>`;
- the managed Ctrie with O(1) immutable snapshots; and
- the Axis 2 C1 positional `RopeCursor<T>` and C2 measured/text
  `MeasuredRopeCursor<T, TMeasure, TMeasureOps>` in C#, plus C++ `rope_cursor<T>`, Haskell
  `RopeCursor a`, and Kotlin/Rust `RopeCursor<T>` snapshot-plus-gap positional semantic checkpoints,
  with Haskell `MeasuredRopeCursor v a`/`TextRopeCursor` and Kotlin
  `MeasuredRopeCursor<T, M>`/`TextRopeCursor` measured/text checkpoints.

CHAMP, Patricia, and RRB have also advanced through the sibling-language work recorded in their
entries; the canonical zip-zip set, Brodal-Okasaki heap, and priority-search queue are implemented
across all six languages, and DABA Lite now exists in every applicable imperative
language (C#, C, C++, Kotlin/JVM, and Rust). The Ctrie's deliberate parity boundary remains C# and
Kotlin/JVM. The Merkle search tree's full trust-boundary tier is complete across all six languages.
The one-way CHAMP editing lifecycle now spans all six languages; the owner-token in-place-edit
optimization and its performance evidence remain C#-only. These are current-state implementation
records, not candidates awaiting a consumer.
Future work on the Axis 1 cores is ordinary hardening, measurement, and demand-driven porting. The
C++/Haskell/Kotlin/Rust checkpoints make no zipper or focus-local complexity claim; measured/text
cursor parity now spans C#, Haskell, and Kotlin but remains absent in C, C++, and Rust. The cursor's C4
extensions retain the separate status recorded in its entry above.

### Remaining candidate sequencing

For work that has not shipped, continue applying the derived catalog's parity-economics rule: stage
a C# reference first, and promote it only with proven value. Items shared with the
[2026-07-09 proposal](../proposals/new-data-structures-2026-07-09.md) keep that proposal's scheduling
slot unless a later dedicated plan says otherwise. The
[Axis 2 final plan](../proposals/axis2-lifecycle-and-sequence-cursors.md) now owns lifecycle/cursor detail.
The cursor's P0/C0/C1/C2/C3 tranche and the transient's P0/T0/T1/T2 tranche are complete;
remaining work is sequenced as follows:

1. **C1 is shipped:** C0 selected the readonly-struct zipper-as-version with focus 16 and flush 256,
   closed focused-root escalation, and published the linear-lineage/O(b log n) branch scope.
2. **T2's optimized kernel is shipped in C#, and semantic sessions are shipped across the sibling
   languages:** T0 qualified the clustered many-edit regime, T1 selected the direct separate-node
   owner-token kernel, and T2 published the C# one-way CHAMP map/set transient after lifecycle,
   failure, retained-memory, and API-shape gates. C, C++, Haskell, Kotlin, and Rust port the
   lifecycle through persistent path copying without inheriting the performance claim.
3. **C2 is shipped:** the measured/text cursor cleared its measure-law, failure/race, text-helper,
   callback, allocation, dirty-query, and measured-workload gates.
4. **C3 is shipped:** Editor and Tour retain measured cursors, use the measured cadence of sixteen,
   and smoke-lock their undo, branch, Unicode, and line/column transcripts.
5. Evaluate **C4 later sequence cursors** separately and only for a named consumer; do not infer a
   deque, RRB, reversible-deque, raw-FingerTree, or Tungsten cursor from C1.
6. Complete the postponed **F0 packed-index signal gate** in isolation, then run F1 only if F0
   records an evidence-backed advance. Complete the dependent **F1 fixed-layout evidence collection**
   before any F2 public implementation.
   Advance F2 only if one general C# frozen hash layout clears the named lookup, enumeration,
   retained-memory, construction, and break-even gates; evaluate F3 Ctrie snapshot conversion only
   after that C# frozen contract ships.
7. **Range-update sequence**, independently reviewing and law-testing the measure-action interface.
   It is not a cursor prerequisite; the later styled-text sample depends on both tracks.
8. **Order-maintenance list** and **persistent chunked bitset**, each only for a concrete client not
   served by existing composition.
9. **Styled-text rope sample**, after measured cursor and range-update foundations settle.

This numbering expresses dependencies and gates, not a ceremonial landing order: the independent C3
and T2 shipments do not clear C4 or the still-unshipped F2 tier.

Automatic size tiers, count/key-specific frozen strategies, key-type-specialized factories, and
unrequested sequence cursor families are postponed rather than placed in this active sequence.
They require a named consumer or benchmark demonstrating that the fixed general representation is
the bottleneck.

The rejected structures remain rejected; none should silently re-enter the schedule without new
evidence addressing the objection recorded in its entry.

## References

Implemented entries were checked against their primary sources during design and hardening. Re-read
the cited source before beginning any still-unimplemented candidate rather than relying on this
catalog's summary alone.

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
  Time*, DEBS 2017; *In-Order Sliding-Window Aggregation in Worst-Case Constant Time*, VLDB
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
  the original committed slate this catalog complements. Patricia and RRB are now implemented;
  its cursor/zipper item is historical and refined by the Axis 2 final plan.
- [Axis 2 final lifecycle and sequence-cursor plan](../proposals/axis2-lifecycle-and-sequence-cursors.md) -
  the normative C#-first state machine, API sketches, complexity corrections, benchmark gates,
  deferrals, and rollout for the active lifecycle/cursor work.
- [Data structure catalog](data-structure-catalog.md) - the concise shipped-surface index. Implemented
  entries here retain frontier design rationale while their public entry points belong in that index.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) - the parity workflow a
  promoted candidate must satisfy; note the Ctrie entry's explicit parity exception.
- [Semantic contracts](semantic-contracts.md) - the no-op identity, ordering, and policy
  obligations that tiered representations must preserve across tiers.
- [C# FingerTree benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) - the measured
  baselines that the benchmark gates in this document extend.
