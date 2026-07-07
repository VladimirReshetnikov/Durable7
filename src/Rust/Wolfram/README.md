# Rust Wolfram Collections

- Created (UTC): 2026-07-07T16:47:22Z
- Repository HEAD: ce785265369e84cdb0963f4e85f31805430ad513
- Audience: Maintainers and AI agents working on the Rust Wolfram-collections port
- Scope: Rust crate under `src/Rust/Wolfram`

`tools-data-structures-wolfram` ports the Wolfram `List` and `Association` collection family to safe
Rust:

- `PersistentList<T>` wraps the Rust FingerTree deque facade.
- `PersistentAssociation<K, V, S>` composes the Rust HAMT with a stamp-ordered persistent deque and
  preserves the C# Wolfram Association ordering rules, keyed lookup behavior, relabel path, slicing,
  and sorting.

Build and test from `src/Rust`:

```powershell
cargo test -p tools-data-structures-wolfram
```

The crate tests cover list operations, Association ordering examples, custom hash/equality policy
behavior through hash builders, relabel stress, and deterministic generated histories.
