# C# HAMT Tests

- Created (UTC): 2026-07-02T21:19:42Z
- Repository HEAD: e375d5f1b031745ac97cf2ae81e0d91cf03ec22e
- Audience: Maintainers validating the C# HAMT workspace
- Scope: xUnit and CsCheck test project under `src/CSharp/tests/Durable7.Hamt.Tests`

`Durable7.Hamt.Tests` is the managed test project for the C# HAMT library, including the
CHAMP map/set/bag/multimap/relation/bimap, map-patch, directed-graph, indexed-map, Ctrie, Patricia,
and Merkle families. It targets the workspace defaults from
`Directory.Build.props`, references the public `Durable7.Hamt` project, and uses xUnit,
`Microsoft.NET.Test.Sdk`, `xunit.runner.visualstudio`, and CsCheck.

## Source Map

- `PersistentHashMapTests.cs` covers construction, lookup, insertion, replacement, removal, no-op behavior,
  comparer preservation, value materialization, concurrent snapshot readers, and immutable-version publication.
- `PersistentHashMapSinglePassUpdateTests.cs` covers persistent `GetOrAdd`/`AddOrUpdate`: eager
  delegate validation, exact factory/hash/equality counts, leaf/collision/bitmap and published
  separate-node paths, stored representatives, present nulls, allocation-free no-ops, callback
  failure atomicity, retained roots, and generated-history canonicality.
- `PersistentHashBagTests.cs` covers construction and lookup, explicit distinct/expanded counts,
  comparer-preserving empties, first representatives, point updates and saturated removal, zero and
  negative copy counts, checked multiplicity/array boundaries, null and collision policies, source
  retention on callback failure, and recursive bag/CHAMP diagnostics.
- `PersistentHashBagAlgebraTests.cs` covers maximum union, minimum intersection, saturated
  difference, checked sum, receiver-comparer normalization, checked collapsed argument classes,
  eager normalization failure, receiver and argument representative precedence, no-op identity,
  self operations, comparer-preserving empty results, and failure atomicity.
- `PersistentHashBagEnumeratorTests.cs` covers expanded occurrence order, matching distinct/entry
  order, concrete/interface/default/before-first/active/exhausted/copied/reset behavior,
  allocation-free concrete enumeration, immutable retained views, and `ToArray` parity and
  `Array.MaxLength` preflight failure.
- `PersistentHashBagApiShapeTests.cs` reflection-locks the exact bag factory/property/query/update/
  algebra/enumeration surface, excludes ambiguous `Count`/`IReadOnlyCollection<T>` and mutable
  lifecycle additions, and verifies that the debugger proxy projects distinct entries instead of
  expanded occurrences.
- `PersistentHashBagPropertyTests.cs` runs deterministic comparer-aware linear-model histories with
  retained snapshots, first representatives, nullable and collision-heavy policies, randomized
  algebra, and invariant validation after commands.
- `PersistentHashMultimapTests.cs` covers independent key/value comparers, first representatives in
  both domains, distinct key/pair counts, duplicate identity, grouped and flattened traversal,
  comparer-preserving absent groups, pair and whole-key removal, last-value group contraction,
  retained branching histories, and recursive invariant validation.
- `PersistentRelationTests.cs` covers many-to-many adjacency, independent domain policies, duplicate
  identity, globally normalized first representatives, symmetric pair and whole-domain removal,
  comparer-preserving absent results, cached inverse identity, branching persistence, and mutually
  inverse multimap invariants.
- `PersistentMapPatchTests.cs` covers computed deltas, strict application, presence-safe nulls,
  conflict atomicity, inversion, composition, policy compatibility, no-op elimination, retained
  representatives, and patch invariants.
- `PersistentDirectedGraphTests.cs` covers explicit/isolated vertices, bidirectional adjacency,
  endpoint normalization, duplicate identity, self-loops, incident removal, cached reversal,
  retained branches, and graph/relation invariants.
- `PersistentIndexedMapTests.cs` covers primary and nonunique secondary lookups, strict/no-op
  updates, selector call counts and failures, group movement/contraction, representatives, policies,
  retained branches, and coupled-index invariants.
- `PersistentBiMapTests.cs` covers independent comparer retention, strict duplicate rejection on
  both domains, stored representatives, configured-value-comparer replacement, claimed-value
  conflicts, symmetric removal, cached inverse identity, nullable representatives, enumerator
  behavior, comparer failure atomicity, a 1,000-command retained-version model, concurrent readers,
  recursive bijection validation, and the closed immutable API shape.
- `PersistentAncestralConnectionForestTests.cs` covers the experimental insertion-only connectivity
  forest, branching history versions, earliest-connection queries, redundant links, retained
  versions, randomized model parity, structural bounds, failure atomicity, and concurrent readers.
- `PersistentHashMapContractOracleTests.cs` and `PersistentHashSetContractOracleTests.cs` are the Axis 2
  executable semantic baseline for comparer identity, stored representatives, collisions, nullable keys/items,
  stable enumeration, no-op identity, retained versions, and callback-exception atomicity.
- `PersistentHashMapDiagnosticsTests.cs` pins the benchmark-only root, node/array sharing, path-copy,
  ownership, retained-size, lookup-visit, canonical bulk-builder diagnostic seam, exact ordinary
  field layout, and b590 source fingerprints for the ordinary nodes and monomorphic lookup loop.
- `PersistentHashMapSeparateNodeKernelTests.cs` validates the
  [selected private Axis 2 T1 direct-separate kernel](../../docs/Hamt/transient-t1-decision.md): O(1)
  adoption/sealing, first-edit ordinary deferral, reusable-path promotion on a later edit,
  copy-on-first-write ownership in separate transient-editable branch/collision classes, lazy token
  and commit-plan allocation, production/diagnostic mode parity, base and published-version isolation,
  recursive CHAMP canonicality/counts, representatives/collisions, consumed sessions,
  callback/allocation/publication failure atomicity including deferred promotion rollback,
  and mixed published hierarchies through alternating exact-type lookup, enumeration, persistent
  update/remove, semantic equality, bidirectional structural diff, and all four map-algebra paths.
- `PersistentHashMapTransientTests.cs` validates the shipped public map lifecycle: O(1)
  factory/adoption, comparer-preserving custom empties, exact clean source identity, complete point
  verbs, key/value representative retention, nulls, collision-heavy canonical publication,
  deterministic dictionary-model histories, base/later-generation isolation, callback/publication
  failure retryability, and consumption of every direct and interface alias.
- `PersistentHashMapTransientEnumeratorTests.cs` locks persistent trie-order parity for pair/key/value
  enumeration, allocation-free copied struct-enumerator independence, changed-edit fail-fast
  invalidation, no-op validity, post-publication disposal, default-enumerator behavior, collisions,
  and full-depth branches.
- `PersistentHashSetTransientTests.cs` validates the thin public `IReadOnlySet<T>` transient facade:
  comparer and clean wrapper identity, bool-returning mutable verbs, stored representatives, nulls,
  collisions, receiver-comparer relations, retained-base and generation isolation, deterministic
  `HashSet<T>` histories, map/set-wrapper publication failure atomicity, version-bound copy-safe
  enumeration, and complete alias consumption.
- `TransientApiShapeTests.cs` reflection-locks the intentionally small map/set surfaces and excludes
  reusable-builder, repeated-snapshot, range-edit, freeze, and mutable set-algebra additions.
- `PersistentHashMapEnumeratorTests.cs` covers allocation-free struct enumerators, copied enumerator independence,
  and key/value/pair enumeration.
- `PersistentHashSetEnumeratorTests.cs` covers the set wrapper's default, before-first, active, exhausted,
  copied, and interface/reset enumerator states.
- `PersistentHashMapCollisionTests.cs` covers equal-hash buckets, deep shared hash prefixes, collision splitting,
  hash-mismatch misses, and equivalent-key retention.
- `PersistentHamtStructureTests.cs` uses internal test access to verify CHAMP data/node maps, canonical
  independent-history topology, collapse behavior, structural equality, slot-aligned diff through
  leaf/collision/branch transitions, randomized invariants, eager validation, no-op root reuse, and sharing.
- `PersistentHashMapBulkBuilderTests.cs` verifies ordinary and combining insertion, exact combiner
  selection, first-key and equal-value retention, checked and callback failure atomicity, null/deep-
  prefix keys, collision and branch freezes, and immutable snapshot detachment while the bulk
  builder continues accepting entries.
- `PersistentHashMapPropertyTests.cs` uses CsCheck generated histories against dictionary-style model state,
  including retained snapshots and deliberately colliding hashes.
- `PersistentHashSetTests.cs` covers set membership, add/remove, try-add/try-remove, custom equality, set algebra,
  `IReadOnlySet<T>` behavior, generated set-algebra checks, and concurrent snapshot readers.
- `ConcurrentHashTrieTests.cs` covers linearizable mutation, generation stamps, stable O(1)
  snapshots/enumerators, deterministic root-RDCSS and GCAS-helping schedules, tomb contraction,
  collision-node re-splitting, stored-key retention, contended updates, concurrent snapshot
  consistency, lazy generation renewal, same-reference value no-ops without equality callbacks,
  and snapshot-to-CHAMP comparer/order/key-and-value-representative/null/collision/generation
  preservation, including mixed singleton/child runs and a frozen singleton tomb.
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
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore .\Durable7.sln --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\Durable7.sln -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    -- RunConfiguration.MaxCpuCount=1
```

Run a focused public-transient pass after the Release build:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter 'FullyQualifiedName~PersistentHashMapTransientTests|FullyQualifiedName~PersistentHashMapTransientEnumeratorTests|FullyQualifiedName~PersistentHashSetTransientTests|FullyQualifiedName~TransientApiShapeTests' `
    -- RunConfiguration.MaxCpuCount=1
```

Run the hash-bag suite together with its internal bulk-construction kernel after the Release build:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter 'FullyQualifiedName~PersistentHashBag|FullyQualifiedName~PersistentHashMapBulkBuilderTests' `
    -- RunConfiguration.MaxCpuCount=1
```

Run restore, build, and test sequentially. These commands disable NuGet restore parallelism, MSBuild
project parallelism/node reuse, compiler and .NET build servers, and test-host parallelism, bounding
both process count and memory use. The repository launcher still suppresses modal Windows
loader/crash reporting for its complete `dotnet` child-process tree; the test assembly independently
repeats the headless process configuration during module initialization, so direct test-runner and
Test Explorer execution is non-interactive after the assembly loads as well.

The T2 shipment checkpoint is **223 passed, 0 failed** for this complete project. The final focused
public transient/API filter passed 33 tests, and the selected T1 kernel suite remained 26 tests.
These counts record the shipment evidence and are not a ceiling for later test growth.

The persistent single-pass update tranche established the pre-bag complete-project checkpoint at
**244 passed, 0 failed**, including 19 focused factory-update tests. The current C# bimap tranche
passes **308 tests, 0 failed** for the complete HAMT project after adding the 16-test gate; the focused bag plus bulk-builder filter
passes **52 tests, 0 failed**. No benchmark is part of the bag acceptance gate; benchmark work
remains postponed to an isolated machine run.

Use the workspace [validation guide](../../docs/Hamt/validation.md) for restore/build split commands, warning policy,
and evidence expectations.
