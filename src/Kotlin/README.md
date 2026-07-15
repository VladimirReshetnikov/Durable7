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
| [Hamt](Hamt/README.md) | Persistent CHAMP map/set, hash-bag, strict-bimap, Ctrie, Patricia, and Merkle port | `tools.datastructures.hamt.PersistentHashMap`, `PersistentHashSet`, `PersistentHashBag`, `PersistentBiMap` | `.\build.ps1 -Workspace Hamt` |
| [FingerTree](FingerTree/README.md) | Persistent measured-tree port of the FingerTree family, positional/measured/text rope cursors, RRB vectors, the policy-canonical zip-zip sorted set, Brodal-Okasaki and priority-search-queue cores, and the mutable DABA Lite FIFO aggregator | `tools.datastructures.fingertree.*` | `.\build.ps1 -Workspace FingerTree` |
| [Ordered](Ordered/README.md) | Neutral persistent insertion-ordered set over the public HAMT and FingerTree APIs, with explicit positional movement and receiver-policy set algebra | `tools.datastructures.ordered.PersistentOrderedSet` | `.\build.ps1 -Workspace Ordered` |
| [Tungsten](Tungsten/README.md) | Application-specific leaf port of Tungsten `List` and `Association` over Kotlin persistent substrates | `tools.datastructures.tungsten.PersistentList`, `PersistentAssociation` | `.\build.ps1 -Workspace Tungsten` |

Run the full Kotlin validation from this directory:

```powershell
.\build.ps1
```

The build script bootstraps a local Windows JDK 21 when a suitable Java 21+ runtime is not already available.
On non-Windows hosts, put Java 21+ on `PATH` or set `JAVA_HOME` before running the script. The verified Kotlin
2.4.0 command-line compiler is bootstrapped into `src/Kotlin/build/tools` on every host, then the script compiles
each workspace and runs its dependency-free executable tests. The `build` directory is ignored by the repository.
Workspaces run sequentially, and each compiler invocation pins the Kotlin backend to one thread; there is no
Gradle daemon or worker pool. Compiler and test JVMs also see one active processor and use the serial collector
unless a caller already selected another collector.
On Windows, the script enables inherited non-interactive OS error handling before launching build tools or tests,
and starts every test JVM with `-Djava.awt.headless=true`. Assertion, exception, loader, and crash failures therefore
remain console diagnostics with nonzero exits instead of opening modal UI.

The Ordered workspace compiles against the public source roots of both Hamt and FingerTree. It does
not compile, reference, wrap, or otherwise depend on the application-specific Tungsten workspace.
Its `PersistentOrderedSet<T>` retains the caller's runtime `HashPolicy<T>`, keeps the first stored
representative of each equality class (including nullable representatives), and maintains an
independently owned sparse-`Long` order index with deterministic relabel fallback.

The FingerTree workspace uses immutable measured AVL sequence nodes throughout the public family.
Cached size and monoidal measures drive logarithmic indexed edits, splits, prefix location, priority,
interval, rope, and text operations; path copying retains unchanged JVM subtrees. The local API notes
spell out the few engine-level differences from the C# lazy digit spine. Its positional
`RopeCursor<T>`, generic `MeasuredRopeCursor<T, M>`, and newline-specialized `TextRopeCursor` preserve
immutable gap/edit/branch and absolute measure-search semantics through exact retained snapshots
without claiming the C# zipper representation, caches, or focus-local complexity. Its separate `DabaLite<T>`
member is deliberately mutable: a six-cursor, chunk-backed schedule maintains a FIFO monoid aggregate
with bounded callback counts and requires external serialization.

`CanonicalSortedSet<T>` is a separate immutable Cartesian-tree core for workloads that need
policy-scoped, insertion-order-independent topology. `ZipTreeRankPolicy<T>` combines a JVM
`Comparator`, an equivalence-class-coherent 64-bit hash, and a random, publicly seeded, or
caller-keyed HMAC-SHA-256 rank source. Persistent zip/unzip edits share untouched nodes; same-policy
equality can reject unequal digests before a lockstep structural comparison.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and representation obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
