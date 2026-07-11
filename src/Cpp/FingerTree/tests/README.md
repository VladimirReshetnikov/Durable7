# C++ FingerTree Tests

- Created (UTC): 2026-07-02T21:06:57Z
- Repository HEAD: 399710816b9007dde1374aef2043f118beddc225
- Updated (UTC): 2026-07-11T16:09:45Z
- Updated Repository HEAD: 66b6821334b243f2d7170a6f9360dae54ef90994
- Audience: Maintainers validating the C++ FingerTree port
- Scope: Native test executable, source grouping, and stress controls under `src/Cpp/FingerTree/tests`

The C++ FingerTree workspace has one dependency-free native test executable, `fingertree_smoke_tests`. CTest runs
that executable through 17 subsystem entries (`fingertree.atomic-box`, `fingertree.command-model`,
`fingertree.concurrency`, `fingertree.deque`, `fingertree.interval-tree`, `fingertree.lazy-cell`,
`fingertree.measure`, `fingertree.measured-lazy-cell`, `fingertree.measured-rope`, `fingertree.measured-tree`,
`fingertree.priority-queue`, `fingertree.reversible-deque`, `fingertree.rope`, `fingertree.rope-text`,
`fingertree.rrb-vector`,
`fingertree.sorted-collections`, and `fingertree.support`). This preserves the small runner in
`test_support/test_runner.hpp` while making CTest failures identify the affected subsystem.

`smoke_tests.cpp` is the executable entry point. It enters repository-wide headless-test mode before constructing
test state, assigns every domain-specific `add_*_tests` function to a runner group, and supports direct filtering,
listing, and replay-seed selection.

## Source Map

- `measure_tests.cpp` covers measure contracts, lower/upper-bound predicates, named min/max/key/order/sum/product
  operations, component projection, and allocation guards.
- `persistent_deque_tests.cpp` covers endpoint updates, indexing, split/concat, sorted search, retained versions,
  forward-iterator concepts/equality/multipass/lifetime/allocation behavior, generic signpost validation, semantic
  result equality, randomized command histories, allocation counters, and operation counters.
- `measured_finger_tree_tests.cpp`, `lazy_cell_tests.cpp`, and `measured_lazy_cell_tests.cpp` cover the measured
  tree core, retained forward streaming and copy behavior, constrained result equality, reference-locate lifetime
  under structural sharing, lazy publication helpers, and computed-cell allocation guards.
- `reversible_deque_tests.cpp` covers reverse orientation, mixed-orientation updates, retained multipass
  forward-iterator logical order, zero-allocation prefix increment over 16,384 values, result equality, random
  histories, and O(1)-reverse allocation checks.
- `rrb_vector_tests.cpp` covers regular-versus-relaxed metadata, boundary sizes through 100,000, persistent point
  and endpoint updates, unequal concatenation, exact leaf-boundary identity reuse, split/range edits, retained
  forward iteration, a deterministic 10,000-step vector model, adversarial density/height drift, append-builder
  snapshot isolation, adopted-prefix sharing, and injected-copy strong-exception guarantees.
- `command_sequence_tests.cpp` instantiates the stateful command recorder against the measured tree, tuned deque,
  reversible deque, positional rope, measured rope, and sorted set. Five default seeds exercise retained-version
  branching; failures are replayed and delta-debugged to a deletion-minimal operation program. The same unit
  covers exhaustive sizes 0 through 24, empty sorted-search behavior, and non-group locate/split-find equivalence.
- `sorted_collection_tests.cpp` covers sorted bag, sorted set, and sorted map ranking, canonical stored-reference
  access, forward-range/copy traversal, navigation, range queries, runtime comparator-state normalization,
  persistence-aware set algebra, and randomized model checks.
- `priority_queue_tests.cpp` covers ordering, duplicate priorities, stability, forward traversal/copy, semantic
  dequeue-result equality, and command-model behavior.
- `interval_tree_tests.cpp` covers insertion, forward traversal/copy, overlap, containment, streaming coalescing,
  removal, and sweep-model checks.
- `rope_tests.cpp`, `measured_rope_tests.cpp`, and `rope_text_tests.cpp` cover chunked sequence editing, retained
  chunk-aware forward traversal, bounded nonmaterializing copy, same-type insertion regression guards, measured
  searches, text interop, line navigation, retained text snapshots, long edit scripts, and randomized
  vector/string-model histories.
- `atomic_box_tests.cpp` and `tearable_concurrency_tests.cpp` cover atomic, data-race-safe publication helpers and
  structure-level tearable-value stress tests. `atomic<shared_ptr>` may serialize internally and is not promised
  lock-free.
- `test_support/` holds the local runner, replay-seed capture, the stateful command recorder and shrinker,
  allocation counting, and operation-count helpers used across the suite. The allocation counter replaces all
  standard throwing, nothrow, and aligned global allocation forms used by the test binary.

## Build And Run

From `src/Cpp/FingerTree`, build and run the Debug CTest target:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure -L fingertree"
```

Run one subsystem through the same headless CTest launcher:

```powershell
& "$cmakeDir\ctest.exe" --test-dir out\build\msvc-debug --output-on-failure -R '^fingertree\.command-model$'
```

Run the built executable directly when changing runner output, failure diagnostics, or tests that need local
iteration outside CTest. The executable enters headless mode itself, so direct failures remain non-interactive:

```powershell
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --list
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --group command-model
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --group rope --filter randomized
```

The runner prints one `[pass]` line per selected test and exits non-zero after any failure. An unknown group,
unmatched filter, malformed seed, or unknown option exits with code 2 instead of silently succeeding.

## Replay And Shrinking

Every `deterministic_rng` construction records and flushes its effective seed before randomized work begins, so
even a native assertion or process fault leaves the seed in captured output. If the surrounding test throws, the
runner repeats the seed in its failure diagnostic together with the two supported replay controls:

```powershell
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --group command-model --seed 0x123456789abcdef

$env:FINGERTREE_REPLAY_SEED = '0x123456789abcdef'
try {
    & "$cmakeDir\ctest.exe" --test-dir out\build\msvc-debug --output-on-failure -R '^fingertree\.command-model$'
}
finally {
    Remove-Item Env:\FINGERTREE_REPLAY_SEED -ErrorAction SilentlyContinue
}
```

`--seed` takes precedence over `FINGERTREE_REPLAY_SEED`; either accepts unsigned decimal or `0x`-prefixed
hexadecimal text. With no override, each command-model family runs five checked-in seeds. A model mismatch or
exception invokes `shrink_failing_sequence`, which repeatedly replays candidate subsequences and reports a
deletion-minimal program whose individual commands can no longer be removed while retaining the failure.

## Stress Controls

`tearable_concurrency_tests.cpp` reads `FINGERTREE_STRESS_SECONDS`. If the variable is unset, stress cases use a
short default suitable for ordinary Debug validation. Raise the value for local soak runs without changing source:

```powershell
$env:FINGERTREE_STRESS_SECONDS = '30'
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure -R ""^fingertree\.concurrency$"""
Remove-Item Env:\FINGERTREE_STRESS_SECONDS
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, warning policy, generated-output
locations, and benchmark-harness status.
GNU builds intentionally disable allocation-DCE for the test target, because several smoke tests assert observable
global replacement `new`/`delete` side effects.
