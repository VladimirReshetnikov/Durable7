"""Signed-order persistent Patricia map and set tests."""

from __future__ import annotations

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.hamt import (
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


def test_int_map_cursor_exposes_every_ordered_gap_and_nullable_entry() -> None:
    keys = [-(1 << 31), -1, 0, 17, (1 << 31) - 1]
    map_value = PersistentIntMap.from_items([(key, None if key == 0 else str(key)) for key in keys])

    for position in range(len(keys) + 1):
        cursor = map_value.cursor(position)
        assert cursor.position == position
        assert cursor.count == len(keys)
        assert cursor.is_at_start == (position == 0)
        assert cursor.is_at_end == (position == len(keys))
        assert cursor.snapshot() is map_value
        previous = cursor.peek_previous()
        next_entry = cursor.peek_next()
        assert (None if previous is None else previous.key) == (
            None if position == 0 else keys[position - 1]
        )
        assert (None if next_entry is None else next_entry.key) == (
            None if position == len(keys) else keys[position]
        )

    assert map_value.lower_bound_cursor(-2).position == 1
    assert map_value.upper_bound_cursor(-1).position == 2
    assert map_value.lower_bound_cursor(18).position == 4
    assert map_value.upper_bound_cursor((1 << 31) - 1).position == len(keys)
    exact = map_value.cursor_at_key(0)
    assert exact.found
    exact_entry = exact.cursor.peek_next()
    assert exact_entry is not None
    assert exact_entry.key == 0
    assert exact_entry.value is None
    miss = map_value.cursor_at_key(1)
    assert not miss.found
    assert miss.cursor.position == 3
    miss_entry = miss.cursor.peek_next()
    assert miss_entry is not None
    assert miss_entry.key == 17

    assert map_value.cursor_at_end().snapshot() is map_value
    with pytest.raises(ValueError):
        map_value.cursor(-1)
    with pytest.raises(ValueError):
        map_value.cursor(map_value.size + 1)
    with pytest.raises(IndexError):
        map_value.cursor().move_previous()
    with pytest.raises(IndexError):
        map_value.cursor_at_end().move_next()


def test_int_map_cursor_edits_branch_persistently_at_the_focused_gap() -> None:
    source = PersistentIntMap.from_items([(-10, "a"), (0, None), (10, "c")])
    at_zero = source.cursor_at_key(0)
    assert at_zero.found
    assert at_zero.cursor.set_next_value(None).snapshot() is source

    updated = at_zero.cursor.set_next_value("b")
    assert updated.position == 1
    assert updated.snapshot().get(0) == "b"
    assert source.get(0) is None
    assert [key for key, _value in at_zero.cursor.delete_next().snapshot()] == [-10, 10]
    assert [key for key, _value in at_zero.cursor.delete_previous().snapshot()] == [0, 10]

    inserted = source.cursor_at_key(5).cursor.insert(5, "five")
    assert inserted.position == 3
    assert [key for key, _value in inserted.snapshot()] == [-10, 0, 5, 10]
    assert [key for key, _value in source] == [-10, 0, 10]
    assert source.lower_bound_cursor(-5).put(-5, "minus five").position == 2
    assert at_zero.cursor.put(0, "zero").position == 1

    with pytest.raises(KeyError):
        at_zero.cursor.insert(0, "duplicate")
    with pytest.raises(ValueError, match="belongs at gap"):
        source.cursor().insert(5, "wrong gap")
    with pytest.raises(IndexError):
        source.cursor_at_end().set_next_value("none")
    with pytest.raises(IndexError):
        source.cursor().delete_previous()
    with pytest.raises(IndexError):
        source.cursor_at_end().delete_next()


def test_long_map_and_set_cursors_cover_signed_boundaries_and_edits() -> None:
    minimum = -(1 << 63)
    maximum = (1 << 63) - 1
    keys = [minimum, -1, 0, 1 << 40, maximum]
    long_map = PersistentLongMap.from_items([(key, key) for key in keys])
    assert long_map.lower_bound_cursor(minimum).position == 0
    assert long_map.upper_bound_cursor(minimum).position == 1
    assert long_map.lower_bound_cursor(1).position == 3
    assert long_map.upper_bound_cursor(maximum).position == len(keys)
    exact = long_map.cursor_at_key(1 << 40)
    assert exact.found
    assert exact.cursor.set_next_value(42).snapshot().get(1 << 40) == 42
    inserted = long_map.cursor_at_key(-2).cursor.insert(-2, 99)
    assert inserted.position == 2
    assert [key for key, _value in inserted.snapshot()] == [
        minimum,
        -2,
        -1,
        0,
        1 << 40,
        maximum,
    ]

    int_set = PersistentIntSet.from_values([-(1 << 31), -1, 0, (1 << 31) - 1])
    int_miss = int_set.cursor_at_item(-2)
    assert not int_miss.found
    int_added = int_miss.cursor.add(-2)
    assert int_added.position == 2
    assert list(int_added.snapshot()) == [-(1 << 31), -2, -1, 0, (1 << 31) - 1]
    assert int_set.cursor_at_item(0).cursor.add(0).snapshot() is int_set
    assert list(int_set.cursor_at_item(0).cursor.delete_next().snapshot()) == [
        -(1 << 31),
        -1,
        (1 << 31) - 1,
    ]

    long_set = PersistentLongSet.from_values([minimum, -1, 0, maximum])
    assert long_set.upper_bound_cursor(minimum).position == 1
    long_added = long_set.cursor_at_item(1).cursor.add(1)
    assert long_added.position == 4
    assert list(long_added.snapshot()) == [minimum, -1, 0, 1, maximum]
    with pytest.raises(ValueError, match="belongs at gap"):
        long_set.cursor().add(1)


@settings(max_examples=300)
@given(
    st.lists(st.integers(min_value=-500, max_value=500), max_size=100, unique=True),
    st.integers(min_value=-550, max_value=550),
)
def test_int_map_cursor_search_ranks_agree_with_sorted_model(
    generated: list[int], probe: int
) -> None:
    keys = sorted(generated)
    map_value = PersistentIntMap.from_items([(key, key) for key in keys])
    lower = next((index for index, key in enumerate(keys) if key >= probe), len(keys))
    upper = next((index for index, key in enumerate(keys) if key > probe), len(keys))
    assert map_value.lower_bound_cursor(probe).position == lower
    assert map_value.upper_bound_cursor(probe).position == upper
    exact = map_value.cursor_at_key(probe)
    assert exact.cursor.position == lower
    assert exact.found == (probe in keys)
