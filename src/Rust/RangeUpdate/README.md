# Rust Range-Update Sequence

- Created (UTC): 2026-07-15T10:22:25Z
- Repository HEAD: 83d2d4bc69d8c77980127695f656f0aa5ecf56bd
- Audience: Consumers, maintainers, reviewers, and sibling-language port authors
- Scope: Neutral Rust range-update algebra, persistent sequence, contracts, and validation entry point

`tools-data-structures-range-update` is the safe-Rust port of the repository's benchmark-independent
range-update sequence. `RangeUpdateSequence<T, A>` combines indexed persistent edits, ordered cached
measures, and lazy algebraic transforms over contiguous ranges. It is a neutral general-purpose
crate: it extends the public FingerTree crate's `MeasurePolicy<T>` vocabulary, owns its own
range-specific implicit AVL representation, and has no dependency on Tungsten production code,
tests, internals, or behavior.

The normative cross-port semantic reference is the
[C# range-update sequence contract](../../CSharp/docs/FingerTree/range-update-sequence.md). This
document records the Rust type mapping, result shaping, storage contract, and validation lane.

## Algebra

An algebra is a nominal zero-sized or static policy type:

```rust
pub trait RangeUpdateAlgebra<T>: MeasurePolicy<T> {
    type Tag: Clone;

    fn identity_tag() -> Self::Tag;
    fn is_identity(tag: &Self::Tag) -> bool;
    fn compose(newer: &Self::Tag, older: &Self::Tag) -> Self::Tag;
    fn apply_element(tag: &Self::Tag, element: &T) -> T;
    fn apply_measure(
        tag: &Self::Tag,
        measure: &Self::Measure,
        count: usize,
    ) -> Self::Measure;
}
```

`compose(newer, older)` always means “apply `older`, then apply `newer`.” The order is observable:
an assignment applied after an addition normally discards the addition, while an addition applied
after an assignment becomes part of the assigned value. `is_identity` is authoritative; every
value it accepts must act as a two-sided composition identity and leave elements and measures
unchanged. `identity_tag` is not used as an absence sentinel because nodes store pending actions as
`Option<A::Tag>`; an active tag may itself be an `Option` value without ambiguity.

The inherited `MeasurePolicy<T>` monoid remains ordered and need not commute. In addition to its
identity and associativity laws, a valid range algebra obeys:

- tag composition is associative and has every recognized identity on both sides;
- element and measure application respect composition in the same older-then-newer order;
- singleton measure application equals measuring the transformed singleton;
- action distributes over ordered measure combination when the two subtree counts are supplied;
  and
- applying any tag to the empty measure at count zero returns the empty measure.

The crate assumes policy methods are deterministic, pure, and lawful. A panic propagates. Because
published nodes are immutable and a returned facade is assembled only after every callback
finishes, a panic cannot mutate or partially publish any input version.

## Public API and Rust result mapping

`RangeUpdateSequence<T, A>` provides:

- `new`, `from_items`, and `from_slice` construction;
- `len`, `is_empty`, cached `measure`, optional `get`, `iter`, and `to_vec` observation;
- `prepend`, `append`, `insert`, `set_item`, and `remove_at` point edits;
- `concat`, `split_at`, and `get_range` structural operations;
- `apply_range` and `measure_range` lazy range operations; and
- `shares_root_with`, `shared_node_count`, `structure_statistics`, `validate_structure`, and
  `validate_structure_by` diagnostics.

`FromIterator<T>` is also implemented as an idiomatic convenience. Because that trait cannot
return a capacity error, collecting more than `MAXIMUM_COUNT` elements panics; `from_items` is the
explicit `Result`-returning constructor. Rust has no source-type-specialized generic iterator
overload corresponding to C# `CreateRange(sequence)`: clone an existing sequence for the O(1)
root-sharing operation, while `from_items(existing_sequence)` deliberately enumerates and rebuilds.

Rust indices and counts are `usize`, so negative-input cases are unrepresentable. Throwing C#
boundary members map to `Result<_, RangeUpdateError>` for edits and structural/range operations;
nonthrowing indexed observation maps to `Option<T>`. The error distinguishes element indices,
boundary indices, ranges, and capacity overflow. Range validation uses `count > len - index` after
checking `index <= len`, so it never performs overflow-prone unchecked addition.

The count ceiling remains `i32::MAX` for cross-port structural parity, exposed as
`MAXIMUM_COUNT`. `from_items` completely materializes its source and enforces that ceiling before
capturing the empty measure or measuring an element. `concat` checks its combined count before any
algebra callback. Allocation failure follows Rust's process-level allocation behavior and is not
reported as `RangeUpdateError`.

The policy is a type parameter rather than a retained runtime object, matching the static C#
`TOps` contract and the existing Rust `MeasurePolicy` convention. Consequently, two sequences with
the same `T` and `A` are algebra-compatible by type and may concatenate without a runtime policy
identity check. Stateful policies should encode state in nominal policy types or choose another
abstraction; this core deliberately does not retain a callback object.

Persistent edits require `T: Clone`; the inherited `MeasurePolicy` already requires a cloneable
measure and `RangeUpdateAlgebra` requires a cloneable tag. This is the Rust ownership mapping for
path copying. A panic from one of those clone implementations has the same publication behavior as
a policy panic: already published snapshots remain unchanged and no successor is returned.

The facade itself has ordinary Rust value semantics rather than observable object identity. The
reference-retaining shortcuts in the normative contract map to `Arc` root sharing, exposed through
`shares_root_with`; independently constructed empty values both have no root, while descendants of
one construction lineage additionally share its captured empty-measure `Arc` internally.

## Lazy representation

Each immutable `Arc` node stores a value, left and right children, height, count, cached measure,
and optional pending tag. Its value and measure already reflect its own pending tag, while its
children do not. The cached measure is the ordered combination of the logical left subtree,
logical node value, and logical right subtree.

A whole-subtree update transforms the root value and cached measure, composes the newer tag after
any older pending tag, and retains both child `Arc`s. A composition recognized as identity clears
the marker while retaining the already transformed logical value and measure. Structural descent
path-copies and pushes the pending tag to child roots before insertion, replacement, removal,
split, join, or rotation. New point-edit values are therefore current values and are never
retroactively changed by a tag installed on an older snapshot.

Reads do not push. Indexed lookup, iteration, and range measurement carry an inherited optional
tag. An ancestor tag is newer than a descendant's local tag, so child inheritance uses
`A::compose(inherited, local)`. The current node value has already received `local` and receives
only the inherited action. Fully covered range-query subtrees transform their cached measure only
by inheritance.

All updates are persistent path copies. `Arc` makes root identity and exact cross-version node
sharing observable without assigning reference identity to the public value-like facade. The
collection is `Send + Sync` whenever its element, measure, tag, and nominal policy types satisfy
the corresponding auto-trait requirements.

## Identity, boundaries, and iteration

- Empty or recognized-identity updates return a root-sharing clone of the receiver. An empty
  update does not call `is_identity`.
- Endpoint splits retain the source root on the nonempty side. Full extraction retains the source
  root; empty extraction returns an empty facade sharing the receiver's captured empty measure.
- Empty concatenation retains the nonempty root. A nonempty result produced from two lineages uses
  the receiver's captured empty-measure lineage for later empty derivatives.
- `set_item` is unconditional because the generic core has no element-equality policy.
- The API intentionally exposes no `add_range` or `assign_range`; those are meanings supplied by a
  particular tag algebra.

`iter` owns `Arc`s for one immutable snapshot and yields owned logical values because inherited
tags may synthesize them. Independently created iterators have independent stacks. This is the
idiomatic Rust mapping of the C# concrete enumerator; Rust does not reproduce its boxed-interface,
`Current`/`Reset`, or copied-struct divergence behavior. The iterator is exact-size and fused, uses
O(log n) traversal state, and installs no persistent nodes or mutable cache.

## Complexity

Let `n` be the sequence length:

| Operation | Worst-case structural cost |
| --- | --- |
| `len`, `is_empty`, whole `measure` | O(1) |
| `from_items`, `from_slice` | O(n) time and nodes |
| `get`, point edits | O(log n) |
| `split_at`, `concat`, proper `get_range` | O(log n) |
| whole nonidentity `apply_range` | O(1), one root replacement |
| proper `apply_range` | O(log n) path copies |
| empty/full `measure_range` | O(1) |
| proper `measure_range` | O(log n), no persistent nodes |
| complete iteration | O(n) time, O(log n) iterator state |

These are representation bounds, not elapsed-time or allocation-rate claims against other data
structures. Benchmarks are intentionally postponed until they can run on an isolated machine.

## Validation

From `src/Rust`, run the focused serialized lane:

```powershell
.\test.ps1 -Workspace RangeUpdate
```

The root wrapper forces one Cargo build job and one Rust test-harness thread even when callers
forward their own job/thread flags. The [test map](tests/README.md) describes the deterministic law,
boundary, model, failure-atomicity, sharing, iterator, and concurrent-reader evidence. The
[documentation index](docs/README.md) links the normative contract and records the deliberate Rust
surface differences.
