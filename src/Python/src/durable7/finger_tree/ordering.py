"""Ordering policies shared by ordered persistent collections."""

from __future__ import annotations

from collections.abc import Callable
from typing import TypeVar

T = TypeVar("T")
Comparator = Callable[[T, T], int]


def default_comparator(left: T, right: T) -> int:
    """Compare values using their native total ordering."""

    return -1 if left < right else 1 if left > right else 0  # type: ignore[operator]


def reverse_comparator(comparator: Comparator[T]) -> Comparator[T]:
    """Reverse an existing comparator while preserving zero."""

    def reversed_comparator(left: T, right: T) -> int:
        """Compare in the opposite order, leaving equivalence classes unchanged."""

        return comparator(right, left)

    return reversed_comparator


__all__ = ["Comparator", "default_comparator", "reverse_comparator"]
