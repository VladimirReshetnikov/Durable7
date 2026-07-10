# Kotlin HAMT Validation

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers validating the Kotlin HAMT workspace
- Scope: Build command, tool bootstrap, and deterministic test coverage

Run from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Hamt
```

The command compiles `Hamt/src` and `Hamt/test` with the Kotlin command-line compiler and runs the
test executable. If no Java 21+ runtime is available on `PATH` on Windows, the script downloads a local Temurin
JDK 21 under `src/Kotlin/build/tools`; on non-Windows hosts, provide Java 21+ through `PATH` or `JAVA_HOME`.
It also downloads and verifies the Kotlin 2.4.0 compiler archive before compilation. All generated files stay
under the ignored `build` directory.
On Windows the script enables inherited non-interactive OS error handling before tool startup, and it launches
the test JVM in AWT headless mode so failures stay on the console and return a nonzero exit.

The test executable covers map persistence, no-op root sharing, duplicate-key rejection, equal-hash
collision buckets, trie-order iteration, last-wins replacement with original-key retention, and set
algebra, including relations between sets built with different policies where the receiver's policy
is authoritative.
