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
- [Porting and semantic parity](guides/porting-and-semantic-parity.md) - workflow for carrying behavior and documentation changes across C#, C++, C, Haskell, Kotlin, and Rust workspaces.

## Reference

- [Reference index](reference/README.md) - durable cross-workspace reference material.
- [Data structure catalog](reference/data-structure-catalog.md) - cross-language catalog of repository-owned data-structure families, public entry points, and primary references.
- [Derived structure catalog](reference/derived-structure-catalog.md) - verified candidate structures buildable on the shipped families, shared enabling API gaps, and composition design rules.
- [Frontier structure catalog](reference/frontier-structure-catalog.md) - surveyed candidates beyond composition: new cores (including recent inventions), hybrid/adaptive representation tiers, and niche-specialized siblings, with verdicts and sequencing.
- [Documentation navigation matrix](reference/navigation-matrix.md) - task-oriented entry points for usage, API, validation, porting, history, and maintenance work.
- [Semantic contracts](reference/semantic-contracts.md) - shared behavior, ownership, policy, ordering, and documentation obligations across repository-owned numerics and data structures.
- [Test suite map](reference/test-suite-map.md) - cross-workspace map of test runners, routine commands, stress knobs, sample smoke tests, and benchmark boundaries.
- [Workspace map](reference/workspace-map.md) - language-first layout, data-structure workspace roles, port lineage, and documentation placement rules.

## Proposals

- [Next data structures (2026-07-09)](proposals/new-data-structures-2026-07-09.md) - prioritized slate of new structures and API additions building on the derived-structure catalog: HAMT update/builder/diff, insertion-ordered set, hash bag, cursor/zipper, Patricia trie family, and numerics extensions.

## Reviews

- [Axis 1 new-cores review (2026-07-12)](reviews/axis1-new-cores-review-2026-07-12.md) - ten-pass adversarial review of the shipped Axis 1 cores (CHAMP, Patricia, RRB, Merkle, canonical zip-zip set, Brodal-Okasaki, priority search queue, DABA Lite, Ctrie) across all six languages: the C canonical-set removal-merge heap overflow fixed this round, the Kotlin Ctrie snapshot isolation defect and Merkle cross-language golden-vector gap flagged for follow-up, per-core parity and test-coverage findings, verified-clean highlights, and a prioritized remediation order.
- [Cross-language implementation review (2026-07-11)](reviews/cross-language-implementation-review-2026-07-11.md) - seven-pass follow-up review after the 2026-07-10 round: empirically confirmed findings (C# enumerator-copy aliasing, three Numerics BCL-parity gaps, Haskell cross-policy symmetric difference and stale bag representatives, C Tungsten map-callback leak, Kotlin/Rust/C++ complexity-parity items), the six fix commits landed, areas re-verified clean, validation evidence, and the remaining backlog (HAMT bulk-builder ports, Kotlin log² bounds).
- [Cross-language implementation review (2026-07-10)](reviews/cross-language-implementation-review-2026-07-10.md) - nine-pass correctness, semantic-parity, and complexity-parity review of the FingerTree, HAMT, Tungsten, and Numerics families across all six languages: verified findings, the eleven fix commits landed, the post-review parity matrix, validation evidence, and the prioritized open-item backlog. (The three 2026-07-09 review reports were resolved and deleted; this report supersedes them.)

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
executable.
