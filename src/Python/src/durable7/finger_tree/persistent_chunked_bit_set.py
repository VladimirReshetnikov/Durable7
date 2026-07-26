"""Sparse persistent bit set over the shared measured sequence."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Literal

from .measured_sequence import MeasuredSequence, SequenceLocate
from .measures import OptionalValue

_MAXIMUM_BIT_INDEX = (1 << 31) - 1
_WORD_MASK = (1 << 64) - 1


@dataclass(frozen=True, slots=True)
class _BitSetChunk:
    word_index: int
    bits: int


@dataclass(frozen=True, slots=True)
class _BitSetSummary:
    chunk_count: int
    pop_count: int
    last_word_index: OptionalValue[int]


class _BitSetMeasure:
    identity = _BitSetSummary(0, 0, OptionalValue.none())

    def combine(self, left: _BitSetSummary, right: _BitSetSummary) -> _BitSetSummary:
        """Add two subtree summaries: chunk counts and population counts sum, and the rightmost word
        index wins.
        """

        return _BitSetSummary(
            left.chunk_count + right.chunk_count,
            left.pop_count + right.pop_count,
            right.last_word_index if right.last_word_index.has_value else left.last_word_index,
        )

    def measure(self, element: _BitSetChunk) -> _BitSetSummary:
        """Summarize a single chunk: one word, its population count, and its index."""

        return _BitSetSummary(1, element.bits.bit_count(), OptionalValue.some(element.word_index))


_BIT_SET_MEASURE = _BitSetMeasure()
_Operation = Literal["union", "intersect", "except", "symmetric_except"]


@dataclass(frozen=True, slots=True)
class ChunkedBitSetUpdateResult:
    """Nonthrowing point-update result."""

    changed: bool
    set: PersistentChunkedBitSet


@dataclass(frozen=True, slots=True)
class ChunkedBitSetStatistics:
    """Validated sparse-word statistics."""

    chunk_count: int
    pop_count: int


@dataclass(frozen=True, slots=True)
class ChunkedBitSetCursorPeek:
    """A set bit found next to a cursor gap.

    Wrapping the index keeps "there is a bit here, index 0" distinct from "nothing here".
    """

    value: int


@dataclass(frozen=True, slots=True)
class ChunkedBitSetCursorSearch:
    """The outcome of seeking a cursor to a bit index.

    On a miss the cursor still sits before the next set bit, so it remains usable.
    """

    found: bool
    cursor: PersistentChunkedBitSetCursor


class PersistentChunkedBitSet:
    """Immutable sparse bit set over the nonnegative signed-32-bit domain.

    Only nonzero 64-bit words are represented. Cached population and last-word summaries provide
    logarithmic point edits, inclusive rank, and zero-based select in represented words.
    """

    __slots__ = ("_chunks",)

    def __init__(self, chunks: MeasuredSequence[_BitSetChunk, _BitSetSummary]) -> None:
        """Wrap an already-built chunk sequence; use :meth:`empty` or :meth:`from_values` instead.
        """

        self._chunks = chunks

    @classmethod
    def empty(cls) -> PersistentChunkedBitSet:
        """Return the empty bit set."""

        return _EMPTY_BIT_SET

    @classmethod
    def from_values(cls, bit_indexes: Iterable[int]) -> PersistentChunkedBitSet:
        """Build a bit set with every listed index set, sorting and deduplicating them in one pass.
        """

        if bit_indexes is None:
            raise TypeError("bit_indexes must be iterable.")
        words: dict[int, int] = {}
        for bit_index in bit_indexes:
            _require_bit_index(bit_index)
            word_index = bit_index >> 6
            words[word_index] = words.get(word_index, 0) | (1 << (bit_index & 63))
        if not words:
            return cls.empty()
        chunks = [_BitSetChunk(index, words[index]) for index in sorted(words)]
        return cls(MeasuredSequence.from_iterable(chunks, _BIT_SET_MEASURE))

    @property
    def count(self) -> int:
        """Number of set bits, read from the cached population count rather than recounted."""

        return self._chunks.measure.pop_count

    @property
    def chunk_count(self) -> int:
        """Number of stored words, that is, how many 64-bit blocks contain at least one set bit.

        A measure of representation size rather than of contents.
        """

        return self._chunks.measure.chunk_count

    @property
    def is_empty(self) -> bool:
        """Whether no bit is set."""

        return self._chunks.is_empty

    def __len__(self) -> int:
        """Number of set bits, matching :attr:`count`."""

        return self.count

    def __bool__(self) -> bool:
        """Whether at least one bit is set."""

        return not self.is_empty

    def contains(self, bit_index: int) -> bool:
        """Whether ``bit_index`` is set.

        An index outside the nonnegative signed-32-bit domain is simply absent rather than an error,
        so membership tests never raise.
        """

        if type(bit_index) is not int or bit_index < 0 or bit_index > _MAXIMUM_BIT_INDEX:
            return False
        word_index = bit_index >> 6
        located = self._locate_word(word_index)
        return bool(
            located.found
            and located.value is not None
            and located.value.word_index == word_index
            and located.value.bits & (1 << (bit_index & 63))
        )

    def __contains__(self, bit_index: object) -> bool:
        """Whether ``bit_index`` is set, for the ``in`` operator."""

        return self.contains(bit_index) if type(bit_index) is int else False

    def add(self, bit_index: int) -> PersistentChunkedBitSet:
        """Return a bit set with ``bit_index`` set.

        Setting an already set bit returns the receiver. An index outside the nonnegative
        signed-32-bit domain raises, since silently dropping a write would be worse than failing.
        """

        _require_bit_index(bit_index)
        word_index = bit_index >> 6
        bit = 1 << (bit_index & 63)
        located = self._locate_word(word_index)
        if located.found and located.value is not None and located.value.word_index == word_index:
            bits = located.value.bits | bit
            if bits == located.value.bits:
                return self
            chunks = self._chunks.set_at(located.index, _BitSetChunk(word_index, bits))
        else:
            chunks = self._chunks.insert_at(located.index, _BitSetChunk(word_index, bit))
        if chunks is None:
            raise AssertionError("A validated bit-set edit failed.")
        return PersistentChunkedBitSet(chunks)

    def try_add(self, bit_index: int) -> ChunkedBitSetUpdateResult:
        """Set ``bit_index``, reporting whether it had been clear."""

        result = self.add(bit_index)
        return ChunkedBitSetUpdateResult(result is not self, result)

    def remove(self, bit_index: int) -> PersistentChunkedBitSet:
        """Return a bit set with ``bit_index`` clear.

        Clearing an unset or out-of-domain index returns the receiver, and a word emptied by the
        removal is dropped so the representation stays sparse.
        """

        if type(bit_index) is not int or bit_index < 0 or bit_index > _MAXIMUM_BIT_INDEX:
            return self
        word_index = bit_index >> 6
        bit = 1 << (bit_index & 63)
        located = self._locate_word(word_index)
        if (
            not located.found
            or located.value is None
            or located.value.word_index != word_index
            or not located.value.bits & bit
        ):
            return self
        bits = located.value.bits & ~bit
        chunks = (
            self._chunks.remove_at(located.index)
            if bits == 0
            else self._chunks.set_at(located.index, _BitSetChunk(word_index, bits))
        )
        if chunks is None:
            raise AssertionError("A validated bit-set removal failed.")
        return PersistentChunkedBitSet(chunks)

    def try_remove(self, bit_index: int) -> ChunkedBitSetUpdateResult:
        """Clear ``bit_index``, reporting whether it had been set."""

        result = self.remove(bit_index)
        return ChunkedBitSetUpdateResult(result is not self, result)

    def rank(self, bit_index: int) -> int:
        """Count set indexes less than or equal to *bit_index*."""

        if type(bit_index) is not int:
            raise TypeError("bit_index must be an integer.")
        if bit_index < 0:
            return 0
        if bit_index >= _MAXIMUM_BIT_INDEX:
            return self.count
        word_index = bit_index >> 6
        located = self._locate_word(word_index)
        if not located.found or located.value is None or located.value.word_index != word_index:
            return located.measure_before.pop_count
        offset = bit_index & 63
        mask = _WORD_MASK if offset == 63 else (1 << (offset + 1)) - 1
        return located.measure_before.pop_count + (located.value.bits & mask).bit_count()

    def select(self, rank: int) -> int:
        """The bit index at zero-based population ``rank``, raising :class:`IndexError` when out of
        range.
        """

        selected = self.try_select(rank)
        if selected is None:
            raise IndexError("rank must identify a set bit.")
        return selected

    def try_select(self, rank: int) -> int | None:
        """The bit index at zero-based population ``rank``, or ``None`` when out of range.

        Descends by cached population counts to the right word, then clears low bits to reach the
        requested one, so it does not scan the set.
        """

        if type(rank) is not int or rank < 0 or rank >= self.count:
            return None
        located = self._chunks.locate(lambda summary: summary.pop_count > rank)
        if not located.found or located.value is None:
            return None
        bits = located.value.bits
        for _ in range(rank - located.measure_before.pop_count):
            bits &= bits - 1
        offset = (bits & -bits).bit_length() - 1
        return (located.value.word_index << 6) + offset

    def union(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        """Return the bits set in either operand.

        Merges word by word rather than bit by bit, so the cost follows the number of stored words
        and not the index range.
        """

        return self if other is self or other.is_empty else self._combine(other, "union")

    def intersect(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        """Return the bits set in both operands. Wordwise, as for :meth:`union`."""

        return self if other is self else self._combine(other, "intersect")

    def except_(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        """Return this set's bits that are clear in ``other``. Wordwise, as for :meth:`union`.

        Named with a trailing underscore because ``except`` is a Python keyword.
        """

        return self.empty() if other is self else self._combine(other, "except")

    def symmetric_except(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        """Return the bits set in exactly one operand. Wordwise, as for :meth:`union`."""

        return self.empty() if other is self else self._combine(other, "symmetric_except")

    def clear(self) -> PersistentChunkedBitSet:
        """Return the empty bit set; a no-op when already empty."""

        return self if self.is_empty else self.empty()

    def cursor_at(self, position: int = 0) -> PersistentChunkedBitSetCursor:
        """A cursor at gap ``position`` of the ascending set-bit sequence."""

        return PersistentChunkedBitSetCursor(self, position)

    def cursor_at_or_after(self, bit_index: int) -> PersistentChunkedBitSetCursor:
        """A cursor positioned before the first set bit at or after ``bit_index``."""

        return self.cursor_at(0 if bit_index <= 0 else self.rank(bit_index - 1))

    def find_cursor(self, bit_index: int) -> ChunkedBitSetCursorSearch:
        """Seek to ``bit_index`` and report whether it is set.

        On a miss the cursor sits before the next set bit, so it remains usable.
        """

        cursor = self.cursor_at_or_after(bit_index)
        candidate = cursor.peek_next()
        return ChunkedBitSetCursorSearch(
            bit_index >= 0 and candidate is not None and candidate.value == bit_index, cursor
        )

    def __iter__(self) -> Iterator[int]:
        """Iterate the set bit indexes in ascending order."""

        for chunk in self._chunks:
            bits = chunk.bits
            while bits:
                lowest = bits & -bits
                yield (chunk.word_index << 6) + lowest.bit_length() - 1
                bits &= bits - 1

    def shares_storage_with(self, other: PersistentChunkedBitSet) -> bool:
        """Whether both sets reference the same chunk sequence.

        A representation test used to confirm that a no-op avoided copying, not an equality test.
        """

        return self._chunks.shares_structure_with(other._chunks)

    def validate_structure(self) -> ChunkedBitSetStatistics:
        """Check the stored words against their cached measure and return the recounted statistics.

        Verifies strictly ascending word indexes, that no all-zero word is retained, and that the
        cached population count matches the bits actually set. A defensive audit; ordinary
        operations maintain these invariants.
        """

        chunk_count = 0
        pop_count = 0
        previous = -1
        for chunk in self._chunks:
            if chunk.word_index <= previous or chunk.bits <= 0 or chunk.bits > _WORD_MASK:
                raise ValueError("The chunked bit set has invalid word order or contents.")
            previous = chunk.word_index
            chunk_count += 1
            pop_count += chunk.bits.bit_count()
        summary = self._chunks.measure
        expected_last = OptionalValue.none() if chunk_count == 0 else OptionalValue.some(previous)
        if (
            summary.chunk_count != chunk_count
            or summary.pop_count != pop_count
            or summary.last_word_index != expected_last
        ):
            raise ValueError("The chunked bit-set annotations disagree with its words.")
        return ChunkedBitSetStatistics(chunk_count, pop_count)

    def _locate_word(self, word_index: int) -> SequenceLocate[_BitSetChunk, _BitSetSummary]:
        return self._chunks.locate(
            lambda summary: (
                summary.last_word_index.has_value and summary.last_word_index.unwrap() >= word_index
            )
        )

    def _combine(
        self, other: PersistentChunkedBitSet, operation: _Operation
    ) -> PersistentChunkedBitSet:
        left = self._chunks.to_list()
        right = other._chunks.to_list()
        result: list[_BitSetChunk] = []
        left_index = 0
        right_index = 0
        while left_index < len(left) or right_index < len(right):
            left_chunk = left[left_index] if left_index < len(left) else None
            right_chunk = right[right_index] if right_index < len(right) else None
            if right_chunk is None or (
                left_chunk is not None and left_chunk.word_index < right_chunk.word_index
            ):
                if operation in ("union", "except", "symmetric_except"):
                    if left_chunk is None:
                        raise AssertionError("The left word stream ended unexpectedly.")
                    result.append(left_chunk)
                left_index += 1
                continue
            if left_chunk is None or right_chunk.word_index < left_chunk.word_index:
                if operation in ("union", "symmetric_except"):
                    result.append(right_chunk)
                right_index += 1
                continue
            bits = (
                left_chunk.bits | right_chunk.bits
                if operation == "union"
                else left_chunk.bits & right_chunk.bits
                if operation == "intersect"
                else left_chunk.bits & ~right_chunk.bits
                if operation == "except"
                else left_chunk.bits ^ right_chunk.bits
            )
            if bits:
                result.append(_BitSetChunk(left_chunk.word_index, bits))
            left_index += 1
            right_index += 1
        if result == left:
            return self
        if not result:
            return self.empty()
        return PersistentChunkedBitSet(MeasuredSequence.from_iterable(result, _BIT_SET_MEASURE))


@dataclass(frozen=True, slots=True)
class PersistentChunkedBitSetCursor:
    """Immutable root-plus-population-rank cursor over present set bits."""

    set: PersistentChunkedBitSet
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        if self.position < 0 or self.position > self.set.count:
            raise IndexError("Cursor position is outside the chunked bit set.")

    @property
    def count(self) -> int:
        """Population count of the set version this cursor is positioned in."""

        return self.set.count

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the lowest set bit."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the highest set bit."""

        return self.position == self.count

    def peek_previous(self) -> ChunkedBitSetCursorPeek | None:
        """The set bit immediately before the gap, or ``None`` at the start."""

        return (
            None
            if self.is_at_start
            else ChunkedBitSetCursorPeek(self.set.select(self.position - 1))
        )

    def peek_next(self) -> ChunkedBitSetCursorPeek | None:
        """The set bit immediately after the gap, or ``None`` at the end."""

        return None if self.is_at_end else ChunkedBitSetCursorPeek(self.set.select(self.position))

    def move_previous(self) -> PersistentChunkedBitSetCursor:
        """A cursor one set bit earlier, raising :class:`IndexError` at the start.

        The receiver is unchanged; movement produces a new cursor over the same set version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PersistentChunkedBitSetCursor(self.set, self.position - 1)

    def move_next(self) -> PersistentChunkedBitSetCursor:
        """A cursor one set bit later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PersistentChunkedBitSetCursor(self.set, self.position + 1)

    def seek_rank(self, position: int) -> PersistentChunkedBitSetCursor:
        """A cursor at gap ``position`` within the same set version, raising when out of range."""

        return (
            self if position == self.position else PersistentChunkedBitSetCursor(self.set, position)
        )

    def add(self, bit_index: int) -> PersistentChunkedBitSetCursor:
        """Set ``bit_index`` and return a cursor positioned just after it.

        The gap moves to the bit's ascending position rather than staying where the receiver was,
        since a bit set's order is decided by index and not by the cursor.
        """

        updated = self.set.add(bit_index)
        if updated is self.set:
            return self
        position = 0 if bit_index == 0 else self.set.rank(bit_index - 1)
        return PersistentChunkedBitSetCursor(updated, position + 1)

    def delete_previous(self) -> PersistentChunkedBitSetCursor:
        """Clear the set bit before the gap and return a cursor in its place, raising at the start.
        """

        bit = self.peek_previous()
        if bit is None:
            raise IndexError("No set bit precedes the cursor.")
        return PersistentChunkedBitSetCursor(self.set.remove(bit.value), self.position - 1)

    def delete_next(self) -> PersistentChunkedBitSetCursor:
        """Clear the set bit after the gap and return a cursor in its place, raising at the end."""

        bit = self.peek_next()
        if bit is None:
            raise IndexError("No set bit follows the cursor.")
        return PersistentChunkedBitSetCursor(self.set.remove(bit.value), self.position)

    def snapshot(self) -> PersistentChunkedBitSet:
        """The set version this cursor is positioned in."""

        return self.set


def _require_bit_index(bit_index: int) -> None:
    if type(bit_index) is not int or bit_index < 0 or bit_index > _MAXIMUM_BIT_INDEX:
        raise ValueError("bit_index must be a nonnegative signed-32-bit integer.")


_EMPTY_BIT_SET = PersistentChunkedBitSet(MeasuredSequence.empty(_BIT_SET_MEASURE))

__all__ = [
    "ChunkedBitSetCursorPeek",
    "ChunkedBitSetCursorSearch",
    "ChunkedBitSetStatistics",
    "ChunkedBitSetUpdateResult",
    "PersistentChunkedBitSet",
    "PersistentChunkedBitSetCursor",
]
