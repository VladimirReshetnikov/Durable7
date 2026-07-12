# C FingerTree API Notes

- Status: Current API notes
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing public C APIs
- Scope: C naming, contracts, ownership, and intentional differences from `src/Cpp/FingerTree`

The public C API lives in `tools/data_structures/finger_tree/fingertree.h` and the focused
`tools/data_structures/finger_tree/canonical_sorted_set.h`,
`tools/data_structures/finger_tree/brodal_okasaki_heap.h`,
`tools/data_structures/finger_tree/priority_search_queue.h`,
`tools/data_structures/finger_tree/rrb_vector.h`, and
`tools/data_structures/finger_tree/daba_lite.h` headers. For setup and handle-lifetime examples, start with the
[usage guide](usage.md). The API uses opaque handles plus explicit policy callbacks rather than C++ templates:

- `ft_value_type` describes element size and optional copy/destroy callbacks.
- `ft_measure_policy` describes the monoid identity, element measure, and measure combine operations.
- `ft_tree_policy` combines the two and must outlive every `ft_tree` created with it.
- Measure values are byte-copied by this checkpoint; custom measure storage should therefore be trivially
  copyable or externally owned.
- Tree and node reps use atomic reference counts, so independently held immutable handles may be copied, read,
  updated into new handles, and disposed concurrently when callers still obey normal handle-lifetime rules.
- Deep nodes use shared lazy middle cells for overflow pushes and boundary-pop repairs. Push cells cache their
  resulting middle measure without forcing; pop cells force only when a later operation needs the repaired
  middle or its measure.

`ft_tree` is immutable. Operations such as `ft_tree_push_back`, `ft_tree_concat`, `ft_tree_set_at`,
`ft_tree_insert_at`, and `ft_tree_remove_at` return new handles and leave their inputs valid. Handles must be
released with `ft_tree_dispose`. Wrappers that reference caller-owned policies (`ft_sorted_set` and
`ft_sorted_multiset`) follow the same value-handle convention as long as the external policy outlives all handles.
Self-owned facades (`ft_sorted_map`, `ft_rope`, `ft_measured_rope`, `ft_priority_queue`,
`ft_interval_tree_i64`, `ft_interval_tree`, and `ft_text_rope`) embed policy state referenced by their nested
tree handles; use their `ft_*_move` helpers when relocating an initialized value into another variable.
The independent `ft_canonical_policy` is instead an identity-bearing reference-counted handle retained by every
canonical set initialized from it. Its callback contexts remain caller-owned.

Most allocation failures are reported as `FT_STATUS_NO_MEMORY`. The current `ft_copy_fn` callback contract is
void-returning, so allocations performed inside library-provided deep-copy callbacks for compound facade values
cannot be reported through `ft_status`; those callbacks terminate the process on allocation failure. This
boundary includes read paths that copy stored compound entries out to the caller — for example `ft_rope_at`,
`ft_measured_rope_at`, `ft_sorted_map_entry_at`, priority-queue entry peeks, and interval-tree entry reads —
which can therefore abort under allocation pressure even though neighboring operations return
`FT_STATUS_NO_MEMORY`. Caller-supplied copy callbacks should either complete successfully or apply their own
fatal/allocation policy consistently.

## Current Scope

Implemented in this checkpoint:

- generic measured-tree core with reference-counted immutable reps, digits, 2/3 nodes, split, locate, concat,
  endpoint operations, indexing, indexed replacement, traversal, lazy middle publication, lazy deep-measure
  publication, and atomic shared-snapshot reference counts;
- size-measured persistent deque alias;
- reversible deque with an orientation-aware immutable tree, O(1) reverse, persistent endpoint/index/edit
  operations, concat, split, and traversal. Deep reps and grouping nodes carry reversal bits so mixed-orientation
  concat/split paths keep tree shape instead of reifying the sequence;
- persistent sorted set, sorted multiset, and sorted map wrappers using a runtime comparator;
- independent persistent canonical zip-zip sorted set with cryptographically keyed deterministic ranks,
  fallible callbacks and allocation, reproducible seeded topology, alias-safe updates, bulk construction,
  policy-gated algebra, receiver-policy relations, lazy content digests, and structural diagnostics;
- independent persistent Brodal-Okasaki bootstrapped skew-binomial min-heap with fallible type-erased values,
  constant worst-case insert/meld/minimum, logarithmic worst-case delete-minimum, retained representatives,
  policy-identity-gated melding, and fused-representation diagnostics;
- independent persistent winner-cached priority search queue with fallible type-erased keys, priorities, and
  values, comparer-defined key identity, exact stored representatives, logarithmic keyed updates, constant-time
  minimum selection, logarithmic delete-minimum, key-order traversal, and inclusive range/threshold traversal;
- generic persistent minimum-priority queue with caller-supplied value and priority copy policies;
- generic closed-interval tree facade, plus a signed 64-bit convenience facade, with insertion, removal,
  containment, first-overlap, and overlap count;
- generic chunked positional rope with cumulative-length indexing, split, concat, insertion, removal, and traversal;
- generic measured rope with cached per-chunk user measures, whole/prefix measure reads, cumulative-measure locate
  and split, split, concat, insertion, removal, and traversal;
- character text rope facade, backed by the newline-measured rope, with insertion/removal/indexing and
  logarithmic line/offset navigation plus column-validated offset lookup;
- type-erased `ft_rrb_vector` with 32-element leaves, radix-indexed regular branches without size
  tables, relaxed branches with cumulative `size_t` prefixes, atomic node references, persistent
  endpoint/concat/split/range edits, ordered visitation, structural diagnostics, and an append-only
  snapshot builder;
- mutable `ft_daba_lite` with the six-cursor DABA Lite schedule, 64-slot linked blocks, FIFO
  noncommutative aggregation, injected allocation, structural statistics, and prompt deterministic
  value/block reclamation;
- deterministic sample executables and a dependency-light benchmark harness.

Deferred from the original measured-tree port:

- allocator customization for the measured-tree/facade core (the independent RRB policy already
  supplies an allocator) and typed macro-generation helpers.

The core now carries the C++ port's shared lazy-middle shape in C form.

## Canonical Zip-Zip Sorted-Set Contract

`ft_canonical_sorted_set` is an immutable type-erased binary-search tree whose heap priorities are derived from
the set's policy. A `ft_canonical_policy_config` supplies the stored value size, a required non-null value-type
identity tag, optional copy/destroy hooks, required comparator and 64-bit rank-hash callbacks, callback context,
and allocator. Copy, compare, rank-hash,
visitor, and shape-visitor callbacks are fallible and their status is propagated unchanged. A failing value-copy
callback must leave its destination uninitialized and ownership-free; destroy and deallocation hooks must return
normally. Values may require fundamental C alignment, but extended-aligned values are unsupported.

Policy handles are reference counted and carry algebra compatibility identity. `ft_canonical_policy_copy` preserves that
identity, and sets retain it, so the caller may dispose the original policy handle before the last set. Creating a
second policy with the same configuration and seed intentionally creates a different identity even though it
reproduces ranks and shape. The value-type tag is pointer identity shared by policies whose values may safely cross
into one another's callbacks; its address must remain stable and valid for every retaining handle. Callback
contexts and referenced objects are not copied or owned and must likewise outlive all retaining policy/set
handles. Callbacks and allocator hooks must not reenter an in-flight operation through the
same policy or set handles, including from destroy during disposal. Immutable operations through distinct handles
may run concurrently only when all hooks and contexts they can invoke are thread-safe; moving, disposing, or
otherwise writing the same handle object concurrently requires external synchronization.

The rank derivation is the cross-port `ZZT2` contract:

- random policies obtain a private 32-byte key from the operating-system cryptographic random source;
- seeded policies derive the 32-byte key as `SHA-256("ZZT2" || seed-be64)` and expose the public seed;
- keyed policies copy a caller-supplied key of at least 32 bytes and do not expose it or retain the input buffer;
- for each value, the policy HMACs the big-endian 64-bit callback rank hash with HMAC-SHA-256. The leading-zero
  count of digest bytes 0-7 is the geometric rank, bytes 8-15 are the unsigned secondary rank, and bytes 16-23
  are the content component, all interpreted big-endian.

Windows builds use CNG (`BCrypt`) and non-Windows builds use the maintained OpenSSL Crypto APIs. Cryptographic
backend failures return `FT_STATUS_CRYPTO_FAILURE`. Seeded policies are intended for reproducibility and test
vectors, not adversarial-input hardening: the seed and derived key are public. Random or secret keyed policies
make priorities unpredictable provided the key and the caller's rank-hash domain are appropriate. Key bytes are
zeroed before policy release, but the API does not claim locked memory, resistance to process inspection, or a
general-purpose cryptographic set digest.

The comparator defines set equivalence and ordering. Comparer-equivalent values must return the same rank hash;
detectable violations return `FT_STATUS_INCONSISTENT_POLICY`. Different values may share a rank hash. The heap
order compares geometric rank, then the secondary `uint64_t` using unsigned ordering, then the value comparator,
which makes topology canonical for a policy despite insertion order or rank collisions. `from_array` performs a
stable sort and retains the first representative of every comparer-equivalence class. Incremental add likewise
keeps an existing representative when an equal value is already present.

Nodes and representatives are immutable, separately allocated, atomically reference counted, and shared across
versions. Add, remove, and clear accept exact source/result aliasing and publish only after a complete successor
exists; allocation, callback, or cryptographic failure leaves the original handle and a distinct output untouched.
Bulk construction and every explicit output follow the same success-only publication rule. Disposal and all
tree walks use explicit stacks or intrusive release worklists, so even a fully colliding, comparator-chained tree
does not recurse through the C call stack.

Union, intersection, and difference require the exact same policy identity and otherwise return
`FT_STATUS_INCOMPATIBLE_POLICY`; this preserves rank coherence and structural sharing. Equality, subset/proper
subset, superset/proper superset, and overlap are semantic across identities only when both policies carry the
same value-type tag; a tag mismatch returns `FT_STATUS_INCOMPATIBLE_POLICY` before any value crosses the type-erased
boundary. With a matching tag, the other operand is normalized under the receiver's comparator and rank policy.
These relations are intentionally receiver-relative when two
comparators define different equivalence classes, so reversing the operands can change the answer. Boolean and
set outputs are written only on success.

`try_get_ref` returns a borrowed representative that remains valid only while the source version is retained.
`content_hash` lazily computes a topology/content diagnostic and atomically publishes each immutable node's cache;
concurrent readers may duplicate the computation, but observe only a fully published digest. It is not a stable
cross-policy or collision-resistant serialization hash. Root/node identity, exact shared-node count, preorder
shape visitation, height, and statistics are diagnostic APIs. `validate` checks ordering, re-derived ranks, heap
order, cached counts/digests, cycles, duplicate nodes, and priority-collision statistics; structural invalidity is
reported as `FT_STATUS_OK` with `valid == false`, while allocation, callback, and crypto failures remain statuses.

Expected lookup and persistent point-update cost is O(log n), with O(log n) new nodes on a change; an adversarial
or deliberately colliding rank stream can produce O(n) height and operation cost. Bulk construction is
O(n log n) for stable sorting followed by O(n) Cartesian construction. Algebra and cross-policy normalization use
ordered visitation plus persistent point operations. A first uncached content hash and full validation are O(n),
while a cached root hash is O(1). Explicit-stack implementation preserves stack safety at the O(n) worst case.

## Brodal-Okasaki Heap Contract

`ft_brodal_heap` is the persistent bootstrapped skew-binomial min-heap from Brodal and Okasaki's
worst-case-optimal purely functional priority queue. One rank-zero global root stores the minimum.
Its native `skew_meld` consolidates child forests by rank buckets with carry rather than the managed
ports' `uniquify`/`unionUnique` spelling; the algorithms are equivalent in ranked-tree multiset,
minimum, validity, and complexity.
Its child list fuses the primitive skew-tree children with the root forest of the embedded heap, so
the owning tree rank determines where the structural prefix ends. The rank-zero ambiguity is decoded
in favor of the structural prefix, matching delete-minimum's split and the C#/Haskell semantic oracle.

`ft_brodal_policy_config` supplies the value size, a required stable non-null value-type identity tag,
an optional fallible copy/infallible destroy pair, a fallible comparator, callback context, and allocator.
The policy is a reference-counted identity-bearing handle retained by every heap. Copying a policy handle
preserves meld compatibility; recreating an equivalent config deliberately does not. The tag, callback
contexts, allocator context, and referenced state remain caller-owned and must outlive all retaining handles.
A destroy callback requires a copy callback. A failed copy leaves its destination ownership-free, while
destroy and allocator hooks return normally.

Values, trees, and forest cons cells are separately allocated immutable reference-counted objects. New
versions share untouched trees and representatives. An empty-side meld shares the nonempty operand root;
self-meld is not a no-op and preserves two logical occurrences, including DAG sharing of the same immutable
subgraph. Every inserted representative remains present even when the comparator considers several values
equivalent. Equal-priority drain order is intentionally unspecified. `try_get_minimum_ref` borrows the exact
stored representative while its source version lives. `try_get_minimum_copy` and successful
`try_delete_minimum` instead construct an independent owned copy; the latter returns the exact representative
removed together with the remainder.

Insert and nonempty meld compare only the global roots and, when the first two forest ranks match, perform one
skew link. The audited ceiling is at most five value comparisons independent of heap size; minimum lookup and
structural visitation perform none. Insert, minimum, and meld are O(1) worst-case. Delete-minimum scans,
decomposes, consolidates, and restores O(log n) ranked trees, using O(log n) comparisons and transient native
storage; the test ceiling is `32 * ceil(log2(n + 1)) + 8`. Array construction is O(n) by repeated worst-case
O(1) insertion. These are worst-case, not amortized, claims.

All persistent operations stage complete successors before publication and support exact result/operand
aliasing. Allocation or callback failure leaves source handles and distinct outputs untouched. In
`try_delete_minimum`, a failing representative copy also discards the staged remainder, so neither output is
published. Disposal uses an intrusive nonallocating release worklist, and delete/visit/validation helpers use
explicit arrays rather than recursive C calls. Concurrent reads, copies, updates into distinct handles, and
disposals of independently owned handles are safe only when all reachable callbacks/allocator hooks are safe
for that call pattern. Reentrancy through the same policy/heap handles and unsynchronized writes or disposal of
one handle object are unsupported.

`visit` exposes unspecified structural order. `visit_shape` additionally reports logical tree identity, rank,
raw fused-child count, and depth, including repeated shared subgraphs after self-meld. `validate` checks the
rank-zero global root, the fused primitive/embedded boundary of every ranked tree, skew-forest rank discipline,
parent/child heap order, logical cardinality, and traversal depth. It returns count, root-forest length, maximum
rank, and maximum depth. Structural invalidity is `FT_STATUS_OK` with `valid == false`; allocator and comparator
failures remain explicit statuses and leave validation outputs unchanged.

## Winner-Cached Priority Search Queue Contract

`ft_priority_search_queue` is an immutable type-erased AVL search tree keyed by the configured key comparator.
Every node caches the winning entry in its subtree, ordered first by the priority comparator and then by the
retained key comparator. That second comparison makes the minimum deterministic when several priorities compare
equivalent. Keyed lookup, set, add, and removal are O(log n); minimum selection is O(1), and deleting the cached
minimum is O(log n). These bounds assume logarithmic AVL height.

An `ft_psq_policy_config` describes key, priority, and value as three independent types. Each has its own required
stable non-null type-identity tag, size, optional fallible copy/infallible destroy pair, equality-callback slot,
and callback context. Key and priority ordering callbacks are separate. Priority equality and value equality are
required for replacement no-op detection. `key.equals` is deliberately optional and is never
invoked: comparer-equivalent keys designate one map slot, and an incoming key's ordinary equality cannot replace
or otherwise affect the first stored key representative. A destroy callback requires a copy callback. Failed
copies leave destinations ownership-free; destroy, allocator, and deallocator hooks return normally.

Policies are reference-counted identity-bearing handles retained by queues and owned entries. Copying a policy
handle preserves identity; independently recreating an equivalent config does not. Type tags, callback contexts,
allocator context, and all referenced state remain caller-owned and must outlive every retaining handle. Hooks
must not reenter an operation in flight through the same policy, queue, or entry handles. Immutable operations
through distinct handles may run concurrently only when every reachable hook and context is safe for that call
pattern. Moving, disposing, or otherwise writing one handle object concurrently requires external
synchronization.

Key, priority, and value representatives are separately allocated immutable reference-counted components. Entry
and AVL-node reps are likewise immutable and atomically reference counted. Updating an existing comparer-
equivalent key shares the original key component into the new entry, while changed priority/value components and
the search path are rebuilt. Untouched subtrees share nodes. `set` applies last-wins priority/value semantics but
shares the complete source root only when the stored and incoming priorities compare equal, priority equality is
true, and value equality is true. `try_add` on an existing key and `remove` on an absent key are root-sharing
no-ops. `from_array` applies entries in array order, retaining the first key representative and the last
priority/value for each key equivalence class.

`try_get_entry_ref` returns borrowed pointers valid only while the source queue version remains alive. Minimum,
try-remove, and delete-minimum operations instead return an `ft_priority_search_entry` that owns the exact stored
key, priority, and value representatives independently of either queue. Dispose that handle when finished. This
owned result is intentionally reference retention rather than user-component copying. Exact source/result queue
aliasing is supported, including operations that also publish an owned entry.

`visit` exposes entries in strict key order without copying components. `visit_at_most` first validates
`minimum_key <= maximum_key`, then visits in key order every entry in the inclusive key interval whose priority is
less than or equal to the supplied threshold. Cached subtree winners prune any subtree whose minimum priority is
already above the threshold. Cost is O(log n + v) for v visited candidate nodes, with O(n) worst case when the
range and threshold admit the whole tree. No Hinze-style pennant output bound is claimed. The focused tests pin
the pruning equations for an exact-key query: if `v` is the number of candidate nodes whose winner is inspected
and `e` is the number emitted, priority comparisons equal `v + e`, and key comparisons equal `1 + 2v`, including
the eager range-order comparison. A threshold below the root winner performs exactly one key and one priority
comparison and visits nothing.

Root identity, per-key node identity, and exact shared-node count are representation diagnostics. Shared-node
count requires exact policy identity. `validate` checks strict key order, AVL balance, cached count/height,
maximum absolute balance factor, and exact cached-winner pointer identity. Structural invalidity returns
`FT_STATUS_OK` with `valid == false`; allocation or comparator failure remains an explicit status and leaves
outputs untouched.

Persistent update, traversal, validation, and release paths use explicit arrays or intrusive nonallocating
worklists rather than recursive C calls. Every operation stages all allocation, component copies, equality tests,
comparisons, and successor construction before success-only output publication. Failure therefore preserves
source handles, exact aliases, and distinct outputs. The API intentionally has no split or slice operation; the
range/threshold visitor is the complete bounded-query surface for this checkpoint.

## DABA Lite Contract

`ft_daba_policy` combines `ft_value_type`, the identity/combine portion of `ft_measure_policy`, and
an allocator pair. DABA Lite stores monoid values directly, so the value and measure sizes must be
equal; the measure callback remains in the shared policy vocabulary but is not invoked. The policy
pointer and both callback contexts must outlive the mutable handle. Use `ft_daba_lite_move` to
relocate ownership: copying a handle by assignment would create two owners of one mutable rep and is
invalid.

The value copy callback and the monoid identity/combine callbacks construct independent owned values
in uninitialized, suitably aligned storage. The implementation stages callback-derived values in a
fixed private plan and transfers ownership into slots or aggregate fields by byte move, destroying
each constructed value exactly once. Values must therefore be relocatable as ordinary C objects and
must not contain self-pointers that become invalid after relocation. `ft_daba_lite_aggregate` follows
the other type-erased output APIs: it constructs an owned value in the caller's uninitialized
destination, and the caller eventually invokes `policy.value.destroy` when that callback is present.

The C callback vocabulary is deliberately void-returning. Copy, destroy, identity, and combine
callbacks must return normally; callback side effects and nonlocal exits are outside the rollback
contract. A callback must not reenter the same `ft_daba_lite` handle: private scratch values and a
provisional end slot/block may exist before logical publication, so recursive observation or
mutation would see an operation in flight. All callback results are nevertheless completed before logical window publication.
Library allocator failures return `FT_STATUS_NO_MEMORY` and leave the size, aggregate, cursor state,
block chain, and occupied slots unchanged. The fixed scratch bound is seven owned temporaries on the
worst insertion/fixup path: inserted aggregate, inserted value, one identity, two final aggregates,
and two pending slot writes.

The structure maintains `F <= L <= R <= A <= B <= E` and performs one incremental-reversal fixup per
slide. Insert invokes combine at most three times, eviction at most twice, and a nonempty aggregate
query exactly once; empty queries invoke identity without combine. Each live position occupies one
slot in a linked sequence of 64-slot blocks. Crossing the front boundary immediately destroys and
deallocates the retired block. `clear` first prepares a replacement block and identity, then commits
the empty state and deterministically destroys all old values and blocks. Consequently clear is
O(n + c) for n live positions in c blocks in C, not the O(1) tracing-GC reset of the C# reference.

The handle is deliberately mutable, non-thread-safe, and non-iterable. DABA Lite overwrites original
values with partial aggregates during reversal, so a general non-invertible monoid cannot recover the
input sequence. `ft_daba_lite_validate` instead checks links, exact slot occupancy, cursor order, size
equations, the two-block slack bound, and statistics without invoking any value or monoid callback.

## RRB Vector Contract

`ft_rrb_policy` combines the existing value copy/destroy vocabulary with required value equality and
an allocator pair. The policy pointer must be identical across concatenated vectors and must outlive
all vectors/builders using it. Injected allocation makes every RRB construction path report
`FT_STATUS_NO_MEMORY`; completed nodes are unwound on failure before any output handle is published.
Updates accept their source handle as the output, so `ft_rrb_vector_set(&v, ..., &v)` and the
corresponding endpoint/concat/range-edit patterns are alias-safe.

Leaves contain one through 32 values. Regular branches have full-capacity children except possibly
the last and navigate by five-bit shifts without allocating prefix sizes. Split and concat may make
a branch relaxed; only those branches own cumulative `size_t` sizes. Lookup and replacement visit
O(log32 n) nodes, while concat/split/range edits rebuild boundary spines and share untouched leaves.
Concat redistributes only the seam and does not enforce a global minimum occupancy elsewhere;
adversarial density ceilings are test gates, not validator invariants. The maximum height is derived
as `(sizeof(size_t) * CHAR_BIT - 1) / 5`, which is twelve on the supported 64-bit targets.
`ft_rrb_vector_validate`, `ft_rrb_vector_root_identity`, and leaf visitation expose representation
diagnostics for tests and embedders without exposing mutable node storage.

`ft_rrb_builder` stages owned 32-value tails. A full tail is transferred to an immutable leaf only
after the builder abandons mutable access; a partial tail is copied during `to_vector`. Clean freezes
retain the cached root, and later appends cannot mutate prior snapshots. Range append clones staging
transactionally, so an allocation failure leaves the builder's logical contents unchanged. Builders
are mutable and not thread-safe; published vectors are immutable and safe for concurrent readers.

## Structural Search Complexity

The generic tree uses cached subtree leaf counts and measures for structural descent. `ft_tree_split_at`
rebuilds only the digit/node path containing the boundary, while `ft_tree_locate` prunes whole subtrees by
testing their cached aggregate measures; `ft_tree_split` composes those two paths. For a monotone measure
predicate, all three operations take O(log n) time and allocate O(log n) structural storage. Split results
share untouched nodes with the source snapshot.

This restores the sibling ports' O(log n) search/edit foundation for the tree core, the interval trees, both
rope facades, and the sorted and priority-queue facades. Every grouping element caches a borrowed last-leaf
signpost. A monotone lower/upper-bound search compares the signpost to reject an entire preceding subtree,
then follows one root-to-leaf path. Membership, add, remove, sorted-map lookup, and priority-queue insertion
therefore perform O(log n) tree work without the former `ft_tree_at` probe copies. The signpost is independent
of the caller's monoidal measure, so a sorted facade can retain an arbitrary measure policy; in particular,
the generic interval tree keeps its `(count, maxHigh)` annotation while reusing the same ordered-search path.

The native suite includes operation-count guards over a 4,096-element tree so a return to leaf-by-leaf split,
locate, or sorted-bound search fails deterministically. Sorted-bound tests additionally use a counting value
policy to prove that read-only key search does not copy payloads.

## Facade Annotations And Chunk Shape

Both interval facades carry a cached `(count, maxHigh)` measure. `try_find_overlap` descends to the first
prefix whose maximum high endpoint reaches the query low in O(log n); overlap counting repeatedly advances
past each hit in O((k + 1) log n) for `k` results. The generic endpoint facade's measure borrows the high
endpoint stored in its owning leaf or shared node. Leaf cloning therefore re-measures from the copied value,
so no annotation points into a released source snapshot.

The positional and measured ropes edit the located chunk directly, split a chunk only when it exceeds the
configured maximum, and merge adjacent boundary chunks on split, concat, and removal whenever they fit.
Edit-heavy workloads consequently remain chunked instead of degenerating toward one leaf per element.
`ft_measured_rope_prefix_measure` descends once and scans at most one bounded chunk: O(log n + chunk-size).

`ft_text_rope` is based on `ft_measured_rope<char>` with a newline-count measure. Line count is O(1), while
`line_of_offset`, `line_start_offset`, `line_column_of`, and the column-validated `offset_of` use measured
descent plus at most one bounded chunk scan.

## Intentional API Differences

- `ft_tree_locate` reports "not found" through its `found` flag with the total measure in
  `measure_before`; the C++ reference instead returns the last element as the hit. This is an
  intentional API difference — porters translating C++ callers must not assume an element is always
  produced.
- `ft_tree_concat` (and the reversible deque's concat) require policy *pointer* identity between
  operands, not just structural compatibility.
