# C++ FingerTree

- Status: Initial C++ workspace
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers implementing and reviewing the C++ port
- Scope: Build entry points, layout, and validation for `Cpp/FingerTree`

This workspace contains the C++ port of the `FingerTree` data-structure library. The port follows
[`docs/port-plan.md`](docs/port-plan.md): a header-first library under the namespace
`tools::data_structures::finger_tree`, CMake/Ninja build entry points for the local MSVC toolchain, and CTest
validation from the first milestone onward.

The first implementation wave is intentionally dependency-light. The core library uses only the standard library,
and the bootstrap tests use a small local test runner so the workspace builds before any package manager is
introduced. The `vcpkg.json` manifest is present for future Catch2 and Google Benchmark integration.

## Build

From this directory:

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "$cmakeDir\cmake.exe" --preset msvc-debug
& "$cmakeDir\cmake.exe" --build --preset msvc-debug
& "$cmakeDir\ctest.exe" --preset msvc-debug
```

Use `msvc-release` for the optimized configuration. If a shell does not already have `cl.exe` on `PATH`, launch it
from an initialized Visual Studio developer environment first.

## Layout

- `include/tools/data_structures/finger_tree/` contains the public header-first library.
- `include/tools/data_structures/finger_tree/detail/` contains implementation helpers.
- `tests/` contains CTest-registered native tests and shared test support.
- `docs/` contains the port plan and C++-specific API/validation notes.
- `samples/` and `benchmarks/` are reserved for the later milestones in the port plan.
