# DataStructures Documentation

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI agents working in the standalone DataStructures repository
- Scope: Repository-level documentation index

This directory contains repository-level guides, reference material, and extraction provenance.
Library-specific design and API documentation lives beside each workspace under `src/`.

## Guides

- [Guides index](guides/README.md) - task-oriented repository procedures.
- [Repository onboarding](guides/repository-onboarding.md) - end-to-end orientation for choosing workspaces, task scope, documentation responsibilities, and validation evidence.
- [Agent workflows](guides/agent-workflows.md) - compact task-conditional workflow guidance inherited from the Tools repository where relevant.
- [Build and validation](guides/build-and-validation.md) - repository-wide validation matrix, exact build/test commands, CMake cache notes, and Markdown checks.
- [Documentation maintenance](guides/documentation-maintenance.md) - documentation placement, writing standards, metadata, and validation.
- [Porting and semantic parity](guides/porting-and-semantic-parity.md) - workflow for carrying behavior and documentation changes across C#, C++, C, Haskell, Kotlin, Rust, TypeScript, and Python workspaces.

## Reference

- [Reference index](reference/README.md) - durable cross-workspace reference material.
- [Data structure catalog](reference/data-structure-catalog.md) - cross-language catalog of repository-owned data-structure families, public entry points, and primary references.
- [Derived structure catalog](reference/derived-structure-catalog.md) - historical verified composition survey with current disposition notes for the shipped ordered map and hash multimap/relation, remaining candidates, and the distinction between Tungsten evidence and independently owned general structures.
- [Frontier structure catalog](reference/frontier-structure-catalog.md) - current-state record of shipped Axis 1/selected Axis 2 work, the eight-language implicit-AVL Range-update family, and remaining candidates beyond composition: new cores, hybrid/adaptive representation tiers, niche-specialized siblings, verdicts, and sequencing.
- [Documentation navigation matrix](reference/navigation-matrix.md) - task-oriented entry points for usage, API, validation, porting, history, and maintenance work.
- [Semantic contracts](reference/semantic-contracts.md) - shared behavior, ownership, policy, ordering, and documentation obligations across repository-owned numerics and data structures.
- [Test suite map](reference/test-suite-map.md) - cross-workspace map of test runners, routine commands, stress knobs, sample smoke tests, and benchmark boundaries.
- [Tungsten application-leaf dependency boundary](reference/tungsten-application-leaf-boundary.md) - normative one-way dependency, semantic-authority, independent-fork, validation, and extraction policy for the application-specific Tungsten collection family.
- [Workspace map](reference/workspace-map.md) - language-first layout, data-structure workspace roles, port lineage, and documentation placement rules.

## Proposals

- [Benchmark-independent next data structures (2026-07-14)](proposals/benchmark-independent-next-structures-2026-07-14.md) - C#-first design and completed execution record: persistent-HAMT single-pass updates, `PersistentHashBag`, the independently owned neutral `PersistentOrderedSet`, and the algebra-law-gated implicit-AVL `RangeUpdateSequence` ship across all eight languages. Benchmarks were not run and remain postponed for an isolated session. Frozen layouts, size tiers, GUID specialization, and other evidence- or consumer-gated work remain parked.
- [Benchmark-independent cross-language completion audit (2026-07-15)](reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md) - detailed per-language implementation, test, package-wiring, dependency-boundary, review-report, and serialized-validation evidence for the completed four-surface proposal.
- [Axis 2 final plan: lifecycle and sequence cursors](proposals/axis2-lifecycle-and-sequence-cursors.md) - authoritative C#-first plan: a version-bound Rope/MeasuredRope cursor leads, while one-way CHAMP transients and a fixed-layout frozen hash tier advance independently through workload, representation, correctness, memory, and break-even gates; automatic size/key specialization remains deferred.
- [Axis 2, cursor-first alternative (2026-07-13)](proposals/axis2-cursor-first-alternative-2026-07-13.md) - historical sequencing proposal incorporated into the final plan with corrections. It supplied cursor priority, a frozen signal gate, and branched-history scrutiny; its current-consumer, calendar-spike, transient-workload, and canonical-order claims are not the final contract.
- [Next data structures (2026-07-09)](proposals/new-data-structures-2026-07-09.md) - historical, partially realized slate: structural HAMT equality/diff, Patricia, and the C# hash bag shipped; Axis 2 supersedes its cursor schedule, and the remaining builder/facade/numerics ideas require current catalog and consumer evidence.
- [Persistent set of GUIDs design study (2026-07-12)](proposals/persistent-guid-set-design-study-2026-07-12.md) - which shipped collection fits a persistent GUID set best (`PersistentHashSet<Guid>` by default), whether a custom full-key 128-bit trie beats it (a capability/robustness win, not a faster membership set — decisive only for untrusted GUIDs, ordered/range queries, or structural set algebra), the measured GUID sort-order facts, a workload-indexed decision table, and consumer-gated adoption economics.

## Reviews

- [Axis 2 lifecycle and sequence-cursor plan review (2026-07-13)](reviews/axis2-lifecycle-and-cursors-review-2026-07-13.md) - historical review of the pinned pre-synthesis plan. Its sequencing, frozen-signal, branched-history, transient-workload, and documentation findings are dispositioned—some accepted and some corrected—in the authoritative final plan.
- [Axis 1 new-cores review — round 2 (2026-07-12)](reviews/axis1-new-cores-review-round2-2026-07-12.md) - resolved review: verifies every first-round remediation and records closure of all fresh follow-ups, including non-vacuous cross-port CHAMP routing/topology gates, the conservative RRB cap, shared Rust Brodal comparer identity, C++ PSQ pruning, C Patricia sharing, the reverse Kotlin Ctrie race, and Patricia assertion coverage.
- [`PersistentHashSet<T>` structural set-algebra remediation (2026-07-12)](reviews/hamt-set-algebra-nonstructural-2026-07-12.md) - resolved review: records the former element-wise limitation and the completed Phase 2 remediation across all six ports, including same-type structural algebra, reference pruning, cached cardinalities, policy contracts, and failure-atomic C coverage.
- [Axis 1 new-cores review (2026-07-12)](reviews/axis1-new-cores-review-2026-07-12.md) - resolved and superseded first-round review of the shipped Axis 1 cores; its remediation table closes the original Critical-through-Low findings, and the round-2 report verifies them against source.
- [Cross-language implementation review (2026-07-11)](reviews/cross-language-implementation-review-2026-07-11.md) - resolved seven-pass follow-up review; its addendum records the later Rust/C++ HAMT builders, Kotlin logarithmic bounds, and C# allocation-test de-flake that close the original three-item backlog.
- [Cross-language implementation review (2026-07-10)](reviews/cross-language-implementation-review-2026-07-10.md) - resolved nine-pass correctness, semantic-parity, and complexity-parity review; its addendum maps every historical open item to the subsequent implementation and replacement validation evidence.

## Migration

- [Migration index](migration/README.md) - extraction provenance, path-history records, and retained history-rewrite artifacts.
- [Migration provenance](migration/extraction-provenance.md) - source repository, source HEAD, filter command, validation notes, and follow-up policy for the extraction from Tools.
- [Language-first reorganization](migration/language-first-reorganization.md) - old-to-new path map and documentation follow-up record for the 2026-07 move under `src/`.
- [Filter-repo commit map](migration/filter-repo-commit-map.tsv) - retained old-to-new commit mapping for the extracted history.

## Placement

Put repository-wide task procedures under `docs/guides`, durable cross-workspace maps under
`docs/reference`, forward-looking design proposals under `docs/proposals`, point-in-time review
reports under `docs/reviews`, and extraction/history records under `docs/migration`.

Put C# HAMT implementation, API, and validation documents under
[src/CSharp/docs/Hamt](../src/CSharp/docs/Hamt/README.md), C# Numerics API, validation, and maintainer
documents under [src/CSharp/docs/Numerics](../src/CSharp/docs/Numerics/README.md), C HAMT port documents under
[src/C/Hamt/docs](../src/C/Hamt/docs/README.md), and C++ HAMT port documents under
[src/Cpp/Hamt/docs](../src/Cpp/Hamt/docs/README.md). Put C# FingerTree usage, implementation, API,
validation, benchmark, and algorithm documents under
[src/CSharp/docs/FingerTree](../src/CSharp/docs/FingerTree/README.md), with native FingerTree usage,
API, port, and validation documents under [src/Cpp/FingerTree/docs](../src/Cpp/FingerTree/docs/README.md) and
[src/C/FingerTree/docs](../src/C/FingerTree/docs/README.md), and Rust FingerTree-family documents under
[src/Rust/FingerTree/docs](../src/Rust/FingerTree/docs/README.md). Put Kotlin HAMT and FingerTree-family
documents under [src/Kotlin/Hamt/docs](../src/Kotlin/Hamt/docs/README.md) and
[src/Kotlin/FingerTree/docs](../src/Kotlin/FingerTree/docs/README.md). Put Haskell package entry points under
[src/Haskell](../src/Haskell/README.md), with test coverage notes beside each package's cabal test
executable. Put the neutral C# insertion-ordered-set overview, usage, API, and validation documents
under [src/CSharp/docs/Ordered](../src/CSharp/docs/Ordered/README.md).
TypeScript package API and validation notes live under
[src/TypeScript/docs](../src/TypeScript/docs/api-notes.md), with its executable test map under
[src/TypeScript/test](../src/TypeScript/test/README.md).
Python package API and validation notes live under
[src/Python/docs](../src/Python/docs/api-notes.md), with its pytest/Hypothesis test map under
[src/Python/tests](../src/Python/tests/README.md).
