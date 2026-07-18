"""Persistent measured sequence with algebraic lazy range updates.

The implementation is an immutable implicit AVL tree.  A node's value and cached measure already
include its own pending tag; its children do not.  Structural descent pushes tags by path copying,
whereas reads carry inherited tags as local traversal state and allocate no persistent nodes.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from operator import eq
from typing import Generic, Protocol, TypeVar, cast

from ._range_update_diagnostics import (
    _record_element_apply_callback,
    _record_element_measure_callback,
    _record_facade_allocation,
    _record_identity_test_callback,
    _record_measure_apply_callback,
    _record_measure_combine_callback,
    _record_node_allocation,
    _record_node_visit,
    _record_push,
    _record_rotation,
    _record_subtree_application,
    _record_tag_compose_callback,
)
from .core import SequenceCursorPeek

T = TypeVar("T")
M = TypeVar("M")
U = TypeVar("U")

_INT32_MAX = 2_147_483_647


class RangeUpdateAlgebra(Protocol[T, M, U]):
    """Ordered-measure monoid and compatible range-tag action.

    ``compose(newer, older)`` denotes applying ``older`` first and ``newer`` second.  The policy
    must make ``apply_element`` and ``apply_measure`` actions of that tag monoid, and
    ``apply_measure`` must distribute over ordered ``combine``.  ``is_identity`` is authoritative;
    neither ``identity_tag`` nor any Python value is used as an absence sentinel.
    """

    @property
    def identity(self) -> M: ...

    def combine(self, left: M, right: M) -> M: ...

    def measure(self, element: T) -> M: ...

    @property
    def identity_tag(self) -> U: ...

    def is_identity(self, tag: U) -> bool: ...

    def compose(self, newer: U, older: U) -> U: ...

    def apply_element(self, tag: U, element: T) -> T: ...

    def apply_measure(self, tag: U, measure: M, count: int) -> M: ...


@dataclass(frozen=True, slots=True)
class _FunctionalRangeUpdateAlgebra(Generic[T, M, U]):
    identity: M
    identity_tag: U
    _combine: Callable[[M, M], M]
    _measure: Callable[[T], M]
    _is_identity: Callable[[U], bool]
    _compose: Callable[[U, U], U]
    _apply_element: Callable[[U, T], T]
    _apply_measure: Callable[[U, M, int], M]

    def combine(self, left: M, right: M) -> M:
        return self._combine(left, right)

    def measure(self, element: T) -> M:
        return self._measure(element)

    def is_identity(self, tag: U) -> bool:
        return self._is_identity(tag)

    def compose(self, newer: U, older: U) -> U:
        return self._compose(newer, older)

    def apply_element(self, tag: U, element: T) -> T:
        return self._apply_element(tag, element)

    def apply_measure(self, tag: U, measure: M, count: int) -> M:
        return self._apply_measure(tag, measure, count)


def create_range_update_algebra(
    identity: M,
    identity_tag: U,
    combine: Callable[[M, M], M],
    measure: Callable[[T], M],
    is_identity: Callable[[U], bool],
    compose: Callable[[U, U], U],
    apply_element: Callable[[U, T], T],
    apply_measure: Callable[[U, M, int], M],
) -> RangeUpdateAlgebra[T, M, U]:
    """Creates a runtime range-update algebra from functions."""

    return _FunctionalRangeUpdateAlgebra(
        identity,
        identity_tag,
        combine,
        measure,
        is_identity,
        compose,
        apply_element,
        apply_measure,
    )


@dataclass(frozen=True, slots=True)
class _Node(Generic[T, M, U]):
    value: T
    left: _Node[T, M, U] | None
    right: _Node[T, M, U] | None
    height: int
    count: int
    measure: M
    has_pending_tag: bool
    pending_tag: U | None


@dataclass(slots=True)
class _RangeUpdateContext(Generic[T, M, U]):
    algebra: RangeUpdateAlgebra[T, M, U]
    empty_measure: M
    empty_sequence: RangeUpdateSequence[T, M, U] | None = None


@dataclass(frozen=True, slots=True)
class _RangeUpdateStructureStatistics:
    count: int
    node_count: int
    height: int
    maximum_absolute_balance_factor: int
    pending_tag_node_count: int
    maximum_pending_tag_depth: int


def _height(node: _Node[T, M, U] | None) -> int:
    return 0 if node is None else node.height


def _count(node: _Node[T, M, U] | None) -> int:
    return 0 if node is None else node.count


def _pending_tag(node: _Node[T, M, U]) -> U:
    if not node.has_pending_tag:
        raise AssertionError("A node without a pending marker has no pending tag.")
    return cast(U, node.pending_tag)


def _measure_element(context: _RangeUpdateContext[T, M, U], element: T) -> M:
    _record_element_measure_callback()
    return context.algebra.measure(element)


def _combine_measures(context: _RangeUpdateContext[T, M, U], left: M, right: M) -> M:
    _record_measure_combine_callback()
    return context.algebra.combine(left, right)


def _is_identity_tag(context: _RangeUpdateContext[T, M, U], tag: U) -> bool:
    _record_identity_test_callback()
    return context.algebra.is_identity(tag)


def _compose_tags(context: _RangeUpdateContext[T, M, U], newer: U, older: U) -> U:
    _record_tag_compose_callback()
    return context.algebra.compose(newer, older)


def _apply_element_tag(context: _RangeUpdateContext[T, M, U], tag: U, element: T) -> T:
    _record_element_apply_callback()
    return context.algebra.apply_element(tag, element)


def _apply_measure_tag(context: _RangeUpdateContext[T, M, U], tag: U, measure: M, count: int) -> M:
    _record_measure_apply_callback()
    return context.algebra.apply_measure(tag, measure, count)


def _new_raw_node(
    value: T,
    left: _Node[T, M, U] | None,
    right: _Node[T, M, U] | None,
    height: int,
    count: int,
    measure: M,
    has_pending_tag: bool,
    pending_tag: U | None,
) -> _Node[T, M, U]:
    _record_node_allocation()
    return _Node(
        value,
        left,
        right,
        height,
        count,
        measure,
        has_pending_tag,
        pending_tag,
    )


def _make_node(
    value: T,
    left: _Node[T, M, U] | None,
    right: _Node[T, M, U] | None,
    context: _RangeUpdateContext[T, M, U],
) -> _Node[T, M, U]:
    # Structural impossibility has deterministic precedence over every user-policy callback.
    height = max(_height(left), _height(right)) + 1
    count = _count(left) + 1 + _count(right)
    if count > _INT32_MAX:
        raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")

    measure = _measure_element(context, value)
    if left is not None:
        measure = _combine_measures(context, left.measure, measure)
    if right is not None:
        measure = _combine_measures(context, measure, right.measure)
    return _new_raw_node(value, left, right, height, count, measure, False, None)


def _build_balanced(
    values: list[T],
    start: int,
    count: int,
    context: _RangeUpdateContext[T, M, U],
) -> _Node[T, M, U] | None:
    if count == 0:
        return None
    left_count = count // 2
    middle = start + left_count
    left = _build_balanced(values, start, left_count, context)
    right = _build_balanced(values, middle + 1, count - left_count - 1, context)
    return _make_node(values[middle], left, right, context)


def _child_inheritance(
    node: _Node[T, M, U],
    has_inherited_tag: bool,
    inherited_tag: object,
    context: _RangeUpdateContext[T, M, U],
) -> tuple[bool, object]:
    if not node.has_pending_tag:
        return has_inherited_tag, inherited_tag
    if not has_inherited_tag:
        return True, _pending_tag(node)

    # Ancestor tags are newer than the node-local pending tag.
    child_tag = _compose_tags(context, cast(U, inherited_tag), _pending_tag(node))
    if _is_identity_tag(context, child_tag):
        return False, None
    return True, child_tag


def _get_element(node: _Node[T, M, U], index: int, context: _RangeUpdateContext[T, M, U]) -> T:
    has_inherited_tag = False
    inherited_tag: object = None
    while True:
        _record_node_visit()
        left_count = _count(node.left)
        if index == left_count:
            if has_inherited_tag:
                return _apply_element_tag(context, cast(U, inherited_tag), node.value)
            return node.value

        has_inherited_tag, inherited_tag = _child_inheritance(
            node, has_inherited_tag, inherited_tag, context
        )
        if index < left_count:
            if node.left is None:
                raise AssertionError("Validated lookup reached a missing left child.")
            node = node.left
        else:
            if node.right is None:
                raise AssertionError("Validated lookup reached a missing right child.")
            index -= left_count + 1
            node = node.right


def _apply_subtree(
    node: _Node[T, M, U], newer_tag: U, context: _RangeUpdateContext[T, M, U]
) -> _Node[T, M, U]:
    _record_node_visit()
    _record_subtree_application()
    value = _apply_element_tag(context, newer_tag, node.value)
    measure = _apply_measure_tag(context, newer_tag, node.measure, node.count)

    has_pending_tag = True
    pending_tag: U | None = newer_tag
    if node.has_pending_tag:
        pending_tag = _compose_tags(context, newer_tag, _pending_tag(node))
        if _is_identity_tag(context, pending_tag):
            has_pending_tag = False
            pending_tag = None

    return _new_raw_node(
        value,
        node.left,
        node.right,
        node.height,
        node.count,
        measure,
        has_pending_tag,
        pending_tag,
    )


def _push(node: _Node[T, M, U], context: _RangeUpdateContext[T, M, U]) -> _Node[T, M, U]:
    if not node.has_pending_tag:
        return node
    _record_push()
    tag = _pending_tag(node)
    left = None if node.left is None else _apply_subtree(node.left, tag, context)
    right = None if node.right is None else _apply_subtree(node.right, tag, context)
    return _new_raw_node(
        node.value,
        left,
        right,
        node.height,
        node.count,
        node.measure,
        False,
        None,
    )


def _rotate_left(node: _Node[T, M, U], context: _RangeUpdateContext[T, M, U]) -> _Node[T, M, U]:
    _record_rotation()
    node = _push(node, context)
    if node.right is None:
        raise AssertionError("Invalid AVL left rotation.")
    pivot = _push(node.right, context)
    lower = _make_node(node.value, node.left, pivot.left, context)
    return _make_node(pivot.value, lower, pivot.right, context)


def _rotate_right(node: _Node[T, M, U], context: _RangeUpdateContext[T, M, U]) -> _Node[T, M, U]:
    _record_rotation()
    node = _push(node, context)
    if node.left is None:
        raise AssertionError("Invalid AVL right rotation.")
    pivot = _push(node.left, context)
    lower = _make_node(node.value, pivot.right, node.right, context)
    return _make_node(pivot.value, pivot.left, lower, context)


def _balance(node: _Node[T, M, U], context: _RangeUpdateContext[T, M, U]) -> _Node[T, M, U]:
    factor = _height(node.left) - _height(node.right)
    if factor > 1:
        if node.left is None:
            raise AssertionError("Invalid AVL left imbalance.")
        if _height(node.left.left) < _height(node.left.right):
            node = _push(node, context)
            if node.left is None:
                raise AssertionError("Invalid AVL double rotation.")
            left = _rotate_left(node.left, context)
            node = _make_node(node.value, left, node.right, context)
        return _rotate_right(node, context)

    if factor < -1:
        if node.right is None:
            raise AssertionError("Invalid AVL right imbalance.")
        if _height(node.right.right) < _height(node.right.left):
            node = _push(node, context)
            if node.right is None:
                raise AssertionError("Invalid AVL double rotation.")
            right = _rotate_right(node.right, context)
            node = _make_node(node.value, node.left, right, context)
        return _rotate_left(node, context)
    return node


def _insert(
    node: _Node[T, M, U] | None,
    index: int,
    item: T,
    context: _RangeUpdateContext[T, M, U],
) -> _Node[T, M, U]:
    if node is None:
        return _make_node(item, None, None, context)
    _record_node_visit()
    node = _push(node, context)
    left_count = _count(node.left)
    if index <= left_count:
        left = _insert(node.left, index, item, context)
        return _balance(_make_node(node.value, left, node.right, context), context)
    right = _insert(node.right, index - left_count - 1, item, context)
    return _balance(_make_node(node.value, node.left, right, context), context)


def _set_item(
    node: _Node[T, M, U],
    index: int,
    item: T,
    context: _RangeUpdateContext[T, M, U],
) -> _Node[T, M, U]:
    _record_node_visit()
    node = _push(node, context)
    left_count = _count(node.left)
    if index < left_count:
        if node.left is None:
            raise AssertionError("Validated replacement reached a missing left child.")
        left = _set_item(node.left, index, item, context)
        return _make_node(node.value, left, node.right, context)
    if index > left_count:
        if node.right is None:
            raise AssertionError("Validated replacement reached a missing right child.")
        right = _set_item(node.right, index - left_count - 1, item, context)
        return _make_node(node.value, node.left, right, context)
    return _make_node(item, node.left, node.right, context)


def _extract_minimum(
    node: _Node[T, M, U], context: _RangeUpdateContext[T, M, U]
) -> tuple[T, _Node[T, M, U] | None]:
    _record_node_visit()
    node = _push(node, context)
    if node.left is None:
        return node.value, node.right
    minimum, left = _extract_minimum(node.left, context)
    return minimum, _balance(_make_node(node.value, left, node.right, context), context)


def _remove(
    node: _Node[T, M, U], index: int, context: _RangeUpdateContext[T, M, U]
) -> _Node[T, M, U] | None:
    _record_node_visit()
    node = _push(node, context)
    left_count = _count(node.left)
    if index < left_count:
        if node.left is None:
            raise AssertionError("Validated removal reached a missing left child.")
        left = _remove(node.left, index, context)
        return _balance(_make_node(node.value, left, node.right, context), context)
    if index > left_count:
        if node.right is None:
            raise AssertionError("Validated removal reached a missing right child.")
        right = _remove(node.right, index - left_count - 1, context)
        return _balance(_make_node(node.value, node.left, right, context), context)
    if node.left is None:
        return node.right
    if node.right is None:
        return node.left
    successor, right = _extract_minimum(node.right, context)
    return _balance(_make_node(successor, node.left, right, context), context)


def _join(
    left: _Node[T, M, U] | None,
    pivot: T,
    right: _Node[T, M, U] | None,
    context: _RangeUpdateContext[T, M, U],
) -> _Node[T, M, U]:
    left_height = _height(left)
    right_height = _height(right)
    if left_height <= right_height + 1 and right_height <= left_height + 1:
        return _make_node(pivot, left, right, context)

    _record_node_visit()
    if left_height > right_height:
        if left is None:
            raise AssertionError("Invalid left-heavy AVL join.")
        left = _push(left, context)
        boundary = _join(left.right, pivot, right, context)
        return _balance(_make_node(left.value, left.left, boundary, context), context)

    if right is None:
        raise AssertionError("Invalid right-heavy AVL join.")
    right = _push(right, context)
    leading = _join(left, pivot, right.left, context)
    return _balance(_make_node(right.value, leading, right.right, context), context)


def _join_concatenated(
    left: _Node[T, M, U] | None,
    right: _Node[T, M, U] | None,
    context: _RangeUpdateContext[T, M, U],
) -> _Node[T, M, U] | None:
    if left is None:
        return right
    if right is None:
        return left
    pivot, remainder = _extract_minimum(right, context)
    return _join(left, pivot, remainder, context)


def _split(
    node: _Node[T, M, U] | None,
    index: int,
    context: _RangeUpdateContext[T, M, U],
) -> tuple[_Node[T, M, U] | None, _Node[T, M, U] | None]:
    if node is None:
        return None, None
    if index == 0:
        return None, node
    if index == node.count:
        return node, None

    _record_node_visit()
    node = _push(node, context)
    left_count = _count(node.left)
    if index <= left_count:
        left, middle = _split(node.left, index, context)
        return left, _join(middle, node.value, node.right, context)
    middle_right, right = _split(node.right, index - left_count - 1, context)
    return _join(node.left, node.value, middle_right, context), right


def _measure_range(
    node: _Node[T, M, U],
    index: int,
    count: int,
    has_inherited_tag: bool,
    inherited_tag: object,
    context: _RangeUpdateContext[T, M, U],
) -> M:
    _record_node_visit()
    if index == 0 and count == node.count:
        if has_inherited_tag:
            return _apply_measure_tag(context, cast(U, inherited_tag), node.measure, node.count)
        return node.measure

    end = index + count
    left_count = _count(node.left)
    has_result = False
    result: M | None = None
    child_tag_computed = False
    has_child_tag = False
    child_tag: object = None

    if index < left_count:
        has_child_tag, child_tag = _child_inheritance(
            node, has_inherited_tag, inherited_tag, context
        )
        child_tag_computed = True
        child_count = min(count, left_count - index)
        if node.left is None:
            raise AssertionError("Range query reached a missing left child.")
        result = _measure_range(node.left, index, child_count, has_child_tag, child_tag, context)
        has_result = True

    if index <= left_count and end > left_count:
        singleton = _measure_element(context, node.value)
        if has_inherited_tag:
            singleton = _apply_measure_tag(context, cast(U, inherited_tag), singleton, 1)
        result = (
            singleton if not has_result else _combine_measures(context, cast(M, result), singleton)
        )
        has_result = True

    right_start_at_root = left_count + 1
    if end > right_start_at_root:
        if not child_tag_computed:
            has_child_tag, child_tag = _child_inheritance(
                node, has_inherited_tag, inherited_tag, context
            )
        local_start = max(0, index - right_start_at_root)
        local_end = end - right_start_at_root
        if node.right is None:
            raise AssertionError("Range query reached a missing right child.")
        right_measure = _measure_range(
            node.right,
            local_start,
            local_end - local_start,
            has_child_tag,
            child_tag,
            context,
        )
        result = (
            right_measure
            if not has_result
            else _combine_measures(context, cast(M, result), right_measure)
        )
        has_result = True

    if not has_result:
        raise AssertionError("A nonempty validated measure query produced no result.")
    return cast(M, result)


@dataclass(slots=True)
class _TraversalFrame(Generic[T, M, U]):
    node: _Node[T, M, U]
    has_inherited_tag: bool
    inherited_tag: object
    stage: int = 0
    child_tag_computed: bool = False
    child_has_inherited_tag: bool = False
    child_inherited_tag: object = None


def _iterate(root: _Node[T, M, U], context: _RangeUpdateContext[T, M, U]) -> Iterator[T]:
    stack = [_TraversalFrame(root, False, None)]
    while stack:
        frame = stack[-1]
        if frame.stage == 0:
            frame.stage = 1
            if frame.node.left is not None:
                if not frame.child_tag_computed:
                    (
                        frame.child_has_inherited_tag,
                        frame.child_inherited_tag,
                    ) = _child_inheritance(
                        frame.node,
                        frame.has_inherited_tag,
                        frame.inherited_tag,
                        context,
                    )
                    frame.child_tag_computed = True
                stack.append(
                    _TraversalFrame(
                        frame.node.left,
                        frame.child_has_inherited_tag,
                        frame.child_inherited_tag,
                    )
                )
            continue

        if frame.stage == 1:
            frame.stage = 2
            if frame.has_inherited_tag:
                yield _apply_element_tag(context, cast(U, frame.inherited_tag), frame.node.value)
            else:
                yield frame.node.value
            continue

        if frame.stage == 2:
            frame.stage = 3
            if frame.node.right is not None:
                if not frame.child_tag_computed:
                    (
                        frame.child_has_inherited_tag,
                        frame.child_inherited_tag,
                    ) = _child_inheritance(
                        frame.node,
                        frame.has_inherited_tag,
                        frame.inherited_tag,
                        context,
                    )
                    frame.child_tag_computed = True
                stack.append(
                    _TraversalFrame(
                        frame.node.right,
                        frame.child_has_inherited_tag,
                        frame.child_inherited_tag,
                    )
                )
            continue

        stack.pop()


@dataclass(frozen=True, slots=True)
class RangeUpdateSplit(Generic[T, M, U]):
    """Named result of splitting a range-update sequence at a boundary."""

    left: RangeUpdateSequence[T, M, U]
    right: RangeUpdateSequence[T, M, U]


class RangeUpdateSequence(Generic[T, M, U]):
    """Immutable measured sequence supporting lazily composed contiguous-range updates."""

    __slots__ = ("_context", "_root")

    def __init__(
        self,
        root: _Node[T, M, U] | None,
        context: _RangeUpdateContext[T, M, U],
        *,
        record_allocation: bool = True,
    ) -> None:
        if record_allocation:
            _record_facade_allocation()
        self._root = root
        self._context = context

    @classmethod
    def _create_context(cls, algebra: RangeUpdateAlgebra[T, M, U]) -> _RangeUpdateContext[T, M, U]:
        if algebra is None:
            raise TypeError("algebra must not be None")
        context = _RangeUpdateContext(algebra, algebra.identity)
        empty = cls(None, context, record_allocation=False)
        context.empty_sequence = empty
        return context

    @classmethod
    def empty(cls, algebra: RangeUpdateAlgebra[T, M, U]) -> RangeUpdateSequence[T, M, U]:
        """Creates an empty lineage whose algebra identity is captured exactly once."""

        context = cls._create_context(algebra)
        return cast(RangeUpdateSequence[T, M, U], context.empty_sequence)

    @classmethod
    def from_iterable(
        cls,
        values: Iterable[T],
        algebra: RangeUpdateAlgebra[T, M, U],
    ) -> RangeUpdateSequence[T, M, U]:
        """Eagerly materializes ``values`` once, then builds a minimal-height AVL tree."""

        if algebra is None:
            raise TypeError("algebra must not be None")
        if type(values) is cls and values.algebra is algebra:
            return values
        materialized = list(values)
        if len(materialized) > _INT32_MAX:
            raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")
        context = cls._create_context(algebra)
        if not materialized:
            return cast(RangeUpdateSequence[T, M, U], context.empty_sequence)
        root = _build_balanced(materialized, 0, len(materialized), context)
        if root is None:
            raise AssertionError("A nonempty materialization produced an empty tree.")
        return cls(root, context)

    @property
    def algebra(self) -> RangeUpdateAlgebra[T, M, U]:
        return self._context.algebra

    def __len__(self) -> int:
        return _count(self._root)

    @property
    def is_empty(self) -> bool:
        return self._root is None

    @property
    def measure(self) -> M:
        return self._context.empty_measure if self._root is None else self._root.measure

    def _empty(self) -> RangeUpdateSequence[T, M, U]:
        empty = self._context.empty_sequence
        if empty is None:
            raise AssertionError("A range-update lineage has no canonical empty facade.")
        return empty

    def _wrap(self, root: _Node[T, M, U] | None) -> RangeUpdateSequence[T, M, U]:
        return self._empty() if root is None else RangeUpdateSequence(root, self._context)

    def _check_element_index(self, index: int) -> None:
        if index < 0 or index >= len(self):
            raise IndexError("index is outside the valid range of the sequence")

    def _check_boundary_index(self, index: int) -> None:
        if index < 0 or index > len(self):
            raise IndexError("index is outside the valid boundary range of the sequence")

    def _check_range(self, index: int, count: int) -> None:
        if index < 0:
            raise IndexError("index must be nonnegative")
        if count < 0:
            raise ValueError("count must be nonnegative")
        if index > len(self):
            raise IndexError("index is outside the valid boundary range of the sequence")
        if count > len(self) - index:
            raise ValueError("the range extends past the end of the sequence")

    def get_at(self, index: int) -> T:
        """Returns the logical element at a nonnegative zero-based index."""

        self._check_element_index(index)
        if self._root is None:
            raise AssertionError("A validated element lookup has no root.")
        return _get_element(self._root, index, self._context)

    def __getitem__(self, index: int) -> T:
        if not isinstance(index, int):
            raise TypeError("range-update sequence indices must be integers")
        return self.get_at(index)

    def prepend(self, item: T) -> RangeUpdateSequence[T, M, U]:
        return self.insert(0, item)

    def append(self, item: T) -> RangeUpdateSequence[T, M, U]:
        return self.insert(len(self), item)

    def insert(self, index: int, item: T) -> RangeUpdateSequence[T, M, U]:
        self._check_boundary_index(index)
        if len(self) == _INT32_MAX:
            raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")
        return self._wrap(_insert(self._root, index, item, self._context))

    def set_item(self, index: int, item: T) -> RangeUpdateSequence[T, M, U]:
        self._check_element_index(index)
        if self._root is None:
            raise AssertionError("A validated replacement has no root.")
        return self._wrap(_set_item(self._root, index, item, self._context))

    def remove_at(self, index: int) -> RangeUpdateSequence[T, M, U]:
        self._check_element_index(index)
        if self._root is None:
            raise AssertionError("A validated removal has no root.")
        return self._wrap(_remove(self._root, index, self._context))

    def concat(self, other: RangeUpdateSequence[T, M, U]) -> RangeUpdateSequence[T, M, U]:
        if not isinstance(other, RangeUpdateSequence):
            raise TypeError("other must be a RangeUpdateSequence")
        if self.algebra is not other.algebra:
            raise TypeError("Sequences must retain the same range-update algebra object.")
        if len(other) > _INT32_MAX - len(self):
            raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        if self._root is None or other._root is None:
            raise AssertionError("A nonempty concatenation operand has no root.")
        pivot, right = _extract_minimum(other._root, self._context)
        return self._wrap(_join(self._root, pivot, right, self._context))

    def split_at(self, index: int) -> RangeUpdateSplit[T, M, U]:
        self._check_boundary_index(index)
        if index == 0:
            return RangeUpdateSplit(self._empty(), self)
        if index == len(self):
            return RangeUpdateSplit(self, self._empty())
        left, right = _split(self._root, index, self._context)
        return RangeUpdateSplit(self._wrap(left), self._wrap(right))

    def get_range(self, index: int, count: int) -> RangeUpdateSequence[T, M, U]:
        self._check_range(index, count)
        if count == 0:
            return self._empty()
        if count == len(self):
            return self
        _, tail = _split(self._root, index, self._context)
        middle, _ = _split(tail, count, self._context)
        return self._wrap(middle)

    def apply_range(self, index: int, count: int, tag: U) -> RangeUpdateSequence[T, M, U]:
        self._check_range(index, count)
        if count == 0:
            return self
        if _is_identity_tag(self._context, tag):
            return self
        if self._root is None:
            raise AssertionError("A nonempty validated update has no root.")
        if count == len(self):
            return self._wrap(_apply_subtree(self._root, tag, self._context))

        left, tail = _split(self._root, index, self._context)
        middle, right = _split(tail, count, self._context)
        if middle is None:
            raise AssertionError("A nonempty validated update isolated no middle tree.")
        middle = _apply_subtree(middle, tag, self._context)
        root = _join_concatenated(left, middle, self._context)
        return self._wrap(_join_concatenated(root, right, self._context))

    def measure_range(self, index: int, count: int) -> M:
        self._check_range(index, count)
        if count == 0:
            return self._context.empty_measure
        if self._root is None:
            raise AssertionError("A nonempty validated measure query has no root.")
        if count == len(self):
            return self._root.measure
        return _measure_range(self._root, index, count, False, None, self._context)

    def __iter__(self) -> Iterator[T]:
        if self._root is None:
            return iter(())
        return _iterate(self._root, self._context)

    def to_list(self) -> list[T]:
        return list(self)

    @property
    def _root_identity(self) -> object | None:
        return self._root

    def _node_identities(self) -> tuple[object, ...]:
        if self._root is None:
            return ()
        result: list[object] = []
        stack = [self._root]
        while stack:
            node = stack.pop()
            result.append(node)
            if node.right is not None:
                stack.append(node.right)
            if node.left is not None:
                stack.append(node.left)
        return tuple(result)

    def _count_shared_nodes(self, other: RangeUpdateSequence[T, M, U]) -> int:
        if not isinstance(other, RangeUpdateSequence):
            raise TypeError("other must be a RangeUpdateSequence")
        if self._root is None or other._root is None:
            return 0
        mine: set[int] = set()
        mine_stack = [self._root]
        while mine_stack:
            node = mine_stack.pop()
            mine.add(id(node))
            if node.left is not None:
                mine_stack.append(node.left)
            if node.right is not None:
                mine_stack.append(node.right)
        shared = 0
        stack = [other._root]
        while stack:
            node = stack.pop()
            if id(node) in mine:
                shared += 1
            if node.left is not None:
                stack.append(node.left)
            if node.right is not None:
                stack.append(node.right)
        return shared

    def _structure_statistics(self) -> _RangeUpdateStructureStatistics:
        if self._root is None:
            return _RangeUpdateStructureStatistics(0, 0, 0, 0, 0, 0)
        node_count = 0
        maximum_balance = 0
        pending_count = 0
        maximum_pending_depth = 0
        stack = [(self._root, 0)]
        while stack:
            node, pending_depth = stack.pop()
            node_count += 1
            maximum_balance = max(maximum_balance, abs(_height(node.left) - _height(node.right)))
            if node.has_pending_tag:
                pending_count += 1
                pending_depth += 1
                maximum_pending_depth = max(maximum_pending_depth, pending_depth)
            if node.right is not None:
                stack.append((node.right, pending_depth))
            if node.left is not None:
                stack.append((node.left, pending_depth))
        return _RangeUpdateStructureStatistics(
            len(self),
            node_count,
            self._root.height,
            maximum_balance,
            pending_count,
            maximum_pending_depth,
        )

    def _validate_invariants(
        self, measure_equals: Callable[[M, M], bool] = eq
    ) -> _RangeUpdateStructureStatistics:
        if measure_equals is None:
            raise TypeError("measure_equals must not be None")
        if self._root is None:
            return self._structure_statistics()
        validated_count, validated_height = _validate_node(
            self._root, self._context, measure_equals
        )
        if validated_count != len(self):
            raise RuntimeError(
                f"Root count {len(self)} disagrees with recomputed count {validated_count}."
            )
        if validated_height != self._root.height:
            raise RuntimeError(
                f"Root height {self._root.height} disagrees with recomputed height "
                f"{validated_height}."
            )
        report = self._structure_statistics()
        if report.node_count != len(self):
            raise RuntimeError(
                f"Reachable node count {report.node_count} disagrees with root count {len(self)}."
            )
        return report

    def get_cursor(self, position: int = 0) -> RangeUpdateSequenceCursor[T, M, U]:
        """Creates an immutable logical gap cursor with directional range operations."""

        return RangeUpdateSequenceCursor(self, position)


@dataclass(frozen=True, slots=True)
class RangeUpdateSequenceCursor(Generic[T, M, U]):
    """Immutable positional and measured cursor over a lazy range-update sequence."""

    _snapshot: RangeUpdateSequence[T, M, U]
    position: int = 0

    def __post_init__(self) -> None:
        self._snapshot._check_boundary_index(self.position)

    @property
    def count(self) -> int:
        return len(self._snapshot)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    @property
    def measure_before(self) -> M:
        return self._snapshot.measure_range(0, self.position)

    @property
    def measure_after(self) -> M:
        return self._snapshot.measure_range(self.position, self.count - self.position)

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        return None if self.is_at_start else SequenceCursorPeek(self._snapshot[self.position - 1])

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        return None if self.is_at_end else SequenceCursorPeek(self._snapshot[self.position])

    def move_previous(self) -> RangeUpdateSequenceCursor[T, M, U]:
        if self.is_at_start:
            raise IndexError("range-update cursor is already at the start")
        return RangeUpdateSequenceCursor(self._snapshot, self.position - 1)

    def move_next(self) -> RangeUpdateSequenceCursor[T, M, U]:
        if self.is_at_end:
            raise IndexError("range-update cursor is already at the end")
        return RangeUpdateSequenceCursor(self._snapshot, self.position + 1)

    def seek(self, position: int) -> RangeUpdateSequenceCursor[T, M, U]:
        return (
            self
            if position == self.position
            else RangeUpdateSequenceCursor(self._snapshot, position)
        )

    def insert(self, value: T) -> RangeUpdateSequenceCursor[T, M, U]:
        return RangeUpdateSequenceCursor(
            self._snapshot.insert(self.position, value), self.position + 1
        )

    def delete_previous(self) -> RangeUpdateSequenceCursor[T, M, U]:
        if self.is_at_start:
            raise IndexError("range-update cursor has no previous element")
        return RangeUpdateSequenceCursor(
            self._snapshot.remove_at(self.position - 1), self.position - 1
        )

    def delete_next(self) -> RangeUpdateSequenceCursor[T, M, U]:
        if self.is_at_end:
            raise IndexError("range-update cursor has no next element")
        return RangeUpdateSequenceCursor(self._snapshot.remove_at(self.position), self.position)

    def replace_next(self, value: T) -> RangeUpdateSequenceCursor[T, M, U]:
        if self.is_at_end:
            raise IndexError("range-update cursor has no next element")
        return RangeUpdateSequenceCursor(
            self._snapshot.set_item(self.position, value), self.position
        )

    def measure_previous(self, count: int) -> M:
        self._check_directional_count(count, self.position)
        return self._snapshot.measure_range(self.position - count, count)

    def measure_next(self, count: int) -> M:
        self._check_directional_count(count, self.count - self.position)
        return self._snapshot.measure_range(self.position, count)

    def apply_previous(self, count: int, tag: U) -> RangeUpdateSequenceCursor[T, M, U]:
        self._check_directional_count(count, self.position)
        snapshot = self._snapshot.apply_range(self.position - count, count, tag)
        return (
            self
            if snapshot is self._snapshot
            else RangeUpdateSequenceCursor(snapshot, self.position)
        )

    def apply_next(self, count: int, tag: U) -> RangeUpdateSequenceCursor[T, M, U]:
        self._check_directional_count(count, self.count - self.position)
        snapshot = self._snapshot.apply_range(self.position, count, tag)
        return (
            self
            if snapshot is self._snapshot
            else RangeUpdateSequenceCursor(snapshot, self.position)
        )

    def snapshot(self) -> RangeUpdateSequence[T, M, U]:
        return self._snapshot

    @staticmethod
    def _check_directional_count(count: int, available: int) -> None:
        if count < 0 or count > available:
            raise ValueError("count is outside the available cursor direction")


def _validate_node(
    node: _Node[T, M, U],
    context: _RangeUpdateContext[T, M, U],
    measure_equals: Callable[[M, M], bool],
) -> tuple[int, int]:
    _record_node_visit()
    left_count, left_height = (
        (0, 0) if node.left is None else _validate_node(node.left, context, measure_equals)
    )
    right_count, right_height = (
        (0, 0) if node.right is None else _validate_node(node.right, context, measure_equals)
    )
    expected_height = max(left_height, right_height) + 1
    expected_count = left_count + 1 + right_count
    if expected_count > _INT32_MAX:
        raise RuntimeError("A reachable subtree exceeds the Int32 element-count limit.")
    balance = left_height - right_height
    if abs(balance) > 1:
        raise RuntimeError(
            f"AVL balance factor {balance} is outside [-1, 1] at a node of count {node.count}."
        )
    if node.height != expected_height:
        raise RuntimeError(
            f"Cached height {node.height} disagrees with recomputed height {expected_height}."
        )
    if node.count != expected_count:
        raise RuntimeError(
            f"Cached count {node.count} disagrees with recomputed count {expected_count}."
        )
    if node.has_pending_tag and _is_identity_tag(context, _pending_tag(node)):
        raise RuntimeError("A node retains a pending tag recognized as identity.")

    expected_measure = _measure_element(context, node.value)
    if node.left is not None:
        left_measure = (
            _apply_measure_tag(context, _pending_tag(node), node.left.measure, node.left.count)
            if node.has_pending_tag
            else node.left.measure
        )
        expected_measure = _combine_measures(context, left_measure, expected_measure)
    if node.right is not None:
        right_measure = (
            _apply_measure_tag(context, _pending_tag(node), node.right.measure, node.right.count)
            if node.has_pending_tag
            else node.right.measure
        )
        expected_measure = _combine_measures(context, expected_measure, right_measure)
    if not measure_equals(node.measure, expected_measure):
        raise RuntimeError(
            "Cached logical measure disagrees with the recomputed measure at a node of count "
            f"{node.count}."
        )
    return expected_count, expected_height


__all__ = [
    "RangeUpdateAlgebra",
    "RangeUpdateSequence",
    "RangeUpdateSequenceCursor",
    "RangeUpdateSplit",
    "create_range_update_algebra",
]
