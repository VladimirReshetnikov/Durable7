"""Persistent primary map with one maintained nonunique secondary index."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy, same_value
from .persistent_hamt import DuplicateKeyError, PersistentHashMap, PersistentHashSet
from .persistent_hash_multimap import PersistentHashMultimap

K = TypeVar("K")
V = TypeVar("V")
I = TypeVar("I")  # noqa: E741 - public generic notation matches the secondary-index domain.


@dataclass(frozen=True, slots=True)
class _IndexedValue(Generic[V, I]):
    value: V
    index_key: I


@dataclass(frozen=True, slots=True)
class IndexedMapEntry(Generic[K, V]):
    """One primary representative and row value."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class IndexedMapLookup(Generic[K, V]):
    """Presence-safe primary lookup."""

    found: bool
    entry: IndexedMapEntry[K, V] | None = None


@dataclass(frozen=True, slots=True)
class IndexedMapIndexResult(Generic[I]):
    """Presence-safe retained secondary-key lookup."""

    found: bool
    index_key: I | None = None


@dataclass(frozen=True, slots=True)
class IndexedMapAddResult(Generic[K, V, I]):
    """Nonthrowing strict-add result."""

    added: bool
    map: PersistentIndexedMap[K, V, I]


class PersistentIndexedMap(Generic[K, V, I]):
    """Immutable primary map with one selector-maintained, set-valued secondary index."""

    __slots__ = ("_index", "_primary", "index_selector", "value_equals")

    def __init__(
        self,
        primary: PersistentHashMap[K, _IndexedValue[V, I]],
        index: PersistentHashMultimap[I, K],
        index_selector: Callable[[K, V], I],
        value_equals: Callable[[V, V], bool],
    ) -> None:
        self._primary = primary
        self._index = index
        self.index_selector = index_selector
        self.value_equals = value_equals

    @classmethod
    def empty(
        cls,
        index_selector: Callable[[K, V], I],
        key_policy: HashPolicy[K] | None = None,
        value_equals: Callable[[V, V], bool] = same_value,
        index_policy: HashPolicy[I] | None = None,
    ) -> PersistentIndexedMap[K, V, I]:
        if not callable(index_selector):
            raise TypeError("index_selector must be callable.")
        effective_keys = default_hash_policy() if key_policy is None else key_policy
        effective_indexes = default_hash_policy() if index_policy is None else index_policy
        return cls(
            PersistentHashMap.empty(effective_keys),
            PersistentHashMultimap.empty(effective_indexes, effective_keys),
            index_selector,
            value_equals,
        )

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        index_selector: Callable[[K, V], I],
        key_policy: HashPolicy[K] | None = None,
        value_equals: Callable[[V, V], bool] = same_value,
        index_policy: HashPolicy[I] | None = None,
    ) -> PersistentIndexedMap[K, V, I]:
        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(index_selector, key_policy, value_equals, index_policy)
        for key, value in items:
            result = result.set(key, value)
        return result

    @property
    def size(self) -> int:
        return self._primary.size

    @property
    def index_key_count(self) -> int:
        return self._index.key_count

    @property
    def is_empty(self) -> bool:
        return self._primary.is_empty

    @property
    def key_policy(self) -> HashPolicy[K]:
        return self._primary.policy

    @property
    def index_policy(self) -> HashPolicy[I]:
        return self._index.key_policy

    def __len__(self) -> int:
        return self.size

    def __bool__(self) -> bool:
        return not self.is_empty

    def contains_key(self, key: K) -> bool:
        return self._primary.contains_key(key)

    def get(self, key: K) -> V | None:
        entry = self._primary.get_entry(key)
        return None if entry is None else entry.value.value

    def __getitem__(self, key: K) -> V:
        entry = self._primary.get_entry(key)
        if entry is None:
            raise KeyError(key)
        return entry.value.value

    def try_get_entry(self, key: K) -> IndexedMapLookup[K, V]:
        entry = self._primary.get_entry(key)
        return (
            IndexedMapLookup(False)
            if entry is None
            else IndexedMapLookup(True, IndexedMapEntry(entry.key, entry.value.value))
        )

    def get_stored_key(self, key: K) -> K | None:
        entry = self._primary.get_entry(key)
        return None if entry is None else entry.key

    def try_get_index_key(self, key: K) -> IndexedMapIndexResult[I]:
        entry = self._primary.get_entry(key)
        return (
            IndexedMapIndexResult(False)
            if entry is None
            else IndexedMapIndexResult(True, entry.value.index_key)
        )

    def get_keys(self, index_key: I) -> PersistentHashSet[K]:
        return self._index.get_values(index_key)

    def contains_index_key(self, index_key: I) -> bool:
        return self._index.contains_key(index_key)

    def add(self, key: K, value: V) -> PersistentIndexedMap[K, V, I]:
        if self._primary.contains_key(key):
            raise DuplicateKeyError("An equivalent primary key is already present.")
        selected = self.index_selector(key, value)
        index = self._index.add(selected, key)
        actual = index.try_get_key(selected)
        if not actual.found:
            raise AssertionError("The added secondary group could not be recovered.")
        actual_index = cast("I", actual.key)
        return PersistentIndexedMap(
            self._primary.add(key, _IndexedValue(value, actual_index)),
            index,
            self.index_selector,
            self.value_equals,
        )

    def try_add(self, key: K, value: V) -> IndexedMapAddResult[K, V, I]:
        if self._primary.contains_key(key):
            return IndexedMapAddResult(False, self)
        return IndexedMapAddResult(True, self.add(key, value))

    def set(self, key: K, value: V) -> PersistentIndexedMap[K, V, I]:
        current = self._primary.get_entry(key)
        if current is None:
            return self.add(key, value)
        if self.value_equals(current.value.value, value):
            return self
        selected = self.index_selector(current.key, value)
        index = self._index
        actual_index = current.value.index_key
        if not self.index_policy.equivalent(current.value.index_key, selected):
            index = index.remove(current.value.index_key, current.key).add(selected, current.key)
            actual = index.try_get_key(selected)
            if not actual.found:
                raise AssertionError("The moved secondary group could not be recovered.")
            actual_index = cast("I", actual.key)
        return PersistentIndexedMap(
            self._primary.put(current.key, _IndexedValue(value, actual_index)),
            index,
            self.index_selector,
            self.value_equals,
        )

    def remove(self, key: K) -> PersistentIndexedMap[K, V, I]:
        current = self._primary.get_entry(key)
        if current is None:
            return self
        return PersistentIndexedMap(
            self._primary.remove(current.key),
            self._index.remove(current.value.index_key, current.key),
            self.index_selector,
            self.value_equals,
        )

    def clear(self) -> PersistentIndexedMap[K, V, I]:
        return (
            self
            if self.is_empty
            else self.empty(
                self.index_selector, self.key_policy, self.value_equals, self.index_policy
            )
        )

    def keys(self) -> Iterator[K]:
        return self._primary.keys()

    def values(self) -> Iterator[V]:
        for entry in self._primary:
            yield entry.value.value

    def __iter__(self) -> Iterator[IndexedMapEntry[K, V]]:
        for entry in self._primary:
            yield IndexedMapEntry(entry.key, entry.value.value)

    def shares_roots_with(self, other: PersistentIndexedMap[K, V, I]) -> bool:
        return self._primary.shares_root_with(other._primary) and self._index.shares_root_with(
            other._index
        )

    def validate_structure(self) -> bool:
        if (
            not self._index.validate_structure()
            or self._primary.size != self._index.pair_count
            or self._index.value_policy is not self.key_policy
        ):
            return False
        for primary_entry in self._primary:
            if not self._index.contains(primary_entry.value.index_key, primary_entry.key):
                return False
        for index_entry in self._index:
            primary = self._primary.get_entry(index_entry.value)
            if (
                primary is None
                or not self.index_policy.equivalent(index_entry.key, primary.value.index_key)
                or primary.key is not index_entry.value
                or primary.value.index_key is not index_entry.key
            ):
                return False
        return True


__all__ = [
    "IndexedMapAddResult",
    "IndexedMapEntry",
    "IndexedMapIndexResult",
    "IndexedMapLookup",
    "PersistentIndexedMap",
]
