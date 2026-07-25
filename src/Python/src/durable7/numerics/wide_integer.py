"""Fixed-width two's-complement integer values backed by Python ``int``."""

from __future__ import annotations

import re
from abc import ABC
from collections.abc import MutableSequence
from typing import ClassVar, Literal, Self

ByteOrder = Literal["little-endian", "big-endian"]


def _modulus(width: int) -> int:
    return 1 << width


def _normalize_bits(value: int, width: int) -> int:
    return value % _modulus(width)


def _truncating_quotient(left: int, right: int) -> int:
    quotient = abs(left) // abs(right)
    return -quotient if (left < 0) != (right < 0) else quotient


def _parse_integer(text: str, radix: int) -> int:
    stripped = text.strip()
    if not stripped:
        raise ValueError("The input is not an integer.")
    if radix == 10:
        if re.fullmatch(r"[+-]?\d+", stripped) is None:
            raise ValueError("The input is not a decimal integer.")
        return int(stripped, 10)
    if radix not in (2, 16):
        raise ValueError("Only radices 2, 10, and 16 are accepted when parsing.")
    negative = stripped.startswith("-")
    unsigned = stripped[1:] if stripped[:1] in "+-" else stripped
    pattern = r"[01]+" if radix == 2 else r"[0-9a-fA-F]+"
    if re.fullmatch(pattern, unsigned) is None:
        raise ValueError("The input is not valid for the requested radix.")
    value = int(unsigned, radix)
    return -value if negative else value


def _digits(value: int, radix: int) -> str:
    if radix < 2 or radix > 36:
        raise ValueError("Radix must be between 2 and 36.")
    if value == 0:
        return "0"
    alphabet = "0123456789abcdefghijklmnopqrstuvwxyz"
    negative = value < 0
    remaining = -value if negative else value
    result: list[str] = []
    while remaining:
        remaining, digit = divmod(remaining, radix)
        result.append(alphabet[digit])
    if negative:
        result.append("-")
    return "".join(reversed(result))


class FixedWidthInteger(ABC):
    """Common immutable surface for the six fixed-width integer types."""

    width: ClassVar[int]
    signed: ClassVar[bool]
    __slots__ = ("_bits",)

    def __init__(self, value: int | str = 0) -> None:
        self._bits = int(value)

    @classmethod
    def parse(cls, text: str, radix: int = 10) -> Self:
        value = _parse_integer(text, radix)
        explicit_negative = text.strip().startswith("-")
        minimum = -(1 << (cls.width - 1)) if cls.signed else 0
        maximum = (
            (1 << (cls.width - 1)) - 1
            if cls.signed and (radix == 10 or explicit_negative)
            else (1 << cls.width) - 1
        )
        if value < minimum or value > maximum:
            raise OverflowError(f"Value does not fit in {cls.width} bits.")
        return cls(value)

    @classmethod
    def from_bytes(
        cls, data: bytes | bytearray | memoryview, order: ByteOrder = "little-endian"
    ) -> Self:
        if order not in ("little-endian", "big-endian"):
            raise ValueError("Unknown byte order.")
        raw = bytes(data)
        expected = cls.width // 8
        if len(raw) != expected:
            raise ValueError(f"Expected exactly {expected} bytes.")
        return cls(
            int.from_bytes(
                raw, byteorder="little" if order == "little-endian" else "big", signed=cls.signed
            )
        )

    @classmethod
    def min_value(cls) -> Self:
        return cls(-(1 << (cls.width - 1)) if cls.signed else 0)

    @classmethod
    def max_value(cls) -> Self:
        return cls((1 << (cls.width - 1)) - 1 if cls.signed else (1 << cls.width) - 1)

    def _require_same_type(self, other: object) -> Self:
        if type(other) is not type(self):
            raise TypeError(f"Expected {type(self).__name__}, got {type(other).__name__}.")
        return other

    def to_int(self) -> int:
        bits = self.to_unsigned_int()
        if not self.signed:
            return bits
        sign = 1 << (self.width - 1)
        return bits if bits & sign == 0 else bits - _modulus(self.width)

    def to_unsigned_int(self) -> int:
        return _normalize_bits(self._bits, self.width)

    @property
    def is_zero(self) -> bool:
        return self.to_unsigned_int() == 0

    @property
    def is_negative(self) -> bool:
        return self.signed and self.to_int() < 0

    @property
    def is_even(self) -> bool:
        return self.to_unsigned_int() & 1 == 0

    @property
    def is_odd(self) -> bool:
        return not self.is_even

    @property
    def is_power_of_two(self) -> bool:
        value = self.to_int()
        return value > 0 and value & (value - 1) == 0

    def compare_to(self, other: Self) -> int:
        right = self._require_same_type(other).to_int()
        left = self.to_int()
        return -1 if left < right else 1 if left > right else 0

    def _create(self, value: int) -> Self:
        return type(self)(value)

    def add(self, other: Self) -> Self:
        return self._create(self.to_int() + self._require_same_type(other).to_int())

    def subtract(self, other: Self) -> Self:
        return self._create(self.to_int() - self._require_same_type(other).to_int())

    def multiply(self, other: Self) -> Self:
        return self._create(self.to_int() * self._require_same_type(other).to_int())

    def divide(self, other: Self) -> Self:
        divisor = self._require_same_type(other).to_int()
        if divisor == 0:
            raise ZeroDivisionError("Division by zero.")
        left = self.to_int()
        if self.signed and left == -(1 << (self.width - 1)) and divisor == -1:
            raise OverflowError("Signed division overflow.")
        return self._create(_truncating_quotient(left, divisor))

    def remainder(self, other: Self) -> Self:
        divisor = self._require_same_type(other).to_int()
        if divisor == 0:
            raise ZeroDivisionError("Division by zero.")
        left = self.to_int()
        if self.signed and left == -(1 << (self.width - 1)) and divisor == -1:
            raise OverflowError("Signed remainder overflow.")
        quotient = _truncating_quotient(left, divisor)
        return self._create(left - quotient * divisor)

    def div_rem(self, other: Self) -> tuple[Self, Self]:
        return self.divide(other), self.remainder(other)

    def negate(self) -> Self:
        return self._create(-self.to_int())

    def increment(self) -> Self:
        return self._create(self.to_int() + 1)

    def decrement(self) -> Self:
        return self._create(self.to_int() - 1)

    def abs(self) -> Self:
        value = self.to_int()
        if self.signed and value == -(1 << (self.width - 1)):
            raise OverflowError("Absolute value overflow.")
        return self._create(abs(value))

    def bitwise_and(self, other: Self) -> Self:
        return self._create(
            self.to_unsigned_int() & self._require_same_type(other).to_unsigned_int()
        )

    def bitwise_or(self, other: Self) -> Self:
        return self._create(
            self.to_unsigned_int() | self._require_same_type(other).to_unsigned_int()
        )

    def bitwise_xor(self, other: Self) -> Self:
        return self._create(
            self.to_unsigned_int() ^ self._require_same_type(other).to_unsigned_int()
        )

    def bitwise_not(self) -> Self:
        return self._create(~self.to_unsigned_int())

    def shift_left(self, count: int) -> Self:
        return self._create(self.to_unsigned_int() << (count % self.width))

    def shift_right(self, count: int) -> Self:
        shift = count % self.width
        return self._create((self.to_int() if self.signed else self.to_unsigned_int()) >> shift)

    def rotate_left(self, count: int) -> Self:
        shift = count % self.width
        bits = self.to_unsigned_int()
        if shift == 0:
            return self._create(bits)
        return self._create((bits << shift) | (bits >> (self.width - shift)))

    def rotate_right(self, count: int) -> Self:
        return self.rotate_left(-count)

    def checked(self, value: int) -> Self:
        minimum = -(1 << (self.width - 1)) if self.signed else 0
        maximum = (1 << (self.width - 1)) - 1 if self.signed else _modulus(self.width) - 1
        if value < minimum or value > maximum:
            raise OverflowError(f"Value does not fit in {self.width} bits.")
        return self._create(value)

    def checked_add(self, other: Self) -> Self:
        return self.checked(self.to_int() + self._require_same_type(other).to_int())

    def checked_subtract(self, other: Self) -> Self:
        return self.checked(self.to_int() - self._require_same_type(other).to_int())

    def checked_multiply(self, other: Self) -> Self:
        return self.checked(self.to_int() * self._require_same_type(other).to_int())

    def checked_negate(self) -> Self:
        return self.checked(-self.to_int())

    def shortest_bit_length(self) -> int:
        value = self.to_int()
        if value == 0:
            return 0
        if not self.signed:
            return value.bit_length()
        return value.bit_length() + 1 if value > 0 else (~value).bit_length() + 1

    def leading_zero_count(self) -> int:
        bits = self.to_unsigned_int()
        return self.width if bits == 0 else self.width - bits.bit_length()

    def trailing_zero_count(self) -> int:
        bits = self.to_unsigned_int()
        return self.width if bits == 0 else (bits & -bits).bit_length() - 1

    def pop_count(self) -> int:
        return self.to_unsigned_int().bit_count()

    def log2(self) -> int:
        value = self.to_int()
        if value < 0:
            raise ValueError("Log2 is undefined for negative values.")
        return 0 if value == 0 else value.bit_length() - 1

    def byte_count(self) -> int:
        return self.width // 8

    def to_bytes(self, order: ByteOrder = "little-endian") -> bytes:
        if order not in ("little-endian", "big-endian"):
            raise ValueError("Unknown byte order.")
        return self.to_unsigned_int().to_bytes(
            self.byte_count(), byteorder="little" if order == "little-endian" else "big"
        )

    def to_string(self, radix: int = 10) -> str:
        value = self.to_unsigned_int() if radix == 16 else self.to_int()
        return _digits(value, radix)

    def format(self, specifier: str = "G") -> str:
        match = re.fullmatch(r"([gGdDnNxX])(\d*)", specifier)
        if match is None:
            raise ValueError("Unsupported integer format.")
        code, precision_text = match.groups()
        precision = (2 if code in "Nn" else 0) if not precision_text else int(precision_text)
        if code in "Gg" and precision:
            raise ValueError("General integer format does not accept precision.")
        if code in "Xx":
            text = f"{self.to_unsigned_int():x}".rjust(precision, "0")
            return text.upper() if code == "X" else text
        value = self.to_int()
        digits = str(abs(value)).rjust(precision, "0")
        if code in "Nn":
            grouped = f"{int(digits):,}"
            suffix = "" if precision == 0 else "." + "0" * precision
            return ("-" if value < 0 else "") + grouped + suffix
        return ("-" if value < 0 else "") + digits

    def __int__(self) -> int:
        return self.to_int()

    def __index__(self) -> int:
        return self.to_int()

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.to_int()})"

    def __str__(self) -> str:
        return self.to_string()

    def __hash__(self) -> int:
        return hash((type(self), self.to_unsigned_int()))

    def __eq__(self, other: object) -> bool:
        return type(other) is type(self) and self.to_unsigned_int() == other.to_unsigned_int()

    def __lt__(self, other: Self) -> bool:
        return self.compare_to(other) < 0

    def __le__(self, other: Self) -> bool:
        return self.compare_to(other) <= 0

    def __gt__(self, other: Self) -> bool:
        return self.compare_to(other) > 0

    def __ge__(self, other: Self) -> bool:
        return self.compare_to(other) >= 0

    def __add__(self, other: Self) -> Self:
        return self.add(other)

    def __sub__(self, other: Self) -> Self:
        return self.subtract(other)

    def __mul__(self, other: Self) -> Self:
        return self.multiply(other)

    def __floordiv__(self, other: Self) -> Self:
        return self.divide(other)

    def __mod__(self, other: Self) -> Self:
        return self.remainder(other)

    def __divmod__(self, other: Self) -> tuple[Self, Self]:
        return self.div_rem(other)

    def __neg__(self) -> Self:
        return self.negate()

    def __abs__(self) -> Self:
        return self.abs()

    def __and__(self, other: Self) -> Self:
        return self.bitwise_and(other)

    def __or__(self, other: Self) -> Self:
        return self.bitwise_or(other)

    def __xor__(self, other: Self) -> Self:
        return self.bitwise_xor(other)

    def __invert__(self) -> Self:
        return self.bitwise_not()

    def __lshift__(self, count: int) -> Self:
        return self.shift_left(count)

    def __rshift__(self, count: int) -> Self:
        return self.shift_right(count)

    def __format__(self, format_spec: str) -> str:
        return self.format(format_spec or "G")


class UInt256(FixedWidthInteger):
    width = 256
    signed = False


class Int256(FixedWidthInteger):
    width = 256
    signed = True


class UInt512(FixedWidthInteger):
    width = 512
    signed = False


class Int512(FixedWidthInteger):
    width = 512
    signed = True


class UInt1024(FixedWidthInteger):
    width = 1024
    signed = False


class Int1024(FixedWidthInteger):
    width = 1024
    signed = True


WideInteger = UInt256 | Int256 | UInt512 | Int512 | UInt1024 | Int1024


class BitConverterEx:
    """Fixed-width byte conversion helpers."""

    @staticmethod
    def get_bytes(value: WideInteger, order: ByteOrder = "little-endian") -> bytes:
        return value.to_bytes(order)

    @staticmethod
    def try_write_bytes(
        value: WideInteger,
        destination: MutableSequence[int] | memoryview,
        order: ByteOrder = "little-endian",
    ) -> bool:
        data = value.to_bytes(order)
        if len(destination) < len(data):
            return False
        destination[: len(data)] = data
        return True


__all__ = [
    "BitConverterEx",
    "ByteOrder",
    "FixedWidthInteger",
    "Int256",
    "Int512",
    "Int1024",
    "UInt256",
    "UInt512",
    "UInt1024",
    "WideInteger",
]
