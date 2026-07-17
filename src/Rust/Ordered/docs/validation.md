# Rust Ordered Collections Validation

- Created (UTC): 2026-07-15T00:00:00Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers and reviewers validating the Rust ordered set/map port
- Scope: Serialized build/test gate and invariant evidence

Run the focused gate from `src/Rust`:

```powershell
.\test.ps1 -Workspace Ordered
```

The wrapper forces one Cargo build job and one Rust test thread. Use `-Release` only for a release
correctness build; do not run Debug and Release concurrently. Benchmarks are separate and are not a
routine validation requirement.

## Current derived-structure evidence

On 2026-07-17, the focused ordered-multimap suite passed 6/6 tests, in addition to the prior 6/6
ordered-map suite. The serialized full Rust workspace subsequently passed in both Debug and
Release, including all Ordered unit and documentation tests. Benchmarks were not run.

The crate's deterministic suite covers:

- ordered-map first-key/last-value construction, strict and replacing updates, positional insertion
  and movement, keyed/positional removal, ranges, reversal, stable sorting, sparse-gap relabeling,
  root sharing, retained branches, and dual-index invariants;
- ordered-multimap grouped key/value order, duplicate suppression, independent hash policies,
  representative retention, pair and group removal, empty-group contraction/reappend, root-sharing
  no-ops, retained versions, and nested invariants;

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
