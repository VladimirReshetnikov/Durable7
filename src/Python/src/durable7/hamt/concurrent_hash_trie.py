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
    lock-based Python facade and deliberately makes no lock-free GCAS/RDCSS progress claim. A
    mutation retries whenever a reentrant factory or hash-policy callback replaces its captured
    root, so it never publishes a successor derived from obsolete state.
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
            while True:
                captured = self._map
                next_value = captured.put(key, value)
                if self._map is not captured:
                    continue
                self._publish(next_value)
                return

    def try_add(self, key: K, value: V) -> bool:
        with self._lock:
            while True:
                captured = self._map
                result = captured.try_add(key, value)
                if self._map is not captured:
                    continue
                if result.added:
                    self._publish(result.value)
                return result.added

    def get_or_put(self, key: K, factory: Callable[[K], V]) -> V:
        """Return a present value or publish one produced from the caller's lookup key.

        A stored ``None`` is present. If the factory re-enters this facade and publishes an
        equivalent key, that nested value wins the post-callback recheck. The user factory runs at
        most once; retries caused by hash-policy reentry reuse its candidate.
        """

        with self._lock:
            while True:
                captured = self._map
                current = captured.get_entry(key)
                if self._map is not captured:
                    continue
                if current is not None:
                    return current.value
                value = factory(key)
                return self._publish_get_or_put_candidate(key, value)

    def compute(
        self,
        key: K,
        add: Callable[[K], V],
        update: Callable[[K, V], V],
    ) -> V:
        """Add or update through the caller's key and the latest stored value.

        Reentrant factories can change the immutable root while the outer operation is computing.
        Such an operation retries against the newly published root, so either factory may run more
        than once and no successor derived from an obsolete root is installed.
        """

        with self._lock:
            while True:
                captured = self._map
                result = captured.add_or_update(key, add, update)
                if self._map is not captured:
                    # RLock permits a factory to call back into this facade. Never publish the
                    # successor it computed from an obsolete root; retry against the root installed
                    # by that nested operation instead.
                    continue
                self._publish(result.map)
                return result.value

    def remove(self, key: K) -> HamtEntry[K, V] | None:
        with self._lock:
            while True:
                captured = self._map
                result = captured.try_remove_entry(key)
                if self._map is not captured:
                    continue
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

    def _publish_get_or_put_candidate(self, key: K, value: V) -> V:
        while True:
            captured = self._map
            result = captured.get_or_add(key, lambda _key: value)
            if self._map is not captured:
                continue
            self._publish(result.map)
            return result.value

    def _publish(self, next_value: PersistentHashMap[K, V]) -> None:
        if next_value is self._map:
            return
        self._map = next_value
        self._generation += 1


__all__ = ["ConcurrentHashTrie", "ConcurrentHashTrieSnapshot"]
