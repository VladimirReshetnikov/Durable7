# Documentation Navigation Matrix

- Created (UTC): 2026-07-02T20:20:21Z
- Repository HEAD: 30fc6f5b883a4771b7102f5c8c60d483075d7372
- Audience: Maintainers and AI agents choosing the right repository document for a task
- Scope: Task-oriented entry points across repository-level docs and workspace-owned docs

This matrix is the "where do I start?" layer. It does not replace the workspace documentation; it
routes a reader to the narrowest document that owns the question.

## Start By Task

| Task | Open first | Then inspect |
| --- | --- | --- |
| Start work in an unfamiliar part of the repository | [Repository onboarding](../guides/repository-onboarding.md) | [Workspace map](workspace-map.md), [catalog](data-structure-catalog.md), affected workspace README |
| Understand repository layout | [Workspace map](workspace-map.md) | Root [README](../../README.md), [source index](../../src/README.md), [data-structure catalog](data-structure-catalog.md) |
| Browse workspaces by language | [Source index](../../src/README.md) | [C](../../src/C/README.md), [C++](../../src/Cpp/README.md), [C#](../../src/CSharp/README.md), [Haskell](../../src/Haskell/README.md), [Kotlin](../../src/Kotlin/README.md), [Rust](../../src/Rust/README.md) |
| Choose a data structure or numerics surface across languages | [Data-structure catalog](data-structure-catalog.md) | The matching workspace usage guide and API specification or notes |
| Understand shared semantic contracts | [Semantic contracts](semantic-contracts.md) | Workspace API specs/notes, [porting guide](../guides/porting-and-semantic-parity.md), local tests |
| Plan a new derived structure or API extension | [Derived structure catalog](derived-structure-catalog.md) | [Porting and semantic parity](../guides/porting-and-semantic-parity.md), affected workspace API specs |
| Plan a new core, representation tier, or specialized sibling | [Frontier structure catalog](frontier-structure-catalog.md) | [Axis 2 final lifecycle/cursor plan](../proposals/axis2-lifecycle-and-sequence-cursors.md), [derived structure catalog](derived-structure-catalog.md), [next-data-structures proposal](../proposals/new-data-structures-2026-07-09.md), [porting guide](../guides/porting-and-semantic-parity.md) |
| Use an existing collection | The relevant usage guide below | Source tests for executable examples when behavior is subtle |
| Batch-edit a C# CHAMP map or set | [C# HAMT usage guide](../../src/CSharp/docs/Hamt/usage.md) | [API specification](../../src/CSharp/docs/Hamt/api-specification.md), [T2 shipment decision](../../src/CSharp/docs/Hamt/transient-t2-decision.md), and public transient tests |
| Edit or navigate a C# rope through a version-bound cursor | [C# FingerTree usage guide](../../src/CSharp/docs/FingerTree/usage.md) | [API specification](../../src/CSharp/docs/FingerTree/api-specification.md), [C0 positional decision](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md), [C2 measured decision](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md), and cursor tests |
| Build or validate a workspace | [Build and validation](../guides/build-and-validation.md) | [Test suite map](test-suite-map.md), workspace validation docs, and workspace README |
| Understand test coverage | [Test suite map](test-suite-map.md) | Workspace tests README and validation guide |
| Change public API or semantics | [Porting and semantic parity](../guides/porting-and-semantic-parity.md) | [Semantic contracts](semantic-contracts.md), API specs/notes for all affected language workspaces, catalog rows, tests |
| Update documentation | [Documentation maintenance](../guides/documentation-maintenance.md) | This matrix, the affected workspace docs index, and the catalog |
| Investigate extraction history | [Migration index](../migration/README.md) | [Extraction provenance](../migration/extraction-provenance.md), [filter-repo commit map](../migration/filter-repo-commit-map.tsv) |
| Translate a pre-`src` workspace path | [Language-first reorganization](../migration/language-first-reorganization.md) | [Workspace map](workspace-map.md), [source index](../../src/README.md), current validation docs |
| Run or interpret benchmarks | [C# FingerTree benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) | Benchmark project [README](../../src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md), root benchmark summary |
| Inspect persistence/concurrency patterns | [C# FingerTree persistence and concurrency](../../src/CSharp/docs/FingerTree/persistence-and-concurrency.md) | Corresponding native usage/API notes for C and C++ ports |
| Review C++ FingerTree port history | [C++ implementation notes](../../src/Cpp/FingerTree/docs/implementation-notes.md) | Port plan, editorial notes, independent review reports |

## Usage Guides

Use these for first-use examples, facade selection, ownership/lifetime rules, and common update
patterns.

| Workspace | Usage guide | Best for |
| --- | --- | --- |
| C# Numerics | [src/CSharp/docs/Numerics/overview.md](../../src/CSharp/docs/Numerics/overview.md) | Fixed-width integer types, sparse integers, binary conversion, and wide-integer maintenance entry points |
| C# HAMT | [src/CSharp/docs/Hamt/usage.md](../../src/CSharp/docs/Hamt/usage.md) | `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>` construction, comparers, persistent updates, one-way transient editing/publication, and set algebra |
| C HAMT | [src/C/Hamt/docs/usage.md](../../src/C/Hamt/docs/usage.md) and [Merkle guide](../../src/C/Hamt/docs/merkle-search-tree.md) | Type-erased HAMT/Patricia values plus Merkle policies/codecs, stores, budgets, proofs, sync, merge, persistent handles, and status/cleanup patterns |
| C++ HAMT | [src/Cpp/Hamt/docs/usage.md](../../src/Cpp/Hamt/docs/usage.md), [Merkle core](../../src/Cpp/Hamt/docs/merkle-search-tree.md), and [persistence guide](../../src/Cpp/Hamt/docs/merkle-persistence.md) | Header inclusion, CHAMP/Patricia values, canonical Merkle codecs/topology, stores, budgets, proofs, synchronization, move-only merge, and diagnostics |
| Haskell HAMT | [src/Haskell/Hamt/README.md](../../src/Haskell/Hamt/README.md) and [Merkle guide](../../src/Haskell/Hamt/docs/merkle-search-tree.md) | HAMT/Patricia values plus pure Merkle construction, immutable stores, bounded verification, proofs, synchronization, and merge |
| Kotlin HAMT | [src/Kotlin/Hamt/docs/api-notes.md](../../src/Kotlin/Hamt/docs/api-notes.md) and [Merkle guide](../../src/Kotlin/Hamt/docs/merkle-search-tree.md) | HAMT/Ctrie/Patricia values plus canonical Merkle construction, stores, bounded verification, proofs, synchronization, and merge |
| Rust HAMT | [src/Rust/Hamt/docs/api-notes.md](../../src/Rust/Hamt/docs/api-notes.md) and [Merkle guide](../../src/Rust/Hamt/docs/merkle-search-tree.md) | Persistent HAMT/Patricia maps plus Merkle construction, persistence, proofs, synchronization, and merge |
| C# FingerTree | [src/CSharp/docs/FingerTree/usage.md](../../src/CSharp/docs/FingerTree/usage.md) | Deques, reversible deques, sorted collections, priority queues, intervals, ropes/text, positional and measured edit cursors, raw measured trees |
| C FingerTree | [src/C/FingerTree/docs/usage.md](../../src/C/FingerTree/docs/usage.md) | C handle lifetime, canonical erased-type/rank policy, Brodal/PSQ ownership and failure atomicity, persistent updates, DABA allocator semantics, facades, and text ropes |
| C++ FingerTree | [src/Cpp/FingerTree/docs/usage.md](../../src/Cpp/FingerTree/docs/usage.md) | Aggregate include path, persistent values, policy-canonical zip-zip set, Brodal and keyed priority-search queues, DABA Lite ownership/exception constraints, ropes/text, and publication patterns |
| Haskell FingerTree | [src/Haskell/FingerTree/README.md](../../src/Haskell/FingerTree/README.md) and [canonical-set guide](../../src/Haskell/FingerTree/docs/canonical-sorted-set.md) | General measured tree, deque, reversible deque, policy-canonical zip-zip set, sorted/priority facades, intervals, ropes, and text helpers |
| Kotlin FingerTree | [src/Kotlin/FingerTree/docs/api-notes.md](../../src/Kotlin/FingerTree/docs/api-notes.md) | Kotlin measured-AVL/RRB persistence, policy-canonical zip-zip set, Brodal-Okasaki heap, winner-cached AVL priority-search queue, derived collections, ropes/text, and mutable DABA Lite aggregation |
| Rust FingerTree | [src/Rust/FingerTree/docs/api-notes.md](../../src/Rust/FingerTree/docs/api-notes.md) | Rust shared-storage persistent surfaces, policy-canonical zip-zip set, Brodal heap, priority-search queue, ordering policies, plus single-threaded DABA Lite and deterministic-drop semantics |
| C# Tungsten collections | [src/CSharp/docs/Tungsten/usage.md](../../src/CSharp/docs/Tungsten/usage.md) | `PersistentList<T>` and `PersistentAssociation<TKey, TValue>` with the Tungsten operation correspondence |
| C/C++/Haskell/Kotlin/Rust Tungsten collections | [data-structure catalog](data-structure-catalog.md#tungsten-collections) | Language-local Tungsten `List` and `Association` entry points, README links, tests, and substrate notes |

## API Contracts

Use these when behavior, complexity, allocation, ownership, or cross-language parity matters.

For a cross-family checklist before drilling into a local spec, start with the
[semantic contracts reference](semantic-contracts.md).

| Workspace | Contract document | Notes |
| --- | --- | --- |
| C# Numerics | [API and behavior reference](../../src/CSharp/docs/Numerics/api-and-behavior-reference.md) | Normative fixed-width integer behavior, conversion, parse/format, and binary representation contract |
| C# HAMT | [API specification](../../src/CSharp/docs/Hamt/api-specification.md) and [T2 shipment decision](../../src/CSharp/docs/Hamt/transient-t2-decision.md) | Normative C# HAMT map/set contract, including the C#-only single-owner transient lifecycle and its evidence boundary |
| C HAMT | [API specification](../../src/C/Hamt/docs/api-specification.md), [Merkle specification](../../src/C/Hamt/docs/merkle-search-tree.md), and [Merkle header](../../src/C/Hamt/include/Tools/DataStructures/Hamt/merkle_search_tree.h) | C ownership/callback contracts plus failure-atomic `MST2`/`MSP2`, bounded stores/import, proofs, sync, and present-null-safe merge |
| C++ HAMT | [API specification](../../src/Cpp/Hamt/docs/api-specification.md), [Merkle core](../../src/Cpp/Hamt/docs/merkle-search-tree.md), [persistence specification](../../src/Cpp/Hamt/docs/merkle-persistence.md), and [aggregate header](../../src/Cpp/Hamt/include/Tools/DataStructures/Hamt/hamt.hpp) | C++ template contracts plus exact `MST2`/`MSP2`, finite budgets, stores, proofs, sync, merge, and native ownership rules |
| Haskell HAMT | [Workspace README](../../src/Haskell/Hamt/README.md), [Merkle guide](../../src/Haskell/Hamt/docs/merkle-search-tree.md), [core](../../src/Haskell/Hamt/src/Data/Structures/Hamt/MerkleSearchTree.hs), and [persistence module](../../src/Haskell/Hamt/src/Data/Structures/Hamt/MerklePersistence.hs) | Haskell HAMT/Patricia APIs plus exact `MST2`/`MSP2`, opaque budgets, pure store snapshots, proofs, sync, and merge |
| Kotlin HAMT | [API notes](../../src/Kotlin/Hamt/docs/api-notes.md), [Merkle guide](../../src/Kotlin/Hamt/docs/merkle-search-tree.md), [core](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/MerkleSearchTree.kt), and [persistence vocabulary](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/MerklePersistence.kt) | Kotlin HAMT/Ctrie/Patricia APIs plus canonical `MST2`/`MSP2` wire, finite budgets, stores, proofs, sync, merge, and managed-reference contracts |
| Rust HAMT | [API notes](../../src/Rust/Hamt/docs/api-notes.md), [Merkle guide](../../src/Rust/Hamt/docs/merkle-search-tree.md), and [source](../../src/Rust/Hamt/src/lib.rs) | Rust map APIs plus the canonical `MST2`/`MSP2` wire, verification budgets, block-store, proof, sync, and merge contracts |
| C# FingerTree | [API specification](../../src/CSharp/docs/FingerTree/api-specification.md) | Deque contract plus measured-tree, reversible-deque, rope, and related surface notes |
| C FingerTree | [API notes](../../src/C/FingerTree/docs/api-notes.md), [canonical header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/canonical_sorted_set.h), [Brodal header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/brodal_okasaki_heap.h), and [PSQ header](../../src/C/FingerTree/include/tools/data_structures/finger_tree/priority_search_queue.h) | C API shape, erased-type identity, fallible callbacks, canonical contracts, Brodal/PSQ ownership and failure semantics, and C++ port differences |
| C++ FingerTree | [API notes](../../src/Cpp/FingerTree/docs/api-notes.md), [canonical header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/canonical_sorted_set.hpp), [Brodal header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/brodal_okasaki_heap.hpp), and [PSQ header](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/priority_search_queue.hpp) | C++ conventions, canonical crypto/policy/digest contracts, move-only Brodal/PSQ representatives, comparator identity, priority-range pruning, DABA no-throw commit and deterministic clear semantics, and active C# differences |
| C# Tungsten collections | [API specification](../../src/CSharp/docs/Tungsten/api-specification.md) | List facade and association contracts, kernel-verified ordering rules, complexity and no-op identity tables |
| C Tungsten collections | [Workspace README](../../src/C/Tungsten/README.md) and [public header](../../src/C/Tungsten/include/tools/data_structures/tungsten/tungsten.h) | C value-struct API, explicit lifetime, policy callbacks, and stamp-sequence representation |
| C++ Tungsten collections | [Workspace README](../../src/Cpp/Tungsten/README.md) and [aggregate header](../../src/Cpp/Tungsten/include/tools/data_structures/tungsten/tungsten.hpp) | C++ value API and header-first List/Association templates |
| Haskell/Kotlin/Rust Tungsten collections | [catalog rows](data-structure-catalog.md#tungsten-collections) | Language-local README and source entry points for Tungsten `List` and `Association` ports |
| Haskell FingerTree | [Workspace README](../../src/Haskell/FingerTree/README.md), [measured source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/Measured.hs), and [canonical source](../../src/Haskell/FingerTree/src/Data/Structures/FingerTree/CanonicalSortedSet.hs) | Haskell measured-tree API plus canonical policy identity, pure/IO, digest, algebra, and sharing-diagnostic contracts |
| Kotlin FingerTree | [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md), [measured-tree source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/PersistentMeasuredTree.kt), [canonical-set source](../../src/Kotlin/FingerTree/src/tools/datastructures/fingertree/CanonicalSortedSet.kt), and [priority-core notes](../../src/Kotlin/FingerTree/docs/priority-cores.md) | Kotlin measured-tree engine, canonical rank policy/digest/algebra contracts, Brodal and PSQ invariants, structural sharing, complexity, and derived collection API shape |
| Rust FingerTree | [API notes](../../src/Rust/FingerTree/docs/api-notes.md), [measured source](../../src/Rust/FingerTree/src/measured.rs), [canonical-set source](../../src/Rust/FingerTree/src/canonical_sorted_set.rs), [Brodal notes](../../src/Rust/FingerTree/docs/brodal-okasaki-heap.md), and [PSQ notes](../../src/Rust/FingerTree/docs/priority-search-queue.md) | Rust measured tree, canonical rank/digest contracts, Brodal/PSQ policy/ownership/invariants, `Clone` boundaries, and derived collection API shape |

## Validation Entry Points

| Scope | Document | What it proves |
| --- | --- | --- |
| Whole repository | [Build and validation](../guides/build-and-validation.md) / [Test suite map](test-suite-map.md) | Canonical commands for C#, C, C++, CMake presets, Markdown checks, and test-suite entry points |
| C# Numerics | [Validation](../../src/CSharp/docs/Numerics/validation.md) | .NET restore/build/test commands, XML-documentation warning gate, and xUnit wide-integer coverage |
| C# HAMT | [Validation](../../src/CSharp/docs/Hamt/validation.md) | .NET restore/build/test commands, XML-documentation warning gate, and xUnit/CsCheck persistent, transient-lifecycle, failpoint, model, and concurrency coverage |
| C HAMT | [Validation](../../src/C/Hamt/docs/validation.md) | MSVC C17 Debug/Release, strict GCC/Clang, sanitizer/analyzer lanes, and HAMT/Patricia plus complete Merkle wire/persistence/proof/sync/merge/failpoint/concurrency coverage |
| C++ HAMT | [Validation](../../src/Cpp/Hamt/docs/validation.md) | MSVC/GCC/Clang Debug/Release lanes, strict warnings/static analysis, CHAMP/Patricia models, complete Merkle wire/persistence/proof/sync/merge/concurrency coverage, and copied-header consumption |
| Rust HAMT | [Validation](../../src/Rust/Hamt/docs/validation.md) | Cargo test, clippy, and rustdoc gates for HAMT/Patricia behavior and full Merkle core, wire, persistence, proof, sync, and merge coverage |
| C# FingerTree | [Validation](../../src/CSharp/docs/FingerTree/validation.md) | .NET restore/build/test commands, sample smoke coverage, benchmark boundary, stress controls, and xUnit/CsCheck coverage |
| C FingerTree | [Validation](../../src/C/FingerTree/docs/validation.md) | CMake/CTest validation, canonical crypto/type-tag gates, Brodal/PSQ failpoints/bounds/models/readers, DABA ownership gates, compiler/sanitizer matrix, sample smokes, and benchmarks |
| C++ FingerTree | [Validation](../../src/Cpp/FingerTree/docs/validation.md) | CMake/CTest validation, canonical rank/topology gates, Brodal/PSQ bounds/models/sharing/move-only/exception/concurrency audits, DABA failures, stress controls, crypto packaging, and benchmarks |
| C# Tungsten collections | [Validation](../../src/CSharp/docs/Tungsten/validation.md) | .NET build/test commands, kernel-verified semantics coverage, model histories, relabel stress |
| C Tungsten collections | [Workspace README](../../src/C/Tungsten/README.md) | `.\build.ps1 -Workspace Tungsten -RunTests` and Release validation for the C CTest executable |
| C++ Tungsten collections | [Workspace README](../../src/Cpp/Tungsten/README.md) | `.\build.ps1 -Workspace Tungsten -RunTests` for the C++ CTest executable |
| Haskell | [Workspace README](../../src/Haskell/README.md) | `.\test.ps1` builds all Haskell packages and runs the HAMT/FingerTree/Tungsten executables without Windows failure dialogs |
| Kotlin | [Workspace README](../../src/Kotlin/README.md) | `.\build.ps1` builds all Kotlin workspaces and runs dependency-free executable tests |
| Kotlin HAMT | [Validation](../../src/Kotlin/Hamt/docs/validation.md) | Kotlin compiler bootstrap and deterministic HAMT/Ctrie/Patricia plus complete Merkle wire/history/persistence/budget/proof/sync/merge/reader tests |
| Kotlin FingerTree | [Validation](../../src/Kotlin/FingerTree/docs/validation.md) | Kotlin compiler bootstrap, persistent-structure invariants, DABA schedules/callback atomicity, canonical rank/topology/sharing/digest checks, Brodal operation bounds, PSQ balance/pruning/retained-history audits, and executable facade tests |
| Rust | [Workspace README](../../src/Rust/README.md) | `.\test.ps1` builds all Rust crates and runs unit/doc tests without Windows failure dialogs |
| Rust FingerTree | [Validation](../../src/Rust/FingerTree/docs/validation.md) | Cargo tests for shared persistent storage, DABA models/atomicity/reclamation, canonical crypto/topology contracts, Brodal bound/model audits, and PSQ balance/pruning/non-`Clone`/concurrency gates |
| C# FingerTree benchmarks | [Benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) | Curated BenchmarkDotNet results and interpretation |
| C# FingerTree samples | [Samples README](../../src/CSharp/samples/README.md) | Runnable tours covering text, measured-tree facades, and editor-grade text extras |
| C# Numerics tests | [Tests README](../../src/CSharp/tests/Tools.Numerics.Tests/README.md) | xUnit project covering fixed-width integer behavior, binary conversion, public API coverage, and declaration parity |
| C# HAMT tests | [Tests README](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests/README.md) | xUnit/CsCheck project covering persistent collections plus public map/set transient API shape, clean identity, model histories, version-bound enumeration, consumed aliases, failure atomicity, and retained-base readers |
| C# FingerTree tests | [Tests README](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md) | xUnit/CsCheck project, source-file grouping, sample smoke hooks, stress controls, and model tests |
| C# Tungsten collections tests | [Tests README](../../src/CSharp/tests/Tools.DataStructures.Tungsten.Tests/README.md) | xUnit/CsCheck project covering kernel-verified ordering examples, ordered-model histories, and relabel stress |
| C Tungsten tests | [Test source](../../src/C/Tungsten/tests/tungsten_c_tests.c) | CTest executable covering list operations, Association ordering examples, policies, relabel stress, and generated histories |
| C++ Tungsten tests | [Test source](../../src/Cpp/Tungsten/tests/tungsten_tests.cpp) | CTest executable covering list operations, Association ordering examples, policies, relabel stress, and generated histories |
| C HAMT tests | [Tests README](../../src/C/Hamt/tests/README.md) | Three native executables covering HAMT, Patricia, and exact Merkle wire, stores/import, all budgets, proofs, sync, merge, exhaustive failure atomicity, reentrant callbacks, and concurrent readers/writers |
| C++ HAMT tests | [Tests README](../../src/Cpp/Hamt/tests/README.md) | Separate CHAMP/Patricia and Merkle executables plus copied aggregate-header consumer; codecs, exact wire, histories, stores/import, all budgets, proofs, sync, present-null/move-only merge, failures, validation, and concurrency |
| C FingerTree tests | [Tests README](../../src/C/FingerTree/tests/README.md) | Core/RRB/DABA/canonical/Brodal/PSQ executable map, including vectors/models/type tags/failpoints/readers, direct paths, and runner behavior |
| C FingerTree samples | [Samples README](../../src/C/FingerTree/samples/README.md) | Deterministic C sample executables and CTest smoke-test names |
| C FingerTree benchmarks | [Benchmarks README](../../src/C/FingerTree/benchmarks/README.md) | Dependency-light timing harness workloads and output shape |
| C++ FingerTree tests | [Tests README](../../src/Cpp/FingerTree/tests/README.md) | Native group map including canonical, Brodal, and PSQ vectors/models/bounds/sharing/move-only/concurrency/destruction, DABA failures, direct runner paths, packaging, and tearable stress controls |
| Haskell HAMT tests | [Tests README](../../src/Haskell/Hamt/test/README.md) | Cabal executable covering HAMT/Patricia plus exact Merkle wire, hostile closures, seven budgets, bomb-codec proof admission, sync repair, typed merge/null states, retained roots, models, and readers |
| Haskell FingerTree tests | [Tests README](../../src/Haskell/FingerTree/test/README.md) | Cabal executable covering measured/derived families plus canonical rank vectors, policy identity, receiver relations, models, sharing, deep chains, and validation faults |
| Haskell Tungsten tests | [Test source](../../src/Haskell/Tungsten/test/Main.hs) | Cabal executable covering list operations, Association ordering examples, policies, relabel stress, and generated histories |
| Kotlin HAMT tests | [Tests README](../../src/Kotlin/Hamt/tests/README.md) | Kotlin executable covering HAMT/Ctrie/Patricia semantics plus exact Merkle wire, retained histories, malformed closures, all seven budgets, proofs, sync repair, atomic import/store conflicts, nullable merge states, and readers |
| Kotlin FingerTree tests | [Tests README](../../src/Kotlin/FingerTree/tests/README.md) | Kotlin executable covering persistent facades and bounds plus adversarial DABA Lite, canonical zip-zip, Brodal heap, and priority-search-queue histories, callbacks, ranks, sharing, pruning, nullability, and concurrent readers |
| Kotlin Tungsten tests | [Test source](../../src/Kotlin/Tungsten/test/tools/datastructures/tungsten/TungstenTests.kt) | Kotlin executable covering list/Association rules, policies, relabel stress, generated histories, and 20,000-element SeqTree balance stress |
| Rust HAMT tests | [Tests README](../../src/Rust/Hamt/tests/README.md) | Cargo unit/integration tests covering HAMT/Patricia models plus exact Merkle wire, closure verification, budgets, proofs, sync, stores/import, merge, and retained concurrent readers |
| Rust FingerTree tests | [Tests README](../../src/Rust/FingerTree/tests/README.md) | Cargo tests covering persistent facades plus DABA, canonical zip-zip, Brodal, and PSQ histories, policy/ownership, non-`Clone` reads, models, sharing, bounds, pruning, and concurrency |
| Rust Tungsten tests | [Source tests](../../src/Rust/Tungsten/src/lib.rs) | Cargo unit tests covering list operations, Association ordering examples, relabel stress, and generated histories |

## Historical And External Material

| Material | Location | Use |
| --- | --- | --- |
| Repository extraction record | [docs/migration](../migration/README.md) | Preserve source-repo, filter-repo, and commit-map provenance |
| C++ FingerTree port reports | [src/Cpp/FingerTree/docs](../../src/Cpp/FingerTree/docs/README.md) | Retain independent review findings and correction context |
| C# FingerTree external references | [external index](../../src/CSharp/docs/FingerTree/external/README.md) | Study source papers and snapshots; not repository-owned license material |
| C# FingerTree design notes | [PDF](../../src/CSharp/docs/FingerTree/FingerTree-Design-Notes.pdf) / [TeX](../../src/CSharp/docs/FingerTree/FingerTree-Design-Notes.tex) | Architecture, algorithms, concurrency, and test-strategy tour |

## Maintenance Rule

When adding or moving a long-lived document, update every layer that helps a reader find it:

- the affected workspace `docs/README.md`;
- the affected workspace `README.md` when it changes the local entry points;
- [docs/README.md](../README.md) or [reference/README.md](README.md) when it is repository-level;
- [data-structure-catalog.md](data-structure-catalog.md) when it changes a public data-structure surface;
- this matrix when it becomes a better first stop for a task.
