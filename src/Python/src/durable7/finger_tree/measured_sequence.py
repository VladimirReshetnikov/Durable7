"""A persistent monoid-measured Hinze-Paterson finger tree with memoized lazy spines.

This module was previously an implicit AVL join tree, which delivered O(log n) endpoint updates
and a concatenation bound keyed to the operands' height difference. It is now the same structure
the reference workspaces carry: 1-4-element digits at each end, 2-3 nodes below them, and a middle
subtree per deep node held behind a **memoized suspension**, which is Hinze and Paterson's lazy
finger tree realized in a strict language, exactly as the C#, C++, and C workspaces realize it.

Bounds (with O(1) policy operations): ``front``/``back`` are O(1) worst-case digit reads;
``prepend``/``append`` and the endpoint views are O(1) amortized and O(log n) worst-case per call;
``concat`` is O(log(min(n, m))) amortized; ``split_at``, ``at``, ``insert_at``, ``set_at``,
``remove_at``, ``locate``, ``prefix_measure``, ``lower_bound``, and ``upper_bound`` are O(log n);
iteration is O(n). The amortized bounds hold under fully persistent branching histories, not
merely ephemeral linear use, because a forced suspension is memoized in a cell shared by every
version that references it: work deferred by one version and forced by another is never repeated.

Laziness is load-bearing in exactly two places, and eager everywhere else. A digit overflow on
``prepend``/``append`` pushes a 2-3 node into the middle *inside* a suspension, and ``concat``
recurses into the two middles *inside* a suspension; every size is computed strictly at
construction, so index arithmetic never forces structure it does not enter. A deep node's measure
is memoized separately and forcing it may force the middle spine - the reason ``measure`` is O(1)
amortized rather than worst-case. A suspension whose computation raises publishes nothing and may
be retried, so policy failures keep every retained version valid.

Each 2-3 node also caches its first and last descendant elements, which is what keeps
``lower_bound``/``upper_bound`` at O(log n) - each level inspects at most eight digit items by
their cached extremes - and makes ``front``/``back`` worst-case O(1).
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .measures import MeasurePolicy

T = TypeVar("T")
M = TypeVar("M")

_PENDING = object()


_OP_PUSH_FRONT = 1
_OP_PUSH_BACK = 2
_OP_CONCAT = 3
_OP_REVERSE = 4


class _Susp:
    """A memoized suspension of a tree, defunctionalized.

    The three deferred operations - push an overflow node into the middle from either end, and
    concatenate two middles around regrouped nodes - are stored as data rather than closures, so
    :meth:`force` can interpret an arbitrarily long chain of pending suspensions with an explicit
    stack. Organic append loops build Theta(n)-deep chains, far past Python's recursion limit; a
    closure-based force would recurse once per link and die, which is exactly the hazard every
    other structure in this workspace handles by iterating.

    Forcing memoizes the result in this cell under the GIL, so every version sharing the cell
    reads the same forced tree and deferred work is never repeated. A raising policy leaves every
    cell on the failure path pending, so a failed force publishes nothing and is retryable.
    """

    __slots__ = ("_first", "_operation", "_policy", "_second", "_value")

    def __init__(self, operation: int, policy: object, first: object, second: object) -> None:
        self._operation = operation
        self._policy = policy
        self._first = first
        self._second = second
        self._value: object = _PENDING

    @classmethod
    def ready(cls, value: object) -> _Susp:
        suspension = cls.__new__(cls)
        suspension._operation = 0
        suspension._policy = None
        suspension._first = None
        suspension._second = None
        suspension._value = value
        return suspension

    @classmethod
    def push_front(cls, policy: object, item: object, middle: _Susp) -> _Susp:
        return cls(_OP_PUSH_FRONT, policy, item, middle)

    @classmethod
    def push_back(cls, policy: object, middle: _Susp, item: object) -> _Susp:
        return cls(_OP_PUSH_BACK, policy, middle, item)

    @classmethod
    def concat(cls, policy: object, left: _Susp, between: list[object], right: _Susp) -> _Susp:
        return cls(_OP_CONCAT, policy, (left, right), between)

    @classmethod
    def reverse_of(cls, policy: object, inner: _Susp) -> _Susp:
        return cls(_OP_REVERSE, policy, inner, None)

    @property
    def is_forced(self) -> bool:
        return self._value is not _PENDING

    def force(self) -> object:
        if self._value is not _PENDING:
            return self._value
        stack: list[_Susp] = [self]
        while stack:
            current = stack[-1]
            if current._value is not _PENDING:
                stack.pop()
                continue
            operation = current._operation
            policy = cast("MeasurePolicy[object, object]", current._policy)
            if operation == _OP_PUSH_FRONT:
                inner = cast("_Susp", current._second)
                if inner._value is _PENDING:
                    stack.append(inner)
                    continue
                current._publish(_cons(policy, current._first, cast("_Tree", inner._value)))
            elif operation == _OP_PUSH_BACK:
                inner = cast("_Susp", current._first)
                if inner._value is _PENDING:
                    stack.append(inner)
                    continue
                current._publish(_snoc(policy, cast("_Tree", inner._value), current._second))
            elif operation == _OP_REVERSE:
                inner = cast("_Susp", current._first)
                if inner._value is _PENDING:
                    stack.append(inner)
                    continue
                current._publish(_reverse_tree(policy, cast("_Tree", inner._value)))
            else:
                pair = cast("tuple[_Susp, _Susp]", current._first)
                left, right = pair
                if left._value is _PENDING:
                    stack.append(left)
                    continue
                if right._value is _PENDING:
                    stack.append(right)
                    continue
                current._publish(
                    _app3(
                        policy,
                        cast("_Tree", left._value),
                        cast("list[object]", current._second),
                        cast("_Tree", right._value),
                    )
                )
        return self._value

    def _publish(self, value: object) -> None:
        self._value = value
        self._policy = None
        self._first = None
        self._second = None


class _Node:
    """A 2-3 node: strict size, measure, and cached first/last descendant elements."""

    __slots__ = ("children", "first", "last", "measure", "size")

    def __init__(
        self, children: tuple[object, ...], size: int, measure: object, first: object, last: object
    ) -> None:
        self.children = children
        self.size = size
        self.measure = measure
        self.first = first
        self.last = last


class _Single:
    """A one-item tree."""

    __slots__ = ("item",)

    def __init__(self, item: object) -> None:
        self.item = item


class _Deep:
    """Digits at both ends around a suspended middle of one-level-deeper nodes.

    ``size`` and ``middle_size`` are strict - every construction site knows them arithmetically -
    so no size read ever forces the middle. The total measure is memoized on first read.
    """

    __slots__ = ("_measure", "middle", "middle_size", "prefix", "size", "suffix")

    def __init__(
        self,
        size: int,
        prefix: tuple[object, ...],
        middle: _Susp,
        middle_size: int,
        suffix: tuple[object, ...],
    ) -> None:
        self.size = size
        self.prefix = prefix
        self.middle = middle
        self.middle_size = middle_size
        self.suffix = suffix
        self._measure: object = _PENDING


_Tree = _Single | _Deep | None

_EMPTY_SUSP = _Susp.ready(None)


# --- item helpers: an item is a user element at level zero, a _Node below ---------------------


def _item_size(item: object) -> int:
    return item.size if isinstance(item, _Node) else 1


def _item_measure(policy: MeasurePolicy[T, M], item: object) -> object:
    if isinstance(item, _Node):
        return item.measure
    return policy.measure(cast("T", item))


def _item_first(item: object) -> object:
    return item.first if isinstance(item, _Node) else item


def _item_last(item: object) -> object:
    return item.last if isinstance(item, _Node) else item


def _make_node(policy: MeasurePolicy[T, M], children: tuple[object, ...]) -> _Node:
    size = 0
    measure: object = None
    for position, child in enumerate(children):
        size += _item_size(child)
        child_measure = _item_measure(policy, child)
        measure = (
            child_measure
            if position == 0
            else policy.combine(cast("M", measure), cast("M", child_measure))
        )
    return _Node(children, size, measure, _item_first(children[0]), _item_last(children[-1]))


def _digit_size(digit: tuple[object, ...]) -> int:
    return sum(_item_size(item) for item in digit)


def _digit_measure(policy: MeasurePolicy[T, M], digit: tuple[object, ...]) -> object:
    measure: object = _item_measure(policy, digit[0])
    for item in digit[1:]:
        measure = policy.combine(cast("M", measure), cast("M", _item_measure(policy, item)))
    return measure


# --- tree helpers ------------------------------------------------------------------------------


def _tree_size(tree: _Tree) -> int:
    if tree is None:
        return 0
    if isinstance(tree, _Single):
        return _item_size(tree.item)
    return tree.size


def _tree_measure(policy: MeasurePolicy[T, M], tree: _Tree) -> object:
    if tree is None:
        return policy.identity
    if isinstance(tree, _Single):
        return _item_measure(policy, tree.item)
    measure = tree._measure
    if measure is _PENDING:
        measure = _digit_measure(policy, tree.prefix)
        middle = cast("_Tree", tree.middle.force())
        if middle is not None:
            measure = policy.combine(cast("M", measure), cast("M", _tree_measure(policy, middle)))
        measure = policy.combine(cast("M", measure), cast("M", _digit_measure(policy, tree.suffix)))
        tree._measure = measure
    return measure


def _tree_first(tree: _Tree) -> object:
    assert tree is not None
    if isinstance(tree, _Single):
        return _item_first(tree.item)
    return _item_first(tree.prefix[0])


def _tree_last(tree: _Tree) -> object:
    assert tree is not None
    if isinstance(tree, _Single):
        return _item_last(tree.item)
    return _item_last(tree.suffix[-1])


def _deep(
    prefix: tuple[object, ...], middle: _Susp, middle_size: int, suffix: tuple[object, ...]
) -> _Deep:
    return _Deep(
        _digit_size(prefix) + middle_size + _digit_size(suffix), prefix, middle, middle_size, suffix
    )


def _digit_to_tree(policy: MeasurePolicy[T, M], digit: tuple[object, ...]) -> _Tree:
    if len(digit) == 1:
        return _Single(digit[0])
    half = len(digit) // 2
    return _deep(digit[:half], _EMPTY_SUSP, 0, digit[half:])


# --- endpoint operations -----------------------------------------------------------------------


def _cons(policy: MeasurePolicy[T, M], item: object, tree: _Tree) -> _Tree:
    if tree is None:
        return _Single(item)
    if isinstance(tree, _Single):
        return _deep((item,), _EMPTY_SUSP, 0, (tree.item,))
    if len(tree.prefix) < 4:
        return _deep((item, *tree.prefix), tree.middle, tree.middle_size, tree.suffix)
    overflow = _make_node(policy, tree.prefix[1:])
    return _deep(
        (item, tree.prefix[0]),
        # The push into the middle is the suspended step Hinze-Paterson amortization needs.
        _Susp.push_front(policy, overflow, tree.middle),
        tree.middle_size + overflow.size,
        tree.suffix,
    )


def _snoc(policy: MeasurePolicy[T, M], tree: _Tree, item: object) -> _Tree:
    if tree is None:
        return _Single(item)
    if isinstance(tree, _Single):
        return _deep((tree.item,), _EMPTY_SUSP, 0, (item,))
    if len(tree.suffix) < 4:
        return _deep(tree.prefix, tree.middle, tree.middle_size, (*tree.suffix, item))
    overflow = _make_node(policy, tree.suffix[:3])
    return _deep(
        tree.prefix,
        _Susp.push_back(policy, tree.middle, overflow),
        tree.middle_size + overflow.size,
        (tree.suffix[3], item),
    )


def _view_left(policy: MeasurePolicy[T, M], tree: _Tree) -> tuple[object, _Tree] | None:
    if tree is None:
        return None
    if isinstance(tree, _Single):
        return (tree.item, None)
    head = tree.prefix[0]
    if len(tree.prefix) > 1:
        return (head, _deep(tree.prefix[1:], tree.middle, tree.middle_size, tree.suffix))
    middle = cast("_Tree", tree.middle.force())
    if middle is None:
        return (head, _digit_to_tree(policy, tree.suffix))
    pulled = _view_left(policy, middle)
    assert pulled is not None
    node, rest = pulled
    return (
        head,
        _deep(
            _item_digit(node), _Susp.ready(rest), tree.middle_size - _item_size(node), tree.suffix
        ),
    )


def _view_right(policy: MeasurePolicy[T, M], tree: _Tree) -> tuple[_Tree, object] | None:
    if tree is None:
        return None
    if isinstance(tree, _Single):
        return (None, tree.item)
    last = tree.suffix[-1]
    if len(tree.suffix) > 1:
        return (_deep(tree.prefix, tree.middle, tree.middle_size, tree.suffix[:-1]), last)
    middle = cast("_Tree", tree.middle.force())
    if middle is None:
        return (_digit_to_tree(policy, tree.prefix), last)
    pulled = _view_right(policy, middle)
    assert pulled is not None
    rest, node = pulled
    return (
        _deep(
            tree.prefix, _Susp.ready(rest), tree.middle_size - _item_size(node), _item_digit(node)
        ),
        last,
    )


# --- concatenation -----------------------------------------------------------------------------


def _group_nodes(policy: MeasurePolicy[T, M], items: list[object]) -> list[object]:
    """Group 2..12 items into 2-3 nodes, standard Hinze-Paterson grouping."""

    nodes: list[object] = []
    index = 0
    remaining = len(items)
    while remaining > 0:
        if remaining == 2 or remaining == 4:
            nodes.append(_make_node(policy, (items[index], items[index + 1])))
            index += 2
            remaining -= 2
        else:
            nodes.append(_make_node(policy, (items[index], items[index + 1], items[index + 2])))
            index += 3
            remaining -= 3
    return nodes


def _app3(policy: MeasurePolicy[T, M], left: _Tree, between: list[object], right: _Tree) -> _Tree:
    if left is None:
        result = right
        for item in reversed(between):
            result = _cons(policy, item, result)
        return result
    if right is None:
        result = left
        for item in between:
            result = _snoc(policy, result, item)
        return result
    if isinstance(left, _Single):
        return _cons(policy, left.item, _app3(policy, None, between, right))
    if isinstance(right, _Single):
        return _snoc(policy, _app3(policy, left, between, None), right.item)
    nodes = _group_nodes(policy, [*left.suffix, *between, *right.prefix])
    nodes_size = sum(_item_size(node) for node in nodes)
    return _deep(
        left.prefix,
        # Concatenation recurses into both middles inside a suspension, which is what makes it
        # O(log(min(n, m))) amortized instead of costing its whole recursion up front.
        _Susp.concat(policy, left.middle, nodes, right.middle),
        left.middle_size + nodes_size + right.middle_size,
        right.suffix,
    )


# --- index-directed access and splitting -------------------------------------------------------


def _item_at(item: object, index: int) -> object:
    while isinstance(item, _Node):
        for child in item.children:
            child_size = _item_size(child)
            if index < child_size:
                item = child
                break
            index -= child_size
    return item


def _tree_at(tree: _Tree, index: int) -> object:
    while True:
        assert tree is not None
        if isinstance(tree, _Single):
            return _item_at(tree.item, index)
        for item in tree.prefix:
            item_size = _item_size(item)
            if index < item_size:
                return _item_at(item, index)
            index -= item_size
        if index < tree.middle_size:
            tree = cast("_Tree", tree.middle.force())
            continue
        index -= tree.middle_size
        for item in tree.suffix:
            item_size = _item_size(item)
            if index < item_size:
                return _item_at(item, index)
            index -= item_size


def _split_digit(
    digit: tuple[object, ...], index: int
) -> tuple[tuple[object, ...], object, tuple[object, ...], int]:
    """Split a digit at an element index, returning (before, item, after, index_into_item)."""

    for position, item in enumerate(digit):
        item_size = _item_size(item)
        if index < item_size:
            return (digit[:position], item, digit[position + 1 :], index)
        index -= item_size
    raise AssertionError("Digit split index out of range.")


def _item_to_tree(policy: MeasurePolicy[T, M], item: object) -> _Tree:
    if isinstance(item, _Node):
        return _digit_to_tree(policy, item.children)
    return _Single(item)


def _item_digit(item: object) -> tuple[object, ...]:
    return item.children if isinstance(item, _Node) else (item,)


def _reverse_item(policy: MeasurePolicy[T, M], item: object) -> object:
    """Mirror one item, recombining its summary in the mirrored order.

    Sizes and end handles survive with roles swapped, but the cached measure is rebuilt from the
    reversed children: a mirrored subtree's summary equals its cached one only under a commutative
    monoid, and this recombination is what makes the reversed view correct under every monoid at
    O(1) combines per node, paid only when the enclosing reversal suspension forces.
    """

    if not isinstance(item, _Node):
        return item
    children = tuple(_reverse_item(policy, child) for child in reversed(item.children))
    measure = _item_measure(policy, children[0])
    for child in children[1:]:
        measure = policy.combine(measure, _item_measure(policy, child))  # type: ignore[arg-type]
    return _Node(children, item.size, measure, _item_first(children[0]), _item_last(children[-1]))


def _reverse_digit(policy: MeasurePolicy[T, M], digit: tuple[object, ...]) -> tuple[object, ...]:
    return tuple(_reverse_item(policy, item) for item in reversed(digit))


def _reverse_tree(policy: MeasurePolicy[T, M], tree: _Tree) -> _Tree:
    """Mirror a tree with the middle's reversal deferred: O(1) immediate work per level."""

    if tree is None:
        return None
    if isinstance(tree, _Single):
        return _Single(_reverse_item(policy, tree.item))
    return _Deep(
        tree.size,
        _reverse_digit(policy, tree.suffix),
        _Susp.reverse_of(policy, tree.middle),
        tree.middle_size,
        _reverse_digit(policy, tree.prefix),
    )


def _deep_left(
    policy: MeasurePolicy[T, M],
    before: tuple[object, ...],
    middle: _Susp,
    middle_size: int,
    suffix: tuple[object, ...],
) -> _Tree:
    """Rebuild a tree whose prefix digit may be empty."""

    if before:
        return _deep(before, middle, middle_size, suffix)
    forced = cast("_Tree", middle.force())
    if forced is None:
        return _digit_to_tree(policy, suffix)
    pulled = _view_left(policy, forced)
    assert pulled is not None
    node, rest = pulled
    return _deep(_item_digit(node), _Susp.ready(rest), middle_size - _item_size(node), suffix)


def _deep_right(
    policy: MeasurePolicy[T, M],
    prefix: tuple[object, ...],
    middle: _Susp,
    middle_size: int,
    after: tuple[object, ...],
) -> _Tree:
    """Rebuild a tree whose suffix digit may be empty."""

    if after:
        return _deep(prefix, middle, middle_size, after)
    forced = cast("_Tree", middle.force())
    if forced is None:
        return _digit_to_tree(policy, prefix)
    pulled = _view_right(policy, forced)
    assert pulled is not None
    rest, node = pulled
    return _deep(prefix, _Susp.ready(rest), middle_size - _item_size(node), _item_digit(node))


def _split_item(
    policy: MeasurePolicy[T, M], item: object, index: int
) -> tuple[list[object], object, list[object]]:
    """Split an item at an element index, collapsing its path into flat item lists."""

    before: list[object] = []
    after: list[object] = []
    while isinstance(item, _Node):
        found = None
        for position, child in enumerate(item.children):
            if found is None:
                child_size = _item_size(child)
                if index < child_size:
                    found = child
                    after = [*item.children[position + 1 :], *after]
                else:
                    index -= child_size
                    before.append(child)
        assert found is not None
        item = found
    return (before, item, after)


def _split_tree(
    policy: MeasurePolicy[T, M], tree: _Tree, index: int
) -> tuple[_Tree, object, _Tree]:
    """Split at an element index: (elements before, the element, elements after)."""

    assert tree is not None
    if isinstance(tree, _Single):
        before_items, element, after_items = _split_item(policy, tree.item, index)
        left: _Tree = None
        for item in before_items:
            left = _snoc(policy, left, item)
        right: _Tree = None
        for item in reversed(after_items):
            right = _cons(policy, item, right)
        return (left, element, right)
    prefix_size = _digit_size(tree.prefix)
    if index < prefix_size:
        before, item, after, inner = _split_digit(tree.prefix, index)
        item_before, element, item_after = _split_item(policy, item, inner)
        left = None
        for piece in (*before, *item_before):
            left = _snoc(policy, left, piece)
        right = _deep_left(policy, tuple(after), tree.middle, tree.middle_size, tree.suffix)
        for piece in reversed(item_after):
            right = _cons(policy, piece, right)
        return (left, element, right)
    index -= prefix_size
    if index < tree.middle_size:
        # The recursion already collapses down to the element level, so its halves are complete.
        middle = cast("_Tree", tree.middle.force())
        middle_left, element, middle_right = _split_tree(policy, middle, index)
        left = _deep_right(
            policy, tree.prefix, _Susp.ready(middle_left), _tree_size(middle_left), ()
        )
        right = _deep_left(
            policy, (), _Susp.ready(middle_right), _tree_size(middle_right), tree.suffix
        )
        return (left, element, right)
    index -= tree.middle_size
    before, item, after, inner = _split_digit(tree.suffix, index)
    item_before, element, item_after = _split_item(policy, item, inner)
    left = _deep_right(policy, tree.prefix, tree.middle, tree.middle_size, tuple(before))
    for piece in item_before:
        left = _snoc(policy, left, piece)
    right = None
    for piece in reversed((*item_after, *after)):
        right = _cons(policy, piece, right)
    return (left, element, right)


def _build_eager(policy: MeasurePolicy[T, M], items: list[object]) -> _Tree:
    """Build a tree bottom-up with ready middles: bulk construction defers nothing.

    Deferral pays off when later operations may never force the work; a bulk build's caller
    almost always consumes the result, so suspending every third append would only add cells to
    force and then discard. Grouping level by level costs the same O(n) with none of that churn.
    """

    count = len(items)
    if count == 0:
        return None
    if count == 1:
        return _Single(items[0])
    if count <= 8:
        half = count // 2
        return _deep(tuple(items[:half]), _EMPTY_SUSP, 0, tuple(items[half:]))
    middle_items = _group_nodes(policy, items[3:-3])
    middle = _build_eager(policy, middle_items)
    return _deep(tuple(items[:3]), _Susp.ready(middle), _tree_size(middle), tuple(items[-3:]))


# --- public dataclasses ------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class SequenceLocate(Generic[T, M]):
    """Where a measure-directed search landed, reported without splitting the sequence. ``found``
    distinguishes a real hit from the end position, so a located ``value`` of ``None`` stays
    unambiguous.
    """

    index: int
    measure_before: M
    value: T | None
    found: bool


@dataclass(frozen=True, slots=True)
class SequenceSplit(Generic[T, M]):
    """The two sequences produced by a split; both share structure with the original."""

    left: MeasuredSequence[T, M]
    right: MeasuredSequence[T, M]


class MeasuredSequence(Generic[T, M]):
    """Persistent measured finger tree with memoized lazy spines."""

    __slots__ = ("_root", "policy")

    def __init__(self, root: _Tree, policy: MeasurePolicy[T, M]) -> None:
        """Wrap an already-built root; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._root = root
        self.policy = policy

    @classmethod
    def empty(cls, policy: MeasurePolicy[T, M]) -> MeasuredSequence[T, M]:
        """Return an empty sequence measured by ``policy``.

        The policy is retained by identity, and only sequences sharing that exact object may be
        concatenated.
        """

        return cls(None, policy)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], policy: MeasurePolicy[T, M]
    ) -> MeasuredSequence[T, M]:
        """Build a sequence from ``values`` in one eager bottom-up O(n) pass."""

        return cls(_build_eager(policy, list(values)), policy)

    def __len__(self) -> int:
        """Number of elements, read from strict cached sizes."""

        return _tree_size(self._root)

    @property
    def is_empty(self) -> bool:
        """Whether the sequence holds no elements."""

        return self._root is None

    @property
    def measure(self) -> M:
        """The combined measure of every element, in sequence order.

        O(1) amortized: the root's measure is memoized, and the first read of a fresh spine may
        force a chain of suspended middles before memoizing. An empty sequence measures as the
        policy identity.
        """

        return cast("M", _tree_measure(self.policy, self._root))

    def front(self) -> T | None:
        """The first element, or ``None`` when the sequence is empty. O(1) worst-case."""

        return None if self._root is None else cast("T", _tree_first(self._root))

    def back(self) -> T | None:
        """The last element, or ``None`` when the sequence is empty. O(1) worst-case."""

        return None if self._root is None else cast("T", _tree_last(self._root))

    def at(self, index: int) -> T | None:
        """The element at ``index``, or ``None`` when the index is out of range.

        Descends by strict cached sizes, so it forces only the middles it actually enters.
        """

        if index < 0 or index >= len(self):
            return None
        return cast("T", _tree_at(self._root, index))

    def prepend(self, value: T) -> MeasuredSequence[T, M]:
        """Return a sequence with ``value`` added at the front. O(1) amortized."""

        return MeasuredSequence(_cons(self.policy, value, self._root), self.policy)

    def append(self, value: T) -> MeasuredSequence[T, M]:
        """Return a sequence with ``value`` added at the back. O(1) amortized."""

        return MeasuredSequence(_snoc(self.policy, self._root, value), self.policy)

    def concat(self, other: MeasuredSequence[T, M]) -> MeasuredSequence[T, M]:
        """Return the elements of this sequence followed by those of ``other``.

        O(log(min(n, m))) amortized: the recursion into the two middles is suspended, and the
        digits between them are regrouped into 2-3 nodes. Both sequences must retain the same
        policy object.
        """

        if self.policy is not other.policy:
            raise TypeError("Sequences must retain the same measure policy object.")
        if other.is_empty:
            return self
        if self.is_empty:
            return other
        return MeasuredSequence(_app3(self.policy, self._root, [], other._root), self.policy)

    def split_at(self, index: int) -> SequenceSplit[T, M] | None:
        """Split into the elements before ``index`` and those from ``index`` on.

        Returns ``None`` when ``index`` falls outside ``0..len``. Splitting at either end shares
        the receiver's root rather than rebuilding it.
        """

        if index < 0 or index > len(self):
            return None
        if index == 0:
            return SequenceSplit(MeasuredSequence.empty(self.policy), self)
        if index == len(self):
            return SequenceSplit(self, MeasuredSequence.empty(self.policy))
        left, element, right = _split_tree(self.policy, self._root, index)
        return SequenceSplit(
            MeasuredSequence(left, self.policy),
            MeasuredSequence(_cons(self.policy, element, right), self.policy),
        )

    def insert_at(self, index: int, value: T) -> MeasuredSequence[T, M] | None:
        """Return a sequence with ``value`` inserted so that it ends up at ``index``. ``None`` when
        ``index`` falls outside ``0..len``. A split and two concatenations, so it does not shift
        the tail.
        """

        split = self.split_at(index)
        return None if split is None else split.left.append(value).concat(split.right)

    def set_at(self, index: int, value: T) -> MeasuredSequence[T, M] | None:
        """Return a sequence with ``index``'s element replaced, or ``None`` when out of range."""

        if index < 0 or index >= len(self):
            return None
        left, _, right = _split_tree(self.policy, self._root, index)
        return MeasuredSequence(
            _app3(self.policy, _snoc(self.policy, left, value), [], right), self.policy
        )

    def remove_at(self, index: int) -> MeasuredSequence[T, M] | None:
        """Return a sequence without the element at ``index``, or ``None`` when out of range."""

        if index < 0 or index >= len(self):
            return None
        left, _, right = _split_tree(self.policy, self._root, index)
        return MeasuredSequence(_app3(self.policy, left, [], right), self.policy)

    def prefix_measure(self, count: int) -> M | None:
        """The combined measure of the first ``count`` elements, or ``None`` when ``count`` exceeds
        the length. Sums cached measures on the way down, so it does not visit the elements it
        skips.
        """

        if count < 0 or count > len(self):
            return None
        if count == 0:
            return self.policy.identity
        if count == len(self):
            return self.measure

        policy = self.policy
        result: object = policy.identity

        def take_items(items: Iterable[object], remaining: int) -> int:
            nonlocal result
            for item in items:
                if remaining == 0:
                    return 0
                item_size = _item_size(item)
                if item_size <= remaining:
                    result = policy.combine(
                        cast("M", result), cast("M", _item_measure(policy, item))
                    )
                    remaining -= item_size
                elif isinstance(item, _Node):
                    remaining = take_items(item.children, remaining)
                else:
                    raise AssertionError("An element has size one.")
            return remaining

        def take_tree(tree: _Tree, remaining: int) -> int:
            nonlocal result
            if tree is None or remaining == 0:
                return remaining
            if isinstance(tree, _Single):
                return take_items((tree.item,), remaining)
            if tree.size <= remaining:
                result = policy.combine(cast("M", result), cast("M", _tree_measure(policy, tree)))
                return remaining - tree.size
            remaining = take_items(tree.prefix, remaining)
            if remaining > 0:
                remaining = take_tree(cast("_Tree", tree.middle.force()), remaining)
            if remaining > 0:
                remaining = take_items(tree.suffix, remaining)
            return remaining

        leftover = take_tree(self._root, count)
        assert leftover == 0
        return cast("M", result)

    def locate(self, predicate: Callable[[M], bool]) -> SequenceLocate[T, M]:
        """Find the first position whose inclusive prefix measure satisfies ``predicate``.

        This is the sequence's central operation: because every node caches its measure, the
        search descends without visiting the elements it skips. ``predicate`` is expected to be
        monotone - false for every prefix up to some boundary and true from there on - which is
        what makes "the first satisfying position" well defined; a non-monotone predicate gives an
        unspecified but still valid result. A miss reports the end position with ``found`` false.
        """

        policy = self.policy
        before: object = policy.identity
        index = 0

        def descend_item(item: object) -> SequenceLocate[T, M]:
            nonlocal before, index
            while isinstance(item, _Node):
                for child in item.children:
                    with_child = policy.combine(
                        cast("M", before), cast("M", _item_measure(policy, child))
                    )
                    if predicate(with_child):
                        item = child
                        break
                    before = with_child
                    index += _item_size(child)
            return SequenceLocate(index, cast("M", before), cast("T", item), True)

        def scan_items(items: Iterable[object]) -> SequenceLocate[T, M] | None:
            nonlocal before, index
            for item in items:
                with_item = policy.combine(
                    cast("M", before), cast("M", _item_measure(policy, item))
                )
                if predicate(with_item):
                    return descend_item(item)
                before = with_item
                index += _item_size(item)
            return None

        def scan_tree(tree: _Tree) -> SequenceLocate[T, M] | None:
            nonlocal before, index
            if tree is None:
                return None
            if isinstance(tree, _Single):
                return scan_items((tree.item,))
            with_tree = policy.combine(cast("M", before), cast("M", _tree_measure(policy, tree)))
            if not predicate(with_tree):
                before = with_tree
                index += tree.size
                return None
            found = scan_items(tree.prefix)
            if found is None:
                found = scan_tree(cast("_Tree", tree.middle.force()))
            if found is None:
                found = scan_items(tree.suffix)
            return found

        found = scan_tree(self._root)
        if found is not None:
            return found
        return SequenceLocate(len(self), cast("M", before), None, False)

    def lower_bound(self, probe: T, comparator: Callable[[T, T], int]) -> int:
        """Index of the first element not ordering below ``probe``, assuming the sequence is
        sorted. That is, where ``probe`` would be inserted before its equals. The sequence must
        already be sorted by ``comparator``; this neither checks nor establishes that.
        """

        return self._bound(probe, comparator, strict=False)

    def upper_bound(self, probe: T, comparator: Callable[[T, T], int]) -> int:
        """Index of the first element ordering above ``probe``, assuming the sequence is sorted.

        That is, where ``probe`` would be inserted after its equals.
        """

        return self._bound(probe, comparator, strict=True)

    def _bound(self, probe: T, comparator: Callable[[T, T], int], strict: bool) -> int:
        """One descent guided by each item's cached last element: O(log n)."""

        threshold = 1 if strict else 0

        def reaches(item: object) -> bool:
            return comparator(cast("T", _item_last(item)), probe) >= threshold

        index = 0

        def descend_item(item: object) -> int:
            nonlocal index
            while isinstance(item, _Node):
                for child in item.children:
                    if reaches(child):
                        item = child
                        break
                    index += _item_size(child)
            return index

        def scan_items(items: Iterable[object]) -> int | None:
            nonlocal index
            for item in items:
                if reaches(item):
                    return descend_item(item)
                index += _item_size(item)
            return None

        def scan_tree(tree: _Tree) -> int | None:
            nonlocal index
            if tree is None:
                return None
            if isinstance(tree, _Single):
                return scan_items((tree.item,))
            found = scan_items(tree.prefix)
            if found is not None:
                return found
            middle = tree.middle
            if tree.middle_size > 0:
                forced = cast("_Tree", middle.force())
                assert forced is not None
                if reaches(_tree_last_item(forced)):
                    found = scan_tree(forced)
                    if found is not None:
                        return found
                else:
                    index += tree.middle_size
            found = scan_items(tree.suffix)
            return found

        found = scan_tree(self._root)
        return len(self) if found is None else found

    def reversed_view(self) -> MeasuredSequence[T, M]:
        """Return the sequence in reverse order, sharing storage lazily.

        O(1) immediate work: digits mirror eagerly and the middle's reversal rides a suspension.
        Correct under **every** monoid, commutative or not: a mirrored node's summary is
        recombined in the mirrored order rather than reused, at O(1) combines per node paid only
        when the reversal suspension forces. (An earlier revision reused cached measures under a
        documented commutative-only contract; the TypeScript port strengthened the guarantee and
        this workspace was equalized upward to match.)
        """

        return MeasuredSequence(_reverse_tree(self.policy, self._root), self.policy)

    def shares_structure_with(self, other: MeasuredSequence[T, M]) -> bool:
        """Whether the two sequences have any node in common by object identity.

        A representation probe for structural-sharing tests, not an equality test. The walk
        forces suspended middles: deferred structure captures shared subtrees inside pending
        suspensions where no identity walk can see them, so a non-forcing probe would report
        false negatives. Forcing replays the deferred construction work - once, memoized, shared
        by every version - so probing an already-forced spine costs nothing.
        """

        if self._root is other._root:
            # Two empty sequences share by root identity, matching the join-tree convention.
            return True
        if self._root is None or other._root is None:
            return False
        mine: set[int] = set()
        _collect_ids(self._root, mine)
        theirs: set[int] = set()
        _collect_ids(other._root, theirs)
        return not mine.isdisjoint(theirs)

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order."""

        return list(self)

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order. O(n) total."""

        return cast("Iterator[T]", _iter_tree(self._root))


def _tree_last_item(tree: _Tree) -> object:
    assert tree is not None
    if isinstance(tree, _Single):
        return tree.item
    return tree.suffix[-1]


def _collect_ids(tree: _Tree, into: set[int]) -> None:
    stack: list[object] = [tree]
    while stack:
        current = stack.pop()
        if current is None:
            continue
        if isinstance(current, _Single):
            into.add(id(current))
            if isinstance(current.item, _Node):
                stack.append(current.item)
        elif isinstance(current, _Deep):
            into.add(id(current))
            if current.middle is not _EMPTY_SUSP:
                # The module-level empty-middle singleton appears in every shallow deep node;
                # counting it would make any two nonempty sequences "share".
                into.add(id(current.middle))
            for item in (*current.prefix, *current.suffix):
                if isinstance(item, _Node):
                    stack.append(item)
            stack.append(cast("_Tree", current.middle.force()))
        elif isinstance(current, _Node):
            into.add(id(current))
            for child in current.children:
                if isinstance(child, _Node):
                    stack.append(child)


def _iter_item(item: object) -> Iterator[object]:
    if isinstance(item, _Node):
        for child in item.children:
            yield from _iter_item(child)
    else:
        yield item


def _iter_tree(tree: _Tree) -> Iterator[object]:
    if tree is None:
        return
    if isinstance(tree, _Single):
        yield from _iter_item(tree.item)
        return
    for item in tree.prefix:
        yield from _iter_item(item)
    yield from _iter_tree(cast("_Tree", tree.middle.force()))
    for item in tree.suffix:
        yield from _iter_item(item)


__all__ = ["MeasuredSequence", "SequenceLocate", "SequenceSplit"]
