# Rust HAMT

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of the Rust HAMT port
- Scope: Public crate shape, semantic parity notes, and validation entry point

`tools-data-structures-hamt` ports the repository HAMT, integer Patricia, and canonical Merkle
search-tree families to safe Rust. It exposes `PersistentHashMap<K, V, S = RandomState>`,
`PersistentHashSet<T, S = RandomState>`, and `BulkBuilder<K, V, S = RandomState>`, the transient
one-pass bulk constructor mirroring the C# reference (mutable unpublished nodes frozen into
detached persistent nodes; used by `FromIterator`, set intersection, the set-relation probes, and
the Tungsten association's index rebuilds).

The trie follows the existing ports:

- 32-way logical branching over 32 truncated hash bits;
- canonical CHAMP branch nodes with separate data/node maps, inline payload arrays, and compact
  child-only arrays;
- immutable same-hash collision buckets;
- `Arc`-shared nodes across persistent versions;
- no-op replacement and absent removal reuse the existing root;
- same-policy map/set union, intersection, difference, and symmetric difference combine CHAMP
  slots directly, prune `Arc`-identical subtries, and cache subtree cardinalities; independently
  created hash-policy states retain the receiver-policy semantic fallback;
- map bulk construction uses last-wins semantics.
- map diff returns owned typed additions, removals, and changes, with a shared-root fast path.
  Equality and diff are deliberately semantic across distinct `BuildHasher` states: Rust key
  equivalence is always `Eq`, and each lookup hashes with the probed map's own builder. This differs
  from C# comparer-object identity without weakening Rust's `Eq`/`Hash` contract.

The crate also exports `PersistentIntMap`/`PersistentIntSet` and
`PersistentLongMap`/`PersistentLongSet`. Their big-endian Patricia core sign-flips keys for
ascending signed iteration, path-compresses on the highest differing bit, shares immutable `Arc`
subtrees, caches subtree cardinalities, and implements prefix-aware union, intersection, and
difference. Map union and intersection also expose key/left/right combining forms without falling
back to per-entry insertion.

`MerkleSearchTree<K, V>` is the exact paper-style B=16 wide-tree port. A
`MerkleSearchTreePolicy<K, V>` binds comparer semantics and explicitly versioned canonical codecs
into the `mst-sha256-b16-v2` SHA-256 domain. Leading zero key-hash nibbles select levels;
consecutive same-level separators share a wide immutable block. Consequently independently built
maps with equal canonical content have the same shape, exact `MST2` block bytes, and root address.
Updates copy one block path through `Arc`, preserve first-equivalent-key/last-value semantics, and
require neither keys nor values to implement `Clone`. Built-in codecs cover big-endian `i32`/`i64`,
nullable UTF-8, nullable bytes, and RFC-4122/network-order GUID bytes. See the dedicated
[Merkle search tree notes](docs/merkle-search-tree.md) for the hash framing and wire manifest.

The same surface owns full content-addressed persistence: immutable `MerkleBlock` values, a
thread-safe `InMemoryMerkleBlockStore`, complete and partial `MerkleBlockPack` transfer, bounded
strict load/import, `MSP2` point/range proofs, closure-pruned synchronization, and typed three-way
merge. Import verifies the complete reachable closure and preflights every destination conflict
before the first write. All seven verification limits are finite by default, and proof queries are
budgeted before any codec or block decode callback. Merge distinguishes deletion from a present
`None` and never exposes a partial tree while conflicts remain.

Rust-specific shape:

- key equality is Rust's `Eq`; hash policy is supplied through `BuildHasher`;
- updates return new values, while `shares_root_with` exposes structural sharing for validation;
- duplicate inserts return `DuplicateKey` instead of throwing;
- iteration is stable for an unchanged map but remains trie-order, not insertion or sorted order.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract and evidence entry points.

Validate from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```
