"""Set-valued persistent hash multimap over the public CHAMP map and set."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .hash_policy import HashPolicy, default_hash_policy
from .persistent_hamt import PersistentHashMap, PersistentHashSet

K = TypeVar("K")
V = TypeVar("V")

_INT64_MAX = (1 << 63) - 1


@dataclass(frozen=True, slots=True)
class HashMultimapEntry(Generic[K, V]):
    """One stored representative pair."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class HashMultimapKeyResult(Generic[K]):
    """Presence-safe stored key lookup; ``key`` may itself be ``None``."""

    found: bool
    key: K | None = None


@dataclass(frozen=True, slots=True)
class HashMultimapValuesResult(Generic[V]):
    """Presence-safe lookup of a non-empty value group."""

    found: bool
    values: PersistentHashSet[V]


class PersistentHashMultimap(Generic[K, V]):
    """Immutable set-valued hash multimap with independent domain policies.

    The outer CHAMP map stores no empty value groups. Both domains retain their first
    representatives, and duplicate addition is an exact object-identity no-op.
    """

    __slots__ = ("_groups", "_pair_count", "_value_policy")

    def __init__(
        self,
        groups: PersistentHashMap[K, PersistentHashSet[V]],
        value_policy: HashPolicy[V],
        pair_count: int,
    ) -> None:
        self._groups = groups
        self._value_policy = value_policy
        self._pair_count = pair_count

    @classmethod
    def empty(
        cls,
        key_policy: HashPolicy[K] | None = None,
        value_policy: HashPolicy[V] | None = None,
    ) -> PersistentHashMultimap[K, V]:
        """Return an empty multimap retaining both exact policy objects."""

        effective_keys = default_hash_policy() if key_policy is None else key_policy
        effective_values = default_hash_policy() if value_policy is None else value_policy
        groups: PersistentHashMap[K, PersistentHashSet[V]] = PersistentHashMap.empty(effective_keys)
        return cls(groups, effective_values, 0)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        key_policy: HashPolicy[K] | None = None,
        value_policy: HashPolicy[V] | None = None,
    ) -> PersistentHashMultimap[K, V]:
        """Build from pairs, ignoring duplicates under the retained policies."""

        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(key_policy, value_policy)
        for key, value in items:
            result = result.add(key, value)
        return result

    @property
    def key_count(self) -> int:
        return self._groups.size

    @property
    def pair_count(self) -> int:
        return self._pair_count

    @property
    def is_empty(self) -> bool:
        return self._pair_count == 0

    @property
    def key_policy(self) -> HashPolicy[K]:
        return self._groups.policy

    @property
    def value_policy(self) -> HashPolicy[V]:
        return self._value_policy

    def __len__(self) -> int:
        return self._pair_count

    def __bool__(self) -> bool:
        return not self.is_empty

    def contains_key(self, key: K) -> bool:
        return self._groups.contains_key(key)

    def contains(self, key: K, value: V) -> bool:
        group = self._groups.get_entry(key)
        return group is not None and group.value.contains(value)

    def try_get_key(self, key: K) -> HashMultimapKeyResult[K]:
        """Return the first stored key representative without conflating ``None`` and absence."""

        group = self._groups.get_entry(key)
        return (
            HashMultimapKeyResult(False)
            if group is None
            else HashMultimapKeyResult(True, group.key)
        )

    def get_values(self, key: K) -> PersistentHashSet[V]:
        """Return the represented group or a value-policy-preserving empty set."""

        group = self._groups.get_entry(key)
        return PersistentHashSet.empty(self._value_policy) if group is None else group.value

    def try_get_values(self, key: K) -> HashMultimapValuesResult[V]:
        group = self._groups.get_entry(key)
        return (
            HashMultimapValuesResult(False, PersistentHashSet.empty(self._value_policy))
            if group is None
            else HashMultimapValuesResult(True, group.value)
        )

    def add(self, key: K, value: V) -> PersistentHashMultimap[K, V]:
        """Add one pair or return this object when it already exists."""

        group = self._groups.get_entry(key)
        if group is None:
            if self._pair_count == _INT64_MAX:
                raise OverflowError("The operation would exceed the signed 64-bit pair count.")
            values = PersistentHashSet.empty(self._value_policy).add(value)
            return PersistentHashMultimap(
                self._groups.add(key, values), self._value_policy, self._pair_count + 1
            )

        added = group.value.try_add(value)
        if not added.added:
            return self
        if self._pair_count == _INT64_MAX:
            raise OverflowError("The operation would exceed the signed 64-bit pair count.")
        return PersistentHashMultimap(
            self._groups.put(group.key, added.value),
            self._value_policy,
            self._pair_count + 1,
        )

    def remove(self, key: K, value: V) -> PersistentHashMultimap[K, V]:
        """Remove one pair and contract a group when its final value disappears."""

        group = self._groups.get_entry(key)
        if group is None:
            return self
        removed = group.value.try_remove(value)
        if removed is None:
            return self
        groups = (
            self._groups.remove(group.key)
            if removed.set.is_empty
            else self._groups.put(group.key, removed.set)
        )
        return PersistentHashMultimap(groups, self._value_policy, self._pair_count - 1)

    def remove_key(self, key: K) -> PersistentHashMultimap[K, V]:
        """Remove one complete key class and all of its pairs."""

        removed = self._groups.try_remove_entry(key)
        if removed is None:
            return self
        return PersistentHashMultimap(
            removed.map,
            self._value_policy,
            self._pair_count - removed.entry.value.size,
        )

    def clear(self) -> PersistentHashMultimap[K, V]:
        return self if self.is_empty else self.empty(self.key_policy, self._value_policy)

    def keys(self) -> Iterator[K]:
        return self._groups.keys()

    def __iter__(self) -> Iterator[HashMultimapEntry[K, V]]:
        for group in self._groups:
            for value in group.value:
                yield HashMultimapEntry(group.key, value)

    def shares_root_with(self, other: PersistentHashMultimap[K, V]) -> bool:
        """Return whether the two facades reference the same outer CHAMP root."""

        return self._groups.shares_root_with(other._groups)

    def validate_structure(self) -> bool:
        """Recompute counts, policy propagation, and the no-empty-group invariant."""

        pairs = 0
        for group in self._groups:
            if group.value.is_empty or group.value.policy is not self._value_policy:
                return False
            pairs += group.value.size
            if pairs > _INT64_MAX:
                return False
        return pairs == self._pair_count and (pairs == 0) == self._groups.is_empty


__all__ = [
    "HashMultimapEntry",
    "HashMultimapKeyResult",
    "HashMultimapValuesResult",
    "PersistentHashMultimap",
]
