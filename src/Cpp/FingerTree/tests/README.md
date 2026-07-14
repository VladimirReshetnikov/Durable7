# C++ FingerTree Tests

- Created (UTC): 2026-07-02T21:06:57Z
- Repository HEAD: 399710816b9007dde1374aef2043f118beddc225
- Updated (UTC): 2026-07-14T04:50:00Z
- Updated Repository HEAD: f814076ceba253306517114ff94d30f952af92e6
- Audience: Maintainers validating the C++ FingerTree port
- Scope: Native test executable, source grouping, and stress controls under `src/Cpp/FingerTree/tests`

The C++ FingerTree workspace has one repository-owned native test executable, `fingertree_smoke_tests`; it uses no
third-party test framework. CTest runs that executable through 21 subsystem entries (`fingertree.atomic-box`,
`fingertree.brodal-okasaki-heap`, `fingertree.canonical-sorted-set`, `fingertree.command-model`,
`fingertree.concurrency`, `fingertree.daba-lite`, `fingertree.deque`, `fingertree.interval-tree`, `fingertree.lazy-cell`,
`fingertree.measure`, `fingertree.measured-lazy-cell`, `fingertree.measured-rope`, `fingertree.measured-tree`,
`fingertree.priority-queue`, `fingertree.priority-search-queue`, `fingertree.reversible-deque`, `fingertree.rope`, `fingertree.rope-text`,
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
- `canonical_sorted_set_tests.cpp` pins the exact C# ZZT2 keyed and public-seed vectors, random/key ownership
  modes, bulk/incremental canonical convergence, first representatives, incoherent hashes, a fully colliding
  4,096-node operation chain, allocation-free destruction of a 16,384-node chain, a 20,000-operation retained-snapshot model, identity-gated algebra, receiver-comparer
  asymmetry, quantified structural sharing, cold concurrent digest publication, move-only values, and callback
  exception safety.
- `brodal_okasaki_heap_tests.cpp` covers ascending, descending, equal, and shuffled 8,192-element shapes;
  randomized meld forests; a 15,000-operation retained multiset history; the audited five-comparison insert/meld
  ceiling; comparator-object compatibility; logical self-meld DAGs; exact off-path sharing; tied representatives;
  move-only insertion and representative-returning deletion; logarithmic delete-min comparison/allocation growth;
  injected comparator failures; concurrent readers; and iterative destruction of deep immutable root chains.
- `daba_lite_tests.cpp` exhausts every short insert/evict history with a noncommutative model, runs a deterministic
  100,000-operation variable-window model, covers the 63/64/65 and 127/128/129 block boundaries and long churn,
  reaches all four incremental-fixup phases, proves the three/two/one `combine` ceilings, injects failures at every
  reachable `combine`, identity, and value-copy ordinal, rejects throwing-move values at constraint checking,
  checks boundary-allocation rollback, validates statistics, and observes prompt reference release plus
  clear/reuse behavior.
- `command_sequence_tests.cpp` instantiates the stateful command recorder against the measured tree, tuned deque,
  reversible deque, positional rope, measured rope, and sorted set. Five default seeds exercise retained-version
  branching; failures are replayed and delta-debugged to a deletion-minimal operation program. The same unit
  covers exhaustive sizes 0 through 24, empty sorted-search behavior, and non-group locate/split-find equivalence.
- `sorted_collection_tests.cpp` covers sorted bag, sorted set, and sorted map ranking, canonical stored-reference
  access, forward-range/copy traversal, navigation, range queries, runtime comparator-state normalization,
  persistence-aware set algebra, and randomized model checks.
- `priority_queue_tests.cpp` covers ordering, duplicate priorities, stability, forward traversal/copy, semantic
  dequeue-result equality, and command-model behavior.
- `priority_search_queue_tests.cpp` covers all AVL rotation/deletion paths, 50,000 ascending keys, a retained
  20,000-operation keyed/priority model, first-key and last-priority/payload semantics, exact no-op and result
  handles, custom ties, eager inclusive range pruning with exact comparator equations, optional and move-only
  components, quantified sharing/allocation growth, injected comparator/equality/component failures, and
  concurrent readers.
- `interval_tree_tests.cpp` covers insertion, forward traversal/copy, overlap, containment, streaming coalescing,
  removal, and sweep-model checks.
- `rope_tests.cpp`, `measured_rope_tests.cpp`, and `rope_text_tests.cpp` cover chunked sequence editing, retained
  chunk-aware forward traversal, bounded nonmaterializing copy, same-type insertion regression guards, positional
  cursor endpoints, chunk seams, no-ops, copy-on-move validity, lvalue-only borrowed peeks, retained branches,
  deterministic vector-gap histories, measured
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

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure -L fingertree"
```

Run one subsystem through the same headless CTest launcher:

```powershell
& "$cmakeDir\ctest.exe" --test-dir out\build\msvc-debug --parallel 1 --output-on-failure -R '^fingertree\.command-model$'
& "$cmakeDir\ctest.exe" --test-dir out\build\msvc-debug --parallel 1 --output-on-failure -R '^fingertree\.canonical-sorted-set$'
& "$cmakeDir\ctest.exe" --test-dir out\build\msvc-debug --parallel 1 --output-on-failure -R '^fingertree\.daba-lite$'
```

Run the built executable directly when changing runner output, failure diagnostics, or tests that need local
iteration outside CTest. The executable enters headless mode itself, so direct failures remain non-interactive:

```powershell
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --list
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --group command-model
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --group canonical-sorted-set
.\out\build\msvc-debug\tests\fingertree_smoke_tests.exe --group daba-lite
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
    & "$cmakeDir\ctest.exe" --test-dir out\build\msvc-debug --parallel 1 --output-on-failure -R '^fingertree\.command-model$'
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
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure -R ""^fingertree\.concurrency$"""
Remove-Item Env:\FINGERTREE_STRESS_SECONDS
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, warning policy, generated-output
locations, and benchmark-harness status.
GNU builds intentionally disable allocation-DCE for the test target, because several smoke tests assert observable
global replacement `new`/`delete` side effects.
