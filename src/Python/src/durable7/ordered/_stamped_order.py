"""Stamp-measured insertion-order sequence shared by the ordered collections.

The ordered set, map, and multimap keep their entries twice: a CHAMP index from key to an
``int64`` *stamp*, and this sequence of stamped entries in insertion order, where stamps rise
strictly from front to back. Turning a stamp back into an ordinal position is therefore a search
over a sorted-by-stamp sequence, and how that search is performed decides the family's keyed
positional bounds.

The previous representation was a size-measured deque, which cannot descend by stamp; positions
were recovered by a binary search whose every probe was itself an :math:`O(\\log n)` indexed
descent, costing :math:`O(\\log^2 n)` per keyed position lookup. This sequence instead caches the
maximum stamp of every subtree as its measure - the same representation the reference workspaces
use - so :meth:`StampedOrder.try_index_of_stamp` is one measure-directed descent:
:math:`O(\\log n)` worst case.

The deque-shaped remainder of the API mirrors :class:`~durable7.finger_tree.PersistentDeque`
where the ordered collections already used it, including ``None`` for out-of-range mutations, so
the collections' unwrap patterns are unchanged.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, Protocol, TypeVar, cast

from ..finger_tree.measured_sequence import MeasuredSequence
from ..finger_tree.measures import MeasurePolicy


class Stamped(Protocol):
    """An order entry carrying the strictly increasing stamp that positions it."""

    @property
    def stamp(self) -> int: ...


E = TypeVar("E", bound=Stamped)

#: Strictly below every representable stamp; the modules clamp stamps to signed 64-bit.
_BELOW_EVERY_STAMP = -(1 << 63) - 1


class StampMeasure:
    """Maximum-stamp monoid; stamps rise strictly left to right, so a subtree's max is its last."""

    identity = _BELOW_EVERY_STAMP

    def combine(self, left: int, right: int) -> int:
        """Return the larger stamp; associative, with the below-everything identity."""

        return left if left >= right else right

    def measure(self, element: Stamped) -> int:
        """Measure an entry by its stamp."""

        return element.stamp


_STAMP_MEASURE = StampMeasure()


@dataclass(frozen=True, slots=True)
class StampedOrderSplit(Generic[E]):
    """The elements before a range, the range itself, and the rest, in order."""

    before: StampedOrder[E]
    range: StampedOrder[E]
    after: StampedOrder[E]


class StampedOrder(Generic[E]):
    """Insertion-order sequence whose cached measure is the subtree maximum stamp."""

    __slots__ = ("_items",)

    def __init__(self, items: MeasuredSequence[E, int]) -> None:
        """Wrap an already-built sequence; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._items = items

    @classmethod
    def empty(cls, policy: MeasurePolicy[E, int] = _STAMP_MEASURE) -> StampedOrder[E]:
        """Return an empty order sequence.

        ``policy`` exists so a test can observe descents through a counting policy; production
        callers always take the shared default.
        """

        return cls(MeasuredSequence.empty(policy))

    @classmethod
    def from_iterable(
        cls, values: Iterable[E], policy: MeasurePolicy[E, int] = _STAMP_MEASURE
    ) -> StampedOrder[E]:
        """Build the sequence in one balanced pass, not by repeated appending."""

        return cls(MeasuredSequence.from_iterable(values, policy))

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the sequence holds no entries."""

        return self._items.is_empty

    def front(self) -> E | None:
        """The first entry, or ``None`` when empty. O(log n)."""

        return self._items.front()

    def back(self) -> E | None:
        """The last entry, or ``None`` when empty. O(log n)."""

        return self._items.back()

    def __getitem__(self, index: int) -> E:
        """The entry at ``index``, raising :class:`IndexError` when out of range."""

        value = self._items.at(index)
        if value is None:
            raise IndexError(index)
        return value

    def __iter__(self) -> Iterator[E]:
        return iter(self._items)

    def to_list(self) -> list[E]:
        """Copy the entries into a list, in insertion order."""

        return self._items.to_list()

    def try_index_of_stamp(self, stamp: int) -> int | None:
        """The ordinal position of the entry carrying ``stamp``, or ``None`` when absent.

        One measure-directed descent - O(log n) worst case. Because stamps rise strictly along the
        sequence, "first prefix whose maximum stamp reaches ``stamp``" is monotone, and the located
        entry either carries exactly ``stamp`` or ``stamp`` is not present at all.
        """

        found = self._items.locate(lambda measured: measured >= stamp)
        if not found.found:
            return None
        entry = cast("E", found.value)
        return found.index if entry.stamp == stamp else None

    def append(self, value: E) -> StampedOrder[E]:
        """Return a sequence with ``value`` appended."""

        return StampedOrder(self._items.append(value))

    def prepend(self, value: E) -> StampedOrder[E]:
        """Return a sequence with ``value`` prepended."""

        return StampedOrder(self._items.prepend(value))

    def insert_at(self, index: int, value: E) -> StampedOrder[E] | None:
        """Return a sequence with ``value`` inserted at ``index``, or ``None`` out of range."""

        items = self._items.insert_at(index, value)
        return None if items is None else StampedOrder(items)

    def set_item(self, index: int, value: E) -> StampedOrder[E] | None:
        """Return a sequence with the entry at ``index`` replaced, or ``None`` out of range."""

        items = self._items.set_at(index, value)
        if items is None:
            return None
        return self if items is self._items else StampedOrder(items)

    def remove_at(self, index: int) -> StampedOrder[E] | None:
        """Return a sequence without the entry at ``index``, or ``None`` out of range."""

        items = self._items.remove_at(index)
        return None if items is None else StampedOrder(items)

    def split_range(self, start: int, count: int) -> StampedOrderSplit[E] | None:
        """Split into before-``start``, the ``count`` entries from it, and the rest.

        ``None`` when the range falls outside the sequence, mirroring the deque this replaced.
        """

        if start < 0 or count < 0 or start + count > len(self):
            return None
        first = self._items.split_at(start)
        if first is None:
            raise AssertionError("Validated ordered split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated ordered range split failed.")
        return StampedOrderSplit(
            StampedOrder(first.left), StampedOrder(second.left), StampedOrder(second.right)
        )

    def shares_storage_with(self, other: StampedOrder[E]) -> bool:
        """Whether both sequences share any node by object identity."""

        return self._items.shares_structure_with(other._items)


__all__ = ["StampMeasure", "Stamped", "StampedOrder", "StampedOrderSplit"]
