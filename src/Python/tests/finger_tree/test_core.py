"""Persistent measured sequence and derived collection tests."""

from __future__ import annotations

from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.core import (
    FingerTree,
    MeasuredItemSplit,
    MeasuredSplit,
    PersistentDeque,
    ReversibleDeque,
)
from durable7.finger_tree.measured_sequence import (
    MeasuredSequence,
    SequenceSplit,
)
from durable7.finger_tree.measures import (
    MaxMeasure,
    MinMeasure,
    NumberSumMeasure,
)
from durable7.finger_tree.priority_interval import (
    Interval,
    IntervalTree,
    PriorityQueue,
)
from durable7.finger_tree.sorted import (
    SortedBag,
    SortedMap,
    SortedSet,
)


def test_deque_preserves_versions_across_endpoint_and_indexed_edits() -> None:
    basis = PersistentDeque.from_iterable([1, 2, 3, 4])
    inserted = basis.prepend(0).append(5).insert_at(3, 99)
    assert inserted is not None
    edited = inserted.remove_at(4)
    assert edited is not None
    assert basis.to_list() == [1, 2, 3, 4]
    assert edited.to_list() == [0, 1, 2, 99, 4, 5]
    split = edited.split_range(2, 3)
    assert split is not None
    assert split.before.to_list() == [0, 1]
    assert split.range.to_list() == [2, 99, 4]
    assert split.after.to_list() == [5]
    large = PersistentDeque.from_iterable(range(256))
    middle = large.split_range(64, 128)
    assert middle is not None
    assert large.shares_storage_with(middle.range)


@settings(max_examples=150)
@given(
    st.lists(
        st.tuples(st.integers(min_value=0, max_value=100), st.integers(), st.booleans()),
        max_size=300,
    )
)
def test_generated_deque_edit_histories_agree_with_lists(
    operations: list[tuple[int, int, bool]],
) -> None:
    actual: PersistentDeque[int] = PersistentDeque.empty()
    expected: list[int] = []
    retained: list[tuple[PersistentDeque[int], list[int]]] = []
    for raw_index, value, remove in operations:
        retained.append((actual, expected.copy()))
        if remove and expected:
            index = raw_index % len(expected)
            next_actual = actual.remove_at(index)
            assert next_actual is not None
            actual = next_actual
            expected.pop(index)
        else:
            index = raw_index % (len(expected) + 1)
            next_actual = actual.insert_at(index, value)
            assert next_actual is not None
            actual = next_actual
            expected.insert(index, value)
    assert actual.to_list() == expected
    assert all(snapshot.to_list() == model for snapshot, model in retained)


def test_reversible_deque_retains_constant_time_orientation_views() -> None:
    value = ReversibleDeque.from_iterable([1, 2, 3, 4])
    reversed_value = value.reverse()
    assert reversed_value.to_list() == [4, 3, 2, 1]
    assert reversed_value.reverse().to_list() == value.to_list()
    assert reversed_value.shares_storage_with(value)
    assert reversed_value.prepend(5).append(0).to_list() == [5, 4, 3, 2, 1, 0]


def test_measured_tree_splits_and_locates_by_prefix() -> None:
    policy = NumberSumMeasure()
    tree = FingerTree.from_iterable([2, 3, 5, 7], policy)
    assert tree.measure == 17
    assert tree.prefix_measure(3) == 10
    located = tree.try_locate(lambda total: total >= 6)
    assert (located.index, located.measure_before, located.item, located.found) == (2, 5, 5, True)
    split = tree.split(lambda total: total >= 6)
    assert isinstance(split, MeasuredSplit)
    assert (split.left.to_list(), split.right.to_list()) == ([2, 3], [5, 7])
    found = tree.try_split_find(lambda total: total >= 6)
    assert isinstance(found, MeasuredItemSplit) and found.item == 5
    assert (found.left.to_list(), found.right.to_list()) == ([2, 3], [7])

    sequence = MeasuredSequence.from_iterable([2, 3, 5, 7], policy)
    sequence_split = sequence.split_at(2)
    assert isinstance(sequence_split, SequenceSplit)
    assert (sequence_split.left.to_list(), sequence_split.right.to_list()) == ([2, 3], [5, 7])


def test_extremal_measures_preserve_nullable_values() -> None:
    def none_high(left: int | None, right: int | None) -> int:
        if left is None:
            return 0 if right is None else 1
        if right is None:
            return -1
        return (left > right) - (left < right)

    maximum = FingerTree.from_iterable([None, 1], MaxMeasure(none_high)).measure
    minimum = FingerTree.from_iterable([None, 1], MinMeasure(none_high)).measure
    assert maximum.has_value and maximum.value is None
    assert minimum.has_value and minimum.value == 1
    assert not FingerTree.empty(MaxMeasure(none_high)).measure.has_value


def test_sorted_bag_set_and_map_expose_rank_and_navigation_semantics() -> None:
    bag = SortedBag.from_iterable([3, 1, 2, 1, 4])
    assert bag.to_list() == [1, 1, 2, 3, 4]
    assert bag.count_of(1) == 2
    assert bag.get_value_range(2, 3).to_list() == [2, 3]

    sorted_set = SortedSet.from_iterable([3.0, 1.0, 2.0, 1.0, 4.0])
    assert sorted_set.to_list() == [1.0, 2.0, 3.0, 4.0]
    assert sorted_set.floor(2.5) == 2
    assert sorted_set.ceiling(2.5) == 3
    assert sorted_set.lower(1) is None

    sorted_map = SortedMap.from_iterable([(2.0, "two"), (1.0, "one"), (2.0, "TWO")])
    assert sorted_map.keys() == [1.0, 2.0]
    assert sorted_map.get(2.0) == "TWO"
    floor = sorted_map.floor_entry(1.5)
    assert floor is not None and floor.key == 1


def test_priority_queue_keeps_stable_equal_priority_order() -> None:
    queue: PriorityQueue[str, int] = PriorityQueue.empty()
    queue = queue.enqueue("a", 2).enqueue("b", 1).enqueue("c", 1).enqueue("d", 3)
    output: list[str] = []
    while not queue.is_empty:
        result = queue.dequeue()
        assert result is not None
        output.append(result.entry.value)
        queue = result.queue
    assert output == ["b", "c", "a", "d"]


def test_interval_tree_performs_closed_overlap_removal_and_coalescing() -> None:
    tree = IntervalTree.from_iterable(
        [Interval(5, 8), Interval(1, 3), Interval(2, 6), Interval(10, 12)]
    )
    assert [(item.low, item.high) for item in tree.find_overlaps(Interval(3, 5))] == [
        (1, 3),
        (2, 6),
        (5, 8),
    ]
    assert tree.find_containing(11) == Interval(10, 12)
    assert [(item.low, item.high) for item in tree.coalesce()] == [(1, 8), (10, 12)]
    assert not tree.remove(Interval(2, 6)).contains(Interval(2, 6))


def test_interval_tree_preserves_nullable_endpoints() -> None:
    def none_high(left: int | None, right: int | None) -> int:
        if left is None:
            return 0 if right is None else 1
        if right is None:
            return -1
        return (left > right) - (left < right)

    interval = Interval(None, None, none_high)
    tree = IntervalTree.from_iterable([interval], none_high)
    assert tree.contains(interval)
    assert tree.find_containing(None) == interval
    assert tree.find_overlaps(interval) == [interval]
