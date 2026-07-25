# Rust HAMT Documentation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of `durable7-hamt`
- Scope: Documentation index for the Rust HAMT workspace

The Rust HAMT crate ports the repository persistent HAMT map/set/bag/bimap contract to safe Rust values
backed by `Arc`-shared immutable nodes, including one-descent map factories and ownership-native
one-way map/set edit sessions. Start here when reviewing Rust naming, `BuildHasher` policy behavior,
borrowed lookup results, cloned removal results, bag arithmetic/algebra, transient publication, or
Cargo validation.

## Current Documents

- [API notes](api-notes.md) describe `PersistentHashMap<K, V, S>`, `PersistentHashSet<T, S>`,
  `PersistentHashBag<T, S>`, one-descent factory results, `TransientHashMap<K, V, S>`,
  `TransientHashSet<T, S>`, checked bag errors, `DuplicateKey`, `BuildHasher` policy preservation,
  collision buckets, root-sharing diagnostics, iteration order, and Rust-specific result shapes.
  They also specify the strict map-patch, explicit-vertex directed-graph, and maintained
  single-secondary-index facades.
- [Merkle search tree](merkle-search-tree.md) specifies the Rust B=16 wide-tree API, policy domain,
  built-in strict codecs, `MST2` block framing, canonical shape, block-store persistence, bounded
  verification, `MSP2` proofs, synchronization, and typed three-way merge.
- [Validation](validation.md) records the Cargo command, local rustup fallback path, safe-Rust boundary,
  and coverage map.
- [Tests README](../tests/README.md) maps unit and integration coverage for collisions, one-descent
  updates, hash-bag arithmetic/algebra, iteration, set algebra, root-sharing behavior, canonical
  Merkle construction, and golden wire vectors.

## Related Repository Docs

- [Data-structure catalog](../../../../docs/reference/data-structure-catalog.md#hamt-map-and-set) lists
  the Rust HAMT public surface beside the sibling ports.
- [Semantic contracts](../../../../docs/reference/semantic-contracts.md#hamt-map-and-set) summarizes the
  shared HAMT obligations that the Rust crate preserves.
- [Porting and semantic parity](../../../../docs/guides/porting-and-semantic-parity.md#hamt-specific-checks)
  gives the cross-language checklist for HAMT changes.
