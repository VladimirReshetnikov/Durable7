"""Tests for the persistent insertion-ordered multimap.

Covers grouped ordering at both levels, survival of first representatives in both domains,
receiver identity for duplicates and removal misses, group contraction when a key loses its
final value together with re-adding appending a fresh group, whole-group removal, policy-bound
empty lookup, and nested invariants across retained branches.
"""

from __future__ import annotations

from dataclasses import dataclass

from durable7 import (
    HashPolicy,
    PersistentOrderedMultimap,
    create_hash_policy,
)


@dataclass(frozen=True)
class Box:
    """A value wrapper with controlled equality, so the test can check which representative is
    retained.
    """

    identifier: int
    name: str


BOXES: HashPolicy[Box] = create_hash_policy(
    lambda box: box.identifier,
    lambda left, right: left.identifier == right.identifier,
)


def test_pairs_use_grouped_key_and_value_insertion_order() -> None:
    mapping = PersistentOrderedMultimap.from_items([("a", 1), ("b", 2), ("a", 3), ("b", 4)])
    assert [(entry.key, entry.value) for entry in mapping] == [
        ("a", 1),
        ("a", 3),
        ("b", 2),
        ("b", 4),
    ]
    assert mapping.key_count == 2
    assert mapping.pair_count == 4


def test_first_representatives_survive_in_both_domains() -> None:
    key = Box(1, "key-first")
    value = Box(2, "value-first")
    mapping = (
        PersistentOrderedMultimap[Box, Box]
        .empty(BOXES, BOXES)
        .add(key, value)
        .add(Box(1, "key-later"), Box(3, "other"))
        .add(Box(1, "key-later"), Box(2, "value-later"))
    )
    assert mapping.try_get_key(Box(1, "probe")).key is key
    assert mapping.try_get_value(key, Box(2, "probe")).value is value
    assert mapping.key_policy is BOXES
    assert mapping.value_policy is BOXES


def test_duplicates_and_removal_misses_return_receiver() -> None:
    source = PersistentOrderedMultimap[str, int].empty().add("a", 1)
    assert source.add("a", 1) is source
    assert not source.try_add("a", 1).added
    assert source.remove("a", 2) is source
    assert source.remove_key("missing") is source
    assert source.shares_groups_roots_with(source)


def test_final_removal_contracts_and_readding_appends_group() -> None:
    source = PersistentOrderedMultimap.from_items([("a", 1), ("b", 2)])
    removed = source.remove("a", 1)
    readded = removed.add("a", 3)
    assert list(removed.keys()) == ["b"]
    assert list(readded.keys()) == ["b", "a"]
    assert source.contains("a", 1)


def test_whole_group_removal_and_policy_bound_empty_lookup() -> None:
    mapping = (
        PersistentOrderedMultimap[Box, Box]
        .empty(BOXES, BOXES)
        .add(Box(1, "a"), Box(2, "x"))
        .add(Box(1, "a2"), Box(3, "y"))
    )
    assert mapping.remove_key(Box(1, "probe")).is_empty
    assert mapping.get_values(Box(9, "missing")).policy is BOXES


def test_retained_branches_and_nested_invariants() -> None:
    source = PersistentOrderedMultimap.from_items([("a", 1), ("a", 2), ("b", 3)])
    branch = source.remove("a", 1)
    assert source.contains("a", 1)
    assert not branch.contains("a", 1)
    assert source.validate_structure()
    assert branch.validate_structure()
