# C# Tungsten Collections Tests

- Created (UTC): 2026-07-07T15:05:40Z
- Repository HEAD: 754f2e474caf2419bfabd5f88565341ddadbf449
- Audience: Maintainers validating the C# Tungsten-collections workspace
- Scope: xUnit and CsCheck test project under `src/CSharp/tests/Tools.DataStructures.Tungsten.Tests`

`Tools.DataStructures.Tungsten.Tests` is the managed test project for the C# Tungsten-collections
library. It targets the workspace defaults from `Directory.Build.props`, references the public
`Tools.DataStructures.Tungsten` project, and uses xUnit, `Microsoft.NET.Test.Sdk`,
`xunit.runner.visualstudio`, and CsCheck.

## Source Map

- `PersistentListTests.cs` covers the Tungsten-List operation surface of `PersistentList<T>`:
  construction, end and positional edits, ranges and splits, reverse, map, membership,
  enumeration, persistence across retained versions, no-op identity, and argument validation.
- `PersistentAssociationTests.cs` covers `PersistentAssociation<TKey, TValue>` with the
  kernel-verified Tungsten Association ordering semantics as the fidelity spec: duplicate-key
  construction, in-place `SetItem`, move-to-end `Append` and move-to-front `Prepend`,
  position-winning `Insert`, `Join`, keyed and positional removal, slicing, `Reverse`,
  stable `KeySort`/`Sort`, `KeyTake`, comparer preservation and stored-key retention, no-op
  identity, relabeling stress (repeated same-point inserts), and argument validation.
- `PersistentListPropertyTests.cs` uses CsCheck generated edit histories against a `List<T>`
  model, including retained snapshots for persistence.
- `PersistentAssociationPropertyTests.cs` uses CsCheck generated histories against an ordered
  pair-list model implementing the Tungsten ordering rules, including retained snapshots.

## Build And Run

From `src/CSharp`, run the full solution test gate:

```powershell
dotnet test .\DataStructures.sln
```

Or run only this project:

```powershell
dotnet test .\tests\Tools.DataStructures.Tungsten.Tests\Tools.DataStructures.Tungsten.Tests.csproj
```
