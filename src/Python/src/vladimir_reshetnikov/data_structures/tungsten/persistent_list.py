"""Tungsten-language persistent ``List`` vocabulary."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from ..finger_tree.core import PersistentDeque

T = TypeVar("T")
R = TypeVar("R")


@dataclass(frozen=True, slots=True)
class PersistentListSplit(Generic[T]):
    left: PersistentList[T]
    right: PersistentList[T]


class PersistentList(Generic[T]):
    """Application-specific immutable list facade used by Tungsten."""

    __slots__ = ("_items",)

    def __init__(self, items: PersistentDeque[T]) -> None:
        self._items = items

    @classmethod
    def empty(cls) -> PersistentList[T]:
        return cls(PersistentDeque.empty())

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> PersistentList[T]:
        if isinstance(values, PersistentList):
            return values
        return cls(PersistentDeque.from_iterable(values))

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    def first(self) -> T | None:
        return self._items.front()

    def last(self) -> T | None:
        return self._items.back()

    def get(self, index: int) -> T | None:
        return self._items.get(index)

    def append(self, value: T) -> PersistentList[T]:
        return PersistentList(self._items.append(value))

    def prepend(self, value: T) -> PersistentList[T]:
        return PersistentList(self._items.prepend(value))

    def join(self, other: PersistentList[T]) -> PersistentList[T]:
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return PersistentList(self._items.concat(other._items))

    def add_range(self, values: Iterable[T]) -> PersistentList[T]:
        if isinstance(values, PersistentList):
            return self.join(values)
        materialized = list(values)
        return self if not materialized else self.join(PersistentList.from_iterable(materialized))

    def insert(self, index: int, value: T) -> PersistentList[T] | None:
        items = self._items.insert_at(index, value)
        return None if items is None else PersistentList(items)

    def insert_range(self, index: int, values: Iterable[T]) -> PersistentList[T] | None:
        split = self._items.split_at(index)
        if split is None:
            return None
        inserted = PersistentDeque.from_iterable(values)
        return (
            self
            if inserted.is_empty
            else PersistentList(split.left.concat(inserted).concat(split.right))
        )

    def remove_at(self, index: int) -> PersistentList[T] | None:
        items = self._items.remove_at(index)
        return None if items is None else PersistentList(items)

    def remove_range(self, index: int, count: int) -> PersistentList[T] | None:
        split = self._items.split_range(index, count)
        if split is None:
            return None
        return self if count == 0 else PersistentList(split.before.concat(split.after))

    def remove_first(self) -> PersistentList[T] | None:
        return self.remove_at(0)

    def remove_last(self) -> PersistentList[T] | None:
        return self.remove_at(len(self) - 1)

    def set_item(self, index: int, value: T) -> PersistentList[T] | None:
        if index < 0 or index >= len(self):
            return None
        # Tungsten deliberately promises a new list even for a default-equal value.
        split = self._items.split_item_at(index)
        if split is None:
            raise AssertionError("Validated list replacement split failed.")
        return PersistentList(split.left.append(value).concat(split.right))

    def update_at(self, index: int, updater: Callable[[T], T]) -> PersistentList[T] | None:
        if index < 0 or index >= len(self):
            return None
        return self.set_item(index, updater(cast(T, self.get(index))))

    def get_range(self, index: int, count: int) -> PersistentList[T] | None:
        split = self._items.split_range(index, count)
        if split is None:
            return None
        if index == 0 and count == len(self):
            return self
        return PersistentList(split.range)

    def take(self, count: int) -> PersistentList[T] | None:
        return self.get_range(0, count)

    def take_last(self, count: int) -> PersistentList[T] | None:
        return None if count < 0 or count > len(self) else self.get_range(len(self) - count, count)

    def drop(self, count: int) -> PersistentList[T] | None:
        return None if count < 0 or count > len(self) else self.get_range(count, len(self) - count)

    def drop_last(self, count: int) -> PersistentList[T] | None:
        return None if count < 0 or count > len(self) else self.get_range(0, len(self) - count)

    def split_at(self, index: int) -> PersistentListSplit[T] | None:
        split = self._items.split_at(index)
        return (
            None
            if split is None
            else PersistentListSplit(PersistentList(split.left), PersistentList(split.right))
        )

    def reverse(self) -> PersistentList[T]:
        return self if len(self) <= 1 else PersistentList.from_iterable(reversed(self.to_list()))

    def map(self, transform: Callable[[T, int], R]) -> PersistentList[R]:
        return PersistentList.from_iterable(
            transform(value, index) for index, value in enumerate(self)
        )

    def index_of(self, value: T, equivalent: Callable[[T, T], bool] | None = None) -> int:
        equals = (
            (lambda left, right: left is right or left == right)
            if equivalent is None
            else equivalent
        )
        return next((index for index, candidate in enumerate(self) if equals(candidate, value)), -1)

    def contains(self, value: T, equivalent: Callable[[T, T], bool] | None = None) -> bool:
        return self.index_of(value, equivalent) >= 0

    def to_list(self) -> list[T]:
        return self._items.to_list()

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)


__all__ = ["PersistentList", "PersistentListSplit"]
