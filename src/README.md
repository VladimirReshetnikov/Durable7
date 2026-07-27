# Source Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents navigating repository source workspaces
- Scope: Language-first source layout under `src`

`src` is organized by programming language first. Each language root owns the toolchain assumptions,
build idioms, and language-specific documentation for the workspaces under it. Most language roots keep
library-family directories directly under the language root; C# is a single managed solution with
projects grouped by role under `src/CSharp/src`, `tests`, `samples`, and `benchmarks`, while Python,
OCaml, and TypeScript package all families into one language-local distribution.

| Language root | Toolchain model | Workspaces | Validation entry point |
| --- | --- | --- | --- |
| [C](C/README.md) | Serialized MSVC/GCC/Clang builds through `build.ps1` and CMake/CTest presets | [Hamt](C/Hamt/README.md), [FingerTree + Range](C/FingerTree/README.md), [Ordered](C/Ordered/README.md) | `.\build.ps1 -Workspace <name> -RunTests` |
| [Cpp](Cpp/README.md) | Serialized MSVC/GCC/Clang builds through `build.ps1` and CMake/CTest presets | [Hamt](Cpp/Hamt/README.md), [FingerTree + Range](Cpp/FingerTree/README.md), [Ordered](Cpp/Ordered/README.md) | `.\build.ps1 -Workspace <name> -RunTests` |
| [CSharp](CSharp/README.md) | One .NET 10 solution with xUnit/CsCheck validation | [HAMT](CSharp/docs/Hamt/overview.md), [FingerTree and Range-update sequence](CSharp/docs/FingerTree/overview.md), [Ordered](CSharp/docs/Ordered/overview.md) | `.\test.ps1` |
| [Haskell](Haskell/README.md) | GHC/cabal packages with dependency-light executable tests | [Hamt](Haskell/Hamt/README.md), [FingerTree + Range](Haskell/FingerTree/README.md), [Ordered](Haskell/Ordered/README.md) | `.\test.ps1` |
| [Kotlin](Kotlin/README.md) | Kotlin/JVM command-line compiler with dependency-free executable tests bootstrapped by `build.ps1` | [Hamt](Kotlin/Hamt/README.md), [FingerTree + Range](Kotlin/FingerTree/README.md), [Ordered](Kotlin/Ordered/README.md) | `.\build.ps1` |
| [OCaml](OCaml/README.md) | opam/Dune package with strict warnings, ocamlformat, odoc, Alcotest, and QCheck | [HAMT, FingerTree + Range, and Ordered](OCaml/docs/api-notes.md#public-families) | `.\test.ps1` |
| [Python](Python/README.md) | Typed Python 3.11+ package with Ruff, strict Mypy, pytest/Hypothesis, and wheel validation | [HAMT, FingerTree, and Ordered](Python/README.md#package-families) | `.\test.ps1` |
| [Rust](Rust/README.md) | Cargo workspace with safe Rust crates and integration tests | [Hamt](Rust/Hamt/README.md), [FingerTree](Rust/FingerTree/README.md), [Ordered](Rust/Ordered/README.md), [RangeUpdate](Rust/RangeUpdate/README.md) | `.\test.ps1` |
| [TypeScript](TypeScript/README.md) | Strict TypeScript/ESM npm package with Vitest and fast-check validation | [HAMT, FingerTree, and Ordered](TypeScript/README.md#public-families) | `npm ci && npm run validate` |

Every port implements the same three families, so the `Hamt` / `FingerTree` / `Ordered` split is the
reliable way to navigate across languages even though the build unit differs. Rust is the one
exception to the three-way split: `RangeUpdate` is a separate crate there because it owns a distinct
implicit-AVL representation instead of composing the finger tree, whereas the other ports keep the
equivalent type inside their FingerTree unit.

All `.ps1` entry points in the table run single-worker by construction; the
[build and validation guide](../docs/guides/build-and-validation.md) explains why and lists the
prerequisites for each toolchain.

The benchmark-independent rollouts now ship one-descent persistent HAMT updates,
`PersistentHashBag`, strict `PersistentBiMap`, neutral `PersistentOrderedSet` and `PersistentOrderedMap`, set-valued
`PersistentHashMultimap`, bidirectional `PersistentRelation`, payload-bearing
`PersistentIntervalMap`, the law-gated `RangeUpdateSequence` family, and the
current `PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`,
`PersistentIndexedMap`, and `PersistentChunkedBitSet` tranche across all nine language roots. C#
owns the detailed managed contracts while siblings express the same semantics through language-local
policies and ownership; the OCaml API notes identify checkpoint implementations that do not inherit
specialized sibling topology or complexity claims. Both complete serialized C# Debug and Release solution builds finish with
zero warnings and zero errors, and both test gates pass 1,158/1,158. No benchmark was run, and
measurements remain postponed until an isolated session. The
[data-structure catalog](../docs/reference/data-structure-catalog.md#derived-persistent-maps-relations-and-sparse-bit-sets)
indexes the composition-first cross-language surfaces. The earlier
[completion audit](../docs/reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md)
and [bimap completion audit](../docs/reviews/persistent-bimap-cross-language-completion-2026-07-15.md)
index the preceding tranches.

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
  language root already has a stronger native workspace convention. OCaml, Python, and TypeScript keep
  family modules inside one package workspace rather than creating separately built family roots.
- Use `CSharp` and `Cpp` for path names; avoid `Cs`, `C#`, or `C++` in directory names.
- Keep workspace-specific API, usage, validation, sample, benchmark, and test documentation inside that
  language root's established family documentation location.
- Update this index, the affected language index, the workspace map, and the relevant catalog or reference docs when
  adding a long-lived workspace or public library family.
