"""Strict immutable bidirectional map over two persistent CHAMP maps."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from threading import Lock
from typing import Generic, Literal, TypeVar

from .hash_policy import HashPolicy, default_hash_policy
from .persistent_hamt import HamtEntry, PersistentHashMap

K = TypeVar("K")
V = TypeVar("V")
T = TypeVar("T")


@dataclass(frozen=True, slots=True)
class BiMapLookupResult(Generic[T]):
    """Presence-safe lookup result; ``value`` may itself be ``None``."""

    found: bool
    value: T | None = None


@dataclass(frozen=True, slots=True)
class BiMapAddResult(Generic[K, V]):
    """Nonthrowing strict-add result."""

    added: bool
    map: PersistentBiMap[K, V]
    conflict: Literal["key", "value"] | None = None


@dataclass(frozen=True, slots=True)
class BiMapRemoveResult(Generic[K, V, T]):
    """Nonthrowing removal result retaining the receiver on a miss."""

    removed: bool
    map: PersistentBiMap[K, V]
    value: T | None = None


class BiMapConflictError(ValueError):
    """Raised when one side of a strict bijection is already represented."""

    def __init__(self, conflict: Literal["key", "value"]) -> None:
        """Record which side of the bijection was already represented."""

        super().__init__(f"An equivalent {conflict} is already present.")
        self.conflict = conflict


class PersistentBiMap(Generic[K, V]):
    """Immutable strict bijection with independent key and value policies.

    Every pair is stored in a forward and inverse CHAMP map. The cached ``inverse`` facade swaps
    those roots in O(1); no mapping entries are rebuilt or enumerated.
    """

    __slots__ = ("_forward", "_inverse", "_inverse_lock", "_inverse_view")

    def __init__(
        self,
        forward: PersistentHashMap[K, V],
        inverse: PersistentHashMap[V, K],
    ) -> None:
        """Wrap already-built forward and inverse maps; use the factory methods instead.

        The caller is responsible for the two maps describing the same pair set.
        """

        self._forward = forward
        self._inverse = inverse
        self._inverse_lock = Lock()
        self._inverse_view: PersistentBiMap[V, K] | None = None

    @classmethod
    def empty(
        cls,
        key_policy: HashPolicy[K] | None = None,
        value_policy: HashPolicy[V] | None = None,
    ) -> PersistentBiMap[K, V]:
        """Return an empty bimap retaining the exact independent policy objects."""

        effective_keys = default_hash_policy() if key_policy is None else key_policy
        effective_values = default_hash_policy() if value_policy is None else value_policy
        return cls(
            PersistentHashMap.empty(effective_keys),
            PersistentHashMap.empty(effective_values),
        )

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        key_policy: HashPolicy[K] | None = None,
        value_policy: HashPolicy[V] | None = None,
    ) -> PersistentBiMap[K, V]:
        """Build a strict bimap, rejecting repeated classes in either domain."""

        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(key_policy, value_policy)
        for key, value in items:
            result = result.add(key, value)
        return result

    @property
    def size(self) -> int:
        """Return the number of pairs."""

        return self._forward.size

    @property
    def is_empty(self) -> bool:
        """Return whether no pairs exist."""

        return self._forward.is_empty

    @property
    def key_policy(self) -> HashPolicy[K]:
        """Return the retained key policy."""

        return self._forward.policy

    @property
    def value_policy(self) -> HashPolicy[V]:
        """Return the retained value policy."""

        return self._inverse.policy

    @property
    def inverse(self) -> PersistentBiMap[V, K]:
        """Return the cached O(1) inverse facade whose inverse is this object."""

        current = self._inverse_view
        if current is not None:
            return current
        with self._inverse_lock:
            current = self._inverse_view
            if current is None:
                current = PersistentBiMap(self._inverse, self._forward)
                current._inverse_view = self
                self._inverse_view = current
            return current

    def __len__(self) -> int:
        """Number of pairs, which is both the key count and the value count."""

        return self.size

    def __bool__(self) -> bool:
        """Whether the bimap holds at least one pair."""

        return not self.is_empty

    def contains_key(self, key: K) -> bool:
        """Return whether a key equivalence class exists."""

        return self._forward.contains_key(key)

    def contains_value(self, value: V) -> bool:
        """Return whether a value equivalence class exists."""

        return self._inverse.contains_key(value)

    def __contains__(self, key: object) -> bool:
        """Whether a key equivalence class exists, for the ``in`` operator.

        Membership is tested against keys only; use :meth:`contains_value` for the other side.
        """

        return self.contains_key(key)  # type: ignore[arg-type]

    def __getitem__(self, key: K) -> V:
        """Return the value paired with ``key``, raising :class:`KeyError` when absent."""

        return self._forward[key]

    def get(self, key: K) -> BiMapLookupResult[V]:
        """Return a presence-safe forward lookup result."""

        entry = self._forward.get_entry(key)
        return BiMapLookupResult(False) if entry is None else BiMapLookupResult(True, entry.value)

    def get_key(self, value: V) -> BiMapLookupResult[K]:
        """Return a presence-safe inverse lookup result."""

        entry = self._inverse.get_entry(value)
        return BiMapLookupResult(False) if entry is None else BiMapLookupResult(True, entry.value)

    def add(self, key: K, value: V) -> PersistentBiMap[K, V]:
        """Strictly add a pair when both equivalence classes are absent."""

        result = self.try_add(key, value)
        if not result.added:
            assert result.conflict is not None
            raise BiMapConflictError(result.conflict)
        return result.map

    def try_add(self, key: K, value: V) -> BiMapAddResult[K, V]:
        """Attempt strict addition without throwing for a represented domain class."""

        if self._forward.contains_key(key):
            return BiMapAddResult(False, self, "key")
        if self._inverse.contains_key(value):
            return BiMapAddResult(False, self, "value")
        return BiMapAddResult(
            True,
            PersistentBiMap(self._forward.add(key, value), self._inverse.add(value, key)),
        )

    def set(self, key: K, value: V) -> PersistentBiMap[K, V]:
        """Add or replace a key's value without displacing another key."""

        previous = self._forward.get_entry(key)
        if previous is None:
            if self._inverse.contains_key(value):
                raise BiMapConflictError("value")
            return PersistentBiMap(self._forward.add(key, value), self._inverse.add(value, key))
        if self.value_policy.equivalent(previous.value, value):
            return self
        if self._inverse.contains_key(value):
            raise BiMapConflictError("value")

        stored_key = self._inverse.get_entry(previous.value)
        if stored_key is None:
            raise RuntimeError("PersistentBiMap invariant failure.")
        return PersistentBiMap(
            self._forward.remove(stored_key.value).add(stored_key.value, value),
            self._inverse.remove(previous.value).add(value, stored_key.value),
        )

    def remove_key(self, key: K) -> PersistentBiMap[K, V]:
        """Remove through the key domain or return this object on a miss."""

        return self.try_remove_key(key).map

    def try_remove_key(self, key: K) -> BiMapRemoveResult[K, V, V]:
        """Attempt key removal and return the opposite stored representative."""

        forward = self._forward.get_entry(key)
        if forward is None:
            return BiMapRemoveResult(False, self)
        inverse = self._inverse.get_entry(forward.value)
        if inverse is None:
            raise RuntimeError("PersistentBiMap invariant failure.")
        return BiMapRemoveResult(
            True,
            PersistentBiMap(
                self._forward.remove(inverse.value), self._inverse.remove(forward.value)
            ),
            forward.value,
        )

    def remove_value(self, value: V) -> PersistentBiMap[K, V]:
        """Remove through the value domain or return this object on a miss."""

        return self.try_remove_value(value).map

    def try_remove_value(self, value: V) -> BiMapRemoveResult[K, V, K]:
        """Attempt value removal and return the opposite stored representative."""

        inverse = self._inverse.get_entry(value)
        if inverse is None:
            return BiMapRemoveResult(False, self)
        forward = self._forward.get_entry(inverse.value)
        if forward is None:
            raise RuntimeError("PersistentBiMap invariant failure.")
        return BiMapRemoveResult(
            True,
            PersistentBiMap(
                self._forward.remove(inverse.value), self._inverse.remove(forward.value)
            ),
            inverse.value,
        )

    def clear(self) -> PersistentBiMap[K, V]:
        """Return an empty compatible bimap, preserving an already-empty identity."""

        return self if self.is_empty else PersistentBiMap.empty(self.key_policy, self.value_policy)

    def keys(self) -> Iterator[K]:
        """Iterate stored keys in forward CHAMP order."""

        return self._forward.keys()

    def values(self) -> Iterator[V]:
        """Iterate corresponding values in forward CHAMP order."""

        return self._forward.values()

    def entries(self) -> Iterator[HamtEntry[K, V]]:
        """Iterate forward entries."""

        return iter(self)

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        """Iterate the pairs in the forward map's order."""

        return iter(self._forward)

    def validate_structure(self) -> bool:
        """Check both directions of the bijection invariant."""

        if self._forward.size != self._inverse.size:
            return False
        for forward_entry in self._forward:
            inverse_entry = self._inverse.get_entry(forward_entry.value)
            if inverse_entry is None or not self.key_policy.equivalent(
                forward_entry.key, inverse_entry.value
            ):
                return False
        for inverse_entry in self._inverse:
            matching_forward = self._forward.get_entry(inverse_entry.value)
            if matching_forward is None or not self.value_policy.equivalent(
                inverse_entry.key, matching_forward.value
            ):
                return False
        return True


__all__ = [
    "BiMapAddResult",
    "BiMapConflictError",
    "BiMapLookupResult",
    "BiMapRemoveResult",
    "PersistentBiMap",
]
