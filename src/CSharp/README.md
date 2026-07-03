# C# Workspace

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C# source root
- Scope: Unified managed data-structure and numerics workspace under `src/CSharp`

The C# root is a single .NET 10 workspace. `DataStructures.sln` contains all managed libraries, tests,
FingerTree samples, and the FingerTree benchmark harness; `Directory.Build.props` applies the shared
preview-language, nullable, documentation, and warning policy to the tree.

Source projects live under `src/`, tests under `tests/`, runnable samples under `samples/`, benchmarks
under `benchmarks/`, and family-specific documentation under `docs/<Family>/`.

| Family | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Numerics](docs/Numerics/overview.md) | Fixed-width and sparse integer numerics library | [project](src/Tools.Numerics/Tools.Numerics.csproj), [API reference](docs/Numerics/api-and-behavior-reference.md), [validation](docs/Numerics/validation.md), [maintainer guidance](docs/Numerics/wide-integer-maintainer-guidance.md) | `dotnet test .\DataStructures.sln`; see [tests](tests/Tools.Numerics.Tests/README.md) |
| [HAMT](docs/Hamt/overview.md) | Canonical managed persistent HAMT map/set library | [project](src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj), [usage](docs/Hamt/usage.md), [API spec](docs/Hamt/api-specification.md), [validation](docs/Hamt/validation.md) | `dotnet test .\DataStructures.sln`; see [tests](tests/Tools.DataStructures.Hamt.Tests/README.md) |
| [FingerTree](docs/FingerTree/overview.md) | Canonical managed FingerTree family: deque, measured tree, sorted collections, priority queue, intervals, ropes, text, samples, and benchmarks | [project](src/Tools.DataStructures.FingerTree/Tools.DataStructures.FingerTree.csproj), [usage](docs/FingerTree/usage.md), [API spec](docs/FingerTree/api-specification.md), [validation](docs/FingerTree/validation.md) | `dotnet test .\DataStructures.sln`; see [tests](tests/Tools.DataStructures.FingerTree.Tests/README.md), [samples](samples/README.md), and [benchmark project](benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md) |

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for port lineage, and the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) plus the
[porting guide](../../docs/guides/porting-and-semantic-parity.md) when changing semantics that should stay aligned
with sibling ports.
