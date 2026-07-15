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
    __slots__ = ("_items",)

    def __init__(self, items: PersistentDeque[T]) -> None:
        self._items = items

    @classmethod
    def empty(cls) -> Rope[T]:
        return cls(PersistentDeque.empty())

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> Rope[T]:
        return cls(PersistentDeque.from_iterable(values))

    @classmethod
    def from_text(cls, text: str) -> Rope[str]:
        return Rope.from_iterable(text)

    def get_cursor(self, position: int = 0) -> RopeCursor[T]:
        return RopeCursor(self, position)

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    def front(self) -> T | None:
        return self._items.front()

    def back(self) -> T | None:
        return self._items.back()

    def get(self, index: int) -> T | None:
        return self._items.get(index)

    def copy_to(
        self,
        index: int,
        destination: MutableSequence[T],
        destination_index: int | None = None,
        count: int | None = None,
    ) -> bool:
        target = len(destination) if destination_index is None else destination_index
        length = len(self) - index if count is None else count
        sliced = self.slice(index, length)
        if sliced is None or target < 0 or target > len(destination):
            return False
        destination[target:target] = sliced.to_list()
        return True

    def push_front(self, value: T) -> Rope[T]:
        return Rope(self._items.prepend(value))

    def push_back(self, value: T) -> Rope[T]:
        return Rope(self._items.append(value))

    def set_item(self, index: int, value: T) -> Rope[T] | None:
        items = self._items.set_item(index, value)
        if items is None:
            return None
        return self if items is self._items else Rope(items)

    def insert_at(self, index: int, value: T) -> Rope[T] | None:
        items = self._items.insert_at(index, value)
        return None if items is None else Rope(items)

    def insert_range(self, index: int, values: Iterable[T]) -> Rope[T] | None:
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
        items = self._items.remove_at(index)
        return None if items is None else Rope(items)

    def remove_range(self, index: int, count: int) -> Rope[T] | None:
        split = self._items.split_range(index, count)
        if split is None:
            return None
        if count == 0:
            return self
        return Rope(split.before.concat(split.after))

    def slice(self, index: int, count: int) -> Rope[T] | None:
        split = self._items.split_range(index, count)
        if split is None:
            return None
        return self if index == 0 and count == len(self) else Rope(split.range)

    def split_at(self, index: int) -> tuple[Rope[T], Rope[T]] | None:
        split = self._items.split_at(index)
        return None if split is None else (Rope(split.left), Rope(split.right))

    def concat(self, other: Rope[T]) -> Rope[T]:
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return Rope(self._items.concat(other._items))

    def compact(self) -> Rope[T]:
        return Rope.from_iterable(self)

    def to_list(self) -> list[T]:
        return self._items.to_list()

    def shares_storage_with(self, other: Rope[T]) -> bool:
        return self._items.shares_storage_with(other._items)

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)


@dataclass(frozen=True, slots=True)
class MeasuredRopeLocate(Generic[T, M]):
    index: int
    measure_before: M
    value: T | None
    found: bool


@dataclass(frozen=True, slots=True)
class MeasuredRopeSplit(Generic[T, M]):
    left: MeasuredRope[T, M]
    right: MeasuredRope[T, M]


@dataclass(frozen=True, slots=True)
class RopeCursorPeek(Generic[T]):
    value: T


@dataclass(frozen=True, slots=True)
class MeasuredRopeCursorSearch(Generic[T, M]):
    cursor: MeasuredRopeCursor[T, M]
    found: bool


@dataclass(frozen=True, slots=True)
class TextRopeCursorSearch:
    cursor: TextRopeCursor
    found: bool


class MeasuredRope(Generic[T, M]):
    __slots__ = ("_items", "policy")

    def __init__(self, items: FingerTree[T, M], policy: MeasurePolicy[T, M]) -> None:
        self._items = items
        self.policy = policy

    @classmethod
    def empty(cls, policy: MeasurePolicy[T, M]) -> MeasuredRope[T, M]:
        return cls(FingerTree.empty(policy), policy)

    @classmethod
    def from_iterable(cls, values: Iterable[T], policy: MeasurePolicy[T, M]) -> MeasuredRope[T, M]:
        return cls(FingerTree.from_iterable(values, policy), policy)

    def get_cursor(self, position: int = 0) -> MeasuredRopeCursor[T, M]:
        return MeasuredRopeCursor(self, position)

    def get_cursor_by_measure(
        self, predicate: Callable[[M], bool]
    ) -> MeasuredRopeCursor[T, M] | None:
        located = self.locate_by_measure(predicate)
        return MeasuredRopeCursor(self, located.index) if located.found else None

    def cursor_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeCursorSearch[T, M]:
        located = self.locate_by_measure(predicate)
        return MeasuredRopeCursorSearch(
            MeasuredRopeCursor(self, located.index if located.found else len(self)), located.found
        )

    def __len__(self) -> int:
        return len(self._items)

    @property
    def is_empty(self) -> bool:
        return self._items.is_empty

    @property
    def measure(self) -> M:
        return self._items.measure

    def front(self) -> T | None:
        return self._items.front()

    def back(self) -> T | None:
        return self._items.back()

    def get(self, index: int) -> T | None:
        return self._items.get(index)

    def prefix_measure(self, count: int) -> M | None:
        return self._items.prefix_measure(count)

    def copy_to(
        self,
        index: int,
        destination: MutableSequence[T],
        destination_index: int | None = None,
        count: int | None = None,
    ) -> bool:
        target = len(destination) if destination_index is None else destination_index
        length = len(self) - index if count is None else count
        sliced = self.slice(index, length)
        if sliced is None or target < 0 or target > len(destination):
            return False
        destination[target:target] = list(sliced)
        return True

    def push_front(self, value: T) -> MeasuredRope[T, M]:
        return MeasuredRope(self._items.prepend(value), self.policy)

    def push_back(self, value: T) -> MeasuredRope[T, M]:
        return MeasuredRope(self._items.append(value), self.policy)

    def set_item(self, index: int, value: T) -> MeasuredRope[T, M] | None:
        items = self._items.set_item(index, value)
        return None if items is None else MeasuredRope(items, self.policy)

    def insert_at(self, index: int, value: T) -> MeasuredRope[T, M] | None:
        items = self._items.insert_at(index, value)
        return None if items is None else MeasuredRope(items, self.policy)

    def insert_range(self, index: int, values: Iterable[T]) -> MeasuredRope[T, M] | None:
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
        items = self._items.remove_at(index)
        return None if items is None else MeasuredRope(items, self.policy)

    def remove_range(self, index: int, count: int) -> MeasuredRope[T, M] | None:
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
        split = self._items.split(predicate)
        return MeasuredRopeSplit(
            MeasuredRope(split.left, self.policy), MeasuredRope(split.right, self.policy)
        )

    def locate_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeLocate[T, M]:
        result = self._items.try_locate(predicate)
        return MeasuredRopeLocate(result.index, result.measure_before, result.item, result.found)

    def concat(self, other: MeasuredRope[T, M]) -> MeasuredRope[T, M]:
        if self.policy is not other.policy:
            raise TypeError("Ropes must retain the same measure policy object.")
        if self.is_empty:
            return other
        if other.is_empty:
            return self
        return MeasuredRope(self._items.concat(other._items), self.policy)

    def compact(self) -> MeasuredRope[T, M]:
        return MeasuredRope.from_iterable(self, self.policy)

    def to_list(self) -> list[T]:
        return list(self)

    def shares_storage_with(self, other: MeasuredRope[T, M]) -> bool:
        return self._items.shares_storage_with(other._items)

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)


class NewlineMeasure:
    identity = 0

    def combine(self, left: int, right: int) -> int:
        return left + right

    def measure(self, element: str) -> int:
        return 1 if element == "\n" else 0


_NEWLINE_MEASURE = NewlineMeasure()


@dataclass(frozen=True, slots=True)
class LineColumn:
    line: int
    column: int


class TextRope:
    """Unicode-code-point text rope with line navigation."""

    __slots__ = ("_characters",)

    def __init__(self, characters: MeasuredRope[str, int]) -> None:
        self._characters = characters

    @classmethod
    def empty(cls) -> TextRope:
        return cls(MeasuredRope.empty(_NEWLINE_MEASURE))

    @classmethod
    def from_text(cls, text: str) -> TextRope:
        return cls(MeasuredRope.from_iterable(text, _NEWLINE_MEASURE))

    @classmethod
    def from_characters(cls, characters: Iterable[str]) -> TextRope:
        materialized = list(characters)
        if any(len(character) != 1 for character in materialized):
            raise ValueError("TextRope characters must each be one Unicode code point.")
        return cls(MeasuredRope.from_iterable(materialized, _NEWLINE_MEASURE))

    def get_cursor(self, position: int = 0) -> TextRopeCursor:
        return TextRopeCursor(self._characters.get_cursor(position), self)

    def __len__(self) -> int:
        return len(self._characters)

    @property
    def is_empty(self) -> bool:
        return self._characters.is_empty

    def as_string(self) -> str:
        return "".join(self._characters)

    def line_count(self) -> int:
        return self._characters.measure + 1

    def line_of_offset(self, offset: int) -> int | None:
        return self._characters.prefix_measure(offset)

    def line_start_offset(self, line: int) -> int | None:
        if line < 0 or line >= self.line_count():
            return None
        if line == 0:
            return 0
        located = self._characters.locate_by_measure(lambda count: count >= line)
        return located.index + 1 if located.found else None

    def line_column_of(self, offset: int) -> LineColumn | None:
        line = self.line_of_offset(offset)
        if line is None:
            return None
        start = self.line_start_offset(line)
        if start is None:
            raise AssertionError("Valid line had no start.")
        return LineColumn(line, offset - start)

    def offset_of(self, line: int, column: int) -> int | None:
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
        return [cast(str, self.get_line(line)) for line in range(self.line_count())]

    def to_rope(self) -> Rope[str]:
        return Rope.from_iterable(self._characters)

    def to_measured_rope(self) -> MeasuredRope[str, int]:
        return self._characters

    def __iter__(self) -> Iterator[str]:
        return iter(self._characters)


class RopeBuilder:
    def __init__(self) -> None:
        self._parts: list[str] = []
        self._size = 0

    def __len__(self) -> int:
        return self._size

    @property
    def is_empty(self) -> bool:
        return self._size == 0

    def as_string(self) -> str:
        return "".join(self._parts)

    def append(self, text: str) -> RopeBuilder:
        self._parts.append(text)
        self._size += len(text)
        return self

    def append_char(self, value: str) -> RopeBuilder:
        if len(value) != 1:
            raise ValueError("append_char requires one Unicode code point.")
        return self.append(value)

    def append_line(self, text: str = "") -> RopeBuilder:
        return self.append(text).append("\n")

    def clear(self) -> RopeBuilder:
        self._parts.clear()
        self._size = 0
        return self

    def to_rope(self) -> Rope[str]:
        return Rope.from_text(self.as_string())

    def to_text_rope(self) -> TextRope:
        return TextRope.from_text(self.as_string())


@dataclass(frozen=True, slots=True)
class RopeCursor(Generic[T]):
    rope: Rope[T]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > len(self.rope):
            raise ValueError("Cursor position is outside the rope.")

    @property
    def count(self) -> int:
        return len(self.rope)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> T | None:
        return None if self.is_at_start else self.rope.get(self.position - 1)

    def peek_next(self) -> T | None:
        return None if self.is_at_end else self.rope.get(self.position)

    def peek_previous_entry(self) -> RopeCursorPeek[T] | None:
        return (
            None if self.is_at_start else RopeCursorPeek(cast(T, self.rope.get(self.position - 1)))
        )

    def peek_next_entry(self) -> RopeCursorPeek[T] | None:
        return None if self.is_at_end else RopeCursorPeek(cast(T, self.rope.get(self.position)))

    def move_previous(self) -> RopeCursor[T]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return RopeCursor(self.rope, self.position - 1)

    def move_next(self) -> RopeCursor[T]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return RopeCursor(self.rope, self.position + 1)

    def seek(self, position: int) -> RopeCursor[T]:
        return self if position == self.position else RopeCursor(self.rope, position)

    def insert(self, value: T) -> RopeCursor[T]:
        rope = self.rope.insert_at(self.position, value)
        if rope is None:
            raise AssertionError("Cursor insertion failed.")
        return RopeCursor(rope, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> RopeCursor[T]:
        materialized = list(values)
        if not materialized:
            return self
        rope = self.rope.insert_range(self.position, materialized)
        if rope is None:
            raise AssertionError("Cursor range insertion failed.")
        return RopeCursor(rope, self.position + len(materialized))

    def delete_previous(self) -> RopeCursor[T]:
        if self.is_at_start:
            raise IndexError("No element precedes the cursor.")
        rope = self.rope.remove_at(self.position - 1)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return RopeCursor(rope, self.position - 1)

    def delete_next(self) -> RopeCursor[T]:
        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        rope = self.rope.remove_at(self.position)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return RopeCursor(rope, self.position)

    def replace_next(self, value: T) -> RopeCursor[T]:
        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        rope = self.rope.set_item(self.position, value)
        if rope is None:
            raise AssertionError("Cursor replacement failed.")
        return RopeCursor(rope, self.position)

    def snapshot(self) -> Rope[T]:
        return self.rope


@dataclass(frozen=True, slots=True)
class MeasuredRopeCursor(Generic[T, M]):
    rope: MeasuredRope[T, M]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > len(self.rope):
            raise ValueError("Cursor position is outside the rope.")

    @property
    def count(self) -> int:
        return len(self.rope)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    @property
    def measure_before(self) -> M:
        return cast(M, self.rope.prefix_measure(self.position))

    @property
    def measure_after(self) -> M:
        sliced = self.rope.slice(self.position, self.count - self.position)
        if sliced is None:
            raise AssertionError("Cursor suffix slice failed.")
        return sliced.measure

    def peek_previous(self) -> T | None:
        return None if self.is_at_start else self.rope.get(self.position - 1)

    def peek_next(self) -> T | None:
        return None if self.is_at_end else self.rope.get(self.position)

    def move_previous(self) -> MeasuredRopeCursor[T, M]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return MeasuredRopeCursor(self.rope, self.position - 1)

    def move_next(self) -> MeasuredRopeCursor[T, M]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return MeasuredRopeCursor(self.rope, self.position + 1)

    def seek(self, position: int) -> MeasuredRopeCursor[T, M]:
        return self if position == self.position else MeasuredRopeCursor(self.rope, position)

    def seek_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeCursor[T, M] | None:
        located = self.rope.locate_by_measure(predicate)
        return MeasuredRopeCursor(self.rope, located.index) if located.found else None

    def search_by_measure(self, predicate: Callable[[M], bool]) -> MeasuredRopeCursorSearch[T, M]:
        located = self.rope.locate_by_measure(predicate)
        return MeasuredRopeCursorSearch(
            self.seek(located.index if located.found else self.count), located.found
        )

    def insert(self, value: T) -> MeasuredRopeCursor[T, M]:
        rope = self.rope.insert_at(self.position, value)
        if rope is None:
            raise AssertionError("Cursor insertion failed.")
        return MeasuredRopeCursor(rope, self.position + 1)

    def insert_range(self, values: Iterable[T]) -> MeasuredRopeCursor[T, M]:
        materialized = list(values)
        if not materialized:
            return self
        rope = self.rope.insert_range(self.position, materialized)
        if rope is None:
            raise AssertionError("Cursor range insertion failed.")
        return MeasuredRopeCursor(rope, self.position + len(materialized))

    def delete_previous(self) -> MeasuredRopeCursor[T, M]:
        if self.is_at_start:
            raise IndexError("No element precedes the cursor.")
        rope = self.rope.remove_at(self.position - 1)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return MeasuredRopeCursor(rope, self.position - 1)

    def delete_next(self) -> MeasuredRopeCursor[T, M]:
        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        rope = self.rope.remove_at(self.position)
        if rope is None:
            raise AssertionError("Cursor deletion failed.")
        return MeasuredRopeCursor(rope, self.position)

    def replace_next(self, value: T) -> MeasuredRopeCursor[T, M]:
        if self.is_at_end:
            raise IndexError("No element follows the cursor.")
        rope = self.rope.set_item(self.position, value)
        if rope is None:
            raise AssertionError("Cursor replacement failed.")
        return MeasuredRopeCursor(rope, self.position)

    def snapshot(self) -> MeasuredRope[T, M]:
        return self.rope


class TextRopeCursor:
    __slots__ = ("_snapshot", "cursor")

    def __init__(
        self, cursor: MeasuredRopeCursor[str, int], snapshot: TextRope | None = None
    ) -> None:
        self.cursor = cursor
        self._snapshot = snapshot

    @property
    def count(self) -> int:
        return self.cursor.count

    @property
    def position(self) -> int:
        return self.cursor.position

    @property
    def line(self) -> int:
        return self.cursor.measure_before

    @property
    def column(self) -> int:
        start = self.snapshot().line_start_offset(self.line)
        if start is None:
            raise AssertionError("Cursor line had no start.")
        return self.position - start

    @property
    def is_at_start(self) -> bool:
        return self.cursor.is_at_start

    @property
    def is_at_end(self) -> bool:
        return self.cursor.is_at_end

    def peek_previous(self) -> str | None:
        return self.cursor.peek_previous()

    def peek_next(self) -> str | None:
        return self.cursor.peek_next()

    def move_previous(self) -> TextRopeCursor:
        return TextRopeCursor(self.cursor.move_previous(), self._snapshot)

    def move_next(self) -> TextRopeCursor:
        return TextRopeCursor(self.cursor.move_next(), self._snapshot)

    def seek(self, position: int) -> TextRopeCursor:
        return (
            self
            if position == self.position
            else TextRopeCursor(self.cursor.seek(position), self._snapshot)
        )

    def seek_line_column(self, line: int, column: int) -> TextRopeCursor:
        offset = self.snapshot().offset_of(line, column)
        if offset is None:
            raise ValueError("Invalid line/column.")
        return self.seek(offset)

    def insert(self, text: str) -> TextRopeCursor:
        return TextRopeCursor(self.cursor.insert_range(text))

    def delete_previous(self) -> TextRopeCursor:
        return TextRopeCursor(self.cursor.delete_previous())

    def delete_next(self) -> TextRopeCursor:
        return TextRopeCursor(self.cursor.delete_next())

    def replace_next(self, value: str) -> TextRopeCursor:
        if len(value) != 1:
            raise ValueError("Replacement must contain one Unicode code point.")
        return TextRopeCursor(self.cursor.replace_next(value))

    def search_line_column(self, line: int, column: int) -> TextRopeCursorSearch:
        offset = self.snapshot().offset_of(line, column)
        return TextRopeCursorSearch(
            self if offset is None else self.seek(offset), offset is not None
        )

    def snapshot(self) -> TextRope:
        if self._snapshot is None:
            self._snapshot = TextRope.from_characters(self.cursor.snapshot())
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
