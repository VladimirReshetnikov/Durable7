# Hamt

- Status: Implemented workspace
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers implementing and reviewing persistent hash-array mapped tries
- Scope: Project layout and validation entry points for `src/CSharp/src/Tools.DataStructures.Hamt`

`src/CSharp/src/Tools.DataStructures.Hamt` contains the .NET 10 C# preview workspace for `Tools.DataStructures.Hamt`, a persistent
CHAMP library. The core type is `PersistentHashMap<TKey, TValue>`, an immutable
unordered dictionary with structural sharing across versions. `PersistentHashSet<T>` is built on the
same HAMT core and implements `IReadOnlySet<T>`.

`ConcurrentHashTrie<TKey, TValue>` is the deliberately mutable member of the family. It publishes
generation-stamped immutable CHAMP roots with compare-and-swap, giving lock-free reads and updates,
stable enumeration, and O(1) `Snapshot()` conversion to `PersistentHashMap<TKey, TValue>`.

`PersistentIntMap<TValue>` / `PersistentIntSet` and `PersistentLongMap<TValue>` /
`PersistentLongSet` are big-endian Patricia tries for signed 32-bit and 64-bit keys. Their
path-compressed binary shape provides ascending signed enumeration and prefix-aware structural
`Union`, `Intersect`, and `Except` with reference-equal subtree pruning.

The trie consumes 5 hash bits per level. Each sparse branch has separate data and node bitmaps,
with key/value payloads inlined into a compact data run and subtries held in a compact child run.
Canonical deletion promotes singleton child payloads back into their parent; equal-hash unequal-key
collisions remain immutable collision buckets. Insert,
replace, lookup, and removal run in O(hash-width / 5) expected time plus collision-bucket length for
adversarial equal hashes; operations clone only the search path and reuse every untouched subtree.
Lookups allocate nothing, single-pass `Add`/`TryAdd` hash and walk once, and both collections expose
allocation-free copy-safe struct enumerators.

From-scratch map/set factories use an internal bulk builder. It stages entries by full hash and
freezes them directly into canonical CHAMP shape, avoiding a persistent path copy for every item.
The same internal facility is available to the sibling Tungsten assembly
for association relabel/sort/reverse rebuilds; no mutable storage is ever shared with a published map.

## Layout

- `DataStructures.sln` is the solution entry point.
- `src/Tools.DataStructures.Hamt/` contains the public library.
  - `PersistentHashMap.cs` is the bitmap-indexed HAMT map implementation.
  - `MapDifference.cs` defines the added/removed/changed result vocabulary used by structural diff.
  - `ConcurrentHashTrie.cs` is the lock-free mutable map with O(1) persistent snapshots.
  - `PersistentIntMap.cs`, `PersistentLongMap.cs`, and their set facades expose the Patricia family.
  - `Internal/PatriciaMapCore.cs` contains the shared width-specialized engine.
  - `PersistentHashSet.cs` is the set wrapper over the map core.
- [`tests/Tools.DataStructures.Hamt.Tests/`](../../tests/Tools.DataStructures.Hamt.Tests/README.md) contains xUnit
  and CsCheck-backed model tests.
- `docs/api-specification.md` documents public contracts and complexity guarantees.
- `docs/usage.md` provides practical construction, comparer, persistent update, iteration, and
  set-algebra examples.
- `docs/validation.md` records local restore/build/test commands, warning policy, and test coverage.

## Validation

Use the local .NET SDK:

```powershell
.\test.ps1
```

See [`docs/validation.md`](validation.md) for the restore/build/test split, XML documentation
warning gate, and xUnit/CsCheck coverage.
