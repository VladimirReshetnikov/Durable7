"""Grouped persistent ordered multimap over the neutral ordered map and set."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from ..hamt import HashPolicy, default_hash_policy
from .persistent_ordered_map import PersistentOrderedMap
from .persistent_ordered_set import OrderedSetValueResult, PersistentOrderedSet

K = TypeVar("K")
V = TypeVar("V")

_INT64_MAX = (1 << 63) - 1


@dataclass(frozen=True, slots=True)
class OrderedMultimapEntry(Generic[K, V]):
    """One pair in key-grouped insertion order."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class OrderedMultimapValuesResult(Generic[V]):
    """Presence-safe lookup of one nonempty ordered value group."""

    found: bool
    values: PersistentOrderedSet[V]


@dataclass(frozen=True, slots=True)
class OrderedMultimapKeyResult(Generic[K]):
    """Presence-safe retained key lookup; the representative may itself be ``None``."""

    found: bool
    key: K | None = None


@dataclass(frozen=True, slots=True)
class OrderedMultimapAddResult(Generic[K, V]):
    """Nonthrowing result of adding one pair."""

    added: bool
    map: PersistentOrderedMultimap[K, V]


@dataclass(frozen=True, slots=True)
class OrderedMultimapCursorSearch(Generic[K, V]):
    """Focused pair or group lookup over flattened key-grouped order."""

    found: bool
    cursor: PersistentOrderedMultimapCursor[K, V]


def _same_set(left: PersistentOrderedSet[V], right: PersistentOrderedSet[V]) -> bool:
    return left is right


class PersistentOrderedMultimap(Generic[K, V]):
    """Immutable set-valued multimap with nested key and value insertion order.

    Flattened iteration is key-grouped rather than one globally interleaved pair-arrival history.
    Key and value equivalence retain independent policies, and empty value groups are never stored.
    """

    __slots__ = ("_groups", "_pair_count", "_value_policy")

    def __init__(
        self,
        groups: PersistentOrderedMap[K, PersistentOrderedSet[V]],
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
    ) -> PersistentOrderedMultimap[K, V]:
        effective_keys = default_hash_policy() if key_policy is None else key_policy
        effective_values = default_hash_policy() if value_policy is None else value_policy
        groups = PersistentOrderedMap[K, PersistentOrderedSet[V]].empty(effective_keys, _same_set)
        return cls(groups, effective_values, 0)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        key_policy: HashPolicy[K] | None = None,
        value_policy: HashPolicy[V] | None = None,
    ) -> PersistentOrderedMultimap[K, V]:
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
        return self._groups.key_policy

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
        group = self._groups.try_get_entry(key)
        return bool(group.found and group.entry is not None and group.entry.value.contains(value))

    def count_values(self, key: K) -> int:
        group = self._groups.try_get_entry(key)
        return 0 if not group.found or group.entry is None else group.entry.value.size

    def get_values(self, key: K) -> PersistentOrderedSet[V]:
        group = self._groups.try_get_entry(key)
        return (
            PersistentOrderedSet.empty(self._value_policy)
            if not group.found or group.entry is None
            else group.entry.value
        )

    def try_get_values(self, key: K) -> OrderedMultimapValuesResult[V]:
        group = self._groups.try_get_entry(key)
        return (
            OrderedMultimapValuesResult(False, PersistentOrderedSet.empty(self._value_policy))
            if not group.found or group.entry is None
            else OrderedMultimapValuesResult(True, group.entry.value)
        )

    def try_get_key(self, key: K) -> OrderedMultimapKeyResult[K]:
        group = self._groups.try_get_entry(key)
        return (
            OrderedMultimapKeyResult(False)
            if not group.found or group.entry is None
            else OrderedMultimapKeyResult(True, group.entry.key)
        )

    def try_get_value(self, key: K, value: V) -> OrderedSetValueResult[V]:
        group = self._groups.try_get_entry(key)
        return (
            OrderedSetValueResult(False, value)
            if not group.found or group.entry is None
            else group.entry.value.try_get_value(value)
        )

    def cursor_at(self, position: int = 0) -> PersistentOrderedMultimapCursor[K, V]:
        """Return an immutable flattened key-grouped pair gap cursor."""

        return PersistentOrderedMultimapCursor(self, position)

    def find_cursor(self, key: K, value: V) -> OrderedMultimapCursorSearch[K, V]:
        """Focus a pair or return the pair-end cursor on a miss."""

        position = self._cursor_index_of(key, value)
        return OrderedMultimapCursorSearch(
            position >= 0,
            PersistentOrderedMultimapCursor(self, self._pair_count if position < 0 else position),
        )

    def find_group_cursor(self, key: K) -> OrderedMultimapCursorSearch[K, V]:
        """Focus the first pair in a key group or return the pair-end cursor."""

        for position, pair in enumerate(self):
            if self.key_policy.equivalent(pair.key, key):
                return OrderedMultimapCursorSearch(
                    True, PersistentOrderedMultimapCursor(self, position)
                )
        return OrderedMultimapCursorSearch(
            False, PersistentOrderedMultimapCursor(self, self._pair_count)
        )

    def _cursor_entry_at(self, rank: int) -> OrderedMultimapEntry[K, V]:
        if type(rank) is not int or rank < 0 or rank >= self._pair_count:
            raise IndexError("rank must identify a key-grouped pair.")
        for position, pair in enumerate(self):
            if position == rank:
                return pair
        raise RuntimeError("The ordered multimap pair count disagrees with its groups.")

    def _cursor_index_of(self, key: K, value: V) -> int:
        for position, pair in enumerate(self):
            if self.key_policy.equivalent(pair.key, key) and self._value_policy.equivalent(
                pair.value, value
            ):
                return position
        return -1

    def _cursor_group_end(self, key: K) -> int:
        """Pair rank of the gap immediately after the last pair of an equivalent key group.

        Returns the pair-end gap when the key is absent. Key groups are contiguous in the flattened
        enumeration, so this walks the leading pairs once and consults only the key policy, never
        the value policy.
        """

        position = 0
        group_end = 0
        seen_group = False
        for pair in self:
            if self.key_policy.equivalent(pair.key, key):
                position += 1
                seen_group = True
                group_end = position
            elif seen_group:
                return group_end
            else:
                position += 1
        return group_end if seen_group else position

    def add(self, key: K, value: V) -> PersistentOrderedMultimap[K, V]:
        group = self._groups.try_get_entry(key)
        if group.found and group.entry is not None:
            values = group.entry.value.add(value)
            if values is group.entry.value:
                return self
            return PersistentOrderedMultimap(
                self._groups.set(group.entry.key, values),
                self._value_policy,
                self._increment_pair_count(),
            )
        values = PersistentOrderedSet.empty(self._value_policy).add(value)
        return PersistentOrderedMultimap(
            self._groups.add(key, values),
            self._value_policy,
            self._increment_pair_count(),
        )

    def try_add(self, key: K, value: V) -> OrderedMultimapAddResult[K, V]:
        result = self.add(key, value)
        return OrderedMultimapAddResult(result is not self, result)

    def remove(self, key: K, value: V) -> PersistentOrderedMultimap[K, V]:
        group = self._groups.try_get_entry(key)
        if not group.found or group.entry is None:
            return self
        removed = group.entry.value.try_remove(value)
        if not removed.removed:
            return self
        groups = (
            self._groups.remove(group.entry.key)
            if removed.set.is_empty
            else self._groups.set(group.entry.key, removed.set)
        )
        return PersistentOrderedMultimap(groups, self._value_policy, self._pair_count - 1)

    def remove_key(self, key: K) -> PersistentOrderedMultimap[K, V]:
        group = self._groups.try_get_entry(key)
        if not group.found or group.entry is None:
            return self
        return PersistentOrderedMultimap(
            self._groups.remove(group.entry.key),
            self._value_policy,
            self._pair_count - group.entry.value.size,
        )

    def clear(self) -> PersistentOrderedMultimap[K, V]:
        return self if self.is_empty else self.empty(self.key_policy, self._value_policy)

    def keys(self) -> Iterator[K]:
        return self._groups.keys()

    def groups(self) -> Iterator[tuple[K, PersistentOrderedSet[V]]]:
        for group in self._groups:
            yield group.key, group.value

    def __iter__(self) -> Iterator[OrderedMultimapEntry[K, V]]:
        for group in self._groups:
            for value in group.value:
                yield OrderedMultimapEntry(group.key, value)

    def shares_groups_roots_with(self, other: PersistentOrderedMultimap[K, V]) -> bool:
        return self._groups.shares_order_storage_with(
            other._groups
        ) and self._groups.shares_membership_root_with(other._groups)

    def validate_structure(self) -> bool:
        if not self._groups.validate_structure():
            return False
        pair_count = 0
        for group in self._groups:
            if group.value.is_empty or group.value.policy is not self._value_policy:
                return False
            group.value._validate_invariants()
            pair_count += group.value.size
            if pair_count > _INT64_MAX:
                return False
        return pair_count == self._pair_count

    def _increment_pair_count(self) -> int:
        if self._pair_count == _INT64_MAX:
            raise OverflowError("The operation would exceed the signed 64-bit pair count.")
        return self._pair_count + 1


@dataclass(frozen=True, slots=True)
class PersistentOrderedMultimapCursor(Generic[K, V]):
    """Immutable root-plus-pair-rank cursor over grouped ordered-multimap enumeration."""

    map: PersistentOrderedMultimap[K, V]
    position: int = 0

    def __post_init__(self) -> None:
        if (
            type(self.position) is not int
            or self.position < 0
            or self.position > self.map.pair_count
        ):
            raise IndexError("Cursor position is outside the ordered multimap.")

    @property
    def pair_count(self) -> int:
        return self.map.pair_count

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.pair_count

    def peek_previous(self) -> OrderedMultimapEntry[K, V] | None:
        return None if self.is_at_start else self.map._cursor_entry_at(self.position - 1)

    def peek_next(self) -> OrderedMultimapEntry[K, V] | None:
        return None if self.is_at_end else self.map._cursor_entry_at(self.position)

    def move_previous(self) -> PersistentOrderedMultimapCursor[K, V]:
        if self.is_at_start:
            raise IndexError("The ordered-multimap cursor is already at the start.")
        return PersistentOrderedMultimapCursor(self.map, self.position - 1)

    def move_next(self) -> PersistentOrderedMultimapCursor[K, V]:
        if self.is_at_end:
            raise IndexError("The ordered-multimap cursor is already at the end.")
        return PersistentOrderedMultimapCursor(self.map, self.position + 1)

    def seek(self, position: int) -> PersistentOrderedMultimapCursor[K, V]:
        return (
            self
            if position == self.position
            else PersistentOrderedMultimapCursor(self.map, position)
        )

    def add(self, key: K, value: V) -> PersistentOrderedMultimapCursor[K, V]:
        """Add under grouped semantics and return the inserted pair's following gap.

        The gap is derived from the key group's end using only the key policy rather than by
        re-scanning for the accepted pair by value equality. A value that is not reflexive under the
        value policy, such as ``float("nan")`` under a bare ``operator.eq`` policy, is one the
        collection accepts but a content re-scan can never find again, so re-scanning would raise on
        a pair the collection just stored.
        """

        updated = self.map.add(key, value)
        if updated is self.map:
            return self
        return PersistentOrderedMultimapCursor(updated, updated._cursor_group_end(key))

    def try_add(self, key: K, value: V) -> tuple[bool, PersistentOrderedMultimapCursor[K, V]]:
        cursor = self.add(key, value)
        return cursor is not self, cursor

    def delete_previous(self) -> PersistentOrderedMultimapCursor[K, V]:
        """Delete the pair immediately before the gap.

        Removal locates the pair by content and is a no-op when the stored value is not reflexive
        under the value policy (a ``float("nan")``, for instance); the pair count validates the
        removal, so a no-op returns this receiver rather than a false success that removed nothing.
        """

        pair = self.peek_previous()
        if pair is None:
            raise IndexError("The ordered-multimap cursor has no previous pair.")
        updated = self.map.remove(pair.key, pair.value)
        if updated.pair_count == self.map.pair_count:
            return self
        return PersistentOrderedMultimapCursor(updated, self.position - 1)

    def delete_next(self) -> PersistentOrderedMultimapCursor[K, V]:
        """Delete the pair immediately after the gap.

        Removal locates the pair by content and is a no-op when the stored value is not reflexive
        under the value policy (a ``float("nan")``, for instance); the pair count validates the
        removal, so a no-op returns this receiver rather than a false success that removed nothing.
        """

        pair = self.peek_next()
        if pair is None:
            raise IndexError("The ordered-multimap cursor has no next pair.")
        updated = self.map.remove(pair.key, pair.value)
        if updated.pair_count == self.map.pair_count:
            return self
        return PersistentOrderedMultimapCursor(updated, self.position)

    def snapshot(self) -> PersistentOrderedMultimap[K, V]:
        """Return the retained multimap version; the cursor stays usable and unconsumed."""

        return self.map


__all__ = [
    "OrderedMultimapAddResult",
    "OrderedMultimapCursorSearch",
    "OrderedMultimapEntry",
    "OrderedMultimapKeyResult",
    "OrderedMultimapValuesResult",
    "PersistentOrderedMultimap",
    "PersistentOrderedMultimapCursor",
]
