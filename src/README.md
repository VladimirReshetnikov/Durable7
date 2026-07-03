# Source Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents navigating repository source workspaces
- Scope: Language-first source layout under `src`

`src` is organized by programming language first and library family second. Each language root
owns the toolchain assumptions, build idioms, and language-specific documentation for the workspaces
under it.

| Language root | Toolchain model | Workspaces |
| --- | --- | --- |
| [C](C/README.md) | MSVC C builds; `build.ps1` for HAMT and CMake/CTest presets for FingerTree | [Hamt](C/Hamt/README.md), [FingerTree](C/FingerTree/README.md) |
| [Cpp](Cpp/README.md) | MSVC C++ builds; `build.ps1` for HAMT and CMake/CTest presets for FingerTree | [Hamt](Cpp/Hamt/README.md), [FingerTree](Cpp/FingerTree/README.md) |
| [CSharp](CSharp/README.md) | .NET 10 solutions and xUnit/CsCheck validation | [Numerics](CSharp/Numerics/README.md), [Hamt](CSharp/Hamt/README.md), [FingerTree](CSharp/FingerTree/README.md) |
| [Haskell](Haskell/README.md) | GHC/cabal packages with dependency-light executable tests | [Hamt](Haskell/Hamt/README.md), [FingerTree](Haskell/FingerTree/README.md) |
| [Kotlin](Kotlin/README.md) | Kotlin/JVM command-line compiler with dependency-free executable tests bootstrapped by `build.ps1` | [Hamt](Kotlin/Hamt/README.md), [FingerTree](Kotlin/FingerTree/README.md) |
| [Rust](Rust/README.md) | Cargo workspace with safe Rust crates and inline unit tests | [Hamt](Rust/Hamt/README.md), [FingerTree](Rust/FingerTree/README.md) |

Use the repository [workspace map](../docs/reference/workspace-map.md) for layout rules and port lineage, the
[data-structure catalog](../docs/reference/data-structure-catalog.md) for cross-language public surfaces, and the
[navigation matrix](../docs/reference/navigation-matrix.md) for task-oriented documentation entry points.

## Placement Rules

- Add source workspaces under `src/<Language>/<LibraryFamily>/`.
- Use `CSharp` and `Cpp` for path names; avoid `Cs`, `C#`, or `C++` in directory names.
- Keep workspace-specific API, usage, validation, sample, benchmark, and test documentation inside that workspace.
- Update this index, the affected language index, the workspace map, and the relevant catalog or reference docs when
  adding a long-lived workspace or public library family.
