# Agent Workflow References

- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: AI agents and maintainers
- Scope: Compact task-conditional workflow guidance for this repository

## Repository Orientation

Use [the workspace map](../reference/workspace-map.md) for the language-first layout, port lineage,
and documentation placement rules. Use [the build and validation guide](build-and-validation.md) for
the complete cross-repository validation matrix.

## C# and .NET validation

Use the real .NET SDK in the local Windows environment:

```powershell
cd C:\DataStructures\src\CSharp\FingerTree
dotnet restore
dotnet build
dotnet test .\FingerTree.sln
```

Prefer deterministic tests for data-structure complexity claims. Do not replace operation-count or allocation guards with timing thresholds unless the task is explicitly benchmark-oriented.

## XML documentation

The library treats missing or malformed public XML documentation as build-breaking through `CS1591` and `CS1573`. Write XML documentation for meaning, not signatures:

- preconditions and postconditions;
- ordering and comparison semantics;
- exception behavior;
- persistence and structural-sharing behavior;
- concurrency and publication behavior;
- complexity and allocation behavior where it is part of the contract.

Include examples for nontrivial APIs, especially measured-tree predicates, persistence/concurrency patterns, and APIs whose behavior depends on ordering or monoidal measures.

## Technical documentation

Documentation should describe current state. Historical context belongs in explicit reports or provenance documents. Update active docs when changing paths, public APIs, complexity guarantees, benchmark claims, or external-reference layout.
Use [the documentation maintenance guide](documentation-maintenance.md) for placement rules, metadata, link/path conventions, and documentation-specific validation.

Every new long-lived document should include:

```markdown
- Created (UTC): YYYY-MM-DDTHH:MM:SSZ
- Repository HEAD: <40-hex-sha>
```

## External references

`src/CSharp/FingerTree/docs/external` is external study material. Do not make substantive edits there unless the task explicitly concerns curation or provenance. Keep license and authorship distinctions visible.

## History filtering

For future repository extraction or path-history work, use `git-filter-repo` through the Python module:

```powershell
python -m git_filter_repo --help
```

Do not run destructive history filters against a primary working repository. Use a clone, or a shared clone followed by `git repack -a -d --no-local`, alternates removal, and `git fsck --full`.
