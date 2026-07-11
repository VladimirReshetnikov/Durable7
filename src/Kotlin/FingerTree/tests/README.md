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
- `IntervalTree` closed overlap, containment, removal, and coalescing;
- `RrbVector` radix boundaries, regular/relaxed layouts, unequal-height and adversarial
  concatenation, exact leaf sharing, split/insert/remove/pop, 10,000 randomized edits with retained
  snapshots, append-builder isolation/caching, nullable elements, checked overflow, and concurrent
  readers;
- `DabaLite` exhaustive short histories with a noncommutative monoid, a 100,000-operation FIFO model,
  all four six-cursor fixups, exact combine ceilings, 64-slot chunk boundaries and churn, callback
  failure at every reachable mutation ordinal, atomic boundary rollback, O(1) clear/reuse,
  callback-free structural statistics, nullable identity, and prompt slot/chunk reclamation;
- `Rope`, `MeasuredRope`, `TextRope`, and `RopeBuilder` complete positional/range edits, copies,
  measure-guided navigation, compaction, snapshot retention, line/column mapping, and string conversion;
- shared measured-AVL invariants and identity sharing across deque, measured tree, sorted, priority,
  interval, rope, measured-rope, and text facades;
- a 5,000-command edit/split model and 100,000-element construction stress for the real-tree
  representation;
- the cross-language recurring-defect checklist: value-equal policy concatenation, stable priority
  comparator guards, comparison-based interval membership, equal-low insertion order, supplied-key
  sorted-map replacement, text-column overflow, and range-overflow rejection;
- JVM concurrent readers over shared immutable deque, reversible deque, rope, and measured-rope
  snapshots.
