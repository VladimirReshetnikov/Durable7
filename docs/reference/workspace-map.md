# Workspace Map

- Created (UTC): 2026-07-02T19:44:02Z
- Repository HEAD: 9bf68f498405e2dce44cb08fad08ea2bbe97d97c
- Audience: Maintainers and AI agents navigating the repository
- Scope: Repository organization, workspace roles, and documentation placement

The repository is organized by programming language first and by data structure second:

```text
src/
├── README.md
├── C/
│   ├── README.md
│   ├── FingerTree/
│   └── Hamt/
├── Cpp/
│   ├── README.md
│   ├── FingerTree/
│   └── Hamt/
└── CSharp/
    ├── README.md
    ├── FingerTree/
    └── Hamt/
```

This makes language-local build systems, toolchains, include paths, and idioms easy to find while keeping
the same data-structure families aligned across languages.

Use the [source index](../../src/README.md) when browsing by language, or jump directly to the
[C](../../src/C/README.md), [C++](../../src/Cpp/README.md), or [C#](../../src/CSharp/README.md) language index.

For the cross-language list of public data-structure surfaces, see the
[data structure catalog](data-structure-catalog.md).

## Workspace Roles

| Workspace | Role | Main entry points | Local docs |
| --- | --- | --- | --- |
| [`src/CSharp/Hamt`](../../src/CSharp/Hamt/README.md) | Canonical managed HAMT library | `Hamt.sln`, `src/Tools.DataStructures.Hamt/` | [`docs`](../../src/CSharp/Hamt/docs/README.md) |
| [`src/C/Hamt`](../../src/C/Hamt/README.md) | C17 HAMT port | `include/Tools/DataStructures/Hamt/hamt.h`, `build.ps1` | [`docs`](../../src/C/Hamt/docs/README.md) |
| [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) | C++20 HAMT port | `include/Tools/DataStructures/Hamt/*.hpp`, `build.ps1` | [`docs`](../../src/Cpp/Hamt/docs/README.md) |
| [`src/CSharp/FingerTree`](../../src/CSharp/FingerTree/README.md) | Canonical managed FingerTree library | `FingerTree.sln`, `src/Tools.DataStructures.FingerTree/` | [`docs`](../../src/CSharp/FingerTree/docs/README.md) |
| [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) | C++23 FingerTree port | `include/tools/data_structures/finger_tree/`, `CMakePresets.json` | [`docs`](../../src/Cpp/FingerTree/docs/README.md) |
| [`src/C/FingerTree`](../../src/C/FingerTree/README.md) | C11 FingerTree port | `include/tools/data_structures/finger_tree/fingertree.h`, `CMakePresets.json` | [`docs`](../../src/C/FingerTree/docs/README.md) |

## Port Lineage

HAMT lineage:

1. `src/CSharp/Hamt` defines the managed public contract and model-test baseline.
2. `src/Cpp/Hamt` ports the HAMT semantics to C++ value types, templates, and `std::shared_ptr` node sharing.
3. `src/C/Hamt` ports the same structure to a type-erased C API with explicit clone/destroy ownership.

FingerTree lineage:

1. `src/CSharp/FingerTree` is the broadest implementation and documentation source: tuned deque, general measured tree, derived sorted/priority/interval collections, ropes, text helpers, samples, benchmarks, and design notes.
2. `src/Cpp/FingerTree` ports the FingerTree family to a header-first C++23 library with CMake/CTest validation.
3. `src/C/FingerTree` starts from the C++ port and exposes a C11 API with explicit handles, ownership, and facade types.

When porting behavior across languages, prefer the managed workspace for the semantic contract, the adjacent
native workspace for local idioms, and the local tests for the exact validation shape. Use the
[porting and semantic parity guide](../guides/porting-and-semantic-parity.md) for the cross-language
change workflow.

## Documentation Organization

Repository-level docs live under `docs/`:

- [`docs/guides`](../guides/README.md) holds workflow documents: validation, agent workflows, and task procedures.
- [`docs/reference`](README.md) holds durable maps and cross-workspace reference material.
- [`docs/migration`](../migration/README.md) preserves extraction and history-filtering provenance.

Workspace-level docs live next to the code they describe:

- API contracts and library-specific design notes belong under the workspace's `docs/` directory.
- Build entry points and quick orientation belong in the workspace `README.md`.
- Long-lived repository-wide reports belong under `docs/`, not inside one language workspace.
- External study material remains segregated under [`src/CSharp/FingerTree/docs/external`](../../src/CSharp/FingerTree/docs/external/README.md).

## Naming And Path Conventions

- Use `CSharp`, not `Cs`, for the managed language root.
- Use `Cpp`, not `C++`, in paths so shell tooling and URLs stay simple.
- Use `Hamt` for hash-array mapped trie workspaces, matching the public project names.
- Use `FingerTree` for the measured finger-tree family, including derived collections and ropes.
- Write current paths in active documentation. Put historical paths only in explicit provenance or review reports.
