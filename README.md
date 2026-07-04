# DataStructures repository

- Status: Active standalone repository
- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI coding agents working on repository-owned data structures and numerics
- Scope: Repository layout, build entry points, and agent guidance

This repository contains Vladimir Reshetnikov's standalone data-structure and numerics workspaces and design references. It was extracted from `C:\Tools0\src\DataStructures` / `VladimirReshetnikov/Tools` with path-local Git history preserved as precisely as practical. The Tools-side handoff is recorded by [`5fc4054da`](https://github.com/VladimirReshetnikov/Tools/commit/5fc4054da), which removes the former subtree and points the Tools indexes here.

This document is the canonical repository guidance for Vladimir and the AI coding agents that help him. `AGENTS.md` and `CLAUDE.md` point here, so keep shared project and agent instructions in this file.

## Where to start

| Goal | Start with | Then open |
| --- | --- | --- |
| Get oriented in the repository | [Repository onboarding](docs/guides/repository-onboarding.md) | [Workspace map](docs/reference/workspace-map.md), [source index](src/README.md), [navigation matrix](docs/reference/navigation-matrix.md) |
| Choose or compare a collection family | [Data-structure catalog](docs/reference/data-structure-catalog.md) | Relevant workspace usage guide and API specification or notes |
| Preserve behavior across ports | [Semantic contracts](docs/reference/semantic-contracts.md) | [Porting guide](docs/guides/porting-and-semantic-parity.md), sibling workspace API docs, test READMEs |
| Build or validate changes | [Build and validation](docs/guides/build-and-validation.md) | [Test suite map](docs/reference/test-suite-map.md), affected workspace validation guide |
| Update documentation | [Documentation maintenance](docs/guides/documentation-maintenance.md) | Affected docs index, [navigation matrix](docs/reference/navigation-matrix.md), repository-owned Markdown checks |

## Top-level structure

```text
.
├── .editorconfig
├── .gitattributes
├── .gitignore
├── AGENTS.md
├── CLAUDE.md
├── LICENSE
├── PREFERENCES.md
├── README.md
├── docs/
│   ├── README.md
│   ├── guides/
│   ├── reference/
│   └── migration/
└── src/
    ├── README.md
    ├── C/
    │   ├── README.md
    │   ├── build.ps1
    │   ├── Hamt/
    │   │   ├── build.ps1
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── include/
    │   │   ├── src/
    │   │   └── tests/
    │   └── FingerTree/
    │       ├── CMakeLists.txt
    │       ├── CMakePresets.json
    │       ├── README.md
    │       ├── benchmarks/
    │       ├── docs/
    │       ├── include/
    │       ├── samples/
    │       ├── src/
    │       └── tests/
    ├── Cpp/
    │   ├── README.md
    │   ├── build.ps1
    │   ├── Hamt/
    │   │   ├── build.ps1
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── include/
    │   │   └── tests/
    │   └── FingerTree/
    │       ├── CMakeLists.txt
    │       ├── CMakePresets.json
    │       ├── README.md
    │       ├── docs/
    │       ├── include/
    │       └── tests/
    ├── CSharp/
    │   ├── README.md
    │   ├── DataStructures.sln
    │   ├── Directory.Build.props
    │   ├── benchmarks/
    │   │   └── Tools.DataStructures.FingerTree.Benchmarks/
    │   ├── docs/
    │   │   ├── FingerTree/
    │   │   ├── Hamt/
    │   │   └── Numerics/
    │   ├── samples/
    │   │   ├── Tools.DataStructures.FingerTree.Editor/
    │   │   ├── Tools.DataStructures.FingerTree.Showcase/
    │   │   └── Tools.DataStructures.FingerTree.Tour/
    │   ├── src/
    │   │   ├── Tools.DataStructures.FingerTree/
    │   │   ├── Tools.DataStructures.Hamt/
    │   │   └── Tools.Numerics/
    │   └── tests/
    │       ├── Tools.DataStructures.FingerTree.Tests/
    │       ├── Tools.DataStructures.Hamt.Tests/
    │       └── Tools.Numerics.Tests/
    ├── Haskell/
    │   ├── README.md
    │   ├── cabal.project
    │   ├── Hamt/
    │   │   ├── README.md
    │   │   ├── tools-data-structures-hamt.cabal
    │   │   ├── src/
    │   │   └── test/
    │   └── FingerTree/
    │       ├── README.md
    │       ├── tools-data-structures-fingertree.cabal
    │       ├── src/
    │       └── test/
    ├── Kotlin/
    │   ├── README.md
    │   ├── build.ps1
    │   ├── Hamt/
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── src/
    │   │   ├── test/
    │   │   └── tests/
    │   └── FingerTree/
    │       ├── README.md
    │       ├── docs/
    │       ├── src/
    │       ├── test/
    │       └── tests/
    └── Rust/
        ├── Cargo.toml
        ├── README.md
        ├── Hamt/
        │   ├── Cargo.toml
        │   ├── README.md
        │   ├── docs/
        │   ├── src/
        │   └── tests/
        └── FingerTree/
            ├── Cargo.toml
            ├── README.md
            ├── docs/
            ├── src/
            └── tests/
```

## Workspaces

The [source index](src/README.md) and language indexes for [C](src/C/README.md),
[C++](src/Cpp/README.md), [C#](src/CSharp/README.md), [Haskell](src/Haskell/README.md),
[Kotlin](src/Kotlin/README.md), and [Rust](src/Rust/README.md) are the quickest way to browse the
language-first layout.

- [C# Numerics](src/CSharp/docs/Numerics/overview.md) is a .NET 10 fixed-width and sparse integer numerics library under [src/CSharp/src/Tools.Numerics](src/CSharp/src/Tools.Numerics/Tools.Numerics.csproj). It provides `UInt256`/`Int256`, `UInt512`/`Int512`, `UInt1024`/`Int1024`, `SparseInteger`, deterministic two's-complement and binary conversion semantics, declaration-parity guardrails, and xUnit tests.
- [C# HAMT](src/CSharp/docs/Hamt/overview.md) is a .NET 10 persistent hash-array mapped trie library under [src/CSharp/src/Tools.DataStructures.Hamt](src/CSharp/src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj). It provides `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>` with bitmap-indexed 32-way branching, immutable equal-hash collision buckets, comparer-preserving factories, structural sharing across versions, and xUnit/CsCheck model tests against BCL dictionaries and sets.
- [C# FingerTree](src/CSharp/docs/FingerTree/overview.md) is a .NET 10 persistent finger-tree library under [src/CSharp/src/Tools.DataStructures.FingerTree](src/CSharp/src/Tools.DataStructures.FingerTree/Tools.DataStructures.FingerTree.csproj): two engine cores (a tuned catenable deque and a general monoid-measured tree), a full collection family (sorted bag/set/dictionary, priority queue, interval tree, reversible deque), product/sum/built-in measures with a closure-free predicate API, and a rope family (positional, measured, and text). It ships a navigable design-notes document ([FingerTree-Design-Notes.pdf](src/CSharp/docs/FingerTree/FingerTree-Design-Notes.pdf), with `.tex` source and a rebuild script alongside), a BenchmarkDotNet harness, three runnable samples, and a three-tier (example + property + model-based command) test suite plus tearable-struct concurrency stress tests.
- [src/C/Hamt](src/C/Hamt/README.md) is a C17 port of the persistent HAMT library. It provides type-erased
  `tds_hamt_map` and `tds_hamt_set` value structs with callback-driven hash/equality/ownership
  policy, reference-counted immutable nodes, structural sharing across versions, and deterministic
  native model tests.
- [src/C/FingerTree](src/C/FingerTree/README.md) is the C11 port from the C++ workspace. It provides a generic measured-tree core with shared lazy middle publication, size-measured deque alias, reversible deque facade, sorted set/multiset/map wrappers, generic priority queue, generic and signed-64-bit interval tree facades, generic chunked and measured ropes, text-rope facade, CTest validation, sample smoke tests, and a dependency-light benchmark harness.
- [src/Cpp/Hamt](src/Cpp/Hamt/README.md) is a C++20 port of the persistent HAMT library. It provides
  header-only `persistent_hash_map` and `persistent_hash_set` templates with bitmap-indexed
  branching, immutable equal-hash collision buckets, custom hash/equality policy objects, structural
  sharing via immutable `std::shared_ptr` nodes, and deterministic native model tests.
- [src/Cpp/FingerTree](src/Cpp/FingerTree/README.md) is the native C++ port of the FingerTree workspace. It is a header-first CMake/Ninja library with the two engine cores, derived collections, ropes, text helpers, and CTest validation.
- [src/Haskell/Hamt](src/Haskell/Hamt/README.md) is a Haskell port of the persistent HAMT library. It provides `HashMap` and `HashSet` values with bitmap-indexed 32-way branching, immutable collision buckets, policy-preserving factories, structural sharing, and dependency-free cabal tests.
- [src/Haskell/FingerTree](src/Haskell/FingerTree/README.md) is a Haskell port of the FingerTree family. It provides a general measured tree, size-measured deque, reversible deque, sorted bag/set/map facades, stable meldable priority queue, interval tree, positional and measured ropes, and text helpers.
- [src/Kotlin/Hamt](src/Kotlin/Hamt/README.md) is a Kotlin/JVM port of the persistent HAMT library. It provides `PersistentHashMap<K, V>` and `PersistentHashSet<T>` values with bitmap-indexed 32-way branching, immutable collision buckets, runtime hash/equality policies, structural sharing, and dependency-free executable tests.
- [src/Kotlin/FingerTree](src/Kotlin/FingerTree/README.md) is a Kotlin/JVM semantic-checkpoint port of the FingerTree family. It exposes persistent deque, measured sequence, reversible deque, sorted bag/set/map, priority queue, interval tree, positional and measured ropes, and text helpers with immutable snapshot semantics.
- [src/Rust/Hamt](src/Rust/Hamt/README.md) is a safe Rust persistent HAMT map/set crate. It provides
  `PersistentHashMap` and `PersistentHashSet` with bitmap-indexed trie nodes, immutable collision
  buckets, `Arc` structural sharing, `BuildHasher` hash policy support, and Cargo unit tests.
- [src/Rust/FingerTree](src/Rust/FingerTree/README.md) is the Rust checkpoint port of the FingerTree
  family. It exposes persistent deque, measured sequence, reversible deque, sorted bag/set/map,
  built-in/product measure policies, priority queue, interval tree, rope, measured rope, and text
  helpers with immutable snapshot semantics. The public FingerTree-family facades now use
  structurally shared Rust tree storage; the current crate documents the remaining
  semantic-checkpoint boundary before final lazy measured-spine asymptotic parity.

## Build and test

Use [docs/guides/build-and-validation.md](docs/guides/build-and-validation.md) as the complete validation guide. In short, use the local .NET SDK toolchain for the C# workspace, the language-root MSVC build wrappers for C and C++, cabal for the Haskell packages, and Cargo for the Rust crates.

```powershell
cd C:\DataStructures\src\CSharp
dotnet restore
dotnet build
dotnet test .\DataStructures.sln

cd C:\DataStructures\src\C
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace Hamt -Configuration Release -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace Hamt -Configuration Release -RunTests

cd C:\DataStructures\src\Rust
cargo test --workspace

cd C:\DataStructures\src\C
.\build.ps1 -Workspace FingerTree -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace FingerTree -RunTests

cd C:\DataStructures\src\Haskell
cabal test all

cd C:\DataStructures\src\Kotlin
.\build.ps1
```

Run benchmarks from the benchmark project:

```powershell
cd C:\DataStructures\src\CSharp\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Documentation

- [docs/README.md](docs/README.md) indexes repository-level documentation and migration provenance.
- [src/README.md](src/README.md) indexes language-level source workspaces.
- [docs/guides/README.md](docs/guides/README.md) indexes task-oriented repository procedures.
- [docs/guides/repository-onboarding.md](docs/guides/repository-onboarding.md) is the end-to-end orientation guide for choosing workspaces, task scope, documentation responsibilities, and validation evidence.
- [docs/guides/agent-workflows.md](docs/guides/agent-workflows.md) holds compact task-conditional workflow guidance.
- [docs/guides/build-and-validation.md](docs/guides/build-and-validation.md) is the repository-wide validation matrix and command guide.
- [docs/guides/documentation-maintenance.md](docs/guides/documentation-maintenance.md) defines documentation placement, writing standards, metadata, and validation.
- [docs/guides/porting-and-semantic-parity.md](docs/guides/porting-and-semantic-parity.md) defines the workflow for keeping C#, C++, C, Haskell, Kotlin, and Rust data-structure surfaces semantically aligned.
- [docs/reference/README.md](docs/reference/README.md) indexes durable cross-workspace reference material.
- [docs/reference/data-structure-catalog.md](docs/reference/data-structure-catalog.md) catalogs repository-owned data-structure families, public entry points, and primary references across C#, C, C++, Haskell, Kotlin, and Rust.
- [docs/reference/navigation-matrix.md](docs/reference/navigation-matrix.md) maps common tasks to the right usage, API, validation, porting, history, and maintenance documents.
- [docs/reference/semantic-contracts.md](docs/reference/semantic-contracts.md) summarizes shared behavior, ownership, policy, ordering, and documentation obligations for repository-owned numerics and data structures.
- [docs/reference/workspace-map.md](docs/reference/workspace-map.md) explains the language-first, library-family layout and port lineage.
- [src/CSharp/docs/Numerics/README.md](src/CSharp/docs/Numerics/README.md) indexes the Numerics library's API and behavior reference, validation guide, maintainer guidance, and design notes.
- [src/CSharp/docs/Hamt/README.md](src/CSharp/docs/Hamt/README.md) indexes the HAMT library's usage guide, API specification, validation guide, and implementation review.
- [src/C/Hamt/docs/README.md](src/C/Hamt/docs/README.md) indexes the C HAMT port's usage guide, API specification, and validation guide.
- [src/Cpp/Hamt/docs/README.md](src/Cpp/Hamt/docs/README.md) indexes the C++ HAMT port's usage
  guide, API specification, and validation guide.
- [src/CSharp/docs/FingerTree/README.md](src/CSharp/docs/FingerTree/README.md) indexes the library's usage guide, specifications, validation guide, design notes, benchmark notes, and external references.
- [src/Cpp/FingerTree/docs/README.md](src/Cpp/FingerTree/docs/README.md) indexes the C++ usage guide, port plan, API notes, validation guide, implementation notes, and review reports.
- [src/C/FingerTree/docs/README.md](src/C/FingerTree/docs/README.md) indexes the C usage guide, API notes, and validation guide.
- [src/Haskell/README.md](src/Haskell/README.md) indexes the Haskell cabal packages.
- [src/Haskell/Hamt/test/README.md](src/Haskell/Hamt/test/README.md) and [src/Haskell/FingerTree/test/README.md](src/Haskell/FingerTree/test/README.md) summarize the Haskell executable test coverage.
- [src/Kotlin/README.md](src/Kotlin/README.md) indexes the Kotlin/JVM workspaces.
- [src/Kotlin/Hamt/docs/README.md](src/Kotlin/Hamt/docs/README.md) indexes the Kotlin HAMT port's API notes and validation guide.
- [src/Kotlin/FingerTree/docs/README.md](src/Kotlin/FingerTree/docs/README.md) indexes the Kotlin FingerTree-family API notes and validation guide.
- [src/Rust/Hamt/docs/README.md](src/Rust/Hamt/docs/README.md) indexes the Rust HAMT port's API notes
  and validation guide.
- [src/Rust/FingerTree/docs/README.md](src/Rust/FingerTree/docs/README.md) indexes the Rust
  FingerTree-family API notes and validation guide.

The large `TECHNICAL_DOCUMENTATION_STANDARD.md` and `XML_DOCUMENTATION_STANDARD.md` files from Tools are intentionally not part of this repository. Keep documentation thorough and current-state oriented, and write XML documentation in semantic terms: contracts, invariants, ordering, failure behavior, complexity, allocation behavior, and examples where they help.

Every new long-lived document should include header provenance metadata:

```markdown
- Created (UTC): YYYY-MM-DDTHH:MM:SSZ
- Repository HEAD: <40-hex-sha>
```

Use `git rev-parse HEAD` for the repository HEAD. When creating reports where filename collisions are likely, append a `__xxxxxxxxxxxx` suffix using 12 lowercase hex digits from a content hash.

## External reference material

Files under [src/CSharp/docs/FingerTree/external](src/CSharp/docs/FingerTree/external/README.md) are external, pre-existing study material. They are not authored by this project and are not covered by this repository's MIT-0 license; each item keeps its own copyright and license.

## Local environment

The expected local Windows environment includes:

- `pwsh` / PowerShell 7.
- `rg` for repository search.
- `git` and `gh` for source-control and GitHub workflows.
- `python` for ad hoc tooling.
- .NET SDK 10.0 or newer with the .NET 10 targeting packs.
- Visual Studio native C/C++ toolchain, including C++23 `/std:c++latest` support for `src/Cpp/FingerTree`, plus the bundled CMake and Ninja used by the `src/C/FingerTree` and `src/Cpp/FingerTree` presets.
- LLVM/Clang for native portability validation. The local Windows installation normally exposes
  `C:\Program Files\LLVM\bin\clang.exe` and `C:\Program Files\LLVM\bin\clang++.exe`; use the Visual Studio
  developer environment when targeting the MSVC ABI.
- GCC/MinGW for native portability validation. The local Windows installation uses WinLibs through winget and
  provides `gcc.exe`, `g++.exe`, `cmake.exe`, `ninja.exe`, and `ctest.exe`; if the current shell has not picked up
  the new `PATH`, use the binaries under
  `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`.
- MSVC C17/C++20 toolchain for `src/C/Hamt` and `src/Cpp/Hamt`; use Scriptorium's
  `Import-VisualCppEnvironment.ps1` helper when compiling from a plain PowerShell process.
- `git-filter-repo` usable as `python -m git_filter_repo` when future history work is needed.
- GHC 9.12 and cabal 3.16 or newer for the Haskell packages under `src/Haskell`.
- A JVM is optional for Kotlin validation because `src/Kotlin/build.ps1` bootstraps a local JDK 21 and
  Kotlin compiler under `src/Kotlin/build/tools` when Java 21+ is not already available.
- Rust toolchain with Cargo for `src/Rust`; the local profile may expose Cargo as
  `$env:USERPROFILE\.cargo\bin\cargo.exe` even when it is not on `PATH`.

Use `dotnet` directly for C# validation in this local environment.

## Cross-repo toolbox

Reusable automation (web mining, browser CDP capture, PDF/OCR, git/GitHub tooling, Windows GUI
control, agent-log processing, installers) lives in the sibling **Scriptorium** repo
(`C:\Scriptorium`; <https://github.com/VladimirReshetnikov/Scriptorium>) — see its `TOOLS.md`
index. **Before writing a new automation script, grep `..\Scriptorium\TOOLS.md`.** Repo-agnostic
scripts are born there and called in place, never copied here. `src/CSharp/docs/FingerTree/build-design-notes.ps1` is a thin wrapper over Scriptorium's `render/Build-LatexDoc.ps1`.
## Agent working guidelines

When starting on a task, read `AGENTS.md` first; in this repository it points to this file. Read the relevant workspace README and local docs before editing source.

Default to acting autonomously and carrying work through implementation, validation, and a clear status report. Vladimir prefers substantial, production-ready work over narrow prototype changes. Be supportive, direct, and technically honest.

Search with `rg` first for repository content. Preserve existing architecture, naming, and style unless the task calls for changing them. Keep edits scoped to the project boundary implied by the task, but update nearby docs when paths, responsibilities, or contracts change.

The worktree may contain changes from Vladimir or other tools. Do not revert changes you did not make unless explicitly asked. If unrelated changes are present, work around them. If they affect the task, understand them and build on them.

## Version control

Commit self-contained changes on `main` after validation. This repository currently has `origin`
configured; push to `origin/main` unless Vladimir explicitly asks not to.

Commit messages should describe the logical change and end with a `Co-Authored-By` trailer for the AI assistant when applicable.

## Work estimates

Do not express estimates in calendar or person-time units. Use velocity-independent units such as files touched, lines changed, test count, affected projects, binary size, or number of API members/call sites.

## Licensing

Unless a more specific license file is present, repository-owned content is licensed under MIT-0. External material under `src/CSharp/docs/FingerTree/external` retains its own copyright and license.
