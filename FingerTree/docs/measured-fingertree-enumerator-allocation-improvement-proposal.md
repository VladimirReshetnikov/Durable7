# Improvement Proposal: General Measured Finger Tree Enumerator Materializes A List

- Created (UTC): 2026-06-30T18:02:49Z
- Repository HEAD: b35dff6f0a8db32c6d957811fd1ba61f0ae20842
- Status: Proposed
- Audience: Maintainers of the C# `FingerTree<TElement, TMeasure, TMeasureOps>` public API
- Scope: Enumeration allocation behavior of the general measured finger tree

## Summary

The public C# general measured finger tree currently implements `GetEnumerator()` by flattening the whole tree into
a `List<TElement>` and returning the list enumerator. This is documented in `FingerTree.cs` as O(n) time and space,
so it is not a hidden correctness defect. It is, however, a performance and allocation shortcoming relative to the
same repository's tuned `FingerTreeDeque<T>.Enumerator`, which traverses with an explicit O(log n) stack and avoids
materializing the collection.

## Evidence

Reviewed source:

- `FingerTree/src/Tools.DataStructures.FingerTree/FingerTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredElements.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTree.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/Internal/TreeElement.cs`
- `FingerTree/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs`

`FingerTree<TElement, TMeasure, TMeasureOps>.GetEnumerator()` does this:

```csharp
var list = new List<TElement>();
_root.Flatten(list);
return list.GetEnumerator();
```

The tuned deque instead exposes tree and node blocks through `IEnumerationBlock<T>` and its struct enumerator keeps
an explicit traversal stack. That gives O(n) total enumeration with O(log n) traversal storage rather than O(n)
materialization storage.

## Impact

For one-off small enumerations, the current implementation is simple and acceptable. For large measured trees or
collection wrappers built on the measured core, enumeration allocates an additional list proportional to the full
element count before yielding the first item. This can matter for:

- streaming a large priority/ordered/positional measured tree;
- repeated enumeration in hot paths;
- memory-sensitive consumers that expect immutable collection enumeration to be incremental;
- parity with `FingerTreeDeque<T>`, which already demonstrates the lower-allocation traversal pattern.

## Proposed Improvement

Introduce an internal measured-tree enumeration block contract analogous to the tuned deque's `IEnumerationBlock<T>`.
Measured tree levels and measured nodes can expose their direct children in left-to-right order:

- a leaf child yields a stored `TElement`;
- a measured node or tree child pushes another block onto the enumerator stack;
- a deep tree enumerates prefix children, forced middle, and suffix children.

Then replace the public general measured `GetEnumerator()` with a struct enumerator that keeps an expandable stack
of these blocks, mirroring `FingerTreeDeque<T>.Enumerator`.

## Expected Result

The public semantics remain unchanged: enumeration still yields all elements left to right over the immutable
snapshot. The complexity contract improves from O(n) extra materialization space before first yield to O(log n)
active traversal storage with O(1) amortized work per yielded element, plus any one-time forcing of suspended middle
subtrees on the traversal path.

## Compatibility

This is behavior-preserving for consumers. The only observable differences should be improved allocation behavior
and earlier delivery of the first element. Existing tests that assert enumeration order should continue to pass.
Allocation-focused tests can be added after implementation, similar to the deque enumeration/copy tests.
