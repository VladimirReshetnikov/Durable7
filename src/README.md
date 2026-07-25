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

| Language root | Toolchain model | Workspaces |
| --- | --- | --- |
| [OCaml](OCaml/README.md) | opam/Dune package with strict warnings, ocamlformat, odoc, Alcotest, and QCheck | [HAMT, FingerTree + Range, and Ordered](OCaml/docs/api-notes.md#public-families) |
| [Python](Python/README.md) | Typed Python 3.11+ package with Ruff, strict Mypy, pytest/Hypothesis, and wheel validation | [HAMT, FingerTree, and Ordered](Python/README.md) |
| [TypeScript](TypeScript/README.md) | Strict TypeScript/ESM npm package with Vitest and fast-check validation | [HAMT, FingerTree, and Ordered](TypeScript/README.md#public-families) |

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
