# Frozen hash F0 packed-index signal decision

- Status: Evidence collection postponed; advance/defer outcome unselected
- Created (UTC): 2026-07-14T02:05:41Z
- Repository HEAD: 37f37d10906003759c9fe972971ecf6bbbd1b982
- Audience: Maintainers deciding whether the Axis 2 frozen hash track may enter F1
- Scope: C# benchmark-local linear-probe packed-index prototype and F0 gate

## Decision status

**No F0 performance decision has been made.** The minimally faithful linear-probe prototype and
its controls exist in the benchmark assembly, but no admissible isolated result yet covers lookup
mixes, enumeration, retained bytes per entry, construction, and calculated break-even reads. The
earlier interactive timing attempts were made under machine contention and are discarded. They do
not advance or defer Track F.

An implementation audit found that the multi-layout F1 harness was built before an explicit F0
advance/defer record was committed. This record restores the normative gate without inventing
evidence: F1 may remain compiled as a quarantined benchmark artifact, but its layout comparison
must not be interpreted and F2 remains unauthorized unless this document first records an F0
advance. The [F1 record](frozen-f1-layout-decision.md) owns only the later fixed-layout selection.

## Faithful F0 artifact

The benchmark-local `PackedFrozenMapPrototype<TKey, TValue>` has one fixed representation:

- source-map-order `(hash, key, value)` entries in one packed array;
- a separate zero-empty integer slot array using linear probing and at most 70% load;
- the exact source comparer object, with real hash and equality callbacks during construction and
  lookup;
- first-stored key representatives, including null in the repository-only semantic lane; and
- stable source-map enumeration independent of slot order.

It is not a provisional public type and has no update surface. Mutable `Dictionary`,
`ImmutableDictionary`, BCL `FrozenDictionary`, and shipped persistent CHAMP are controls; controls
that reject null are omitted from that lane instead of receiving a sentinel.

## Separate correctness gate

Run the untimed verifier before collecting any performance artifact:

```powershell
Set-Location C:\Users\vresh\.codex\worktrees\5cd5\DataStructures\src\CSharp\benchmarks\Durable7.FingerTree.Benchmarks
$driver = '.\bin\Release\net10.0\Durable7.FingerTree.Benchmarks.dll'
dotnet $driver --verify-axis2-frozen-layouts
```

This command does not invoke BenchmarkDotNet, start generated builds, measure elapsed time, or emit
retained-size evidence. `AXIS2_FROZEN_VERIFY_V2` validates all 39 locked uniform,
clustered-prefix, equal-full-hash, and null/case-insensitive cases across 0%, 50%, and 100% hits,
plus one reference-key/reference-value representative case. Fixture construction checks content,
exact enumeration, comparer identity, fixed load/slot diagnostics, and stored representatives for
the linear, Robin-Hood, and quadratic repository prototypes. Every candidate is also converted to
canonical CHAMP and recreated from that result, proving exact order, comparer, key representative,
and value representative preservation across the planned freeze -> persistent -> freeze sequence.
The verifier separately checks exact construction/hit/miss/conversion comparer callback counts,
failed-`TryGetKey` representative behavior, the invariant machine-readable retained-layout row,
break-even arithmetic, empty-map callback avoidance, and construction/lookup/conversion propagation
of throwing hash and equality callbacks.

Passing this command is correctness evidence only. It cannot close F0 or F1.

## Locked F0 evidence matrix

F0 interprets only the linear repository prototype, even though the shared fixture constructs the
quarantined F1 candidates for semantic parity. The deciding matrix is:

- uniform hashes at 1, 8, 32, 1,024, and 100,000 entries;
- clustered-prefix and equal-full-hash lanes at 8, 32, and 1,024 entries;
- null plus case-insensitive stored representatives at 8 and 32 entries;
- 0%, 50%, and 100% lookup hits;
- lookup, full enumeration, construction from the same retained source map, allocation, and
  retained arrays/bytes per entry; and
- calculated reads needed to amortize linear-layout construction against the already-existing
  persistent map.

The materiality threshold is the larger of the five-process measured noise floor and 10% for mean
latency, allocation, and retained bytes. A credible signal names a read-heavy regime where the
linear layout materially improves lookup and/or enumeration/memory without an unacceptable
load-bearing regression and has a realistic construction break-even. A win in one hit ratio alone
is insufficient.

## Exact isolated F0 commands

First use the environment, credential scrubbing, affinity, restore, and single-worker Release build
prefix in the [F1 record](frozen-f1-layout-decision.md#exact-single-worker-commands), stopping after
`$benchmarkDll` is assigned. Do not overlap any command. Then run only the persistent, linear, and
control methods:

```powershell
$uniformF0 = @(
    '*FrozenLookupBenchmarks.Persistent*',
    '*FrozenLookupBenchmarks.PackedPrototype*',
    '*FrozenLookupBenchmarks.Dictionary*',
    '*FrozenLookupBenchmarks.Immutable*',
    '*FrozenLookupBenchmarks.BclFrozen*')
$clusteredF0 = @(
    '*FrozenClusteredLookupBenchmarks.Persistent*',
    '*FrozenClusteredLookupBenchmarks.PackedPrototype*',
    '*FrozenClusteredLookupBenchmarks.Dictionary*',
    '*FrozenClusteredLookupBenchmarks.Immutable*',
    '*FrozenClusteredLookupBenchmarks.BclFrozen*')
$collisionF0 = @(
    '*FrozenCollisionLookupBenchmarks.Persistent*',
    '*FrozenCollisionLookupBenchmarks.PackedPrototype*',
    '*FrozenCollisionLookupBenchmarks.Dictionary*',
    '*FrozenCollisionLookupBenchmarks.Immutable*',
    '*FrozenCollisionLookupBenchmarks.BclFrozen*')
$nullF0 = @(
    '*FrozenNullLookupBenchmarks.Persistent*',
    '*FrozenNullLookupBenchmarks.PackedPrototype*')

dotnet $benchmarkDll --buildTimeout 600 --filter $uniformF0 `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f0\full-uniform'
dotnet $benchmarkDll --buildTimeout 600 --filter $clusteredF0 `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f0\full-clustered'
dotnet $benchmarkDll --buildTimeout 600 --filter $collisionF0 `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f0\full-collision'
dotnet $benchmarkDll --buildTimeout 600 --filter $nullF0 `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-f0\full-null'

1..5 | ForEach-Object {
    dotnet $benchmarkDll --buildTimeout 600 `
        --filter '*FrozenLookupBenchmarks.PersistentLookupMix*' `
        --artifacts ".\BenchmarkDotNet.Artifacts\axis2-f0\noise-persistent-$($_)"
}
```

The full processes emit `AXIS2_F1_RETAINED_V1` rows because F0 and F1 share diagnostics schema v1;
for F0, curate only the persistent and linear fields. Raw artifact directories remain git-ignored.
Record the executed commit, SDK/runtime, CPU and affinity, GC mode, exact commands, confidence
intervals, five-process noise calculation, linear bytes per entry, construction means, and
per-regime break-even reads here.

The benchmark classes also contain `ToPersistent` groups for all three candidates and the null
lane. Those layout-independent methods implement the broader Axis 2 conversion matrix, but they do
not contribute to the narrower F0 advance/defer tuple unless the deciding regime explicitly charges
conversion back to an editable persistent value.

## Curated evidence

Postponed. No admissible timing, allocation, retained-array, noise-floor, or break-even result has
been collected for F0.

## Exit outcomes

After every required F0 artifact exists, replace the pending status with exactly one result:

1. **Advance to F1**, naming the credible read-heavy regime and the complete materiality,
   retained-memory, construction, and break-even evidence; or
2. **Defer Track F**, because the faithful linear signal establishes no credible regime. In this
   outcome, do not run or interpret the F1 layout bake-off merely to rescue the track with a more
   favorable candidate.

Only an evidence-backed F0 advance authorizes the isolated F1 commands. Neither outcome by itself
authorizes a public frozen type.
