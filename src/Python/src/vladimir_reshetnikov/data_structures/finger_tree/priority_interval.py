"""Measured stable priority queue and max-high interval tree."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .measured_sequence import MeasuredSequence
from .measures import OptionalValue
from .ordering import Comparator, default_comparator

T = TypeVar("T")
P = TypeVar("P")


@dataclass(frozen=True, slots=True)
class PriorityEntry(Generic[T, P]):
    value: T
    priority: P


@dataclass(frozen=True, slots=True)
class PriorityDequeue(Generic[T, P]):
    entry: PriorityEntry[T, P]
    queue: PriorityQueue[T, P]


@dataclass(frozen=True, slots=True)
class _PrioritySummary(Generic[T, P]):
    best: PriorityEntry[T, P] | None


class _PriorityMeasure(Generic[T, P]):
    identity: _PrioritySummary[T, P] = _PrioritySummary(None)

    def __init__(self, comparator: Comparator[P]) -> None:
        self.comparator = comparator

    def measure(self, element: PriorityEntry[T, P]) -> _PrioritySummary[T, P]:
        return _PrioritySummary(element)

    def combine(
        self, left: _PrioritySummary[T, P], right: _PrioritySummary[T, P]
    ) -> _PrioritySummary[T, P]:
        if left.best is None:
            return right
        if right.best is None:
            return left
        return left if self.comparator(left.best.priority, right.best.priority) <= 0 else right


class PriorityQueue(Generic[T, P]):
    """Stable immutable priority queue with a cached constant-time minimum."""

    __slots__ = ("_entries", "_measure", "comparator")

    def __init__(
        self,
        entries: MeasuredSequence[PriorityEntry[T, P], _PrioritySummary[T, P]],
        comparator: Comparator[P],
        measure: _PriorityMeasure[T, P],
    ) -> None:
        self._entries = entries
        self.comparator = comparator
        self._measure = measure

    @classmethod
    def empty(cls, comparator: Comparator[P] = default_comparator) -> PriorityQueue[T, P]:
        measure: _PriorityMeasure[T, P] = _PriorityMeasure(comparator)
        return cls(MeasuredSequence.empty(measure), comparator, measure)

    def __len__(self) -> int:
        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        return self._entries.is_empty

    def enqueue(self, value: T, priority: P) -> PriorityQueue[T, P]:
        return PriorityQueue(
            self._entries.append(PriorityEntry(value, priority)), self.comparator, self._measure
        )

    def meld(self, other: PriorityQueue[T, P]) -> PriorityQueue[T, P]:
        if self.comparator is not other.comparator:
            raise TypeError("Cannot meld queues with different comparator objects.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        entries = MeasuredSequence.from_iterable((*self, *other), self._measure)
        return PriorityQueue(entries, self.comparator, self._measure)

    def peek_entry(self) -> PriorityEntry[T, P] | None:
        return self._entries.measure.best

    def peek(self) -> tuple[T, P] | None:
        entry = self.peek_entry()
        return None if entry is None else (entry.value, entry.priority)

    def peek_priority(self) -> P | None:
        entry = self.peek_entry()
        return None if entry is None else entry.priority

    def dequeue(self) -> PriorityDequeue[T, P] | None:
        entry = self.peek_entry()
        if entry is None:
            return None
        located = self._entries.locate(lambda summary: summary.best is entry)
        if not located.found:
            raise AssertionError("Cached priority summary did not identify an entry.")
        entries = self._entries.remove_at(located.index)
        if entries is None:
            raise AssertionError("Located priority removal failed.")
        return PriorityDequeue(entry, PriorityQueue(entries, self.comparator, self._measure))

    def to_list(self) -> list[PriorityEntry[T, P]]:
        return self._entries.to_list()

    def shares_storage_with(self, other: PriorityQueue[T, P]) -> bool:
        return self._entries.shares_structure_with(other._entries)

    def __iter__(self) -> Iterator[PriorityEntry[T, P]]:
        return iter(self._entries)


@dataclass(frozen=True, slots=True)
class Interval(Generic[T]):
    """Closed interval."""

    low: T
    high: T

    def __init__(self, low: T, high: T, comparator: Comparator[T] = default_comparator) -> None:
        if comparator(low, high) > 0:
            raise ValueError("Interval low endpoint must not exceed high endpoint.")
        object.__setattr__(self, "low", low)
        object.__setattr__(self, "high", high)

    def overlaps(self, other: Interval[T], comparator: Comparator[T] = default_comparator) -> bool:
        return comparator(self.low, other.high) <= 0 and comparator(other.low, self.high) <= 0

    def contains_point(self, point: T, comparator: Comparator[T] = default_comparator) -> bool:
        return comparator(self.low, point) <= 0 and comparator(point, self.high) <= 0


@dataclass(frozen=True, slots=True)
class IntervalRemoveResult(Generic[T]):
    tree: IntervalTree[T]
    interval: Interval[T]


@dataclass(frozen=True, slots=True)
class IntervalCursorPeek(Generic[T]):
    value: Interval[T]


@dataclass(frozen=True, slots=True)
class IntervalCursorSearch(Generic[T]):
    found: bool
    cursor: IntervalTreeCursor[T]


@dataclass(frozen=True, slots=True)
class _IntervalSummary(Generic[T]):
    maximum_high: OptionalValue[T]
    last_low: OptionalValue[T]


class _IntervalMeasure(Generic[T]):
    def __init__(self, comparator: Comparator[T]) -> None:
        self.comparator = comparator
        absent: OptionalValue[T] = OptionalValue.none()
        self.identity = _IntervalSummary(absent, absent)

    def measure(self, element: Interval[T]) -> _IntervalSummary[T]:
        return _IntervalSummary(OptionalValue.some(element.high), OptionalValue.some(element.low))

    def combine(self, left: _IntervalSummary[T], right: _IntervalSummary[T]) -> _IntervalSummary[T]:
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
        return _IntervalSummary(
            maximum, right.last_low if right.last_low.has_value else left.last_low
        )


class IntervalTree(Generic[T]):
    """Immutable max-high annotated interval tree ordered by low endpoint."""

    __slots__ = ("_intervals", "_measure", "comparator")

    def __init__(
        self,
        intervals: MeasuredSequence[Interval[T], _IntervalSummary[T]],
        comparator: Comparator[T],
        measure: _IntervalMeasure[T],
    ) -> None:
        self._intervals = intervals
        self.comparator = comparator
        self._measure = measure

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> IntervalTree[T]:
        measure: _IntervalMeasure[T] = _IntervalMeasure(comparator)
        return cls(MeasuredSequence.empty(measure), comparator, measure)

    @classmethod
    def from_iterable(
        cls, values: Iterable[Interval[T]], comparator: Comparator[T] = default_comparator
    ) -> IntervalTree[T]:
        result = cls.empty(comparator)
        for value in values:
            result = result.insert(value)
        return result

    def __len__(self) -> int:
        return len(self._intervals)

    @property
    def is_empty(self) -> bool:
        return self._intervals.is_empty

    def _lower_bound(self, low: T) -> int:
        return self._intervals.locate(
            lambda summary: (
                summary.last_low.has_value and self.comparator(summary.last_low.unwrap(), low) >= 0
            )
        ).index

    def _index_of(self, interval: Interval[T]) -> int:
        index = self._lower_bound(interval.low)
        while index < len(self):
            current = self._intervals.at(index)
            if current is None:
                raise AssertionError("Valid interval index was absent.")
            if self.comparator(current.low, interval.low) != 0:
                return -1
            if self.comparator(current.high, interval.high) == 0:
                return index
            index += 1
        return -1

    def insert(self, interval: Interval[T]) -> IntervalTree[T]:
        if self.comparator(interval.low, interval.high) > 0:
            raise ValueError("Interval low endpoint must not exceed high endpoint.")
        intervals = self._intervals.insert_at(self._lower_bound(interval.low), interval)
        if intervals is None:
            raise AssertionError("Validated interval insertion failed.")
        return IntervalTree(intervals, self.comparator, self._measure)

    def contains(self, interval: Interval[T]) -> bool:
        return self._index_of(interval) >= 0

    def remove(self, interval: Interval[T]) -> IntervalTree[T]:
        result = self.try_remove(interval)
        return self if result is None else result.tree

    def try_remove(self, interval: Interval[T]) -> IntervalRemoveResult[T] | None:
        index = self._index_of(interval)
        if index < 0:
            return None
        removed = self._intervals.at(index)
        intervals = self._intervals.remove_at(index)
        if removed is None or intervals is None:
            raise AssertionError("Validated interval removal failed.")
        return IntervalRemoveResult(
            IntervalTree(intervals, self.comparator, self._measure), removed
        )

    def _next_overlap(
        self,
        probe: Interval[T],
        source: MeasuredSequence[Interval[T], _IntervalSummary[T]],
    ) -> tuple[Interval[T], MeasuredSequence[Interval[T], _IntervalSummary[T]]] | None:
        located = source.locate(
            lambda summary: (
                summary.maximum_high.has_value
                and self.comparator(summary.maximum_high.unwrap(), probe.low) >= 0
            )
        )
        value = located.value
        if not located.found or value is None or self.comparator(value.low, probe.high) > 0:
            return None
        split = source.split_at(located.index + 1)
        if split is None:
            raise AssertionError("Located overlap split failed.")
        return value, split.right

    def find_overlap(self, probe: Interval[T]) -> Interval[T] | None:
        result = self._next_overlap(probe, self._intervals)
        return None if result is None else result[0]

    def find_containing(self, point: T) -> Interval[T] | None:
        return self.find_overlap(Interval(point, point, self.comparator))

    def find_overlaps(self, probe: Interval[T]) -> list[Interval[T]]:
        result: list[Interval[T]] = []
        remaining = self._intervals
        while True:
            next_overlap = self._next_overlap(probe, remaining)
            if next_overlap is None:
                return result
            result.append(next_overlap[0])
            remaining = next_overlap[1]

    def count_overlaps(self, probe: Interval[T]) -> int:
        return len(self.find_overlaps(probe))

    def cursor_at(self, position: int = 0) -> IntervalTreeCursor[T]:
        return IntervalTreeCursor(self, position)

    def cursor_at_lower_bound(self, low: T) -> IntervalTreeCursor[T]:
        return self.cursor_at(self._lower_bound(low))

    def cursor_at_upper_bound(self, low: T) -> IntervalTreeCursor[T]:
        position = self._lower_bound(low)
        values = self.to_list()
        while position < len(values) and self.comparator(values[position].low, low) == 0:
            position += 1
        return self.cursor_at(position)

    def find_cursor(self, interval: Interval[T]) -> IntervalCursorSearch[T]:
        position = self._lower_bound(interval.low)
        values = self.to_list()
        while position < len(values) and self.comparator(values[position].low, interval.low) == 0:
            if self.comparator(values[position].high, interval.high) == 0:
                return IntervalCursorSearch(True, self.cursor_at(position))
            position += 1
        return IntervalCursorSearch(False, self.cursor_at(self._lower_bound(interval.low)))

    def find_overlap_cursor(self, probe: Interval[T]) -> IntervalCursorSearch[T]:
        return self._find_overlap_cursor_from(0, probe)

    def find_containing_cursor(self, point: T) -> IntervalCursorSearch[T]:
        return self.find_overlap_cursor(Interval(point, point, self.comparator))

    def _find_overlap_cursor_from(self, start: int, probe: Interval[T]) -> IntervalCursorSearch[T]:
        if start < 0 or start > len(self):
            raise IndexError("Cursor start is outside the interval tree.")
        for position, interval in enumerate(self.to_list()[start:], start):
            if self.comparator(interval.low, probe.high) > 0:
                break
            if interval.overlaps(probe, self.comparator):
                return IntervalCursorSearch(True, self.cursor_at(position))
        return IntervalCursorSearch(False, self.cursor_at(len(self)))

    def coalesce(self) -> IntervalTree[T]:
        if len(self) < 2:
            return self
        result: list[Interval[T]] = []
        for next_interval in self:
            current = result[-1] if result else None
            if current is not None and self.comparator(next_interval.low, current.high) <= 0:
                high = (
                    current.high
                    if self.comparator(current.high, next_interval.high) >= 0
                    else next_interval.high
                )
                result[-1] = Interval(current.low, high, self.comparator)
            else:
                result.append(next_interval)
        return IntervalTree.from_iterable(result, self.comparator)

    def to_list(self) -> list[Interval[T]]:
        return self._intervals.to_list()

    def shares_storage_with(self, other: IntervalTree[T]) -> bool:
        return self._intervals.shares_structure_with(other._intervals)

    def __iter__(self) -> Iterator[Interval[T]]:
        return iter(self._intervals)


@dataclass(frozen=True, slots=True)
class IntervalTreeCursor(Generic[T]):
    """Immutable low-endpoint-order root-plus-rank cursor."""

    tree: IntervalTree[T]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > len(self.tree):
            raise IndexError("Cursor position is outside the interval tree.")

    @property
    def count(self) -> int:
        return len(self.tree)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> IntervalCursorPeek[T] | None:
        return (
            None if self.is_at_start else IntervalCursorPeek(self.tree.to_list()[self.position - 1])
        )

    def peek_next(self) -> IntervalCursorPeek[T] | None:
        return None if self.is_at_end else IntervalCursorPeek(self.tree.to_list()[self.position])

    def move_previous(self) -> IntervalTreeCursor[T]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return IntervalTreeCursor(self.tree, self.position - 1)

    def move_next(self) -> IntervalTreeCursor[T]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return IntervalTreeCursor(self.tree, self.position + 1)

    def seek_rank(self, position: int) -> IntervalTreeCursor[T]:
        return self if position == self.position else IntervalTreeCursor(self.tree, position)

    def seek_next_overlap(self, probe: Interval[T]) -> IntervalCursorSearch[T]:
        start = self.position + 1 if self.position < self.count else self.count
        return self.tree._find_overlap_cursor_from(start, probe)

    def insert(self, interval: Interval[T]) -> IntervalTreeCursor[T]:
        position = self.tree._lower_bound(interval.low)
        return IntervalTreeCursor(self.tree.insert(interval), position + 1)

    def _delete_at(self, rank: int, position: int) -> IntervalTreeCursor[T]:
        intervals = self.tree._intervals.remove_at(rank)
        if intervals is None:
            raise AssertionError("Validated interval cursor removal failed.")
        return IntervalTreeCursor(
            IntervalTree(intervals, self.tree.comparator, self.tree._measure), position
        )

    def delete_previous(self) -> IntervalTreeCursor[T]:
        if self.is_at_start:
            raise IndexError("No occurrence precedes the cursor.")
        return self._delete_at(self.position - 1, self.position - 1)

    def delete_next(self) -> IntervalTreeCursor[T]:
        if self.is_at_end:
            raise IndexError("No occurrence follows the cursor.")
        return self._delete_at(self.position, self.position)

    def snapshot(self) -> IntervalTree[T]:
        return self.tree


__all__ = [
    "Interval",
    "IntervalCursorPeek",
    "IntervalCursorSearch",
    "IntervalRemoveResult",
    "IntervalTree",
    "IntervalTreeCursor",
    "PriorityDequeue",
    "PriorityEntry",
    "PriorityQueue",
]
