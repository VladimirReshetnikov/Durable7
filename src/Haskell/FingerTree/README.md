# Haskell FingerTree

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell finger-tree family port
- Scope: `tools-data-structures-fingertree` package

This package ports the repository finger-tree family to Haskell. It includes a general measured
finger tree, a size-measured deque, a reversible deque, sorted bag/set/map facades, a stable
meldable priority queue, interval tree helpers, positional ropes, measured ropes, and text-rope
navigation helpers.

`ReversibleDeque a` uses its own orientation-aware strict finger-tree core. `reverse` is an O(1)
mirror of the root, and `append` glues logical digits through mirrored middle views, so
mixed-orientation appends such as `append (reverse left) right` remain tree-based instead of
reifying either operand through lists.

The core measured tree follows the Hinze-Paterson shape directly. Some derived Haskell facades use
idiomatic `containers` storage where it preserves the same observable contract more naturally than
copying a stricter managed implementation detail.

```powershell
cd src\Haskell
cabal test ft-test
```

The local [test README](test/README.md) lists the deterministic coverage areas.
