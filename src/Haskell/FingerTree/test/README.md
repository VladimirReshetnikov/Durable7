# Haskell FingerTree Tests

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents validating the Haskell FingerTree port
- Scope: `tools-data-structures-fingertree` test executable

Run from `src/Haskell`:

```powershell
cabal test ft-test
```

The dependency-free executable covers measured-tree split/view semantics, deque indexing and sorted
search, reversible deque orientation plus all mixed-orientation append combinations, sorted
bag/set/map facades, stable priority dequeue, interval queries and coalescing, positional ropes,
measured ropes, and newline-aware text helpers.
