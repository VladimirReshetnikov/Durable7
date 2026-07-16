# Kotlin FingerTree Tests

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers navigating Kotlin FingerTree-family test coverage
- Scope: Test location, command, and coverage map

Tests live in [`../test`](../test) and are compiled into a dependency-free executable by the Kotlin
root build script. Run them from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace FingerTree
```

Coverage groups:

- `PersistentDeque` snapshot preservation, splitting, and endpoint edits;
- `ReversibleDeque` logical orientation, storage sharing across whole-deque reverse, mixed-orientation
  concatenation, split/rejoin behavior, endpoint views, and larger mixed concat histories;
- `FingerTree` runtime measure policies, prefix measures, splits, and locate;
- `SortedBag`, `SortedSet`, and `SortedMap` ordering, duplicate/range behavior, navigation, duplicate insert
  rejection, and comparator-aware last-wins bulk construction;
- counting-comparator guards over 65,536-element sorted collections proving bag counting bounds,
  set rank/neighbor navigation, and keyed map lookup each finish within one logarithmic descent;
- `PriorityQueue` stable minimum-priority dequeue and meld;
- `BrodalOkasakiHeap` fused-boundary/rank invariants, 4,096-element adversarial drains, a
  20,000-operation branching retained multiset, comparer-equivalent representatives, exact
  comparator-identity/no-op sharing, nullable minimums, logarithmic identity retention, comparison
  ceilings through 65,536 elements, and concurrent immutable readers;
- `PrioritySearchQueue` replacement and first-key semantics, all AVL rotation/deletion paths, a
  50,000-key ascending stress, a 20,000-operation retained keyed model, winner/key-order ties,
  exact no-op and far-subtree identity, range/threshold pruning comparison equations, invalid
  ranges, nullable key/priority/payload result semantics, and concurrent immutable readers;
- `IntervalTree` closed overlap, containment, removal, and coalescing;
- `PersistentIntervalMap` exact payload replacement, retained snapshots, overlap enumeration/count,
  removal, and cross-index validation;
- `RrbVector` radix boundaries, regular/relaxed layouts, unequal-height and adversarial
  concatenation, exact leaf sharing, split/insert/remove/pop, 10,000 randomized edits with retained
  snapshots, append-builder isolation/caching, nullable elements, checked overflow, and concurrent
  readers;
- `DabaLite` exhaustive short histories with a noncommutative monoid, a 100,000-operation FIFO model,
  all four six-cursor fixups, exact combine ceilings, 64-slot chunk boundaries and churn, callback
  failure at every reachable mutation ordinal, atomic boundary rollback, O(1) clear/reuse,
  callback-free structural statistics, nullable identity, and prompt slot/chunk reclamation;
- `RangeUpdateSequence` directional affine laws, value-distinct identities, noncommutative measures,
  lazy-tag point/range edits, nullable elements/measures/tags, exact no-op and off-root sharing,
  every policy-callback failure ordinal plus iterator retry, a 1,000-command branching retained list
  model with recursive validation, exact-`Int` shared-DAG overflow, and bounded concurrent readers;
- `CanonicalSortedSet` bulk/incremental permutation and delete/reinsert topology convergence, a
  15,000-command retained-snapshot model, bulk first-representative and nullable-lookup semantics,
  exact keyed/public-seed HMAC derivation, hidden-key and same-seed policy behavior, unsigned
  secondary priority ordering, comparer/hash coherence rejection, and receiver-comparer equality
  asymmetry plus every set relation. Canonical algebra, no-op identity, quantified add/remove
  off-path sharing, fully colliding 4,096-node stack safety, digest inequality short-circuit,
  concurrent digest publication, and injected-metadata validator rejection have dedicated checks;
- `Rope`, positional `RopeCursor`, `MeasuredRope`, `MeasuredRopeCursor`, `TextRope`,
  `TextRopeCursor`, and `RopeBuilder`: complete positional/range edits, cursor empty/start/end and
  nullable-peek behavior, identity no-ops, one-shot range capture, equality-free replacement,
  retained branches, positional and measured 750-command gap models, ordered noncommutative
  before/after measures, nullable aggregate retention, absolute prefix-search hit/miss/empty behavior,
  callback-failure retry,
  positional and measured exact-`Int.MAX_VALUE` shared-DAG overflow, copies, compaction, exact text-
  facade snapshots, UTF-16 line/column mapping across surrogate/combining/CRLF content, and string
  conversion;
- shared measured-AVL invariants and identity sharing across deque, measured tree, sorted, priority,
  interval, rope, measured-rope, and text facades;
- a 5,000-command edit/split model and 100,000-element construction stress for the real-tree
  representation;
- the cross-language recurring-defect checklist: value-equal policy concatenation, stable priority
  comparator guards, comparison-based interval membership, equal-low insertion order, supplied-key
  sorted-map replacement, text-column overflow, and range-overflow rejection;
- JVM concurrent readers over shared immutable deque, reversible deque, rope, positional/measured/text
  cursors, and measured-rope snapshots.
