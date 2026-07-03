# C FingerTree Tests

- Created (UTC): 2026-07-02T21:10:40Z
- Repository HEAD: 0e776179bac890ed5b792de5134c10bbc0e9d808
- Audience: Maintainers validating the C FingerTree port
- Scope: Native test executable and source organization under `src/C/FingerTree/tests`

The C FingerTree workspace currently has one native test executable, `fingertree_c_tests`, registered with CTest as
`fingertree_c.core`. The executable is intentionally dependency-free: `fingertree_c_tests.c` contains the test
runner, assertion macros, policy helpers, and all test cases.

The runner prints one `[pass]` line per named test case, writes failed requirements to standard error with file and
line information, and exits non-zero if any test increments the failure count. A successful direct run ends with
`all C FingerTree tests passed`.

## Test Cases

`fingertree_c_tests.c` registers these cases:

- `concurrent snapshot refcounts` copies, reads, updates, and disposes shared immutable snapshots from multiple
  threads.
- `reversible deque` checks logical reversal, endpoint edits, mixed-orientation concat, split/rejoin,
  set/insert/remove, persistence, and traversal.
- `tree endpoint/index/split/concat` covers the generic tree/deque surface, including indexed replacement.
- `lazy middle force paths` exercises memoized middle publication through reads, pops, split, concat, traversal,
  and disposal.
- `measure locate and split` covers size and custom measure-guided navigation.
- `sorted set and multiset` covers uniqueness, duplicates, rank access, removal, and traversal.
- `sorted map` covers insert, set, duplicate rejection, lookup, rank access, traversal, and persistence.
- `rope` covers chunked positional construction, indexing, traversal, split/reconcat, insertion, removal, append,
  and persistence.
- `measured rope` covers cached measures, cumulative-measure locate/split, editing, append, and persistence.
- `priority queue` covers minimum-first drain order and FIFO stability for equal priorities.
- `interval tree` covers the signed 64-bit closed-interval facade.
- `generic interval tree` covers caller-supplied endpoint policies and invalid interval rejection.
- `text rope` covers construction, editing, indexing, line count, line/column navigation, and traversal.

## Build And Run

From `src/C/FingerTree`, build and run the core CTest target:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure -R ""^fingertree_c\.core$"""
```

Run the built executable directly when changing runner diagnostics or a single-file test case:

```powershell
.\out\build\msvc-debug\tests\fingertree_c_tests.exe
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, warning policy,
generated-output locations, sample smoke tests, and benchmark entry points.
