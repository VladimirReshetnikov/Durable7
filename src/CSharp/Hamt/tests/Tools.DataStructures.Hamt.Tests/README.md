# C# HAMT Tests

- Created (UTC): 2026-07-02T21:19:42Z
- Repository HEAD: e375d5f1b031745ac97cf2ae81e0d91cf03ec22e
- Audience: Maintainers validating the C# HAMT workspace
- Scope: xUnit and CsCheck test project under `src/CSharp/Hamt/tests/Tools.DataStructures.Hamt.Tests`

`Tools.DataStructures.Hamt.Tests` is the managed test project for the C# HAMT library. It targets the workspace
defaults from `Directory.Build.props`, references the public `Tools.DataStructures.Hamt` project, and uses xUnit,
`Microsoft.NET.Test.Sdk`, `xunit.runner.visualstudio`, and CsCheck.

## Source Map

- `PersistentHashMapTests.cs` covers construction, lookup, insertion, replacement, removal, no-op behavior,
  comparer preservation, and value materialization.
- `PersistentHashMapEnumeratorTests.cs` covers allocation-free struct enumerators, copied enumerator independence,
  and key/value/pair enumeration.
- `PersistentHashMapCollisionTests.cs` covers equal-hash buckets, deep shared hash prefixes, collision splitting,
  hash-mismatch misses, and equivalent-key retention.
- `PersistentHamtStructureTests.cs` uses internal test access to verify root shape, collapse behavior, no-op root
  reuse, and structural sharing of untouched subtrees.
- `PersistentHashMapPropertyTests.cs` uses CsCheck generated histories against dictionary-style model state,
  including retained snapshots and deliberately colliding hashes.
- `PersistentHashSetTests.cs` covers set membership, add/remove, try-add/try-remove, custom equality, set algebra,
  `IReadOnlySet<T>` behavior, and generated set-algebra checks.

## Build And Run

From `src/CSharp/Hamt`, run the full solution test gate:

```powershell
dotnet test .\Hamt.sln
```

Run only this test project when iterating on test code:

```powershell
dotnet test .\tests\Tools.DataStructures.Hamt.Tests\Tools.DataStructures.Hamt.Tests.csproj
```

Filter a class while developing a focused change:

```powershell
dotnet test .\Hamt.sln --filter FullyQualifiedName~PersistentHashMapPropertyTests
```

Use the workspace [validation guide](../../docs/validation.md) for restore/build split commands, warning policy,
and evidence expectations.
