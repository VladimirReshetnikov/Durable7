# Rust Ordered Collections Test Map

- Created (UTC): 2026-07-15T00:00:00Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers and reviewers of executable parity evidence
- Scope: `tests/persistent_ordered_set.rs` and `tests/persistent_ordered_map.rs`

The integration suite treats the public API as a consumer would. Example tests cover the complete
surface and representative rules; collision and hasher-instrumentation tests cover receiver-policy
normalization; relabel and generated-history tests exercise the dual index through long edit
sequences; failure tests retain and re-check source snapshots; and concurrency tests read cloned
published versions without locks.

`persistent_ordered_map.rs` covers first-key/last-value construction, strict and replacing updates,
explicit positional insertion/movement, keyed removal, ranges, reverse, stable sort, deterministic
relabel fallback, root sharing, retained branching histories, and map dual-index invariants.

Run it through the serialized workspace wrapper:

```powershell
.\test.ps1 -Workspace Ordered
```
