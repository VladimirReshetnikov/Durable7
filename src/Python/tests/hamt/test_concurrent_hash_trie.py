"""Consumer semantics for the RLock-coordinated concurrent hash-trie facade."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.hamt import (
    ConcurrentHashTrie,
    ConcurrentHashTrieSnapshot,
    HashPolicy,
    create_hash_policy,
)


@dataclass(frozen=True, slots=True)
class _Key:
    text: str
    identity: int


@dataclass(frozen=True, slots=True, eq=False)
class _Value:
    semantic: int
    identity: int

    def __eq__(self, other: object) -> bool:
        return isinstance(other, _Value) and self.semantic == other.semantic


class _ReentrantIntPolicy(HashPolicy[int]):
    def __init__(self) -> None:
        self._hash_hook: Callable[[], None] | None = None
        self._equivalent_hook: Callable[[], None] | None = None

    def arm_hash(self, hook: Callable[[], None]) -> None:
        self._hash_hook = hook

    def arm_equivalent(self, hook: Callable[[], None]) -> None:
        self._equivalent_hook = hook

    def hash(self, _key: int) -> int:
        hook = self._hash_hook
        self._hash_hook = None
        if hook is not None:
            hook()
        return 0

    def equivalent(self, left: int, right: int) -> bool:
        hook = self._equivalent_hook
        self._equivalent_hook = None
        if hook is not None:
            hook()
        return left == right


def _casefold_collision_policy() -> HashPolicy[_Key]:
    return create_hash_policy(
        lambda _key: 0,
        lambda left, right: left.text.casefold() == right.text.casefold(),
    )


def test_compute_uses_the_lookup_key_and_retains_stored_representatives() -> None:
    policy = _casefold_collision_policy()
    stored_key = _Key("alpha", 1)
    lookup_key = _Key("ALPHA", 2)
    stored_value = _Value(10, 1)
    trie = ConcurrentHashTrie[_Key, _Value](policy)
    trie.set(stored_key, stored_value)

    observed: tuple[_Key, _Value] | None = None

    def unexpected_add(_key: _Key) -> _Value:
        raise AssertionError("the add factory must not run for a present key")

    def equal_update(key: _Key, value: _Value) -> _Value:
        nonlocal observed
        observed = (key, value)
        return _Value(value.semantic, 2)

    selected = trie.compute(lookup_key, unexpected_add, equal_update)
    entry = trie.get_entry(lookup_key)
    assert observed == (lookup_key, stored_value)
    assert observed is not None
    assert observed[0] is lookup_key
    assert selected is stored_value
    assert entry is not None
    assert entry.key is stored_key
    assert entry.value is stored_value
    assert trie.generation == 1

    changed_value = _Value(12, 3)
    changed = trie.compute(lookup_key, unexpected_add, lambda _key, _value: changed_value)
    changed_entry = trie.get_entry(lookup_key)
    assert changed is changed_value
    assert changed_entry is not None
    assert changed_entry.key is stored_key
    assert changed_entry.value is changed_value
    assert trie.generation == 2


def test_get_or_put_preserves_none_presence_and_rechecks_after_reentrancy() -> None:
    policy = _casefold_collision_policy()
    stored_key = _Key("alpha", 1)
    lookup_key = _Key("ALPHA", 2)
    trie = ConcurrentHashTrie[_Key, int | None](policy)
    trie.set(stored_key, None)
    snapshot = trie.snapshot()
    calls = 0

    def unexpected_factory(_key: _Key) -> int:
        nonlocal calls
        calls += 1
        return 10

    assert trie.get_or_put(lookup_key, unexpected_factory) is None
    assert calls == 0
    assert trie.generation == 1

    nested_key = _Key("beta", 3)
    nested_lookup = _Key("BETA", 4)

    def reentrant_factory(key: _Key) -> int:
        nonlocal calls
        calls += 1
        assert key is nested_lookup
        trie.set(nested_key, None)
        return 20

    assert trie.get_or_put(nested_lookup, reentrant_factory) is None
    nested_entry = trie.get_entry(nested_lookup)
    assert nested_entry is not None
    assert nested_entry.key is nested_key
    assert nested_entry.value is None
    assert calls == 1
    assert trie.generation == 2
    assert not snapshot.contains_key(nested_lookup)


def test_compute_retries_after_a_reentrant_factory_changes_the_root() -> None:
    policy = _casefold_collision_policy()
    stored_key = _Key("alpha", 1)
    lookup_key = _Key("ALPHA", 2)
    side_key = _Key("side", 3)
    trie = ConcurrentHashTrie[_Key, int](policy)
    trie.set(stored_key, 10)
    snapshot = trie.snapshot()
    observations: list[tuple[_Key, int]] = []

    def unexpected_add(_key: _Key) -> int:
        raise AssertionError("the add factory must not run for a present key")

    def reentrant_update(key: _Key, value: int) -> int:
        observations.append((key, value))
        if len(observations) == 1:
            trie.set(_Key("Alpha", 99), 100)
            trie.set(side_key, 7)
        return value + 1

    assert trie.compute(lookup_key, unexpected_add, reentrant_update) == 101
    assert observations == [(lookup_key, 10), (lookup_key, 100)]
    assert all(key is lookup_key for key, _value in observations)
    retained = trie.get_entry(lookup_key)
    assert retained is not None
    assert retained.key is stored_key
    assert retained.value == 101
    assert trie.get(side_key) == 7
    assert trie.generation == 4
    assert snapshot.get(lookup_key) == 10
    assert not snapshot.contains_key(side_key)


def test_compute_missing_retries_as_an_update_after_a_reentrant_insert() -> None:
    policy = _casefold_collision_policy()
    lookup_key = _Key("ALPHA", 1)
    nested_key = _Key("alpha", 2)
    trie = ConcurrentHashTrie[_Key, int](policy)
    add_observations: list[_Key] = []
    update_observations: list[tuple[_Key, int]] = []

    def reentrant_add(key: _Key) -> int:
        add_observations.append(key)
        trie.set(nested_key, 40)
        return 10

    def update(key: _Key, value: int) -> int:
        update_observations.append((key, value))
        return value + 2

    assert trie.compute(lookup_key, reentrant_add, update) == 42
    assert add_observations == [lookup_key]
    assert add_observations[0] is lookup_key
    assert update_observations == [(lookup_key, 40)]
    assert update_observations[0][0] is lookup_key
    entry = trie.get_entry(lookup_key)
    assert entry is not None
    assert entry.key is nested_key
    assert entry.value == 42
    assert trie.generation == 2


def test_set_retries_after_a_reentrant_hash_policy_publication() -> None:
    policy = _ReentrantIntPolicy()
    trie = ConcurrentHashTrie[int, int](policy)
    trie.set(1, 10)
    policy.arm_hash(lambda: trie.set(2, 20))

    trie.set(1, 11)

    assert trie.get(1) == 11
    assert trie.get(2) == 20
    assert trie.generation == 3


def test_try_add_rechecks_after_a_reentrant_equality_policy_insert() -> None:
    policy = _ReentrantIntPolicy()
    trie = ConcurrentHashTrie[int, int](policy)
    trie.set(1, 10)
    policy.arm_equivalent(lambda: trie.set(3, 30))

    assert not trie.try_add(3, 300)
    assert trie.get(1) == 10
    assert trie.get(3) == 30
    assert trie.generation == 2


def test_get_or_put_rechecks_policy_publications_without_repeating_its_factory() -> None:
    policy = _ReentrantIntPolicy()
    trie = ConcurrentHashTrie[int, int](policy)
    trie.set(1, 10)
    calls = 0

    def factory(key: int) -> int:
        nonlocal calls
        calls += 1
        policy.arm_hash(lambda: trie.set(2, 20))
        return key * 10

    assert trie.get_or_put(3, factory) == 30
    assert calls == 1
    assert {entry.key: entry.value for entry in trie} == {1: 10, 2: 20, 3: 30}
    assert trie.generation == 3

    policy.arm_equivalent(lambda: trie.set(4, 400))
    assert trie.get_or_put(4, factory) == 400
    assert calls == 1
    assert trie.get(4) == 400
    assert trie.generation == 4


def test_compute_retries_after_a_reentrant_hash_policy_publication() -> None:
    policy = _ReentrantIntPolicy()
    trie = ConcurrentHashTrie[int, int](policy)
    trie.set(1, 10)
    update_calls = 0

    def update(key: int, value: int) -> int:
        nonlocal update_calls
        update_calls += 1
        assert key == 1
        return value + 1

    policy.arm_hash(lambda: trie.set(2, 20))
    assert trie.compute(1, lambda _key: 0, update) == 11
    assert update_calls == 2
    assert trie.get(1) == 11
    assert trie.get(2) == 20
    assert trie.generation == 3


def test_remove_retries_and_returns_the_latest_reentrant_policy_value() -> None:
    policy = _ReentrantIntPolicy()
    trie = ConcurrentHashTrie[int, int](policy)
    trie.set(1, 10)
    trie.set(2, 20)
    policy.arm_hash(lambda: trie.set(3, 30))

    first = trie.remove(1)
    assert first is not None
    assert (first.key, first.value) == (1, 10)
    assert trie.get(2) == 20
    assert trie.get(3) == 30
    assert trie.generation == 4

    policy.arm_equivalent(lambda: trie.set(2, 200))
    second = trie.remove(2)
    assert second is not None
    assert (second.key, second.value) == (2, 200)
    assert not trie.contains_key(2)
    assert trie.get(3) == 30
    assert trie.generation == 6


def test_factory_failures_do_not_publish_an_outer_successor() -> None:
    trie = ConcurrentHashTrie[str, int | None]()
    trie.set("present", None)
    before = trie.snapshot()
    generation = trie.generation

    def fail_add(_key: str) -> int | None:
        raise RuntimeError("add failed")

    def fail_update(_key: str, _value: int | None) -> int | None:
        raise RuntimeError("update failed")

    with pytest.raises(RuntimeError, match="add failed"):
        trie.compute("missing", fail_add, fail_update)
    with pytest.raises(RuntimeError, match="update failed"):
        trie.compute("present", fail_add, fail_update)
    with pytest.raises(RuntimeError, match="add failed"):
        trie.get_or_put("missing", fail_add)

    after = trie.snapshot()
    assert after.to_persistent_hash_map() is before.to_persistent_hash_map()
    assert after.contains_key("present")
    present = after.get_entry("present")
    assert present is not None
    assert present.value is None
    assert not after.contains_key("missing")
    assert trie.generation == generation


def test_collision_iteration_and_snapshots_are_canonical_and_stable() -> None:
    policy: HashPolicy[int] = create_hash_policy(
        lambda _key: 0,
        lambda left, right: left == right,
    )
    trie = ConcurrentHashTrie[int, int | None](policy)
    for key, value in ((4, 40), (1, None), (7, 70), (2, 20)):
        trie.set(key, value)

    snapshot = trie.snapshot()
    expected = tuple(snapshot)
    assert [entry.key for entry in expected] == [4, 1, 7, 2]
    assert expected == tuple(snapshot)
    assert expected == tuple(snapshot.to_persistent_hash_map())
    assert expected == tuple(trie)

    def increment(_key: int, value: int | None) -> int:
        assert value is not None
        return value + 1

    trie.compute(7, lambda _key: 0, increment)
    trie.remove(1)
    trie.set(9, 90)
    assert tuple(snapshot) == expected
    assert tuple(trie) == tuple(trie.snapshot())


@settings(max_examples=75)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=0, max_value=5),
            st.integers(min_value=-4, max_value=4),
            st.one_of(st.none(), st.integers(min_value=-20, max_value=20)),
        ),
        max_size=100,
    )
)
def test_generated_histories_match_a_collision_dictionary_model(
    operations: list[tuple[int, int, int | None]],
) -> None:
    policy: HashPolicy[int] = create_hash_policy(
        lambda _key: 0,
        lambda left, right: left == right,
    )
    trie = ConcurrentHashTrie[int, int | None](policy)
    model: dict[int, int | None] = {}
    generation = 0
    retained: list[tuple[ConcurrentHashTrieSnapshot[int, int | None], dict[int, int | None]]] = []

    for index, (operation, key, value) in enumerate(operations):
        if index % 8 == 0:
            retained.append((trie.snapshot(), model.copy()))

        if operation == 0:
            changed = key not in model or model[key] != value
            trie.set(key, value)
            model[key] = value
            generation += int(changed)
        elif operation == 1:
            added = key not in model
            assert trie.try_add(key, value) is added
            if added:
                model[key] = value
                generation += 1
        elif operation == 2:
            changed = key not in model or model[key] != value

            def select_add(_key: int, selected: int | None = value) -> int | None:
                return selected

            def select_update(
                _key: int,
                _current: int | None,
                selected: int | None = value,
            ) -> int | None:
                return selected

            selected = trie.compute(key, select_add, select_update)
            model[key] = value
            assert selected == value
            generation += int(changed)
        elif operation == 3:
            was_present = key in model
            expected = model.pop(key, None)
            removed = trie.remove(key)
            assert (removed is not None) is was_present
            if removed is not None:
                assert removed.key == key
                assert removed.value == expected
                generation += 1
        elif operation == 4:
            was_present = key in model
            expected = model[key] if was_present else value

            def factory(_key: int, selected: int | None = value) -> int | None:
                return selected

            assert trie.get_or_put(key, factory) == expected
            if not was_present:
                model[key] = value
                generation += 1
        else:
            changed = bool(model)
            trie.clear()
            model.clear()
            generation += int(changed)

        snapshot = trie.snapshot()
        entries = tuple(snapshot)
        assert {entry.key: entry.value for entry in entries} == model
        assert entries == tuple(snapshot)
        assert entries == tuple(snapshot.to_persistent_hash_map())
        assert entries == tuple(trie)
        assert len(snapshot) == len(model)
        assert trie.generation == generation

    for snapshot, expected_model in retained:
        assert {entry.key: entry.value for entry in snapshot} == expected_model
