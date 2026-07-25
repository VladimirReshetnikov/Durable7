# Durable7

- Status: Active standalone repository
- Created (UTC): 2026-06-30T01:28:46Z
- Repository HEAD: d8c6160a9d3ae266e310089bfa73d71cc76ed5c3
- Audience: Maintainers and AI coding agents working on repository-owned data structures and numerics
- Scope: Repository layout, build entry points, and agent guidance

**Durable7** is Vladimir Reshetnikov's library of persistent data structures, authenticated
collections, and fixed-width numerics, shipped as semantically aligned ports across nine languages.
`Durable7` is the single brand for every port: C# namespaces are `Durable7.*`, C++ and OCaml use
`durable7`, C uses the `d7_` identifier prefix and `durable7/` include roots, Haskell modules are
`Durable7.*`, and the Kotlin, Rust, TypeScript, and Python packages are all named `durable7`. The
pre-rebrand namespace roots and the old C identifier prefix are retired: no new code, documentation,
build script, or package metadata may reintroduce them.

This repository contains the Durable7 workspaces and design references. It was extracted from `C:\Tools0\src\DataStructures` / `VladimirReshetnikov/Tools` with path-local Git history preserved as precisely as practical. The Tools-side handoff is recorded by [`5fc4054da`](https://github.com/VladimirReshetnikov/Tools/commit/5fc4054da), which removes the former subtree and points the Tools indexes here.

This document is the canonical repository guidance for Vladimir and the AI coding agents that help him. `AGENTS.md` and `CLAUDE.md` point here, so keep shared project and agent instructions in this file.

## Tungsten Dependency Boundary

Tungsten collections are application-specific leaf consumers for the Tungsten project, an
alternative interpreter for Wolfram Language. Their behavior may change as Wolfram-kernel behavior
is newly discovered, reinterpreted, or changed, and the Tungsten workspaces may move out of this
repository.

Dependency direction is therefore one-way:

- Tungsten may consume repository-general HAMT, FingerTree, and other data-structure libraries.
- No general-purpose collection, new data structure, or non-Tungsten project may reference a
  Tungsten package or type, use Tungsten as its implementation substrate, or adopt Tungsten behavior
  as its semantic baseline.
- If a Tungsten mechanism is attractive generally, fork it into an independently owned
  implementation with its own API, contracts, tests, and evolution policy. General code must not
  wrap, delegate to, subclass, or remain constrained by the Tungsten implementation.
- Kernel-driven Tungsten changes flow only through the sibling Tungsten ports. They do not
  automatically flow into an independent general-purpose fork.

The detailed normative policy, including allowed references, fork requirements, extraction rules,
and a worked ordered-set example, is the
[Tungsten application-leaf dependency boundary](docs/reference/tungsten-application-leaf-boundary.md).

## Where to start

| Goal | Start with | Then open |
| --- | --- | --- |
| Get oriented in the repository | [Repository onboarding](docs/guides/repository-onboarding.md) | [Workspace map](docs/reference/workspace-map.md), [source index](src/README.md), [navigation matrix](docs/reference/navigation-matrix.md) |
| Choose or compare a collection family | [Data-structure catalog](docs/reference/data-structure-catalog.md) | Relevant workspace usage guide and API specification or notes |
| Preserve behavior across ports | [Semantic contracts](docs/reference/semantic-contracts.md) | [Porting guide](docs/guides/porting-and-semantic-parity.md), sibling workspace API docs, test READMEs |
| Build or validate changes | [Build and validation](docs/guides/build-and-validation.md) | [Test suite map](docs/reference/test-suite-map.md), affected workspace validation guide |
| Update documentation | [Documentation maintenance](docs/guides/documentation-maintenance.md) | Affected docs index, [navigation matrix](docs/reference/navigation-matrix.md), repository-owned Markdown checks |

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
├── eng/
│   ├── Enable-HeadlessTestMode.ps1
│   ├── HeadlessTest.cmake
│   └── Invoke-HeadlessTest.ps1
└── src/
    ├── README.md
    ├── C/
    │   ├── README.md
    │   ├── build.ps1
    │   ├── FingerTree/
    │   │   ├── CMakeLists.txt
    │   │   ├── CMakePresets.json
    │   │   ├── README.md
    │   │   ├── benchmarks/
    │   │   ├── docs/
    │   │   ├── include/
    │   │   ├── samples/
    │   │   ├── src/
    │   │   └── tests/
    │   ├── Hamt/
    │   │   ├── build.ps1
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── include/
    │   │   ├── src/
    │   │   └── tests/
    │   └── Tungsten/
    │       ├── CMakeLists.txt
    │       ├── CMakePresets.json
    │       ├── README.md
    │       ├── include/
    │       ├── src/
    │       └── tests/
    ├── Cpp/
    │   ├── README.md
    │   ├── build.ps1
    │   ├── FingerTree/
    │   │   ├── CMakeLists.txt
    │   │   ├── CMakePresets.json
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── include/
    │   │   └── tests/
    │   ├── Hamt/
    │   │   ├── build.ps1
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── include/
    │   │   └── tests/
    │   └── Tungsten/
    │       ├── CMakeLists.txt
    │       ├── CMakePresets.json
    │       ├── README.md
    │       ├── include/
    │       └── tests/
    ├── CSharp/
    │   ├── README.md
    │   ├── Durable7.sln
    │   ├── Directory.Build.props
    │   ├── Directory.Build.targets
    │   ├── test.ps1
    │   ├── test.runsettings
    │   ├── benchmarks/
    │   │   └── Durable7.FingerTree.Benchmarks/
    │   ├── docs/
    │   │   ├── FingerTree/
    │   │   ├── Hamt/
    │   │   ├── Numerics/
    │   │   ├── Ordered/
    │   │   └── Tungsten/
    │   ├── samples/
    │   │   ├── Durable7.FingerTree.Editor/
    │   │   ├── Durable7.FingerTree.Showcase/
    │   │   └── Durable7.FingerTree.Tour/
    │   ├── src/
    │   │   ├── Durable7.FingerTree/
    │   │   ├── Durable7.Hamt/
    │   │   ├── Durable7.Ordered/
    │   │   ├── Durable7.Tungsten/
    │   │   └── Durable7.Numerics/
    │   └── tests/
    │       ├── Durable7.FingerTree.Tests/
    │       ├── Durable7.Hamt.Tests/
    │       ├── Durable7.Ordered.Tests/
    │       ├── Durable7.Tungsten.Tests/
    │       └── Durable7.Numerics.Tests/
    ├── Haskell/
    │   ├── README.md
    │   ├── cabal.project
    │   ├── test.ps1
    │   ├── FingerTree/
    │   │   ├── README.md
    │   │   ├── durable7-fingertree.cabal
    │   │   ├── src/
    │   │   └── test/
    │   ├── Hamt/
    │   │   ├── README.md
    │   │   ├── durable7-hamt.cabal
    │   │   ├── src/
    │   │   └── test/
    │   └── Tungsten/
    │       ├── README.md
    │       ├── durable7-tungsten.cabal
    │       ├── src/
    │       └── test/
    ├── Kotlin/
    │   ├── README.md
    │   ├── build.ps1
    │   ├── FingerTree/
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── src/
    │   │   ├── test/
    │   │   └── tests/
    │   ├── Hamt/
    │   │   ├── README.md
    │   │   ├── docs/
    │   │   ├── src/
    │   │   ├── test/
    │   │   └── tests/
    │   └── Tungsten/
    │       ├── README.md
    │       ├── src/
    │       └── test/
    ├── OCaml/
    │   ├── README.md
    │   ├── dune-project
    │   ├── durable7.opam
    │   ├── test.ps1
    │   ├── docs/
    │   ├── lib/
    │   │   ├── common/
    │   │   ├── finger_tree/
    │   │   ├── hamt/
    │   │   ├── numerics/
    │   │   ├── ordered/
    │   │   └── tungsten/
    │   └── tests/
    ├── Python/
    │   ├── README.md
    │   ├── pyproject.toml
    │   ├── requirements-dev.txt
    │   ├── test.ps1
    │   ├── docs/
    │   ├── src/
    │   │   └── durable7/
    │   │       ├── finger_tree/
    │   │       ├── hamt/
    │   │       ├── numerics/
    │   │       ├── ordered/
    │   │       └── tungsten/
    │   └── tests/
    ├── test_support/
    │   └── include/
    ├── Rust/
        ├── Cargo.toml
        ├── README.md
        ├── test.ps1
        ├── Hamt/
        │   ├── Cargo.toml
        │   ├── README.md
        │   ├── docs/
        │   ├── src/
        │   └── tests/
        ├── FingerTree/
        │   ├── Cargo.toml
        │   ├── README.md
        │   ├── docs/
        │   ├── src/
        │   └── tests/
        └── Tungsten/
            ├── Cargo.toml
            ├── README.md
            └── src/
    └── TypeScript/
        ├── README.md
        ├── package.json
        ├── package-lock.json
        ├── test.ps1
        ├── docs/
        ├── src/
        │   ├── finger-tree/
        │   ├── hamt/
        │   ├── numerics/
        │   ├── ordered/
        │   └── tungsten/
        └── test/
```

## Workspaces

The [source index](src/README.md) and language indexes for [C](src/C/README.md),
[C++](src/Cpp/README.md), [C#](src/CSharp/README.md), [Haskell](src/Haskell/README.md),
[Kotlin](src/Kotlin/README.md), [OCaml](src/OCaml/README.md), [Python](src/Python/README.md), [Rust](src/Rust/README.md), and
[TypeScript](src/TypeScript/README.md) are the quickest way to browse the
language-first layout.

All nine ports ship the shared persistent-cursor tier for Patricia maps/sets; measured and
positional sequence families; sorted, canonical, priority-search, interval, and sparse-bit
collections; neutral Ordered set/map/multimap; and authenticated Merkle search trees. Public
cursors are immutable version-bound gaps or ordered search locations. Unordered CHAMP composites,
live Ctries, graphs, non-search heaps, lifecycle/support objects, numerics, DABA Lite, and Tungsten
collections intentionally have no public cursor. The
[repository-wide cursor design](docs/proposals/repository-wide-persistent-cursor-design.md) is the
exhaustive applicability, API, ownership, complexity, and validation contract.

- [C# Numerics](src/CSharp/docs/Numerics/overview.md) is a .NET 10 fixed-width and sparse integer numerics library under [src/CSharp/src/Durable7.Numerics](src/CSharp/src/Durable7.Numerics/Durable7.Numerics.csproj). It provides `UInt256`/`Int256`, `UInt512`/`Int512`, `UInt1024`/`Int1024`, `SparseInteger`, deterministic two's-complement and binary conversion semantics, declaration-parity guardrails, and xUnit tests.
- [C# HAMT](src/CSharp/docs/Hamt/overview.md) is a .NET 10 hash-trie library under [src/CSharp/src/Durable7.Hamt](src/CSharp/src/Durable7.Hamt/Durable7.Hamt.csproj). Its canonical CHAMP `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>` preserve comparers, stored representatives, and structural sharing; the map exposes one-descent persistent `GetOrAdd`/`AddOrUpdate`, and both collections expose optimized single-owner `Transient` sessions with owner-token in-place edits, O(1) adoption, and one-way O(1) publication. `PersistentHashBag<T>`, strict `PersistentBiMap<TKey, TValue>`, set-valued `PersistentHashMultimap<TKey, TValue>`, bidirectional `PersistentRelation<TLeft, TRight>`, strict `PersistentMapPatch<TKey, TValue>`, `PersistentDirectedGraph<TVertex>`, and `PersistentIndexedMap<TKey, TValue, TIndexKey>` add composition-first families with retained policies and atomic multi-index publication. These derived families ship across all nine languages. All eight siblings expose the same semantic edit-then-publish lifecycle through language-local sessions whose changed point edits remain persistent path copies and carry no performance claim. The workspace also owns the lock-free snapshotting Ctrie, 32/64-bit Patricia maps and sets, and the policy-bound Merkle search tree; xUnit/CsCheck suites cover persistent, transient, and concurrent behavior.
- [C# FingerTree](src/CSharp/docs/FingerTree/overview.md) is a .NET 10 persistent-sequence library under [src/CSharp/src/Durable7.FingerTree](src/CSharp/src/Durable7.FingerTree/Durable7.FingerTree.csproj): two finger-tree engines (a tuned catenable deque and a general monoid-measured tree), a full derived collection family including payload-bearing `PersistentIntervalMap<TEndpoint, TValue>`, sparse rank/select `PersistentChunkedBitSet`, RRB vectors, ropes/text with version-bound cursors, and the independently implemented implicit-AVL `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>`. The range-update sibling combines indexed persistent edits with lazy logarithmic range updates and range measures under the law-gated `IRangeUpdateAlgebra`; its cached logical-measure and pending-tag invariant is specified in the [range-update contract](src/CSharp/docs/FingerTree/range-update-sequence.md). Language-local IntervalMap, chunked-bit-set, and Range siblings ship in C, C++, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml. Both complete serialized C# Debug and Release solution builds finish with zero warnings and zero errors, and both full test gates pass 1,503/1,503 tests. It also ships navigable design notes, three runnable samples, and example/property/model/concurrency suites. Benchmarks were not run for this shipment and remain postponed until an isolated session.
- [C# Ordered collections](src/CSharp/docs/Ordered/overview.md) is an independently owned neutral .NET 10 general-purpose library under [src/CSharp/src/Durable7.Ordered](src/CSharp/src/Durable7.Ordered/Durable7.Ordered.csproj). `PersistentOrderedSet<T>`, `PersistentOrderedMap<TKey, TValue>`, and `PersistentOrderedMultimap<TKey, TValue>` separate equality-defined identity from insertion and explicit-position order, retain first key/value representatives, and own explicit movement, positional range, stable one-shot sort, sparse-label, relabel, and grouped-order contracts; the set additionally owns receiver-policy algebra. Their indexes compose public CHAMP and FingerTree surfaces, and the project and its tests have no Tungsten dependency or Tungsten semantic baseline. Neutral sibling ports ship in C, C++, Haskell, Kotlin, Rust, TypeScript, Python, and OCaml. The current complete serialized C# Debug and Release gates each pass 1,503/1,503 tests with zero build warnings or errors. Benchmarks remain postponed until they can run in isolation.
- [C# Tungsten collections](src/CSharp/docs/Tungsten/overview.md) is a .NET 10 application-specific leaf library under [src/CSharp/src/Durable7.Tungsten](src/CSharp/src/Durable7.Tungsten/Durable7.Tungsten.csproj) composing the HAMT and FingerTree families into persistent collections for the Tungsten project: `PersistentList<T>` (the `List` operation vocabulary over the catenable deque) and `PersistentAssociation<TKey, TValue>` (an insertion-ordered map with keyed and positional access following the kernel-verified `Association` ordering rules). The primary external client is the Tungsten engine in the Smithereens repository; the C# implementation is the semantic reference only for sibling Tungsten ports and is never a foundation for general collections.
- [src/C/Hamt](src/C/Hamt/README.md) is a C17 port of the persistent HAMT library. It provides type-erased
  `d7_hamt_map`, `d7_hamt_set`, `d7_hamt_bag`, strict `d7_hamt_bi_map`, set-valued
  `d7_hamt_multimap`, and bidirectional `d7_hamt_relation` value structs with callback-driven hash/equality/ownership
  policy, explicit ref-counted one-way edit-session handles whose aliases share lifecycle status,
  Patricia integer maps/sets, and a type-erased Merkle search tree with exact cross-language
  `MST2` blocks, bounded verified persistence, `MSP2` proofs, synchronization, and present-null-safe
  merge. Atomic immutable handles, a synchronized block store, fallible callbacks and allocators,
  failure-atomic publication, exhaustive failpoints, and native concurrency tests define its C
  ownership and trust-boundary surface.
- [src/C/FingerTree](src/C/FingerTree/README.md) is the C11 port from the C++ workspace. It provides the measured-tree/deque family, RRB vectors, derived sorted/priority/interval collections, a type-erased CNG/OpenSSL-backed policy-canonical zip-zip set, failure-atomic type-erased Brodal-Okasaki and winner-cached priority-search cores, ropes/text with explicit-lifetime positional/measured/text cursors, and a separate mutable DABA Lite with allocation-atomic updates and O(n+c) deterministic clear, all covered by CTest and a dependency-light benchmark harness.
- [src/C/Tungsten](src/C/Tungsten/README.md) is the C17 port of the Tungsten collections. It provides type-erased `d7_tungsten_list` and `d7_tungsten_association` value structs, composing the C FingerTree deque, C HAMT, and an internal ref-counted AVL stamp sequence for C#-parity keyed and positional Association operations.
- [src/Cpp/Hamt](src/Cpp/Hamt/README.md) is a C++20 port of the persistent HAMT library. It provides
  header-first CHAMP hash maps/sets, a checked hash bag, a strict persistent bimap, a set-valued
  multimap, a bidirectional relation, move-only one-way edit sessions, Patricia integer maps/sets,
  plus a policy-bound Merkle
  search tree with CNG/OpenSSL SHA-256, byte-identical `MST2` blocks, seven bounded-verification
  limits, immutable block-store snapshots, exact `MSP2` proofs, iterative synchronization, and
  present-null-safe three-way merge. Immutable `std::shared_ptr` state supports structural sharing
  and move-only representatives; strict MSVC/GCC/Clang model, wire, failure, validation,
  concurrency, analyzer, and packaged-header gates cover the native value-semantics surface.
- [src/Cpp/FingerTree](src/Cpp/FingerTree/README.md) is the native C++ port of the FingerTree workspace and newer sequence/streaming cores. It is a header-first CMake/Ninja library with the persistent engines and facades, RRB vectors, a CNG/OpenSSL-backed policy-canonical zip-zip set, move-only-capable Brodal-Okasaki and winner-cached priority-search cores, ropes/text, positional/measured/text snapshot-plus-gap cursors, and a noncopyable mutable DABA Lite whose no-throw publication and O(n+c) deterministic clear are covered by CTest and benchmarks.
- [src/Cpp/Tungsten](src/Cpp/Tungsten/README.md) is the C++23 header-first Tungsten-collections port. It provides `persistent_list<T>` and `persistent_association<Key, T, Hash, KeyEqual, ValueEqual>` over the C++ FingerTree and HAMT substrates, with CTest coverage for the Tungsten ordering rules and relabel path.
- [src/Haskell/Hamt](src/Haskell/Hamt/README.md) is a Haskell persistent-map workspace. It provides CHAMP hash maps/sets, a checked hash bag, strict `BiMap`, set-valued `HashMultimap`, bidirectional `Relation`, one-way `MapTransient`/`SetTransient` sessions in `IO`, Patricia integer maps/sets, and a pure wire-compatible Merkle search tree with immutable block-store snapshots, bounded verification, `MSP2` proofs, frontier synchronization, and typed merge.
- [src/Haskell/FingerTree](src/Haskell/FingerTree/README.md) is a Haskell port of the FingerTree family. It provides a general measured tree, size-measured deque, reversible deque, sorted bag/set/map facades, an IO-created/purely operated policy-canonical zip-zip set, Brodal-Okasaki and measured priority queues, a keyed priority-search queue, interval tree, positional and measured ropes, immutable snapshot-plus-gap positional/measured/text cursors, and newline-aware text helpers.
- [src/Haskell/Tungsten](src/Haskell/Tungsten/README.md) is a Haskell port of the Tungsten collections. It provides `PersistentList` and `PersistentAssociation` modules over the Haskell FingerTree and HAMT packages, with a balanced stamp sequence for ordered Association operations.
- [src/Kotlin/Hamt](src/Kotlin/Hamt/README.md) is a Kotlin/JVM persistent-map workspace. It provides CHAMP hash maps/sets, a checked hash bag, strict `PersistentBiMap`, set-valued `PersistentHashMultimap`, bidirectional `PersistentRelation`, runtime-consumed version-view-bound `Transient` sessions, the managed Ctrie, Patricia integer maps/sets, and a wire-compatible Merkle search tree with bounded verified persistence, `MSP2` proofs, frontier synchronization, and typed three-way merge.
- [src/Kotlin/FingerTree](src/Kotlin/FingerTree/README.md) is a Kotlin/JVM port of the FingerTree-family collections and newer sequence/streaming cores. Its immutable measured AVL, RRB, canonical zip-zip-tree, bootstrapped skew-binomial heap, and winner-cached AVL substrates provide structurally shared deque, measured sequence, vector, sorted bag/set/map, policy-canonical sorted set, both measured and worst-case-optimal meldable priority queues, a keyed priority-search queue, max-high interval tree, positional/measured ropes, snapshot-plus-gap positional and measured cursors, and a UTF-16 text cursor that retains the `TextRope` facade; the separate mutable DABA Lite core maintains FIFO monoid aggregates with worst-case bounded callbacks.
- [src/Kotlin/Tungsten](src/Kotlin/Tungsten/README.md) is the Kotlin/JVM Tungsten-collections port. It exposes `PersistentList<T>` and `PersistentAssociation<K, V>` with immutable snapshots, runtime `HashPolicy` support, sparse stamps, and generated-history executable tests.
- [src/OCaml](src/OCaml/README.md) is the opam/Dune port of every repository-owned family: fixed-width and sparse numerics; CHAMP/derived HAMT, Patricia, synchronized snapshots, and exact `MST2` Merkle persistence/proofs; measured, sorted, priority, interval, vector, bit-set, Range, rope/cursor, canonical-set, heap/search, and DABA collections; neutral ordered set/map/multimap; and application-leaf Tungsten List/Association. Strict warnings, ocamlformat, odoc, Alcotest, QCheck, and one-worker validation define its gate; [API notes](src/OCaml/docs/api-notes.md) record intentional OCaml implementation distinctions.
- [src/Rust/Hamt](src/Rust/Hamt/README.md) is a safe Rust persistent-map workspace. It provides
  CHAMP hash maps/sets, a checked hash bag, strict `PersistentBiMap`, set-valued
  `PersistentHashMultimap`, bidirectional `PersistentRelation`, ownership-consuming `TransientHashMap`/`TransientHashSet` sessions, and
  Patricia integer maps/sets, plus a policy-bound Merkle search tree with
  C#-compatible `MST2` blocks, bounded verified persistence, `MSP2` proofs, synchronization, and
  three-way merge. Immutable nodes and values use `Arc` sharing and the crate forbids unsafe code.
- [src/Rust/FingerTree](src/Rust/FingerTree/README.md) is the Rust checkpoint port of the FingerTree
  family and newer sequence/streaming cores. Its persistent deque, measured sequence, RRB vector,
  policy-canonical zip-zip sorted set, non-`Clone` bootstrapped skew-binomial heap, winner-cached
  priority-search queue, reversible deque, sorted bag/set/map, priority queue, interval tree, and
  rope/text facades use structurally shared Rust storage; `RopeCursor<T>`,
  `MeasuredRopeCursor<T, P>`, and `TextRopeCursor` add positional, measured, and nominal text
  snapshot-plus-gap editing checkpoints. The separate mutable
  `DabaLite<T, M>` preserves bounded FIFO aggregation callbacks and prompt deterministic
  reclamation; it is `!Send`/`!Sync`, and `clear` is explicitly O(n + c) because owned values must
  be dropped.
- [src/Rust/Tungsten](src/Rust/Tungsten/README.md) is the safe Rust Tungsten-collections crate. It exposes
  `PersistentList<T>` and `PersistentAssociation<K, V, S>` over the Rust FingerTree and HAMT crates,
  preserving the Tungsten Association ordering rules, slicing, sorting, and relabel behavior.
- [src/TypeScript](src/TypeScript/README.md) is the strict ESM port for Node.js 24+. It packages the
  HAMT/transient/Ctrie/Patricia/Merkle family—including one-descent map factories, the hash bag,
  strict bimap, set-valued multimap, bidirectional relation, construction-only bulk builder, and
  complete transient-set relations—measured sequence and derived FingerTree collections,
  RRB/canonical-set/Brodal/priority-search/DABA cores, positional/measured/text rope cursors,
  the neutral insertion-ordered set, Tungsten `List`/`Association`, and fixed-width numerics. Its `MST2`/`MSP2` wire is byte-identical
  to the sibling ports; runtime-specific concurrency and owner-token performance distinctions are
  documented locally.
- [src/Python](src/Python/README.md) is the typed Python 3.11+ distribution. It packages CHAMP with
  one-descent map factories, a construction-only bulk builder, the hash bag, strict bimap, set-valued
  multimap, bidirectional relation, and complete path-copy
  one-way sessions, a lock-coordinated concurrent facade, Patricia maps/sets, the exact
  `MST2`/`MSP2` Merkle tier with seven verification budgets, measured-AVL and RRB sequence families,
  canonical zip-zip/Brodal/priority-search/DABA cores, code-point-indexed rope cursors, the
  neutral insertion-ordered set, application-leaf Tungsten `List`/`Association`, and bigint-backed fixed-width/sparse numerics.
  Ruff, strict Mypy, pytest/Hypothesis, source/wheel builds, metadata checks, and an installed-wheel
  smoke test form its validation gate.

## Build and test

Use [docs/guides/build-and-validation.md](docs/guides/build-and-validation.md) as the complete validation guide. In short, use the local .NET SDK toolchain for the C# workspace, the language-root MSVC build wrappers for C and C++, cabal for the Haskell packages, Cargo for the Rust crates, opam/Dune for OCaml, npm for TypeScript, and Python 3.11+ with the checked-in launcher for Python.

```powershell
cd C:\DataStructures\src\CSharp
dotnet restore --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
.\test.ps1

cd C:\DataStructures\src\C
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace Hamt -Configuration Release -RunTests
.\build.ps1 -Workspace Tungsten -RunTests
.\build.ps1 -Workspace Tungsten -Configuration Release -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace Hamt -RunTests
.\build.ps1 -Workspace Hamt -Configuration Release -RunTests
.\build.ps1 -Workspace Tungsten -RunTests

cd C:\DataStructures\src\Rust
.\test.ps1

cd C:\DataStructures\src\TypeScript
npm ci
npm run validate

cd C:\DataStructures\src\Python
.\test.ps1

cd C:\DataStructures\src\C
.\build.ps1 -Workspace FingerTree -RunTests

cd C:\DataStructures\src\Cpp
.\build.ps1 -Workspace FingerTree -RunTests

cd C:\DataStructures\src\Haskell
.\test.ps1

cd C:\DataStructures\src\Kotlin
.\build.ps1

cd C:\DataStructures\src\OCaml
opam install . --deps-only --with-test --with-doc --with-dev-setup
opam exec -- dune build -j 1 @check @fmt @doc
opam exec -- dune runtest -j 1 --force
```

The checked-in launchers and native presets force one build worker/job; test runners are likewise
restricted to one test host/thread where their toolchain supports it. Run language workspaces
sequentially rather than overlapping restore, build, or test processes. Local benchmarks are a
separate, explicit activity and are not part of routine validation. The C++ GitHub workflow's short
harness probes are isolated compile/runtime smoke checks, not performance evidence.

Run benchmarks from the benchmark project:

```powershell
cd C:\DataStructures\src\CSharp\benchmarks\Durable7.FingerTree.Benchmarks
dotnet run -c Release -- --filter * --job short
```

Release configuration is required for meaningful benchmark numbers.

## Documentation

- [docs/README.md](docs/README.md) indexes repository-level documentation and migration provenance.
- [src/README.md](src/README.md) indexes language-level source workspaces.
- [docs/guides/README.md](docs/guides/README.md) indexes task-oriented repository procedures.
- [docs/guides/repository-onboarding.md](docs/guides/repository-onboarding.md) is the end-to-end orientation guide for choosing workspaces, task scope, documentation responsibilities, and validation evidence.
- [docs/guides/agent-workflows.md](docs/guides/agent-workflows.md) holds compact task-conditional workflow guidance.
- [docs/guides/build-and-validation.md](docs/guides/build-and-validation.md) is the repository-wide validation matrix and command guide.
- [docs/guides/documentation-maintenance.md](docs/guides/documentation-maintenance.md) defines documentation placement, writing standards, metadata, and validation.
- [docs/guides/porting-and-semantic-parity.md](docs/guides/porting-and-semantic-parity.md) defines the workflow for keeping C#, C++, C, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python data-structure surfaces semantically aligned.
- [docs/reference/README.md](docs/reference/README.md) indexes durable cross-workspace reference material.
- [docs/reference/data-structure-catalog.md](docs/reference/data-structure-catalog.md) catalogs repository-owned data-structure families, public entry points, and primary references across C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python.
- [docs/reference/navigation-matrix.md](docs/reference/navigation-matrix.md) maps common tasks to the right usage, API, validation, porting, history, and maintenance documents.
- [docs/reference/semantic-contracts.md](docs/reference/semantic-contracts.md) summarizes shared behavior, ownership, policy, ordering, and documentation obligations for repository-owned numerics and data structures.
- [docs/reference/workspace-map.md](docs/reference/workspace-map.md) explains the language-first, library-family layout and port lineage.
- [src/CSharp/docs/Numerics/README.md](src/CSharp/docs/Numerics/README.md) indexes the Numerics library's API and behavior reference, validation guide, maintainer guidance, and design notes.
- [src/CSharp/docs/Hamt/README.md](src/CSharp/docs/Hamt/README.md) indexes the HAMT library's usage guide, API specification, validation guide, and implementation review.
- [src/C/Hamt/docs/README.md](src/C/Hamt/docs/README.md) indexes the C HAMT port's usage guide, API specification, and validation guide.
- [src/Cpp/Hamt/docs/README.md](src/Cpp/Hamt/docs/README.md) indexes the C++ HAMT port's usage
  guide, API specification, exact-wire Merkle specification, and validation guide.
- [src/CSharp/docs/FingerTree/README.md](src/CSharp/docs/FingerTree/README.md) indexes the library's usage guide, specifications, validation guide, design notes, benchmark notes, and external references.
- [src/CSharp/docs/Ordered/README.md](src/CSharp/docs/Ordered/README.md) indexes the neutral, independently owned insertion-ordered set's overview, usage guide, API specification, validation guide, Tungsten-free contract, project, and tests.
- [src/CSharp/docs/Tungsten/README.md](src/CSharp/docs/Tungsten/README.md) indexes the Tungsten-collections library's overview, usage guide, API specification, and validation guide.
- [src/C/Tungsten/README.md](src/C/Tungsten/README.md) indexes the C Tungsten-collections port.
- [src/Cpp/Tungsten/README.md](src/Cpp/Tungsten/README.md) indexes the C++ Tungsten-collections port.
- [src/Haskell/Tungsten/README.md](src/Haskell/Tungsten/README.md) indexes the Haskell Tungsten-collections port.
- [src/Kotlin/Tungsten/README.md](src/Kotlin/Tungsten/README.md) indexes the Kotlin Tungsten-collections port.
- [src/Rust/Tungsten/README.md](src/Rust/Tungsten/README.md) indexes the Rust Tungsten-collections port.
- [src/Cpp/FingerTree/docs/README.md](src/Cpp/FingerTree/docs/README.md) indexes the C++ usage guide, port plan, API notes, validation guide, implementation notes, and review reports.
- [src/C/FingerTree/docs/README.md](src/C/FingerTree/docs/README.md) indexes the C usage guide, API notes, and validation guide.
- [src/Haskell/README.md](src/Haskell/README.md) indexes the Haskell cabal packages.
- [src/Haskell/Hamt/test/README.md](src/Haskell/Hamt/test/README.md) and [src/Haskell/FingerTree/test/README.md](src/Haskell/FingerTree/test/README.md) summarize the Haskell executable test coverage.
- [src/Kotlin/README.md](src/Kotlin/README.md) indexes the Kotlin/JVM workspaces.
- [src/Kotlin/Hamt/docs/README.md](src/Kotlin/Hamt/docs/README.md) indexes the Kotlin HAMT port's API notes and validation guide.
- [src/Kotlin/FingerTree/docs/README.md](src/Kotlin/FingerTree/docs/README.md) indexes the Kotlin FingerTree-family API notes and validation guide.
- [src/Rust/Hamt/docs/README.md](src/Rust/Hamt/docs/README.md) indexes the Rust HAMT, Patricia, and
  Merkle search-tree API notes and validation guidance.
- [src/Rust/FingerTree/docs/README.md](src/Rust/FingerTree/docs/README.md) indexes the Rust
  FingerTree-family API notes and validation guide.
- [src/TypeScript/README.md](src/TypeScript/README.md) indexes the TypeScript package;
  [API notes](src/TypeScript/docs/api-notes.md), [validation](src/TypeScript/docs/validation.md),
  and the [test map](src/TypeScript/test/README.md) document its runtime mappings and gates.
- [src/Python/README.md](src/Python/README.md) indexes the Python package;
  [API notes](src/Python/docs/api-notes.md), [validation](src/Python/docs/validation.md), and the
  [test map](src/Python/tests/README.md) document its runtime mappings and gates.
- [src/OCaml/README.md](src/OCaml/README.md) indexes the OCaml package;
  [API notes](src/OCaml/docs/api-notes.md), [validation](src/OCaml/docs/validation.md), and the
  [test map](src/OCaml/tests/README.md) document its qualified modules and gates.

The large `TECHNICAL_DOCUMENTATION_STANDARD.md` and `XML_DOCUMENTATION_STANDARD.md` files from Tools are intentionally not part of this repository. Keep documentation thorough and current-state oriented, and write XML documentation in semantic terms: contracts, invariants, ordering, failure behavior, complexity, allocation behavior, and examples where they help.

Every new long-lived document should include header provenance metadata:

```markdown
- Created (UTC): YYYY-MM-DDTHH:MM:SSZ
- Repository HEAD: <40-hex-sha>
```

Use `git rev-parse HEAD` for the repository HEAD. When creating reports where filename collisions are likely, append a `__xxxxxxxxxxxx` suffix using 12 lowercase hex digits from a content hash.

## External reference material

Files under [src/CSharp/docs/FingerTree/external](src/CSharp/docs/FingerTree/external/README.md) are external, pre-existing study material. They are not authored by this project and are not covered by this repository's MIT-0 license; each item keeps its own copyright and license.

## Local environment

The expected local Windows environment includes:

- `pwsh` / PowerShell 7.
- `rg` for repository search.
- `git` and `gh` for source-control and GitHub workflows.
- Python 3.11 or newer with `venv` and `pip`, both for `src/Python` and ad hoc tooling.
- .NET SDK 10.0 or newer with the .NET 10 targeting packs.
- Visual Studio native C/C++ toolchain, including C++23 `/std:c++latest` support for `src/Cpp/FingerTree`, plus the bundled CMake and Ninja used by the `src/C/FingerTree` and `src/Cpp/FingerTree` presets.
- LLVM/Clang for native portability validation. The local Windows installation normally exposes
  `C:\Program Files\LLVM\bin\clang.exe` and `C:\Program Files\LLVM\bin\clang++.exe`; use the Visual Studio
  developer environment when targeting the MSVC ABI.
- GCC/MinGW for native portability validation. The local Windows installation uses WinLibs through winget and
  provides `gcc.exe`, `g++.exe`, `cmake.exe`, `ninja.exe`, and `ctest.exe`; if the current shell has not picked up
  the new `PATH`, use the binaries under
  `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`.
- MSVC C17/C++20 toolchain for `src/C/Hamt` and `src/Cpp/Hamt`; use Scriptorium's
  `Import-VisualCppEnvironment.ps1` helper when compiling from a plain PowerShell process.
- `git-filter-repo` usable as `python -m git_filter_repo` when future history work is needed.
- GHC 9.12 and cabal 3.16 or newer for the Haskell packages under `src/Haskell`.
- A JVM is optional for Kotlin validation because `src/Kotlin/build.ps1` bootstraps a local JDK 21 and
  Kotlin compiler under `src/Kotlin/build/tools` when Java 21+ is not already available.
- Rust toolchain with Cargo for `src/Rust`; the local profile may expose Cargo as
  `$env:USERPROFILE\.cargo\bin\cargo.exe` even when it is not on `PATH`.
- opam 2.1+, OCaml 4.14+, and Dune 3.20+ for `src/OCaml`; install the package dependencies from
  `durable7.opam` and keep both opam and Dune at one job during repository validation.
- Node.js 24 or newer and npm for `src/TypeScript`; use the committed lockfile with `npm ci`.

Use `dotnet` directly for C# restore/build operations and `src/CSharp/test.ps1` for unattended
test validation in this local environment.

## Cross-repo toolbox

Reusable automation (web mining, browser CDP capture, PDF/OCR, git/GitHub tooling, Windows GUI
control, agent-log processing, installers) lives in the sibling **Scriptorium** repo
(`C:\Scriptorium`; <https://github.com/VladimirReshetnikov/Scriptorium>) — see its `TOOLS.md`
index. **Before writing a new automation script, grep `..\Scriptorium\TOOLS.md`.** Repo-agnostic
scripts are born there and called in place, never copied here. `src/CSharp/docs/FingerTree/build-design-notes.ps1` is a thin wrapper over Scriptorium's `render/Build-LatexDoc.ps1`.
## Agent working guidelines

When starting on a task, read `AGENTS.md` first; in this repository it points to this file. Read the relevant workspace README and local docs before editing source.

Default to acting autonomously and carrying work through implementation, validation, and a clear status report. Vladimir prefers substantial, production-ready work over narrow prototype changes. Be supportive, direct, and technically honest.

Search with `rg` first for repository content. Preserve existing architecture, naming, and style unless the task calls for changing them. Keep edits scoped to the project boundary implied by the task, but update nearby docs when paths, responsibilities, or contracts change.

The worktree may contain changes from Vladimir or other tools. Do not revert changes you did not make unless explicitly asked. If unrelated changes are present, work around them. If they affect the task, understand them and build on them.

## Version control

Commit self-contained changes on `main` after validation. This repository currently has `origin`
configured; push to `origin/main` unless Vladimir explicitly asks not to.

Commit messages should describe the logical change and end with a `Co-Authored-By` trailer for the AI assistant when applicable.

## Work estimates

Do not express estimates in calendar or person-time units. Use velocity-independent units such as files touched, lines changed, test count, affected projects, binary size, or number of API members/call sites.

## Licensing

Unless a more specific license file is present, repository-owned content is licensed under MIT-0. External material under `src/CSharp/docs/FingerTree/external` retains its own copyright and license.
