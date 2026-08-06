"""Tests for the retained value-equality policies.

The load-bearing property is reflexivity. A delta-tracking collection decides that a position has
returned to its checkpoint value by asking the retained relation, so a relation that reports a value
unequal to itself leaves that position permanently dirty and no internal validation can detect it.
Bare ``==`` has exactly that defect on ``float("nan")``, which is why neither policy here is a bare
``==``.
"""

from __future__ import annotations

import math
from typing import Any

import pytest

from durable7.finger_tree.equality import (
    ValueEquality,
    natural_value_equality,
    reflexive_ieee_equality,
)

POLICIES: list[ValueEquality[Any]] = [natural_value_equality, reflexive_ieee_equality]


@pytest.mark.parametrize("policy", POLICIES)
def test_every_policy_is_reflexive_where_bare_equality_is_not(policy: ValueEquality[Any]) -> None:
    not_a_number = float("nan")
    assert not_a_number != not_a_number, "the hazard being guarded against"
    assert policy(not_a_number, not_a_number)


@pytest.mark.parametrize("policy", POLICIES)
@pytest.mark.parametrize(
    "value",
    [0, 1, -1, 2**70, "alpha", (1, 2), frozenset({1}), None, math.inf, -math.inf, 0.0],
)
def test_every_policy_is_reflexive_on_ordinary_values(
    policy: ValueEquality[Any], value: Any
) -> None:
    assert policy(value, value)


@pytest.mark.parametrize("policy", POLICIES)
def test_every_policy_is_symmetric(policy: ValueEquality[Any]) -> None:
    values = [0, 1, 0.0, -0.0, "a", None, float("nan"), float("nan"), math.inf]
    for left in values:
        for right in values:
            assert policy(left, right) == policy(right, left)


@pytest.mark.parametrize("policy", POLICIES)
def test_every_policy_is_transitive_over_a_small_domain(policy: ValueEquality[Any]) -> None:
    values = [0, 0.0, -0.0, False, 1, 1.0, True, float("nan"), float("nan"), "a"]
    for left in values:
        for middle in values:
            for right in values:
                if policy(left, middle) and policy(middle, right):
                    assert policy(left, right), (left, middle, right)


def test_the_two_policies_differ_exactly_on_distinct_nan_objects() -> None:
    first, second = float("nan"), float("nan")
    assert first is not second
    assert not natural_value_equality(first, second), "natural keeps distinct NaNs apart"
    assert reflexive_ieee_equality(first, second), "the IEEE policy collapses them, as .NET does"


def test_the_ieee_policy_agrees_with_dot_net_on_signed_zero() -> None:
    assert reflexive_ieee_equality(-0.0, 0.0)
    assert natural_value_equality(-0.0, 0.0)


def test_identity_rescues_a_value_whose_equality_is_broken() -> None:
    """A payload that denies its own equality is still equal to itself under both policies.

    This is the general form of the ``NaN`` case: identity is consulted first precisely so a value
    cannot be lost by its own ``__eq__``.
    """

    class NeverEqual:
        __slots__ = ()

        def __eq__(self, other: object) -> bool:
            return False

        __hash__ = None  # type: ignore[assignment]

    value = NeverEqual()
    assert value != value
    for policy in POLICIES:
        assert policy(value, value)


def test_neither_policy_calls_equality_when_the_arguments_are_identical() -> None:
    """The identity short-circuit must be a real short-circuit, not just a first disjunct."""

    calls = 0

    class Counting:
        __slots__ = ()

        def __eq__(self, other: object) -> bool:
            nonlocal calls
            calls += 1
            return True

        __hash__ = None  # type: ignore[assignment]

    value = Counting()
    for policy in POLICIES:
        assert policy(value, value)
    assert calls == 0, "an identical pair must not reach __eq__"

    # Positive control: the counter is live, so the assertion above is not vacuous.
    assert natural_value_equality(Counting(), Counting())
    assert calls == 1
