"""Tests for the ancestral slice queue.

Covers the anchored-empty rule and the distinct anchors a front drain and a back drain leave
behind, the whole-window operations that reuse the source tail, the exact ancestor-query counts the
query discipline promises, endpoint and boundary errors, branch independence across retained
versions, the structural audit, and agreement with a list model under exhaustive boundary sweeps
and randomized branching histories.

The query-profile test holds its own :class:`MyersIncrementalAncestorArena` reference, because
``statistics()`` deliberately belongs to that backend rather than to the arena seam.
"""

from __future__ import annotations

import random
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from typing import TypeVar

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.ancestral_slice_queue import (
    AncestralSliceQueue,
    AncestralSliceQueuePop,
    AncestralSliceQueueSplit,
)
from durable7.finger_tree.incremental_ancestor import MyersIncrementalAncestorArena

_T = TypeVar("_T")


def _assert_state(queue: AncestralSliceQueue[_T], expected: list[_T]) -> None:
    """Check every read the queue offers against one expected model, and audit the window."""

    assert len(queue) == len(expected)
    assert queue.count == len(expected)
    assert queue.is_empty == (not expected)
    assert queue.to_list() == expected
    assert list(queue) == expected

    for index, item in enumerate(expected):
        assert queue.get_at(index) == item
        assert queue[index] == item
    with pytest.raises(IndexError):
        queue.get_at(len(expected))
    with pytest.raises(IndexError):
        queue.get_at(-1)

    if expected:
        assert queue.first == expected[0]
        assert queue.last == expected[-1]
        popped_first = queue.try_remove_first()
        assert popped_first is not None
        assert popped_first.value == expected[0]
        assert popped_first.rest.to_list() == expected[1:]
        popped_last = queue.try_remove_last()
        assert popped_last is not None
        assert popped_last.value == expected[-1]
        assert popped_last.rest.to_list() == expected[:-1]
    else:
        with pytest.raises(IndexError):
            _ = queue.first
        with pytest.raises(IndexError):
            _ = queue.last
        with pytest.raises(IndexError):
            queue.remove_first()
        with pytest.raises(IndexError):
            queue.remove_last()
        assert queue.try_remove_first() is None
        assert queue.try_remove_last() is None

    statistics = queue.validate_structure()
    assert statistics.count == len(expected)
    assert statistics.tail_depth == statistics.low_depth + len(expected) - 1
    assert statistics.anchor_depth == statistics.low_depth - 1
    assert statistics.anchored_at_bottom == (statistics.anchor_depth == -1)


def _queue_of(values: list[int]) -> AncestralSliceQueue[int]:
    """A Myers-backed queue holding ``values``."""

    return AncestralSliceQueue.from_iterable(values)


def _assert_arena_delta(
    arena: MyersIncrementalAncestorArena[int],
    expected_adds: int,
    expected_queries: int,
    action: Callable[[], None],
) -> None:
    """Run ``action`` and assert the exact arena additions and ancestor queries it caused."""

    published_before = arena.published_node_count
    queries_before = arena.statistics().ancestor_query_count
    action()
    assert arena.published_node_count - published_before == expected_adds
    assert arena.statistics().ancestor_query_count - queries_before == expected_queries


def _assert_value(actual: int, expected: int) -> None:
    """Assert one read value inside an arena-delta measurement."""

    assert actual == expected


def _assert_length(queue: AncestralSliceQueue[int], expected: int) -> None:
    """Assert a derived queue's length inside an arena-delta measurement."""

    assert len(queue) == expected


def _assert_split(
    split: AncestralSliceQueueSplit[int], expected_left: int, expected_right: int
) -> None:
    """Assert both sides of a split inside an arena-delta measurement."""

    assert len(split.left) == expected_left
    assert len(split.right) == expected_right


def _assert_popped(
    popped: AncestralSliceQueuePop[int] | None, expected_value: int, expected_rest: int
) -> None:
    """Assert an endpoint removal inside an arena-delta measurement."""

    assert popped is not None
    assert popped.value == expected_value
    assert len(popped.rest) == expected_rest


def test_every_range_matches_the_model_and_empty_anchors_remain_appendable() -> None:
    length = 65
    values = list(range(length))
    source = _queue_of(values)

    for start in range(length + 1):
        for count in range(length - start + 1):
            window = source.get_range(start, count)
            assert window.to_list() == values[start : start + count]
            assert len(window) == count
            if count == 0:
                # The anchored-empty rule: appending to any empty slice yields exactly the new
                # values and never resurrects the discarded prefix.
                continued = window.add_last(-start - 1).add_last(10_000 + start)
                assert continued.to_list() == [-start - 1, 10_000 + start]
                assert window.to_list() == []

    _assert_state(source.get_range(0, length), values)
    _assert_state(source.take(length), values)
    _assert_state(source.drop(0), values)
    _assert_state(source, values)


def test_take_drop_and_split_at_partition_the_source_at_every_boundary() -> None:
    values = list(range(129))
    source = _queue_of(values)

    for position in range(len(values) + 1):
        prefix = source.take(position)
        suffix = source.drop(position)
        assert prefix.to_list() == values[:position]
        assert suffix.to_list() == values[position:]
        prefix.validate_structure()
        suffix.validate_structure()

        split = source.split_at(position)
        assert split.left.to_list() == values[:position]
        assert split.right.to_list() == values[position:]

        # Both results stay real ancestry slices and start independent branches.
        marker = -position - 1
        assert prefix.add_last(marker).to_list() == [*values[:position], marker]
        assert suffix.add_last(marker).to_list() == [*values[position:], marker]

    _assert_state(source, values)


def test_square_block_boundaries_support_index_range_and_branching() -> None:
    maximum_root = 16
    length = maximum_root * maximum_root + 1
    values = list(range(length))
    source = _queue_of(values)

    for root in range(maximum_root + 1):
        square = root * root
        assert source.get_at(square) == square

        start = max(square - 1, 0)
        count = min(3, len(source) - start)
        assert source.get_range(start, count).to_list() == values[start : start + count]

        marker = -square - 1
        assert source.get_range(square, 0).add_last(marker).to_list() == [marker]

        if square > 0:
            # The versions of length square - 1 and square end immediately before and at the seam.
            before_seam = source.take(square - 1)
            assert before_seam.add_last(marker).to_list() == [*values[: square - 1], marker]
            at_seam = source.take(square)
            assert at_seam.add_last(marker).to_list() == [*values[:square], marker]

        if root < maximum_root:
            next_square = (root + 1) * (root + 1)
            assert next_square - square == 2 * root + 1
            whole_block = source.get_range(square, next_square - square)
            assert whole_block.to_list() == values[square:next_square]

    _assert_state(source, values)


def test_whole_window_operations_return_a_value_sharing_the_source_tail() -> None:
    values = list(range(24))
    source = _queue_of(values)

    for candidate in (source.take(len(source)), source.drop(0), source.get_range(0, len(source))):
        assert candidate is source
        assert candidate.shares_tail_with(source)
        assert candidate.shares_arena_with(source)

    # Every suffix, including the empty one, keeps the source tail without rebuilding it.
    for start in range(len(values) + 1):
        suffix = source.get_range(start, len(source) - start)
        assert suffix.shares_tail_with(source)
        assert suffix.to_list() == values[start:]

    # A prefix that stops short of the tail necessarily retains a different node.
    prefix = source.take(5)
    assert not prefix.shares_tail_with(source)
    assert prefix.shares_arena_with(source)

    # Independently created queues share neither arena nor history.
    other = _queue_of(values)
    assert not other.shares_arena_with(source)
    assert not other.shares_tail_with(source)


def test_query_discipline_pins_exact_ancestor_query_counts() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    queue: AncestralSliceQueue[int] = AncestralSliceQueue.create(arena)
    for value in range(16):
        queue = queue.add_last(value)

    def query_free_reads() -> None:
        """Every read the O(1) group promises, none of which may consult the ancestor index."""

        assert len(queue) == 16
        assert not queue.is_empty
        assert queue.last == 15
        # Indexed access at the last visible index uses the tail directly.
        assert queue.get_at(15) == 15
        assert queue.to_list() == list(range(16))
        assert list(queue) == list(range(16))
        # The structural audit uses only the arena's O(1) primitives.
        assert queue.validate_structure().count == 16

    _assert_arena_delta(arena, 0, 0, query_free_reads)
    _assert_arena_delta(arena, 0, 1, lambda: _assert_value(queue.first, 0))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_value(queue.get_at(7), 7))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.remove_first(), 15))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.remove_last(), 15))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.drop(7), 9))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.drop(0), 16))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.take(16), 16))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_length(queue.take(7), 7))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_length(queue.take(0), 0))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.get_range(0, 16), 16))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_length(queue.get_range(3, 5), 5))
    # A suffix range keeps the old tail, so it makes no ancestor query.
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.get_range(3, 13), 13))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(queue.get_range(16, 0), 0))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_length(queue.get_range(4, 1), 1))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_split(queue.split_at(7), 7, 9))
    # A split at 0 of a non-empty queue is not query-free: the anchored-empty rule makes the empty
    # prefix retain the node at low_depth - 1, which only an ancestor query can name from the tail.
    _assert_arena_delta(arena, 0, 1, lambda: _assert_split(queue.split_at(0), 0, 16))
    # A split at the length reuses the source tail and only moves the low boundary.
    _assert_arena_delta(arena, 0, 0, lambda: _assert_split(queue.split_at(16), 16, 0))
    _assert_arena_delta(arena, 0, 1, lambda: _assert_popped(queue.try_remove_first(), 0, 15))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_popped(queue.try_remove_last(), 15, 15))
    _assert_arena_delta(arena, 1, 0, lambda: _assert_length(queue.add_last(16), 17))

    # A singleton reuses its retained tail for the front, so its front reads make no query.
    singleton = queue.get_range(4, 1)
    _assert_arena_delta(arena, 0, 0, lambda: _assert_value(singleton.first, 4))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_popped(singleton.try_remove_first(), 4, 0))

    # On an empty queue the two split boundaries coincide, so the split is query-free.
    empty: AncestralSliceQueue[int] = AncestralSliceQueue.create(arena)
    _assert_arena_delta(arena, 0, 0, lambda: _assert_split(empty.split_at(0), 0, 0))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(empty.take(0), 0))
    _assert_arena_delta(arena, 0, 0, lambda: _assert_length(empty.get_range(0, 0), 0))


def test_front_and_back_drains_keep_different_anchors() -> None:
    values = list(range(64))
    source = _queue_of(values)

    front_versions = [source]
    back_versions = [source]
    for _ in values:
        front_versions.append(front_versions[-1].remove_first())
        back_versions.append(back_versions[-1].remove_last())

    for removed in range(len(values) + 1):
        assert front_versions[removed].to_list() == values[removed:]
        assert back_versions[removed].to_list() == values[: len(values) - removed]

    drained_front = front_versions[len(values)]
    drained_back = back_versions[len(values)]
    assert drained_front.is_empty
    assert drained_back.is_empty

    # Both denote the empty sequence, yet they retain different anchors from different histories.
    front_statistics = drained_front.validate_structure()
    back_statistics = drained_back.validate_structure()
    assert front_statistics.anchor_depth == len(values) - 1
    assert back_statistics.anchor_depth == -1
    assert not front_statistics.anchored_at_bottom
    assert back_statistics.anchored_at_bottom
    assert not drained_front.shares_tail_with(drained_back)
    assert drained_front.shares_arena_with(drained_back)

    # Appending to either yields exactly the new value and resurrects no discarded prefix.
    assert drained_front.add_last(-1).to_list() == [-1]
    assert drained_back.add_last(-2).to_list() == [-2]
    _assert_state(source, values)


def test_empty_slices_anchor_before_their_window_and_never_resurrect_a_prefix() -> None:
    values = list(range(16))
    source = _queue_of(values)

    for start in range(len(values) + 1):
        for empty in (source.get_range(start, 0), source.take(start).drop(start)):
            _assert_state(empty, [])
            statistics = empty.validate_structure()
            assert statistics.low_depth == start
            assert statistics.anchor_depth == start - 1
            assert statistics.anchored_at_bottom == (start == 0)
            assert empty.add_last(-1).to_list() == [-1]

    # A singleton emptied from either end appends to exactly one visible value.
    singleton = _queue_of([7])
    assert singleton.remove_first().add_last(8).to_list() == [8]
    assert singleton.remove_last().add_last(9).to_list() == [9]
    _assert_state(singleton, [7])


def test_empty_queue_boundary_operations_stay_anchored() -> None:
    empty: AncestralSliceQueue[int] = AncestralSliceQueue.create_myers()

    _assert_state(empty, [])
    assert empty.take(0) is empty
    assert empty.drop(0) is empty
    assert empty.get_range(0, 0) is empty

    split = empty.split_at(0)
    _assert_state(split.left, [])
    _assert_state(split.right, [])
    assert split.left.add_last(1).to_list() == [1]
    assert split.right.add_last(2).to_list() == [2]


def test_out_of_range_arguments_are_rejected_without_changing_the_source() -> None:
    source = _queue_of([10, 20, 30])

    with pytest.raises(IndexError):
        source.get_at(3)
    with pytest.raises(IndexError):
        source.get_at(-1)
    with pytest.raises(IndexError):
        source.get_range(4, 0)
    with pytest.raises(IndexError):
        source.get_range(-1, 0)
    with pytest.raises(ValueError, match="past the end"):
        source.get_range(2, 2)
    with pytest.raises(ValueError, match="nonnegative"):
        source.get_range(0, -1)
    with pytest.raises(ValueError, match="exceeds"):
        source.take(4)
    with pytest.raises(ValueError, match="nonnegative"):
        source.take(-1)
    with pytest.raises(ValueError, match="exceeds"):
        source.drop(4)
    with pytest.raises(ValueError, match="nonnegative"):
        source.drop(-1)
    with pytest.raises(IndexError):
        source.split_at(4)
    with pytest.raises(IndexError):
        source.split_at(-1)

    _assert_state(source, [10, 20, 30])


def test_create_rejects_an_arena_that_does_not_anchor_at_depth_minus_one() -> None:
    class _FlatBottomArena:
        """An arena whose bottom node is not at depth -1, which the queue must refuse."""

        @property
        def bottom(self) -> int:
            return 0

        @property
        def published_node_count(self) -> int:
            return 0

        def add_leaf(self, parent: int, value: int) -> int:
            raise AssertionError("The queue must reject this arena before using it.")

        def depth_of(self, node: int) -> int:
            return 0

        def parent_of(self, node: int) -> int:
            raise AssertionError("The queue must reject this arena before using it.")

        def ancestor_at_depth(self, node: int, depth: int) -> int:
            raise AssertionError("The queue must reject this arena before using it.")

        def value_at(self, node: int) -> int:
            raise AssertionError("The queue must reject this arena before using it.")

    with pytest.raises(ValueError, match="depth -1"):
        AncestralSliceQueue.create(_FlatBottomArena())

    # The shipped backend does anchor at depth -1, so the same call succeeds with it.
    anchored: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    _assert_state(AncestralSliceQueue.create(anchored), [])


def test_stored_none_is_a_present_value_and_absence_uses_the_result_shape() -> None:
    nothing: list[str | None] = []
    stored_none: list[str | None] = [None]
    stored_tail: list[str | None] = ["tail"]
    both: list[str | None] = [None, "tail"]

    empty: AncestralSliceQueue[str | None] = AncestralSliceQueue.create_myers()
    _assert_state(empty, nothing)

    singleton = empty.add_last(None)
    _assert_state(singleton, stored_none)
    assert singleton.first is None
    assert singleton.last is None

    pair = singleton.add_last("tail")
    _assert_state(pair, both)
    _assert_state(singleton, stored_none)

    popped_front = pair.try_remove_first()
    assert popped_front is not None
    assert popped_front.value is None
    _assert_state(popped_front.rest, stored_tail)

    popped_back = pair.try_remove_last()
    assert popped_back is not None
    assert popped_back.value == "tail"
    _assert_state(popped_back.rest, stored_none)
    _assert_state(pair, both)


def test_iteration_is_repeatable_and_does_not_observe_later_branches() -> None:
    values = list(range(100))
    source = _queue_of(values)
    window = source.get_range(20, 40)
    captured = iter(window)

    branch = window.add_last(-1).add_last(-2)

    assert window.to_list() == values[20:60]
    assert window.to_list() == values[20:60]
    assert branch.to_list() == [*values[20:60], -1, -2]
    # The iterator captured before the branch existed still yields the original path.
    assert list(captured) == values[20:60]
    _assert_state(source, values)


def test_retained_branches_from_whole_and_sliced_versions_stay_independent() -> None:
    values = list(range(200))
    source = _queue_of(values)
    middle = source.get_range(50, 100)

    branches = [
        (middle.add_last(-1), [*values[50:150], -1]),
        (middle.add_last(-2).add_last(-3), [*values[50:150], -2, -3]),
        (middle.remove_first().add_last(-4), [*values[51:150], -4]),
        (middle.remove_last().add_last(-5), [*values[50:149], -5]),
        (middle.take(40).add_last(-6), [*values[50:90], -6]),
        (middle.drop(60).add_last(-7), [*values[110:150], -7]),
        (middle.get_range(30, 0).add_last(-8), [-8]),
    ]
    for branch, expected in branches:
        _assert_state(branch, expected)
        assert branch.shares_arena_with(source)

    _assert_state(middle, values[50:150])
    _assert_state(source, values)


def test_factories_create_independent_queues_with_private_arenas() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    explicit: AncestralSliceQueue[int] = AncestralSliceQueue.create(arena)
    explicit = explicit.add_last(10).add_last(20)
    _assert_state(explicit, [10, 20])
    assert arena.published_node_count == 2
    assert explicit.validate_structure().arena_published_node_count == 2
    assert explicit.arena is arena
    assert explicit.low_depth == 0

    built = _queue_of([1, 2, 3])
    _assert_state(built, [1, 2, 3])
    _assert_state(built.add_last(4), [1, 2, 3, 4])
    _assert_state(built, [1, 2, 3])

    # A queue passed back through the factory is rebuilt onto a private arena.
    rebuilt = AncestralSliceQueue.from_iterable(built)
    _assert_state(rebuilt, [1, 2, 3])
    assert not rebuilt.shares_arena_with(built)

    first: AncestralSliceQueue[int] = AncestralSliceQueue.create_myers()
    second: AncestralSliceQueue[int] = AncestralSliceQueue.create_myers()
    assert not first.shares_arena_with(second)
    assert first.add_last(1).to_list() == [1]
    assert second.add_last(2).to_list() == [2]


def test_validate_structure_rejects_a_window_that_disagrees_with_the_arena() -> None:
    source = _queue_of([1, 2, 3])
    statistics = source.validate_structure()
    assert statistics.count == 3
    assert statistics.low_depth == 0
    assert statistics.tail_depth == 2
    assert statistics.anchor_depth == -1
    assert statistics.anchored_at_bottom
    assert statistics.arena_published_node_count == 3

    inconsistent = AncestralSliceQueue(source.arena, source.tail_handle, source.low_depth, 2)
    with pytest.raises(ValueError, match="disagrees"):
        inconsistent.validate_structure()

    negative_low = AncestralSliceQueue(source.arena, source.tail_handle, -1, 3)
    with pytest.raises(ValueError, match="not a real depth"):
        negative_low.validate_structure()

    negative_count = AncestralSliceQueue(source.arena, source.tail_handle, 0, -1)
    with pytest.raises(ValueError, match="count is negative"):
        negative_count.validate_structure()


def test_repr_renders_the_visible_interval() -> None:
    source = _queue_of([1, 2, 3, 4, 5])
    assert repr(source.get_range(1, 3)) == "AncestralSliceQueue([2, 3, 4])"
    empty: AncestralSliceQueue[int] = AncestralSliceQueue.create_myers()
    assert repr(empty) == "AncestralSliceQueue([])"


def test_equality_and_hashing_stay_by_identity() -> None:
    source = _queue_of([1, 2, 3])
    alias = source
    same_values = _queue_of([1, 2, 3])

    assert source == alias
    assert source != same_values
    assert source.to_list() == same_values.to_list()
    assert len({source, alias, same_values}) == 2


def test_randomized_branching_history_matches_list_models() -> None:
    generator = random.Random(0x511CE)
    histories: list[tuple[AncestralSliceQueue[int], list[int]]] = [
        (AncestralSliceQueue.create_myers(), [])
    ]

    for step in range(1_200):
        source, model = histories[generator.randrange(len(histories))]
        length = len(model)
        choice = generator.randrange(9)

        result: AncestralSliceQueue[int]
        expected: list[int]
        if choice == 1 and length != 0:
            result, expected = source.remove_first(), model[1:]
        elif choice == 2 and length != 0:
            result, expected = source.remove_last(), model[:-1]
        elif choice == 3:
            count = generator.randrange(length + 1)
            result, expected = source.take(count), model[:count]
        elif choice == 4:
            count = generator.randrange(length + 1)
            result, expected = source.drop(count), model[count:]
        elif choice == 5:
            start = generator.randrange(length + 1)
            count = generator.randrange(length - start + 1)
            result, expected = source.get_range(start, count), model[start : start + count]
        elif choice == 6:
            position = generator.randrange(length + 1)
            split = source.split_at(position)
            if generator.randrange(2) == 0:
                result, expected = split.left, model[:position]
            else:
                result, expected = split.right, model[position:]
        elif choice == 7 and length != 0:
            popped_first = source.try_remove_first()
            assert popped_first is not None
            assert popped_first.value == model[0]
            result, expected = popped_first.rest, model[1:]
        elif choice == 8 and length != 0:
            popped_last = source.try_remove_last()
            assert popped_last is not None
            assert popped_last.value == model[-1]
            result, expected = popped_last.rest, model[:-1]
        else:
            result, expected = source.add_last(step), [*model, step]

        # Every retained version keeps denoting exactly what it always denoted.
        assert source.to_list() == model
        assert result.to_list() == expected
        result.validate_structure()
        histories.append((result, expected))

    for queue, model in histories[::53]:
        _assert_state(queue, model)


@settings(max_examples=40, deadline=None)
@given(
    st.lists(st.integers(-50, 50), max_size=40),
    st.integers(0, 40),
    st.integers(0, 40),
)
def test_slicing_agrees_with_list_slicing_for_every_window(
    values: list[int], start: int, count: int
) -> None:
    source = AncestralSliceQueue.from_iterable(values)
    if start > len(values) or count > len(values) - start:
        with pytest.raises((IndexError, ValueError)):
            source.get_range(start, count)
        return

    window = source.get_range(start, count)
    assert window.to_list() == values[start : start + count]
    assert window.take(count).to_list() == values[start : start + count]
    assert window.drop(count).to_list() == []
    split = window.split_at(count)
    assert split.left.to_list() == values[start : start + count]
    assert split.right.to_list() == []
    window.validate_structure()


def test_concurrent_branch_appends_publish_complete_independent_versions() -> None:
    values = list(range(256))
    source = _queue_of(values)
    thread_count = 4
    branches_per_thread = 8

    def build(thread_index: int) -> list[tuple[int, AncestralSliceQueue[int]]]:
        """Append sibling branches while reading level ancestors of the shared source."""

        produced: list[tuple[int, AncestralSliceQueue[int]]] = []
        for offset in range(branches_per_thread):
            marker = -(thread_index * branches_per_thread + offset) - 1
            branch = source.add_last(marker)
            assert len(branch) == len(values) + 1
            assert branch.last == marker
            # Read level ancestors while sibling nodes are being published elsewhere.
            for index in range(offset % 31, len(values), 127):
                assert source.get_at(index) == values[index]
            assert source.get_range(100, 40).to_list() == values[100:140]
            produced.append((marker, branch))
        return produced

    with ThreadPoolExecutor(max_workers=thread_count) as pool:
        published = [item for batch in pool.map(build, range(thread_count)) for item in batch]

    assert len(published) == thread_count * branches_per_thread
    for marker, branch in published:
        assert branch.to_list() == [*values, marker]
    _assert_state(source, values)
