# C HAMT Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:30:09Z
- Repository HEAD: 44a09cefa5719bb8cbdb78354353aff7f4075aa5
- Audience: Maintainers validating the C HAMT port
- Scope: Local build, test, warning-policy, and generated-output guidance for `src/C/Hamt`

Use this guide when changing the C HAMT public header, implementation, tests, examples, or documentation
that makes build or validation claims. For semantic contracts and practical ownership examples, pair it with
the [API specification](api-specification.md) and [usage guide](usage.md).

## Build Script

`build.ps1` is the validation entry point. It imports the local MSVC environment through
`C:\Scriptorium\windows\Import-VisualCppEnvironment.ps1 -IncludePrerelease`, then compiles
`src/hamt.c` and `tests/hamt_tests.c` into `build/<Configuration>/hamt_tests.exe`, then compiles
`src/patricia.c` and `tests/patricia_tests.c` into
`build/<Configuration>/patricia_tests.exe`, and finally compiles `src/merkle_search_tree.c` and
`tests/merkle_search_tree_tests.c` into
`build/<Configuration>/merkle_search_tree_tests.exe`. The Merkle target links Windows CNG through
`bcrypt.lib`; portable non-Windows builds link OpenSSL Crypto.

The script uses these project-level compiler gates:

- C mode: `/TC` and `/std:c17`.
- Warning policy: `/W4`, `/WX`, and `/permissive-`.
- Exception mode: `/EHsc-`; the C port does not use C++ exceptions.
- Flexible-array-member warning suppression: `/wd4200`, because the implementation intentionally uses
  flexible storage for compact HAMT nodes.
- Test-only allocation hooks: `/DTDS_HAMT_TESTING`, enabling deterministic fail-after-N allocation injection
  inside the native executable without adding hooks to the public header.
- Debug configuration: `/Od`, `/Zi`, `/MDd`.
- Release configuration: `/O2`, `/MD`, `/DNDEBUG`.

Generated object, PDB, and executable outputs live under `build/<Configuration>/`, which is ignored by
the repository.

## Commands

From `src/C/Hamt`:

```powershell
.\build.ps1
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

Use the first command when you only need a compile gate. Use the `-RunTests` forms before committing
behavior changes, public API changes, ownership-policy changes, or documentation that claims the tests pass.

## Compiler Matrix Policy

For changes to C HAMT source, headers, tests, examples, or behavior documentation, compile and run tests under
all three supported compiler lanes:

- MSVC Debug and Release through `build.ps1`.
- GCC/MinGW with `-std=c17 -Wall -Wextra -Wpedantic -Werror`.
- LLVM/Clang with `-std=c17 -Wall -Wextra -Wpedantic -Werror`.

Each non-MSVC lane must run the executable it just produced. Keep generated binaries under `build/` or
`build/portable/`, which is ignored by the repository.

Typical direct GCC lane:

```powershell
New-Item -ItemType Directory -Force build\portable | Out-Null
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -DTDS_HAMT_TESTING -Iinclude src\hamt.c tests\hamt_tests.c `
    -o build\portable\hamt_tests_gcc.exe
.\build\portable\hamt_tests_gcc.exe
```

The Merkle lane on Windows uses CNG:

```powershell
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -Iinclude `
    src\merkle_search_tree.c tests\merkle_search_tree_tests.c -lbcrypt `
    -o build\portable\merkle_search_tree_tests_gcc.exe
.\build\portable\merkle_search_tree_tests_gcc.exe
```

On non-Windows hosts replace `-lbcrypt` with the OpenSSL Crypto link flags (normally `-lcrypto`).

Typical Clang lane on Windows, using the Visual Studio developer environment for MSVC ABI headers and libraries:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$clang = "C:\Program Files\LLVM\bin\clang.exe"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$clang"" -std=c17 -Wall -Wextra -Wpedantic -Werror -DTDS_HAMT_TESTING -Iinclude src\hamt.c tests\hamt_tests.c -o build\portable\hamt_tests_clang.exe && build\portable\hamt_tests_clang.exe"
```

## Portable Sanitizer Check

On hosts with GCC or Clang supporting AddressSanitizer and UndefinedBehaviorSanitizer, add a sanitizer run for
ownership-policy and collision-bucket changes:

```powershell
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -DTDS_HAMT_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer `
    -Iinclude src/hamt.c tests/hamt_tests.c -o build/hamt_tests_asan
./build/hamt_tests_asan
```

## Test Coverage

`tests/hamt_tests.c` is a deterministic native test executable. It prints `[PASS]` lines and exits nonzero
on the first failed check. See the [tests README](../tests/README.md) for named test cases, the direct
executable path, and runner failure behavior.

`tests/patricia_tests.c` is the explicit-width integer-map/set executable. It covers signed extrema
and traversal order, root-sharing no-ops, retained snapshots, fixed and callback-combining map
algebra, set algebra, 10,000 deterministic randomized updates against an array model, randomized
structural set algebra, and value retain/release balance.

`tests/merkle_search_tree_tests.c` is the canonical core/wire/persistence executable. It covers every built-in
codec, strict malformed-input rejection, digest parsing, policy/tag compatibility, the exact C# and
Rust MST2 golden vector, history-independent bulk/incremental shape, structural sharing, inclusive
ranges, digest-pruned diff, stable equivalent-key representatives, retained randomized snapshots,
deep validation, streaming callback failure, and fail-at-every-allocation unwind/publication for
policy creation, point updates, bulk construction, validation, sharing diagnostics, block export,
point/range proofs, verified load/import, sync pack/plan, and three-way merge. It validates all seven
verification limits, root-closure requirements, legal unreachable authenticated blocks, destination
preflight, exact MSP2 membership/nonmembership/range proofs, query/step/expansion precedence before
bomb callbacks, malformed/tampered/foreign/extra/omitted proof material, nullable present-null merge
semantics, and callback-failure publication. Windows validation also runs eight concurrent tree
readers. The memory-store race uses eight real workers on both Windows and the C11 `threads.h` lane
across identical and conflicting puts.
A reentrant allocator/deallocator regression calls back into the live store during growth, snapshot
visitation, and clear to prove no user callback executes under the non-recursive store lock; malicious
store callbacks also verify owning-output and put-state/status shielding.

The suite covers:

- empty map/set behavior and count/lookup invariants;
- persistent `set`, `try_add`, `remove`, `try_remove`, `set_many`, and `clear` behavior;
- first equivalent key/item retention for custom equality policies;
- equal-hash collision buckets, collision-bucket splitting, and hash-mismatch misses;
- deep shared hash-prefix lookup/removal cases;
- all seven inline iterator frames through a depth-7 shared-prefix traversal;
- fail-after-N allocation injection across recursive hash-node merge and bitmap `node_set` insertion paths,
  verifying source persistence and complete unwind at every allocation boundary;
- no-op root reuse and structural sharing shape checks;
- independent iterator copies;
- randomized map histories checked against an in-memory model, including retained snapshots;
- a long scripted collision and deep-prefix scenario with retained snapshots and no-op root reuse checks;
- randomized histories with deliberately colliding hashes;
- set add/remove/contains persistence;
- set algebra against model sets, including duplicate treatment for symmetric difference.

For new behavior, prefer adding deterministic model checks here before relying on example-only coverage.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/C/Hamt> .\build.ps1 -Configuration Release -RunTests
```

If a docs-only change only updates links or wording and does not alter commands or C API claims, the
repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
