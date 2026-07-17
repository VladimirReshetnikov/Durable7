"""Presence-safe strict patches for persistent hash maps."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy, same_value
from .persistent_hamt import DuplicateKeyError, PersistentHashMap

K = TypeVar("K")
V = TypeVar("V")


@dataclass(frozen=True, slots=True)
class MapPatchValue(Generic[V]):
    """An absent state or a present value, including a present ``None``."""

    is_present: bool
    _value: V | None = None

    @classmethod
    def absent(cls) -> MapPatchValue[V]:
        return cls(False)

    @classmethod
    def present(cls, value: V) -> MapPatchValue[V]:
        return cls(True, value)

    @property
    def value(self) -> V:
        if not self.is_present:
            raise ValueError("An absent patch value has no value.")
        return cast("V", self._value)


@dataclass(frozen=True, slots=True)
class MapPatchEntry(Generic[K, V]):
    """One strict presence-safe before/after change."""

    key: K
    before: MapPatchValue[V]
    after: MapPatchValue[V]


@dataclass(frozen=True, slots=True)
class _Change(Generic[V]):
    before: MapPatchValue[V]
    after: MapPatchValue[V]


class MapPatchConflictError(Exception):
    """Strict application observed a state different from the recorded expectation."""

    def __init__(
        self, key: object, expected: MapPatchValue[object], actual: MapPatchValue[object]
    ) -> None:
        super().__init__("The map does not match the patch's expected state.")
        self.key = key
        self.expected = expected
        self.actual = actual


class MapPatchCompositionError(Exception):
    """Two adjacent patches disagree about their intermediate state."""

    def __init__(
        self,
        key: object,
        left_after: MapPatchValue[object],
        right_before: MapPatchValue[object],
    ) -> None:
        super().__init__("The patches disagree about their intermediate map state.")
        self.key = key
        self.left_after = left_after
        self.right_before = right_before


class PersistentMapPatch(Generic[K, V]):
    """Immutable, invertible strict changes between policy-compatible maps."""

    __slots__ = ("_changes", "value_equals")

    def __init__(
        self,
        changes: PersistentHashMap[K, _Change[V]],
        value_equals: Callable[[V, V], bool],
    ) -> None:
        self._changes = changes
        self.value_equals = value_equals

    @classmethod
    def empty(
        cls,
        key_policy: HashPolicy[K] | None = None,
        value_equals: Callable[[V, V], bool] = same_value,
    ) -> PersistentMapPatch[K, V]:
        effective = default_hash_policy() if key_policy is None else key_policy
        return cls(PersistentHashMap.empty(effective), value_equals)

    @classmethod
    def from_entries(
        cls,
        entries: Iterable[MapPatchEntry[K, V]],
        key_policy: HashPolicy[K] | None = None,
        value_equals: Callable[[V, V], bool] = same_value,
    ) -> PersistentMapPatch[K, V]:
        if entries is None:
            raise TypeError("entries must be iterable.")
        effective = default_hash_policy() if key_policy is None else key_policy
        changes = PersistentHashMap[K, _Change[V]].empty(effective)
        for entry in entries:
            if cls._states_equal(entry.before, entry.after, value_equals):
                continue
            added = changes.try_add(entry.key, _Change(entry.before, entry.after))
            if not added.added:
                raise DuplicateKeyError("An equivalent changed key is already present.")
            changes = added.value
        return cls(changes, value_equals)

    @classmethod
    def between(
        cls,
        before: PersistentHashMap[K, V],
        after: PersistentHashMap[K, V],
        value_equals: Callable[[V, V], bool] = same_value,
    ) -> PersistentMapPatch[K, V]:
        if before.policy is not after.policy:
            raise TypeError("Maps must retain the same hash-policy object.")
        if before.shares_root_with(after):
            return cls.empty(before.policy, value_equals)
        changes = PersistentHashMap[K, _Change[V]].empty(before.policy)
        for entry in before:
            next_entry = after.get_entry(entry.key)
            if next_entry is None:
                changes = changes.add(
                    entry.key, _Change(MapPatchValue.present(entry.value), MapPatchValue.absent())
                )
            elif not value_equals(entry.value, next_entry.value):
                changes = changes.add(
                    entry.key,
                    _Change(
                        MapPatchValue.present(entry.value),
                        MapPatchValue.present(next_entry.value),
                    ),
                )
        for entry in after:
            if not before.contains_key(entry.key):
                changes = changes.add(
                    entry.key, _Change(MapPatchValue.absent(), MapPatchValue.present(entry.value))
                )
        return cls(changes, value_equals)

    @property
    def size(self) -> int:
        return self._changes.size

    @property
    def is_empty(self) -> bool:
        return self._changes.is_empty

    @property
    def key_policy(self) -> HashPolicy[K]:
        return self._changes.policy

    def __len__(self) -> int:
        return self.size

    def __bool__(self) -> bool:
        return not self.is_empty

    def keys(self) -> Iterator[K]:
        return self._changes.keys()

    def apply(self, source: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        self._require_map(source)
        for entry in self._changes:
            actual_entry = source.get_entry(entry.key)
            actual = (
                MapPatchValue[V].absent()
                if actual_entry is None
                else MapPatchValue.present(actual_entry.value)
            )
            if not self._states_equal(entry.value.before, actual, self.value_equals):
                raise MapPatchConflictError(
                    entry.key,
                    cast("MapPatchValue[object]", entry.value.before),
                    cast("MapPatchValue[object]", actual),
                )
        result = source
        for entry in self._changes:
            result = (
                result.put(entry.key, entry.value.after.value)
                if entry.value.after.is_present
                else result.remove(entry.key)
            )
        return result

    def invert(self) -> PersistentMapPatch[K, V]:
        if self.is_empty:
            return self
        changes = PersistentHashMap[K, _Change[V]].empty(self.key_policy)
        for entry in self._changes:
            changes = changes.add(entry.key, _Change(entry.value.after, entry.value.before))
        return PersistentMapPatch(changes, self.value_equals)

    def compose(self, next_patch: PersistentMapPatch[K, V]) -> PersistentMapPatch[K, V]:
        self._require_patch(next_patch)
        if self.is_empty:
            return next_patch
        if next_patch.is_empty:
            return self
        changes = PersistentHashMap[K, _Change[V]].empty(self.key_policy)
        for entry in self._changes:
            right = next_patch._changes.get_entry(entry.key)
            if right is None:
                changes = changes.add(entry.key, entry.value)
                continue
            if not self._states_equal(entry.value.after, right.value.before, self.value_equals):
                raise MapPatchCompositionError(
                    entry.key,
                    cast("MapPatchValue[object]", entry.value.after),
                    cast("MapPatchValue[object]", right.value.before),
                )
            if not self._states_equal(entry.value.before, right.value.after, self.value_equals):
                changes = changes.add(entry.key, _Change(entry.value.before, right.value.after))
        for entry in next_patch._changes:
            if not self._changes.contains_key(entry.key):
                changes = changes.add(entry.key, entry.value)
        return PersistentMapPatch(changes, self.value_equals)

    def clear(self) -> PersistentMapPatch[K, V]:
        return self if self.is_empty else self.empty(self.key_policy, self.value_equals)

    def shares_root_with(self, other: PersistentMapPatch[K, V]) -> bool:
        return self._changes.shares_root_with(other._changes)

    def __iter__(self) -> Iterator[MapPatchEntry[K, V]]:
        for entry in self._changes:
            yield MapPatchEntry(entry.key, entry.value.before, entry.value.after)

    def validate_structure(self) -> bool:
        return all(
            not self._states_equal(entry.value.before, entry.value.after, self.value_equals)
            for entry in self._changes
        )

    def _require_map(self, map_value: PersistentHashMap[K, V]) -> None:
        if map_value.policy is not self.key_policy:
            raise TypeError("The map and patch must retain the same hash-policy object.")

    def _require_patch(self, other: PersistentMapPatch[K, V]) -> None:
        if other.key_policy is not self.key_policy or other.value_equals is not self.value_equals:
            raise TypeError("Patches must retain the same key and value policy objects.")

    @staticmethod
    def _states_equal(
        left: MapPatchValue[V],
        right: MapPatchValue[V],
        value_equals: Callable[[V, V], bool],
    ) -> bool:
        return left.is_present == right.is_present and (
            not left.is_present or value_equals(left.value, right.value)
        )


__all__ = [
    "MapPatchCompositionError",
    "MapPatchConflictError",
    "MapPatchEntry",
    "MapPatchValue",
    "PersistentMapPatch",
]
