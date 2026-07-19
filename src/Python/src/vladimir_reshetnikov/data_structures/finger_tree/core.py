"""Persistent deque, reversible deque, and measured-tree facades."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator, Sequence
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .measured_sequence import MeasuredSequence
from .measures import MeasurePolicy, SizeMeasure

T = TypeVar("T")
M = TypeVar("M")

_SHARED_SIZE_MEASURE: SizeMeasure[object] = SizeMeasure()


def _size_policy() -> SizeMeasure[T]:
    return cast(SizeMeasure[T], _SHARED_SIZE_MEASURE)


@dataclass(frozen=True, slots=True)
class DequeSplit(Generic[T]):
    left: PersistentDeque[T]
    right: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class DequeItemSplit(Generic[T]):
    left: PersistentDeque[T]
    item: T
    right: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class DequeRangeSplit(Generic[T]):
    before: PersistentDeque[T]
    range: PersistentDeque[T]
    after: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class DequePop(Generic[T]):
    value: T
    rest: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class MeasuredSplit(Generic[T, M]):
    left: FingerTree[T, M]
    right: FingerTree[T, M]


@dataclass(frozen=True, slots=True)
class MeasuredItemSplit(Generic[T, M]):
    left: FingerTree[T, M]
    item: T
    right: FingerTree[T, M]


@dataclass(frozen=True, slots=True)
class SequenceCursorPeek(Generic[T]):
    """Present neighbor returned by a sequence cursor, including a stored ``None``."""

    value: T


class PersistentDeque(Generic[T]):
    """Persistent catenable sequence over a measured balanced tree."""

    __slots__ = ("_items",)

    def __init__(self, items: MeasuredSequence[T, int]) -> None:
        self._items = items

    @classmethod
    def empty(cls) -> PersistentDeque[T]:
        return cls(MeasuredSequence.empty(_size_policy()))

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> PersistentDeque[T]:
        return cls(MeasuredSequence.from_iterable(values, _size_policy()))

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    def front(self) -> T | None:
        return self._items.front()

    def back(self) -> T | None:
        return self._items.back()

    def get(self, index: int) -> T | None:
        return self._items.at(index)

    def __getitem__(self, index: int) -> T:
        value = self.get(index)
        if index < 0 or index >= len(self):
            raise IndexError(index)
        return cast(T, value)

    def prepend(self, value: T) -> PersistentDeque[T]:
        return PersistentDeque(self._items.prepend(value))

    def append(self, value: T) -> PersistentDeque[T]:
        return PersistentDeque(self._items.append(value))

    def concat(self, other: PersistentDeque[T]) -> PersistentDeque[T]:
        if other.is_empty:
            return self
        if self.is_empty:
            return other
        return PersistentDeque(self._items.concat(other._items))

    def split_at(self, index: int) -> DequeSplit[T] | None:
        split = self._items.split_at(index)
        if split is None:
            return None
        return DequeSplit(PersistentDeque(split.left), PersistentDeque(split.right))

    def split_item_at(self, index: int) -> DequeItemSplit[T] | None:
        if index < 0 or index >= len(self):
            return None
        first = self.split_at(index)
        if first is None:
            raise AssertionError("Validated split failed.")
        second = first.right.split_at(1)
        if second is None:
            raise AssertionError("Validated item split failed.")
        return DequeItemSplit(first.left, cast(T, first.right.front()), second.right)

    def split_range(self, start: int, count: int) -> DequeRangeSplit[T] | None:
        if start < 0 or count < 0 or start + count > len(self):
            return None
        first = self.split_at(start)
        if first is None:
            raise AssertionError("Validated split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated range split failed.")
        return DequeRangeSplit(first.left, second.left, second.right)

    def insert_at(self, index: int, value: T) -> PersistentDeque[T] | None:
        next_items = self._items.insert_at(index, value)
        return None if next_items is None else PersistentDeque(next_items)

    def set_item(self, index: int, value: T) -> PersistentDeque[T] | None:
        next_items = self._items.set_at(index, value)
        if next_items is None:
            return None
        return self if next_items is self._items else PersistentDeque(next_items)

    def remove_at(self, index: int) -> PersistentDeque[T] | None:
        next_items = self._items.remove_at(index)
        return None if next_items is None else PersistentDeque(next_items)

    def try_view_left(self) -> DequePop[T] | None:
        if self.is_empty:
            return None
        split = self.split_at(1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return DequePop(cast(T, self.front()), split.right)

    def try_view_right(self) -> DequePop[T] | None:
        if self.is_empty:
            return None
        split = self.split_at(len(self) - 1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return DequePop(cast(T, self.back()), split.left)

    def reverse(self) -> PersistentDeque[T]:
        if len(self) < 2:
            return self
        return PersistentDeque.from_iterable(reversed(self.to_list()))

    def to_list(self) -> list[T]:
        return self._items.to_list()

    def shares_storage_with(self, other: PersistentDeque[T]) -> bool:
        return self._items.shares_structure_with(other._items)

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)

    def get_cursor(self, position: int = 0) -> PersistentDequeCursor[T]:
        """Creates an immutable gap cursor at ``position`` in ``0..len(self)``."""

        return PersistentDequeCursor(self, position)


class ReversibleDeque(Generic[T]):
    """Orientation-aware immutable deque with constant-time whole-value reversal."""

    __slots__ = ("_items", "_reversed")

    def __init__(self, items: PersistentDeque[T], reversed_: bool = False) -> None:
        self._items = items
        self._reversed = reversed_

    @classmethod
    def empty(cls) -> ReversibleDeque[T]:
        return cls(PersistentDeque.empty())

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> ReversibleDeque[T]:
        return cls(PersistentDeque.from_iterable(values))

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    def front(self) -> T | None:
        return self._items.back() if self._reversed else self._items.front()

    def back(self) -> T | None:
        return self._items.front() if self._reversed else self._items.back()

    def get(self, index: int) -> T | None:
        if index < 0 or index >= len(self):
            return None
        return self._items.get(len(self) - index - 1 if self._reversed else index)

    def reverse(self) -> ReversibleDeque[T]:
        return ReversibleDeque(self._items, not self._reversed)

    def _aligned_run(self, values: Sequence[T]) -> ReversibleDeque[T]:
        """Builds ``values`` in logical order under this deque's physical orientation.

        A reversed receiver stores the logical range ``[x0..xm-1]`` physically reversed, so a run
        built this way concatenates with either side of a split of this deque through the
        structure-sharing path instead of the orientation-mismatch materialization.
        """

        if not self._reversed:
            return ReversibleDeque(PersistentDeque.from_iterable(values))
        return ReversibleDeque(PersistentDeque.from_iterable(reversed(values)), True)

    def prepend(self, value: T) -> ReversibleDeque[T]:
        items = self._items.append(value) if self._reversed else self._items.prepend(value)
        return ReversibleDeque(items, self._reversed)

    def append(self, value: T) -> ReversibleDeque[T]:
        items = self._items.prepend(value) if self._reversed else self._items.append(value)
        return ReversibleDeque(items, self._reversed)

    def concat(self, other: ReversibleDeque[T]) -> ReversibleDeque[T]:
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        if self._reversed == other._reversed:
            joined = (
                other._items.concat(self._items)
                if self._reversed
                else self._items.concat(other._items)
            )
            return ReversibleDeque(joined, self._reversed)
        return ReversibleDeque.from_iterable((*self, *other))

    def split_at(self, index: int) -> tuple[ReversibleDeque[T], ReversibleDeque[T]] | None:
        if index < 0 or index > len(self):
            return None
        if not self._reversed:
            split = self._items.split_at(index)
            if split is None:
                raise AssertionError("Validated split failed.")
            return ReversibleDeque(split.left), ReversibleDeque(split.right)
        split = self._items.split_at(len(self) - index)
        if split is None:
            raise AssertionError("Validated reverse split failed.")
        return ReversibleDeque(split.right, True), ReversibleDeque(split.left, True)

    def try_view_left(self) -> tuple[T, ReversibleDeque[T]] | None:
        if self.is_empty:
            return None
        split = self.split_at(1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.front()), split[1]

    def try_view_right(self) -> tuple[T, ReversibleDeque[T]] | None:
        if self.is_empty:
            return None
        split = self.split_at(len(self) - 1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.back()), split[0]

    def to_list(self) -> list[T]:
        return list(self)

    def shares_storage_with(self, other: ReversibleDeque[T]) -> bool:
        return self._items.shares_storage_with(other._items)

    def __iter__(self) -> Iterator[T]:
        return iter(self._items) if not self._reversed else reversed(self._items.to_list())

    def get_cursor(self, position: int = 0) -> ReversibleDequeCursor[T]:
        """Creates an immutable cursor at a logical-order gap."""

        return ReversibleDequeCursor(self, position)


@dataclass(frozen=True, slots=True)
class LocateResult(Generic[T, M]):
    index: int
    measure_before: M
    item: T | None
    found: bool


class FingerTree(Generic[T, M]):
    """General persistent monoid-measured sequence."""

    __slots__ = ("_items", "policy")

    def __init__(self, items: MeasuredSequence[T, M], policy: MeasurePolicy[T, M]) -> None:
        self._items = items
        self.policy = policy

    @classmethod
    def empty(cls, policy: MeasurePolicy[T, M]) -> FingerTree[T, M]:
        return cls(MeasuredSequence.empty(policy), policy)

    @classmethod
    def from_iterable(cls, values: Iterable[T], policy: MeasurePolicy[T, M]) -> FingerTree[T, M]:
        return cls(MeasuredSequence.from_iterable(values, policy), policy)

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    @property
    def measure(self) -> M:
        return self._items.measure

    def front(self) -> T | None:
        return self._items.front()

    def back(self) -> T | None:
        return self._items.back()

    def get(self, index: int) -> T | None:
        return self._items.at(index)

    def prepend(self, value: T) -> FingerTree[T, M]:
        return FingerTree(self._items.prepend(value), self.policy)

    def append(self, value: T) -> FingerTree[T, M]:
        return FingerTree(self._items.append(value), self.policy)

    def concat(self, other: FingerTree[T, M]) -> FingerTree[T, M]:
        if self.policy is not other.policy:
            raise TypeError("Trees must retain the same measure policy object.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return FingerTree(self._items.concat(other._items), self.policy)

    def split(self, predicate: Callable[[M], bool]) -> MeasuredSplit[T, M]:
        located = self._items.locate(predicate)
        result = self.split_at_index(located.index if located.found else len(self))
        if result is None:
            raise AssertionError("Located split failed.")
        return result

    def split_at_index(self, index: int) -> MeasuredSplit[T, M] | None:
        split = self._items.split_at(index)
        if split is None:
            return None
        return MeasuredSplit(
            FingerTree(split.left, self.policy), FingerTree(split.right, self.policy)
        )

    def try_split_find(self, predicate: Callable[[M], bool]) -> MeasuredItemSplit[T, M] | None:
        located = self._items.locate(predicate)
        if not located.found:
            return None
        first = self.split_at_index(located.index)
        if first is None:
            raise AssertionError("Located split failed.")
        second = first.right.split_at_index(1)
        if second is None:
            raise AssertionError("Located item split failed.")
        return MeasuredItemSplit(first.left, cast(T, located.value), second.right)

    def prefix_measure(self, count: int) -> M | None:
        return self._items.prefix_measure(count)

    def try_locate(self, predicate: Callable[[M], bool]) -> LocateResult[T, M]:
        result = self._items.locate(predicate)
        return LocateResult(result.index, result.measure_before, result.value, result.found)

    def set_item(self, index: int, value: T) -> FingerTree[T, M] | None:
        next_items = self._items.set_at(index, value)
        return None if next_items is None else FingerTree(next_items, self.policy)

    def insert_at(self, index: int, value: T) -> FingerTree[T, M] | None:
        next_items = self._items.insert_at(index, value)
        return None if next_items is None else FingerTree(next_items, self.policy)

    def remove_at(self, index: int) -> FingerTree[T, M] | None:
        next_items = self._items.remove_at(index)
        return None if next_items is None else FingerTree(next_items, self.policy)

    def try_view_left(self) -> tuple[T, FingerTree[T, M]] | None:
        if self.is_empty:
            return None
        split = self.split_at_index(1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.front()), split.right

    def try_view_right(self) -> tuple[T, FingerTree[T, M]] | None:
        if self.is_empty:
            return None
        split = self.split_at_index(len(self) - 1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.back()), split.left

    def to_list(self) -> list[T]:
        return self._items.to_list()

    def shares_storage_with(self, other: FingerTree[T, M]) -> bool:
        return self._items.shares_structure_with(other._items)

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)

    def get_cursor_at_start(self) -> FingerTreeCursor[T, M]:
        """Creates a measure-aware cursor before the first element."""

        return FingerTreeCursor(self, 0)

    def get_cursor_at_end(self) -> FingerTreeCursor[T, M]:
        """Creates a measure-aware cursor after the final element."""

        return FingerTreeCursor(self, len(self))

    def get_cursor(self, predicate: Callable[[M], bool]) -> tuple[bool, FingerTreeCursor[T, M]]:
        """Locates the first inclusive prefix satisfying a monotone predicate."""

        located = self.try_locate(predicate)
        position = located.index if located.found else len(self)
        return located.found, FingerTreeCursor(self, position)


@dataclass(frozen=True, slots=True)
class PersistentDequeCursor(Generic[T]):
    """Immutable snapshot-plus-position cursor over a persistent deque."""

    _snapshot: PersistentDeque[T]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > len(self._snapshot):
            raise IndexError("cursor position is outside the deque boundary range")

    @property
    def count(self) -> int:
        return len(self._snapshot)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        return None if self.is_at_start else SequenceCursorPeek(self._snapshot[self.position - 1])

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        return None if self.is_at_end else SequenceCursorPeek(self._snapshot[self.position])

    def move_previous(self) -> PersistentDequeCursor[T]:
        if self.is_at_start:
            raise IndexError("deque cursor is already at the start")
        return PersistentDequeCursor(self._snapshot, self.position - 1)

    def move_next(self) -> PersistentDequeCursor[T]:
        if self.is_at_end:
            raise IndexError("deque cursor is already at the end")
        return PersistentDequeCursor(self._snapshot, self.position + 1)

    def seek(self, position: int) -> PersistentDequeCursor[T]:
        return (
            self if position == self.position else PersistentDequeCursor(self._snapshot, position)
        )

    def insert(self, value: T) -> PersistentDequeCursor[T]:
        snapshot = self._snapshot.insert_at(self.position, value)
        if snapshot is None:
            raise AssertionError("validated cursor insertion failed")
        return PersistentDequeCursor(snapshot, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> PersistentDequeCursor[T]:
        materialized = tuple(values)
        if not materialized:
            return self
        split = self._snapshot.split_at(self.position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        middle = PersistentDeque.from_iterable(materialized)
        return PersistentDequeCursor(
            split.left.concat(middle).concat(split.right), self.position + len(materialized)
        )

    def delete_previous(self) -> PersistentDequeCursor[T]:
        if self.is_at_start:
            raise IndexError("deque cursor has no previous element")
        snapshot = self._snapshot.remove_at(self.position - 1)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return PersistentDequeCursor(snapshot, self.position - 1)

    def delete_next(self) -> PersistentDequeCursor[T]:
        if self.is_at_end:
            raise IndexError("deque cursor has no next element")
        snapshot = self._snapshot.remove_at(self.position)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return PersistentDequeCursor(snapshot, self.position)

    def replace_next(self, value: T) -> PersistentDequeCursor[T]:
        if self.is_at_end:
            raise IndexError("deque cursor has no next element")
        snapshot = self._snapshot.set_item(self.position, value)
        if snapshot is None:
            raise AssertionError("validated cursor replacement failed")
        return PersistentDequeCursor(snapshot, self.position)

    def snapshot(self) -> PersistentDeque[T]:
        return self._snapshot


@dataclass(frozen=True, slots=True)
class ReversibleDequeCursor(Generic[T]):
    """Immutable logical-order gap cursor over a reversible deque."""

    _snapshot: ReversibleDeque[T]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > len(self._snapshot):
            raise IndexError("cursor position is outside the reversible-deque boundary range")

    @property
    def count(self) -> int:
        return len(self._snapshot)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        if self.is_at_start:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self.position - 1)))

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        if self.is_at_end:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self.position)))

    def move_previous(self) -> ReversibleDequeCursor[T]:
        if self.is_at_start:
            raise IndexError("reversible-deque cursor is already at the start")
        return ReversibleDequeCursor(self._snapshot, self.position - 1)

    def move_next(self) -> ReversibleDequeCursor[T]:
        if self.is_at_end:
            raise IndexError("reversible-deque cursor is already at the end")
        return ReversibleDequeCursor(self._snapshot, self.position + 1)

    def seek(self, position: int) -> ReversibleDequeCursor[T]:
        return (
            self if position == self.position else ReversibleDequeCursor(self._snapshot, position)
        )

    def insert(self, value: T) -> ReversibleDequeCursor[T]:
        split = self._snapshot.split_at(self.position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        snapshot = split[0].append(value).concat(split[1])
        return ReversibleDequeCursor(snapshot, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> ReversibleDequeCursor[T]:
        materialized = tuple(values)
        if not materialized:
            return self
        split = self._snapshot.split_at(self.position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        middle = self._snapshot._aligned_run(materialized)
        snapshot = split[0].concat(middle).concat(split[1])
        return ReversibleDequeCursor(snapshot, self.position + len(materialized))

    def delete_previous(self) -> ReversibleDequeCursor[T]:
        if self.is_at_start:
            raise IndexError("reversible-deque cursor has no previous element")
        first = self._snapshot.split_at(self.position - 1)
        if first is None:
            raise AssertionError("validated cursor split failed")
        second = first[1].split_at(1)
        if second is None:
            raise AssertionError("validated cursor item split failed")
        return ReversibleDequeCursor(first[0].concat(second[1]), self.position - 1)

    def delete_next(self) -> ReversibleDequeCursor[T]:
        if self.is_at_end:
            raise IndexError("reversible-deque cursor has no next element")
        first = self._snapshot.split_at(self.position)
        if first is None:
            raise AssertionError("validated cursor split failed")
        second = first[1].split_at(1)
        if second is None:
            raise AssertionError("validated cursor item split failed")
        return ReversibleDequeCursor(first[0].concat(second[1]), self.position)

    def replace_next(self, value: T) -> ReversibleDequeCursor[T]:
        return self.delete_next().insert(value).move_previous()

    def reverse(self) -> ReversibleDequeCursor[T]:
        return ReversibleDequeCursor(self._snapshot.reverse(), self.count - self.position)

    def snapshot(self) -> ReversibleDeque[T]:
        return self._snapshot


@dataclass(frozen=True, slots=True)
class FingerTreeCursor(Generic[T, M]):
    """Immutable measure-aware cursor over one exact finger-tree version."""

    _snapshot: FingerTree[T, M]
    _position: int

    def __post_init__(self) -> None:
        if self._position < 0 or self._position > len(self._snapshot):
            raise IndexError("cursor position is outside the finger-tree boundary range")

    @property
    def is_at_start(self) -> bool:
        return self._position == 0

    @property
    def is_at_end(self) -> bool:
        return self._position == len(self._snapshot)

    @property
    def measure_before(self) -> M:
        measure = self._snapshot.prefix_measure(self._position)
        if measure is None:
            raise AssertionError("validated prefix measure failed")
        return measure

    @property
    def measure_after(self) -> M:
        split = self._snapshot.split_at_index(self._position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        return split.right.measure

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        if self.is_at_start:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self._position - 1)))

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        if self.is_at_end:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self._position)))

    def move_previous(self) -> FingerTreeCursor[T, M]:
        if self.is_at_start:
            raise IndexError("finger-tree cursor is already at the start")
        return FingerTreeCursor(self._snapshot, self._position - 1)

    def move_next(self) -> FingerTreeCursor[T, M]:
        if self.is_at_end:
            raise IndexError("finger-tree cursor is already at the end")
        return FingerTreeCursor(self._snapshot, self._position + 1)

    def seek_by_measure(self, predicate: Callable[[M], bool]) -> FingerTreeCursor[T, M]:
        found, cursor = self._snapshot.get_cursor(predicate)
        _ = found
        return cursor

    def insert(self, value: T) -> FingerTreeCursor[T, M]:
        snapshot = self._snapshot.insert_at(self._position, value)
        if snapshot is None:
            raise AssertionError("validated cursor insertion failed")
        return FingerTreeCursor(snapshot, self._position + 1)

    def delete_previous(self) -> FingerTreeCursor[T, M]:
        if self.is_at_start:
            raise IndexError("finger-tree cursor has no previous element")
        snapshot = self._snapshot.remove_at(self._position - 1)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return FingerTreeCursor(snapshot, self._position - 1)

    def delete_next(self) -> FingerTreeCursor[T, M]:
        if self.is_at_end:
            raise IndexError("finger-tree cursor has no next element")
        snapshot = self._snapshot.remove_at(self._position)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return FingerTreeCursor(snapshot, self._position)

    def replace_next(self, value: T) -> FingerTreeCursor[T, M]:
        if self.is_at_end:
            raise IndexError("finger-tree cursor has no next element")
        snapshot = self._snapshot.set_item(self._position, value)
        if snapshot is None:
            raise AssertionError("validated cursor replacement failed")
        return FingerTreeCursor(snapshot, self._position)

    def snapshot(self) -> FingerTree[T, M]:
        return self._snapshot


__all__ = [
    "DequeItemSplit",
    "DequePop",
    "DequeRangeSplit",
    "DequeSplit",
    "FingerTree",
    "FingerTreeCursor",
    "LocateResult",
    "MeasuredItemSplit",
    "MeasuredSplit",
    "PersistentDeque",
    "PersistentDequeCursor",
    "ReversibleDeque",
    "ReversibleDequeCursor",
    "SequenceCursorPeek",
]
