# Rust Range-Update Sequence Test Map

- Created (UTC): 2026-07-15T10:22:25Z
- Repository HEAD: 83d2d4bc69d8c77980127695f656f0aa5ecf56bd
- Audience: Maintainers and reviewers of executable range-update parity evidence
- Scope: `tests/range_update_sequence.rs`

The integration suite consumes only the public crate API. It covers the tag-monoid and action laws,
including directional assignment/addition composition and value-distinct identities; an ordered,
noncommutative token-list measure; every small split/range boundary; edits through pending tags;
root and interior sharing; deterministic generated histories with retained-version branching; all
six user action/measure callback panic sites plus empty-measure initialization; eager source
materialization; independent iterators; and concurrent immutable readers.

The generated test uses a fixed local linear-congruential stream and a mutable `Vec<i64>` oracle.
It is reproducible, dependency-free, and checks logical values, complete measure, indexed reads, and
the recursive AVL/lazy-measure invariant after every command. These are semantic and structural
tests, not elapsed-time benchmarks.

Run the focused lane from `src/Rust`:

```powershell
.\test.ps1 -Workspace RangeUpdate
```

The wrapper enforces one Cargo job and one Rust test-harness thread. Do not overlap it with another
workspace toolchain, and do not treat a passing test run as benchmark evidence.
