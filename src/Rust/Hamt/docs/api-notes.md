# Rust HAMT API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust HAMT port
- Scope: Rust naming, contracts, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-hamt`, with library name
`tools_data_structures_hamt`.

Primary entry points:

- `PersistentHashMap<K, V, S = RandomState>`;
- `PersistentHashSet<T, S = RandomState>`;
- `DuplicateKey`.

The port follows the repository HAMT semantics:

- updates return new persistent values and keep old versions usable;
- nodes are immutable and shared through `Arc`;
- the trie uses 32-way bitmap-indexed branching over 32 truncated hash bits;
- equal full-hash collisions are kept in immutable collision buckets;
- no-op value replacement and absent removal preserve the existing root;
- duplicate `add`/`try_add` calls reject the key without changing the root;
- replacing an existing key retains the originally stored key object;
- bulk map construction is last-wins.

Rust-specific differences:

- key equivalence is Rust `Eq`; the hash policy is supplied through `BuildHasher`;
- duplicate insertion returns `Result<_, DuplicateKey>` rather than throwing;
- lookups return references, and removal returns owned cloned values;
- `shares_root_with` exposes root sharing for tests and diagnostics;
- iteration streams trie order through an explicit traversal stack rather than materializing all entries up front.

The hash contract is the standard Rust hash-map contract: keys that compare equal must hash equally under the
chosen `BuildHasher`. The implementation truncates `Hasher::finish()` to 32 bits to match the repository HAMT
shape.
