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

The workspace uses CMake presets. The `msvc-*` presets use Visual Studio's bundled Ninja by absolute path;
the `ninja-*` presets use `cmake` and `ninja` from `PATH` for host-agnostic validation. `CMakeLists.txt` builds the
`tools_data_structures_finger_tree_c` static library from `src/fingertree.c`, with these options enabled by
default:

- `FINGERTREE_C_BUILD_TESTS`: builds `tests/fingertree_c_tests` and registers `fingertree_c.core`.
- `FINGERTREE_C_BUILD_SAMPLES`: builds `samples/fingertree_c_showcase` and
  `samples/fingertree_c_snapshots`, both registered as CTest smoke tests.
- `FINGERTREE_C_BUILD_BENCHMARKS`: builds `benchmarks/fingertree_c_benchmarks`.

The project is C11 (`C_STANDARD 11`, required, extensions off). MSVC targets build with `/permissive-`,
`/W4`, `/WX`, `/external:anglebrackets`, and `/external:W0`; non-MSVC targets use `-Wall -Wextra
-Wpedantic -Werror`. Generated files live under `out/build/<preset>/`, which is ignored by the repository.

## Compiler Matrix Policy

For changes to C FingerTree source, headers, tests, samples, benchmarks, or behavior documentation, compile and
run tests under all three supported compiler lanes:

- MSVC Debug and Release through `msvc-debug` and `msvc-release`.
- GCC/MinGW in a separate CMake/Ninja build directory.
- LLVM/Clang in a separate CMake/Ninja build directory.

Each lane must run CTest against the binaries from its own build directory. Do not reuse `out/build/msvc-*`
binaries as evidence for GCC or Clang.

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

## GCC Build And Tests

Use the WinLibs compiler, CMake, Ninja, and CTest directly when they are not on the current `PATH`:

```powershell
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"

& "$mingw\cmake.exe" -S . -B out\build\gcc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="$mingw\gcc.exe" -DCMAKE_MAKE_PROGRAM="$mingw\ninja.exe"
& "$mingw\cmake.exe" --build out\build\gcc-debug
& "$mingw\ctest.exe" --test-dir out\build\gcc-debug --output-on-failure
```

## Clang Build And Tests

Use the Visual Studio developer environment when Clang targets the MSVC ABI:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$ninja = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$clang = "C:\Program Files\LLVM\bin\clang.exe"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" -S . -B out\build\clang-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=""$clang"" -DCMAKE_MAKE_PROGRAM=""$ninja"" && ""$cmake"" --build out\build\clang-debug && ""$ctest"" --test-dir out\build\clang-debug --output-on-failure"
```

## Current Coverage

The bootstrap test executable `fingertree_c_tests` is registered as `fingertree_c.core`. See the
[tests README](../tests/README.md) for named test cases, direct executable path, and runner failure behavior.

The executable covers:

- endpoint, index, indexed replacement, split, concat, and persistence behavior for the generic tree/deque surface;
- lazy middle force paths through boundary pop, measure reads, indexing, traversal, split, concat, and disposal;
- size-measure `locate` and measure-guided split behavior;
- concurrent shared-snapshot copy/read/update/dispose behavior over atomic node and tree reference counts;
- reversible-deque logical reversal, endpoint edits, index reads, traversal, mixed-orientation concat, split,
  set/insert/remove, and persistence;
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

Both `msvc-debug` and `msvc-release` are expected to build warning-free under `/W4 /WX`. On hosts with
GCC or Clang available through CMake, the portable presets are also available:

```powershell
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug --output-on-failure

cmake --preset ninja-asan
cmake --build --preset ninja-asan
ctest --preset ninja-asan --output-on-failure
```

`ninja-asan` enables AddressSanitizer and UndefinedBehaviorSanitizer flags for compilers that support the GCC-style
sanitizer options. Prefer it when changing handle lifetime, copy/dispose paths, or persistent update code.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/C/FingerTree> cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --preset msvc-debug --output-on-failure"
```

If a docs-only change only updates links or wording and does not alter commands, C API claims, samples, or
benchmark claims, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
