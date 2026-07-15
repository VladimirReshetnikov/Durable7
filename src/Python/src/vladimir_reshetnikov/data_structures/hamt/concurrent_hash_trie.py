"""Lock-coordinated mutable facade over immutable CHAMP roots."""

from __future__ import annotations

from collections.abc import Callable, Iterator
from threading import RLock
from typing import Generic, TypeVar

from .hash_policy import HashPolicy, default_hash_policy
from .persistent_hamt import HamtEntry, PersistentHashMap

K = TypeVar("K")
V = TypeVar("V")


class ConcurrentHashTrieSnapshot(Generic[K, V]):
    """Immutable O(1) point-in-time view of a concurrent trie facade."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentHashMap[K, V]) -> None:
        self._map = map_value

    @property
    def size(self) -> int:
        return self._map.size

    @property
    def is_empty(self) -> bool:
        return self._map.is_empty

    def __len__(self) -> int:
        return self.size

    def get(self, key: K) -> V | None:
        return self._map.get(key)

    def get_entry(self, key: K) -> HamtEntry[K, V] | None:
        return self._map.get_entry(key)

    def contains_key(self, key: K) -> bool:
        return self._map.contains_key(key)

    def to_persistent_hash_map(self) -> PersistentHashMap[K, V]:
        return self._map

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        return iter(self._map)


class ConcurrentHashTrie(Generic[K, V]):
    """Thread-safe snapshotting hash trie coordinated by one reentrant lock.

    Updates publish immutable CHAMP roots and snapshots capture the current root in O(1). This is a
    lock-based Python facade and deliberately makes no lock-free GCAS/RDCSS progress claim.
    """

    __slots__ = ("_generation", "_lock", "_map", "policy")

    def __init__(self, policy: HashPolicy[K] | None = None) -> None:
        self.policy = default_hash_policy() if policy is None else policy
        self._map: PersistentHashMap[K, V] = PersistentHashMap.empty(self.policy)
        self._generation = 0
        self._lock = RLock()

    @property
    def generation(self) -> int:
        with self._lock:
            return self._generation

    @property
    def size(self) -> int:
        with self._lock:
            return self._map.size

    @property
    def is_empty(self) -> bool:
        return self.size == 0

    def __len__(self) -> int:
        return self.size

    def get(self, key: K) -> V | None:
        with self._lock:
            return self._map.get(key)

    def get_entry(self, key: K) -> HamtEntry[K, V] | None:
        with self._lock:
            return self._map.get_entry(key)

    def contains_key(self, key: K) -> bool:
        with self._lock:
            return self._map.contains_key(key)

    def set(self, key: K, value: V) -> None:
        with self._lock:
            self._publish(self._map.put(key, value))

    def try_add(self, key: K, value: V) -> bool:
        with self._lock:
            result = self._map.try_add(key, value)
            if result.added:
                self._publish(result.value)
            return result.added

    def get_or_put(self, key: K, factory: Callable[[K], V]) -> V:
        with self._lock:
            current = self._map.get_entry(key)
            if current is not None:
                return current.value
            value = factory(key)
            # A callback may re-enter because this facade uses RLock. Respect a value it published.
            current = self._map.get_entry(key)
            if current is not None:
                return current.value
            self._publish(self._map.put(key, value))
            return value

    def compute(
        self,
        key: K,
        add: Callable[[K], V],
        update: Callable[[K, V], V],
    ) -> V:
        with self._lock:
            current = self._map.get_entry(key)
            next_value = add(key) if current is None else update(current.key, current.value)
            self._publish(self._map.put(key, next_value))
            stored = self._map.get_entry(key)
            if stored is None:
                raise RuntimeError("Concurrent trie publication lost its computed entry.")
            return stored.value

    def remove(self, key: K) -> HamtEntry[K, V] | None:
        with self._lock:
            result = self._map.try_remove_entry(key)
            if result is None:
                return None
            self._publish(result.map)
            return result.entry

    def clear(self) -> None:
        with self._lock:
            self._publish(self._map.clear())

    def snapshot(self) -> ConcurrentHashTrieSnapshot[K, V]:
        with self._lock:
            return ConcurrentHashTrieSnapshot(self._map)

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        return iter(self.snapshot())

    def _publish(self, next_value: PersistentHashMap[K, V]) -> None:
        if next_value is self._map:
            return
        self._map = next_value
        self._generation += 1


__all__ = ["ConcurrentHashTrie", "ConcurrentHashTrieSnapshot"]
