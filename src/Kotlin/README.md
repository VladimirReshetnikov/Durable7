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
| [FingerTree](FingerTree/README.md) | Persistent measured-tree port of the FingerTree family: deque, measured sequence, reversible deque, sorted collections, priority queue, max-high intervals, ropes, and measured text | `tools.datastructures.fingertree.*` | `.\build.ps1 -Workspace FingerTree` |
| [Tungsten](Tungsten/README.md) | Tungsten `List` and `Association` collection port over Kotlin persistent substrates | `tools.datastructures.tungsten.PersistentList`, `PersistentAssociation` | `.\build.ps1 -Workspace Tungsten` |

Run the full Kotlin validation from this directory:

```powershell
.\build.ps1
```

The build script bootstraps a local Windows JDK 21 when a suitable Java 21+ runtime is not already available.
On non-Windows hosts, put Java 21+ on `PATH` or set `JAVA_HOME` before running the script. The verified Kotlin
2.4.0 command-line compiler is bootstrapped into `src/Kotlin/build/tools` on every host, then the script compiles
each workspace and runs its dependency-free executable tests. The `build` directory is ignored by the repository.
On Windows, the script enables inherited non-interactive OS error handling before launching build tools or tests,
and starts every test JVM with `-Djava.awt.headless=true`. Assertion, exception, loader, and crash failures therefore
remain console diagnostics with nonzero exits instead of opening modal UI.

The FingerTree workspace uses immutable measured AVL sequence nodes throughout the public family.
Cached size and monoidal measures drive logarithmic indexed edits, splits, prefix location, priority,
interval, rope, and text operations; path copying retains unchanged JVM subtrees. The local API notes
spell out the few engine-level differences from the C# lazy digit spine.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and representation obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
