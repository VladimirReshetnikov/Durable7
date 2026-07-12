# Rust FingerTree Documentation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of `tools-data-structures-fingertree`
- Scope: Documentation index for the Rust FingerTree-family workspace

The Rust FingerTree crate is a safe Rust checkpoint for the repository FingerTree family. Its
persistent public facades use structurally shared Rust storage where implemented and preserve
immutable snapshot behavior. DABA Lite is the documented single-threaded mutable exception; the
remaining persistent-family lazy-spine asymptotic parity boundary stays explicit.

## Current Documents

- [Brodal-Okasaki heap](brodal-okasaki-heap.md) records the direct bootstrapped skew-binomial
  representation, ordering-policy identity, ownership/result shapes, complexity, diagnostics, and
  adversarial validation evidence.
- [Priority-search queue](priority-search-queue.md) records the winner-cached AVL representation,
  first-key/last-value semantics, exact no-ops, borrowed pruning iterator, ownership/result shapes,
  and validation evidence.
- [API notes](api-notes.md) describe `PersistentDeque<T>`, `ReversibleDeque<T>`,
  `DabaLite<T, M>`, `DabaMonoid<T>`, `RrbVector<T>`, `CanonicalSortedSet<T>`,
  `ZipTreeRankPolicy<T>`, `FingerTree<T, P>`, `MeasurePolicy<T>`,
  built-in and product measures, sorted
  facades, priority queues, interval trees, ropes, measured ropes, text helpers, Rust result shapes,
  and checkpoint limitations.
- [Validation](validation.md) records the Cargo command, local rustup fallback path, safe-Rust boundary,
  and coverage map for the checkpoint behavior.
- [Tests README](../tests/README.md) maps unit coverage for deque operations, reversible orientation,
  DABA Lite state-machine and failure atomicity, RRB representation invariants, canonical zip-tree
  policy/topology/persistence, Brodal heaps, priority-search queues, measured splits, sorted facades,
  measured priority queues, intervals, ropes, measured ropes, and text helpers.

## Related Repository Docs

- [Data-structure catalog](../../../../docs/reference/data-structure-catalog.md#finger-tree-core-and-deque)
  lists the Rust FingerTree-family public surface beside the sibling ports.
- [Semantic contracts](../../../../docs/reference/semantic-contracts.md#finger-tree-core) summarizes the
  shared contracts for measured trees, facades, ropes, and text helpers.
- [Reversible deque complexity audit](../../../../docs/reference/reversible-deque-complexity-audit.md)
  records the cross-language orientation-aware deque checks.
- [Porting and semantic parity](../../../../docs/guides/porting-and-semantic-parity.md#fingertree-specific-checks)
  gives the cross-language checklist for FingerTree-family changes.
