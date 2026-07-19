"""A persistent monoid-measured implicit AVL sequence."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .measures import MeasurePolicy

T = TypeVar("T")
M = TypeVar("M")


@dataclass(frozen=True, slots=True)
class _Node(Generic[T, M]):
    left: _Node[T, M] | None
    value: T
    right: _Node[T, M] | None
    height: int
    size: int
    measure: M


def _height(node: _Node[T, M] | None) -> int:
    return 0 if node is None else node.height


def _size(node: _Node[T, M] | None) -> int:
    return 0 if node is None else node.size


def _make_node(
    left: _Node[T, M] | None,
    value: T,
    right: _Node[T, M] | None,
    policy: MeasurePolicy[T, M],
) -> _Node[T, M]:
    left_measure = policy.identity if left is None else left.measure
    right_measure = policy.identity if right is None else right.measure
    return _Node(
        left,
        value,
        right,
        max(_height(left), _height(right)) + 1,
        _size(left) + _size(right) + 1,
        policy.combine(policy.combine(left_measure, policy.measure(value)), right_measure),
    )


def _balance(
    left: _Node[T, M] | None,
    value: T,
    right: _Node[T, M] | None,
    policy: MeasurePolicy[T, M],
) -> _Node[T, M]:
    if _height(left) > _height(right) + 1:
        if left is None:
            raise AssertionError("Invalid AVL left imbalance.")
        if _height(left.left) >= _height(left.right):
            return _make_node(
                left.left, left.value, _make_node(left.right, value, right, policy), policy
            )
        middle = left.right
        if middle is None:
            raise AssertionError("Invalid AVL double rotation.")
        return _make_node(
            _make_node(left.left, left.value, middle.left, policy),
            middle.value,
            _make_node(middle.right, value, right, policy),
            policy,
        )
    if _height(right) > _height(left) + 1:
        if right is None:
            raise AssertionError("Invalid AVL right imbalance.")
        if _height(right.right) >= _height(right.left):
            return _make_node(
                _make_node(left, value, right.left, policy), right.value, right.right, policy
            )
        middle = right.left
        if middle is None:
            raise AssertionError("Invalid AVL double rotation.")
        return _make_node(
            _make_node(left, value, middle.left, policy),
            middle.value,
            _make_node(middle.right, right.value, right.right, policy),
            policy,
        )
    return _make_node(left, value, right, policy)


def _remove_min(node: _Node[T, M], policy: MeasurePolicy[T, M]) -> tuple[T, _Node[T, M] | None]:
    if node.left is None:
        return node.value, node.right
    value, rest = _remove_min(node.left, policy)
    return value, _balance(rest, node.value, node.right, policy)


def _join(
    left: _Node[T, M] | None,
    value: T,
    right: _Node[T, M] | None,
    policy: MeasurePolicy[T, M],
) -> _Node[T, M]:
    if _height(left) > _height(right) + 1:
        if left is None:
            raise AssertionError("Invalid AVL join.")
        return _balance(left.left, left.value, _join(left.right, value, right, policy), policy)
    if _height(right) > _height(left) + 1:
        if right is None:
            raise AssertionError("Invalid AVL join.")
        return _balance(_join(left, value, right.left, policy), right.value, right.right, policy)
    return _make_node(left, value, right, policy)


def _concat_nodes(
    left: _Node[T, M] | None,
    right: _Node[T, M] | None,
    policy: MeasurePolicy[T, M],
) -> _Node[T, M] | None:
    if left is None:
        return right
    if right is None:
        return left
    value, rest = _remove_min(right, policy)
    return _join(left, value, rest, policy)


def _split_nodes(
    node: _Node[T, M] | None,
    index: int,
    policy: MeasurePolicy[T, M],
) -> tuple[_Node[T, M] | None, _Node[T, M] | None]:
    if node is None:
        return None, None
    left_size = _size(node.left)
    if index <= left_size:
        left, right = _split_nodes(node.left, index, policy)
        return left, _join(right, node.value, node.right, policy)
    left, right = _split_nodes(node.right, index - left_size - 1, policy)
    return _join(node.left, node.value, left, policy), right


def _build_balanced(
    values: list[T], start: int, count: int, policy: MeasurePolicy[T, M]
) -> _Node[T, M] | None:
    if count == 0:
        return None
    left_count = count // 2
    value_index = start + left_count
    return _make_node(
        _build_balanced(values, start, left_count, policy),
        values[value_index],
        _build_balanced(values, value_index + 1, count - left_count - 1, policy),
        policy,
    )


def _iterate_nodes(root: _Node[T, M]) -> Iterator[T]:
    stack: list[_Node[T, M]] = []
    current: _Node[T, M] | None = root
    while current is not None or stack:
        while current is not None:
            stack.append(current)
            current = current.left
        current = stack.pop()
        yield current.value
        current = current.right


@dataclass(frozen=True, slots=True)
class SequenceLocate(Generic[T, M]):
    index: int
    measure_before: M
    value: T | None
    found: bool


@dataclass(frozen=True, slots=True)
class SequenceSplit(Generic[T, M]):
    left: MeasuredSequence[T, M]
    right: MeasuredSequence[T, M]


class MeasuredSequence(Generic[T, M]):
    """Persistent implicit AVL tree with cached monoid measures."""

    __slots__ = ("_root", "policy")

    def __init__(self, root: _Node[T, M] | None, policy: MeasurePolicy[T, M]) -> None:
        self._root = root
        self.policy = policy

    @classmethod
    def empty(cls, policy: MeasurePolicy[T, M]) -> MeasuredSequence[T, M]:
        return cls(None, policy)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], policy: MeasurePolicy[T, M]
    ) -> MeasuredSequence[T, M]:
        materialized = list(values)
        return cls(_build_balanced(materialized, 0, len(materialized), policy), policy)

    def __len__(self) -> int:
        return _size(self._root)

    @property
    def is_empty(self) -> bool:
        return self._root is None

    @property
    def measure(self) -> M:
        return self.policy.identity if self._root is None else self._root.measure

    def front(self) -> T | None:
        return self.at(0)

    def back(self) -> T | None:
        return self.at(len(self) - 1)

    def at(self, index: int) -> T | None:
        if index < 0 or index >= len(self):
            return None
        current = self._root
        remaining = index
        while current is not None:
            left_size = _size(current.left)
            if remaining < left_size:
                current = current.left
            elif remaining == left_size:
                return current.value
            else:
                remaining -= left_size + 1
                current = current.right
        return None

    def prepend(self, value: T) -> MeasuredSequence[T, M]:
        return MeasuredSequence(_join(None, value, self._root, self.policy), self.policy)

    def append(self, value: T) -> MeasuredSequence[T, M]:
        return MeasuredSequence(_join(self._root, value, None, self.policy), self.policy)

    def concat(self, other: MeasuredSequence[T, M]) -> MeasuredSequence[T, M]:
        if self.policy is not other.policy:
            raise TypeError("Sequences must retain the same measure policy object.")
        if other.is_empty:
            return self
        if self.is_empty:
            return other
        return MeasuredSequence(_concat_nodes(self._root, other._root, self.policy), self.policy)

    def split_at(self, index: int) -> SequenceSplit[T, M] | None:
        if index < 0 or index > len(self):
            return None
        if index == 0:
            return SequenceSplit(MeasuredSequence.empty(self.policy), self)
        if index == len(self):
            return SequenceSplit(self, MeasuredSequence.empty(self.policy))
        left, right = _split_nodes(self._root, index, self.policy)
        return SequenceSplit(
            MeasuredSequence(left, self.policy), MeasuredSequence(right, self.policy)
        )

    def insert_at(self, index: int, value: T) -> MeasuredSequence[T, M] | None:
        split = self.split_at(index)
        return None if split is None else split.left.append(value).concat(split.right)

    def set_at(self, index: int, value: T) -> MeasuredSequence[T, M] | None:
        if index < 0 or index >= len(self):
            return None
        split = self.split_at(index)
        if split is None:
            raise AssertionError("Validated split failed.")
        tail = split.right.split_at(1)
        if tail is None:
            raise AssertionError("Validated tail split failed.")
        return split.left.append(value).concat(tail.right)

    def remove_at(self, index: int) -> MeasuredSequence[T, M] | None:
        if index < 0 or index >= len(self):
            return None
        split = self.split_at(index)
        if split is None:
            raise AssertionError("Validated split failed.")
        tail = split.right.split_at(1)
        if tail is None:
            raise AssertionError("Validated tail split failed.")
        return split.left.concat(tail.right)

    def prefix_measure(self, count: int) -> M | None:
        if count < 0 or count > len(self):
            return None
        result = self.policy.identity
        remaining = count
        current = self._root
        while current is not None and remaining > 0:
            left_size = _size(current.left)
            if remaining <= left_size:
                current = current.left
                continue
            left_measure = self.policy.identity if current.left is None else current.left.measure
            result = self.policy.combine(result, left_measure)
            element_measure = self.policy.measure(current.value)
            if remaining == left_size + 1:
                return self.policy.combine(result, element_measure)
            result = self.policy.combine(result, element_measure)
            remaining -= left_size + 1
            current = current.right
        return result

    def locate(self, predicate: Callable[[M], bool]) -> SequenceLocate[T, M]:
        before = self.policy.identity
        index = 0
        current = self._root
        while current is not None:
            left_measure = self.policy.identity if current.left is None else current.left.measure
            through_left = self.policy.combine(before, left_measure)
            if current.left is not None and predicate(through_left):
                current = current.left
                continue
            index += _size(current.left)
            through_value = self.policy.combine(through_left, self.policy.measure(current.value))
            if predicate(through_value):
                return SequenceLocate(index, through_left, current.value, True)
            before = through_value
            index += 1
            current = current.right
        return SequenceLocate(len(self), before, None, False)

    def lower_bound(self, probe: T, comparator: Callable[[T, T], int]) -> int:
        current = self._root
        base = 0
        result = len(self)
        while current is not None:
            index = base + _size(current.left)
            if comparator(current.value, probe) < 0:
                base = index + 1
                current = current.right
            else:
                result = index
                current = current.left
        return result

    def upper_bound(self, probe: T, comparator: Callable[[T, T], int]) -> int:
        current = self._root
        base = 0
        result = len(self)
        while current is not None:
            index = base + _size(current.left)
            if comparator(current.value, probe) <= 0:
                base = index + 1
                current = current.right
            else:
                result = index
                current = current.left
        return result

    def shares_structure_with(self, other: MeasuredSequence[T, M]) -> bool:
        if self._root is other._root:
            return True
        if self._root is None or other._root is None:
            return False
        nodes: set[int] = set()
        stack = [self._root]
        while stack:
            node = stack.pop()
            nodes.add(id(node))
            if node.left is not None:
                stack.append(node.left)
            if node.right is not None:
                stack.append(node.right)
        probe = [other._root]
        while probe:
            node = probe.pop()
            if id(node) in nodes:
                return True
            if node.left is not None:
                probe.append(node.left)
            if node.right is not None:
                probe.append(node.right)
        return False

    def to_list(self) -> list[T]:
        return list(self)

    def __iter__(self) -> Iterator[T]:
        return iter(()) if self._root is None else _iterate_nodes(self._root)


__all__ = ["MeasuredSequence", "SequenceLocate", "SequenceSplit"]
