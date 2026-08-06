"""Mutable six-cursor DABA Lite FIFO monoid aggregation."""

from __future__ import annotations

import sys
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .measures import Monoid

T = TypeVar("T")

_CHUNK_CAPACITY = 64
_EMPTY = object()


class _Block(Generic[T]):
    __slots__ = ("items", "next", "previous")

    def __init__(self) -> None:
        """Allocate one fixed-capacity chunk of empty slots, unlinked from any neighbor."""

        self.items: list[T | object] = [_EMPTY] * _CHUNK_CAPACITY
        self.previous: _Block[T] | None = None
        self.next: _Block[T] | None = None


@dataclass(frozen=True, slots=True)
class _Cursor(Generic[T]):
    block: _Block[T]
    index: int


def _same_cursor(left: _Cursor[T], right: _Cursor[T]) -> bool:
    return left.block is right.block and left.index == right.index


class _ChunkedQueue(Generic[T]):
    __slots__ = ("_first",)

    def __init__(self) -> None:
        """Start a queue holding one empty chunk."""

        self._first = _Block[T]()

    @property
    def begin(self) -> _Cursor[T]:
        """Cursor at the first slot of the first chunk."""

        return _Cursor(self._first, 0)

    @property
    def first_block(self) -> _Block[T]:
        """The frontmost chunk, exposed for structural validation."""

        return self._first

    @staticmethod
    def read(cursor: _Cursor[T]) -> T:
        """Return the value at ``cursor``, which must name an occupied slot.

        Raises :class:`AssertionError` on an empty slot, since reading one would mean the
        algorithm's cursors had drifted out of the live window.
        """

        value = cursor.block.items[cursor.index]
        if value is _EMPTY:
            raise AssertionError("DABA Lite attempted to read an empty active slot.")
        return cast(T, value)

    @staticmethod
    def read_raw(cursor: _Cursor[T]) -> T | object:
        """Return the slot's contents, which may be the empty marker rather than a stored value.

        Used by insertion to save a slot before overwriting it, so a failure can restore it.
        """

        return cursor.block.items[cursor.index]

    @staticmethod
    def write(cursor: _Cursor[T], value: T | object) -> None:
        """Store ``value``, or the empty marker, at ``cursor``."""

        cursor.block.items[cursor.index] = value

    def reset(self) -> _Cursor[T]:
        """Discard every chunk, install a fresh one, and return a cursor to its first slot."""

        self._first = _Block()
        return self.begin

    @staticmethod
    def advance(cursor: _Cursor[T]) -> _Cursor[T]:
        """Return the next cursor within the existing chunks, never allocating.

        Raises :class:`AssertionError` past the final chunk; use :meth:`advance_end` when the queue
        may need to grow.
        """

        if cursor.index + 1 < _CHUNK_CAPACITY:
            return _Cursor(cursor.block, cursor.index + 1)
        if cursor.block.next is None:
            raise AssertionError("Cannot advance beyond the DABA Lite end chunk.")
        return _Cursor(cursor.block.next, 0)

    @staticmethod
    def advance_end(cursor: _Cursor[T]) -> _Cursor[T]:
        """Return the next cursor, appending a chunk when the current one is exhausted.

        Growth is one chunk at a time, which is what keeps insertion worst-case constant rather than
        amortized.
        """

        if cursor.index + 1 < _CHUNK_CAPACITY:
            return _Cursor(cursor.block, cursor.index + 1)
        next_block = cursor.block.next
        if next_block is None:
            next_block = _Block()
            next_block.previous = cursor.block
            cursor.block.next = next_block
        return _Cursor(next_block, 0)

    @staticmethod
    def retreat(cursor: _Cursor[T]) -> _Cursor[T]:
        """Return the previous cursor. Raises :class:`AssertionError` before the front chunk."""

        if cursor.index != 0:
            return _Cursor(cursor.block, cursor.index - 1)
        if cursor.block.previous is None:
            raise AssertionError("Cannot retreat before the DABA Lite front chunk.")
        return _Cursor(cursor.block.previous, _CHUNK_CAPACITY - 1)

    @staticmethod
    def restore_successor(block: _Block[T], successor: _Block[T] | None) -> None:
        """Relink ``block`` to ``successor``, undoing a growth step.

        Used to roll insertion back when a monoid callback raises after a chunk was appended.
        """

        current = block.next
        if current is successor:
            return
        block.next = successor
        if successor is not None:
            successor.previous = block
        if current is not None:
            current.previous = None

    def trim_before(self, front: _Cursor[T]) -> None:
        """Drop the chunks entirely behind ``front``, so evicted slots stop retaining values."""

        if self._first is front.block:
            return
        predecessor = front.block.previous
        self._first = front.block
        self._first.previous = None
        if predecessor is not None:
            predecessor.next = None


@dataclass(frozen=True, slots=True)
class DabaLiteStatistics:
    """Cursor positions and chunk occupancy returned by a structural audit.

    The four interior lengths describe how far the incremental reversal has progressed, which is
    what the worst-case bounds are stated in terms of.
    """

    count: int
    front_length: int
    back_length: int
    left_length: int
    right_length: int
    accumulator_length: int
    block_count: int
    allocated_slot_capacity: int
    slack_slot_count: int


class DabaLite(Generic[T]):
    """Mutable FIFO monoid aggregator with worst-case bounded callback work.

    Insert performs at most three combines, eviction at most two, and aggregate lookup at most one.
    Mutation computes every fallible callback result before publishing cursor or slot changes.
    """

    __slots__ = (
        "_a",
        "_aggregate_b",
        "_aggregate_ra",
        "_b",
        "_count",
        "_e",
        "_f",
        "_l",
        "_monoid",
        "_queue",
        "_r",
    )

    def __init__(self, monoid: Monoid[T]) -> None:
        """Create an empty window aggregated by ``monoid``.

        The monoid must be associative with its identity as a two-sided identity; it need not be
        commutative or invertible, and the algorithm never assumes it is.
        """

        self._monoid = monoid
        self._queue = _ChunkedQueue[T]()
        begin = self._queue.begin
        self._f = begin
        self._l = begin
        self._r = begin
        self._a = begin
        self._b = begin
        self._e = begin
        self._aggregate_ra = monoid.identity
        self._aggregate_b = monoid.identity
        self._count = 0

    def __len__(self) -> int:
        """Number of values currently in the window."""

        return self._count

    @property
    def count(self) -> int:
        """Number of values currently in the window."""

        return self._count

    @property
    def is_empty(self) -> bool:
        """Whether the window holds no values."""

        return self._count == 0

    @property
    def aggregate(self) -> T:
        """The combination of every value in the window, in FIFO order.

        At most one combine, because the incremental reversal keeps both halves summarized. An empty
        window aggregates to the monoid identity.
        """

        if self._count == 0:
            return self._monoid.identity
        return self._monoid.combine(self._queue.read(self._f), self._aggregate_b)

    def insert(self, value: T) -> None:
        """Append ``value`` to the back of the window.

        At most three combines, and one bounded step of the incremental reversal. The combine
        happens before any structural change, so a monoid that raises leaves the window exactly as
        it was.
        """

        if self._count == sys.maxsize:
            raise OverflowError("The DABA Lite window is too large.")
        old_end = self._e
        old_count = self._count
        old_aggregate_b = self._aggregate_b
        old_value = self._queue.read_raw(old_end)
        old_successor = old_end.block.next
        # This first combine is deliberately before any structural mutation.
        next_aggregate_b = self._monoid.combine(old_aggregate_b, value)
        try:
            new_end = self._queue.advance_end(old_end)
            self._queue.write(old_end, value)
            self._e = new_end
            self._count = old_count + 1
            self._aggregate_b = next_aggregate_b
            self._fixup()
        except BaseException:
            self._queue.write(old_end, old_value)
            self._queue.restore_successor(old_end.block, old_successor)
            self._e = old_end
            self._count = old_count
            self._aggregate_b = old_aggregate_b
            raise

    def evict(self) -> None:
        """Remove the value at the front of the window.

        Raises :class:`IndexError` when the window is empty; use :meth:`try_evict` to test and
        remove at once.
        """

        if not self.try_evict():
            raise IndexError("Cannot evict from an empty DABA Lite window.")

    def try_evict(self) -> bool:
        """Remove the front value, reporting whether there was one to remove.

        At most two combines. A monoid that raises restores the front cursor and count, so the
        window is unchanged.
        """

        if self._count == 0:
            return False
        old_front = self._f
        old_count = self._count
        self._f = self._queue.advance(old_front)
        self._count -= 1
        try:
            self._fixup()
        except BaseException:
            self._f = old_front
            self._count = old_count
            raise
        self._queue.write(old_front, _EMPTY)
        self._queue.trim_before(self._f)
        return True

    def clear(self) -> None:
        """Discard every value and return to the empty window.

        The identity is read before anything is discarded, so a monoid that raises cannot leave the
        window half-cleared.
        """

        if self._count == 0:
            return
        # Acquire identity first so a fallible policy cannot partially clear the queue.
        identity = self._monoid.identity
        begin = self._queue.reset()
        self._f = begin
        self._l = begin
        self._r = begin
        self._a = begin
        self._b = begin
        self._e = begin
        self._aggregate_ra = identity
        self._aggregate_b = identity
        self._count = 0

    def validate_structure(self) -> DabaLiteStatistics:
        """Walk the chunk list and the six cursors, returning the measurements they imply.

        Checks that the cursors are in range, ordered as the algorithm requires, and reachable from
        the first chunk, and that the chunk list is acyclic and correctly linked in both directions.
        Raises :class:`ValueError` on the first violation. A defensive audit, not part of normal
        use.
        """

        cursors = (self._f, self._l, self._r, self._a, self._b, self._e)
        for name, cursor in zip(("F", "L", "R", "A", "B", "E"), cursors, strict=True):
            if cursor.index < 0 or cursor.index >= _CHUNK_CAPACITY:
                raise ValueError(f"The DABA Lite {name} cursor has an invalid index.")
        first = self._queue.first_block
        if first.previous is not None or first is not self._f.block:
            raise ValueError("Invalid DABA Lite first chunk.")
        positions = [-1] * 6
        seen: set[int] = set()
        previous: _Block[T] | None = None
        block = first
        block_base = 0
        block_count = 0
        while True:
            identity = id(block)
            if identity in seen:
                raise ValueError("The DABA Lite chunk chain contains a cycle.")
            seen.add(identity)
            if block.previous is not previous or (
                previous is not None and previous.next is not block
            ):
                raise ValueError("Invalid DABA Lite chunk links.")
            for index, cursor in enumerate(cursors):
                if cursor.block is block:
                    positions[index] = block_base + cursor.index
            block_count += 1
            if block is self._e.block:
                if block.next is not None:
                    raise ValueError("End cursor is not in the last chunk.")
                break
            previous = block
            if block.next is None:
                raise ValueError("End cursor is unreachable.")
            block = block.next
            block_base += _CHUNK_CAPACITY
        if any(position < 0 for position in positions):
            raise ValueError("A cursor lies outside the active chain.")
        f, left, right, accumulator, back, end = positions
        if not (f <= left <= right <= accumulator <= back <= end):
            raise ValueError("DABA Lite cursor ordering is invalid.")
        if end - f != self._count:
            raise ValueError("DABA Lite count disagrees with cursor distance.")
        front_length = back - f
        back_length = end - back
        left_length = right - left
        right_length = accumulator - right
        accumulator_length = back - accumulator
        if self._count == 0:
            if not f == left == right == accumulator == back == end:
                raise ValueError("Empty DABA Lite cursors differ.")
        else:
            if left_length != right_length:
                raise ValueError("DABA Lite work-region sizes differ.")
            if left_length + right_length + accumulator_length + 1 != front_length - back_length:
                raise ValueError("DABA Lite size equation is invalid.")
            if left - f != back_length + 1:
                raise ValueError("DABA Lite processed-prefix equation is invalid.")
        expected_blocks = (self._f.index + self._count) // _CHUNK_CAPACITY + 1
        if block_count != expected_blocks:
            raise ValueError("DABA Lite block count is invalid.")
        allocated_slot_capacity = block_count * _CHUNK_CAPACITY
        slack_slot_count = allocated_slot_capacity - self._count
        if slack_slot_count < 1 or slack_slot_count >= 2 * _CHUNK_CAPACITY:
            raise ValueError("DABA Lite slack exceeds its bound.")
        return DabaLiteStatistics(
            self._count,
            front_length,
            back_length,
            left_length,
            right_length,
            accumulator_length,
            block_count,
            allocated_slot_capacity,
            slack_slot_count,
        )

    def _fixup(self) -> None:
        next_left = self._l
        next_right = self._r
        next_accumulator = self._a
        next_back = self._b
        next_aggregate_ra = self._aggregate_ra
        next_aggregate_b = self._aggregate_b
        cached_identity: T | None = None
        has_identity = False
        if _same_cursor(self._f, next_back):
            identity = self._monoid.identity
            self._publish(self._e, self._e, self._e, self._e, identity, identity)
            return
        if _same_cursor(next_left, next_back):
            cached_identity = self._monoid.identity
            has_identity = True
            next_left = self._f
            next_accumulator = self._e
            next_back = self._e
            next_aggregate_ra = next_aggregate_b
            next_aggregate_b = cached_identity
        if _same_cursor(next_left, next_right):
            transition_identity: T = (
                cast(T, cached_identity) if has_identity else self._monoid.identity
            )
            self._publish(
                self._queue.advance(next_left),
                self._queue.advance(next_right),
                self._queue.advance(next_accumulator),
                next_back,
                transition_identity,
                next_aggregate_b,
            )
            return
        advanced_left = self._queue.advance(next_left)
        before_accumulator = self._queue.retreat(next_accumulator)
        # Both callback results are computed before either slot is overwritten.
        new_left_value = self._monoid.combine(self._queue.read(next_left), next_aggregate_ra)
        product_accumulator = (
            self._monoid.identity
            if _same_cursor(next_accumulator, next_back)
            else self._queue.read(next_accumulator)
        )
        new_accumulator_value = self._monoid.combine(
            self._queue.read(before_accumulator), product_accumulator
        )
        self._queue.write(next_left, new_left_value)
        self._queue.write(before_accumulator, new_accumulator_value)
        self._publish(
            advanced_left,
            next_right,
            before_accumulator,
            next_back,
            next_aggregate_ra,
            next_aggregate_b,
        )

    def _publish(
        self,
        left: _Cursor[T],
        right: _Cursor[T],
        accumulator: _Cursor[T],
        back: _Cursor[T],
        aggregate_ra: T,
        aggregate_b: T,
    ) -> None:
        self._l = left
        self._r = right
        self._a = accumulator
        self._b = back
        self._aggregate_ra = aggregate_ra
        self._aggregate_b = aggregate_b


__all__ = ["DabaLite", "DabaLiteStatistics"]
