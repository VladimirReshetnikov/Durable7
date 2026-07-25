from __future__ import annotations

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.rrb_vector import RrbVector


def test_rrb_crosses_regular_and_relaxed_radix_boundaries() -> None:
    basis = RrbVector.from_iterable(range(5_000))
    assert len(basis) == 5_000
    assert [basis.get(index) for index in (0, 31, 32, 1_023, 1_024, 4_999)] == [
        0,
        31,
        32,
        1_023,
        1_024,
        4_999,
    ]
    statistics = basis.validate_structure()
    assert statistics.count == 5_000
    assert statistics.maximum_branching_factor <= 32
    assert statistics.maximum_leaf_length <= 32

    split = basis.split_at(1_777)
    assert split is not None
    assert len(split.left) == 1_777
    assert split.right.front() == 1_777
    restored = split.left.concat(split.right)
    assert restored.to_list() == basis.to_list()
    assert basis.shared_leaf_count(split.left) > 0
    assert restored.validate_structure().count == 5_000

    relaxed = RrbVector.from_iterable(range(1_025)).concat(
        RrbVector.from_iterable(range(1_025, 1_125))
    )
    assert relaxed.to_list() == list(range(1_125))
    assert relaxed.validate_structure().relaxed_branch_count > 0


def test_rrb_builder_snapshots_are_cached_isolated_and_versioned() -> None:
    builder = RrbVector[int].builder()
    builder.append_all(range(100))
    first = builder.to_immutable()
    assert builder.to_immutable() is first
    builder.append(100)
    second = builder.to_immutable()
    assert first.to_list() == list(range(100))
    assert second.to_list() == list(range(101))
    assert second.shared_leaf_count(first) > 0

    builder.append_all((101, 102))
    iterator = iter(builder)
    assert next(iterator) == 0
    builder.append(103)
    with pytest.raises(RuntimeError, match="modified during iteration"):
        next(iterator)


def test_rrb_point_range_and_python_sequence_contracts() -> None:
    marker = object()
    basis = RrbVector.from_iterable((object(), marker, object()))
    assert basis.set_item(1, marker) is basis
    replacement = object()
    changed = basis.set_item(1, replacement)
    assert changed is not None and changed[1] is replacement
    assert basis[1] is marker
    assert basis[-1] is basis.back()
    assert basis[1:3].to_list() == [marker, basis.back()]
    assert basis[::-1].to_list() == list(reversed(basis.to_list()))
    with pytest.raises(IndexError):
        _ = basis[3]

    assert basis.split_at(-1) is None
    assert basis.split_at(4) is None
    assert basis.insert_range(1, ()) is basis
    assert basis.remove_range(1, 0) is basis
    assert basis.remove_range(-1, 1) is None
    assert basis.remove_range(2, 2) is None
    assert RrbVector.empty().clear().is_empty
    assert RrbVector.empty().try_remove_last() is None
    pop = basis.try_remove_last()
    assert pop is not None and pop.value is basis.back()
    assert pop.rest.to_list() == basis.to_list()[:-1]


@settings(max_examples=80, deadline=None)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=0, max_value=200),
            st.integers(),
            st.booleans(),
        ),
        max_size=180,
    )
)
def test_rrb_random_edits_match_list_model(
    operations: list[tuple[int, int, bool]],
) -> None:
    vector: RrbVector[int] = RrbVector.empty()
    model: list[int] = []
    retained: list[tuple[RrbVector[int], list[int]]] = []
    for raw_index, value, remove in operations:
        if remove and model:
            index = raw_index % len(model)
            updated = vector.remove_at(index)
            assert updated is not None
            vector = updated
            model.pop(index)
        else:
            index = raw_index % (len(model) + 1)
            updated = vector.insert_at(index, value)
            assert updated is not None
            vector = updated
            model.insert(index, value)
        if raw_index % 17 == 0:
            retained.append((vector, model.copy()))
    assert vector.to_list() == model
    assert vector.validate_structure().count == len(model)
    for snapshot, expected in retained:
        assert snapshot.to_list() == expected
        snapshot.validate_structure()


@settings(max_examples=50, deadline=None)
@given(st.lists(st.lists(st.integers(), max_size=80), max_size=20))
def test_rrb_concat_partitions_match_flattened_model(parts: list[list[int]]) -> None:
    vector: RrbVector[int] = RrbVector.empty()
    expected: list[int] = []
    for part in parts:
        vector = vector.concat(RrbVector.from_iterable(part))
        expected.extend(part)
    assert vector.to_list() == expected
    assert vector.validate_structure().count == len(expected)
