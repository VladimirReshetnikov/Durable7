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
- CHAMP split-bitmap invariants, canonical independent-history equality, and typed map diff;
- streaming trie-order iteration;
- last-wins replacement and original-key retention through an equivalence policy;
- set algebra, equality, and proper subset/superset relations;
- cross-policy subset/superset/equality/overlap relations under the receiver's `HashPolicy`;
- JVM concurrent readers over shared immutable map/set snapshots;
- Ctrie node-local GCAS updates, O(1) retained generations, lazy renewal, contended unique adds and
  counters, equal-hash collision nodes, and snapshot-to-CHAMP conversion.
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
