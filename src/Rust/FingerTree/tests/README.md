# Rust FingerTree Tests

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers navigating Rust FingerTree-family test coverage
- Scope: Test location, command, and coverage map

Tests currently live inline in the module files under [`../src`](../src). Run them from `src/Rust`:

```powershell
cargo test -p tools-data-structures-fingertree
```

Coverage groups:

- `deque.rs`: structurally shared persistent deque, model replay, subtree-sharing checks, and reversible deque
  orientation facade;
- `measured.rs`: structurally shared measured sequence core, cached-measure validation, prefix locate, and built-in
  measure policies;
- `sorted.rs`: sorted bag, set, and map facades with shared-storage rank edits and ranges;
- `priority_queue.rs`: stable minimum-priority queue, meld, and shared-storage updates;
- `interval_tree.rs`: closed intervals, overlap queries, coalescing, and shared-storage updates;
- `rope.rs`: structurally shared positional and measured ropes, text helpers, and builder.
