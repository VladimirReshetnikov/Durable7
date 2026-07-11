# Haskell HAMT

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell persistent HAMT port
- Scope: `tools-data-structures-hamt` package

This package ports the repository HAMT map and set family to Haskell. It provides persistent
`HashMap` and `HashSet` values with a canonical 32-way CHAMP trie, strict split data/node maps,
inline payload runs, immutable equal-hash collision buckets, structural sharing between versions,
and optional runtime `HashPolicy` values for custom hash/equality behavior. Maps expose semantic
`mapEquals` and typed `MapDifference` classification.

The default factories use the package-local `Hashable` class plus `Eq`, avoiding third-party
dependencies while keeping the public shape close to Haskell's `containers` style.
`HashMap.validStructure` provides a key/value-agnostic diagnostic for cached cardinality and
canonical node shape, including child-only node runs, bitmap cardinality, singleton payload
promotion, and collision-bucket demotion after deletion.

```powershell
cd src\Haskell
.\test.ps1 -Workspace Hamt
```

The local [test README](test/README.md) lists the deterministic coverage areas.

Enumeration follows trie bitmap order and collision-bucket order: stable for an unchanged
version, but neither insertion order nor sorted order (matching the C# reference's documented
contract).

`Data.Structures.Hamt.Patricia` adds `IntMap32`/`IntSet32` and `IntMap64`/`IntSet64`. The shared
strict big-endian Patricia core sign-flips keys for ascending signed traversal, compresses common
prefixes at the highest differing bit, and implements prefix-aware right-biased union,
left-valued intersection, and difference. Every branch caches its subtree cardinality, so a
structurally pruned algebra operation can publish the result count without traversing the retained
subtrees. `unionWith`/`unionWithKey` and `intersectionWith`/`intersectionWithKey` receive left and
right values in argument order; the keyed forms additionally receive the shared integer key.
`validStructure` verifies both the root count and every cached branch count.
