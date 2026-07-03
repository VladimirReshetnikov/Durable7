# Kotlin HAMT API Notes

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers implementing and reviewing the Kotlin HAMT port
- Scope: Kotlin naming, contracts, and intentional differences from the C# and Rust workspaces

Primary entry points:

- `PersistentHashMap<K, V>`;
- `PersistentHashSet<T>`;
- `HashPolicy<K>` for runtime hash/equality policy injection;
- `DuplicateKeyException`, `AddResult<T>`, and removal result records.

The port follows the repository HAMT semantics:

- updates return new persistent values and keep old versions usable;
- trie nodes are immutable and shared by JVM object reference;
- the trie uses 32-way bitmap-indexed branching over 32 hash bits;
- equal full-hash collisions are kept in immutable collision buckets;
- no-op value replacement and absent removal preserve the existing root;
- duplicate `add` / `tryAdd` calls reject the key without changing the root;
- replacing an existing key retains the originally stored key object;
- bulk map construction is last-wins;
- set algebra includes union, intersection, difference, symmetric difference, subset/superset,
  proper subset/superset, overlap, and equality checks.

Kotlin-specific differences:

- default equality is Kotlin/JVM `equals`; custom behavior is supplied through `HashPolicy<K>`;
- duplicate insertion throws `DuplicateKeyException` for `add` and returns `AddResult` for `tryAdd`;
- lookup miss paths return `null`;
- iteration is exposed as Kotlin `Sequence`/`Iterable` over stable trie order;
- `sharesRootWith` exposes root sharing for tests and diagnostics.

The hash contract is the standard hash-map contract: keys considered equivalent by the active
`HashPolicy` must produce the same hash through that policy.
