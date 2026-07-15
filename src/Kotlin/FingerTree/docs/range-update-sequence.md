# Kotlin Range-Update Sequence

- Status: Implemented and validated
- Created (UTC): 2026-07-15T10:52:11Z
- Repository HEAD: 626fc8685a85100db99b1c1deebfde7f0e988a77
- Audience: Kotlin consumers, maintainers, reviewers, and cross-language port authors
- Scope: Algebra, public surface, representation, bounds, persistence, and validation

`RangeUpdateSequence<T, M, Tag>` is an immutable indexed sequence whose contiguous ranges can be
transformed and remeasured without visiting their interiors. It is a sibling core in the Kotlin
FingerTree-family package, not a change to `PersistentMeasuredTree` or the existing deque/rope
facades. A path-copied implicit-key AVL tree provides indexed editing, splitting, joining, and
concatenation; `RangeUpdateAlgebra<T, M, Tag>` supplies the ordered measure monoid and lazy tag
action.

The capability claim is structural. A whole-sequence nonidentity update replaces one root in O(1).
An indexed edit, proper range update, proper range query, split, or concatenation visits or copies
O(log n) boundary nodes. The implementation and tests make no wall-clock or allocation-rate claim,
and no benchmark was run while establishing this checkpoint.

## Algebra

```kotlin
public interface RangeUpdateAlgebra<T, M, Tag> : MeasurePolicy<T, M> {
    public val identityTag: Tag
    public fun isIdentity(tag: Tag): Boolean
    public fun compose(newer: Tag, older: Tag): Tag
    public fun applyElement(tag: Tag, element: T): T
    public fun applyMeasure(tag: Tag, measure: M, count: Int): M
}
```

`compose(newer, older)` means apply `older` first and `newer` second. The parameter order is
observable for assignment/addition algebras: a newer assignment discards an older addition, while
a newer addition is folded into an older assignment. The policy must obey the measure-monoid laws,
make tags a monoid under that directional composition, agree between element and singleton-measure
actions, distribute the measure action over ordered combination, and map every tag accepted by
`isIdentity` to complete identity behavior. A pending tag is held in a separate wrapper, so `null`,
the default-like value of a tag type, and value-distinct identity tags are never confused with
absence.

Policies are expected to be deterministic, side-effect-free, and safe for the caller's concurrency
pattern. A throwing callback publishes no facade and cannot mutate an older version. Algebra laws
are semantic preconditions; the collection cannot prove them for arbitrary user code.

## Public surface

The Kotlin surface provides:

- `empty(algebra)` and eager, one-shot `from(values, algebra)` construction;
- `size`, `isEmpty`, `measure`, indexed `get`, `toList`, and independent iteration;
- `prepend`, element `append`, `insertAt`, `setItem`, and `removeAt`;
- `concat`, `splitAt`, and `getRange`;
- `applyRange` and `measureRange`;
- `validateStructure`, `sharesRootWith`, and test-facing `sharesStructureWith` diagnostics.

Invalid indices, boundaries, and ranges return `null`, matching the existing Kotlin workspace.
That convention makes a stored `null` element or nullable measure observationally ambiguous through
the corresponding nullable lookup alone; iteration/materialization and valid prechecked calls retain
the value exactly. `Math.addExact` rejects count growth beyond `Int.MAX_VALUE` before policy
callbacks or publication. Range validation uses `count <= size - start`, avoiding overflowing
`start + count` checks.

`concat` accepts operands retaining the same algebra object or algebra values that compare equal,
then uses the receiver's policy lineage. Empty concatenation returns the existing nonempty facade.
Same-lineage `from` returns an existing range-update sequence by identity. Endpoint splits retain
the source on the nonempty side; empty and identity range updates retain the exact source facade.

## Representation and lazy tags

Each node stores a logical value, left and right child references, cached height and count, cached
ordered logical measure, and an optional pending-tag wrapper. A node value and cached measure already
include its own pending tag; its child roots do not. Applying a tag to an entire subtree transforms
the root value and aggregate, composes the new tag after an older pending tag, and retains both child
references.

Before structural descent or rotation, immutable push applies a pending tag to each nonempty child
root and clears it on the rebuilt parent. Rotations therefore cannot strand an action above a child
that leaves its range. Indexed reads, range measurement, and iteration do not publish pushed nodes.
They carry an inherited tag down the traversal, composing an ancestor tag as the newer action over a
node-local pending tag. The current node value already includes its own pending tag and receives only
the inherited action when observed.

`measureRange` consumes cached measures for fully covered subtrees and measures only boundary node
values. `applyRange` splits at both boundaries, applies one lazy tag to the isolated middle root, and
joins the pieces. Split/join paths copy AVL spines; pushing may additionally replace an off-spine
child root while retaining that child's interior. No node reachable from an older facade is mutated.

## Complexity

| Operation | Worst-case structural bound |
| --- | --- |
| `size`, `isEmpty`, `measure` | O(1) |
| `from` | O(n) time and nodes |
| indexed read | O(log n), no persistent node allocation |
| point edit/remove/insert | O(log n) copied nodes |
| split/concat/nontrivial range extraction | O(log n) copied nodes |
| full nonidentity `applyRange` | O(1), one replacement root |
| proper `applyRange` | O(log n) copied nodes |
| empty/full `measureRange` | O(1) |
| proper `measureRange` | O(log n), no persistent node allocation |
| iteration | O(n) time and O(log n) traversal stack |

These bounds treat algebra callbacks as O(1). A policy whose `applyMeasure` scans its represented
elements adds its own cost and defeats the intended range-update capability.

## Validation evidence

The serialized `src/Kotlin/build.ps1 -Workspace FingerTree` gate compiles with one Kotlin backend
thread and runs one test JVM pinned to one active processor with Serial GC. Thirteen dedicated cases cover
directional affine composition, associativity and identity laws, value-distinct identities,
noncommutative ordered measures, every indexed/range/split boundary, replacement after lazy tags,
nullable elements/measures/tags, one-shot construction ordering, retryable iterator callbacks,
every reachable policy-callback failure ordinal, root/interior sharing, recursive AVL and cached-
measure validation, a 1,000-command branching retained-snapshot list model, exact `Int.MAX_VALUE`
shared-DAG overflow rejection, and bounded concurrent readers. The complete FingerTree executable
passes. Benchmarks remain postponed for an isolated machine run.
