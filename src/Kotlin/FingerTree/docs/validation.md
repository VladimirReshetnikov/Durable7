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

The test executable covers persistent deque snapshots, reversible orientation, measured prefix
splits/locates, sorted bag/set/map ordering and ranges, stable cached-priority dequeue, max-high closed
interval queries and coalescing, positional and measured ropes, measured text line navigation, and
rope builder conveniences. Representation coverage validates AVL balance and identity sharing across
every facade, a 5,000-command sequence model, 100,000-element construction, policy compatibility,
overflow and comparison regressions, and concurrent readers over retained snapshots.
