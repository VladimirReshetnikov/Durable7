# C FingerTree API Notes

- Status: Current API notes
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing public C APIs
- Scope: C naming, contracts, ownership, and intentional differences from `src/Cpp/FingerTree`

The public C API lives in `tools/data_structures/finger_tree/fingertree.h` and the focused
`tools/data_structures/finger_tree/rrb_vector.h`. For setup and handle-lifetime
examples, start with the [usage guide](usage.md). The API uses opaque handles plus explicit policy callbacks
rather than C++ templates:

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
- deterministic sample executables and a dependency-light benchmark harness.

Deferred from the original measured-tree port:

- allocator customization for the measured-tree/facade core (the independent RRB policy already
  supplies an allocator) and typed macro-generation helpers.

The core now carries the C++ port's shared lazy-middle shape in C form.

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
