# Documentation Maintenance Guide

- Created (UTC): 2026-07-02T19:44:02Z
- Repository HEAD: 333ae7f0e811f642228b538e235a420b1dc6405f
- Audience: Maintainers and AI agents creating or updating repository documentation
- Scope: Documentation placement, writing standards, metadata, and validation

This repository treats documentation as engineering memory. Keep it current-state oriented, close to
the code it explains, and explicit about historical or external material.

## Documentation Layers

Use the narrowest layer that owns the information:

| Layer | Use for | Examples |
| --- | --- | --- |
| Root `README.md` / `AGENTS.md` / `CLAUDE.md` | Repository entry point, canonical agent guidance, top-level build and layout orientation | Workspaces list, local environment, version-control policy |
| [`docs/guides`](README.md) | Procedures and repeatable workflows | Validation commands, agent workflow guidance, documentation maintenance, porting parity |
| [`docs/reference`](../reference/README.md) | Durable cross-workspace maps and facts | Language/data-structure layout, port lineage, data-structure catalog, test-suite map |
| [`docs/migration`](../migration/README.md) | Extraction, history filtering, and provenance records | Filter-repo notes, commit maps |
| Workspace `README.md` | Workspace orientation and local entry points | Purpose, layout, primary build/test command |
| Workspace `docs/` | API contracts, design notes, validation details, benchmark notes, review reports | `src/CSharp/FingerTree/docs/api-specification.md` |
| Workspace `tests/` README | Local test runner shape, test-file grouping, direct executable path, filters, stress knobs | `src/CSharp/FingerTree/tests/Tools.DataStructures.FingerTree.Tests/README.md` |
| Workspace `samples/` or `benchmarks/` README | Runnable sample programs, expected transcript markers, benchmark workloads, output shape | `src/C/FingerTree/samples/README.md` |
| Workspace `docs/external/` | External study material and source snapshots | Papers, source snapshots, article copies |

When a change crosses boundaries, update every layer whose readers would otherwise be misled. A path
move usually touches root guidance, repository reference docs, workspace README files, and any active
validation guides.

## Current-State And Historical Docs

Active docs should describe the repository as it is now. Historical docs should be clearly marked by
their location or title:

- Keep current commands, paths, and contracts in active guides, README files, and API specifications.
- Preserve old commands and paths in migration provenance or review reports only when they are part of
  the historical record.
- If a historical report links to a moved active document, update the link target while preserving the
  report's original conclusion.
- Do not silently reinterpret historical `Repository HEAD` metadata. If a document is substantively
  rewritten into a current-state guide, say so in the body or create a new guide.

## What To Update

Use this checklist when changing repository behavior:

| Change | Documentation to inspect |
| --- | --- |
| Workspace move, rename, or new workspace | Root `README.md`, [`workspace-map.md`](../reference/workspace-map.md), [`build-and-validation.md`](build-and-validation.md), affected workspace README/docs indexes |
| Public API change | Workspace API specification, XML docs, examples, README surface summary, relevant port notes, [`porting-and-semantic-parity.md`](porting-and-semantic-parity.md), [`data-structure-catalog.md`](../reference/data-structure-catalog.md) when a long-lived public data-structure surface changes |
| Complexity, allocation, or concurrency behavior | API specification, benchmark notes, validation guide, persistence/concurrency docs, tests called out as evidence |
| Test runner, test-file, sample-smoke, or stress-control change | Workspace tests README, workspace validation guide, [`test-suite-map.md`](../reference/test-suite-map.md), [`build-and-validation.md`](build-and-validation.md) if commands changed |
| Build/test command change | Root `README.md`, [`build-and-validation.md`](build-and-validation.md), [`test-suite-map.md`](../reference/test-suite-map.md), affected workspace README, validation docs |
| Benchmark result or benchmark harness change | Workspace benchmark README, benchmark notes, [`test-suite-map.md`](../reference/test-suite-map.md), root benchmark summary if claims changed |
| External reference addition | External index, license/provenance note, root external-material policy if the shape changes |
| New long-lived report | Correct `docs/` bucket or workspace `docs/`, provenance metadata, collision-safe filename if needed |

## Writing Standards

Write docs as contracts and maps, not as narration of what a command happens to print today:

- Explain ownership, invariants, failure behavior, complexity, allocation behavior, and concurrency
  consequences where they are part of the design.
- Prefer examples for APIs whose behavior depends on ordering, measures, ownership, or persistence.
- Name validation evidence precisely: the command, workspace, test suite, and what it proves.
- Keep speculative plans separate from current-state docs. Plans belong in explicit `*-plan.md`
  documents and should not be mistaken for shipped behavior.
- Keep external study material clearly segregated. Repository-owned docs may summarize it, but should
  not imply that external material is covered by this repository's MIT-0 license.
- Do not include secrets, access tokens, machine-local credentials, or transient absolute paths except
  for intentional local environment guidance such as `C:\DataStructures` or toolchain locations.

## Metadata

Every new long-lived document should start with:

```markdown
- Created (UTC): YYYY-MM-DDTHH:MM:SSZ
- Repository HEAD: <40-hex-sha>
```

Use:

```powershell
Get-Date -AsUTC -Format "yyyy-MM-ddTHH:mm:ssZ"
git rev-parse HEAD
```

When filename collisions are likely, append a `__xxxxxxxxxxxx` suffix using 12 lowercase hex digits
from a content hash. Do this for review reports, defect reports, and generated analysis artifacts.

## Links And Paths

- Prefer relative Markdown links inside the repository.
- Link to directory indexes (`README.md`) when pointing at a doc bucket.
- Use the current language-first paths under `src/C`, `src/Cpp`, and `src/CSharp`.
- Use `CSharp` and `Cpp` in paths; reserve `C#` and `C++` for prose.
- Avoid ranges in Markdown links. Link to a file or an exact line only when the renderer supports it.
- Exclude `src/CSharp/FingerTree/docs/external` from repository-owned link/content checks unless the
  task is explicitly about external material.

## Validation

For documentation-only changes, at minimum run this current-state scan, excluding migration
provenance where old extraction paths are intentional:

```powershell
rg -n "C:\\DataStructures\\(Hamt|HamtC|HamtCpp|FingerTree|C\\FingerTree|Cpp\\FingerTree)|sr[s]rc|src[/\\]src|iladimi[r]|T[i]alue|MS[i]C|[i]ersion|docs/agent-workflows\\.md" README.md docs src --glob "!docs/migration/**" --glob "!src/CSharp/FingerTree/docs/external/**" --glob "!*.pdf"
git diff --check
```

Then run the Markdown link checker from [`build-and-validation.md`](build-and-validation.md#documentation-checks).

For docs that change commands, paths, or build claims, also run the relevant command from
[`build-and-validation.md`](build-and-validation.md) or explain why the command was not rerun.

## Review Questions

Before committing docs:

- Can a new maintainer find the right workspace, command, and local docs from the root README?
- Can they find the local test map and the repository [test-suite map](../reference/test-suite-map.md) when coverage,
  stress, sample-smoke, or benchmark boundaries matter?
- Does each new document live in the right layer?
- Are active docs current-state, and are historical notes explicitly historical?
- Are external sources and repository-owned material separated?
- Do links resolve after the move?
- Does the validation evidence match the scope of the claim?
