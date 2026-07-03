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
crate tests. It does not yet claim the C#/C++ lazy finger-tree asymptotic profile: several facades
currently rebuild `Arc`-shared vectors for updates. That makes the Rust surface useful and testable
now while leaving a clean replacement path for the real lazy measured spine.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract, checkpoint boundary, and evidence entry points.

Validate from `src/Rust`:

```powershell
cargo test -p tools-data-structures-fingertree
```
