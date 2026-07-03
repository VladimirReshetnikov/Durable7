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
  O(1) mirrored views with mixed-orientation concat/split/pop coverage;
- `measured.rs`: structurally shared measured sequence core, cached-measure validation, prefix locate, built-in
  measure policies, key lower/upper-bound splits, product-measure component splits, cumulative-weight selection,
  priority extraction helpers, and order-statistic count plus last-key measures;
- `sorted.rs`: sorted bag, set, and map facades with cached order-statistic measures, rank/key-boundary edits,
  inclusive value/key ranges, proper set relations, shared measured storage, and ranges;
- `priority_queue.rs`: stable minimum-priority queue, meld, cached minimum-priority measures, and shared-storage
  updates;
- `interval_tree.rs`: closed intervals, overlap queries, cached maximum-high measures, coalescing, and
  shared-storage updates;
- `rope.rs`: chunked positional and measured rope storage over cached count/user-measure summaries, chunk-copy
  construction, caller-supplied copy targets, cached-newline text helpers, Rust string/display conversions, and
  builder conveniences.
