# Kotlin HAMT

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and AI agents reviewing the Kotlin persistent HAMT port
- Scope: `tools.datastructures.hamt` package

This workspace ports the repository HAMT map and set family to Kotlin/JVM. It provides persistent
`PersistentHashMap<K, V>` and `PersistentHashSet<T>` values with a canonical 32-way CHAMP trie,
separate data/node bitmaps, inline payload runs, immutable equal-hash collision buckets, structural
sharing between versions, and optional runtime `HashPolicy<K>` values for custom hash/equality
behavior. Maps expose policy-compatible semantic equality and typed added/removed/changed diff.

`ConcurrentHashTrie<K,V>` is the deliberately mutable JVM member. It uses generation-stamped
indirection nodes and helping GCAS descriptors for lock-free updates, captures immutable generations
in O(1), lazily renews old-generation children on later write paths, and converts snapshots to the
canonical persistent CHAMP representation explicitly in O(n).

The integer-specialized family provides `PersistentIntMap`/`PersistentIntSet` and
`PersistentLongMap`/`PersistentLongSet`. A shared big-endian Patricia engine path-compresses on the
highest differing bit, sign-flips keys for ascending signed iteration, and performs prefix-aware
structural union, intersection, and difference without hashing or collision buckets. Branches cache
subtree cardinality, so algebra results publish their count without a finishing traversal; map union
and intersection also provide `(key, leftValue, rightValue)` combining overloads. Rebuilds retain the
receiver and its root whenever an update or algebra operation is semantically unchanged.

The default factories use Kotlin `hashCode`/`equals`, keeping the public shape close to JVM collection
expectations while preserving the repository HAMT contracts: persistent updates, duplicate-key
rejection, last-wins bulk replacement, original-key retention on equivalent-key replacement, and set
algebra.

Validate from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Hamt
```

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md).
