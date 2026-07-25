# CHAMP transient T2 shipment decision

- Status: Shipped — public one-way C# map/set transients
- Created (UTC): 2026-07-13T14:32:45Z
- Repository HEAD: 1bdb739da3e5fe8e135afc07539a60fdc11981b5
- Implementation checkpoints: 5cfe8c2044f442f24b7723099761169bf1c7b633, 6c3be9e5cb072069e6fa604c4bcb05c2b3146de1, b63ba7e69738bd6a853c0c102c1cfedeb28f2c69, 4d582a7f421ca577ece00324c70e526132e50413, 1bdb739da3e5fe8e135afc07539a60fdc11981b5
- Audience: Maintainers deciding whether the Axis 2 CHAMP transient satisfies its public shipment gate
- Scope: C# T2 public API, lifecycle, failure, validation, benchmark, retained-memory, and language-posture decision

## Decision status

**Ship `PersistentHashMap<TKey, TValue>.Transient` and
`PersistentHashSet<T>.Transient` as the one-way C# CHAMP editing lifecycle.** T2 closes its required
public API/XML, consumed-alias, failure-atomicity, comparer/representative/no-op, enumeration,
retained-memory, and explicit-decision gates.

The decision rests on both semantic and performance evidence:

- the public map/set surface is reflection-locked and fully XML-documented;
- successful `Persist()` consumes every direct, interface, view, relation, enumerator, and copied-
  enumerator alias, while failed publication leaves the session active and retryable;
- the complete C# HAMT project passes 223/223 tests under a single-worker build/test protocol;
- the selected representation already cleared the locked T1 End and Every64 gates after charging
  complete allocation and actual retained graph bytes; and
- a subsequent 100-sample, affinity-pinned full run through the **public** `ToTransient()` /
  `Persist()` path independently clears the same locked latency cutoff, with zero adoption and
  publication node visits and the same selected retained layout.

This is a C#-only shipment. It does not add sibling-language transients, a reusable builder, a
repeated-snapshot lifecycle, a frozen hash collection, or a Ctrie-to-frozen conversion.

## Shipped public surface

The map surface is intentionally limited to point edits and one terminal publication:

```csharp
public sealed partial class PersistentHashMap<TKey, TValue>
{
    public static Transient CreateTransient(
        IEqualityComparer<TKey>? comparer = null);

    public Transient ToTransient();

    public sealed class Transient : IReadOnlyDictionary<TKey, TValue>
    {
        public int Count { get; }
        public IEqualityComparer<TKey> Comparer { get; }
        public TValue this[TKey key] { get; }
        public IEnumerable<TKey> Keys { get; }
        public IEnumerable<TValue> Values { get; }

        public bool ContainsKey(TKey key);
        public bool TryGetValue(TKey key, out TValue value);
        public bool TryGetKey(TKey equalKey, out TKey actualKey);
        public void Add(TKey key, TValue value);
        public bool TryAdd(TKey key, TValue value);
        public void SetItem(TKey key, TValue value);
        public bool Remove(TKey key);
        public void Clear();
        public Enumerator GetEnumerator();
        public PersistentHashMap<TKey, TValue> Persist();
    }
}
```

The set exposes the same factory/adoption/publication lifecycle through a sealed
`Transient : IReadOnlySet<T>`. Its public members are `Count`, `Comparer`, `Contains`,
`TryGetValue`, bool-returning `Add` and `Remove`, `Clear`, `IsSubsetOf`, `IsProperSubsetOf`,
`IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, `SetEquals`, `GetEnumerator`, and `Persist`.

Neither transient has a public constructor. Reflection tests exclude range edits, reusable
`ToImmutable`/builder vocabulary, repeated snapshots, freeze operations, and mutable set-algebra
verbs. Those are different lifecycle models, not aliases for this terminal session.

## Lifecycle and semantic contract

`CreateTransient(comparer)` creates an active comparer-preserving empty session. `ToTransient()`
adopts a persistent source/root in O(1), without walking or copying the trie. The transient is
unsynchronized and has one logical owner. Sequential ownership transfer between threads requires
caller-provided synchronization; concurrent access is unsupported. The retained source and every
previously published map/set remain immutable and concurrently readable.

The first successful `Persist()` prepares every potentially throwing wrapper allocation, then
performs only non-throwing publication assignments, increments the version, and makes the session
inactive. After that transition, every property read, lookup, mutation, set relation, enumeration
request, or second publication through any alias throws `ObjectDisposedException`. Views and
enumerators obtained beforehand capture the session/version and throw the same exception; they
cannot drain an alias of the newly persistent graph.

A successful changed edit increments one version epoch and invalidates previously captured map
pair/key/value or set enumerators with `InvalidOperationException`. Logical no-ops do not increment
the version, allocate/copy an edit path, or invalidate enumeration. They include duplicate
`TryAdd`/set `Add`, absent removal, clearing an empty session, and a `SetItem` value equal under
`EqualityComparer<TValue>.Default`.

A clean `source.ToTransient().Persist()` returns the exact `source` object, including after any
number of logical no-ops. A clean factory session returns its comparer-preserving empty; the default
policy uses the canonical `Empty`. Every transition preserves comparer identity, comparer-defined
null behavior, the first equivalent key/item representative, equal-value object retention, stable
trie order, equal-hash collision-bucket behavior, recursive counts, and canonical branch
contraction. `Clear()` retains the comparer.

Every point edit has the strong exception guarantee. Potentially throwing hash, equality, value-
equality, and allocation work completes before the operation's first in-place write; commit consists
only of non-throwing field/reference assignments. Deterministic failure tests prove that a failed
edit or publication preserves content, root identity, token/plan state, version, counters, source or
deferred-wrapper identity, and captured-enumerator validity. Set publication prepares both its map
and set wrappers before consuming either layer, so a set-wrapper allocation failure is likewise
retryable.

## Selected public representation

T2 promotes the selected T1 direct separate-node engine into the public nested map type itself. It
does **not** place a facade around that engine, so `ToTransient()` allocates exactly the public map
session users receive and no additional map-transient wrapper. The set necessarily adds a thin
`IReadOnlySet<T>` facade over its map session.

Ordinary `LeafNode`, `CollisionNode`, and `BitmapIndexedNode` layouts retain no owner field, ownership
flag, or common mutable base. The first changed edit uses the ordinary persistent operation and
caches that exact wrapper. A later changed edit promotes only reusable branch/collision paths into
`SeparateTransientBranchNode` / `SeparateTransientCollisionNode`; a two-bit mask tracks data-array
and child-array ownership independently. Tokens and bounded prepare/commit plans remain lazy.

Publication is O(1) and visits no nodes. It does not disguise an O(n) tag-clearing pass: separate
nodes retain their sealed token references and edit metadata in the persistent graph. A later
transient has a different token and copies before writing those paths.

## Single-worker evidence protocol

Restore, build, test, generated BenchmarkDotNet build, and every benchmark process ran sequentially.
NuGet parallel restore, MSBuild project parallelism and node reuse, compiler-server sharing, .NET
build servers, and test-host parallelism were disabled. Benchmark processes were pinned to logical
processor 0 (`--affinity 1`) on the heterogeneous host, matching the locked T1 protocol.

The public-path full run used the prebuilt Release driver after scrubbing credential-like environment
variables:

```powershell
$repo = (git rev-parse --show-toplevel).Trim()
Set-Location (Join-Path $repo 'src\CSharp')
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore Durable7.sln --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build benchmarks\Durable7.FingerTree.Benchmarks\Durable7.FingerTree.Benchmarks.csproj `
    -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false

Get-ChildItem Env: | Where-Object {
    $_.Name -match '(?i)(TOKEN|KEY|SECRET|PASSWORD|CREDENTIAL|CONNECTION|COOKIE|AUTH|IGCCSVC)'
} | Remove-Item -ErrorAction SilentlyContinue

Set-Location benchmarks\Durable7.FingerTree.Benchmarks
$driver = '.\bin\Release\net10.0\Durable7.FingerTree.Benchmarks.dll'
$publicEnd = '*TransientLifecycleBenchmarks.SeparateNodeKernelHistory*History: ClusteredPrefix*PublicationCadence: End*Workload: N100000_E512)'
dotnet $driver --filter $publicEnd --affinity 1 `
    --artifacts '.\BenchmarkDotNet.Artifacts\axis2-t2-public-end-full'
```

The literal closing parenthesis in the filter is significant. It prevents the `N100000_E512` lane
from admitting another parameter value. The legacy benchmark method name preserves the T1 filter;
its timed body now calls the public `map.ToTransient()` and `Persist()` path and is categorized
`Axis2T2` / `EditPublication` / `PublicTransient`. Diagnostics run only in setup/cleanup to validate
semantic, canonical, and retained-layout parity and emit counters.

Raw artifacts are git-ignored. The complete public full-run artifact is
`BenchmarkDotNet.Artifacts/axis2-t2-public-end-full/`.

## Locked performance result

The locked noise floor comes from five independent affinity-pinned persistent End controls. The
median of process medians is 440.547 us. Three scaled median absolute deviations yield
36.1298501%, larger than the 10% practical margin and the largest relative 99.9% interval. The
mean-latency point comparison uses the median of process means, 443.293 us, producing a deciding
upper cutoff of 283.132 us.

The original T1 representation result was:

| Lane | Mean | Median | 99.9% confidence interval | Allocated | Reachable retained bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Persistent control center | 443.293 us | 440.547 us | five processes | 700 KB | 4,306,320 |
| Direct separate-node T1 | 227.600 us | 216.741 us | 217.736–237.464 us | 253.66 KB | 4,314,808 |

The T1 candidate mean was 48.66% lower than the control center; its confidence-interval upper bound
was 46.43% lower. Allocation fell about 63.76%, while actual reachable retention rose 8,488 bytes
(0.1971%). The Every64 corroboration improved mean latency 46.61%, allocation 50.11%, and even its
adverse confidence-endpoint comparison cleared the locked threshold; retention rose 8,544 bytes
(0.1984%).

The decisive confirmation through the shipped public path was:

| Lane | Samples | Mean | Median | 99.9% confidence interval | Allocated | Reachable retained bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Public `ToTransient` / `Persist` | 100 | 236.700 us | 220.608 us | 222.285–251.115 us | 253.67 KB | 4,314,808 |

The public mean is 46.60% below the 443.293 us control center. Its entire confidence interval is
below the 283.132 us cutoff; even the upper endpoint is 43.35% below the control center. The timed
path therefore preserves a material margin after exposing the public surface. Structural setup
reports zero adoption and publication node visits, one retained token, 1,058 separate branch nodes,
8,464 bytes of separate-node metadata, and 4,314,808 total retained bytes versus 4,306,320 ordinary
bytes (+8,488, +0.1971%).

Before this full run, a pinned one-case BenchmarkDotNet Dry job exercised the public path and
completed semantic/canonical setup with zero adoption/publication node visits. That run remains
classified solely as a harness smoke. A Dry job supplies no stable distribution, contributes no
performance number to this decision, and is not combined with the 100-sample full result.

## Sparse-session and retained-memory boundary

The T1 `N100000_E1 / ClusteredPrefix / EveryEdit` guard remains the honest usage boundary:

| Lane | Mean | Median | 99.9% confidence interval | Allocated | Reachable retained bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Direct persistent | 581.787 ns | 563.411 ns | 563.876–599.698 ns | 1,400 B | 4,306,320 |
| Transient | 645.411 ns | 618.623 ns | 616.297–674.525 ns | 1,488 B | 4,306,320 |

Mean latency increased 10.94%, median increased 9.80%, and allocation increased 88 bytes (6.29%).
The combined confidence bounds imply roughly a 2.77%–19.62% slowdown, so the bare 10% line is
inconclusive; the result remains inside the locked 36.1298501% noise-qualified regression threshold.
The one-edit deferral allocated no token, plan, separate node, or retained owner metadata. This is a
bounded cost, not a win claim: callers with one sparse edit should continue using the ordinary
persistent operation.

Published long-session graphs intentionally retain sealed edit metadata to keep publication O(1).
The measured +0.1971%/+0.1984% figures clear the 10% retained-byte guard for the named workloads but
are not universal constants. Applications retaining many transient-produced histories should
measure their own graph mix and must not assume publication removes tags.

## Correctness and phase-exit evidence

The C# T2 checkpoint records:

- Release HAMT and benchmark builds with zero warnings;
- 33 final focused public transient/API tests;
- the existing 26 focused direct-kernel tests; and
- the complete `Durable7.Hamt.Tests` project: **223 passed, 0 failed**.

Coverage includes exact public shape, XML documentation, factories, clean identity, no-op versions,
comparer identity, stored representatives, nulls, equal-hash collisions, deep prefixes, stable
enumeration, copied struct enumerators, map key/value views, receiver-comparer set relations,
dictionary/`HashSet<T>` model histories across publications, retained-base/later-generation
isolation, recursive canonicality/counts, and consumed direct/interface/view/enumerator aliases.
Deterministic callback, edit-allocation, map-publication, and set-wrapper-publication failures prove
the strong exception and retry contracts through the public verbs.

## Language posture and remaining Axis 2 work

This decision changes the C# current-state catalog only. A later port must preserve result semantics,
policy/comparer identity, stored representatives, no-op identity, snapshot isolation, and failure
behavior, but may require a language-specific ownership representation. T2 makes no blanket sibling-
language parity promise.

Track F remains planned rather than shipped. Its fixed-layout F1 bake-off and explicit select/defer
gate are recorded separately; F2 public frozen map/set and F3 Ctrie snapshot conversion require their
own benchmark, API, semantic, memory, and conversion evidence. T2 neither prejudges that layout nor
uses a transient win as evidence for a frozen tier.

## Exit outcome

T2 exits with **Ship the public C# CHAMP map/set transient**. The public production path directly
uses the selected engine, clears the locked many-edits-per-publication gate, retains a disclosed and
measured metadata cost, preserves the sparse one-edit warning, and satisfies the one-way lifecycle
and public failure contracts. Track F remains the next independent hash-tier decision.
