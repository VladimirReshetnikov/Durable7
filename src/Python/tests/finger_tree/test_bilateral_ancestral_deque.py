"""Tests for the bilateral ancestral deque.

Covers the two-interval representation and the O(1) reversal that only exchanges the intervals, the
exact level-ancestor query profile the query discipline promises -- full tables rather than
ceilings, including the cross-arm slices and the four cached endpoints that index with no query at
all -- the two-value shortcut and anchor selection inside a centre-crossing removal, the no-op
operations that return a value sharing the source storage, endpoint and boundary errors, branch
independence across retained versions, the structural audit and its own query cost, and agreement
with a list model under exhaustive endpoint-construction words, randomized branching histories, and
concurrent growth.
"""

from __future__ import annotations

import random
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from functools import partial
from typing import TypeVar

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.bilateral_ancestral_deque import (
    BilateralAncestralDeque,
    BilateralAncestralDequeInterval,
)
from durable7.finger_tree.incremental_ancestor import (
    IncrementalAncestorArena,
    MyersIncrementalAncestorArena,
)

_T = TypeVar("_T")
_R = TypeVar("_R")


# The reference deque used by every query-profile assertion: four values added at the front and four
# at the back, so both arms hold exactly four values and every cross-arm case is reachable.
_PROFILE_VALUES = (1, 2, 3, 4, 5, 6, 7, 8)

# Queries spent by get_at at each visible index.  The zeros are the four cached endpoints: index 0
# is the left tail, index 3 the left base, index 4 the right base, and index 7 the right tail.
_GET_AT_QUERIES = (0, 1, 1, 0, 0, 1, 1, 0)

# Queries spent by take(k) and drop(k) for k in 0..8.  A prefix or suffix stops inside at most one
# arm, so neither ever spends more than one query.
_TAKE_QUERIES = (0, 0, 1, 1, 0, 0, 1, 1, 0)
_DROP_QUERIES = (0, 1, 1, 1, 0, 1, 1, 1, 0)

# Queries spent by split_at(k); the pair is a prefix plus its complementary suffix, so the
# documented ceiling of two is reached but never exceeded.
_SPLIT_QUERIES = (0, 1, 2, 2, 0, 1, 2, 2, 0)

# Queries spent by get_range(start, count); row `start`, column `count`.  The ceiling of two holds
# for the cross-arm rows too, because a crossing range takes a suffix of the left arm and a prefix
# of the right arm, and each of those spends at most one.
_RANGE_QUERIES = (
    (0, 0, 1, 1, 0, 0, 1, 1, 0),
    (0, 1, 2, 1, 1, 2, 2, 1),
    (0, 1, 1, 1, 2, 2, 1),
    (0, 1, 1, 2, 2, 1),
    (0, 0, 1, 1, 0),
    (0, 1, 2, 1),
    (0, 1, 1),
    (0, 1),
    (0,),
)


def _deque_of(values: list[int]) -> BilateralAncestralDeque[int]:
    """Build a Myers-backed deque holding ``values``."""

    return BilateralAncestralDeque.from_iterable(values)


def _bilateral(
    arena: MyersIncrementalAncestorArena[int], front: list[int], back: list[int]
) -> BilateralAncestralDeque[int]:
    """Grow a deque in ``arena`` so that both arms are populated as requested."""

    deque: BilateralAncestralDeque[int] = BilateralAncestralDeque.create(arena)
    for value in front:
        deque = deque.add_first(value)
    for value in back:
        deque = deque.add_last(value)
    return deque


def _measure(arena: MyersIncrementalAncestorArena[int], action: Callable[[], _R]) -> tuple[int, _R]:
    """Run ``action`` and report the level-ancestor queries it caused with its result."""

    before = arena.statistics().ancestor_query_count
    result = action()
    return arena.statistics().ancestor_query_count - before, result


def _assert_arena_delta(
    arena: MyersIncrementalAncestorArena[int],
    expected_adds: int,
    expected_queries: int,
    action: Callable[[], object],
) -> None:
    """Run ``action`` and assert the exact arena additions and ancestor queries it caused."""

    published_before = arena.published_node_count
    queries, _ = _measure(arena, action)
    assert arena.published_node_count - published_before == expected_adds
    assert queries == expected_queries


def _assert_values(deque: BilateralAncestralDeque[_T], expected: list[_T]) -> None:
    """Check the cheap reads and the structural audit against one expected model."""

    assert len(deque) == len(expected)
    assert deque.count == len(expected)
    assert deque.is_empty == (not expected)
    assert deque.to_list() == expected
    assert list(deque) == expected

    statistics = deque.validate_structure()
    assert statistics.count == len(expected)
    assert statistics.left_count + statistics.right_count == len(expected)


def _assert_state(deque: BilateralAncestralDeque[_T], expected: list[_T]) -> None:
    """Check every read the deque offers against one expected model, and audit the intervals."""

    _assert_values(deque, expected)

    for index, item in enumerate(expected):
        assert deque.get_at(index) == item
        assert deque[index] == item
    with pytest.raises(IndexError):
        deque.get_at(len(expected))
    with pytest.raises(IndexError):
        deque.get_at(-1)

    if expected:
        assert deque.first == expected[0]
        assert deque.last == expected[-1]
        popped_first = deque.try_remove_first()
        assert popped_first is not None
        assert popped_first.value == expected[0]
        assert popped_first.rest.to_list() == expected[1:]
        popped_last = deque.try_remove_last()
        assert popped_last is not None
        assert popped_last.value == expected[-1]
        assert popped_last.rest.to_list() == expected[:-1]
    else:
        with pytest.raises(IndexError):
            _ = deque.first
        with pytest.raises(IndexError):
            _ = deque.last
        with pytest.raises(IndexError):
            deque.remove_first()
        with pytest.raises(IndexError):
            deque.remove_last()
        assert deque.try_remove_first() is None
        assert deque.try_remove_last() is None


class _MisplacedBottomArena:
    """An arena whose bottom node reports depth zero, used to prove eager rejection.

    Every navigating member fails loudly, so a rejected arena that was nevertheless consulted would
    surface as an error rather than as a silently wrong deque.
    """

    def __init__(self) -> None:
        self.touched = 0

    @property
    def bottom(self) -> int:
        return 0

    @property
    def published_node_count(self) -> int:
        return 0

    def add_leaf(self, parent: int, value: int) -> int:
        self.touched += 1
        raise AssertionError("A rejected arena is never asked to publish.")

    def depth_of(self, node: int) -> int:
        return 0

    def parent_of(self, node: int) -> int:
        raise AssertionError("A rejected arena is never navigated.")

    def ancestor_at_depth(self, node: int, depth: int) -> int:
        raise AssertionError("A rejected arena is never queried.")

    def value_at(self, node: int) -> int:
        raise AssertionError("A rejected arena holds no values.")


def test_empty_deque_reads_fail_and_no_op_results_share_storage() -> None:
    empty: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
    _assert_state(empty, [])

    for produced in (
        empty.clear(),
        empty.take(0),
        empty.drop(0),
        empty.get_range(0, 0),
        empty.reverse(),
    ):
        _assert_values(produced, [])
        assert produced is empty
        assert produced.shares_storage_with(empty)

    split = empty.split_at(0)
    assert split.left is empty
    assert split.right is empty

    with pytest.raises(ValueError, match="exceeds"):
        empty.take(1)
    with pytest.raises(ValueError, match="exceeds"):
        empty.drop(1)
    with pytest.raises(IndexError):
        empty.split_at(1)
    with pytest.raises(ValueError, match="past the end"):
        empty.get_range(0, 1)
    with pytest.raises(IndexError):
        empty.get_range(1, 0)

    # An empty handle still grows into either arm, and each addition starts its own branch.
    _assert_state(empty.add_first(7), [7])
    _assert_state(empty.add_last(8), [8])
    _assert_state(empty, [])


def test_endpoint_growth_and_removal_retain_every_prior_branch() -> None:
    root: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
    one = root.add_last(10)
    two = one.add_last(20)
    three = two.add_first(5)
    four = three.add_first(1)
    five = four.add_last(30)

    _assert_state(root, [])
    _assert_state(one, [10])
    _assert_state(two, [10, 20])
    _assert_state(three, [5, 10, 20])
    _assert_state(four, [1, 5, 10, 20])
    _assert_state(five, [1, 5, 10, 20, 30])

    _assert_state(five.remove_first(), [5, 10, 20, 30])
    _assert_state(five.remove_last(), [1, 5, 10, 20])
    _assert_state(five, [1, 5, 10, 20, 30])

    # A one-armed deque drained from the far end crosses the centre on every removal.
    right_only = root.add_last(1).add_last(2).add_last(3)
    after_one = right_only.remove_first()
    after_two = after_one.remove_first()
    _assert_state(after_one, [2, 3])
    _assert_state(after_two, [3])
    _assert_state(after_two.remove_first(), [])
    _assert_state(right_only, [1, 2, 3])

    left_only = root.add_first(3).add_first(2).add_first(1)
    without_last = left_only.remove_last()
    without_two = without_last.remove_last()
    _assert_state(without_last, [1, 2])
    _assert_state(without_two, [1])
    _assert_state(without_two.remove_last(), [])
    _assert_state(left_only, [1, 2, 3])

    # Two independent branches grow from one retained version.
    _assert_state(two.add_first(-1), [-1, 10, 20])
    _assert_state(two.add_last(99), [10, 20, 99])
    _assert_state(two, [10, 20])

    cleared = five.clear()
    _assert_state(cleared, [])
    _assert_state(cleared.add_first(-7).add_last(8), [-7, 8])
    _assert_state(five, [1, 5, 10, 20, 30])


def test_reverse_exchanges_the_two_intervals_in_constant_time() -> None:
    deque = _bilateral(MyersIncrementalAncestorArena(), [3, 2, 1], [4, 5, 6, 7])
    reversed_deque = deque.reverse()
    restored = reversed_deque.reverse()

    _assert_state(deque, [1, 2, 3, 4, 5, 6, 7])
    _assert_state(reversed_deque, [7, 6, 5, 4, 3, 2, 1])
    _assert_state(restored, [1, 2, 3, 4, 5, 6, 7])

    # Reversal is an exchange of the two interval descriptors, nothing more.
    assert reversed_deque.left_interval == deque.right_interval
    assert reversed_deque.right_interval == deque.left_interval
    assert restored.shares_storage_with(deque)

    _assert_state(reversed_deque.add_first(0).add_last(8), [0, 7, 6, 5, 4, 3, 2, 1, 8])
    _assert_state(reversed_deque.remove_first().remove_last(), [6, 5, 4, 3, 2])
    _assert_state(deque, [1, 2, 3, 4, 5, 6, 7])

    # A deque of at most one value is its own reversal and returns a value sharing its storage.
    for short in (
        BilateralAncestralDeque.create_myers(),
        BilateralAncestralDeque.from_iterable([42]),
    ):
        assert short.reverse() is short


def test_every_slice_and_split_boundary_crossing_both_arms_remains_growable() -> None:
    deque = _bilateral(MyersIncrementalAncestorArena(), [3, 2, 1], [4, 5, 6, 7])
    expected = [1, 2, 3, 4, 5, 6, 7]

    for start in range(len(expected) + 1):
        for count in range(len(expected) - start + 1):
            window = deque.get_range(start, count)
            model = expected[start : start + count]
            _assert_state(window, model)

            grown = window.add_first(-100 - start).add_last(100 + count)
            _assert_state(grown, [-100 - start, *model, 100 + count])
            _assert_state(window, model)

            flipped = window.reverse().add_first(-1).add_last(-2)
            _assert_state(flipped, [-1, *model[::-1], -2])

    for boundary in range(len(expected) + 1):
        split = deque.split_at(boundary)
        _assert_state(split.left, expected[:boundary])
        _assert_state(split.right, expected[boundary:])
        _assert_state(deque.take(boundary), expected[:boundary])
        _assert_state(deque.drop(boundary), expected[boundary:])
        _assert_state(split.left.add_last(88), [*expected[:boundary], 88])
        _assert_state(split.right.add_first(77), [77, *expected[boundary:]])

    _assert_state(deque, expected)


def test_small_endpoint_construction_words_match_the_sequence_oracle() -> None:
    for length in range(8):
        for word in range(1 << length):
            deque: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
            model: list[int] = []
            for value in range(length):
                if word & (1 << value) == 0:
                    deque = deque.add_first(value)
                    model.insert(0, value)
                else:
                    deque = deque.add_last(value)
                    model.append(value)

            _assert_values(deque, model)
            for index, item in enumerate(model):
                assert deque.get_at(index) == item

            for start in range(length + 1):
                for count in range(length - start + 1):
                    window = deque.get_range(start, count)
                    expected = model[start : start + count]
                    _assert_values(window, expected)
                    _assert_values(window.add_first(-1).add_last(-2), [-1, *expected, -2])
                    _assert_values(window.reverse(), expected[::-1])

            for boundary in range(length + 1):
                split = deque.split_at(boundary)
                _assert_values(split.left, model[:boundary])
                _assert_values(split.right, model[boundary:])
                _assert_values(deque.take(boundary), model[:boundary])
                _assert_values(deque.drop(boundary), model[boundary:])


def test_query_profile_pins_exact_ancestor_query_counts() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    deque = _bilateral(arena, [4, 3, 2, 1], [5, 6, 7, 8])
    values = list(_PROFILE_VALUES)
    assert deque.to_list() == values
    assert deque.left_interval.count == 4
    assert deque.right_interval.count == 4

    # Growth, both endpoint reads, reversal, clearing, and whole-sequence enumeration are free.
    _assert_arena_delta(arena, 0, 0, lambda: len(deque))
    _assert_arena_delta(arena, 0, 0, lambda: deque.is_empty)
    _assert_arena_delta(arena, 0, 0, lambda: deque.first)
    _assert_arena_delta(arena, 0, 0, lambda: deque.last)
    _assert_arena_delta(arena, 0, 0, lambda: deque.reverse())
    _assert_arena_delta(arena, 0, 0, lambda: deque.clear())
    _assert_arena_delta(arena, 0, 0, lambda: deque.to_list())
    _assert_arena_delta(arena, 0, 0, lambda: list(deque))
    _assert_arena_delta(arena, 1, 0, lambda: deque.add_first(0))
    _assert_arena_delta(arena, 1, 0, lambda: deque.add_last(9))

    # Indexing: the exact profile, not merely the ceiling of one.
    profile = []
    for index in range(len(values)):
        queries, value = _measure(arena, partial(deque.get_at, index))
        assert value == values[index]
        profile.append(queries)
    assert tuple(profile) == _GET_AT_QUERIES
    assert max(_GET_AT_QUERIES) == 1

    take_profile = []
    drop_profile = []
    split_profile = []
    for boundary in range(len(values) + 1):
        queries, prefix = _measure(arena, partial(deque.take, boundary))
        assert prefix.to_list() == values[:boundary]
        take_profile.append(queries)

        queries, suffix = _measure(arena, partial(deque.drop, boundary))
        assert suffix.to_list() == values[boundary:]
        drop_profile.append(queries)

        queries, split = _measure(arena, partial(deque.split_at, boundary))
        assert split.left.to_list() == values[:boundary]
        assert split.right.to_list() == values[boundary:]
        split_profile.append(queries)
    assert tuple(take_profile) == _TAKE_QUERIES
    assert tuple(drop_profile) == _DROP_QUERIES
    assert tuple(split_profile) == _SPLIT_QUERIES
    assert max(_SPLIT_QUERIES) == 2

    range_profile = []
    for start in range(len(values) + 1):
        row = []
        for count in range(len(values) - start + 1):
            queries, window = _measure(arena, partial(deque.get_range, start, count))
            assert window.to_list() == values[start : start + count]
            row.append(queries)
        range_profile.append(tuple(row))
    assert tuple(range_profile) == _RANGE_QUERIES
    assert max(max(row) for row in _RANGE_QUERIES) == 2

    # The cross-arm rows really are cross-arm, and they still respect the ceiling of two.
    assert _RANGE_QUERIES[1][5] == 2
    assert _RANGE_QUERIES[3][3] == 2

    # Removal on its own nonempty arm makes no query at all.
    _assert_arena_delta(arena, 0, 0, lambda: deque.remove_first())
    _assert_arena_delta(arena, 0, 0, lambda: deque.remove_last())


def test_removal_query_profile_pins_the_two_value_shortcut_and_anchor_selection() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    right_only = _bilateral(arena, [], [10, 11, 12, 13])
    left_only = _bilateral(arena, [13, 12, 11, 10], [])

    # A drain that always crosses the centre spends one query per removal until the owning interval
    # is down to two values: at two the surviving value is already the retained tail, and at one the
    # result is an empty handle.  Both endpoints of the profile are exactly zero.
    front_profile = []
    current = right_only
    while len(current):
        queries, current = _measure(arena, current.remove_first)
        front_profile.append(queries)
    assert front_profile == [1, 1, 0, 0]

    back_profile = []
    current = left_only
    while len(current):
        queries, current = _measure(arena, current.remove_last)
        back_profile.append(queries)
    assert back_profile == [1, 1, 0, 0]

    # The single query selects the ancestor at depth(tail) - count + 2, which is the *next* cached
    # base.  An off-by-one there would either resurrect the removed value or skip one, and both the
    # endpoint read and the structural audit would see it.
    shortened = right_only.remove_first()
    assert shortened.to_list() == [11, 12, 13]
    assert shortened.first == 11
    assert shortened.right_interval.base == right_only.right_interval.base + 1
    assert shortened.right_interval.anchor == right_only.right_interval.base
    statistics = shortened.validate_structure()
    assert statistics.right_count == 3
    assert statistics.right_anchor_depth == 0

    twice = shortened.remove_first()
    assert twice.to_list() == [12, 13]
    assert twice.first == 12
    assert twice.right_interval.anchor == shortened.right_interval.base
    assert twice.validate_structure().right_anchor_depth == 1

    # The third removal is the one that sees a two-value interval, and its shortcut names the
    # retained tail directly instead of querying for it.
    thrice = twice.remove_first()
    assert thrice.to_list() == [13]
    assert thrice.first == 13
    assert thrice.right_interval.base == thrice.right_interval.tail == twice.right_interval.tail
    assert thrice.right_interval.anchor == twice.right_interval.base
    assert thrice.validate_structure().right_anchor_depth == 2

    _assert_state(right_only, [10, 11, 12, 13])
    _assert_state(left_only, [10, 11, 12, 13])


def test_no_op_operations_return_a_value_sharing_the_source_storage() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    deque = _bilateral(arena, [3, 2, 1], [4, 5, 6])
    canonical_empty = deque.clear()

    for whole in (deque.get_range(0, len(deque)), deque.take(len(deque)), deque.drop(0)):
        assert whole is deque
        assert whole.shares_storage_with(deque)

    # An endpoint split keeps one side as the receiver itself and anchors the other at the arena
    # bottom, so neither side is rebuilt.
    front_split = deque.split_at(0)
    back_split = deque.split_at(len(deque))
    assert front_split.right is deque
    assert back_split.left is deque
    assert front_split.left.shares_storage_with(canonical_empty)
    assert back_split.right.shares_storage_with(canonical_empty)
    assert front_split.left.shares_arena_with(deque)

    # Every empty result in one arena is the same anchored handle, and clearing an already-empty
    # deque returns that very value.
    assert canonical_empty.clear() is canonical_empty
    assert deque.get_range(3, 0).shares_storage_with(canonical_empty)
    assert deque.take(0).shares_storage_with(canonical_empty)
    assert deque.drop(len(deque)).shares_storage_with(canonical_empty)

    # A slice that stops inside an arm genuinely retains different endpoints.
    assert not deque.take(2).shares_storage_with(deque)
    assert deque.take(2).shares_arena_with(deque)
    assert not _deque_of([1, 2, 3, 4, 5, 6]).shares_arena_with(deque)


def test_validate_structure_reports_counts_and_rejects_a_corrupted_interval() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    deque = _bilateral(arena, [2, 1], [3])
    statistics = deque.validate_structure()
    assert statistics.count == 3
    assert statistics.left_count == 2
    assert statistics.right_count == 1
    assert statistics.left_anchor_depth == -1
    assert statistics.right_anchor_depth == -1
    assert statistics.arena_published_node_count == 3

    empty = deque.clear()
    wrong_total = BilateralAncestralDeque(
        deque.arena, deque.left_interval, deque.right_interval, deque.count + 1
    )
    with pytest.raises(ValueError, match="total count disagrees"):
        wrong_total.validate_structure()

    unshared_empty = BilateralAncestralDeque(
        deque.arena,
        replace(empty.left_interval, tail=empty.left_interval.tail + 1),
        empty.right_interval,
        0,
    )
    with pytest.raises(ValueError, match="one shared endpoint"):
        unshared_empty.validate_structure()

    wrong_span = BilateralAncestralDeque(
        deque.arena, replace(deque.left_interval, count=1), deque.right_interval, 2
    )
    with pytest.raises(ValueError, match="endpoint depths"):
        wrong_span.validate_structure()

    wrong_base = BilateralAncestralDeque(
        deque.arena,
        replace(deque.left_interval, base=deque.left_interval.tail),
        deque.right_interval,
        3,
    )
    with pytest.raises(ValueError, match="first node after the anchor"):
        wrong_base.validate_structure()

    # Two sibling branches of one arena let the ancestry-path checks be provoked without also
    # breaking the cheaper checks that run before them.
    branch_arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    first_child = branch_arena.add_leaf(branch_arena.bottom, 1)
    grandchild = branch_arena.add_leaf(first_child, 2)
    sibling = branch_arena.add_leaf(branch_arena.bottom, 10)
    sibling_child = branch_arena.add_leaf(sibling, 20)
    sibling_tail = branch_arena.add_leaf(sibling_child, 30)
    branch_empty: BilateralAncestralDeque[int] = BilateralAncestralDeque.create(branch_arena)

    foreign_anchor = BilateralAncestralDeque(
        branch_arena,
        BilateralAncestralDequeInterval(first_child, grandchild, sibling_tail, 2),
        branch_empty.right_interval,
        2,
    )
    with pytest.raises(ValueError, match="anchor is not an ancestor"):
        foreign_anchor.validate_structure()

    foreign_base = BilateralAncestralDeque(
        branch_arena,
        BilateralAncestralDequeInterval(branch_arena.bottom, first_child, sibling_tail, 3),
        branch_empty.right_interval,
        3,
    )
    with pytest.raises(ValueError, match="not on the tail's ancestry path"):
        foreign_base.validate_structure()

    negative = BilateralAncestralDeque(
        deque.arena, replace(deque.left_interval, count=-1), deque.right_interval, 0
    )
    with pytest.raises(ValueError, match="count is negative"):
        negative.validate_structure()

    _assert_state(deque, [1, 2, 3])


def test_validate_structure_spends_two_queries_per_nonempty_interval() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    empty: BilateralAncestralDeque[int] = BilateralAncestralDeque.create(arena)
    right_only = _bilateral(arena, [], [1, 2, 3])
    left_only = _bilateral(arena, [3, 2, 1], [])
    bilateral = _bilateral(arena, [2, 1], [3, 4])

    # An empty handle has no ancestry path to confirm.
    _assert_arena_delta(arena, 0, 0, empty.validate_structure)
    # One nonempty interval places its anchor and its cached base: exactly two queries.
    _assert_arena_delta(arena, 0, 2, right_only.validate_structure)
    _assert_arena_delta(arena, 0, 2, left_only.validate_structure)
    # Both intervals nonempty is the documented ceiling of four, not O(1) work.
    _assert_arena_delta(arena, 0, 4, bilateral.validate_structure)


def test_out_of_range_arguments_are_rejected_without_publishing_or_querying() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    deque = _bilateral(arena, [2, 1], [3])
    published_before = arena.published_node_count
    queries_before = arena.statistics().ancestor_query_count

    with pytest.raises(IndexError):
        deque.get_at(3)
    with pytest.raises(IndexError):
        deque.get_at(-1)
    with pytest.raises(IndexError):
        deque[3]
    with pytest.raises(IndexError):
        deque.get_range(4, 0)
    with pytest.raises(IndexError):
        deque.get_range(-1, 0)
    with pytest.raises(ValueError, match="past the end"):
        deque.get_range(2, 2)
    with pytest.raises(ValueError, match="nonnegative"):
        deque.get_range(0, -1)
    with pytest.raises(ValueError, match="exceeds"):
        deque.take(4)
    with pytest.raises(ValueError, match="nonnegative"):
        deque.take(-1)
    with pytest.raises(ValueError, match="exceeds"):
        deque.drop(4)
    with pytest.raises(ValueError, match="nonnegative"):
        deque.drop(-1)
    with pytest.raises(IndexError):
        deque.split_at(4)
    with pytest.raises(IndexError):
        deque.split_at(-1)

    assert arena.published_node_count == published_before
    assert arena.statistics().ancestor_query_count == queries_before
    _assert_state(deque, [1, 2, 3])


def test_create_rejects_an_arena_whose_bottom_is_not_at_depth_minus_one() -> None:
    rejected = _MisplacedBottomArena()
    seam: IncrementalAncestorArena[int] = rejected
    with pytest.raises(ValueError, match="depth -1"):
        BilateralAncestralDeque.create(seam)
    assert rejected.touched == 0


def test_stored_none_is_a_present_value_and_absence_uses_the_result_shape() -> None:
    empty: BilateralAncestralDeque[str | None] = BilateralAncestralDeque.create_myers()
    _assert_state(empty, [])

    pair = empty.add_first(None).add_last("tail")
    _assert_state(pair, [None, "tail"])
    assert pair.first is None
    assert pair.last == "tail"
    assert pair.get_at(0) is None

    tail_only: list[str | None] = ["tail"]
    none_only: list[str | None] = [None]
    popped_front = pair.try_remove_first()
    assert popped_front is not None
    assert popped_front.value is None
    _assert_state(popped_front.rest, tail_only)

    popped_back = pair.try_remove_last()
    assert popped_back is not None
    assert popped_back.value == "tail"
    _assert_state(popped_back.rest, none_only)
    _assert_state(pair, [None, "tail"])


def test_factories_create_independent_deques_and_a_shared_arena_serves_families() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    root: BilateralAncestralDeque[int] = BilateralAncestralDeque.create(arena)
    assert root.arena is arena

    first_family = root.add_last(1).add_last(2)
    second_family = root.add_first(9).add_first(8)
    _assert_state(first_family, [1, 2])
    _assert_state(second_family, [8, 9])
    assert arena.published_node_count == 4
    assert first_family.shares_arena_with(second_family)
    assert first_family.validate_structure().arena_published_node_count == 4

    built = BilateralAncestralDeque.from_iterable([1, 2, 3])
    _assert_state(built, [1, 2, 3])
    _assert_state(built.add_last(4), [1, 2, 3, 4])
    _assert_state(built, [1, 2, 3])
    # from_iterable always rebuilds onto a private arena rather than reusing its source's history.
    rebuilt = BilateralAncestralDeque.from_iterable(built)
    _assert_state(rebuilt, [1, 2, 3])
    assert not rebuilt.shares_arena_with(built)

    left: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
    right: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
    assert not left.shares_arena_with(right)


def test_repr_renders_the_visible_values() -> None:
    deque = _bilateral(MyersIncrementalAncestorArena(), [2, 1], [3, 4, 5])
    assert repr(deque) == "BilateralAncestralDeque([1, 2, 3, 4, 5])"
    assert repr(deque.get_range(1, 3)) == "BilateralAncestralDeque([2, 3, 4])"
    empty: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
    assert repr(empty) == "BilateralAncestralDeque([])"


def test_equality_and_hashing_stay_by_identity() -> None:
    source = _deque_of([1, 2, 3])
    same_values = _deque_of([1, 2, 3])
    assert source != same_values
    assert source == source
    assert len({source, same_values}) == 2


def test_iteration_is_repeatable_and_does_not_observe_later_branches() -> None:
    values = list(range(100))
    source = _deque_of(values)
    window = source.get_range(20, 40)
    captured = iter(window)

    branch = window.add_first(-1).add_last(-2)

    assert window.to_list() == values[20:60]
    assert window.to_list() == values[20:60]
    assert branch.to_list() == [-1, *values[20:60], -2]
    # The iterator captured before the branch existed still yields the original paths.
    assert list(captured) == values[20:60]
    _assert_state(source, values)


def test_randomized_branching_history_matches_list_models() -> None:
    generator = random.Random(0xB11A7E)
    histories: list[tuple[BilateralAncestralDeque[int], list[int]]] = [
        (BilateralAncestralDeque.create_myers(), [])
    ]

    for step in range(700):
        source, model = histories[generator.randrange(len(histories))]
        length = len(model)
        choice = generator.randrange(11)

        result: BilateralAncestralDeque[int]
        expected: list[int]
        if choice == 1:
            result, expected = source.add_first(step), [step, *model]
        elif choice == 2 and length != 0:
            result, expected = source.remove_first(), model[1:]
        elif choice == 3 and length != 0:
            result, expected = source.remove_last(), model[:-1]
        elif choice == 4:
            result, expected = source.reverse(), model[::-1]
        elif choice == 5:
            start = generator.randrange(length + 1)
            count = generator.randrange(length - start + 1)
            result, expected = source.get_range(start, count), model[start : start + count]
        elif choice == 6:
            boundary = generator.randrange(length + 1)
            split = source.split_at(boundary)
            if generator.randrange(2) == 0:
                result, expected = split.left, model[:boundary]
            else:
                result, expected = split.right, model[boundary:]
        elif choice == 7:
            count = generator.randrange(length + 1)
            result, expected = source.take(count), model[:count]
        elif choice == 8:
            count = generator.randrange(length + 1)
            result, expected = source.drop(count), model[count:]
        elif choice == 9:
            result, expected = source.clear(), []
        elif choice == 10 and length != 0:
            popped = source.try_remove_first()
            assert popped is not None
            assert popped.value == model[0]
            result, expected = popped.rest, model[1:]
        else:
            result, expected = source.add_last(step), [*model, step]

        assert source.to_list() == model
        assert result.to_list() == expected
        result.validate_structure()
        histories.append((result, expected))

    for deque, model in histories[::37]:
        _assert_state(deque, model)


@settings(max_examples=40, deadline=None)
@given(
    st.lists(st.tuples(st.integers(-50, 50), st.booleans()), max_size=30),
    st.integers(0, 30),
    st.integers(0, 30),
)
def test_slicing_agrees_with_list_slicing_for_valid_windows(
    additions: list[tuple[int, bool]], start: int, count: int
) -> None:
    deque: BilateralAncestralDeque[int] = BilateralAncestralDeque.create_myers()
    model: list[int] = []
    for value, at_back in additions:
        if at_back:
            deque = deque.add_last(value)
            model.append(value)
        else:
            deque = deque.add_first(value)
            model.insert(0, value)

    assert deque.to_list() == model
    if start > len(model) or count > len(model) - start:
        with pytest.raises((IndexError, ValueError)):
            deque.get_range(start, count)
        return

    window = deque.get_range(start, count)
    assert window.to_list() == model[start : start + count]
    assert window.reverse().to_list() == model[start : start + count][::-1]
    assert window.take(count).to_list() == model[start : start + count]
    assert window.drop(count).to_list() == []
    split = window.split_at(count)
    assert split.left.to_list() == model[start : start + count]
    assert split.right.to_list() == []
    window.validate_structure()


def test_concurrent_branch_additions_publish_complete_independent_versions() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    values = list(range(128))
    source = _bilateral(arena, values[:64][::-1], values[64:])
    assert source.to_list() == values
    thread_count = 4
    branches_per_thread = 8

    def build(thread_index: int) -> list[tuple[int, BilateralAncestralDeque[int]]]:
        """Grow sibling branches while reading level ancestors of the shared source."""

        produced: list[tuple[int, BilateralAncestralDeque[int]]] = []
        for offset in range(branches_per_thread):
            marker = -(thread_index * branches_per_thread + offset) - 1
            branch = source.add_first(marker).add_last(-marker)
            assert len(branch) == len(values) + 2
            assert branch.first == marker
            assert branch.last == -marker
            for index in range(offset % 17, len(values), 29):
                assert source.get_at(index) == values[index]
            assert source.get_range(50, 40).to_list() == values[50:90]
            produced.append((marker, branch))
        return produced

    with ThreadPoolExecutor(max_workers=thread_count) as pool:
        published = [item for batch in pool.map(build, range(thread_count)) for item in batch]

    assert len(published) == thread_count * branches_per_thread
    for marker, branch in published:
        assert branch.to_list() == [marker, *values, -marker]
    assert arena.published_node_count == len(values) + 2 * len(published)
    _assert_state(source, values)
