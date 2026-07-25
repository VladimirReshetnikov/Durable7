# C++ FingerTree Port — Independent Review Report

- Status: Review report (independent audit of the C# → C++ port, outcome and process)
- Created (UTC): 2026-06-30T20:06:08Z
- Repository HEAD: c63af7d8f38d10ebc623cbeee002db21588b30ed
- Audience: Durable7 maintainers and AI agents continuing the C++ port
- Scope: Correctness, concurrency, complexity, API fidelity, test/validation completeness, build, and documentation
  of the `Cpp/FingerTree` port, plus a review of the porting *process* captured in git

## 1. Executive summary

The C++ port under [`Cpp/FingerTree`](../README.md) is a **high-quality, faithful translation of the C#
`Durable7.FingerTree` library**. The implemented code is correct, idiomatic, and — on the most
dangerous part of the design (the lazy-memoized spine and its concurrency contract) — demonstrably sound. Both
the debug and release MSVC configurations build cleanly under `/W4 /WX` and pass the bundled test suite, and every
public header is self-contained. Independent experiments I wrote and ran corroborate every headline behavioral
claim: tear-free concurrent first reads, allocation-free endpoint/measure reads, flat marginal cost under
branching persistence, and O(1) `reverse()`.

The port is **not finished against its own plan**, and that is the substance of this report. The gaps are almost
entirely in the *validation, benchmark, and sample harness* — Milestone 8 is unstarted, and several validation
guards the plan explicitly labels "non-optional" (tree-level tearable concurrency, allocation/complexity guards,
stateful command-sequence shrinking) are absent. There are two genuine **API-fidelity divergences** from the C#
contract (`try_locate` totality; the missing named-operation free-function layer), and a handful of low-severity
documentation/build-hygiene issues.

Crucially: a deep, adversarially-verified review across all components surfaced **no correctness or data-race
defect in the shipped code** — the four deepest review passes (concurrency, reversible deque, sorted collections,
priority queue/interval tree) each found nothing, and that matches my own brute-force differential testing.

**Verdict:** The *code* is production-grade. The *deliverable* is not yet production-complete: the safety net the
plan designed to protect these contracts under future change is largely missing, and a few documented contracts
are silently narrower than C#. None of the gaps block use of the library today; all are addressable without
redesign.

## 2. What was reviewed

- **Scope of the port.** Two engine cores (tuned persistent deque, general measured tree); the measure/predicate
  framework; sorted bag/set/map; priority queue; interval tree; reversible deque; positional + measured ropes;
  newline measure, line helpers, and the char/text builder. ~9,900 lines of headers, ~3,950 lines of tests.
  Milestone 8 (samples + benchmarks) is not present.
- **Process artifacts.** The git history (`c9aef96`…`c63af7d`, ~20 commits), [`port-plan.md`](port-plan.md),
  [`port-plan-editorial-notes.md`](port-plan-editorial-notes.md), [`implementation-notes.md`](implementation-notes.md),
  [`api-notes.md`](api-notes.md), [`validation.md`](validation.md), and the three C# defect/improvement reports the
  port produced.

## 3. Methodology

1. **Read everything.** All five C++ design/notes documents, every public header and the three large `detail/`
   engine headers, the test suite, the CMake/preset files, and the corresponding C# sources and defect reports.
2. **Built and tested both configurations.** Initialized the VS 18 Insiders environment and ran the documented
   `cmake --preset` + `ctest` for `msvc-debug` and `msvc-release`. Both green under `/W4 /WX`.
3. **Independent experiments (the proof).** Wrote four standalone programs that include *only the public aggregate
   header* (so they double as the missing consumer smoke project) and compiled them under `/W4 /WX`:
   - `experiment.cpp` — functional correctness vs. models; `try_split_find`/`try_locate` equivalence across every
     threshold; and the *missing* tree-level tear test: 400 rounds × 12 threads of concurrent **first** reads of
     fresh, never-forced trees of a 4-word struct used as both element and measure, plus a lock-free
     single-producer/multi-consumer publication soak.
   - `probe_alloc.cpp` — allocation counting for endpoint/measure reads and for branching marginal cost.
   - `probe_wrappers.cpp` — brute-force differential tests of interval tree, priority queue, sorted set, and the
     two-level measured-rope navigation.
   - `probe_reversible.cpp` — reversible-deque correctness with interleaved `reverse`, and an O(1)-`reverse`
     allocation guard.
   - A script compiling each of the 18 public headers standalone.
4. **Multi-agent adversarial review.** Ran a 10-dimension review workflow (concurrency, measured tree, deque,
   reversible, measures, sorted, priority/interval, rope, tests/validation, build/docs). Each dimension produced
   structured findings that were then re-verified by an independent skeptical agent reading the exact cited lines.
   Result: 16 findings — 15 CONFIRMED, 1 PLAUSIBLE, 0 REFUTED.

## 4. Process review (the port as captured in git)

The process is **exemplary** and is itself a large part of the deliverable's value.

- **Plan-first, hazard-driven.** Before any code, the author wrote a detailed port plan *and* a companion
  editorial-notes document that reads the C# source line-by-line and enumerates the non-obvious C#→C++ porting
  hazards (the two memoization cells per deep node, boxing/tear-freedom, "a plain `shared_ptr` is not race-safe",
  acquire/release ordering, push-arithmetic vs. pop-force asymmetry, mandatory type erasure for polymorphic
  recursion, the three comparator regimes, `int`→`size_t` signed/unsigned hazards). Each hazard cites C# evidence
  by `file:line`. This is the strongest part of the project: the highest-risk areas were identified and resolved
  *on paper* before implementation.
- **Disciplined, bottom-up checkpoints.** Commits proceed primitives → measures → engine cores → wrappers → ropes,
  each with an `implementation-notes.md` checkpoint recording exactly which C# source/tests were compared, the C++
  choice made, justified divergences, and validation performed. This is auditable and honest.
- **Honest divergence accounting.** Where the C++ differs from C# (`size_t` counts, `std::optional` instead of
  `bool`+out, static comparison policies, pointer-published measures), each is documented with rationale.
- **Genuine findings about the C# original.** The port produced three C# reports — an interval-tree XML-wording
  defect, a measured-tree enumerator-allocation improvement, and a `Rope.FromChunks` ownership improvement. I read
  the cited C# lines: **all three are valid and correctly scoped** (e.g. `IntervalTree.cs:100-101` does literally
  say "matching the underlying strict measured tree," which is misleading for a lazy-memoized core).

The one process weakness: the `implementation-notes.md` checkpoints repeatedly state "Ran `ctest`… all tests
passed," which is true, but the suite never grew the *non-optional* complexity/concurrency/command-shrinking guards
the plan mandates (see §6). The notes describe the tests that exist accurately; they just don't flag that the
plan's required guards were skipped. The provenance `Repository HEAD` SHAs on several docs are stale (they record
creation, not last-content-update — see finding D1).

## 5. Outcome review — correctness, concurrency, and performance

This is where the port is strongest. Both the adversarial review and my independent experiments agree.

### 5.1 Correctness (independently verified)

Every component passed differential testing against a reference model:

| Component | Independent check | Result |
|---|---|---|
| `finger_tree` | vs. vector model; `split`/`try_split_find`/`try_locate` equivalence over every threshold | pass |
| `persistent_deque` | 1,000-op randomized history with retained-snapshot branching vs. `std::deque` | pass |
| `reversible_deque` | 3,000-op history with interleaved `reverse`/`insert`; double-reverse; all 4 concat orientations | pass |
| `sorted_set` | union/intersect/except/symmetric_except/subset/overlaps/set_equals/index_of vs. `std::set` (30 trials) | pass |
| `priority_queue` | drain order + FIFO tie-break (element-ordinal check); `meld`; max-queue via `reverse_comparison` | pass |
| `interval_tree` | overlap/count/single-overlap vs. brute force; `coalesce` vs. sweep; duplicate-remove-one | pass |
| `measured_rope` | `prefix_measure` at every offset; `try_locate_by_measure` (exact boundary element + measure-before) | pass |
| `rope<char>` | 3,000 inserts + 1,500 deletes vs. `std::string` | pass |

The adversarial review's four deep correctness/concurrency dimensions returned **zero findings**, independently
confirming the algorithms.

### 5.2 Concurrency / tear-freedom (independently verified)

The plan's highest-risk area. The primitives (`lazy_cell`, `atomic_box`, `measured_lazy_cell`) correctly use
`std::atomic<std::shared_ptr<…>>` for every concurrently published-and-read cell, publish measures as
fully-constructed pointers (never `std::atomic<measure_type>` of a wide type, never a racy `mutable` field), drop
the pending op + captured source on CAS publication (no `call_once` retention), and retry-after-exception. The
reversible core is confirmed **strict** — no lazy/atomic machinery crept in.

My tree-level tear experiment — the test the suite is *missing* — ran 400 rounds × 12 threads of concurrent first
reads of fresh, never-forced trees using a 4-word struct (`is_intact()` detector) as both element and measure:
**0 tears, 0 wrong measures.** A lock-free single-producer/multi-consumer publication soak over an atomic
`shared_ptr`: **0 tears.**

> Caveat: this toolchain has no ThreadSanitizer (MSVC only; no clang). "No tearing observed" is strong empirical
> evidence on x86-64 plus a correct-by-construction atomic design, but it is not a formal proof of race-freedom.
> A TSan run on a clang/libc++ CI would close this.

### 5.3 Performance contracts (independently measured)

| Claim | Measurement | Result |
|---|---|---|
| Endpoint/`measure` reads don't re-force or allocate | 200k `front`/`back`/`measure` reads on forced `finger_tree`/deque | **0 allocations** |
| Amortized-under-branching (size-independent marginal cost) | one `push_back` off one retained version, n=10³ vs n=10⁵ | **4.00 allocs/op at both sizes** (flat) |
| O(1) `reverse()` (shares children, no deep copy) | 1,000 `reverse()` calls, n=10³ vs n=10⁵ | **3.00 allocs/op at both sizes** (flat) |
| Public headers are self-contained | compile all 18 standalone under `/W4 /WX` | **0 failures** |

These are exactly the properties the plan's "non-optional" complexity guards were supposed to assert. The behavior
is correct **today**; what is missing is the regression net (see D-class findings).

## 6. Findings

Severity reflects post-verification adjudication (H = wrong result/race/contract break; M = real fidelity or
completeness gap; L = hygiene/precision). No finding is a correctness defect in shipped code.

### A. API fidelity

**A1 [M] `try_locate` / `try_locate_by_measure` are optional-wrapped, not total — the miss-path whole-tree
`measure_before` is discarded.**
`finger_tree::try_locate` ([measured_finger_tree.hpp:171](../include/durable7/finger_tree/measured_finger_tree.hpp)) returns `std::optional<finger_tree_locate_result>` and yields
`std::nullopt` on a miss, where C# `TryLocate` (`FingerTree.cs:332-346`) returns `false` but still sets
`measureBefore = _root.Measure`. The port plan calls this out *by name* as the single exception to the
Try→`optional` mapping and mandates a total result `{ measure_type measure_before; std::optional<T> found; }`
([port-plan.md:255-259, 418-423](port-plan.md)). The same applies to `measured_rope::try_locate_by_measure`
([measured_rope.hpp:357](../include/durable7/finger_tree/measured_rope.hpp)). *Mitigation:* in-repo
count-measure consumers compensate (`sorted_bag.hpp:206` returns `size()` on a miss), so no current behavior is
wrong — but a general external caller using a non-count measure cannot read the full accumulated measure on a miss
and must call `measure()` redundantly. The contract is silently narrower than C# and the plan. *Fix:* adopt the
total result shape the plan prescribes; extend the equivalence test to assert miss-path `measure_before ==
measure()`.

**A2 [M] The named-operation free-function layer is entirely missing.**
The plan designates the free-function layer over the raw primitives — `try_peek_max`/`try_extract_max`/min,
`split_by_lower_bound`/`split_by_upper_bound`, `split_at_index`, `split_by_cumulative_weight`/
`try_select_by_cumulative_weight`, and the product-component `split_by_first`/`second` / `find_by_*` — as
"headline API, not optional sugar" ([port-plan.md:452-457](port-plan.md)), to be co-located in
`built_in_measures.hpp` / `product_measure.hpp` / `sum_measure.hpp`. None exist (a recursive grep returns zero
hits). The predicate building blocks are all present, and the functionality is reachable through the collection
wrappers (`priority_queue`, `sorted_set::operator[]`, etc.), so this is a missing *surface*, not missing
capability. *Fix:* add the wrappers as the plan specifies.

### B. Test / validation completeness (the main gap)

**B1 [H] No tree-level tearable concurrent-first-read test.** Threading appears only in the primitive
`atomic_box_tests.cpp` / `lazy_cell_tests.cpp`; there is no `Tearable` analogue wired through `finger_tree` or a
`rope`/`measured_rope` (plan Milestone 3 + 7; the C# `TearableConcurrencyStressTests.cs` is the model). This is
the *only* test that would exercise the deep-node two-cell publication through the real tree. *I verified the
behavior is correct (§5.2), but there is no in-suite guard.*

**B2 [H] No complexity guards for either core.** No allocation-flatness / endpoint-no-force / size-independent
marginal-cost guard exists; the general tree has no comparer-call-count guard. The plan labels these
"non-optional" for both M3 and M4 ([port-plan.md:794-796, 828-830](port-plan.md)); the C# ships
`FingerTreeDequeComplexityGuardTests.cs` / `AllocationFreeReadTests.cs`. The test-support infrastructure
(`allocation_counting_scope`, `operation_counter`) exists but is used only in two trivial spots. *I verified the
properties hold (§5.3), but they are unguarded against regression.*

**B3 [M] No stateful command-sequence model test with shrinking.** `command_model.hpp` defines a
`command_sequence` recorder, but it is **never instantiated** and there is no shrink-to-minimal-failing-program
logic. The randomized tests are fixed-seed data histories with per-step invariant checks — good, but not the
"most powerful tier" the plan commits to for deque/general-tree/sorted-set/rope/measured-rope.

**B4 [M] No benchmarks (Milestone 8).** No `benchmarks/` dir, no Google Benchmark dependency, no targets. The
plan's highest-value empirical guard — the persistence/branching-flatness benchmark at 100/10k/1M — is absent (I
substituted a manual measurement; see §5.3).

**B5 [M] No samples (Milestone 8).** No `showcase.cpp` / `persistent_snapshots.cpp` (the Tour) and no
`run(std::ostream&)` smoke seam, so the headline undo/redo + lock-free-snapshot story has no end-to-end demo or
rot guard.

**B6 [L] No consumer smoke project / no install/export.** The `INTERFACE` target is never `install()`/`export()`ed
and nothing compiles against only the public headers as an external consumer. *I verified all 18 public headers
compile standalone and a public-header-only consumer builds, so no leak exists today*, but the
`$<INSTALL_INTERFACE:include>` path is unexercised.

**B7 [L] Single CTest test; no replay seed printed.** The whole suite is one executable registered as
`fingertree.smoke`, so CTest gives one aggregate result (an `abort` kills the whole binary) and there is no
per-test filtering. Randomized tests use fixed compile-time seeds (so failures *are* reproducible by re-running),
but `validation.md:29`'s "must print a replay seed on failure" is literally unmet.

**B8 [L] `try_locate`/`try_split_find` equivalence test covers only `size_measure`.** The plan requires the
equivalence on a non-group order-statistic/key measure too; the C++ test only exercises the count measure (the
non-group `count_last_key_measure` tree tests `split` but not the distinct `locate` path).

### C. Documentation / build hygiene

- **D1 [L] Stale provenance SHAs.** `api-notes.md`, `README.md`, `docs/README.md`, and `validation.md` pin
  `Repository HEAD: bdc938f` (the plan-expansion commit); `implementation-notes.md` pins `d140fb0` (the bare
  skeleton). All document APIs/checkpoints added *after* those commits. The CLAUDE.md convention has no
  "Updated HEAD" field, so the create-time SHA is misread as the content's state. *Fix:* bump to the
  last-content-update commit, or add an `Updated (UTC)/HEAD` line.
- **D2 [L] `validation.md` scope overstates content.** Header says "build, test, **stress, and benchmark**
  commands"; the body has none. *Fix:* narrow the scope line or note the deferral, mirroring how `README.md`
  hedges `samples/`/`benchmarks/`.
- **D3 [L] `vcpkg.json` is inert.** Empty `dependencies`, no toolchain wiring, no `find_package`. Documented as
  future work, but a contributor could assume `--preset` runs a manifest install. *Fix:* add a placeholder note or
  defer the manifest until the first real dependency lands.
- **D4 [L] Presets hardcode the VS 18 Insiders absolute Ninja/CMake path.** `cmake --preset` fails on any other
  install even with a valid Ninja on PATH. Consistent with the documented local-environment assumption, but a
  portability footgun for a second machine or CI. *Fix:* let CMake discover Ninja on PATH, or move the path to a
  non-committed `CMakeUserPresets.json`.

## 7. Recommendations (prioritized)

1. **Close the two API-fidelity gaps (A1, A2)** — these change the public surface, so do them before declaring API
   stability. Make `try_locate`/`try_locate_by_measure` total per the plan, and add the named-operation
   free-function layer.
2. **Land the "non-optional" guards the plan mandates (B1, B2)** — the tree-level tearable test and the
   allocation/complexity guards. The behaviors already pass (§5); these convert verified-once into
   protected-forever. The experiments in this review's scratch are a ready starting point.
3. **Complete Milestone 8 (B4, B5)** — benchmarks (especially persistence branching-flatness) and the two samples
   with smoke seams.
4. **Add the command-sequence shrinking tier (B3)** and the consumer smoke project with install/export (B6).
5. **Tidy hygiene (B7, B8, D1–D4)** — per-test CTest registration or seed printing; broaden the locate equivalence
   test; refresh provenance SHAs; fix the `validation.md` scope line, the inert `vcpkg.json`, and the hardcoded
   preset path.
6. **(Optional, high value) Run the suite + experiments under clang/libc++ ThreadSanitizer** in CI to upgrade the
   concurrency evidence from "no tearing observed" to "race-free under TSan."

## 8. Bottom line

The author set out to do "substantial, production-ready work," and on the dimension that matters most — a correct,
concurrency-safe, performant translation of a genuinely hard persistent data structure — they succeeded. The
hazard analysis is the best part: the riskiest C#→C++ pitfalls were found and solved before coding, and the
implementation honors them. My independent testing found no correctness or concurrency defect, and confirmed the
headline performance and tear-freedom claims by direct measurement.

What keeps this from being "done" is completeness against the port's own plan: the validation/benchmark/sample
safety net (Milestone 8 and several non-optional Milestone 3–7 guards) is largely missing, and two documented
contracts (`try_locate` totality; the named-op layer) are narrower than C#. These are additive, well-understood,
and require no redesign. Address §7 items 1–3 and the port reaches the production-complete bar its plan sets.

---

### Appendix: verification artifacts

- Build: `msvc-debug` and `msvc-release` both configure/build/test green under `/W4 /WX` (debug suite ~8.5s,
  release ~1.0s; one CTest test `fingertree.smoke`).
- Independent experiments (consumer-style, public headers only): `experiment.cpp`, `probe_alloc.cpp`,
  `probe_wrappers.cpp`, `probe_reversible.cpp`, plus a per-header standalone-compile sweep — all pass.
- Adversarial multi-agent review: 16 findings, 15 CONFIRMED / 1 PLAUSIBLE / 0 REFUTED; the four deep
  correctness/concurrency dimensions returned zero findings.

## Resolution Addendum (2026-07-10)

This report remains an accurate historical snapshot; its actionable gaps are now resolved in the remediation
worktree. Locate is total on misses, named measure operations and structural result equality are present, and the
collection family has forward streaming traversal/copy paths. The native suite now has 16 headless subsystem CTest
entries, replay seeds printed before randomized work and on caught failure, six-family stateful command programs
with five default seeds and deletion-minimal shrinking, non-group locate/split equivalence, exhaustive small-size
coverage, complexity guards, and structure-level tearable concurrency tests.

Milestone 8 now ships deterministic `showcase` and persistent text-snapshot samples with captured-output smoke
tests plus a dependency-free benchmark harness. The short harness covers all planned families; retained branching
measured exactly 4.00 allocations / 472 bytes per operation at sizes 100, 10,000, and 1,000,000. Install/export
rules provide the architecture-independent `durable7::finger_tree` CMake target, version metadata,
public headers, and MIT-0 license. The headless installed-consumer test requires the staged package, disables
registries and repository developer targets, and rejects source-include leakage.

The inert vcpkg manifest and machine-specific preset path are gone. Focused CI covers MSVC/GCC/Clang
Debug/Release, Clang static analysis of the aggregate consumer, separate ASan+UBSan and TSan jobs, all CTests, and
Release branching probes. Fresh local MSVC Debug and Release runs each passed all 18 CTests; the Release build was
clean under `/W4 /WX`. Snapshot publication is now described precisely as atomic and data-race-safe—not guaranteed
lock-free, because `atomic<shared_ptr>` may serialize internally.
