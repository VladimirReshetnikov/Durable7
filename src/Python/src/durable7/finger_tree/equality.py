"""Retained value-equality policies for the package's delta-tracking collections.

The delta structures here take value equality from a retained relation rather than from a bare
``==``, so a collection can decide what counts as "the same value" by a caller-supplied rule and can
remember which rule it was built with. The rule is load-bearing rather than cosmetic: it defines
which writes are semantic no-ops and when a recorded change cancels against its checkpoint, so it
determines a collection's observable change set, not just the answer to an incidental comparison.
That is also why it is retained rather than passed per call - comparing or replaying the changes of
two collections is only meaningful when both decided cancellation the same way.

A relation passed here must be a genuine equivalence relation: reflexive, symmetric, and transitive.
Reflexivity is the one that breaks in practice, because ``float("nan") == float("nan")`` is false.
A non-reflexive relation silently corrupts change accounting - a position can never be recognized as
having returned to its checkpoint value - and no amount of validation inside the collection can
detect it in general. :func:`natural_value_equality` therefore tries identity first, and
:func:`reflexive_ieee_equality` is the escape hatch for float payloads, mirroring Rust's
``EqualityPolicy::reflexive_ieee``.

This module is the counterpart of :mod:`durable7.finger_tree.ordering`, which retains orderings the
same way and for the same reasons.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import TypeVar

T = TypeVar("T")
ValueEquality = Callable[[T, T], bool]


def natural_value_equality(left: T, right: T) -> bool:
    """Compare values by identity first and then by ``==``.

    Identity is tried first so that a value whose ``__eq__`` is expensive, partial, or non-reflexive
    still counts as equal to itself. That is what makes this relation reflexive on ``float("nan")``,
    which a bare ``left == right`` is not, and reflexivity is what the cancellation and
    run-accounting rules require. Two *distinct* ``NaN`` objects remain in different classes; pass
    :func:`reflexive_ieee_equality` for the answer .NET's default comparer gives.
    """

    return left is right or bool(left == right)


def reflexive_ieee_equality(left: T, right: T) -> bool:
    """Compare values by ``==`` with every ``NaN`` collapsed into one equivalence class.

    This is the canonical float relation, the counterpart of Rust's
    ``EqualityPolicy::reflexive_ieee`` and an exact match for .NET's default ``double`` comparer:
    any two ``NaN`` values are equal to each other, and ``-0.0`` is equal to ``0.0``. ``NaN``
    detection is the self-inequality test ``value != value``, so it applies to the payload itself
    and not to floats nested inside a payload this relation cannot see into.
    """

    return left is right or bool(left == right) or (bool(left != left) and bool(right != right))


__all__ = ["ValueEquality", "natural_value_equality", "reflexive_ieee_equality"]
