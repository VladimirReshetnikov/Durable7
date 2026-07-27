"""Persistent deque, reversible deque, and measured-tree facades."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator, Sequence
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .measured_sequence import MeasuredSequence
from .measures import MeasurePolicy, SizeMeasure

T = TypeVar("T")
M = TypeVar("M")

_SHARED_SIZE_MEASURE: SizeMeasure[object] = SizeMeasure()


def _size_policy() -> SizeMeasure[T]:
    return cast(SizeMeasure[T], _SHARED_SIZE_MEASURE)


@dataclass(frozen=True, slots=True)
class DequeSplit(Generic[T]):
    """The two deques produced by a positional split; both share structure with the original."""

    left: PersistentDeque[T]
    right: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class DequeItemSplit(Generic[T]):
    """The pieces produced by splitting around one element: what precedes it, the element, and what
    follows it.
    """

    left: PersistentDeque[T]
    item: T
    right: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class DequeRangeSplit(Generic[T]):
    """The three deques produced by splitting out a range: before it, the range itself, and after
    it.
    """

    before: PersistentDeque[T]
    range: PersistentDeque[T]
    after: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class DequePop(Generic[T]):
    """An endpoint element together with the deque that remains after removing it."""

    value: T
    rest: PersistentDeque[T]


@dataclass(frozen=True, slots=True)
class MeasuredSplit(Generic[T, M]):
    """The two trees produced by a split; both carry their own recomputed measure."""

    left: FingerTree[T, M]
    right: FingerTree[T, M]


@dataclass(frozen=True, slots=True)
class MeasuredItemSplit(Generic[T, M]):
    """The pieces produced by splitting around one element, with the element itself."""

    left: FingerTree[T, M]
    item: T
    right: FingerTree[T, M]


@dataclass(frozen=True, slots=True)
class SequenceCursorPeek(Generic[T]):
    """Present neighbor returned by a sequence cursor, including a stored ``None``."""

    value: T


class PersistentDeque(Generic[T]):
    """Persistent catenable sequence over a measured balanced tree."""

    __slots__ = ("_items",)

    def __init__(self, items: MeasuredSequence[T, int]) -> None:
        """Wrap an already-built representation; use :meth:`empty` or :meth:`from_iterable` instead.
        """

        self._items = items

    @classmethod
    def empty(cls) -> PersistentDeque[T]:
        """Return the empty deque."""

        return cls(MeasuredSequence.empty(_size_policy()))

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> PersistentDeque[T]:
        """Build a deque from ``values`` in one balanced pass."""

        return cls(MeasuredSequence.from_iterable(values, _size_policy()))

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the deque holds no elements."""

        return self._items.is_empty

    def front(self) -> T | None:
        """The first element, or ``None`` when empty."""

        return self._items.front()

    def back(self) -> T | None:
        """The last element, or ``None`` when empty."""

        return self._items.back()

    def get(self, index: int) -> T | None:
        """The element at ``index``, or ``None`` when out of range. Descends by cached subtree
        counts rather than walking.
        """

        return self._items.at(index)

    def __getitem__(self, index: int) -> T:
        """The element at ``index``, raising :class:`IndexError` when out of range."""

        value = self.get(index)
        if index < 0 or index >= len(self):
            raise IndexError(index)
        return cast(T, value)

    def prepend(self, value: T) -> PersistentDeque[T]:
        """Return a deque with ``value`` added at the front."""

        return PersistentDeque(self._items.prepend(value))

    def append(self, value: T) -> PersistentDeque[T]:
        """Return a deque with ``value`` added at the back."""

        return PersistentDeque(self._items.append(value))

    def concat(self, other: PersistentDeque[T]) -> PersistentDeque[T]:
        """Return this deque's elements followed by ``other``'s. Joins the two trees rather than
        copying either, so the cost follows their height difference and not their sizes.
        """

        if other.is_empty:
            return self
        if self.is_empty:
            return other
        return PersistentDeque(self._items.concat(other._items))

    def split_at(self, index: int) -> DequeSplit[T] | None:
        """Split into the elements before ``index`` and those from ``index`` on, or ``None`` when
        ``index`` falls outside ``0..len``. Both halves share structure with the receiver.
        """

        split = self._items.split_at(index)
        if split is None:
            return None
        return DequeSplit(PersistentDeque(split.left), PersistentDeque(split.right))

    def split_item_at(self, index: int) -> DequeItemSplit[T] | None:
        """Split around the element at ``index``, or ``None`` when out of range."""

        if index < 0 or index >= len(self):
            return None
        first = self.split_at(index)
        if first is None:
            raise AssertionError("Validated split failed.")
        second = first.right.split_at(1)
        if second is None:
            raise AssertionError("Validated item split failed.")
        return DequeItemSplit(first.left, cast(T, first.right.front()), second.right)

    def split_range(self, start: int, count: int) -> DequeRangeSplit[T] | None:
        """Split into the elements before ``start``, the ``count`` from ``start``, and the rest.
        ``None`` when the range falls outside the deque.
        """

        if start < 0 or count < 0 or start + count > len(self):
            return None
        first = self.split_at(start)
        if first is None:
            raise AssertionError("Validated split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated range split failed.")
        return DequeRangeSplit(first.left, second.left, second.right)

    def insert_at(self, index: int, value: T) -> PersistentDeque[T] | None:
        """Return a deque with ``value`` inserted so that it ends up at ``index``, or ``None`` when
        ``index`` falls outside ``0..len``. A split and two joins, so it does not shift the tail.
        """

        next_items = self._items.insert_at(index, value)
        return None if next_items is None else PersistentDeque(next_items)

    def set_item(self, index: int, value: T) -> PersistentDeque[T] | None:
        """Return a deque with the element at ``index`` replaced, or ``None`` when out of range."""

        next_items = self._items.set_at(index, value)
        if next_items is None:
            return None
        return self if next_items is self._items else PersistentDeque(next_items)

    def remove_at(self, index: int) -> PersistentDeque[T] | None:
        """Return a deque without the element at ``index``, or ``None`` when out of range."""

        next_items = self._items.remove_at(index)
        return None if next_items is None else PersistentDeque(next_items)

    def try_view_left(self) -> DequePop[T] | None:
        """The first element together with the remaining deque, or ``None`` when empty."""

        if self.is_empty:
            return None
        split = self.split_at(1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return DequePop(cast(T, self.front()), split.right)

    def try_view_right(self) -> DequePop[T] | None:
        """The last element together with the remaining deque, or ``None`` when empty."""

        if self.is_empty:
            return None
        split = self.split_at(len(self) - 1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return DequePop(cast(T, self.back()), split.left)

    def reverse(self) -> PersistentDeque[T]:
        """Return a deque with the elements in reverse order."""

        if len(self) < 2:
            return self
        return PersistentDeque.from_iterable(reversed(self.to_list()))

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order."""

        return self._items.to_list()

    def shares_storage_with(self, other: PersistentDeque[T]) -> bool:
        """Whether both deques share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_structure_with(other._items)

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order."""

        return iter(self._items)

    def get_cursor(self, position: int = 0) -> PersistentDequeCursor[T]:
        """Creates an immutable gap cursor at ``position`` in ``0..len(self)``."""

        return PersistentDequeCursor(self, position)


class ReversibleDeque(Generic[T]):
    """Orientation-aware immutable deque with constant-time whole-value reversal."""

    __slots__ = ("_items", "_reversed")

    def __init__(self, items: PersistentDeque[T], reversed_: bool = False) -> None:
        """Wrap an already-built representation; use :meth:`empty` or :meth:`from_iterable` instead.
        """

        self._items = items
        self._reversed = reversed_

    @classmethod
    def empty(cls) -> ReversibleDeque[T]:
        """Return the empty deque."""

        return cls(PersistentDeque.empty())

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> ReversibleDeque[T]:
        """Build a deque from ``values`` in one balanced pass."""

        return cls(PersistentDeque.from_iterable(values))

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the deque holds no elements."""

        return self._items.is_empty

    def front(self) -> T | None:
        """The first element, or ``None`` when empty. Reads and writes follow the deque's current
        orientation.
        """

        return self._items.back() if self._reversed else self._items.front()

    def back(self) -> T | None:
        """The last element, or ``None`` when empty. Reads and writes follow the deque's current
        orientation.
        """

        return self._items.front() if self._reversed else self._items.back()

    def get(self, index: int) -> T | None:
        """The element at ``index``, or ``None`` when out of range. Descends by cached subtree
        counts rather than walking. Reads and writes follow the deque's current orientation.
        """

        if index < 0 or index >= len(self):
            return None
        return self._items.get(len(self) - index - 1 if self._reversed else index)

    def reverse(self) -> ReversibleDeque[T]:
        """Return this deque with its order reversed. Flips an orientation flag and shares the
        underlying tree, so this is constant time rather than a rebuild.
        """

        return ReversibleDeque(self._items, not self._reversed)

    def _aligned_run(self, values: Sequence[T]) -> ReversibleDeque[T]:
        """Builds ``values`` in logical order under this deque's physical orientation.

        A reversed receiver stores the logical range ``[x0..xm-1]`` physically reversed, so a run
        built this way concatenates with either side of a split of this deque through the
        structure-sharing path instead of the orientation-mismatch materialization.
        """

        if not self._reversed:
            return ReversibleDeque(PersistentDeque.from_iterable(values))
        return ReversibleDeque(PersistentDeque.from_iterable(reversed(values)), True)

    def prepend(self, value: T) -> ReversibleDeque[T]:
        """Return a deque with ``value`` added at the front. Reads and writes follow the deque's
        current orientation.
        """

        items = self._items.append(value) if self._reversed else self._items.prepend(value)
        return ReversibleDeque(items, self._reversed)

    def append(self, value: T) -> ReversibleDeque[T]:
        """Return a deque with ``value`` added at the back. Reads and writes follow the deque's
        current orientation.
        """

        items = self._items.prepend(value) if self._reversed else self._items.append(value)
        return ReversibleDeque(items, self._reversed)

    def concat(self, other: ReversibleDeque[T]) -> ReversibleDeque[T]:
        """Return this deque's elements followed by ``other``'s. Joins the two trees rather than
        copying either, so the cost follows their height difference and not their sizes. Two
        deques with the same orientation join structurally; opposite orientations fall back to
        materializing.
        """

        if self.is_empty:
            return other
        if other.is_empty:
            return self
        if self._reversed == other._reversed:
            joined = (
                other._items.concat(self._items)
                if self._reversed
                else self._items.concat(other._items)
            )
            return ReversibleDeque(joined, self._reversed)
        return ReversibleDeque.from_iterable((*self, *other))

    def split_at(self, index: int) -> tuple[ReversibleDeque[T], ReversibleDeque[T]] | None:
        """Split into the elements before ``index`` and those from ``index`` on, or ``None`` when
        ``index`` falls outside ``0..len``. Both halves share structure with the receiver.
        """

        if index < 0 or index > len(self):
            return None
        if not self._reversed:
            split = self._items.split_at(index)
            if split is None:
                raise AssertionError("Validated split failed.")
            return ReversibleDeque(split.left), ReversibleDeque(split.right)
        split = self._items.split_at(len(self) - index)
        if split is None:
            raise AssertionError("Validated reverse split failed.")
        return ReversibleDeque(split.right, True), ReversibleDeque(split.left, True)

    def try_view_left(self) -> tuple[T, ReversibleDeque[T]] | None:
        """The first element together with the remaining deque, or ``None`` when empty."""

        if self.is_empty:
            return None
        split = self.split_at(1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.front()), split[1]

    def try_view_right(self) -> tuple[T, ReversibleDeque[T]] | None:
        """The last element together with the remaining deque, or ``None`` when empty."""

        if self.is_empty:
            return None
        split = self.split_at(len(self) - 1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.back()), split[0]

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order. Reads and writes follow the deque's
        current orientation.
        """

        return list(self)

    def shares_storage_with(self, other: ReversibleDeque[T]) -> bool:
        """Whether both deques share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_storage_with(other._items)

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order. Reads and writes follow the deque's current
        orientation.
        """

        return iter(self._items) if not self._reversed else reversed(self._items.to_list())

    def get_cursor(self, position: int = 0) -> ReversibleDequeCursor[T]:
        """Creates an immutable cursor at a logical-order gap."""

        return ReversibleDequeCursor(self, position)


@dataclass(frozen=True, slots=True)
class LocateResult(Generic[T, M]):
    """Where a measure-directed search landed, reported without splitting the tree. ``found``
    distinguishes a real hit from the end position, so a located ``item`` of ``None`` stays
    unambiguous.
    """

    index: int
    measure_before: M
    item: T | None
    found: bool


class FingerTree(Generic[T, M]):
    """General persistent monoid-measured sequence."""

    __slots__ = ("_items", "policy")

    def __init__(self, items: MeasuredSequence[T, M], policy: MeasurePolicy[T, M]) -> None:
        """Wrap an already-built representation; use :meth:`empty` or :meth:`from_iterable` instead.
        """

        self._items = items
        self.policy = policy

    @classmethod
    def empty(cls, policy: MeasurePolicy[T, M]) -> FingerTree[T, M]:
        """Return the empty tree."""

        return cls(MeasuredSequence.empty(policy), policy)

    @classmethod
    def from_iterable(cls, values: Iterable[T], policy: MeasurePolicy[T, M]) -> FingerTree[T, M]:
        """Build a tree from ``values`` in one balanced pass."""

        return cls(MeasuredSequence.from_iterable(values, policy), policy)

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the tree holds no elements."""

        return self._items.is_empty

    @property
    def measure(self) -> M:
        """The combined measure of every element, read from the root's cached measure."""

        return self._items.measure

    def front(self) -> T | None:
        """The first element, or ``None`` when empty."""

        return self._items.front()

    def back(self) -> T | None:
        """The last element, or ``None`` when empty."""

        return self._items.back()

    def get(self, index: int) -> T | None:
        """The element at ``index``, or ``None`` when out of range. Descends by cached subtree
        counts rather than walking.
        """

        return self._items.at(index)

    def prepend(self, value: T) -> FingerTree[T, M]:
        """Return a tree with ``value`` added at the front."""

        return FingerTree(self._items.prepend(value), self.policy)

    def append(self, value: T) -> FingerTree[T, M]:
        """Return a tree with ``value`` added at the back."""

        return FingerTree(self._items.append(value), self.policy)

    def concat(self, other: FingerTree[T, M]) -> FingerTree[T, M]:
        """Return this tree's elements followed by ``other``'s. Joins the two trees rather than
        copying either, so the cost follows their height difference and not their sizes.
        """

        if self.policy is not other.policy:
            raise TypeError("Trees must retain the same measure policy object.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return FingerTree(self._items.concat(other._items), self.policy)

    def split(self, predicate: Callable[[M], bool]) -> MeasuredSplit[T, M]:
        """Split at the first position whose inclusive prefix measure satisfies the predicate. This
        is the tree's central operation: because every node caches its measure, the search
        descends without visiting the elements it skips. The predicate is expected to be
        monotone, which is what makes "the first satisfying position" well defined.
        """

        located = self._items.locate(predicate)
        result = self.split_at_index(located.index if located.found else len(self))
        if result is None:
            raise AssertionError("Located split failed.")
        return result

    def split_at_index(self, index: int) -> MeasuredSplit[T, M] | None:
        """Split at a positional boundary in ``0..len``, or ``None`` when ``index`` exceeds the
        length.
        """

        split = self._items.split_at(index)
        if split is None:
            return None
        return MeasuredSplit(
            FingerTree(split.left, self.policy), FingerTree(split.right, self.policy)
        )

    def try_split_find(self, predicate: Callable[[M], bool]) -> MeasuredItemSplit[T, M] | None:
        """Find the first element whose inclusive prefix measure satisfies the predicate and return
        it with the elements on either side, or ``None`` when none does. Unlike :meth:`split`,
        this distinguishes "found here" from "not found".
        """

        located = self._items.locate(predicate)
        if not located.found:
            return None
        first = self.split_at_index(located.index)
        if first is None:
            raise AssertionError("Located split failed.")
        second = first.right.split_at_index(1)
        if second is None:
            raise AssertionError("Located item split failed.")
        return MeasuredItemSplit(first.left, cast(T, located.value), second.right)

    def prefix_measure(self, count: int) -> M | None:
        """The combined measure of the first ``count`` elements, or ``None`` when ``count`` exceeds
        the length. Summed from cached node measures rather than element by element.
        """

        return self._items.prefix_measure(count)

    def try_locate(self, predicate: Callable[[M], bool]) -> LocateResult[T, M]:
        """Report where the first element satisfying the predicate sits, without splitting the tree.
        A miss reports the end position with ``found`` false.
        """

        result = self._items.locate(predicate)
        return LocateResult(result.index, result.measure_before, result.value, result.found)

    def set_item(self, index: int, value: T) -> FingerTree[T, M] | None:
        """Return a tree with the element at ``index`` replaced, or ``None`` when out of range."""

        next_items = self._items.set_at(index, value)
        return None if next_items is None else FingerTree(next_items, self.policy)

    def insert_at(self, index: int, value: T) -> FingerTree[T, M] | None:
        """Return a tree with ``value`` inserted so that it ends up at ``index``, or ``None`` when
        ``index`` falls outside ``0..len``. A split and two joins, so it does not shift the tail.
        """

        next_items = self._items.insert_at(index, value)
        return None if next_items is None else FingerTree(next_items, self.policy)

    def remove_at(self, index: int) -> FingerTree[T, M] | None:
        """Return a tree without the element at ``index``, or ``None`` when out of range."""

        next_items = self._items.remove_at(index)
        return None if next_items is None else FingerTree(next_items, self.policy)

    def try_view_left(self) -> tuple[T, FingerTree[T, M]] | None:
        """The first element together with the remaining tree, or ``None`` when empty."""

        if self.is_empty:
            return None
        split = self.split_at_index(1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.front()), split.right

    def try_view_right(self) -> tuple[T, FingerTree[T, M]] | None:
        """The last element together with the remaining tree, or ``None`` when empty."""

        if self.is_empty:
            return None
        split = self.split_at_index(len(self) - 1)
        if split is None:
            raise AssertionError("Validated view split failed.")
        return cast(T, self.back()), split.left

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order."""

        return self._items.to_list()

    def shares_storage_with(self, other: FingerTree[T, M]) -> bool:
        """Whether both trees share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_structure_with(other._items)

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order."""

        return iter(self._items)

    def get_cursor_at_start(self) -> FingerTreeCursor[T, M]:
        """Creates a measure-aware cursor before the first element."""

        return FingerTreeCursor(self, 0)

    def get_cursor_at_end(self) -> FingerTreeCursor[T, M]:
        """Creates a measure-aware cursor after the final element."""

        return FingerTreeCursor(self, len(self))

    def get_cursor(self, predicate: Callable[[M], bool]) -> tuple[bool, FingerTreeCursor[T, M]]:
        """Locates the first inclusive prefix satisfying a monotone predicate."""

        located = self.try_locate(predicate)
        position = located.index if located.found else len(self)
        return located.found, FingerTreeCursor(self, position)


@dataclass(frozen=True, slots=True)
class PersistentDequeCursor(Generic[T]):
    """Immutable snapshot-plus-position cursor over a persistent deque."""

    _snapshot: PersistentDeque[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self._snapshot):
            raise IndexError("cursor position is outside the deque boundary range")

    @property
    def count(self) -> int:
        """Number of elements in the deque version this cursor is positioned in."""

        return len(self._snapshot)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        """The element immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else SequenceCursorPeek(self._snapshot[self.position - 1])

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        """The element immediately after the gap, or ``None`` at the end."""

        return None if self.is_at_end else SequenceCursorPeek(self._snapshot[self.position])

    def move_previous(self) -> PersistentDequeCursor[T]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("deque cursor is already at the start")
        return PersistentDequeCursor(self._snapshot, self.position - 1)

    def move_next(self) -> PersistentDequeCursor[T]:
        """A cursor one position later, raising :class:`IndexError` at the end. The receiver is
        unchanged.
        """

        if self.is_at_end:
            raise IndexError("deque cursor is already at the end")
        return PersistentDequeCursor(self._snapshot, self.position + 1)

    def seek(self, position: int) -> PersistentDequeCursor[T]:
        """A cursor at ``position`` within the same deque version, raising
        :class:`IndexError` when it is out of range.
        """

        return (
            self if position == self.position else PersistentDequeCursor(self._snapshot, position)
        )

    def insert(self, value: T) -> PersistentDequeCursor[T]:
        """Insert ``value`` at the gap and return a cursor positioned after it. The receiver keeps
        its own version, so cursors retained beforehand never see it.
        """

        snapshot = self._snapshot.insert_at(self.position, value)
        if snapshot is None:
            raise AssertionError("validated cursor insertion failed")
        return PersistentDequeCursor(snapshot, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> PersistentDequeCursor[T]:
        """Insert every element of ``values`` at the gap, in order, and return a cursor after the
        last. Splits and joins once regardless of how many are inserted.
        """

        materialized = tuple(values)
        if not materialized:
            return self
        split = self._snapshot.split_at(self.position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        middle = PersistentDeque.from_iterable(materialized)
        return PersistentDequeCursor(
            split.left.concat(middle).concat(split.right), self.position + len(materialized)
        )

    def delete_previous(self) -> PersistentDequeCursor[T]:
        """Remove the element before the gap and return a cursor in its place, raising
        :class:`IndexError` at the start.
        """

        if self.is_at_start:
            raise IndexError("deque cursor has no previous element")
        snapshot = self._snapshot.remove_at(self.position - 1)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return PersistentDequeCursor(snapshot, self.position - 1)

    def delete_next(self) -> PersistentDequeCursor[T]:
        """Remove the element after the gap and return a cursor in its place, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("deque cursor has no next element")
        snapshot = self._snapshot.remove_at(self.position)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return PersistentDequeCursor(snapshot, self.position)

    def replace_next(self, value: T) -> PersistentDequeCursor[T]:
        """Replace the element after the gap, keeping the gap where it is, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("deque cursor has no next element")
        snapshot = self._snapshot.set_item(self.position, value)
        if snapshot is None:
            raise AssertionError("validated cursor replacement failed")
        return PersistentDequeCursor(snapshot, self.position)

    def snapshot(self) -> PersistentDeque[T]:
        """The deque version this cursor is positioned in."""

        return self._snapshot


@dataclass(frozen=True, slots=True)
class ReversibleDequeCursor(Generic[T]):
    """Immutable logical-order gap cursor over a reversible deque."""

    _snapshot: ReversibleDeque[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self._snapshot):
            raise IndexError("cursor position is outside the reversible-deque boundary range")

    @property
    def count(self) -> int:
        """Number of elements in the deque version this cursor is positioned in."""

        return len(self._snapshot)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element. Reads and writes follow the deque's current
        orientation.
        """

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element. Reads and writes follow the deque's current
        orientation.
        """

        return self.position == self.count

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        """The element immediately before the gap, or ``None`` at the start."""

        if self.is_at_start:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self.position - 1)))

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        """The element immediately after the gap, or ``None`` at the end."""

        if self.is_at_end:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self.position)))

    def move_previous(self) -> ReversibleDequeCursor[T]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("reversible-deque cursor is already at the start")
        return ReversibleDequeCursor(self._snapshot, self.position - 1)

    def move_next(self) -> ReversibleDequeCursor[T]:
        """A cursor one position later, raising :class:`IndexError` at the end. The receiver is
        unchanged.
        """

        if self.is_at_end:
            raise IndexError("reversible-deque cursor is already at the end")
        return ReversibleDequeCursor(self._snapshot, self.position + 1)

    def seek(self, position: int) -> ReversibleDequeCursor[T]:
        """A cursor at ``position`` within the same deque version, raising
        :class:`IndexError` when it is out of range.
        """

        return (
            self if position == self.position else ReversibleDequeCursor(self._snapshot, position)
        )

    def insert(self, value: T) -> ReversibleDequeCursor[T]:
        """Insert ``value`` at the gap and return a cursor positioned after it. The receiver keeps
        its own version, so cursors retained beforehand never see it.
        """

        split = self._snapshot.split_at(self.position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        snapshot = split[0].append(value).concat(split[1])
        return ReversibleDequeCursor(snapshot, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> ReversibleDequeCursor[T]:
        """Insert every element of ``values`` at the gap, in order, and return a cursor after the
        last. Splits and joins once regardless of how many are inserted.
        """

        materialized = tuple(values)
        if not materialized:
            return self
        split = self._snapshot.split_at(self.position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        middle = self._snapshot._aligned_run(materialized)
        snapshot = split[0].concat(middle).concat(split[1])
        return ReversibleDequeCursor(snapshot, self.position + len(materialized))

    def delete_previous(self) -> ReversibleDequeCursor[T]:
        """Remove the element before the gap and return a cursor in its place, raising
        :class:`IndexError` at the start.
        """

        if self.is_at_start:
            raise IndexError("reversible-deque cursor has no previous element")
        first = self._snapshot.split_at(self.position - 1)
        if first is None:
            raise AssertionError("validated cursor split failed")
        second = first[1].split_at(1)
        if second is None:
            raise AssertionError("validated cursor item split failed")
        return ReversibleDequeCursor(first[0].concat(second[1]), self.position - 1)

    def delete_next(self) -> ReversibleDequeCursor[T]:
        """Remove the element after the gap and return a cursor in its place, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("reversible-deque cursor has no next element")
        first = self._snapshot.split_at(self.position)
        if first is None:
            raise AssertionError("validated cursor split failed")
        second = first[1].split_at(1)
        if second is None:
            raise AssertionError("validated cursor item split failed")
        return ReversibleDequeCursor(first[0].concat(second[1]), self.position)

    def replace_next(self, value: T) -> ReversibleDequeCursor[T]:
        """Replace the element after the gap, keeping the gap where it is, raising
        :class:`IndexError` at the end.
        """

        return self.delete_next().insert(value).move_previous()

    def reverse(self) -> ReversibleDequeCursor[T]:
        """A cursor over the reversed deque, positioned at the mirrored gap."""

        return ReversibleDequeCursor(self._snapshot.reverse(), self.count - self.position)

    def snapshot(self) -> ReversibleDeque[T]:
        """The deque version this cursor is positioned in."""

        return self._snapshot


@dataclass(frozen=True, slots=True)
class FingerTreeCursor(Generic[T, M]):
    """Immutable measure-aware cursor over one exact finger-tree version."""

    _snapshot: FingerTree[T, M]
    _position: int

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        if self._position < 0 or self._position > len(self._snapshot):
            raise IndexError("cursor position is outside the finger-tree boundary range")

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self._position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self._position == len(self._snapshot)

    @property
    def measure_before(self) -> M:
        """The combined measure of every element before the gap."""

        measure = self._snapshot.prefix_measure(self._position)
        if measure is None:
            raise AssertionError("validated prefix measure failed")
        return measure

    @property
    def measure_after(self) -> M:
        """The combined measure of every element at or after the gap."""

        split = self._snapshot.split_at_index(self._position)
        if split is None:
            raise AssertionError("validated cursor split failed")
        return split.right.measure

    def peek_previous(self) -> SequenceCursorPeek[T] | None:
        """The element immediately before the gap, or ``None`` at the start."""

        if self.is_at_start:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self._position - 1)))

    def peek_next(self) -> SequenceCursorPeek[T] | None:
        """The element immediately after the gap, or ``None`` at the end."""

        if self.is_at_end:
            return None
        return SequenceCursorPeek(cast(T, self._snapshot.get(self._position)))

    def move_previous(self) -> FingerTreeCursor[T, M]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("finger-tree cursor is already at the start")
        return FingerTreeCursor(self._snapshot, self._position - 1)

    def move_next(self) -> FingerTreeCursor[T, M]:
        """A cursor one position later, raising :class:`IndexError` at the end. The receiver is
        unchanged.
        """

        if self.is_at_end:
            raise IndexError("finger-tree cursor is already at the end")
        return FingerTreeCursor(self._snapshot, self._position + 1)

    def seek_by_measure(self, predicate: Callable[[M], bool]) -> FingerTreeCursor[T, M]:
        """Seek to the first position whose inclusive prefix measure satisfies the predicate,
        reporting whether one did. On a miss the cursor sits at the end and remains usable.
        """

        found, cursor = self._snapshot.get_cursor(predicate)
        _ = found
        return cursor

    def insert(self, value: T) -> FingerTreeCursor[T, M]:
        """Insert ``value`` at the gap and return a cursor positioned after it. The receiver keeps
        its own version, so cursors retained beforehand never see it.
        """

        snapshot = self._snapshot.insert_at(self._position, value)
        if snapshot is None:
            raise AssertionError("validated cursor insertion failed")
        return FingerTreeCursor(snapshot, self._position + 1)

    def delete_previous(self) -> FingerTreeCursor[T, M]:
        """Remove the element before the gap and return a cursor in its place, raising
        :class:`IndexError` at the start.
        """

        if self.is_at_start:
            raise IndexError("finger-tree cursor has no previous element")
        snapshot = self._snapshot.remove_at(self._position - 1)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return FingerTreeCursor(snapshot, self._position - 1)

    def delete_next(self) -> FingerTreeCursor[T, M]:
        """Remove the element after the gap and return a cursor in its place, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("finger-tree cursor has no next element")
        snapshot = self._snapshot.remove_at(self._position)
        if snapshot is None:
            raise AssertionError("validated cursor deletion failed")
        return FingerTreeCursor(snapshot, self._position)

    def replace_next(self, value: T) -> FingerTreeCursor[T, M]:
        """Replace the element after the gap, keeping the gap where it is, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("finger-tree cursor has no next element")
        snapshot = self._snapshot.set_item(self._position, value)
        if snapshot is None:
            raise AssertionError("validated cursor replacement failed")
        return FingerTreeCursor(snapshot, self._position)

    def snapshot(self) -> FingerTree[T, M]:
        """The tree version this cursor is positioned in."""

        return self._snapshot


__all__ = [
    "DequeItemSplit",
    "DequePop",
    "DequeRangeSplit",
    "DequeSplit",
    "FingerTree",
    "FingerTreeCursor",
    "LocateResult",
    "MeasuredItemSplit",
    "MeasuredSplit",
    "PersistentDeque",
    "PersistentDequeCursor",
    "ReversibleDeque",
    "ReversibleDequeCursor",
    "SequenceCursorPeek",
]
