# Haskell HAMT

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell persistent HAMT port
- Scope: `tools-data-structures-hamt` package

This package ports the repository's persistent map cores to Haskell. It provides persistent
`HashMap` and `HashSet` values with a canonical 32-way CHAMP trie, strict split data/node maps,
inline payload runs, immutable equal-hash collision buckets, structural sharing between versions,
and optional runtime `HashPolicy` values for custom hash/equality behavior. Maps expose semantic
`mapEquals` and typed `MapDifference` classification.

`Data.Structures.Hamt.MerkleEncoding`, `Data.Structures.Hamt.MerkleSearchTree`, and
`Data.Structures.Hamt.MerklePersistence` provide the policy-bound canonical Merkle search tree.
The pure SHA-256 implementation, strict versioned codecs, domain/key framing, empty digest,
complete `MST2` blocks, and `MSP2` proof queries match C#, Rust, and Kotlin exactly. The immutable
wide tree supports stable first-key/last-value bulk construction, path-copy updates, ordered
lookup/range enumeration, digest-pruned diff, exact block/shape inspection, shared-content
diagnostics, and deep re-encoding validation. Its pure persistence tier adds immutable block-store
snapshots, complete and partial packs, seven-limit bounded verification, atomic-result save/import,
membership/nonmembership/range proofs, closure-pruned and iterative synchronization, and typed
present/absent-safe three-way merge. See the dedicated
[Merkle search-tree guide](docs/merkle-search-tree.md).

The HAMT default factories use the package-local `Hashable` class plus `Eq`, keeping the public
shape close to Haskell's `containers` style.
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
contract). `mapEquals` and `diff` require semantically compatible `HashPolicy` values and interpret
left keys through the right map where applicable. This is a documented caller precondition because
Haskell functions have no decidable identity; unlike C#, the library cannot enforce comparer-object
identity.

`Data.Structures.Hamt.Patricia` adds `IntMap32`/`IntSet32` and `IntMap64`/`IntSet64`. The shared
strict big-endian Patricia core sign-flips keys for ascending signed traversal, compresses common
prefixes at the highest differing bit, and implements prefix-aware right-biased union,
left-valued intersection, and difference. Every branch caches its subtree cardinality, so a
structurally pruned algebra operation can publish the result count without traversing the retained
subtrees. `unionWith`/`unionWithKey` and `intersectionWith`/`intersectionWithKey` receive left and
right values in argument order; the keyed forms additionally receive the shared integer key.
`validStructure` verifies both the root count and every cached branch count. The unconstrained
`insert` deliberately does not require `Eq` for values, so replacement rebuilds the affected leaf
and path even when the new value is extensionally equal; callers that need equality-gated no-op
identity must compare first.
