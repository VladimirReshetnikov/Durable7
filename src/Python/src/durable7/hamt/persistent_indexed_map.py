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
        """Wrap already-built primary and index structures; use :meth:`empty` or :meth:`from_items`.

        The caller is responsible for the index agreeing with the primary map.
        """

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
        """Return an empty map whose secondary index is keyed by ``index_selector``.

        The selector, both hash policies, and the value comparison are retained by identity, so
        every version derived from this one indexes the same way.
        """

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
        """Build a map from pairs, populating the secondary index as it goes.

        A repeated key replaces the earlier value, as :meth:`set` does.
        """

        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(index_selector, key_policy, value_equals, index_policy)
        for key, value in items:
            result = result.set(key, value)
        return result

    @property
    def size(self) -> int:
        """Number of primary entries."""

        return self._primary.size

    @property
    def index_key_count(self) -> int:
        """Number of distinct index keys, never more than :attr:`size` because the index is
        nonunique.
        """

        return self._index.key_count

    @property
    def is_empty(self) -> bool:
        """Whether the map holds no entries."""

        return self._primary.is_empty

    @property
    def key_policy(self) -> HashPolicy[K]:
        """The retained hash policy defining primary-key equivalence."""

        return self._primary.policy

    @property
    def index_policy(self) -> HashPolicy[I]:
        """The retained hash policy defining index-key equivalence."""

        return self._index.key_policy

    def __len__(self) -> int:
        """Number of primary entries, matching :attr:`size`."""

        return self.size

    def __bool__(self) -> bool:
        """Whether the map holds at least one entry."""

        return not self.is_empty

    def contains_key(self, key: K) -> bool:
        """Whether ``key`` is present in the primary map."""

        return self._primary.contains_key(key)

    def get(self, key: K) -> V | None:
        """Return the value stored for ``key``, or ``None`` when absent.

        Use :meth:`try_get_entry` when a stored ``None`` must stay distinguishable from absence.
        """

        entry = self._primary.get_entry(key)
        return None if entry is None else entry.value.value

    def __getitem__(self, key: K) -> V:
        """Return the value stored for ``key``, raising :class:`KeyError` when absent."""

        entry = self._primary.get_entry(key)
        if entry is None:
            raise KeyError(key)
        return entry.value.value

    def try_get_entry(self, key: K) -> IndexedMapLookup[K, V]:
        """Return ``key``'s stored representative and value without conflating ``None`` with
        absence.
        """

        entry = self._primary.get_entry(key)
        return (
            IndexedMapLookup(False)
            if entry is None
            else IndexedMapLookup(True, IndexedMapEntry(entry.key, entry.value.value))
        )

    def get_stored_key(self, key: K) -> K | None:
        """Return the stored key representative equivalent to ``key``, or ``None`` when absent."""

        entry = self._primary.get_entry(key)
        return None if entry is None else entry.key

    def try_get_index_key(self, key: K) -> IndexedMapIndexResult[I]:
        """Return the index key currently filed for ``key``, without conflating ``None`` with
        absence.

        Reports the value recorded at write time rather than re-running the selector.
        """

        entry = self._primary.get_entry(key)
        return (
            IndexedMapIndexResult(False)
            if entry is None
            else IndexedMapIndexResult(True, entry.value.index_key)
        )

    def get_keys(self, index_key: I) -> PersistentHashSet[K]:
        """Return the primary keys filed under ``index_key``, or a policy-preserving empty set.

        This is the point of the secondary index: the lookup does not scan the primary map.
        """

        return self._index.get_values(index_key)

    def contains_index_key(self, index_key: I) -> bool:
        """Whether at least one entry is filed under ``index_key``."""

        return self._index.contains_key(index_key)

    def add(self, key: K, value: V) -> PersistentIndexedMap[K, V, I]:
        """Add a new entry and file it in the index.

        Raises :class:`DuplicateKeyError` when ``key`` is already present, without invoking the
        selector. The index key is normalized to the group's stored representative.
        """

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
        """Add a new entry, reporting whether it was added instead of raising on a duplicate."""

        if self._primary.contains_key(key):
            return IndexedMapAddResult(False, self)
        return IndexedMapAddResult(True, self.add(key, value))

    def set(self, key: K, value: V) -> PersistentIndexedMap[K, V, I]:
        """Add ``key``, or replace its value when present, moving it between index groups as needed.

        Writing a value the comparison treats as equal returns the receiver without invoking the
        selector. When the selector yields a different index key, the entry is unfiled from its old
        group and filed under the new one; the primary key keeps its stored representative.
        """

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
        """Remove ``key`` from the primary map and from its index group, dropping an emptied group.

        The selector is not invoked, since the filed index key is already recorded. Removing an
        absent key returns the receiver.
        """

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
        """Return an empty map retaining the selector, both policies, and the value comparison."""

        return (
            self
            if self.is_empty
            else self.empty(
                self.index_selector, self.key_policy, self.value_equals, self.index_policy
            )
        )

    def keys(self) -> Iterator[K]:
        """Iterate the primary keys."""

        return self._primary.keys()

    def values(self) -> Iterator[V]:
        """Iterate the values, in their keys' order."""

        for entry in self._primary:
            yield entry.value.value

    def __iter__(self) -> Iterator[IndexedMapEntry[K, V]]:
        """Iterate the primary entries as :class:`IndexedMapEntry` values."""

        for entry in self._primary:
            yield IndexedMapEntry(entry.key, entry.value.value)

    def shares_roots_with(self, other: PersistentIndexedMap[K, V, I]) -> bool:
        """Whether both maps reference the same primary and index roots.

        A representation test used to confirm that a no-op avoided copying, not an equality test.
        """

        return self._primary.shares_root_with(other._primary) and self._index.shares_root_with(
            other._index
        )

    def validate_structure(self) -> bool:
        """Check that the index agrees with the primary map: matching entry counts, a shared key
        policy, and every entry filed exactly once under the index key recorded for it. A
        defensive audit; ordinary operations maintain these invariants.
        """

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
