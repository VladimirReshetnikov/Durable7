"""Tests for the strict, invertible, composable map patch.

Covers diffing and application across additions, removals, and changes; a present ``None``
staying distinct from absence; inversion restoring the source; composition matching sequential
application and dropping round trips; conflicts leaving both inputs unchanged; and policy
mismatch handling together with no-op elision.
"""

from __future__ import annotations

import pytest

from durable7 import (
    HashPolicy,
    MapPatchCompositionError,
    MapPatchConflictError,
    MapPatchEntry,
    MapPatchValue,
    PersistentHashMap,
    PersistentMapPatch,
    create_hash_policy,
)


def test_between_and_apply_cover_add_remove_and_change() -> None:
    policy: HashPolicy[str] = create_hash_policy(
        lambda key: len(key), lambda left, right: left == right
    )
    before = PersistentHashMap.from_items([("a", 1), ("bb", 2)], policy)
    after = PersistentHashMap.from_items([("bb", 20), ("ccc", 3)], policy)
    patch = PersistentMapPatch.between(before, after)
    assert len(patch) == 3
    assert patch.apply(before).map_equals(after)
    assert patch.validate_structure()


def test_present_none_is_distinct_from_absence() -> None:
    source = PersistentHashMap[str, int | None].empty().put("present", None)
    target = source.remove("present").put("added", None)
    patch = PersistentMapPatch.between(source, target)
    entries = {entry.key: entry for entry in patch}
    assert entries["present"].before.is_present
    assert entries["present"].before.value is None
    assert not entries["present"].after.is_present
    assert not entries["added"].before.is_present
    assert entries["added"].after.is_present
    assert patch.apply(source).contains_key("added")


def test_inversion_restores_source() -> None:
    before = PersistentHashMap.from_items([("a", 1), ("b", 2)])
    after = before.remove("a").put("b", 3).put("c", 4)
    patch = PersistentMapPatch.between(before, after)
    assert patch.invert().apply(patch.apply(before)).map_equals(before)


def test_composition_matches_sequential_application_and_drops_round_trip() -> None:
    first = PersistentMapPatch.from_entries(
        [
            MapPatchEntry("a", MapPatchValue.absent(), MapPatchValue.present(1)),
            MapPatchEntry("b", MapPatchValue.present(2), MapPatchValue.present(3)),
        ]
    )
    second = PersistentMapPatch.from_entries(
        [
            MapPatchEntry("a", MapPatchValue.present(1), MapPatchValue.absent()),
            MapPatchEntry("b", MapPatchValue.present(3), MapPatchValue.present(4)),
        ],
        first.key_policy,
        first.value_equals,
    )
    source = PersistentHashMap.from_items([("b", 2)], first.key_policy)
    composed = first.compose(second)
    assert len(composed) == 1
    assert composed.apply(source).map_equals(second.apply(first.apply(source)))


def test_conflicts_leave_inputs_unchanged() -> None:
    patch = PersistentMapPatch.from_entries(
        [MapPatchEntry("a", MapPatchValue.present(1), MapPatchValue.present(2))]
    )
    source = PersistentHashMap.from_items([("a", 9)], patch.key_policy)
    with pytest.raises(MapPatchConflictError):
        patch.apply(source)
    assert source["a"] == 9
    incompatible = PersistentMapPatch.from_entries(
        [MapPatchEntry("a", MapPatchValue.present(7), MapPatchValue.present(8))],
        patch.key_policy,
        patch.value_equals,
    )
    with pytest.raises(MapPatchCompositionError):
        patch.compose(incompatible)


def test_policy_mismatch_and_no_op_elision() -> None:
    policy: HashPolicy[str] = create_hash_policy(lambda _key: 0, lambda left, right: left == right)
    patch = PersistentMapPatch.from_entries(
        [MapPatchEntry("a", MapPatchValue.present(1), MapPatchValue.present(1))], policy
    )
    assert patch.is_empty
    with pytest.raises(TypeError):
        patch.apply(PersistentHashMap.empty())
    assert patch.clear() is patch
