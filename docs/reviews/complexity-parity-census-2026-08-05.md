# Cross-Language Complexity Parity Census — 2026-08-05

- Created (UTC): 2026-08-05
- Repository HEAD (surveyed): `main` at 1630565 (post Python port)
- Audience: Maintainers executing the complexity-equalization campaign
- Scope: Every place where a workspace's data-structure representation or delivered asymptotic
  bound diverges from the library's shared profile, ranked, with the target guarantee and the fix
- Status: **Remediation complete (2026-08-06).** Every ranked row is struck; this document remains
  the campaign's record, with each row carrying its landing evidence and probe numbers.

> **Current-state note (2026-08-07):** every struck row below was independently re-verified and
> genuinely landed — but the census's *survey* was not exhaustive, so "remediation complete" does
> not mean the library is at parity. See
> [Complexity-parity claim validation](complexity-parity-claim-validation-2026-08-07.md). In
> particular: the ranked rows here cover push-side suspension but never endpoint *pop*, which is
> undeferred in all five rebuilt cores; the OCaml placeholder list is six entries short
> (`priority_queue`, `interval_tree`, `persistent_interval_map`, `persistent_chunked_bit_set`,
> `merkle_search_tree`, `daba_lite`, plus `text_rope`); row A7's defect shape was fixed in Python
> and OCaml but is still live in Rust and C; Class B never reached Rust's `deque.rs` or Kotlin's
> `ReversibleDeque`, which are separate trees; and row A6 left an equalize-upward obligation
> unclosed, since Haskell's hedge-class set algebra beats the reference's own Θ(n+m).

## Governing Principle

Every language uses the same algorithms and data structures, with the same complexity guarantees,
wherever possible. When one workspace delivers a stronger guarantee than the reference, the target
becomes that stronger guarantee for every workspace where it is achievable. No fix may make any
delivered bound asymptotically worse. Where laziness is required for a bound (Hinze–Paterson
amortization under persistence), strict languages implement memoized suspensions by hand — C#, C++,
and C already do exactly this, so it is proven feasible in this codebase, including in plain C.

## Method

Nine parallel code-first censuses (one per workspace), reading node representations and delivered
bounds from the code and treating documentation as claims to check. Two claims were independently
re-verified by hand before this document was written: Rust's `front()`/`back()` docs claim O(1)
while the code recurses the spine (`measured.rs:657-671` vs `measured.rs:889-899`), and Python's
ordered-map keyed position lookup is a binary search whose every probe is an O(log n) tree descent
(`persistent_ordered_map.py:588-599`). Raw census JSON: session scratchpad `census-full.json`;
donor-mechanism extractions (C#, C++, Haskell lazy cores): `donors-full.json`.

## The Substrate Map (who has what today)

| Workspace | Measured core | Verdict |
| --- | --- | --- |
| C# | Lazy Hinze–Paterson finger tree, memoized suspensions | reference |
| C | Lazy H–P finger tree, hand-rolled memoized middle, persistence-safe | conforming donor |
| C++ | Lazy H–P finger tree, atomic immutable publication | conforming donor |
| Haskell | H–P finger tree, native laziness | conforming donor |
| Rust | Lazy H–P finger tree, `OnceLock` suspensions, iterative force AND drop (18d216d) | conforming |
| Kotlin | Lazy H–P finger tree, sealed suspensions, CAS publication (0e8aaba) | conforming |
| OCaml | Lazy H–P finger tree, defunctionalized force, field publication (b9434bb) | conforming |
| Python | Lazy H–P finger tree, memoized thunk slots (bc238ca) | conforming |
| TypeScript | Lazy H–P finger tree, plain-assignment publication (9e4a1e1) | conforming |

## Ranked Violations

### Class A — asymptotically worse class or placeholder structure (worst)

| # | Where | Delivered today | Target (reference profile) | Fix |
| --- | --- | --- | --- | --- |
| ~~A1~~ | ~~OCaml `Sorted_set`/`Sorted_bag`/`Sorted_map`: flat immutable arrays~~ | **FIXED (Wave 2a).** Rebuilt on the lazy finger-tree core with a (count, last-key) order-statistic measure: O(log n) point writes (probe: 31 comparisons at n=1024 vs 50 at n=32768; one write allocates ~29 KB at n=32768 where the array copied ≥256 KB), O(1) comparator-free extremes (mutation-checked), O(m log(n+m)) iterate-and-insert set algebra, O(n log n) `of_list`. Rank select regressed to O(log n) exactly as ruled below. `Persistent_delta_map` write bounds flipped Θ(N+k) → O(log N) with its comparison-budget tests unchanged | done |
| ~~A2~~ | ~~OCaml `Brodal_okasaki_heap`~~ | **FIXED.** Rebuilt as the untagged specialization of the monotone action heap's skew-binomial kernel: O(1) worst-case insert/meld/minimum, O(log n) delete-min, O(1) count; `statistics` measures the real forest shape, and a linking-disabled mutant fails the forest-length ceiling (4095 vs 14 at n=4096) | done |
| ~~A3~~ | ~~OCaml `Priority_search_queue`: flat sorted array~~ | **FIXED (Wave 2b).** Rebuilt as the winner-cached key-ordered AVL every sibling ships: O(log n) keyed writes (probe: 44 comparator calls at n=1024 vs 64 at n=32768 — the depth difference, not 32x), O(1) find-min reading the root's cached winner with zero comparator calls at n=8192 (mutation-checked: a winner cache that ignores its children fails the probe by value), O(log n) delete-min as a keyed delete of the winner (29 vs 44 calls), and one write allocating ~3 KB at n=32768 where the array copied ≥256 KB. Rank select regressed O(1) → O(log n) exactly as the A1 ruling precedent, stated in the `.mli` | done |
| ~~A4~~ | ~~OCaml `Range_update_sequence`: flat array, tags applied eagerly per element~~ | **FIXED (Wave 2b).** Rebuilt as the path-copied implicit-key AVL with composable pending tags (node's own value/measure reflect its tag, children defer; `compose newer older`; the pending marker is the option slot, never a sentinel): tagging a 10-element and a 10,000-element range of the same 16,384-element sequence costs 124 vs 162 algebra calls — apply+compose 6 vs 22, where the eager per-element mutant pays 20,118 and fails the probe — a whole-sequence tag is exactly 1 apply_element + 1 apply_measure, a 10,000-element range measure is 9 calls, and a covered point read is 1. Point indexing regressed O(1) → O(log n) per the ruling precedent, stated in the `.mli` | done |
| ~~A5~~ | ~~OCaml ordered family: flat arrays with linear scans~~ | **FIXED (Wave 2b).** Rebuilt as the CHAMP-plus-stamped-sequence composite with the stamp-measured single-descent design from Python's A7 fix (`Ordered.Stamped_order`, max-stamp measure over the lazy finger-tree core): expected-O(1) membership (3 policy calls at both n=1024 and n=32768), stamp→position one measure-directed locate — 29 combines at n=1024 vs 50 at n=32768, delta 21, where the purged binary-search-through-indexing shape performs 0 combines and fails the probe's lower bound — O(log n) positional writes (one add allocates ~4 KB at n=32768 vs the ≥256 KB array copy), sparse 2{^20} stamp stride with deterministic full relabel, first-representative retention and move semantics pinned by the unchanged tests; the multimap composes the rebuilt pair. Rank access regressed O(1) → O(log n) per the ruling precedent, stated in the `.mli`s | done |
| ~~A6~~ | ~~Haskell `SortedSet`/`SortedMap`: facades over `Data.Set`/`Data.Map`~~ | **FIXED (Wave 3).** Rebuilt on the workspace's own finger tree with `SortedBag`'s (count, last-key) measure: extremes O(log n) → O(1) digit reads (probe: 0 comparisons at n=4096, negative-control seek 30; comparator-seek mutant fails at 2); `SortedSet` rank-slice Θ(position+length) → two comparison-free count-splits (0 comparisons and 108 KB vs 139 KB allocation at positions 10 vs 30,000 of n=32,768; toList/take/drop mutant fails both gates at 63 comparisons and 60.6 MB far-position allocation); point writes stay O(log n) (26 → 40 comparisons across 32× growth). Set algebra kept the hedge class O(m log(n/m+1)) via run-adopting boundary-split merges — disjoint ranges 2 comparisons, interleaved equal sizes 5.0/element flat across 4× growth, 32-into-32,768 at 1,015 — no regression. `PersistentDeltaMap` untouched atop it: budgets green with the two-split `keyRange` at 32 comparisons for its 40-ceiling window, extremes doc flipped Θ(log N) → O(1) | done |
| ~~A7~~ | ~~Python ordered map/set/multimap keyed position ops~~ | **FIXED in Wave 0** (ac8e02f, before the strike convention started): `StampedOrder` max-stamp measure, one locate descent; counting-policy probe pins ≤24 extra combines across a 32× size jump. OCaml's Wave-2b `Stamped_order` reuses the same shape | done |
| ~~A8~~ | ~~Reversible-deque cross-orientation concat: TS, Python, OCaml~~ — **closed everywhere** | Θ(n+m) re-materialization (**Python FIXED**: lazy structural `reversed_view` as a fourth defunctionalized suspension op — O(1) mirror, O(log min) mismatched concat, plain-deque `reverse()` upgraded from Θ(n) to O(1) as a by-product; probe-pinned) | O(log(min)) — C# delivers `a.Reverse().Concat(b)` at O(log min) | Orientation-aware concat; falls out of the Wave-1 rebase for TS/OCaml |
| ~~A9~~ | ~~OCaml reversible deque~~ | **FIXED with the Wave-1 core** (b9434bb): orientation-aware structural concat across all four flag pairings, O(log n) cursor splices | done | done |

### Class B — the keystone: join-tree cores (blocks most of Class A's siblings)

Five workspaces (~~Rust~~, ~~Kotlin~~, ~~OCaml~~, ~~Python~~, ~~TypeScript~~) must replace their join-tree
measured cores with genuine lazy Hinze–Paterson finger trees, **in place, same module, same public
API**. **OCaml landed fifth — CLASS B IS CLOSED** (b9434bb + docs): the weight-balanced join tree is
gone, native `Lazy.force` banned from the spine per the 200k-overflow experiment, suspensions
published by single mutable-field assignment under the 4.14 runtime lock, and the same probe
signature as all four siblings (340/340, 0-vs-4, 3,336-vs-5, 200k chains). Rows A9 and A8 closed
with it: the reversible deque's Θ(n+m) concat and Θ(n) cursor edits became structural through the
orientation-aware rebase and the every-monoid reversed view. All five cores now measure identical
probe numbers — the library's measured substrate is one algorithm in five more languages.
**TypeScript landed fourth** (9e4a1e1 + follow-ups): same probe signature (340/340, 3,336-vs-5.0,
0-vs-4), plain-assignment publication documented as the single-threaded workspace's atomic
primitive, and row A8 closed for TS via the lazy reversed view. It also STRENGTHENED the reversed
view beyond the Python original — mirrored node summaries recombine in mirrored order, correct
under every monoid rather than a commutative-only contract — and Python was equalized upward to
match in the same wave, mutation-verified by a spelling-measure probe only reversed-order
recombination can pass. A8 now lists only OCaml, which takes it with its core.
**Kotlin landed third** (0e8aaba + docs): identical probe shape once more (340/340 endpoint
combines, 3,336 vs 5-per-branch memoization, 0-vs-4 concat), sealed-class suspensions published by
`AtomicReference.compareAndSet`, no iterative teardown needed (JVM GC); its formerly false Core.kt
claims are now delivered, and its arena carries the C1 ruling sentence deferred from 932d670.
**Rust landed second** (18d216d + docs): same probe shape — 340 combines for 512 endpoint
pushes at both n=1024 and n=32768, first force 3,336 vs 5 per later branch, 200k chains force AND
drop iteratively (Rust adds the iterative `Drop`, since Arc-chain drop recursion overflows where
Python's GC hides it); its formerly false module claims are now true. **Python landed first** (its core, probes, and consumer upgrades): endpoints O(s)/O(1)
amortized with persistence-safe memoization proven by a 49-branch probe, concat O(log min),
extremes O(1), suspensions defunctionalized against the 1000-frame limit. Four remain:

| Operation | Join tree today | Finger-tree target |
| --- | --- | --- |
| endpoint push/pop | Θ(log n) worst AND amortized | O(1) amortized, valid under persistent branching; O(log n) worst |
| first/last read | Θ(log n) (Rust even *claims* O(1)) | O(1) worst-case (digit read) |
| concat | O(height difference) → O(log max) | O(log min(n,m)) amortized |
| split / index / locate | O(log n) | O(log n) — unchanged (no-regression rule holds) |

Laziness mechanism per language (all proven in-repo by C#/C++/C donors): Rust `Arc` + `OnceLock`
suspension cells; Kotlin `lazy {}`/atomic publication; OCaml native `Lazy.t`; Python memoized thunk
slot under the GIL; TypeScript memoized closure cell. Everything downstream inherits the upgrade:
deques, sorted families, ropes, priority queues, interval trees, chunked bit sets, and the
contextual rank sequence (endpoints Θ(s log n) → O(s) amortized; concat keyed to height difference
→ O(s log min) — deleting the substrate-divergence notes those four ports carry today).

Also in this class: reversible deques in the five workspaces get the reference's real
finger-tree-with-orientation representation (fixes A8/A9 as a by-product).

### Class C — amortized vs worst-case gaps

| # | Where | Gap | Fix |
| --- | --- | --- | --- |
| ~~C1~~ | ~~Incremental ancestor arena, 7 workspaces~~ | **RESOLVED BY RULING (2026-08-06): the Θ(√M) block-boundary spike is accepted as the shared contract.** Real-time two-table migration was analyzed, found implementable in all eight arena workspaces, and declined by the maintainer in favour of the simpler odd-block representation. All seven arena workspaces now document the identical contract explicitly — O(1) amortized leaf add, a single block-boundary call paying Θ(√M) for the next odd block — and Haskell's arena-free port is recorded in its own docs as *exceeding* the shared profile at O(1) worst case, sanctioned under the "if possible at all" clause the same way the OCaml rank-select regression was. (Kotlin's arena doc gets the same sentence when its in-flight core lands, to avoid touching an active workspace.) | done — by documentation, per ruling |
| ~~C2~~ | ~~C# run-delta-vector doc wording~~ | **FIXED in Wave 0** (375e269, before the strike convention started): C# and Rust run-delta docs raised to the delivered worst-case wording | done |

### Class D — conditional vs unconditional guarantees

| # | Where | Gap | Fix |
| --- | --- | --- | --- |
| ~~D1~~ | ~~Connection forest, Rust + Haskell~~ | **FIXED.** Both pin the MurmurHash3 fmix32 bijection; Haskell additionally caps the universe at 2^31 − 1 (the universe every sibling already has — no 32-bit hash is injective over a larger domain), and both carry inverse-round-trip tests exhibiting the bijection | done |

General CHAMP maps are expected-O(1) in **all nine** workspaces (arbitrary user keys collide by
pigeonhole) — that is parity, not a violation.

### Class E — doc-vs-code mismatches resolved by the code fixes above

Rust `measured.rs` header ("2-3 finger tree", O(1) front/back, O(log min) concat — all currently
false, become true after Wave 1); Kotlin `Core.kt` amortized-O(1)/log-min claims (same); TS
`core.ts` "finger tree" naming (same); OCaml `rrb_vector.ml` "effectively constant-time indexing"
(**now true — Wave 2a replaced the facade with a genuine eager 32-way RRB**: 5-bit-radix packed
regular branches, size tables only on relaxed branches, seam-only concat rebalance, endpoint push
via singleton concat; `Persistent_run_delta_vector` flipped to the reference's eager worst-case
O(log n) splice wording); OCaml `sorted_map.mli` "built in bulk" vs Θ(n²) `of_list` (resolved by
A1); Python `measured_sequence.py` concat docstring omitting the remove-min term (superseded by
Wave 1).

### Deliberate non-violations (recorded so nobody "fixes" them)

- **OCaml array O(1) rank-select regresses to O(log n) under A1.** No persistent structure gives
  both O(log n) writes and O(1) select; the O(1) select was an artifact of the placeholder. The
  reference profile (O(log n) select, O(1) extremes, O(log n) writes) is the contract. Ruled
  acceptable under "if possible at all" — flagged, not silent.
- **The arena's Θ(√M) block-boundary spike (former C1) is the accepted shared contract** by
  maintainer ruling: every arena workspace documents O(1) amortized adds with the explicit
  spike, and Haskell's arena-free O(1) worst case is a recorded over-delivery, not a target.
- Rope chunking (chunked leaves in C#/C/C++/Rust/Haskell vs per-element in Kotlin/Python/TS):
  constant-factor, not asymptotic. Representation-parity follow-up, not part of this campaign's
  hard constraint.
- DABA Lite is deliberately mutable everywhere it exists; Haskell has none (coverage, not
  complexity).

## Remediation Waves (dependency-ordered)

1. **Wave 0 — small, independent, immediate:** ~~A2~~ ~~A7~~ ~~D1~~ ~~C2~~ — **all done** (commits 375e269, ac8e02f, 564de90, and the A2 commit).
2. **Wave 1 — keystone:** the five finger-tree cores + facade rebases + doc/test tightening.
   Order: Python (freshest workspace knowledge, strictest gates) → Rust → Kotlin → TypeScript →
   OCaml (whose Wave-2 rebuilds sit on its new core).
3. **Wave 2 — OCaml catch-up:** ~~A1~~, ~~A3~~, ~~A4~~, ~~A5~~, ~~real RRB~~, on the new core —
   Wave 2a (sorted family + real RRB) landed; Wave 2b (A3 + A4 + A5) landed. **Wave 2 is
   closed.**
4. ~~**Wave 3 — Haskell sorted family:** A6~~ — **done**; with Wave 2b landed alongside it, **Class A is fully closed**.
5. ~~**Wave 4 — arena worst-case O(1)**~~ — dissolved by the C1 ruling; the documentation pass landed with the ruling itself.

Verification per wave: existing suites stay green (no-regression); the combine-counting probes that
today *prove the weak bounds* are inverted to prove the strong ones (endpoint cost independent of
n; concat cost tracking the min operand); a persistence-focused amortization test (forcing one
version's suspended spine from many branches must not multiply work); and mutation checks on every
new load-bearing mechanism.
