# Complexity Parity Campaign — Retrospective — 2026-08-06

- Created (UTC): 2026-08-06
- Repository HEAD (written at): 4e59684
- Audience: Maintainers wanting the story, evidence, and transferable lessons of the campaign; the
  [census](complexity-parity-census-2026-08-05.md) remains the row-by-row work order
- Scope: How every asymptotic-complexity divergence across the nine language workspaces was found,
  fixed or ruled on, and verified — and what the exercise taught about keeping a multi-language
  library honest

## How It Started

While explaining why the freshly landed Python port of the research-derived collections stated
different bounds than its siblings, a routine substrate comparison surfaced something worse than a
porting divergence: Rust's `measured.rs` called itself "the measured 2-3 finger tree" while its
node type was a plain height-balanced join tree, and its docs claimed O(1) `front()`/`back()` that
the code answered with a full spine recursion. The maintainer elevated the response from a doc fix
to a governing principle: **every language uses the same algorithms and data structures, with the
same complexity guarantees, wherever possible; when one workspace delivers a stronger guarantee,
that stronger guarantee becomes the target for all; and no fix may make any delivered bound
asymptotically worse.**

## What the Census Found

Nine parallel code-first censuses — one per workspace, reading node representations and delivered
bounds from the code, treating documentation as claims to check — produced a picture far beyond
the original finding:

- Only **four of nine** workspaces (C#, C, C++, Haskell) carried the genuine lazy Hinze–Paterson
  finger tree the library's bounds assume. Rust, Kotlin, OCaml, Python, and TypeScript shipped
  height-balanced join trees, three of them under finger-tree names, two with documented claims
  their code did not deliver.
- OCaml additionally shipped **six placeholder substrates**: flat-array sorted family (Θ(n)
  writes, Θ(n²) `of_list`), flat-array priority search queue, flat-array range-update sequence
  (Θ(n) range edits), linear-scan ordered family (Θ(n) *membership*), a list-backed Brodal heap
  (Θ(n) find-min), and an RRB vector that was a type alias of the deque.
- Haskell's sorted set and map were facades over `containers` with O(log n) extremes and a
  **linear** rank-slice; Python's ordered family recovered keyed positions by a binary search
  whose every probe was itself a descent — **O(log² n)**; the Rust and Haskell connection forests
  carried *expected* CHAMP factors where five siblings had unconditional ones.
- Two spots-checked claims were re-verified by hand before the census was trusted; both held.

## What Was Done

Twenty-six commits, `7995830` through `4e59684`, in dependency-ordered waves. Full ledger in the
census; the shape:

| Wave | Content | Headline commits |
| --- | --- | --- |
| 0 | OCaml Brodal heap → real skew-binomial kernel; Python ordered O(log² n) → one stamped-measure descent; Rust+Haskell forests → unconditional CHAMP factor via injective fmix32; C#/Rust run-delta wording raised to delivered worst-case | `f50a7be`, `ac8e02f`, `564de90`, `375e269` |
| C1 ruling | The arena's Θ(√M) block-boundary spike **accepted** as the shared contract; documented identically in all seven arena workspaces; Haskell recorded as exceeding | `932d670` |
| 1 (keystone) | Five lazy Hinze–Paterson cores, in place, same public APIs: Python, Rust, Kotlin, TypeScript, OCaml — plus the reversed-view strengthening and its Python backport, and rows A8/A9 (reversible-deque cross-orientation concat, deque reverse) closed structurally | `bc238ca`, `18d216d`, `0e8aaba`, `9e4a1e1`, `b9434bb`, `8790f16`, `63901f4` |
| 2 | OCaml catch-up on the new core: sorted family (order-statistic measure), real 32-way RRB, winner-cached PSQ, lazy-tag range-update, CHAMP+stamped ordered family | `a0df414`, `dd38499` |
| 3 | Haskell sorted set/map onto its own finger tree; the hedge-merge class held, so the no-regression guard never fired | `2e7b86f` |
| Coda | The PSQ coverage gap the rebuild recorded: `enumerate_at_most` with the reference's O(log n + v) | `4e59684` |

### The keystone, in one table

| Operation | Join-tree workspaces before | All nine workspaces now |
| --- | --- | --- |
| first/last read | Θ(log n) (Rust *claimed* O(1)) | O(1) worst case (digit read) |
| endpoint push/pop | Θ(log n) worst and amortized | O(1) amortized, valid under persistent branching; O(log n) worst per call |
| concat | O(height difference) → O(log max) | O(log min(n, m)) amortized |
| split / index / locate | O(log n) | O(log n) — unchanged, per the no-regression rule |
| deque reverse (Py/TS/OCaml) | Θ(n) rebuild | O(1) lazy structural mirror |
| cross-orientation reversible concat | Θ(n+m) re-materialization | O(log min) structural |

Each strict language realized the laziness with its own primitive — Python memoized thunk slots
under the GIL, Rust `OnceLock` cells, Kotlin `AtomicReference` compare-and-set, TypeScript plain
assignment (documented as the one single-threaded workspace where that *is* the atomic
publication), OCaml a mutable field under the 4.14 runtime lock — and all five publish
identically: only after the deferred computation succeeds, so a raising policy publishes nothing
and every force is retryable.

## The Evidence Discipline

Three practices carried the campaign, and they are the part worth reusing.

**Asymptotic claims got probes, not just correctness tests.** A join tree passes every behavioural
test a finger tree passes; only counting the policy's `combine`/`measure` calls distinguishes
them. The probe suites pin exact numbers, and the numbers came out **byte-identical in all five
new cores**: 512 endpoint pushes cost exactly 340 combines at both n=1024 and n=32768; concat's
immediate cost is 0 combines against a singleton and 4 against a 1024-element operand; a
10,000-append spine branched 49 ways pays 3,336 combines on its first force and exactly 5 per
warmed branch; 200,000-element pending chains force (and, in Rust, drop) iteratively. Five
languages, five memoization primitives, one algorithm — the identical signature is the parity
principle made measurable.

**Every load-bearing mechanism was mutation-checked.** Applied, shown failing with a
characteristic signature, reverted: the eager-tag range-update mutant paid 20,118 algebra calls
against a 600-call bound; the disabled RRB seam merge produced exactly the predicted 544 leaves;
the unpruned PSQ enumeration paid 12,289 comparisons against a 168 bound; the binary-search
ordered mutant tripped a *zero-combine* lower bound (size-directed indexing performs none — the
tripwire is an assertion that the counter moved at all).

**Where the obvious counter could not see the fix, the probe measured something else.** The OCaml
sorted-family flip was invisible to a comparator counter — the old array's binary-search *seeks*
were already logarithmic; only its Θ(n) *writes* were the violation, and writes don't compare — so
the probe pins allocation: ~29 KB per write at n=32768 against the array's ≥256 KB copy. The
Haskell slice probe had to route positions through an `IORef` because common-subexpression
elimination constant-folded the measurement to zero bytes on the first attempt.

## Equalize-Upward Events

The principle's most interesting clause — a stronger guarantee anywhere becomes the target
everywhere — fired twice in mid-campaign, in directions nobody planned:

- **TypeScript strengthened the reversed view.** Python's original reused cached node measures
  under a documented commutative-only contract; the TS port recombined mirrored summaries in
  mirrored order, correct under *every* monoid at no immediate cost. Python was upgraded the same
  day (`8790f16`), mutation-verified by a spelling-measure probe only reversed-order recombination
  can pass, and OCaml inherited the stronger shape from birth.
- **Python's run-delta vector out-claimed the reference.** Its eager substrates deliver worst-case
  splice bounds where C# and Rust said "amortized" over equally eager paths — so the *documentation*
  equalized upward to the delivered worst-case wording.

## The Rulings

Two tradeoffs were decided by the maintainer and recorded rather than silently resolved:

1. **The arena's Θ(√M) block-boundary spike stays.** Real-time two-table migration was analyzed —
   including the correction that naive next-block pre-initialization cannot work in GC languages,
   where array allocation itself zeroes Θ(block) — found implementable in all eight arena
   workspaces, and declined. All seven arena implementations now document the identical two-part
   contract (O(1) amortized, explicit spike), and Haskell's arena-free O(1) worst case is a
   recorded over-delivery.
2. **Sorted rank-select regressed O(1) → O(log n)** where flat arrays died, because no persistent
   structure gives both O(log n) writes and O(1) select; the O(1) select was an artifact of the
   placeholder. Stated in every affected `.mli`, not hidden.

Both rulings live in the census under "Deliberate non-violations" so a future maintainer finds the
decision, the analysis, and the alternative that was declined.

## Transferable Lessons

1. **Documentation is a claim, not a fact.** Two workspaces' core docs asserted bounds their code
   did not deliver, and one module's header promised an API that no export provided. Code-first
   auditing — with the doc treated as the hypothesis to falsify — found every one.
2. **Amortized bounds under persistence require laziness; laziness at scale requires
   defunctionalization.** Organic append loops build Θ(n)-deep suspension chains. Closure-based
   forcing died at Python's 1000-frame limit; OCaml's *native* `Lazy.force` overflowed at a
   200,000-deep chain (established by experiment before designing that port); Rust additionally
   needed an iterative `Drop`, because `Arc` chain teardown recurses where tracing GCs do not.
   Every new core stores pending operations as data and forces them with an explicit stack. Whether
   the three donor cores (C#, C++, C) survive the same chains was never tested and is filed as an
   open probe.
3. **A no-regression rule needs an escape valve and a stop condition.** The "if possible at all"
   clause absorbed the two genuine tradeoffs as rulings; the Haskell set-algebra rebuild carried an
   explicit instruction to stop and report rather than land a regression if `containers`'
   hedge-merge class proved unreachable — it did not (run-adopting merges hold O(m log(n/m + 1))),
   but the guard is the reason that outcome is trustworthy.
4. **Cross-language parity is testable.** The strongest artifact of the campaign is not any single
   fix but the shared probe signature: identical operation-count fingerprints in every language.
   A future substrate change that breaks parity breaks a pinned number, not a prose promise.
5. **Fix the work order before executing it.** The census itself was corrected twice mid-campaign —
   once when analysis showed its C1 mechanism was naive, once when a ruling dissolved the wave.
   Both corrections were committed with their reasoning before any agent could faithfully
   implement the wrong design.

## Current State and Loose Ends

All nine workspaces conform; every census row is struck with its landing commit and probe numbers;
each workspace's full gate is green at HEAD (Python 430 tests; Rust 30 binaries / 243
finger-tree; Kotlin 299 across three workspaces; TypeScript 265; OCaml 176; Haskell, C#, C, C++
suites unchanged and passing where touched). One follow-up remains deliberately open: the donor
deep-chain robustness probe (whether the C#, C++, and C cores force and release 200k-deep
suspension chains without stack overflow — the hazard the five new cores were built to resist was
never tested against the three originals). If a new structure is added to the library, the census
plus this retrospective are the standing checklist: state delivered bounds against the real
substrate, probe them, mutation-check the mechanism, and equalize upward.
