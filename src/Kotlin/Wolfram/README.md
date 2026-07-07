# Kotlin Wolfram Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Kotlin Wolfram-collections port
- Scope: Kotlin/JVM sources and tests under `src/Kotlin/Wolfram`

This workspace ports the Wolfram `List` and `Association` collection family to Kotlin/JVM. Public
types live in `tools.datastructures.wolfram`:

- `PersistentList<T>` provides Wolfram-style persistent list operations.
- `PersistentAssociation<K, V>` combines the Kotlin HAMT with a persistent balanced positional
  sequence and sparse stamps for Wolfram Association ordering, keyed lookup, positional access,
  relabeling, slicing, and sorting.

Build and test from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Wolfram
```

The dependency-free executable tests cover list examples, Association ordering rules, custom
`HashPolicy` behavior, relabel stress, and generated histories against ordered models.
