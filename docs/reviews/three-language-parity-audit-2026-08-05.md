# Three-Language Parity Audit — 2026-08-05

> **Current-state note (2026-08-05, later the same day).** Haskell and Kotlin ports of the same seven
> collections shipped after this audit, so coverage is now five languages. This audit's scope and
> findings are unchanged and still describe the C# / Rust / C shipment; the later ports carry their
> own verification, recorded in
> [the Haskell port review](haskell-collection-port-2026-08-05.md) and
> [the Kotlin port review](kotlin-collection-port-2026-08-05.md).

- Created (UTC): 2026-08-05
- Repository HEAD (audited): `experimental` branch, post C-port (`a001dea`)
- Audience: Maintainers verifying the completeness and faithfulness of the Rust and C ports of the
  seven research-derived collections against the C# baseline
- Scope: Member-by-member API completeness and adversarial semantic-parity verification of all seven
  collections plus the shared incremental level-ancestor arena, in both Rust and C, against the C#
  sources of truth; the fixes applied; and the validation evidence

## Method

Eight independent audit passes, one per unit (`AncestralSliceQueue`, `BilateralAncestralDeque`,
`ContextualRankSequence`, `IncrementalAncestorArena`, `PersistentDeltaMap`,
`PersistentMonotoneActionHeap`, `PersistentRunDeltaVector`, `PersistentAncestralConnectionForest`).
Each pass read the full C# source, the full Rust module, and the full C header plus implementation,
mapped every public C# member to its counterparts, adversarially compared boundary conditions,
error contracts, algorithmic behavior, and complexity claims, and checked the documented-divergence
lists in both directions. Candidate findings were re-examined against source before being reported;
refuted candidates are recorded in the audit transcripts and summarized per collection below only
where load-bearing.

## Verdict

**Both ports are complete and faithful.** Every public C# member has a counterpart in Rust and C
(modulo the workspace shape conventions the porting guide sanctions), no critical semantic bug was
found in any port, and every substantive divergence is intentional and documented. Nine findings
survived adversarial re-examination — one comparator-frugality contract the C delta map documented
but did not deliver, one missing C accessor, one missing defensive assert in Rust, and six
documentation gaps — all fixed in this change.

## Findings And Remediation

| # | Unit | Language | Severity | Finding | Fix |
| --- | --- | --- | --- | --- | --- |
| 1 | Arena seam (reported independently by three passes) | Rust | doc error | `api-notes.md` described the consolidated arena as a live "deliberate divergence from C#", but C# has since converged on the same single seam | Reworded as a historical note matching the C phrasing |
| 2 | ContextualRankSequence | Rust | minor | `try_select_event` documented a panic on a machine returning an out-of-range state but never checked it, so a nondeterministic contract-violating machine would embed the invalid state silently | Added the same `assert!` used by `element_summary` |
| 3 | ContextualRankSequence | C | doc error | Header claimed O(1) full-sequence `evaluate`, but the C path copies the O(s)-wide root effect table into scratch and can report `FT_STATUS_NO_MEMORY`; C# and Rust genuinely deliver O(1) | Header reworded at all three claim sites: O(s) cached-summary read, O(1) for a fixed machine |
| 4 | PersistentDeltaMap | C | undelivered documented bound | Header promised "O(log(k + 1)) key comparisons" for range-restricted change enumeration, but the walk compared at every visited node — Θ(output + log k) comparator invocations, observable through counting/failing comparers; C# and Rust deliver the bound | Range walk rewritten with both-bounds-established propagation: comparisons now occur only along the two boundary paths, the in-range interior is walked comparison-free; the comparison-count test bound tightened from 512 to 96 |
| 5 | PersistentDeltaMap | Rust | doc gap | Eager change enumeration (Θ(k + 1) at iterator construction vs C#'s lazy Θ(1) handle) was documented in rustdoc but missing from the api-notes divergence list | Added to the divergence paragraph |
| 6 | PersistentRunDeltaVector | Rust | doc gap | The `Eq` bound on natural policy constructors and the `reflexive_ieee()` float escape (NaN-reflexivity parity with .NET) were documented in rustdoc but missing from the api-notes divergence list | Added to the divergence paragraph |
| 7 | PersistentRunDeltaVector | C | missing API | No `ft_run_delta_vector_get_policy`; C# exposes `ValueComparer`, Rust `value_policy()`, and the sibling C delta map has exactly this accessor | Accessor added mirroring `ft_delta_map_get_policy`, with retained-handle discipline and tests (identity round-trip plus invalid-argument rejection) |
| 8 | PersistentMonotoneActionHeap | C | doc gap | The C port's exposed-root refinement (fresh root + one tagged forest cell on `transform_all` instead of child-forest sharing; representation-dependent `tagged_component_count`) was documented in the header but missing from the api-notes | Added to the api-notes monotone-action paragraph |
| 9 | PersistentAncestralConnectionForest | C | doc gap | `_link` header comment omitted its failure statuses (`D7_HAMT_OVERFLOW`, `D7_HAMT_OUT_OF_MEMORY`) while C# documents `OverflowException` and the same header documents statuses elsewhere | Failure statuses added to the `_link` comment |

Finding 4 is the only one that changed behavior a caller could observe: comparator callbacks are
now invoked O(log(k + 1)) times as documented rather than once per in-range node. Wall-clock
complexity was unaffected either way.

## Notable Verified-Faithful Points

- The load-bearing checkpoint-differential rules — no-op write detection via the retained value
  policy, first-effective-write `before` capture, coalescing, set-then-restore cancellation with
  exact checkpoint-representative restoration, and clean-index root canonicalization — are
  branch-order identical in all three languages for both the delta map and the run-delta vector.
- The monotone-action heap's composition direction (`compose(outer, inner)` with the newer action
  outer), clamp algebra edge cases (disjoint collapse keeps the newer boundary; overlapping
  intersection keeps the older representative on equal boundaries), and expose-before-attach
  temporal ordering are transliteration-exact in both ports.
- The Myers skip-link construction and query loop are line-for-line equivalent in all three arenas;
  no port claims the unimplemented Alstrup–Holm O(1) worst-case bound.
- The C connection forest's unconditional CHAMP path factor is genuinely earned by a bijective
  32-bit mix, while the Rust port's expected-cost caveat for its truncated `BuildHasher` remains
  accurately documented — an intentional, documented asymmetry, not drift.
- The C workspace's extra obligations — failure atomicity under injected allocation and callback
  failures, exact result/operand aliasing, balanced retain/release — held at every site probed.

## Validation

- C#: 1,254 tests pass (367 Hamt + 807 FingerTree + 80 Ordered) — unchanged by this audit.
- Rust: full suite passes, 30 binaries, 0 failures, after the `try_select_event` assert.
- C FingerTree: reconfigured and rebuilt clean under GCC/Ninja with `-Wall -Wextra -Wpedantic
  -Werror`; 18/18 CTest cases pass, including the tightened delta-map comparison-count bound and
  the new run-delta policy-accessor coverage.
- C Hamt: connection-forest module compiles warning-free under the same flags and passes 16/16.
- MSVC remains the canonical C gate and was unavailable on the auditing machine; the GCC runs are
  evidence, not the gate, matching the port commit's recorded status.
