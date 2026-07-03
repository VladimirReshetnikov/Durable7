# Rust FingerTree Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust FingerTree-family workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
cargo test -p tools-data-structures-fingertree
```

If Cargo is available under rustup but not on `PATH`, use:

```powershell
& $env:USERPROFILE\.cargo\bin\cargo.exe test -p tools-data-structures-fingertree
```

The crate uses `#![forbid(unsafe_code)]`. Unit tests are inline in the module files under `FingerTree/src/` and
cover:

- persistent deque updates, subtree sharing, bounded-depth tree shape, splitting, concatenation, sorted search, and
  randomized model replay;
- reversible-deque O(1) storage-sharing reversal;
- measured sequence size/sum/min/max policies, split, and locate;
- sorted bag/set/map rank, navigation, algebra, and duplicate handling;
- stable priority dequeue and meld behavior;
- closed interval overlap, containment, and coalescing;
- positional rope edits, measured-rope navigation, text line helpers, and builder output.

The current validation proves the structurally shared Rust deque behavior and the checkpoint behavior of the
remaining facades, not final C#/C++ lazy-spine asymptotic parity for the whole crate.
