# Kotlin FingerTree API Notes

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers implementing and reviewing the Kotlin FingerTree-family port
- Scope: Kotlin naming, contracts, checkpoint limitations, and intentional differences

Current public families:

- `PersistentDeque<T>` and `ReversibleDeque<T>`;
- `FingerTree<T, M>` over `MeasurePolicy<T, M>`;
- built-in policies `SizeMeasure<T>`, `IntSumMeasure`, `MaxMeasure<T>`, `MinMeasure<T>`, and
  `ProductMeasure<T, A, B>` with `MeasurePair<A, B>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>` and `PriorityEntry<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, M>`, `TextRope`, `RopeBuilder`, `NewlineMeasure`, and `LineColumn`.

The Kotlin surface follows Kotlin/JVM conventions:

- fallible indexed operations return `null`;
- duplicate sorted-map insertion throws `SortedDuplicateKeyException` for `insert` and returns
  `SortedAddResult` for `tryInsert`;
- measure policies are runtime objects with identity, element measure, and combine operations;
- sorted and priority facades accept JVM `Comparator` values where natural ordering is not enough;
- text offsets are Kotlin `Char` offsets, matching the repository's `Rope<char>` interpretation.

This workspace is a semantic checkpoint, not the final lazy finger-tree representation. It preserves
immutable snapshot behavior and the observable behavior covered by the executable tests. The current
facades use immutable Kotlin lists and comparator-guided algorithms, so future representation work can
replace the internals with a lazy measured spine without changing the Kotlin-facing surface.
