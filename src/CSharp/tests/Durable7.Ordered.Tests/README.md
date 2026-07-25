# C# Ordered Collections Tests

- Status: Current test-suite map
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Maintainers validating `Durable7.Ordered`
- Scope: xUnit/CsCheck coverage for the persistent ordered map, set, and multimap

The test project directly references only `Durable7.Ordered`; HAMT and FingerTree arrive
through the production project's public dependency graph. The suite is independent of Tungsten code,
tests, internals, and runtime behavior.

| File | Coverage |
| --- | --- |
| `PersistentOrderedMultimapTests.cs` | Independent policies, grouped key/value order, first representatives, duplicate identity, group contraction and reappend, exact counts, range construction, retained branches, and recursive dual-index invariants |
| `PersistentOrderedMapTests.cs` | Independent policies, first-key/last-value construction, strict addition, in-place replacement, explicit movement, removal, ranges, reversal, repeated relabeling, branching histories, and dual-index invariants |
| `PersistentOrderedCursorTests.cs` | Set/map explicit-order gap cursors, flattened grouped-pair multimap cursors, persistent adjacent edits, representative retention, duplicate no-ops, and retained source branches |
| `PersistentOrderedSetCoreTests.cs` | Empty/comparer identity, construction collapse, comparer-defined null, representative lookup, addition/removal, retained versions, clear, and owned exceptions |
| `PersistentOrderedSetMovementRangeAndSortTests.cs` | Exhaustive small final-index movement, endpoint moves, insertion- and move-triggered relabel histories with rebuild-failure atomicity, every small range boundary, reverse, default and custom stable one-shot sort, and result behavior after sort |
| `PersistentOrderedSetAlgebraAndRelationTests.cs` | Receiver order/representatives, same-type and enumerable overloads, shared, reference-distinct equivalent, and semantically different comparers, first argument representative, eager normalization/failures, no-op identity, null, exhaustive small relation truth tables, and null operands |
| `PersistentOrderedSetEnumeratorTests.cs` | Default/current/reset/dispose behavior, ordered representatives, fail-fast divergent copies, independent enumerators, exhausted copies, interface paths, and version binding |
| `PersistentOrderedSetFailureAndInvariantTests.cs` | Validation-before-callback precedence, hash/equality/rebuild/sort failure atomicity, extreme comparer results, callback-bypassing identity paths, and deliberate invariant corruption detection |
| `PersistentOrderedSetPropertyTests.cs` | Deterministic generated branching command histories against an independent comparer-aware ordered-list model, validating retained branches and invariants after every command |
| `PersistentOrderedSetConcurrencyTests.cs` | Concurrent readers over current and retained snapshots, including enumeration, membership, positional lookup, and branch publication |
| `PersistentOrderedSetApiShapeTests.cs` | Exact type/property/method/enumerator signatures, absence of extra sorted-set vocabulary, and bounded ordered debugger projection |
| `OrderedDependencyBoundaryTests.cs` | Allowlisted packages/project references, generator-route rejection, friend grants, compiled references, public-type leakage, recursive and linked source scans, namespace uses, and live-oracle guards |
| `OrderedSetTestSupport.cs` | Independent model, representative/comparer fixtures, throwing policies/enumerables, and shared assertions |

## Serialized Commands

From `src/CSharp`, run phases sequentially:

```powershell
$configuration = 'Debug' # Repeat the build/test phases with 'Release'.

dotnet restore .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    -c $configuration --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
.\test.ps1 `
    -Project .\tests\Durable7.Ordered.Tests\Durable7.Ordered.Tests.csproj `
    -Configuration $configuration -NoRestore -NoBuild
```

The launcher enables inherited headless Windows failure handling before the SDK/testhost starts,
uses the repository runsettings, and reasserts the single-worker build and test policies.

The ordered-set shipment's serialized Debug and Release lanes each discovered and passed 62 of 62 tests with zero build
warnings or errors. The complete serialized C# Release solution built with zero warnings or errors
and passed all 1,355 tests: Numerics 319, HAMT 292, FingerTree 630, Ordered 62, and Tungsten 52.

The subsequent ordered-map and ordered-multimap development lanes pass 81 of 81 tests in Debug and
Release. The complete serialized C# gates pass 1,530/1,530 tests after zero-warning, zero-error
builds.

Benchmarks are deliberately outside this suite and remain postponed to an isolated machine run.
See the [Ordered validation guide](../../docs/Ordered/validation.md) for the full gate and dependency
boundary.
