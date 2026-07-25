# C# Workspace

- Created (UTC): 2026-07-02T21:16:52Z
- Repository HEAD: 691c5c26545da0aa6135b4b28737da6c8a0ce773
- Audience: Maintainers and AI agents working in the C# source root
- Scope: Unified managed data-structure and numerics workspace under `src/CSharp`

The C# root is a single .NET 10 workspace. `Durable7.sln` contains all managed libraries, tests,
FingerTree samples, and the FingerTree benchmark harness; `Directory.Build.props` applies the shared
preview-language, nullable, documentation, warning, serialized-build, and test-runsettings policy to the tree.

Source projects live under `src/`, tests under `tests/`, runnable samples under `samples/`, benchmarks
under `benchmarks/`, and family-specific documentation under `docs/<Family>/`.

| Family | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Numerics](docs/Numerics/overview.md) | Fixed-width and sparse integer numerics library | [project](src/Durable7.Numerics/Durable7.Numerics.csproj), [API reference](docs/Numerics/api-and-behavior-reference.md), [validation](docs/Numerics/validation.md), [maintainer guidance](docs/Numerics/wide-integer-maintainer-guidance.md) | `.\test.ps1`; see [tests](tests/Durable7.Numerics.Tests/README.md) |
| [HAMT](docs/Hamt/overview.md) | Canonical CHAMP map/set with one-descent persistent map factories and one-way owner-token transients; immutable hash bag; strict two-policy persistent bimap; lock-free snapshotting Ctrie; 32/64-bit Patricia maps/sets; and the policy-bound Merkle search tree | [project](src/Durable7.Hamt/Durable7.Hamt.csproj), [usage](docs/Hamt/usage.md), [API spec](docs/Hamt/api-specification.md), [T2 shipment decision](docs/Hamt/transient-t2-decision.md), [validation](docs/Hamt/validation.md) | `.\test.ps1`; see [tests](tests/Durable7.Hamt.Tests/README.md) |
| [FingerTree](docs/FingerTree/overview.md) | Persistent sequence and aggregation family: finger trees, RRB vector, DABA Lite, sorted/priority/interval facades, ropes/text, and the independent implicit-AVL `RangeUpdateSequence` with a law-gated lazy tag action | [project](src/Durable7.FingerTree/Durable7.FingerTree.csproj), [usage](docs/FingerTree/usage.md), [API spec](docs/FingerTree/api-specification.md), [Range contract](docs/FingerTree/range-update-sequence.md), [validation](docs/FingerTree/validation.md) | `.\test.ps1`; see [tests](tests/Durable7.FingerTree.Tests/README.md), [samples](samples/README.md), and [benchmark project](benchmarks/Durable7.FingerTree.Benchmarks/README.md) |
| [Ordered](docs/Ordered/overview.md) | Independently owned general-purpose `PersistentOrderedSet<T>` over the public HAMT and FingerTree substrates, with comparer-defined membership, insertion/explicit-position order, first-representative retention, positional ranges, stable one-shot sorting, receiver-comparer algebra, and a dual CHAMP/FingerTree index | [project](src/Durable7.Ordered/Durable7.Ordered.csproj), [usage](docs/Ordered/usage.md), [API spec](docs/Ordered/api-specification.md), [validation](docs/Ordered/validation.md) | `.\test.ps1 -Project .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj`; see [tests](tests/Durable7.Ordered.Tests/README.md) |
| [Tungsten](docs/Tungsten/overview.md) | Application-specific leaf collections for the Tungsten project, composed from the public HAMT and FingerTree families: `PersistentList<T>` and insertion-ordered `PersistentAssociation<TKey, TValue>` | [project](src/Durable7.Tungsten/Durable7.Tungsten.csproj), [usage](docs/Tungsten/usage.md), [API spec](docs/Tungsten/api-specification.md), [validation](docs/Tungsten/validation.md) | `.\test.ps1`; see [tests](tests/Durable7.Tungsten.Tests/README.md) |

Tungsten may depend on the general managed libraries, but dependency direction is never reversed.
No general C# collection may reference the Tungsten project, namespace, types, internals, or behavioral
contract. Reusable mechanics must be forked into an independently owned project with its own API,
tests, documentation, and evolution policy; see the normative
[application-leaf boundary](../../docs/reference/tungsten-application-leaf-boundary.md).

`Durable7.Ordered` is such an independent general owner: it references only the public
HAMT and FingerTree projects and neither references nor derives its contract from Tungsten.

## Non-Interactive Test Runs

From `src/CSharp`, use the workspace test launcher:

```powershell
.\test.ps1
.\test.ps1 -Configuration Release
.\test.ps1 -Project .\tests\Durable7.Numerics.Tests\Durable7.Numerics.Tests.csproj
.\test.ps1 -Project .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj
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

The focused serialized Ordered Debug and Release lanes each discover and pass 62 tests with zero
build warnings. At its historical pre-Range shipment checkpoint, the complete serialized C# Release
gate built with zero warnings or errors and passed all 1,355 tests.

The shipped Range-update tranche added 62 focused Range tests to the then-692-test FingerTree
project. Its historical complete serialized C# Debug and Release gates passed 1,417/1,417 tests;
the later bimap shipment passed 16 focused tests and all 308 HAMT tests in both configurations.
The Range gate covers algebra laws, implicit-AVL/tag/measure invariants,
API and identity semantics, generated retained-branch models, deterministic operation ceilings,
failure atomicity, enumerator behavior, and concurrent readers. No benchmark was run for shipment;
all performance measurements remain postponed until they can run without competing agents or other
CPU, memory, and I/O contention.

The current C# derived-structure tranche additionally ships `PersistentOrderedMultimap`,
`PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, and
`PersistentChunkedBitSet`. Complete serialized Debug and Release solution builds both finish with
zero warnings and zero errors, and both full gates pass 1,503/1,503 tests: 319 Numerics + 347 HAMT
+ 709 FingerTree + 76 Ordered + 52 Tungsten. The five focused new-structure lanes pass 38/38 tests.
Benchmarks were not run.

Cross-language follow-through is complete: the one-descent HAMT operations, hash bag, strict bimap,
neutral ordered set/map/multimap, hash multimap, relation, interval map, map patch, directed graph,
indexed map, chunked bit set, and `RangeUpdateSequence` now ship in C, C++, Haskell, Kotlin, Rust,
TypeScript, and Python under language-local APIs and ownership models.

Use the parent [source index](../README.md) for the full language list, the repository
[workspace map](../../docs/reference/workspace-map.md) for port lineage, and the
[semantic contracts reference](../../docs/reference/semantic-contracts.md) plus the
[porting guide](../../docs/guides/porting-and-semantic-parity.md) when changing semantics that should stay aligned
with sibling ports.
