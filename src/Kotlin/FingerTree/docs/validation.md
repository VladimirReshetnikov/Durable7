# Kotlin FingerTree Validation

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers validating the Kotlin FingerTree workspace
- Scope: Build command, tool bootstrap, and deterministic test coverage

Run from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace FingerTree
```

The command compiles `FingerTree/src` and `FingerTree/test` with the Kotlin command-line compiler and
runs the test executable. If no Java 21+ runtime is available on `PATH` on Windows, the script downloads a local
Temurin JDK 21 under `src/Kotlin/build/tools`; on non-Windows hosts, provide Java 21+ through `PATH` or
`JAVA_HOME`. It also downloads and verifies the Kotlin 2.4.0 compiler archive before compilation. All generated
files stay under the ignored `build` directory.
On Windows the script enables inherited non-interactive OS error handling before tool startup, and it launches
the test JVM in AWT headless mode so failures stay on the console and return a nonzero exit.

The test executable covers persistent deque snapshots, reversible orientation, measured prefix
splits/locates, sorted bag/set/map ordering and ranges, stable cached-priority dequeue, max-high closed
interval queries and coalescing, complete positional/range editing surfaces for positional and measured ropes,
comparator-aware sorted-map bulk construction, measured text line navigation, and rope builder conveniences.
Counting-comparator guards over 65,536-element sorted collections prove that bag counting bounds, set
rank/neighbor navigation, and keyed map lookup finish within one logarithmic descent.
Representation coverage validates AVL balance and identity sharing across
every facade, a 5,000-command sequence model, 100,000-element construction, policy compatibility,
overflow and comparison regressions, and concurrent readers over retained snapshots.

RRB validation covers every 32-way boundary through 100,000 elements, unequal-height and uneven
fragment concatenation, exact-boundary leaf identity, regular-versus-relaxed size-table invariants,
a 10,000-operation retained-snapshot model, and 2,000 adversarial split/concat rounds with explicit
density and height bounds. Builder tests cover full-tail transfer, partial-tail copying, cached
snapshots, adopted prefixes, fail-fast iteration, clearing, and source-array isolation. Nullable
elements, checked count overflow, invalid overflowing ranges, no-op identity, and concurrent readers
have dedicated cases.
