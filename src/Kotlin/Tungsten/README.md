# Kotlin Tungsten Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Kotlin Tungsten-collections port
- Scope: Kotlin/JVM sources and tests under `src/Kotlin/Tungsten`

This workspace ports the Tungsten `List` and `Association` collection family to Kotlin/JVM. Public
types live in `tools.datastructures.tungsten`:

This is an application-specific leaf port. It may consume the Kotlin HAMT and FingerTree packages,
but no general Kotlin/JVM library may depend on Tungsten or treat its kernel-derived behavior as a
baseline. Fork reusable mechanics into an independently owned implementation; C# is authoritative
only for the sibling Tungsten ports. See the normative
[application-leaf boundary](../../../docs/reference/tungsten-application-leaf-boundary.md).

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
concurrent readers over shared immutable snapshots. Balance-factor assertions run throughout the
generated histories, and a 20,000-element sequence undergoes 2,000 split/join/remove cycles with the
AVL height and cached-size invariant checked after every operation.
