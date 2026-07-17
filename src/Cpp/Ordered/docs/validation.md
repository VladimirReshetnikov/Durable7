# C++ Ordered Validation

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Updated (UTC): 2026-07-17T00:25:16Z
- Updated Repository HEAD: a26aac8f4ec2fa60a2d4871568c2c02d24c9b2a2
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
tests. The 2026-07-16 portable audit passed the complete 23-test target with strict Clang 21 in
both Debug and Release. Ordered sorting uses each entry's monotone stamp as its final tie-break, so
`std::sort` preserves the public stable-order contract without depending on deprecated temporary-buffer
internals in older standard libraries.

Routine validation intentionally runs no benchmark. Performance measurement remains postponed to an
isolated session without competing agents or substantial CPU, memory, and I/O contention.
