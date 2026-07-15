# Workspace Map

- Created (UTC): 2026-07-02T19:44:02Z
- Repository HEAD: 9bf68f498405e2dce44cb08fad08ea2bbe97d97c
- Audience: Maintainers and AI agents navigating the repository
- Scope: Repository organization, workspace roles, and documentation placement

The repository is organized by programming language first. Native, Haskell, Kotlin, and Rust roots keep
library-family directories directly under the language root. The C# root is a single managed solution
with projects grouped by role; Python and TypeScript each package their family modules into one
language-local distribution:

```text
src/
├── README.md
├── C/
│   ├── README.md
│   ├── FingerTree/
│   ├── Hamt/
│   └── Tungsten/
├── Cpp/
│   ├── README.md
│   ├── FingerTree/
│   ├── Hamt/
│   └── Tungsten/
├── CSharp/
│   ├── README.md
│   ├── DataStructures.sln
│   ├── Directory.Build.props
│   ├── benchmarks/
│   ├── docs/
│   │   ├── FingerTree/
│   │   ├── Hamt/
│   │   ├── Numerics/
│   │   ├── Ordered/
│   │   └── Tungsten/
│   ├── samples/
│   ├── src/
│   │   ├── Tools.DataStructures.FingerTree/
│   │   ├── Tools.DataStructures.Hamt/
│   │   ├── Tools.DataStructures.Ordered/
│   │   ├── Tools.DataStructures.Tungsten/
│   │   └── Tools.Numerics/
│   └── tests/
├── Haskell/
│   ├── README.md
│   ├── FingerTree/
│   ├── Hamt/
│   └── Tungsten/
├── Kotlin/
│   ├── README.md
│   ├── FingerTree/
│   ├── Hamt/
│   └── Tungsten/
├── Python/
│   ├── README.md
│   ├── docs/
│   ├── src/vladimir_reshetnikov/data_structures/
│   ├── tests/
│   └── test.ps1
├── Rust/
│   ├── README.md
│   ├── FingerTree/
│   ├── Hamt/
│   └── Tungsten/
└── TypeScript/
    ├── README.md
    ├── docs/
    ├── src/
    └── test/
```

This makes language-local build systems, toolchains, include paths, and idioms easy to find while keeping
related library families aligned across languages where ports exist. In C#, the single solution keeps
managed package boundaries intact while allowing one restore/build/test entry point for the whole managed
surface.

Use the [source index](../../src/README.md) when browsing by language, or jump directly to the
[C](../../src/C/README.md), [C++](../../src/Cpp/README.md), [C#](../../src/CSharp/README.md),
[Haskell](../../src/Haskell/README.md), [Kotlin](../../src/Kotlin/README.md),
[Python](../../src/Python/README.md), [Rust](../../src/Rust/README.md), or
[TypeScript](../../src/TypeScript/README.md) language index.

For the cross-language list of public library surfaces, see the
[data structure catalog](data-structure-catalog.md). For the shared behavior, ownership, policy,
ordering, and documentation obligations that should remain recognizable across language ports, see the
[semantic contracts reference](semantic-contracts.md).

## Dependency Direction

Tungsten is an application-specific leaf family for the Tungsten project, not a repository-general
foundation. It may consume HAMT, FingerTree, and other general libraries; dependency arrows must
never point from a general or non-Tungsten workspace to a Tungsten package, type, or implementation.
This boundary keeps the general families independent if kernel discoveries change Tungsten behavior
or the Tungsten workspaces move out of this repository.

A generally useful Tungsten mechanism must be forked into a separately owned workspace with an
independent API, contract, test suite, and evolution policy. The fork may deliberately relax
Tungsten-specific fidelity or complexity guarantees. Provenance and translated tests are welcome;
a project reference, wrapper, shared implementation owner, or Tungsten semantic baseline is not.
See the detailed
[Tungsten application-leaf dependency boundary](tungsten-application-leaf-boundary.md) for the
normative code, test, documentation, porting, and extraction rules.

`Tools.DataStructures.Ordered` is an independently owned general workspace created under that rule.
Its dependency graph is deliberately limited to public general substrates:

```text
Tools.DataStructures.Ordered
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree
```

There is no Ordered-to-Tungsten project, source, test-oracle, or semantic-authority edge. Ordered and
Tungsten are separately owned sibling consumers of HAMT/FingerTree mechanics; similarity in sparse
order labels does not create shared ownership or permission for either family to define the other's
contract.

## Workspace Roles

| Workspace | Role | Main entry points | Local docs |
| --- | --- | --- | --- |
| [C# Numerics](../../src/CSharp/docs/Numerics/overview.md) | Managed fixed-width and sparse integer numerics library | `DataStructures.sln`, `src/Tools.Numerics/`, `tests/Tools.Numerics.Tests/` | [`docs`](../../src/CSharp/docs/Numerics/README.md) |
| [C# HAMT](../../src/CSharp/docs/Hamt/overview.md) | Canonical managed persistent hash map/set, multiplicity bag, strict bimap, Ctrie, Patricia, and Merkle library | `DataStructures.sln`, `src/Tools.DataStructures.Hamt/`, `tests/Tools.DataStructures.Hamt.Tests/` | [`docs`](../../src/CSharp/docs/Hamt/README.md) |
| [`src/C/Hamt`](../../src/C/Hamt/README.md) | C17 HAMT/bag/bimap/Patricia port and complete type-erased wire-compatible Merkle search tree | `include/Tools/DataStructures/Hamt/*.h`, `build.ps1` | [`docs`](../../src/C/Hamt/docs/README.md), [Merkle guide](../../src/C/Hamt/docs/merkle-search-tree.md) |
| [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) | C++20 HAMT/bag/bimap/Patricia port and complete wire-compatible Merkle search tree | `include/Tools/DataStructures/Hamt/*.hpp`, `build.ps1` | [`docs`](../../src/Cpp/Hamt/docs/README.md), [Merkle core](../../src/Cpp/Hamt/docs/merkle-search-tree.md), [persistence](../../src/Cpp/Hamt/docs/merkle-persistence.md) |
| [`src/Haskell/Hamt`](../../src/Haskell/Hamt/README.md) | Haskell HAMT/bag/bimap/Patricia port and complete pure wire-compatible Merkle search tree | `tools-data-structures-hamt.cabal`, `src/Data/Structures/Hamt/` | [`README`](../../src/Haskell/Hamt/README.md), [Merkle guide](../../src/Haskell/Hamt/docs/merkle-search-tree.md) |
| [`src/Kotlin/Hamt`](../../src/Kotlin/Hamt/README.md) | Kotlin/JVM HAMT/bag/bimap/Ctrie/Patricia port and complete wire-compatible Merkle search tree | `src/tools/datastructures/hamt/`, `test/tools/datastructures/hamt/` | [`docs`](../../src/Kotlin/Hamt/docs/README.md) |
| [`src/Rust/Hamt`](../../src/Rust/Hamt/README.md) | Rust HAMT/bag/bimap/Patricia port and wire-compatible Merkle search tree | `Cargo.toml`, `src/lib.rs`, `src/merkle_search_tree.rs` | [`docs`](../../src/Rust/Hamt/docs/README.md) |
| [C# FingerTree](../../src/CSharp/docs/FingerTree/overview.md) | Canonical managed persistent-sequence library: FingerTree family plus the independent implicit-AVL Range-update core | `DataStructures.sln`, `src/Tools.DataStructures.FingerTree/`, `tests/Tools.DataStructures.FingerTree.Tests/`, `samples/`, `benchmarks/` | [`docs`](../../src/CSharp/docs/FingerTree/README.md), [Range contract](../../src/CSharp/docs/FingerTree/range-update-sequence.md) |
| [C# Ordered collections](../../src/CSharp/docs/Ordered/overview.md) | Independently owned neutral insertion/explicit-position ordered set with first-representative and receiver-policy algebra contracts over public CHAMP/FingerTree substrates | [`DataStructures.sln`](../../src/CSharp/DataStructures.sln), [project](../../src/CSharp/src/Tools.DataStructures.Ordered/Tools.DataStructures.Ordered.csproj), [tests](../../src/CSharp/tests/Tools.DataStructures.Ordered.Tests/README.md) | [`docs`](../../src/CSharp/docs/Ordered/README.md), [validation](../../src/CSharp/docs/Ordered/validation.md) |
| [`src/C/Ordered`](../../src/C/Ordered/README.md) | Neutral type-erased C17 ordered-set port | `include/`, `src/`, `tests/`, `CMakePresets.json` | [API](../../src/C/Ordered/docs/api-specification.md), [validation](../../src/C/Ordered/docs/validation.md) |
| [`src/Cpp/Ordered`](../../src/Cpp/Ordered/README.md) | Neutral header-first C++ persistent ordered-set port | `include/`, `tests/`, `CMakePresets.json` | [API](../../src/Cpp/Ordered/docs/api-notes.md), [validation](../../src/Cpp/Ordered/docs/validation.md) |
| [`src/Haskell/Ordered`](../../src/Haskell/Ordered/README.md) | Neutral Haskell persistent ordered-set package | `tools-data-structures-ordered.cabal`, `src/`, `test/` | [`README`](../../src/Haskell/Ordered/README.md) |
| [`src/Kotlin/Ordered`](../../src/Kotlin/Ordered/README.md) | Neutral Kotlin/JVM persistent ordered-set workspace | `src/`, `test/` | [`docs`](../../src/Kotlin/Ordered/docs/README.md) |
| [`src/Rust/Ordered`](../../src/Rust/Ordered/README.md) | Neutral safe Rust persistent ordered-set crate | `Cargo.toml`, `src/`, `tests/` | [API](../../src/Rust/Ordered/docs/api-notes.md), [validation](../../src/Rust/Ordered/docs/validation.md) |
| [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) | C++23 FingerTree/RRB/canonical-set family with Range, Brodal/PSQ cores, positional/measured/text rope cursors, and native DABA Lite | `include/tools/data_structures/finger_tree/`, `CMakePresets.json` | [`docs`](../../src/Cpp/FingerTree/docs/README.md) |
| [`src/C/FingerTree`](../../src/C/FingerTree/README.md) | C11 FingerTree/RRB/canonical-set family with Range, positional/measured/text rope cursors, type-erased Brodal/PSQ cores, and DABA Lite | `include/tools/data_structures/finger_tree/`, `CMakePresets.json` | [`docs`](../../src/C/FingerTree/docs/README.md) |
| [`src/Haskell/FingerTree`](../../src/Haskell/FingerTree/README.md) | Haskell FingerTree/RRB/canonical-set family port with Range and positional/measured/text rope cursors | `tools-data-structures-fingertree.cabal`, `src/Data/Structures/FingerTree/` | [`README`](../../src/Haskell/FingerTree/README.md) |
| [`src/Kotlin/FingerTree`](../../src/Kotlin/FingerTree/README.md) | Kotlin/JVM persistent measured-tree/RRB/canonical-set/Range/optimal-priority family plus managed DABA Lite | `src/tools/datastructures/fingertree/`, `test/tools/datastructures/fingertree/` | [`docs`](../../src/Kotlin/FingerTree/docs/README.md) |
| [`src/Rust/FingerTree`](../../src/Rust/FingerTree/README.md) | Rust FingerTree/RRB/canonical-set checkpoint with non-`Clone` Brodal/PSQ cores plus single-threaded DABA Lite | `Cargo.toml`, `src/` | [`docs`](../../src/Rust/FingerTree/docs/README.md) |
| [`src/Rust/RangeUpdate`](../../src/Rust/RangeUpdate/README.md) | Safe Rust implicit-AVL range-update sequence crate | `Cargo.toml`, `src/`, `tests/` | [`docs`](../../src/Rust/RangeUpdate/docs/README.md) |
| [C# Tungsten collections](../../src/CSharp/docs/Tungsten/overview.md) | Application-specific leaf collections for the Tungsten project; canonical only within the sibling Tungsten port family and not a general collection foundation | `DataStructures.sln`, `src/Tools.DataStructures.Tungsten/`, `tests/Tools.DataStructures.Tungsten.Tests/` | [`docs`](../../src/CSharp/docs/Tungsten/README.md) |
| [`src/C/Tungsten`](../../src/C/Tungsten/README.md) | C17 Tungsten `List` and `Association` port | `include/tools/data_structures/tungsten/tungsten.h`, `CMakePresets.json` | [`README`](../../src/C/Tungsten/README.md) |
| [`src/Cpp/Tungsten`](../../src/Cpp/Tungsten/README.md) | C++23 Tungsten `List` and `Association` port | `include/tools/data_structures/tungsten/`, `CMakePresets.json` | [`README`](../../src/Cpp/Tungsten/README.md) |
| [`src/Haskell/Tungsten`](../../src/Haskell/Tungsten/README.md) | Haskell Tungsten `List` and `Association` port | `tools-data-structures-tungsten.cabal`, `src/Data/Structures/Tungsten/` | [`README`](../../src/Haskell/Tungsten/README.md) |
| [`src/Kotlin/Tungsten`](../../src/Kotlin/Tungsten/README.md) | Kotlin/JVM Tungsten `List` and `Association` port | `src/tools/datastructures/tungsten/`, `test/tools/datastructures/tungsten/` | [`README`](../../src/Kotlin/Tungsten/README.md) |
| [`src/Rust/Tungsten`](../../src/Rust/Tungsten/README.md) | Safe Rust Tungsten `List` and `Association` crate | `Cargo.toml`, `src/lib.rs` | [`README`](../../src/Rust/Tungsten/README.md) |
| [`src/TypeScript`](../../src/TypeScript/README.md) | Strict TypeScript/ESM port of the current HAMT/FingerTree-derived/Range/Ordered/Numerics surfaces plus application-leaf Tungsten | `package.json`, `src/`, `test.ps1` | [API notes](../../src/TypeScript/docs/api-notes.md), [validation](../../src/TypeScript/docs/validation.md) |
| [`src/Python`](../../src/Python/README.md) | Typed Python 3.11+ port of the current HAMT/FingerTree-derived/Range/Ordered/Numerics surfaces plus application-leaf Tungsten | `pyproject.toml`, `src/vladimir_reshetnikov/data_structures/`, `tests/`, `test.ps1` | [API notes](../../src/Python/docs/api-notes.md), [validation](../../src/Python/docs/validation.md), [tests](../../src/Python/tests/README.md) |

## Port Lineage

HAMT lineage:

1. C# HAMT (`src/CSharp/src/Tools.DataStructures.Hamt`) defines the managed public contract and model-test baseline.
2. `src/Cpp/Hamt` ports the HAMT semantics to C++ value types, templates, and `std::shared_ptr` node
   sharing, and ports the Merkle search tree through exact `MST2` blocks, bounded verified
   persistence, `MSP2` proofs, iterative synchronization, and present-null-safe typed merge.
3. `src/C/Hamt` ports the same structure to a type-erased C API with explicit clone/destroy
   ownership, and ports the Merkle search tree through fallible erased-type codecs/stores, exact
   `MST2`/`MSP2`, bounded verified persistence, synchronization, and present-null-safe merge.
4. `src/Haskell/Hamt` ports the persistent HAMT semantics to Haskell values with a package-local
   `Hashable` class and optional runtime `HashPolicy`, and ports the Merkle search tree through exact
   `MST2` blocks, pure immutable store snapshots, bounded verified persistence, `MSP2` proofs,
   iterative synchronization, and present/absent-safe typed merge.
5. `src/Kotlin/Hamt` ports the HAMT contract to Kotlin/JVM values, runtime `HashPolicy` objects, and
   JVM-reference structural sharing, owns the managed Ctrie, and ports the C# Merkle search tree
   through exact `MST2` blocks, bounded verified persistence, `MSP2` proofs, iterative
   synchronization, and present-null-aware typed merge.
6. `src/Rust/Hamt` ports the HAMT contract to Rust value types, `BuildHasher` hash policies, and
   `Arc` structural sharing, and ports the C# Merkle search tree through the exact `MST2` wire,
   bounded verified persistence, `MSP2` proofs, synchronization, and typed three-way merge.
7. `src/TypeScript` ports the persistent/transient CHAMP, Patricia, and exact `MST2`/`MSP2`
   contracts to strict ESM, with one-descent map factories, hash bag, strict bimap, reusable unpublished-node
   bulk construction, JavaScript-native policies, and isolate-local concurrency semantics.
8. `src/Python` ports the same contracts to Python 3.11+ with runtime `HashPolicy`, path-copy
   one-way sessions, one-descent map factories, hash bag, strict bimap, reusable unpublished-node bulk
   construction, a lock-coordinated thread-safe facade over persistent roots, and exact
   byte-compatible Merkle persistence/proofs with all seven verification budgets.

FingerTree lineage:

1. C# FingerTree (`src/CSharp/src/Tools.DataStructures.FingerTree`) is the broadest implementation and documentation source: tuned deque, general measured tree, derived sorted/priority/interval collections, ropes, text helpers, the independently implemented implicit-AVL `RangeUpdateSequence`, samples, benchmarks, and design notes.
2. `src/Cpp/FingerTree` ports the persistent family to a header-first C++23 library, adds the
   system-crypto-backed policy-canonical zip-zip set and move-only-capable Brodal-Okasaki and
   winner-cached priority-search cores, includes positional/measured/text root-sharing rope cursor
   checkpoints, and separately exposes native DABA Lite with deterministic reclamation and
   CMake/CTest validation.
3. `src/C/FingerTree` starts from the C++ port and exposes a C11 API with explicit handles,
   ownership, persistent facades, RRB vectors, an erased-type-safe policy-canonical zip-zip set, a
   failure-atomic type-erased Brodal-Okasaki heap and winner-cached priority-search queue,
   explicit-lifetime root-sharing positional/measured/text rope cursors, and mutable DABA Lite.
4. `src/Haskell/FingerTree` ports the family to Haskell with a general measured tree,
   deque/reversible deque, derived collections, the explicitly identified policy-canonical zip-zip
   set, intervals, ropes, positional/measured/text snapshot-plus-gap cursors, and text helpers.
5. `src/Kotlin/FingerTree` ports the persistent family to Kotlin/JVM over immutable measured AVL and
   RRB sequences, adds the keyed policy-canonical zip-zip sorted set, directly implements the
   bootstrapped skew-binomial Brodal-Okasaki heap and winner-cached AVL priority-search queue, and
   separately exposes the mutable six-cursor DABA Lite streaming aggregator.
6. `src/Rust/FingerTree` is a Rust semantic checkpoint for the persistent family names over shared
   tree/RRB storage, includes the keyed policy-canonical zip-zip sorted set and `Arc`-owned
   Brodal-Okasaki heap and winner-cached priority-search queue, and keeps a separate single-threaded
   DABA Lite whose deterministic-drop clear cost is documented locally.
7. `src/TypeScript` ports the measured family and all shipped derived/new cores, with persistent
   JavaScript gap cursors and a mutable single-isolate DABA Lite.
8. `src/Python` ports the measured family over immutable measured AVL and RRB substrates, including
   sorted/priority/interval facades, the canonical zip-zip set, Brodal and priority-search cores,
   mutable DABA Lite, and positional/measured/text gap cursors whose text offsets count Unicode code
   points.

Range-update lineage:

1. C# owns the semantic reference in
   `src/CSharp/src/Tools.DataStructures.FingerTree`: `IRangeUpdateAlgebra` defines the tag monoid and
   its count-aware action on elements and ordered measures, while `RangeUpdateSequence` owns the
   path-copied implicit-AVL node/tag invariant, indexed and range API, public struct enumerator,
   failure atomicity, and deterministic structural bounds. The exact contract is
   [range-update-sequence.md](../../src/CSharp/docs/FingerTree/range-update-sequence.md).
2. C, C++, Haskell, Kotlin, Rust, TypeScript, and Python now ship language-local ports preserving
   the algebra laws, logical cached-measure invariant, composition direction, persistence,
   validation, and failure semantics through their native policy and ownership idioms.
3. Both full serialized C# Debug and Release solution builds complete with zero warnings and zero
   errors, and both gates pass 1,417/1,417 tests. No benchmark was run; measurements remain
   postponed for isolation.

For the surrounding benchmark-independent tranches, single-pass HAMT updates,
`PersistentHashBag`, strict `PersistentBiMap`, and `PersistentOrderedSet` now ship across all eight
languages.

Ordered-set lineage:

1. C# Ordered (`src/CSharp/src/Tools.DataStructures.Ordered`) owns the neutral
   `PersistentOrderedSet<T>` contract, dual-index invariants, sparse labels, explicit movement,
   positional ranges, stable one-shot sort, receiver-policy algebra, and the enforced absence of a
   Tungsten dependency. Deterministic relabel fallback, eager failure behavior, focused serialized
   Debug/Release lanes, and the full serialized C# Release gate are locked by its hardened tests.
2. `src/TypeScript/src/ordered` ports the same observable contract to strict ESM with a runtime
   `HashPolicy`, persistent measured sequence, bigint-private labels, typed lookup/removal
   results, and a public invariant diagnostic used by package tests.
3. `src/Python/src/vladimir_reshetnikov/data_structures/ordered` ports it to typed Python with a
   retained `HashPolicy`, persistent deque order index, presence-safe dataclass results, and
   Python-native exceptions. Neither sibling package imports or delegates to its Tungsten module.
4. `src/C/Ordered`, `src/Cpp/Ordered`, `src/Haskell/Ordered`, `src/Kotlin/Ordered`, and
   `src/Rust/Ordered` complete the neutral sibling family through their language-local ownership
   models, never through Tungsten.

Tungsten collections lineage:

This lineage is application-local. It establishes parity only among the Tungsten ports; it creates
no dependency or semantic obligation for a general collection. New kernel evidence may revise the
whole lineage, and any generally useful mechanism must move by an independently owned fork rather
than by making another family depend on Tungsten.

1. C# Tungsten collections (`src/CSharp/src/Tools.DataStructures.Tungsten`) define the managed
   Tungsten-port contract: `PersistentList<T>` over the FingerTree deque and
   `PersistentAssociation<TKey, TValue>` composed per the historical
   [derived structure catalog](derived-structure-catalog.md) case study, with the kernel-verified
   Tungsten ordering rules as the family-local fidelity spec.
2. `src/Cpp/Tungsten`, `src/C/Tungsten`, `src/Haskell/Tungsten`, `src/Kotlin/Tungsten`,
   `src/Rust/Tungsten`, `src/TypeScript`, and `src/Python` port the same public family to their language-local ownership and policy
   models while preserving the substrate composition, sparse-stamp relabel behavior, and
   average/worst-case operation bounds.

Numerics originates in `src/CSharp/src/Tools.Numerics`; `src/TypeScript/src/numerics` and
`src/Python/src/vladimir_reshetnikov/data_structures/numerics` port the six fixed-width types,
binary/format semantics, `SparseInteger`, and `BitConverterEx` over their native arbitrary-precision
integer substrates.

When porting behavior across languages, prefer the managed workspace for the semantic contract, the adjacent
native workspace for local idioms, and the local tests for the exact validation shape. Use the
[porting and semantic parity guide](../guides/porting-and-semantic-parity.md) for the cross-language
change workflow.

## Documentation Organization

Repository-level docs live under `docs/`:

- [`docs/guides`](../guides/README.md) holds workflow documents: validation, agent workflows, and task procedures.
- [`docs/reference`](README.md) holds durable maps and cross-workspace reference material.
- [`docs/migration`](../migration/README.md) preserves extraction and history-filtering provenance.

Workspace-level docs live near the code they describe:

- C# API contracts and library-specific design notes belong under `src/CSharp/docs/<Family>/`.
- Other language API contracts and library-specific design notes belong under the family workspace's `docs/`
  directory.
- Python and TypeScript package-wide API and validation notes belong under their language-root
  `docs/` directories, with family modules under their package `src/` trees.
- Build entry points and quick orientation belong in the language-root or family-root `README.md`, whichever owns
  the build entry point.
- Long-lived repository-wide reports belong under `docs/`, not inside one language workspace.
- External study material remains segregated under [`src/CSharp/docs/FingerTree/external`](../../src/CSharp/docs/FingerTree/external/README.md).

## Naming And Path Conventions

- Use `CSharp`, not `Cs`, for the managed language root.
- Use `Cpp`, not `C++`, in paths so shell tooling and URLs stay simple.
- Use `Numerics` for fixed-width and sparse integer numeric workspaces.
- Use `Hamt` for hash-array mapped trie workspaces, matching the public project names.
- Use `FingerTree` for the measured finger-tree family, including derived collections and ropes.
- Use `Ordered` for independently owned insertion/explicit-position ordered general collections;
  do not place comparison-sorted collections or application-specific Tungsten surfaces there.
- Write current paths in active documentation. Put historical paths only in explicit provenance or review reports.
