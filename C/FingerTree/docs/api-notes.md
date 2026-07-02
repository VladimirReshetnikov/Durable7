# C FingerTree API Notes

- Status: Initial notes
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing public C APIs
- Scope: C naming, contracts, ownership, and intentional differences from `Cpp/FingerTree`

The public C API lives in `tools/data_structures/finger_tree/fingertree.h`. It uses opaque handles plus explicit
policy callbacks rather than C++ templates:

- `ft_value_type` describes element size and optional copy/destroy callbacks.
- `ft_measure_policy` describes the monoid identity, element measure, and measure combine operations.
- `ft_tree_policy` combines the two and must outlive every `ft_tree` created with it.
- Measure values are byte-copied by this checkpoint; custom measure storage should therefore be trivially
  copyable or externally owned.

`ft_tree` is immutable. Operations such as `ft_tree_push_back`, `ft_tree_concat`, `ft_tree_insert_at`, and
`ft_tree_remove_at` return new handles and leave their inputs valid. Handles must be released with
`ft_tree_dispose`. Related wrappers (`ft_sorted_set`, `ft_sorted_multiset`, and `ft_text_rope`) follow the same
persistent-update convention.

## Current Scope

Implemented in this checkpoint:

- strict measured-tree core with reference-counted immutable reps, digits, 2/3 nodes, split, locate, concat,
  endpoint operations, indexing, and traversal;
- size-measured persistent deque alias;
- reversible deque facade with a logical orientation bit and persistent endpoint operations;
- persistent sorted set, sorted multiset, and sorted map wrappers using a runtime comparator;
- generic persistent minimum-priority queue with caller-supplied value and priority copy policies;
- generic closed-interval tree facade, plus a signed 64-bit convenience facade, with insertion, removal,
  containment, first-overlap, and overlap count;
- generic chunked positional rope with cumulative-length indexing, split, concat, insertion, removal, and traversal;
- character text rope facade with insertion/removal/indexing and basic line navigation.

Deferred from the C++ port:

- atomic lazy-middle cells and the full persistent amortization/concurrency argument;
- measured rope navigation and samples/benchmarks;
- allocator customization and typed macro-generation helpers.

The strict core is still useful for validating C ownership, policy, and ABI shape before porting the lazy
publication machinery. Documentation and comments should not claim the C++ lazy-spine bounds for this C workspace
until that follow-up lands.
