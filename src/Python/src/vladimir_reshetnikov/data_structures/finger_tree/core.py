"""Persistent deque, reversible deque, and measured-tree facades."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
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


__all__ = [
    "DequeItemSplit",
    "DequePop",
    "DequeRangeSplit",
    "DequeSplit",
    "FingerTree",
    "LocateResult",
    "MeasuredItemSplit",
    "MeasuredSplit",
    "PersistentDeque",
    "ReversibleDeque",
]
