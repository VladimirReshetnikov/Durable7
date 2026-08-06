# Cross-Language Complexity Parity Census — 2026-08-05

- Created (UTC): 2026-08-05
- Repository HEAD (surveyed): `main` at 1630565 (post Python port)
- Audience: Maintainers executing the complexity-equalization campaign
- Scope: Every place where a workspace's data-structure representation or delivered asymptotic
  bound diverges from the library's shared profile, ranked, with the target guarantee and the fix
- Status: **Identification complete; remediation in progress.** This document is the campaign's
  work order; strike through rows as they land.

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
| **Rust** | Height-balanced join tree **named `FingerTree`** | **violating** |
| **Kotlin** | Measured AVL join tree | **violating** |
| **OCaml** | Weight-balanced join tree | **violating** |
| **Python** | Implicit AVL join tree | **violating** |
| **TypeScript** | Measured AVL join tree named "finger tree" | **violating** |

## Ranked Violations

### Class A — asymptotically worse class or placeholder structure (worst)

| # | Where | Delivered today | Target (reference profile) | Fix |
| --- | --- | --- | --- | --- |
| A1 | OCaml `Sorted_set`/`Sorted_bag`/`Sorted_map`: flat immutable arrays | Θ(n) point writes, Θ(n·m) set algebra, Θ(n²) `of_list` | O(log n) writes, O(1) extremes | Rebuild on the new OCaml finger-tree core (order-statistic measure), as C#/C/C++ do |
| A2 | OCaml `Brodal_okasaki_heap`: plain element list | Θ(n) find-min/delete-min, Θ(left) meld | O(1) insert/meld/find-min, O(log n) delete-min | Real bootstrapped skew-binomial forest; the kernel already exists in `persistent_monotone_action_heap.ml` |
| A3 | OCaml `Priority_search_queue`: flat sorted array | Θ(n) writes and pops | O(log n) writes/pops, O(1) find-min | Winner-cached AVL, as all eight siblings |
| A4 | OCaml `Range_update_sequence`: flat array, tags applied eagerly per element | Θ(n) range update / insert / split | O(log n) range edit independent of range length | Path-copied implicit AVL with composable pending tags, as siblings |
| A5 | OCaml ordered family: flat arrays with linear scans | Θ(n) membership, Θ(n) writes, Θ(n·m) algebra | expected O(1) membership; O(log n) positional | CHAMP stamp map + stamped sequence composite, as siblings (`Persistent_hamt` is real and ready) |
| A6 | Haskell `SortedSet`/`SortedMap`: facades over `Data.Set`/`Data.Map` | O(log n) min/max; `SortedSet` rank-slice is **Θ(position+length)** | O(1) extremes; O(log n) slice via two splits | Rebuild on the workspace's own finger tree, as its `SortedBag` already is |
| A7 | Python ordered map/set/multimap keyed position ops | **O(log² n)** (`_index_of_stamp` binary search × O(log n) probes) | O(log n): one measure-directed descent | Give `_order` a last-stamp measure and locate by monotone predicate, as TypeScript does |
| A8 | Reversible-deque cross-orientation concat: TS, Python, OCaml | Θ(n+m) re-materialization | O(log(min)) — C# delivers `a.Reverse().Concat(b)` at O(log min) | Orientation-aware concat; falls out of the Wave-1 rebase |
| A9 | OCaml reversible deque | Θ(n+m) concat, Θ(n) cursor edits (list rebuild) | O(log) via tree ops | Rebase on new core (Wave 1) |

### Class B — the keystone: join-tree cores (blocks most of Class A's siblings)

Five workspaces (Rust, Kotlin, OCaml, Python, TypeScript) must replace their join-tree measured
cores with genuine lazy Hinze–Paterson finger trees, **in place, same module, same public API**:

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
| C1 | Incremental ancestor arena, 7 workspaces | Haskell delivers O(1) **worst-case** leaf add (no arena; a node is its own handle); all arena implementations are O(1) amortized, and C's is O(√M) worst at a directory doubling | Incremental next-block pre-initialization makes block allocation worst-case O(1); C's directory doubling likewise incremental. Equalize upward to worst-case O(1) everywhere |
| C2 | C# run-delta-vector doc wording | Claims "O(log n) amortized" splices over a fully eager RRB path — delivered is worst-case | Raise the documented claim to worst-case (Python already states it; verify Rust/others in passing) |

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
(true once a real 32-way RRB replaces the facade in Wave 2); OCaml `sorted_map.mli` "built in bulk"
vs Θ(n²) `of_list` (resolved by A1); Python `measured_sequence.py` concat docstring omitting the
remove-min term (superseded by Wave 1).

### Deliberate non-violations (recorded so nobody "fixes" them)

- **OCaml array O(1) rank-select regresses to O(log n) under A1.** No persistent structure gives
  both O(log n) writes and O(1) select; the O(1) select was an artifact of the placeholder. The
  reference profile (O(log n) select, O(1) extremes, O(log n) writes) is the contract. Ruled
  acceptable under "if possible at all" — flagged, not silent.
- Rope chunking (chunked leaves in C#/C/C++/Rust/Haskell vs per-element in Kotlin/Python/TS):
  constant-factor, not asymptotic. Representation-parity follow-up, not part of this campaign's
  hard constraint.
- DABA Lite is deliberately mutable everywhere it exists; Haskell has none (coverage, not
  complexity).

## Remediation Waves (dependency-ordered)

1. **Wave 0 — small, independent, immediate:** A2 (OCaml Brodal), ~~A7~~ (done, commit ac8e02f),
   ~~D1~~ (done), ~~C2~~ (done, commit 375e269).
2. **Wave 1 — keystone:** the five finger-tree cores + facade rebases + doc/test tightening.
   Order: Python (freshest workspace knowledge, strictest gates) → Rust → Kotlin → TypeScript →
   OCaml (whose Wave-2 rebuilds sit on its new core).
3. **Wave 2 — OCaml catch-up:** A1, A3, A4, A5, real RRB, on the new core.
4. **Wave 3 — Haskell sorted family:** A6.
5. **Wave 4 — arena worst-case O(1):** C1 across all seven arena workspaces.

Verification per wave: existing suites stay green (no-regression); the combine-counting probes that
today *prove the weak bounds* are inverted to prove the strong ones (endpoint cost independent of
n; concat cost tracking the min operand); a persistence-focused amortization test (forcing one
version's suspended spine from many branches must not multiply work); and mutation checks on every
new load-bearing mechanism.
