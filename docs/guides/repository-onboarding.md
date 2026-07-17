# Repository Onboarding Guide

- Created (UTC): 2026-07-03T23:50:37Z
- Repository HEAD: 96a766f45fa42b5bd14c5ae3173956300cbff21b
- Audience: Maintainers and AI agents starting work in this repository
- Scope: End-to-end orientation, task classification, documentation responsibilities, and validation evidence

This guide is the front door for doing productive work in the DataStructures repository. It explains
how to choose the right workspace, which documents own which facts, how to decide whether a change is
local or cross-language, and what evidence to collect before calling the work done.

Use it together with:

- the [workspace map](../reference/workspace-map.md) for repository layout and port lineage;
- the [data-structure catalog](../reference/data-structure-catalog.md) for public library surfaces;
- the [semantic contracts reference](../reference/semantic-contracts.md) for shared behavior to preserve;
- the [build and validation guide](build-and-validation.md) for exact commands;
- the [documentation maintenance guide](documentation-maintenance.md) for placement, metadata, and link checks.

## Repository Shape

The repository is language-first under `src/`. That keeps build systems, toolchains, and language
idioms local while preserving family-level parity across languages.

| Layer | What it answers | Primary documents |
| --- | --- | --- |
| Root | What is this repository, what workspaces exist, and how should agents work here? | [Root README](../../README.md), [AGENTS.md](../../AGENTS.md), [source index](../../src/README.md) |
| Repository guides | How do I perform recurring repository tasks? | [Guides index](README.md), [validation](build-and-validation.md), [porting](porting-and-semantic-parity.md), [documentation maintenance](documentation-maintenance.md) |
| Repository reference | What facts stay true across workspaces? | [Reference index](../reference/README.md), [workspace map](../reference/workspace-map.md), [catalog](../reference/data-structure-catalog.md), [contracts](../reference/semantic-contracts.md), [test suite map](../reference/test-suite-map.md) |
| Workspace docs | What does this library expose, how is it used, and how is it validated? | `src/<Language>/<Family>/README.md`, `src/<Language>/<Family>/docs/`, `src/CSharp/docs/<Family>/`, or package-level `src/Python/docs/`, `src/TypeScript/docs/`, and `src/OCaml/docs/` |
| Local test docs | What tests exist and which behavior do they cover? | `tests/README.md` beside the affected workspace or project |
| Migration docs | What happened during extraction or path rewriting? | [Migration index](../migration/README.md) |

When a fact belongs to multiple layers, put the full contract at the narrowest authoritative layer and
link to it from broader indexes. For example, C# FingerTree complexity promises belong in the C#
FingerTree API specification; the repository catalog should say the surface exists and point at that
specification.

### Tungsten is a leaf, not a foundation

Treat every Tungsten workspace as an application-specific consumer that may move out of this
repository or change with new Wolfram-kernel evidence. It may depend on general HAMT/FingerTree
families. No general or non-Tungsten workspace may depend on Tungsten code, types, internals, or
semantics. When reusing an attractive mechanism, create an independently owned implementation and
state its own contract, tests, dependency direction, and deliberately retained or relaxed
guarantees. Do not use Tungsten as a wrapper substrate or live test oracle. Consult the normative
[application-leaf boundary](../reference/tungsten-application-leaf-boundary.md) before generalizing
or relocating Tungsten work.

## Task Classification

Classify the task before editing. The classification determines which docs and tests are relevant.

| Task shape | Start here | Also inspect | Documentation usually touched | Validation evidence |
| --- | --- | --- | --- | --- |
| Use or explain an existing API | [Navigation matrix](../reference/navigation-matrix.md), [catalog](../reference/data-structure-catalog.md) | Usage guide, API spec or notes, source tests | Usually none unless a gap is found | None, or targeted examples if docs are updated |
| Documentation-only improvement | Affected doc index and [documentation maintenance](documentation-maintenance.md) | Source or API specs for factual claims | Indexes, guides, reference docs, affected workspace docs | Markdown link checker, stale-path scan, `git diff --check` |
| Build, test, or toolchain change | [Build and validation](build-and-validation.md) | Workspace validation guide, local tests README | Root README, validation guide, test suite map, workspace README | Changed command plus docs checks |
| Public API or semantic change | [Porting guide](porting-and-semantic-parity.md), [semantic contracts](../reference/semantic-contracts.md) | Managed API spec, sibling port docs, tests | API spec/notes, usage guide, catalog, test suite map when coverage changes | Affected workspace tests, broader port tests when parity changes |
| Internal implementation change | Workspace README and local docs | Public API docs if contracts may be affected, tests | Usually local validation guide or implementation notes only when behavior or evidence changes | Workspace tests; stress or benchmarks when the hot path changes |
| Source move or workspace addition | [Workspace map](../reference/workspace-map.md), [source index](../../src/README.md) | Build scripts, docs indexes, migration records | Root README, source index, workspace map, validation guide, catalog | Commands for moved workspace plus docs checks |
| New sample, benchmark, or test suite | [Test suite map](../reference/test-suite-map.md) | Local validation guide and README | Local tests/samples/benchmarks README, test suite map, validation guide | Runner command and any benchmark command used |
| External reference curation | External index in the owning workspace | License/provenance notes | External README and repository external-material policy if shape changes | Link check scoped to touched files |

If the task spans multiple rows, use the stricter row. A public behavior change with docs updates is
not "documentation-only"; it needs implementation evidence.

## Standard Work Loop

1. Read the root guidance first. In this repository, `AGENTS.md`, `CLAUDE.md`, and `README.md` are
   intentionally aligned entry points.
2. Check `git status --short --branch`. Preserve user changes and work with them rather than
   reverting them.
3. Use `rg` and the navigation matrix to find the owning workspace, API spec, tests, and validation
   guide.
4. Read the narrow authoritative docs before changing source or writing new claims.
5. Inspect the relevant public source or headers. Docs should describe implemented contracts, not
   hopes.
6. Make the change in the owning workspace. Update adjacent tests and examples when behavior changes.
7. Update every discovery layer that would otherwise mislead a reader: workspace README, docs index,
   catalog, navigation matrix, test suite map, and validation guide as applicable.
8. Run the narrowest validation that proves the change, then broaden when the change crosses language
   or behavior boundaries.
9. Run repository-owned Markdown checks for docs changes.
10. Review the diff for stale paths, accidental generated output, credentials, and historical text
    accidentally rewritten as current state.

## Reading Order For Common Work

For a new maintainer orienting on the repository:

1. [Root README](../../README.md)
2. [Workspace map](../reference/workspace-map.md)
3. [Source index](../../src/README.md)
4. [Navigation matrix](../reference/navigation-matrix.md)
5. The relevant language/workspace README

For a collection usage question:

1. [Data-structure catalog](../reference/data-structure-catalog.md)
2. The relevant usage guide
3. The API specification or API notes
4. Tests README for executable examples and edge cases

For a cross-language behavior change:

1. [Porting and semantic parity](porting-and-semantic-parity.md)
2. [Semantic contracts](../reference/semantic-contracts.md)
3. [Workspace map](../reference/workspace-map.md) for lineage
4. Affected API specs/notes and public source
5. [Build and validation](build-and-validation.md) and [test suite map](../reference/test-suite-map.md)

For a docs-only cleanup:

1. [Documentation maintenance](documentation-maintenance.md)
2. The affected docs index
3. [Navigation matrix](../reference/navigation-matrix.md)
4. Source/API docs needed to verify factual claims
5. Markdown link checker and stale-path scan

## Documentation Completeness Checklist

A long-lived workspace doc set should answer these questions without requiring a reader to reverse
engineer the code:

| Concern | Required documentation |
| --- | --- |
| Purpose | What family does the workspace implement, and what problem does it solve? |
| Entry points | Which public types, modules, headers, or packages should a caller import first? |
| Construction | How are empty values, singleton values, bulk values, builders, and custom policies created? |
| Updates | Which operations return new persistent versions, which mutate builders or handles, and which preserve existing versions? |
| Ordering | What order do iteration, rank, sorted, priority, interval, rope, and text operations expose? |
| Failure behavior | What happens for duplicate keys, missing keys, empty values, invalid indexes, invalid intervals, allocation failures, and callback errors? |
| Ownership | Who owns keys, values, handles, callbacks, buffers, builders, and returned references? |
| Complexity | Which operations carry contractual asymptotic or allocation promises, and which are checkpoint limitations? |
| Concurrency | Which immutable reads are safe, which handle or builder operations are not, and which publication paths are intentionally stress-tested? |
| Examples | Are there examples for nontrivial policies, measures, persistent updates, sorted/rank operations, and text navigation? |
| Validation | Which command proves the documented behavior, and where is the test coverage described? |
| Cross-links | Can a reader navigate from the workspace to the repository catalog, contracts, validation guide, and sibling ports? |

Short index pages do not need to duplicate all details, but they should explain what each local document
owns. An index that says only "API notes" and "Validation" is useful for machines; an index that says
what those documents prove is useful for maintainers.

## Cross-Workspace Rules Of Thumb

- Treat C# as the broadest semantic baseline for HAMT, FingerTree, and Numerics unless a local API
  spec explicitly states otherwise.
- Treat C# Tungsten as a baseline only for sibling Tungsten ports. General collections must fork
  useful mechanics and choose their contracts independently.
- Treat C and C++ docs as authoritative for ownership, callback, RAII, and native build details.
- Treat Haskell, Kotlin, Rust, TypeScript, Python, and OCaml docs as authoritative for idiomatic result
  shapes, persistent representation choices, and explicitly documented engine-level complexity
  differences.
- Do not infer parity from similar names. Confirm the contract in the local API notes or source.
- When a sibling port intentionally differs, document the shared semantic contract first and the local
  divergence second.
- Keep current-state docs free of old paths except in migration documents and historical reports.
- Preserve external material boundaries. Repository-owned docs may summarize external papers, but
  external snapshots keep their own license and provenance.

## Validation Evidence Format

Record evidence in final notes, commit messages, or review summaries with enough detail to be useful:

```text
Validation:
- `dotnet test .\DataStructures.sln` from `src/CSharp`: passed; proves managed libraries, XML docs, samples, and xUnit/CsCheck suites.
- Markdown link checker from `docs/guides/build-and-validation.md`: passed; proves repository-owned Markdown links resolve.
- `git diff --check`: passed; proves no whitespace errors in the staged diff.
```

For skipped validation, say why:

```text
Not run: C/C++/Rust/Haskell/Kotlin/TypeScript/Python/OCaml builds, because this change only updates repository-level Markdown indexes and does not alter commands, source, or workspace contracts.
```

Avoid saying "all tests pass" unless every relevant command actually ran. Prefer exact commands and
what each command proves.

## Final Review Before Commit

Before committing, verify:

- the branch is the intended branch;
- no user changes were reverted or staged accidentally;
- new docs have `Created (UTC)` and `Repository HEAD` metadata;
- active docs describe current paths and current behavior;
- historical docs remain historical;
- repository-owned Markdown links resolve;
- indexes point to every new long-lived document;
- validation evidence matches the actual blast radius.
