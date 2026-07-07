# Kotlin Tungsten Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Kotlin Tungsten-collections port
- Scope: Kotlin/JVM sources and tests under `src/Kotlin/Tungsten`

This workspace ports the Tungsten `List` and `Association` collection family to Kotlin/JVM. Public
types live in `tools.datastructures.tungsten`:

- `PersistentList<T>` provides Tungsten-style persistent list operations.
- `PersistentAssociation<K, V>` combines the Kotlin HAMT with a persistent balanced positional
  sequence and sparse stamps for Tungsten Association ordering, keyed lookup, positional access,
  relabeling, slicing, and sorting.

Build and test from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Tungsten
```

The dependency-free executable tests cover list examples, Association ordering rules, custom
`HashPolicy` behavior, relabel stress, generated histories against ordered models, and JVM
concurrent readers over shared immutable snapshots.
