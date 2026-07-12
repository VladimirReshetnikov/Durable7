# Rust HAMT Tests

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers navigating Rust HAMT test coverage
- Scope: Test location, command, and coverage map

HAMT and Patricia tests live inline in [`../src/lib.rs`](../src/lib.rs) and
[`../src/patricia.rs`](../src/patricia.rs). Merkle core and wire tests live in
[`merkle_core_wire.rs`](merkle_core_wire.rs). Run them from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```

Coverage groups:

- map persistence and version isolation;
- root-sharing no-op behavior;
- duplicate-key rejection;
- equal-hash collision buckets;
- streaming trie-order iteration and exact-size accounting;
- last-wins replacement and original-key retention;
- set algebra, equality, and proper subset/superset relations;
- transient bulk-builder snapshot detachment, duplicate-identity rules, final-hash-level splits,
  and differential agreement with incremental construction;
- 32/64-bit Patricia ordering and randomized histories, structural algebra, key/left/right map
  combiners, cached branch cardinalities, and receiver-root no-op identity;
- `Send`/`Sync` assertions and spawned-thread readers over shared immutable snapshots.
- strict Merkle codec and digest parsing, exact domain/empty/root/block golden vectors, canonical
  wide-tree construction, retained-version model histories, structural sharing, ranges and diff,
  non-`Clone` values, adversarial hash layers, and spawned-reader safety.
