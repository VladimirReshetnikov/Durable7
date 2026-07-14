# C++ HAMT Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:30:09Z
- Repository HEAD: 44a09cefa5719bb8cbdb78354353aff7f4075aa5
- Audience: Maintainers validating the C++ HAMT port
- Scope: Local build, test, warning-policy, and generated-output guidance for `src/Cpp/Hamt`

Use this guide when changing the C++ HAMT public headers, tests, examples, or documentation that
makes build or validation claims. For semantic contracts and practical value-semantics examples,
pair it with the [API specification](api-specification.md), [usage guide](usage.md), and exact
[Merkle search-tree specification](merkle-search-tree.md).

## Build Script

`build.ps1` is the validation entry point. It imports the local MSVC environment through
`C:\Scriptorium\windows\Import-VisualCppEnvironment.ps1 -IncludePrerelease`, copies the complete
public `include/Tools` subtree into `build/<Configuration>/package/include`, and compiles:

- `tests/persistent_hamt_tests.cpp` into `persistent_hamt_tests.exe`;
- `tests/merkle_search_tree_tests.cpp` into `merkle_search_tree_tests.exe`; and
- `tests/merkle_header_consumer.cpp` against only the copied package include root into
  `merkle_header_consumer.exe`.

The third program is an installed-header closure gate: it prevents the aggregate/public CHAMP and
Merkle surfaces from accidentally depending on source-tree-relative files or undeclared transitive
includes. It instantiates map/set transients through publication as well as the Merkle persistence
surface. All three programs link Windows CNG through `bcrypt.lib` because the public SHA-256
implementation selects CNG on Windows. Non-Windows consumers link OpenSSL Crypto.

The script uses these project-level compiler gates:

- Language mode: `/std:c++20`.
- Warning policy: `/W4`, `/WX`, and `/permissive-`.
- Standard-library feature reporting: `/Zc:__cplusplus`.
- Exception mode: `/EHsc`.
- Debug configuration: `/Od`, `/Zi`, `/MDd`.
- Release configuration: `/O2`, `/MD`, `/DNDEBUG`.

Generated package headers, object files, PDBs, and executables live under
`build/<Configuration>/`, which is ignored by the repository.

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

For changes to C++ HAMT public headers, tests, examples, or behavior documentation, compile and run
all three executables under all supported compiler lanes:

- MSVC Debug and Release through `build.ps1`.
- GCC/MinGW with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`.
- LLVM/Clang with `-std=c++20 -Wall -Wextra -Wpedantic -Werror`.

Each non-MSVC lane must run the executable it just produced. Keep generated binaries under `build/` or
`build/portable/`, which is ignored by the repository.

Typical direct GCC/MinGW Merkle lane (repeat for the existing HAMT suite and copied-header consumer):

```powershell
New-Item -ItemType Directory -Force build\portable | Out-Null
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude tests\merkle_search_tree_tests.cpp `
    -lbcrypt -o build\portable\merkle_search_tree_tests_gcc.exe
.\build\portable\merkle_search_tree_tests_gcc.exe
```

Typical Clang lane on Windows, using the Visual Studio developer environment for MSVC ABI headers and libraries:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$clangxx = "C:\Program Files\LLVM\bin\clang++.exe"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$clangxx"" -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude tests\merkle_search_tree_tests.cpp bcrypt.lib -o build\portable\merkle_search_tree_tests_clang.exe && build\portable\merkle_search_tree_tests_clang.exe"
```

## Portable Sanitizer Check

On hosts with GCC or Clang supporting AddressSanitizer and UndefinedBehaviorSanitizer, add a
sanitizer run for policy-object, collision-bucket, structural-sharing, codec, or block-construction
changes. Add `-lcrypto` when compiling the Merkle executable on a non-Windows host:

```powershell
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer `
    -Iinclude tests/persistent_hamt_tests.cpp -o build/persistent_hamt_tests_asan
./build/persistent_hamt_tests_asan
```

## Test Coverage

`tests/persistent_hamt_tests.cpp` and `tests/merkle_search_tree_tests.cpp` are deterministic native
test executables with small local registries. They print `[PASS]` lines and exit nonzero if any
check fails. `tests/merkle_header_consumer.cpp` is a focused aggregate/package include smoke and
returns nonzero on an API or exact-root disagreement. See the [tests README](../tests/README.md) for
the coverage groups, direct executable paths, and runner failure behavior.

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
- set algebra against `std::unordered_set`, including same-policy structural overloads, zero-hash
  shared-ancestry pruning, independent-policy fallback, and duplicate treatment for symmetric
  difference;
- bulk-builder construction: freeze isolation across later builder mutations, first-key and
  equal-value retention for duplicates, final-hash-level branching, a collision-heavy randomized
  build checked against persistent updates, and builder-backed `create_range`/`intersect_with`
  semantics.
- move-only CHAMP map/set edit sessions: empty and retained-value adoption, clean/no-op root and
  policy preservation, stored representatives, point edits and clear, source isolation, active
  lookup and materialization, and rvalue-only one-way publication;
- session lifecycle and iteration: copied generation-bound iterators, real-edit invalidation,
  no-op stability, transfer across move construction, overwritten-destination invalidation on move
  assignment, deterministic consumed/moved-from failures, and set-facade delegation;
- active set-session relations through receiver-policy initializer-list, persistent-set, and range
  overloads, including equivalent duplicates, iterator stability, and empty-probe consumed checks;
- edit-session failure/model coverage: injected hash failure without state or iterator change and a
  5,000-operation collision-heavy map session checked against `std::unordered_map` while retaining
  the original source snapshot;
- 32-/64-bit Patricia signed ordering, 10,000 deterministic updates against `std::map`, retained
  no-op roots, cached subtree counts, fixed-bias and resolver-combining map algebra, and integer-set
  union/intersection/difference.

The Merkle suite covers:

- strict signed big-endian integer, tagged nullable UTF-8/bytes, RFC-4122 GUID, SHA-256 digest, and
  malformed/noncanonical codec vectors;
- Unicode-whitespace-aware policy and codec identifier validation, explicit policy identity/domain
  compatibility, and the shared C#/C/Haskell/Kotlin/Rust domain, empty digest, root digest, and
  complete `MST2` block golden;
- canonical bulk/incremental construction, insertion, replacement, deletion/contraction,
  encoded-value no-ops, absent-remove root reuse, and independent-history convergence;
- move-only keys and values, owning entry handles, root/policy/block identity, retained-snapshot
  sharing, comparator-ordered iteration, inclusive ranges, semantic equality, and typed diff;
- a 12,000-operation deterministic randomized model against `std::map`, retained versions, and an
  independent canonical rebuild;
- throwing codec/comparator paths and unchanged published snapshots after failures;
- deep validation of entry encodings, key levels, child intervals, cached metadata, exact blocks,
  and digests, including a mutated-representative regression; and
- multiple concurrent readers over retained immutable trees.
- exact `MSP2` membership, nonmembership, and inclusive-range query goldens;
- complete and partial pack save/load/import, immutable block-store snapshots, and destination
  conflict preflight before publication;
- all seven verification limits, including query/step/expansion shape precedence before proof
  maps, hashing, codecs, or decode allocation;
- missing, digest-tampered, malformed, noncanonical, foreign-domain, unsupported, count-corrupt,
  extra-proof, missing-proof, and bad-expansion rejection;
- closure-pruned packs and iterative missing-frontier synchronization;
- present-null-safe three-way merge, unresolved all-or-nothing output, resolver failure safety, and
  canonical handle reuse;
- move-only key/value load, import, proof, and merge instantiation; and
- concurrent store publication, load, proof verification, and sync reads.

The copied-header consumer includes the installed aggregate header without source-tree include
paths, instantiates map/set edit sessions and publication, pins the one-entry golden root, and
instantiates export/save/load, proof verification, and merge. This verifies public include closure
and the required crypto link independently from the full native suite.

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
