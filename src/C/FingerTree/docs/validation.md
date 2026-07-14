# C FingerTree Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers validating the C port
- Scope: Local build, test, sample, benchmark, warning-policy, and generated-output guidance for `src/C/FingerTree`

Use this guide when changing the C FingerTree public header, implementation, tests, samples, benchmark harness,
or documentation that makes build or validation claims. For API shape and practical ownership examples, pair it
with the [API notes](api-notes.md) and [usage guide](usage.md).

## Build Model

The workspace uses CMake presets. The `msvc-*` presets use Visual Studio's bundled Ninja by absolute path;
the `ninja-*` presets use `cmake` and `ninja` from `PATH` for host-agnostic validation. `CMakeLists.txt` builds the
`tools_data_structures_finger_tree_c` static library from `src/fingertree.c`,
`src/brodal_okasaki_heap.c`, `src/canonical_sorted_set.c`, `src/priority_search_queue.c`,
`src/rrb_vector.c`, and `src/daba_lite.c`,
with these options enabled by default:

- `FINGERTREE_C_BUILD_TESTS`: builds `tests/fingertree_c_tests`,
  `tests/brodal_okasaki_heap_c_tests`, `tests/canonical_sorted_set_c_tests`,
  `tests/priority_search_queue_c_tests`, `tests/rrb_vector_c_tests`, and `tests/daba_lite_c_tests`, registering
  `fingertree_c.core`, `fingertree_c.brodal_okasaki_heap`, `fingertree_c.canonical_sorted_set`,
  `fingertree_c.priority_search_queue`, `fingertree_c.rrb_vector`, and `fingertree_c.daba_lite`.
- `FINGERTREE_C_BUILD_SAMPLES`: builds `samples/fingertree_c_showcase` and
  `samples/fingertree_c_snapshots`, both registered as CTest smoke tests.
- `FINGERTREE_C_BUILD_BENCHMARKS`: builds `benchmarks/fingertree_c_benchmarks`.

With tests and samples enabled, a complete CTest run contains eight targets: six library test executables and
two sample smoke tests.

The project is C11 (`C_STANDARD 11`, required, extensions off). MSVC targets build with `/permissive-`,
`/W4`, `/WX`, `/external:anglebrackets`, and `/external:W0`; non-MSVC targets use `-Wall -Wextra
-Wpedantic -Werror`. Generated files live under `out/build/<preset>/`, which is ignored by the repository.
Canonical rank derivation and random-key creation use Windows CNG and link `bcrypt` on Windows. Other
platforms require the maintained OpenSSL Crypto package discovered by CMake and link `OpenSSL::Crypto`.

## Compiler Matrix Policy

For changes to C FingerTree source, headers, tests, samples, benchmarks, or behavior documentation, compile and
run tests under all three supported compiler lanes:

- MSVC Debug and Release through `msvc-debug` and `msvc-release`.
- GCC/MinGW in a separate CMake/Ninja build directory.
- LLVM/Clang in a separate CMake/Ninja build directory.

Each lane must run CTest against the binaries from its own build directory. Do not reuse `out/build/msvc-*`
binaries as evidence for GCC or Clang.

## Debug Build And Tests

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure"
```

## Release Build And Tests

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-release && ""$cmakeDir\cmake.exe"" --build --preset msvc-release --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-release --parallel 1 --output-on-failure"
```

A plain PowerShell invocation of `VsDevCmd.bat` does not persist its environment changes in the current
PowerShell process; keep configure/build/test in one `cmd.exe` chain when starting from an uninitialized shell.

## GCC Build And Tests

Use the WinLibs compiler, CMake, Ninja, and CTest directly when they are not on the current `PATH`:

```powershell
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"

& "$mingw\cmake.exe" -S . -B out\build\gcc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="$mingw\gcc.exe" -DCMAKE_MAKE_PROGRAM="$mingw\ninja.exe"
& "$mingw\cmake.exe" --build out\build\gcc-debug --parallel 1
& "$mingw\ctest.exe" --test-dir out\build\gcc-debug --parallel 1 --output-on-failure
```

## Clang Build And Tests

Use the Visual Studio developer environment when Clang targets the MSVC ABI:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$ninja = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$clang = "C:\Program Files\LLVM\bin\clang.exe"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmake"" -S . -B out\build\clang-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=""$clang"" -DCMAKE_MAKE_PROGRAM=""$ninja"" && ""$cmake"" --build out\build\clang-debug --parallel 1 && ""$ctest"" --test-dir out\build\clang-debug --parallel 1 --output-on-failure"
```

## Current Coverage

The bootstrap test executable `fingertree_c_tests` is registered as `fingertree_c.core`. See the
[tests README](../tests/README.md) for named test cases, direct executable path, and runner failure behavior.

The executable covers:

- endpoint, index, indexed replacement, split, concat, and persistence behavior for the generic tree/deque surface;
- lazy middle force paths through boundary pop, measure reads, indexing, traversal, split, concat, and disposal;
- size-measure `locate` and measure-guided split behavior;
- exhaustive structural split/locate boundary checks plus operation-count ceilings over a 4,096-element tree,
  guarding the O(log n) cached-subtree descent against leaf-by-leaf regressions;
- concurrent shared-snapshot copy/read/update/dispose behavior over atomic node and tree reference counts;
- reversible-deque logical reversal, endpoint edits, index reads, traversal, mixed-orientation concat, split,
  set/insert/remove, and persistence;
- sorted set uniqueness, sorted multiset duplicates, rank access, removal, traversal, and O(log n)
  signpost-guided bounds with zero payload copies on read-only search;
- sorted map insert/set/remove, duplicate rejection, lookup, rank access, traversal, and persistence;
- chunked rope construction across chunk boundaries, cumulative indexing, traversal, split/reconcat, chunk-local
  insertion/removal, boundary coalescing, bounded chunk counts, append, and persistence;
- explicit-lifetime positional rope cursors across empty/start/end and chunk-seam gaps, copied peeks,
  copy/move/dispose ownership, exact-alias edits, retained branches, unconditional replacement, range insertion,
  failure-output preservation, a deterministic 750-command gap model, and concurrent distinct-handle readers;
- empty/singleton and exact-maximum chunk boundaries, including split-on-insert and merge-on-remove/concat for
  positional and measured ropes;
- measured rope construction across chunk boundaries, cached whole/prefix measure reads, one-descent prefix
  measurement, cumulative-measure locate and split, chunk-local editing/coalescing, append, and persistence;
- priority queue minimum-first drain order and FIFO stability for equal priorities, including a 128-element tie run;
- signed 64-bit interval insertion, ordering, containment, removal, first-overlap, and overlap counting;
- generic endpoint interval insertion, ordering, containment, removal, invalid interval rejection, max-high-guided
  first-overlap and overlap counting, shared-annotation lifetime, comparison-count ceilings, and persistence;
- text rope construction, measured editing, indexing, O(1) line count, bidirectional offset/line-column navigation,
  invalid-column rejection, bounded chunk counts, and traversal.
- long text-rope edit scripts over retained snapshots, with model checks for indexing, traversal, line counts, and
  line/column navigation.

The independent RRB executable covers 0/1/31/32/33/1,023/1,024/1,025/100,000-element boundaries,
unequal-height concatenation, exact leaf identity through aligned splits and updates, regular versus
relaxed prefix-table invariants, and a 10,000-operation aliasing/model history with retained
snapshots. It also runs 2,000 adversarial boundary splits with explicit density/height bounds,
builder cache/isolation checks, policy lifetime accounting, policy incompatibility, deterministic
allocation-failure sweeps with rollback/leak assertions, and concurrent copy/read/dispose stress.

The independent `fingertree_c.daba_lite` executable validates FIFO order through exhaustive
noncommutative histories and a 100,000-operation variable-window model. It covers all four fixup
phases, exact insert/evict/query combine ceilings of 3/2/1, 64-slot boundaries and sustained churn,
callback-free structural statistics, prompt deterministic ownership reclamation, aligned
type-erased storage, clear/reuse, and every library allocation failpoint in create, boundary growth,
and clear with state/leak rollback assertions. Handle-move coverage verifies populated ownership
transfer, moved-from queries/destruction, continued destination use, and final destruction. The C callback policy is infallible by type; callbacks
must return normally, while injected library allocation failure is fully status-tested.

The independent `fingertree_c.brodal_okasaki_heap` executable covers:

- 4,096-element ascending, descending, and comparer-equivalent heaps; fused primitive/embedded forest
  boundaries; rank/count/depth statistics; representative preservation; empty-side root sharing; and self-meld
  DAG multiplicity;
- exact comparison guards: zero for minimum/visitation, at most five for insert and meld, and at most
  `32 * ceil(log2(n + 1)) + 8` for delete-minimum;
- a 10,000-operation insert/meld/delete-minimum model with retained snapshots, repeated invariant validation,
  and exact minimum representative removal;
- every observed allocator and comparator position for insert, meld, delete-minimum, and array construction;
  every bulk copy position; alias-failure rollback; representative-copy rollback in try-delete; validator/visitor
  allocation failure; and exact copy/destroy/allocation lifetime accounting; and
- concurrent copy/minimum/validate/dispose operations over independently held immutable handles.

The tests distinguish logical occurrences from unique nodes: self-meld may revisit an immutable shared subtree,
but logical count and representative multiplicity still double. The policy/tag/callback contexts remain
caller-owned, callback reentrancy through in-flight handles is unsupported, and concurrent distinct-handle use
requires thread-safe hooks.

The independent `fingertree_c.priority_search_queue` executable covers:

- comparer-equivalent key replacement with first-representative retention; priority comparer/equality
  independence; exact value no-ops; nullable representations; deterministic priority/key minimum order;
  try-add/remove behavior; policy/tag mismatch; and owned removed-entry lifetime across exact queue aliasing;
- AVL rotations and deletion repairs, a 50,000-key ascending stack-safety history, a 4,095-key adversarial
  insertion order, exact root/node sharing diagnostics, absent-remove no-ops, validation, and height bounds;
- inclusive key-range/priority-threshold traversal in key order, winner-cache pruning, and exact comparison
  equations, including the one-key/one-priority impossible-threshold path and logarithmic exact-key descent;
- a 20,000-operation randomized keyed model with 96 retained immutable snapshots and repeated minimum,
  lookup, count, order, and invariant checks;
- every observed allocator, key/priority/value copy, priority/value equality, key/priority comparator,
  visitor, validator, shared-node, point-update, deletion, and array-construction failpoint, including exact
  alias rollback, success-only outputs, a trap proving `key.equals` is never invoked, and exact ownership
  accounting; and
- eight concurrent distinct-handle readers copying, looking up, selecting minima, validating, traversing, and
  disposing snapshots of a shared 10,000-entry queue.

The independent `fingertree_c.canonical_sorted_set` executable covers:

- exact SHA-256/HMAC-based `ZZT2` rank vectors, unsigned secondary ordering, random-policy hidden-key
  separation, keyed-input copying, and public-seed diagnostics;
- bulk versus incremental canonical topology, stable first-representative selection, nullable payloads,
  delete/reinsert convergence, and same-seed shape agreement across distinct policy identities;
- 4,096 fully priority-colliding elements, explicit-stack lookup/update/digest/validation/disposal, and
  10,000 randomized persistent operations checked against a sorted model with retained snapshots;
- union/intersection/difference, exact policy-identity rejection, semantic equality, every proper/nonproper
  subset/superset/overlap relation, same-size/different-type-tag rejection, matching-tag receiver-policy
  asymmetry, exact aliasing, and shared-node diagnostics;
- deterministic allocator, copy, compare, and rank-hash failures across bulk construction, point updates,
  algebra, content hashing, and validation, with output atomicity and exact ownership balance; and
- concurrent independent-handle copy/read/digest/dispose stress, including benign duplicate lazy-digest
  computation and atomic publication.

The callback concurrency contract is part of this coverage boundary: immutable operations through distinct
handles are supported only when user callbacks, allocator hooks, and their contexts are safe for the permitted
parallel calls. Hooks must not reenter an operation in flight through the same policy/set handles, and moving,
disposing, or writing one handle object concurrently remains unsupported.

The sample executables are registered as CTest smoke tests:

- `fingertree_c.sample.showcase` exercises the priority queue, sorted set, interval tree, and text rope.
- `fingertree_c.sample.snapshots` exercises persistent text snapshots and edit/restore behavior.

See the sample [README](../samples/README.md) for direct executable paths and expected transcript markers.

The benchmark executable is built when `FINGERTREE_C_BUILD_BENCHMARKS` is enabled. Run it from the workspace
root after a release build:

```powershell
.\out\build\msvc-release\benchmarks\fingertree_c_benchmarks.exe 10000
```

See the benchmark [README](../benchmarks/README.md) for workload names, output shape, and timing caveats.

Both `msvc-debug` and `msvc-release` are expected to build warning-free under `/W4 /WX`. On hosts with
GCC or Clang available through CMake, the portable presets are also available:

```powershell
cmake --preset ninja-debug
cmake --build --preset ninja-debug --parallel 1
ctest --preset ninja-debug --parallel 1 --output-on-failure

cmake --preset ninja-asan
cmake --build --preset ninja-asan --parallel 1
ctest --preset ninja-asan --parallel 1 --output-on-failure
```

`ninja-asan` enables AddressSanitizer and UndefinedBehaviorSanitizer flags for compilers that support the GCC-style
sanitizer options. Prefer it when changing handle lifetime, copy/dispose paths, or persistent update code.

Windows Clang provides ASan and UBSan but not LeakSanitizer. A Clang/MSVC-ABI instrumented build may use the
same `-fsanitize=address,undefined -fno-omit-frame-pointer` compile flags and
`-fsanitize=address,undefined` executable-link flags shown by the `ninja-asan` preset. Run its tests with
supported options only:

```powershell
$env:ASAN_OPTIONS = "detect_leaks=0:halt_on_error=1:strict_string_checks=1"
$env:UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1"
& "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir out\build\clang-asan --parallel 1 --output-on-failure
```

Using `detect_leaks=1` on this platform terminates each executable before `main` with an unsupported-option
diagnostic; that is an environment limitation, not a test failure. Canonical-set, Brodal-heap, and priority-search
queue leak liveness is independently covered by deterministic allocator-failure sweeps and exact
outstanding-allocation/copy-destroy accounting.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/C/FingerTree> cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset msvc-debug && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure"
```

If a docs-only change only updates links or wording and does not alter commands, C API claims, samples, or
benchmark claims, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
