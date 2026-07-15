# C# Ordered Collections Tests

- Status: Current test-suite map
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Maintainers validating `Tools.DataStructures.Ordered`
- Scope: xUnit/CsCheck coverage for `PersistentOrderedSet<T>`

The test project directly references only `Tools.DataStructures.Ordered`; HAMT and FingerTree arrive
through the production project's public dependency graph. The suite is independent of Tungsten code,
tests, internals, and runtime behavior.

| File | Coverage |
| --- | --- |
| `PersistentOrderedSetCoreTests.cs` | Empty/comparer identity, construction collapse, comparer-defined null, representative lookup, addition/removal, retained versions, clear, and owned exceptions |
| `PersistentOrderedSetMovementRangeAndSortTests.cs` | Exhaustive small final-index movement, endpoint moves, repeated same-point relabel histories and branches, every small range boundary, reverse, stable one-shot sort, and result behavior after sort |
| `PersistentOrderedSetAlgebraAndRelationTests.cs` | Receiver order/representatives, same-type and enumerable overloads, mismatched comparers, first argument representative, eager normalization/failures, no-op identity, null, exhaustive small relation truth tables, and null operands |
| `PersistentOrderedSetEnumeratorTests.cs` | Default/current/reset/dispose behavior, ordered representatives, fail-fast divergent copies, independent enumerators, exhausted copies, interface paths, and version binding |
| `PersistentOrderedSetFailureAndInvariantTests.cs` | Validation-before-callback precedence, hash/equality/rebuild/sort failure atomicity, extreme comparer results, callback-bypassing identity paths, and deliberate invariant corruption detection |
| `PersistentOrderedSetPropertyTests.cs` | Deterministic generated branching command histories against an independent comparer-aware ordered-list model, validating retained branches and invariants after every command |
| `PersistentOrderedSetConcurrencyTests.cs` | Concurrent readers over current and retained snapshots, including enumeration, membership, positional lookup, and branch publication |
| `PersistentOrderedSetApiShapeTests.cs` | Exact type/property/method/enumerator signatures, absence of extra sorted-set vocabulary, and bounded ordered debugger projection |
| `OrderedDependencyBoundaryTests.cs` | Project references, friend grants, compiled references, public-type leakage, linked sources, namespace uses, and live-oracle/source scans |
| `OrderedSetTestSupport.cs` | Independent model, representative/comparer fixtures, throwing policies/enumerables, and shared assertions |

## Serialized Commands

From `src/CSharp`, run phases sequentially:

```powershell
dotnet restore .\tests\Tools.DataStructures.Ordered.Tests\Tools.DataStructures.Ordered.Tests.csproj `
    --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\tests\Tools.DataStructures.Ordered.Tests\Tools.DataStructures.Ordered.Tests.csproj `
    --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet test .\tests\Tools.DataStructures.Ordered.Tests\Tools.DataStructures.Ordered.Tests.csproj `
    --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    -- RunConfiguration.MaxCpuCount=1
```

The first serialized Debug checkpoint discovered and passed 62 tests with zero build warnings. Run
the corresponding Release lane and the complete managed solution before final C# shipment evidence
is recorded.

Benchmarks are deliberately outside this suite and remain postponed to an isolated machine run.
See the [Ordered validation guide](../../docs/Ordered/validation.md) for the full gate and dependency
boundary.
