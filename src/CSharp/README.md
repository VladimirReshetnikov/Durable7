# C# Workspace

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C# source root
- Scope: Unified managed data-structure and numerics workspace under `src/CSharp`

The C# root is a single .NET 10 workspace. `DataStructures.sln` contains all managed libraries, tests,
FingerTree samples, and the FingerTree benchmark harness; `Directory.Build.props` applies the shared
preview-language, nullable, documentation, warning, serialized-build, and test-runsettings policy to the tree.

Source projects live under `src/`, tests under `tests/`, runnable samples under `samples/`, benchmarks
under `benchmarks/`, and family-specific documentation under `docs/<Family>/`.

| Family | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Numerics](docs/Numerics/overview.md) | Fixed-width and sparse integer numerics library | [project](src/Tools.Numerics/Tools.Numerics.csproj), [API reference](docs/Numerics/api-and-behavior-reference.md), [validation](docs/Numerics/validation.md), [maintainer guidance](docs/Numerics/wide-integer-maintainer-guidance.md) | `.\test.ps1`; see [tests](tests/Tools.Numerics.Tests/README.md) |
| [HAMT](docs/Hamt/overview.md) | Canonical CHAMP with one-descent persistent map factories, one-way owner-token map/set transients, lock-free snapshotting Ctrie, 32/64-bit Patricia maps/sets, and the policy-bound Merkle search tree | [project](src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj), [usage](docs/Hamt/usage.md), [API spec](docs/Hamt/api-specification.md), [T2 shipment decision](docs/Hamt/transient-t2-decision.md), [validation](docs/Hamt/validation.md) | `.\test.ps1`; see [tests](tests/Tools.DataStructures.Hamt.Tests/README.md) |
| [FingerTree](docs/FingerTree/overview.md) | Persistent sequence and aggregation family: finger trees, RRB vector, DABA Lite, sorted/priority/interval facades, ropes, and text | [project](src/Tools.DataStructures.FingerTree/Tools.DataStructures.FingerTree.csproj), [usage](docs/FingerTree/usage.md), [API spec](docs/FingerTree/api-specification.md), [validation](docs/FingerTree/validation.md) | `.\test.ps1`; see [tests](tests/Tools.DataStructures.FingerTree.Tests/README.md), [samples](samples/README.md), and [benchmark project](benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md) |
| [Tungsten](docs/Tungsten/overview.md) | Application-specific leaf collections for the Tungsten project, composed from the public HAMT and FingerTree families: `PersistentList<T>` and insertion-ordered `PersistentAssociation<TKey, TValue>` | [project](src/Tools.DataStructures.Tungsten/Tools.DataStructures.Tungsten.csproj), [usage](docs/Tungsten/usage.md), [API spec](docs/Tungsten/api-specification.md), [validation](docs/Tungsten/validation.md) | `.\test.ps1`; see [tests](tests/Tools.DataStructures.Tungsten.Tests/README.md) |

Tungsten may depend on the general managed libraries, but dependency direction is never reversed.
No general C# collection may reference the Tungsten project, namespace, types, internals, or behavioral
contract. Reusable mechanics must be forked into an independently owned project with its own API,
tests, documentation, and evolution policy; see the normative
[application-leaf boundary](../../docs/reference/tungsten-application-leaf-boundary.md).

## Non-Interactive Test Runs

From `src/CSharp`, use the workspace test launcher:

```powershell
.\test.ps1
.\test.ps1 -Configuration Release
.\test.ps1 -Project .\tests\Tools.Numerics.Tests\Tools.Numerics.Tests.csproj
.\test.ps1 -Filter FullyQualifiedName~SparseIntegerTests
```

The launcher enables the repository-wide headless Windows error mode before it starts `dotnet`, so the SDK,
MSBuild, vstest, testhost, and their descendants return loader, critical-I/O, and crash failures through exit codes
and console output instead of modal dialogs. Every test assembly also includes an early module initializer that
reasserts the process error mode and disables Windows Error Reporting UI; this covers direct Test Explorer and
`dotnet test` execution after the CLR has loaded the test assembly. The launcher remains the canonical unattended
entry point because only its inherited process setting can cover failures before managed startup.

`test.runsettings` is applied automatically through `Directory.Build.props`, treats a run that discovers no
tests as an error, and limits vstest/xUnit to one host/thread. The launcher disables build servers and forces one
MSBuild node while the shared properties disable parallel restore/project builds and compiler sharing.
`-NoRestore`, `-NoBuild`, and `-Blame` map to their `dotnet test` counterparts; `-AdditionalArguments` forwards
explicit dotnet arguments. Custom settings switches/properties and opaque response files are rejected so they
cannot replace the single-worker policy. To forward test-host RunSettings arguments from PowerShell, pass the
separator literally, for example
`-AdditionalArguments @('--', 'RunConfiguration.TestSessionTimeout=60000')`; a bare `--` is consumed by
PowerShell. Run restore, build, and test sequentially.

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for port lineage, and the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) plus the
[porting guide](../../docs/guides/porting-and-semantic-parity.md) when changing semantics that should stay aligned
with sibling ports.
