"""Immutable cursor contracts for the persistent sequence families."""

from __future__ import annotations

from dataclasses import dataclass

import pytest

from durable7.finger_tree import (
    FingerTree,
    NumberSumMeasure,
    PersistentDeque,
    RangeUpdateSequence,
    ReversibleDeque,
    RrbVector,
    create_range_update_algebra,
)


def test_deque_cursor_keeps_exact_versions_and_distinguishes_stored_none() -> None:
    basis = PersistentDeque.from_iterable([1, None, 3])
    cursor = basis.get_cursor(2)

    previous = cursor.peek_previous()
    assert previous is not None and previous.value is None
    following = cursor.peek_next()
    assert following is not None and following.value == 3

    edited = cursor.insert_range([7, 8]).delete_previous().replace_next(9)
    assert edited.position == 3
    assert edited.snapshot().to_list() == [1, None, 7, 9]
    assert basis.to_list() == [1, None, 3]

    with pytest.raises(IndexError, match="start"):
        basis.get_cursor().move_previous()


def test_reversible_deque_cursor_uses_logical_order_and_maps_reverse_gap() -> None:
    basis = ReversibleDeque.from_iterable([1, 2, 3, 4]).reverse()
    cursor = basis.get_cursor(1)
    previous = cursor.peek_previous()
    following = cursor.peek_next()
    assert previous is not None and previous.value == 4
    assert following is not None and following.value == 3

    edited = cursor.insert(9).delete_next()
    assert edited.snapshot().to_list() == [4, 9, 2, 1]
    reversed_cursor = edited.reverse()
    assert reversed_cursor.position == 2
    assert reversed_cursor.snapshot().to_list() == [1, 2, 9, 4]


def test_general_finger_tree_cursor_exposes_measures_without_public_position() -> None:
    tree = FingerTree.from_iterable([2, 3, 5, 7], NumberSumMeasure())
    found, cursor = tree.get_cursor(lambda total: total >= 6)
    assert found
    assert cursor.measure_before == 5
    assert cursor.measure_after == 12
    following = cursor.peek_next()
    assert following is not None and following.value == 5
    assert not hasattr(cursor, "position")

    edited = cursor.insert(11).delete_next().replace_next(13)
    assert edited.snapshot().to_list() == [2, 3, 11, 13]
    assert tree.to_list() == [2, 3, 5, 7]
    sought = edited.seek_by_measure(lambda total: total >= 16).peek_next()
    assert sought is not None and sought.value == 11


def test_rrb_cursor_splices_existing_vectors_and_preserves_source() -> None:
    basis = RrbVector.from_iterable(range(96))
    inserted = RrbVector.from_iterable([500, 501, 502])
    cursor = basis.get_cursor(32).insert_range(inserted)

    assert cursor.position == 35
    assert cursor.snapshot()[30:37].to_list() == [30, 31, 500, 501, 502, 32, 33]
    assert basis.to_list() == list(range(96))
    assert cursor.snapshot().shared_leaf_count(basis) > 0


def test_range_cursor_preserves_logical_measures_and_directional_tags() -> None:
    algebra = create_range_update_algebra(
        0,
        0,
        lambda left, right: left + right,
        lambda element: element,
        lambda tag: tag == 0,
        lambda newer, older: newer + older,
        lambda tag, element: element + tag,
        lambda tag, measure, count: measure + tag * count,
    )
    basis = RangeUpdateSequence.from_iterable([1, 2, 3, 4], algebra)
    cursor = basis.get_cursor(2)
    assert cursor.measure_before == 3
    assert cursor.measure_after == 7
    assert cursor.measure_previous(2) == 3
    assert cursor.measure_next(2) == 7

    edited = cursor.apply_previous(1, 10).apply_next(2, 20).replace_next(99)
    assert edited.position == 2
    assert edited.snapshot().to_list() == [1, 12, 99, 24]
    assert basis.to_list() == [1, 2, 3, 4]


@dataclass(frozen=True, eq=False)
class _KeyedWeight:
    """Element whose equality ignores the field the measure reads."""

    key: str
    weight: int

    def __eq__(self, other: object) -> bool:
        return isinstance(other, _KeyedWeight) and self.key == other.key

    def __hash__(self) -> int:
        return hash(self.key)


class _WeightMeasure:
    identity = 0

    def combine(self, left: int, right: int) -> int:
        return left + right

    def measure(self, element: _KeyedWeight) -> int:
        return element.weight


def test_generic_sequence_replacement_has_no_element_equality_shortcut() -> None:
    """Replacement is unconditional; the deque and tree have no equality policy."""

    tree = FingerTree.from_iterable([_KeyedWeight("a", 1), _KeyedWeight("b", 2)], _WeightMeasure())
    replacement = _KeyedWeight("b", 100)

    replaced = tree.set_item(1, replacement)
    assert replaced is not None
    assert replaced.get(1) is replacement
    assert replaced.measure == 101
    assert tree.measure == 3

    through_cursor = tree.get_cursor_at_end().move_previous().replace_next(replacement).snapshot()
    assert through_cursor.get(1) is replacement
    assert through_cursor.measure == 101

    deque = PersistentDeque.from_iterable([_KeyedWeight("a", 1), _KeyedWeight("b", 2)])
    edited = deque.get_cursor(1).replace_next(replacement).snapshot()
    assert edited.get(1) is replacement
    assert deque.get(1) is not replacement


def test_reversed_range_insert_shares_structure_in_physical_orientation() -> None:
    """A reversed receiver inserts the range physically reversed and keeps sharing."""

    forward = ReversibleDeque.from_iterable(range(64))
    reversed_ = ReversibleDeque.from_iterable(reversed(range(64))).reverse()
    assert reversed_.to_list() == list(range(64))

    for basis in (forward, reversed_):
        for position in (0, 17, 64):
            cursor = basis.get_cursor(position).insert_range([100, 200, 300])
            expected = list(range(64))
            expected[position:position] = [100, 200, 300]
            assert cursor.snapshot().to_list() == expected
            assert cursor.position == position + 3
            assert cursor.snapshot().shares_storage_with(basis)
            assert basis.to_list() == list(range(64))

    unchanged = reversed_.get_cursor(8)
    assert unchanged.insert_range(()) is unchanged
