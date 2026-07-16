# Rust HAMT Tests

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers navigating Rust HAMT test coverage
- Scope: Test location, command, and coverage map

HAMT, hash-bag invariant, and Patricia tests live inline in [`../src/lib.rs`](../src/lib.rs),
[`../src/hash_bag.rs`](../src/hash_bag.rs), and [`../src/patricia.rs`](../src/patricia.rs).
One-descent map-factory tests live in
[`map_factory_updates.rs`](map_factory_updates.rs), hash-bag tests live in
[`persistent_hash_bag.rs`](persistent_hash_bag.rs), hash-multimap tests live in
[`persistent_hash_multimap.rs`](persistent_hash_multimap.rs), and Merkle core/wire and persistence tests live
in [`merkle_core_wire.rs`](merkle_core_wire.rs) and
[`merkle_persistence.rs`](merkle_persistence.rs). Run them from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```

Coverage groups:

- map persistence and version isolation;
- root-sharing no-op behavior;
- one-hash/one-descent map factories, exact closure selection, retained key and `Arc` value
  representatives, collision paths, panic atomicity, and a deterministic collision-heavy model;
- persistent hash-bag construction, checked counts, retained representatives, expanded/distinct/
  entry iteration, saturated removal, receiver-policy algebra and eager normalization, algebra
  failure atomicity, and a deterministic collision-heavy multiset model;
- set-valued hash-multimap policies, first representatives, key/pair counts, duplicate root sharing,
  last-value group contraction, whole-key removal, retained branches, and invariants;
- duplicate-key rejection;
- equal-hash collision buckets and insertion-order-independent collision-key topology comparison;
- CHAMP hash-prefix routing through the final two-bit level, with deliberately malformed routing,
  over-depth, and compact-run fixtures proving the diagnostics reject invalid shapes;
- same-policy lockstep equality and typed diff, with exact hash/value callback counters proving
  descendant-level `Arc` pruning on a partially shared trie;
- streaming trie-order iteration and exact-size accounting;
- last-wins replacement and original-key retention;
- one-way `TransientHashMap` / `TransientHashSet` sessions, including clean/no-op root and policy
  identity, consuming publication, active reads and iteration, stored representatives, receiver-
  policy relations, source snapshot isolation, collision-heavy point edits, clear, and a
  deterministic 4,096-command map model;
- set algebra, equality, and proper subset/superset relations;
- scratch bulk-builder snapshot detachment, duplicate-identity rules, final-hash-level splits,
  and differential agreement with incremental construction;
- 32/64-bit Patricia ordering and randomized histories, structural algebra, key/left/right map
  combiners, cached branch cardinalities, and receiver-root no-op identity;
- `Send`/`Sync` assertions and spawned-thread readers over shared immutable snapshots;
- strict Merkle codec and digest parsing, exact domain/empty/root/block golden vectors, canonical
  wide-tree construction, retained-version model histories, structural sharing, ranges and diff,
  non-`Clone` values, adversarial hash layers, and spawned-reader safety;
- exact save/load/export/import closure round trips, malformed/tampered/count/reference failures,
  seven verification budgets, and preflight-atomic stores;
- complete/partial/iterative synchronization, canonical point/range proofs and tampering, and typed
  three-way merge including present-`None` versus deletion and no partial conflicted result.
