# Tungsten Collections Validation Guide

- Created (UTC): 2026-07-07T15:05:40Z
- Repository HEAD: 754f2e474caf2419bfabd5f88565341ddadbf449
- Audience: Maintainers validating changes to `Tools.DataStructures.Tungsten`
- Scope: Build and test commands, coverage map, and validation expectations

## Commands

From `src/CSharp`:

```powershell
dotnet build .\DataStructures.sln     # includes the XML-documentation warnings-as-errors gate
.\test.ps1                            # full workspace gate
.\test.ps1 -Project .\tests\Tools.DataStructures.Tungsten.Tests\Tools.DataStructures.Tungsten.Tests.csproj
```

The test launcher establishes the inherited Windows headless error mode before starting `dotnet`; the shared
test-assembly initializer repeats the setting and disables WER UI for direct runner and Test Explorer execution.
Failures remain visible in console output and the process exit code.

The library builds under the workspace `Directory.Build.props` policy: .NET 10, preview language,
nullable enabled, `GenerateDocumentationFile` with `CS1591`/`CS1573` as errors. A change that
compiles with warnings fails the gate.

## What The Suite Covers

See the [tests README](../../tests/Tools.DataStructures.Tungsten.Tests/README.md) for the source
map. The important gates:

- **Kernel-verified ordering semantics.** Every rule in the
  [API specification](api-specification.md) ordering table has a direct example test carrying the
  Tungsten expression it mirrors (duplicate-key construction, in-place `SetItem`, move-to-end
  `Append`, move-to-front `Prepend`, position-winning `Insert`, `Join`, positional slicing,
  stable `KeySort`/`Sort`, `KeyTake`). When re-verifying against a live kernel, the
  implementation-header comment in `PersistentAssociation.cs` lists the source expressions.
- **Model-based histories.** CsCheck drives generated edit histories (120 operations, 300
  iterations) against `List<T>` and ordered pair-list models, checking full contents, keyed
  lookups, positional reads, and retained snapshots (branching persistence) after every step.
- **Order-maintenance stress.** Repeated same-point inserts force stamp-gap exhaustion and full
  relabels; order, keyed lookups, and positions must survive.
- **Fused stamp edits.** Generated set/append/prepend/insert/remove histories exercise the
  one-descent sorted update/removal path while retained snapshots verify reconstruction remains
  persistent.
- **No-op identity.** Reference-equality checks for observably unchanged writes on both types.
- **Policy preservation.** Custom key comparers govern equality, survive every derivation
  (slices, sorts, reversal, removal), and stored-key retention matches the HAMT contract.
- **Exceptions.** Out-of-range, null-argument, and empty-state failures.

## Expectations For Changes

- Semantics changes must update the XML docs, the [API specification](api-specification.md), and
  the example tests together, and must be re-checked against a live Tungsten kernel when they
  claim Tungsten fidelity (`tungstenscript -code '...'` with `InputForm` printing is sufficient;
  see the repository policy on validating against the locally installed kernel).
- Complexity or allocation claims follow the repository documentation standard: state them in
  the XML remarks and keep the specification tables in sync.
- Ports must transcribe the example tests as their fidelity spec per the
  [porting guide](../../../../docs/guides/porting-and-semantic-parity.md).
