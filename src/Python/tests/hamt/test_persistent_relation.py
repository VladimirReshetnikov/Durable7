"""Tests for the persistent many-to-many relation.

Covers adjacency in both directions, one global representative per equivalence class, the cached
constant-time involutive inverse, symmetric pair removal that preserves the source, removal of
complete left and right groups, and identity preservation for duplicate and missing edits.
"""

from dataclasses import dataclass

from durable7.hamt import (
    HashPolicy,
    PersistentRelation,
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


def test_represents_many_to_many_adjacency_in_both_directions() -> None:
    relation = PersistentRelation.from_items((("a", 1), ("a", 2), ("b", 2)))
    assert relation.pair_count == 3
    assert sorted(relation.get_rights("a")) == [1, 2]
    assert sorted(relation.get_lefts(2)) == ["a", "b"]
    assert relation.validate_structure()


def test_retains_one_global_representative_per_class() -> None:
    left_one = Box(1, "left-one")
    left_two = Box(2, "left-two")
    right = Box(3, "right-first")
    relation = (
        PersistentRelation.empty(BOXES, BOXES)
        .add(left_one, right)
        .add(left_two, Box(3, "right-later"))
    )
    assert relation.get_rights(left_two).get(Box(3, "probe")) is right
    assert relation.get_lefts(right).get(Box(2, "probe")) is left_two
    assert relation.validate_structure()


def test_caches_an_involutive_constant_time_inverse() -> None:
    relation = PersistentRelation.from_items((("a", 1), ("b", 1)))
    assert relation.inverse.inverse is relation
    assert relation.inverse.contains(1, "a")
    assert relation.inverse is relation.inverse


def test_removes_pairs_symmetrically_and_preserves_source() -> None:
    source = PersistentRelation.from_items((("a", 1), ("a", 2), ("b", 2)))
    branch = source.remove("a", 2)
    assert not branch.contains("a", 2)
    assert branch.get_lefts(2).contains("b")
    assert source.contains("a", 2)
    assert branch.validate_structure()


def test_removes_complete_left_and_right_groups() -> None:
    source = PersistentRelation.from_items((("a", 1), ("a", 2), ("b", 2), ("c", 3)))
    no_a = source.remove_left("a")
    no_two = source.remove_right(2)
    assert not no_a.contains_left("a")
    assert no_a.get_lefts(2).contains("b")
    assert not no_two.contains_right(2)
    assert no_two.contains("a", 1)
    assert no_a.validate_structure() and no_two.validate_structure()


def test_duplicate_and_missing_edits_preserve_identity() -> None:
    source = PersistentRelation[str, int].empty().add("a", 1)
    assert source.add("a", 1) is source
    assert source.remove("a", 2) is source
    assert source.remove_left("missing") is source
    assert source.remove_right(9) is source
    assert source.clear().is_empty
