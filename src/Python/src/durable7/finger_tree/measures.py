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
    def identity(self) -> M:
        """The two-sided identity for :meth:`combine`, and the measure of the empty sequence."""
        ...

    def combine(self, left: M, right: M) -> M:
        """Combine two measures in sequence order, ``left`` preceding ``right``.

        Must be associative. It need not be commutative; measures are always combined left to
        right, so an order-sensitive monoid is well defined.
        """
        ...


class MeasurePolicy(Monoid[M], Protocol[T_contra, M]):
    """Monoid-valued measurement of sequence elements."""

    def measure(self, element: T_contra) -> M:
        """Return the measure of a single element."""
        ...


class SizeMeasure(Generic[T]):
    """Counts elements, making a measured sequence indexable by position."""

    identity = 0

    def combine(self, left: int, right: int) -> int:
        """Add two counts."""

        return left + right

    def measure(self, element: T) -> int:
        """Every element counts as one, whatever it is."""

        del element
        return 1


class NumberSumMeasure:
    """Sums numeric elements, turning a sequence into a cumulative-weight structure.

    Accepts ``int`` and ``float``; use :class:`IntegerSumMeasure` when exactness matters, since
    floating-point addition is not associative and would break the monoid law.
    """

    identity = 0

    def combine(self, left: int | float, right: int | float) -> int | float:
        """Add two running totals."""

        return left + right

    def measure(self, element: int | float) -> int | float:
        """An element measures as its own numeric value."""

        return element


class IntegerSumMeasure:
    """Arbitrary-precision integer sum measure (Python's bigint mapping)."""

    identity = 0

    def combine(self, left: int, right: int) -> int:
        """Add two running totals exactly, without overflow."""

        return left + right

    def measure(self, element: int) -> int:
        """An element measures as its own integer value."""

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
        """The absent carrier, which is the identity of the extremal measures."""

        return cls(False)

    @classmethod
    def some(cls, value: T) -> OptionalValue[T]:
        """The carrier holding ``value``, even when that value is ``None``."""

        return cls(True, value)

    def unwrap(self) -> T:
        """Return the carried value, raising :class:`ValueError` when the carrier is absent."""

        if not self.has_value:
            raise ValueError("An absent OptionalValue has no value.")
        return cast(T, self.value)


class MaxMeasure(Generic[T]):
    """Tracks the largest element, making a measured sequence a max-priority structure.

    The identity is the absent carrier rather than a sentinel value, so no element of ``T`` has to
    be reserved and a stored ``None`` stays representable.
    """

    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        """Order elements by ``comparator``, defaulting to their natural ordering."""

        self.comparator = comparator
        self.identity: OptionalValue[T] = OptionalValue.none()

    def combine(self, left: OptionalValue[T], right: OptionalValue[T]) -> OptionalValue[T]:
        """Return the larger carrier, or the present one when the other is absent.

        Ties keep the left operand, so the leftmost maximum is the one reported.
        """

        if not left.has_value:
            return right
        if not right.has_value:
            return left
        return left if self.comparator(left.unwrap(), right.unwrap()) >= 0 else right

    def measure(self, element: T) -> OptionalValue[T]:
        """An element measures as the carrier holding itself."""

        return OptionalValue.some(element)


class MinMeasure(Generic[T]):
    """Tracks the smallest element, the mirror image of :class:`MaxMeasure`."""

    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        """Order elements by ``comparator``, defaulting to their natural ordering."""

        self.comparator = comparator
        self.identity: OptionalValue[T] = OptionalValue.none()

    def combine(self, left: OptionalValue[T], right: OptionalValue[T]) -> OptionalValue[T]:
        """Return the smaller carrier, or the present one when the other is absent.

        Ties keep the left operand, so the leftmost minimum is the one reported.
        """

        if not left.has_value:
            return right
        if not right.has_value:
            return left
        return left if self.comparator(left.unwrap(), right.unwrap()) <= 0 else right

    def measure(self, element: T) -> OptionalValue[T]:
        """An element measures as the carrier holding itself."""

        return OptionalValue.some(element)


@dataclass(frozen=True, slots=True)
class MeasurePair(Generic[A, B]):
    """The measure computed by a :class:`ProductMeasure`: two measures carried together."""

    first: A
    second: B


class ProductMeasure(Generic[T, A, B]):
    """Runs two measure policies side by side over the same elements.

    The product of two monoids is a monoid, so one sequence can be searched by either component -
    counting elements and summing weights at once, for instance, so the same tree answers both
    "the element at index 10" and "where the running total passes 500".
    """

    def __init__(self, first: MeasurePolicy[T, A], second: MeasurePolicy[T, B]) -> None:
        """Pair two policies, applied to every element in this order."""

        self.first = first
        self.second = second

    @property
    def identity(self) -> MeasurePair[A, B]:
        """The pair of component identities."""

        return MeasurePair(self.first.identity, self.second.identity)

    def combine(self, left: MeasurePair[A, B], right: MeasurePair[A, B]) -> MeasurePair[A, B]:
        """Combine each component independently."""

        return MeasurePair(
            self.first.combine(left.first, right.first),
            self.second.combine(left.second, right.second),
        )

    def measure(self, element: T) -> MeasurePair[A, B]:
        """Measure the element under each component."""

        return MeasurePair(self.first.measure(element), self.second.measure(element))


@dataclass(frozen=True, slots=True)
class _FunctionalMeasure(Generic[T, M]):
    identity: M
    _combine: Callable[[M, M], M]
    _measure: Callable[[T], M]

    def combine(self, left: M, right: M) -> M:
        """Delegate to the caller-supplied combine function."""

        return self._combine(left, right)

    def measure(self, element: T) -> M:
        """Delegate to the caller-supplied measure function."""

        return self._measure(element)


def create_measure_policy(
    identity: M,
    combine: Callable[[M, M], M],
    measure: Callable[[T], M],
) -> MeasurePolicy[T, M]:
    """Build a measure policy from plain functions instead of a class.

    The caller is responsible for the monoid laws: ``combine`` must be associative with
    ``identity`` as its two-sided identity, since measured search relies on both.
    """

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
