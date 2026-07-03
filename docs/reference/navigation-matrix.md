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
| Understand repository layout | [Workspace map](workspace-map.md) | Root [README](../../README.md), [source index](../../src/README.md), [data-structure catalog](data-structure-catalog.md) |
| Browse workspaces by language | [Source index](../../src/README.md) | [C](../../src/C/README.md), [C++](../../src/Cpp/README.md), [C#](../../src/CSharp/README.md), [Haskell](../../src/Haskell/README.md), [Kotlin](../../src/Kotlin/README.md), [Rust](../../src/Rust/README.md) |
| Choose a data structure or numerics surface across languages | [Data-structure catalog](data-structure-catalog.md) | The matching workspace usage guide and API specification or notes |
| Plan a new derived structure or API extension | [Derived structure catalog](derived-structure-catalog.md) | [Porting and semantic parity](../guides/porting-and-semantic-parity.md), affected workspace API specs |
| Use an existing collection | The relevant usage guide below | Source tests for executable examples when behavior is subtle |
| Build or validate a workspace | [Build and validation](../guides/build-and-validation.md) | [Test suite map](test-suite-map.md), workspace validation docs, and workspace README |
| Understand test coverage | [Test suite map](test-suite-map.md) | Workspace tests README and validation guide |
| Change public API or semantics | [Porting and semantic parity](../guides/porting-and-semantic-parity.md) | API specs/notes for all affected language workspaces, catalog rows, tests |
| Update documentation | [Documentation maintenance](../guides/documentation-maintenance.md) | This matrix, the affected workspace docs index, and the catalog |
| Investigate extraction history | [Migration index](../migration/README.md) | [Extraction provenance](../migration/extraction-provenance.md), [filter-repo commit map](../migration/filter-repo-commit-map.tsv) |
| Translate a pre-`src` workspace path | [Language-first reorganization](../migration/language-first-reorganization.md) | [Workspace map](workspace-map.md), [source index](../../src/README.md), current validation docs |
| Run or interpret benchmarks | [C# FingerTree benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) | Benchmark project [README](../../src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md), root benchmark summary |
| Inspect persistence/concurrency patterns | [C# FingerTree persistence and concurrency](../../src/CSharp/docs/FingerTree/persistence-and-concurrency.md) | Corresponding native usage/API notes for C and C++ ports |
| Review C++ FingerTree port history | [C++ implementation notes](../../src/Cpp/FingerTree/docs/implementation-notes.md) | Port plan, editorial notes, independent review reports |

## Usage Guides

Use these for first-use examples, facade selection, ownership/lifetime rules, and common update
patterns.

| Workspace | Usage guide | Best for |
| --- | --- | --- |
| C# Numerics | [src/CSharp/docs/Numerics/overview.md](../../src/CSharp/docs/Numerics/overview.md) | Fixed-width integer types, sparse integers, binary conversion, and wide-integer maintenance entry points |
| C# HAMT | [src/CSharp/docs/Hamt/usage.md](../../src/CSharp/docs/Hamt/usage.md) | `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>` construction, comparers, persistent updates, set algebra |
| C HAMT | [src/C/Hamt/docs/usage.md](../../src/C/Hamt/docs/usage.md) | `tds_hamt_map` / `tds_hamt_set` policies, borrowed versus owned pointers, status/cleanup patterns |
| C++ HAMT | [src/Cpp/Hamt/docs/usage.md](../../src/Cpp/Hamt/docs/usage.md) | Header inclusion, value semantics, custom hash/equality policy objects, set algebra |
| Haskell HAMT | [src/Haskell/Hamt/README.md](../../src/Haskell/Hamt/README.md) | `HashMap`, `HashSet`, `HashPolicy`, package-local `Hashable`, and cabal validation |
| Kotlin HAMT | [src/Kotlin/Hamt/docs/api-notes.md](../../src/Kotlin/Hamt/docs/api-notes.md) | `PersistentHashMap`, `PersistentHashSet`, runtime `HashPolicy`, and executable validation |
| Rust HAMT | [src/Rust/Hamt/docs/api-notes.md](../../src/Rust/Hamt/docs/api-notes.md) | `PersistentHashMap`, `PersistentHashSet`, hash policies, and Cargo validation |
| C# FingerTree | [src/CSharp/docs/FingerTree/usage.md](../../src/CSharp/docs/FingerTree/usage.md) | Deques, reversible deques, sorted collections, priority queues, intervals, ropes/text, raw measured trees |
| C FingerTree | [src/C/FingerTree/docs/usage.md](../../src/C/FingerTree/docs/usage.md) | C handle lifetime, policy setup, persistent updates, facades, text ropes |
| C++ FingerTree | [src/Cpp/FingerTree/docs/usage.md](../../src/Cpp/FingerTree/docs/usage.md) | Aggregate include path, value semantics, persistent deque/tree facades, ropes/text, publication patterns |
| Haskell FingerTree | [src/Haskell/FingerTree/README.md](../../src/Haskell/FingerTree/README.md) | General measured tree, deque, reversible deque, sorted facades, priority queue, intervals, ropes, and text helpers |
| Kotlin FingerTree | [src/Kotlin/FingerTree/docs/api-notes.md](../../src/Kotlin/FingerTree/docs/api-notes.md) | Kotlin semantic-checkpoint surfaces for deque, measured sequence, sorted collections, priority queue, intervals, ropes/text |
| Rust FingerTree | [src/Rust/FingerTree/docs/api-notes.md](../../src/Rust/FingerTree/docs/api-notes.md) | Rust shared-storage checkpoint surfaces for deque, measured sequence, sorted collections, priority queue, intervals, ropes/text |

## API Contracts

Use these when behavior, complexity, allocation, ownership, or cross-language parity matters.

| Workspace | Contract document | Notes |
| --- | --- | --- |
| C# Numerics | [API and behavior reference](../../src/CSharp/docs/Numerics/api-and-behavior-reference.md) | Normative fixed-width integer behavior, conversion, parse/format, and binary representation contract |
| C# HAMT | [API specification](../../src/CSharp/docs/Hamt/api-specification.md) | Normative C# HAMT map/set contract |
| C HAMT | [API specification](../../src/C/Hamt/docs/api-specification.md) | C API ownership, callback policy, and complexity contract |
| C++ HAMT | [API specification](../../src/Cpp/Hamt/docs/api-specification.md) | C++ template API and C# parity notes |
| Haskell HAMT | [Workspace README](../../src/Haskell/Hamt/README.md) and [source](../../src/Haskell/Hamt/src/Data/Structures/Hamt/HashMap.hs) | Haskell HAMT map/set API shape |
| Kotlin HAMT | [API notes](../../src/Kotlin/Hamt/docs/api-notes.md) and [source](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/PersistentHamt.kt) | Kotlin HAMT map/set API shape, runtime policy, and root-sharing diagnostics |
| Rust HAMT | [API notes](../../src/Rust/Hamt/docs/api-notes.md) and [source](../../src/Rust/Hamt/src/lib.rs) | Rust value API, `BuildHasher`, `Arc` sharing, and trie-order iteration |
| C# FingerTree | [API specification](../../src/CSharp/docs/FingerTree/api-specification.md) | Deque contract plus measured-tree, reversible-deque, rope, and related surface notes |
| C FingerTree | [API notes](../../src/C/FingerTree/docs/api-notes.md) | C API shape, ownership rules, and C++ port differences |
| C++ FingerTree | [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) | C++ conventions and active differences from the C# workspace |
| Haskell FingerTree | [Workspace README](../../src/Haskell/FingerTree/README.md) and [source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measured.hs) | Haskell measured tree and derived collection API shape |
| Kotlin FingerTree | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md) and [source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/Core.kt) | Kotlin measured tree and derived collection API shape plus semantic-checkpoint boundary |
| Rust FingerTree | [API notes](../../src/Rust/FingerTree/docs/api-notes.md) and [source](../../src/Rust/FingerTree/src/measured.rs) | Rust measured tree and derived collection API shape |

## Validation Entry Points

| Scope | Document | What it proves |
| --- | --- | --- |
| Whole repository | [Build and validation](../guides/build-and-validation.md) / [Test suite map](test-suite-map.md) | Canonical commands for C#, C, C++, CMake presets, Markdown checks, and test-suite entry points |
| C# Numerics | [Validation](../../src/CSharp/docs/Numerics/validation.md) | .NET restore/build/test commands, XML-documentation warning gate, and xUnit wide-integer coverage |
| C# HAMT | [Validation](../../src/CSharp/docs/Hamt/validation.md) | .NET restore/build/test commands, XML-documentation warning gate, and xUnit/CsCheck coverage |
| C HAMT | [Validation](../../src/C/Hamt/docs/validation.md) | MSVC C17 build script, Debug/Release commands, warning policy, and native model tests |
| C++ HAMT | [Validation](../../src/Cpp/Hamt/docs/validation.md) | MSVC C++20 build script, Debug/Release commands, warning policy, and native model tests |
| C# FingerTree | [Validation](../../src/CSharp/docs/FingerTree/validation.md) | .NET restore/build/test commands, sample smoke coverage, benchmark boundary, stress controls, and xUnit/CsCheck coverage |
| C FingerTree | [Validation](../../src/C/FingerTree/docs/validation.md) | CMake build, CTest validation, sample smoke tests, and benchmark harness entry points |
| C++ FingerTree | [Validation](../../src/Cpp/FingerTree/docs/validation.md) | CMake build, CTest validation, stress controls, and benchmark-harness status |
| Haskell | [Workspace README](../../src/Haskell/README.md) | `cabal test all` builds both Haskell packages and runs the HAMT/FingerTree executables |
| Kotlin | [Workspace README](../../src/Kotlin/README.md) | `.\build.ps1` builds both Kotlin workspaces and runs dependency-free executable tests |
| Kotlin HAMT | [Validation](../../src/Kotlin/Hamt/docs/validation.md) | Kotlin compiler bootstrap and deterministic HAMT executable tests |
| Kotlin FingerTree | [Validation](../../src/Kotlin/FingerTree/docs/validation.md) | Kotlin compiler bootstrap and semantic-checkpoint executable tests across collection facades |
| Rust | [Workspace README](../../src/Rust/README.md) | `cargo test --workspace` builds both Rust crates and runs unit/doc tests |
| Rust FingerTree | [Validation](../../src/Rust/FingerTree/docs/validation.md) | Cargo unit tests for shared storage and checkpoint semantics across deque, measured sequence, sorted, priority, interval, rope, and text helpers |
| C# FingerTree benchmarks | [Benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) | Curated BenchmarkDotNet results and interpretation |
| C# FingerTree samples | [Samples README](../../src/CSharp/samples/README.md) | Runnable tours covering text, measured-tree facades, and editor-grade text extras |
| C# Numerics tests | [Tests README](../../src/CSharp/tests/Tools.Numerics.Tests/README.md) | xUnit project covering fixed-width integer behavior, binary conversion, public API coverage, and declaration parity |
| C# HAMT tests | [Tests README](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests/README.md) | xUnit/CsCheck project, source-file grouping, filter commands, and property coverage |
| C# FingerTree tests | [Tests README](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md) | xUnit/CsCheck project, source-file grouping, sample smoke hooks, stress controls, and model tests |
| C HAMT tests | [Tests README](../../src/C/Hamt/tests/README.md) | C native HAMT executable, named test cases, direct executable path, and runner failure behavior |
| C++ HAMT tests | [Tests README](../../src/Cpp/Hamt/tests/README.md) | C++ native HAMT executable, named test cases, direct executable path, and runner failure behavior |
| C FingerTree tests | [Tests README](../../src/C/FingerTree/tests/README.md) | Core CTest executable, named test cases, direct executable path, and runner failure behavior |
| C FingerTree samples | [Samples README](../../src/C/FingerTree/samples/README.md) | Deterministic C sample executables and CTest smoke-test names |
| C FingerTree benchmarks | [Benchmarks README](../../src/C/FingerTree/benchmarks/README.md) | Dependency-light timing harness workloads and output shape |
| C++ FingerTree tests | [Tests README](../../src/Cpp/FingerTree/tests/README.md) | Native smoke runner source map, direct executable path, and tearable stress controls |
| Haskell HAMT tests | [Tests README](../../src/Haskell/Hamt/test/README.md) | Cabal executable covering collision buckets, custom policies, key recovery, and set algebra |
| Haskell FingerTree tests | [Tests README](../../src/Haskell/FingerTree/test/README.md) | Cabal executable covering measured tree, facades, intervals, ropes, and text helpers |
| Kotlin HAMT tests | [Tests README](../../src/Kotlin/Hamt/tests/README.md) | Kotlin executable covering collisions, root sharing, replacement, iteration, and set algebra |
| Kotlin FingerTree tests | [Tests README](../../src/Kotlin/FingerTree/tests/README.md) | Kotlin executable covering deque, measured tree, facades, intervals, ropes, and text helpers |
| Rust HAMT tests | [Tests README](../../src/Rust/Hamt/tests/README.md) | Cargo unit tests covering collisions, updates, iteration, and set algebra |
| Rust FingerTree tests | [Tests README](../../src/Rust/FingerTree/tests/README.md) | Cargo unit tests covering deque, measured tree, facades, intervals, ropes, and text helpers |

## Historical And External Material

| Material | Location | Use |
| --- | --- | --- |
| Repository extraction record | [docs/migration](../migration/README.md) | Preserve source-repo, filter-repo, and commit-map provenance |
| C++ FingerTree port reports | [src/Cpp/FingerTree/docs](../../src/Cpp/FingerTree/docs/README.md) | Retain independent review findings and correction context |
| C# FingerTree external references | [external index](../../src/CSharp/docs/FingerTree/external/README.md) | Study source papers and snapshots; not repository-owned license material |
| C# FingerTree design notes | [PDF](../../src/CSharp/docs/FingerTree/FingerTree-Design-Notes.pdf) / [TeX](../../src/CSharp/docs/FingerTree/FingerTree-Design-Notes.tex) | Architecture, algorithms, concurrency, and test-strategy tour |

## Maintenance Rule

When adding or moving a long-lived document, update every layer that helps a reader find it:

- the affected workspace `docs/README.md`;
- the affected workspace `README.md` when it changes the local entry points;
- [docs/README.md](../README.md) or [reference/README.md](README.md) when it is repository-level;
- [data-structure-catalog.md](data-structure-catalog.md) when it changes a public data-structure surface;
- this matrix when it becomes a better first stop for a task.
