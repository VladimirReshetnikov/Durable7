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
│   ├── agent-workflows.md
│   └── migration/
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

- [FingerTree](FingerTree/README.md) is a .NET 10 persistent finger-tree library: two engine cores (a tuned catenable deque and a general monoid-measured tree), a full collection family (sorted bag/set/dictionary, priority queue, interval tree, reversible deque), product/sum/built-in measures with a closure-free predicate API, and a rope family (positional, measured, and text). It ships a navigable design-notes document ([FingerTree-Design-Notes.pdf](FingerTree/docs/FingerTree-Design-Notes.pdf), with `.tex` source and a rebuild script alongside), a BenchmarkDotNet harness, three runnable samples, and a three-tier (example + property + model-based command) test suite plus tearable-struct concurrency stress tests.

## Build and test

Use the local .NET SDK toolchain. The solution targets `net10.0` and uses C# preview features.

```powershell
cd C:\DataStructures\FingerTree
dotnet restore
dotnet build
dotnet test .\FingerTree.sln
```

Run benchmarks from the benchmark project:

```powershell
cd C:\DataStructures\FingerTree\benchmarks\Tools.DataStructures.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Documentation

- [docs/README.md](docs/README.md) indexes repository-level documentation and migration provenance.
- [docs/agent-workflows.md](docs/agent-workflows.md) holds compact task-conditional workflow guidance.
- [FingerTree/docs/README.md](FingerTree/docs/README.md) indexes the library's specifications, design notes, benchmark notes, and external references.

The large `TECHNICAL_DOCUMENTATION_STANDARD.md` and `XML_DOCUMENTATION_STANDARD.md` files from Tools are intentionally not part of this repository. Keep documentation thorough and current-state oriented, and write XML documentation in semantic terms: contracts, invariants, ordering, failure behavior, complexity, allocation behavior, and examples where they help.

Every new long-lived document should include header provenance metadata:

```markdown
- Created (UTC): YYYY-MM-DDTHH:MM:SSZ
- Repository HEAD: <40-hex-sha>
```

Use `git rev-parse HEAD` for the repository HEAD. When creating reports where filename collisions are likely, append a `__xxxxxxxxxxxx` suffix using 12 lowercase hex digits from a content hash.

## External reference material

Files under [FingerTree/docs/external](FingerTree/docs/external/README.md) are external, pre-existing study material. They are not authored by this project and are not covered by this repository's MIT-0 license; each item keeps its own copyright and license.

## Local environment

The expected local Windows environment includes:

- `pwsh` / PowerShell 7.
- `rg` for repository search.
- `git` and `gh` for source-control and GitHub workflows.
- `python` for ad hoc tooling.
- .NET SDK 10.0 or newer with the .NET 10 targeting packs.
- `git-filter-repo` usable as `python -m git_filter_repo` when future history work is needed.

Use `dotnet` directly for C# validation in this local environment.

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

Unless a more specific license file is present, repository-owned content is licensed under MIT-0. External material under `FingerTree/docs/external` retains its own copyright and license.
