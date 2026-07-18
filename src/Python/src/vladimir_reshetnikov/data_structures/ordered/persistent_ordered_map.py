"""Persistent insertion-ordered map over neutral HAMT and deque substrates."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from functools import cmp_to_key
from typing import Generic, TypeVar, cast

from ..finger_tree import Comparator, PersistentDeque
from ..hamt import DuplicateKeyError, HashPolicy, PersistentHashMap, default_hash_policy

K = TypeVar("K")
V = TypeVar("V")

_STAMP_STRIDE = 1 << 20
_INT32_MAX = (1 << 31) - 1
_INT64_MIN = -(1 << 63)
_INT64_MAX = (1 << 63) - 1


@dataclass(frozen=True, slots=True)
class _StoredEntry(Generic[K, V]):
    stamp: int
    key: K
    value: V


@dataclass(frozen=True, slots=True)
class OrderedMapEntry(Generic[K, V]):
    """One stored key/value representative in explicit map order."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class OrderedMapLookup(Generic[K, V]):
    """Presence-safe keyed lookup; either component may itself be ``None``."""

    found: bool
    entry: OrderedMapEntry[K, V] | None = None


@dataclass(frozen=True, slots=True)
class OrderedMapAddResult(Generic[K, V]):
    """Nonthrowing strict-add result."""

    added: bool
    map: PersistentOrderedMap[K, V]


@dataclass(frozen=True, slots=True)
class OrderedMapRemoveResult(Generic[K, V]):
    """Keyed removal result retaining the receiver on a miss."""

    removed: bool
    map: PersistentOrderedMap[K, V]
    entry: OrderedMapEntry[K, V] | None = None


@dataclass(frozen=True, slots=True)
class OrderedMapCursorSearch(Generic[K, V]):
    """Focused key lookup with an append-position miss cursor."""

    found: bool
    cursor: PersistentOrderedMapCursor[K, V]


def _values_equal(left: V, right: V) -> bool:
    if left is right:
        return True
    try:
        result = left == right
    except (TypeError, ValueError):
        return False
    return result if isinstance(result, bool) else False


class PersistentOrderedMap(Generic[K, V]):
    """Immutable insertion-ordered map with explicit positional movement.

    A key policy defines equivalence and first-representative retention. Values live only in the
    persistent positional deque; the CHAMP map stores key-to-stamp navigation so arbitrary payloads
    are not retained twice. Replacing a value never changes its key representative or position.
    """

    __slots__ = ("_order", "_stamps", "value_equals")

    def __init__(
        self,
        order: PersistentDeque[_StoredEntry[K, V]],
        stamps: PersistentHashMap[K, int],
        value_equals: Callable[[V, V], bool],
    ) -> None:
        if len(order) != stamps.size:
            raise ValueError("The order and key indexes must have equal counts.")
        self._order = order
        self._stamps = stamps
        self.value_equals = value_equals

    @classmethod
    def empty(
        cls,
        key_policy: HashPolicy[K] | None = None,
        value_equals: Callable[[V, V], bool] = _values_equal,
    ) -> PersistentOrderedMap[K, V]:
        effective = default_hash_policy() if key_policy is None else key_policy
        return cls(PersistentDeque.empty(), PersistentHashMap.empty(effective), value_equals)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        key_policy: HashPolicy[K] | None = None,
        value_equals: Callable[[V, V], bool] = _values_equal,
    ) -> PersistentOrderedMap[K, V]:
        """Build with the first key and position and last distinct value winning."""

        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(key_policy, value_equals)
        for key, value in items:
            result = result.set(key, value)
        return result

    @property
    def size(self) -> int:
        return len(self._order)

    def __len__(self) -> int:
        return self.size

    @property
    def is_empty(self) -> bool:
        return self._order.is_empty

    def __bool__(self) -> bool:
        return not self.is_empty

    @property
    def key_policy(self) -> HashPolicy[K]:
        return self._stamps.policy

    @property
    def first(self) -> OrderedMapEntry[K, V]:
        if self.is_empty:
            raise IndexError("The ordered map is empty.")
        entry = cast("_StoredEntry[K, V]", self._order.front())
        return OrderedMapEntry(entry.key, entry.value)

    @property
    def last(self) -> OrderedMapEntry[K, V]:
        if self.is_empty:
            raise IndexError("The ordered map is empty.")
        entry = cast("_StoredEntry[K, V]", self._order.back())
        return OrderedMapEntry(entry.key, entry.value)

    def contains_key(self, key: K) -> bool:
        return self._stamps.contains_key(key)

    def __contains__(self, key: object) -> bool:
        return self.contains_key(cast("K", key))

    def get(self, key: K) -> V | None:
        lookup = self.try_get_entry(key)
        return None if not lookup.found or lookup.entry is None else lookup.entry.value

    def __getitem__(self, key: K) -> V:
        lookup = self.try_get_entry(key)
        if not lookup.found or lookup.entry is None:
            raise KeyError(key)
        return lookup.entry.value

    def try_get_entry(self, key: K) -> OrderedMapLookup[K, V]:
        indexed = self._stamps.get_entry(key)
        if indexed is None:
            return OrderedMapLookup(False)
        stored = self._order[self._index_of_stamp(indexed.value)]
        return OrderedMapLookup(True, OrderedMapEntry(stored.key, stored.value))

    def get_stored_key(self, key: K) -> K | None:
        indexed = self._stamps.get_entry(key)
        return None if indexed is None else indexed.key

    def entry_at(self, index: int) -> OrderedMapEntry[K, V]:
        self._check_element_index(index)
        stored = self._order[index]
        return OrderedMapEntry(stored.key, stored.value)

    def index_of_key(self, key: K) -> int:
        indexed = self._stamps.get_entry(key)
        return -1 if indexed is None else self._index_of_stamp(indexed.value)

    def cursor_at(self, position: int = 0) -> PersistentOrderedMapCursor[K, V]:
        """Return an immutable explicit-order gap cursor."""

        return PersistentOrderedMapCursor(self, position)

    def find_cursor(self, key: K) -> OrderedMapCursorSearch[K, V]:
        """Focus an equivalent key or return the append-position cursor on a miss."""

        position = self.index_of_key(key)
        return OrderedMapCursorSearch(
            position >= 0,
            PersistentOrderedMapCursor(self, self.size if position < 0 else position),
        )

    def add(self, key: K, value: V) -> PersistentOrderedMap[K, V]:
        result = self.try_add(key, value)
        if not result.added:
            raise DuplicateKeyError("An equivalent key is already present.")
        return result.map

    def try_add(self, key: K, value: V) -> OrderedMapAddResult[K, V]:
        return self._insert_core(self.size, key, value)

    def add_first(self, key: K, value: V) -> PersistentOrderedMap[K, V]:
        result = self._insert_core(0, key, value)
        if not result.added:
            raise DuplicateKeyError("An equivalent key is already present.")
        return result.map

    def insert(self, index: int, key: K, value: V) -> PersistentOrderedMap[K, V]:
        self._check_insertion_index(index)
        result = self._insert_core(index, key, value)
        if not result.added:
            raise DuplicateKeyError("An equivalent key is already present.")
        return result.map

    def set(self, key: K, value: V) -> PersistentOrderedMap[K, V]:
        """Add or replace a value without changing the stored key or position."""

        indexed = self._stamps.get_entry(key)
        if indexed is None:
            return self.add(key, value)
        index = self._index_of_stamp(indexed.value)
        stored = self._order[index]
        if self.value_equals(stored.value, value):
            return self
        order = self._order.set_item(index, _StoredEntry(stored.stamp, stored.key, value))
        if order is None:
            raise AssertionError("Validated ordered replacement failed.")
        return PersistentOrderedMap(order, self._stamps, self.value_equals)

    def move_to_first(self, key: K) -> PersistentOrderedMap[K, V]:
        return self._move_existing(0, key)

    def move_to_last(self, key: K) -> PersistentOrderedMap[K, V]:
        if self.is_empty:
            raise KeyError(key)
        return self._move_existing(self.size - 1, key)

    def move_to(self, index: int, key: K) -> PersistentOrderedMap[K, V]:
        self._check_element_index(index)
        return self._move_existing(index, key)

    def remove(self, key: K) -> PersistentOrderedMap[K, V]:
        return self.try_remove(key).map

    def try_remove(self, key: K) -> OrderedMapRemoveResult[K, V]:
        removed = self._stamps.try_remove_entry(key)
        if removed is None:
            return OrderedMapRemoveResult(False, self)
        index = self._index_of_stamp(removed.entry.value)
        stored = self._order[index]
        order = self._order.remove_at(index)
        if order is None:
            raise AssertionError("Validated ordered removal failed.")
        return OrderedMapRemoveResult(
            True,
            self._wrap(order, removed.map, self.value_equals),
            OrderedMapEntry(stored.key, stored.value),
        )

    def remove_at(self, index: int) -> PersistentOrderedMap[K, V]:
        self._check_element_index(index)
        stored = self._order[index]
        removed = self._stamps.try_remove_entry(stored.key)
        if removed is None or removed.entry.value != stored.stamp:
            raise RuntimeError("The ordered map's indexes disagree.")
        order = self._order.remove_at(index)
        if order is None:
            raise AssertionError("Validated ordered removal failed.")
        return self._wrap(order, removed.map, self.value_equals)

    def remove_first(self) -> PersistentOrderedMap[K, V]:
        if self.is_empty:
            raise IndexError("The ordered map is empty.")
        return self.remove_at(0)

    def remove_last(self) -> PersistentOrderedMap[K, V]:
        if self.is_empty:
            raise IndexError("The ordered map is empty.")
        return self.remove_at(self.size - 1)

    def clear(self) -> PersistentOrderedMap[K, V]:
        return self if self.is_empty else self.empty(self.key_policy, self.value_equals)

    def get_range(self, index: int, count: int) -> PersistentOrderedMap[K, V]:
        self._check_range(index, count)
        if count == self.size:
            return self
        if count == 0:
            return self.empty(self.key_policy, self.value_equals)
        split = self._order.split_range(index, count)
        if split is None:
            raise AssertionError("Validated ordered range split failed.")
        kept = split.range
        if count <= self.size - count:
            stamps = self._build_index(kept, self.key_policy)
        else:
            stamps = self._stamps
            for entry in split.before:
                stamps = stamps.remove(entry.key)
            for entry in split.after:
                stamps = stamps.remove(entry.key)
        return PersistentOrderedMap(kept, stamps, self.value_equals)

    def take(self, count: int) -> PersistentOrderedMap[K, V]:
        self._check_count(count)
        return self.get_range(0, count)

    def drop(self, count: int) -> PersistentOrderedMap[K, V]:
        self._check_count(count)
        return self.get_range(count, self.size - count)

    def reverse(self) -> PersistentOrderedMap[K, V]:
        if self.size <= 1:
            return self
        return self._build_from_distinct(
            list(reversed(self.to_list())), self.key_policy, self.value_equals
        )

    def sort(self, comparator: Comparator[OrderedMapEntry[K, V]]) -> PersistentOrderedMap[K, V]:
        """Perform a stable one-shot positional sort."""

        if not callable(comparator):
            raise TypeError("comparator must be callable.")
        if self.size <= 1:
            return self
        original = self._order.to_list()
        sorted_entries = list(original)

        def compare(left: _StoredEntry[K, V], right: _StoredEntry[K, V]) -> int:
            result = comparator(
                OrderedMapEntry(left.key, left.value), OrderedMapEntry(right.key, right.value)
            )
            if result != 0:
                return result
            return -1 if left.stamp < right.stamp else 1 if left.stamp > right.stamp else 0

        sorted_entries.sort(key=cmp_to_key(compare))
        if all(entry.stamp == original[index].stamp for index, entry in enumerate(sorted_entries)):
            return self
        return self._build_from_distinct(
            [OrderedMapEntry(entry.key, entry.value) for entry in sorted_entries],
            self.key_policy,
            self.value_equals,
        )

    def keys(self) -> Iterator[K]:
        for entry in self._order:
            yield entry.key

    def values(self) -> Iterator[V]:
        for entry in self._order:
            yield entry.value

    def to_list(self) -> list[OrderedMapEntry[K, V]]:
        return list(self)

    def __iter__(self) -> Iterator[OrderedMapEntry[K, V]]:
        for entry in self._order:
            yield OrderedMapEntry(entry.key, entry.value)

    def shares_order_storage_with(self, other: PersistentOrderedMap[K, V]) -> bool:
        return self._order.shares_storage_with(other._order)

    def shares_membership_root_with(self, other: PersistentOrderedMap[K, V]) -> bool:
        return self._stamps.shares_root_with(other._stamps)

    def validate_structure(self) -> bool:
        """Recompute strict stamps and representative identity across both indexes."""

        if len(self._order) != self._stamps.size:
            return False
        previous: int | None = None
        for entry in self._order:
            if previous is not None and entry.stamp <= previous:
                return False
            previous = entry.stamp
            indexed = self._stamps.get_entry(entry.key)
            if indexed is None or indexed.value != entry.stamp or indexed.key is not entry.key:
                return False
        for indexed in self._stamps:
            ordered = self._order[self._index_of_stamp(indexed.value)]
            if ordered.key is not indexed.key:
                return False
        return True

    def _insert_core(self, index: int, key: K, value: V) -> OrderedMapAddResult[K, V]:
        stamp = self._pick_stamp(self._order, index)
        if stamp is not None:
            added = self._stamps.try_add(key, stamp)
            if not added.added:
                return OrderedMapAddResult(False, self)
            if self.size == _INT32_MAX:
                raise OverflowError("The operation would exceed the signed 32-bit map count.")
            stored = _StoredEntry(stamp, key, value)
            if index == 0:
                order = self._order.prepend(stored)
            elif index == self.size:
                order = self._order.append(stored)
            else:
                inserted = self._order.insert_at(index, stored)
                if inserted is None:
                    raise AssertionError("Validated ordered insertion failed.")
                order = inserted
            return OrderedMapAddResult(
                True, PersistentOrderedMap(order, added.value, self.value_equals)
            )

        duplicate = self._stamps.try_add(key, 0)
        if not duplicate.added:
            return OrderedMapAddResult(False, self)
        entries = self.to_list()
        entries.insert(index, OrderedMapEntry(key, value))
        return OrderedMapAddResult(
            True, self._build_from_distinct(entries, self.key_policy, self.value_equals)
        )

    def _move_existing(self, final_index: int, key: K) -> PersistentOrderedMap[K, V]:
        indexed = self._stamps.get_entry(key)
        if indexed is None:
            raise KeyError(key)
        old_index = self._index_of_stamp(indexed.value)
        if old_index == final_index:
            return self
        stored = self._order[old_index]
        trimmed = self._order.remove_at(old_index)
        if trimmed is None:
            raise AssertionError("Validated movement removal failed.")
        stamp = self._pick_stamp(trimmed, final_index)
        if stamp is None:
            entries = [OrderedMapEntry(entry.key, entry.value) for entry in trimmed]
            entries.insert(final_index, OrderedMapEntry(stored.key, stored.value))
            return self._build_from_distinct(entries, self.key_policy, self.value_equals)
        moved = _StoredEntry(stamp, stored.key, stored.value)
        if final_index == 0:
            order = trimmed.prepend(moved)
        elif final_index == len(trimmed):
            order = trimmed.append(moved)
        else:
            inserted = trimmed.insert_at(final_index, moved)
            if inserted is None:
                raise AssertionError("Validated movement insertion failed.")
            order = inserted
        return PersistentOrderedMap(order, self._stamps.put(stored.key, stamp), self.value_equals)

    def _index_of_stamp(self, stamp: int) -> int:
        low = 0
        high = self.size
        while low < high:
            middle = low + ((high - low) // 2)
            if self._order[middle].stamp < stamp:
                low = middle + 1
            else:
                high = middle
        if low >= self.size or self._order[low].stamp != stamp:
            raise RuntimeError("The ordered map's indexes disagree.")
        return low

    @staticmethod
    def _pick_stamp(order: PersistentDeque[_StoredEntry[K, V]], index: int) -> int | None:
        if order.is_empty:
            return 0
        if index == 0:
            first = cast("_StoredEntry[K, V]", order.front()).stamp
            return None if first < _INT64_MIN + _STAMP_STRIDE else first - _STAMP_STRIDE
        if index == len(order):
            last = cast("_StoredEntry[K, V]", order.back()).stamp
            return None if last > _INT64_MAX - _STAMP_STRIDE else last + _STAMP_STRIDE
        left = order[index - 1].stamp
        right = order[index].stamp
        return None if right - left < 2 else left + ((right - left) // 2)

    @classmethod
    def _build_from_distinct(
        cls,
        entries: Iterable[OrderedMapEntry[K, V]],
        key_policy: HashPolicy[K],
        value_equals: Callable[[V, V], bool],
    ) -> PersistentOrderedMap[K, V]:
        materialized = list(entries)
        if not materialized:
            return cls.empty(key_policy, value_equals)
        if len(materialized) > _INT32_MAX:
            raise OverflowError("The operation would exceed the signed 32-bit map count.")
        ordered: list[_StoredEntry[K, V]] = []
        indexed: list[tuple[K, int]] = []
        for index, entry in enumerate(materialized):
            stamp = index * _STAMP_STRIDE
            if stamp > _INT64_MAX:
                raise OverflowError("The private order label exceeds signed 64-bit range.")
            ordered.append(_StoredEntry(stamp, entry.key, entry.value))
            indexed.append((entry.key, stamp))
        return cls(
            PersistentDeque.from_iterable(ordered),
            PersistentHashMap.from_items(indexed, key_policy),
            value_equals,
        )

    @staticmethod
    def _build_index(
        order: PersistentDeque[_StoredEntry[K, V]], key_policy: HashPolicy[K]
    ) -> PersistentHashMap[K, int]:
        return PersistentHashMap.from_items(
            ((entry.key, entry.stamp) for entry in order), key_policy
        )

    @classmethod
    def _wrap(
        cls,
        order: PersistentDeque[_StoredEntry[K, V]],
        stamps: PersistentHashMap[K, int],
        value_equals: Callable[[V, V], bool],
    ) -> PersistentOrderedMap[K, V]:
        return (
            cls.empty(stamps.policy, value_equals)
            if order.is_empty
            else cls(order, stamps, value_equals)
        )

    def _check_element_index(self, index: int) -> None:
        if index < 0 or index >= self.size:
            raise IndexError(index)

    def _check_insertion_index(self, index: int) -> None:
        if index < 0 or index > self.size:
            raise IndexError(index)

    def _check_count(self, count: int) -> None:
        if count < 0 or count > self.size:
            raise ValueError("count must lie within 0 through the map size.")

    def _check_range(self, index: int, count: int) -> None:
        if index < 0 or index > self.size:
            raise IndexError(index)
        if count < 0 or count > self.size - index:
            raise ValueError("count extends past the end of the ordered map.")


@dataclass(frozen=True, slots=True)
class PersistentOrderedMapCursor(Generic[K, V]):
    """Immutable root-plus-position gap cursor over a persistent ordered map."""

    map: PersistentOrderedMap[K, V]
    position: int = 0

    def __post_init__(self) -> None:
        if type(self.position) is not int or self.position < 0 or self.position > self.map.size:
            raise IndexError("Cursor position is outside the ordered map.")

    @property
    def size(self) -> int:
        return self.map.size

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.size

    def peek_previous(self) -> OrderedMapEntry[K, V] | None:
        return None if self.is_at_start else self.map.entry_at(self.position - 1)

    def peek_next(self) -> OrderedMapEntry[K, V] | None:
        return None if self.is_at_end else self.map.entry_at(self.position)

    def move_previous(self) -> PersistentOrderedMapCursor[K, V]:
        if self.is_at_start:
            raise IndexError("The ordered-map cursor is already at the start.")
        return PersistentOrderedMapCursor(self.map, self.position - 1)

    def move_next(self) -> PersistentOrderedMapCursor[K, V]:
        if self.is_at_end:
            raise IndexError("The ordered-map cursor is already at the end.")
        return PersistentOrderedMapCursor(self.map, self.position + 1)

    def seek(self, position: int) -> PersistentOrderedMapCursor[K, V]:
        return self if position == self.position else PersistentOrderedMapCursor(self.map, position)

    def insert(self, key: K, value: V) -> PersistentOrderedMapCursor[K, V]:
        return PersistentOrderedMapCursor(
            self.map.insert(self.position, key, value), self.position + 1
        )

    def try_insert(self, key: K, value: V) -> tuple[bool, PersistentOrderedMapCursor[K, V]]:
        existing = self.map.index_of_key(key)
        return (
            (False, PersistentOrderedMapCursor(self.map, existing))
            if existing >= 0
            else (True, self.insert(key, value))
        )

    def set_next_value(self, value: V) -> PersistentOrderedMapCursor[K, V]:
        entry = self.peek_next()
        if entry is None:
            raise IndexError("The ordered-map cursor has no next entry.")
        return PersistentOrderedMapCursor(self.map.set(entry.key, value), self.position)

    def delete_previous(self) -> PersistentOrderedMapCursor[K, V]:
        if self.is_at_start:
            raise IndexError("The ordered-map cursor has no previous entry.")
        return PersistentOrderedMapCursor(self.map.remove_at(self.position - 1), self.position - 1)

    def delete_next(self) -> PersistentOrderedMapCursor[K, V]:
        if self.is_at_end:
            raise IndexError("The ordered-map cursor has no next entry.")
        return PersistentOrderedMapCursor(self.map.remove_at(self.position), self.position)


__all__ = [
    "OrderedMapAddResult",
    "OrderedMapCursorSearch",
    "OrderedMapEntry",
    "OrderedMapLookup",
    "OrderedMapRemoveResult",
    "PersistentOrderedMap",
    "PersistentOrderedMapCursor",
]
