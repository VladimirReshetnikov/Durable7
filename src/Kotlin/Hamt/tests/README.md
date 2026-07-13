# Kotlin HAMT Tests

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers navigating Kotlin HAMT test coverage
- Scope: Test location, command, and coverage map

Tests live in [`../test`](../test) and are compiled into a dependency-free executable by the Kotlin
root build script. Run them from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Hamt
```

Coverage groups:

- map persistence and version isolation;
- root-sharing no-op behavior;
- duplicate-key rejection;
- equal-hash collision buckets through a constant `HashPolicy`;
- CHAMP split-bitmap invariants, full hash-prefix routing through shift 30, malformed-depth and
  bitmap-cardinality rejection, policy-aware collision topology, canonical independent-history
  equality, and typed map diff;
- streaming trie-order iteration;
- last-wins replacement and original-key retention through an equivalence policy;
- set algebra, equality, and proper subset/superset relations;
- cross-policy subset/superset/equality/overlap relations under the receiver's `HashPolicy`;
- JVM concurrent readers over shared immutable map/set snapshots;
- Ctrie node-local GCAS and root/main RDCSS helping, deterministic snapshot/write races in both
  linearization directions, O(1)
  retained generations, lazy renewal, deep/equal-hash tomb contraction, contended same-key updates,
  250-round short-history linearizability, structural validation, and snapshot-to-CHAMP conversion.
- 32/64-bit big-endian Patricia boundary ordering, randomized histories, cached-cardinality
  structural map/set algebra, right-biased and combining map semantics, and receiver identity on
  semantic no-ops;
- strict Merkle codec/digest vectors and the exact C#/Rust policy, empty-tree, root, and complete
  `MST2` block golden bytes;
- Merkle canonicality under bulk construction, incremental updates, adversarial five-level
  insertion/removal, and independent churn histories;
- Merkle structural sharing, nullable values, typed diff, inclusive ranges, first-equivalent-key
  retention, caller reference semantics, mutated-representative validation, randomized retained-
  version models, and concurrent readers.
- immutable block/store/pack ownership, exact closure save/load/import, partial-pack overlays,
  deterministic explicit exports, and all seven finite hostile-input budgets;
- missing/tampered/malformed/noncanonical/foreign blocks, authenticated count and interval
  corruption, zero-write destination conflict preflight, and concurrent idempotent store access;
- exact `MSP2` membership, nonmembership, and inclusive-range queries, canonical proof expansion,
  early query-budget gating, altered/extra/omitted steps, and duplicate-envelope rejection;
- closure-pruned complete packs and iterative frontier synchronization; and
- typed three-way merge across disjoint, identical, unresolved, resolver-selected, deletion, and
  present-null cases, with complete-output withholding and canonical-entry reuse.
