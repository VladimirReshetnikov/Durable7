"""Payload-bearing persistent interval map over one measured sequence."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .measured_sequence import MeasuredSequence
from .measures import OptionalValue
from .ordering import Comparator, default_comparator
from .priority_interval import Interval

T = TypeVar("T")
V = TypeVar("V")

_INT32_MAX = (1 << 31) - 1


@dataclass(frozen=True, slots=True)
class IntervalMapEntry(Generic[T, V]):
    """One stored interval representative and payload."""

    interval: Interval[T]
    value: V


@dataclass(frozen=True, slots=True)
class IntervalMapLookup(Generic[T, V]):
    """Presence-safe exact-key lookup; the payload may itself be ``None``."""

    found: bool
    entry: IntervalMapEntry[T, V] | None = None


@dataclass(frozen=True, slots=True)
class IntervalMapAddResult(Generic[T, V]):
    """Nonthrowing strict insertion result."""

    added: bool
    map: PersistentIntervalMap[T, V]


@dataclass(frozen=True, slots=True)
class IntervalMapCursorPeek(Generic[T, V]):
    """An entry found next to a cursor gap. Wrapping it keeps a stored ``None`` payload distinct
    from "nothing there".
    """

    value: IntervalMapEntry[T, V]


@dataclass(frozen=True, slots=True)
class IntervalMapCursorSearch(Generic[T, V]):
    """The outcome of seeking a cursor. On a miss the cursor remains usable, positioned where the
    key would be inserted.
    """

    found: bool
    cursor: PersistentIntervalMapCursor[T, V]


class DuplicateIntervalError(ValueError):
    """Raised when strict insertion names an existing interval key."""


@dataclass(frozen=True, slots=True)
class _IntervalMapSummary(Generic[T]):
    last_interval: OptionalValue[Interval[T]]
    maximum_high: OptionalValue[T]


def _values_equal(left: V, right: V) -> bool:
    if left is right:
        return True
    try:
        result = left == right
    except (TypeError, ValueError):
        return False
    return result if isinstance(result, bool) else False


def _compare_intervals(left: Interval[T], right: Interval[T], comparator: Comparator[T]) -> int:
    low = comparator(left.low, right.low)
    return low if low != 0 else comparator(left.high, right.high)


class _IntervalMapMeasure(Generic[T, V]):
    def __init__(self, comparator: Comparator[T]) -> None:
        """Measure entries under ``comparator``, which fixes the key ordering."""

        self.comparator = comparator
        self.identity: _IntervalMapSummary[T] = _IntervalMapSummary(
            OptionalValue.none(), OptionalValue.none()
        )

    def measure(self, entry: IntervalMapEntry[T, V]) -> _IntervalMapSummary[T]:
        """Summarize one entry: its interval is both the running last key and the running maximum
        high endpoint.
        """

        return _IntervalMapSummary(
            OptionalValue.some(entry.interval), OptionalValue.some(entry.interval.high)
        )

    def combine(
        self, left: _IntervalMapSummary[T], right: _IntervalMapSummary[T]
    ) -> _IntervalMapSummary[T]:
        """Combine two subtree summaries: the right key wins, and the higher endpoint wins. Caching
        the maximum high endpoint is what lets an overlap query skip subtrees that cannot reach
        the probe.
        """

        if not left.maximum_high.has_value:
            maximum = right.maximum_high
        elif not right.maximum_high.has_value:
            maximum = left.maximum_high
        else:
            maximum = (
                left.maximum_high
                if self.comparator(left.maximum_high.unwrap(), right.maximum_high.unwrap()) >= 0
                else right.maximum_high
            )
        last = right.last_interval if right.last_interval.has_value else left.last_interval
        return _IntervalMapSummary(last, maximum)


class PersistentIntervalMap(Generic[T, V]):
    """Immutable map from validated closed intervals to payload values.

    Keys are unique and ordered lexicographically by ``(low, high)``. One measured sequence caches
    both the complete rightmost key and maximum high endpoint, supporting exact-key positioning and
    pruned overlap queries without a second index. Distinct overlapping intervals remain distinct.
    """

    __slots__ = ("_entries", "_measure", "comparator", "value_equals")

    def __init__(
        self,
        entries: MeasuredSequence[IntervalMapEntry[T, V], _IntervalMapSummary[T]],
        comparator: Comparator[T],
        value_equals: Callable[[V, V], bool],
        measure: _IntervalMapMeasure[T, V],
    ) -> None:
        """Wrap an already-built entry sequence; use :meth:`empty` or :meth:`from_items` instead."""

        self._entries = entries
        self.comparator = comparator
        self.value_equals = value_equals
        self._measure = measure

    @classmethod
    def empty(
        cls,
        comparator: Comparator[T] = default_comparator,
        value_equals: Callable[[V, V], bool] = _values_equal,
    ) -> PersistentIntervalMap[T, V]:
        """Return an empty map ordered by ``comparator``, comparing payloads with ``value_equals``.
        Both are retained by identity.
        """

        measure: _IntervalMapMeasure[T, V] = _IntervalMapMeasure(comparator)
        return cls(MeasuredSequence.empty(measure), comparator, value_equals, measure)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[Interval[T], V]],
        comparator: Comparator[T] = default_comparator,
        value_equals: Callable[[V, V], bool] = _values_equal,
    ) -> PersistentIntervalMap[T, V]:
        """Build with first key representatives and later distinct payloads winning."""

        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(comparator, value_equals)
        for interval, value in items:
            result = result.set(interval, value)
        return result

    def __len__(self) -> int:
        """Number of interval keys."""

        return len(self._entries)

    @property
    def size(self) -> int:
        """Number of interval keys."""

        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        """Whether the map holds no entries."""

        return self._entries.is_empty

    def __bool__(self) -> bool:
        """Whether the map holds at least one entry."""

        return not self.is_empty

    def get(self, interval: Interval[T]) -> V | None:
        """The payload filed under exactly this interval, or ``None`` when absent. Lookup is by
        whole-key identity, not overlap. Use :meth:`try_get_entry` when a stored ``None`` must
        stay distinct from absence.
        """

        lookup = self.try_get_entry(interval)
        return None if not lookup.found or lookup.entry is None else lookup.entry.value

    def try_get_entry(self, interval: Interval[T]) -> IntervalMapLookup[T, V]:
        """The stored entry for exactly this interval, reported presence-safely. Recovers the stored
        interval representative, which payload replacement retains.
        """

        self._require_valid(interval)
        index = self._lower_bound(interval)
        stored = self._entries.at(index)
        if stored is None or _compare_intervals(stored.interval, interval, self.comparator) != 0:
            return IntervalMapLookup(False)
        return IntervalMapLookup(True, stored)

    def __getitem__(self, interval: Interval[T]) -> V:
        """The payload filed under exactly this interval, raising :class:`KeyError` when absent."""

        lookup = self.try_get_entry(interval)
        if not lookup.found or lookup.entry is None:
            raise KeyError(interval)
        return lookup.entry.value

    def contains_key(self, interval: Interval[T]) -> bool:
        """Whether exactly this interval key is present."""

        return self.try_get_entry(interval).found

    def add(self, interval: Interval[T], value: V) -> PersistentIntervalMap[T, V]:
        """Add an entry, raising :class:`DuplicateIntervalError` when the interval is already a key.
        Two entries collide only when their endpoints are equal; a partial overlap is a distinct
        key.
        """

        result = self.try_add(interval, value)
        if not result.added:
            raise DuplicateIntervalError("An equivalent interval key is already present.")
        return result.map

    def try_add(self, interval: Interval[T], value: V) -> IntervalMapAddResult[T, V]:
        """Add an entry, reporting whether it was added rather than raising on a duplicate. Raises
        :class:`OverflowError` rather than exceeding the cross-port signed 32-bit entry count.
        """

        self._require_valid(interval)
        index = self._lower_bound(interval)
        stored = self._entries.at(index)
        if (
            stored is not None
            and _compare_intervals(stored.interval, interval, self.comparator) == 0
        ):
            return IntervalMapAddResult(False, self)
        if self.size == _INT32_MAX:
            raise OverflowError("The operation would exceed the signed 32-bit interval-map count.")
        entries = self._entries.insert_at(index, IntervalMapEntry(interval, value))
        if entries is None:
            raise AssertionError("Validated interval insertion failed.")
        return IntervalMapAddResult(True, self._wrap(entries))

    def set(self, interval: Interval[T], value: V) -> PersistentIntervalMap[T, V]:
        """Add or replace a payload while retaining the first interval representative."""

        self._require_valid(interval)
        index = self._lower_bound(interval)
        stored = self._entries.at(index)
        if stored is None or _compare_intervals(stored.interval, interval, self.comparator) != 0:
            return self.add(interval, value)
        if self.value_equals(stored.value, value):
            return self
        entries = self._entries.set_at(index, IntervalMapEntry(stored.interval, value))
        if entries is None:
            raise AssertionError("Validated interval replacement failed.")
        return self._wrap(entries)

    def remove(self, interval: Interval[T]) -> PersistentIntervalMap[T, V]:
        """Return a map without exactly this interval key; a no-op when it is absent."""

        self._require_valid(interval)
        index = self._lower_bound(interval)
        stored = self._entries.at(index)
        if stored is None or _compare_intervals(stored.interval, interval, self.comparator) != 0:
            return self
        entries = self._entries.remove_at(index)
        if entries is None:
            raise AssertionError("Validated interval removal failed.")
        return self._wrap(entries)

    def clear(self) -> PersistentIntervalMap[T, V]:
        """Return an empty map retaining the comparator and value comparison; a no-op when already
        empty.
        """

        return self if self.is_empty else self.empty(self.comparator, self.value_equals)

    def find_overlap(self, probe: Interval[T]) -> IntervalMapEntry[T, V] | None:
        """Some entry whose key overlaps ``probe``, or ``None`` when none does. The cached maximum
        high endpoint prunes subtrees that cannot reach ``probe``, so this does not scan the map.
        Which of several overlapping entries is returned is unspecified.
        """

        self._require_valid(probe)
        candidates = self._candidate_prefix(probe.high)
        located = candidates.locate(
            lambda summary: (
                summary.maximum_high.has_value
                and self.comparator(summary.maximum_high.unwrap(), probe.low) >= 0
            )
        )
        return located.value if located.found else None

    def find_containing(self, point: T) -> IntervalMapEntry[T, V] | None:
        """Some entry whose key contains ``point``, or ``None`` when none does."""

        candidates = self._candidate_prefix(point)
        located = candidates.locate(
            lambda summary: (
                summary.maximum_high.has_value
                and self.comparator(summary.maximum_high.unwrap(), point) >= 0
            )
        )
        return located.value if located.found else None

    def find_overlaps(self, probe: Interval[T]) -> list[IntervalMapEntry[T, V]]:
        """Return overlaps in lexicographic key order through repeated measured pruning."""

        self._require_valid(probe)
        result: list[IntervalMapEntry[T, V]] = []
        remaining = self._candidate_prefix(probe.high)
        while not remaining.is_empty:
            located = remaining.locate(
                lambda summary: (
                    summary.maximum_high.has_value
                    and self.comparator(summary.maximum_high.unwrap(), probe.low) >= 0
                )
            )
            if not located.found or located.value is None:
                break
            result.append(located.value)
            split = remaining.split_at(located.index + 1)
            if split is None:
                raise AssertionError("Located overlap split failed.")
            remaining = split.right
        return result

    def count_overlaps(self, probe: Interval[T]) -> int:
        """How many entries have keys overlapping ``probe``."""

        return len(self.find_overlaps(probe))

    def cursor_at(self, position: int = 0) -> PersistentIntervalMapCursor[T, V]:
        """A cursor at gap ``position`` of the key-ordered sequence."""

        return PersistentIntervalMapCursor(self, position)

    def cursor_at_lower_bound(self, interval: Interval[T]) -> PersistentIntervalMapCursor[T, V]:
        """A cursor before the first entry whose key is not below ``interval``."""

        self._require_valid(interval)
        return self.cursor_at(self._lower_bound(interval))

    def cursor_at_upper_bound(self, interval: Interval[T]) -> PersistentIntervalMapCursor[T, V]:
        """A cursor after any entry whose key equals ``interval``."""

        cursor = self.cursor_at_lower_bound(interval)
        candidate = cursor.peek_next()
        return (
            cursor.move_next()
            if candidate is not None
            and _compare_intervals(candidate.value.interval, interval, self.comparator) == 0
            else cursor
        )

    def find_cursor(self, interval: Interval[T]) -> IntervalMapCursorSearch[T, V]:
        """Seek to exactly this interval key and report whether it is present. On a miss the cursor
        sits where the key would be inserted.
        """

        cursor = self.cursor_at_lower_bound(interval)
        candidate = cursor.peek_next()
        return IntervalMapCursorSearch(
            candidate is not None
            and _compare_intervals(candidate.value.interval, interval, self.comparator) == 0,
            cursor,
        )

    def find_overlap_cursor(self, probe: Interval[T]) -> IntervalMapCursorSearch[T, V]:
        """Seek to the first entry whose key overlaps ``probe`` and report whether one exists.
        Advancing with :meth:`PersistentIntervalMapCursor.seek_next_overlap` streams the
        remaining matches instead of collecting them all.
        """

        return self._find_overlap_cursor_from(0, probe)

    def find_containing_cursor(self, point: T) -> IntervalMapCursorSearch[T, V]:
        """Seek to the first entry whose key contains ``point`` and report whether one exists."""

        return self.find_overlap_cursor(Interval(point, point, self.comparator))

    def _find_overlap_cursor_from(
        self, start: int, probe: Interval[T]
    ) -> IntervalMapCursorSearch[T, V]:
        self._require_valid(probe)
        if start < 0 or start > len(self):
            raise IndexError("Cursor start is outside the interval map.")
        for position, entry in enumerate(tuple(self)[start:], start):
            if self.comparator(entry.interval.low, probe.high) > 0:
                break
            if entry.interval.overlaps(probe, self.comparator):
                return IntervalMapCursorSearch(True, self.cursor_at(position))
        return IntervalMapCursorSearch(False, self.cursor_at(len(self)))

    def keys(self) -> Iterator[Interval[T]]:
        """Iterate the interval keys in ascending order."""

        for entry in self._entries:
            yield entry.interval

    def values(self) -> Iterator[V]:
        """Iterate the payloads in their keys' ascending order."""

        for entry in self._entries:
            yield entry.value

    def __iter__(self) -> Iterator[IntervalMapEntry[T, V]]:
        """Iterate the entries in ascending key order."""

        return iter(self._entries)

    def shares_storage_with(self, other: PersistentIntervalMap[T, V]) -> bool:
        """Whether both maps share any node of the entry sequence. Used to confirm that a no-op
        avoided copying, not an equality test.
        """

        return self._entries.shares_structure_with(other._entries)

    def validate_structure(self) -> bool:
        """Recompute strict key order and both cached annotations."""

        previous: Interval[T] | None = None
        maximum: OptionalValue[T] = OptionalValue.none()
        for entry in self._entries:
            if self.comparator(entry.interval.low, entry.interval.high) > 0:
                return False
            if (
                previous is not None
                and _compare_intervals(previous, entry.interval, self.comparator) >= 0
            ):
                return False
            previous = entry.interval
            if not maximum.has_value or self.comparator(entry.interval.high, maximum.unwrap()) > 0:
                maximum = OptionalValue.some(entry.interval.high)

        summary = self._entries.measure
        if summary.last_interval.has_value != (previous is not None):
            return False
        if previous is not None and summary.last_interval.unwrap() is not previous:
            return False
        if summary.maximum_high.has_value != maximum.has_value:
            return False
        return (
            not maximum.has_value
            or self.comparator(summary.maximum_high.unwrap(), maximum.unwrap()) == 0
        )

    def _require_valid(self, interval: Interval[T]) -> None:
        if interval is None:
            raise TypeError("interval must not be None.")
        if self.comparator(interval.low, interval.high) > 0:
            raise ValueError("Interval low endpoint must not exceed high endpoint.")

    def _lower_bound(self, interval: Interval[T]) -> int:
        return self._entries.locate(
            lambda summary: (
                summary.last_interval.has_value
                and _compare_intervals(summary.last_interval.unwrap(), interval, self.comparator)
                >= 0
            )
        ).index

    def _candidate_prefix(
        self, high: T
    ) -> MeasuredSequence[IntervalMapEntry[T, V], _IntervalMapSummary[T]]:
        end = self._entries.locate(
            lambda summary: (
                summary.last_interval.has_value
                and self.comparator(summary.last_interval.unwrap().low, high) > 0
            )
        ).index
        split = self._entries.split_at(end)
        if split is None:
            raise AssertionError("Computed interval prefix split failed.")
        return split.left

    def _wrap(
        self,
        entries: MeasuredSequence[IntervalMapEntry[T, V], _IntervalMapSummary[T]],
    ) -> PersistentIntervalMap[T, V]:
        return PersistentIntervalMap(entries, self.comparator, self.value_equals, self._measure)


@dataclass(frozen=True, slots=True)
class PersistentIntervalMapCursor(Generic[T, V]):
    """Immutable interval-key-order root-plus-rank cursor."""

    map: PersistentIntervalMap[T, V]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self.map):
            raise IndexError("Cursor position is outside the interval map.")

    @property
    def count(self) -> int:
        """Entry count of the map version this cursor is positioned in."""

        return len(self.map)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first entry."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last entry."""

        return self.position == self.count

    def peek_previous(self) -> IntervalMapCursorPeek[T, V] | None:
        """The entry immediately before the gap, or ``None`` at the start."""

        return (
            None if self.is_at_start else IntervalMapCursorPeek(tuple(self.map)[self.position - 1])
        )

    def peek_next(self) -> IntervalMapCursorPeek[T, V] | None:
        """The entry immediately after the gap, or ``None`` at the end."""

        return None if self.is_at_end else IntervalMapCursorPeek(tuple(self.map)[self.position])

    def move_previous(self) -> PersistentIntervalMapCursor[T, V]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same map version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PersistentIntervalMapCursor(self.map, self.position - 1)

    def move_next(self) -> PersistentIntervalMapCursor[T, V]:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PersistentIntervalMapCursor(self.map, self.position + 1)

    def seek_rank(self, position: int) -> PersistentIntervalMapCursor[T, V]:
        """A cursor at ``position`` within the same map version, raising when out of range."""

        return (
            self if position == self.position else PersistentIntervalMapCursor(self.map, position)
        )

    def seek_next_overlap(self, probe: Interval[T]) -> IntervalMapCursorSearch[T, V]:
        """Advance to the next entry after the gap whose key overlaps ``probe``, reporting whether
        one was found. Repeated calls stream the matches one at a time.
        """

        start = self.position + 1 if self.position < self.count else self.count
        return self.map._find_overlap_cursor_from(start, probe)

    def insert(self, interval: Interval[T], value: V) -> PersistentIntervalMapCursor[T, V]:
        """Add an entry and return a cursor just after it in key order. Raises
        :class:`DuplicateIntervalError` on an existing interval key. The gap moves to the key's
        ordered position, since placement is decided by the endpoints.
        """

        position = self.map._lower_bound(interval)
        return PersistentIntervalMapCursor(self.map.add(interval, value), position + 1)

    def try_insert(self, interval: Interval[T], value: V) -> IntervalMapCursorSearch[T, V]:
        """Add an entry, reporting whether it was added. On a duplicate the returned cursor focuses
        the retained entry and the map is unchanged.
        """

        position = self.map._lower_bound(interval)
        result = self.map.try_add(interval, value)
        cursor = (
            PersistentIntervalMapCursor(result.map, position + 1)
            if result.added
            else PersistentIntervalMapCursor(self.map, position)
        )
        return IntervalMapCursorSearch(result.added, cursor)

    def set_next_value(self, value: V) -> PersistentIntervalMapCursor[T, V]:
        """Replace the payload of the entry after the gap, keeping its interval key and position.
        Raises :class:`IndexError` at the end.
        """

        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return PersistentIntervalMapCursor(self.map.set(entry.value.interval, value), self.position)

    def delete_previous(self) -> PersistentIntervalMapCursor[T, V]:
        """Remove the entry before the gap, returning a cursor there; raises at the start."""

        entry = self.peek_previous()
        if entry is None:
            raise IndexError("No entry precedes the cursor.")
        return PersistentIntervalMapCursor(self.map.remove(entry.value.interval), self.position - 1)

    def delete_next(self) -> PersistentIntervalMapCursor[T, V]:
        """Remove the entry after the gap and return a cursor in its place, raising at the end."""

        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return PersistentIntervalMapCursor(self.map.remove(entry.value.interval), self.position)

    def snapshot(self) -> PersistentIntervalMap[T, V]:
        """The map version this cursor is positioned in."""

        return self.map


__all__ = [
    "DuplicateIntervalError",
    "IntervalMapAddResult",
    "IntervalMapCursorPeek",
    "IntervalMapCursorSearch",
    "IntervalMapEntry",
    "IntervalMapLookup",
    "PersistentIntervalMap",
    "PersistentIntervalMapCursor",
]
