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
    """The outcome of a non-overwriting insertion; the value is the receiver when nothing was added.
    """

    value: R
    added: bool


@dataclass(frozen=True, slots=True)
class SortedMapEntry(Generic[K, V]):
    """One key-value pair as the sorted map stores and presents it."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class SortedMapRemoveResult(Generic[K, V]):
    """The map remaining after a removal, together with the value that was removed."""

    map: SortedMap[K, V]
    value: V


@dataclass(frozen=True, slots=True)
class OrderedCursorPeek(Generic[T]):
    """An element found next to a cursor gap, wrapped so a stored ``None`` stays distinct from
    "nothing there".
    """

    value: T


@dataclass(frozen=True, slots=True)
class OrderedCursorSearch(Generic[R]):
    """The outcome of seeking a cursor. On a miss the cursor sits at the lower bound, where the
    value would be inserted, and remains usable.
    """

    found: bool
    cursor: R


class SortedBag(Generic[T]):
    """A persistent sorted multiset with rank and select.

    Elements are kept in ascending order and equivalent elements may repeat. Because the backing
    sequence caches element counts, rank and select cost a descent rather than a scan.
    """

    __slots__ = ("_items", "comparator")

    def __init__(self, items: MeasuredSequence[T, int], comparator: Comparator[T]) -> None:
        """Wrap an already-built sequence; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._items = items
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> SortedBag[T]:
        """Return an empty bag ordered by ``comparator``, which is retained by identity."""

        return cls(MeasuredSequence.empty(_policy()), comparator)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], comparator: Comparator[T] = default_comparator
    ) -> SortedBag[T]:
        """Build a bag from ``values``."""

        ordered = sorted(values, key=cmp_to_key(comparator))
        return cls(MeasuredSequence.from_iterable(ordered, _policy()), comparator)

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the bag holds no elements."""

        return self._items.is_empty

    def min(self) -> T | None:
        """The smallest element, or ``None`` when empty."""

        return self._items.front()

    def max(self) -> T | None:
        """The largest element, or ``None`` when empty."""

        return self._items.back()

    def get(self, rank: int) -> T | None:
        """The element at ordinal ``rank`` in ascending order, or ``None`` when out of range. A
        descent by cached counts, not a scan.
        """

        return self._items.at(rank)

    def contains(self, value: T) -> bool:
        """Whether an element equivalent to ``value`` is present."""

        return self.count_of(value) != 0

    def count_less_than(self, value: T) -> int:
        """How many elements are strictly below ``value``, that is, its rank."""

        return self._items.lower_bound(value, self.comparator)

    def count_at_most(self, value: T) -> int:
        """How many elements are at most ``value``."""

        return self._items.upper_bound(value, self.comparator)

    def count_of(self, value: T) -> int:
        """The multiplicity of ``value``, computed as the gap between its two rank bounds."""

        return self.count_at_most(value) - self.count_less_than(value)

    def add(self, value: T) -> SortedBag[T]:
        """Return a bag with ``value`` inserted at its sorted position, after any existing equals.
        """

        items = self._items.insert_at(self.count_at_most(value), value)
        if items is None:
            raise AssertionError("Validated bag insertion failed.")
        return SortedBag(items, self.comparator)

    def add_range(self, values: Iterable[T]) -> SortedBag[T]:
        """Insert every element of ``values``, publishing only the final bag."""

        result = self
        for value in values:
            result = result.add(value)
        return result

    def remove(self, value: T) -> SortedBag[T]:
        """Remove one occurrence of ``value``; a no-op when absent."""

        index = self.count_less_than(value)
        current = self._items.at(index)
        if index >= len(self) or self.comparator(cast(T, current), value) != 0:
            return self
        items = self._items.remove_at(index)
        if items is None:
            raise AssertionError("Validated bag removal failed.")
        return SortedBag(items, self.comparator)

    def remove_all(self, value: T) -> SortedBag[T]:
        """Remove every occurrence of ``value`` in one split and join, regardless of multiplicity.
        """

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
        """The ``count`` elements starting at ordinal ``start`` as a new bag, or ``None`` when the
        range falls outside it.
        """

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
        """The elements in the inclusive value range as a new bag. An inverted range yields an empty
        bag.
        """

        if self.comparator(low, high) > 0:
            return SortedBag.empty(self.comparator)
        start = self.count_less_than(low)
        result = self.get_range(start, self.count_at_most(high) - start)
        if result is None:
            raise AssertionError("Validated value range failed.")
        return result

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in ascending order."""

        return self._items.to_list()

    def shares_storage_with(self, other: SortedBag[T]) -> bool:
        """Whether both bags share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_structure_with(other._items)

    def cursor_at(self, position: int = 0) -> SortedBagCursor[T]:
        """A cursor at gap ``position`` of the ascending element sequence."""

        return SortedBagCursor(self, position)

    def cursor_at_lower_bound(self, value: T) -> SortedBagCursor[T]:
        """A cursor before the first entry not below the probe, where it would be inserted."""

        return self.cursor_at(self.count_less_than(value))

    def cursor_at_upper_bound(self, value: T) -> SortedBagCursor[T]:
        """A cursor after any entry equivalent to the probe."""

        return self.cursor_at(self.count_at_most(value))

    def find_cursor(self, value: T) -> OrderedCursorSearch[SortedBagCursor[T]]:
        """Seek to the probe and report whether it is present. On a miss the cursor sits at the
        lower bound and remains usable.
        """

        cursor = self.cursor_at_lower_bound(value)
        candidate = cursor.peek_next()
        return OrderedCursorSearch(
            candidate is not None and self.comparator(candidate.value, value) == 0, cursor
        )

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in ascending order."""

        return iter(self._items)


class SortedSet(Generic[T]):
    """A persistent sorted set with rank and select.

    Like :class:`SortedBag` but holding at most one element per equivalence class, keeping the first
    representative inserted.
    """

    __slots__ = ("_items", "comparator")

    def __init__(self, items: MeasuredSequence[T, int], comparator: Comparator[T]) -> None:
        """Wrap an already-built sequence; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._items = items
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> SortedSet[T]:
        """Return an empty set ordered by ``comparator``, which is retained by identity."""

        return cls(MeasuredSequence.empty(_policy()), comparator)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], comparator: Comparator[T] = default_comparator
    ) -> SortedSet[T]:
        """Build a set from ``values``."""

        result = cls.empty(comparator)
        for value in values:
            result = result.add(value)
        return result

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the set holds no elements."""

        return self._items.is_empty

    def min(self) -> T | None:
        """The smallest element, or ``None`` when empty."""

        return self._items.front()

    def max(self) -> T | None:
        """The largest element, or ``None`` when empty."""

        return self._items.back()

    def get(self, rank: int) -> T | None:
        """The element at ordinal ``rank`` in ascending order, or ``None`` when out of range. A
        descent by cached counts, not a scan.
        """

        return self._items.at(rank)

    def index_of(self, value: T) -> int | None:
        """The ordinal rank of ``value``, or ``None`` when absent."""

        index = self._items.lower_bound(value, self.comparator)
        current = self._items.at(index)
        return (
            index if index < len(self) and self.comparator(cast(T, current), value) == 0 else None
        )

    def contains(self, value: T) -> bool:
        """Whether an element equivalent to ``value`` is present."""

        return self.index_of(value) is not None

    def add(self, value: T) -> SortedSet[T]:
        """Return a set containing ``value``; an already present class is a no-op that keeps the
        existing representative.
        """

        index = self._items.lower_bound(value, self.comparator)
        current = self._items.at(index)
        if index < len(self) and self.comparator(cast(T, current), value) == 0:
            return self
        items = self._items.insert_at(index, value)
        if items is None:
            raise AssertionError("Validated set insertion failed.")
        return SortedSet(items, self.comparator)

    def union(self, values: Iterable[T]) -> SortedSet[T]:
        """Return the elements of this set and ``values``."""

        result = self
        for value in values:
            result = result.add(value)
        return result

    def remove(self, value: T) -> SortedSet[T]:
        """Return a set without ``value``'s class; a no-op when absent."""

        index = self.index_of(value)
        if index is None:
            return self
        items = self._items.remove_at(index)
        if items is None:
            raise AssertionError("Validated set removal failed.")
        return SortedSet(items, self.comparator)

    def floor(self, value: T) -> T | None:
        """The largest element at most ``value``, or ``None`` when every element exceeds it."""

        return self.get(self._items.upper_bound(value, self.comparator) - 1)

    def ceiling(self, value: T) -> T | None:
        """The smallest element at least ``value``, or ``None`` when every element is below it."""

        return self.get(self._items.lower_bound(value, self.comparator))

    def lower(self, value: T) -> T | None:
        """The largest element strictly below ``value``, or ``None`` when none is."""

        return self.get(self._items.lower_bound(value, self.comparator) - 1)

    def higher(self, value: T) -> T | None:
        """The smallest element strictly above ``value``, or ``None`` when none is."""

        return self.get(self._items.upper_bound(value, self.comparator))

    def get_range(self, start: int, count: int) -> SortedSet[T] | None:
        """The ``count`` elements starting at ordinal ``start`` as a new set, or ``None`` when the
        range falls outside it.
        """

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
        """The elements in the inclusive value range as a new set. An inverted range yields an empty
        set.
        """

        if self.comparator(low, high) > 0:
            return SortedSet.empty(self.comparator)
        start = self._items.lower_bound(low, self.comparator)
        result = self.get_range(start, self._items.upper_bound(high, self.comparator) - start)
        if result is None:
            raise AssertionError("Validated set value range failed.")
        return result

    def intersect(self, values: Iterable[T]) -> SortedSet[T]:
        """Return the elements this set and ``values`` have in common."""

        other = SortedSet.from_iterable(values, self.comparator)
        result = SortedSet.empty(self.comparator)
        for value in self:
            if other.contains(value):
                result = result.add(value)
        return self if result.set_equals(self) else result

    def except_(self, values: Iterable[T]) -> SortedSet[T]:
        """Return this set's elements that do not occur in ``values``. Named with a trailing
        underscore because ``except`` is a Python keyword.
        """

        result = self
        for value in values:
            result = result.remove(value)
        return result

    def symmetric_except(self, values: Iterable[T]) -> SortedSet[T]:
        """Return the elements occurring in exactly one of this set and ``values``."""

        result = self
        for value in SortedSet.from_iterable(values, self.comparator):
            result = result.remove(value) if result.contains(value) else result.add(value)
        return result

    def is_subset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of this set also occurs in ``values``."""

        other = SortedSet.from_iterable(values, self.comparator)
        return len(self) <= len(other) and all(other.contains(value) for value in self)

    def is_proper_subset_of(self, values: Iterable[T]) -> bool:
        """Whether this set is a subset of ``values`` and ``values`` has an element it lacks."""

        other = SortedSet.from_iterable(values, self.comparator)
        return len(self) < len(other) and self.is_subset_of(other)

    def is_superset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of ``values`` occurs in this set."""

        return all(self.contains(value) for value in values)

    def is_proper_superset_of(self, values: Iterable[T]) -> bool:
        """Whether this set is a superset of ``values`` and holds an element they lack."""

        other = SortedSet.from_iterable(values, self.comparator)
        return len(self) > len(other) and self.is_superset_of(other)

    def overlaps(self, values: Iterable[T]) -> bool:
        """Whether this set shares at least one element with ``values``."""

        return any(self.contains(value) for value in values)

    def set_equals(self, values: Iterable[T]) -> bool:
        """Whether this set holds exactly the distinct elements of ``values``."""

        other = (
            values
            if isinstance(values, SortedSet) and values.comparator is self.comparator
            else SortedSet.from_iterable(values, self.comparator)
        )
        return len(self) == len(other) and self.is_subset_of(other)

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in ascending order."""

        return self._items.to_list()

    def shares_storage_with(self, other: SortedSet[T]) -> bool:
        """Whether both sets share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_structure_with(other._items)

    def cursor_at(self, position: int = 0) -> SortedSetCursor[T]:
        """A cursor at gap ``position`` of the ascending element sequence."""

        return SortedSetCursor(self, position)

    def cursor_at_lower_bound(self, value: T) -> SortedSetCursor[T]:
        """A cursor before the first entry not below the probe, where it would be inserted."""

        return self.cursor_at(self._items.lower_bound(value, self.comparator))

    def cursor_at_upper_bound(self, value: T) -> SortedSetCursor[T]:
        """A cursor after any entry equivalent to the probe."""

        return self.cursor_at(self._items.upper_bound(value, self.comparator))

    def find_cursor(self, value: T) -> OrderedCursorSearch[SortedSetCursor[T]]:
        """Seek to the probe and report whether it is present. On a miss the cursor sits at the
        lower bound and remains usable.
        """

        cursor = self.cursor_at_lower_bound(value)
        candidate = cursor.peek_next()
        return OrderedCursorSearch(
            candidate is not None and self.comparator(candidate.value, value) == 0, cursor
        )

    def __contains__(self, value: object) -> bool:
        """Whether ``value``'s equivalence class is present, for the ``in`` operator."""

        return self.contains(cast(T, value))

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in ascending order."""

        return iter(self._items)


class SortedMap(Generic[K, V]):
    """A persistent sorted map with rank and select over its keys.

    Entries are ordered by key, one per key equivalence class, and the first key representative
    survives value replacement.
    """

    __slots__ = ("_entries", "comparator")

    def __init__(
        self,
        entries: MeasuredSequence[SortedMapEntry[K, V], int],
        comparator: Comparator[K],
    ) -> None:
        """Wrap an already-built sequence; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._entries = entries
        self.comparator = comparator

    @classmethod
    def empty(cls, comparator: Comparator[K] = default_comparator) -> SortedMap[K, V]:
        """Return an empty map ordered by ``comparator``, which is retained by identity."""

        return cls(MeasuredSequence.empty(_policy()), comparator)

    @classmethod
    def from_iterable(
        cls,
        items: Iterable[tuple[K, V] | SortedMapEntry[K, V]],
        comparator: Comparator[K] = default_comparator,
    ) -> SortedMap[K, V]:
        """Build a map from ``values``."""

        result = cls.empty(comparator)
        for item in items:
            if isinstance(item, SortedMapEntry):
                result = result.set_item(item.key, item.value)
            else:
                result = result.set_item(item[0], item[1])
        return result

    def __len__(self) -> int:
        """Number of entries."""

        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        """Whether the map holds no entries."""

        return self._entries.is_empty

    def _entry_comparator(self, entry: SortedMapEntry[K, V], probe: SortedMapEntry[K, V]) -> int:
        return self.comparator(entry.key, probe.key)

    def _probe(self, key: K) -> SortedMapEntry[K, V]:
        return SortedMapEntry(key, cast(V, None))

    def index_of_key(self, key: K) -> int | None:
        """The ordinal rank of ``key``, or ``None`` when absent."""

        index = self._entries.lower_bound(self._probe(key), self._entry_comparator)
        current = self._entries.at(index)
        return (
            index
            if index < len(self)
            and self.comparator(cast(SortedMapEntry[K, V], current).key, key) == 0
            else None
        )

    def contains_key(self, key: K) -> bool:
        """Whether ``key`` is present."""

        return self.index_of_key(key) is not None

    def get(self, key: K) -> V | None:
        """The value stored for ``key``, or ``None`` when absent. Use :meth:`get_entry` when a
        stored ``None`` must stay distinct from absence.
        """

        index = self.index_of_key(key)
        entry = None if index is None else self._entries.at(index)
        return None if entry is None else entry.value

    def get_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The stored entry for ``key``, or ``None`` when absent."""

        index = self.index_of_key(key)
        return None if index is None else self._entries.at(index)

    def entry_at(self, rank: int) -> SortedMapEntry[K, V] | None:
        """The entry at ordinal ``index``, or ``None`` when out of range."""

        return self._entries.at(rank)

    def min_entry(self) -> SortedMapEntry[K, V] | None:
        """The entry with the smallest key, or ``None`` when empty."""

        return self._entries.front()

    def max_entry(self) -> SortedMapEntry[K, V] | None:
        """The entry with the largest key, or ``None`` when empty."""

        return self._entries.back()

    def set_item(self, key: K, value: V) -> SortedMap[K, V]:
        """Add ``key``, or replace its value when present, retaining the stored key representative.
        Writing an equal value returns the receiver.
        """

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
        """Add a new entry, raising :class:`SortedDuplicateKeyError` when ``key`` is already
        present. Use :meth:`set_item` to overwrite instead.
        """

        result = self.try_insert(key, value)
        if not result.added:
            raise SortedDuplicateKeyError("An equivalent key is already present.")
        return result.value

    def try_insert(self, key: K, value: V) -> SortedAddResult[SortedMap[K, V]]:
        """Add a new entry, reporting whether it was added rather than raising on a duplicate."""

        if self.contains_key(key):
            return SortedAddResult(self, False)
        return SortedAddResult(self.set_item(key, value), True)

    def remove(self, key: K) -> SortedMap[K, V]:
        """Return a map without ``key``; a no-op when absent."""

        result = self.try_remove(key)
        return self if result is None else result.map

    def try_remove(self, key: K) -> SortedMapRemoveResult[K, V] | None:
        """Remove ``key`` and report the value that went with it, or ``None`` when absent."""

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
        """The entry with the largest key at most ``key``, or ``None`` when every key exceeds it."""

        return self.entry_at(self._upper_bound(key) - 1)

    def ceiling_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The entry with the smallest key at least ``key``, or ``None`` when every key is below it.
        """

        return self.entry_at(self._lower_bound(key))

    def lower_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The entry with the largest key strictly below ``key``, or ``None`` when none is."""

        return self.entry_at(self._lower_bound(key) - 1)

    def higher_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The entry with the smallest key strictly above ``key``, or ``None`` when none is."""

        return self.entry_at(self._upper_bound(key))

    def get_range(self, start: int, count: int) -> SortedMap[K, V] | None:
        """The ``count`` entries starting at ordinal ``start`` as a new map, or ``None`` when the
        range falls outside it.
        """

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
        """The entries whose keys lie in the inclusive range as a new map. An inverted range yields
        an empty map.
        """

        if self.comparator(low, high) > 0:
            return SortedMap.empty(self.comparator)
        start = self._lower_bound(low)
        result = self.get_range(start, self._upper_bound(high) - start)
        if result is None:
            raise AssertionError("Validated key range failed.")
        return result

    def keys(self) -> list[K]:
        """Iterate the keys in ascending order."""

        return [entry.key for entry in self]

    def values(self) -> list[V]:
        """Iterate the values in their keys' ascending order."""

        return [entry.value for entry in self]

    def to_list(self) -> list[SortedMapEntry[K, V]]:
        """Copy the entries into a list, in ascending order."""

        return self._entries.to_list()

    def shares_storage_with(self, other: SortedMap[K, V]) -> bool:
        """Whether both maps share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._entries.shares_structure_with(other._entries)

    def cursor_at(self, position: int = 0) -> SortedMapCursor[K, V]:
        """A cursor at gap ``position`` of the ascending entry sequence."""

        return SortedMapCursor(self, position)

    def cursor_at_lower_bound(self, key: K) -> SortedMapCursor[K, V]:
        """A cursor before the first entry not below the probe, where it would be inserted."""

        return self.cursor_at(self._lower_bound(key))

    def cursor_at_upper_bound(self, key: K) -> SortedMapCursor[K, V]:
        """A cursor after any entry equivalent to the probe."""

        return self.cursor_at(self._upper_bound(key))

    def find_cursor(self, key: K) -> OrderedCursorSearch[SortedMapCursor[K, V]]:
        """Seek to the probe and report whether it is present. On a miss the cursor sits at the
        lower bound and remains usable.
        """

        cursor = self.cursor_at_lower_bound(key)
        candidate = cursor.peek_next()
        return OrderedCursorSearch(
            candidate is not None and self.comparator(candidate.value.key, key) == 0, cursor
        )

    def __iter__(self) -> Iterator[SortedMapEntry[K, V]]:
        """Iterate the entries in ascending order."""

        return iter(self._entries)


@dataclass(frozen=True, slots=True)
class SortedBagCursor(Generic[T]):
    """Immutable root-plus-rank cursor over a persistent sorted bag."""

    bag: SortedBag[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        _require_cursor_rank(self.position, len(self.bag), "sorted bag")

    @property
    def count(self) -> int:
        """Number of elements in the bag version this cursor is positioned in."""

        return len(self.bag)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> OrderedCursorPeek[T] | None:
        """The element immediately before the gap, reported presence-safely."""

        return (
            None
            if self.is_at_start
            else OrderedCursorPeek(cast(T, self.bag.get(self.position - 1)))
        )

    def peek_next(self) -> OrderedCursorPeek[T] | None:
        """The element immediately after the gap, reported presence-safely."""

        return None if self.is_at_end else OrderedCursorPeek(cast(T, self.bag.get(self.position)))

    def move_previous(self) -> SortedBagCursor[T]:
        """A cursor one position earlier, or ``None`` at the start. The receiver is unchanged;
        movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return SortedBagCursor(self.bag, self.position - 1)

    def move_next(self) -> SortedBagCursor[T]:
        """A cursor one position later, or ``None`` at the end. The receiver is unchanged."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return SortedBagCursor(self.bag, self.position + 1)

    def seek_rank(self, position: int) -> SortedBagCursor[T]:
        """A cursor at ``position`` within the same bag version, or ``None`` when out of range."""

        return self if position == self.position else SortedBagCursor(self.bag, position)

    def add(self, value: T) -> SortedBagCursor[T]:
        """Insert ``value`` and return a cursor just after it. The gap moves to the element's sorted
        position, since placement is decided by the ordering rather than by the cursor.
        """

        position = self.bag.count_at_most(value)
        return SortedBagCursor(self.bag.add(value), position + 1)

    def _delete_at(self, rank: int, position: int) -> SortedBagCursor[T]:
        items = self.bag._items.remove_at(rank)
        if items is None:
            raise AssertionError("Validated cursor removal failed.")
        return SortedBagCursor(SortedBag(items, self.bag.comparator), position)

    def delete_previous(self) -> SortedBagCursor[T]:
        """Remove the element before the gap and return a cursor in its place, or ``None`` at the
        start.
        """

        if self.is_at_start:
            raise IndexError("No occurrence precedes the cursor.")
        return self._delete_at(self.position - 1, self.position - 1)

    def delete_next(self) -> SortedBagCursor[T]:
        """Remove the element after the gap and return a cursor in its place, or ``None`` at the
        end.
        """

        if self.is_at_end:
            raise IndexError("No occurrence follows the cursor.")
        return self._delete_at(self.position, self.position)

    def snapshot(self) -> SortedBag[T]:
        """The bag version this cursor is positioned in."""

        return self.bag


@dataclass(frozen=True, slots=True)
class SortedSetCursor(Generic[T]):
    """Immutable root-plus-rank cursor over a persistent sorted set."""

    set: SortedSet[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        _require_cursor_rank(self.position, len(self.set), "sorted set")

    @property
    def count(self) -> int:
        """Number of elements in the set version this cursor is positioned in."""

        return len(self.set)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> OrderedCursorPeek[T] | None:
        """The element immediately before the gap, reported presence-safely."""

        return (
            None
            if self.is_at_start
            else OrderedCursorPeek(cast(T, self.set.get(self.position - 1)))
        )

    def peek_next(self) -> OrderedCursorPeek[T] | None:
        """The element immediately after the gap, reported presence-safely."""

        return None if self.is_at_end else OrderedCursorPeek(cast(T, self.set.get(self.position)))

    def move_previous(self) -> SortedSetCursor[T]:
        """A cursor one position earlier, or ``None`` at the start. The receiver is unchanged;
        movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return SortedSetCursor(self.set, self.position - 1)

    def move_next(self) -> SortedSetCursor[T]:
        """A cursor one position later, or ``None`` at the end. The receiver is unchanged."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return SortedSetCursor(self.set, self.position + 1)

    def seek_rank(self, position: int) -> SortedSetCursor[T]:
        """A cursor at ``position`` within the same set version, or ``None`` when out of range."""

        return self if position == self.position else SortedSetCursor(self.set, position)

    def add(self, value: T) -> SortedSetCursor[T]:
        """Insert ``value`` and return a cursor just after it. The gap moves to the element's sorted
        position, since placement is decided by the ordering rather than by the cursor.
        """

        location = self.set.cursor_at_lower_bound(value)
        return SortedSetCursor(self.set.add(value), location.position + 1)

    def delete_previous(self) -> SortedSetCursor[T]:
        """Remove the element before the gap and return a cursor in its place, or ``None`` at the
        start.
        """

        item = self.peek_previous()
        if item is None:
            raise IndexError("No item precedes the cursor.")
        return SortedSetCursor(self.set.remove(item.value), self.position - 1)

    def delete_next(self) -> SortedSetCursor[T]:
        """Remove the element after the gap and return a cursor in its place, or ``None`` at the
        end.
        """

        item = self.peek_next()
        if item is None:
            raise IndexError("No item follows the cursor.")
        return SortedSetCursor(self.set.remove(item.value), self.position)

    def snapshot(self) -> SortedSet[T]:
        """The set version this cursor is positioned in."""

        return self.set


@dataclass(frozen=True, slots=True)
class SortedMapCursor(Generic[K, V]):
    """Immutable key-order root-plus-rank cursor over a persistent sorted map."""

    map: SortedMap[K, V]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        _require_cursor_rank(self.position, len(self.map), "sorted map")

    @property
    def count(self) -> int:
        """Number of entries in the map version this cursor is positioned in."""

        return len(self.map)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first entry."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last entry."""

        return self.position == self.count

    def peek_previous(self) -> OrderedCursorPeek[SortedMapEntry[K, V]] | None:
        """The entry immediately before the gap, reported presence-safely."""

        return (
            None
            if self.is_at_start
            else OrderedCursorPeek(cast(SortedMapEntry[K, V], self.map.entry_at(self.position - 1)))
        )

    def peek_next(self) -> OrderedCursorPeek[SortedMapEntry[K, V]] | None:
        """The entry immediately after the gap, reported presence-safely."""

        return (
            None
            if self.is_at_end
            else OrderedCursorPeek(cast(SortedMapEntry[K, V], self.map.entry_at(self.position)))
        )

    def move_previous(self) -> SortedMapCursor[K, V]:
        """A cursor one position earlier, or ``None`` at the start. The receiver is unchanged;
        movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return SortedMapCursor(self.map, self.position - 1)

    def move_next(self) -> SortedMapCursor[K, V]:
        """A cursor one position later, or ``None`` at the end. The receiver is unchanged."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return SortedMapCursor(self.map, self.position + 1)

    def seek_rank(self, position: int) -> SortedMapCursor[K, V]:
        """A cursor at ``position`` within the same map version, or ``None`` when out of range."""

        return self if position == self.position else SortedMapCursor(self.map, position)

    def insert(self, key: K, value: V) -> SortedMapCursor[K, V]:
        """Add a new entry at the gap and return a cursor just after it. Strict: an already present
        key raises rather than moving or overwriting it. The gap lands at the key's sorted
        position, since placement is decided by the ordering.
        """

        position = self.map._lower_bound(key)
        return SortedMapCursor(self.map.insert(key, value), position + 1)

    def try_insert(self, key: K, value: V) -> OrderedCursorSearch[SortedMapCursor[K, V]]:
        """Add a new entry, reporting whether it was added. On a duplicate the returned cursor
        focuses the retained entry.
        """

        position = self.map._lower_bound(key)
        result = self.map.try_insert(key, value)
        cursor = (
            SortedMapCursor(result.value, position + 1)
            if result.added
            else SortedMapCursor(self.map, position)
        )
        return OrderedCursorSearch(result.added, cursor)

    def set_item(self, key: K, value: V) -> SortedMapCursor[K, V]:
        """Add ``key``, or replace its value when present, returning a cursor at the resulting
        entry.
        """

        location = self.map.find_cursor(key)
        return SortedMapCursor(
            self.map.set_item(key, value),
            location.cursor.position if location.found else location.cursor.position + 1,
        )

    def set_next_value(self, value: V) -> SortedMapCursor[K, V]:
        """Replace the value of the entry after the gap, keeping its key and position. ``None`` at
        the end.
        """

        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return SortedMapCursor(self.map.set_item(entry.value.key, value), self.position)

    def delete_previous(self) -> SortedMapCursor[K, V]:
        """Remove the entry before the gap and return a cursor in its place, or ``None`` at the
        start.
        """

        entry = self.peek_previous()
        if entry is None:
            raise IndexError("No entry precedes the cursor.")
        return SortedMapCursor(self.map.remove(entry.value.key), self.position - 1)

    def delete_next(self) -> SortedMapCursor[K, V]:
        """Remove the entry after the gap and return a cursor in its place, or ``None`` at the end.
        """

        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return SortedMapCursor(self.map.remove(entry.value.key), self.position)

    def snapshot(self) -> SortedMap[K, V]:
        """The map version this cursor is positioned in."""

        return self.map


def _require_cursor_rank(position: int, count: int, family: str) -> None:
    if position < 0 or position > count:
        raise IndexError(f"Cursor position is outside the {family}.")


class SortedSetBuilder(Generic[T]):
    """A mutable accumulator for building a sorted set in bulk, published on demand."""

    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        """Wrap an already-built sequence; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._value = SortedSet.empty(comparator)

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._value)

    def add(self, value: T) -> SortedSetBuilder[T]:
        """Add one element, mutating the builder in place."""

        self._value = self._value.add(value)
        return self

    def remove(self, value: T) -> bool:
        """Remove one element, reporting whether it was there to remove."""

        next_value = self._value.remove(value)
        changed = next_value is not self._value
        self._value = next_value
        return changed

    def clear(self) -> None:
        """Discard everything accumulated so far."""

        self._value = SortedSet.empty(self._value.comparator)

    def to_immutable(self) -> SortedSet[T]:
        """Freeze the accumulated elements into a persistent set, leaving the builder usable."""

        return self._value


class SortedMapBuilder(Generic[K, V]):
    """A mutable accumulator for building a sorted map in bulk, published on demand."""

    def __init__(self, comparator: Comparator[K] = default_comparator) -> None:
        """Wrap an already-built sequence; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._value: SortedMap[K, V] = SortedMap.empty(comparator)

    def __len__(self) -> int:
        """Number of entries."""

        return len(self._value)

    def set(self, key: K, value: V) -> SortedMapBuilder[K, V]:
        """Add or replace one entry, mutating the builder in place."""

        self._value = self._value.set_item(key, value)
        return self

    def remove(self, key: K) -> bool:
        """Remove one entry by key, reporting whether it was there to remove."""

        next_value = self._value.remove(key)
        changed = next_value is not self._value
        self._value = next_value
        return changed

    def clear(self) -> None:
        """Discard everything accumulated so far."""

        self._value = SortedMap.empty(self._value.comparator)

    def to_immutable(self) -> SortedMap[K, V]:
        """Freeze the accumulated entries into a persistent map, leaving the builder usable."""

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
