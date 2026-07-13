# C++ HAMT Tests

- Created (UTC): 2026-07-02T21:13:37Z
- Repository HEAD: 30159246f73321480596ee7d9971a951f939280d
- Audience: Maintainers validating the C++ HAMT port
- Scope: Native test executable and source organization under `src/Cpp/Hamt/tests`

The C++ HAMT workspace has two dependency-free native suites and one installed-header consumer. The
workspace [`build.ps1`](../build.ps1) script builds:

- `persistent_hamt_tests.cpp` as `build/<Configuration>/persistent_hamt_tests.exe`;
- `merkle_search_tree_tests.cpp` as `build/<Configuration>/merkle_search_tree_tests.exe`; and
- `merkle_header_consumer.cpp` as `build/<Configuration>/merkle_header_consumer.exe` against a
  copied `build/<Configuration>/package/include` tree rather than the source include directory.

All three programs link Windows CNG through `bcrypt.lib`. The copied-header consumer validates that
the public aggregate and Merkle headers are self-contained when consumed like installed files.

Each full suite contains a small local registry behind the `TEST` macro. The runner executes every
registered test, prints `[PASS]` or `[FAIL]` per case, reports thrown exceptions with diagnostics,
and exits non-zero if any case fails. A successful run ends with `<N> test(s) passed`. The focused
header consumer reports only failures and exits zero after its compile/link/runtime contract holds.

## CHAMP And Patricia Test Cases

The executable registers these cases:

- `EmptyMap_HasNoEntries`
- `SetItem_AddsReplacesAndPreservesOldVersions`
- `AddAndTryAdd_RejectDuplicates`
- `RemoveAndTryRemove_DeletePresentKeys`
- `CreateRange_LastWinsAndRetainsFirstEquivalentKey`
- `KeysAndValues_AlignWithPairEnumeration`
- `HelperKeyComparisons_AreExercisedForPortableWarningBuilds`
- `EqualHashCollisionBucket_PreservesEveryKey`
- `DeepSharedHashPrefixes_LookupAndRemoveCorrectly`
- `CollisionBucket_SplitsWhenDifferentHashKeyArrives`
- `CollisionBucket_HashMismatchProbesMissAndSplitDeeply`
- `CollisionBucket_EqualValueKeepsRootAndReplaceKeepsKey`
- `Structure_RootShapeTracksContentsAndCollapse`
- `Structure_UpdateSharesUntouchedSiblingSubtrees`
- `Champ_IndependentHistoriesAndTypedDiff`
- `Champ_TopologyComparatorRejectsDifferentCollisionKeys`
- `Patricia_SignedOrderingHistoriesAndStructuralAlgebra`
- `PatriciaMap_CachedCountsAndNoOpAlgebraPreserveRoots`
- `Enumerator_CopiedIteratorAdvancesIndependently`
- `Enumerator_RetainsTheTrieBeyondTheSourceMapValue`
- `RandomHistory_MatchesUnorderedMapAndPreservesSnapshots`
- `ScriptedCollisionSnapshotStory_MatchesModel`
- `RandomHistory_WithCollidingHashes_MatchesUnorderedMap`
- `ConcurrentReaders_ObserveConsistentRetainedSnapshots`
- `Set_AddRemoveContainsAndPersistence`
- `Set_TryAddAndTryRemove_ReportWhetherMembershipChanged`
- `Set_CustomComparerDefinesEqualityAndRetainsFirstItem`
- `Set_AlgebraMatchesUnorderedSet`
- `Set_AlgebraHonorsCustomComparer`
- `Set_SymmetricExceptTreatsInputDuplicatesAsOneItem`
- `Map_MovedFromMapReadsAsEmpty`
- `Set_MovedFromSetReadsAsEmpty`
- `BulkBuilder_FrozenSnapshotsRemainImmutableAcrossBuilderMutations`
- `BulkBuilder_EquivalentKeysRetainFirstKeyAndEqualValue`
- `BulkBuilder_DeepPrefixKeysBranchAtFinalHashLevel`
- `BulkBuilder_RandomizedBuildMatchesPersistentUpdates`
- `BulkBuilder_CreateRangeAndIntersectionUseBuilderSemantics`

The CHAMP canonicalization fixture uses explicit spreading hashes on every standard library, an
exact deep-bridge collapse case, and all four reachable terminal fragments. Validation checks full
hash-prefix routing; topology comparison matches collision keys through `KeyEqual` without depending
on collision insertion order.

## Merkle Core, Wire, And Persistence Coverage

`merkle_search_tree_tests.cpp` covers the in-memory core, exact wire, and verified persistence tier. Its groups
exercise:

- exact signed big-endian integer, tagged nullable UTF-8/bytes, RFC-4122 GUID, SHA-256 digest, and
  malformed/noncanonical codec behavior;
- Unicode-whitespace-aware policy/codec identifier validation plus explicit policy identity and
  domain compatibility;
- the shared cross-language domain digest, empty digest, one-entry root, and every byte of the
  canonical `MST2` block;
- encoded-value no-op identity and absent-remove root reuse;
- canonical convergence across bulk, incremental, removal/reinsertion, and independent histories;
- move-only key/value representatives, owning entry handles, ordered iteration, inclusive ranges,
  semantic equality, typed diff, and block/shape/statistics diagnostics;
- root, policy, entry, and untouched-block sharing across retained snapshots;
- a 12,000-operation deterministic randomized history against `std::map`, including retained
  versions and independently rebuilt canonical contents;
- unchanged published snapshots when codecs or comparers throw;
- deep validator rejection of a retained representative whose current encoding disagrees with its
  captured canonical bytes; and
- concurrent readers over retained immutable snapshots.

The registered cases are:

- `CanonicalCodecsAndDigestRejectMalformedRepresentations`
- `PolicyValidationIdentityAndCompatibilityAreExplicit`
- `SingleEntryMst2GoldenLocksDomainRootAndExactBlockBytes`
- `CanonicalConstructionIsIndependentOfHistoryAndPolicyIdentity`
- `PersistentMutationSharingRangeAndDiffHonorMapSemantics`
- `MoveOnlyKeysAndValuesExposeStableSharedRepresentatives`
- `RandomizedPersistentHistoriesMatchOrderedMapAndRetainedSnapshots`
- `ThrowingComparersAndCodecsLeavePersistentSourcesUntouched`
- `ValidatorDetectsMutationBehindAConstRepresentative`
- `ConcurrentReadersObserveConsistentRetainedMerkleSnapshots`
- `PersistenceGoldenPackAndMsp2QueriesMatchEverySiblingPort`
- `PersistenceSaveLoadImportAndPartialOverlayRoundTripExactClosure`
- `PersistenceRejectsMissingTamperedMalformedForeignAndCountCorruption`
- `PersistenceSevenBudgetsAndProofShapeArePreflightedStrictly`
- `PersistenceProofsRejectTamperingExtrasMissingStepsAndBadExpansions`
- `PersistenceClosurePrunedPacksAndIterativeSyncConverge`
- `PersistenceThreeWayMergeIsPresentNullSafeAndNeverPublishesPartialOutput`
- `PersistenceMoveOnlyKeysAndValuesLoadProveImportAndMerge`
- `PersistenceStoreLoadProofAndSyncAreConcurrentSafe`

The persistence groups lock exact `MSP2`, complete/partial closure round trips, destination
preflight, missing/tampered/noncanonical inputs, all seven limits and proof-preflight precedence,
proof expansion/tampering, iterative sync, present-null/no-partial merge, move-only values, and
concurrent store/load/proof/sync.

`merkle_header_consumer.cpp` includes the aggregate header from the copied package tree, creates and
validates the one-entry golden tree, then instantiates export/save/load, proof verification, and
merge. It is an independent public-header closure and crypto-link gate, not a replacement for the
model suite.

## Build And Run

From `src/Cpp/Hamt`, build and run the Debug test executable:

```powershell
.\build.ps1 -RunTests
```

Run the built executable directly when changing runner diagnostics or investigating a local failure:

```powershell
.\build\Debug\persistent_hamt_tests.exe
.\build\Debug\merkle_search_tree_tests.exe
.\build\Debug\merkle_header_consumer.exe
```

Use the workspace [validation guide](../docs/validation.md) for Release validation, portable GCC
and Clang lanes, compiler flags, generated-output locations, and coverage policy. See the
[Merkle core](../docs/merkle-search-tree.md) and
[persistence specification](../docs/merkle-persistence.md) for the exact contracts under test.
