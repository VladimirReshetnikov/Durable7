# Hamt

- Status: Implemented workspace
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers implementing and reviewing the CHAMP, Ctrie, Patricia, and Merkle families
- Scope: Project layout and validation entry points for `src/CSharp/src/Tools.DataStructures.Hamt`

`src/CSharp/src/Tools.DataStructures.Hamt` contains the .NET 10 C# preview workspace for
`Tools.DataStructures.Hamt`, a persistent and concurrent trie/search-tree library led by canonical
CHAMP. `PersistentHashMap<TKey, TValue>` is an immutable unordered dictionary with structural
sharing across versions. `PersistentHashSet<T>` is built on the same core and implements
`IReadOnlySet<T>`. Both expose the optimized C# one-way `Transient` editing session for many edits
per publication. `CreateTransient` starts empty and `ToTransient` adopts a persistent value in O(1);
`Persist` publishes in O(1), consumes the session, and returns the exact source object when the
session remained logically clean. The map surface is the selected direct separate-node engine
itself, not an additional public facade allocation; the set surface is a thin `IReadOnlySet<T>`
facade over that map engine. C, C++, Haskell, Kotlin, and Rust separately expose the same one-way
lifecycle through semantic sessions whose changed edits remain persistent path-copy operations;
they do not inherit this C# engine's owner-token performance claim.

`ConcurrentHashTrie<TKey, TValue>` is the deliberately mutable member of the family. It applies
GCAS descriptors to generation-stamped indirection nodes and a root/main RDCSS transition for
snapshots, giving lock-free reads and updates, stable enumeration, and O(1) immutable generation
snapshots without losing a writer that races between root-main observation and publication.
Deletion tombs and promotes sparse children to keep paths compact. A snapshot can be copied into the
canonical `PersistentHashMap<TKey, TValue>` representation explicitly in O(n).

`PersistentIntMap<TValue>` / `PersistentIntSet` and `PersistentLongMap<TValue>` /
`PersistentLongSet` are big-endian Patricia tries for signed 32-bit and 64-bit keys. Their
path-compressed binary shape provides ascending signed enumeration and prefix-aware structural
`Union`, `Intersect`, and `Except` with reference-equal subtree pruning.

`MerkleSearchTree<TKey, TValue>` is the ordered content-addressed sibling. The
`mst-sha256-b16-v2` format assigns every canonical key to a geometric layer by counting leading
zero hexadecimal digits in its policy-bound SHA-256 key digest. A block holds the consecutive
entries at one layer plus one child digest per intervening key interval, giving a canonical B=16
wide tree. It provides ordered ranges, O(1)
content-address comparison, verified semantic equality and diff, strict block persistence,
membership/non-membership/range proofs, block-level synchronization, and typed three-way merge.

Merkle persistence is explicit and defensive. Bidirectional codecs must reject malformed,
non-canonical, and trailing input. `Save`/`Load` and complete or partial `MerkleBlockPack` values use
an `IMerkleBlockStore`; loading rehashes exact block bytes, round-trips decoded entries, validates
layers, ordering, child intervals, subtree counts, and the root, and enforces finite caller-selected
resource budgets. Proof verification charges its canonical query descriptor before codec or block
decoding as well as charging every distinct block. Proofs and synchronization inherit SHA-256's
collision-resistance assumption and still require a trusted root and policy domain; they do not
provide signatures or peer authentication.

The trie consumes 5 hash bits per level. Each sparse branch has separate data and node bitmaps,
with key/value payloads inlined into a compact data run and subtries held in a compact child run.
Canonical deletion promotes singleton child payloads back into their parent; equal-hash unequal-key
collisions remain immutable collision buckets. Insert,
replace, lookup, and removal run in O(hash-width / 5) expected time plus collision-bucket length for
adversarial equal hashes; operations clone only the search path and reuse every untouched subtree.
Lookups allocate nothing, single-pass `Add`/`TryAdd` hash and walk once, and both collections expose
allocation-free copy-safe struct enumerators.

From-scratch map/set factories use an internal bulk builder. It stages entries by full hash and
freezes them directly into canonical CHAMP topology, avoiding a persistent path copy for every item.
The same internal facility is available to the sibling Tungsten assembly
for association relabel/sort/reverse rebuilds; no mutable storage is ever shared with a published map.

Transient sessions preserve the persistent CHAMP comparer, stored-representative, collision,
enumeration, and no-op rules. They are unsynchronized and have one logical owner; sequential transfer
between threads requires external synchronization, and concurrent access is unsupported. A changed
edit invalidates captured enumerators and map key/value views, while a logical no-op does not.
Successful publication invalidates every direct, interface, view, and enumerator alias with
`ObjectDisposedException`; no alias can continue reading the newly persistent graph. The retained
base remains immutable and concurrently readable throughout the session.

The first changed edit stays on the ordinary persistent path. A later changed edit may promote only
reusable branch or collision paths into separate transient-editable node types. Published edited
graphs retain sealed ownership metadata so adoption and publication never walk the graph or clear
tags. This is a measured space cost: the pinned 100,000-entry/512-edit End lane retained 8,488 extra
bytes (0.1971%), and the Every64 lane retained 8,544 extra bytes (0.1984%). A one-edit sparse session
retained no extra graph bytes but was slower and allocated 88 more bytes than the direct persistent
operation, so direct persistent methods remain the recommended one-off path.

`MapEquals` and `Diff` walk that topology in lockstep and prune reference-equal descendants. This is
especially effective for versions with shared ancestry. Independently built equal maps still need a
full semantic traversal: collision order and stored representatives are intentionally not canonical,
and canonical topology alone does not confer reference identity.

## Layout

- `DataStructures.sln` is the solution entry point.
- `src/Tools.DataStructures.Hamt/` contains the public library.
  - `PersistentHashMap.cs` is the bitmap-indexed HAMT map implementation.
  - `PersistentHashMap.Transient.cs` and `PersistentHashMap.OwnerTokenKernel.cs` expose and implement
    the public one-way map transient.
  - `MapDifference.cs` defines the added/removed/changed result vocabulary used by structural diff.
  - `ConcurrentHashTrie.cs` is the lock-free mutable map with O(1) immutable snapshots.
  - `PersistentIntMap.cs`, `PersistentLongMap.cs`, and their set facades expose the Patricia family.
  - `Internal/PatriciaMapCore.cs` contains the shared width-specialized engine.
  - `MerkleEncoding.cs` defines strict canonical codecs, the 256-bit digest, and versioned policy
    domain.
  - `MerkleSearchTree.cs` implements the canonical B=16 wide-block ordered map and typed diff.
  - `MerklePersistence.cs` defines immutable blocks and packs, the block-store abstraction,
    synchronization/proof envelopes, verification budgets and failures, and typed merge results.
  - `MerkleSearchTree.PersistenceAlgorithms.cs` implements save/load/import, one-shot and iterative
    synchronization, point/range proofs, and three-way merge.
  - `PersistentHashSet.cs` is the set wrapper over the map core.
  - `PersistentHashSet.Transient.cs` is the public one-way set facade.
- [`tests/Tools.DataStructures.Hamt.Tests/`](../../tests/Tools.DataStructures.Hamt.Tests/README.md) contains xUnit
  and CsCheck-backed model tests.
- [`benchmarks/Tools.DataStructures.FingerTree.Benchmarks/`](../../benchmarks/Tools.DataStructures.FingerTree.Benchmarks/README.md)
  is the shared C# persistent-collections harness. Its `ChampBenchmarks`, `CtrieBenchmarks`,
  `TransientLifecycleBenchmarks`, `PatriciaMapBenchmarks`, and `MerkleSearchTreeBenchmarks` classes
  cover this workspace; Release configuration is required for meaningful measurements. The
  [CHAMP T1 decision](transient-t1-decision.md) curates the selected representation evidence, and
  the [T2 decision](transient-t2-decision.md) records public shipment.
- `docs/api-specification.md` documents public contracts and complexity guarantees.
- `docs/usage.md` provides practical construction, comparer, persistent update, iteration, and
  set-algebra examples.
- `docs/validation.md` records local restore/build/test commands, warning policy, and test coverage.

## Validation

Use the local .NET SDK:

```powershell
$env:DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER = '1'
$env:DOTNET_CLI_USE_MSBUILD_SERVER = '0'
$env:MSBUILDDISABLENODEREUSE = '1'
$env:BuildInParallel = 'false'
$env:UseSharedCompilation = 'false'
$env:RestoreDisableParallel = 'true'

dotnet test .\tests\Tools.DataStructures.Hamt.Tests\Tools.DataStructures.Hamt.Tests.csproj `
    -c Release --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false `
    -- RunConfiguration.MaxCpuCount=1
```

See [`docs/validation.md`](validation.md) for the restore/build/test split, XML documentation
warning gate, complete single-node commands, and the 223-test C# HAMT checkpoint.
