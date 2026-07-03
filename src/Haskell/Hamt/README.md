# Haskell HAMT

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell persistent HAMT port
- Scope: `tools-data-structures-hamt` package

This package ports the repository HAMT map and set family to Haskell. It provides persistent
`HashMap` and `HashSet` values with a 32-way bitmap-indexed trie, immutable equal-hash collision
buckets, structural sharing between versions, and optional runtime `HashPolicy` values for custom
hash/equality behavior.

The default factories use the package-local `Hashable` class plus `Eq`, avoiding third-party
dependencies while keeping the public shape close to Haskell's `containers` style.

```powershell
cd src\Haskell
cabal test hamt-test
```

The local [test README](test/README.md) lists the deterministic coverage areas.
