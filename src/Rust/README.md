# Rust Workspaces

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents working in the Rust source root
- Scope: Rust data-structure workspaces under `src/Rust`

The Rust root contains Cargo crates for repository-owned persistent data structures. The crates use
safe Rust only and follow Rust naming and result-shaping conventions while preserving the observable
contracts of the C# baseline where the current implementation exposes equivalent capability.

| Workspace | Role | Primary entry points | Validation |
| --- | --- | --- | --- |
| [Hamt](Hamt/README.md) | Persistent HAMT map/set port with 32-way bitmap-indexed trie nodes and `Arc` structural sharing | `tools_data_structures_hamt::{PersistentHashMap, PersistentHashSet}` | `cargo test -p tools-data-structures-hamt` |
| [FingerTree](FingerTree/README.md) | Rust checkpoint for the FingerTree family: persistent deque, measured sequence with built-in and product policies, reversible deque, sorted collections, priority queue, intervals, ropes, and text helpers | `tools_data_structures_fingertree::*` | `cargo test -p tools-data-structures-fingertree` |

Run the full Rust validation from this directory:

```powershell
cargo test --workspace
```

If Cargo is installed under the default rustup profile but not on `PATH`, use:

```powershell
& $env:USERPROFILE\.cargo\bin\cargo.exe test --workspace
```

The FingerTree crate intentionally starts as a semantic checkpoint rather than a final asymptotic
parity port. Its public families now use structurally shared Rust tree storage, while some
higher-level algorithms remain simpler Rust checkpoint implementations instead of the C#/C++ lazy
finger-tree spine. Its README marks that boundary so future work can tune representations without
changing the Rust-facing surface.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and checkpoint obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
