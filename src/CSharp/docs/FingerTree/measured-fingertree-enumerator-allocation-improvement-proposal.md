# Improvement Proposal: General Measured Finger Tree Enumerator Materializes A List

- Created (UTC): 2026-06-30T18:02:49Z
- Repository HEAD: b35dff6f0a8db32c6d957811fd1ba61f0ae20842
- Status: Implemented 2026-07-01
- Audience: Maintainers of the C# `FingerTree<TElement, TMeasure, TMeasureOps>` public API
- Scope: Enumeration allocation behavior of the general measured finger tree

## Summary

The public C# general measured finger tree used to implement `GetEnumerator()` by flattening the whole tree into a
`List<TElement>` and returning the list enumerator. That was documented in `FingerTree.cs` as O(n) time and space,
so it was not a hidden correctness defect. It was, however, a performance and allocation shortcoming relative to the
same repository's tuned `FingerTreeDeque<T>.Enumerator`, which traverses with an explicit O(log n) stack and avoids
materializing the collection. The C# implementation now uses the same stack-enumerator pattern for the general
measured tree.

## Evidence

Reviewed source:

- `src/CSharp/src/Tools.DataStructures.FingerTree/FingerTree.cs`
- `src/CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredElements.cs`
- `src/CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTree.cs`
- `src/CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs`
- `src/CSharp/src/Tools.DataStructures.FingerTree/Internal/TreeElement.cs`
- `src/CSharp/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs`

Before the fix, `FingerTree<TElement, TMeasure, TMeasureOps>.GetEnumerator()` did this:

```csharp
var list = new List<TElement>();
_root.Flatten(list);
return list.GetEnumerator();
```

The tuned deque instead exposes tree and node blocks through `IEnumerationBlock<T>` and its struct enumerator keeps
an explicit traversal stack. That gives O(n) total enumeration with O(log n) traversal storage rather than O(n)
materialization storage.

The measured tree now follows that pattern: measured elements and tree levels expose enumeration blocks, and the
public enumerator walks them with an explicit frame stack.

## Impact

For one-off small enumerations, the former implementation was simple and acceptable. For large measured trees or
collection wrappers built on the measured core, it allocated an additional list proportional to the full element
count before yielding the first item. That could matter for:

- streaming a large priority/ordered/positional measured tree;
- repeated enumeration in hot paths;
- memory-sensitive consumers that expect immutable collection enumeration to be incremental;
- parity with `FingerTreeDeque<T>`, which already demonstrates the lower-allocation traversal pattern.

## Implemented Improvement

The implementation introduces an internal measured-tree enumeration block contract analogous to the tuned deque's
`IEnumerationBlock<T>`.
Measured tree levels and measured nodes can expose their direct children in left-to-right order:

- a leaf child yields a stored `TElement`;
- a measured node or tree child pushes another block onto the enumerator stack;
- a deep tree enumerates prefix children, forced middle, and suffix children.

The public general measured `GetEnumerator()` now returns a struct enumerator that keeps an expandable stack of
these blocks, mirroring `FingerTreeDeque<T>.Enumerator`.

## Expected Result

The public semantics are unchanged: enumeration still yields all elements left to right over the immutable snapshot.
The complexity contract improved from O(n) extra materialization space before first yield to O(log n) active
traversal storage with O(1) amortized work per yielded element, plus any one-time forcing of suspended middle
subtrees on the traversal path.

## Compatibility

This is behavior-preserving for consumers. The only observable differences should be improved allocation behavior
and earlier delivery of the first element. Existing tests that assert enumeration order continue to pass; an
allocation-focused regression test now verifies the first yield does not materialize the entire tree.
