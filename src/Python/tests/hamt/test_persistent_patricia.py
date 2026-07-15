"""Signed-order persistent Patricia map and set tests."""

from __future__ import annotations

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.hamt import (
    PersistentIntMap,
    PersistentIntSet,
    PersistentLongMap,
    PersistentLongSet,
)


def test_int32_and_int64_keys_enumerate_in_signed_order() -> None:
    int_keys = [-(1 << 31), -1, 0, 1, (1 << 31) - 1]
    int_map = PersistentIntMap.from_items([(key, str(key)) for key in reversed(int_keys)])
    assert [key for key, _value in int_map] == int_keys

    long_keys = [-(1 << 63), -1, 0, 1, (1 << 63) - 1]
    long_map = PersistentLongMap.from_items([(key, str(key)) for key in reversed(long_keys)])
    assert [key for key, _value in long_map] == long_keys


def test_key_ranges_are_checked_before_lookup_or_update() -> None:
    int_map = PersistentIntMap[int].empty()
    with pytest.raises(ValueError):
        int_map.put(-(1 << 31) - 1, 0)
    with pytest.raises(ValueError):
        int_map.contains_key(1 << 31)

    long_map = PersistentLongMap[int].empty()
    with pytest.raises(ValueError):
        long_map.put(-(1 << 63) - 1, 0)
    with pytest.raises(ValueError):
        long_map.get(1 << 63)


def test_map_algebra_combines_values_and_preserves_identity_no_ops() -> None:
    left = PersistentIntMap.from_items([(-4, 40), (2, 20), (7, 70)])
    right = PersistentIntMap.from_items([(2, 3), (7, 5), (9, 90)])

    def combine(key: int, first: int, second: int) -> int:
        return key + first * 100 + second

    assert list(left.union(right, combine)) == [
        (-4, 40),
        (2, 2005),
        (7, 7012),
        (9, 90),
    ]
    assert list(left.intersect(right, combine)) == [(2, 2005), (7, 7012)]
    assert list(left.except_(right)) == [(-4, 40)]
    assert left.put(2, 20) is left
    assert left.remove(1_000) is left
    assert left.clear().is_empty


def test_nullable_values_remain_distinguishable_by_contains_key() -> None:
    map_value = PersistentIntMap[int | None].empty().put(1, None)
    assert map_value.contains_key(1)
    assert map_value.get(1) is None
    assert not map_value.contains_key(2)


@settings(max_examples=150)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=-(1 << 31), max_value=(1 << 31) - 1), st.integers(), st.booleans()
        ),
        max_size=300,
    )
)
def test_random_int_map_histories_agree_with_sorted_models(
    operations: list[tuple[int, int, bool]],
) -> None:
    actual = PersistentIntMap[int].empty()
    expected: dict[int, int] = {}
    retained: list[tuple[PersistentIntMap[int], dict[int, int]]] = []
    for key, value, remove in operations:
        if remove:
            actual = actual.remove(key)
            expected.pop(key, None)
        else:
            actual = actual.put(key, value)
            expected[key] = value
        if key & 15 == 0:
            retained.append((actual, expected.copy()))
    assert list(actual) == sorted(expected.items())
    for snapshot, model in retained:
        assert list(snapshot) == sorted(model.items())


def test_patricia_sets_support_structural_algebra() -> None:
    left = PersistentIntSet.from_values([-3, -1, 1, 3])
    right = PersistentIntSet.from_values([-1, 0, 1])
    assert list(left.union(right)) == [-3, -1, 0, 1, 3]
    assert list(left.intersect(right)) == [-1, 1]
    assert list(left.except_(right)) == [-3, 3]
    assert left.add(1) is left
    assert left.remove(99) is left

    long_set = PersistentLongSet.from_values([-1, 0, 1])
    assert list(long_set.remove(0)) == [-1, 1]
    assert long_set.contains(-1)
    assert not long_set.is_empty


def test_long_map_endpoint_updates_preserve_old_versions() -> None:
    empty = PersistentLongMap[str].empty()
    first = empty.put(-(1 << 63), "minimum")
    second = first.put((1 << 63) - 1, "maximum")
    assert empty.is_empty
    assert list(first) == [(-(1 << 63), "minimum")]
    assert list(second) == [
        (-(1 << 63), "minimum"),
        ((1 << 63) - 1, "maximum"),
    ]
    assert second.shares_root_with(second)
    assert not first.shares_root_with(second)
