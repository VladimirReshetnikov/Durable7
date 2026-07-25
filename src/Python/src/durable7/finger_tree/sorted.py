"""Persistent order-statistic sorted bag, set, and map facades."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from functools import cmp_to_key
from typing import Generic, TypeVar, cast

from .measured_sequence import MeasuredSequence
from .measures import SizeMeasure
from .ordering import Comparator, default_comparator

T = TypeVar("T")
K = TypeVar("K")
V = TypeVar("V")
R = TypeVar("R")

_SORTED_SIZE_MEASURE: SizeMeasure[object] = SizeMeasure()


def _policy() -> SizeMeasure[T]:
    return cast(SizeMeasure[T], _SORTED_SIZE_MEASURE)


def _equal_value(left: object, right: object) -> bool:
    return left is right or left == right


class SortedDuplicateKeyError(ValueError):
    """Raised when duplicate-rejecting insertion sees an equivalent key."""


@dataclass(frozen=True, slots=True)
class SortedAddResult(Generic[R]):
    value: R
    added: bool


@dataclass(frozen=True, slots=True)
class SortedMapEntry(Generic[K, V]):
    key: K
    value: V


@dataclass(frozen=True, slots=True)
class SortedMapRemoveResult(Generic[K, V]):
    map: SortedMap[K, V]
    value: V


@dataclass(frozen=True, slots=True)
class OrderedCursorPeek(Generic[T]):
    value: T


@dataclass(frozen=True, slots=True)
class OrderedCursorSearch(Generic[R]):
    found: bool
    cursor: R


class SortedBag(Generic[T]):
    __slots__ = ("_items", "comparator")

    def __init__(self, items: MeasuredSequence[T, int], comparator: Comparator[T]) -> None:
        self._items = items
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> SortedBag[T]:
        return cls(MeasuredSequence.empty(_policy()), comparator)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], comparator: Comparator[T] = default_comparator
    ) -> SortedBag[T]:
        ordered = sorted(values, key=cmp_to_key(comparator))
        return cls(MeasuredSequence.from_iterable(ordered, _policy()), comparator)

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    def min(self) -> T | None:
        return self._items.front()

    def max(self) -> T | None:
        return self._items.back()

    def get(self, rank: int) -> T | None:
        return self._items.at(rank)

    def contains(self, value: T) -> bool:
        return self.count_of(value) != 0

    def count_less_than(self, value: T) -> int:
        return self._items.lower_bound(value, self.comparator)

    def count_at_most(self, value: T) -> int:
        return self._items.upper_bound(value, self.comparator)

    def count_of(self, value: T) -> int:
        return self.count_at_most(value) - self.count_less_than(value)

    def add(self, value: T) -> SortedBag[T]:
        items = self._items.insert_at(self.count_at_most(value), value)
        if items is None:
            raise AssertionError("Validated bag insertion failed.")
        return SortedBag(items, self.comparator)

    def add_range(self, values: Iterable[T]) -> SortedBag[T]:
        result = self
        for value in values:
            result = result.add(value)
        return result

    def remove(self, value: T) -> SortedBag[T]:
        index = self.count_less_than(value)
        current = self._items.at(index)
        if index >= len(self) or self.comparator(cast(T, current), value) != 0:
            return self
        items = self._items.remove_at(index)
        if items is None:
            raise AssertionError("Validated bag removal failed.")
        return SortedBag(items, self.comparator)

    def remove_all(self, value: T) -> SortedBag[T]:
        start, end = self.count_less_than(value), self.count_at_most(value)
        if start == end:
            return self
        first = self._items.split_at(start)
        if first is None:
            raise AssertionError("Validated bag split failed.")
        second = first.right.split_at(end - start)
        if second is None:
            raise AssertionError("Validated bag range split failed.")
        return SortedBag(first.left.concat(second.right), self.comparator)

    def get_range(self, start: int, count: int) -> SortedBag[T] | None:
        if start < 0 or count < 0 or start + count > len(self):
            return None
        if start == 0 and count == len(self):
            return self
        first = self._items.split_at(start)
        if first is None:
            raise AssertionError("Validated bag split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated bag range split failed.")
        return SortedBag(second.left, self.comparator)

    def get_value_range(self, low: T, high: T) -> SortedBag[T]:
        if self.comparator(low, high) > 0:
            return SortedBag.empty(self.comparator)
        start = self.count_less_than(low)
        result = self.get_range(start, self.count_at_most(high) - start)
        if result is None:
            raise AssertionError("Validated value range failed.")
        return result

    def to_list(self) -> list[T]:
        return self._items.to_list()

    def shares_storage_with(self, other: SortedBag[T]) -> bool:
        return self._items.shares_structure_with(other._items)

    def cursor_at(self, position: int = 0) -> SortedBagCursor[T]:
        return SortedBagCursor(self, position)

    def cursor_at_lower_bound(self, value: T) -> SortedBagCursor[T]:
        return self.cursor_at(self.count_less_than(value))

    def cursor_at_upper_bound(self, value: T) -> SortedBagCursor[T]:
        return self.cursor_at(self.count_at_most(value))

    def find_cursor(self, value: T) -> OrderedCursorSearch[SortedBagCursor[T]]:
        cursor = self.cursor_at_lower_bound(value)
        candidate = cursor.peek_next()
        return OrderedCursorSearch(
            candidate is not None and self.comparator(candidate.value, value) == 0, cursor
        )

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)


class SortedSet(Generic[T]):
    __slots__ = ("_items", "comparator")

    def __init__(self, items: MeasuredSequence[T, int], comparator: Comparator[T]) -> None:
        self._items = items
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> SortedSet[T]:
        return cls(MeasuredSequence.empty(_policy()), comparator)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], comparator: Comparator[T] = default_comparator
    ) -> SortedSet[T]:
        result = cls.empty(comparator)
        for value in values:
            result = result.add(value)
        return result

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    def min(self) -> T | None:
        return self._items.front()

    def max(self) -> T | None:
        return self._items.back()

    def get(self, rank: int) -> T | None:
        return self._items.at(rank)

    def index_of(self, value: T) -> int | None:
        index = self._items.lower_bound(value, self.comparator)
        current = self._items.at(index)
        return (
            index if index < len(self) and self.comparator(cast(T, current), value) == 0 else None
        )

    def contains(self, value: T) -> bool:
        return self.index_of(value) is not None

    def add(self, value: T) -> SortedSet[T]:
        index = self._items.lower_bound(value, self.comparator)
        current = self._items.at(index)
        if index < len(self) and self.comparator(cast(T, current), value) == 0:
            return self
        items = self._items.insert_at(index, value)
        if items is None:
            raise AssertionError("Validated set insertion failed.")
        return SortedSet(items, self.comparator)

    def union(self, values: Iterable[T]) -> SortedSet[T]:
        result = self
        for value in values:
            result = result.add(value)
        return result

    def remove(self, value: T) -> SortedSet[T]:
        index = self.index_of(value)
        if index is None:
            return self
        items = self._items.remove_at(index)
        if items is None:
            raise AssertionError("Validated set removal failed.")
        return SortedSet(items, self.comparator)

    def floor(self, value: T) -> T | None:
        return self.get(self._items.upper_bound(value, self.comparator) - 1)

    def ceiling(self, value: T) -> T | None:
        return self.get(self._items.lower_bound(value, self.comparator))

    def lower(self, value: T) -> T | None:
        return self.get(self._items.lower_bound(value, self.comparator) - 1)

    def higher(self, value: T) -> T | None:
        return self.get(self._items.upper_bound(value, self.comparator))

    def get_range(self, start: int, count: int) -> SortedSet[T] | None:
        if start < 0 or count < 0 or start + count > len(self):
            return None
        if start == 0 and count == len(self):
            return self
        first = self._items.split_at(start)
        if first is None:
            raise AssertionError("Validated set split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated set range split failed.")
        return SortedSet(second.left, self.comparator)

    def get_value_range(self, low: T, high: T) -> SortedSet[T]:
        if self.comparator(low, high) > 0:
            return SortedSet.empty(self.comparator)
        start = self._items.lower_bound(low, self.comparator)
        result = self.get_range(start, self._items.upper_bound(high, self.comparator) - start)
        if result is None:
            raise AssertionError("Validated set value range failed.")
        return result

    def intersect(self, values: Iterable[T]) -> SortedSet[T]:
        other = SortedSet.from_iterable(values, self.comparator)
        result = SortedSet.empty(self.comparator)
        for value in self:
            if other.contains(value):
                result = result.add(value)
        return self if result.set_equals(self) else result

    def except_(self, values: Iterable[T]) -> SortedSet[T]:
        result = self
        for value in values:
            result = result.remove(value)
        return result

    def symmetric_except(self, values: Iterable[T]) -> SortedSet[T]:
        result = self
        for value in SortedSet.from_iterable(values, self.comparator):
            result = result.remove(value) if result.contains(value) else result.add(value)
        return result

    def is_subset_of(self, values: Iterable[T]) -> bool:
        other = SortedSet.from_iterable(values, self.comparator)
        return len(self) <= len(other) and all(other.contains(value) for value in self)

    def is_proper_subset_of(self, values: Iterable[T]) -> bool:
        other = SortedSet.from_iterable(values, self.comparator)
        return len(self) < len(other) and self.is_subset_of(other)

    def is_superset_of(self, values: Iterable[T]) -> bool:
        return all(self.contains(value) for value in values)

    def is_proper_superset_of(self, values: Iterable[T]) -> bool:
        other = SortedSet.from_iterable(values, self.comparator)
        return len(self) > len(other) and self.is_superset_of(other)

    def overlaps(self, values: Iterable[T]) -> bool:
        return any(self.contains(value) for value in values)

    def set_equals(self, values: Iterable[T]) -> bool:
        other = (
            values
            if isinstance(values, SortedSet) and values.comparator is self.comparator
            else SortedSet.from_iterable(values, self.comparator)
        )
        return len(self) == len(other) and self.is_subset_of(other)

    def to_list(self) -> list[T]:
        return self._items.to_list()

    def shares_storage_with(self, other: SortedSet[T]) -> bool:
        return self._items.shares_structure_with(other._items)

    def cursor_at(self, position: int = 0) -> SortedSetCursor[T]:
        return SortedSetCursor(self, position)

    def cursor_at_lower_bound(self, value: T) -> SortedSetCursor[T]:
        return self.cursor_at(self._items.lower_bound(value, self.comparator))

    def cursor_at_upper_bound(self, value: T) -> SortedSetCursor[T]:
        return self.cursor_at(self._items.upper_bound(value, self.comparator))

    def find_cursor(self, value: T) -> OrderedCursorSearch[SortedSetCursor[T]]:
        cursor = self.cursor_at_lower_bound(value)
        candidate = cursor.peek_next()
        return OrderedCursorSearch(
            candidate is not None and self.comparator(candidate.value, value) == 0, cursor
        )

    def __contains__(self, value: object) -> bool:
        return self.contains(cast(T, value))

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)


class SortedMap(Generic[K, V]):
    __slots__ = ("_entries", "comparator")

    def __init__(
        self,
        entries: MeasuredSequence[SortedMapEntry[K, V], int],
        comparator: Comparator[K],
    ) -> None:
        self._entries = entries
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[K] = default_comparator) -> SortedMap[K, V]:
        return cls(MeasuredSequence.empty(_policy()), comparator)

    @classmethod
    def from_iterable(
        cls,
        items: Iterable[tuple[K, V] | SortedMapEntry[K, V]],
        comparator: Comparator[K] = default_comparator,
    ) -> SortedMap[K, V]:
        result = cls.empty(comparator)
        for item in items:
            if isinstance(item, SortedMapEntry):
                result = result.set_item(item.key, item.value)
            else:
                result = result.set_item(item[0], item[1])
        return result

    def __len__(self) -> int:
        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        return self._entries.is_empty

    def _entry_comparator(self, entry: SortedMapEntry[K, V], probe: SortedMapEntry[K, V]) -> int:
        return self.comparator(entry.key, probe.key)

    def _probe(self, key: K) -> SortedMapEntry[K, V]:
        return SortedMapEntry(key, cast(V, None))

    def index_of_key(self, key: K) -> int | None:
        index = self._entries.lower_bound(self._probe(key), self._entry_comparator)
        current = self._entries.at(index)
        return (
            index
            if index < len(self)
            and self.comparator(cast(SortedMapEntry[K, V], current).key, key) == 0
            else None
        )

    def contains_key(self, key: K) -> bool:
        return self.index_of_key(key) is not None

    def get(self, key: K) -> V | None:
        index = self.index_of_key(key)
        entry = None if index is None else self._entries.at(index)
        return None if entry is None else entry.value

    def get_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        index = self.index_of_key(key)
        return None if index is None else self._entries.at(index)

    def entry_at(self, rank: int) -> SortedMapEntry[K, V] | None:
        return self._entries.at(rank)

    def min_entry(self) -> SortedMapEntry[K, V] | None:
        return self._entries.front()

    def max_entry(self) -> SortedMapEntry[K, V] | None:
        return self._entries.back()

    def set_item(self, key: K, value: V) -> SortedMap[K, V]:
        probe = self._probe(key)
        index = self._entries.lower_bound(probe, self._entry_comparator)
        current = self._entries.at(index)
        if index < len(self) and self.comparator(cast(SortedMapEntry[K, V], current).key, key) == 0:
            entry = cast(SortedMapEntry[K, V], current)
            if _equal_value(entry.value, value):
                return self
            entries = self._entries.set_at(index, SortedMapEntry(entry.key, value))
        else:
            entries = self._entries.insert_at(index, SortedMapEntry(key, value))
        if entries is None:
            raise AssertionError("Validated map edit failed.")
        return SortedMap(entries, self.comparator)

    def insert(self, key: K, value: V) -> SortedMap[K, V]:
        result = self.try_insert(key, value)
        if not result.added:
            raise SortedDuplicateKeyError("An equivalent key is already present.")
        return result.value

    def try_insert(self, key: K, value: V) -> SortedAddResult[SortedMap[K, V]]:
        if self.contains_key(key):
            return SortedAddResult(self, False)
        return SortedAddResult(self.set_item(key, value), True)

    def remove(self, key: K) -> SortedMap[K, V]:
        result = self.try_remove(key)
        return self if result is None else result.map

    def try_remove(self, key: K) -> SortedMapRemoveResult[K, V] | None:
        index = self.index_of_key(key)
        if index is None:
            return None
        current = self._entries.at(index)
        entries = self._entries.remove_at(index)
        if current is None or entries is None:
            raise AssertionError("Validated map removal failed.")
        return SortedMapRemoveResult(SortedMap(entries, self.comparator), current.value)

    def _lower_bound(self, key: K) -> int:
        return self._entries.lower_bound(self._probe(key), self._entry_comparator)

    def _upper_bound(self, key: K) -> int:
        return self._entries.upper_bound(self._probe(key), self._entry_comparator)

    def floor_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        return self.entry_at(self._upper_bound(key) - 1)

    def ceiling_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        return self.entry_at(self._lower_bound(key))

    def lower_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        return self.entry_at(self._lower_bound(key) - 1)

    def higher_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        return self.entry_at(self._upper_bound(key))

    def get_range(self, start: int, count: int) -> SortedMap[K, V] | None:
        if start < 0 or count < 0 or start + count > len(self):
            return None
        if start == 0 and count == len(self):
            return self
        first = self._entries.split_at(start)
        if first is None:
            raise AssertionError("Validated map split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated map range split failed.")
        return SortedMap(second.left, self.comparator)

    def get_key_range(self, low: K, high: K) -> SortedMap[K, V]:
        if self.comparator(low, high) > 0:
            return SortedMap.empty(self.comparator)
        start = self._lower_bound(low)
        result = self.get_range(start, self._upper_bound(high) - start)
        if result is None:
            raise AssertionError("Validated key range failed.")
        return result

    def keys(self) -> list[K]:
        return [entry.key for entry in self]

    def values(self) -> list[V]:
        return [entry.value for entry in self]

    def to_list(self) -> list[SortedMapEntry[K, V]]:
        return self._entries.to_list()

    def shares_storage_with(self, other: SortedMap[K, V]) -> bool:
        return self._entries.shares_structure_with(other._entries)

    def cursor_at(self, position: int = 0) -> SortedMapCursor[K, V]:
        return SortedMapCursor(self, position)

    def cursor_at_lower_bound(self, key: K) -> SortedMapCursor[K, V]:
        return self.cursor_at(self._lower_bound(key))

    def cursor_at_upper_bound(self, key: K) -> SortedMapCursor[K, V]:
        return self.cursor_at(self._upper_bound(key))

    def find_cursor(self, key: K) -> OrderedCursorSearch[SortedMapCursor[K, V]]:
        cursor = self.cursor_at_lower_bound(key)
        candidate = cursor.peek_next()
        return OrderedCursorSearch(
            candidate is not None and self.comparator(candidate.value.key, key) == 0, cursor
        )

    def __iter__(self) -> Iterator[SortedMapEntry[K, V]]:
        return iter(self._entries)


@dataclass(frozen=True, slots=True)
class SortedBagCursor(Generic[T]):
    """Immutable root-plus-rank cursor over a persistent sorted bag."""

    bag: SortedBag[T]
    position: int = 0

    def __post_init__(self) -> None:
        _require_cursor_rank(self.position, len(self.bag), "sorted bag")

    @property
    def count(self) -> int:
        return len(self.bag)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> OrderedCursorPeek[T] | None:
        return (
            None
            if self.is_at_start
            else OrderedCursorPeek(cast(T, self.bag.get(self.position - 1)))
        )

    def peek_next(self) -> OrderedCursorPeek[T] | None:
        return None if self.is_at_end else OrderedCursorPeek(cast(T, self.bag.get(self.position)))

    def move_previous(self) -> SortedBagCursor[T]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return SortedBagCursor(self.bag, self.position - 1)

    def move_next(self) -> SortedBagCursor[T]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return SortedBagCursor(self.bag, self.position + 1)

    def seek_rank(self, position: int) -> SortedBagCursor[T]:
        return self if position == self.position else SortedBagCursor(self.bag, position)

    def add(self, value: T) -> SortedBagCursor[T]:
        position = self.bag.count_at_most(value)
        return SortedBagCursor(self.bag.add(value), position + 1)

    def _delete_at(self, rank: int, position: int) -> SortedBagCursor[T]:
        items = self.bag._items.remove_at(rank)
        if items is None:
            raise AssertionError("Validated cursor removal failed.")
        return SortedBagCursor(SortedBag(items, self.bag.comparator), position)

    def delete_previous(self) -> SortedBagCursor[T]:
        if self.is_at_start:
            raise IndexError("No occurrence precedes the cursor.")
        return self._delete_at(self.position - 1, self.position - 1)

    def delete_next(self) -> SortedBagCursor[T]:
        if self.is_at_end:
            raise IndexError("No occurrence follows the cursor.")
        return self._delete_at(self.position, self.position)

    def snapshot(self) -> SortedBag[T]:
        return self.bag


@dataclass(frozen=True, slots=True)
class SortedSetCursor(Generic[T]):
    """Immutable root-plus-rank cursor over a persistent sorted set."""

    set: SortedSet[T]
    position: int = 0

    def __post_init__(self) -> None:
        _require_cursor_rank(self.position, len(self.set), "sorted set")

    @property
    def count(self) -> int:
        return len(self.set)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> OrderedCursorPeek[T] | None:
        return (
            None
            if self.is_at_start
            else OrderedCursorPeek(cast(T, self.set.get(self.position - 1)))
        )

    def peek_next(self) -> OrderedCursorPeek[T] | None:
        return None if self.is_at_end else OrderedCursorPeek(cast(T, self.set.get(self.position)))

    def move_previous(self) -> SortedSetCursor[T]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return SortedSetCursor(self.set, self.position - 1)

    def move_next(self) -> SortedSetCursor[T]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return SortedSetCursor(self.set, self.position + 1)

    def seek_rank(self, position: int) -> SortedSetCursor[T]:
        return self if position == self.position else SortedSetCursor(self.set, position)

    def add(self, value: T) -> SortedSetCursor[T]:
        location = self.set.cursor_at_lower_bound(value)
        return SortedSetCursor(self.set.add(value), location.position + 1)

    def delete_previous(self) -> SortedSetCursor[T]:
        item = self.peek_previous()
        if item is None:
            raise IndexError("No item precedes the cursor.")
        return SortedSetCursor(self.set.remove(item.value), self.position - 1)

    def delete_next(self) -> SortedSetCursor[T]:
        item = self.peek_next()
        if item is None:
            raise IndexError("No item follows the cursor.")
        return SortedSetCursor(self.set.remove(item.value), self.position)

    def snapshot(self) -> SortedSet[T]:
        return self.set


@dataclass(frozen=True, slots=True)
class SortedMapCursor(Generic[K, V]):
    """Immutable key-order root-plus-rank cursor over a persistent sorted map."""

    map: SortedMap[K, V]
    position: int = 0

    def __post_init__(self) -> None:
        _require_cursor_rank(self.position, len(self.map), "sorted map")

    @property
    def count(self) -> int:
        return len(self.map)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> OrderedCursorPeek[SortedMapEntry[K, V]] | None:
        return (
            None
            if self.is_at_start
            else OrderedCursorPeek(cast(SortedMapEntry[K, V], self.map.entry_at(self.position - 1)))
        )

    def peek_next(self) -> OrderedCursorPeek[SortedMapEntry[K, V]] | None:
        return (
            None
            if self.is_at_end
            else OrderedCursorPeek(cast(SortedMapEntry[K, V], self.map.entry_at(self.position)))
        )

    def move_previous(self) -> SortedMapCursor[K, V]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return SortedMapCursor(self.map, self.position - 1)

    def move_next(self) -> SortedMapCursor[K, V]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return SortedMapCursor(self.map, self.position + 1)

    def seek_rank(self, position: int) -> SortedMapCursor[K, V]:
        return self if position == self.position else SortedMapCursor(self.map, position)

    def insert(self, key: K, value: V) -> SortedMapCursor[K, V]:
        position = self.map._lower_bound(key)
        return SortedMapCursor(self.map.insert(key, value), position + 1)

    def try_insert(self, key: K, value: V) -> OrderedCursorSearch[SortedMapCursor[K, V]]:
        position = self.map._lower_bound(key)
        result = self.map.try_insert(key, value)
        cursor = (
            SortedMapCursor(result.value, position + 1)
            if result.added
            else SortedMapCursor(self.map, position)
        )
        return OrderedCursorSearch(result.added, cursor)

    def set_item(self, key: K, value: V) -> SortedMapCursor[K, V]:
        location = self.map.find_cursor(key)
        return SortedMapCursor(
            self.map.set_item(key, value),
            location.cursor.position if location.found else location.cursor.position + 1,
        )

    def set_next_value(self, value: V) -> SortedMapCursor[K, V]:
        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return SortedMapCursor(self.map.set_item(entry.value.key, value), self.position)

    def delete_previous(self) -> SortedMapCursor[K, V]:
        entry = self.peek_previous()
        if entry is None:
            raise IndexError("No entry precedes the cursor.")
        return SortedMapCursor(self.map.remove(entry.value.key), self.position - 1)

    def delete_next(self) -> SortedMapCursor[K, V]:
        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return SortedMapCursor(self.map.remove(entry.value.key), self.position)

    def snapshot(self) -> SortedMap[K, V]:
        return self.map


def _require_cursor_rank(position: int, count: int, family: str) -> None:
    if position < 0 or position > count:
        raise IndexError(f"Cursor position is outside the {family}.")


class SortedSetBuilder(Generic[T]):
    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        self._value = SortedSet.empty(comparator)

    def __len__(self) -> int:
        return len(self._value)

    def add(self, value: T) -> SortedSetBuilder[T]:
        self._value = self._value.add(value)
        return self

    def remove(self, value: T) -> bool:
        next_value = self._value.remove(value)
        changed = next_value is not self._value
        self._value = next_value
        return changed

    def clear(self) -> None:
        self._value = SortedSet.empty(self._value.comparator)

    def to_immutable(self) -> SortedSet[T]:
        return self._value


class SortedMapBuilder(Generic[K, V]):
    def __init__(self, comparator: Comparator[K] = default_comparator) -> None:
        self._value: SortedMap[K, V] = SortedMap.empty(comparator)

    def __len__(self) -> int:
        return len(self._value)

    def set(self, key: K, value: V) -> SortedMapBuilder[K, V]:
        self._value = self._value.set_item(key, value)
        return self

    def remove(self, key: K) -> bool:
        next_value = self._value.remove(key)
        changed = next_value is not self._value
        self._value = next_value
        return changed

    def clear(self) -> None:
        self._value = SortedMap.empty(self._value.comparator)

    def to_immutable(self) -> SortedMap[K, V]:
        return self._value


__all__ = [
    "OrderedCursorPeek",
    "OrderedCursorSearch",
    "SortedAddResult",
    "SortedBag",
    "SortedBagCursor",
    "SortedDuplicateKeyError",
    "SortedMap",
    "SortedMapBuilder",
    "SortedMapCursor",
    "SortedMapEntry",
    "SortedMapRemoveResult",
    "SortedSet",
    "SortedSetBuilder",
    "SortedSetCursor",
]
