"""Tests for the persistent insertion-ordered map.

Covers the identity-versus-position split - first key and position win while the last distinct
value is stored - strict insertion, value-only replacement, explicit movement with positional
no-ops, keyed and positional removal preserving the source, ranges, reversal, stable sorting,
and dual-index validation under sparse order-stamp relabeling pressure.
"""

from dataclasses import dataclass

import pytest

from durable7 import (
    DuplicateKeyError,
    HashPolicy,
    PersistentOrderedMap,
    create_hash_policy,
)


@dataclass(frozen=True)
class Key:
    """A key wrapper with controlled equality, so the test can check which representative is
    retained.
    """

    identifier: int
    name: str


KEYS: HashPolicy[Key] = create_hash_policy(
    lambda key: key.identifier,
    lambda left, right: left.identifier == right.identifier,
)


def test_first_key_and_position_and_last_distinct_value_win() -> None:
    first = Key(1, "first")
    ordered = PersistentOrderedMap.from_items(
        ((first, "one"), (Key(2, "second"), "two"), (Key(1, "later"), "updated")),
        KEYS,
    )
    assert ordered.size == 2
    assert ordered.first.key is first
    assert ordered.get(Key(1, "probe")) == "updated"
    assert ordered.index_of_key(first) == 0


def test_strict_insertion_and_value_only_replacement() -> None:
    source = PersistentOrderedMap[str, int].empty().add("a", 1).add("c", 3)
    inserted = source.insert(1, "b", 2)
    replaced = inserted.set("b", 20)
    assert list(inserted.keys()) == ["a", "b", "c"]
    assert list(replaced.values()) == [1, 20, 3]
    assert replaced.index_of_key("b") == 1
    assert replaced.shares_membership_root_with(inserted)
    with pytest.raises(DuplicateKeyError):
        source.add("a", 9)


def test_explicit_movement_and_positional_no_ops() -> None:
    source = PersistentOrderedMap.from_items((("a", 1), ("b", 2), ("c", 3)))
    assert source.move_to(1, "b") is source
    assert list(source.move_to_first("c").keys()) == ["c", "a", "b"]
    assert list(source.move_to_last("a").keys()) == ["b", "c", "a"]
    with pytest.raises(KeyError):
        source.move_to_first("missing")


def test_keyed_and_positional_removal_preserve_source() -> None:
    source = PersistentOrderedMap.from_items((("a", 1), ("b", 2), ("c", 3)))
    removed = source.try_remove("b")
    assert removed.removed and removed.entry is not None
    assert removed.entry.key == "b" and removed.entry.value == 2
    assert list(removed.map.keys()) == ["a", "c"]
    assert source.contains_key("b")
    assert source.remove("missing") is source
    assert list(source.remove_at(0).keys()) == ["b", "c"]


def test_ranges_reverse_and_stable_sort() -> None:
    source = PersistentOrderedMap.from_items((("a", 2), ("b", 1), ("c", 2), ("d", 1)))
    assert list(source.get_range(1, 2).keys()) == ["b", "c"]
    assert list(source.take(2).keys()) == ["a", "b"]
    assert list(source.drop(2).keys()) == ["c", "d"]
    assert list(source.reverse().keys()) == ["d", "c", "b", "a"]
    assert list(source.sort(lambda left, right: left.value - right.value).keys()) == [
        "b",
        "d",
        "a",
        "c",
    ]


def test_policies_sparse_relabel_pressure_and_dual_index_validation() -> None:
    def values_equal(left: str, right: str) -> bool:
        return left.casefold() == right.casefold()

    ordered = (
        PersistentOrderedMap[Key, str]
        .empty(KEYS, values_equal)
        .add(Key(0, "zero"), "Value")
        .add(Key(1, "one"), "one")
    )
    assert ordered.set(Key(0, "probe"), "value") is ordered
    for identifier in range(2, 32):
        ordered = ordered.insert(1, Key(identifier, str(identifier)), str(identifier))
    assert ordered.key_policy is KEYS
    assert ordered.value_equals is values_equal
    assert ordered.validate_structure()
    assert ordered.clear().validate_structure()
