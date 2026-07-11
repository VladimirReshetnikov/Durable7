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
- `ConcurrentHashTrie<K,V>` and its immutable `Snapshot<K,V>`.

The port follows the repository HAMT semantics:

- updates return new persistent values and keep old versions usable;
- trie nodes are immutable and shared by JVM object reference;
- the trie uses 32-way CHAMP branching over 32 hash bits, with separate payload and child bitmaps;
- ordinary key/value leaves are inlined in compact payload runs, while child runs contain only
  subtries; deletion promotes singleton child payloads to restore canonical shape;
- equal full-hash collisions are kept in immutable collision buckets;
- no-op value replacement and absent removal preserve the existing root;
- duplicate `add` / `tryAdd` calls reject the key without changing the root;
- replacing an existing key retains the originally stored key object;
- bulk map construction is last-wins;
- set algebra includes union, intersection, difference, symmetric difference, subset/superset,
  proper subset/superset, overlap, and equality checks.
- `mapEquals` requires the same `HashPolicy` object and compares map contents; `diff` reports typed
  added, removed, and changed entries and returns immediately for a shared root.

Kotlin-specific differences:

- default equality is Kotlin/JVM `equals`; custom behavior is supplied through `HashPolicy<K>`;
- duplicate insertion throws `DuplicateKeyException` for `add` and returns `AddResult` for `tryAdd`;
- lookup miss paths return `null`;
- iteration is exposed as Kotlin `Sequence`/`Iterable` over stable trie order;
- `sharesRootWith` exposes root sharing for tests and diagnostics.

The hash contract is the standard hash-map contract: keys considered equivalent by the active
`HashPolicy` must produce the same hash through that policy.

## Concurrent Ctrie

The mutable Ctrie stores bitmap C-nodes behind generation-stamped indirection nodes. Updates install
helping GCAS descriptors on the owning indirection node. `snapshot()` replaces only the root
generation and returns the frozen predecessor in O(1); later writers copy old-generation child
indirections only along paths they modify. `Snapshot.toPersistentHashMap()` performs the explicit
O(n) conversion into canonical CHAMP form. `getOrPut` and `compute` callbacks can run repeatedly
after a lost GCAS and must therefore be repeatable. Progress is lock-free, not wait-free.
