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
`src/hamt.c`, `src/persistent_hash_bag.c`, and `tests/persistent_hash_bag_tests.c` into
`build/<Configuration>/persistent_hash_bag_tests.exe`, then compiles
`src/hamt.c`, `src/persistent_bi_map.c`, and `tests/persistent_bi_map_tests.c` into
`build/<Configuration>/persistent_bi_map_tests.exe`, then compiles
`src/persistent_hash_multimap.c`, `src/persistent_relation.c`, and
`tests/persistent_hash_multimap_tests.c` into
`build/<Configuration>/persistent_hash_multimap_tests.exe`, then compiles
`src/persistent_map_patch.c`, `src/persistent_directed_graph.c`,
`src/persistent_indexed_map.c`, and `tests/persistent_derived_structures_tests.c` into
`build/<Configuration>/persistent_derived_structures_tests.exe`, then compiles
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
- Test-only allocation hooks: `/DD7_HAMT_TESTING`, enabling deterministic fail-after-N allocation
  injection inside the native executables without adding hooks to the public headers.
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
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -DD7_HAMT_TESTING `
    -Iinclude -I../../test_support/include src\hamt.c tests\hamt_tests.c `
    -o build\portable\hamt_tests_gcc.exe
.\build\portable\hamt_tests_gcc.exe
```

Compile the bag executable against both production sources in the same serialized lane:

```powershell
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -DD7_HAMT_TESTING `
    -Iinclude -I../../test_support/include `
    src\hamt.c src\persistent_hash_bag.c tests\persistent_hash_bag_tests.c `
    -o build\portable\persistent_hash_bag_tests_gcc.exe
.\build\portable\persistent_hash_bag_tests_gcc.exe
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

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$clang"" -std=c17 -Wall -Wextra -Wpedantic -Werror -DD7_HAMT_TESTING -Iinclude -I../../test_support/include src\hamt.c tests\hamt_tests.c -o build\portable\hamt_tests_clang.exe && build\portable\hamt_tests_clang.exe"
```

## Portable Sanitizer Check

On hosts with GCC or Clang supporting AddressSanitizer and UndefinedBehaviorSanitizer, add a sanitizer run for
ownership-policy and collision-bucket changes:

```powershell
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -DD7_HAMT_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer `
    -Iinclude -I../../test_support/include src/hamt.c tests/hamt_tests.c -o build/hamt_tests_asan
./build/hamt_tests_asan
```

## Test Coverage

`tests/hamt_tests.c` is a deterministic native test executable. It prints `[PASS]` lines and exits nonzero
on the first failed check. See the [tests README](../tests/README.md) for named test cases, the direct
executable path, and runner failure behavior.

`tests/persistent_hash_bag_tests.c` is the deterministic map-backed multiset executable. It covers
first-representative retention, positive one-descent factory updates, checked `int32_t`/`int64_t`
overflow, validation-before-callback behavior, root-sharing no-ops, saturated removal, alias-safe
publication, expanded/distinct/entry iteration, same- and cross-policy algebra, eager foreign-policy
collapse, retained snapshots, and a 1,000-step model history. Independent fail-after-N sweeps cover
HAMT node and internal count allocation in point updates and all four foreign-policy algebra
operations. Targeted item-retain and range-construction failures add unchanged-output and
balanced-owner checks.

`tests/persistent_bi_map_tests.c` covers independent key/value callback contexts, strict conflict
precedence, first representatives, policy-aware non-displacing replacement, symmetric removal,
stored `NULL`, clear, O(1) inverse round trips, a 2,000-operation collision-heavy two-map model,
canonical validation, and failpoint atomicity.
`tests/persistent_hash_multimap_tests.c` covers independent key/value policies, nonempty group
normalization, distinct-key versus checked pair counts, first representatives, alias-safe point and
group edits, receiver-policy algebra, retained snapshots, and two-level validation.
The same executable covers relation forward/reverse lookup, exact inverse maintenance, duplicate
and missing-pair no-ops, clear, retained snapshots, and failure-atomic publication of both component
successors.

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
- one-hash/one-descent `get_or_add` and `add_or_update` factory selection across leaf, collision,
  inline bitmap, and child bitmap paths, including eager validation, stored representative
  retention, copied-retain outputs, callback failure, and fail-at-every-allocation atomicity;
- first equivalent key/item retention for custom equality policies;
- equal-hash collision buckets, collision-bucket splitting, and hash-mismatch misses;
- deep shared hash-prefix lookup/removal cases;
- all seven inline iterator frames through a depth-7 shared-prefix traversal;
- fail-after-N allocation injection across recursive hash-node merge and bitmap `node_set` insertion paths,
  verifying source persistence and complete unwind at every allocation boundary;
- no-op root reuse and structural sharing shape checks;
- canonical lockstep map equality/diff over independent histories, including reversed equal-hash
  collision runs and typed added/removed/changed payloads, plus a retained-lineage fixture proving
  that pointer-identical descendants are skipped without hashing and with localized key/value
  equality callbacks;
- independent iterator copies;
- randomized map histories checked against an in-memory model, including retained snapshots;
- a long scripted collision and deep-prefix scenario with retained snapshots and no-op root reuse checks;
- randomized histories with deliberately colliding hashes;
- one-way map/set transient creation, O(1)-shape root adoption, policy/context and first-
  representative preservation, clean root identity, changed snapshot isolation, active reads,
  version-bound iteration, clear, terminal publication, and shared-alias consumption;
- a 4,096-step deterministic transient map history checked against the persistent array model;
- transient fail-at-every-allocation sweeps for wrapper adoption, collision-heavy add/replace/remove,
  and the set façade, plus retain-callback failure sweeps that prove unchanged root/content/version,
  untouched output flags, iterator validity, retryability, and balanced unwind;
- transient set relations over duplicate-heavy arrays and cross-policy persistent sets, including
  consumed lifecycle and allocation-failure sweeps with atomic boolean outputs;
- set add/remove/contains persistence;
- set algebra against model sets, including structural two-set overloads, zero-rehash shared-node
  pruning, cached-cardinality validation, and duplicate treatment for symmetric difference.

For new behavior, prefer adding deterministic model checks here before relying on example-only coverage.

## Persistent Bidirectional Map Shipment Evidence

The persistent bidirectional map was validated on 2026-07-15 without running benchmarks. All
commands ran sequentially with a single build worker:

- `./build.ps1 -RunTests` built the MSVC Debug executables and passed 43 core HAMT tests, 9 hash-bag
  tests, the persistent-bimap suite, the Patricia suite, and 22 Merkle tests;
- `./build.ps1 -Configuration Release -RunTests` passed the same complete set under MSVC Release;
- a focused GCC C17 build of `hamt.c`, `persistent_bi_map.c`, and
  `persistent_bi_map_tests.c` passed with `-Wall -Wextra -Wpedantic -Werror`; and
- the equivalent focused LLVM/Clang C17 build passed with the same strict warning flags.

The focused portable lanes exercise the complete bimap executable, including the 2,000-operation
model history and allocation-failure atomicity checks. Performance measurements remain deliberately
postponed until they can run in an isolated environment.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/C/Hamt> .\build.ps1 -Configuration Release -RunTests
```

If a docs-only change only updates links or wording and does not alter commands or C API claims, the
repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
