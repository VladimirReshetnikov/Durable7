# Reversible Deque Complexity Audit

- Created (UTC): 2026-07-03T17:22:56Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers verifying cross-language reversible deque behavior
- Scope: O(1) reverse, reversed-operation complexity, concat/split behavior, and known API limits

This audit records the repository-owned reversible deque implementations. The property under review is stricter
than sequence correctness: reversing a deque must be constant work, and later operations on a reversed or
previously reversed deque must not pay a hidden O(n) materialization cost.

## Verdict

| Workspace | Reverse representation | Reversed operations | Concat/split status |
| --- | --- | --- | --- |
| C# FingerTree | Full reversible tree: deep levels and nodes carry a reversal bit. | Endpoint, index, set, insert/remove, split, and copy use logical accessors. | `Concat` is reversal-aware and keeps the same finger-tree concat path for all orientation combinations. |
| C++ FingerTree | Port of the C# reversible tree over immutable `shared_ptr` storage. | Endpoint, index, set, insert/remove, split, and copy use logical accessors. | `concat` is reversal-aware and tested for all orientation combinations. |
| C FingerTree | Facade over `ft_tree` plus a logical `reversed` flag. | Index and endpoint operations map logical positions to the opposite physical end when reversed. | Not applicable: the public `ft_reversible_deque` facade does not expose concat or split. |
| Haskell FingerTree | Strict reversible tree: deep levels and grouping nodes carry a reversal bit. | Endpoint, index, append, and traversal use logical accessors. | Fixed in this checkpoint: mixed-orientation `append` now glues logical digits through the tree instead of `toList`/`fromList` reification. |
| Rust FingerTree | `DequeTree::Reversed` wraps a shared root and cancels double reverse. | Endpoint, index, set, insert/remove, split, concat, iteration, and materialization interpret mirrored nodes. | Fixed in this checkpoint: mixed-orientation concat/split/pop stay tree-based instead of `to_vec`/`from_vec` reification. |

## Evidence

C#:

- `src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs` delegates `Reverse()` to
  `_root.Mirror()` and `Concat` to `RevTreeOps.Concat`.
- `src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleTree.cs` makes deep
  `Mirror()` flip a bit and implements `Glue` through `LogicalPrefix`, `LogicalMiddle`, and `LogicalSuffix`.
- `src/CSharp/FingerTree/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleElements.cs` gives
  internal nodes the same O(1) mirror bit and logical child access.
- `src/CSharp/FingerTree/tests/Tools.DataStructures.FingerTree.Tests/ReversibleDequeTests.cs` covers all four
  concat orientation combinations, reversed split reconstruction, deep reversed-middle updates, and constant
  allocation for whole-deque reverse.

C++:

- `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/reversible_deque.hpp` delegates `reverse()` to
  `root_.mirror()` and `concat` to `detail::rev_concat`.
- `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/detail/reversible_tree.hpp` mirrors the C#
  logical-prefix/middle/suffix algorithm; `rev_concat_with_middle` builds bridges from logical digits.
- `src/Cpp/FingerTree/tests/reversible_deque_tests.cpp` covers all four concat orientation combinations,
  reversed split reconstruction, deep reversed-middle updates, random histories with reverse, and allocation
  flatness for repeated reverse calls at different sizes.

C:

- `src/C/FingerTree/include/tools/data_structures/finger_tree/fingertree.h` exposes `ft_reversible_deque` as
  `{ ft_tree tree; bool reversed; }` and declares reverse, index, endpoint push/pop, and copy/dispose operations.
- `src/C/FingerTree/src/fingertree.c` implements `ft_reversible_deque_reverse` by copying the shared `ft_tree`
  handle and toggling the orientation flag. Indexing maps `index` to `size - 1 - index` when reversed; front/back
  and push/pop delegate to the corresponding physical endpoint.
- `src/C/FingerTree/tests/fingertree_c_tests.c` covers reversal, logical indexing, endpoint edits, pop, and
  persistence. The C docs now state that reversible concat/split are not part of the public C facade.

Haskell:

- `src/Haskell/FingerTree/src/Data/Structures/FingerTree/ReversibleDeque.hs` now owns a strict reversible tree
  with `Tree` and `Elem` reversal bits. `reverse` mirrors the root in O(1).
- `append` delegates to `treeConcat`; its `glue` path combines logical suffix/middle/prefix digits and recurses
  through `logicalMiddle`, matching the C#/C++ reversal-aware concat shape instead of flattening either operand.
- `viewL`, `viewR`, `index`, and `toList` read through logical orientation helpers, so concatenated reversed
  operands do not carry a deferred normalization step.
- `src/Haskell/FingerTree/test/Main.hs` covers all four append orientation combinations plus a larger
  reverse/reverse append with endpoint, boundary-index, count, and round-trip checks.

Rust:

- `src/Rust/FingerTree/src/deque.rs` now has a `DequeTree::Reversed` node with cached length and height. `mirror`
  wraps nontrivial trees and cancels an existing wrapper.
- `split_at`, `first`, `last`, `get`, `set`, `bound_index`, `copy_to_vec`, iteration, balancing, and concat all
  interpret mirrored nodes rather than flattening them.
- `ReversibleDeque<T>` delegates logical operations to a `PersistentDeque<T>` whose root already encodes the
  logical orientation. The former reversed-flag paths that rebuilt vectors for `pop_front`, `pop_back`,
  `split_at`, and mixed-orientation `concat` have been removed.
- Rust tests now include storage-sharing guards for reverse, edits after reverse, and mixed-orientation
  concat/split/pop over larger trees.
