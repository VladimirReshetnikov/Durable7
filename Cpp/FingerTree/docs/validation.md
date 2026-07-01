# C++ FingerTree Validation

- Status: Initial validation guide
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers validating the C++ port
- Scope: Local build, test, and stress commands; benchmark harness status

## Debug Build And Tests

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$cmakeDir\cmake.exe" --preset msvc-debug
& "$cmakeDir\cmake.exe" --build --preset msvc-debug
& "$cmakeDir\ctest.exe" --preset msvc-debug
```

## Release Build

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$cmakeDir\cmake.exe" --preset msvc-release
& "$cmakeDir\cmake.exe" --build --preset msvc-release
& "$cmakeDir\ctest.exe" --preset msvc-release
```

## Test Policy

Each randomized test must print a replay seed on failure. Complexity and concurrency tests should use deterministic
operation counters, allocation counters, and duration environment variables rather than timing thresholds.

The current bootstrap tests are self-contained CTest executables. Structure-level tearable concurrency stress tests
honor `FINGERTREE_STRESS_SECONDS`; unset runs use a short default suitable for ordinary `ctest`, while soak runs
can raise the value without editing source.

Later milestones may add Catch2 through vcpkg once the dependency manager is intentionally introduced.

## Benchmark Harness Status

No native benchmark target is currently checked in. Milestone 8 still needs the planned persistence, catenation,
reversal, priority, interval, and rope benchmark harnesses.
