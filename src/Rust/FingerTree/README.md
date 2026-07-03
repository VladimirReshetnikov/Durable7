# Rust FingerTree Family

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of the Rust FingerTree-family port
- Scope: Public crate shape, checkpoint semantics, and validation entry point

`tools-data-structures-fingertree` is the Rust checkpoint port for the repository's FingerTree
family. It exposes Rust-native names for the same public families:

- `PersistentDeque<T>`;
- `FingerTree<T, P>` over a `MeasurePolicy<T>`;
- `ReversibleDeque<T>`;
- `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>`;
- `PriorityQueue<T, P>`;
- `Interval<T>` and `IntervalTree<T>`;
- `Rope<T>`, `MeasuredRope<T, P>`, `TextRope`, and `RopeBuilder`.

This checkpoint preserves immutable snapshot semantics and the observable behavior covered by the
crate tests. `PersistentDeque<T>` now uses structurally shared balanced tree storage,
`ReversibleDeque<T>` is an orientation layer over that shared deque, and `Rope<T>` now uses chunked
length-measured storage over the shared measured tree. `TextRope` inherits that positional rope
storage for character content. `FingerTree<T, P>` now uses structurally shared measured tree storage
with cached monoid measures, and `MeasuredRope<T, P>` composes that measured core with a counted
user measure. `PriorityQueue<T, P>` now composes the measured core with cached minimum-priority
measures, and `IntervalTree<T>` uses cached maximum-high interval measures for overlap and
containment entry points. `SortedBag<T>`, `SortedSet<T>`, and `SortedMap<K, V>` now use
order-statistic measured tree storage with cached count plus last-key measures. The crate still does
not claim the C#/C++ lazy finger-tree asymptotic profile overall; derived algorithms remain
semantic-checkpoint implementations until the lazy measured spine is ported through the whole family.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract, checkpoint boundary, and evidence entry points.

Validate from `src/Rust`:

```powershell
cargo test -p tools-data-structures-fingertree
```
