# C++ Ordered Validation

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers and agents validating the C++ Ordered workspace
- Scope: Serialized configure, compile, and CTest gates for `src/Cpp/Ordered`

Run the workspace through the C++ root wrapper:

```powershell
cd src/Cpp
.\build.ps1 -Workspace Ordered -RunTests
.\build.ps1 -Workspace Ordered -Configuration Release -RunTests
```

The wrapper selects the matching MSVC preset and explicitly passes `--parallel 1` to both the CMake
build and CTest. Every checked-in build and test preset also fixes its job count at one. Run the two
configurations sequentially and do not overlap them with another repository toolchain.

Portable local presets are available for later isolated validation:

```powershell
cd src/Cpp/Ordered
cmake --preset ninja-debug
cmake --build --preset ninja-debug --parallel 1
ctest --preset ninja-debug --parallel 1 --output-on-failure
```

The target compiles with strict warnings-as-errors (`/W4 /WX` on MSVC or
`-Wall -Wextra -Wpedantic -Werror` elsewhere). CTest enters the repository headless process mode.
The test executable covers the semantic areas listed in the [test map](../tests/README.md).

Routine validation intentionally runs no benchmark. Performance measurement remains postponed to an
isolated session without competing agents or substantial CPU, memory, and I/O contention.
