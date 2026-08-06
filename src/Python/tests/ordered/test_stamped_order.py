"""Tests for the stamp-measured order sequence behind the ordered collections.

The load-bearing case is the descent count. The whole point of measuring the sequence by maximum
stamp is that a keyed position lookup becomes ONE O(log n) descent; the representation this
replaced recovered positions by a binary search whose every probe was itself an O(log n) indexed
descent, i.e. O(log^2 n). Only counting the policy's combine calls can distinguish those two
shapes, so that is what the headline test does.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from durable7.ordered import PersistentOrderedMap
from durable7.ordered._stamped_order import StampedOrder, StampMeasure


@dataclass(frozen=True, slots=True)
class _Entry:
    stamp: int
    label: str


class _CountingStampMeasure(StampMeasure):
    """The production measure with a live combine counter."""

    def __init__(self) -> None:
        self.combine_calls = 0

    def combine(self, left: int, right: int) -> int:
        self.combine_calls += 1
        return super().combine(left, right)


def _build(count: int, policy: _CountingStampMeasure) -> StampedOrder[_Entry]:
    # Stride 3 leaves absent stamps between every pair of present ones.
    return StampedOrder.from_iterable(
        (_Entry(stamp * 3, f"e{stamp}") for stamp in range(count)), policy
    )


def test_index_of_stamp_is_one_logarithmic_descent() -> None:
    small_policy = _CountingStampMeasure()
    large_policy = _CountingStampMeasure()
    small = _build(1_024, small_policy)
    large = _build(32_768, large_policy)

    small_policy.combine_calls = 0
    large_policy.combine_calls = 0
    assert small.try_index_of_stamp(3 * 700) == 700
    small_cost = small_policy.combine_calls
    assert large.try_index_of_stamp(3 * 20_000) == 20_000
    large_cost = large_policy.combine_calls

    # A locate performs a bounded number of combines per level of one root-to-target path. The
    # factor-32 growth in size may add only the extra ~5 levels' worth of work; the O(log^2 n)
    # shape this replaced would pay hundreds of combines here, and a linear shape tens of
    # thousands.
    small_bound = 4 * math.ceil(math.log2(1_024)) + 4
    large_bound = 4 * math.ceil(math.log2(32_768)) + 4
    assert 0 < small_cost <= small_bound, small_cost
    assert 0 < large_cost <= large_bound, large_cost
    assert large_cost - small_cost <= 24, (small_cost, large_cost)


def test_every_present_stamp_maps_back_and_absent_stamps_return_none() -> None:
    policy = _CountingStampMeasure()
    order = _build(500, policy)

    for index in range(500):
        assert order.try_index_of_stamp(index * 3) == index

    assert order.try_index_of_stamp(-1) is None
    assert order.try_index_of_stamp(1) is None, "between the first pair of present stamps"
    assert order.try_index_of_stamp(3 * 250 + 1) is None, "between two interior present stamps"
    assert order.try_index_of_stamp(3 * 500) is None, "past the last present stamp"
    assert StampedOrder.empty(policy).try_index_of_stamp(0) is None


def test_keyed_position_lookups_stay_correct_through_edits() -> None:
    """The map wiring: index_of_key must agree with the enumerated order after every reshaping."""

    ordered: PersistentOrderedMap[str, int] = PersistentOrderedMap.empty()
    for value in range(256):
        ordered = ordered.add(f"k{value}", value)
    ordered = ordered.move_to_first("k200")
    ordered = ordered.move_to_last("k7")
    ordered = ordered.remove("k100")

    for position, entry in enumerate(ordered):
        assert ordered.index_of_key(entry.key) == position
    assert ordered.index_of_key("k100") == -1
