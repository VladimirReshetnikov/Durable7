"""Tests for the checkpoint-differential persistent sorted map.

The load-bearing properties are the ones a plain persistent map cannot supply: a write of an
equivalent value is a semantic no-op that shares every root, repeated writes to one key coalesce
into a single record, returning a key to its checkpoint state deletes that record, and a wholly
cancelled epoch snaps its current root back to the exact checkpoint root. Alongside those, the
suite pins key-representative retention, O(1) checkpoint and rollback root swaps, output-optimal
and policy-free change enumeration, and a range restriction that seeks its boundaries instead of
filtering the whole change set.

Sharing assertions here are always made across two genuinely distinct values, or paired with a
negative control, so that none of them degenerates into an object being compared with itself.
"""

from __future__ import annotations

import random
from collections.abc import Callable

import pytest

from durable7.finger_tree.equality import natural_value_equality
from durable7.finger_tree.ordering import Comparator, default_comparator
from durable7.finger_tree.persistent_delta_map import (
    DeltaMapValue,
    PersistentDeltaMap,
    PersistentMapChange,
    PersistentMapChangeKind,
    _DeltaRecord,
)
from durable7.finger_tree.sorted import SortedMap

IntMap = PersistentDeltaMap[int, str]

_ABSENT = object()


class _Counter:
    """Counts policy callbacks so a test can assert an operation made none at all."""

    def __init__(self) -> None:
        self.count = 0

    def reset(self) -> None:
        self.count = 0


def counting_comparator(counter: _Counter) -> Comparator[int]:
    """Wrap the natural order so every comparison is observable."""

    def compare(left: int, right: int) -> int:
        counter.count += 1
        return default_comparator(left, right)

    return compare


def counting_value_equality(counter: _Counter) -> Callable[[str, str], bool]:
    """Wrap natural value equality so every value comparison is observable."""

    def equals(left: str, right: str) -> bool:
        counter.count += 1
        return left == right

    return equals


def descending(left: int, right: int) -> int:
    """Order integers from greatest to least."""

    return default_comparator(right, left)


def case_insensitive(left: str, right: str) -> int:
    """Order strings ignoring case, so ``Alpha`` and ``ALPHA`` are one key class."""

    return default_comparator(left.lower(), right.lower())


def type_strict_equality(left: object, right: object) -> bool:
    """A relation strictly finer than ``==``: ``1``, ``1.0``, and ``True`` are all distinct."""

    return type(left) is type(right) and bool(left == right)


def contents(source: PersistentDeltaMap[int, str]) -> list[tuple[int, str]]:
    """The current entries as plain pairs, in ascending key order."""

    return [(entry.key, entry.value) for entry in source]


def change_keys(source: PersistentDeltaMap[int, str]) -> list[int]:
    """The net-changed key representatives, in ascending order."""

    return [change.key for change in source.get_changes()]


def range_keys(source: PersistentDeltaMap[int, str], low: int, high: int) -> list[int]:
    """The net-changed key representatives inside an inclusive key range."""

    return [change.key for change in source.changes_in_range(low, high)]


def endpoints(change: PersistentMapChange[int, str]) -> tuple[object, object]:
    """Both endpoints with absence rendered as a sentinel distinct from every stored value."""

    return (
        _ABSENT if change.before is None else change.before.value,
        _ABSENT if change.after is None else change.after.value,
    )


def test_point_edits_classify_added_updated_and_removed_keys() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(1, "one"), (2, "two")])
    assert not source.has_changes
    assert source.is_clean

    changed = source.set_item(3, "three").set_item(1, "ONE").remove(2)
    assert contents(changed) == [(1, "ONE"), (3, "three")]
    assert change_keys(changed) == [1, 2, 3]

    updated, removed, added = list(changed.get_changes())
    assert updated.kind is PersistentMapChangeKind.UPDATED
    assert endpoints(updated) == ("one", "ONE")
    assert removed.kind is PersistentMapChangeKind.REMOVED
    assert endpoints(removed) == ("two", _ABSENT)
    assert added.kind is PersistentMapChangeKind.ADDED
    assert endpoints(added) == (_ABSENT, "three")

    for change in (updated, removed, added):
        assert changed.get_change(change.key) == change
    assert changed.get_change(4) is None

    statistics = changed.validate_structure()
    assert statistics.count == 2
    assert statistics.checkpoint_count == 2
    assert statistics.change_count == 3
    assert (statistics.added_count, statistics.removed_count, statistics.updated_count) == (1, 1, 1)
    assert statistics.is_clean is False

    # The receiver is untouched, as for every other producing method.
    assert contents(source) == [(1, "one"), (2, "two")]
    assert source.validate_structure().is_clean is True


def test_a_write_of_an_equivalent_value_is_a_semantic_no_op() -> None:
    # Distinct list objects that compare equal, so identity assertions below mean something.
    first = ["payload"]
    duplicate = ["payload"]
    assert first is not duplicate and first == duplicate

    source: PersistentDeltaMap[str, list[str]] = PersistentDeltaMap.from_items(
        [(f"k{index}", [str(index)]) for index in range(64)] + [("target", first)]
    )
    again = source.set_item("target", duplicate)

    # The no-op returns the receiver, so this is an identity assertion about two named values and
    # not a structure comparison of an object with itself.
    assert again is source
    assert not again.has_changes
    assert again.change_count == 0
    stored = again.get_entry("target")
    assert stored is not None and stored.value is first

    # An effective write does produce a distinct version, and that version shares nodes with the
    # one it came from.
    effective = source.set_item("target", ["other"])
    assert effective is not source
    assert effective.current_snapshot() is not source.current_snapshot()
    assert effective.current_snapshot().shares_storage_with(source.current_snapshot())
    assert effective.checkpoint_snapshot() is source.current_snapshot()

    # Negative control: an independently built map of the same entries shares nothing.
    independent: PersistentDeltaMap[str, list[str]] = PersistentDeltaMap.from_items(
        [(f"k{index}", [str(index)]) for index in range(64)] + [("target", first)]
    )
    assert not independent.current_snapshot().shares_storage_with(source.current_snapshot())
    assert not independent.shares_storage_with(source)
    # The effective successor keeps the checkpoint root but grows a change index from empty, and an
    # empty index shares no node with a non-empty one, so the conjunction refuses it.
    assert not effective.shares_storage_with(source)


def test_first_effective_write_captures_before_and_repeated_writes_coalesce() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(1, "one")])
    once = source.set_item(1, "a")
    twice = once.set_item(1, "b")
    thrice = twice.set_item(1, "c")

    assert [source.change_count, once.change_count, twice.change_count, thrice.change_count] == [
        0,
        1,
        1,
        1,
    ]
    for version, after in ((once, "a"), (twice, "b"), (thrice, "c")):
        change = version.get_change(1)
        assert change is not None
        assert endpoints(change) == ("one", after)
        assert change.kind is PersistentMapChangeKind.UPDATED
        assert version.validate_structure().updated_count == 1

    # An addition coalesces the same way, and stays an addition rather than becoming an update.
    added = source.set_item(9, "x").set_item(9, "y").set_item(9, "z")
    assert added.change_count == 1
    change = added.get_change(9)
    assert change is not None
    assert endpoints(change) == (_ABSENT, "z")
    assert change.kind is PersistentMapChangeKind.ADDED


def test_returning_a_key_to_its_checkpoint_state_cancels_its_record() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(index, f"v{index}") for index in range(32)])

    # Set then restore.
    restored = source.set_item(7, "changed").set_item(7, "v7")
    assert not restored.has_changes
    assert restored.get_change(7) is None
    assert contents(restored) == contents(source)

    # Add then remove.
    cancelled = source.set_item(99, "new").remove(99)
    assert not cancelled.has_changes
    assert cancelled.get_change(99) is None

    # Remove then re-add the same value.
    round_trip = source.remove(3).set_item(3, "v3")
    assert not round_trip.has_changes

    for version in (restored, cancelled, round_trip):
        statistics = version.validate_structure()
        assert statistics.change_count == 0
        assert statistics.is_clean is True

    # One live change keeps the epoch dirty even though the other key cancelled.
    partial = source.set_item(7, "changed").set_item(8, "kept").set_item(7, "v7")
    assert change_keys(partial) == [8]
    assert partial.validate_structure().updated_count == 1


def test_an_empty_change_index_snaps_the_current_root_back_to_the_checkpoint() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(index, f"v{index}") for index in range(32)])
    dirty = source.set_item(7, "changed")
    assert dirty.current_snapshot() is not dirty.checkpoint_snapshot()
    assert not dirty.is_clean

    restored = dirty.set_item(7, "v7")
    # Two independently obtained handles that turn out to be the same object: had the write merely
    # rebuilt an extensionally equal tree, this would fail.
    assert restored.current_snapshot() is source.checkpoint_snapshot()
    assert restored.current_snapshot() is restored.checkpoint_snapshot()
    assert restored.is_clean
    # A distinct version that nonetheless shares all three roots with the one it started from.
    assert restored is not source
    assert restored.shares_storage_with(source)

    # The same snap happens through remove, and through a whole batch.
    assert source.set_item(99, "new").remove(99).current_snapshot() is source.current_snapshot()
    batch = source.set_items([(1, "x"), (2, "y"), (1, "v1"), (2, "v2")])
    assert batch.current_snapshot() is source.current_snapshot()
    assert batch.is_clean


def test_key_representatives_survive_updates_and_delete_re_add_round_trips() -> None:
    alpha = "Alpha"
    probe = "ALPHA"
    source: PersistentDeltaMap[str, int] = PersistentDeltaMap.from_items(
        [(alpha, 1)], case_insensitive
    )

    updated = source.set_item(probe, 2)
    assert len(updated) == 1
    stored = updated.get_entry("alpha")
    # "Alpha" and "ALPHA" are different objects, so identity here really distinguishes them.
    assert stored is not None and stored.key is alpha
    assert stored.value == 2
    assert [change.key for change in updated.get_changes()] == [alpha]

    # A delete/re-add round trip keeps the checkpoint representative and cancels the record.
    round_trip = updated.remove(probe).set_item(probe, 1)
    assert not round_trip.has_changes
    assert round_trip.current_snapshot() is source.checkpoint_snapshot()

    # The same round trip beside an unrelated change cannot snap back, so the representative rule
    # has to hold on its own.
    restored = source.set_item("Omega", 9).remove(probe).set_item(probe, 1)
    assert [change.key for change in restored.get_changes()] == ["Omega"]
    kept = restored.get_entry(probe)
    assert kept is not None and kept.key is alpha
    restored.validate_structure()

    # A net-added class keeps its first representative while its record stays active.
    beta = "Beta"
    added = source.set_item(beta, 3).set_item("BETA", 4)
    entry = added.get_entry("beta")
    assert entry is not None and entry.key is beta and entry.value == 4
    assert [change.key for change in added.get_changes()] == [beta]

    # After complete cancellation a later addition begins a new representative episode.
    empty: PersistentDeltaMap[str, int] = PersistentDeltaMap.empty(case_insensitive)
    cancelled = empty.set_item(beta, 1).remove("BETA")
    assert not cancelled.has_changes
    episode = cancelled.set_item("BETA", 2)
    fresh = episode.entry_at(0)
    assert fresh.key == "BETA" and fresh.key is not beta
    episode.validate_structure()


def test_removing_an_absent_key_is_a_no_op() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(index, f"v{index}") for index in range(32)])
    assert source.remove(999) is source
    assert source.remove(-1) is source

    dirty = source.set_item(1, "changed")
    assert dirty.remove(999) is dirty
    assert change_keys(dirty) == [1]

    # Removing a key that is already recorded as removed is also a no-op.
    once = source.remove(5)
    assert once.remove(5) is once
    assert change_keys(once) == [5]


def test_checkpoint_and_rollback_are_root_swaps_without_policy_calls() -> None:
    keys, values = _Counter(), _Counter()
    source: IntMap = PersistentDeltaMap.from_items(
        [(index, f"v{index}") for index in range(32)],
        counting_comparator(keys),
        counting_value_equality(values),
    )
    dirty = source.set_item(1, "changed").remove(2).set_item(99, "new")

    keys.reset()
    values.reset()
    committed = dirty.checkpoint()
    reverted = dirty.rollback()
    clean_again = committed.checkpoint()
    same = committed.rollback()
    assert keys.count == 0
    assert values.count == 0

    assert committed.current_snapshot() is dirty.current_snapshot()
    assert committed.checkpoint_snapshot() is dirty.current_snapshot()
    assert committed.is_clean and not committed.has_changes
    assert reverted.current_snapshot() is dirty.checkpoint_snapshot()
    assert reverted.is_clean and contents(reverted) == contents(source)

    # An already-clean version returns the receiver, so both of these are identity assertions.
    assert clean_again is committed
    assert same is committed
    assert source.checkpoint() is source
    assert source.rollback() is source

    committed.validate_structure()
    reverted.validate_structure()

    # Rollback discards the epoch but not the retained branch it came from.
    assert change_keys(dirty) == [1, 2, 99]


def test_change_enumeration_is_output_optimal_and_free_of_policy_calls() -> None:
    def build(count: int) -> tuple[IntMap, _Counter, _Counter]:
        keys, values = _Counter(), _Counter()
        source: IntMap = PersistentDeltaMap.from_items(
            [(index, str(index)) for index in range(count)],
            counting_comparator(keys),
            counting_value_equality(values),
        )
        changed = source.set_item(-1, "low").set_item(count // 2, "middle").remove(count - 1)
        assert changed.change_count == 3
        return changed, keys, values

    small, small_keys, small_values = build(128)
    large, large_keys, large_values = build(2048)

    small_keys.reset()
    small_values.reset()
    small_changes = list(small.get_changes())

    large_keys.reset()
    large_values.reset()
    large_changes = list(large.get_changes())

    assert [change.key for change in small_changes] == [-1, 64, 127]
    assert [change.key for change in large_changes] == [-1, 1024, 2047]
    # The whole point of the maintained index: the walk is output-sized and policy-free, whatever
    # the baseline holds.
    assert (small_keys.count, small_values.count) == (0, 0)
    assert (large_keys.count, large_values.count) == (0, 0)

    # The walk is lazy, so abandoning it early really does skip the rest.
    large_keys.reset()
    large_values.reset()
    assert next(large.get_changes()).key == -1
    assert (large_keys.count, large_values.count) == (0, 0)

    # Immutability makes the enumeration repeatable even though each call is single-pass.
    assert [change.key for change in large.get_changes()] == [-1, 1024, 2047]
    assert list(large.get_changes()) == large_changes


def test_changes_in_range_seeks_boundaries_instead_of_filtering() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(10, "ten"), (30, "thirty"), (50, "fifty")])
    changed = source.set_item(20, "twenty").set_item(30, "THIRTY").remove(50).set_item(70, "seven")
    assert change_keys(changed) == [20, 30, 50, 70]

    # Present boundaries are inclusive; absent boundaries bound the walk without appearing.
    assert range_keys(changed, 20, 50) == [20, 30, 50]
    assert range_keys(changed, 21, 69) == [30, 50]
    assert range_keys(changed, 30, 30) == [30]
    assert range_keys(changed, 31, 31) == []

    # Full cover, no cover on either side, and an inverted range.
    assert range_keys(changed, -100, 100) == change_keys(changed)
    assert range_keys(changed, 100, 200) == []
    assert range_keys(changed, -200, -100) == []
    assert range_keys(changed, 50, 20) == []
    assert len(changed.get_key_range(50, 20)) == 0

    # Endpoints, kinds, and representatives survive the restriction unchanged.
    restricted = list(changed.changes_in_range(30, 50))
    assert [change.kind for change in restricted] == [
        PersistentMapChangeKind.UPDATED,
        PersistentMapChangeKind.REMOVED,
    ]
    assert [endpoints(change) for change in restricted] == [
        ("thirty", "THIRTY"),
        ("fifty", _ABSENT),
    ]
    for change in restricted:
        assert changed.get_change(change.key) == change

    # Every boundary pair agrees with filtering the unrestricted enumeration.
    for low in range(-1, 9):
        for high in range(-1, 9):
            expected = [key for key in change_keys(changed) if low * 10 <= key <= high * 10]
            assert range_keys(changed, low * 10, high * 10) == expected

    # An empty change set is empty over every range, including an inverted one.
    assert range_keys(source, -100, 100) == []
    empty: IntMap = PersistentDeltaMap.empty()
    assert range_keys(empty, 0, 10) == []
    assert range_keys(empty, 10, 0) == []
    changed.validate_structure()


def test_changes_in_range_is_a_bounded_seek_rather_than_a_scan() -> None:
    keys, values = _Counter(), _Counter()
    size = 512
    source: IntMap = PersistentDeltaMap.from_items(
        [(index, str(index)) for index in range(size)],
        counting_comparator(keys),
        counting_value_equality(values),
    )
    changed = source.set_items([(index, f"changed{index}") for index in range(size)])
    assert changed.change_count == size

    keys.reset()
    values.reset()
    selected = [change.key for change in changed.changes_in_range(400, 402)]
    assert selected == [400, 401, 402]
    # Zero value comparisons, and a key-comparison count that is logarithmic in the change set
    # rather than proportional to it: a filter over all k records could not stay this far below k.
    assert values.count == 0
    assert 0 < keys.count <= 64
    assert keys.count * 4 < changed.change_count

    # Nothing about the restriction depends on how many changes fall outside it.
    keys.reset()
    assert [change.key for change in changed.changes_in_range(0, 2)] == [0, 1, 2]
    assert keys.count <= 64


def test_an_inverted_range_is_decided_by_the_retained_order() -> None:
    source: IntMap = PersistentDeltaMap.from_items(
        [(1, "one"), (3, "three"), (5, "five")], descending
    )
    changed = source.set_item(6, "six").set_item(4, "four").set_item(3, "THREE").remove(1)
    assert change_keys(changed) == [6, 4, 3, 1]
    assert [entry.key for entry in changed] == [6, 5, 4, 3]

    # Under a descending order the low endpoint is the numerically greater key.
    assert range_keys(changed, 6, 3) == [6, 4, 3]
    assert range_keys(changed, 5, 2) == [4, 3]
    assert range_keys(changed, 4, 1) == [4, 3, 1]
    # "Inverted" follows the retained order, not the natural one.
    assert range_keys(changed, 3, 6) == []
    assert range_keys(changed, 1, 4) == []
    changed.validate_structure()


def test_endpoints_distinguish_a_stored_none_from_absence() -> None:
    source: PersistentDeltaMap[int, str | None] = PersistentDeltaMap.from_items([(1, None)])
    assert source.get(1) is None
    entry = source.get_entry(1)
    assert entry is not None and entry.value is None

    filled = source.set_item(1, "value")
    change = filled.get_change(1)
    assert change is not None
    # A present ``None`` before endpoint, which a bare ``str | None`` could not tell from absence.
    assert change.before is not None and change.before.value is None
    assert change.after is not None and change.after.value == "value"
    assert change.kind is PersistentMapChangeKind.UPDATED

    with_none = source.set_item(2, None)
    added = with_none.get_change(2)
    assert added is not None
    assert added.before is None
    assert added.after is not None and added.after.value is None
    assert added.kind is PersistentMapChangeKind.ADDED

    removed = source.remove(1).get_change(1)
    assert removed is not None
    assert removed.before is not None and removed.before.value is None
    assert removed.after is None
    assert removed.kind is PersistentMapChangeKind.REMOVED

    # Writing None over a stored None is still a no-op under the natural relation.
    assert source.set_item(1, None) is source

    # The wrapper itself compares by ordinary Python equality, which is not the map's relation.
    assert DeltaMapValue("x") == DeltaMapValue("x")
    assert DeltaMapValue("x") != DeltaMapValue("y")

    filled.validate_structure()
    with_none.validate_structure()


def test_a_strict_value_relation_forces_a_write_the_substrate_would_skip() -> None:
    # ``1 == 1.0`` is true, so the ordinary sorted map would treat this write as a no-op; the
    # retained relation says otherwise and must win.
    source: PersistentDeltaMap[str, object] = PersistentDeltaMap.from_items(
        [("a", 1)], default_comparator, type_strict_equality
    )
    promoted = source.set_item("a", 1.0)

    assert promoted is not source
    stored = promoted.get_entry("a")
    assert stored is not None and type(stored.value) is float
    change = promoted.get_change("a")
    assert change is not None
    assert change.before is not None and type(change.before.value) is int
    assert change.after is not None and type(change.after.value) is float
    promoted.validate_structure()

    # Writing the identical type and value back cancels the record and snaps the root.
    restored = promoted.set_item("a", 1)
    assert not restored.has_changes
    assert restored.current_snapshot() is source.checkpoint_snapshot()

    # Construction respects the relation too: the last value of a class wins even when the values
    # compare ``==``.
    built: PersistentDeltaMap[str, object] = PersistentDeltaMap.from_items(
        [("a", 1), ("a", True)], default_comparator, type_strict_equality
    )
    entry = built.entry_at(0)
    assert type(entry.value) is bool
    assert built.is_clean

    # The representative half of the same rule, which the value assertion above cannot see because
    # both keys there are the same interned literal. This port deliberately keeps the *first*
    # representative while the last value wins, where C#'s CreateRange keeps the last; without this
    # assertion adopting the C# rule is a free mutation.
    first, second = "Alpha", "ALPHA"
    assert case_insensitive(first, second) == 0
    duplicated: PersistentDeltaMap[str, int] = PersistentDeltaMap.from_items(
        [(first, 1), (second, 2)], case_insensitive
    )
    retained = duplicated.entry_at(0)
    assert retained.key is first, "from_items retains the first representative of a key class"
    assert retained.value == 2, "while the last value of that class wins"
    assert len(duplicated) == 1


def test_set_items_is_exactly_the_fold_of_set_item() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(1, "one"), (2, "two")])
    batch = [(3, "three"), (1, "ONE"), (2, "two"), (3, "THREE")]

    folded = source
    for key, value in batch:
        folded = folded.set_item(key, value)
    bulk = source.set_items(batch)

    assert contents(bulk) == contents(folded)
    assert list(bulk.get_changes()) == list(folded.get_changes())
    assert change_keys(bulk) == [1, 3]
    assert bulk[3] == "THREE"
    first = bulk.get_change(1)
    assert first is not None and endpoints(first) == ("one", "ONE")

    # An empty batch, and one made only of no-ops, both return the receiver.
    assert source.set_items([]) is source
    assert source.set_items([(1, "one"), (2, "two")]) is source
    assert contents(source) == [(1, "one"), (2, "two")]
    bulk.validate_structure()
    folded.validate_structure()


def test_retained_versions_evolve_independently() -> None:
    root: IntMap = PersistentDeltaMap.from_items([(index, f"v{index}") for index in range(16)])
    left = root.set_item(0, "left").set_item(100, "left-only")
    right = root.set_item(0, "right").remove(1)
    committed = left.checkpoint().set_item(0, "after-checkpoint")

    assert change_keys(left) == [0, 100]
    assert change_keys(right) == [0, 1]
    assert change_keys(committed) == [0]

    left_change = left.get_change(0)
    right_change = right.get_change(0)
    committed_change = committed.get_change(0)
    assert left_change is not None and endpoints(left_change) == ("v0", "left")
    assert right_change is not None and endpoints(right_change) == ("v0", "right")
    assert committed_change is not None
    # The committed branch measures against its own newer checkpoint, not the original one.
    assert endpoints(committed_change) == ("left", "after-checkpoint")

    assert contents(root) == [(index, f"v{index}") for index in range(16)]
    assert root.is_clean
    for version in (root, left, right, committed):
        version.validate_structure()


def test_the_ordered_read_surface_reports_the_current_state() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(10, "a"), (20, "b"), (30, "c")])
    changed = source.set_item(25, "new").remove(10)

    assert len(changed) == 3
    assert changed.size == 3
    assert not changed.is_empty
    assert bool(changed)
    assert 20 in changed and 10 not in changed
    assert changed.contains_key(30)
    assert changed.get(20) == "b"
    assert changed.get(10) is None
    assert changed[25] == "new"
    assert changed.index_of_key(25) == 1
    assert changed.index_of_key(10) is None
    assert changed.entry_at(0).key == 20
    minimum, maximum = changed.min_entry(), changed.max_entry()
    assert minimum is not None and minimum.key == 20
    assert maximum is not None and maximum.key == 30
    assert changed.keys() == [20, 25, 30]
    assert changed.values() == ["b", "new", "c"]
    assert [entry.key for entry in changed.to_list()] == [20, 25, 30]

    for finder, probe, expected in (
        (changed.floor_entry, 25, 25),
        (changed.ceiling_entry, 21, 25),
        (changed.lower_entry, 25, 20),
        (changed.higher_entry, 25, 30),
    ):
        found = finder(probe)
        assert found is not None and found.key == expected
    assert changed.lower_entry(20) is None
    assert changed.higher_entry(30) is None

    assert [entry.key for entry in changed.get_key_range(20, 25)] == [20, 25]
    assert len(changed.get_key_range(30, 20)) == 0

    empty: IntMap = PersistentDeltaMap.empty()
    assert empty.is_empty and not bool(empty) and len(empty) == 0
    assert empty.min_entry() is None and empty.max_entry() is None
    assert "change_count=2" in repr(changed)
    assert "is_clean=True" in repr(empty)


def test_error_conventions_are_index_key_and_assertion() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(1, "one")])

    with pytest.raises(IndexError):
        source.entry_at(1)
    with pytest.raises(IndexError):
        source.entry_at(-1)
    with pytest.raises(KeyError):
        source[2]

    # A record absent at both endpoints is not a net change and is never produced; asking for its
    # classification is an internal invariant violation.
    impossible: PersistentMapChange[int, str] = PersistentMapChange(1, None, None)
    with pytest.raises(AssertionError):
        _ = impossible.kind


def test_validate_structure_rejects_a_forged_representation() -> None:
    source: IntMap = PersistentDeltaMap.from_items([(1, "one"), (2, "two")])

    # A version whose change index is empty must reuse the exact checkpoint root.
    drifted: IntMap = PersistentDeltaMap(
        source.current_snapshot().set_item(1, "ONE"),
        source.checkpoint_snapshot(),
        SortedMap.empty(source.comparator),
        source.comparator,
        source.value_equals,
    )
    with pytest.raises(AssertionError, match="does not reuse its checkpoint root"):
        drifted.validate_structure()

    # A record whose after endpoint contradicts the current state.
    lying_changes: SortedMap[int, _DeltaRecord[str]] = SortedMap.from_iterable(
        [(1, _DeltaRecord(DeltaMapValue("one"), DeltaMapValue("ONE")))], source.comparator
    )
    lying: IntMap = PersistentDeltaMap(
        source.current_snapshot(),
        source.checkpoint_snapshot(),
        lying_changes,
        source.comparator,
        source.value_equals,
    )
    with pytest.raises(AssertionError, match="after endpoint differs"):
        lying.validate_structure()

    # A record with equivalent endpoints should have cancelled instead of being stored.
    idle_changes: SortedMap[int, _DeltaRecord[str]] = SortedMap.from_iterable(
        [(1, _DeltaRecord(DeltaMapValue("one"), DeltaMapValue("one")))], source.comparator
    )
    idle: IntMap = PersistentDeltaMap(
        source.current_snapshot(),
        source.checkpoint_snapshot(),
        idle_changes,
        source.comparator,
        source.value_equals,
    )
    with pytest.raises(AssertionError, match="equivalent endpoints"):
        idle.validate_structure()

    # A consistent record for key 1, but a current addition nothing accounts for.
    honest_changes: SortedMap[int, _DeltaRecord[str]] = SortedMap.from_iterable(
        [(1, _DeltaRecord(DeltaMapValue("one"), DeltaMapValue("ONE")))], source.comparator
    )
    unaccounted: IntMap = PersistentDeltaMap(
        source.current_snapshot().set_item(1, "ONE").set_item(5, "five"),
        source.checkpoint_snapshot(),
        honest_changes,
        source.comparator,
        source.value_equals,
    )
    with pytest.raises(AssertionError, match="current addition has no recorded change"):
        unaccounted.validate_structure()

    # The mirror image: a checkpoint entry that vanished without a record.
    vanished: IntMap = PersistentDeltaMap(
        source.current_snapshot().set_item(1, "ONE").remove(2),
        source.checkpoint_snapshot(),
        honest_changes,
        source.comparator,
        source.value_equals,
    )
    with pytest.raises(AssertionError, match="checkpoint difference has no recorded change"):
        vanished.validate_structure()


def test_natural_value_equality_prefers_identity() -> None:
    class Awkward:
        def __eq__(self, other: object) -> bool:
            raise AssertionError("This value refuses to be compared.")

    value = Awkward()
    assert natural_value_equality(value, value)
    # Distinct objects, so ``==`` really has to decide these.
    assert natural_value_equality(["a"], ["a"])
    assert not natural_value_equality(["a"], ["b"])

    # Reflexivity survives even for a value whose ``__eq__`` cannot be called, which is what the
    # cancellation rule needs.
    source: PersistentDeltaMap[int, Awkward] = PersistentDeltaMap.from_items([(1, value)])
    assert source.set_item(1, value) is source


def test_randomized_histories_match_the_checkpoint_and_current_models() -> None:
    rng = random.Random(20260805)
    checkpoint_model: dict[int, str] = {}
    current_model: dict[int, str] = {}
    live: IntMap = PersistentDeltaMap.empty()
    retained: list[tuple[IntMap, dict[int, str], dict[int, str]]] = []

    for step in range(500):
        action = rng.random()
        key = rng.randrange(0, 24)
        if action < 0.50:
            value = f"v{rng.randrange(0, 4)}"
            live = live.set_item(key, value)
            current_model[key] = value
        elif action < 0.78:
            live = live.remove(key)
            current_model.pop(key, None)
        elif action < 0.90:
            live = live.checkpoint()
            checkpoint_model = dict(current_model)
        else:
            live = live.rollback()
            current_model = dict(checkpoint_model)

        statistics = live.validate_structure()
        assert contents(live) == sorted(current_model.items())
        assert statistics.count == len(current_model)
        assert statistics.checkpoint_count == len(checkpoint_model)
        assert live.is_clean == (current_model == checkpoint_model)

        expected: dict[int, tuple[object, object]] = {
            candidate: (
                checkpoint_model.get(candidate, _ABSENT),
                current_model.get(candidate, _ABSENT),
            )
            for candidate in set(checkpoint_model) | set(current_model)
            if checkpoint_model.get(candidate, _ABSENT) != current_model.get(candidate, _ABSENT)
        }
        actual = {change.key: endpoints(change) for change in live.get_changes()}
        assert actual == expected
        assert change_keys(live) == sorted(expected)
        assert statistics.change_count == len(expected)

        low, high = sorted((rng.randrange(0, 24), rng.randrange(0, 24)))
        assert range_keys(live, low, high) == [
            key for key in sorted(expected) if low <= key <= high
        ]

        if step % 50 == 0:
            retained.append((live, dict(checkpoint_model), dict(current_model)))

    # Every retained version still reproduces the model it was captured with.
    assert len(retained) == 10
    for version, baseline, state in retained:
        version.validate_structure()
        assert contents(version) == sorted(state.items())
        assert {entry.key: entry.value for entry in version.checkpoint_snapshot()} == baseline
