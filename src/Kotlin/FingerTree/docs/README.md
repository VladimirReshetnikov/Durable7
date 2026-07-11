# Kotlin FingerTree Docs

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and reviewers of the Kotlin FingerTree workspace
- Scope: Documentation index for `src/Kotlin/FingerTree`

The Kotlin FingerTree workspace is a structurally shared measured-tree port of the repository
FingerTree family. Public facades share a persistent measured AVL sequence with cached size/measure;
the API notes document its complexity and the deliberate strict-AVL versus lazy-digit-spine engine
difference.

## Current Documents

- [API notes](api-notes.md) describe `PersistentDeque<T>`, `ReversibleDeque<T>`,
  `FingerTree<T, M>`, `RrbVector<T>`, `CanonicalSortedSet<T>`, `DabaLite<T>`, measure/monoid policies, sorted facades,
  priority queues, interval trees, ropes, text helpers, Kotlin result shapes, comparator policy
  behavior, measured-tree/RRB persistence, and complexity.
- [Validation](validation.md) records the `src/Kotlin/build.ps1` command shape, local JDK and Kotlin
  compiler bootstrap behavior, generated-output locations, and executable-test coverage boundary.
- [Tests README](../tests/README.md) maps deterministic coverage for deque operations, reversible
  orientation, measured splits, sorted facades, priority queues, intervals, ropes, measured ropes, text
  helpers, RRB vectors/builders, canonical zip-zip policies/trees, and adversarial DABA Lite schedules.

## Related Repository Docs

- [Data-structure catalog](../../../../docs/reference/data-structure-catalog.md#finger-tree-core-and-deque)
  lists the Kotlin FingerTree-family public surface beside the sibling ports.
- [Semantic contracts](../../../../docs/reference/semantic-contracts.md#finger-tree-core) summarizes the
  shared contracts for measured trees, facades, ropes, and text helpers.
- [Reversible deque complexity audit](../../../../docs/reference/reversible-deque-complexity-audit.md)
  records the cross-language orientation-aware deque checks.
- [Porting and semantic parity](../../../../docs/guides/porting-and-semantic-parity.md#fingertree-specific-checks)
  gives the cross-language checklist for FingerTree-family changes.
