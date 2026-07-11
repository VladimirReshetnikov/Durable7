# C++ FingerTree Validation

- Status: Current validation guide
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Updated (UTC): 2026-07-11T16:09:45Z
- Updated Repository HEAD: 66b6821334b243f2d7170a6f9360dae54ef90994
- Audience: Maintainers validating the C++ port
- Scope: Local/CI build, test, stress, sample, packaging, sanitizer, and benchmark guidance

Use this guide when changing the C++ FingerTree public headers, tests, or documentation that makes build,
validation, stress, API, or benchmark-status claims. For API shape and practical examples, pair it with the
[API notes](api-notes.md) and [usage guide](usage.md).

## Build Model

The workspace uses CMake/Ninja presets without checked-in machine-specific tool paths. The `msvc-*` presets select
the local configuration after a Visual Studio developer environment has put the toolchain and Ninja on `PATH`;
the `ninja-*` presets use the compiler, CMake, and Ninja from the host environment. `CMakeLists.txt` defines the
header-first interface library `tools_data_structures_finger_tree` and its
`tools::data_structures::finger_tree` alias. Tests, deterministic samples, and the benchmark harness build by
default and can be disabled independently with `FINGERTREE_BUILD_TESTS`, `FINGERTREE_BUILD_SAMPLES`, and
`FINGERTREE_BUILD_BENCHMARKS`.

The public interface advertises `cxx_std_23`. MSVC targets also receive `/std:c++latest`,
`/permissive-`, `/Zc:__cplusplus`, `/external:anglebrackets`, and `/external:W0`. Test targets require
`CXX_STANDARD 23`, disable extensions, and build with `/W4 /WX`; non-MSVC targets use `-Wall -Wextra
-Wpedantic -Werror`. Generated files live under `out/build/<preset>/`, which is ignored by the repository.
GNU test targets also receive `-fno-allocation-dce`, because the smoke suite intentionally observes global
replacement `new`/`delete` side effects through the allocation counter.

## Compiler Matrix Policy

For changes to C++ FingerTree public headers, tests, or behavior documentation, run Debug and Release CTest lanes
for MSVC, GCC/MinGW, and Clang. Each lane must run CTest against the binaries from its own
`out/build/<compiler>-<configuration>` directory; do not reuse another compiler's build output as evidence.

## MSVC Debug Build And Tests

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

## MSVC Release Build

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-release && ""$cmakeDir\cmake.exe"" --build --preset msvc-release && ""$cmakeDir\ctest.exe"" --preset msvc-release --output-on-failure"
```

## GCC Build And Tests

Use the WinLibs toolchain directly when the current shell has not reloaded `PATH` after installation:

```powershell
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"

& "$mingw\cmake.exe" -S . -B out\build\gcc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$mingw\g++.exe" -DCMAKE_MAKE_PROGRAM="$mingw\ninja.exe"
& "$mingw\cmake.exe" --build out\build\gcc-debug
& "$mingw\ctest.exe" --test-dir out\build\gcc-debug --output-on-failure

& "$mingw\cmake.exe" -S . -B out\build\gcc-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$mingw\g++.exe" -DCMAKE_MAKE_PROGRAM="$mingw\ninja.exe"
& "$mingw\cmake.exe" --build out\build\gcc-release
& "$mingw\ctest.exe" --test-dir out\build\gcc-release --output-on-failure
```

## Clang Build And Tests

The local LLVM installation provides an MSVC-targeting `clang++.exe`, so configure and link it from a Visual
Studio developer environment:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$ninja = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$clang = "C:\Program Files\LLVM\bin\clang++.exe"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" -S . -B out\build\clang-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=""$clang"" -DCMAKE_MAKE_PROGRAM=""$ninja"" && ""$cmake"" --build out\build\clang-debug && ""$ctest"" --test-dir out\build\clang-debug --output-on-failure"
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" -S . -B out\build\clang-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=""$clang"" -DCMAKE_MAKE_PROGRAM=""$ninja"" && ""$cmake"" --build out\build\clang-release && ""$ctest"" --test-dir out\build\clang-release --output-on-failure"
```

## Test Policy

Each randomized test must print a replay seed on failure. `deterministic_rng` captures and flushes every effective
seed before randomized work begins, preserving it even if a native assertion or process fault bypasses C++
exception handling; the local runner repeats captured seeds when it catches a test failure. Use
`--seed <decimal-or-0x-hex>` on the native executable or set `FINGERTREE_REPLAY_SEED` for CTest replay. The
command-model group runs five checked-in seeds by default and shrinks a failing stateful program to a
deletion-minimal replay log. Complexity and concurrency tests should use deterministic operation counters,
allocation counters, and duration environment variables rather than timing thresholds.

The current bootstrap tests are self-contained CTest executables. Structure-level tearable concurrency stress tests
honor `FINGERTREE_STRESS_SECONDS`; unset runs use a short default suitable for ordinary `ctest`, while soak runs
can raise the value without editing source.

The workspace has no package-manager manifest and no third-party build/test dependency. Introduce a manifest only
when a real dependency is intentionally wired into CMake and this validation matrix.

## Portable And Sanitizer Presets

Use the portable Ninja presets on hosts with a suitable C++23 compiler and `ninja` on `PATH`:

```powershell
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug --output-on-failure

cmake --preset ninja-asan
cmake --build --preset ninja-asan
ctest --preset ninja-asan --output-on-failure

cmake --preset ninja-tsan
cmake --build --preset ninja-tsan
ctest --preset ninja-tsan --output-on-failure
```

`ninja-asan` enables AddressSanitizer and UndefinedBehaviorSanitizer flags for compilers that support the GCC-style
sanitizer options. `ninja-tsan` is a separate ThreadSanitizer build; do not combine TSan with ASan. Both sanitizer
presets disable global allocation interposition, and benchmark targets are omitted because sanitizer timing is not
performance evidence. Prefer ASan+UBSan for ownership, persistent sharing, ropes, and lazy publication changes;
run TSan on a Linux Clang runtime for atomic publication or concurrent-read changes. The Windows Clang/MSVC-ABI
lane does not provide a viable TSan runtime.

## Current Coverage

CTest registers 19 cases: 17 subsystem cases backed by `tests/fingertree_smoke_tests`, from `fingertree.atomic-box` through
`fingertree.support`. Each case invokes the same local runner with an exact `--group` filter through the repository
headless launcher, so a subsystem failure is isolated without introducing Catch2/GoogleTest or duplicating test
execution. `fingertree.samples` checks two deterministic transcripts, and `fingertree.installed-consumer` performs
the staged package integration test. All 18 carry the `fingertree` label and all Windows invocations—including the
nested install/configure/build/test command—inherit the no-dialog error mode. Use
`ctest --test-dir out/build/msvc-debug -N -L fingertree` to list the cases, or `-R` with one exact case name for a
focused run. See the [tests README](../tests/README.md) for the complete group list, direct runner options,
replay-seed controls, shrinking contract, and stress notes.

The suite covers:

- `persistent_deque<T>` endpoint, indexing, splitting, concatenation, sorted-search including the empty case,
  randomized branching histories, allocation counters, and operation counters;
- the general measured finger tree, lazy cells, measured lazy cells, measure predicates, product/sum/order
  measures, and named operations;
- reversible deque reversal, mixed-orientation operations, random histories, retained forward-iterator semantics,
  zero-allocation prefix increment over 16,384 reversed values, and O(1)-reverse allocation guards;
- RRB-vector packed construction at every 32-way boundary tier, regular/relaxed metadata, persistent point and
  range edits, unequal concatenation, exact-boundary identity reuse, 10,000-step vector-model histories,
  adversarial density/height sequences, append-builder snapshot isolation, and injected-copy exception safety;
- sorted bag/set/map ranking, navigation, range, custom order, set algebra, and randomized model checks;
- priority queue ordering, stability, and command-model behavior;
- interval tree insertion, overlap, containment, coalescing, removal, and model comparisons;
- `rope<T>`, measured rope, text rope, line navigation, chunked mutations, retained text snapshots, long edit
  scripts, and randomized vector-model histories;
- atomic-box/lazy-publication helpers, allocation counters, operation counters, and command-model support;
- tearable-struct concurrency stress tests for measured trees, measured ropes, atomic data-race-safe rope
  publication, and branching histories over retained shared bases. These tests make no lock-free progress claim;
  `atomic<shared_ptr>` may serialize internally;
- stateful command programs over the measured tree, tuned deque, reversible deque, positional/measured ropes, and
  sorted set, including five default seeds, retained versions, invariant checks after every command, automatic
  failure shrinking, exhaustive size-0-through-24 checks, and non-group locate/split-find equivalence.

## Benchmark Harness Status

Build and run the dependency-free harness in Release configuration:

```powershell
cmake --build --preset msvc-release --target fingertree_benchmarks
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --short
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --filter=persistence_branching
```

`--short` is the required sanity tier. It reduces repetitions while retaining the 100/10k/1M branching ladder.
The branching case counts allocations and fails when marginal allocation cost is not size-flat. The remaining
cases cover endpoint updates and endpoint/index reads, ordinary and reversible catenation/endpoint overhead,
RRB-vector indexing and concatenation against positional-rope baselines, O(1) reverse, sorted search, weighted
selection, rope insert/split/slice, measured navigation versus a linear
scan, priority meld, and interval overlap queries. A nonempty `--filter` that matches no case is an error. See the
[benchmark guide](../benchmarks/README.md) for the complete contract.

## Samples And Installed Consumer

The ordinary build compiles `fingertree_showcase` and `fingertree_persistent_snapshots`. Their reusable
`run(std::ostream&)` seams are captured by the sample test:

```powershell
ctest --preset msvc-debug -R '^fingertree\.samples$' --output-on-failure
```

The packaging test performs a real installation to a configuration-specific private prefix, then configures a
fresh project with `FINGERTREE_BUILD_TESTS`, `FINGERTREE_BUILD_SAMPLES`, and
`FINGERTREE_BUILD_BENCHMARKS` all off. The consumer uses only
`find_package(ToolsDataStructuresFingerTree CONFIG)`, the exported
`tools::data_structures::finger_tree` target, and installed headers:

```powershell
ctest --preset msvc-debug -R '^fingertree\.installed-consumer$' --output-on-failure
```

## Continuous Integration

`.github/workflows/cpp-fingertree.yml` runs Debug/Release CTest lanes for MSVC, GCC, and Clang. Separate Linux
Clang jobs run ASan+UBSan and TSan with allocation tracking disabled. Release MSVC and GCC jobs also run the short
persistence-branching probe. The Clang Debug lane additionally runs the Clang static analyzer with warnings as
errors over the aggregate public-header consumer. This is the deliberate practical static-analysis gate: it
covers the aggregate include and representative deque/measure/rope instantiations, not every possible template
specialization. MSVC `/analyze` remains a complementary local probe when viable. Because the installed consumer
is an ordinary CTest entry, every compiler lane proves the package export and relocation path rather than merely
compiling against the source-tree include directory.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/Cpp/FingerTree> cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --preset msvc-debug --output-on-failure"
```

If a docs-only change only updates links or wording and does not alter commands, C++ API claims, stress behavior,
or benchmark-status claims, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.

For a focused stateful replay, record both the CTest case and explicit seed, for example:

```text
src/Cpp/FingerTree> out/build/msvc-debug/tests/fingertree_smoke_tests.exe --group command-model --seed 0x123456789abcdef
```
