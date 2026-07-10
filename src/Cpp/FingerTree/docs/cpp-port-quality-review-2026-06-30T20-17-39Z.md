# C++ FingerTree Port — Quality Review

- Status: Completed review
- Created (UTC): 2026-06-30T20:17:39Z
- Repository HEAD: c63af7d8f38d10ebc623cbeee002db21588b30ed
- Audience: Vladimir Reshetnikov and future maintainers/agents of `src/Cpp/FingerTree`
- Scope: Independent quality review of the C# → C++ port — process, code, tests, build, and documentation
- Related: a second, fully independent review (different session, same task, concurrent) landed on
  `main` as
  [`cpp-port-review-report-2026-06-30T20-06-08Z.md`](cpp-port-review-report-2026-06-30T20-06-08Z.md).
  The two converge strikingly on the top three substantive findings (see "Cross-check against a
  second independent review" at the end of this document) despite using different methodologies and
  having no shared context — treat that convergence as the strongest evidence in this review that
  those three findings are real and worth fixing first.

## Summary

The C++ port is, on the whole, a careful and largely faithful translation of the C# FingerTree
library, built on top of unusually rigorous up-front planning. The two hardest engineering
problems in the port — eliminating C#'s polymorphic recursion via type erasure, and replacing
the CLR's atomic-reference guarantees with explicit `std::atomic<std::shared_ptr<T>>` publication
— were both identified in writing *before* implementation and were then implemented correctly. I
verified the highest-risk code (the lazy-cell/atomic-box primitives, and their wiring into the
measured tree, deque, and reversible-tree cores) by direct reading and by compiling and running my
own multithreaded stress and exception-safety probes against the public headers; nothing I tried
broke it.

Three real, concrete gaps survived adversarial review, all in the "high" tier (not critical, not
correctness-threatening to existing tests, but real):

1. **`finger_tree::try_locate` (and `measured_rope::try_locate_by_measure`) silently discard the
   whole-tree measure on a miss**, contradicting an explicit, twice-written project mandate.
2. **A large, explicitly-promised public API surface — the C# named-operation convenience layer
   (`TryPeekMax`/`TryExtractMax`/`SplitByLowerBound`/`SplitAtIndex`/`SplitByCumulativeWeight`,
   etc., ~700+ lines of C# source) — was never ported**, despite the project's own docs calling it
   "headline API, not optional sugar."
3. **The structure-level concurrent-publication safety claim — the single most important safety
   property of the whole port — is currently untested.** Real-thread stress exists only for the
   two low-level primitives (`atomic_box`, `lazy_cell`); nothing forces the lazy spine of an
   actual `finger_tree`, `persistent_deque`, `rope`, or `measured_rope` under concurrent load, and
   the `FINGERTREE_STRESS_SECONDS`-style duration-bounded test the project's own validation policy
   requires does not exist anywhere.

Below that tier sits a long but individually minor tail: a few documentation/test-density gaps, one
arithmetic-helper inconsistency, a backwards-named private helper duplicated three times, and a
consistent pattern of thinner randomized/persistence-stress coverage than the C# suite. No memory
corruption, no crash, and no tearing was found by either static reading or live stress testing.

## Methodology

This review combined four independent techniques so that no single method's blind spots dominate
the conclusions:

1. **Provenance reading.** Read the full git history of the port (16 commits), the four-document
   planning trail (`port-plan.md`, `port-plan-editorial-notes.md`, `implementation-notes.md`,
   `api-notes.md`), and the four C#-side defect/improvement reports the porting process produced
   along the way, before touching the C++ code. This established what the port *claims* to do and
   *why*, including a pre-registered list of the specific hazards (type erasure, dual atomic
   memoization cells, three distinct comparator regimes, signed→unsigned migration, rope
   chunk-granularity navigation) that the implementation needed to get right.
2. **Independent build and test execution.** Configured and built both the MSVC Debug and Release
   presets from a clean state (`cmake --preset`, `cmake --build --preset`,
   `ctest --preset --output-on-failure`) myself, rather than trusting the docs' claim. Both
   presets built clean under `/W4 /WX` and all 109 named test cases passed in both configurations.
3. **An 8-dimension multi-agent structural review with adversarial verification.** Partitioned the
   ~9,900-line C++ header tree plus its ~3,500-line test suite into eight review dimensions
   (concurrency/memory-model, general measured-tree core, tuned deque core, reversible-deque core,
   derived collections, rope family, a cross-cutting `size_t`/API-hygiene sweep, and
   build/test/doc fidelity). Each dimension's reviewer read every C++ file in its scope in full
   side-by-side with the corresponding original C# source, then every individual finding was
   handed to a second, independent agent instructed to actively try to *refute* it — re-reading the
   cited lines, checking for a missed guard or mitigating context — before being accepted. This
   surfaced 3 outright refutations and several severity corrections (both up and down) that a
   single-pass review would have missed: e.g. one finding's claim that a naming defect also
   appeared in `rope.hpp`/`measured_rope.hpp` was checked and found false and was excised even
   though the core finding survived; a build-hygiene finding about missing CMake `install()`
   rules was downgraded after the verifier found the gap explicitly disclosed as deferred work.
4. **Direct hands-on experimentation.** Independently of the agent review, I personally read the
   most safety-critical files (`detail/atomic_box.hpp`, `detail/lazy_cell.hpp`,
   `detail/reversible_tree.hpp`, `detail/rope_chunk.hpp`) end to end, and compiled and ran four
   standalone probe programs directly against the public headers (no test framework, just `cl.exe`
   against the installed MSVC toolchain) to empirically stress the properties that are hardest to
   verify by reading alone:
   - **Concurrent first-force stress**: built fresh, never-forced `finger_tree<long long, tearable_measure>`
     and `persistent_deque<long long>` instances (a 4×`int64` "tearable" measure type modeled on
     the C# stress suite's own fixture) and had 8–48 real OS threads race to force the lazy
     spine for the first time simultaneously, repeated across dozens of trials and thousands of
     elements, both in a plain optimized build and under AddressSanitizer.
   - **Exception safety**: induced a predicate to throw mid-`split()` and confirmed the source
     tree was completely unaffected and remained normally usable afterward.
   - **API-contract check**: confirmed by direct compilation that none of the container types
     (`persistent_deque`, `finger_tree`) expose `operator==` (a `requires`-expression negative
     compile check), matching the project's "containers must not default-compare by pointer"
     rule.
   - **Source reading of the rope chunk-ownership/tear-free path** to independently confirm
     `compact()`, `set_at`, `insert_at`, `remove_at`, and `concat` all allocate a fresh backing
     vector rather than ever mutating shared storage in place.

   All four probes passed cleanly across every run (zero failures, zero ASan reports). This is
   strong empirical corroboration but is **not a substitute for genuine ThreadSanitizer coverage**:
   x86-64's relatively strong memory model can mask pure reordering bugs that would manifest on
   ARM or under TSan instrumentation, and the local toolchain (MSVC 19.50 Insiders) does not have a
   TSan-equivalent available. Treat the empirical concurrency result as "no crash or tearing
   observed under heavy real-thread + ASan stress," not "formally proven race-free."

The full per-finding evidence trail (file:line citations, verbatim code quotes, and the
verification agent's independent re-reading notes) is preserved in this review's working
transcript; this document reports the conclusions and the most load-bearing evidence.

## Findings

Severity reflects the verifier's corrected assessment, not the first-pass reviewer's. "High"
here means a real, confirmed defect with public-API or safety-validation impact — none of the
findings below are crashes, data corruption, or build breaks; both Debug and Release builds pass
all 109 tests as shipped.

### High severity

**H1 — `finger_tree::try_locate` silently discards `measure_before` on every miss; `measured_rope::try_locate_by_measure` repeats the same defect.**
[`measured_finger_tree.hpp:169-179`](../include/tools/data_structures/finger_tree/measured_finger_tree.hpp:169),
[`measured_rope.hpp:357-363`](../include/tools/data_structures/finger_tree/measured_rope.hpp:357)

```cpp
[[nodiscard]] std::optional<finger_tree_locate_result<Element, MeasurePolicy>> try_locate(Predicate predicate) const
{
    if (root_.is_empty() || !std::invoke(predicate, root_.measure())) {
        return std::nullopt;   // <-- the whole-tree measure just computed is thrown away
    }
    ...
```

The internal engine type (`detail::measured_tree::locate_tree`'s result, and the public
`finger_tree_locate_result` struct) is *already* shaped correctly as a total result
(`measure_before` + element). The only defect is the public facade wrapping that already-correct
struct in an outer `std::optional`, which throws away `measure_before` on a miss. This is exactly
the anti-pattern the project's own design docs name and warn against:

> "**Exception:** `TryLocate` sets `measureBefore` to the **whole-tree measure** on the false
> path — a meaningful value... A bare `std::optional<locate_result>` would discard it. Use a
> *total* result... Do not wrap the whole thing in `std::optional`." —
> [`port-plan-editorial-notes.md:350-358`](port-plan-editorial-notes.md), restated at
> [`port-plan.md:418-423`](port-plan.md)

The C# original (`FingerTree.cs:332-346`) sets `measureBefore = _root.Measure` on the false path —
a meaningful value (e.g. the full element count for a rank query) — never a discarded default. A
caller who relies on `try_locate` to recover "how far past the end" or "the full count when
nothing matched" gets nothing back from the C++ API today; they would need to call `.measure()`
separately, which works but silently diverges from the documented/intended contract and from the
C# behavior. The existing test suite does not catch this: the only test comparing `try_locate`'s
`measure_before` against a model only checks it `if (split.has_value())`, never exercising the
miss path. *Fix*: change `try_locate`'s return type to a total `locate_result<T, M>{ M
measure_before; std::optional<T> found; }` exactly as the docs already specify, and propagate the
same fix to `measured_rope::try_locate_by_measure`.

**H2 — The C# "named-operation" convenience layer has no C++ counterpart at all.**
[`built_in_measures.hpp`](../include/tools/data_structures/finger_tree/built_in_measures.hpp),
[`product_measure.hpp`](../include/tools/data_structures/finger_tree/product_measure.hpp),
[`sum_measure.hpp`](../include/tools/data_structures/finger_tree/sum_measure.hpp)

These three headers (plus `measure_predicates.hpp`) contain only the monoid-policy struct
definitions (`max_measure`, `order_statistic_measure`, `priority_measure`, `interval_measure`,
`product_measure`, `sum_measure`, etc.) and predicate functor types. A repository-wide search for
anything resembling `TryPeekMax`/`TryExtractMax`/`TryPeekMin`/`TryExtractMin`,
`SplitByLowerBound`/`SplitByUpperBound`, `SplitAtIndex`, the product-projecting
`SplitByFirst`/`SplitBySecond` family, `SplitByCumulativeWeight`, or `TrySelectByCumulativeWeight`
returns nothing in `include/`. On the C# side, `FingerTreeMeasureExtensions.cs` (345 lines) and
`FingerTreeProductExtensions.cs` (390 lines) define exactly this layer as the library's actual
public ergonomics over the raw `split`/`try_split_find`/`try_locate` primitives (`SplitByCumulativeWeight`
and `TrySelectByCumulativeWeight` live in `SumMeasure.cs`, not a separate file — a minor correction
to file-naming, the substance is unaffected). The project's own docs are unambiguous that this was
required, not optional:

> "The named-operation convenience layer has no home... These map to a free-function layer
> co-located in `built_in_measures.hpp` / `product_measure.hpp` / `sum_measure.hpp` and need a
> milestone deliverable, **or they will silently be dropped**." —
> [`port-plan-editorial-notes.md`](port-plan-editorial-notes.md)
>
> "...they are **headline API, not optional sugar**." — [`port-plan.md:452-457`](port-plan.md)

That predicted outcome is exactly what happened: there is no milestone checkpoint in
`implementation-notes.md` for this layer, and it does not exist. The functional gap is partially
mitigated in practice — `priority_queue`, `sorted_bag/set/map`, and `interval_tree` each implement
their *own* member-function equivalents of the relevant slice of this layer (peek/extract,
floor/ceiling, rank), so a user who only needs those derived collections is not blocked. But the
generic, reusable, compose-with-any-measure layer the C# library actually ships — e.g. wrapping an
arbitrary `finger_tree<T, MaxMeasure<T>>` with `TryExtractMax` without writing a custom collection
— has no C++ equivalent today. *Fix*: port the layer as free functions as originally planned, or
explicitly amend the docs to mark it descoped if the decision is to not pursue it.

**H3 — No duration-based, structure-level concurrency stress test exists; the port's central safety claim is currently untested above the primitive level.**
[`tests/atomic_box_tests.cpp`](../tests/atomic_box_tests.cpp), [`tests/lazy_cell_tests.cpp`](../tests/lazy_cell_tests.cpp) (the only thread-using test files)

A search of the entire C++ test suite for `std::thread`/`std::jthread`/`std::async` returns hits
in exactly two files: `atomic_box_tests.cpp` and `lazy_cell_tests.cpp`. Both are genuinely good
tests of the *primitives* — `lazy_cell_tests.cpp` spins 16 real threads racing `cell.get()` on a
bare `lazy_cell<int>` and confirms convergence, and `atomic_box_tests.cpp` does the analogous
thing with a wide tear-detecting value. But nothing in the suite forces the lazy spine of an
actual `finger_tree`/`measured_tree`, `persistent_deque`, `rope`, or `measured_rope` concurrently.
The project's own validation policy treats this as a hard pre-usability gate, not a nice-to-have:

> "Run concurrency tests under a duration environment variable similar to
> `FINGERTREE_STRESS_SECONDS`." — [`port-plan.md:953`](port-plan.md), under "Before treating the
> C++ port as usable"

`FINGERTREE_STRESS_SECONDS` (or any duration-bounded stress knob) appears nowhere in the C++
source tree — only in the doc that requires it. The C# original's
`TearableConcurrencyStressTests.cs` (264 lines) is explicitly named as the model to follow
(editorial notes §10, tagged "H-adjacent... the only test that exercises §1's atomic-measure-publication
and tear-freedom invariants") and runs four `Parallel.For`/`Task.Run`-based soaks directly against
`Rope<Tearable>`/`MeasuredRope<Tearable,...>` — concurrent first-reads of a freshly-built tree,
lock-free single-producer/multi-consumer publication, and a branching-and-reading soak off a
retained base. None of the four has a C++ analogue.

I partially closed this gap empirically during this review (see Methodology — concurrent
first-force stress on `finger_tree`/`persistent_deque` with a tearable measure, clean under both a
plain build and AddressSanitizer across dozens of trials), which gives real evidence the design is
sound in practice. But a one-off review probe is not a regression-guarded, checked-in,
duration-scalable test, and it didn't cover `rope`/`measured_rope` (Milestone 7's own validation
list specifically calls for "a tearable concurrent-read test over a `rope`/`measured_rope` of a
multi-word struct," which remains undone) or the lock-free-publication and branching-soak shapes.
*Fix*: port `TearableConcurrencyStressTests.cs`'s four scenarios against the actual C++ structures,
honoring a duration env var, as both Milestone 3 and Milestone 7's validation sections require.

### Medium severity

**M1 — The measured-tree core engine and its public facade carry zero documentation comments.**
[`detail/measured_tree.hpp`](../include/tools/data_structures/finger_tree/detail/measured_tree.hpp) (1,198 lines),
[`measured_finger_tree.hpp`](../include/tools/data_structures/finger_tree/measured_finger_tree.hpp)

Undocumented code is a repository-wide pattern across this port (none of the 23 publisrc/C/detail
headers carry Doxygen-style contract comments), but these two files are the most load-bearing
for a reader to get right without re-deriving it from the C# source: the asymmetric
amortized-vs-worst-case complexity of `measure()` (O(1) amortized, never worst-case, because a pop
suspension must force) is exactly the kind of subtlety the project's own docs flag as a recurring
historical failure mode on the C# side; the C# API specification was amended after an adjudicated defect
found over-claimed worst-case bounds in a complexity table. The C++ implementation gets the *behavior* right — I confirmed
`measure()` routes through `atomic_box::get_or_compute` (amortized) while `front()`/`back()` read
the prefix/suffix digits directly without ever touching the lazy middle (worst-case O(1)) — but
nothing in the header states this contract for a future maintainer or consumer to rely on. The
project's own Documentation Policy is explicit that this matters: "Public headers should document
contracts, persistence, complexity, exception behavior, and iterator invalidation semantics" and
"Avoid promising hard worst-case endpoint updates... where the implementation only proves
persistent amortized bounds" (`port-plan.md`, Documentation Policy section). *Fix*: add contract
comments to the public surface of `finger_tree`/`measured_finger_tree.hpp` at minimum, covering
complexity, persistence, and exception-safety per the project's own stated policy.

**M2 — Milestone 8 (samples and benchmarks) was never started.**
[`port-plan.md:902-935`](port-plan.md)

No `samples/` or `benchmarks/` directory exists under `src/Cpp/FingerTree/` (both are listed as
present-but-empty placeholders in the workspace `README.md`, and the workspace layout in
`port-plan.md` describes five planned benchmark files including "persistence/branching-flatness...
the highest-value benchmark for the riskiest part of the port"). `implementation-notes.md`'s
checkpoint log simply stops after the rope/text checkpoints with no Milestone 8 entry — not even a
"deferred" note. This is honestly disclosed in the workspace README ("reserved for the later
milestones"), so it is not a misrepresentation, but it does mean the single benchmark the project's
own plan calls the most important empirical validation of the whole lazy-suspension design has
never been run against this implementation. *Fix*: at minimum, add an explicit tracking note in
`implementation-notes.md` that Milestone 8 is not started, and prioritize the persistence/
branching-flatness benchmark given it directly tests the highest-risk subsystem.

**M3 — `implementation-notes.md`'s later checkpoints state "all tests passed" without flagging the still-missing tearable-concurrency requirement.**
[`implementation-notes.md:126-217, 419-428, 797-806, 868-876`](implementation-notes.md)

The "Atomic Lazy Cell" and "Atomic Measure Box" checkpoints correctly cite the C# tearable-stress
file as comparison material and describe what they actually validated (primitive-level concurrent
convergence). But the later "General Measured Finger Tree Core," "Positional Rope," and "Measured
Rope" checkpoints' Validation sections list only single-threaded scenarios and say "all tests
passed" with no caveat that the structure-level concurrency requirement from the editorial notes
(§10, explicitly tagged H-adjacent) remains open. This is a documentation-accuracy issue, not a
runtime bug — but it means a reader of `implementation-notes.md` alone, without independently
grepping the test suite as this review did, would not learn that H3 above is still outstanding.
*Fix*: add an explicit "Remaining work" or "Known gap" line to those three checkpoints pointing at
the missing structure-level concurrency test.

### Low severity (selected — full list below)

A consistent secondary pattern across the deque, reversible-deque, and rope test suites is that
randomized/persistence/regression-guard coverage is real but measurably thinner than the C#
suite's: a single fixed random seed where C# uses five, no exhaustive small-N invariant sweep
where C# has dedicated ones for sizes 0–24, no allocation-count regression guard for
`reversible_deque::reverse()`'s O(1) claim (the property itself was independently confirmed
correct by code reading — `mirror()` shares child handles and never deep-copies — but there is no
checked-in guard against a future regression), and no test exercising sorted search or invariant
validation on a genuinely empty `persistent_deque`. Individually each of these is low severity;
collectively they suggest the C++ suite, while genuinely solid (109 dense, often
internally-looping test cases), has not yet reached the C# suite's edge-case density.

Two more specific, easily-fixed items:

- **`validate_and_count`'s rightmost-leaf signpost check is compiled out via `if constexpr` for any
  element type without `operator==`**
  ([`detail/deque_tree.hpp:588-606`](../include/tools/data_structures/finger_tree/detail/deque_tree.hpp:588)),
  silently weakening the deque's own diagnostic invariant check relative to the C# original's
  unconditional `EqualityComparer<T>.Default.Equals`. Confined to the debug-only validation path,
  not production operations.
- **One isolated arithmetic-helper inconsistency**: `sorted_set::merge()`
  ([`sorted_set.hpp:315`](../include/tools/data_structures/finger_tree/sorted_set.hpp:315)) computes
  `left.size() + right.size()` directly instead of via the `checked_add` helper used essentially
  everywhere else in the port (e.g. the directly analogous `rope_chunk::concat()` does use it).
  Practical overflow risk is nil (the addends are already-validated container sizes), but it is
  the sole exception to an otherwise consistent and good convention.

### Informational (confirmed strengths worth recording)

These were independently verified — both by the review agents and by me directly — and represent
the port doing the hard parts correctly:

- **Type erasure is genuinely correct and non-recursive.** Every level's child is
  `std::shared_ptr<const measured_node<Element, MeasurePolicy>>` — a single fixed type regardless
  of tree depth, with no `node<node<node<...>>>` instantiation anywhere. This is the one change
  that was *mandatory for compilation* (C# polymorphic recursion has no direct C++ analogue), and
  it was done correctly.
- **The dual atomic-memoization-cell requirement (the project's own designated highest-risk area)
  was implemented correctly.** `atomic_box`, `lazy_cell`, and `measured_lazy_cell` all use
  `std::atomic<std::shared_ptr<T>>` (never a racily-reassigned plain `shared_ptr`), use the
  implicit `seq_cst` default everywhere (a `memory_order` grep across the entire `include/` tree
  returns zero hits — no relaxed-ordering "optimization" was attempted), and a throwing factory
  always leaves the cell pending for retry because the factory call unconditionally precedes the
  only publish point in all three primitives. `deep_measured_tree_rep` correctly wires up *both*
  required cells (a `measured_lazy_cell` for the middle suspension and a separate `atomic_box` for
  the combined measure); `deep_deque_tree_rep` correctly has *no* measure box at all, just a plain
  immutable `cached_size`, matching the size-is-invertible/measure-is-not distinction the editorial
  notes single out. I independently reproduced this conclusion by reading the primitives end to
  end and by running my own concurrent-first-force stress probes (see Methodology) against the
  finished structures with zero observed tearing or corruption, including under AddressSanitizer.
- **The reversible-deque core is correctly strict.** `detail/reversible_tree.hpp` has zero
  references to `lazy_cell`/`measured_lazy_cell`/`atomic_box`/`std::atomic` anywhere — confirmed
  independently by me via direct grep — and `rev_node::mirror()` shares child handles (`shared_ptr`)
  rather than deep-copying them, which is what makes O(1) `reverse()` actually true rather than
  aspirational.
- **The rope chunk tear-free invariant holds.** `rope_chunk::set_at`/`insert_at`/`remove_at`/
  `concat` all allocate a fresh backing `std::vector` and never mutate `storage_` in place;
  `storage()` only ever exposes a `const` reference. I confirmed this independently by reading
  `detail/rope_chunk.hpp` end to end.
- **The general measured-tree's `split`/`locate` accumulator order, node-grouping/chunking rule,
  and the cons/snoc deferred-suspension measure-probe arithmetic are line-for-line structural
  matches to the C# original.**
- **The three distinct comparator regimes the editorial notes warn about (order-independent
  measure + runtime predicate comparer for sorted collections; comparator-baked-into-the-monoid
  compile-time policy for priority queue/interval tree; static comparison policy family) are kept
  cleanly separate**, with `priority_measure`/`interval_measure` correctly preventing a `meld`/
  `concat` between differently-configured instances via the type system rather than convention.
- **Exception safety holds at the public API level**, confirmed by my own probe: a predicate that
  throws mid-`split()` leaves the source tree completely unaffected and normally reusable
  afterward.
- **Container types correctly omit `operator==`** (confirmed by direct negative-compile-check
  probe), avoiding the C# port's explicitly-flagged pointer-comparison hazard.
- **The deque's sorted-search signpost optimization, its `checked_add`/`checked_difference`
  overflow discipline, and its `sorted_binary_search` bitwise-complement encoding were all
  independently confirmed correct and genuinely tested**, not just present.

## A naming defect I found independently (not flagged by the agent review)

While reading the public facade headers directly, I noticed `measured_finger_tree.hpp`,
`persistent_deque.hpp`, and `reversible_deque.hpp` each independently define their own private
helper named `throw_if_not_empty()` —

```cpp
void throw_if_not_empty() const
{
    if (empty()) {
        throw std::logic_error("finger_tree is empty");
    }
}
```

— which actually throws when the container **is** empty, the exact opposite of what the name says.
The behavior is correct everywhere it's used (guarding `front()`/`back()`/`min()`/`max()`
preconditions); this is purely a misleading, backwards name, duplicated three times instead of
unified into one shared helper. It is doubly confirmed as an inconsistency by the fact that
`sorted_set.hpp` independently implements the identical check with the *correct* name,
`throw_if_empty()`. *Fix*: rename to `throw_if_empty` in the three files and consider hoisting it
into `detail/common.hpp` alongside the existing `throw_if_index_out_of_range` family, removing the
duplication.

## What I deliberately did not chase further

- **True data-race detection.** ASan and heavy real-thread stress testing are good but not
  equivalent to ThreadSanitizer for memory-ordering-class bugs; no TSan-capable toolchain was
  available locally (MSVC, not clang). Given the design review found correct `seq_cst` usage
  throughout with no relaxed-ordering shortcuts, I judge the residual risk here low, but it is
  exactly the class of risk that empirical testing alone cannot fully retire — see H3 above.
- **Performance/complexity validation.** No benchmarks exist (M2), so this review could not
  empirically confirm the claimed O(log n)/amortized-O(1) bounds beyond what static reading
  supports through my own probes. (The second independent review described below went further here
  — see the cross-check section for its allocation-counting measurements.)
- **Exhaustive enumeration of every low-severity test-coverage gap.** The review surfaced roughly
  16 low-severity findings, almost all in the shape "C# has N more test scenarios/seeds/sizes than
  C++ for the same already-correctly-implemented behavior." I've summarized the pattern and the
  two most actionable individual items above rather than listing all 16 verbatim; the underlying
  multi-agent review transcript has the complete per-finding citations if useful as a follow-up
  worklist.

## Recommendations, in priority order

1. Fix H1 (`try_locate`'s discarded `measure_before`) — small, mechanical, and directly contradicts
   a project mandate that's written down twice.
2. Decide on H2 (the named-operation layer): either commit to porting it, or explicitly amend
   `port-plan.md`/`api-notes.md` to mark it descoped so the gap stops being silent.
3. Close H3 by porting the C# `TearableConcurrencyStressTests.cs` scenarios against the real
   `finger_tree`/`persistent_deque`/`rope`/`measured_rope` structures with a duration knob — this
   is the validation for the riskiest part of the whole port and is currently the single largest
   gap between "passes all tests" and "the project's own definition of usable."
4. Add contract/complexity doc comments to at least `finger_tree.hpp`/`measured_finger_tree.hpp`
   (M1) — cheap, and this exact category of gap (overclaiming or under-documenting complexity
   bounds) has already bitten the C# sibling project once.
5. Rename `throw_if_not_empty` → `throw_if_empty` and centralize it in `detail/common.hpp`.
6. Treat Milestone 8 (samples/benchmarks) as the next major chunk of work once the above are
   closed — the persistence/branching-flatness benchmark in particular is the empirical
   counterpart to H3's correctness validation.

None of the above blocks using the library today for its currently-tested surface; they are gaps
between "good, working, well-architected port" and "the port plan's own definition of done."

## Cross-check against a second independent review

While finishing this review I discovered that a second, fully independent agent session (different
methodology, no shared context, running concurrently against the same repository state — HEAD
`c63af7d`) had just landed its own report directly on `main`:
[`cpp-port-review-report-2026-06-30T20-06-08Z.md`](cpp-port-review-report-2026-06-30T20-06-08Z.md).
That review used a 10-dimension multi-agent adversarial pass (vs. this review's 8) plus more
extensive hands-on quantitative experiments — most notably allocation-counting proof that
branching-persistence marginal cost is genuinely flat (4.00 allocs/op at both n=10³ and n=10⁵) and
that `reverse()` is genuinely O(1) in allocations (3.00 allocs/op at both sizes), a 400-round ×
12-thread tree-level tear test, and a standalone-compilation sweep of all 18 public headers.

The convergence between the two independent reviews is the strongest evidence in this whole
exercise: **both reviews, working independently, identified the same three top findings** —
`try_locate`/`try_locate_by_measure` discarding `measure_before` on a miss, the entirely-missing
named-operation free-function layer, and the absent structure-level (tree/rope-level) tearable
concurrency test — and both independently concluded the shipped code has no correctness or
data-race defect. The other review additionally surfaced several lower-severity items this review
did not separately call out (no complexity/allocation-flatness regression guards checked into the
test suite despite the test-support infrastructure existing for it; `command_model.hpp`'s
`command_sequence` recorder is defined but never instantiated, so the "most powerful" stateful
command-sequence-with-shrinking test tier the plan commits to does not exist; stale provenance
`Repository HEAD` stamps on several docs; an inert `vcpkg.json`; hardcoded absolute toolchain paths
in `CMakePresets.json`). None of those contradict anything in this review; they are additive and
worth folding into the same fix list, particularly the complexity/allocation-flatness guards, which
pair naturally with this review's H3 (both are "the behavior is correct, but nothing protects it
from regressing").

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
rules provide the architecture-independent `tools::data_structures::finger_tree` CMake target, version metadata,
public headers, and MIT-0 license. The headless installed-consumer test requires the staged package, disables
registries and repository developer targets, and rejects source-include leakage.

The inert vcpkg manifest and machine-specific preset path are gone. Focused CI covers MSVC/GCC/Clang
Debug/Release, Clang static analysis of the aggregate consumer, separate ASan+UBSan and TSan jobs, all CTests, and
Release branching probes. Fresh local MSVC Debug and Release runs each passed all 18 CTests; the Release build was
clean under `/W4 /WX`. Snapshot publication is now described precisely as atomic and data-race-safe—not guaranteed
lock-free, because `atomic<shared_ptr>` may serialize internally.
