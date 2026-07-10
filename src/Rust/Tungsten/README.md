# Rust Tungsten Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Rust Tungsten-collections port
- Scope: Rust crate under `src/Rust/Tungsten`

`tools-data-structures-tungsten` ports the Tungsten `List` and `Association` collection family to safe
Rust:

- `PersistentList<T>` wraps the Rust FingerTree deque facade.
- `PersistentAssociation<K, V, S>` composes the Rust HAMT with a stamp-ordered persistent deque and
  preserves the C# Tungsten Association ordering rules, keyed lookup behavior, relabel path, slicing,
  and sorting.

Build and test from `src/Rust`:

```powershell
.\test.ps1 -Workspace Tungsten
```

The crate tests cover list operations, Association ordering examples, custom hash/equality policy
behavior through hash builders, relabel stress, deterministic generated histories, `Send`/`Sync`
assertions, and spawned-thread readers over shared immutable snapshots.
