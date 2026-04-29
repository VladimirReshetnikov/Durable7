# FingerTree Documentation

- Status: Informational
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers and implementers working on the planned C# finger-tree collection
- Scope: Index of design references and local specifications for `src/DataStructures/FingerTree`

## Current Documents

- [Finger Tree Deque API Specification](api-specification.md) defines the planned public C# API shape, semantic contracts, and complexity targets for the persistent catenable deque.

## Reference Material

- [Finger Trees Explained Anew, and Slightly Simplified](<Finger Trees Explained Anew, and Slightly Simplified.md>) is the primary algorithmic reference for the simplified digit and node shape; the matching `.tex` and `.pdf` files remain alongside it.
- [Finger trees: a simple general-purpose data structure](<Finger trees - a simple general-purpose data structure/Finger trees - a simple general-purpose data structure.md>) is the original measured-finger-tree reference for splitting, positional annotations, and ordered-sequence search; the matching `.tex` source remains alongside it, with the PDF one directory up.
- [Haskell containers 0.8 `Data.Sequence.Internal`](containers-0.8/src/Data/Sequence/Internal.hs) is a production reference for strictness, splitting, indexing, and edge-case handling, though it uses the original 1-through-4 digit representation rather than the simplified 1-through-3 digit representation.
