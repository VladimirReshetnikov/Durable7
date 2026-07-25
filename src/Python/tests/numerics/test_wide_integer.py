"""Fixed-width and sparse integer parity tests."""

from __future__ import annotations

from collections.abc import Callable

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.numerics import (
    BitConverterEx,
    FixedWidthInteger,
    Int256,
    Int512,
    Int1024,
    SparseInteger,
    UInt256,
    UInt512,
    UInt1024,
)

WideFactory = Callable[[int | str], FixedWidthInteger]


@pytest.mark.parametrize(
    ("width", "unsigned", "signed"),
    [
        (256, UInt256, Int256),
        (512, UInt512, Int512),
        (1024, UInt1024, Int1024),
    ],
)
def test_unchecked_arithmetic_wraps_and_checked_arithmetic_rejects_overflow(
    width: int,
    unsigned: type[FixedWidthInteger],
    signed: type[FixedWidthInteger],
) -> None:
    unsigned_max = unsigned.max_value()
    assert int(unsigned_max + unsigned(1)) == 0
    with pytest.raises(OverflowError):
        unsigned_max.checked_add(unsigned(1))

    signed_max = signed.max_value()
    assert int(signed_max + signed(1)) == int(signed.min_value())
    with pytest.raises(OverflowError):
        signed_max.checked_add(signed(1))
    assert signed.width == width


@pytest.mark.parametrize(
    ("width", "signed"),
    [(256, Int256), (512, Int512), (1024, Int1024)],
)
def test_twos_complement_parse_and_byte_round_trips(
    width: int, signed: type[FixedWidthInteger]
) -> None:
    minus_one = signed(-1)
    assert int(signed.parse("f" * (width // 4), 16)) == -1
    assert int(signed.from_bytes(minus_one.to_bytes(), "little-endian")) == -1
    assert int(signed.from_bytes(minus_one.to_bytes("big-endian"), "big-endian")) == -1
    assert minus_one.to_bytes() == b"\xff" * (width // 8)


@settings(max_examples=100)
@given(
    st.integers(min_value=-(1 << 200), max_value=1 << 200),
    st.integers(min_value=-(1 << 200), max_value=1 << 200),
)
def test_uint256_arithmetic_matches_normalized_python_int(left: int, right: int) -> None:
    modulus = 1 << 256
    a, b = UInt256(left), UInt256(right)
    assert (a + b).to_unsigned_int() == (left + right) % modulus
    assert (a - b).to_unsigned_int() == (left - right) % modulus
    assert (a * b).to_unsigned_int() == (left * right) % modulus


@pytest.mark.parametrize("unsigned", [UInt512, UInt1024])
@settings(max_examples=50)
@given(
    st.integers(min_value=-(1 << 200), max_value=1 << 200),
    st.integers(min_value=-(1 << 200), max_value=1 << 200),
)
def test_other_unsigned_widths_match_normalized_python_int(
    unsigned: type[FixedWidthInteger], left: int, right: int
) -> None:
    modulus = 1 << unsigned.width
    a, b = unsigned(left), unsigned(right)
    assert (a + b).to_unsigned_int() == (left + right) % modulus
    assert (a - b).to_unsigned_int() == (left - right) % modulus
    assert (a * b).to_unsigned_int() == (left * right) % modulus


def test_formatting_shifts_rotations_and_fixed_byte_conversion() -> None:
    value = UInt256(0x1234)
    assert value.format("X8") == "00001234"
    assert Int256(-12345).format("N0") == "-12,345"
    assert value.rotate_left(4).rotate_right(4) == value
    assert value.byte_count() == 32
    destination = bytearray(32)
    assert BitConverterEx.try_write_bytes(value, destination)
    assert UInt256.from_bytes(destination) == value
    assert not BitConverterEx.try_write_bytes(value, bytearray(31))


def test_division_uses_truncation_and_signed_overflow_contracts() -> None:
    assert int(Int256(-7) // Int256(3)) == -2
    assert int(Int256(-7) % Int256(3)) == -1
    with pytest.raises(OverflowError):
        Int256.min_value().divide(Int256(-1))
    with pytest.raises(ZeroDivisionError):
        UInt256(1).divide(UInt256(0))


def test_bit_diagnostics_predicates_and_div_rem() -> None:
    value = UInt256(0b1011_0000)
    assert (
        value.leading_zero_count(),
        value.trailing_zero_count(),
        value.pop_count(),
        value.log2(),
    ) == (248, 4, 3, 7)
    assert value.is_even
    assert UInt256(1 << 200).is_power_of_two
    assert UInt256.max_value().increment().is_zero
    quotient, remainder = divmod(Int256(-7), Int256(3))
    assert (int(quotient), int(remainder)) == (-2, -1)
    assert int(abs(Int256(-7))) == 7
    with pytest.raises(OverflowError):
        abs(Int256.min_value())


def test_sparse_integer_supports_sparse_powers_and_arithmetic() -> None:
    power = SparseInteger.power_of_two(1000)
    assert int(power.exact_log2()) == 1000
    assert int(power + SparseInteger(1)) == (1 << 1000) + 1
    assert str(SparseInteger(123) * SparseInteger(456)) == str(123 * 456)
    assert int(SparseInteger.parse("  +123456 ")) == 123456
    with pytest.raises(ValueError):
        SparseInteger(-1)
