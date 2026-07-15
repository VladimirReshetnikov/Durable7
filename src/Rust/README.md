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
| [Hamt](Hamt/README.md) | Persistent HAMT map/set port with 32-way bitmap-indexed trie nodes and `Arc` structural sharing | `tools_data_structures_hamt::{PersistentHashMap, PersistentHashSet}` | `.\test.ps1 -Workspace Hamt` |
| [FingerTree](FingerTree/README.md) | Rust checkpoint for the FingerTree family, positional/measured/text rope cursors, and mutable DABA Lite FIFO-window aggregator | `tools_data_structures_fingertree::*` | `.\test.ps1 -Workspace FingerTree` |
| [Ordered](Ordered/README.md) | Neutral persistent insertion-ordered set with explicit positional movement, ranges, stable one-shot sorting, and receiver-policy set algebra | `tools_data_structures_ordered::PersistentOrderedSet` | `.\test.ps1 -Workspace Ordered` |
| [RangeUpdate](RangeUpdate/README.md) | Neutral implicit-AVL sequence with ordered cached measures and algebraic lazy contiguous range updates | `tools_data_structures_range_update::{RangeUpdateAlgebra, RangeUpdateSequence}` | `.\test.ps1 -Workspace RangeUpdate` |
| [Tungsten](Tungsten/README.md) | Application-specific leaf port of Tungsten `List` and `Association` over Rust persistent substrates | `tools_data_structures_tungsten::{PersistentList, PersistentAssociation}` | `.\test.ps1 -Workspace Tungsten` |

Run the full Rust validation from this directory:

```powershell
.\test.ps1
```

The wrapper finds Cargo on `PATH` or under the default rustup profile. On Windows it enables inherited
non-interactive OS error handling before Cargo starts a test binary, so assertion, panic, loader, and crash
failures remain console diagnostics with nonzero exits instead of opening modal UI. Use `-Workspace Hamt`,
`-Workspace FingerTree`, `-Workspace Ordered`, `-Workspace RangeUpdate`, or `-Workspace Tungsten`
for focused runs; `-Release` selects the release profile,
and `-CargoArguments` forwards additional Cargo or test-harness options. The wrapper appends
`--jobs 1` before the harness boundary and `--test-threads=1` after it, and also scopes
`CARGO_BUILD_JOBS=1` / `RUST_TEST_THREADS=1` to the invocation, so caller options cannot fan out.

The FingerTree crate intentionally starts as a semantic checkpoint rather than a final asymptotic
parity port. Its public families now use structurally shared Rust tree storage, while some
higher-level algorithms remain simpler Rust checkpoint implementations instead of the C#/C++ lazy
finger-tree spine. Its README marks that boundary so future work can tune representations without
changing the Rust-facing surface. `RopeCursor<T>`, `MeasuredRopeCursor<T, P>`, and `TextRopeCursor`
likewise preserve positional, ordered-measure, text-facade, edit, and branch semantics through
root-sharing rope snapshots without claiming the C# zipper representation or its focus-local
complexity.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and checkpoint obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.

The [RangeUpdate crate](RangeUpdate/README.md) extends FingerTree's public ordered-measure policy
with a nominal lazy-action algebra and owns a separate persistent implicit AVL representation. It
provides logarithmic split/concat, point edits, proper range updates, and proper range measures,
plus O(1) whole-range updates. Its validation is structural and semantic; local benchmarks remain
postponed until an isolated run and are not part of the checked-in gate.
