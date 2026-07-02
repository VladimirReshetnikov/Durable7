# C++ HAMT Tests

- Created (UTC): 2026-07-02T21:13:37Z
- Repository HEAD: 30159246f73321480596ee7d9971a951f939280d
- Audience: Maintainers validating the C++ HAMT port
- Scope: Native test executable and source organization under `src/Cpp/Hamt/tests`

`persistent_hamt_tests.cpp` is the dependency-free native test executable for the C++ HAMT port. The workspace
[`build.ps1`](../build.ps1) script compiles it into `build/<Configuration>/persistent_hamt_tests.exe`.

The file contains a small local registry behind the `TEST` macro. The runner executes every registered test, prints
`[PASS]` or `[FAIL]` per case, reports thrown exceptions with diagnostics, and exits non-zero if any case fails. A
successful run ends with `<N> test(s) passed`.

## Test Cases

The executable registers these cases:

- `EmptyMap_HasNoEntries`
- `SetItem_AddsReplacesAndPreservesOldVersions`
- `AddAndTryAdd_RejectDuplicates`
- `RemoveAndTryRemove_DeletePresentKeys`
- `CreateRange_LastWinsAndRetainsFirstEquivalentKey`
- `KeysAndValues_AlignWithPairEnumeration`
- `EqualHashCollisionBucket_PreservesEveryKey`
- `DeepSharedHashPrefixes_LookupAndRemoveCorrectly`
- `CollisionBucket_SplitsWhenDifferentHashKeyArrives`
- `CollisionBucket_HashMismatchProbesMissAndSplitDeeply`
- `CollisionBucket_EqualValueKeepsRootAndReplaceKeepsKey`
- `Structure_RootShapeTracksContentsAndCollapse`
- `Structure_UpdateSharesUntouchedSiblingSubtrees`
- `Enumerator_CopiedIteratorAdvancesIndependently`
- `RandomHistory_MatchesUnorderedMapAndPreservesSnapshots`
- `RandomHistory_WithCollidingHashes_MatchesUnorderedMap`
- `Set_AddRemoveContainsAndPersistence`
- `Set_TryAddAndTryRemove_ReportWhetherMembershipChanged`
- `Set_CustomComparerDefinesEqualityAndRetainsFirstItem`
- `Set_AlgebraMatchesUnorderedSet`
- `Set_AlgebraHonorsCustomComparer`
- `Set_SymmetricExceptTreatsInputDuplicatesAsOneItem`

## Build And Run

From `src/Cpp/Hamt`, build and run the Debug test executable:

```powershell
.\build.ps1 -RunTests
```

Run the built executable directly when changing runner diagnostics or investigating a local failure:

```powershell
.\build\Debug\persistent_hamt_tests.exe
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, compiler flags,
generated-output locations, and coverage policy.
