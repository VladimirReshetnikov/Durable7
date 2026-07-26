"""Tests for the persistent Brodal-Okasaki meldable heap.

Covers draining adversarial insertion orders in sorted order, comparator identity governing
which heaps may meld, unambiguous empty and minimum views, bounded and failure-atomic callback
behavior, and agreement with a sorted reference model under randomized multisets and melds.
"""

from __future__ import annotations

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.brodal_okasaki_heap import (
    BrodalOkasakiHeap,
)


def _drain(heap: BrodalOkasakiHeap[int]) -> list[int]:
    output: list[int] = []
    while not heap.is_empty:
        view = heap.minimum_view()
        assert view is not None
        output.append(view.minimum)
        heap = view.remainder
    return output


def _integer_comparator(left: int, right: int) -> int:
    return left - right


def test_brodal_heap_drains_adversarial_inputs_in_sorted_order() -> None:
    values = list(reversed(range(4_096)))
    heap = BrodalOkasakiHeap.from_iterable(values)
    assert heap.validate_structure().count == len(values)
    assert _drain(heap) == list(range(4_096))
    assert heap.minimum == 0
    assert len(heap) == 4_096


def test_brodal_meld_uses_comparator_identity_and_retains_versions() -> None:
    comparator = _integer_comparator
    left = BrodalOkasakiHeap.from_iterable((1, 3, 5), comparator)
    right = BrodalOkasakiHeap.from_iterable((2, 4, 6), comparator)
    merged = left.meld(right)
    assert list(sorted(left)) == [1, 3, 5]
    assert list(sorted(right)) == [2, 4, 6]
    assert _drain(merged) == [1, 2, 3, 4, 5, 6]
    assert merged.shared_tree_count(left) > 0
    with pytest.raises(TypeError, match="same comparator object"):
        left.meld(BrodalOkasakiHeap.from_iterable((0,), lambda a, b: a - b))
    assert left.meld(BrodalOkasakiHeap.empty(comparator)) is left
    assert BrodalOkasakiHeap.empty(comparator).meld(right) is right


def test_brodal_empty_and_minimum_views_are_unambiguous() -> None:
    empty: BrodalOkasakiHeap[int] = BrodalOkasakiHeap.empty()
    assert empty.minimum_view() is None
    assert empty.validate_structure().count == 0
    with pytest.raises(IndexError, match="empty"):
        _ = empty.minimum
    with pytest.raises(IndexError, match="empty"):
        empty.delete_minimum()
    singleton = empty.insert(7)
    view = singleton.minimum_view()
    assert view is not None and view.minimum == 7 and view.remainder.is_empty
    assert empty.is_empty


def test_brodal_insert_meld_callbacks_are_bounded_and_failure_atomic() -> None:
    class Comparator:
        """A counting comparator that can be armed to raise, so the test can bound callback counts
        and check failure atomicity.
        """

        def __init__(self) -> None:
            self.calls = 0
            self.fail = False

        def __call__(self, left: int, right: int) -> int:
            self.calls += 1
            if self.fail:
                raise RuntimeError("comparison failed")
            return left - right

    comparator = Comparator()
    heap = BrodalOkasakiHeap.empty(comparator)
    for value in range(256):
        comparator.calls = 0
        heap = heap.insert(value)
        assert comparator.calls <= 5
    other = BrodalOkasakiHeap.from_iterable(range(256, 512), comparator)
    comparator.calls = 0
    merged = heap.meld(other)
    assert comparator.calls <= 5
    assert len(merged) == 512

    before = list(heap)
    comparator.fail = True
    with pytest.raises(RuntimeError, match="comparison failed"):
        heap.insert(-1)
    comparator.fail = False
    assert list(heap) == before
    assert heap.validate_structure().count == 256


@settings(max_examples=80, deadline=None)
@given(st.lists(st.integers(), max_size=350))
def test_brodal_random_multisets_match_sorted_model(values: list[int]) -> None:
    heap = BrodalOkasakiHeap.from_iterable(values)
    assert heap.validate_structure().count == len(values)
    assert _drain(heap) == sorted(values)


@settings(max_examples=50, deadline=None)
@given(
    st.lists(st.integers(), max_size=160),
    st.lists(st.integers(), max_size=160),
)
def test_brodal_random_melds_match_combined_multiset(
    left_values: list[int], right_values: list[int]
) -> None:
    comparator = _integer_comparator
    left = BrodalOkasakiHeap.from_iterable(left_values, comparator)
    right = BrodalOkasakiHeap.from_iterable(right_values, comparator)
    merged = left.meld(right)
    assert _drain(merged) == sorted([*left_values, *right_values])
    assert sorted(left) == sorted(left_values)
    assert sorted(right) == sorted(right_values)
