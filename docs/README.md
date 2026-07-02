# DataStructures Documentation

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI agents working in the standalone DataStructures repository
- Scope: Repository-level documentation index

This directory contains repository-level guides, reference material, and extraction provenance.
Library-specific design and API documentation lives beside each workspace under `src/`.

## Guides

- [Guides index](guides/README.md) - task-oriented repository procedures.
- [Agent workflows](guides/agent-workflows.md) - compact task-conditional workflow guidance inherited from the Tools repository where relevant.
- [Build and validation](guides/build-and-validation.md) - repository-wide validation matrix, exact build/test commands, CMake cache notes, and Markdown checks.
- [Documentation maintenance](guides/documentation-maintenance.md) - documentation placement, writing standards, metadata, and validation.
- [Porting and semantic parity](guides/porting-and-semantic-parity.md) - workflow for carrying behavior and documentation changes across C#, C++, and C workspaces.

## Reference

- [Reference index](reference/README.md) - durable cross-workspace reference material.
- [Data structure catalog](reference/data-structure-catalog.md) - cross-language catalog of repository-owned data-structure families, public entry points, and primary references.
- [Documentation navigation matrix](reference/navigation-matrix.md) - task-oriented entry points for usage, API, validation, porting, history, and maintenance work.
- [Test suite map](reference/test-suite-map.md) - cross-workspace map of test runners, routine commands, stress knobs, sample smoke tests, and benchmark boundaries.
- [Workspace map](reference/workspace-map.md) - language-first layout, data-structure workspace roles, port lineage, and documentation placement rules.

## Migration

- [Migration index](migration/README.md) - extraction provenance and retained history-rewrite artifacts.
- [Migration provenance](migration/extraction-provenance.md) - source repository, source HEAD, filter command, validation notes, and follow-up policy for the extraction from Tools.
- [Filter-repo commit map](migration/filter-repo-commit-map.tsv) - retained old-to-new commit mapping for the extracted history.

## Placement

Put repository-wide task procedures under `docs/guides`, durable cross-workspace maps under
`docs/reference`, and extraction/history records under `docs/migration`.

Put C# HAMT implementation, API, and validation documents under
[src/CSharp/Hamt/docs](../src/CSharp/Hamt/docs/README.md), C HAMT port documents under
[src/C/Hamt/docs](../src/C/Hamt/docs/README.md), and C++ HAMT port documents under
[src/Cpp/Hamt/docs](../src/Cpp/Hamt/docs/README.md). Put C# FingerTree usage, implementation, API,
validation, benchmark, and algorithm documents under
[src/CSharp/FingerTree/docs](../src/CSharp/FingerTree/docs/README.md), with native FingerTree usage,
API, port, and validation documents under [src/Cpp/FingerTree/docs](../src/Cpp/FingerTree/docs/README.md) and
[src/C/FingerTree/docs](../src/C/FingerTree/docs/README.md).
