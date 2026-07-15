# Python range-update sequence

- Created (UTC): 2026-07-15T05:37:18Z
- Repository HEAD: b0256f3bfe3b227e7872a82958b8eaa592e63d6f

`RangeUpdateSequence[T, M, U]` is the typed Python port of the independently implemented C#
range-update sequence. It is an immutable implicit AVL tree with cached ordered measures and
lazily composed tags. The normative algebra, callback-order, and persistence contract is the
[C# range-update specification](../../CSharp/docs/FingerTree/range-update-sequence.md); this
document records the Python runtime mapping and public names.

## Public surface

The `vladimir_reshetnikov.data_structures` root and `finger_tree` package export:

- `RangeUpdateAlgebra[T, M, U]`, a structural runtime policy protocol;
- `create_range_update_algebra(...)`, the functional policy adapter;
- `RangeUpdateSequence[T, M, U]`, the immutable facade; and
- `RangeUpdateSplit[T, M, U]`, the frozen named split result.

Construction and state are `empty(algebra)`, `from_iterable(values, algebra)`, `algebra`,
`is_empty`, `measure`, and `len(sequence)`. Indexed and structural operations are `get_at`, integer
`sequence[index]`, `prepend`, `append`, `insert`, `set_item`, `remove_at`, `concat`, `split_at`, and
`get_range`. Range actions and queries are `apply_range` and `measure_range`. `to_list` and normal
iteration enumerate logical values in index order.

The generic core deliberately has no element equality policy, so `set_item` is unconditional. It
also has no `add_range`, `assign_range`, or other tag-specific shortcut: addition and assignment
are examples of policies, not operations built into the data structure.

## Algebra contract

An algebra provides:

```python
class RangeUpdateAlgebra(Protocol[T, M, U]):
    identity: M
    identity_tag: U

    def combine(self, left: M, right: M) -> M: ...
    def measure(self, element: T) -> M: ...
    def is_identity(self, tag: U) -> bool: ...
    def compose(self, newer: U, older: U) -> U: ...
    def apply_element(self, tag: U, element: T) -> T: ...
    def apply_measure(self, tag: U, measure: M, count: int) -> M: ...
```

`combine` is associative with `identity` but need not commute. Tag composition is ordered:
`compose(newer, older)` means that `older` acts first and `newer` acts second. For every lawful
value, tag, measure, and nonnegative count, the policy must preserve these laws:

- `is_identity(identity_tag)` is true, and every recognized identity acts neutrally;
- `compose` is associative modulo action and has every recognized identity as a two-sided identity;
- applying a composition equals applying its older component and then its newer component;
- `apply_measure(tag, measure(element), 1)` equals `measure(apply_element(tag, element))`;
- measure action distributes over ordered combination, using each operand's count; and
- applying a tag to the empty measure with count zero yields the empty measure.

`is_identity` is authoritative. Python equality is never consulted for tags. A value-distinct tag
may be an identity, and `identity_tag` is not an absence sentinel.

## Runtime policy and empty identity

Each independently constructed sequence lineage retains the exact algebra object and captures its
`identity` measure once. All successors share that context and reuse one lineage-canonical empty
facade. Empty range extraction, removal of the last item, and endpoint splits therefore return the
same empty object within a lineage. Independent calls to `empty(algebra)` establish independent
lineages and are not promised to return the same object.

Concatenation requires `left.algebra is right.algebra`, including empty operands. If compatible,
an empty operand retains the nonempty operand by identity. A newly joined nonempty result uses the
receiver's lineage context.

`from_iterable` first materializes its complete source exactly once. Only after successful
materialization and signed-32-bit count validation does it capture the empty measure and begin
element-measure callbacks. A throwing source therefore cannot produce a partial build or invoke
tree policy callbacks. Passing a `RangeUpdateSequence` of the same runtime class and exact algebra
returns it directly.

## Lazy-node invariant

Each frozen internal node stores its value, children, height, element count, cached logical
measure, a pending-tag presence bit, and a tag field. If a node has a pending tag:

- its own value already includes the tag;
- its cached measure already includes the tag over the entire subtree; and
- its children do not yet include that tag.

The presence bit alone distinguishes a pending tag. The tag field may be `None` while the bit is
true, so `None` is a fully supported active tag as well as a supported element or transformed
result.

Structural descent pushes a pending tag immutably onto the roots of both children and clears the
new parent wrapper. Push order is left then right. Inserting or replacing after that push ensures
an older range update cannot transform a newly supplied element. Rotations push the same roots and
pivots as the C# algorithm before rebuilding cached metadata.

Read-only descent does not push. It carries an optional inherited tag in stack or local state. An
ancestor tag is newer than a descendant's pending tag, so the child frame uses
`compose(inherited, node_pending)`. The current node's value applies only the inherited tag; its own
pending tag is already reflected and must not be applied twice. If actual composition yields a tag
recognized as identity, the child frame erases the marker.

## Operations and persistence

Indexing, point edits, splits, joins, proper range updates, and proper range measures are O(log n)
worst-case. Enumeration is O(n) with an O(log n) traversal stack. A nonidentity whole-sequence
update is O(1): it transforms the root value and root measure and allocates one replacement node.
A proper update splits twice, tags the isolated middle root, and rejoins. A range query consumes
the cached measure of each fully covered subtree and measures only boundary values; it allocates no
persistent nodes.

All nodes are immutable, and a public facade is allocated only after every required policy
callback succeeds. An exception from `measure`, `combine`, `is_identity`, `compose`,
`apply_element`, or `apply_measure` therefore publishes no successor and leaves every older
version usable. Unaffected subtrees retain object identity.

Identity behavior is observable:

- an empty or identity update returns the receiver;
- an empty update does not call `is_identity`;
- endpoint splits retain the source on the nonempty side;
- a full `get_range` returns the receiver;
- an empty `get_range` returns the lineage-canonical empty;
- compatible empty concatenation retains the nonempty operand; and
- `set_item` always returns a successor.

## Validation and Python iteration

Element and boundary positions reject negative indexing rather than adopting Python's usual
from-the-end convention. Invalid positions raise `IndexError`; invalid counts raise `ValueError`.
Validation order is negative index, negative count, excessive boundary index, then excessive
count using subtraction rather than overflow-prone addition. Counts are bounded by
`Int32.MaxValue`; overflow raises `OverflowError` before policy callbacks.

Python iterators are ordinary independent, snapshot-bound iterator objects. Each iterator carries
logical tags without materializing a pushed tree. This is the intentional language mapping of the
C# value enumerator: Python does not expose `Current`, `Reset`, disposal, or fail-fast behavior
between copied enumerator structs.

Internal test hooks report reachable-node count, AVL height and balance, pending-node count and
depth, root/node identities, sharing between snapshots, and full cached-measure invariants. A
thread-local nested observer counts deterministic tree visits, allocations, rotations, pushes,
subtree applications, and each policy callback. These counters establish algorithmic work bounds;
they are not elapsed-time benchmarks or performance evidence.

## Validation lane

Run the workspace gate sequentially from `src/Python`:

```powershell
.\test.ps1
```

For an already provisioned environment, `test.ps1 -SkipInstall` retains the complete Ruff, strict
Mypy, pytest/Hypothesis, package-build, metadata, and installed-wheel smoke gates. Do not overlap
those commands with another workspace build or test process. Benchmarks are outside this gate and
remain postponed until an isolated session.
