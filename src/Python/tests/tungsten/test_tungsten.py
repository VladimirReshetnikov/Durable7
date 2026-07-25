"""Application-leaf Tungsten collection parity tests."""

from __future__ import annotations

from dataclasses import dataclass

from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.hamt.hash_policy import HashPolicy, create_hash_policy
from durable7.tungsten import (
    PersistentAssociation,
    PersistentList,
)


def test_persistent_list_supports_tungsten_vocabulary_and_identity_contracts() -> None:
    basis = PersistentList.from_iterable([1, 2, 3])
    assert basis.prepend(0).append(4).to_list() == [0, 1, 2, 3, 4]
    inserted = basis.insert_range(1, [8, 9])
    removed = basis.remove_range(1, 1)
    taken = basis.take_last(2)
    dropped = basis.drop_last(2)
    assert inserted is not None and inserted.to_list() == [1, 8, 9, 2, 3]
    assert removed is not None and removed.to_list() == [1, 3]
    assert taken is not None and taken.to_list() == [2, 3]
    assert dropped is not None and dropped.to_list() == [1]
    assert basis.reverse().to_list() == [3, 2, 1]
    assert basis.insert_range(1, []) is basis
    assert basis.remove_range(1, 0) is basis
    assert basis.get_range(0, len(basis)) is basis
    replaced = basis.set_item(1, 2)
    assert replaced is not None and replaced is not basis and replaced.to_list() == basis.to_list()


def test_association_set_preserves_position_while_append_and_prepend_move_keys() -> None:
    basis = PersistentAssociation.from_pairs([("a", 1), ("b", 2), ("c", 3)])
    assert basis.set_item("b", 20).to_list() == [("a", 1), ("b", 20), ("c", 3)]
    assert basis.append("a", 10).to_list() == [("b", 2), ("c", 3), ("a", 10)]
    assert basis.prepend("c", 30).to_list() == [("c", 30), ("a", 1), ("b", 2)]
    inserted = basis.insert(2, "a", 10)
    assert inserted is not None and inserted.to_list() == [("b", 2), ("a", 10), ("c", 3)]
    assert basis.set_item("a", 1) is basis
    assert basis.append("c", 3) is basis
    assert basis.prepend("a", 1) is basis
    assert basis.get_range(0, len(basis)) is basis


@dataclass(frozen=True)
class _Key:
    text: str
    identifier: int


def test_association_retains_stored_representatives_under_custom_policy() -> None:
    policy: HashPolicy[_Key] = create_hash_policy(
        lambda key: len(key.text), lambda left, right: left.text == right.text
    )
    original, probe = _Key("x", 1), _Key("x", 2)
    association: PersistentAssociation[_Key, int] = PersistentAssociation.empty(policy)
    association = association.set_item(original, 1).set_item(probe, 2)
    assert len(association) == 1
    assert association.get_stored_key(probe) is original
    assert association.first() == (original, 2)
    moved = association.append(probe, 3)
    assert moved.get_stored_key(original) is probe


def test_association_relabels_exhausted_midpoint_gaps_without_changing_order() -> None:
    association = PersistentAssociation.from_pairs([(0, 0), (1, 1)])
    model = [(0, 0), (1, 1)]
    for key in range(2, 500):
        next_association = association.insert(1, key, key)
        assert next_association is not None
        association = next_association
        model.insert(1, (key, key))
    assert association.to_list() == model
    assert len(association) == 500


@settings(max_examples=100)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=0, max_value=30),
            st.integers(),
            st.integers(min_value=0, max_value=3),
        ),
        max_size=300,
    )
)
def test_generated_association_histories_agree_with_ordered_map_models(
    commands: list[tuple[int, int, int]],
) -> None:
    actual: PersistentAssociation[int, int] = PersistentAssociation.empty()
    model: list[tuple[int, int]] = []
    retained: list[tuple[PersistentAssociation[int, int], list[tuple[int, int]]]] = []
    for key, value, operation in commands:
        retained.append((actual, model.copy()))
        existing = next((i for i, entry in enumerate(model) if entry[0] == key), -1)
        if operation == 0:
            actual = actual.set_item(key, value)
            if existing < 0:
                model.append((key, value))
            else:
                model[existing] = (key, value)
        elif operation == 1:
            actual = actual.append(key, value)
            if existing >= 0:
                model.pop(existing)
            model.append((key, value))
        elif operation == 2:
            actual = actual.prepend(key, value)
            if existing >= 0:
                model.pop(existing)
            model.insert(0, (key, value))
        else:
            actual = actual.remove(key)
            if existing >= 0:
                model.pop(existing)
    assert actual.to_list() == model
    assert all(snapshot.to_list() == expected for snapshot, expected in retained)
