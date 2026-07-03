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
- streaming trie-order iteration;
- last-wins replacement and original-key retention through an equivalence policy;
- set algebra, equality, and proper subset/superset relations.
