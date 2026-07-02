# C++ FingerTree Tests

- Created (UTC): 2026-07-02T21:06:57Z
- Repository HEAD: 399710816b9007dde1374aef2043f118beddc225
- Audience: Maintainers validating the C++ FingerTree port
- Scope: Native test executable, source grouping, and stress controls under `src/Cpp/FingerTree/tests`

The C++ FingerTree workspace currently has one native test executable, `fingertree_smoke_tests`, registered with
CTest as `fingertree.smoke`. The executable uses the small local runner in `test_support/test_runner.hpp` instead
of Catch2 or GoogleTest, keeping the workspace dependency-free while the port remains header-first.

`smoke_tests.cpp` is the executable entry point. It configures non-interactive MSVC failure reporting, registers
basic aggregate-header and test-support checks, and then calls each domain-specific `add_*_tests` function.

## Source Map

- `measure_tests.cpp` covers measure contracts, lower/upper-bound predicates, named min/max/key/order/sum/product
  operations, component projection, and allocation guards.
- `persistent_deque_tests.cpp` covers endpoint updates, indexing, split/concat, sorted search, retained versions,
  randomized command histories, allocation counters, and operation counters.
- `measured_finger_tree_tests.cpp`, `lazy_cell_tests.cpp`, and `measured_lazy_cell_tests.cpp` cover the measured
  tree core plus lazy publication helpers.
- `reversible_deque_tests.cpp` covers reverse orientation, mixed-orientation updates, random histories, and
  O(1)-reverse allocation checks.
- `sorted_collection_tests.cpp` covers sorted bag, sorted set, and sorted map ranking, navigation, range queries,
  custom order, set algebra, and randomized model checks.
- `priority_queue_tests.cpp` covers ordering, duplicate priorities, stability, and command-model behavior.
- `interval_tree_tests.cpp` covers insertion, overlap, containment, coalescing, removal, and sweep-model checks.
- `rope_tests.cpp`, `measured_rope_tests.cpp`, and `rope_text_tests.cpp` cover chunked sequence editing, measured
  searches, text interop, line navigation, and randomized vector/string-model histories.
- `atomic_box_tests.cpp` and `tearable_concurrency_tests.cpp` cover lock-free publication helpers and structure-level
  tearable-value stress tests.
- `test_support/` holds the local runner, deterministic command-history diagnostics, allocation counting, and
  operation-count helpers used across the suite.

## Build And Run

From `src/Cpp/FingerTree`, build and run the Debug CTest target:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure -R ""^fingertree\.smoke$"""
```

Run the built executable directly when changing runner output, failure diagnostics, or tests that need local
iteration outside CTest:

```powershell
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe
```

The runner prints one `[pass]` line per registered test and exits non-zero after any failed test.

## Stress Controls

`tearable_concurrency_tests.cpp` reads `FINGERTREE_STRESS_SECONDS`. If the variable is unset, stress cases use a
short default suitable for ordinary Debug validation. Raise the value for local soak runs without changing source:

```powershell
$env:FINGERTREE_STRESS_SECONDS = '30'
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure -R ""^fingertree\.smoke$"""
Remove-Item Env:\FINGERTREE_STRESS_SECONDS
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, warning policy, generated-output
locations, and benchmark-harness status.
