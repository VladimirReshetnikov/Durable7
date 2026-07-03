# C# Workspaces

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C# source root
- Scope: Managed data-structure and numerics workspaces under `src/CSharp`

The C# root contains managed reference workspaces for the repository's persistent data structures and fixed-width
integer numerics. These libraries target .NET 10 and provide the broadest semantic baseline for managed API and
validation work.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Numerics](Numerics/README.md) | Fixed-width and sparse integer numerics library | `Numerics.sln`, [API reference](Numerics/docs/api-and-behavior-reference.md), [validation](Numerics/docs/validation.md), [maintainer guidance](Numerics/docs/wide-integer-maintainer-guidance.md) | `dotnet test .\Numerics.sln`; see [tests](Numerics/tests/Tools.Numerics.Tests/README.md) |
| [Hamt](Hamt/README.md) | Canonical managed persistent HAMT map/set library | `Hamt.sln`, [usage](Hamt/docs/usage.md), [API spec](Hamt/docs/api-specification.md), [validation](Hamt/docs/validation.md) | `dotnet test .\Hamt.sln`; see [tests](Hamt/tests/Tools.DataStructures.Hamt.Tests/README.md) |
| [FingerTree](FingerTree/README.md) | Canonical managed FingerTree family: deque, measured tree, sorted collections, priority queue, intervals, ropes, text, samples, and benchmarks | `FingerTree.sln`, [usage](FingerTree/docs/usage.md), [API spec](FingerTree/docs/api-specification.md), [validation](FingerTree/docs/validation.md) | `dotnet test .\FingerTree.sln`; see [tests](FingerTree/tests/Tools.DataStructures.FingerTree.Tests/README.md), [samples](FingerTree/samples/README.md), and [benchmark project](FingerTree/benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md) |

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for port lineage, and the
[porting guide](../../docs/guides/porting-and-semantic-parity.md) when changing semantics that should stay aligned
with the native C and C++ ports.
