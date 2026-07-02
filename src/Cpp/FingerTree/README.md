# C++ FingerTree

- Status: Active C++ workspace
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Audience: Maintainers implementing and reviewing the C++ port
- Scope: Build entry points, layout, and validation for `src/Cpp/FingerTree`

This workspace contains the C++ port of the `FingerTree` data-structure library. The port follows
[`docs/port-plan.md`](docs/port-plan.md): a header-first library under the namespace
`tools::data_structures::finger_tree`, CMake/Ninja build entry points for the local MSVC toolchain, and CTest
validation from the first milestone onward.

The workspace is intentionally dependency-light. The core library uses only the standard library, and the CTest
executables use a small local test runner so the workspace builds without a package manager. The `vcpkg.json`
manifest is present but currently has no dependencies; introduce Catch2, Google Benchmark, or other packages only
when they are intentionally wired into CMake.

The active CMake model is C++23 plus MSVC `/std:c++latest`: the interface library advertises `cxx_std_23`, test
targets use `CXX_STANDARD 23`, and MSVC targets receive `/std:c++latest` explicitly because the bundled CMake does
not model the installed compiler's latest language mode as a standard number.

## Build

From this directory:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure"
```

Use `msvc-release` for the optimized configuration. Keep the Visual Studio environment setup, configure, build,
and CTest run in one `cmd.exe` chain when starting from plain PowerShell; invoking `VsDevCmd.bat` directly from
PowerShell does not persist its environment changes in that process. For release commands, stress controls,
warning policy, and generated-output locations, see the [validation guide](docs/validation.md).

## Layout

- `include/tools/data_structures/finger_tree/` contains the public header-first library.
- `include/tools/data_structures/finger_tree/detail/` contains implementation helpers.
- `tests/` contains the [CTest-registered native smoke runner](tests/README.md) and shared test support.
- `docs/` contains the port plan and C++-specific API, usage, implementation, review, and validation notes.
- No C++ `samples/` or `benchmarks/` directory is currently checked in; those remain later port-plan milestones.
