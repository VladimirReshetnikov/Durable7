"""Persistent insertion-ordered set over the general HAMT and deque substrates."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from functools import cmp_to_key
from typing import Generic, TypeVar, cast

from ..finger_tree import Comparator, PersistentDeque, default_comparator
from ..hamt import HashPolicy, PersistentHashMap, default_hash_policy

T = TypeVar("T")

_STAMP_STRIDE = 1 << 20
_INT32_MAX = (1 << 31) - 1
_INT64_MIN = -(1 << 63)
_INT64_MAX = (1 << 63) - 1


@dataclass(frozen=True, slots=True)
class _Entry(Generic[T]):
    stamp: int
    value: T


@dataclass(frozen=True, slots=True)
class OrderedSetValueResult(Generic[T]):
    """Presence-preserving stored-representative lookup result.

    On a miss, ``value`` is the caller's lookup value. This keeps a stored ``None`` distinct from
    absence while matching the representative-recovery contract of the sibling implementations.
    """

    found: bool
    value: T


@dataclass(frozen=True, slots=True)
class OrderedSetRemoveResult(Generic[T]):
    """Result of a non-throwing ordered-set removal."""

    set: PersistentOrderedSet[T]
    removed: bool


@dataclass(frozen=True, slots=True)
class OrderedSetCursorPeek(Generic[T]):
    """Presence-discriminated adjacent representative, including stored ``None``."""

    found: bool
    value: T | None = None


@dataclass(frozen=True, slots=True)
class OrderedSetCursorSearch(Generic[T]):
    """Focused equivalence lookup with an append-position miss cursor."""

    found: bool
    cursor: PersistentOrderedSetCursor[T]


@dataclass(frozen=True, slots=True)
class _NormalizedArgument(Generic[T]):
    items: tuple[T, ...]
    membership: PersistentHashMap[T, bool]


class PersistentOrderedSet(Generic[T]):
    """Immutable insertion-ordered set with explicit positional movement.

    Membership and representative retention use one exact :class:`HashPolicy` object. The ordered
    index is an independent persistent deque; no application-specific collection is used as a
    substrate or behavioral oracle.
    """

    __slots__ = ("_order", "_stamps")

    def __init__(
        self,
        order: PersistentDeque[_Entry[T]],
        stamps: PersistentHashMap[T, int],
    ) -> None:
        """Wrap an already-built order sequence and stamp index; use the factory methods instead.

        The caller is responsible for the two indexes describing the same elements.
        """

        if len(order) != stamps.size:
            raise ValueError("The order and membership indexes must have equal counts.")
        self._order = order
        self._stamps = stamps

    @classmethod
    def empty(cls, policy: HashPolicy[T] | None = None) -> PersistentOrderedSet[T]:
        """Return an empty set retaining the effective equality/hash policy."""

        effective = default_hash_policy() if policy is None else policy
        if effective is default_hash_policy():
            return cast("PersistentOrderedSet[T]", _DEFAULT_EMPTY)
        return cls(PersistentDeque.empty(), PersistentHashMap.empty(effective))

    @classmethod
    def from_values(
        cls,
        values: Iterable[T],
        policy: HashPolicy[T] | None = None,
    ) -> PersistentOrderedSet[T]:
        """Build once from first representatives in source order."""

        if values is None:
            raise TypeError("values must be an iterable, not None.")
        effective = default_hash_policy() if policy is None else policy
        seen: PersistentHashMap[T, bool] = PersistentHashMap.empty(effective)
        distinct: list[T] = []
        for value in values:
            added = seen.try_add(value, True)
            if not added.added:
                continue
            seen = added.value
            cls._add_checked(distinct, value)
        return cls._build_from_items(distinct, effective)

    @property
    def size(self) -> int:
        """Return the number of receiver-policy equivalence classes."""

        return len(self._order)

    def __len__(self) -> int:
        """Number of distinct elements, matching :attr:`size`."""

        return self.size

    @property
    def is_empty(self) -> bool:
        """Whether the set holds no elements."""

        return self._order.is_empty

    def __bool__(self) -> bool:
        """Whether the set holds at least one element."""

        return not self.is_empty

    @property
    def policy(self) -> HashPolicy[T]:
        """Return the exact policy object retained by this version."""

        return self._stamps.policy

    @property
    def first(self) -> T:
        """Return the first representative, raising ``IndexError`` when empty."""

        if self.is_empty:
            raise IndexError("The ordered set is empty.")
        return cast("_Entry[T]", self._order.front()).value

    @property
    def last(self) -> T:
        """Return the last representative, raising ``IndexError`` when empty."""

        if self.is_empty:
            raise IndexError("The ordered set is empty.")
        return cast("_Entry[T]", self._order.back()).value

    def contains(self, value: T) -> bool:
        """Whether ``value``'s equivalence class is present.

        Answered through the hash index, so it does not scan the order sequence.
        """

        return self._stamps.contains_key(value)

    def __contains__(self, value: object) -> bool:
        """Whether ``value``'s equivalence class is present, for the ``in`` operator."""

        return self.contains(cast("T", value))

    def try_get_value(self, equal_value: T) -> OrderedSetValueResult[T]:
        """Recover the stored representative without conflating ``None`` and absence."""

        entry = self._stamps.get_entry(equal_value)
        return (
            OrderedSetValueResult(False, equal_value)
            if entry is None
            else OrderedSetValueResult(True, entry.key)
        )

    def get(self, equal_value: T) -> T | None:
        """Return a stored representative or ``None`` as a convenience lookup."""

        result = self.try_get_value(equal_value)
        return result.value if result.found else None

    def get_at(self, index: int) -> T:
        """Return the representative at a non-negative ordered position."""

        self._check_element_index(index)
        return self._order[index].value

    def __getitem__(self, index: int) -> T:
        """The element at ordinal ``index``, raising :class:`IndexError` when out of range."""

        return self.get_at(index)

    def index_of(self, equal_value: T) -> int:
        """Ordinal position of ``equal_value``'s class, or ``-1`` when absent."""

        indexed = self._stamps.get_entry(equal_value)
        return -1 if indexed is None else self._index_of_stamp(indexed.value)

    def cursor_at(self, position: int = 0) -> PersistentOrderedSetCursor[T]:
        """Return an immutable explicit-order gap cursor."""

        return PersistentOrderedSetCursor(self, position)

    def find_cursor(self, equal_value: T) -> OrderedSetCursorSearch[T]:
        """Focus an equivalence class or return the append-position cursor on a miss."""

        position = self.index_of(equal_value)
        return OrderedSetCursorSearch(
            position >= 0,
            PersistentOrderedSetCursor(self, self.size if position < 0 else position),
        )

    def add(self, value: T) -> PersistentOrderedSet[T]:
        """Append an absent class; duplicate addition is an identity no-op."""

        return self._insert_core(self.size, value)

    def add_first(self, value: T) -> PersistentOrderedSet[T]:
        """Prepend an absent class; duplicate addition is an identity no-op."""

        return self._insert_core(0, value)

    def insert(self, index: int, value: T) -> PersistentOrderedSet[T]:
        """Insert an absent class before ``index``, validating before policy callbacks."""

        self._check_insertion_index(index)
        return self._insert_core(index, value)

    def move_to_first(self, equal_value: T) -> PersistentOrderedSet[T]:
        """Move a stored representative to the front, keeping the representative itself."""

        return self._move_existing(0, equal_value)

    def move_to_last(self, equal_value: T) -> PersistentOrderedSet[T]:
        """Move a stored representative to the back, keeping the representative itself."""

        return self._move_existing(self.size - 1, equal_value)

    def move_to(self, index: int, equal_value: T) -> PersistentOrderedSet[T]:
        """Move a stored representative to its final result index."""

        self._check_element_index(index)
        return self._move_existing(index, equal_value)

    def remove(self, equal_value: T) -> PersistentOrderedSet[T]:
        """Return a set without ``equal_value``'s class, closing the gap in the order.

        Removing an absent element returns the receiver.
        """

        return self.try_remove(equal_value).set

    def try_remove(self, equal_value: T) -> OrderedSetRemoveResult[T]:
        """Remove a class, returning the receiver on a miss."""

        removed = self._stamps.try_remove_entry(equal_value)
        if removed is None:
            return OrderedSetRemoveResult(self, False)
        index = self._index_of_stamp(removed.entry.value)
        order = self._order.remove_at(index)
        if order is None:
            raise AssertionError("A located ordered representative could not be removed.")
        return OrderedSetRemoveResult(self._wrap(order, removed.map), True)

    def remove_at(self, index: int) -> PersistentOrderedSet[T]:
        """Remove an ordered position, validating before policy callbacks."""

        self._check_element_index(index)
        ordered = self._order[index]
        removed = self._stamps.try_remove_entry(ordered.value)
        if removed is None or removed.entry.value != ordered.stamp:
            raise RuntimeError("The ordered set's membership and order indexes disagree.")
        order = self._order.remove_at(index)
        if order is None:
            raise AssertionError("Validated ordered removal failed.")
        return self._wrap(order, removed.map)

    def remove_first(self) -> PersistentOrderedSet[T]:
        """Return a set without its first element, raising :class:`IndexError` when empty."""

        if self.is_empty:
            raise IndexError("The ordered set is empty.")
        return self.remove_at(0)

    def remove_last(self) -> PersistentOrderedSet[T]:
        """Return a set without its last element, raising :class:`IndexError` when empty."""

        if self.is_empty:
            raise IndexError("The ordered set is empty.")
        return self.remove_at(self.size - 1)

    def clear(self) -> PersistentOrderedSet[T]:
        """Return a policy-preserving empty without invoking policy callbacks."""

        return self if self.is_empty else self.empty(self.policy)

    def get_range(self, index: int, count: int) -> PersistentOrderedSet[T]:
        """Extract a contiguous ordered range with overflow-safe validation."""

        self._check_range(index, count)
        if count == self.size:
            return self
        if count == 0:
            return self.empty(self.policy)

        split = self._order.split_range(index, count)
        if split is None:
            raise AssertionError("Validated ordered range split failed.")
        kept = split.range
        removed_count = self.size - count
        if count <= removed_count:
            stamps = self._build_index(kept, self.policy)
        else:
            stamps = self._stamps
            for entry in split.before:
                stamps = stamps.remove(entry.value)
            for entry in split.after:
                stamps = stamps.remove(entry.value)
        return PersistentOrderedSet(kept, stamps)

    def take(self, count: int) -> PersistentOrderedSet[T]:
        """Return the first ``count`` elements as a new set, preserving their order."""

        self._check_count(count)
        return self.get_range(0, count)

    def drop(self, count: int) -> PersistentOrderedSet[T]:
        """Return every element after the first ``count``, preserving their order."""

        self._check_count(count)
        return self.get_range(count, self.size - count)

    def reverse(self) -> PersistentOrderedSet[T]:
        """Reverse nontrivial order and rebuild the private index labels."""

        if self.size <= 1:
            return self
        return self._rebuild_entries(list(reversed(self._order.to_list())))

    def sort(
        self,
        comparator: Comparator[T] | None = None,
    ) -> PersistentOrderedSet[T]:
        """Stably reorder once without retaining the ordering comparator."""

        if self.size <= 1:
            return self
        effective = default_comparator if comparator is None else comparator
        if not callable(effective):
            raise TypeError("comparator must be callable or None.")

        entries = self._order.to_list()

        def compare(left: _Entry[T], right: _Entry[T]) -> int:
            """Order by the caller's comparator, breaking ties by existing position.

            Falling back to the order stamp is what makes the sort stable.
            """

            by_value = effective(left.value, right.value)
            if by_value != 0:
                return by_value
            return -1 if left.stamp < right.stamp else 1 if left.stamp > right.stamp else 0

        entries.sort(key=cmp_to_key(compare))
        unchanged = all(
            entries[index - 1].stamp < entries[index].stamp for index in range(1, len(entries))
        )
        return self if unchanged else self._rebuild_entries(entries)

    def union(self, other: Iterable[T]) -> PersistentOrderedSet[T]:
        """Append normalized argument-only representatives in argument order."""

        argument = self._normalize(other)
        result: list[T] | None = None
        for value in argument.items:
            if self._stamps.contains_key(value):
                continue
            if result is None:
                result = self.to_list()
            self._add_checked(result, value)
        return self if result is None else self._build_from_items(result, self.policy)

    def intersect(self, other: Iterable[T]) -> PersistentOrderedSet[T]:
        """Keep receiver representatives in receiver order."""

        argument = self._normalize(other)
        result = [
            entry.value for entry in self._order if argument.membership.contains_key(entry.value)
        ]
        return self if len(result) == self.size else self._build_from_items(result, self.policy)

    def except_(self, other: Iterable[T]) -> PersistentOrderedSet[T]:
        """Remove normalized argument classes while retaining receiver order."""

        argument = self._normalize(other)
        result: list[T] | None = None
        for position, entry in enumerate(self._order):
            if argument.membership.contains_key(entry.value):
                if result is None:
                    result = [prefix.value for prefix in self._take_entries(position)]
            elif result is not None:
                result.append(entry.value)
        return self if result is None else self._build_from_items(result, self.policy)

    def symmetric_except(self, other: Iterable[T]) -> PersistentOrderedSet[T]:
        """Return receiver-only values followed by normalized argument-only values."""

        argument = self._normalize(other)
        if not argument.items:
            return self
        result = [
            entry.value
            for entry in self._order
            if not argument.membership.contains_key(entry.value)
        ]
        for value in argument.items:
            if not self._stamps.contains_key(value):
                self._add_checked(result, value)
        return self._build_from_items(result, self.policy)

    def is_subset_of(self, other: Iterable[T]) -> bool:
        """Whether every element of this set also occurs in ``other``. ``other`` is normalized under
        this set's policy first, so a repeated element counts once. Order is irrelevant to every
        predicate in this group.
        """

        argument = self._normalize(other)
        return self.size <= len(argument.items) and all(
            argument.membership.contains_key(entry.value) for entry in self._order
        )

    def is_proper_subset_of(self, other: Iterable[T]) -> bool:
        """Whether this set is a subset of ``other`` and ``other`` has an element it lacks."""

        argument = self._normalize(other)
        return self.size < len(argument.items) and all(
            argument.membership.contains_key(entry.value) for entry in self._order
        )

    def is_superset_of(self, other: Iterable[T]) -> bool:
        """Whether every distinct element of ``other`` occurs in this set."""

        argument = self._normalize(other)
        return self.size >= len(argument.items) and all(
            self._stamps.contains_key(value) for value in argument.items
        )

    def is_proper_superset_of(self, other: Iterable[T]) -> bool:
        """Whether this set is a superset of ``other`` and holds an element ``other`` lacks."""

        argument = self._normalize(other)
        return self.size > len(argument.items) and all(
            self._stamps.contains_key(value) for value in argument.items
        )

    def overlaps(self, other: Iterable[T]) -> bool:
        """Whether this set and ``other`` share at least one element."""

        argument = self._normalize(other)
        return any(self._stamps.contains_key(value) for value in argument.items)

    def set_equals(self, other: Iterable[T]) -> bool:
        """Whether this set holds exactly ``other``'s distinct elements, disregarding order.

        Two sets built by inserting the same elements in different orders are equal here even though
        they iterate differently.
        """

        argument = self._normalize(other)
        return self.size == len(argument.items) and all(
            self._stamps.contains_key(value) for value in argument.items
        )

    def to_list(self) -> list[T]:
        """Materialize stored representatives in ordered-set order."""

        return [entry.value for entry in self._order]

    def __iter__(self) -> Iterator[T]:
        """Iterate the stored representatives in the set's order."""

        for entry in self._order:
            yield entry.value

    def _insert_core(self, index: int, value: T) -> PersistentOrderedSet[T]:
        stamp = self._pick_stamp(self._order, index)
        if stamp is not None:
            added = self._stamps.try_add(value, stamp)
            if not added.added:
                return self
            if self.size == _INT32_MAX:
                raise OverflowError("The operation would exceed the signed 32-bit set count.")
            entry = _Entry(stamp, value)
            if index == 0:
                order = self._order.prepend(entry)
            elif index == self.size:
                order = self._order.append(entry)
            else:
                inserted = self._order.insert_at(index, entry)
                if inserted is None:
                    raise AssertionError("Validated ordered insertion failed.")
                order = inserted
            return PersistentOrderedSet(order, added.value)

        duplicate_check = self._stamps.try_add(value, 0)
        if not duplicate_check.added:
            return self
        return self._rebuild_inserted(self._order, index, value)

    def _move_existing(self, final_index: int, equal_value: T) -> PersistentOrderedSet[T]:
        indexed = self._stamps.get_entry(equal_value)
        if indexed is None:
            raise KeyError(equal_value)
        old_index = self._index_of_stamp(indexed.value)
        if old_index == final_index:
            return self

        stored = self._order[old_index].value
        trimmed = self._order.remove_at(old_index)
        if trimmed is None:
            raise AssertionError("A located ordered representative could not be removed.")
        stamp = self._pick_stamp(trimmed, final_index)
        if stamp is None:
            return self._rebuild_inserted(trimmed, final_index, stored)

        entry = _Entry(stamp, stored)
        if final_index == 0:
            order = trimmed.prepend(entry)
        elif final_index == len(trimmed):
            order = trimmed.append(entry)
        else:
            inserted = trimmed.insert_at(final_index, entry)
            if inserted is None:
                raise AssertionError("Validated ordered movement failed.")
            order = inserted
        return PersistentOrderedSet(order, self._stamps.put(stored, stamp))

    def _index_of_stamp(self, stamp: int) -> int:
        low = 0
        high = self.size
        while low < high:
            middle = low + ((high - low) // 2)
            candidate = self._order[middle].stamp
            if candidate < stamp:
                low = middle + 1
            else:
                high = middle
        if low >= self.size or self._order[low].stamp != stamp:
            raise RuntimeError("The ordered set's membership and order indexes disagree.")
        return low

    @staticmethod
    def _pick_stamp(order: PersistentDeque[_Entry[T]], index: int) -> int | None:
        if order.is_empty:
            return 0
        if index == 0:
            first = cast("_Entry[T]", order.front()).stamp
            return None if first < _INT64_MIN + _STAMP_STRIDE else first - _STAMP_STRIDE
        if index == len(order):
            last = cast("_Entry[T]", order.back()).stamp
            return None if last > _INT64_MAX - _STAMP_STRIDE else last + _STAMP_STRIDE
        left = order[index - 1].stamp
        right = order[index].stamp
        gap = right - left
        return None if gap < 2 else left + (gap // 2)

    def _rebuild_inserted(
        self,
        order: PersistentDeque[_Entry[T]],
        index: int,
        value: T,
    ) -> PersistentOrderedSet[T]:
        if len(order) == _INT32_MAX:
            raise OverflowError("The operation would exceed the signed 32-bit set count.")
        entries = order.to_list()
        entries.insert(index, _Entry(0, value))
        return self._rebuild_entries(entries)

    def _rebuild_entries(self, entries: list[_Entry[T]]) -> PersistentOrderedSet[T]:
        if not entries:
            return self.empty(self.policy)
        values = [entry.value for entry in entries]
        return self._build_from_items(values, self.policy)

    @classmethod
    def _build_from_items(
        cls,
        items: Iterable[T],
        policy: HashPolicy[T],
    ) -> PersistentOrderedSet[T]:
        values = list(items)
        if not values:
            return cls.empty(policy)
        if len(values) > _INT32_MAX:
            raise OverflowError("The operation would exceed the signed 32-bit set count.")
        order_entries: list[_Entry[T]] = []
        pairs: list[tuple[T, int]] = []
        for index, value in enumerate(values):
            stamp = index * _STAMP_STRIDE
            if stamp > _INT64_MAX:
                raise OverflowError("The private order label exceeds signed 64-bit range.")
            order_entries.append(_Entry(stamp, value))
            pairs.append((value, stamp))
        stamps: PersistentHashMap[T, int] = PersistentHashMap.from_items(pairs, policy)
        return cls(PersistentDeque.from_iterable(order_entries), stamps)

    @staticmethod
    def _build_index(
        order: PersistentDeque[_Entry[T]],
        policy: HashPolicy[T],
    ) -> PersistentHashMap[T, int]:
        return PersistentHashMap.from_items(
            ((entry.value, entry.stamp) for entry in order),
            policy,
        )

    @classmethod
    def _wrap(
        cls,
        order: PersistentDeque[_Entry[T]],
        stamps: PersistentHashMap[T, int],
    ) -> PersistentOrderedSet[T]:
        return cls.empty(stamps.policy) if order.is_empty else cls(order, stamps)

    def _normalize(self, other: Iterable[T]) -> _NormalizedArgument[T]:
        if other is None:
            raise TypeError("ordered-set operations require an iterable, not None.")
        membership: PersistentHashMap[T, bool] = PersistentHashMap.empty(self.policy)
        items: list[T] = []
        for value in other:
            added = membership.try_add(value, True)
            if not added.added:
                continue
            membership = added.value
            self._add_checked(items, value)
        return _NormalizedArgument(tuple(items), membership)

    def _take_entries(self, count: int) -> Iterator[_Entry[T]]:
        for index, entry in enumerate(self._order):
            if index == count:
                break
            yield entry

    @staticmethod
    def _add_checked(target: list[T], value: T) -> None:
        if len(target) == _INT32_MAX:
            raise OverflowError("The operation would exceed the signed 32-bit set count.")
        target.append(value)

    def _check_element_index(self, index: int) -> None:
        if index < 0 or index >= self.size:
            raise IndexError(index)

    def _check_insertion_index(self, index: int) -> None:
        if index < 0 or index > self.size:
            raise IndexError(index)

    def _check_count(self, count: int) -> None:
        if count < 0 or count > self.size:
            raise ValueError("count must lie within 0 through the set size.")

    def _check_range(self, index: int, count: int) -> None:
        if index < 0 or index > self.size:
            raise IndexError(index)
        if count < 0 or count > self.size - index:
            raise ValueError("count extends past the end of the ordered set.")

    def _validate_invariants(self) -> None:
        """Recompute both independently owned indexes for white-box tests."""

        if len(self._order) != self._stamps.size:
            raise RuntimeError("The order and membership indexes have different counts.")
        previous: int | None = None
        for entry in self._order:
            if previous is not None and entry.stamp <= previous:
                raise RuntimeError("Order-maintenance stamps are not strictly ascending.")
            previous = entry.stamp
            indexed = self._stamps.get_entry(entry.value)
            if indexed is None or indexed.value != entry.stamp:
                raise RuntimeError("An ordered representative has no matching membership stamp.")
            if indexed.key is not entry.value:
                raise RuntimeError("The indexes retain different representative objects.")
        for indexed in self._stamps:
            position = self._index_of_stamp(indexed.value)
            ordered = self._order[position]
            if not self.policy.equivalent(indexed.key, ordered.value):
                raise RuntimeError("A membership entry has no matching ordered representative.")
            if indexed.key is not ordered.value:
                raise RuntimeError("The indexes retain different representative objects.")


_DEFAULT_EMPTY: PersistentOrderedSet[object] = PersistentOrderedSet(
    PersistentDeque.empty(),
    PersistentHashMap.empty(default_hash_policy()),
)


@dataclass(frozen=True, slots=True)
class PersistentOrderedSetCursor(Generic[T]):
    """Immutable root-plus-position gap cursor over a persistent ordered set."""

    set: PersistentOrderedSet[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..size``, so every cursor names a real gap."""

        if type(self.position) is not int or self.position < 0 or self.position > self.set.size:
            raise IndexError("Cursor position is outside the ordered set.")

    @property
    def size(self) -> int:
        """Element count of the set version this cursor is positioned in."""

        return self.set.size

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.size

    def peek_previous(self) -> OrderedSetCursorPeek[T]:
        """The element immediately before the gap, reported presence-safely.

        The result carries a found flag, so a stored ``None`` stays distinguishable from "nothing
        there".
        """

        return (
            OrderedSetCursorPeek(False)
            if self.is_at_start
            else OrderedSetCursorPeek(True, self.set.get_at(self.position - 1))
        )

    def peek_next(self) -> OrderedSetCursorPeek[T]:
        """The element immediately after the gap, reported presence-safely."""

        return (
            OrderedSetCursorPeek(False)
            if self.is_at_end
            else OrderedSetCursorPeek(True, self.set.get_at(self.position))
        )

    def move_previous(self) -> PersistentOrderedSetCursor[T]:
        """A cursor one position earlier, raising :class:`IndexError` at the start.

        The receiver is unchanged; movement produces a new cursor over the same set version.
        """

        if self.is_at_start:
            raise IndexError("The ordered-set cursor is already at the start.")
        return PersistentOrderedSetCursor(self.set, self.position - 1)

    def move_next(self) -> PersistentOrderedSetCursor[T]:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("The ordered-set cursor is already at the end.")
        return PersistentOrderedSetCursor(self.set, self.position + 1)

    def seek(self, position: int) -> PersistentOrderedSetCursor[T]:
        """A cursor at ``position`` within the same set version, raising when it is out of range."""

        return self if position == self.position else PersistentOrderedSetCursor(self.set, position)

    def insert(self, value: T) -> PersistentOrderedSetCursor[T]:
        """Insert ``value`` at the gap and return a cursor positioned after it.

        Because the set is insertion-ordered rather than sorted, the cursor decides where the
        element goes. An already present class is a no-op that returns the receiver, keeping the
        existing position and representative; use :meth:`try_insert` to tell the two outcomes apart.
        """

        updated = self.set.insert(self.position, value)
        return (
            self if updated is self.set else PersistentOrderedSetCursor(updated, self.position + 1)
        )

    def try_insert(self, value: T) -> tuple[bool, PersistentOrderedSetCursor[T]]:
        """Insert ``value``, returning whether it was added along with the resulting cursor."""

        cursor = self.insert(value)
        return cursor is not self, cursor

    def delete_previous(self) -> PersistentOrderedSetCursor[T]:
        """Remove the element before the gap, returning a cursor there; raises at the start."""

        if self.is_at_start:
            raise IndexError("The ordered-set cursor has no previous representative.")
        return PersistentOrderedSetCursor(self.set.remove_at(self.position - 1), self.position - 1)

    def delete_next(self) -> PersistentOrderedSetCursor[T]:
        """Remove the element after the gap and return a cursor in its place, raising at the end."""

        if self.is_at_end:
            raise IndexError("The ordered-set cursor has no next representative.")
        return PersistentOrderedSetCursor(self.set.remove_at(self.position), self.position)

    def snapshot(self) -> PersistentOrderedSet[T]:
        """Return the retained set version; the cursor stays usable and unconsumed."""

        return self.set


__all__ = [
    "OrderedSetCursorPeek",
    "OrderedSetCursorSearch",
    "OrderedSetRemoveResult",
    "OrderedSetValueResult",
    "PersistentOrderedSet",
    "PersistentOrderedSetCursor",
]
