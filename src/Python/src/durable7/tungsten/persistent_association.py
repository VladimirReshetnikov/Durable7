"""Tungsten insertion-ordered persistent association."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from functools import cmp_to_key
from typing import Generic, TypeVar, cast

from ..finger_tree.core import PersistentDeque
from ..finger_tree.ordering import Comparator, default_comparator
from ..hamt.hash_policy import HashPolicy, default_hash_policy
from ..hamt.persistent_hamt import PersistentHashMap

K = TypeVar("K")
V = TypeVar("V")

_STAMP_GAP = 1 << 20


def _equal_value(left: object, right: object) -> bool:
    return left is right or left == right


@dataclass(frozen=True, slots=True)
class _Slot(Generic[V]):
    stamp: int
    value: V


@dataclass(frozen=True, slots=True)
class _AssociationEntry(Generic[K, V]):
    stamp: int
    key: K
    value: V


@dataclass(frozen=True, slots=True)
class AssociationRemoveResult(Generic[K, V]):
    association: PersistentAssociation[K, V]
    value: V


class PersistentAssociation(Generic[K, V]):
    """Persistent insertion-ordered map with Tungsten positioning rules."""

    __slots__ = ("_entries", "_index")

    def __init__(
        self,
        entries: PersistentDeque[_AssociationEntry[K, V]],
        index: PersistentHashMap[K, _Slot[V]],
    ) -> None:
        self._entries = entries
        self._index = index

    @classmethod
    def empty(cls, policy: HashPolicy[K] | None = None) -> PersistentAssociation[K, V]:
        actual_policy = default_hash_policy() if policy is None else policy
        return cls(PersistentDeque.empty(), PersistentHashMap.empty(actual_policy))

    @classmethod
    def from_pairs(
        cls,
        pairs: Iterable[tuple[K, V]],
        policy: HashPolicy[K] | None = None,
    ) -> PersistentAssociation[K, V]:
        return cls.empty(policy).set_items(pairs)

    def __len__(self) -> int:
        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        return self._entries.is_empty

    @property
    def policy(self) -> HashPolicy[K]:
        return self._index.policy

    def contains_key(self, key: K) -> bool:
        return self._index.contains_key(key)

    def get(self, key: K) -> V | None:
        found = self._index.get_entry(key)
        return None if found is None else found.value.value

    def get_stored_key(self, key: K) -> K | None:
        found = self._index.get_entry(key)
        return None if found is None else found.key

    @staticmethod
    def _pair(entry: _AssociationEntry[K, V] | None) -> tuple[K, V] | None:
        return None if entry is None else (entry.key, entry.value)

    def first(self) -> tuple[K, V] | None:
        return self._pair(self._entries.front())

    def last(self) -> tuple[K, V] | None:
        return self._pair(self._entries.back())

    def get_at(self, index: int) -> tuple[K, V] | None:
        return self._pair(self._entries.get(index))

    def _index_of_stamp(self, stamp: int) -> int:
        low, high = 0, len(self)
        while low < high:
            middle = (low + high) // 2
            entry = self._entries.get(middle)
            if entry is None:
                raise AssertionError("Valid association position was absent.")
            if entry.stamp < stamp:
                low = middle + 1
            else:
                high = middle
        entry = self._entries.get(low)
        if low >= len(self) or entry is None or entry.stamp != stamp:
            raise AssertionError("Association stamp is absent from order.")
        return low

    def index_of_key(self, key: K) -> int:
        found = self._index.get_entry(key)
        return -1 if found is None else self._index_of_stamp(found.value.stamp)

    def set_item(self, key: K, value: V) -> PersistentAssociation[K, V]:
        found = self._index.get_entry(key)
        if found is None:
            return self._insert_new(self._entries, self._index, len(self), key, value)
        slot = found.value
        if _equal_value(slot.value, value):
            return self
        position = self._index_of_stamp(slot.stamp)
        stored = self._entries.get(position)
        if stored is None:
            raise AssertionError("Association index pointed outside order.")
        entries = self._entries.set_item(position, _AssociationEntry(slot.stamp, stored.key, value))
        if entries is None:
            raise AssertionError("Validated association replacement failed.")
        return PersistentAssociation(entries, self._index.put(key, _Slot(slot.stamp, value)))

    def set_items(self, pairs: Iterable[tuple[K, V]]) -> PersistentAssociation[K, V]:
        result = self
        for key, value in pairs:
            result = result.set_item(key, value)
        return result

    def join(self, other: PersistentAssociation[K, V]) -> PersistentAssociation[K, V]:
        if other.is_empty:
            return self
        if self.is_empty and self.policy is other.policy:
            return other
        return self.set_items(other)

    def append(self, key: K, value: V) -> PersistentAssociation[K, V]:
        found = self._index.get_entry(key)
        if found is None:
            return self._insert_new(self._entries, self._index, len(self), key, value)
        position = self._index_of_stamp(found.value.stamp)
        if position == len(self) - 1 and _equal_value(found.value.value, value):
            return self
        entries = self._entries.remove_at(position)
        if entries is None:
            raise AssertionError("Validated association move failed.")
        return self._insert_new(entries, self._index.remove(key), len(entries), key, value)

    def prepend(self, key: K, value: V) -> PersistentAssociation[K, V]:
        found = self._index.get_entry(key)
        if found is None:
            return self._insert_new(self._entries, self._index, 0, key, value)
        position = self._index_of_stamp(found.value.stamp)
        if position == 0 and _equal_value(found.value.value, value):
            return self
        entries = self._entries.remove_at(position)
        if entries is None:
            raise AssertionError("Validated association move failed.")
        return self._insert_new(entries, self._index.remove(key), 0, key, value)

    def insert(self, position: int, key: K, value: V) -> PersistentAssociation[K, V] | None:
        if position < 0 or position > len(self):
            return None
        entries, index, target = self._entries, self._index, position
        found = index.get_entry(key)
        if found is not None:
            old = self._index_of_stamp(found.value.stamp)
            removed = entries.remove_at(old)
            if removed is None:
                raise AssertionError("Validated association move failed.")
            entries, index = removed, index.remove(key)
            if old < target:
                target -= 1
        return self._insert_new(entries, index, target, key, value)

    def remove(self, key: K) -> PersistentAssociation[K, V]:
        result = self.try_remove(key)
        return self if result is None else result.association

    def try_remove(self, key: K) -> AssociationRemoveResult[K, V] | None:
        removed = self._index.try_remove(key)
        if removed is None:
            return None
        position = self._index_of_stamp(removed.value.stamp)
        entries = self._entries.remove_at(position)
        if entries is None:
            raise AssertionError("Validated association removal failed.")
        return AssociationRemoveResult(
            PersistentAssociation(entries, removed.map), removed.value.value
        )

    def remove_range(self, keys: Iterable[K]) -> PersistentAssociation[K, V]:
        result = self
        for key in keys:
            result = result.remove(key)
        return result

    def key_take(self, keys: Iterable[K]) -> PersistentAssociation[K, V]:
        result: PersistentAssociation[K, V] = PersistentAssociation.empty(self.policy)
        for key in keys:
            found = self._index.get_entry(key)
            if found is not None and not result.contains_key(key):
                result = result.append(found.key, found.value.value)
        return result

    def remove_at(self, position: int) -> PersistentAssociation[K, V] | None:
        entry = self._entries.get(position)
        if entry is None:
            return None
        entries = self._entries.remove_at(position)
        if entries is None:
            raise AssertionError("Validated association positional removal failed.")
        return PersistentAssociation(entries, self._index.remove(entry.key))

    def remove_first(self) -> PersistentAssociation[K, V] | None:
        return self.remove_at(0)

    def remove_last(self) -> PersistentAssociation[K, V] | None:
        return self.remove_at(len(self) - 1)

    def get_range(self, position: int, count: int) -> PersistentAssociation[K, V] | None:
        split = self._entries.split_range(position, count)
        if split is None:
            return None
        if position == 0 and count == len(self):
            return self
        index: PersistentHashMap[K, _Slot[V]] = PersistentHashMap.empty(self.policy)
        for entry in split.range:
            index = index.put(entry.key, _Slot(entry.stamp, entry.value))
        return PersistentAssociation(split.range, index)

    def take(self, count: int) -> PersistentAssociation[K, V] | None:
        return self.get_range(0, count)

    def drop(self, count: int) -> PersistentAssociation[K, V] | None:
        return None if count < 0 or count > len(self) else self.get_range(count, len(self) - count)

    def reverse(self) -> PersistentAssociation[K, V]:
        return self if len(self) <= 1 else self._rebuild(reversed(self._entries.to_list()))

    def key_sort(
        self, comparator: Comparator[K] = default_comparator
    ) -> PersistentAssociation[K, V]:
        if len(self) <= 1:
            return self

        def compare(left: _AssociationEntry[K, V], right: _AssociationEntry[K, V]) -> int:
            order = comparator(left.key, right.key)
            return (
                order
                if order
                else (-1 if left.stamp < right.stamp else 1 if left.stamp > right.stamp else 0)
            )

        return self._rebuild(sorted(self._entries, key=cmp_to_key(compare)))

    def sort(self, comparator: Comparator[V] = default_comparator) -> PersistentAssociation[K, V]:
        if len(self) <= 1:
            return self

        def compare(left: _AssociationEntry[K, V], right: _AssociationEntry[K, V]) -> int:
            order = comparator(left.value, right.value)
            return (
                order
                if order
                else (-1 if left.stamp < right.stamp else 1 if left.stamp > right.stamp else 0)
            )

        return self._rebuild(sorted(self._entries, key=cmp_to_key(compare)))

    def keys(self) -> list[K]:
        return [entry.key for entry in self._entries]

    def values(self) -> list[V]:
        return [entry.value for entry in self._entries]

    def to_list(self) -> list[tuple[K, V]]:
        return list(self)

    @staticmethod
    def _pick_stamp(entries: PersistentDeque[_AssociationEntry[K, V]], position: int) -> int | None:
        if entries.is_empty:
            return 0
        if position == 0:
            return cast(_AssociationEntry[K, V], entries.front()).stamp - _STAMP_GAP
        if position == len(entries):
            return cast(_AssociationEntry[K, V], entries.back()).stamp + _STAMP_GAP
        left = cast(_AssociationEntry[K, V], entries.get(position - 1)).stamp
        right = cast(_AssociationEntry[K, V], entries.get(position)).stamp
        return None if right - left <= 1 else left + (right - left) // 2

    def _insert_new(
        self,
        entries: PersistentDeque[_AssociationEntry[K, V]],
        index: PersistentHashMap[K, _Slot[V]],
        position: int,
        key: K,
        value: V,
    ) -> PersistentAssociation[K, V]:
        stamp = self._pick_stamp(entries, position)
        if stamp is None:
            ordered = entries.to_list()
            ordered.insert(position, _AssociationEntry(0, key, value))
            return self._rebuild(ordered)
        entry = _AssociationEntry(stamp, key, value)
        if position == 0:
            next_entries = entries.prepend(entry)
        elif position == len(entries):
            next_entries = entries.append(entry)
        else:
            inserted = entries.insert_at(position, entry)
            if inserted is None:
                raise AssertionError("Validated association insertion failed.")
            next_entries = inserted
        return PersistentAssociation(next_entries, index.put(key, _Slot(stamp, value)))

    def _rebuild(self, ordered: Iterable[_AssociationEntry[K, V]]) -> PersistentAssociation[K, V]:
        index: PersistentHashMap[K, _Slot[V]] = PersistentHashMap.empty(self.policy)
        entries: list[_AssociationEntry[K, V]] = []
        for position, entry in enumerate(ordered):
            stamp = position * _STAMP_GAP
            entries.append(_AssociationEntry(stamp, entry.key, entry.value))
            index = index.put(entry.key, _Slot(stamp, entry.value))
        return PersistentAssociation(PersistentDeque.from_iterable(entries), index)

    def __iter__(self) -> Iterator[tuple[K, V]]:
        return ((entry.key, entry.value) for entry in self._entries)


__all__ = ["AssociationRemoveResult", "PersistentAssociation"]
