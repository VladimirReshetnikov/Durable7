# HAMT C# Usage Guide

- Created (UTC): 2026-07-02T20:12:28Z
- Repository HEAD: f448af2c7626e4f3b06f74701c3f9f9383db7446
- Audience: .NET consumers and maintainers using the C# HAMT, Ctrie, Patricia, and Merkle families
- Scope: Construction, persistent and transient updates, comparer behavior, iteration, concurrency, and content-addressed workflows

This guide is the practical companion to the [C# API specification](api-specification.md). It shows
the common usage patterns for the CHAMP, Ctrie, Patricia, and Merkle families; the API specification
remains the normative contract for complexity, allocation behavior, and edge cases.

## Namespace And Build

The public types live in the `Tools.DataStructures.Hamt` namespace:

```csharp
using Tools.DataStructures.Hamt;
```

Validate the workspace through the solution:

```powershell
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet restore .\DataStructures.sln --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build .\DataStructures.sln -c Release --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet test .\tests\Tools.DataStructures.Hamt.Tests\Tools.DataStructures.Hamt.Tests.csproj `
    -c Release --no-restore --no-build --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false `
    -- RunConfiguration.MaxCpuCount=1
```

The repository currently builds the library from source under
[`src/Tools.DataStructures.Hamt`](../../src/Tools.DataStructures.Hamt/Tools.DataStructures.Hamt.csproj).

## Persistent Values

Maps and sets are immutable persistent values. Update-shaped members return a new version and leave
the source version usable:

```csharp
var empty = PersistentHashMap<int, string>.Empty;
var one = empty.SetItem(1, "one");
var two = one.SetItem(2, "two");
var replaced = two.SetItem(1, "uno");

// empty has no keys.
// one has { 1 -> "one" }.
// two has { 1 -> "one", 2 -> "two" }.
// replaced has { 1 -> "uno", 2 -> "two" }.
```

No-op updates return the current instance. Examples include replacing a value with one that compares
equal under `EqualityComparer<TValue>.Default`, removing an absent key, clearing an empty map, and
adding an already present item to a set.

## Persistent Map

Use `SetItem` for add-or-replace:

```csharp
var map = PersistentHashMap<int, string>.Empty
    .SetItem(1, "one")
    .SetItem(2, "two");

if (map.TryGetValue(1, out var value))
{
    // value == "one"
}

var twoValue = map[2];
```

Use `Add` when duplicates are programmer errors, and `TryAdd` when duplicate rejection is part of
normal control flow:

```csharp
var unique = PersistentHashMap<int, string>.Empty.Add(1, "one");

if (!unique.TryAdd(1, "duplicate", out var same))
{
    // same is the original map.
}

if (unique.TryAdd(2, "two", out var withTwo))
{
    // withTwo has both keys; unique is unchanged.
}
```

`TryRemove` reports whether the key existed and returns the removed value:

```csharp
if (withTwo.TryRemove(1, out var withoutOne, out var removedValue))
{
    // removedValue == "one"
    // withoutOne no longer contains key 1.
}
```

Use `CreateRange` to build from scratch and `SetItems` to add or replace a sequence on an existing
map. Both apply entries in enumeration order with last-wins value semantics for equivalent keys:

```csharp
var built = PersistentHashMap<int, string>.CreateRange(new[]
{
    KeyValuePair.Create(1, "one"),
    KeyValuePair.Create(2, "two"),
    KeyValuePair.Create(1, "uno"),
});
```

Use `MapEquals` for semantic map equality and `Diff` for typed added/removed/changed entries. Both
operations require the same key-comparer object so their lockstep CHAMP traversal has one hash and
equivalence policy:

```csharp
IEqualityComparer<string> keys = StringComparer.OrdinalIgnoreCase;
var before = PersistentHashMap<string, int>
    .Create(keys)
    .SetItem("alpha", 1)
    .SetItem("beta", 2);
var after = before
    .SetItem("BETA", 20)
    .SetItem("gamma", 3);

bool unchanged = before.MapEquals(after); // false

foreach (MapDifference<string, int> change in before.Diff(after))
{
    Console.WriteLine($"{change.Kind}: {change.Key}: {change.OldValue} -> {change.NewValue}");
}
```

Reference-equal descendants are pruned, so closely related versions visit only their non-shared
trie regions plus reported output. Independently allocated maps can require O(n + m) traversal;
canonical CHAMP topology is not object identity, and unordered equal-hash collision buckets add
their key-matching cost.

## Comparers And Equivalent Keys

The default factories use `EqualityComparer<TKey>.Default`. Pass an `IEqualityComparer<TKey>` when
keys need custom hash/equality semantics:

```csharp
var map = PersistentHashMap<string, int>
    .Create(StringComparer.OrdinalIgnoreCase)
    .SetItem("Alpha", 1)
    .SetItem("beta", 2);

var found = map.TryGetValue("ALPHA", out var alphaValue);
```

Equivalent keys must produce equal hash codes. Comparer behavior should remain stable for the
lifetime of every map version created with that comparer.

When an update uses an equivalent key, the map keeps the originally stored key object. Use
`TryGetKey` to recover it:

```csharp
if (map.TryGetKey("alpha", out var actualKey))
{
    // actualKey is the stored equivalent key, for example "Alpha".
}
```

The same rule applies to `CreateRange`: later equivalent keys replace the value, while the first
stored key object remains the enumerated key. If the replacement value compares equal under
`EqualityComparer<TValue>.Default`, the earlier stored value object is retained as well.

Null-key behavior is entirely comparer-defined. The default comparer supports null for nullable
reference keys; a custom comparer must do whatever its own contract promises.

## Iteration

`PersistentHashMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`, and its `Keys` and
`Values` views follow pair enumeration order:

```csharp
foreach (var (key, value) in map)
{
    // Inspect key and value.
}

foreach (var key in map.Keys)
{
    // Inspect keys in the same trie order as pair enumeration.
}
```

Enumeration follows the HAMT bitmap/collision shape. It is stable for an unchanged version, but it
is not insertion order or sorted order. The public struct enumerators are copy-safe and keep
traversal state inline; `foreach` through the concrete collection avoids boxing the enumerator.

## Persistent Set

`PersistentHashSet<T>` is the set facade over the map core and implements `IReadOnlySet<T>`:

```csharp
var empty = PersistentHashSet<int>.Empty;
var one = empty.Add(1);
var two = one.Add(2);
var removed = two.Remove(1);

var hasTwo = removed.Contains(2);
```

Use try-pattern operations when membership changes matter:

```csharp
if (!one.TryAdd(1, out var duplicate))
{
    // duplicate is one.
}

if (one.TryAdd(2, out var withTwo) &&
    withTwo.TryRemove(1, out var withoutOne))
{
    // withoutOne contains 2 and no longer contains 1.
}
```

Pass a comparer through `Create` or `CreateRange` for custom item equality. Use `TryGetValue` to
recover the originally stored item object when an equivalent item is present:

```csharp
var set = PersistentHashSet<string>
    .CreateRange(new[] { "Alpha", "beta" }, StringComparer.OrdinalIgnoreCase);

if (set.TryGetValue("ALPHA", out var actualValue))
{
    // actualValue is "Alpha".
}
```

## One-Way Transient Editing

Use a transient when one logical owner will apply many point edits before publishing the next
persistent version. `ToTransient()` adopts an existing map in O(1), without walking or copying its
trie. The source remains immutable and safe for concurrent readers:

```csharp
var baseline = PersistentHashMap<int, string>.CreateRange(
    Enumerable.Range(0, 100_000).Select(i => KeyValuePair.Create(i, $"v{i}")));

var edit = baseline.ToTransient();
for (int i = 0; i < 512; i++)
    edit.SetItem(i * 3, $"changed-{i}");

edit.Remove(40_000);
edit.TryAdd(100_001, "new");
PersistentHashMap<int, string> published = edit.Persist();

// baseline is unchanged; edit has been consumed.
Console.WriteLine(baseline[0]);
```

`PersistentHashMap<TKey, TValue>.CreateTransient(comparer)` starts an empty session. The map
transient implements `IReadOnlyDictionary<TKey, TValue>` and exposes `Count`, `Comparer`, the
indexer, `Keys`, `Values`, `ContainsKey`, `TryGetValue`, `TryGetKey`, `Add`, `TryAdd`, `SetItem`,
`Remove`, `Clear`, `GetEnumerator`, and `Persist`. It deliberately has no range methods, reusable
snapshot method, or `ToImmutable` builder lifecycle.

Equivalent keys keep the first stored key object. `SetItem` replaces only the value; a replacement
equal under `EqualityComparer<TValue>.Default` retains the stored value object and is a logical
no-op. Duplicate `TryAdd`, absent `Remove`, and clearing an empty session are also no-ops. They do
not invalidate existing enumerators or key/value views, and a session containing only such edits
publishes its exact source map by reference:

```csharp
var source = PersistentHashMap<string, int>
    .Create(StringComparer.OrdinalIgnoreCase)
    .SetItem("Alpha", 1);

var edit = source.ToTransient();
edit.SetItem("ALPHA", 1);       // no-op; representative and value are retained
edit.TryAdd("alpha", 99);       // false; no-op
edit.Remove("missing");         // false; no-op

var same = edit.Persist();
Debug.Assert(ReferenceEquals(source, same));
Debug.Assert(ReferenceEquals(source.Comparer, same.Comparer));
```

The set facade follows the same lifecycle and implements `IReadOnlySet<T>`. `Add` and `Remove`
return whether membership changed; `TryGetValue` recovers the first stored representative; and the
six read-only relation methods use the receiver's comparer:

```csharp
var edit = PersistentHashSet<string>
    .CreateTransient(StringComparer.OrdinalIgnoreCase);

edit.Add("Alpha");              // true
edit.Add("ALPHA");              // false; keeps "Alpha"
edit.Add("Beta");
bool includesAlpha = edit.IsSupersetOf(new[] { "alpha" });
PersistentHashSet<string> published = edit.Persist();
```

`Persist()` is terminal and O(1). After a successful call, every property read, lookup, mutation,
relation query, enumeration request, and second `Persist()` through any direct or interface alias
throws `ObjectDisposedException`. Map `Keys`/`Values` views and map/set enumerators obtained before
publication throw the same exception; they cannot be used to drain an alias of the published graph.
A changed edit instead invalidates previously captured enumerators/views with
`InvalidOperationException`. Concrete enumeration uses the allocation-free struct enumerator and
follows the same stable-but-unspecified trie order as the persistent collection.

Each point edit has the strong exception guarantee. Hash/equality callbacks and replacement
allocations complete before any session-owned node is changed. Publication prepares the immutable
map and, for a set, its wrapper before the non-throwing consume step; a preparation failure leaves
the session active, unchanged, and retryable.

The session is unsynchronized and has one logical owner. Transfer it between threads only
sequentially and under caller-provided synchronization; concurrent access is unsupported. Prefer
ordinary persistent operations for a sparse one-off edit. The selected implementation defers its
editable machinery until a later changed edit can reuse a path, but a measured one-edit session was
still slower and allocated 88 more bytes than the direct persistent operation. Long edited graphs
may also retain sealed ownership metadata after publication; `Persist()` never hides that cost with
an O(n) tag-clearing walk.

## Set Algebra

Set algebra methods accept `IEnumerable<T>` inputs and interpret membership through the receiver's
comparer:

```csharp
var left = PersistentHashSet<int>.CreateRange(new[] { 1, 2, 3 });
var right = new[] { 3, 4 };

var union = left.Union(right);
var intersection = left.Intersect(right);
var difference = left.Except(right);
var symmetric = left.SymmetricExcept(right);

var subset = left.IsSubsetOf(new[] { 1, 2, 3, 4 });
var overlaps = left.Overlaps(right);
var equal = left.SetEquals(new[] { 3, 2, 1 });
```

`Intersect`, `SymmetricExcept`, `IsSubsetOf`, `IsProperSubsetOf`, `IsProperSupersetOf`, and
`SetEquals` materialize the input into a temporary `HashSet<T>` using the receiver's comparer.
`IsSupersetOf` and `Overlaps` stream their inputs and can exit early.

## Concurrency And Lifetime

Map and set instances are immutable after construction. Independent snapshots can be read and
enumerated concurrently while other threads compute new versions from the same snapshot. Normal .NET
variable ownership still applies: do not race on the same mutable variable while another thread
reassigns it to a newer version.

For a shared mutable map, use `ConcurrentHashTrie<TKey, TValue>`. Its updates are atomic and its
snapshot is an O(1) immutable Ctrie generation:

```csharp
var live = new ConcurrentHashTrie<string, int>(StringComparer.OrdinalIgnoreCase);
live.AddOrUpdate("requests", _ => 1, (_, count) => count + 1);

ConcurrentHashTrie<string, int>.SnapshotView published = live.Snapshot();
live["requests"] = 100;

// The snapshot remains stable at 1 while the live trie advances.
Console.WriteLine(published["REQUESTS"]);

// Conversion to the canonical CHAMP family is explicit and O(n).
PersistentHashMap<string, int> champ = published.ToPersistentHashMap();
```

The conversion enumerates that captured generation once. The resulting CHAMP map keeps the exact
comparer object, snapshot enumeration sequence, and stored key/value representatives, including
null and equal-full-hash collision entries when the comparer supports them. Writes before, during,
or after conversion can advance the live Ctrie but cannot alter the captured generation or the
resulting persistent map.

Factories passed to `GetOrAdd` and `AddOrUpdate` may be invoked more than once under contention;
keep them repeatable and free of non-repeatable side effects.

## Integer-Key Patricia Maps

Choose the Patricia family when keys are exactly signed 32-bit or 64-bit integers and ordered
enumeration or structural merge matters:

```csharp
var baseline = PersistentIntMap<string>.CreateRange(
    new[] { KeyValuePair.Create(-1, "old"), KeyValuePair.Create(10, "ten") });
var delta = PersistentIntMap<string>.CreateRange(
    new[] { KeyValuePair.Create(-1, "new"), KeyValuePair.Create(20, "twenty") });

var merged = baseline.Union(delta); // right-biased
var combined = baseline.Union(delta, (key, left, right) => $"{key}:{left}+{right}");

// -1, 10, 20: ascending signed order.
foreach (var key in merged.Keys)
    Console.WriteLine(key);
```

Use `PersistentLongMap<TValue>` for `long` keys. `PersistentIntSet` and `PersistentLongSet` expose
the corresponding value-set surfaces with structural `Union`, `Intersect`, and `Except`.

## Content-Addressed Ordered Maps

Construct a Merkle tree only with an explicit semantic and encoding policy. The policy id must
version the application's comparer semantics, and each codec id must end in `-v` plus decimal
digits:

```csharp
var policy = MerkleSearchTreePolicy<int, string?>.Create(
    "document-index-v1",
    Comparer<int>.Default,
    MerkleCodecs.Int32,
    MerkleCodecs.Utf8String);

var baseline = MerkleSearchTree<int, string?>.Create(policy)
    .SetItem(10, "ten")
    .SetItem(20, "twenty");
var updated = baseline.SetItem(20, "TWENTY").SetItem(30, "thirty");

MerkleDigest address = updated.RootHash;
IReadOnlyList<MerkleMapDifference<int, string?>> changes = baseline.Diff(updated);

foreach (var (key, value) in updated.EnumerateRange(15, 30))
    Console.WriteLine($"{key}: {value}");
```

Recreate the same policy id, comparer semantics, codec ids, and logical entries in another process
to obtain the same B=16 wide-block graph and root address. `IMerkleCodec<T>.Decode` must accept
exactly one canonical encoding and reject malformed, non-canonical, or trailing input. Do not use a
codec whose output depends on process-randomized hashes, culture, object identity, ambient
serialization settings, or mutable state.

### Persist And Verify Blocks

`Save` stores the complete closure. `Load` never trusts the store: it recomputes block digests,
strictly decodes and re-encodes entries, follows and validates child references, checks the expected
root, and applies a finite verification budget.

```csharp
var store = new InMemoryMerkleBlockStore();
int blocksAdded = updated.Save(store);

var loaded = MerkleSearchTree<int, string?>.Load(
    updated.RootHash,
    policy,
    store);

if (!loaded.MapEquals(updated))
    throw new InvalidOperationException("Verified round-trip changed the map.");
```

Use a pack at a transport boundary. A complete pack is self-contained; a partial pack can be
completed from blocks already present in the destination store. When a destination is supplied,
`Import` verifies the complete root closure before committing the pack's blocks.

```csharp
MerkleBlockPack outbound = updated.ExportPack();
var replicaStore = new InMemoryMerkleBlockStore();

var replica = MerkleSearchTree<int, string?>.Import(
    outbound,
    policy,
    replicaStore);

Console.WriteLine(replica.RootHash == updated.RootHash); // True
```

For untrusted network input, retain or tighten the bounded defaults:

```csharp
var networkBudget = new MerkleVerificationBudget(
    maxBlockCount: 50_000,
    maxTotalByteCount: 64L << 20,
    maxBlockByteCount: 1 << 20,
    maxDepth: 128,
    maxEntryCount: 2_000_000,
    maxChildReferencesPerBlock: 8_192,
    maxProofQueryByteCount: 64 << 10);

var bounded = MerkleSearchTree<int, string?>.Import(
    outbound,
    policy,
    budget: networkBudget);
```

Proof verification charges its query descriptor before decoding codecs or supplied blocks. The
six-argument constructor uses `maxBlockByteCount` as the query limit; pass the seventh argument when
the transport should admit large blocks but only small point/range descriptors.

`MerkleBlock` and `IMerkleBlockStore.Put` do not by themselves verify transported bytes. Publish a
received root only after successful `Load` or `Import`.

### Prove Point And Range Claims

Point proofs are canonical membership or non-membership proofs. Range proofs establish completeness
for inclusive bounds by expanding every intersecting child interval.

```csharp
MerkleProof present = updated.CreateProof(20);
MerkleProof absent = updated.CreateProof(99);
MerkleProof range = updated.CreateRangeProof(15, 30);

foreach (var proof in new[] { present, absent, range })
{
    MerkleProofVerificationResult result =
        MerkleSearchTree<int, string?>.VerifyProof(proof, policy);

    if (!result.IsValid || result.ComputedRootHash != updated.RootHash)
        throw new InvalidDataException(result.FailureMessage ?? "Proof verification failed.");
}
```

Verification establishes the proof's encoded claim relative to its declared root and policy
domain. Obtain the expected root through a trusted channel. A proof is not a signature and does not
authenticate its sender.

### Synchronize By Block Address

For a receiver whose stored blocks each imply a previously verified descendant closure, create one
pack containing all missing target regions:

```csharp
var receiverStore = new InMemoryMerkleBlockStore();
MerkleBlockPack missing = updated.CreateSyncPack(receiverStore);

var synchronized = MerkleSearchTree<int, string?>.Import(
    missing,
    policy,
    receiverStore);
```

For an empty or incomplete staging store, exchange frontier requests iteratively. The sender owns
the target tree; the receiver stages immutable blocks and verifies the complete closure once no more
blocks are requested:

```csharp
var staging = new InMemoryMerkleBlockStore();
MerkleSearchTree<int, string?> published = baseline;

while (true)
{
    MerkleSyncPlan plan = updated.PlanSync(published, staging);
    if (!plan.RequiresBlocks)
        break;

    MerkleBlockPack response = updated.ExportPack(plan.RequestedBlocks);
    foreach (MerkleBlock block in response.Blocks)
        staging.Put(block); // Staged, not yet trusted.
}

published = MerkleSearchTree<int, string?>.Load(
    updated.RootHash,
    policy,
    staging);
```

`RootsMatch` means the supplied published tree already has the target root. A plan with
`RequiresBlocks == false` can instead mean that staging has a complete target closure awaiting
verification.

### Merge Descendants From A Common Base

Three-way merge accepts one-sided changes and identical two-sided changes without invoking the
resolver. A true conflict carries typed base/left/right states; `MerkleMergeValue<T>` distinguishes
an absent key from a present null value.

```csharp
var common = updated.SetItem(20, "twenty");
var left = common.SetItem(20, "TWENTY").SetItem(40, "forty");
var right = common.SetItem(20, "veinte").SetItem(50, "fifty");

MerkleThreeWayMergeResult<int, string?> merge =
    MerkleSearchTree<int, string?>.Merge(
        common,
        left,
        right,
        conflict => conflict.Key == 20
            ? MerkleMergeResolution<string?>.SetValue("20")
            : MerkleMergeResolution<string?>.Unresolved);

var merged = merge.IsSuccess
    ? merge.MergedTree
    : throw new InvalidOperationException(
        $"Unresolved conflicts: {merge.UnresolvedConflicts.Count}");
```

Resolvers can also return `UseBase`, `UseLeft`, `UseRight`, or `Delete`. If any conflict remains
unresolved, the result exposes every unresolved conflict and no partial tree.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Immutable unordered key/value collection | `PersistentHashMap<TKey, TValue>` |
| Many single-owner map edits per publication | `PersistentHashMap<TKey, TValue>.Transient` |
| Add-or-replace update | `SetItem` or `SetItems` |
| Duplicate-rejecting insert | `Add` or `TryAdd` |
| Stored equivalent key recovery | `TryGetKey` |
| Immutable unordered value set | `PersistentHashSet<T>` |
| Many single-owner set edits per publication | `PersistentHashSet<T>.Transient` |
| Stored equivalent item recovery | `TryGetValue` |
| Union/intersection/difference | `Union`, `Intersect`, `Except`, `SymmetricExcept` |
| Custom value semantics | `Create(comparer)` or `CreateRange(items, comparer)` |
| Shared mutable map with O(1) immutable snapshots | `ConcurrentHashTrie<TKey, TValue>` |
| Signed integer keys with ordered structural merge | `PersistentIntMap<TValue>` / `PersistentLongMap<TValue>` |
| Canonical cross-process content address and ordered ranges | `MerkleSearchTree<TKey, TValue>` |
| Verified block persistence and transfer | `Save`, `Load`, `ExportPack`, `Import` |
| Membership, non-membership, or inclusive-range proof | `CreateProof`, `CreateRangeProof`, `VerifyProof` |
| One-shot or iterative block synchronization | `CreateSyncPack` or `PlanSync` |
| Typed three-way content merge | `MerkleSearchTree<TKey, TValue>.Merge` |

For cross-language contract alignment, see the repository
[porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
