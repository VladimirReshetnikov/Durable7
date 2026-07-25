from __future__ import annotations

import math
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.priority_search_queue import (
    PrioritySearchEntry,
    PrioritySearchQueue,
)


def test_psq_minimum_cache_ties_by_key_and_prunes_ranges() -> None:
    queue = PrioritySearchQueue.from_iterable(
        (
            PrioritySearchEntry(4, 1, "d"),
            PrioritySearchEntry(2, 1, "b"),
            PrioritySearchEntry(3, 9, "c"),
            PrioritySearchEntry(8, -1, "outside"),
        )
    )
    assert queue.minimum == PrioritySearchEntry(8, -1, "outside")
    assert list(queue.enumerate_at_most(2, 4, 2)) == [
        PrioritySearchEntry(2, 1, "b"),
        PrioritySearchEntry(4, 1, "d"),
    ]
    assert queue.validate_structure().count == 4
    with pytest.raises(ValueError, match="minimum key"):
        list(queue.enumerate_at_most(4, 2, 9))


@dataclass(frozen=True)
class _Key:
    order: int
    representative: int


def _key_comparator(left: _Key, right: _Key) -> int:
    return left.order - right.order


def test_psq_retains_first_keys_supports_noops_and_try_results() -> None:
    priority = object()

    def priority_comparator(_left: object, _right: object) -> int:
        return 0

    original_key = _Key(1, 1)
    probe_key = _Key(1, 2)
    value = object()
    queue: PrioritySearchQueue[_Key, object, object] = PrioritySearchQueue.empty(
        _key_comparator, priority_comparator
    )
    queue = queue.set_item(original_key, priority, value)
    assert queue.set_item(probe_key, priority, value) is queue
    found = queue.get_entry(probe_key)
    assert found is not None and found.key is original_key and found.value is value
    duplicate = queue.try_add(probe_key, object(), object())
    assert not duplicate.added and duplicate.queue is queue
    removed = queue.try_remove(probe_key)
    assert removed.removed and removed.entry is found and removed.queue.is_empty
    missing = queue.try_remove(_Key(2, 0))
    assert not missing.removed and missing.entry is None and missing.queue is queue


def test_psq_ascending_updates_remain_avl_and_share_untouched_nodes() -> None:
    basis: PrioritySearchQueue[int, int, str] = PrioritySearchQueue.empty()
    for key in range(4_096):
        basis = basis.set_item(key, 4_096 - key, str(key))
    statistics = basis.validate_structure()
    assert statistics.maximum_absolute_balance_factor <= 1
    assert basis.height <= 2 * math.ceil(math.log2(len(basis) + 1))
    edited = basis.set_item(2_048, -1, "changed")
    removed = basis.remove(1_024)
    assert edited.shared_node_count(basis) > 3_900
    assert removed.shared_node_count(basis) > 3_900
    assert basis.get_entry(2_048) == PrioritySearchEntry(2_048, 2_048, "2048")
    assert edited.minimum == PrioritySearchEntry(2_048, -1, "changed")
    basis.validate_structure()
    edited.validate_structure()
    removed.validate_structure()


def test_psq_empty_views_and_failure_atomicity() -> None:
    empty: PrioritySearchQueue[int, int, str] = PrioritySearchQueue.empty()
    assert empty.minimum_or_none() is None
    assert empty.minimum_view() is None
    assert empty.remove(1) is empty
    with pytest.raises(IndexError, match="empty"):
        _ = empty.minimum
    with pytest.raises(IndexError, match="empty"):
        empty.delete_minimum()

    class Comparator:
        fail = False

        def __call__(self, left: int, right: int) -> int:
            if self.fail:
                raise RuntimeError("comparison failed")
            return left - right

    key_comparator = Comparator()
    queue: PrioritySearchQueue[int, int, str] = PrioritySearchQueue.empty(key_comparator)
    queue = queue.set_item(1, 3, "one")
    key_comparator.fail = True
    with pytest.raises(RuntimeError, match="comparison failed"):
        queue.set_item(2, 1, "two")
    key_comparator.fail = False
    assert list(queue) == [PrioritySearchEntry(1, 3, "one")]
    assert queue.validate_structure().count == 1


def test_psq_immutable_snapshots_are_safe_for_parallel_readers() -> None:
    queue = PrioritySearchQueue.from_iterable(
        PrioritySearchEntry(key, key % 31, str(key)) for key in range(2_000)
    )

    def read(worker: int) -> tuple[int, int, int]:
        key = worker * 97 % 2_000
        entry = queue.get_entry(key)
        assert entry is not None
        matches = list(queue.enumerate_at_most(100, 1_500, 5))
        return entry.key, len(matches), queue.validate_structure().count

    with ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(read, range(64)))
    assert all(match_count == results[0][1] for _, match_count, _ in results)
    assert all(count == 2_000 for _, _, count in results)


@settings(max_examples=80, deadline=None)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=-100, max_value=100),
            st.integers(min_value=-20, max_value=20),
            st.booleans(),
        ),
        max_size=350,
    )
)
def test_psq_random_updates_and_drains_match_map_model(
    operations: list[tuple[int, int, bool]],
) -> None:
    queue: PrioritySearchQueue[int, int, str] = PrioritySearchQueue.empty()
    model: dict[int, tuple[int, str]] = {}
    snapshots: list[tuple[PrioritySearchQueue[int, int, str], dict[int, tuple[int, str]]]] = []
    for index, (key, priority, remove) in enumerate(operations):
        if remove:
            queue = queue.remove(key)
            model.pop(key, None)
        else:
            queue = queue.set_item(key, priority, str(key))
            model[key] = priority, str(key)
        if index % 53 == 0:
            snapshots.append((queue, model.copy()))
    assert [(entry.key, entry.priority) for entry in queue] == [
        (key, value[0]) for key, value in sorted(model.items())
    ]
    queue.validate_structure()
    expected = sorted(model.items(), key=lambda item: (item[1][0], item[0]))
    actual: list[tuple[int, int]] = []
    while not queue.is_empty:
        view = queue.delete_minimum()
        actual.append((view.entry.key, view.entry.priority))
        queue = view.remainder
    assert actual == [(key, value[0]) for key, value in expected]
    for snapshot, expected_snapshot in snapshots:
        assert {entry.key: (entry.priority, entry.value) for entry in snapshot} == expected_snapshot
        snapshot.validate_structure()
