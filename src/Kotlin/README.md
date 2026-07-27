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
| [Hamt](Hamt/README.md) | Persistent CHAMP collections plus strict patches, directed graphs, indexed maps, Ctrie, Patricia, and Merkle | `PersistentHashMap`, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap` | `.\build.ps1 -Workspace Hamt` |
| [FingerTree](FingerTree/README.md) | Persistent measured-tree family, sparse chunked bit set, cursors, RRB, priority cores, and DABA Lite | `durable7.fingertree.*` | `.\build.ps1 -Workspace FingerTree` |
| [Ordered](Ordered/README.md) | Neutral persistent insertion-ordered set, map, and grouped multimap | `PersistentOrderedSet`, `PersistentOrderedMap`, `PersistentOrderedMultimap` | `.\build.ps1 -Workspace Ordered` |

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

The Ordered workspace compiles against the public source roots of both Hamt and FingerTree and adds
no other dependency. Its `PersistentOrderedSet<T>` retains the caller's runtime `HashPolicy<T>`, keeps the first stored
representative of each equality class (including nullable representatives), and maintains an
independently owned sparse-`Long` order index with deterministic relabel fallback.

The FingerTree workspace uses immutable measured AVL sequence nodes throughout the public family.
Cached size and monoidal measures drive logarithmic indexed edits, splits, prefix location, priority,
interval, rope, and text operations; path copying retains unchanged JVM subtrees. The local API notes
spell out the few engine-level differences from the C# lazy digit spine. Its positional
`RopeCursor<T>`, generic `MeasuredRopeCursor<T, M>`, and newline-specialized `TextRopeCursor` preserve
immutable gap/edit/branch and absolute measure-search semantics through exact retained snapshots
without claiming the C# focused cursor representation, caches, or focus-local complexity. Its separate `DabaLite<T>`
member is deliberately mutable: a six-cursor, chunk-backed schedule maintains a FIFO monoid aggregate
with bounded callback counts and requires external serialization.

`CanonicalSortedSet<T>` is a separate immutable Cartesian-tree core for workloads that need
policy-scoped, insertion-order-independent topology. `ZipTreeRankPolicy<T>` combines a JVM
`Comparator`, an equivalence-class-coherent 64-bit hash, and a random, publicly seeded, or
caller-keyed HMAC-SHA-256 rank source. Persistent zip/unzip edits share untouched nodes; same-policy
equality can reject unequal digests before a lockstep structural comparison.

## Documentation coverage

Kotlin has no compiler gate for missing KDoc, unlike C# where `CS1591` is an error, so coverage here
is a convention rather than a guarantee. As of 2026-07-27, 750 of 1,313 public functions and
properties carry a KDoc comment. The cursor surfaces are complete: `SequenceCursors.kt` and
`OrderedSearchCursors.kt` document every member, which matters most because the null-at-boundary
contract, the gap-moves-to-sorted-position behavior of `add`/`insert`, and the exact-key versus
overlap distinction in the interval collections are not visible in a signature.

The remaining gap is concentrated in `Sorted.kt`, `Core.kt`, `PersistentPatricia.kt`, `Rope.kt`, and
`PersistentHamt.kt`. Prefer documenting a member when its contract is not evident from the type -
what a `null` return means, which version an edit lands in, what an exception signals - over
restating the signature in prose.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and representation obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
