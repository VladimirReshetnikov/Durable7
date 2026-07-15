# C# Ordered Collections Validation

- Status: Current validation guide
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Maintainers validating `Tools.DataStructures.Ordered`
- Scope: Serialized restore, build, test, dependency, documentation, and benchmark-boundary guidance

Use this guide with the [API specification](api-specification.md) and [test-suite map](../../tests/Tools.DataStructures.Ordered.Tests/README.md).

## Build Topology

`DataStructures.sln` contains:

- `src/Tools.DataStructures.Ordered/Tools.DataStructures.Ordered.csproj`, referencing only the
  public FingerTree and HAMT projects; and
- `tests/Tools.DataStructures.Ordered.Tests/Tools.DataStructures.Ordered.Tests.csproj`, directly
  referencing only Ordered plus ordinary test packages.

`Directory.Build.props` targets .NET 10, enables nullable annotations and preview C#, generates XML
documentation, promotes missing/malformed public XML documentation warnings to errors, and disables
parallel project builds, parallel restore, and compiler sharing. `test.runsettings` limits the test
host and xUnit to one worker.

## Serialized Commands

From `src/CSharp`, run each phase sequentially:

```powershell
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore .\tests\Tools.DataStructures.Ordered.Tests\Tools.DataStructures.Ordered.Tests.csproj `
    --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\tests\Tools.DataStructures.Ordered.Tests\Tools.DataStructures.Ordered.Tests.csproj `
    -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet test .\tests\Tools.DataStructures.Ordered.Tests\Tools.DataStructures.Ordered.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    -- RunConfiguration.MaxCpuCount=1
```

For the final managed-workspace gate, apply the same flags to `DataStructures.sln`, then run every
test project through `./test.ps1` or equivalent serialized `dotnet test` invocations. Do not overlap
restore, build, test, native toolchains, npm, Cargo, Cabal, Kotlin, or benchmark processes.

## Required Coverage

The Ordered suite independently covers:

- every public member and positional boundary;
- default, custom, constant-hash, case-folding, and comparer-defined-null policies;
- first-representative retention with equal but object-distinct values;
- duplicate addition versus explicit movement and final-index movement semantics;
- absent removal/movement, empty endpoints, eager positional validation, and identity no-ops;
- range extraction, smaller-side index reconciliation, reverse, stable sort, and unchanged-sort identity;
- repeated same-position histories that cross private label gaps and exercise relabel rebuilds;
- same-type and enumerable algebra under equal, reference-different, and semantically different comparer objects;
- receiver ordering, receiver representatives, first normalized argument representatives, and all six relations;
- eager argument normalization, throwing enumerables/comparers, and source failure atomicity;
- deterministic generated command histories with retained branches and a comparer-aware list model;
- direct Ordered-owned dual-index invariant validation after every history step;
- ordered enumeration, default/copy/reset behavior, independently obtained concurrent enumerators,
  and retained-version concurrent readers;
- exact public API/reflection shape and debugger projection; and
- the project/dependency boundary described below.

## Dependency Boundary Gate

Validation rejects:

- an Ordered production or test project reference to `Tools.DataStructures.Tungsten`;
- a compiled Ordered assembly reference to Tungsten;
- a Tungsten namespace/type use, linked Tungsten source file, or source-generator route;
- a new Ordered friend grant in HAMT or FingerTree; and
- a live `PersistentAssociation` test oracle.

The test project may use `InternalsVisibleTo` from Ordered solely for Ordered-owned invariant
diagnostics. It receives no foundation internals. Provenance text in proposal or documentation is
non-normative and allowed.

## Documentation Checks

After source/docs changes, run the repository-owned stale-path scan and Markdown link checker from
[build and validation](../../../../docs/guides/build-and-validation.md#documentation-checks), followed
by:

```powershell
git diff --check
```

## Benchmark Boundary

No benchmark is an Ordered shipment gate. The project makes no claim that this representation beats
another insertion-ordered set. Do not add or run contention-tainted benchmarks during routine
validation. Any future measurement must run in isolation and may inform optimization work without
changing the semantic, invariant, persistence, or asymptotic contracts above.
