from __future__ import annotations

from dataclasses import dataclass

import pytest

from vladimir_reshetnikov.data_structures import (
    DuplicateKeyError,
    HashPolicy,
    PersistentIndexedMap,
    create_hash_policy,
)


@dataclass(frozen=True)
class Key:
    identifier: int
    name: str


KEYS: HashPolicy[Key] = create_hash_policy(
    lambda key: key.identifier,
    lambda left, right: left.identifier == right.identifier,
)


def test_nonunique_secondary_groups_are_populated() -> None:
    mapping = PersistentIndexedMap.from_items(
        [("a", 1), ("b", 3), ("c", 2)], lambda _key, value: value % 2
    )
    assert mapping.get_keys(1).set_equals(["a", "b"])
    assert mapping.get_keys(0).set_equals(["c"])
    assert mapping.validate_structure()


def test_duplicate_add_and_equal_update_skip_selector() -> None:
    calls = 0

    def selector(_key: str, value: int) -> int:
        nonlocal calls
        calls += 1
        return value % 2

    source = PersistentIndexedMap.empty(selector).add("a", 1)
    with pytest.raises(DuplicateKeyError):
        source.add("a", 9)
    assert not source.try_add("a", 9).added
    assert source.set("a", 1) is source
    assert calls == 1


def test_changed_value_moves_between_groups() -> None:
    source = PersistentIndexedMap[str, int, int].empty(lambda _key, value: value % 2).add("a", 1)
    changed = source.set("a", 2)
    assert changed.get_keys(1).is_empty
    assert changed.get_keys(0).contains("a")
    assert source.get_keys(1).contains("a")


def test_primary_and_secondary_representatives_are_retained() -> None:
    primary = Key(1, "primary")
    index = Key(2, "index")
    mapping = (
        PersistentIndexedMap[Key, str, Key]
        .empty(lambda _key, _value: index, KEYS, index_policy=KEYS)
        .add(primary, "value")
    )
    assert mapping.get_stored_key(Key(1, "probe")) is primary
    assert mapping.try_get_index_key(primary).index_key is index
    assert mapping.get_keys(Key(2, "probe")).get(Key(1, "probe")) is primary


def test_removal_skips_selector_and_contracts_group() -> None:
    calls = 0

    def selector(_key: str, value: int) -> int:
        nonlocal calls
        calls += 1
        return value

    source = PersistentIndexedMap.empty(selector).add("a", 1)
    removed = source.remove("a")
    assert calls == 1
    assert removed.is_empty
    assert removed.index_key_count == 0
    assert source.remove("missing") is source


def test_selector_failure_leaves_source_reusable() -> None:
    def selector(_key: str, value: int) -> int:
        if value == 9:
            raise RuntimeError("selector failure")
        return value

    source = PersistentIndexedMap.empty(selector).add("a", 1)
    with pytest.raises(RuntimeError, match="selector failure"):
        source.set("a", 9)
    assert source["a"] == 1
    assert source.validate_structure()
