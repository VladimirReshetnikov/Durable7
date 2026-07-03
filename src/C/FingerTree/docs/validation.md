# C FingerTree Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers validating the C port
- Scope: Local build, test, sample, benchmark, warning-policy, and generated-output guidance for `src/C/FingerTree`

Use this guide when changing the C FingerTree public header, implementation, tests, samples, benchmark harness,
or documentation that makes build or validation claims. For API shape and practical ownership examples, pair it
with the [API notes](api-notes.md) and [usage guide](usage.md).

## Build Model

The workspace uses CMake presets with Visual Studio's bundled Ninja. `CMakeLists.txt` builds the
`tools_data_structures_finger_tree_c` static library from `src/fingertree.c`, with these options enabled by
default:

- `FINGERTREE_C_BUILD_TESTS`: builds `tests/fingertree_c_tests` and registers `fingertree_c.core`.
- `FINGERTREE_C_BUILD_SAMPLES`: builds `samples/fingertree_c_showcase` and
  `samples/fingertree_c_snapshots`, both registered as CTest smoke tests.
- `FINGERTREE_C_BUILD_BENCHMARKS`: builds `benchmarks/fingertree_c_benchmarks`.

The project is C11 (`C_STANDARD 11`, required, extensions off). MSVC targets build with `/permissive-`,
`/W4`, `/WX`, `/external:anglebrackets`, and `/external:W0`; non-MSVC targets use `-Wall -Wextra
-Wpedantic -Werror`. Generated files live under `out/build/<preset>/`, which is ignored by the repository.

## Debug Build And Tests

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure"
```

## Release Build And Tests

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-release && ""$cmakeDir\cmake.exe"" --build --preset msvc-release && ""$cmakeDir\ctest.exe"" --preset msvc-release --output-on-failure"
```

A plain PowerShell invocation of `VsDevCmd.bat` does not persist its environment changes in the current
PowerShell process; keep configure/build/test in one `cmd.exe` chain when starting from an uninitialized shell.

## Current Coverage

The bootstrap test executable `fingertree_c_tests` is registered as `fingertree_c.core`. See the
[tests README](../tests/README.md) for named test cases, direct executable path, and runner failure behavior.

The executable covers:

- endpoint, index, split, concat, and persistence behavior for the generic tree/deque surface;
- lazy middle force paths through boundary pop, measure reads, indexing, traversal, split, concat, and disposal;
- size-measure `locate` and measure-guided split behavior;
- concurrent shared-snapshot copy/read/update/dispose behavior over atomic node and tree reference counts;
- reversible-deque logical reversal, endpoint edits, index reads, and persistence. The public C reversible facade
  has no concat/split surface; concat and split coverage belongs to `ft_tree`/`ft_persistent_deque`;
- sorted set uniqueness, sorted multiset duplicates, rank access, removal, and traversal;
- sorted map insert/set/remove, duplicate rejection, lookup, rank access, traversal, and persistence;
- chunked rope construction across chunk boundaries, cumulative indexing, traversal, split/reconcat, insertion,
  removal, append, and persistence;
- measured rope construction across chunk boundaries, cached whole/prefix measure reads, cumulative-measure locate
  and split, split/reconcat, insertion, removal, append, and persistence;
- priority queue minimum-first drain order and FIFO stability for equal priorities;
- signed 64-bit interval insertion, ordering, containment, removal, first-overlap, and overlap counting;
- generic endpoint interval insertion, ordering, containment, removal, invalid interval rejection, first-overlap,
  overlap counting, and persistence;
- text rope construction, editing, indexing, line count, line/column navigation, and traversal.

The sample executables are registered as CTest smoke tests:

- `fingertree_c.sample.showcase` exercises the priority queue, sorted set, interval tree, and text rope.
- `fingertree_c.sample.snapshots` exercises persistent text snapshots and edit/restore behavior.

See the sample [README](../samples/README.md) for direct executable paths and expected transcript markers.

The benchmark executable is built when `FINGERTREE_C_BUILD_BENCHMARKS` is enabled. Run it from the workspace
root after a release build:

```powershell
.\out\build\msvc-release\benchmarks\fingertree_c_benchmarks.exe 10000
```

See the benchmark [README](../benchmarks/README.md) for workload names, output shape, and timing caveats.

Both `msvc-debug` and `msvc-release` are expected to build warning-free under `/W4 /WX`.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/C/FingerTree> cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --preset msvc-debug --output-on-failure"
```

If a docs-only change only updates links or wording and does not alter commands, C API claims, samples, or
benchmark claims, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
