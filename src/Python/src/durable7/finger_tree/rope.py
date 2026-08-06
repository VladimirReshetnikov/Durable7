"""Persistent positional, measured, and text ropes with immutable gap cursors."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator, MutableSequence
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .core import FingerTree, PersistentDeque
from .measures import MeasurePolicy

T = TypeVar("T")
M = TypeVar("M")


class Rope(Generic[T]):
    """A persistent chunked sequence addressed by position.

    Every operation returns a new rope and leaves the receiver valid; versions share their unchanged
    structure, so an edit copies a path rather than the sequence.
    """

    __slots__ = ("_items",)

    def __init__(self, items: PersistentDeque[T]) -> None:
        """Wrap an already-built representation; use the factory methods instead."""

        self._items = items

    @classmethod
    def empty(cls) -> Rope[T]:
        """Return the empty rope."""

        return cls(PersistentDeque.empty())

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> Rope[T]:
        """Build a rope from ``values``."""

        return cls(PersistentDeque.from_iterable(values))

    @classmethod
    def from_text(cls, text: str) -> Rope[str]:
        """Build a character rope from ``text``, one element per code point."""

        return Rope.from_iterable(text)

    def get_cursor(self, position: int = 0) -> RopeCursor[T]:
        """A cursor at gap ``position`` of the rope."""

        return RopeCursor(self, position)

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the rope holds no elements."""

        return self._items.is_empty

    def front(self) -> T | None:
        """The first element, or ``None`` when empty."""

        return self._items.front()

    def back(self) -> T | None:
        """The last element, or ``None`` when empty."""

        return self._items.back()

    def get(self, index: int) -> T | None:
        """The element at ``index``, or ``None`` when out of range."""

        return self._items.get(index)

    def copy_to(
        self,
        index: int,
        destination: MutableSequence[T],
        destination_index: int | None = None,
        count: int | None = None,
    ) -> bool:
        """Copy ``count`` elements starting at ``index`` into ``destination``, reporting whether the
        range was valid. Copies whole runs where it can rather than one element at a time.
        """

        target = len(destination) if destination_index is None else destination_index
        length = len(self) - index if count is None else count
        sliced = self.slice(index, length)
        if sliced is None or target < 0 or target > len(destination):
            return False
        destination[target:target] = sliced.to_list()
        return True

    def push_front(self, value: T) -> Rope[T]:
        """Return a rope with ``value`` added at the front."""

        return Rope(self._items.prepend(value))

    def push_back(self, value: T) -> Rope[T]:
        """Return a rope with ``value`` added at the back."""

        return Rope(self._items.append(value))

    def set_item(self, index: int, value: T) -> Rope[T] | None:
        """Return a rope with the element at ``index`` replaced, or ``None`` when out of range."""

        items = self._items.set_item(index, value)
        if items is None:
            return None
        return self if items is self._items else Rope(items)

    def insert_at(self, index: int, value: T) -> Rope[T] | None:
        """Return a rope with ``value`` inserted so that it ends up at ``index``, or ``None`` when
        ``index`` falls outside ``0..len``.
        """

        items = self._items.insert_at(index, value)
        return None if items is None else Rope(items)

    def insert_range(self, index: int, values: Iterable[T]) -> Rope[T] | None:
        """Insert every element of ``values`` at ``index``, in order. Splits and joins once
        regardless of how many are inserted.
        """

        split = self._items.split_at(index)
        if split is None:
            return None
        materialized = list(values)
        if not materialized:
            return self
        return Rope(
            split.left.concat(PersistentDeque.from_iterable(materialized)).concat(split.right)
        )

    def remove_at(self, index: int) -> Rope[T] | None:
        """Return a rope without the element at ``index``, or ``None`` when out of range."""

        items = self._items.remove_at(index)
        return None if items is None else Rope(items)

    def remove_range(self, index: int, count: int) -> Rope[T] | None:
        """Remove ``count`` elements starting at ``index``, or return ``None`` when the range falls
        outside the rope.
        """

        split = self._items.split_range(index, count)
        if split is None:
            return None
        if count == 0:
            return self
        return Rope(split.before.concat(split.after))

    def slice(self, index: int, count: int) -> Rope[T] | None:
        """The ``count`` elements starting at ``index`` as a new rope, or ``None`` when the range
        falls outside it. The result shares structure with the receiver.
        """

        split = self._items.split_range(index, count)
        if split is None:
            return None
        return self if index == 0 and count == len(self) else Rope(split.range)

    def split_at(self, index: int) -> tuple[Rope[T], Rope[T]] | None:
        """Split into the elements before ``index`` and those from ``index`` on, or ``None`` when
        ``index`` falls outside ``0..len``.
        """

        split = self._items.split_at(index)
        return None if split is None else (Rope(split.left), Rope(split.right))

    def concat(self, other: Rope[T]) -> Rope[T]:
        """Return this rope's elements followed by ``other``'s, joining the two trees rather than
        copying either.
        """

        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return Rope(self._items.concat(other._items))

    def compact(self) -> Rope[T]:
        """Return an equal rope whose chunks have been merged. Many small splits can leave more,
        smaller chunks than needed; compacting trades one linear rebuild for cheaper later
        traversal.
        """

        return Rope.from_iterable(self)

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order."""

        return self._items.to_list()

    def shares_storage_with(self, other: Rope[T]) -> bool:
        """Whether both ropes share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_storage_with(other._items)

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order."""

        return iter(self._items)


@dataclass(frozen=True, slots=True)
class MeasuredRopeLocate(Generic[T, M]):
    """Where a measure-directed search landed, reported without splitting the rope. ``found``
    distinguishes a real hit from the end position, so a located ``value`` of ``None`` stays
    unambiguous.
    """

    index: int
    measure_before: M
    value: T | None
    found: bool


@dataclass(frozen=True, slots=True)
class MeasuredRopeSplit(Generic[T, M]):
    """The two ropes produced by a split; both carry their own recomputed measure."""

    left: MeasuredRope[T, M]
    right: MeasuredRope[T, M]


@dataclass(frozen=True, slots=True)
class RopeCursorPeek(Generic[T]):
    """An element found next to a cursor gap, wrapped so a stored ``None`` stays distinct from
    "nothing there".
    """

    value: T


@dataclass(frozen=True, slots=True)
class MeasuredRopeCursorSearch(Generic[T, M]):
    """The outcome of a measure-directed cursor seek. On a miss the cursor sits at the end and
    remains usable.
    """

    cursor: MeasuredRopeCursor[T, M]
    found: bool


@dataclass(frozen=True, slots=True)
class TextRopeCursorSearch:
    """The outcome of seeking a text cursor to a line and column. On a miss the cursor remains
    usable.
    """

    cursor: TextRopeCursor
    found: bool


class MeasuredRope(Generic[T, M]):
    """A persistent rope that also caches a caller-chosen measure at every node.

    Adds measure-directed search and splitting to what :class:`Rope` provides, so one structure
    answers both positional and monoid questions.
    """

    __slots__ = ("_items", "policy")

    def __init__(self, items: FingerTree[T, M], policy: MeasurePolicy[T, M]) -> None:
        """Wrap an already-built representation; use the factory methods instead."""

        self._items = items
        self.policy = policy

    @classmethod
    def empty(cls, policy: MeasurePolicy[T, M]) -> MeasuredRope[T, M]:
        """Return the empty rope."""

        return cls(FingerTree.empty(policy), policy)

    @classmethod
    def from_iterable(cls, values: Iterable[T], policy: MeasurePolicy[T, M]) -> MeasuredRope[T, M]:
        """Build a rope from ``values``."""

        return cls(FingerTree.from_iterable(values, policy), policy)

    def get_cursor(self, position: int = 0) -> MeasuredRopeCursor[T, M]:
        """A cursor at gap ``position`` of the rope."""

        return MeasuredRopeCursor(self, position)

    def get_cursor_by_measure(
        self, predicate: Callable[[M], bool]
    ) -> MeasuredRopeCursor[T, M] | None:
        """A cursor at the first position whose inclusive prefix measure satisfies the predicate."""

        located = self.locate_by_measure(predicate)
        return MeasuredRopeCursor(self, located.index) if located.found else None

    def cursor_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeCursorSearch[T, M]:
        """Seek by measure and report whether any prefix satisfied the predicate."""

        located = self.locate_by_measure(predicate)
        return MeasuredRopeCursorSearch(
            MeasuredRopeCursor(self, located.index if located.found else len(self)), located.found
        )

    def __len__(self) -> int:
        """Number of elements."""

        return len(self._items)

    @property
    def is_empty(self) -> bool:
        """Whether the rope holds no elements."""

        return self._items.is_empty

    @property
    def measure(self) -> M:
        """The combined measure of every element, read from the cached root measure."""

        return self._items.measure

    def front(self) -> T | None:
        """The first element, or ``None`` when empty."""

        return self._items.front()

    def back(self) -> T | None:
        """The last element, or ``None`` when empty."""

        return self._items.back()

    def get(self, index: int) -> T | None:
        """The element at ``index``, or ``None`` when out of range."""

        return self._items.get(index)

    def prefix_measure(self, count: int) -> M | None:
        """The combined measure of the first ``count`` elements, or ``None`` when ``count`` exceeds
        the length. Summed from cached node measures rather than element by element.
        """

        return self._items.prefix_measure(count)

    def copy_to(
        self,
        index: int,
        destination: MutableSequence[T],
        destination_index: int | None = None,
        count: int | None = None,
    ) -> bool:
        """Copy ``count`` elements starting at ``index`` into ``destination``, reporting whether the
        range was valid. Copies whole runs where it can rather than one element at a time.
        """

        target = len(destination) if destination_index is None else destination_index
        length = len(self) - index if count is None else count
        sliced = self.slice(index, length)
        if sliced is None or target < 0 or target > len(destination):
            return False
        destination[target:target] = list(sliced)
        return True

    def push_front(self, value: T) -> MeasuredRope[T, M]:
        """Return a rope with ``value`` added at the front."""

        return MeasuredRope(self._items.prepend(value), self.policy)

    def push_back(self, value: T) -> MeasuredRope[T, M]:
        """Return a rope with ``value`` added at the back."""

        return MeasuredRope(self._items.append(value), self.policy)

    def set_item(self, index: int, value: T) -> MeasuredRope[T, M] | None:
        """Return a rope with the element at ``index`` replaced, or ``None`` when out of range."""

        items = self._items.set_item(index, value)
        return None if items is None else MeasuredRope(items, self.policy)

    def insert_at(self, index: int, value: T) -> MeasuredRope[T, M] | None:
        """Return a rope with ``value`` inserted so that it ends up at ``index``, or ``None`` when
        ``index`` falls outside ``0..len``.
        """

        items = self._items.insert_at(index, value)
        return None if items is None else MeasuredRope(items, self.policy)

    def insert_range(self, index: int, values: Iterable[T]) -> MeasuredRope[T, M] | None:
        """Insert every element of ``values`` at ``index``, in order. Splits and joins once
        regardless of how many are inserted.
        """

        split = self.split_at(index)
        if split is None:
            return None
        materialized = list(values)
        if not materialized:
            return self
        return split.left.concat(MeasuredRope.from_iterable(materialized, self.policy)).concat(
            split.right
        )

    def remove_at(self, index: int) -> MeasuredRope[T, M] | None:
        """Return a rope without the element at ``index``, or ``None`` when out of range."""

        items = self._items.remove_at(index)
        return None if items is None else MeasuredRope(items, self.policy)

    def remove_range(self, index: int, count: int) -> MeasuredRope[T, M] | None:
        """Remove ``count`` elements starting at ``index``, or return ``None`` when the range falls
        outside the rope.
        """

        if index < 0 or count < 0 or index + count > len(self):
            return None
        if count == 0:
            return self
        first = self.split_at(index)
        if first is None:
            raise AssertionError("Validated measured-rope split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated measured-rope range split failed.")
        return first.left.concat(second.right)

    def slice(self, index: int, count: int) -> MeasuredRope[T, M] | None:
        """The ``count`` elements starting at ``index`` as a new rope, or ``None`` when the range
        falls outside it. The result shares structure with the receiver.
        """

        if index < 0 or count < 0 or index + count > len(self):
            return None
        if index == 0 and count == len(self):
            return self
        first = self.split_at(index)
        if first is None:
            raise AssertionError("Validated measured-rope split failed.")
        second = first.right.split_at(count)
        if second is None:
            raise AssertionError("Validated measured-rope range split failed.")
        return second.left

    def split_at(self, index: int) -> MeasuredRopeSplit[T, M] | None:
        """Split into the elements before ``index`` and those from ``index`` on, or ``None`` when
        ``index`` falls outside ``0..len``.
        """

        split = self._items.split_at_index(index)
        return (
            None
            if split is None
            else MeasuredRopeSplit(
                MeasuredRope(split.left, self.policy),
                MeasuredRope(split.right, self.policy),
            )
        )

    def split_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeSplit[T, M]:
        """Split at the first position whose inclusive prefix measure satisfies the predicate. The
        predicate is expected to be monotone, which is what makes "the first satisfying position"
        well defined.
        """

        split = self._items.split(predicate)
        return MeasuredRopeSplit(
            MeasuredRope(split.left, self.policy), MeasuredRope(split.right, self.policy)
        )

    def locate_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeLocate[T, M]:
        """Report where the first element satisfying the predicate sits, without splitting."""

        result = self._items.try_locate(predicate)
        return MeasuredRopeLocate(result.index, result.measure_before, result.item, result.found)

    def concat(self, other: MeasuredRope[T, M]) -> MeasuredRope[T, M]:
        """Return this rope's elements followed by ``other``'s, joining the two trees rather than
        copying either.
        """

        if self.policy is not other.policy:
            raise TypeError("Ropes must retain the same measure policy object.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return MeasuredRope(self._items.concat(other._items), self.policy)

    def compact(self) -> MeasuredRope[T, M]:
        """Return an equal rope whose chunks have been merged, as :meth:`Rope.compact` does."""

        return MeasuredRope.from_iterable(self, self.policy)

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order."""

        return list(self)

    def shares_storage_with(self, other: MeasuredRope[T, M]) -> bool:
        """Whether both ropes share any node by object identity. A representation test used to
        confirm that a no-op avoided copying, not an equality test.
        """

        return self._items.shares_storage_with(other._items)

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order."""

        return iter(self._items)


class NewlineMeasure:
    """Counts line feeds, which is what gives a text rope logarithmic offset-to-line conversion."""

    identity = 0

    def combine(self, left: int, right: int) -> int:
        """Add two newline counts."""

        return left + right

    def measure(self, element: str) -> int:
        """A line feed measures as one; every other character as zero."""

        return 1 if element == "\n" else 0


_NEWLINE_MEASURE = NewlineMeasure()


@dataclass(frozen=True, slots=True)
class LineColumn:
    """A zero-based line and column position within a text rope."""

    line: int
    column: int


class TextRope:
    """Unicode-code-point text rope with line navigation."""

    __slots__ = ("_characters",)

    def __init__(self, characters: MeasuredRope[str, int]) -> None:
        """Wrap an already-built representation; use the factory methods instead."""

        self._characters = characters

    @classmethod
    def empty(cls) -> TextRope:
        """Return the empty rope."""

        return cls(MeasuredRope.empty(_NEWLINE_MEASURE))

    @classmethod
    def from_text(cls, text: str) -> TextRope:
        """Build a text rope from ``text``, one element per code point."""

        return cls(MeasuredRope.from_iterable(text, _NEWLINE_MEASURE))

    @classmethod
    def from_characters(cls, characters: Iterable[str]) -> TextRope:
        """Build a text rope from individual characters."""

        materialized = list(characters)
        if any(len(character) != 1 for character in materialized):
            raise ValueError("TextRope characters must each be one Unicode code point.")
        return cls(MeasuredRope.from_iterable(materialized, _NEWLINE_MEASURE))

    def get_cursor(self, position: int = 0) -> TextRopeCursor:
        """A cursor at gap ``position`` of the rope."""

        return TextRopeCursor(self._characters.get_cursor(position), self)

    def __len__(self) -> int:
        """Number of characters."""

        return len(self._characters)

    @property
    def is_empty(self) -> bool:
        """Whether the rope holds no characters."""

        return self._characters.is_empty

    def as_string(self) -> str:
        """Materialize the whole rope as a string."""

        return "".join(self._characters)

    def line_count(self) -> int:
        """Number of lines, counting the trailing line even when empty. Constant time: newline
        counts are cached in the measure.
        """

        return self._characters.measure + 1

    def line_of_offset(self, offset: int) -> int | None:
        """The zero-based line containing ``offset``, or ``None`` when it exceeds the length. The
        cached newline counts avoid scanning the text.
        """

        return self._characters.prefix_measure(offset)

    def line_start_offset(self, line: int) -> int | None:
        """The offset at which ``line`` begins, or ``None`` when ``line`` is out of range."""

        if line < 0 or line >= self.line_count():
            return None
        if line == 0:
            return 0
        located = self._characters.locate_by_measure(lambda count: count >= line)
        return located.index + 1 if located.found else None

    def line_column_of(self, offset: int) -> LineColumn | None:
        """Convert a character offset into a zero-based line and column, or ``None`` when the offset
        exceeds the length.
        """

        line = self.line_of_offset(offset)
        if line is None:
            return None
        start = self.line_start_offset(line)
        if start is None:
            raise AssertionError("Valid line had no start.")
        return LineColumn(line, offset - start)

    def offset_of(self, line: int, column: int) -> int | None:
        """Convert a zero-based line and column back into a character offset, or ``None`` when the
        position does not exist. The inverse of :meth:`line_column_of`.
        """

        if column < 0:
            return None
        start = self.line_start_offset(line)
        if start is None:
            return None
        if line + 1 < self.line_count():
            next_start = self.line_start_offset(line + 1)
            if next_start is None:
                raise AssertionError("Valid next line had no start.")
            end = next_start - 1
        else:
            end = len(self)
        return start + column if start + column <= end else None

    def get_line(self, line: int) -> str | None:
        """The text of ``line`` without its terminator, or ``None`` when out of range."""

        start = self.line_start_offset(line)
        if start is None:
            return None
        if line + 1 < self.line_count():
            next_start = self.line_start_offset(line + 1)
            if next_start is None:
                raise AssertionError("Valid next line had no start.")
            end = next_start - 1
        else:
            end = len(self)
        sliced = self._characters.slice(start, end - start)
        if sliced is None:
            raise AssertionError("Validated line slice failed.")
        return "".join(sliced)

    def lines(self) -> list[str]:
        """Every line's text, without terminators."""

        return [cast(str, self.get_line(line)) for line in range(self.line_count())]

    def to_rope(self) -> Rope[str]:
        """The underlying characters as a plain :class:`Rope`, dropping the newline measure."""

        return Rope.from_iterable(self._characters)

    def to_measured_rope(self) -> MeasuredRope[str, int]:
        """The underlying characters as a :class:`MeasuredRope` that keeps the newline measure."""

        return self._characters

    def __iter__(self) -> Iterator[str]:
        """Iterate the characters in sequence order."""

        return iter(self._characters)


class RopeBuilder:
    """A mutable accumulator for building a character or text rope in bulk.

    Deliberately mutable and not a snapshot: text is appended into a plain buffer and turned into a
    persistent rope only on demand, so bulk construction avoids per-append tree work.
    """

    def __init__(self) -> None:
        """Wrap an already-built representation; use the factory methods instead."""

        self._parts: list[str] = []
        self._size = 0

    def __len__(self) -> int:
        """Number of characters."""

        return self._size

    @property
    def is_empty(self) -> bool:
        """Whether the builder holds no characters."""

        return self._size == 0

    def as_string(self) -> str:
        """The accumulated text, without building a rope."""

        return "".join(self._parts)

    def append(self, text: str) -> RopeBuilder:
        """Append ``text``, returning the builder for chaining."""

        self._parts.append(text)
        self._size += len(text)
        return self

    def append_char(self, value: str) -> RopeBuilder:
        """Append one character, returning the builder for chaining."""

        if len(value) != 1:
            raise ValueError("append_char requires one Unicode code point.")
        return self.append(value)

    def append_line(self, text: str = "") -> RopeBuilder:
        """Append ``text`` followed by a line feed, returning the builder for chaining."""

        return self.append(text).append("\n")

    def clear(self) -> RopeBuilder:
        """Discard the accumulated text, returning the builder for chaining."""

        self._parts.clear()
        self._size = 0
        return self

    def to_rope(self) -> Rope[str]:
        """Build a character :class:`Rope` from the accumulated text, leaving the builder usable."""

        return Rope.from_text(self.as_string())

    def to_text_rope(self) -> TextRope:
        """Build a :class:`TextRope` from the accumulated text, leaving the builder usable."""

        return TextRope.from_text(self.as_string())


@dataclass(frozen=True, slots=True)
class RopeCursor(Generic[T]):
    """An immutable gap cursor over one :class:`Rope` version."""

    rope: Rope[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self.rope):
            raise ValueError("Cursor position is outside the rope.")

    @property
    def count(self) -> int:
        """Number of elements in the rope version this cursor is positioned in."""

        return len(self.rope)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> T | None:
        """The element immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else self.rope.get(self.position - 1)

    def peek_next(self) -> T | None:
        """The element immediately after the gap, or ``None`` at the end."""

        return None if self.is_at_end else self.rope.get(self.position)

    def peek_previous_entry(self) -> RopeCursorPeek[T] | None:
        """The element before the gap wrapped for presence, so a stored ``None`` stays distinct from
        "nothing there".
        """

        return (
            None if self.is_at_start else RopeCursorPeek(cast(T, self.rope.get(self.position - 1)))
        )

    def peek_next_entry(self) -> RopeCursorPeek[T] | None:
        """The element after the gap wrapped for presence, so a stored ``None`` stays distinct from
        "nothing there".
        """

        return None if self.is_at_end else RopeCursorPeek(cast(T, self.rope.get(self.position)))

    def move_previous(self) -> RopeCursor[T]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return RopeCursor(self.rope, self.position - 1)

    def move_next(self) -> RopeCursor[T]:
        """A cursor one position later, raising :class:`IndexError` at the end. The receiver is
        unchanged.
        """

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return RopeCursor(self.rope, self.position + 1)

    def seek(self, position: int) -> RopeCursor[T]:
        """A cursor at ``position`` within the same rope version, raising
        :class:`IndexError` when it is out of range.
        """

        return self if position == self.position else RopeCursor(self.rope, position)

    def insert(self, value: T) -> RopeCursor[T]:
        """Insert ``value`` at the gap and return a cursor positioned after it. The receiver keeps
        its own version, so cursors retained beforehand never see it.
        """

        rope = self.rope.insert_at(self.position, value)
        if rope is None:
            raise AssertionError("Cursor insertion failed.")
        return RopeCursor(rope, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> RopeCursor[T]:
        """Insert every element of ``values`` at ``index``, in order. Splits and joins once
        regardless of how many are inserted.
        """

        materialized = list(values)
        if not materialized:
            return self
        rope = self.rope.insert_range(self.position, materialized)
        if rope is None:
            raise AssertionError("Cursor range insertion failed.")
        return RopeCursor(rope, self.position + len(materialized))

    def delete_previous(self) -> RopeCursor[T]:
        """Remove the element before the gap and return a cursor in its place, raising
        :class:`IndexError` at the start.
        """

        if self.is_at_start:
            raise IndexError("No element precedes the cursor.")
        rope = self.rope.remove_at(self.position - 1)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return RopeCursor(rope, self.position - 1)

    def delete_next(self) -> RopeCursor[T]:
        """Remove the element after the gap and return a cursor in its place, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        rope = self.rope.remove_at(self.position)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return RopeCursor(rope, self.position)

    def replace_next(self, value: T) -> RopeCursor[T]:
        """Replace the element after the gap, keeping the gap where it is, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        without_current = self.rope.remove_at(self.position)
        if without_current is None:
            raise AssertionError("Cursor replacement removal failed.")
        rope = without_current.insert_at(self.position, value)
        if rope is None:
            raise AssertionError("Cursor replacement failed.")
        return RopeCursor(rope, self.position)

    def snapshot(self) -> Rope[T]:
        """The rope version this cursor is positioned in."""

        return self.rope


@dataclass(frozen=True, slots=True)
class MeasuredRopeCursor(Generic[T, M]):
    """An immutable gap cursor over one :class:`MeasuredRope` version, able to seek by measure."""

    rope: MeasuredRope[T, M]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..len``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self.rope):
            raise ValueError("Cursor position is outside the rope.")

    @property
    def count(self) -> int:
        """Number of elements in the rope version this cursor is positioned in."""

        return len(self.rope)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    @property
    def measure_before(self) -> M:
        """The combined measure of every element before the gap."""

        return cast(M, self.rope.prefix_measure(self.position))

    @property
    def measure_after(self) -> M:
        """The combined measure of every element at or after the gap."""

        sliced = self.rope.slice(self.position, self.count - self.position)
        if sliced is None:
            raise AssertionError("Cursor suffix slice failed.")
        return sliced.measure

    def peek_previous(self) -> T | None:
        """The element immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else self.rope.get(self.position - 1)

    def peek_next(self) -> T | None:
        """The element immediately after the gap, or ``None`` at the end."""

        return None if self.is_at_end else self.rope.get(self.position)

    def peek_previous_entry(self) -> RopeCursorPeek[T] | None:
        """The element before the gap wrapped for presence, so a stored ``None`` stays distinct from
        "nothing there".
        """

        return (
            None if self.is_at_start else RopeCursorPeek(cast(T, self.rope.get(self.position - 1)))
        )

    def peek_next_entry(self) -> RopeCursorPeek[T] | None:
        """The element after the gap wrapped for presence, so a stored ``None`` stays distinct from
        "nothing there".
        """

        return None if self.is_at_end else RopeCursorPeek(cast(T, self.rope.get(self.position)))

    def move_previous(self) -> MeasuredRopeCursor[T, M]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return MeasuredRopeCursor(self.rope, self.position - 1)

    def move_next(self) -> MeasuredRopeCursor[T, M]:
        """A cursor one position later, raising :class:`IndexError` at the end. The receiver is
        unchanged.
        """

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return MeasuredRopeCursor(self.rope, self.position + 1)

    def seek(self, position: int) -> MeasuredRopeCursor[T, M]:
        """A cursor at ``position`` within the same rope version, raising
        :class:`IndexError` when it is out of range.
        """

        return self if position == self.position else MeasuredRopeCursor(self.rope, position)

    def seek_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeCursor[T, M] | None:
        """A cursor at the first position whose inclusive prefix measure satisfies the predicate."""

        located = self.rope.locate_by_measure(predicate)
        return MeasuredRopeCursor(self.rope, located.index) if located.found else None

    def search_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeCursorSearch[T, M]:
        """Seek by measure and report whether any prefix satisfied the predicate."""

        located = self.rope.locate_by_measure(predicate)
        return MeasuredRopeCursorSearch(
            self.seek(located.index if located.found else self.count), located.found
        )

    def insert(self, value: T) -> MeasuredRopeCursor[T, M]:
        """Insert ``value`` at the gap and return a cursor positioned after it. The receiver keeps
        its own version, so cursors retained beforehand never see it.
        """

        rope = self.rope.insert_at(self.position, value)
        if rope is None:
            raise AssertionError("Cursor insertion failed.")
        return MeasuredRopeCursor(rope, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> MeasuredRopeCursor[T, M]:
        """Insert every element of ``values`` at ``index``, in order. Splits and joins once
        regardless of how many are inserted.
        """

        materialized = list(values)
        if not materialized:
            return self
        rope = self.rope.insert_range(self.position, materialized)
        if rope is None:
            raise AssertionError("Cursor range insertion failed.")
        return MeasuredRopeCursor(rope, self.position + len(materialized))

    def delete_previous(self) -> MeasuredRopeCursor[T, M]:
        """Remove the element before the gap and return a cursor in its place, raising
        :class:`IndexError` at the start.
        """

        if self.is_at_start:
            raise IndexError("No element precedes the cursor.")
        rope = self.rope.remove_at(self.position - 1)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return MeasuredRopeCursor(rope, self.position - 1)

    def delete_next(self) -> MeasuredRopeCursor[T, M]:
        """Remove the element after the gap and return a cursor in its place, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        rope = self.rope.remove_at(self.position)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return MeasuredRopeCursor(rope, self.position)

    def replace_next(self, value: T) -> MeasuredRopeCursor[T, M]:
        """Replace the element after the gap, keeping the gap where it is, raising
        :class:`IndexError` at the end.
        """

        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        without_current = self.rope.remove_at(self.position)
        if without_current is None:
            raise AssertionError("Cursor replacement removal failed.")
        rope = without_current.insert_at(self.position, value)
        if rope is None:
            raise AssertionError("Cursor replacement failed.")
        return MeasuredRopeCursor(rope, self.position)

    def snapshot(self) -> MeasuredRope[T, M]:
        """The rope version this cursor is positioned in."""

        return self.rope


class TextRopeCursor:
    """An immutable gap cursor over one :class:`TextRope` version, tracking line and column."""

    __slots__ = ("_snapshot", "cursor")

    def __init__(
        self, cursor: MeasuredRopeCursor[str, int], snapshot: TextRope | None = None
    ) -> None:
        """Wrap an already-built representation; use the factory methods instead."""

        self.cursor = cursor
        self._snapshot = snapshot

    @property
    def count(self) -> int:
        """Number of characters in the rope version this cursor is positioned in."""

        return self.cursor.count

    @property
    def position(self) -> int:
        """The cursor's character offset in ``0..len``."""

        return self.cursor.position

    @property
    def line(self) -> int:
        """The zero-based line the gap sits on."""

        return self.cursor.measure_before

    @property
    def column(self) -> int:
        """The zero-based column the gap sits at within its line."""

        start = self.snapshot().line_start_offset(self.line)
        if start is None:
            raise AssertionError("Cursor line had no start.")
        return self.position - start

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first character."""

        return self.cursor.is_at_start

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last character."""

        return self.cursor.is_at_end

    def peek_previous(self) -> str | None:
        """The character immediately before the gap, or ``None`` at the start."""

        return self.cursor.peek_previous()

    def peek_next(self) -> str | None:
        """The character immediately after the gap, or ``None`` at the end."""

        return self.cursor.peek_next()

    def move_previous(self) -> TextRopeCursor:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        return TextRopeCursor(self.cursor.move_previous(), self._snapshot)

    def move_next(self) -> TextRopeCursor:
        """A cursor one position later, raising :class:`IndexError` at the end. The receiver is
        unchanged.
        """

        return TextRopeCursor(self.cursor.move_next(), self._snapshot)

    def seek(self, position: int) -> TextRopeCursor:
        """A cursor at ``position`` within the same rope version, raising
        :class:`IndexError` when it is out of range.
        """

        return (
            self
            if position == self.position
            else TextRopeCursor(self.cursor.seek(position), self._snapshot)
        )

    def seek_line_column(self, line: int, column: int) -> TextRopeCursor:
        """A cursor at the given zero-based line and column, raising :class:`ValueError` when
        that position does not exist.
        """

        offset = self.snapshot().offset_of(line, column)
        if offset is None:
            raise ValueError("Invalid line/column.")
        return self.seek(offset)

    def insert(self, text: str) -> TextRopeCursor:
        """Insert ``text`` at the gap and return a cursor positioned after it. The receiver keeps
        its own version, so cursors retained beforehand never see it.
        """

        # Empty text is an identity no-op in the measured cursor; returning this receiver preserves
        # the memoized snapshot instead of rewrapping the same measured cursor in a fresh facade.
        inserted = self.cursor.insert_range(text)
        return self if inserted is self.cursor else TextRopeCursor(inserted)

    def delete_previous(self) -> TextRopeCursor:
        """Remove the character before the gap and return a cursor in its place, raising
        :class:`IndexError` at the start.
        """

        return TextRopeCursor(self.cursor.delete_previous())

    def delete_next(self) -> TextRopeCursor:
        """Remove the character after the gap and return a cursor in its place, raising
        :class:`IndexError` at the end.
        """

        return TextRopeCursor(self.cursor.delete_next())

    def replace_next(self, value: str) -> TextRopeCursor:
        """Replace the character after the gap, keeping the gap where it is, raising
        :class:`IndexError` at the end.
        """

        if len(value) != 1:
            raise ValueError("Replacement must contain one Unicode code point.")
        return TextRopeCursor(self.cursor.replace_next(value))

    def search_line_column(self, line: int, column: int) -> TextRopeCursorSearch:
        """Seek to a line and column, reporting whether that position exists."""

        offset = self.snapshot().offset_of(line, column)
        return TextRopeCursorSearch(
            self if offset is None else self.seek(offset), offset is not None
        )

    def snapshot(self) -> TextRope:
        """The rope version this cursor is positioned in."""

        if self._snapshot is None:
            self._snapshot = TextRope(self.cursor.snapshot())
        return self._snapshot


__all__ = [
    "LineColumn",
    "MeasuredRope",
    "MeasuredRopeCursor",
    "MeasuredRopeCursorSearch",
    "MeasuredRopeLocate",
    "MeasuredRopeSplit",
    "NewlineMeasure",
    "Rope",
    "RopeBuilder",
    "RopeCursor",
    "RopeCursorPeek",
    "TextRope",
    "TextRopeCursor",
    "TextRopeCursorSearch",
]
