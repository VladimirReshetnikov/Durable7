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
        return _BitSetSummary(
            left.chunk_count + right.chunk_count,
            left.pop_count + right.pop_count,
            right.last_word_index if right.last_word_index.has_value else left.last_word_index,
        )

    def measure(self, element: _BitSetChunk) -> _BitSetSummary:
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


class PersistentChunkedBitSet:
    """Immutable sparse bit set over the nonnegative signed-32-bit domain.

    Only nonzero 64-bit words are represented. Cached population and last-word summaries provide
    logarithmic point edits, inclusive rank, and zero-based select in represented words.
    """

    __slots__ = ("_chunks",)

    def __init__(self, chunks: MeasuredSequence[_BitSetChunk, _BitSetSummary]) -> None:
        self._chunks = chunks

    @classmethod
    def empty(cls) -> PersistentChunkedBitSet:
        return _EMPTY_BIT_SET

    @classmethod
    def from_values(cls, bit_indexes: Iterable[int]) -> PersistentChunkedBitSet:
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
        return self._chunks.measure.pop_count

    @property
    def chunk_count(self) -> int:
        return self._chunks.measure.chunk_count

    @property
    def is_empty(self) -> bool:
        return self._chunks.is_empty

    def __len__(self) -> int:
        return self.count

    def __bool__(self) -> bool:
        return not self.is_empty

    def contains(self, bit_index: int) -> bool:
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
        return self.contains(bit_index) if type(bit_index) is int else False

    def add(self, bit_index: int) -> PersistentChunkedBitSet:
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
        result = self.add(bit_index)
        return ChunkedBitSetUpdateResult(result is not self, result)

    def remove(self, bit_index: int) -> PersistentChunkedBitSet:
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
        selected = self.try_select(rank)
        if selected is None:
            raise IndexError("rank must identify a set bit.")
        return selected

    def try_select(self, rank: int) -> int | None:
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
        return self if other is self or other.is_empty else self._combine(other, "union")

    def intersect(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        return self if other is self else self._combine(other, "intersect")

    def except_(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        return self.empty() if other is self else self._combine(other, "except")

    def symmetric_except(self, other: PersistentChunkedBitSet) -> PersistentChunkedBitSet:
        return self.empty() if other is self else self._combine(other, "symmetric_except")

    def clear(self) -> PersistentChunkedBitSet:
        return self if self.is_empty else self.empty()

    def __iter__(self) -> Iterator[int]:
        for chunk in self._chunks:
            bits = chunk.bits
            while bits:
                lowest = bits & -bits
                yield (chunk.word_index << 6) + lowest.bit_length() - 1
                bits &= bits - 1

    def shares_storage_with(self, other: PersistentChunkedBitSet) -> bool:
        return self._chunks.shares_structure_with(other._chunks)

    def validate_structure(self) -> ChunkedBitSetStatistics:
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


def _require_bit_index(bit_index: int) -> None:
    if type(bit_index) is not int or bit_index < 0 or bit_index > _MAXIMUM_BIT_INDEX:
        raise ValueError("bit_index must be a nonnegative signed-32-bit integer.")


_EMPTY_BIT_SET = PersistentChunkedBitSet(MeasuredSequence.empty(_BIT_SET_MEASURE))

__all__ = [
    "ChunkedBitSetStatistics",
    "ChunkedBitSetUpdateResult",
    "PersistentChunkedBitSet",
]
