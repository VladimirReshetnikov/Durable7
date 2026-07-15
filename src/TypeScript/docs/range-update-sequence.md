# TypeScript range-update sequence

- Created (UTC): 2026-07-15T05:31:49Z
- Repository HEAD: b0256f3bfe3b227e7872a82958b8eaa592e63d6f
- Scope: TypeScript API, algebra, representation, failure, diagnostics, and runtime mapping

`RangeUpdateSequence<Element, Measure, Tag>` is the strict TypeScript port of the independently
implemented C# range-update sequence. It is an immutable, indexed, ordered sequence with cached
monoid measures and algebraic lazy updates over contiguous ranges. It does not wrap the general
measured finger tree: its implementation is a path-copied implicit AVL tree whose lazy-tag invariant
is specific to this collection.

The [C# range-update contract](../../CSharp/docs/FingerTree/range-update-sequence.md) remains the
normative cross-port semantic reference. This document records the runtime policy mapping and the
intentional JavaScript differences.

## Algebra policy

TypeScript has no static-interface operations corresponding to the C# `TOps` parameter. Every
sequence instead retains one exact `RangeUpdateAlgebra<Element, Measure, Tag>` object:

```ts
interface RangeUpdateAlgebra<Element, Measure, Tag>
  extends MeasurePolicy<Element, Measure> {
  readonly identityTag: Tag;
  isIdentity(tag: Tag): boolean;
  compose(newer: Tag, older: Tag): Tag;
  applyElement(tag: Tag, element: Element): Element;
  applyMeasure(tag: Tag, measure: Measure, count: number): Measure;
}
```

`compose(newer, older)` means applying `older` first and `newer` second. Tags must form a monoid and
act consistently on elements and cached measures. The action must agree on singleton measures,
distribute over ordered `combine` with the supplied left/right counts, and map the empty measure at
count zero to itself. `isIdentity` is authoritative: every value it recognizes must obey the full
identity laws, even when that value is not the same JavaScript object as `identityTag`.

Policy operations are expected to be deterministic, pure, and lawful. The sequence cannot establish
these laws dynamically. A policy violation produces unspecified logical results.

The exact policy object is semantic state. Empty sequences are canonical per policy object, factory
identity shortcuts require that same object, and `concat` rejects a sequence retaining another
policy object. A `WeakMap`-backed context caches the policy's empty measure once and never treats
`identityTag` as a storage sentinel.

## Public sequence API

The TypeScript surface provides `empty`, `create`, `createRange`, and the idiomatic `from` alias;
`size` and cross-port `count`; cached `measure`; throwing `get`; `prepend`, `append`, `insert`,
`setItem`, and `removeAt`; `concat`, `splitAt`, and `getRange`; `applyRange` and `measureRange`;
iteration and `toArray`; plus `validateStructure` and `sharedNodeCount` diagnostics.

`create` accepts a dense readonly array, rejects a sparse array before policy callbacks, and builds a
minimal-height AVL in O(n). `createRange`/`from` enumerate a source exactly once and finish that
enumeration before element measurement begins. Passing a `RangeUpdateSequence` retaining the exact
requested algebra returns that immutable source directly.

Indices and counts must be finite integers. Element indices are in `[0, size)`, boundary indices are
in `[0, size]`, and ranges use an index plus count. Range validation checks the index's sign and
integrality, then the count's sign and integrality, then the upper index boundary, and finally
`count > size - index`; it never relies on unchecked `index + count`. Stored counts are capped at
`2^31 - 1` to match the reference's `Int32` structural contract.

An empty slice returns the policy's canonical empty sequence and a full slice returns the receiver.
Endpoint splits retain the receiver on the nonempty side without pushing a pending root. Empty
concatenation retains the nonempty operand. `setItem` is unconditional because the core has no
element-equality policy; replacing an element with the identical object still constructs the
specified persistent edit. The API intentionally has no named `addRange` or `assignRange`; those are
ordinary user-defined tags.

## Node and lazy-tag invariant

Every immutable node stores a value, left and right children, exact height and count, cached ordered
logical measure, a pending-presence bit, and a separate tag field. The presence bit is the only
absence representation. Consequently, stored `undefined` elements and even an `undefined` tag are
unambiguous.

For every node:

- the node value and cached measure already include its pending tag;
- its children do not yet include that tag;
- the cached measure is the logical ordered combination of the left subtree, value, and right
  subtree; and
- count, height, and AVL balance metadata are exact.

Applying a tag to an entire subtree transforms only its root value and cached measure, composes the
new tag over an older pending tag, and retains the children and metadata. When composition produces
a recognized identity, the pending marker is removed while the already transformed value and
measure are retained. Applying a nonidentity tag to the entire sequence therefore takes O(1) and
allocates one node wrapper plus the returned facade.

Structural descent pushes a pending tag immutably to both child roots before editing, splitting,
joining, or rotating. Rotations push both their root and pivot. Newly inserted and replacement
elements are current logical values: tags applied to the older sequence do not transform them.

Read-only descent never pushes. Indexed lookup, range measurement, and iteration carry an explicit
inherited-tag presence/value pair. An ancestor tag is newer than a node-local pending tag, so child
inheritance composes the ancestor tag first: `compose(inherited, local)`. The current node value has
already received its local pending tag and receives only the inherited tag. Fully covered range
queries use a cached subtree measure transformed only by inheritance. No read allocates persistent
nodes or installs a mutable cache.

## Persistence, sharing, and failures

All updates are path copies. Every input version remains usable and unaffected subtrees retain node
identity. Pushing can allocate replacement roots for both children of a tagged node, including an
off-spine child, but those wrappers retain the child's interior.

Structural count checks happen before user measure callbacks. Public arguments are validated before
tag or measure callbacks, and concatenation checks its resulting count before traversing either
operand. A throwing enumerator, algebra callback, or checked structural operation publishes no
partial sequence and cannot mutate any input. As with other JavaScript libraries, no recoverability
claim is made for process-level out-of-memory termination.

## Diagnostics and runtime-specific iteration

`validateStructure` recursively verifies counts, heights, AVL balance, pending-tag canonicalization,
and cached logical measures. When a node has a pending tag, validation applies that tag to its child
measures before reconstructing the node measure. It returns count/node-count/height, maximum absolute
balance, pending-node count, and maximum pending depth. Callers whose measures are not compared
correctly by `Object.is` supply an explicit equality function. `sharedNodeCount` reports exact
cross-version node sharing and also requires policy identity.

The source tree has a non-exported synchronous observer used by tests to count node visits and
allocations, facade allocations, rotations, pushes, subtree applications, and each policy callback
kind. It is not re-exported through the package subpath.

JavaScript iterators are independent reference objects bound to an immutable source snapshot. They
preserve logical order, lazy-tag semantics, and stored `undefined`, but do not emulate the C# struct
enumerator's copy-divergence, boxing, `Current`, or `Reset` behavior. Likewise, immutable snapshots
are safe to interleave within an isolate, but this port makes no same-object multi-thread or lock-free
progress claim: ordinary JavaScript object graphs are not shared directly between workers.

## Complexity

- State inspection and full-sequence measure: O(1).
- Construction: O(n) time, O(n) persistent nodes, O(log n) recursion.
- Whole nonidentity update: O(1), one replacement node.
- Indexed access, point edits, split, concatenation, slice, proper range update, and proper range
  query: O(log n) worst-case.
- Iteration: O(n) total with an O(log n) mutable iterator stack and no persistent-node allocation.
- Persistent storage: O(n), with O(log n) path copying for ordinary edits.

## Focused validation

The dedicated suites cover algebra laws and a noncommutative measure; exhaustive small boundaries;
nested assign/add/affine updates; point edits through tags; all weighted range measures; retained
branching fast-check histories; failpoint sweeps; exact and height-scaled diagnostic counters;
sharing; stored `undefined`; and runtime-local iterator behavior. Run them serially through the
checked-in one-worker Vitest configuration before the complete workspace gate.

The shipment checkpoint passes all 7 focused files and 45 tests with one worker and file parallelism
disabled. Strict `tsc --noEmit` checking and the declaration/ESM build also pass. No benchmark,
package dry run, or complete workspace gate is part of this focused checkpoint.
