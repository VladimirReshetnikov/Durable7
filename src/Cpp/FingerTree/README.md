# C++ FingerTree

- Status: Active C++ workspace
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Updated (UTC): 2026-07-10T19:40:22Z
- Updated against repository HEAD: 82a19b89405110255d76b848e6dff8a8f8d73bee
- Audience: Maintainers implementing and reviewing the C++ port
- Scope: Build entry points, layout, and validation for `src/Cpp/FingerTree`

This workspace contains the C++ port of the `FingerTree` data-structure library. The port follows
[`docs/port-plan.md`](docs/port-plan.md): a header-first library under the namespace
`tools::data_structures::finger_tree`, CMake/Ninja build entry points for the local MSVC toolchain, and CTest
validation from the first milestone onward.

The workspace is intentionally dependency-free beyond the C++ standard library. Its native tests use a small
local runner and its benchmark harness is repository-owned, so configuring a preset does not implicitly run a
package manager. There is deliberately no empty `vcpkg.json`; add a manifest only when a real dependency is wired
into CMake and validation.

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
PowerShell does not persist its environment changes in that process. The checked-in presets do not pin a Visual
Studio installation: they resolve `ninja` from the initialized environment or `PATH`. The validation matrix also covers GCC/MinGW
and Clang Debug/Release CTest lanes in separate `out/build/<compiler>-<configuration>` directories.
Host-agnostic `ninja-debug`, `ninja-release`, and `ninja-asan` presets are available when CMake, Ninja, and a
suitable C++23 compiler are on `PATH`. For release commands, sanitizer validation, stress controls, warning
policy, and generated-output locations, see the [validation guide](docs/validation.md).

CMake builds tests, samples, and the dependency-free benchmark harness by default. Disable individual developer
surfaces with `FINGERTREE_BUILD_TESTS=OFF`, `FINGERTREE_BUILD_SAMPLES=OFF`, or
`FINGERTREE_BUILD_BENCHMARKS=OFF`. The test suite includes a real installed-package consumer: it installs the
headers and package metadata to a private staging prefix, configures a new project with only `find_package`, links
`tools::data_structures::finger_tree`, and runs the resulting executable.

## Install And Consume

Install the header-first package from any configured build directory:

```powershell
cmake --install out/build/msvc-release --prefix out/install/fingertree --config Release
```

An external CMake project can then consume it without a source-tree include path:

```cmake
find_package(ToolsDataStructuresFingerTree 0.1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tools::data_structures::finger_tree)
```

Point `CMAKE_PREFIX_PATH` or `ToolsDataStructuresFingerTree_DIR` at the installation prefix when it is outside
CMake's normal search locations. The installed package includes version compatibility metadata and only public
headers as code artifacts, plus the repository MIT-0 license under the installation data directory; repository
tests, samples, and benchmarks are never part of the consumer build.

## Layout

- `include/tools/data_structures/finger_tree/` contains the public header-first library.
- `include/tools/data_structures/finger_tree/detail/` contains implementation helpers.
- `tests/` contains the [CTest-registered native smoke runner](tests/README.md) and shared test support.
- `samples/` contains the deterministic [showcase and persistent-snapshot tour](samples/README.md).
- `benchmarks/` contains the [dependency-free Milestone 8 harness](benchmarks/README.md).
- `cmake/` contains the installed-package configuration and consumer-smoke driver.
- `docs/` contains the port plan and C++-specific API, usage, implementation, review, and validation notes.
