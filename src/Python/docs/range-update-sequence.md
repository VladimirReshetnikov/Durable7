# Python range-update sequence

- Created (UTC): 2026-07-15T05:37:18Z
- Repository HEAD: b0256f3bfe3b227e7872a82958b8eaa592e63d6f

`RangeUpdateSequence[T, M, U]` is the typed Python port of the independently implemented C#
range-update sequence. It is an immutable implicit AVL tree with cached ordered measures and
lazily composed tags. The normative algebra, callback-order, and persistence contract is the
[C# range-update specification](../../CSharp/docs/FingerTree/range-update-sequence.md); this
document records the Python runtime mapping and public names.

## Public surface

The `durable7` root and `finger_tree` package export:

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

## Positional and measured cursor

`RangeUpdateSequence.get_cursor(position=0)` returns a `RangeUpdateSequenceCursor[T, M, U]`, the
family's public cursor. It is a **Profile R root-plus-position semantic checkpoint** in the sense of
the [repository-wide cursor design][cursor-design]:
a frozen, slotted dataclass holding one retained sequence version plus a validated gap in
`0 .. len(sequence)`, and nothing else. It retains no AVL context frames, no normalized edit spine,
no composed-tag stack, and no canonical-snapshot memo. It therefore inherits **none** of the C# rope
cursor tier's focused representation, memoization, callback ceiling, allocation bound, or
amortized-locality claims; every operation below delegates to the ordinary persistent operation
named beside it and costs exactly what that operation costs. There is no uninitialized, default, or
moved-from cursor state — the constructor validates the position immediately.

State and navigation are `count`, `is_at_start`, `is_at_end`, `measure_before`, `measure_after`,
`peek_previous()`, `peek_next()`, `move_previous()`, `move_next()`, `seek(position)`, and
`snapshot()`. Peeks return `SequenceCursorPeek[T]` or `None`, so a stored `None` element stays
distinct from a boundary; `snapshot()` returns the retained sequence and never consumes the cursor.
Edits are `insert(value)`, `delete_previous()`, `delete_next()`, and `replace_next(value)`. Range
operations are `measure_previous(count)`, `measure_next(count)`, `apply_previous(count, tag)`, and
`apply_next(count, tag)`.

Gap conventions follow the shared positional model. `insert` places the value at the gap and returns
the gap after it. `delete_previous` removes the element at `position - 1` and moves the gap left.
`delete_next` removes the element at `position` and keeps the gap fixed. `replace_next` addresses
`position` and keeps the gap fixed; because the generic core has no element-equality policy it is
unconditional and always publishes a successor. `apply_previous(k, tag)` targets
`[position - k, position)` and `apply_next(k, tag)` targets `[position, position + k)`; both keep
the gap fixed. `seek(position)` returns the receiver when the position is unchanged, and both
`apply_previous` and `apply_next` return the receiver cursor whenever the underlying `apply_range`
returns the receiver sequence.

The cursor keeps the two error channels the sequence already defines. Boundary positions are
`IndexError`: constructing or seeking outside `0 .. len(sequence)` goes through
`_check_boundary_index`, and `move_previous`, `move_next`, `delete_previous`, `delete_next`, and
`replace_next` raise `IndexError` at a boundary with no adjacent element. Directional counts are
`ValueError`: `measure_previous`, `measure_next`, `apply_previous`, and `apply_next` validate
`count` against the available direction before any policy call, and the underlying `apply_range` and
`measure_range` keep the same index-versus-count split. `insert` raises `OverflowError` at the
signed-32-bit element ceiling before touching the tree. Validation always precedes policy callbacks,
so a rejected argument invokes no `measure`, `combine`, `is_identity`, `compose`, `apply_element`,
or `apply_measure`.

Tag semantics are the collection's, unchanged. Navigation, peeks, and measure reads are read-only
descents: they carry an inherited tag as local state, composing it as `compose(inherited, pending)`
at each child frame, and never push a tag or path-copy a node. Moving away and back therefore leaves
the retained snapshot reference-identical. A structural edit through a tagged path pushes the old
pending tag onto both children before inserting or replacing, so an older range update cannot
transform a newly supplied element, and rotations rebuild height, count, and cached logical measure
afterwards. The cursor never treats `identity_tag`, `None`, or tag equality as an absence marker;
the separate presence bit remains authoritative, and a composed tag is cleared only through
`is_identity`.

Both measure properties are read-only. `measure_before` is `measure_range(0, position)` and
`measure_after` is `measure_range(position, count - position)`; neither performs a structural split
and neither allocates a persistent node. `combine(measure_before, measure_after)` equals
`snapshot().measure` in that order, and both reflect every carried tag. A zero-length
`measure_previous` or `measure_next` returns the algebra identity without invoking any element or
tag callback, because `measure_range` short-circuits on `count == 0` before reading the root.
Likewise a zero-length `apply_previous` or `apply_next` returns the receiver without calling
`is_identity`, while a nonempty range with a tag recognized as identity returns the receiver after
exactly one `is_identity` call.

Costs are the sequence's own. Navigation — `count`, the boundary predicates, `move_previous`,
`move_next`, and `seek` — only rewrites an integer and is O(1). A peek is one O(log n) indexed
lookup, so a full traversal by move-plus-peek is O(n log n); prefer ordinary iteration, which is
O(n) with an O(log n) stack. `measure_before`, `measure_after`, `measure_previous`, and
`measure_next` are O(log n) read-only descents that consume the cached measure of each fully covered
subtree. `insert`, `delete_previous`, `delete_next`, and `replace_next` are O(log n) path-copying
edits. A proper subrange `apply_previous`/`apply_next` splits twice, tags the isolated middle root,
and rejoins, so it is O(log n). A nonidentity tag covering the whole sequence — `apply_next(count,
tag)` at the start, or `apply_previous(count, tag)` at the end — retains the substrate's O(1) root
update, transforming the root value and root measure and allocating one replacement node; the clean
root-plus-gap checkpoint is exactly the case in which that O(1) result is legitimately inherited.
`snapshot()` is O(1) because the cursor already holds the canonical facade, and it needs no memo.

Because every edit is a whole ordinary operation over an immutable tree, failure atomicity is the
collection's: a throwing policy callback publishes no successor sequence and therefore no successor
cursor, and the receiver cursor, its snapshot, and every retained branch remain valid and usable.

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

[cursor-design]: ../../../docs/proposals/repository-wide-persistent-cursor-design.md
