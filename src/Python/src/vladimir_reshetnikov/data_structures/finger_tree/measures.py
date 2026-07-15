"""Monoid and element-measure policies."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Generic, Protocol, TypeVar, cast

from .ordering import Comparator, default_comparator

T = TypeVar("T")
T_contra = TypeVar("T_contra", contravariant=True)
M = TypeVar("M")
A = TypeVar("A")
B = TypeVar("B")


class Monoid(Protocol[M]):
    """Associative operation with an identity."""

    @property
    def identity(self) -> M: ...

    def combine(self, left: M, right: M) -> M: ...


class MeasurePolicy(Monoid[M], Protocol[T_contra, M]):
    """Monoid-valued measurement of sequence elements."""

    def measure(self, element: T_contra) -> M: ...


class SizeMeasure(Generic[T]):
    identity = 0

    def combine(self, left: int, right: int) -> int:
        return left + right

    def measure(self, element: T) -> int:
        del element
        return 1


class NumberSumMeasure:
    identity = 0

    def combine(self, left: int | float, right: int | float) -> int | float:
        return left + right

    def measure(self, element: int | float) -> int | float:
        return element


class IntegerSumMeasure:
    """Arbitrary-precision integer sum measure (Python's bigint mapping)."""

    identity = 0

    def combine(self, left: int, right: int) -> int:
        return left + right

    def measure(self, element: int) -> int:
        return element


BigIntSumMeasure = IntegerSumMeasure


@dataclass(frozen=True, slots=True)
class OptionalValue(Generic[T]):
    """Presence-preserving optional monoid carrier.

    ``None`` can be a legitimate stored value, so the explicit ``has_value`` bit
    supplies the identity needed by extremal measures without reserving a value
    from ``T``.
    """

    has_value: bool
    value: T | None = None

    @classmethod
    def none(cls) -> OptionalValue[T]:
        return cls(False)

    @classmethod
    def some(cls, value: T) -> OptionalValue[T]:
        return cls(True, value)

    def unwrap(self) -> T:
        if not self.has_value:
            raise ValueError("An absent OptionalValue has no value.")
        return cast(T, self.value)


class MaxMeasure(Generic[T]):
    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        self.comparator = comparator
        self.identity: OptionalValue[T] = OptionalValue.none()

    def combine(self, left: OptionalValue[T], right: OptionalValue[T]) -> OptionalValue[T]:
        if not left.has_value:
            return right
        if not right.has_value:
            return left
        return left if self.comparator(left.unwrap(), right.unwrap()) >= 0 else right

    def measure(self, element: T) -> OptionalValue[T]:
        return OptionalValue.some(element)


class MinMeasure(Generic[T]):
    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        self.comparator = comparator
        self.identity: OptionalValue[T] = OptionalValue.none()

    def combine(self, left: OptionalValue[T], right: OptionalValue[T]) -> OptionalValue[T]:
        if not left.has_value:
            return right
        if not right.has_value:
            return left
        return left if self.comparator(left.unwrap(), right.unwrap()) <= 0 else right

    def measure(self, element: T) -> OptionalValue[T]:
        return OptionalValue.some(element)


@dataclass(frozen=True, slots=True)
class MeasurePair(Generic[A, B]):
    first: A
    second: B


class ProductMeasure(Generic[T, A, B]):
    def __init__(self, first: MeasurePolicy[T, A], second: MeasurePolicy[T, B]) -> None:
        self.first = first
        self.second = second

    @property
    def identity(self) -> MeasurePair[A, B]:
        return MeasurePair(self.first.identity, self.second.identity)

    def combine(self, left: MeasurePair[A, B], right: MeasurePair[A, B]) -> MeasurePair[A, B]:
        return MeasurePair(
            self.first.combine(left.first, right.first),
            self.second.combine(left.second, right.second),
        )

    def measure(self, element: T) -> MeasurePair[A, B]:
        return MeasurePair(self.first.measure(element), self.second.measure(element))


@dataclass(frozen=True, slots=True)
class _FunctionalMeasure(Generic[T, M]):
    identity: M
    _combine: Callable[[M, M], M]
    _measure: Callable[[T], M]

    def combine(self, left: M, right: M) -> M:
        return self._combine(left, right)

    def measure(self, element: T) -> M:
        return self._measure(element)


def create_measure_policy(
    identity: M,
    combine: Callable[[M, M], M],
    measure: Callable[[T], M],
) -> MeasurePolicy[T, M]:
    return _FunctionalMeasure(identity, combine, measure)


__all__ = [
    "BigIntSumMeasure",
    "IntegerSumMeasure",
    "MaxMeasure",
    "MeasurePair",
    "MeasurePolicy",
    "MinMeasure",
    "Monoid",
    "NumberSumMeasure",
    "OptionalValue",
    "ProductMeasure",
    "SizeMeasure",
    "create_measure_policy",
]
