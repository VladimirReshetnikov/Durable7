# Rust FingerTree Tests

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers navigating Rust FingerTree-family test coverage
- Scope: Test location, command, and coverage map

Tests currently live inline in the module files under [`../src`](../src). Run them from `src/Rust`:

```powershell
.\test.ps1 -Workspace FingerTree
```

Coverage groups:

- `deque.rs`: structurally shared persistent deque, cached endpoint-signpost validation, logarithmic sorted bounds,
  sorted split/equal-range/insert/remove operations (including custom ordering), model replay, subtree-sharing
  checks, and reversible deque O(1) mirrored views with reversible-typed results, logical iteration, and
  mixed-orientation concat/split/pop coverage;
- `measured.rs`: structurally shared measured sequence core, cached-measure validation, prefix locate, built-in
  measure policies, key lower/upper-bound splits, product-measure component splits, cumulative-weight selection,
  priority extraction helpers, and order-statistic count plus last-key measures;
- `sorted.rs`: sorted bag, set, and map facades with cached order-statistic measures, rank/key-boundary edits,
  streaming large-set algebra, inclusive value/key ranges, proper set relations, shared measured storage, and ranges;
- `priority_queue.rs`: stable minimum-priority queue, meld, cached minimum-priority measures, and shared-storage
  updates;
- `interval_tree.rs`: closed intervals, last-low/maximum-high measured overlap descent, a 100,000-interval
  sparse-hit regression, coalescing, and shared-storage updates;
- `rope.rs`: chunked positional and measured rope storage over cached count/user-measure summaries, chunk-copy
  construction, caller-supplied copy targets, structurally shared measured-rope point/range edits and slices,
  deterministic vector-model replay, append-builder measure tracking, and immutable snapshot isolation;
- `text_extras.rs`: Unicode-scalar addressing, UAX #29 extended grapheme segmentation and offset conversion,
  LF/CRLF/CR/mixed newline detection, and CRLF-aware line text over both character and text ropes.
- `lib.rs`: public `Send`/`Sync` assertions and spawned-thread readers over shared immutable deque,
  reversible deque, rope, and measured-rope snapshots.
