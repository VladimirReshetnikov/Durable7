# C# Ordered Collections Validation

- Status: Current validation guide
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Maintainers validating `Durable7.Ordered`
- Scope: Serialized restore, build, test, dependency, documentation, and benchmark-boundary guidance

Use this guide with the [API specification](api-specification.md) and [test-suite map](../../tests/Durable7.Ordered.Tests/README.md).

## Build Topology

`Durable7.sln` contains:

- `src/Durable7.Ordered/Durable7.Ordered.csproj`, referencing only the
  public FingerTree and HAMT projects; and
- `tests/Durable7.Ordered.Tests/Durable7.Ordered.Tests.csproj`, directly
  referencing only Ordered plus ordinary test packages.

`Directory.Build.props` targets .NET 10, enables nullable annotations and preview C#, generates XML
documentation, promotes missing-public-member and mismatched-parameter documentation warnings
(`CS1591` and `CS1573`) to errors, and disables parallel project builds, parallel restore, and compiler
sharing. `test.runsettings` limits the test host and xUnit to one worker.

## Serialized Commands

From `src/CSharp`, run each phase sequentially:

```powershell
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
.\test.ps1 `
    -Project .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    -Configuration Release -NoRestore -NoBuild
```

The launcher establishes inherited headless Windows failure handling before the SDK and testhost
start, supplies `test.runsettings`, and reasserts the one-worker build and test settings. For the final
managed-workspace gate, apply the same flags to `Durable7.sln`, then run every test project
through `./test.ps1`. Do not overlap restore, build, test, native toolchains, npm, Cargo, Cabal,
Kotlin, or benchmark processes.

## Recorded Shipment Evidence

The C# shipment gate completed with every phase serialized and build parallelism, node reuse, build
servers, and compiler sharing disabled:

- the focused Debug lane built with zero warnings or errors and passed 62 of 62 tests;
- the focused Release lane built with zero warnings or errors and passed 62 of 62 tests;
- the complete C# Release solution built with zero warnings or errors; and
- the complete test run passed 1,355 of 1,355 tests: Numerics 319, HAMT 292, FingerTree 630,
  Ordered 62, and Tungsten 52.

The later derived-structure integration gate on 2026-07-17 UTC adds seven focused ordered-map and
seven focused ordered-multimap tests. The complete Ordered project passes 76/76 tests in both full
serialized Debug and Release solution gates. Both builds finish with zero warnings and zero errors,
and both full C# gates pass
1,503/1,503 tests: Numerics 319, HAMT 347, FingerTree 709, Ordered 76, and Tungsten 52. This is the
complete C# tranche evidence; the final cross-language shipment record will supersede it after all
ports are complete.

The solution build compiled the benchmark project as an ordinary project dependency, but no
benchmark was executed and no performance evidence was recorded.

## Required Coverage

The focused ordered-multimap lane is:

```powershell
.\test.ps1 `
    -Project .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    -Filter FullyQualifiedName~PersistentOrderedMultimapTests
```

It currently passes 7/7 tests covering grouped order, policies, representatives, identity,
contraction, retained branches, and recursive invariants.

The Ordered suite independently covers:

- every public member and positional boundary;
- ordered-map independent key/value policies, first-key/last-value construction, in-place value
  replacement, strict insertion, explicit movement, ranges, reversal, relabeling, and retained branches;
- default, custom, constant-hash, case-folding, and comparer-defined-null policies;
- first-representative retention with equal but object-distinct values;
- duplicate addition versus explicit movement and final-index movement semantics;
- absent removal/movement, empty endpoints, eager positional validation, and identity no-ops;
- range extraction, smaller-side index reconciliation, reverse, stable sort, and unchanged-sort identity;
- repeated same-position histories that cross private label gaps, plus a deterministic move-triggered
  relabel and its rebuild-failure atomicity;
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

- an Ordered production or test project reference to `Durable7.Tungsten`;
- a compiled Ordered assembly reference to Tungsten;
- a Tungsten namespace/type use, nested or linked Tungsten source file, unapproved package, analyzer,
  additional-file, project-as-analyzer, import, target, task, SDK, or other manifest generator route;
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
