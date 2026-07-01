# Defect Report: IntervalTree Documentation Calls The Measured Core Strict

- Status: Fixed 2026-07-01
- Created (UTC): 2026-06-30T17:21:46Z
- Repository HEAD: d140fb07d8ae21726e96b9ad916154c3bf87411d
- Audience: Maintainers of the C# FingerTree workspace
- Scope: Misleading XML documentation wording in `IntervalTree<T>`

## Summary

`IntervalTree<T>` has an XML documentation sentence that says its amortized bounds are "matching the underlying
strict measured tree." The word "strict" is misleading: the underlying general measured finger tree is explicitly
the lazy-memoized measured core. The implementation is in a strict language, but the data structure itself uses
memoized suspensions in its deep middle spine and lazy measure boxes.

This was a documentation wording defect only. I did not find evidence of a runtime or test defect while
cross-checking the measure and predicate layer for the C++ port. It was fixed by changing the XML documentation to
describe the lazy-memoized measured finger tree directly.

## Evidence

- `FingerTree/src/Tools.DataStructures.FingerTree/IntervalTree.cs` formerly contained the phrase:
  "Bounds are amortized for ephemeral use, matching the underlying strict measured tree."
- `FingerTree/README.md` describes the shared general measured core as holding each deep node's middle subtree
  behind a memoized suspension and computing measures lazily.
- `FingerTree/docs/api-specification.md` describes `FingerTree<TElement, TMeasure, TMeasureOps>` as a
  lazy-memoized strict-language realization of Hinze and Paterson's lazy finger tree, and explicitly states that
  the amortized bounds hold under fully persistent branching histories.

## Impact

The wording can confuse implementers and reviewers in two ways:

- It suggests `IntervalTree<T>` is backed by a fully strict measured-tree representation, which would not have the
  same branching-persistence amortization story as the documented lazy-memoized core.
- It can obscure the distinction between "implemented in strict C#" and "strict data structure."

This is especially relevant during the C++ port, where strict versus lazy internal representation has direct
memory-model and complexity consequences.

## Suggested Fix

Replace the phrase with wording that points at the lazy-memoized measured core, for example:

```text
Bounds follow the underlying lazy-memoized measured finger tree.
```

or, if the sentence wants to emphasize the host-language adaptation:

```text
Bounds follow the underlying measured finger tree's strict-language lazy-memoization strategy.
```

No public API or test behavior should need to change.
