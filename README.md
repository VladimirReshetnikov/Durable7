# DataStructures repository

- Status: Active standalone repository
- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI coding agents working on repository-owned data structures
- Scope: Repository layout, build entry points, and agent guidance

This repository contains Vladimir Reshetnikov's standalone data-structure workspaces and design references. It was extracted from `C:\Tools0\src\DataStructures` / `VladimirReshetnikov/Tools` with path-local Git history preserved as precisely as practical. The Tools-side handoff is recorded by [`5fc4054da`](https://github.com/VladimirReshetnikov/Tools/commit/5fc4054da), which removes the former subtree and points the Tools indexes here.

This document is the canonical repository guidance for Vladimir and the AI coding agents that help him. `AGENTS.md` and `CLAUDE.md` point here, so keep shared project and agent instructions in this file.

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
    ├── C/
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
    └── CSharp/
        ├── Hamt/
        │   ├── Directory.Build.props
        │   ├── Hamt.sln
        │   ├── README.md
        │   ├── docs/
        │   ├── src/
        │   └── tests/
        └── FingerTree/
            ├── Directory.Build.props
            ├── FingerTree.sln
            ├── README.md
            ├── benchmarks/
            ├── docs/
            ├── samples/
            ├── src/
            └── tests/
```

## Workspaces

- [src/CSharp/Hamt](src/CSharp/Hamt/README.md) is a .NET 10 persistent hash-array mapped trie library. It provides `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>` with bitmap-indexed 32-way branching, immutable equal-hash collision buckets, comparer-preserving factories, structural sharing across versions, and xUnit/CsCheck model tests against BCL dictionaries and sets.
- [src/CSharp/FingerTree](src/CSharp/FingerTree/README.md) is a .NET 10 persistent finger-tree library: two engine cores (a tuned catenable deque and a general monoid-measured tree), a full collection family (sorted bag/set/dictionary, priority queue, interval tree, reversible deque), product/sum/built-in measures with a closure-free predicate API, and a rope family (positional, measured, and text). It ships a navigable design-notes document ([FingerTree-Design-Notes.pdf](src/CSharp/FingerTree/docs/FingerTree-Design-Notes.pdf), with `.tex` source and a rebuild script alongside), a BenchmarkDotNet harness, three runnable samples, and a three-tier (example + property + model-based command) test suite plus tearable-struct concurrency stress tests.
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

## Build and test

Use [docs/guides/build-and-validation.md](docs/guides/build-and-validation.md) as the complete validation guide. In short, use the local .NET SDK toolchain for the C# workspaces, the local MSVC C17/C++20 toolchain for HAMT native ports, and the Visual Studio-bundled CMake/Ninja presets for the FingerTree native ports.

```powershell
cd C:\DataStructures\src\CSharp\FingerTree
dotnet restore
dotnet build
dotnet test .\FingerTree.sln

cd C:\DataStructures\src\CSharp\Hamt
dotnet restore
dotnet build
dotnet test .\Hamt.sln

cd C:\DataStructures\src\C\Hamt
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests

cd C:\DataStructures\src\Cpp\Hamt
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cd C:\DataStructures\src\C\FingerTree
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure"

cd C:\DataStructures\src\Cpp\FingerTree
cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure"
```

Run benchmarks from the benchmark project:

```powershell
cd C:\DataStructures\src\CSharp\FingerTree\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Documentation

- [docs/README.md](docs/README.md) indexes repository-level documentation and migration provenance.
- [docs/guides/README.md](docs/guides/README.md) indexes task-oriented repository procedures.
- [docs/guides/agent-workflows.md](docs/guides/agent-workflows.md) holds compact task-conditional workflow guidance.
- [docs/guides/build-and-validation.md](docs/guides/build-and-validation.md) is the repository-wide validation matrix and command guide.
- [docs/reference/README.md](docs/reference/README.md) indexes durable cross-workspace reference material.
- [docs/reference/workspace-map.md](docs/reference/workspace-map.md) explains the language-first, data-structure-second layout and port lineage.
- [src/CSharp/Hamt/docs/README.md](src/CSharp/Hamt/docs/README.md) indexes the HAMT library's API specification.
- [src/C/Hamt/docs/README.md](src/C/Hamt/docs/README.md) indexes the C HAMT port's API specification.
- [src/Cpp/Hamt/docs/README.md](src/Cpp/Hamt/docs/README.md) indexes the C++ HAMT port's API
  specification.
- [src/CSharp/FingerTree/docs/README.md](src/CSharp/FingerTree/docs/README.md) indexes the library's specifications, design notes, benchmark notes, and external references.
- [src/Cpp/FingerTree/docs/README.md](src/Cpp/FingerTree/docs/README.md) indexes the C++ port plan, API notes, validation guide, and review reports.
- [src/C/FingerTree/docs/README.md](src/C/FingerTree/docs/README.md) indexes the C port API and validation notes.

The large `TECHNICAL_DOCUMENTATION_STANDARD.md` and `XML_DOCUMENTATION_STANDARD.md` files from Tools are intentionally not part of this repository. Keep documentation thorough and current-state oriented, and write XML documentation in semantic terms: contracts, invariants, ordering, failure behavior, complexity, allocation behavior, and examples where they help.

Every new long-lived document should include header provenance metadata:

```markdown
- Created (UTC): YYYY-MM-DDTHH:MM:SSZ
- Repository HEAD: <40-hex-sha>
```

Use `git rev-parse HEAD` for the repository HEAD. When creating reports where filename collisions are likely, append a `__xxxxxxxxxxxx` suffix using 12 lowercase hex digits from a content hash.

## External reference material

Files under [src/CSharp/FingerTree/docs/external](src/CSharp/FingerTree/docs/external/README.md) are external, pre-existing study material. They are not authored by this project and are not covered by this repository's MIT-0 license; each item keeps its own copyright and license.

## Local environment

The expected local Windows environment includes:

- `pwsh` / PowerShell 7.
- `rg` for repository search.
- `git` and `gh` for source-control and GitHub workflows.
- `python` for ad hoc tooling.
- .NET SDK 10.0 or newer with the .NET 10 targeting packs.
- Visual Studio native C/C++ toolchain, plus the bundled CMake and Ninja used by the `src/C/FingerTree` and `src/Cpp/FingerTree` presets.
- MSVC C17/C++20 toolchain for `src/C/Hamt` and `src/Cpp/Hamt`; use Scriptorium's
  `Import-VisualCppEnvironment.ps1` helper when compiling from a plain PowerShell process.
- `git-filter-repo` usable as `python -m git_filter_repo` when future history work is needed.

Use `dotnet` directly for C# validation in this local environment.

## Cross-repo toolbox

Reusable automation (web mining, browser CDP capture, PDF/OCR, git/GitHub tooling, Windows GUI
control, agent-log processing, installers) lives in the sibling **Scriptorium** repo
(`C:\Scriptorium`; <https://github.com/VladimirReshetnikov/Scriptorium>) — see its `TOOLS.md`
index. **Before writing a new automation script, grep `..\Scriptorium\TOOLS.md`.** Repo-agnostic
scripts are born there and called in place, never copied here. `src/CSharp/FingerTree/docs/build-design-notes.ps1` is a thin wrapper over Scriptorium's `render/Build-LatexDoc.ps1`.
## Agent working guidelines

When starting on a task, read `AGENTS.md` first; in this repository it points to this file. Read the relevant workspace README and local docs before editing source.

Default to acting autonomously and carrying work through implementation, validation, and a clear status report. Vladimir prefers substantial, production-ready work over narrow prototype changes. Be supportive, direct, and technically honest.

Search with `rg` first for repository content. Preserve existing architecture, naming, and style unless the task calls for changing them. Keep edits scoped to the project boundary implied by the task, but update nearby docs when paths, responsibilities, or contracts change.

The worktree may contain changes from Vladimir or other tools. Do not revert changes you did not make unless explicitly asked. If unrelated changes are present, work around them. If they affect the task, understand them and build on them.

## Version control

Commit self-contained changes on `main` after validation. If a remote is configured, push to `origin/main` unless Vladimir explicitly asks not to. This extracted repository starts without a GitHub remote; add one only when intentionally publishing it.

Commit messages should describe the logical change and end with a `Co-Authored-By` trailer for the AI assistant when applicable.

## Work estimates

Do not express estimates in calendar or person-time units. Use velocity-independent units such as files touched, lines changed, test count, affected projects, binary size, or number of API members/call sites.

## Licensing

Unless a more specific license file is present, repository-owned content is licensed under MIT-0. External material under `src/CSharp/FingerTree/docs/external` retains its own copyright and license.
