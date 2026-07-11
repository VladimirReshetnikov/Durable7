# Rust HAMT

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of the Rust HAMT port
- Scope: Public crate shape, semantic parity notes, and validation entry point

`tools-data-structures-hamt` ports the repository HAMT map and set to safe Rust. It exposes
`PersistentHashMap<K, V, S = RandomState>`, `PersistentHashSet<T, S = RandomState>`, and
`BulkBuilder<K, V, S = RandomState>`, the transient one-pass bulk constructor mirroring the C#
reference (mutable unpublished nodes frozen into detached persistent nodes; used by `FromIterator`,
set intersection, the set-relation probes, and the Tungsten association's index rebuilds).

The trie follows the existing ports:

- 32-way logical branching over 32 truncated hash bits;
- canonical CHAMP branch nodes with separate data/node maps, inline payload arrays, and compact
  child-only arrays;
- immutable same-hash collision buckets;
- `Arc`-shared nodes across persistent versions;
- no-op replacement and absent removal reuse the existing root;
- map bulk construction uses last-wins semantics.
- map diff returns owned typed additions, removals, and changes, with a shared-root fast path.

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
