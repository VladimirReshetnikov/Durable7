# Workspace Map

- Created (UTC): 2026-07-02T19:44:02Z
- Repository HEAD: 9bf68f498405e2dce44cb08fad08ea2bbe97d97c
- Audience: Maintainers and AI agents navigating the repository
- Scope: Repository organization, workspace roles, and documentation placement

The repository is organized by programming language first. Native, Haskell, Kotlin, and Rust roots keep
library-family directories directly under the language root. The C# root is a single managed solution
with projects grouped by role:

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
│   │   └── Tungsten/
│   ├── samples/
│   ├── src/
│   │   ├── Tools.DataStructures.FingerTree/
│   │   ├── Tools.DataStructures.Hamt/
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
└── Rust/
    ├── README.md
    ├── FingerTree/
    ├── Hamt/
    └── Tungsten/
```

This makes language-local build systems, toolchains, include paths, and idioms easy to find while keeping
related library families aligned across languages where ports exist. In C#, the single solution keeps
managed package boundaries intact while allowing one restore/build/test entry point for the whole managed
surface.

Use the [source index](../../src/README.md) when browsing by language, or jump directly to the
[C](../../src/C/README.md), [C++](../../src/Cpp/README.md), [C#](../../src/CSharp/README.md),
[Haskell](../../src/Haskell/README.md), [Kotlin](../../src/Kotlin/README.md), or
[Rust](../../src/Rust/README.md) language index.

For the cross-language list of public library surfaces, see the
[data structure catalog](data-structure-catalog.md). For the shared behavior, ownership, policy,
ordering, and documentation obligations that should remain recognizable across language ports, see the
[semantic contracts reference](semantic-contracts.md).

## Workspace Roles

| Workspace | Role | Main entry points | Local docs |
| --- | --- | --- | --- |
| [C# Numerics](../../src/CSharp/docs/Numerics/overview.md) | Managed fixed-width and sparse integer numerics library | `DataStructures.sln`, `src/Tools.Numerics/`, `tests/Tools.Numerics.Tests/` | [`docs`](../../src/CSharp/docs/Numerics/README.md) |
| [C# HAMT](../../src/CSharp/docs/Hamt/overview.md) | Canonical managed HAMT library | `DataStructures.sln`, `src/Tools.DataStructures.Hamt/`, `tests/Tools.DataStructures.Hamt.Tests/` | [`docs`](../../src/CSharp/docs/Hamt/README.md) |
| [`src/C/Hamt`](../../src/C/Hamt/README.md) | C17 HAMT/Patricia port and type-erased exact-wire Merkle core | `include/Tools/DataStructures/Hamt/*.h`, `build.ps1` | [`docs`](../../src/C/Hamt/docs/README.md) |
| [`src/Cpp/Hamt`](../../src/Cpp/Hamt/README.md) | C++20 HAMT/Patricia port and complete wire-compatible Merkle search tree | `include/Tools/DataStructures/Hamt/*.hpp`, `build.ps1` | [`docs`](../../src/Cpp/Hamt/docs/README.md), [Merkle core](../../src/Cpp/Hamt/docs/merkle-search-tree.md), [persistence](../../src/Cpp/Hamt/docs/merkle-persistence.md) |
| [`src/Haskell/Hamt`](../../src/Haskell/Hamt/README.md) | Haskell HAMT/Patricia port and complete pure wire-compatible Merkle search tree | `tools-data-structures-hamt.cabal`, `src/Data/Structures/Hamt/` | [`README`](../../src/Haskell/Hamt/README.md), [Merkle guide](../../src/Haskell/Hamt/docs/merkle-search-tree.md) |
| [`src/Kotlin/Hamt`](../../src/Kotlin/Hamt/README.md) | Kotlin/JVM HAMT/Ctrie/Patricia port and complete wire-compatible Merkle search tree | `src/tools/datastructures/hamt/`, `test/tools/datastructures/hamt/` | [`docs`](../../src/Kotlin/Hamt/docs/README.md) |
| [`src/Rust/Hamt`](../../src/Rust/Hamt/README.md) | Rust HAMT/Patricia port and wire-compatible Merkle search tree | `Cargo.toml`, `src/lib.rs`, `src/merkle_search_tree.rs` | [`docs`](../../src/Rust/Hamt/docs/README.md) |
| [C# FingerTree](../../src/CSharp/docs/FingerTree/overview.md) | Canonical managed FingerTree library | `DataStructures.sln`, `src/Tools.DataStructures.FingerTree/`, `tests/Tools.DataStructures.FingerTree.Tests/`, `samples/`, `benchmarks/` | [`docs`](../../src/CSharp/docs/FingerTree/README.md) |
| [`src/Cpp/FingerTree`](../../src/Cpp/FingerTree/README.md) | C++23 FingerTree/RRB/canonical-set family with Brodal/PSQ cores plus native DABA Lite | `include/tools/data_structures/finger_tree/`, `CMakePresets.json` | [`docs`](../../src/Cpp/FingerTree/docs/README.md) |
| [`src/C/FingerTree`](../../src/C/FingerTree/README.md) | C11 FingerTree/RRB/canonical-set family with type-erased Brodal/PSQ cores plus DABA Lite | `include/tools/data_structures/finger_tree/`, `CMakePresets.json` | [`docs`](../../src/C/FingerTree/docs/README.md) |
| [`src/Haskell/FingerTree`](../../src/Haskell/FingerTree/README.md) | Haskell FingerTree/RRB/canonical-set family port | `tools-data-structures-fingertree.cabal`, `src/Data/Structures/FingerTree/` | [`README`](../../src/Haskell/FingerTree/README.md) |
| [`src/Kotlin/FingerTree`](../../src/Kotlin/FingerTree/README.md) | Kotlin/JVM persistent measured-tree/RRB/canonical-set/optimal-priority family plus managed DABA Lite | `src/tools/datastructures/fingertree/`, `test/tools/datastructures/fingertree/` | [`docs`](../../src/Kotlin/FingerTree/docs/README.md) |
| [`src/Rust/FingerTree`](../../src/Rust/FingerTree/README.md) | Rust FingerTree/RRB/canonical-set checkpoint with non-`Clone` Brodal/PSQ cores plus single-threaded DABA Lite | `Cargo.toml`, `src/` | [`docs`](../../src/Rust/FingerTree/docs/README.md) |
| [C# Tungsten collections](../../src/CSharp/docs/Tungsten/overview.md) | Canonical managed Tungsten-semantics collections (list facade and insertion-ordered association) composed from the HAMT and FingerTree families | `DataStructures.sln`, `src/Tools.DataStructures.Tungsten/`, `tests/Tools.DataStructures.Tungsten.Tests/` | [`docs`](../../src/CSharp/docs/Tungsten/README.md) |
| [`src/C/Tungsten`](../../src/C/Tungsten/README.md) | C17 Tungsten `List` and `Association` port | `include/tools/data_structures/tungsten/tungsten.h`, `CMakePresets.json` | [`README`](../../src/C/Tungsten/README.md) |
| [`src/Cpp/Tungsten`](../../src/Cpp/Tungsten/README.md) | C++23 Tungsten `List` and `Association` port | `include/tools/data_structures/tungsten/`, `CMakePresets.json` | [`README`](../../src/Cpp/Tungsten/README.md) |
| [`src/Haskell/Tungsten`](../../src/Haskell/Tungsten/README.md) | Haskell Tungsten `List` and `Association` port | `tools-data-structures-tungsten.cabal`, `src/Data/Structures/Tungsten/` | [`README`](../../src/Haskell/Tungsten/README.md) |
| [`src/Kotlin/Tungsten`](../../src/Kotlin/Tungsten/README.md) | Kotlin/JVM Tungsten `List` and `Association` port | `src/tools/datastructures/tungsten/`, `test/tools/datastructures/tungsten/` | [`README`](../../src/Kotlin/Tungsten/README.md) |
| [`src/Rust/Tungsten`](../../src/Rust/Tungsten/README.md) | Safe Rust Tungsten `List` and `Association` crate | `Cargo.toml`, `src/lib.rs` | [`README`](../../src/Rust/Tungsten/README.md) |

## Port Lineage

HAMT lineage:

1. C# HAMT (`src/CSharp/src/Tools.DataStructures.Hamt`) defines the managed public contract and model-test baseline.
2. `src/Cpp/Hamt` ports the HAMT semantics to C++ value types, templates, and `std::shared_ptr` node
   sharing, and ports the Merkle search tree through exact `MST2` blocks, bounded verified
   persistence, `MSP2` proofs, iterative synchronization, and present-null-safe typed merge.
3. `src/C/Hamt` ports the same structure to a type-erased C API with explicit clone/destroy
   ownership, and ports the Merkle core/wire to fallible erased-type codecs and failure-atomic
   handles while leaving its persistence tier separate.
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

FingerTree lineage:

1. C# FingerTree (`src/CSharp/src/Tools.DataStructures.FingerTree`) is the broadest implementation and documentation source: tuned deque, general measured tree, derived sorted/priority/interval collections, ropes, text helpers, samples, benchmarks, and design notes.
2. `src/Cpp/FingerTree` ports the persistent family to a header-first C++23 library, adds the
   system-crypto-backed policy-canonical zip-zip set and move-only-capable Brodal-Okasaki and
   winner-cached priority-search cores, and separately exposes native DABA Lite with deterministic
   reclamation and CMake/CTest validation.
3. `src/C/FingerTree` starts from the C++ port and exposes a C11 API with explicit handles,
   ownership, persistent facades, RRB vectors, an erased-type-safe policy-canonical zip-zip set, a
   failure-atomic type-erased Brodal-Okasaki heap and winner-cached priority-search queue, and
   mutable DABA Lite.
4. `src/Haskell/FingerTree` ports the family to Haskell with a general measured tree,
   deque/reversible deque, derived collections, the explicitly identified policy-canonical zip-zip
   set, intervals, ropes, and text helpers.
5. `src/Kotlin/FingerTree` ports the persistent family to Kotlin/JVM over immutable measured AVL and
   RRB sequences, adds the keyed policy-canonical zip-zip sorted set, directly implements the
   bootstrapped skew-binomial Brodal-Okasaki heap and winner-cached AVL priority-search queue, and
   separately exposes the mutable six-cursor DABA Lite streaming aggregator.
6. `src/Rust/FingerTree` is a Rust semantic checkpoint for the persistent family names over shared
   tree/RRB storage, includes the keyed policy-canonical zip-zip sorted set and `Arc`-owned
   Brodal-Okasaki heap and winner-cached priority-search queue, and keeps a separate single-threaded
   DABA Lite whose deterministic-drop clear cost is documented locally.

Tungsten collections lineage:

1. C# Tungsten collections (`src/CSharp/src/Tools.DataStructures.Tungsten`) define the managed public
   contract: `PersistentList<T>` over the FingerTree deque and `PersistentAssociation<TKey, TValue>`
   composed per the [derived structure catalog](derived-structure-catalog.md)'s
   `PersistentOrderedMap` pattern, with the kernel-verified Tungsten ordering rules as the fidelity
   spec.
2. `src/Cpp/Tungsten`, `src/C/Tungsten`, `src/Haskell/Tungsten`, `src/Kotlin/Tungsten`, and
   `src/Rust/Tungsten` port the same public family to their language-local ownership and policy
   models while preserving the substrate composition, sparse-stamp relabel behavior, and
   average/worst-case operation bounds.

Numerics currently has a C# project only. `src/CSharp/src/Tools.Numerics` owns the fixed-width integer and
sparse-integer contract and implementation, with tests under `src/CSharp/tests/Tools.Numerics.Tests`; add
future ports or generated variants as separate language-family workspaces only when they have their own
toolchain and validation shape.

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
- Write current paths in active documentation. Put historical paths only in explicit provenance or review reports.
