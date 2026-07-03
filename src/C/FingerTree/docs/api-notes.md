# C FingerTree API Notes

- Status: Current API notes
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing public C APIs
- Scope: C naming, contracts, ownership, and intentional differences from `src/Cpp/FingerTree`

The public C API lives in `tools/data_structures/finger_tree/fingertree.h`. For setup and handle-lifetime
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
cannot be reported through `ft_status`; those callbacks terminate the process on allocation failure. Caller-supplied
copy callbacks should either complete successfully or apply their own fatal/allocation policy consistently.

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
- character text rope facade, backed by the chunked rope, with insertion/removal/indexing and basic line
  navigation.
- deterministic sample executables and a dependency-light benchmark harness.

Deferred from the C++ port:

- allocator customization and typed macro-generation helpers.

The core now carries the C++ port's shared lazy-middle shape in C form. Remaining follow-up work is mostly API
ergonomics and allocator/tooling polish rather than the central persistent-amortization mechanism.
