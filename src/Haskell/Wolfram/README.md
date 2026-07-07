# Haskell Wolfram Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Haskell Wolfram-collections port
- Scope: Cabal package under `src/Haskell/Wolfram`

`tools-data-structures-wolfram` ports the Wolfram `List` and `Association` collection family to
dependency-light Haskell modules:

- `Data.Structures.Wolfram.List` wraps the Haskell FingerTree deque for persistent list operations.
- `Data.Structures.Wolfram.Association` composes the Haskell HAMT with an internal balanced
  stamp-ordered sequence for Wolfram Association ordering, keyed lookup, positional access, slicing,
  relabeling, and sorting.
- `Data.Structures.Wolfram` re-exports the public collection types.

Build and test from `src/Haskell`:

```powershell
cabal test tools-data-structures-wolfram
```

The `wolfram-test` executable covers list examples, Association ordering rules, custom `HashPolicy`
behavior, relabel stress, and deterministic generated histories against an ordered-pair model.
