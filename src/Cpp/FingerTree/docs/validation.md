# C++ FingerTree Validation

- Status: Current validation guide
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers validating the C++ port
- Scope: Local build, test, stress, warning-policy, generated-output, and benchmark-harness-status guidance

Use this guide when changing the C++ FingerTree public headers, tests, or documentation that makes build,
validation, stress, API, or benchmark-status claims. For API shape and practical examples, pair it with the
[API notes](api-notes.md) and [usage guide](usage.md).

## Build Model

The workspace uses CMake presets with Visual Studio's bundled Ninja. `CMakeLists.txt` defines the
header-first interface library `tools_data_structures_finger_tree` and registers the test executable
`tests/fingertree_smoke_tests` when `FINGERTREE_BUILD_TESTS` is enabled.

The public interface advertises `cxx_std_23`. MSVC targets also receive `/std:c++latest`,
`/permissive-`, `/Zc:__cplusplus`, `/external:anglebrackets`, and `/external:W0`. Test targets require
`CXX_STANDARD 23`, disable extensions, and build with `/W4 /WX`; non-MSVC targets use `-Wall -Wextra
-Wpedantic -Werror`. Generated files live under `out/build/<preset>/`, which is ignored by the repository.

## Debug Build And Tests

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure"
```

The C++ targets require the active MSVC latest language mode. CMake records that as `CXX_STANDARD 23` plus an
explicit `/std:c++latest` compile option for MSVC targets; keep validation shells on a Visual Studio toolchain
new enough for that mode. A plain PowerShell invocation of `VsDevCmd.bat` does not persist its environment
changes in the current PowerShell process; keep configure/build/test in one `cmd.exe` chain when starting from
an uninitialized shell.

## Release Build

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-release && ""$cmakeDir\cmake.exe"" --build --preset msvc-release && ""$cmakeDir\ctest.exe"" --preset msvc-release --output-on-failure"
```

## Test Policy

Each randomized test must print a replay seed on failure. Complexity and concurrency tests should use deterministic
operation counters, allocation counters, and duration environment variables rather than timing thresholds.

The current bootstrap tests are self-contained CTest executables. Structure-level tearable concurrency stress tests
honor `FINGERTREE_STRESS_SECONDS`; unset runs use a short default suitable for ordinary `ctest`, while soak runs
can raise the value without editing source.

The checked-in `vcpkg.json` is intentionally dependency-free today. Later milestones may add Catch2 through vcpkg
once the dependency manager is intentionally introduced and wired into CMake.

## Current Coverage

CTest currently registers one executable, `fingertree.smoke`, backed by `tests/fingertree_smoke_tests`.
It is a local test-runner binary rather than a Catch2/GoogleTest target.

The suite covers:

- `persistent_deque<T>` endpoint, indexing, splitting, concatenation, sorted-search, randomized branching
  histories, allocation counters, and operation counters;
- the general measured finger tree, lazy cells, measured lazy cells, measure predicates, product/sum/order
  measures, and named operations;
- reversible deque reversal, mixed-orientation operations, random histories, and O(1)-reverse allocation guards;
- sorted bag/set/map ranking, navigation, range, custom order, set algebra, and randomized model checks;
- priority queue ordering, stability, and command-model behavior;
- interval tree insertion, overlap, containment, coalescing, removal, and model comparisons;
- `rope<T>`, measured rope, text rope, line navigation, chunked mutations, and randomized vector-model histories;
- atomic-box/lazy-publication helpers, allocation counters, operation counters, and command-model support;
- tearable-struct concurrency stress tests for measured trees, measured ropes, lock-free rope publication, and
  branching histories over retained shared bases.

## Benchmark Harness Status

No native benchmark target is currently checked in. Milestone 8 still needs the planned persistence, catenation,
reversal, priority, interval, and rope benchmark harnesses.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/Cpp/FingerTree> cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --preset msvc-debug --output-on-failure"
```

If a docs-only change only updates links or wording and does not alter commands, C++ API claims, stress behavior,
or benchmark-status claims, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
