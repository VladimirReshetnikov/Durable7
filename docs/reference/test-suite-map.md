# Test Suite Map

- Created (UTC): 2026-07-02T21:24:32Z
- Repository HEAD: c3fc7180fd857ec873bff96e8133d322152a4010
- Audience: Maintainers and AI agents choosing test entry points and coverage references
- Scope: Repository-owned test suites, local test READMEs, routine commands, and stress/sample boundaries

This map answers "which tests exist, where do I start, and what evidence should I record?" It complements the
[build and validation guide](../guides/build-and-validation.md), which owns full command blocks and documentation
checks, and the workspace validation guides, which own local warning policy and coverage wording.

## Routine Test Suites

| Workspace | Runner shape | Routine command | Local test map | Notes |
| --- | --- | --- | --- | --- |
| [C# Numerics](../../src/CSharp/docs/Numerics/overview.md) | xUnit test project with declaration parity guardrails | `.\test.ps1` from `src/CSharp` | [Tests README](../../src/CSharp/tests/Tools.Numerics.Tests/README.md) | Library build, XML-doc gate, fixed-width and sparse integer behavior, binary conversion, public API coverage, and declaration parity |
| [C# HAMT](../../src/CSharp/docs/Hamt/overview.md) | xUnit test project with CsCheck generated histories | `.\test.ps1` from `src/CSharp` | [Tests README](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests/README.md) | Library build, XML-doc gate, example tests, model histories, set-algebra properties, concurrent snapshot readers, and immutable-version publication |
| [C# FingerTree](../../src/CSharp/docs/FingerTree/overview.md) | xUnit test project with CsCheck, sample-smoke hooks, and default-duration stress tests | `.\test.ps1` from `src/CSharp` | [Tests README](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md) | Library build, sample project build/smoke, model/property suites, and tearable concurrency stress |
| [C# Tungsten collections](../../src/CSharp/docs/Tungsten/overview.md) | xUnit test project with CsCheck generated histories | `.\test.ps1` from `src/CSharp` | [Tests README](../../src/CSharp/tests/Tools.DataStructures.Tungsten.Tests/README.md) | Library build, XML-doc gate, kernel-verified ordering examples, ordered-model histories, relabel stress, no-op identity, concurrent snapshot readers, and immutable-version publication |
| [C Tungsten](../../src/C/Tungsten/README.md) | CMake/CTest executable | `.\build.ps1 -Workspace Tungsten -RunTests` from `src/C` | [Test source](../../src/C/Tungsten/tests/tungsten_c_tests.c) | List examples, Association ordering rules, custom policies, relabel stress, generated histories, and retained-snapshot reader threads on Windows |
| [C++ Tungsten](../../src/Cpp/Tungsten/README.md) | CMake/CTest executable with local runner | `.\build.ps1 -Workspace Tungsten -RunTests` from `src/Cpp` | [Test source](../../src/Cpp/Tungsten/tests/tungsten_tests.cpp) | List examples, Association ordering rules, custom policies, relabel stress, generated histories, and retained-snapshot reader threads |
| [C HAMT](../../src/C/Hamt/README.md) | Dependency-free native executable built by `build.ps1` | `.\build.ps1 -Workspace Hamt -RunTests` from `src/C` | [Tests README](../../src/C/Hamt/tests/README.md) | Deterministic map/set unit and model checks, plus retained-snapshot reader threads on Windows; fail-fast runner |
| [C++ HAMT](../../src/Cpp/Hamt/README.md) | Dependency-free native executable built by `build.ps1` | `.\build.ps1 -Workspace Hamt -RunTests` from `src/Cpp` | [Tests README](../../src/Cpp/Hamt/tests/README.md) | Deterministic map/set unit and model checks plus retained-snapshot reader threads; local registry runner |
| [C FingerTree](../../src/C/FingerTree/README.md) | Three focused native executables plus two sample smoke tests | `.\build.ps1 -Workspace FingerTree -RunTests` from `src/C` | [Tests README](../../src/C/FingerTree/tests/README.md) | Core/RRB tests plus DABA histories, callback ceilings, allocation rollback, ownership/alignment/move checks, sample smokes, and a benchmark executable |
| [C++ FingerTree](../../src/Cpp/FingerTree/README.md) | Local runner exposed as 18 subsystem CTests plus sample and installed-consumer integration | `.\build.ps1 -Workspace FingerTree -RunTests` from `src/Cpp` | [Tests README](../../src/Cpp/FingerTree/tests/README.md) | Header-first C++23 suite with replay/shrinking, DABA model/callback/copy-failure gates, complexity guards, tearable concurrency, samples, and package relocation |
| [Haskell HAMT](../../src/Haskell/Hamt/README.md) | Cabal exit-code executable | `.\test.ps1 -Workspace Hamt` from `src/Haskell` | [Tests README](../../src/Haskell/Hamt/test/README.md) | Map/set behavior, collision shrink, strict mapping, one-pass adjust, receiver-policy relations, 100,000-entry construction, and `forkIO` readers |
| [Haskell FingerTree](../../src/Haskell/FingerTree/README.md) | Cabal exit-code executable | `.\test.ps1 -Workspace FingerTree` from `src/Haskell` | [Tests README](../../src/Haskell/FingerTree/test/README.md) | Measured/deque families, 200,000-entry construction, mixed reversal, max-high intervals, structurally shared ropes/measured text, and `forkIO` readers |
| [Haskell Tungsten](../../src/Haskell/Tungsten/README.md) | Cabal exit-code executable | `.\test.ps1 -Workspace Tungsten` from `src/Haskell` | [Test source](../../src/Haskell/Tungsten/test/Main.hs) | List examples, Association ordering rules, custom `HashPolicy`, relabel stress, generated histories, and `forkIO` concurrent readers |
| [Kotlin HAMT](../../src/Kotlin/Hamt/README.md) | Kotlin/JVM executable test jar built by `src/Kotlin/build.ps1` | `.\build.ps1 -Workspace Hamt` from `src/Kotlin` | [Tests README](../../src/Kotlin/Hamt/tests/README.md) | Trie persistence, no-op sharing, collisions, key retention, set algebra, receiver-policy cross-policy relations, and JVM concurrent readers |
| [Kotlin FingerTree](../../src/Kotlin/FingerTree/README.md) | Kotlin/JVM executable test jar built by `src/Kotlin/build.ps1` | `.\build.ps1 -Workspace FingerTree` from `src/Kotlin` | [Tests README](../../src/Kotlin/FingerTree/tests/README.md) | Measured-AVL/RRB facades, DABA schedules and failure atomicity, canonical zip-zip rank vectors/topology/sharing/deep-chain invariants, generated histories, and JVM concurrent readers |
| [Kotlin Tungsten](../../src/Kotlin/Tungsten/README.md) | Kotlin/JVM executable test jar built by `src/Kotlin/build.ps1` | `.\build.ps1 -Workspace Tungsten` from `src/Kotlin` | [Test source](../../src/Kotlin/Tungsten/test/tools/datastructures/tungsten/TungstenTests.kt) | List/Association rules, custom policy, relabel/generated histories, 20,000-element AVL split/join stress, and JVM concurrent readers |
| [Rust HAMT](../../src/Rust/Hamt/README.md) | Cargo unit tests | `.\test.ps1 -Workspace Hamt` from `src/Rust` | [Tests README](../../src/Rust/Hamt/tests/README.md) | Map/set unit checks for collisions, updates, iteration, set algebra, `Send`/`Sync` assertions, and spawned-thread readers |
| [Rust FingerTree](../../src/Rust/FingerTree/README.md) | Cargo unit tests inline across crate modules | `.\test.ps1 -Workspace FingerTree` from `src/Rust` | [Tests README](../../src/Rust/FingerTree/tests/README.md) | Structurally shared persistent-family tests, DABA histories/atomicity/reclamation, and canonical zip-zip crypto/rank vectors, non-`Clone` boundaries, topology/sharing/model/deep-chain invariants, and cold concurrent digest publication |
| [Rust Tungsten](../../src/Rust/Tungsten/README.md) | Cargo unit tests inline in crate | `.\test.ps1 -Workspace Tungsten` from `src/Rust` | [Source tests](../../src/Rust/Tungsten/src/lib.rs) | List examples, Association ordering rules, relabel stress, generated histories, `Send`/`Sync` assertions, and spawned-thread readers |

## Stress And Duration Knobs

| Scope | Control | Default use | Longer-run command shape |
| --- | --- | --- | --- |
| C# FingerTree tearable concurrency stress | `FINGERTREE_STRESS_SECONDS` | Short enough for ordinary `.\test.ps1` | Set the variable, run `.\test.ps1 -Filter FullyQualifiedName~TearableConcurrencyStressTests`, then remove the variable |
| C++ FingerTree tearable concurrency stress | `FINGERTREE_STRESS_SECONDS` | Short enough for ordinary CTest | Set the variable, run CTest with `-R "^fingertree\.concurrency$"`, then remove the variable |

Raise duration knobs when changing lazy memoization, atomic publication, structural sharing under concurrent reads,
or tearable element/measure paths. Record the variable value in validation evidence.

## Samples And Benchmarks

| Scope | Where documented | Routine role |
| --- | --- | --- |
| C# FingerTree samples | [Samples README](../../src/CSharp/samples/README.md) and [test project README](../../src/CSharp/tests/Tools.DataStructures.FingerTree.Tests/README.md) | Sample projects are referenced by the test project and smoke-tested by `.\test.ps1` |
| C# FingerTree benchmarks | [Benchmark notes](../../src/CSharp/docs/FingerTree/benchmarks.md) and [benchmark project README](../../src/CSharp/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md) | Run only for performance-sensitive code, benchmark changes, or performance claims |
| C FingerTree samples | [Samples README](../../src/C/FingerTree/samples/README.md) | Sample executables are registered as CTest smoke tests |
| C FingerTree benchmarks | [Benchmarks README](../../src/C/FingerTree/benchmarks/README.md) | Dependency-light persistent-family and DABA slide/aggregate timing harness; run for local sanity checks or benchmark-doc changes |
| C++ FingerTree samples and benchmarks | [C++ validation guide](../../src/Cpp/FingerTree/docs/validation.md#benchmark-harness-status) | Deterministic samples are CTest-smoked; the dependency-free Release harness covers the persistent families, DABA slide/query and validation, and the branching-flatness guard |

## Coverage Change Rules

- Add direct example tests for new public behavior, then add a model, property, or stress test when there is a
  natural oracle or concurrency risk.
- Keep local test READMEs current when adding, renaming, or deleting test files, sample smoke hooks, stress knobs,
  or direct executable paths.
- Update the workspace validation guide when a command, warning policy, runner shape, sample/benchmark boundary,
  or coverage claim changes.
- Update this map when a new long-lived test suite, runner, sample-smoke path, benchmark harness, or stress control
  becomes part of the repository.
