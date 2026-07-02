# DataStructures Documentation

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI agents working in the standalone DataStructures repository
- Scope: Repository-level documentation index

This directory contains repository-level guidance and extraction provenance. Library-specific design
and API documentation lives under [Hamt/docs](../Hamt/docs/README.md),
[HamtC/docs](../HamtC/docs/README.md), [HamtCpp/docs](../HamtCpp/docs/README.md), and
[FingerTree/docs](../FingerTree/docs/README.md), with native FingerTree port docs under
[Cpp/FingerTree/docs](../Cpp/FingerTree/docs/README.md) and
[C/FingerTree/docs](../C/FingerTree/docs/README.md).

## Current documents

- [Agent workflows](agent-workflows.md) - compact task-conditional workflow guidance inherited from the Tools repository where relevant.
- [Migration provenance](migration/extraction-provenance.md) - source repository, source HEAD, filter command, validation notes, and follow-up policy for the extraction from Tools.
- [Filter-repo commit map](migration/filter-repo-commit-map.tsv) - retained old-to-new commit mapping for the extracted history.

## Placement

Put repository-wide reports and migration records here. Put C# HAMT implementation and API
documents under [Hamt/docs](../Hamt/docs/README.md), C HAMT port documents under
[HamtC/docs](../HamtC/docs/README.md), C++ HAMT port documents under
[HamtCpp/docs](../HamtCpp/docs/README.md), and FingerTree implementation, API, benchmark, and
algorithm documents under [FingerTree/docs](../FingerTree/docs/README.md). Put native FingerTree port
documents under [Cpp/FingerTree/docs](../Cpp/FingerTree/docs/README.md) and
[C/FingerTree/docs](../C/FingerTree/docs/README.md).
