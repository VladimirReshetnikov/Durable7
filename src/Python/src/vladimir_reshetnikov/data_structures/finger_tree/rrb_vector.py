"""Persistent relaxed radix-balanced vectors with 32-way branching."""

from __future__ import annotations

import sys
from collections.abc import Iterable, Iterator, Sequence
from dataclasses import dataclass
from typing import Generic, TypeVar, overload

T = TypeVar("T")

_BRANCH_FACTOR = 32
_RADIX_BITS = 5


@dataclass(frozen=True, slots=True)
class _Leaf(Generic[T]):
    items: tuple[T, ...]
    count: int
    height: int = 0

    def __init__(self, items: Iterable[T]) -> None:
        materialized = tuple(items)
        if not 1 <= len(materialized) <= _BRANCH_FACTOR:
            raise ValueError("Invalid RRB leaf size.")
        object.__setattr__(self, "items", materialized)
        object.__setattr__(self, "count", len(materialized))
        object.__setattr__(self, "height", 0)


@dataclass(frozen=True, slots=True)
class _Branch(Generic[T]):
    children: tuple[_Node[T], ...]
    height: int
    count: int
    cumulative_sizes: tuple[int, ...] | None

    def __init__(self, children: Iterable[_Node[T]]) -> None:
        materialized = tuple(children)
        if not 1 <= len(materialized) <= _BRANCH_FACTOR:
            raise ValueError("Invalid RRB branch factor.")
        child_height = materialized[0].height
        if any(child.height != child_height for child in materialized):
            raise ValueError("RRB siblings must have equal heights.")
        height = child_height + 1
        count = 0
        for child in materialized:
            count = _checked_count(count, child.count)
        object.__setattr__(self, "children", materialized)
        object.__setattr__(self, "height", height)
        object.__setattr__(self, "count", count)
        object.__setattr__(
            self,
            "cumulative_sizes",
            None if _has_regular_layout(materialized, height) else _build_sizes(materialized),
        )

    def find_child(self, index: int) -> tuple[int, int]:
        if index < 0 or index >= self.count:
            raise IndexError("RRB child index is outside the branch.")
        if self.cumulative_sizes is None:
            capacity = 1 << (self.height * _RADIX_BITS)
            child_index = index // capacity
            return child_index, child_index * capacity
        low = 0
        high = len(self.cumulative_sizes) - 1
        while low < high:
            middle = (low + high) // 2
            if index < self.cumulative_sizes[middle]:
                high = middle
            else:
                low = middle + 1
        return low, 0 if low == 0 else self.cumulative_sizes[low - 1]


_Node = _Leaf[T] | _Branch[T]


def _checked_count(left: int, right: int) -> int:
    result = left + right
    if result > sys.maxsize:
        raise OverflowError("An RRB vector cannot exceed sys.maxsize elements.")
    return result


def _has_regular_layout(children: Sequence[_Node[T]], height: int) -> bool:
    capacity = 1 << (height * _RADIX_BITS)
    return (
        all(child.count == capacity for child in children[:-1]) and children[-1].count <= capacity
    )


def _build_sizes(children: Sequence[_Node[T]]) -> tuple[int, ...]:
    sizes: list[int] = []
    count = 0
    for child in children:
        count = _checked_count(count, child.count)
        sizes.append(count)
    return tuple(sizes)


def _get_node(root: _Node[T], initial_index: int) -> T:
    node = root
    index = initial_index
    while isinstance(node, _Branch):
        child_index, before = node.find_child(index)
        index -= before
        node = node.children[child_index]
    return node.items[index]


def _set_node(node: _Node[T], index: int, value: T) -> _Node[T]:
    if isinstance(node, _Leaf):
        if node.items[index] is value:
            return node
        items = list(node.items)
        items[index] = value
        return _Leaf(items)
    child_index, before = node.find_child(index)
    current = node.children[child_index]
    child = _set_node(current, index - before, value)
    if child is current:
        return node
    children = list(node.children)
    children[child_index] = child
    return _Branch(children)


def _partition(children: Sequence[_Node[T]]) -> tuple[_Node[T], ...]:
    if len(children) <= _BRANCH_FACTOR:
        return (_Branch(children),)
    split = len(children) // 2
    return _Branch(children[:split]), _Branch(children[split:])


def _concat_same_height(left: _Node[T], right: _Node[T]) -> tuple[_Node[T], ...]:
    if isinstance(left, _Leaf):
        if not isinstance(right, _Leaf):
            raise AssertionError("RRB height mismatch.")
        if left.count == _BRANCH_FACTOR and right.count == _BRANCH_FACTOR:
            return left, right
        combined = left.items + right.items
        if len(combined) <= _BRANCH_FACTOR:
            return (_Leaf(combined),)
        split = len(combined) // 2
        return _Leaf(combined[:split]), _Leaf(combined[split:])
    if not isinstance(right, _Branch):
        raise AssertionError("RRB height mismatch.")
    boundary = _concat_same_height(left.children[-1], right.children[0])
    return _partition((*left.children[:-1], *boundary, *right.children[1:]))


def _concat_nodes(left: _Node[T], right: _Node[T]) -> tuple[_Node[T], ...]:
    if left.height == right.height:
        return _concat_same_height(left, right)
    if left.height > right.height:
        if not isinstance(left, _Branch):
            raise AssertionError("RRB height mismatch.")
        boundary = _concat_nodes(left.children[-1], right)
        return _partition((*left.children[:-1], *boundary))
    if not isinstance(right, _Branch):
        raise AssertionError("RRB height mismatch.")
    boundary = _concat_nodes(left, right.children[0])
    return _partition((*boundary, *right.children[1:]))


def _build_same_height(nodes: Sequence[_Node[T]]) -> _Node[T] | None:
    return None if not nodes else _Branch(nodes)


def _split_node(node: _Node[T], index: int) -> tuple[_Node[T] | None, _Node[T] | None]:
    if index == 0:
        return None, node
    if index == node.count:
        return node, None
    if isinstance(node, _Leaf):
        return _Leaf(node.items[:index]), _Leaf(node.items[index:])
    child_index, before = node.find_child(index)
    child_left, child_right = _split_node(node.children[child_index], index - before)
    left = (*node.children[:child_index], *((child_left,) if child_left is not None else ()))
    right = (
        *((child_right,) if child_right is not None else ()),
        *node.children[child_index + 1 :],
    )
    return _build_same_height(left), _build_same_height(right)


def _build_level(nodes: Sequence[_Node[T]]) -> _Node[T]:
    if not nodes:
        raise ValueError("Cannot build an empty RRB level.")
    level = list(nodes)
    while len(level) > 1:
        level = [
            _Branch(level[index : index + _BRANCH_FACTOR])
            for index in range(0, len(level), _BRANCH_FACTOR)
        ]
    return level[0]


def _normalize_root(candidate: _Node[T] | None) -> _Node[T] | None:
    node = candidate
    while isinstance(node, _Branch) and len(node.children) == 1:
        node = node.children[0]
    return node


def _iterate(root: _Node[T]) -> Iterator[T]:
    stack = [root]
    while stack:
        node = stack.pop()
        if isinstance(node, _Leaf):
            yield from node.items
        else:
            stack.extend(reversed(node.children))


@dataclass(frozen=True, slots=True)
class RrbPop(Generic[T]):
    value: T
    rest: RrbVector[T]


@dataclass(frozen=True, slots=True)
class RrbVectorSplit(Generic[T]):
    left: RrbVector[T]
    right: RrbVector[T]


@dataclass(frozen=True, slots=True)
class RrbVectorStatistics:
    count: int
    height: int
    leaf_count: int
    branch_count: int
    regular_branch_count: int
    relaxed_branch_count: int
    minimum_leaf_length: int
    maximum_leaf_length: int
    minimum_branching_factor: int
    maximum_branching_factor: int


class RrbVector(Generic[T]):
    """Immutable relaxed radix-balanced vector with 32-way branches."""

    __slots__ = ("_root",)

    def __init__(self, root: _Node[T] | None = None) -> None:
        self._root = _normalize_root(root)

    @classmethod
    def empty(cls) -> RrbVector[T]:
        return cls()

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> RrbVector[T]:
        if isinstance(values, RrbVector):
            return values
        return RrbVectorBuilder[T]().append_all(values).to_immutable()

    @classmethod
    def builder(cls) -> RrbVectorBuilder[T]:
        return RrbVectorBuilder()

    def __len__(self) -> int:
        return 0 if self._root is None else self._root.count

    @property
    def size(self) -> int:
        return len(self)

    @property
    def is_empty(self) -> bool:
        return self._root is None

    @property
    def height(self) -> int:
        return 0 if self._root is None else self._root.height

    def get(self, index: int) -> T | None:
        if index < 0 or index >= len(self):
            return None
        if self._root is None:
            raise AssertionError("A validated nonempty vector has no root.")
        return _get_node(self._root, index)

    @overload
    def __getitem__(self, index: int) -> T: ...

    @overload
    def __getitem__(self, index: slice) -> RrbVector[T]: ...

    def __getitem__(self, index: int | slice) -> T | RrbVector[T]:
        if isinstance(index, slice):
            start, stop, step = index.indices(len(self))
            if step != 1:
                return RrbVector.from_iterable(self.to_list()[index])
            split_start = self.split_at(start)
            if split_start is None:
                raise AssertionError("Normalized slice start failed.")
            split_length = split_start.right.split_at(stop - start)
            if split_length is None:
                raise AssertionError("Normalized slice length failed.")
            return split_length.left
        normalized = index + len(self) if index < 0 else index
        value = self.get(normalized)
        if normalized < 0 or normalized >= len(self):
            raise IndexError("RRB vector index out of range.")
        return value  # type: ignore[return-value]

    def front(self) -> T | None:
        return self.get(0)

    def back(self) -> T | None:
        return self.get(len(self) - 1)

    def append(self, value: T) -> RrbVector[T]:
        return self.concat(RrbVector(_Leaf((value,))))

    def prepend(self, value: T) -> RrbVector[T]:
        return RrbVector(_Leaf((value,))).concat(self)

    def set_item(self, index: int, value: T) -> RrbVector[T] | None:
        if index < 0 or index >= len(self):
            return None
        if self._root is None:
            raise AssertionError("A validated nonempty vector has no root.")
        root = _set_node(self._root, index, value)
        return self if root is self._root else RrbVector(root)

    def concat(self, other: RrbVector[T]) -> RrbVector[T]:
        if self._root is None:
            return other
        if other._root is None:
            return self
        _checked_count(len(self), len(other))
        roots = _concat_nodes(self._root, other._root)
        return RrbVector(roots[0] if len(roots) == 1 else _Branch(roots))

    def split_at(self, index: int) -> RrbVectorSplit[T] | None:
        if index < 0 or index > len(self):
            return None
        if index == 0:
            return RrbVectorSplit(RrbVector.empty(), self)
        if index == len(self):
            return RrbVectorSplit(self, RrbVector.empty())
        if self._root is None:
            raise AssertionError("A validated nonempty vector has no root.")
        left, right = _split_node(self._root, index)
        return RrbVectorSplit(RrbVector(left), RrbVector(right))

    def insert_at(self, index: int, value: T) -> RrbVector[T] | None:
        return self.insert_range(index, (value,))

    def insert_range(self, index: int, values: Iterable[T]) -> RrbVector[T] | None:
        split = self.split_at(index)
        if split is None:
            return None
        middle = RrbVector.from_iterable(values)
        return self if middle.is_empty else split.left.concat(middle).concat(split.right)

    def remove_at(self, index: int) -> RrbVector[T] | None:
        return self.remove_range(index, 1)

    def remove_range(self, index: int, count: int) -> RrbVector[T] | None:
        if index < 0 or count < 0 or index + count > len(self):
            return None
        if count == 0:
            return self
        first = self.split_at(index)
        if first is None:
            raise AssertionError("Validated first split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated second split failed.")
        return first.left.concat(second.right)

    def try_remove_last(self) -> RrbPop[T] | None:
        if self.is_empty:
            return None
        value = self.back()
        rest = self.remove_at(len(self) - 1)
        if rest is None:
            raise AssertionError("Validated last removal failed.")
        return RrbPop(value, rest)  # type: ignore[arg-type]

    def clear(self) -> RrbVector[T]:
        return self if self.is_empty else RrbVector.empty()

    def to_builder(self) -> RrbVectorBuilder[T]:
        return RrbVectorBuilder(self)

    def to_list(self) -> list[T]:
        return list(self)

    def shares_root_with(self, other: RrbVector[T]) -> bool:
        return self._root is other._root

    def shared_leaf_count(self, other: RrbVector[T]) -> int:
        leaves: set[int] = set()
        stack = [] if self._root is None else [self._root]
        while stack:
            node = stack.pop()
            if isinstance(node, _Leaf):
                leaves.add(id(node))
            else:
                stack.extend(node.children)
        count = 0
        probe = [] if other._root is None else [other._root]
        while probe:
            node = probe.pop()
            if isinstance(node, _Leaf):
                count += id(node) in leaves
            else:
                probe.extend(node.children)
        return count

    def validate_structure(self) -> RrbVectorStatistics:
        if self._root is None:
            return RrbVectorStatistics(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        leaf_count = branch_count = regular = relaxed = 0
        min_leaf = _BRANCH_FACTOR
        max_leaf = 0
        min_branch = _BRANCH_FACTOR
        max_branch = 0

        def visit(node: _Node[T], is_root: bool) -> tuple[int, int]:
            nonlocal leaf_count, branch_count, regular, relaxed
            nonlocal min_leaf, max_leaf, min_branch, max_branch
            if isinstance(node, _Leaf):
                if not 1 <= node.count <= _BRANCH_FACTOR or node.count != len(node.items):
                    raise ValueError("Invalid RRB leaf size.")
                leaf_count += 1
                min_leaf = min(min_leaf, node.count)
                max_leaf = max(max_leaf, node.count)
                return node.count, 0
            if not 1 <= len(node.children) <= _BRANCH_FACTOR:
                raise ValueError("Invalid RRB branch factor.")
            if is_root and len(node.children) == 1:
                raise ValueError("An RRB root branch must have at least two children.")
            branch_count += 1
            min_branch = min(min_branch, len(node.children))
            max_branch = max(max_branch, len(node.children))
            if node.cumulative_sizes is None:
                regular += 1
            else:
                relaxed += 1
            results = [visit(child, False) for child in node.children]
            if any(height != results[0][1] for _, height in results):
                raise ValueError("RRB sibling height mismatch.")
            count = sum(child_count for child_count, _ in results)
            height = results[0][1] + 1
            if count != node.count or height != node.height:
                raise ValueError("Invalid RRB metadata.")
            expected_regular = _has_regular_layout(node.children, height)
            if (node.cumulative_sizes is None) != expected_regular:
                raise ValueError("Invalid RRB regularity metadata.")
            if node.cumulative_sizes is not None and node.cumulative_sizes != _build_sizes(
                node.children
            ):
                raise ValueError("Invalid RRB size table.")
            return count, height

        count, height = visit(self._root, True)
        return RrbVectorStatistics(
            count,
            height,
            leaf_count,
            branch_count,
            regular,
            relaxed,
            min_leaf,
            max_leaf,
            0 if branch_count == 0 else min_branch,
            max_branch,
        )

    def __iter__(self) -> Iterator[T]:
        return iter(()) if self._root is None else _iterate(self._root)


class RrbVectorBuilder(Generic[T]):
    """Mutable append staging surface that publishes immutable RRB snapshots."""

    __slots__ = ("_leaves", "_prefix", "_staged_count", "_tail", "_version")

    def __init__(self, prefix: RrbVector[T] | None = None) -> None:
        self._prefix = RrbVector.empty() if prefix is None else prefix
        self._leaves: list[_Node[T]] = []
        self._tail: list[T] = []
        self._staged_count = 0
        self._version = 0

    def __len__(self) -> int:
        return _checked_count(len(self._prefix), self._staged_count)

    @property
    def size(self) -> int:
        return len(self)

    def append(self, value: T) -> RrbVectorBuilder[T]:
        self._tail.append(value)
        self._staged_count += 1
        if len(self._tail) == _BRANCH_FACTOR:
            self._leaves.append(_Leaf(self._tail))
            self._tail = []
        self._version += 1
        return self

    def append_all(self, values: Iterable[T]) -> RrbVectorBuilder[T]:
        for value in values:
            self.append(value)
        return self

    def clear(self) -> RrbVectorBuilder[T]:
        if len(self) != 0:
            self._prefix = RrbVector.empty()
            self._leaves = []
            self._tail = []
            self._staged_count = 0
            self._version += 1
        return self

    def to_immutable(self) -> RrbVector[T]:
        if self._staged_count == 0:
            return self._prefix
        nodes = self._leaves if not self._tail else [*self._leaves, _Leaf(self._tail)]
        staged = RrbVector(_build_level(nodes))
        self._prefix = staged if self._prefix.is_empty else self._prefix.concat(staged)
        self._leaves = []
        self._tail = []
        self._staged_count = 0
        return self._prefix

    def __iter__(self) -> Iterator[T]:
        version = self._version
        for value in self.to_immutable():
            if version != self._version:
                raise RuntimeError("RRB builder modified during iteration.")
            yield value


__all__ = [
    "RrbPop",
    "RrbVector",
    "RrbVectorBuilder",
    "RrbVectorSplit",
    "RrbVectorStatistics",
]
