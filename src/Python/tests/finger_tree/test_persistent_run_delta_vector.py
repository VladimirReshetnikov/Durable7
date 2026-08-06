"""Tests for the fixed-length persistent vector with a maximal-run change index.

Covers the empty and range factories, relation-equal writes as receiver-returning no-ops, the four
run-index edit shapes (left merge, right merge, both merge, and the four-way remove split), the
output-optimal descriptor enumeration, comparison-free structural accept and revert of one indexed
run with a live-counter positive control, both run addressing modes, exact checkpoint-representative
restoration under a payload whose identity is genuinely observable, the reflexivity obligation that
``NaN`` payloads impose on the retained relation, O(1) checkpoint and rollback root swaps, the
independent structural audit and the counterexamples it must reject, atomicity under a raising
relation, and a randomized cross-check against a list model over retained branching versions.

Structural-sharing claims here are always made across two *distinct* values, or paired with a
negative control, because an operation that returns its receiver would turn a sharing assertion into
a self-comparison that cannot fail.
"""

from __future__ import annotations

import random
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field

import pytest

from durable7.finger_tree.equality import natural_value_equality, reflexive_ieee_equality
from durable7.finger_tree.persistent_run_delta_vector import (
    PersistentDirtyRun,
    PersistentRunDeltaVector,
    RunDeltaVectorStatistics,
    _Cell,
)
from durable7.finger_tree.rrb_vector import RrbVector, _Branch
from durable7.finger_tree.sorted import SortedMap


@dataclass
class _CountingEquality:
    """Natural equality that records how many times the vector consulted the relation."""

    calls: int = 0

    def __call__(self, left: int, right: int) -> bool:
        """Compare two payloads, counting the call."""

        self.calls += 1
        return left == right


@dataclass
class _RaisingEquality:
    """Natural equality that raises on one armed call, standing in for a throwing comparer."""

    calls: int = 0
    armed: int = 0

    def arm(self, call: int) -> None:
        """Make the ``call``-th subsequent invocation raise."""

        self.calls = 0
        self.armed = call

    def __call__(self, left: int, right: int) -> bool:
        """Compare two payloads, raising when this is the armed call."""

        self.calls += 1
        if self.calls == self.armed:
            self.armed = 0
            raise RuntimeError("value relation failure")
        return left == right


@dataclass(frozen=True, slots=True)
class _Label:
    """A payload equal by value but never interned, so ``is`` on it stays meaningful."""

    text: str


def _fold_case(left: str, right: str) -> bool:
    """Case-insensitive string equivalence, coarser than ``==``."""

    return left.casefold() == right.casefold()


def _empty_runs() -> SortedMap[int, int]:
    return SortedMap.empty()


_OPERATIONS = ["set"] * 8 + ["reset"] * 2 + ["checkpoint", "rollback", "accept", "revert"]


def _expected_runs(current: list[int], checkpoint: list[int]) -> list[PersistentDirtyRun]:
    """Recompute the maximal runs by a straightforward linear scan of the two models."""

    runs: list[PersistentDirtyRun] = []
    index = 0
    while index < len(current):
        if current[index] == checkpoint[index]:
            index += 1
            continue
        start = index
        index += 1
        while index < len(current) and current[index] != checkpoint[index]:
            index += 1
        runs.append(PersistentDirtyRun(start, index - start))
    return runs


def _assert_matches_model(
    vector: PersistentRunDeltaVector[int], current: list[int], checkpoint: list[int]
) -> None:
    """Assert that a version agrees with an independently maintained pair of list models."""

    assert len(vector) == len(current)
    assert vector.to_list() == current
    assert vector.checkpoint_to_list() == checkpoint
    runs = _expected_runs(current, checkpoint)
    for index in range(len(current)):
        assert vector[index] == current[index]
        assert vector.checkpoint_value(index) == checkpoint[index]
        containing = next((run for run in runs if run.contains(index)), None)
        assert vector.is_dirty(index) is (containing is not None)
        assert vector.dirty_run_containing(index) == containing
    assert vector.dirty_count == sum(run.length for run in runs)
    assert vector.dirty_run_count == len(runs)
    assert list(vector.dirty_runs()) == runs
    for rank, run in enumerate(runs):
        assert vector.dirty_run_at(rank) == run
    with pytest.raises(IndexError):
        vector.dirty_run_at(len(runs))
    vector.validate_structure()


def test_empty_and_range_factories_have_exact_contracts() -> None:
    empty: PersistentRunDeltaVector[int] = PersistentRunDeltaVector.empty()
    assert len(empty) == 0
    assert empty.size == 0
    assert empty.is_empty
    assert not empty
    assert not empty.has_changes
    assert empty.is_clean
    assert empty.dirty_count == 0
    assert empty.dirty_run_count == 0
    assert list(empty) == []
    assert empty.to_list() == []
    assert list(empty.dirty_runs()) == []
    assert empty.checkpoint() is empty
    assert empty.rollback() is empty
    assert empty.validate_structure() == RunDeltaVectorStatistics(0, 0, 0, 0, True)

    for probe in (
        lambda: empty[0],
        lambda: empty.checkpoint_value(0),
        lambda: empty.is_dirty(0),
        lambda: empty.dirty_run_containing(0),
        lambda: empty.dirty_run_at(0),
        lambda: empty.set_item(0, 1),
        lambda: empty.reset_item(0),
        lambda: empty.accept_dirty_run_at(0),
        lambda: empty.revert_dirty_run_at(0),
        lambda: empty.accept_dirty_run_containing(0),
        lambda: empty.revert_dirty_run_containing(0),
        lambda: empty.shares_representative_at(0),
    ):
        with pytest.raises(IndexError):
            probe()

    built = PersistentRunDeltaVector.from_values(range(4))
    assert built.to_list() == [0, 1, 2, 3]
    assert built.checkpoint_to_list() == [0, 1, 2, 3]
    assert not built.has_changes
    assert built.is_clean
    assert built.value_equals is natural_value_equality
    assert built[-1] == 3
    assert built.checkpoint_value(-4) == 0
    assert built.shares_representative_at(-1)
    assert "dirty_run_count=0" in repr(built)
    with pytest.raises(IndexError):
        built[-5]
    with pytest.raises(IndexError):
        built[4]

    run = PersistentDirtyRun(3, 2)
    assert str(run) == "[3, 5)"
    assert run.end_exclusive == 5
    assert run.contains(3) and run.contains(4)
    assert not run.contains(2) and not run.contains(5)
    assert PersistentDirtyRun(1, 1) < PersistentDirtyRun(1, 2) < PersistentDirtyRun(2, 1)
    for start, length in ((-1, 1), (0, 0), (0, -1)):
        with pytest.raises(ValueError, match="never empty"):
            PersistentDirtyRun(start, length)


def test_relation_equal_write_is_a_receiver_returning_no_op() -> None:
    source = PersistentRunDeltaVector.from_values(["Alpha", "Beta"], _fold_case)
    no_op = source.set_item(0, "ALPHA")

    # The result *is* the receiver, so a sharing predicate applied to it could not fail. Identity is
    # the only assertion that carries information here.
    assert no_op is source
    assert not no_op.has_changes
    assert no_op.dirty_count == 0
    assert no_op.dirty_run_count == 0
    assert no_op[0] == "Alpha"
    assert no_op.shares_representative_at(0)

    # Negative control: the sharing predicate is falsifiable between independently built roots.
    twin = PersistentRunDeltaVector.from_values(["Alpha", "Beta"], _fold_case)
    assert twin.to_list() == source.to_list()
    assert not twin.shares_current_root_with(source)
    assert not twin.shares_checkpoint_root_with(source)
    assert source.shares_current_root_with(source)

    dirty = source.set_item(0, "Gamma")
    assert dirty is not source
    assert dirty[0] == "Gamma"
    assert dirty.checkpoint_value(0) == "Alpha"
    assert list(dirty.dirty_runs()) == [PersistentDirtyRun(0, 1)]
    # The successor kept the checkpoint root it started from; that is a genuine cross-value claim.
    assert dirty.shares_checkpoint_root_with(source)
    assert not dirty.shares_current_root_with(source)

    # A rewrite that is relation-equal to the *current* value is still a no-op on the dirty version.
    assert dirty.set_item(0, "GAMMA") is dirty

    cancelled = dirty.set_item(0, "alpha")
    assert not cancelled.has_changes
    assert cancelled[0] == "Alpha"
    assert cancelled.shares_current_root_with(source)
    assert cancelled.shares_checkpoint_root_with(source)
    cancelled.validate_structure()
    # The retained intermediate version is untouched.
    assert dirty[0] == "Gamma"
    dirty.validate_structure()


def test_point_edits_maintain_exact_maximal_dirty_runs() -> None:
    source = PersistentRunDeltaVector.from_values([0] * 12)
    vector = source.set_item(2, 20).set_item(4, 40)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(2, 1), PersistentDirtyRun(4, 1)]
    vector.validate_structure()

    # A new position between two singleton runs merges both.
    vector = vector.set_item(3, 30)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(2, 3)]

    # A detached position (right merge is not available), then the gap that joins it leftwards.
    vector = vector.set_item(6, 60)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(2, 3), PersistentDirtyRun(6, 1)]
    vector = vector.set_item(5, 50)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(2, 5)]

    # A fresh position immediately before an existing run extends it leftwards.
    vector = vector.set_item(1, 10)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(1, 6)]
    vector = vector.reset_item(1)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(2, 5)]

    # Interior clearing splits one run in two.
    vector = vector.reset_item(4)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(2, 2), PersistentDirtyRun(5, 2)]
    # Left-edge clearing shrinks from the start.
    vector = vector.reset_item(2)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(3, 1), PersistentDirtyRun(5, 2)]
    # Right-edge clearing shrinks from the end.
    vector = vector.reset_item(6)
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(3, 1), PersistentDirtyRun(5, 1)]
    vector.validate_structure()
    assert vector.dirty_count == 2

    # Clearing the last dirty position canonicalizes the current root onto the checkpoint root.
    cleared = vector.reset_item(3).reset_item(5)
    assert not cleared.has_changes
    assert cleared.is_clean
    assert list(cleared.dirty_runs()) == []
    assert cleared.to_list() == [0] * 12
    assert cleared is not source
    assert cleared.shares_current_root_with(source)
    assert cleared.shares_checkpoint_root_with(source)
    cleared.validate_structure()

    # Resetting an already clean position is a receiver-returning no-op.
    assert cleared.reset_item(3) is cleared
    assert cleared.set_item(3, 0) is cleared

    # Every retained intermediate version still reports its own runs.
    assert list(vector.dirty_runs()) == [PersistentDirtyRun(3, 1), PersistentDirtyRun(5, 1)]


def test_run_descriptors_are_output_optimal_for_clustered_deltas() -> None:
    length = 2048
    relation = _CountingEquality()
    source = PersistentRunDeltaVector.from_values([0] * length, relation)

    edited = source
    for index in range(length - 2):
        edited = edited.set_item(index, 1)
    edited = edited.set_item(length - 1, 1)
    expected = [PersistentDirtyRun(0, length - 2), PersistentDirtyRun(length - 1, 1)]

    assert edited.dirty_count == length - 1
    assert edited.dirty_run_count == 2
    # Thousands of dirty positions, exactly two descriptors: the descriptor count tracks r and not
    # k, and enumerating the index consults no value relation at all.
    before = relation.calls
    assert list(edited.dirty_runs()) == expected
    assert relation.calls == before

    # Doubling the dirty positions inside the same two clusters leaves the output size unchanged.
    smaller = PersistentRunDeltaVector.from_values([0] * length, relation)
    for index in range(length // 4):
        smaller = smaller.set_item(index, 1)
    smaller = smaller.set_item(length - 1, 1)
    assert smaller.dirty_run_count == 2
    assert len(list(smaller.dirty_runs())) == len(list(edited.dirty_runs()))
    assert smaller.dirty_count * 3 < edited.dirty_count

    edited.validate_structure()
    assert source.to_list() == [0] * length


def test_indexed_run_splices_are_structural_and_comparison_free() -> None:
    relation = _CountingEquality()
    original = PersistentRunDeltaVector.from_values(range(20), relation)
    edited = original
    for index in [*range(2, 6), *range(9, 14), 17]:
        edited = edited.set_item(index, 1000 + index)
    expected = [PersistentDirtyRun(2, 4), PersistentDirtyRun(9, 5), PersistentDirtyRun(17, 1)]
    assert list(edited.dirty_runs()) == expected
    assert edited.dirty_count == 10

    # Positive control: the counter is live, so a zero delta below means something.
    before = relation.calls
    edited.set_item(0, -1)
    assert relation.calls > before

    before = relation.calls
    accepted = edited.accept_dirty_run_at(1)
    assert relation.calls == before
    assert list(accepted.dirty_runs()) == [PersistentDirtyRun(2, 4), PersistentDirtyRun(17, 1)]
    for index in range(9, 14):
        assert accepted[index] == 1000 + index
        assert accepted.checkpoint_value(index) == 1000 + index
        assert accepted.shares_representative_at(index)
    # The accepted version reuses the receiver's current root untouched and rebuilds only the
    # checkpoint spine; both halves are claims about two distinct values.
    assert accepted is not edited
    assert accepted.shares_current_root_with(edited)
    assert not accepted.shares_checkpoint_root_with(edited)

    before = relation.calls
    reverted = accepted.revert_dirty_run_at(0)
    assert relation.calls == before
    assert list(reverted.dirty_runs()) == [PersistentDirtyRun(17, 1)]
    for index in range(2, 6):
        assert reverted[index] == index
        assert reverted.shares_representative_at(index)
    assert reverted.shares_checkpoint_root_with(accepted)
    assert not reverted.shares_current_root_with(accepted)

    before = relation.calls
    clean = reverted.accept_dirty_run_at(0)
    assert relation.calls == before
    assert not clean.has_changes
    assert clean.is_clean
    assert clean.checkpoint_value(17) == 1017
    assert clean.shares_current_root_with(reverted)

    # Accepting a two-thousand-position run costs no comparisons, and the splice really is
    # structural: the two roots share no leaf before it and dozens of leaves afterwards, which a
    # per-position rebuild of the changed range could not produce.
    length = 2048
    long_source = PersistentRunDeltaVector.from_values([0] * length, relation)
    long_edited = long_source
    for index in range(length - 48):
        long_edited = long_edited.set_item(index, 1)
    long_edited = long_edited.set_item(length - 8, 1)
    assert list(long_edited.dirty_runs()) == [
        PersistentDirtyRun(0, length - 48),
        PersistentDirtyRun(length - 8, 1),
    ]
    assert long_edited.shared_leaf_count() == 0
    before = relation.calls
    long_accepted = long_edited.accept_dirty_run_at(0)
    long_reverted = long_edited.revert_dirty_run_at(0)
    assert relation.calls == before
    assert long_accepted.shared_leaf_count() > 32
    assert long_reverted.shared_leaf_count() > 32
    assert list(long_accepted.dirty_runs()) == [PersistentDirtyRun(length - 8, 1)]
    assert long_accepted.checkpoint_to_list() == [1] * (length - 48) + [0] * 48
    assert long_reverted.to_list() == [0] * (length - 8) + [1] + [0] * 7

    # Every retained source version is untouched and still valid.
    assert list(edited.dirty_runs()) == expected
    assert not original.has_changes
    assert original.to_list() == list(range(20))
    for version in (
        original,
        edited,
        accepted,
        reverted,
        clean,
        long_edited,
        long_accepted,
        long_reverted,
    ):
        version.validate_structure()
    for bad_rank in (3, -4):
        with pytest.raises(IndexError):
            edited.accept_dirty_run_at(bad_rank)
        with pytest.raises(IndexError):
            edited.revert_dirty_run_at(bad_rank)
    assert edited.dirty_run_at(-1) == PersistentDirtyRun(17, 1)


def test_runs_are_addressable_by_rank_and_by_contained_position() -> None:
    relation = _CountingEquality()
    original = PersistentRunDeltaVector.from_values(range(16), relation)
    edited = original
    for index in [*range(2, 6), *range(9, 12)]:
        edited = edited.set_item(index, 1000 + index)
    expected = [PersistentDirtyRun(2, 4), PersistentDirtyRun(9, 3)]
    assert list(edited.dirty_runs()) == expected

    for bad_index in (16, -17):
        with pytest.raises(IndexError):
            edited.accept_dirty_run_containing(bad_index)
        with pytest.raises(IndexError):
            edited.revert_dirty_run_containing(bad_index)

    # A clean position is vacuous rather than an error: the receiver itself comes back, unchanged
    # and without consulting the relation. Identity is asserted because a sharing predicate on the
    # receiver could not fail.
    for index in (0, 6, 8, 15):
        assert not edited.is_dirty(index)
        assert edited.dirty_run_containing(index) is None
        before = relation.calls
        assert edited.accept_dirty_run_containing(index) is edited
        assert edited.revert_dirty_run_containing(index) is edited
        assert relation.calls == before

    # Every position inside a run acts exactly as the rank-addressed operation on that run.
    for rank, run in enumerate(expected):
        for index in range(run.start, run.end_exclusive):
            assert edited.dirty_run_containing(index) == run
            before = relation.calls
            by_position_accept = edited.accept_dirty_run_containing(index)
            by_position_revert = edited.revert_dirty_run_containing(index)
            assert relation.calls == before
            by_rank_accept = edited.accept_dirty_run_at(rank)
            by_rank_revert = edited.revert_dirty_run_at(rank)
            for by_position, by_rank in (
                (by_position_accept, by_rank_accept),
                (by_position_revert, by_rank_revert),
            ):
                assert by_position.to_list() == by_rank.to_list()
                assert by_position.checkpoint_to_list() == by_rank.checkpoint_to_list()
                assert by_position.dirty_count == by_rank.dirty_count
                assert list(by_position.dirty_runs()) == list(by_rank.dirty_runs())
                by_position.validate_structure()

    # Negative positions address the same runs from the end.
    assert edited.dirty_run_containing(-16) is None
    assert edited.dirty_run_containing(-16 + 9) == PersistentDirtyRun(9, 3)

    # The sole-run case still collapses onto the whole-checkpoint and whole-rollback paths.
    single = original.set_item(4, 4000).set_item(5, 5000)
    accepted = single.accept_dirty_run_containing(5)
    assert not accepted.has_changes
    assert accepted.checkpoint_value(4) == 4000
    assert accepted.shares_current_root_with(single)
    reverted = single.revert_dirty_run_containing(4)
    assert not reverted.has_changes
    assert reverted.to_list() == list(range(16))
    assert reverted.shares_current_root_with(original)

    assert list(edited.dirty_runs()) == expected
    for version in (original, edited, single, accepted, reverted):
        version.validate_structure()


def test_cancellation_restores_the_exact_checkpoint_representative() -> None:
    # ``_Label`` is equal by value but never interned, so ``is`` on the payload is meaningful. A
    # small int or a short literal string would make every identity assertion below vacuous.
    checkpoint_value = _Label("seven")
    twin_value = _Label("seven")
    assert checkpoint_value == twin_value
    assert checkpoint_value is not twin_value

    source = PersistentRunDeltaVector.from_values([checkpoint_value, _Label("other")])
    assert source[0] is checkpoint_value
    assert source.shares_representative_at(0)

    dirty = source.set_item(0, _Label("changed"))
    assert dirty.is_dirty(0)
    assert not dirty.shares_representative_at(0)
    assert dirty.checkpoint_value(0) is checkpoint_value

    # Writing a value that is merely *equal* to the checkpoint must restore the checkpoint's own
    # representative, not the equal object the caller supplied.
    cancelled = dirty.set_item(0, twin_value)
    assert not cancelled.has_changes
    assert cancelled[0] == twin_value
    assert cancelled[0] is checkpoint_value
    assert cancelled[0] is not twin_value
    assert cancelled.shares_representative_at(0)
    assert cancelled.shares_current_root_with(source)
    cancelled.validate_structure()

    # ``reset_item`` takes the same path.
    reset = dirty.reset_item(0)
    assert not reset.has_changes
    assert reset[0] is checkpoint_value
    assert reset.shares_representative_at(0)
    reset.validate_structure()

    # The decisive shape: cancel one position while another stays dirty, so the successor cannot
    # canonicalize onto the checkpoint root and the restored representative has to be the real one.
    # Without this case a wholly cancelled version's root swap would hide a fresh-cell restoration.
    survivor = _Label("survivor")
    pair = PersistentRunDeltaVector.from_values([checkpoint_value, survivor])
    both_dirty = pair.set_item(0, _Label("changed")).set_item(1, _Label("also changed"))
    assert both_dirty.dirty_run_count == 1
    partly_cancelled = both_dirty.set_item(0, twin_value)
    assert list(partly_cancelled.dirty_runs()) == [PersistentDirtyRun(1, 1)]
    assert not partly_cancelled.is_clean
    assert not partly_cancelled.shares_current_root_with(pair)
    assert partly_cancelled[0] is checkpoint_value
    assert partly_cancelled[0] is not twin_value
    assert partly_cancelled.shares_representative_at(0)
    assert not partly_cancelled.shares_representative_at(1)
    partly_cancelled.validate_structure()

    # A coarser relation makes the distinction observable at the value level too: the surviving
    # representative is the checkpoint's casing, never the cancelling write's.
    folded = PersistentRunDeltaVector.from_values(["Alpha", "Beta"], _fold_case)
    round_trip = folded.set_item(0, "Gamma").set_item(1, "Delta").set_item(0, "ALPHA")
    assert round_trip[0] == "Alpha"
    assert round_trip.shares_representative_at(0)
    assert list(round_trip.dirty_runs()) == [PersistentDirtyRun(1, 1)]
    round_trip.validate_structure()


def test_nan_payloads_require_a_reflexive_relation() -> None:
    nan = float("nan")
    # The trap, stated as executable facts: bare float equality is not reflexive, identity is, and a
    # container hides the difference because it compares elementwise by identity first.
    assert nan != nan
    assert nan is nan
    assert [nan] == [nan]

    assert natural_value_equality(nan, nan)
    assert not natural_value_equality(nan, float("nan"))
    assert reflexive_ieee_equality(nan, nan)
    assert reflexive_ieee_equality(nan, float("nan"))
    assert reflexive_ieee_equality(0.0, -0.0)
    assert not reflexive_ieee_equality(1.0, 2.0)
    assert reflexive_ieee_equality(1.0, 1.0)

    source = PersistentRunDeltaVector.from_values([nan, 1.0, 0.0], reflexive_ieee_equality)
    rewritten = source.set_item(0, float("nan"))
    assert rewritten is source
    assert rewritten.dirty_count == 0
    assert rewritten.dirty_run_count == 0
    rewritten.validate_structure()

    # Signed zeros are one class too, matching .NET's default comparer.
    assert source.set_item(2, -0.0) is source

    # A genuine change is still recorded, and returning any NaN cancels it.
    dirty = source.set_item(0, 2.0)
    assert list(dirty.dirty_runs()) == [PersistentDirtyRun(0, 1)]
    cancelled = dirty.set_item(0, float("nan"))
    assert not cancelled.has_changes
    assert cancelled.shares_representative_at(0)
    cancelled.validate_structure()

    # The default relation is reflexive too, because it tries identity first; a *distinct* NaN is a
    # distinct class under it, which is coarser than .NET but still a true equivalence relation.
    natural = PersistentRunDeltaVector.from_values([nan, 1.0])
    assert natural.set_item(0, nan) is natural
    assert natural.set_item(0, float("nan")).dirty_count == 1

    # Negative control: a non-reflexive relation leaves a phantom run that no audit could disprove,
    # which is precisely why the relation contract demands reflexivity.
    def naive(left: float, right: float) -> bool:
        return left == right

    phantom = PersistentRunDeltaVector.from_values([nan], naive).set_item(0, nan)
    assert phantom.dirty_count == 1
    assert phantom[0] is nan
    assert phantom.checkpoint_value(0) is nan


def test_checkpoint_and_rollback_are_root_swaps() -> None:
    source = PersistentRunDeltaVector.from_values(range(8))
    # A clean version returns its receiver; sharing predicates on it would be self-comparisons.
    assert source.checkpoint() is source
    assert source.rollback() is source

    dirty = source.set_item(3, 300).set_item(4, 400)
    assert dirty.dirty_count == 2

    accepted = dirty.checkpoint()
    assert accepted is not dirty
    assert accepted.is_clean
    assert accepted.to_list() == dirty.to_list()
    assert accepted.checkpoint_to_list() == dirty.to_list()
    # Two distinct values sharing one root: the O(1) swap kept the current root in place.
    assert accepted.shares_current_root_with(dirty)
    assert not accepted.shares_checkpoint_root_with(dirty)
    accepted.validate_structure()

    rolled = dirty.rollback()
    assert rolled is not dirty
    assert rolled.is_clean
    assert rolled.to_list() == list(range(8))
    assert rolled.shares_checkpoint_root_with(dirty)
    assert rolled.shares_current_root_with(source)
    assert not rolled.shares_current_root_with(dirty)
    rolled.validate_structure()

    # The retained dirty version is untouched by either swap.
    assert dirty.dirty_count == 2
    assert list(dirty.dirty_runs()) == [PersistentDirtyRun(3, 2)]
    dirty.validate_structure()


def test_validate_structure_rejects_corrupted_representations() -> None:
    source = PersistentRunDeltaVector.from_values(range(4))
    dirty = source.set_item(1, 9)
    assert dirty.validate_structure() == RunDeltaVectorStatistics(4, 1, 1, 3, False)

    def rebuild(
        runs: SortedMap[int, int],
        dirty_count: int,
        base: PersistentRunDeltaVector[int] = dirty,
    ) -> PersistentRunDeltaVector[int]:
        """Assemble a deliberately broken version out of an otherwise valid one's private roots."""

        return PersistentRunDeltaVector(
            base._current, base._checkpoint, runs, dirty_count, base.value_equals
        )

    # Root lengths must agree.
    other = PersistentRunDeltaVector.from_values(range(3))
    mismatched = PersistentRunDeltaVector(
        dirty._current, other._checkpoint, dirty._dirty_runs, 1, dirty.value_equals
    )
    with pytest.raises(AssertionError, match="different lengths"):
        mismatched.validate_structure()

    with pytest.raises(AssertionError, match="outside the position count"):
        rebuild(dirty._dirty_runs, 99).validate_structure()

    # A run index claiming more than the cardinality records is rejected.
    with pytest.raises(AssertionError, match="do not sum"):
        rebuild(dirty._dirty_runs.set_item(1, 3), 1).validate_structure()

    # Adjacent records are not maximal.
    with pytest.raises(AssertionError, match="adjacent"):
        rebuild(_empty_runs().set_item(1, 2).set_item(2, 3), 2).validate_structure()

    # A version left uncanonicalized after its last change cleared is rejected.
    with pytest.raises(AssertionError, match="canonicalized"):
        rebuild(_empty_runs(), 0).validate_structure()

    # An empty index with a non-zero cardinality is rejected on the canonical root.
    stale = PersistentRunDeltaVector(
        source._current, source._checkpoint, _empty_runs(), 1, source.value_equals
    )
    with pytest.raises(AssertionError, match="non-zero dirty cardinality"):
        stale.validate_structure()

    # A genuinely dirty position left outside every run is rejected.
    with pytest.raises(AssertionError, match="exact checkpoint representative"):
        rebuild(_empty_runs().set_item(3, 4), 1).validate_structure()

    # A run over positions that are relation-equal to their checkpoint is rejected.
    untruthful = PersistentRunDeltaVector(
        source._current, source._checkpoint, _empty_runs().set_item(3, 4), 1, source.value_equals
    )
    with pytest.raises(AssertionError, match="relation-equal"):
        untruthful.validate_structure()

    # A structurally invalid RRB root is surfaced as an invariant failure, not a bare ValueError.
    intact_root = dirty._current._root
    assert intact_root is not None
    broken_root: RrbVector[_Cell[int]] = RrbVector()
    broken_root._root = _Branch((intact_root,))
    with pytest.raises(AssertionError, match="current RRB root"):
        PersistentRunDeltaVector(
            broken_root, dirty._checkpoint, dirty._dirty_runs, 1, dirty.value_equals
        ).validate_structure()
    with pytest.raises(AssertionError, match="checkpoint RRB root"):
        PersistentRunDeltaVector(
            dirty._current, broken_root, dirty._dirty_runs, 1, dirty.value_equals
        ).validate_structure()

    # The valid version came through every counterexample unharmed.
    assert dirty.validate_structure() == RunDeltaVectorStatistics(4, 1, 1, 3, False)


def test_relation_failure_publishes_no_partial_version() -> None:
    relation = _RaisingEquality()
    source = PersistentRunDeltaVector.from_values([1, 2, 3], relation)

    # The second relation call is the checkpoint comparison, immediately before a successor would be
    # constructed.
    relation.arm(2)
    with pytest.raises(RuntimeError, match="value relation failure"):
        source.set_item(1, 99)

    assert source.to_list() == [1, 2, 3]
    assert not source.has_changes
    source.validate_structure()

    edited = source.set_item(1, 99)
    assert edited.to_list() == [1, 99, 3]
    assert list(edited.dirty_runs()) == [PersistentDirtyRun(1, 1)]
    assert source.to_list() == [1, 2, 3]

    relation.arm(1)
    with pytest.raises(RuntimeError, match="value relation failure"):
        edited.reset_item(1)
    assert edited.to_list() == [1, 99, 3]
    edited.validate_structure()


def test_retained_versions_are_independent_under_concurrent_readers() -> None:
    initial = [index % 17 for index in range(512)]
    source = PersistentRunDeltaVector.from_values(initial)
    for index in range(100, 200):
        source = source.set_item(index, -index)
    expected_current = source.to_list()

    def worker(seed: int) -> list[int]:
        """Derive a private branch while reading the shared snapshot."""

        branch = source
        current = list(expected_current)
        for iteration in range(100):
            index = (seed * 97 + iteration * 31) % len(current)
            assert source[index] == expected_current[index]
            value = seed * 10_000 + iteration
            branch = branch.set_item(index, value)
            current[index] = value
        branch.validate_structure()
        return branch.to_list() if branch.to_list() == current else []

    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(worker, range(4)))
    assert all(results)
    _assert_matches_model(source, expected_current, initial)


def test_randomized_retained_versions_match_an_independent_model() -> None:
    length = 40
    rng = random.Random(0xD17E_2026)
    initial = [index % 7 for index in range(length)]
    versions: list[_Version] = [
        _Version(PersistentRunDeltaVector.from_values(initial), list(initial), list(initial))
    ]

    for _ in range(2000):
        parent = versions[rng.randrange(len(versions))]
        current = list(parent.current)
        checkpoint = list(parent.checkpoint)
        result = parent.vector
        # Writes dominate on purpose: the wholesale cleaning operations must not be frequent enough
        # to keep the run index near-empty, or cancellation would almost always be the *last* one
        # and the canonicalizing root swap would mask how the representative was restored.
        choice = rng.choice(_OPERATIONS)

        if choice == "set":
            index = rng.randrange(length)
            # Aim a third of the writes at the checkpoint's own value, so the cancellation path --
            # the one that must restore the exact checkpoint representative rather than an equal
            # one -- is exercised while other runs are still dirty.
            value = checkpoint[index] if rng.randrange(3) == 0 else rng.randrange(-8, 9)
            result = result.set_item(index, value)
            current[index] = value
        elif choice == "reset":
            index = rng.randrange(length)
            result = result.reset_item(index)
            current[index] = checkpoint[index]
        elif choice == "checkpoint":
            result = result.checkpoint()
            checkpoint = list(current)
        elif choice == "rollback":
            result = result.rollback()
            current = list(checkpoint)
        else:
            runs = _expected_runs(current, checkpoint)
            if runs:
                rank = rng.randrange(len(runs))
                run = runs[rank]
                span = slice(run.start, run.end_exclusive)
                if choice == "accept":
                    result = result.accept_dirty_run_at(rank)
                    checkpoint[span] = current[span]
                else:
                    result = result.revert_dirty_run_containing(
                        run.start + rng.randrange(run.length)
                    )
                    current[span] = checkpoint[span]

        _assert_matches_model(result, current, checkpoint)
        # Deriving a branch leaves the parent version exactly as it was.
        _assert_matches_model(parent.vector, parent.current, parent.checkpoint)
        versions.append(_Version(result, current, checkpoint))
        if len(versions) > 24:
            versions.pop(1 + rng.randrange(len(versions) - 2))


@dataclass
class _Version:
    """One retained version together with the list models it must agree with."""

    vector: PersistentRunDeltaVector[int]
    current: list[int] = field(default_factory=list)
    checkpoint: list[int] = field(default_factory=list)
