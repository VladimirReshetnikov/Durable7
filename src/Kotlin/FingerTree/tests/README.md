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
- `SortedBag`, `SortedSet`, and `SortedMap` ordering, duplicate/range behavior, navigation, and
  duplicate insert rejection;
- `PriorityQueue` stable minimum-priority dequeue and meld;
- `IntervalTree` closed overlap, containment, removal, and coalescing;
- `Rope`, `MeasuredRope`, `TextRope`, and `RopeBuilder` positional edits, copies, measure-guided
  navigation, line/column mapping, and string conversion;
- JVM concurrent readers over shared immutable deque, reversible deque, rope, and measured-rope
  snapshots.
