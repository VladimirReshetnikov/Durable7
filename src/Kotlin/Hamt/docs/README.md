# Kotlin HAMT Docs

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and reviewers of the Kotlin HAMT workspace
- Scope: Documentation index for `src/Kotlin/Hamt`

The Kotlin HAMT workspace ports the repository persistent HAMT map/set/bag contract to idiomatic JVM
values. Start here when reviewing Kotlin naming, runtime hash policies, immutable update behavior,
one-descent map factories, multiset algebra, or dependency-free validation.

## Current Documents

- [API notes](api-notes.md) describe `PersistentHashMap<K, V>`, `PersistentHashSet<T>`,
  `PersistentHashBag<T>`, `HashPolicy<K>`, one-descent map factories, collision behavior, no-op
  sharing diagnostics, multiplicity and receiver-policy algebra, duplicate handling, null miss
  paths, and intentional differences from the C# and native ports.
- [Merkle search tree](merkle-search-tree.md) specifies the `mst-sha256-b16-v2` policy domain,
  strict canonical codecs, SHA-256 key layering, immutable wide-tree operations, exact `MST2`
  block framing, verified persistence, exact `MSP2` proofs, synchronization, three-way merge,
  reference semantics, and validation invariants.
- [Validation](validation.md) records the `src/Kotlin/build.ps1` command shape, JDK/Kotlin compiler
  bootstrap behavior, generated-output locations, and the coverage boundary for the executable tests.
- [Tests README](../tests/README.md) maps deterministic executable coverage for collisions, root
  sharing, single-descent callback counts, replacement, key/value representative retention,
  hash-bag point edits and algebra, iteration, set algebra, Merkle golden bytes, canonical history
  convergence, structural sharing, bounded hostile-input rejection, proof tampering, iterative
  synchronization, present-null merge conflicts, randomized retained versions, and concurrent
  readers/stores.

## Related Repository Docs

- [Data-structure catalog](../../../../docs/reference/data-structure-catalog.md#hamt-map-and-set) lists
  the Kotlin HAMT public surface beside the sibling ports.
- [Semantic contracts](../../../../docs/reference/semantic-contracts.md#hamt-map-and-set) summarizes the
  shared HAMT obligations that the Kotlin port preserves.
- [Porting and semantic parity](../../../../docs/guides/porting-and-semantic-parity.md#hamt-specific-checks)
  gives the cross-language checklist for HAMT changes.
