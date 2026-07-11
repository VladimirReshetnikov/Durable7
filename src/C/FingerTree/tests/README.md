# C FingerTree Tests

- Created (UTC): 2026-07-02T21:10:40Z
- Repository HEAD: 0e776179bac890ed5b792de5134c10bbc0e9d808
- Audience: Maintainers validating the C FingerTree port
- Scope: Native test executable and source organization under `src/C/FingerTree/tests`

The C FingerTree workspace has two dependency-free native test executables. `fingertree_c_tests` is
registered as `fingertree_c.core`; `rrb_vector_c_tests` is registered as
`fingertree_c.rrb_vector`. Each source contains its runner, assertion macros, policy helpers, and
test cases.

The runner prints one `[pass]` line per named test case, writes failed requirements to standard error with file and
line information, and exits non-zero if any test increments the failure count. A successful direct run ends with
`all C FingerTree tests passed`. The focused runner ends with `all C RRB vector tests passed`.

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
- `structural split and locate costs` checks deep-tree split/locate boundaries and enforces logarithmic
  value-copy/measure-combine ceilings over 4,096 elements.
- `sorted set and multiset` covers uniqueness, duplicates, rank access, removal, and traversal.
- `sorted facade structural bounds` covers 4,096-element signpost-guided bounds, comparison ceilings, and
  zero-copy read-only searches under a counting value policy.
- `sorted map` covers insert, set, duplicate rejection, lookup, rank access, traversal, and persistence.
- `rope` covers chunked positional construction, indexing, traversal, split/reconcat, chunk-local editing,
  boundary coalescing, bounded chunk counts, append, and persistence.
- `rope chunk boundaries` covers empty/singleton transitions, exact-maximum chunks, overflow splitting, removal
  re-coalescing, and exact-boundary concat for both positional and measured ropes.
- `measured rope` covers cached and prefix measures, cumulative-measure locate/split, chunk-local editing,
  coalescing, bounded chunk counts, append, and persistence.
- `priority queue` covers minimum-first drain order and FIFO stability through a 128-element equal-priority run.
- `interval tree` covers the signed 64-bit closed-interval facade.
- `generic interval tree` covers caller-supplied endpoint policies, invalid interval rejection, max-high descent,
  shared-annotation lifetime, and comparison-count complexity ceilings.
- `text rope` covers measured construction/editing, indexing, line count, both directions of line/column navigation,
  invalid-column rejection, bounded chunk counts, and traversal.
- `text rope long edit script` covers retained snapshots and repeated edits across a multi-line document, comparing
  indexing, traversal, line counts, and line/column navigation against a plain C string model.

`rrb_vector_tests.c` covers:

- radix boundaries and unequal-height concatenation through 100,000 values;
- exact leaf sharing, root no-op identity, and relaxed-layout diagnostics;
- a 10,000-operation list model with aliasing updates and retained snapshots;
- 2,000 adversarial split/concat rounds with density and height bounds;
- append-builder cached snapshots and adopted immutable prefixes;
- value copy/destroy lifetime balance and policy-pointer compatibility;
- deterministic failpoint allocation rollback for construction, updates, and builder staging; and
- concurrent vector copy/read/validate/dispose over atomic node references.

## Build And Run

From `src/C/FingerTree`, build and run the core CTest target:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure -R ""^fingertree_c\.core$"""
```

Run the built executables directly when changing runner diagnostics or a focused test case:

```powershell
.\out\build\msvc-debug\tests\fingertree_c_tests.exe
.\out\build\msvc-debug\tests\rrb_vector_c_tests.exe
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, warning policy,
generated-output locations, sample smoke tests, and benchmark entry points.
