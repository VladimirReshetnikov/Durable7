# C# HAMT Validation

- Status: Current validation guide
- Created (UTC): 2026-07-02T20:33:30Z
- Repository HEAD: 7c02f68ae23244d48871317ea90d26c0defd2394
- Audience: Maintainers validating the C# HAMT workspace
- Scope: Local restore, build, test, warning-policy, and test-coverage guidance for `src/CSharp/src/Durable7.Hamt`

Use this guide when changing the C# HAMT library, tests, examples, or documentation that makes build,
test, API, or complexity claims. For semantic contracts and usage examples, pair it with the
[API specification](api-specification.md) and [usage guide](usage.md).

## Build Model

`Durable7.sln` contains:

- `src/Durable7.Hamt/Durable7.Hamt.csproj`, the public library containing
  the CHAMP map/set/bag/bimap/multimap/relation, Ctrie, Patricia, and Merkle families.
- `tests/Durable7.Hamt.Tests/Durable7.Hamt.Tests.csproj`, the xUnit/CsCheck
  test project.

`Directory.Build.props` applies the workspace defaults:

- Target framework: `net10.0`.
- Language version: C# `preview`.
- Nullable annotations and implicit usings enabled.
- XML documentation generation enabled.
- Public XML documentation warnings `CS1591` and `CS1573` promoted to errors.

The test project references the library project and uses `xunit`, `xunit.runner.visualstudio`,
`Microsoft.NET.Test.Sdk`, and `CsCheck`.

## Commands

From `src/CSharp`:

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

Run these commands sequentially; do not overlap restore, build, test, or BenchmarkDotNet processes.
NuGet parallel restore, MSBuild project parallelism and node reuse, compiler-server sharing, .NET
build servers, and test-host parallelism are all disabled to keep the process count and memory bound.
The repository `.\test.ps1` launcher remains useful for unattended full-workspace validation, but
the explicit commands above are the reproducible single-node HAMT gate and give each phase a clear
failure boundary.

No benchmark is an exit criterion for `PersistentHashBag<T>` or `PersistentBiMap<TKey, TValue>`. Validate their semantic, invariant,
operation-count, allocation-shape, and failure-atomicity contracts through the serialized build and
test gates; postpone any performance measurements until the machine can run them in isolation.

For a focused public-transient pass after the Release build, substitute this final command:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter 'FullyQualifiedName~PersistentHashMapTransientTests|FullyQualifiedName~PersistentHashMapTransientEnumeratorTests|FullyQualifiedName~PersistentHashSetTransientTests|FullyQualifiedName~TransientApiShapeTests' `
    -- RunConfiguration.MaxCpuCount=1
```

For a focused hash-bag and internal construction-kernel pass after the Release build, use:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter 'FullyQualifiedName~PersistentHashBag|FullyQualifiedName~PersistentHashMapBulkBuilderTests' `
    -- RunConfiguration.MaxCpuCount=1
```

For a focused set-valued hash-multimap pass, use:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter FullyQualifiedName~PersistentHashMultimapTests `
    -- RunConfiguration.MaxCpuCount=1
```

For the mutually inverse relation contract, use:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter FullyQualifiedName~PersistentRelationTests `
    -- RunConfiguration.MaxCpuCount=1
```

For the strict patch, directed graph, and indexed-map contracts, use:

```powershell
dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter 'FullyQualifiedName~PersistentMapPatchTests|FullyQualifiedName~PersistentDirectedGraphTests|FullyQualifiedName~PersistentIndexedMapTests' `
    -- RunConfiguration.MaxCpuCount=1
```

The focused lane currently passes 23/23 tests: 8 patch, 7 graph, and 8 indexed-map tests.

## Test Coverage

### Current Derived-Structure Integration Evidence

On 2026-07-29 UTC, the complete HAMT project passed 366/366 tests and the serialized full C# solution
passed 1,240/1,240 tests in both Debug and Release. The focused experimental
`PersistentAncestralConnectionForestTests` lane passed 12/12 tests. The full breakdown is 366 HAMT +
794 FingerTree + 80 Ordered. Solution builds completed in both configurations; the rebased base
currently emits pre-existing XML-documentation warnings. Benchmarks were not run.

For historical comparison, the pre-experiment checkpoint contributed 354 HAMT tests to a
1,158-test full-solution gate. The consolidated totals above supersede that snapshot.

Run the forest lane with:

```powershell
.\test.ps1 -Project .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj `
    -Filter FullyQualifiedName~PersistentAncestralConnectionForestTests
```

`PersistentBiMapTests` provides the bimap shipment gate: strict two-domain uniqueness, independent
policy retention, configured-value-comparer replacement, first representatives, inverse identity,
symmetric removal, nullable values, failure atomicity, retained 1,000-command histories, concurrent
readers, enumeration, public API shape, and the bidirectional invariant. Run it alone with:

```powershell
dotnet test tests/Durable7.Hamt.Tests/Durable7.Hamt.Tests.csproj `
    --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    --filter FullyQualifiedName~PersistentBiMapTests
```

`tests/Durable7.Hamt.Tests/` covers the xUnit/CsCheck suite. See the
[tests README](../../tests/Durable7.Hamt.Tests/README.md) for source-file grouping and filter examples.

The suite covers:

- strict presence-safe map patches, conflict atomicity, null/absence, inversion, composition,
  policy compatibility, representatives, and invariant validation;
- explicit-vertex directed graphs, bidirectional adjacency, self-loops, incident removal, cached
  reversal, representatives, retained branches, and coupled-index invariants;
- selector-maintained indexed maps, nonunique group movement/contraction, exact selector invocation,
  selector failure atomicity, policies, representatives, retained branches, and index agreement;

- map construction, lookup, replacement, removal, no-op behavior, and enumeration;
- persistent single-pass `GetOrAdd`/`AddOrUpdate` factory selection, exact hash/equality callback
  counts, leaf/collision/bitmap and published-transient node paths, representative/null behavior,
  allocation-free no-ops, failure atomicity, and generated-history canonicality;
- set construction, membership, add/remove, set algebra, and `IReadOnlySet<T>` behavior;
- hash-bag construction, lookup, explicit distinct/expanded counts, checked copy boundaries,
  saturated removal, comparer-preserving empties, first representatives, null/collision policies,
  no-op identity, retained versions, and callback/enumerator failure atomicity;
- hash-bag maximum/minimum/subtractive/additive algebra, receiver-comparer normalization, checked
  collapsed classes, eager normalization failures, representative precedence, and unchanged-result
  identity;
- hash-bag expanded/distinct/entry enumeration order, allocation-free copied concrete enumerators,
  default/before-first/active/exhausted/interface/reset states, `Array.MaxLength` materialization
  guard, distinct debugger projection, and exact API shape excluding `Count` and
  `IReadOnlyCollection<T>`;
- hash-multimap construction, independent comparer retention, first representatives in both
  domains, distinct key/pair counts, duplicate identity, comparer-preserving absent groups,
  last-value group contraction, whole-group removal, retained histories, and recursive invariants;
- relation many-to-many adjacency, independent policy retention, global representatives, pair and
  whole-domain removal, comparer-preserving absent adjacency sets, cached inverse identity,
  retained branching histories, and mutually inverse index invariants;
- comparer-aware linear-model hash-bag histories with retained snapshots and invariant validation
  after commands under ordinary, nullable, and collision-heavy policies;
- Axis 2 map/set contract oracles for comparer identity, stored representatives, nullable keys/items,
  collisions, stable enumeration, no-op identity, retained versions, and callback-exception atomicity;
- benchmark-only CHAMP diagnostics that pin root/path sharing, retained size, exact ordinary field
  layout, and the b590 source fingerprints for ordinary nodes and the monomorphic lookup loop;
- the selected 26-test private T1 direct-separate kernel, including first-edit deferral, reusable-path
  promotion, independent array ownership, production/diagnostic parity, O(1) adoption/publication,
  recursive canonicality, base/version isolation, consumed sessions, and deterministic callback,
  allocation, promotion, and publication failure rollback;
- the shipped public map transient: exact reflection-locked surface, O(1) factory/adoption, clean
  source identity, default/custom comparer empties, point verbs, nullable keys/values, first key/value
  representative retention, collisions and deep prefixes, deterministic model histories, base and
  later-generation isolation, public callback/publication failure atomicity, and complete direct,
  interface, key-view, value-view, enumerator, copied-enumerator, and second-publication consumption;
- the shipped public set transient: exact `IReadOnlySet<T>` facade shape, comparer and clean set
  identity, bool-returning mutable verbs, representative/null/collision behavior, receiver-comparer
  relations, deterministic `HashSet<T>` model histories, map/set wrapper publication failpoints,
  base isolation, version-bound copy-safe enumeration, and complete post-publication alias
  invalidation;
- transient enumeration parity: concrete/interface/key/value traversal follows persistent trie order,
  changed edits fail fast, logical no-ops preserve captured aliases, and successful publication
  changes failure precedence to `ObjectDisposedException`;
- comparer preservation, first equivalent key/item retention, and custom equality;
- equal-hash collision buckets, deep shared hash prefixes, and collision splitting;
- allocation-free copy-safe enumerators;
- CHAMP data-map/node-map shape, canonical independent-history topology, structural equality,
  slot-aligned semantic diff across every node-shape transition, eager validation, key-representative
  semantics, randomized invariant checking, and reference-pruning bounds through internal test access;
- bulk-builder semantics, including collision/deep-prefix construction, checked combining insertion
  with first-key/equal-value retention and exact callback selection, callback failure atomicity, and
  detachment of already-frozen snapshots from later builder mutations;
- generated map histories checked against model dictionaries with retained snapshots;
- generated set behavior checked against model set semantics.
- concurrent hash-trie root/main RDCSS and node GCAS helping, deterministic snapshot/write race
  schedules, tomb contraction, collision re-splitting, stored-key retention, stable snapshots,
  same-reference value no-ops without equality callbacks, snapshot-to-CHAMP comparer/order/key-and-
  value-representative/null/collision/generation preservation, mixed inline/child canonical order,
  frozen singleton-tomb promotion during enumeration, contended publication/accumulation,
  and 400 exhaustively serialized short-history linearizability checks across ordinary,
  shared-prefix, and equal-hash policies.
- 32/64-bit Patricia signed-boundary ordering, 35,000 randomized model operations with retained
  snapshots, combining overloads, set relations, and randomized prefix-aware structural algebra.
- Merkle `mst-sha256-b16-v2` codec, digest, empty-manifest, and complete block golden vectors;
  strict bidirectional codec round trips; and malformed, trailing, non-canonical, unversioned-id,
  ill-formed-Unicode, domain, and digest rejection;
- wide-block Merkle independent-history convergence under ordinary and adversarial layer schedules,
  randomized model histories with retained snapshots and ordered ranges, exact-root restoration,
  off-path block sharing, structure statistics, no-op identity, and shape-changing typed diff in both
  directions;
- Merkle save/load and complete/partial pack round trips; missing, tampered, malformed,
  non-canonical, foreign-domain, resource-budget, and destination-conflict failures; and commit
  atomicity for preflight/verification failures;
- complete and partial-closure block synchronization, canonical membership/non-membership/inclusive-
  range proofs, pre-decode proof-query byte limits, exact query-plus-block accounting,
  tampered-query/block and extra-step rejection, and typed three-way merge including disjoint edits,
  identical edits, unresolved/resolved conflicts, deletion, and present-null state.

For a new public operation, add both direct examples and model/property coverage when there is a natural
BCL or simple in-memory oracle.

The T2 shipment checkpoint is **223 passed, 0 failed** for the complete C# HAMT project under the
single-worker command above. The final focused public transient/API filter passed 33 tests, and the
existing selected-kernel suite remained 26 tests. Treat these as a named checkpoint rather than a
permanent expected-count assertion; new tests should increase the total.

The persistent single-pass update tranche established the pre-bag complete-project checkpoint at
**244 passed, 0 failed**, including 19 focused `PersistentHashMapSinglePassUpdateTests`. The current
C# bimap tranche passes **308 tests, 0 failed** for the complete HAMT project in both Debug and
Release; the focused bimap gate passes 16/16. The earlier focused bag
plus bulk-builder filter passes **52 tests, 0 failed**. The earlier full C# workspace gate also
passed 319 Numerics, 630 FingerTree, and 52 Tungsten tests under the same single-worker policy; rerun
that complete workspace gate after integration changes rather than treating the earlier totals as
current bag evidence.

## Public Transient Benchmark Validation

`TransientLifecycleBenchmarks.SeparateNodeKernelHistory` retains its historical name so the locked
T1 filters and archived artifacts remain reproducible, but the timed method now calls the public
`map.ToTransient()` / `Persist()` path and is categorized `Axis2T2`, `EditPublication`, and
`PublicTransient`. Setup separately replays the diagnostics-enabled engine to validate semantic,
canonical, and retained-layout parity and to emit structural counters; diagnostic work is outside
the timed method.

The decisive public confirmation used affinity 1 and the locked
`N100000_E512 / ClusteredPrefix / End` tuple. Across 100 samples, the public path measured
236.700 us mean, 220.608 us median, a 222.285–251.115 us 99.9% confidence interval, and 253.67 KB
allocated. The entire interval is below the locked 283.132 us cutoff: the mean is 46.60% below the
443.293 us persistent-control center, and the upper endpoint is 43.35% below it. Counters retain zero
adoption/publication node visits; the published graph is 4,314,808 bytes versus 4,306,320 ordinary
bytes (+8,488, +0.1971%). This is full public-path performance evidence; the complete command and
artifact contract are in the [T2 decision](transient-t2-decision.md).

An earlier pinned one-case BenchmarkDotNet Dry-job run completed the same public
semantic/canonical setup and zero-visit checks. It remains a harness smoke only: a Dry job supplies
no statistical performance evidence and is not combined with either the affinity-pinned T1 matrix
or the 100-sample public result.

## Evidence To Record

When reporting validation, include the workspace and exact command, for example:

```text
src/CSharp> dotnet test .\tests\Durable7.Hamt.Tests\Durable7.Hamt.Tests.csproj -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false -p:BuildInParallel=false -p:UseSharedCompilation=false -- RunConfiguration.MaxCpuCount=1
```

If a docs-only change only updates links or wording and does not alter commands, API claims, or XML
documentation behavior, the repository-wide Markdown checks from
[`docs/guides/build-and-validation.md`](../../../../docs/guides/build-and-validation.md#documentation-checks)
are usually the relevant evidence.
