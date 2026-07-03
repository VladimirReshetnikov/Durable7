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
| [C# Numerics](../../src/CSharp/Numerics/README.md) | xUnit test project with declaration parity guardrails | `dotnet test .\Numerics.sln` | [Tests README](../../src/CSharp/Numerics/tests/Tools.Numerics.Tests/README.md) | Library build, XML-doc gate, fixed-width and sparse integer behavior, binary conversion, public API coverage, and declaration parity |
| [C# HAMT](../../src/CSharp/Hamt/README.md) | xUnit test project with CsCheck generated histories | `dotnet test .\Hamt.sln` | [Tests README](../../src/CSharp/Hamt/tests/Tools.DataStructures.Hamt.Tests/README.md) | Library build, XML-doc gate, example tests, model histories, and set-algebra properties |
| [C# FingerTree](../../src/CSharp/FingerTree/README.md) | xUnit test project with CsCheck, sample-smoke hooks, and default-duration stress tests | `dotnet test .\FingerTree.sln` | [Tests README](../../src/CSharp/FingerTree/tests/Tools.DataStructures.FingerTree.Tests/README.md) | Library build, sample project build/smoke, model/property suites, and tearable concurrency stress |
| [C HAMT](../../src/C/Hamt/README.md) | Dependency-free native executable built by `build.ps1` | `.\build.ps1 -RunTests` | [Tests README](../../src/C/Hamt/tests/README.md) | Deterministic map/set unit and model checks; fail-fast runner |
| [C++ HAMT](../../src/Cpp/Hamt/README.md) | Dependency-free native executable built by `build.ps1` | `.\build.ps1 -RunTests` | [Tests README](../../src/Cpp/Hamt/tests/README.md) | Deterministic map/set unit and model checks; local registry runner |
| [C FingerTree](../../src/C/FingerTree/README.md) | CMake/CTest core executable plus sample smoke tests | Visual Studio CMake/CTest chain from the validation guide | [Tests README](../../src/C/FingerTree/tests/README.md) | Core C API tests, sample smoke tests, and a separate benchmark executable |
| [C++ FingerTree](../../src/Cpp/FingerTree/README.md) | CMake/CTest smoke executable with a local runner | Visual Studio CMake/CTest chain from the validation guide | [Tests README](../../src/Cpp/FingerTree/tests/README.md) | Header-first C++23 suite with tearable concurrency stress controls |
| [Haskell HAMT](../../src/Haskell/Hamt/README.md) | Cabal exit-code executable | `cabal test hamt-test` from `src/Haskell` | [Tests README](../../src/Haskell/Hamt/test/README.md) | Map/set unit checks for collision buckets, custom policies, key recovery, and set algebra |
| [Haskell FingerTree](../../src/Haskell/FingerTree/README.md) | Cabal exit-code executable | `cabal test ft-test` from `src/Haskell` | [Tests README](../../src/Haskell/FingerTree/test/README.md) | Measured tree, deque, reversible deque, sorted facades, priority queue, intervals, ropes, and text helpers |
| [Kotlin HAMT](../../src/Kotlin/Hamt/README.md) | Kotlin/JVM executable test jar built by `src/Kotlin/build.ps1` | `.\build.ps1 -Workspace Hamt` from `src/Kotlin` | [Tests README](../../src/Kotlin/Hamt/tests/README.md) | Trie persistence, no-op sharing, duplicate rejection, collision buckets, original-key retention, iteration, and set algebra |
| [Kotlin FingerTree](../../src/Kotlin/FingerTree/README.md) | Kotlin/JVM executable test jar built by `src/Kotlin/build.ps1` | `.\build.ps1 -Workspace FingerTree` from `src/Kotlin` | [Tests README](../../src/Kotlin/FingerTree/tests/README.md) | Deque, reversible deque, measured tree, sorted facades, priority queue, intervals, ropes, measured ropes, text helpers, and builder coverage |
| [Rust HAMT](../../src/Rust/Hamt/README.md) | Cargo unit tests | `cargo test -p tools-data-structures-hamt` from `src/Rust` | [Tests README](../../src/Rust/Hamt/tests/README.md) | Map/set unit checks for collisions, updates, iteration, and set algebra |
| [Rust FingerTree](../../src/Rust/FingerTree/README.md) | Cargo unit tests inline across crate modules | `cargo test -p tools-data-structures-fingertree` | [Tests README](../../src/Rust/FingerTree/tests/README.md) | Structurally shared storage and cached-measure tests across deque, reversible deque, sorted collections, priority queue, intervals, ropes, measured tree, and text helpers |

## Stress And Duration Knobs

| Scope | Control | Default use | Longer-run command shape |
| --- | --- | --- | --- |
| C# FingerTree tearable concurrency stress | `FINGERTREE_STRESS_SECONDS` | Short enough for ordinary `dotnet test` | Set the variable, run `dotnet test .\FingerTree.sln --filter FullyQualifiedName~TearableConcurrencyStressTests`, then remove the variable |
| C++ FingerTree tearable concurrency stress | `FINGERTREE_STRESS_SECONDS` | Short enough for ordinary CTest | Set the variable, run CTest with `-R "^fingertree\.smoke$"`, then remove the variable |

Raise duration knobs when changing lazy memoization, atomic publication, structural sharing under concurrent reads,
or tearable element/measure paths. Record the variable value in validation evidence.

## Samples And Benchmarks

| Scope | Where documented | Routine role |
| --- | --- | --- |
| C# FingerTree samples | [Samples README](../../src/CSharp/FingerTree/samples/README.md) and [test project README](../../src/CSharp/FingerTree/tests/Tools.DataStructures.FingerTree.Tests/README.md) | Sample projects are referenced by the test project and smoke-tested by `dotnet test .\FingerTree.sln` |
| C# FingerTree benchmarks | [Benchmark notes](../../src/CSharp/FingerTree/docs/benchmarks.md) and [benchmark project README](../../src/CSharp/FingerTree/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md) | Run only for performance-sensitive code, benchmark changes, or performance claims |
| C FingerTree samples | [Samples README](../../src/C/FingerTree/samples/README.md) | Sample executables are registered as CTest smoke tests |
| C FingerTree benchmarks | [Benchmarks README](../../src/C/FingerTree/benchmarks/README.md) | Dependency-light timing harness; run for local sanity checks or benchmark-doc changes |
| C++ FingerTree samples and benchmarks | [C++ validation guide](../../src/Cpp/FingerTree/docs/validation.md#benchmark-harness-status) | Not currently checked in; Milestone 8 remains future work |

## Coverage Change Rules

- Add direct example tests for new public behavior, then add a model, property, or stress test when there is a
  natural oracle or concurrency risk.
- Keep local test READMEs current when adding, renaming, or deleting test files, sample smoke hooks, stress knobs,
  or direct executable paths.
- Update the workspace validation guide when a command, warning policy, runner shape, sample/benchmark boundary,
  or coverage claim changes.
- Update this map when a new long-lived test suite, runner, sample-smoke path, benchmark harness, or stress control
  becomes part of the repository.
