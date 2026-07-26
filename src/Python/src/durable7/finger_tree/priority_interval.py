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
    """One queued value together with the priority it is ordered by."""

    value: T
    priority: P


@dataclass(frozen=True, slots=True)
class PriorityDequeue(Generic[T, P]):
    """The entry removed by a dequeue, together with the queue that remains."""

    entry: PriorityEntry[T, P]
    queue: PriorityQueue[T, P]


@dataclass(frozen=True, slots=True)
class _PrioritySummary(Generic[T, P]):
    best: PriorityEntry[T, P] | None


class _PriorityMeasure(Generic[T, P]):
    identity: _PrioritySummary[T, P] = _PrioritySummary(None)

    def __init__(self, comparator: Comparator[P]) -> None:
        """Order priorities by ``comparator``."""

        self.comparator = comparator

    def measure(self, element: PriorityEntry[T, P]) -> _PrioritySummary[T, P]:
        """An entry summarizes as itself, its own best candidate."""

        return _PrioritySummary(element)

    def combine(
        self, left: _PrioritySummary[T, P], right: _PrioritySummary[T, P]
    ) -> _PrioritySummary[T, P]:
        """Keep the better of two subtree candidates, preferring the left on a tie. Preferring the
        left is what makes the queue stable: among equal priorities, the earliest enqueued is
        served first.
        """

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
        """Wrap an already-built entry sequence; use :meth:`empty` instead."""

        self._entries = entries
        self.comparator = comparator
        self._measure = measure

    @classmethod
    def empty(cls, comparator: Comparator[P] = default_comparator) -> PriorityQueue[T, P]:
        """Return an empty queue ordered by ``comparator``, which is retained by identity. Only
        queues sharing that exact object may be melded.
        """

        measure: _PriorityMeasure[T, P] = _PriorityMeasure(comparator)
        return cls(MeasuredSequence.empty(measure), comparator, measure)

    def __len__(self) -> int:
        """Number of queued entries."""

        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        """Whether the queue holds no entries."""

        return self._entries.is_empty

    def enqueue(self, value: T, priority: P) -> PriorityQueue[T, P]:
        """Return a queue with ``value`` added at ``priority``. Appended to the sequence, so equal
        priorities keep enqueue order.
        """

        return PriorityQueue(
            self._entries.append(PriorityEntry(value, priority)), self.comparator, self._measure
        )

    def meld(self, other: PriorityQueue[T, P]) -> PriorityQueue[T, P]:
        """Return a queue holding every entry of both. Entries from this queue precede ``other``'s,
        so the stable ordering carries over. Both queues must retain the same comparator object.
        """

        if self.comparator is not other.comparator:
            raise TypeError("Cannot meld queues with different comparator objects.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        entries = MeasuredSequence.from_iterable((*self, *other), self._measure)
        return PriorityQueue(entries, self.comparator, self._measure)

    def peek_entry(self) -> PriorityEntry[T, P] | None:
        """The entry with the smallest priority, or ``None`` when empty. Read from the sequence's
        cached measure, so no search is needed.
        """

        return self._entries.measure.best

    def peek(self) -> tuple[T, P] | None:
        """The smallest-priority value and its priority, or ``None`` when empty."""

        entry = self.peek_entry()
        return None if entry is None else (entry.value, entry.priority)

    def peek_priority(self) -> P | None:
        """The smallest queued priority, or ``None`` when empty."""

        entry = self.peek_entry()
        return None if entry is None else entry.priority

    def dequeue(self) -> PriorityDequeue[T, P] | None:
        """Remove the smallest-priority entry, returning it with the remaining queue. ``None`` when
        the queue is empty. The entry is located by a measure-directed descent rather than a
        scan.
        """

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
        """Copy the entries into a list in enqueue order, not priority order."""

        return self._entries.to_list()

    def shares_storage_with(self, other: PriorityQueue[T, P]) -> bool:
        """Whether both queues share any node of the entry sequence. A representation test, not an
        equality test.
        """

        return self._entries.shares_structure_with(other._entries)

    def __iter__(self) -> Iterator[PriorityEntry[T, P]]:
        """Iterate the entries in enqueue order, not priority order."""

        return iter(self._entries)


@dataclass(frozen=True, slots=True)
class Interval(Generic[T]):
    """Closed interval."""

    low: T
    high: T

    def __init__(self, low: T, high: T, comparator: Comparator[T] = default_comparator) -> None:
        """Build the closed interval ``[low, high]``, raising when the endpoints are inverted. An
        inverted interval has no contents, so it is rejected here rather than allowed to produce
        silently empty query results.
        """

        if comparator(low, high) > 0:
            raise ValueError("Interval low endpoint must not exceed high endpoint.")
        object.__setattr__(self, "low", low)
        object.__setattr__(self, "high", high)

    def overlaps(self, other: Interval[T], comparator: Comparator[T] = default_comparator) -> bool:
        """Whether the two closed intervals share at least one point. Touching endpoints count as
        overlapping.
        """

        return comparator(self.low, other.high) <= 0 and comparator(other.low, self.high) <= 0

    def contains_point(self, point: T, comparator: Comparator[T] = default_comparator) -> bool:
        """Whether ``point`` lies within this closed interval, endpoints included."""

        return comparator(self.low, point) <= 0 and comparator(point, self.high) <= 0


@dataclass(frozen=True, slots=True)
class IntervalRemoveResult(Generic[T]):
    """The tree remaining after a removal, together with the interval that was removed."""

    tree: IntervalTree[T]
    interval: Interval[T]


@dataclass(frozen=True, slots=True)
class IntervalCursorPeek(Generic[T]):
    """An interval found next to a cursor gap, wrapped so absence stays unambiguous."""

    value: Interval[T]


@dataclass(frozen=True, slots=True)
class IntervalCursorSearch(Generic[T]):
    """The outcome of seeking a cursor. On a miss the cursor remains usable."""

    found: bool
    cursor: IntervalTreeCursor[T]


@dataclass(frozen=True, slots=True)
class _IntervalSummary(Generic[T]):
    maximum_high: OptionalValue[T]
    last_low: OptionalValue[T]


class _IntervalMeasure(Generic[T]):
    def __init__(self, comparator: Comparator[T]) -> None:
        """Order endpoints by ``comparator``."""

        self.comparator = comparator
        absent: OptionalValue[T] = OptionalValue.none()
        self.identity = _IntervalSummary(absent, absent)

    def measure(self, element: Interval[T]) -> _IntervalSummary[T]:
        """An interval summarizes as its own low endpoint and its own high endpoint."""

        return _IntervalSummary(OptionalValue.some(element.high), OptionalValue.some(element.low))

    def combine(self, left: _IntervalSummary[T], right: _IntervalSummary[T]) -> _IntervalSummary[T]:
        """Combine two subtree summaries: the right low endpoint wins, and the greater high endpoint
        wins. Caching the maximum high endpoint is what lets an overlap query prune subtrees that
        cannot reach the probe.
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
        """Wrap an already-built interval sequence; use :meth:`empty` or :meth:`from_iterable`."""

        self._intervals = intervals
        self.comparator = comparator
        self._measure = measure

    @classmethod
    def empty(cls, comparator: Comparator[T] = default_comparator) -> IntervalTree[T]:
        """Return an empty tree ordered by ``comparator``."""

        measure: _IntervalMeasure[T] = _IntervalMeasure(comparator)
        return cls(MeasuredSequence.empty(measure), comparator, measure)

    @classmethod
    def from_iterable(
        cls, values: Iterable[Interval[T]], comparator: Comparator[T] = default_comparator
    ) -> IntervalTree[T]:
        """Build a tree from ``intervals``."""

        result = cls.empty(comparator)
        for value in values:
            result = result.insert(value)
        return result

    def __len__(self) -> int:
        """Number of stored intervals, counting duplicates separately."""

        return len(self._intervals)

    @property
    def is_empty(self) -> bool:
        """Whether the tree stores no intervals."""

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
        """Return a tree with ``interval`` added, keeping ascending low-endpoint order. Duplicates
        are permitted: this is a bag of intervals, not a set. Raises on an inverted interval.
        """

        if self.comparator(interval.low, interval.high) > 0:
            raise ValueError("Interval low endpoint must not exceed high endpoint.")
        intervals = self._intervals.insert_at(self._lower_bound(interval.low), interval)
        if intervals is None:
            raise AssertionError("Validated interval insertion failed.")
        return IntervalTree(intervals, self.comparator, self._measure)

    def contains(self, interval: Interval[T]) -> bool:
        """Whether an interval with exactly these endpoints is stored. Endpoint identity, not
        overlap; see :meth:`find_overlap` for the latter.
        """

        return self._index_of(interval) >= 0

    def remove(self, interval: Interval[T]) -> IntervalTree[T]:
        """Return a tree with one occurrence of ``interval`` removed; a no-op when absent."""

        result = self.try_remove(interval)
        return self if result is None else result.tree

    def try_remove(self, interval: Interval[T]) -> IntervalRemoveResult[T] | None:
        """Remove one occurrence of ``interval``, reporting the stored interval, or ``None`` when
        absent.
        """

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
        """Some stored interval overlapping ``probe``, or ``None`` when none does. The cached
        maximum high endpoint prunes subtrees that cannot reach ``probe``. Which of several
        overlapping intervals is returned is unspecified.
        """

        result = self._next_overlap(probe, self._intervals)
        return None if result is None else result[0]

    def find_containing(self, point: T) -> Interval[T] | None:
        """Some stored interval containing ``point``, or ``None`` when none does."""

        return self.find_overlap(Interval(point, point, self.comparator))

    def find_overlaps(self, probe: Interval[T]) -> list[Interval[T]]:
        """Every stored interval overlapping ``probe``, in ascending order."""

        result: list[Interval[T]] = []
        remaining = self._intervals
        while True:
            next_overlap = self._next_overlap(probe, remaining)
            if next_overlap is None:
                return result
            result.append(next_overlap[0])
            remaining = next_overlap[1]

    def count_overlaps(self, probe: Interval[T]) -> int:
        """How many stored intervals overlap ``probe``."""

        return len(self.find_overlaps(probe))

    def cursor_at(self, position: int = 0) -> IntervalTreeCursor[T]:
        """A cursor at gap ``position`` of the ascending interval sequence."""

        return IntervalTreeCursor(self, position)

    def cursor_at_lower_bound(self, low: T) -> IntervalTreeCursor[T]:
        """A cursor before the first interval whose low endpoint is not below ``low``."""

        return self.cursor_at(self._lower_bound(low))

    def cursor_at_upper_bound(self, low: T) -> IntervalTreeCursor[T]:
        """A cursor after every interval whose low endpoint equals ``low``."""

        position = self._lower_bound(low)
        values = self.to_list()
        while position < len(values) and self.comparator(values[position].low, low) == 0:
            position += 1
        return self.cursor_at(position)

    def find_cursor(self, interval: Interval[T]) -> IntervalCursorSearch[T]:
        """Seek to an interval with exactly these endpoints and report whether it is stored."""

        position = self._lower_bound(interval.low)
        values = self.to_list()
        while position < len(values) and self.comparator(values[position].low, interval.low) == 0:
            if self.comparator(values[position].high, interval.high) == 0:
                return IntervalCursorSearch(True, self.cursor_at(position))
            position += 1
        return IntervalCursorSearch(False, self.cursor_at(self._lower_bound(interval.low)))

    def find_overlap_cursor(self, probe: Interval[T]) -> IntervalCursorSearch[T]:
        """Seek to the first interval overlapping ``probe`` and report whether one exists. Advancing
        with :meth:`IntervalTreeCursor.seek_next_overlap` streams the remaining matches instead
        of collecting them all.
        """

        return self._find_overlap_cursor_from(0, probe)

    def find_containing_cursor(self, point: T) -> IntervalCursorSearch[T]:
        """Seek to the first interval containing ``point`` and report whether one exists."""

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
        """Return a tree in which overlapping and touching intervals have been merged. The result
        covers exactly the same points using the fewest intervals, so duplicates and partial
        overlaps disappear.
        """

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
        """Copy the stored intervals into a list, in ascending order."""

        return self._intervals.to_list()

    def shares_storage_with(self, other: IntervalTree[T]) -> bool:
        """Whether both trees share any node of the interval sequence. A representation test, not an
        equality test.
        """

        return self._intervals.shares_structure_with(other._intervals)

    def __iter__(self) -> Iterator[Interval[T]]:
        """Iterate the stored intervals in ascending order."""

        return iter(self._intervals)


@dataclass(frozen=True, slots=True)
class IntervalTreeCursor(Generic[T]):
    """Immutable low-endpoint-order root-plus-rank cursor."""

    tree: IntervalTree[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self.tree):
            raise IndexError("Cursor position is outside the interval tree.")

    @property
    def count(self) -> int:
        """Interval count of the tree version this cursor is positioned in."""

        return len(self.tree)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first interval."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last interval."""

        return self.position == self.count

    def peek_previous(self) -> IntervalCursorPeek[T] | None:
        """The interval immediately before the gap, or ``None`` at the start."""

        return (
            None if self.is_at_start else IntervalCursorPeek(self.tree.to_list()[self.position - 1])
        )

    def peek_next(self) -> IntervalCursorPeek[T] | None:
        """The interval immediately after the gap, or ``None`` at the end."""

        return None if self.is_at_end else IntervalCursorPeek(self.tree.to_list()[self.position])

    def move_previous(self) -> IntervalTreeCursor[T]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same tree version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return IntervalTreeCursor(self.tree, self.position - 1)

    def move_next(self) -> IntervalTreeCursor[T]:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return IntervalTreeCursor(self.tree, self.position + 1)

    def seek_rank(self, position: int) -> IntervalTreeCursor[T]:
        """A cursor at ``position`` within the same tree version, raising when out of range."""

        return self if position == self.position else IntervalTreeCursor(self.tree, position)

    def seek_next_overlap(self, probe: Interval[T]) -> IntervalCursorSearch[T]:
        """Advance to the next interval after the gap that overlaps ``probe``, reporting whether one
        was found. Repeated calls stream the matches one at a time.
        """

        start = self.position + 1 if self.position < self.count else self.count
        return self.tree._find_overlap_cursor_from(start, probe)

    def insert(self, interval: Interval[T]) -> IntervalTreeCursor[T]:
        """Insert ``interval`` and return a cursor just after it. The gap moves to the interval's
        sorted position, since placement is decided by the endpoints rather than by the cursor.
        """

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
        """Remove the interval before the gap and return a cursor in its place, raising at the
        start.
        """

        if self.is_at_start:
            raise IndexError("No occurrence precedes the cursor.")
        return self._delete_at(self.position - 1, self.position - 1)

    def delete_next(self) -> IntervalTreeCursor[T]:
        """Remove the interval after the gap and return a cursor in its place, raising at the end.
        """

        if self.is_at_end:
            raise IndexError("No occurrence follows the cursor.")
        return self._delete_at(self.position, self.position)

    def snapshot(self) -> IntervalTree[T]:
        """The tree version this cursor is positioned in."""

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
