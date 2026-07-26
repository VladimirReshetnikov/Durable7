"""Tests for the persistent interval-keyed map.

Covers lexicographic ordering of equal-low keys and duplicate rejection, first-key retention with
receiver identity for equal values, rejection of intervals invalid under the map comparator,
overlap and stabbing queries driven by the cached maximum endpoint, exact-key removal leaving
retained snapshots intact, and policy preservation with annotation validation.
"""

import pytest

from durable7.finger_tree import (
    DuplicateIntervalError,
    Interval,
    PersistentIntervalMap,
)


def test_orders_same_low_keys_lexicographically_and_rejects_duplicates() -> None:
    interval_map = (
        PersistentIntervalMap[int, str]
        .empty()
        .add(Interval(1, 5), "wide")
        .add(Interval(1, 3), "narrow")
        .add(Interval(0, 9), "first")
    )
    assert [(entry.interval.low, entry.interval.high) for entry in interval_map] == [
        (0, 9),
        (1, 3),
        (1, 5),
    ]
    with pytest.raises(DuplicateIntervalError):
        interval_map.add(Interval(1, 3), "duplicate")


def test_retains_first_key_and_returns_self_for_equal_values() -> None:
    first = Interval(1, 3)
    equal = Interval(1, 3)
    interval_map = PersistentIntervalMap[int, str].empty().add(first, "value")
    assert interval_map.set(equal, "value") is interval_map
    lookup = interval_map.set(equal, "changed").try_get_entry(equal)
    assert lookup.found and lookup.entry is not None
    assert lookup.entry.interval is first
    assert lookup.entry.value == "changed"


def test_rejects_intervals_invalid_under_the_map_comparator() -> None:
    def descending(left: int, right: int) -> int:
        return right - left

    forged = Interval(5, 1, descending)
    interval_map = PersistentIntervalMap[int, str].empty()
    with pytest.raises(ValueError):
        interval_map.add(forged, "invalid")
    with pytest.raises(ValueError):
        interval_map.find_overlap(forged)


def test_finds_overlaps_and_stabbing_matches_through_cached_maximum() -> None:
    interval_map = PersistentIntervalMap.from_items(
        (
            (Interval(0, 2), "a"),
            (Interval(4, 9), "b"),
            (Interval(5, 6), "c"),
            (Interval(11, 12), "d"),
        )
    )
    assert [entry.value for entry in interval_map.find_overlaps(Interval(6, 10))] == ["b", "c"]
    containing = interval_map.find_containing(5)
    assert containing is not None and containing.value == "b"
    assert interval_map.count_overlaps(Interval(6, 10)) == 2


def test_removes_exact_keys_and_preserves_retained_snapshots() -> None:
    key = Interval(1, 2)
    source = PersistentIntervalMap.from_items(((key, "a"), (Interval(3, 4), "b")))
    branch = source.remove(Interval(1, 2))
    assert not branch.contains_key(key)
    assert source.contains_key(key)
    assert branch.remove(key) is branch

    larger = PersistentIntervalMap.from_items(
        (Interval(index, index + 1), index) for index in range(16)
    )
    assert larger.remove(Interval(7, 8)).shares_storage_with(larger)


def test_preserves_policies_and_validates_annotations() -> None:
    def descending(left: int, right: int) -> int:
        return right - left

    def case_insensitive(left: str, right: str) -> bool:
        return left.casefold() == right.casefold()

    interval_map = (
        PersistentIntervalMap[int, str]
        .empty(descending, case_insensitive)
        .add(Interval(5, 3, descending), "Value")
    )
    assert interval_map.set(Interval(5, 3, descending), "value") is interval_map
    assert interval_map.comparator is descending
    assert interval_map.value_equals is case_insensitive
    assert interval_map.validate_structure()
    assert interval_map.clear().validate_structure()
