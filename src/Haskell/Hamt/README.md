# Haskell HAMT

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell persistent HAMT port
- Scope: `tools-data-structures-hamt` package

This package ports the repository's persistent map cores to Haskell. It provides persistent
`HashMap` and `HashSet` values with a canonical 32-way CHAMP trie, strict split data/node maps,
inline payload runs, immutable equal-hash collision buckets, structural sharing between versions,
and optional runtime `HashPolicy` values for custom hash/equality behavior. Same-policy maps expose
lockstep node-based `mapEquals` and typed `MapDifference` classification; cross-policy maps retain
semantic lookup comparison. Right-valued union, left-valued intersection, difference, and
symmetric difference are implemented by direct CHAMP-slot combination.

`Data.Structures.Hamt.Transient` adds one-way `MapTransient` and `SetTransient` editing sessions in
`IO`. Creating a session adopts an immutable source by reference, and `persistMap` / `persistSet`
publish the current value by reference and consume the session. Clean and logical-no-op sessions
retain the exact source root; successful edits preserve the hash policy, first equivalent key/item
representative, old snapshots, and canonical trie shape. Callback and path construction finish
before a masked `IORef` commit, so synchronous or asynchronous failure cannot partially publish an
edit. Sessions are deliberately unsynchronized and support one logical owner.

This first Haskell port is a semantic lifecycle checkpoint, not an owner-token optimization. Point
edits call the existing persistent path-copying operations and retain their complexity and
allocation behavior; only adoption, clear, and terminal publication are O(1). No benchmark or
speedup claim is attached to the API. A future internal mutable-node engine may optimize the same
surface after separately reviewed evidence without changing its observable contract.

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
`HashMap.validStructure` provides a key/value-agnostic diagnostic for cached branch/collision
cardinality and
canonical node shape, including child-only node runs, bitmap cardinality, singleton payload
promotion, and collision-bucket demotion after deletion.

```powershell
cd src\Haskell
.\test.ps1 -Workspace Hamt
```

The local [test README](test/README.md) lists the deterministic coverage areas.

Enumeration follows trie bitmap order and collision-bucket order: stable for an unchanged
version, but neither insertion order nor sorted order (matching the C# reference's documented
contract). Structural algebra uses GHC's one-way pointer-identity primitive to prune identical
immutable roots, subtries, and policy values without hashing. A negative pointer comparison never
affects semantics: the right operand is normalized under the receiver policy before combination.
When both maps retain the exact same hash and equality function closures, `mapEquals` and `diff`
traverse the two canonical node graphs in lockstep, pruning pointer-identical descendants before key
or value equality callbacks and using stored hashes rather than rehashing keys. Independently
supplied policies use the semantic lookup path, so compatible equality functions may retain
distinct coherent hash functions. They must still define compatible key equivalence. This is a
documented caller precondition because Haskell functions have no semantic equality operation for
functions; pointer identity is used only as a safe positive optimization, not as a compatibility
verdict.

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
