"""Immutable comparer-aware hash bag over the persistent CHAMP map."""

from __future__ import annotations

import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy
from .persistent_hamt import PersistentHashMap

T = TypeVar("T")

_MAX_MULTIPLICITY = (1 << 31) - 1
_DEFAULT_EMPTY_BAG: PersistentHashBag[object] | None = None


def _validate_copy_count(count: int) -> None:
    if not isinstance(count, int):
        raise TypeError("count must be an integer.")
    if count < 0:
        raise ValueError("count must be non-negative.")
    if count > _MAX_MULTIPLICITY:
        raise OverflowError("count exceeds the maximum per-class multiplicity.")


def _checked_add_count(left: int, right: int) -> int:
    result = left + right
    if result > _MAX_MULTIPLICITY:
        raise OverflowError("The resulting per-class multiplicity exceeds 2^31 - 1.")
    return result


@dataclass(frozen=True, slots=True)
class HashBagEntry(Generic[T]):
    """A stored representative and its positive multiplicity."""

    value: T
    count: int


class PersistentHashBag(Generic[T]):
    """Persistent unordered multiset with receiver-policy algebra.

    Default iteration is expanded: each representative is yielded ``count`` times,
    contiguously, in the underlying stable-for-one-version CHAMP order.
    """

    __slots__ = ("_counts", "_total_count")

    def __init__(self, counts: PersistentHashMap[T, int], total_count: int) -> None:
        """Wrap an already-built count map; use :meth:`empty` or :meth:`from_values` instead.

        The caller is responsible for ``total_count`` matching ``counts`` and for every stored count
        being positive.
        """

        self._counts = counts
        self._total_count = total_count

    @classmethod
    def empty(cls, policy: HashPolicy[T] | None = None) -> PersistentHashBag[T]:
        """Return an empty bag retaining the exact policy object."""

        effective_policy = default_hash_policy() if policy is None else policy
        if effective_policy is default_hash_policy():
            global _DEFAULT_EMPTY_BAG
            if _DEFAULT_EMPTY_BAG is None:
                _DEFAULT_EMPTY_BAG = PersistentHashBag(
                    PersistentHashMap[object, int].empty(
                        cast("HashPolicy[object]", effective_policy)
                    ),
                    0,
                )
            return cast("PersistentHashBag[T]", _DEFAULT_EMPTY_BAG)
        return cls(PersistentHashMap.empty(effective_policy), 0)

    @classmethod
    def from_values(
        cls,
        values: Iterable[T],
        policy: HashPolicy[T] | None = None,
    ) -> PersistentHashBag[T]:
        """Build a bag from values, collapsing equivalent ones into counted classes.

        The first occurrence of each class becomes its stored representative.
        """

        builder = PersistentHashMap[T, int].create_bulk_builder(policy)
        total_count = 0
        for value in values:
            builder._add_or_update(value, 1, _checked_add_count)
            total_count += 1
        if total_count == 0:
            return cls.empty(policy)
        return cls(builder.to_immutable(), total_count)

    @property
    def distinct_count(self) -> int:
        """Number of distinct equivalence classes, ignoring multiplicities."""

        return self._counts.size

    @property
    def total_count(self) -> int:
        """Total number of occurrences, summed across classes.

        Widened beyond the per-class 32-bit count domain so that summing in-range classes cannot
        overflow.
        """

        return self._total_count

    @property
    def is_empty(self) -> bool:
        """Whether the bag holds no occurrences."""

        return self._counts.is_empty

    @property
    def policy(self) -> HashPolicy[T]:
        """The retained hash policy defining equivalence classes."""

        return self._counts.policy

    def __bool__(self) -> bool:
        """Whether the bag holds at least one occurrence."""

        return not self.is_empty

    def contains(self, value: T) -> bool:
        """Whether ``value``'s class occurs at least once."""

        return self._counts.contains_key(value)

    def __contains__(self, value: object) -> bool:
        """Whether ``value``'s class occurs at least once, for the ``in`` operator."""

        return self.contains(cast("T", value))

    def count_of(self, value: T) -> int:
        """Multiplicity of ``value``'s class, or zero when absent."""

        entry = self._counts.get_entry(value)
        return 0 if entry is None else entry.value

    def get(self, equal_value: T) -> T | None:
        """Return the stored representative equivalent to ``equal_value``, or ``None`` when absent.

        The stored representative is the first one added for its class, which need not be the value
        passed in.
        """

        entry = self._counts.get_entry(equal_value)
        return None if entry is None else entry.key

    def get_entry(self, equal_value: T) -> HashBagEntry[T] | None:
        """Return the stored representative together with its count, or ``None`` when absent."""

        entry = self._counts.get_entry(equal_value)
        return None if entry is None else HashBagEntry(entry.key, entry.value)

    def add(self, value: T) -> PersistentHashBag[T]:
        """Return a bag with one more occurrence of ``value``."""

        return self.add_copies(value, 1)

    def add_copies(self, value: T, count: int) -> PersistentHashBag[T]:
        """Return a bag with ``count`` more occurrences of ``value``.

        Adding zero copies is a receiver-returning no-op. A negative count, or a class that would
        exceed the 32-bit per-class maximum, raises rather than wrapping.
        """

        _validate_copy_count(count)
        if count == 0:
            return self
        update = self._counts.add_or_update(
            value,
            lambda _key: count,
            lambda _key, current: _checked_add_count(current, count),
        )
        return self._with_counts(update.map, self._total_count + count)

    def remove(self, value: T) -> PersistentHashBag[T]:
        """Return a bag with one fewer occurrence of ``value``; a no-op when absent."""

        return self.remove_copies(value, 1)

    def remove_copies(self, value: T, count: int) -> PersistentHashBag[T]:
        """Return a bag with ``count`` fewer occurrences of ``value``.

        Removing at least the current multiplicity drops the class entirely rather than storing a
        zero count. Removing zero copies, or removing from an absent class, returns the receiver.
        """

        _validate_copy_count(count)
        if count == 0:
            return self
        existing = self._counts.get_entry(value)
        if existing is None:
            return self
        if count >= existing.value:
            removed = self._counts.try_remove_entry(value)
            if removed is None:
                raise AssertionError("A located bag entry could not be removed.")
            return self._with_counts(removed.map, self._total_count - removed.entry.value)
        return self._with_counts(
            self._counts.put(value, existing.value - count),
            self._total_count - count,
        )

    def remove_all(self, value: T) -> PersistentHashBag[T]:
        """Return a bag without ``value``'s class, whatever its multiplicity; a no-op when absent.
        """

        removed = self._counts.try_remove_entry(value)
        if removed is None:
            return self
        return self._with_counts(removed.map, self._total_count - removed.entry.value)

    def clear(self) -> PersistentHashBag[T]:
        """Return an empty bag retaining this bag's policy."""

        return self._with_counts(self._counts.clear(), 0)

    def union(self, other: PersistentHashBag[T]) -> PersistentHashBag[T]:
        """Return the multiset union: each class keeps the larger of the two multiplicities.

        The argument is normalized to this bag's policy first, so membership is judged the way the
        receiver defines it, and the receiver's stored representatives survive.
        """

        normalized = self._normalize_argument(other)
        if normalized is self:
            return self
        result = self
        for entry in normalized.entries():
            current = result.count_of(entry.value)
            if entry.count > current:
                result = result.add_copies(entry.value, entry.count - current)
        return result

    def intersect(self, other: PersistentHashBag[T]) -> PersistentHashBag[T]:
        """Return the multiset intersection: each class keeps the smaller of the two multiplicities.

        Judged under the receiver's policy, as for :meth:`union`.
        """

        normalized = self._normalize_argument(other)
        if normalized is self:
            return self
        result = self
        for entry in self.entries():
            argument_count = normalized.count_of(entry.value)
            if argument_count == 0:
                result = result.remove_all(entry.value)
            elif argument_count < entry.count:
                result = result.remove_copies(entry.value, entry.count - argument_count)
        return result

    def except_(self, other: PersistentHashBag[T]) -> PersistentHashBag[T]:
        """Return this bag with the argument's multiplicities subtracted, floored at zero.

        Named with a trailing underscore because ``except`` is a Python keyword. Judged under the
        receiver's policy, as for :meth:`union`.
        """

        normalized = self._normalize_argument(other)
        if normalized is self:
            return self.clear()
        result = self
        for entry in normalized.entries():
            result = result.remove_copies(entry.value, entry.count)
        return result

    def sum(self, other: PersistentHashBag[T]) -> PersistentHashBag[T]:
        """Return the multiset sum: multiplicities are added rather than maximized.

        This is the operation that differs from :meth:`union`, which takes the maximum.
        """

        normalized = self._normalize_argument(other)
        result = self
        for entry in normalized.entries():
            result = result.add_copies(entry.value, entry.count)
        return result

    def distinct_items(self) -> Iterator[T]:
        """Iterate the stored representatives once each, ignoring multiplicities."""

        return self._counts.keys()

    def entries(self) -> Iterator[HashBagEntry[T]]:
        """Iterate each stored representative together with its multiplicity."""

        for entry in self._counts:
            yield HashBagEntry(entry.key, entry.value)

    def to_list(self) -> list[T]:
        """Return every occurrence as a list, repeating each representative by its count.

        Raises :class:`OverflowError` when the expanded total exceeds what a Python list can hold.
        """

        if self._total_count > sys.maxsize:
            raise OverflowError("The expanded bag does not fit in a Python list.")
        return list(self)

    def _normalize_argument(self, other: PersistentHashBag[T]) -> PersistentHashBag[T]:
        if not isinstance(other, PersistentHashBag):
            raise TypeError("Hash-bag algebra requires another PersistentHashBag.")
        if self.policy is other.policy:
            return other
        builder = PersistentHashMap[T, int].create_bulk_builder(self.policy)
        total_count = 0
        for entry in other.entries():
            builder._add_or_update(entry.value, entry.count, _checked_add_count)
            total_count += entry.count
        return PersistentHashBag(builder.to_immutable(), total_count)

    def _with_counts(
        self,
        counts: PersistentHashMap[T, int],
        total_count: int,
    ) -> PersistentHashBag[T]:
        if counts is self._counts:
            if total_count != self._total_count:
                raise AssertionError("An unchanged bag map cannot have a different total count.")
            return self
        if counts.is_empty and counts.policy is default_hash_policy():
            return PersistentHashBag.empty()
        return PersistentHashBag(counts, total_count)

    def __iter__(self) -> Iterator[T]:
        """Iterate occurrences expanded, yielding each representative ``count`` times contiguously.
        """

        for entry in self._counts:
            for _ in range(entry.value):
                yield entry.key


__all__ = ["HashBagEntry", "PersistentHashBag"]
