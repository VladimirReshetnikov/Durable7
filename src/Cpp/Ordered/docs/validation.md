# C++ Ordered Validation

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Updated (UTC): 2026-07-16T22:52:15Z
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
The test executable covers both ordered collections and the semantic areas listed in the
[test map](../tests/README.md). The current executable runs 23 tests, including six ordered-map
tests. Local strict GCC 12 Debug and Clang 21 Release builds both pass the complete CTest target.

Routine validation intentionally runs no benchmark. Performance measurement remains postponed to an
isolated session without competing agents or substantial CPU, memory, and I/O contention.
