# C++ HAMT Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:30:09Z
- Repository HEAD: 44a09cefa5719bb8cbdb78354353aff7f4075aa5
- Audience: Maintainers validating the C++ HAMT port
- Scope: Local build, test, warning-policy, and generated-output guidance for `src/Cpp/Hamt`

Use this guide when changing the C++ HAMT public headers, tests, examples, or documentation that makes build
or validation claims. For semantic contracts and practical value-semantics examples, pair it with the
[API specification](api-specification.md) and [usage guide](usage.md).

## Build Script

`build.ps1` is the validation entry point. It imports the local MSVC environment through
`C:\Scriptorium\windows\Import-VisualCppEnvironment.ps1 -IncludePrerelease`, then compiles
`tests/persistent_hamt_tests.cpp` into `build/<Configuration>/persistent_hamt_tests.exe`.

The script uses these project-level compiler gates:

- Language mode: `/std:c++20`.
- Warning policy: `/W4`, `/WX`, and `/permissive-`.
- Standard-library feature reporting: `/Zc:__cplusplus`.
- Exception mode: `/EHsc`.
- Debug configuration: `/Od`, `/Zi`, `/MDd`.
- Release configuration: `/O2`, `/MD`, `/DNDEBUG`.

Generated object, PDB, and executable outputs live under `build/<Configuration>/`, which is ignored by
the repository.

## Commands

From `src/Cpp/Hamt`:

```powershell
.\build.ps1
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

Use the first command when you only need a compile gate. Use the `-RunTests` forms before committing
behavior changes, public API changes, policy-object changes, or documentation that claims the tests pass.

## Compiler Matrix Policy

For changes to C++ HAMT public headers, tests, examples, or behavior documentation, compile and run tests under
all three supported compiler lanes:

- MSVC Debug and Release through `build.ps1`.
- GCC/MinGW with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`.
- LLVM/Clang with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`.

Each non-MSVC lane must run the executable it just produced. Keep generated binaries under `build/` or
`build/portable/`, which is ignored by the repository.

Typical direct GCC lane:

```powershell
New-Item -ItemType Directory -Force build\portable | Out-Null
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude tests\persistent_hamt_tests.cpp `
    -o build\portable\persistent_hamt_tests_gcc.exe
.\build\portable\persistent_hamt_tests_gcc.exe
```

Typical Clang lane on Windows, using the Visual Studio developer environment for MSVC ABI headers and libraries:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$clangxx = "C:\Program Files\LLVM\bin\clang++.exe"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$clangxx"" -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude tests\persistent_hamt_tests.cpp -o build\portable\persistent_hamt_tests_clang.exe && build\portable\persistent_hamt_tests_clang.exe"
```

## Portable Sanitizer Check

On hosts with GCC or Clang supporting AddressSanitizer and UndefinedBehaviorSanitizer, add a sanitizer run for
policy-object, collision-bucket, and structural-sharing changes:

```powershell
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer `
    -Iinclude tests/persistent_hamt_tests.cpp -o build/persistent_hamt_tests_asan
./build/persistent_hamt_tests_asan
```

## Test Coverage

`tests/persistent_hamt_tests.cpp` is a deterministic native test executable with a small local test
registry. It prints `[PASS]` lines and exits nonzero if any check fails. See the
[tests README](../tests/README.md) for named test cases, the direct executable path, and runner failure behavior.

The suite covers:

- empty map/set behavior and count/lookup invariants;
- persistent insert, update, duplicate rejection, remove, and clear behavior;
- custom hash/equality policy objects, including case-insensitive string keys;
- equal-hash collision buckets and deep shared-prefix shapes;
- first equivalent key/item retention;
- no-op root reuse and structural sharing shape checks;
- iteration and value materialization;
- randomized map histories checked against `std::unordered_map`, including retained snapshots;
- a long scripted collision and deep-prefix scenario with retained snapshots and no-op root reuse checks;
- randomized histories with deliberately colliding hashes;
- set add/remove/contains persistence;
- set algebra against `std::unordered_set`, including duplicate treatment for symmetric difference.

For new behavior, prefer adding deterministic model checks here before relying on example-only coverage.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/Cpp/Hamt> .\build.ps1 -Configuration Release -RunTests
```

If a docs-only change only updates links or wording and does not alter commands or C++ API claims, the
repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
