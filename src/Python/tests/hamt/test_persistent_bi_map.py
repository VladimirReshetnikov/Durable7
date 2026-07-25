"""Contract and model tests for the persistent bidirectional map."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.hamt import (
    BiMapConflictError,
    BiMapLookupResult,
    HashPolicy,
    PersistentBiMap,
    create_hash_policy,
)


def test_policies_lookup_and_cached_inverse() -> None:
    keys: HashPolicy[str] = create_hash_policy(
        lambda _value: 0, lambda a, b: a.casefold() == b.casefold()
    )
    values: HashPolicy[int] = create_hash_policy(hash, lambda a, b: a == b)
    mapping = PersistentBiMap.empty(keys, values).add("One", 1).add("Two", 2)

    assert mapping.key_policy is keys
    assert mapping.value_policy is values
    assert mapping.get("ONE") == BiMapLookupResult(True, 1)
    assert mapping.get_key(2) == BiMapLookupResult(True, "Two")
    assert mapping.inverse is mapping.inverse
    assert mapping.inverse.inverse is mapping
    assert mapping.inverse[1] == "One"
    assert mapping.validate_structure()


def test_strict_duplicates_and_nonthrowing_add() -> None:
    policy: HashPolicy[str] = create_hash_policy(
        lambda _value: 0, lambda a, b: a.casefold() == b.casefold()
    )
    mapping = PersistentBiMap.empty(policy, policy).add("Key", "Value")

    with pytest.raises(BiMapConflictError, match="key") as key_problem:
        mapping.add("KEY", "other")
    assert key_problem.value.conflict == "key"
    with pytest.raises(BiMapConflictError, match="value") as value_problem:
        mapping.add("other", "VALUE")
    assert value_problem.value.conflict == "value"
    key_result = mapping.try_add("KEY", "other")
    value_result = mapping.try_add("other", "VALUE")
    assert not key_result.added and key_result.map is mapping and key_result.conflict == "key"
    assert (
        not value_result.added and value_result.map is mapping and value_result.conflict == "value"
    )


@dataclass(frozen=True)
class _Token:
    text: str
    identity: int


def test_set_uses_value_policy_and_preserves_representatives() -> None:
    policy: HashPolicy[_Token] = create_hash_policy(
        lambda _value: 0,
        lambda a, b: a.text.casefold() == b.text.casefold(),
    )
    key = _Token("Key", 1)
    value = _Token("Value", 2)
    mapping = (
        PersistentBiMap.empty(policy, policy)
        .add(key, value)
        .add(_Token("Other", 3), _Token("Claimed", 4))
    )

    same = mapping.set(_Token("KEY", 5), _Token("VALUE", 6))
    assert same is mapping
    assert same.get_key(_Token("value", 7)).value is key
    assert same.get(_Token("key", 8)).value is value
    with pytest.raises(BiMapConflictError):
        mapping.set(_Token("key", 9), _Token("CLAIMED", 10))

    replacement = _Token("Replacement", 11)
    changed = mapping.set(_Token("key", 12), replacement)
    assert changed.get_key(replacement).value is key
    assert not changed.contains_value(value)
    assert mapping.get(key).value is value
    assert changed.validate_structure()


def test_none_representatives_are_presence_safe() -> None:
    mapping = PersistentBiMap[str | None, int | None].empty().add(None, 1).add("none-value", None)

    assert mapping.get(None) == BiMapLookupResult(True, 1)
    assert mapping.get_key(None) == BiMapLookupResult(True, "none-value")
    assert mapping.get("missing") == BiMapLookupResult(False)
    assert mapping.validate_structure()


def test_symmetric_removal_enumeration_and_clear() -> None:
    mapping = PersistentBiMap.from_items([(1, "one"), (2, "two"), (3, "three")])
    assert mapping.try_remove_key(9).map is mapping
    assert mapping.try_remove_value("nine").map is mapping
    assert mapping.remove_key(9) is mapping
    assert mapping.remove_value("nine") is mapping
    assert list(mapping.keys()) == [entry.key for entry in mapping]
    assert list(mapping.values()) == [entry.value for entry in mapping]

    first = mapping.try_remove_key(1)
    assert first.removed and first.value == "one"
    second = first.map.try_remove_value("two")
    assert second.removed and second.value == 2
    remaining = second.map.remove_key(3)
    assert remaining.is_empty
    assert remaining.clear() is remaining
    assert len(mapping) == 3


def test_policy_failure_is_atomic() -> None:
    fail = False

    def value_hash(value: str) -> int:
        if fail:
            raise RuntimeError("injected")
        return len(value)

    policy = create_hash_policy(value_hash, lambda a, b: a == b)
    mapping = PersistentBiMap[int, str].empty(value_policy=policy).add(1, "one")
    inverse = mapping.inverse
    fail = True
    with pytest.raises(RuntimeError, match="injected"):
        mapping.add(2, "two")
    fail = False
    assert len(mapping) == 1
    assert mapping.inverse is inverse
    assert inverse.inverse is mapping
    assert mapping.validate_structure()


Command = tuple[str, int, int]


@given(
    st.lists(
        st.tuples(
            st.sampled_from(["add", "set", "remove-key", "remove-value"]),
            st.integers(min_value=0, max_value=15),
            st.integers(min_value=0, max_value=15),
        ),
        max_size=250,
    )
)
@settings(max_examples=80, deadline=None)
def test_model_histories(commands: list[Command]) -> None:
    actual = PersistentBiMap[int, int].empty()
    forward: dict[int, int] = {}
    inverse: dict[int, int] = {}
    retained: list[tuple[PersistentBiMap[int, int], dict[int, int]]] = []

    for kind, key, value in commands:
        if len(retained) < 8 and (key + value) % 11 == 0:
            retained.append((actual, dict(forward)))
        if kind == "add":
            can_add = key not in forward and value not in inverse
            result = actual.try_add(key, value)
            assert result.added == can_add
            actual = result.map
            if can_add:
                forward[key] = value
                inverse[value] = key
        elif kind == "set":
            owner = inverse.get(value)
            if owner is not None and owner != key:
                with pytest.raises(BiMapConflictError):
                    actual.set(key, value)
            else:
                previous = forward.get(key)
                if previous is not None:
                    inverse.pop(previous)
                forward[key] = value
                inverse[value] = key
                actual = actual.set(key, value)
        elif kind == "remove-key":
            previous = forward.pop(key, None)
            if previous is not None:
                inverse.pop(previous)
            actual = actual.remove_key(key)
        else:
            previous_key = inverse.pop(value, None)
            if previous_key is not None:
                forward.pop(previous_key)
            actual = actual.remove_value(value)
        _assert_model(actual, forward)

    for snapshot, model in retained:
        _assert_model(snapshot, model)


def test_concurrent_readers_publish_one_inverse() -> None:
    mapping = PersistentBiMap.from_items((index, -index) for index in range(1_000))

    def read_snapshot(_: int) -> int:
        inverse = mapping.inverse
        for index in range(1_000):
            assert mapping[index] == -index
            assert inverse[-index] == index
            assert inverse.inverse is mapping
        return id(inverse)

    with ThreadPoolExecutor(max_workers=8) as executor:
        identities = list(executor.map(read_snapshot, range(8)))
    assert len(set(identities)) == 1


def _assert_model(mapping: PersistentBiMap[int, int], expected: dict[int, int]) -> None:
    assert len(mapping) == len(expected)
    assert mapping.validate_structure()
    assert sorted((entry.key, entry.value) for entry in mapping) == sorted(expected.items())
    for key, value in expected.items():
        assert mapping[key] == value
        assert mapping.inverse[value] == key
