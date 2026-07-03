# Rust FingerTree API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust FingerTree-family port
- Scope: Rust naming, contracts, checkpoint limitations, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-fingertree`, with library name
`tools_data_structures_fingertree`.

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, P>` over `MeasurePolicy<T>`;
- built-in policies `SizeMeasure`, `SumMeasure<T>`, `MaxMeasure`, and `MinMeasure`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, P>`, `TextRope`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

The Rust surface follows Rust conventions:

- fallible indexed operations return `Option` instead of throwing;
- duplicate sorted-map insertion returns `Result<_, DuplicateKeyError>`;
- binary search returns `Result<usize, usize>`, matching Rust's insertion-index convention;
- measure policies are ordinary traits with static functions for identity, element measure, and combine;
- text offsets are character offsets, matching the repository's `Rope<char>` interpretation rather than UTF-8 byte offsets.

This workspace is a semantic checkpoint, not the final lazy finger-tree representation. It preserves immutable
snapshot behavior, stable observable ordering, rank/range semantics, priority stability, closed-interval overlap
semantics, and text line navigation. `PersistentDeque<T>` has moved past the initial vector snapshot and now uses
an `Arc`-shared balanced tree, so nontrivial splits, concatenations, range operations, and point updates share
unchanged subtrees. The general `FingerTree<T, P>` now uses an `Arc`-shared measured tree with cached monoid
measures at every node, so measure-guided split and locate operations can skip whole subtrees and split results
share unchanged structure. `MeasuredRope<T, P>` now reuses that measured tree through an internal count-plus-user
measure policy, so indexed splits, concatenation, point replacement, prefix measurement, and measure-guided locate
share unchanged measured subtrees. `ReversibleDeque<T>`, positional `Rope<T>`/`TextRope` helpers, and derived
facades still use checkpoint storage and do not yet claim the C#/C++ lazy measured-spine complexity or allocation
profile.

Future representation work should keep the Rust public names and result shapes stable while replacing the remaining
checkpoint internals of `ReversibleDeque`, positional ropes/text helpers, and the derived facades with structurally
shared measured-tree nodes.
