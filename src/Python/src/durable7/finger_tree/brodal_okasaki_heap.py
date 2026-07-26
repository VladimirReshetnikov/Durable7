"""Persistent bootstrapped skew-binomial priority queues."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .ordering import Comparator, default_comparator

T = TypeVar("T")


@dataclass(frozen=True, slots=True)
class _Tree(Generic[T]):
    rank: int
    value: T
    children: _Forest[T] | None


@dataclass(frozen=True, slots=True)
class _Forest(Generic[T]):
    head: _Tree[T]
    tail: _Forest[T] | None


@dataclass(frozen=True, slots=True)
class _SplitForest(Generic[T]):
    zeros: _Forest[T] | None
    trees: _Forest[T] | None
    embedded_forest: _Forest[T] | None


@dataclass(frozen=True, slots=True)
class BrodalOkasakiHeapStatistics:
    """Shape measurements returned by a successful structural audit."""

    count: int
    root_forest_length: int
    maximum_rank: int
    maximum_depth: int


@dataclass(frozen=True, slots=True)
class BrodalMinimumView(Generic[T]):
    """The smallest element together with the heap that remains once it is removed.

    Returning both at once lets a caller inspect the minimum and continue draining without a
    second descent.
    """

    minimum: T
    remainder: BrodalOkasakiHeap[T]


class BrodalOkasakiHeap(Generic[T]):
    """Immutable bootstrapped skew-binomial min-heap.

    Insert, meld, and minimum are worst-case constant-time in the functional cost model; deleting
    the minimum is logarithmic.  Every operation publishes a new root and preserves prior versions.
    """

    __slots__ = ("_count", "_root", "comparator")

    def __init__(self, root: _Tree[T] | None, count: int, comparator: Comparator[T]) -> None:
        """Wrap an already-built forest; use :meth:`empty` or :meth:`from_iterable` instead.

        The caller is responsible for ``count`` matching ``root`` and for the heap order holding.
        """

        self._root = root
        self._count = count
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> BrodalOkasakiHeap[T]:
        """Return an empty heap ordered by ``comparator``.

        The comparator is retained by identity, and only heaps sharing that exact object may meld.
        """

        return cls(None, 0, comparator)

    @classmethod
    def from_iterable(
        cls,
        items: Iterable[T],
        comparator: Comparator[T] = default_comparator,
    ) -> BrodalOkasakiHeap[T]:
        """Build a heap from ``items`` under ``comparator``."""

        result = cls.empty(comparator)
        for item in items:
            result = result.insert(item)
        return result

    def __len__(self) -> int:
        """Number of elements, matching :attr:`count`."""

        return self._count

    @property
    def count(self) -> int:
        """Number of elements, counting duplicates separately."""

        return self._count

    @property
    def is_empty(self) -> bool:
        """Whether the heap holds no elements."""

        return self._root is None

    @property
    def minimum(self) -> T:
        """The smallest element under the retained comparator.

        Reads the bootstrapped root directly rather than searching the forest. Raises
        :class:`IndexError` when the heap is empty; use :meth:`minimum_view` to test and read at
        once.
        """

        if self._root is None:
            raise IndexError("The heap is empty.")
        return self._root.value

    def _le(self, left: T, right: T) -> bool:
        return self.comparator(left, right) <= 0

    def _skew_link(self, zero: _Tree[T], first: _Tree[T], second: _Tree[T]) -> _Tree[T]:
        if first.rank != second.rank:
            raise ValueError("Invalid skew-link ranks.")
        if self._le(first.value, zero.value) and self._le(first.value, second.value):
            return _Tree(
                first.rank + 1,
                first.value,
                _Forest(zero, _Forest(second, first.children)),
            )
        if self._le(second.value, zero.value) and self._le(second.value, first.value):
            return _Tree(
                second.rank + 1,
                second.value,
                _Forest(zero, _Forest(first, second.children)),
            )
        return _Tree(
            first.rank + 1,
            zero.value,
            _Forest(first, _Forest(second, zero.children)),
        )

    def _skew_insert(self, tree: _Tree[T], forest: _Forest[T] | None) -> _Forest[T]:
        tail = None if forest is None else forest.tail
        if forest is not None and tail is not None and forest.head.rank == tail.head.rank:
            return _Forest(self._skew_link(tree, forest.head, tail.head), tail.tail)
        return _Forest(tree, forest)

    def insert(self, item: T) -> BrodalOkasakiHeap[T]:
        """Return a heap with ``item`` added.

        Either ``item`` becomes the new bootstrapped root or it is skew-linked into the existing
        root's forest, so no forest traversal is needed.
        """

        if self._root is None:
            return BrodalOkasakiHeap(_Tree(0, item, None), 1, self.comparator)
        if self._le(item, self._root.value):
            root = _Tree(0, item, _Forest(self._root, None))
        else:
            root = _Tree(
                0,
                self._root.value,
                self._skew_insert(_Tree(0, item, None), self._root.children),
            )
        return BrodalOkasakiHeap(root, self._count + 1, self.comparator)

    def meld(self, other: BrodalOkasakiHeap[T]) -> BrodalOkasakiHeap[T]:
        """Return a heap holding every element of both operands.

        The two roots are compared and the loser is linked into the winner's forest, so melding does
        not depend on either heap's size. Both heaps must retain the same comparator object; mixing
        comparators raises :class:`TypeError` rather than producing a malformed heap.
        """

        if self.comparator is not other.comparator:
            raise TypeError("Heap melding requires the same comparator object.")
        if self._root is None:
            return other
        if other._root is None:
            return self
        if self._le(self._root.value, other._root.value):
            root = _Tree(
                0,
                self._root.value,
                self._skew_insert(other._root, self._root.children),
            )
        else:
            root = _Tree(
                0,
                other._root.value,
                self._skew_insert(self._root, other._root.children),
            )
        return BrodalOkasakiHeap(root, self._count + other._count, self.comparator)

    def minimum_view(self) -> BrodalMinimumView[T] | None:
        """Return the minimum and the remaining heap, or ``None`` when the heap is empty.

        The presence-safe counterpart of :attr:`minimum`, which raises instead.
        """

        return (
            None
            if self._root is None
            else BrodalMinimumView(self._root.value, self.delete_minimum())
        )

    def _link(self, first: _Tree[T], second: _Tree[T]) -> _Tree[T]:
        if first.rank != second.rank:
            raise ValueError("Invalid binomial-link ranks.")
        if self._le(first.value, second.value):
            return _Tree(first.rank + 1, first.value, _Forest(second, first.children))
        return _Tree(second.rank + 1, second.value, _Forest(first, second.children))

    def _minimum_tree(self, forest: _Forest[T]) -> tuple[_Tree[T], _Forest[T] | None]:
        minimum = forest
        cursor = forest.tail
        while cursor is not None:
            if self.comparator(cursor.head.value, minimum.head.value) < 0:
                minimum = cursor
            cursor = cursor.tail
        prefix: list[_Tree[T]] = []
        cursor = forest
        while cursor is not minimum:
            prefix.append(cursor.head)
            if cursor.tail is None:
                raise AssertionError("The selected minimum must be reachable.")
            cursor = cursor.tail
        remainder = minimum.tail
        for tree in reversed(prefix):
            remainder = _Forest(tree, remainder)
        return minimum.head, remainder

    def _split_forest(self, rank: int, initial: _Forest[T] | None) -> _SplitForest[T]:
        current_rank = rank
        zeros: _Forest[T] | None = None
        trees: _Forest[T] | None = None
        forest = initial
        while True:
            if current_rank == 0:
                return _SplitForest(zeros, trees, forest)
            if forest is None:
                raise ValueError("Invalid skew-binomial forest invariant.")
            tail = forest.tail
            if current_rank == 1 and tail is None:
                return _SplitForest(zeros, _Forest(forest.head, trees), None)
            if tail is None:
                raise ValueError("Invalid skew-binomial forest invariant.")
            first = forest.head
            second = tail.head
            rest = tail.tail
            if current_rank == 1:
                if second.rank == 0:
                    return _SplitForest(_Forest(first, zeros), _Forest(second, trees), rest)
                return _SplitForest(zeros, _Forest(first, trees), _Forest(second, rest))
            if first.rank == second.rank:
                return _SplitForest(zeros, _Forest(first, _Forest(second, trees)), rest)
            if first.rank == 0:
                zeros = _Forest(first, zeros)
                trees = _Forest(second, trees)
                forest = rest
            else:
                trees = _Forest(first, trees)
                forest = _Forest(second, rest)
            current_rank -= 1

    def _insert_ranked(self, tree: _Tree[T], forest: _Forest[T] | None) -> _Forest[T]:
        if forest is None:
            return _Forest(tree, None)
        if tree.rank > forest.head.rank:
            raise ValueError("Invalid ranked insertion order.")
        if tree.rank < forest.head.rank:
            return _Forest(tree, forest)
        return self._insert_ranked(self._link(tree, forest.head), forest.tail)

    def _uniquify(self, forest: _Forest[T] | None) -> _Forest[T] | None:
        if forest is None:
            return None
        # A valid root forest has logarithmic length; recursion therefore stays shallow.
        return self._insert_ranked(forest.head, self._uniquify(forest.tail))

    def _union_unique(self, left: _Forest[T] | None, right: _Forest[T] | None) -> _Forest[T] | None:
        if left is None:
            return right
        if right is None:
            return left
        if left.head.rank < right.head.rank:
            return _Forest(left.head, self._union_unique(left.tail, right))
        if left.head.rank > right.head.rank:
            return _Forest(right.head, self._union_unique(left, right.tail))
        return self._insert_ranked(
            self._link(left.head, right.head), self._union_unique(left.tail, right.tail)
        )

    def _skew_meld(self, left: _Forest[T] | None, right: _Forest[T] | None) -> _Forest[T] | None:
        return self._union_unique(self._uniquify(left), self._uniquify(right))

    def delete_minimum(self) -> BrodalOkasakiHeap[T]:
        """Return the heap without its smallest element.

        Unlike insertion and melding, this must rebuild the root's forest, so it is the one
        operation whose cost grows with the heap's rank. Raises :class:`IndexError` when the heap is
        empty.
        """

        if self._root is None:
            raise IndexError("The heap is empty.")
        if self._root.children is None:
            return BrodalOkasakiHeap.empty(self.comparator)
        minimum, remainder = self._minimum_tree(self._root.children)
        split = self._split_forest(minimum.rank, minimum.children)
        merged = self._skew_meld(self._skew_meld(split.trees, remainder), split.embedded_forest)
        zeros: list[_Tree[T]] = []
        cursor = split.zeros
        while cursor is not None:
            zeros.append(cursor.head)
            cursor = cursor.tail
        for tree in reversed(zeros):
            merged = self._skew_insert(tree, merged)
        return BrodalOkasakiHeap(_Tree(0, minimum.value, merged), self._count - 1, self.comparator)

    def _validate_fused(self, tree: _Tree[T]) -> _Forest[T] | None:
        rank = tree.rank
        forest = tree.children
        while rank > 0:
            if forest is None:
                raise ValueError("Ranked heap tree is missing children.")
            first = forest.head
            if rank == 1:
                if first.rank != 0:
                    raise ValueError("Invalid rank-one child.")
                tail = forest.tail
                if tail is None:
                    return None
                return tail.tail if tail.head.rank == 0 else tail
            tail = forest.tail
            if tail is None:
                raise ValueError("Incomplete fused children.")
            second = tail.head
            if first.rank == second.rank:
                if first.rank != rank - 1:
                    raise ValueError("Invalid skew child ranks.")
                return tail.tail
            if first.rank == 0:
                if second.rank != rank - 1:
                    raise ValueError("Invalid ranked child.")
                forest = tail.tail
            else:
                if first.rank != rank - 1:
                    raise ValueError("Invalid linked child.")
                forest = tail
            rank -= 1
        return forest

    @staticmethod
    def _validate_forest(initial: _Forest[T] | None) -> int:
        length = 0
        previous = -1
        duplicate = False
        forest = initial
        while forest is not None:
            rank = forest.head.rank
            if rank < previous or rank < 0:
                raise ValueError("Invalid heap forest rank order.")
            if rank == previous:
                if length != 1 or duplicate:
                    raise ValueError("Only the first two skew ranks may match.")
                duplicate = True
            previous = rank
            length += 1
            forest = forest.tail
        return length

    def validate_structure(self) -> BrodalOkasakiHeapStatistics:
        """Walk the whole representation and return its shape measurements.

        Checks the bootstrapped root's rank, the skew-binomial forest structure, the heap order
        between every tree and its children, and agreement with the cached count. Raises
        :class:`ValueError` on the first violation. A defensive audit, not part of normal use.
        """

        if self._root is None:
            if self._count != 0:
                raise ValueError("Empty heap count mismatch.")
            return BrodalOkasakiHeapStatistics(0, 0, 0, 0)
        if self._root.rank != 0:
            raise ValueError("Global heap root must have rank zero.")
        root_forest_length = self._validate_forest(self._root.children)
        pending: list[tuple[_Tree[T], int]] = [(self._root, 1)]
        count = maximum_rank = maximum_depth = 0
        while pending:
            tree, depth = pending.pop()
            count += 1
            maximum_rank = max(maximum_rank, tree.rank)
            maximum_depth = max(maximum_depth, depth)
            self._validate_forest(self._validate_fused(tree))
            forest = tree.children
            while forest is not None:
                if not self._le(tree.value, forest.head.value):
                    raise ValueError("Heap order invariant failed.")
                pending.append((forest.head, depth + 1))
                forest = forest.tail
        if count != self._count:
            raise ValueError("Heap logical count mismatch.")
        return BrodalOkasakiHeapStatistics(count, root_forest_length, maximum_rank, maximum_depth)

    def shared_tree_count(self, other: BrodalOkasakiHeap[T]) -> int:
        """Number of tree nodes reachable from both heaps by object identity.

        Used by the tests to show that a derived version really does share structure with the
        version it came from, rather than having been rebuilt.
        """

        def collect(root: _Tree[T] | None) -> set[int]:
            """Gather the identities of every tree node reachable from ``root``."""

            destination: set[int] = set()
            pending = [] if root is None else [root]
            while pending:
                tree = pending.pop()
                identity = id(tree)
                if identity in destination:
                    continue
                destination.add(identity)
                forest = tree.children
                while forest is not None:
                    pending.append(forest.head)
                    forest = forest.tail
            return destination

        return len(collect(self._root) & collect(other._root))

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in unspecified order.

        This is a traversal of the forest, not a sorted drain; repeated :meth:`delete_minimum` gives
        ascending order.
        """

        pending = [] if self._root is None else [self._root]
        while pending:
            tree = pending.pop()
            forest = tree.children
            while forest is not None:
                pending.append(forest.head)
                forest = forest.tail
            yield tree.value


__all__ = [
    "BrodalMinimumView",
    "BrodalOkasakiHeap",
    "BrodalOkasakiHeapStatistics",
]
