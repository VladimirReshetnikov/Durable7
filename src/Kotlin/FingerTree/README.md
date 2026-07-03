# Kotlin FingerTree Family

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and reviewers of the Kotlin FingerTree-family port
- Scope: `tools.datastructures.fingertree` package

This workspace ports the repository FingerTree collection family to Kotlin/JVM. It exposes Kotlin
names for the public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, M>` over runtime `MeasurePolicy<T, M>` values, including built-in size, sum,
  min/max, and product policies;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, M>`, `TextRope`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

This is a semantic checkpoint port. It preserves immutable snapshot behavior, stable observable
ordering, rank/range semantics, stable equal-priority dequeue behavior, closed-interval overlap
semantics, and text line navigation. The current representation is an immutable JVM-value checkpoint
rather than the final C#/C++ lazy measured-spine implementation, so it does not claim asymptotic
parity for every operation.

Validate from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace FingerTree
```

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md).
