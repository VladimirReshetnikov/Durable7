"""A non-negative arbitrary-precision integer with sparse-integer semantics."""

from __future__ import annotations

import re
from typing import Self


class SparseInteger:
    """Immutable non-negative arbitrary-precision integer."""

    __slots__ = ("_value",)

    def __init__(self, value: int | str = 0) -> None:
        parsed = int(value)
        if parsed < 0:
            raise ValueError("SparseInteger cannot be negative.")
        self._value = parsed

    @classmethod
    def parse(cls, text: str) -> Self:
        if re.fullmatch(r"\s*\+?\d+\s*", text) is None:
            raise ValueError("Invalid SparseInteger.")
        return cls(text.strip().removeprefix("+"))

    @classmethod
    def from_int(cls, value: int) -> Self:
        return cls(value)

    @classmethod
    def power_of_two(cls, exponent: SparseInteger | int) -> Self:
        value = exponent._value if isinstance(exponent, SparseInteger) else int(exponent)
        if value < 0:
            raise ValueError("Exponent cannot be negative.")
        return cls(1 << value)

    @property
    def is_zero(self) -> bool:
        return self._value == 0

    def to_int(self) -> int:
        return self._value

    def compare_to(self, other: SparseInteger) -> int:
        return -1 if self._value < other._value else 1 if self._value > other._value else 0

    def add(self, other: SparseInteger) -> Self:
        return type(self)(self._value + other._value)

    def multiply(self, other: SparseInteger) -> Self:
        return type(self)(self._value * other._value)

    def exact_log2(self) -> Self:
        if self._value == 0 or self._value & (self._value - 1):
            raise ValueError("The value is not an exact power of two.")
        return type(self)(self._value.bit_length() - 1)

    def __int__(self) -> int:
        return self._value

    def __index__(self) -> int:
        return self._value

    def __str__(self) -> str:
        return str(self._value)

    def __repr__(self) -> str:
        return f"SparseInteger({self._value})"

    def __hash__(self) -> int:
        return hash(self._value)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, SparseInteger) and self._value == other._value

    def __lt__(self, other: SparseInteger) -> bool:
        return self._value < other._value

    def __le__(self, other: SparseInteger) -> bool:
        return self._value <= other._value

    def __gt__(self, other: SparseInteger) -> bool:
        return self._value > other._value

    def __ge__(self, other: SparseInteger) -> bool:
        return self._value >= other._value

    def __add__(self, other: SparseInteger) -> Self:
        return self.add(other)

    def __mul__(self, other: SparseInteger) -> Self:
        return self.multiply(other)


__all__ = ["SparseInteger"]
