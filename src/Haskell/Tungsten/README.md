# Haskell Tungsten Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Haskell Tungsten-collections port
- Scope: Cabal package under `src/Haskell/Tungsten`

`tools-data-structures-tungsten` ports the Tungsten `List` and `Association` collection family to
dependency-light Haskell modules:

- `Data.Structures.Tungsten.List` wraps the Haskell FingerTree deque for persistent list operations.
- `Data.Structures.Tungsten.Association` composes the Haskell HAMT with an internal balanced
  stamp-ordered sequence for Tungsten Association ordering, keyed lookup, positional access, slicing,
  relabeling, and sorting.
- `Data.Structures.Tungsten` re-exports the public collection types.

Build and test from `src/Haskell`:

```powershell
cabal test tools-data-structures-tungsten
```

The `tungsten-test` executable covers list examples, Association ordering rules, custom `HashPolicy`
behavior, relabel stress, deterministic generated histories against an ordered-pair model, and
`forkIO` concurrent readers over shared immutable snapshots.
