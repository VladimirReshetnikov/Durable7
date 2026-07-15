# Source Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents navigating repository source workspaces
- Scope: Language-first source layout under `src`

`src` is organized by programming language first. Each language root owns the toolchain assumptions,
build idioms, and language-specific documentation for the workspaces under it. Most language roots keep
library-family directories directly under the language root; C# is a single managed solution with
projects grouped by role under `src/CSharp/src`, `tests`, `samples`, and `benchmarks`, while Python
and TypeScript package all families into one language-local distribution.

| Language root | Toolchain model | Workspaces |
| --- | --- | --- |
| [C](C/README.md) | Serialized MSVC/GCC/Clang builds through `build.ps1` and CMake/CTest presets | [Hamt](C/Hamt/README.md), [FingerTree + Range](C/FingerTree/README.md), [Ordered](C/Ordered/README.md), [Tungsten](C/Tungsten/README.md) |
| [Cpp](Cpp/README.md) | Serialized MSVC/GCC/Clang builds through `build.ps1` and CMake/CTest presets | [Hamt](Cpp/Hamt/README.md), [FingerTree + Range](Cpp/FingerTree/README.md), [Ordered](Cpp/Ordered/README.md), [Tungsten](Cpp/Tungsten/README.md) |
| [CSharp](CSharp/README.md) | One .NET 10 solution with xUnit/CsCheck validation | [Numerics](CSharp/docs/Numerics/overview.md), [HAMT](CSharp/docs/Hamt/overview.md), [FingerTree and Range-update sequence](CSharp/docs/FingerTree/overview.md), [Ordered](CSharp/docs/Ordered/overview.md), [Tungsten](CSharp/docs/Tungsten/overview.md) |
| [Haskell](Haskell/README.md) | GHC/cabal packages with dependency-light executable tests | [Hamt](Haskell/Hamt/README.md), [FingerTree + Range](Haskell/FingerTree/README.md), [Ordered](Haskell/Ordered/README.md), [Tungsten](Haskell/Tungsten/README.md) |
| [Kotlin](Kotlin/README.md) | Kotlin/JVM command-line compiler with dependency-free executable tests bootstrapped by `build.ps1` | [Hamt](Kotlin/Hamt/README.md), [FingerTree + Range](Kotlin/FingerTree/README.md), [Ordered](Kotlin/Ordered/README.md), [Tungsten](Kotlin/Tungsten/README.md) |
| [Python](Python/README.md) | Typed Python 3.11+ package with Ruff, strict Mypy, pytest/Hypothesis, and wheel validation | [HAMT, FingerTree, Ordered, Tungsten, and Numerics](Python/README.md) |
| [Rust](Rust/README.md) | Cargo workspace with safe Rust crates and integration tests | [Hamt](Rust/Hamt/README.md), [FingerTree](Rust/FingerTree/README.md), [Ordered](Rust/Ordered/README.md), [RangeUpdate](Rust/RangeUpdate/README.md), [Tungsten](Rust/Tungsten/README.md) |
| [TypeScript](TypeScript/README.md) | Strict TypeScript/ESM npm package with Vitest and fast-check validation | [HAMT, FingerTree, Ordered, Tungsten, and Numerics](TypeScript/README.md#public-families) |

The benchmark-independent rollout now ships one-descent persistent HAMT updates,
`PersistentHashBag`, neutral `PersistentOrderedSet`, and independently implemented implicit-AVL
`RangeUpdateSequence` surfaces across all eight language roots. C# owns the law-gated reference
`IRangeUpdateAlgebra`, while siblings express the same action through language-local policies. Both complete
serialized C# Debug and Release solution builds finish with zero warnings and zero errors, and both
test gates pass 1,417/1,417. No benchmark was run, and measurements remain postponed until an
isolated session. The detailed [completion audit](../docs/reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md)
indexes every source, test, dependency, and validation checkpoint.

Across every language root, Tungsten is an application-specific leaf consumer. It may depend on
general libraries; no general workspace may depend on it. See the normative
[application-leaf boundary](../docs/reference/tungsten-application-leaf-boundary.md).

Use the repository [onboarding guide](../docs/guides/repository-onboarding.md) when starting work in an
unfamiliar area, the [workspace map](../docs/reference/workspace-map.md) for layout rules and port lineage,
the [data-structure catalog](../docs/reference/data-structure-catalog.md) for cross-language public surfaces,
the [semantic contracts reference](../docs/reference/semantic-contracts.md) for shared behavior obligations,
and the [navigation matrix](../docs/reference/navigation-matrix.md) for task-oriented documentation entry points.

## Placement Rules

- Add new C# projects under `src/CSharp/src`, tests under `src/CSharp/tests`, samples under
  `src/CSharp/samples`, benchmarks under `src/CSharp/benchmarks`, and family docs under
  `src/CSharp/docs/<LibraryFamily>/`.
- For other languages, add source workspaces under `src/<Language>/<LibraryFamily>/` unless the
  language root already has a stronger native workspace convention. Python and TypeScript keep
  family modules inside one package workspace rather than creating separately built family roots.
- Use `CSharp` and `Cpp` for path names; avoid `Cs`, `C#`, or `C++` in directory names.
- Keep workspace-specific API, usage, validation, sample, benchmark, and test documentation inside that
  language root's established family documentation location.
- Update this index, the affected language index, the workspace map, and the relevant catalog or reference docs when
  adding a long-lived workspace or public library family.
