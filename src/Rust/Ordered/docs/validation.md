# Rust Ordered Collections Validation

- Created (UTC): 2026-07-15T00:00:00Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers and reviewers validating the Rust ordered-set port
- Scope: Serialized build/test gate and invariant evidence

Run the focused gate from `src/Rust`:

```powershell
.\test.ps1 -Workspace Ordered
```

The wrapper forces one Cargo build job and one Rust test thread. Use `-Release` only for a release
correctness build; do not run Debug and Release concurrently. Benchmarks are separate and are not a
routine validation requirement.

The crate's deterministic suite covers:

- construction, duplicate collapse, first-representative retention, and full-hash collisions;
- every positional addition, movement, removal, range, reverse, and stable-sort operation;
- repeated midpoint insertions that exhaust sparse gaps and force relabel fallback;
- receiver-hasher normalization, receiver/argument representative precedence, algebra order, and
  all six relations;
- root sharing for every specified logical no-op and snapshot immutability for changed branches;
- invalid positions, absent movement, late iterator panic, comparator panic, and source preservation;
- dual-index diagnostics, `Send + Sync`, and concurrent snapshot readers; and
- a deterministic generated-history model with retained earlier versions.

When changing stamp selection, range reconciliation, or algebra, call `validate_structure` after
every generated step. An ordered result alone is insufficient evidence because a stale CHAMP stamp
can remain latent until a later movement or removal.
