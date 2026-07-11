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
