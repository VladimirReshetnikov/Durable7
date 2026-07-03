# C++ FingerTree Port Review

- Created (UTC): 2026-06-30T20:14:15Z
- Repository HEAD: c63af7d8f38d10ebc623cbeee002db21588b30ed
- Branch: `codex/review-cpp-port-main`
- Reviewer: Codex
- Scope: Outcome and git-process review of the C# `FingerTree` data-structure library port to `src/Cpp/FingerTree`

## Executive Summary

The C++ port is a serious and technically ambitious first implementation, not a superficial wrapper. The core
design choices are generally aligned with the C# architecture: the tuned deque and the general measured tree are
separate; the measured core uses type erasure rather than impossible recursive templates; lazy middle suspension
and lazy deep-node measure publication are represented with atomic `shared_ptr` cells; the rope chunk ownership
model is safer than the C# `ReadOnlyMemory<T>` import surface; and the current native debug/release builds pass.

That said, I would not yet call the C++ port production-grade. The current implementation is better described as a
large, promising, well-documented first wave. The main blockers are not a single obvious memory corruption bug.
They are contract drift and insufficient evidence: a public `try_locate` contract called out as load-bearing in the
port plan is implemented with the wrong result shape; public record-like result structs lack structural equality;
named measured-tree convenience operations are missing; most collection families expose materialization rather
than streaming iteration; samples, benchmarks, install/export packaging, static analysis, and CI are absent; and
the native validation is much thinner than the C# library's validation tier.

The porting process captured in git is unusually transparent and valuable. It has planning commits, an editorial
hazard review, detailed implementation-note updates, and validation claims in commit bodies. The process weakness
is that about 16.8k lines of C++/docs/tests landed in a very compressed sequence, with major data-structure cores
added minutes apart, and without the validation depth that the plan itself says is required before the port is
usable.

## Methodology

I reviewed both the declared intent and the code outcome.

Documentation read:

- `AGENTS.md` and repository `README.md`.
- `docs/README.md` and `docs/guides/agent-workflows.md`.
- `src/CSharp/docs/FingerTree/overview.md` and `src/CSharp/docs/FingerTree/README.md`.
- `src/CSharp/docs/FingerTree/api-specification.md`, with targeted checks against C# source and tests.
- `src/Cpp/FingerTree/README.md`.
- `src/Cpp/FingerTree/docs/README.md`.
- `src/Cpp/FingerTree/docs/port-plan.md`.
- `src/Cpp/FingerTree/docs/port-plan-editorial-notes.md`.
- `src/Cpp/FingerTree/docs/implementation-notes.md`.
- `src/Cpp/FingerTree/docs/api-notes.md`.
- `src/Cpp/FingerTree/docs/validation.md`.

Source reviewed:

- C++ public headers under `src/Cpp/FingerTree/include/tools/data_structures/finger_tree`.
- C++ internal headers for `detail::lazy_cell`, `detail::atomic_box`, `detail::measured_lazy_cell`,
  `detail::measured_tree`, `detail::deque_tree`, `detail::reversible_tree`, and rope chunks.
- C++ tests under `src/Cpp/FingerTree/tests`.
- C# source and tests for interval-tree semantics, measured-tree locate behavior, rope/text behavior, sorted
  collections, priority queues, and the validation strategy.

Git-process review:

- Reconstructed the C++ path history with `git log -- src/Cpp/FingerTree`.
- Read commit bodies for the C++ planning and implementation commits.
- Compared total diff/stat shape from the extraction commit to the current head.
- Checked whether the documentation and commit history reflect the current implemented surface.

Experiments and validation:

- Ran the documented C++ debug configure/build/test after initializing a Visual Studio developer environment.
- Ran the documented C++ release configure/build/test.
- Repeated the release native test executable 20 times with CTest's `--repeat until-fail:20`.
- Ran the C# baseline `dotnet test .\DataStructures.sln`.
- Compiled and ran a small out-of-tree C++ consumer using only the public aggregate header and `src/Cpp/FingerTree/include`.

## Validation Results

### C++ Native Validation

Initial direct configure failed in a plain PowerShell shell:

```powershell
& "...\cmake.exe" --preset msvc-debug
```

The failure was not project code. CMake found `cl.exe` but could not link the compiler probe because `rc` and `mt`
were unavailable:

```text
RC Pass 1: command "rc ... manifest.rc" failed ... no such file or directory
CMAKE_MT-NOTFOUND
```

After running through `VsDevCmd.bat`, the documented presets passed:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug --output-on-failure
```

Result:

```text
1/1 Test #1: fingertree.smoke .................   Passed   32.94 sec
100% tests passed, 0 tests failed out of 1
```

Release passed as well:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release --output-on-failure
```

Result:

```text
1/1 Test #1: fingertree.smoke .................   Passed    2.15 sec
100% tests passed, 0 tests failed out of 1
```

The native test executable reports:

```text
109 test(s) passed
```

The release suite also passed 20 repeated CTest runs:

```text
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 11.19 sec
```

### C# Baseline Validation

The C# baseline passed:

```powershell
dotnet test .\DataStructures.sln
```

Result:

```text
Passed! - Failed: 0, Passed: 346, Skipped: 0, Total: 346, Duration: 27 s
```

### Public Header Consumer Smoke

An out-of-tree consumer using:

```cpp
#include <tools/data_structures/finger_tree/finger_tree.hpp>
```

compiled and ran with only:

```text
/I src/Cpp/FingerTree/include
```

It exercised `persistent_deque`, `sorted_set`, `sorted_map`, `priority_queue`, and text-rope helpers. This is a
good sign for basic public include hygiene, although it is not a substitute for an install/export package test.

## Git Process Findings

The C++ work is captured as 18 path-relevant commits:

| Commit | Time | Subject |
| --- | --- | --- |
| `85afac7` | 2026-06-29 19:56 -0700 | Plan C++ finger tree port |
| `c9aef96` | 2026-06-29 21:00 -0700 | Refine C++ finger-tree port plan; add editorial notes |
| `bdc938f` | 2026-06-30 10:01 -0700 | Expand C++ finger tree port plan |
| `d140fb0` | 2026-06-30 10:15 -0700 | Add C++ FingerTree workspace skeleton |
| `af687e0` | 2026-06-30 10:24 -0700 | Add C++ measure infrastructure |
| `d62d9dc` | 2026-06-30 10:27 -0700 | Add atomic lazy-cell primitive |
| `2c49852` | 2026-06-30 10:31 -0700 | Add atomic measure-box primitive |
| `372cfd9` | 2026-06-30 10:34 -0700 | Add measured lazy-cell primitive |
| `b35dff6` | 2026-06-30 10:55 -0700 | Add C++ persistent deque core |
| `7881221` | 2026-06-30 11:04 -0700 | Add C++ general measured finger tree core |
| `f90486d` | 2026-06-30 11:08 -0700 | Add C++ priority queue wrapper |
| `6d7756a` | 2026-06-30 11:21 -0700 | Route MSVC test assertions to stderr |
| `a62b050` | 2026-06-30 11:29 -0700 | Add C++ interval tree wrapper |
| `a038cf5` | 2026-06-30 11:38 -0700 | Add C++ sorted collection wrappers |
| `85732ea` | 2026-06-30 11:49 -0700 | Add C++ reversible deque |
| `2e351a3` | 2026-06-30 12:00 -0700 | Add C++ positional rope |
| `50c3617` | 2026-06-30 12:08 -0700 | Add C++ measured rope |
| `21e2b01` | 2026-06-30 12:12 -0700 | Add C++ rope text helpers |

The net C++ path diff from the standalone extraction point is:

```text
57 files changed, 16818 insertions(+)
```

Positive process observations:

- The plan existed before the implementation and was refined against C# source before code landed.
- `port-plan-editorial-notes.md` is unusually good. It identifies the genuinely dangerous areas: C++ memory model,
  atomic `shared_ptr` publication, measure boxing, type erasure, comparator regimes, reversible strictness, rope
  ownership, and `int` to `size_t` hazards.
- Commit bodies explain C# files/tests compared, intentional divergences, and validation performed.
- The implementation-note document acts as a useful audit log and links to C# defect/improvement reports found
  during porting.
- The separate commit for routing MSVC assertions to stderr is a good automation-hardening response to native
  debug behavior.

Process risks:

- The implementation pace is extremely compressed. For example, the persistent deque core commit reports 3,153
  inserted lines at 10:55, and the general measured core reports 1,897 more lines at 11:04. The sorted collections,
  reversible deque, ropes, measured ropes, and text helpers then land within about 68 more minutes. That cadence is
  not itself a defect, but for this algorithmic surface it demands unusually strong independent validation, which
  is not yet present.
- The history is mostly checkpoint commits with self-reported validation, not a visible review/CI process.
- The implementation notes are thorough but partly aspirational: they mention test scaffolds for command models,
  allocation counters, and operation counters, yet the current tests use only small slices of that machinery.
- The plan says what is required before the port is treated as usable, but the history does not show those gates
  being completed: benchmarks, sample smoke tests, actual tree-level concurrency stress, static analysis, and a
  consumer install/export test are absent.

## Outcome Findings

### Strengths

1. The port is structurally real.

   `persistent_deque` and `finger_tree` are backed by native finger-tree engines, not by a vector compatibility
   layer. The internal files are large and complex because they are actually carrying the intended representation.

2. The hardest C# -> C++ memory-model hazard was taken seriously.

   `detail::lazy_cell<T>`, `detail::atomic_box<T>`, and `detail::measured_lazy_cell<Tree>` use atomic
   `shared_ptr` publication. This is the right shape for the C# `Volatile.Read` / `Interlocked.CompareExchange`
   reference-publication pattern. The code also avoids `std::call_once`, which the plan correctly rejects for
   captured-source lifetime reasons.

3. The tuned deque and general measured tree remain separate.

   This preserves the C# architecture and avoids flattening the tuned deque into a general measured-tree wrapper.

4. The rope chunk ownership boundary is improved.

   C++ zero-copy `from_chunks` accepts `shared_ptr<const vector<T>>`, making ownership and immutability visible in
   the type, unlike the more ambiguous C# `ReadOnlyMemory<T>` import.

5. The current tests are stable and pass under debug and release.

   This is useful baseline evidence. The native test suite is not flaky under a 20-repeat release CTest probe.

### P1. `finger_tree::try_locate` Violates The Planned Public Contract

Evidence:

- `src/Cpp/FingerTree/docs/port-plan.md:256-259` says `try_locate` is the exception to optional-shaped `Try` results:
  on a miss it must still return the whole-tree `measure_before`.
- `src/Cpp/FingerTree/docs/port-plan.md:418-423` repeats this with a total `locate_result` whose boundary element is
  optional.
- `src/Cpp/FingerTree/docs/port-plan.md:291` lists total `try_locate(predicate) -> locate_result<T, measure_type>` in
  the public surface checklist.
- `src/Cpp/FingerTree/include/tools/data_structures/finger_tree/measured_finger_tree.hpp:169-178` instead implements:

```cpp
std::optional<finger_tree_locate_result<Element, MeasurePolicy>> try_locate(Predicate predicate) const
{
    if (root_.is_empty() || !std::invoke(predicate, root_.measure())) {
        return std::nullopt;
    }

    auto located = root_.locate_tree(predicate, MeasurePolicy::empty());
    return finger_tree_locate_result<Element, MeasurePolicy>{std::move(located.measure_before), located.hit.value()};
}
```

Why it matters:

- The plan called this out because C# `TryLocate` reports `measureBefore` even when no boundary element is found.
  For rank/count queries, that miss-path measure is the answer.
- Some wrappers manually paper over this by returning `size()` when `try_locate` returns `nullopt`
  (`sorted_set.hpp:299-304`, `sorted_map.hpp:329-334`), but the raw tree API is still wrong and less expressive
  than the C# contract.
- This also means a test that only checks found boundaries can pass while a user loses the no-boundary measure.

Recommendation:

- Change `finger_tree_locate_result` to carry `measure_before` and `std::optional<Element> item`.
- Make `finger_tree::try_locate` total, returning `MeasurePolicy::empty()`/`nullopt` for an empty tree and
  `root_.measure()`/`nullopt` when the predicate never flips.
- Add tests for empty, miss, single, and every threshold boundary, including the equivalence with
  `try_split_find` demanded by `port-plan.md:791-794`.
- Revisit wrappers that currently compensate for `nullopt`; they should use the total result.

### P1. The Validation Suite Is Not Yet Production-Grade For This Data Structure

Evidence:

- C++ native suite: one CTest target, 109 local-runner tests.
- C# baseline suite: 346 xUnit tests, including property/model/stress categories.
- `src/Cpp/FingerTree/docs/port-plan.md:797-804` requires tearable concurrent-first-read and stateful command-sequence
  tests for the measured tree.
- `src/Cpp/FingerTree/docs/port-plan.md:825-828` requires command-sequence and complexity guards for the deque.
- `src/Cpp/FingerTree/docs/port-plan.md:851-854` requires persistence and allocation/operation-count guards for
  derived collections.
- `src/Cpp/FingerTree/docs/port-plan.md:871-872` requires randomized reversible histories plus command sequences and
  O(1) reverse guards.
- `src/Cpp/FingerTree/docs/port-plan.md:896-899` requires measured-rope command tests and tearable concurrent-read
  tests.
- Current test search found actual threading tests only for `lazy_cell` and `atomic_box`, not for published
  `finger_tree`, `persistent_deque`, `rope`, or `measured_rope` instances.
- Allocation and operation counters exist, but current usage is minimal: one product-predicate allocation check,
  one sorted-search comparison-count check, and smoke tests for the counters themselves.

Why it matters:

- The central C# quality claim is not just functional correctness. It is persistent amortized complexity under
  branching histories, zero-allocation hot reads, and safe concurrent reads of lazy memoized state.
- Those claims are exactly where straightforward unit tests are least likely to catch defects.
- Atomic publication primitives passing their own tests does not prove the whole tree maintains the one-operation
  suspension discipline or tear-free publication under real tree operations.

Recommendation:

- Port the C# validation tiers before calling this production-ready:
  - tree-level tearable multi-word element/measure stress;
  - concurrent first `measure`, `front`, `back`, locate, rope navigation, and publication stress;
  - endpoint no-force allocation guards;
  - branching flatness / marginal allocation guards;
  - `try_locate` vs `try_split_find` at every threshold;
  - command-sequence model tests with replayable seeds and shrinking or at least minimized replay logs;
  - allocation-free hot-read tests for sorted/priority/interval wrappers;
  - O(1) reversible `reverse()` allocation/node-touch guard.
- Split tests into multiple CTest targets so failures identify the subsystem before reading local-runner output.
- Keep the local runner if desired, but add Catch2 or another mature test framework once vcpkg is introduced.

### P2. Public Result Structs Lack Structural Equality

Evidence:

- `src/Cpp/FingerTree/docs/port-plan.md:264-267` requires value/result carriers to default `operator==`, while warning
  not to default equality on containers.
- `operator==` exists for measure carriers such as `optional_measure`, `ranked_key`, `priority_entry`, `interval`,
  and `measure_pair`.
- It does not exist for public result structs:
  - `finger_tree_split` and `finger_tree_item_split` in `measured_finger_tree.hpp:25-36`;
  - `deque_split`, `deque_item_split`, `deque_range_split`, and `deque_pop` in `persistent_deque.hpp:25-49`;
  - `priority_queue_dequeue` in `priority_queue.hpp:23-29`;
  - `rope_split` in `rope.hpp:22-27`;
  - `measured_rope_split` and `measured_rope_locate_result` in `measured_rope.hpp:24-37`;
  - reversible result structs in `reversible_deque.hpp:215-225`.

Why it matters:

- The C# result carriers are `record struct`s. Structural equality is part of their normal ergonomics.
- Native tests and users must hand-compare fields, which is friction and a contract drift.
- This is easy to fix now and harder after users start depending on ad hoc result comparison behavior.

Recommendation:

- Add defaulted `operator==` to pure value result structs whose fields are themselves equality comparable.
- Do not add `operator==` to containers unless it is a deliberate sequence/value equality operation, not pointer
  equality.

### P2. Named Measured-Tree Convenience Operations Are Missing

Evidence:

- `src/Cpp/FingerTree/docs/port-plan.md:432-434` says named operations should remain the safe public path.
- `src/Cpp/FingerTree/docs/port-plan.md:453-459` explicitly names the C# extension families to port:
  peek/extract max/min, lower/upper-bound split, split-at-index, product-component splits/finds, and cumulative
  weight split/select.
- The C++ implementation has measure policies and predicate types, but `sum_measure.hpp` only defines
  `sum_measure`, `product_measure.hpp` only defines `measure_pair` and `product_measure`, and
  `measure_predicates.hpp` exposes low-level predicates. There is no comparable free-function layer.

Why it matters:

- The raw measured tree is powerful but easy to misuse. The C# API's named operations are what make common
  structures discoverable and less error-prone.
- Without them, users have to assemble predicates and split semantics manually, including the strict `>` vs
  non-strict `>=` details.
- The absence also reduces parity with the C# headline examples: weighted selection, product component lookup,
  order-statistic split, and max/min extraction.

Recommendation:

- Add free functions or constrained customization-point objects for the named operations, with tests ported from
  `FingerTreeMeasureExtensions`, `FingerTreeProductExtensions`, and `FingerTreeSumExtensions`.
- Ensure the functions use the corrected total `try_locate` where appropriate.

### P2. Iteration And Materialization Are Not At The Planned Production Surface

Evidence:

- `src/Cpp/FingerTree/docs/port-plan.md:363-371` says to use forward iterators first and make `persistent_deque`
  genuinely lazy and allocation-free after construction.
- `src/Cpp/FingerTree/docs/port-plan.md:986-988` resolves iterator direction similarly.
- `persistent_deque::const_iterator` declares `std::input_iterator_tag` at
  `persistent_deque.hpp:483-489`, not a forward iterator.
- `finger_tree::copy_to` materializes a vector first (`measured_finger_tree.hpp:188-195`).
- `rope`, `measured_rope`, `priority_queue`, `sorted_bag`, `sorted_set`, `sorted_map`, `interval_tree`, and
  `reversible_deque` primarily expose `to_vector()` rather than streaming `begin`/`end`.
- Rope and measured-rope `copy_to` use `slice(...).tree_.to_vector()` before copying chunks
  (`rope.hpp:333-345`, `measured_rope.hpp:388-400`).

Why it matters:

- A persistent data-structure library is often used precisely to avoid full materialization in read paths.
- C++ users expect standard range integration when an API presents itself as a collection.
- The C# library is not perfect here either, but the port plan explicitly identifies streaming iterators as part
  of the C++ direction, especially for the tuned deque and ropes.

Recommendation:

- Upgrade `persistent_deque::const_iterator` to meet the forward iterator requirements if its implementation is
  semantically multipass, or document it as input-only and update the plan/API notes.
- Add lazy chunk/tree iterators for ropes and measured ropes.
- Add at least materializing iterators for wrappers as an interim step, clearly documented, then improve to
  streaming traversal where complexity matters.
- Make `copy_to` for ropes traverse chunks without allocating a vector of chunks.

### P2. Samples, Benchmarks, And Stress Entrypoints Are Missing

Evidence:

- `src/Cpp/FingerTree/docs/port-plan.md:27-35` says the target is a stable header-first C++ library with tests,
  samples, and benchmarks, and that the first complete port preserves the Tour sample's core story.
- `src/Cpp/FingerTree/docs/port-plan.md:904-915` lists the intended C++ samples.
- `src/Cpp/FingerTree/docs/port-plan.md:917-930` lists the benchmark families.
- `src/Cpp/FingerTree/README.md:38` says `samples/` and `benchmarks/` are reserved for later milestones.
- The actual C++ workspace directories are `docs`, `include`, `out`, and `tests`; no `samples` or `benchmarks`
  directory exists.

Why it matters:

- The C# project validates runnable samples in tests. That is part of the user-facing quality story.
- Benchmarks are not optional ornamentation here. They are evidence for the library's key claims: catenation,
  branching persistence, reversible reverse, weighted selection, and rope line navigation.
- Without benchmarks, there is no native proof that the C++ type erasure and `shared_ptr` design has acceptable
  constant factors.

Recommendation:

- Add deterministic `showcase.cpp` and `persistent_snapshots.cpp` samples with `run(std::ostream&)` seams and
  CTest smoke checks.
- Add a Google Benchmark harness or a temporary local harness if vcpkg is not ready.
- Prioritize persistence/branching flatness, reversible reverse, priority-queue meld, rope editing, measured-rope
  line navigation, and interval overlap queries.

### P2. Build/Packaging Is Still A Workspace, Not A Production Library

Evidence:

- `src/Cpp/FingerTree/CMakeLists.txt` defines an interface library and test target but no `install()`, export set,
  package config, version config, or installed-header validation.
- The out-of-tree consumer compiles only by manually passing `/I src/Cpp/FingerTree/include`.
- No CI configuration is present.
- No non-MSVC compiler configuration is validated in this repository.

Why it matters:

- A header-first library still needs an integration story: installed includes, exported CMake target, package
  versioning, and a consumer smoke target.
- Header-only template libraries are especially vulnerable to accidental reliance on private include paths or
  transitive includes.

Recommendation:

- Add install/export rules for `tools::data_structures::finger_tree`.
- Add a tiny consumer CMake project or CTest that uses only the exported target.
- Add CI for MSVC at minimum, and clang-cl or clang/gcc if portability is a goal.

### P3. The Document Status And Build Instructions Need Tightening

Evidence:

- `src/Cpp/FingerTree/README.md:3` says "Initial C++ workspace".
- `src/Cpp/FingerTree/docs/validation.md:3` says "Initial validation guide" while its scope says stress and benchmark
  commands (`validation.md:7`), but it contains only debug/release build/test commands and a policy paragraph.
- The README build section says to initialize a Visual Studio developer environment if `cl.exe` is not already on
  `PATH`. In this worktree, `cl.exe` was on `PATH`, but configure still failed until `VsDevCmd.bat` supplied
  `rc.exe`/`mt.exe`.

Why it matters:

- The docs are high quality, but their status vocabulary blurs "initial workspace", "first complete port", and
  "production-ready library".
- A maintainer following the literal README can hit a configure failure despite having `cl.exe`.

Recommendation:

- State that the CMake presets require a full Visual Studio developer environment, not merely `cl.exe`.
- Add a helper script such as `build-msvc.ps1` that calls `VsDevCmd.bat` and then runs configure/build/test.
- Update validation docs to say stress, benchmark, sample, static-analysis, and install-consumer checks are not
  implemented yet.
- Update status fields to distinguish "implemented first-wave code" from "production-validated".

### P3. Test Granularity Is Too Coarse

Evidence:

- `src/Cpp/FingerTree/tests/CMakeLists.txt` builds all tests into `fingertree_smoke_tests` and registers one CTest
  test: `fingertree.smoke`.

Why it matters:

- A single executable is convenient at bootstrap, but it gives poor CI signal once the codebase is large.
- Native failures, especially debug CRT assertions, are easier to triage when subsystem tests are separate CTest
  cases.

Recommendation:

- Split into subsystem executables or one executable with CTest cases by filter.
- Keep a top-level aggregate target for local convenience.

## Recommendations By Priority

1. Fix the raw `finger_tree::try_locate` result shape and add miss-path tests.
2. Add the missing validation gates from the port plan: tree-level concurrency/tearable stress, command-model
   tests, allocation/operation complexity guards, and threshold-equivalence tests.
3. Add structural equality to public value/result carriers.
4. Add the named measured-tree operation layer.
5. Add samples and benchmarks, with CTest smoke coverage for samples.
6. Improve iteration and `copy_to` surfaces so common reads do not materialize whole vectors unnecessarily.
7. Add install/export packaging and an out-of-tree CMake consumer test.
8. Split the native test suite into more granular CTest entries.
9. Tighten build docs around `VsDevCmd.bat`, `rc.exe`, and `mt.exe`.

## Bottom Line

The port has excellent bones. The algorithmic intent is understood, the hardest C++ memory-model problem was not
hand-waved, and the current code compiles and passes its native smoke suite. But the current outcome should be
treated as an impressive first wave, not as production-grade parity with the C# library. The path to production is
clear: close the public contract drift, port the missing named operations, and make the validation match the
claims the data structures are built to make.
