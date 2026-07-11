# Kotlin FingerTree API Notes

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers implementing and reviewing the Kotlin FingerTree-family port
- Scope: Kotlin naming, contracts, measured-tree representation, complexity, and intentional differences

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
- `SortedMap.from(values, comparator)` provides comparator-aware bulk construction and keeps the last supplied
  entry, including its key instance, from every comparator-equal run;
- text offsets are Kotlin `Char` offsets, matching the repository's `Rope<char>` interpretation.

## Representation and complexity

`PersistentMeasuredTree<T, M>` is the shared internal engine: an immutable AVL sequence whose nodes
cache subtree size, height, and the order-sensitive monoidal measure. Construction builds a balanced
tree in O(n). Indexing, path-copying updates, insertion/removal, prefix measurement, measure-guided
location, and splits are O(log n); concatenation joins trees by height and retains unchanged nodes.
The public `sharesStorageWith` diagnostics report shared node identity, and executable tests validate
the AVL bound after generated histories and 100,000-element construction.

`PersistentDeque<T>` and `FingerTree<T, M>` use that engine directly. `MeasuredRope` exposes front/back,
endpoint and positional insertion, range insertion/removal, replacement, slicing, splitting, concatenation,
copying, and compaction over the measured engine; every result retains the supplied measure policy and cached
aggregate. `SortedBag`, `SortedSet`, and
`SortedMap` use `PersistentDeque`; comparator-guided binary bounds are O(log² n) because each of the
O(log n) comparisons performs tree indexing, while the resulting edit is O(log n). `Rope` uses the
same positional tree. `MeasuredRope` caches its supplied measure, and `TextRope` is newline-measured
rather than string-backed. Full enumeration, conversion, sorting/filter rebuilding, and
`PersistentDeque.reverse()` are O(n).

`PriorityQueue` caches the stable leftmost minimum entry, making peek O(1), enqueue/meld O(log n), and
dequeue O(log n). `IntervalTree` caches last-low and maximum-high summaries: lower-bound insertion and
the first overlap query are O(log n), while overlap enumeration repeatedly prunes unreachable
prefixes. Measure or comparator policies on concatenated values must compare equal. `meld` compares
comparators by identity (JVM `Comparator` has no structural equality): natural-order queues share one
stdlib singleton and always meld, but queues built with custom comparators must be constructed from a
single shared comparator instance.

`ReversibleDeque<T>` retains its specialized orientation-aware balanced storage: `reverse()` wraps or
unwraps a root in O(1), `concat` joins logical roots without materializing either operand, and endpoint
views, indexing, and splits navigate logical orientation. The general engine is a strict measured AVL
sequence rather than the C#/C++ lazy Hinze–Paterson digit spine; consequently its endpoint operations
are O(log n), not amortized O(1). No public facade retains the former flat-`List` representation.
