# Kotlin Workspaces

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and AI agents working on Kotlin ports of repository-owned data structures
- Scope: Kotlin/JVM workspaces under `src/Kotlin`

The Kotlin root contains JVM ports of the repository-owned collection families. The workspaces use
idiomatic immutable Kotlin values while preserving the observable contracts of the managed C# baseline
where the language surfaces line up.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | Persistent HAMT map/set port with 32-way bitmap-indexed trie nodes and immutable collision buckets | `tools.datastructures.hamt.PersistentHashMap`, `PersistentHashSet` | `.\build.ps1 -Workspace Hamt` |
| [FingerTree](FingerTree/README.md) | Kotlin semantic checkpoint for the FingerTree family: persistent deque, measured sequence, reversible deque, sorted collections, priority queue, intervals, ropes, and text helpers | `tools.datastructures.fingertree.*` | `.\build.ps1 -Workspace FingerTree` |

Run the full Kotlin validation from this directory:

```powershell
.\build.ps1
```

The build script bootstraps a local JDK 21 and the Kotlin 2.4.0 command-line compiler into
`src/Kotlin/build/tools` when a suitable Java 21+ runtime is not already available, then compiles each
workspace and runs its dependency-free executable tests. The `build` directory is ignored by the
repository.

The FingerTree workspace intentionally starts as a semantic checkpoint rather than a final lazy
finger-tree representation. It preserves immutable snapshot behavior and the public family surfaces;
its README and API notes mark the remaining asymptotic representation boundary.
