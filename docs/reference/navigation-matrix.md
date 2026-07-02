# Documentation Navigation Matrix

- Created (UTC): 2026-07-02T20:20:21Z
- Repository HEAD: 30fc6f5b883a4771b7102f5c8c60d483075d7372
- Audience: Maintainers and AI agents choosing the right repository document for a task
- Scope: Task-oriented entry points across repository-level docs and workspace-owned docs

This matrix is the "where do I start?" layer. It does not replace the workspace documentation; it
routes a reader to the narrowest document that owns the question.

## Start By Task

| Task | Open first | Then inspect |
| --- | --- | --- |
| Understand repository layout | [Workspace map](workspace-map.md) | Root [README](../../README.md), [data-structure catalog](data-structure-catalog.md) |
| Choose a data structure across languages | [Data-structure catalog](data-structure-catalog.md) | The matching workspace usage guide and API specification or notes |
| Use an existing collection | The relevant usage guide below | Source tests for executable examples when behavior is subtle |
| Build or validate a workspace | [Build and validation](../guides/build-and-validation.md) | Workspace validation docs and workspace README |
| Change public API or semantics | [Porting and semantic parity](../guides/porting-and-semantic-parity.md) | API specs/notes for all affected language workspaces, catalog rows, tests |
| Update documentation | [Documentation maintenance](../guides/documentation-maintenance.md) | This matrix, the affected workspace docs index, and the catalog |
| Investigate extraction history | [Migration provenance](../migration/extraction-provenance.md) | [Filter-repo commit map](../migration/filter-repo-commit-map.tsv) |
| Run or interpret benchmarks | [C# FingerTree benchmark notes](../../src/CSharp/FingerTree/docs/benchmarks.md) | Benchmark project [README](../../src/CSharp/FingerTree/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md), root benchmark summary |
| Inspect persistence/concurrency patterns | [C# FingerTree persistence and concurrency](../../src/CSharp/FingerTree/docs/persistence-and-concurrency.md) | Corresponding native usage/API notes for C and C++ ports |
| Review C++ FingerTree port history | [C++ implementation notes](../../src/Cpp/FingerTree/docs/implementation-notes.md) | Port plan, editorial notes, independent review reports |

## Usage Guides

Use these for first-use examples, facade selection, ownership/lifetime rules, and common update
patterns.

| Workspace | Usage guide | Best for |
| --- | --- | --- |
| C# HAMT | [src/CSharp/Hamt/docs/usage.md](../../src/CSharp/Hamt/docs/usage.md) | `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>` construction, comparers, persistent updates, set algebra |
| C HAMT | [src/C/Hamt/docs/usage.md](../../src/C/Hamt/docs/usage.md) | `tds_hamt_map` / `tds_hamt_set` policies, borrowed versus owned pointers, status/cleanup patterns |
| C++ HAMT | [src/Cpp/Hamt/docs/usage.md](../../src/Cpp/Hamt/docs/usage.md) | Header inclusion, value semantics, custom hash/equality policy objects, set algebra |
| C# FingerTree | [src/CSharp/FingerTree/docs/usage.md](../../src/CSharp/FingerTree/docs/usage.md) | Deques, reversible deques, sorted collections, priority queues, intervals, ropes/text, raw measured trees |
| C FingerTree | [src/C/FingerTree/docs/usage.md](../../src/C/FingerTree/docs/usage.md) | C handle lifetime, policy setup, persistent updates, facades, text ropes |
| C++ FingerTree | [src/Cpp/FingerTree/docs/usage.md](../../src/Cpp/FingerTree/docs/usage.md) | Aggregate include path, value semantics, persistent deque/tree facades, ropes/text, publication patterns |

## API Contracts

Use these when behavior, complexity, allocation, ownership, or cross-language parity matters.

| Workspace | Contract document | Notes |
| --- | --- | --- |
| C# HAMT | [API specification](../../src/CSharp/Hamt/docs/api-specification.md) | Normative C# HAMT map/set contract |
| C HAMT | [API specification](../../src/C/Hamt/docs/api-specification.md) | C API ownership, callback policy, and complexity contract |
| C++ HAMT | [API specification](../../src/Cpp/Hamt/docs/api-specification.md) | C++ template API and C# parity notes |
| C# FingerTree | [API specification](../../src/CSharp/FingerTree/docs/api-specification.md) | Deque contract plus measured-tree, reversible-deque, rope, and related surface notes |
| C FingerTree | [API notes](../../src/C/FingerTree/docs/api-notes.md) | C API shape, ownership rules, and C++ port differences |
| C++ FingerTree | [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) | C++ conventions and active differences from the C# workspace |

## Validation Entry Points

| Scope | Document | What it proves |
| --- | --- | --- |
| Whole repository | [Build and validation](../guides/build-and-validation.md) | Canonical commands for C#, C, C++, CMake presets, and Markdown checks |
| C HAMT | [Validation](../../src/C/Hamt/docs/validation.md) | MSVC C17 build script, Debug/Release commands, warning policy, and native model tests |
| C++ HAMT | [Validation](../../src/Cpp/Hamt/docs/validation.md) | MSVC C++20 build script, Debug/Release commands, warning policy, and native model tests |
| C FingerTree | [Validation](../../src/C/FingerTree/docs/validation.md) | CMake build, CTest validation, sample smoke tests, and benchmark harness entry points |
| C++ FingerTree | [Validation](../../src/Cpp/FingerTree/docs/validation.md) | CMake build, CTest validation, stress controls, and benchmark entry points |
| C# FingerTree benchmarks | [Benchmark notes](../../src/CSharp/FingerTree/docs/benchmarks.md) | Curated BenchmarkDotNet results and interpretation |
| C# FingerTree samples | [Samples README](../../src/CSharp/FingerTree/samples/README.md) | Runnable tours covering text, measured-tree facades, and editor-grade text extras |

## Historical And External Material

| Material | Location | Use |
| --- | --- | --- |
| Repository extraction record | [docs/migration](../migration/extraction-provenance.md) | Preserve source-repo, filter-repo, and commit-map provenance |
| C++ FingerTree port reports | [src/Cpp/FingerTree/docs](../../src/Cpp/FingerTree/docs/README.md) | Retain independent review findings and correction context |
| C# FingerTree external references | [external index](../../src/CSharp/FingerTree/docs/external/README.md) | Study source papers and snapshots; not repository-owned license material |
| C# FingerTree design notes | [PDF](../../src/CSharp/FingerTree/docs/FingerTree-Design-Notes.pdf) / [TeX](../../src/CSharp/FingerTree/docs/FingerTree-Design-Notes.tex) | Architecture, algorithms, concurrency, and test-strategy tour |

## Maintenance Rule

When adding or moving a long-lived document, update every layer that helps a reader find it:

- the affected workspace `docs/README.md`;
- the affected workspace `README.md` when it changes the local entry points;
- [docs/README.md](../README.md) or [reference/README.md](README.md) when it is repository-level;
- [data-structure-catalog.md](data-structure-catalog.md) when it changes a public data-structure surface;
- this matrix when it becomes a better first stop for a task.
