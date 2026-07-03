# Source Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents navigating repository source workspaces
- Scope: Language-first source layout under `src`

`src` is organized by programming language first and data-structure family second. Each language root
owns the toolchain assumptions, build idioms, and language-specific documentation for the workspaces
under it.

| Language root | Toolchain model | Workspaces |
| --- | --- | --- |
| [C](C/README.md) | MSVC C builds; `build.ps1` for HAMT and CMake/CTest presets for FingerTree | [Hamt](C/Hamt/README.md), [FingerTree](C/FingerTree/README.md) |
| [Cpp](Cpp/README.md) | MSVC C++ builds; `build.ps1` for HAMT and CMake/CTest presets for FingerTree | [Hamt](Cpp/Hamt/README.md), [FingerTree](Cpp/FingerTree/README.md) |
| [CSharp](CSharp/README.md) | .NET 10 solutions and xUnit/CsCheck validation | [Hamt](CSharp/Hamt/README.md), [FingerTree](CSharp/FingerTree/README.md) |
| [Haskell](Haskell/README.md) | GHC/cabal packages with dependency-light executable tests | [Hamt](Haskell/Hamt/README.md), [FingerTree](Haskell/FingerTree/README.md) |

Use the repository [workspace map](../docs/reference/workspace-map.md) for layout rules and port lineage, the
[data-structure catalog](../docs/reference/data-structure-catalog.md) for cross-language public surfaces, and the
[navigation matrix](../docs/reference/navigation-matrix.md) for task-oriented documentation entry points.

## Placement Rules

- Add source workspaces under `src/<Language>/<DataStructure>/`.
- Use `CSharp` and `Cpp` for path names; avoid `Cs`, `C#`, or `C++` in directory names.
- Keep workspace-specific API, usage, validation, sample, benchmark, and test documentation inside that workspace.
- Update this index, the affected language index, the workspace map, and the data-structure catalog when adding a
  long-lived workspace or public data-structure family.
