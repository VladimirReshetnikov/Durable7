# C# HAMT Tests

- Created (UTC): 2026-07-02T21:19:42Z
- Repository HEAD: e375d5f1b031745ac97cf2ae81e0d91cf03ec22e
- Audience: Maintainers validating the C# HAMT workspace
- Scope: xUnit and CsCheck test project under `src/CSharp/tests/Tools.DataStructures.Hamt.Tests`

`Tools.DataStructures.Hamt.Tests` is the managed test project for the C# HAMT library. It targets the workspace
defaults from `Directory.Build.props`, references the public `Tools.DataStructures.Hamt` project, and uses xUnit,
`Microsoft.NET.Test.Sdk`, `xunit.runner.visualstudio`, and CsCheck.

## Source Map

- `PersistentHashMapTests.cs` covers construction, lookup, insertion, replacement, removal, no-op behavior,
  comparer preservation, value materialization, concurrent snapshot readers, and immutable-version publication.
- `PersistentHashMapContractOracleTests.cs` and `PersistentHashSetContractOracleTests.cs` are the Axis 2
  executable semantic baseline for comparer identity, stored representatives, collisions, nullable keys/items,
  stable enumeration, no-op identity, retained versions, and callback-exception atomicity.
- `PersistentHashMapDiagnosticsTests.cs` pins the benchmark-only root, node/array sharing, path-copy,
  ownership, retained-size, lookup-visit, canonical bulk-builder diagnostic seam, exact ordinary
  field layout, and b590 source fingerprints for the ordinary nodes and monomorphic lookup loop.
- `PersistentHashMapSeparateNodeKernelTests.cs` validates the private Axis 2 T1 owner-free gate
  build: O(1) adoption/sealing, first-edit ordinary deferral, reusable-path promotion on a later edit,
  copy-on-first-write ownership in separate transient-editable branch/collision classes, lazy token
  and commit-plan allocation, production/diagnostic mode parity, base and published-version isolation,
  recursive CHAMP canonicality/counts, representatives/collisions, consumed sessions,
  callback/allocation/publication failure atomicity including deferred promotion rollback,
  and mixed published hierarchies through alternating exact-type lookup, enumeration, persistent
  update/remove, semantic equality, bidirectional structural diff, and all four map-algebra paths.
- `PersistentHashMapEnumeratorTests.cs` covers allocation-free struct enumerators, copied enumerator independence,
  and key/value/pair enumeration.
- `PersistentHashSetEnumeratorTests.cs` covers the set wrapper's default, before-first, active, exhausted,
  copied, and interface/reset enumerator states.
- `PersistentHashMapCollisionTests.cs` covers equal-hash buckets, deep shared hash prefixes, collision splitting,
  hash-mismatch misses, and equivalent-key retention.
- `PersistentHamtStructureTests.cs` uses internal test access to verify CHAMP data/node maps, canonical
  independent-history topology, collapse behavior, structural equality, slot-aligned diff through
  leaf/collision/branch transitions, randomized invariants, eager validation, no-op root reuse, and sharing.
- `PersistentHashMapBulkBuilderTests.cs` verifies duplicate retention, null/deep-prefix keys, collision and branch
  freezes, and immutable snapshot detachment while the bulk builder continues accepting entries.
- `PersistentHashMapPropertyTests.cs` uses CsCheck generated histories against dictionary-style model state,
  including retained snapshots and deliberately colliding hashes.
- `PersistentHashSetTests.cs` covers set membership, add/remove, try-add/try-remove, custom equality, set algebra,
  `IReadOnlySet<T>` behavior, generated set-algebra checks, and concurrent snapshot readers.
- `ConcurrentHashTrieTests.cs` covers linearizable mutation, generation stamps, stable O(1)
  snapshots/enumerators, deterministic root-RDCSS and GCAS-helping schedules, tomb contraction,
  collision-node re-splitting, stored-key retention, contended updates, concurrent snapshot
  consistency, lazy generation renewal, same-reference value no-ops without equality callbacks,
  and explicit snapshot-to-CHAMP conversion.
- `ConcurrentHashTrieLinearizabilityTests.cs` records invocation/response intervals for 400 mixed
  five-operation histories and exhaustively checks all real-time-compatible serializations against a
  dictionary model under ordinary, shared-prefix, and all-equal-hash policies.
- `PersistentIntegerPatriciaTests.cs` covers signed boundary ordering, randomized 32/64-bit model
  histories, retained snapshots, structural map algebra, combining overloads including exact self-operation
  callback counts, and set relations.
- `PersistentHashSetStructuralAlgebraTests.cs` covers same-root zero-callback pruning, shared-ancestry
  no-rehash algebra, comparer admission, representative retention, collision-heavy randomized set
  models, same-type relations, and the corresponding structural map operations.
- `MerkleSearchTreeTests.cs` covers canonical codec vectors, independent policy/history shape and
  root-address convergence, typed digest-pruned diff, randomized histories/snapshots, ranges, and domain separation.
- `MerkleEncodingWireTests.cs` locks built-in codec ids and golden vectors, canonical round trips,
  malformed/trailing-input rejection, policy/domain framing, and strict digest parsing/writing.
- `MerklePersistenceAlgorithmsTests.cs` covers golden block bytes, save/load/export/import,
  destination atomicity, malformed/tampered/resource-bounded verification, proof-query budgets,
  membership/non-membership/range proofs, synchronization plans, and typed three-way merge.
- `MerkleSearchTreeCoreStressTests.cs` drives adversarial layers, long randomized mutation histories,
  retained snapshots, independent-history convergence, shape-changing diff, off-path block sharing,
  and exact-root restoration against ordered models.

## Build And Run

From `src/CSharp`, run the full solution test gate:

```powershell
.\test.ps1
```

Run only this test project when iterating on test code:

```powershell
.\test.ps1 -Project .\tests\Tools.DataStructures.Hamt.Tests\Tools.DataStructures.Hamt.Tests.csproj
```

Filter a class while developing a focused change:

```powershell
.\test.ps1 -Filter FullyQualifiedName~PersistentHashMapPropertyTests
```

The launcher suppresses modal Windows loader/crash reporting for the complete `dotnet` child-process tree. The
test assembly repeats the headless process configuration during module initialization, so direct test-runner and
Test Explorer execution is non-interactive after the assembly loads as well.

Use the workspace [validation guide](../../docs/Hamt/validation.md) for restore/build split commands, warning policy,
and evidence expectations.
