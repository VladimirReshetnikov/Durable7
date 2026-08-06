"""Tests for the contextual rank/select sequence.

Covers the per-state effect table making a context-dependent scan associative, the deliberate
non-commutativity of ordered composition, single-descent event select reporting the emitting
element and its transition, multi-event transitions, the absent-result and exception contracts,
the machine-identity gate on concatenation, failure atomicity under an invalid or overflowing
machine, cached queries never re-invoking the machine, the structural audit and what it detects,
and agreement with an independent streaming scanner over exhaustive small words and randomized
edits.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import product
from typing import TypeVar

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.contextual_rank_sequence import (
    ContextualEventLocation,
    ContextualEventMachine,
    ContextualEventTransition,
    ContextualPrefixSummary,
    ContextualRankSequence,
    create_contextual_event_machine,
)

T = TypeVar("T")

_INT64_MAX = (1 << 63) - 1


class _QuotedCommaMachine:
    """Two states - outside quotes (0) and inside quotes (1). A comma outside quotes is an event."""

    state_count = 2

    def transition(self, state: int, element: str) -> ContextualEventTransition:
        """Toggle on a quote; emit one event for an unquoted comma; otherwise stand still."""

        if element == '"':
            return ContextualEventTransition(1 - state, 0)
        if element == "," and state == 0:
            return ContextualEventTransition(state, 1)
        return ContextualEventTransition(state, 0)


class _TernaryMachine:
    """Three states, with transitions that emit zero, one, or two events."""

    state_count = 3

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Shift by the element's magnitude and emit the state that shift lands in."""

        next_state = (state + abs(element)) % 3
        return ContextualEventTransition(next_state, next_state)


class _ResetShiftMachine:
    """Three states where ``"r"`` resets and ``"s"`` shifts, so the two do not commute.

    ``r`` moves to state 0 and emits nothing; ``s`` advances the state and emits the state it came
    from. Running ``r`` then ``s`` and running ``s`` then ``r`` disagree in both the outgoing state
    and the emitted count, which is exactly the property the measure must not average away.
    """

    state_count = 3

    def transition(self, state: int, element: str) -> ContextualEventTransition:
        """Reset on ``"r"``; otherwise advance and emit the incoming state."""

        if element == "r":
            return ContextualEventTransition(0, 0)
        return ContextualEventTransition((state + 1) % 3, state)


@dataclass(slots=True)
class _CountingMachine:
    """Counts every transition, so a test can prove a cached query never re-runs the machine."""

    calls: int = 0
    state_count: int = 2

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Alternate on odd elements, emitting one event whenever the parities agree."""

        self.calls += 1
        return ContextualEventTransition(
            (state + (element & 1)) & 1, int((element ^ state) & 3 == 0)
        )


class _InvalidStateMachine:
    """Declares one state but leaves it, violating the machine contract."""

    state_count = 1

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Return a state the machine never declared."""

        del state, element
        return ContextualEventTransition(1, 0)


class _NegativeEventMachine:
    """Emits a negative event count, which would break select's monotone predicate."""

    state_count = 1

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Return a count no prefix could ever contain."""

        del element
        return ContextualEventTransition(state, -1)


class _StatelessMachine:
    """Declares no states at all, which is a policy bug."""

    state_count = 0

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Stand still; the machine is rejected before this is ever reached."""

        del element
        return ContextualEventTransition(state, 0)


class _HugeEventMachine:
    """Emits the largest representable event count, so two elements overflow the composition."""

    state_count = 1

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Emit the signed 64-bit maximum from the single state."""

        del element
        return ContextualEventTransition(state, _INT64_MAX)


class _MutableMachine:
    """A machine whose declared shape can be changed after the fact, to drive the audit."""

    def __init__(self, state_count: int, emit: int) -> None:
        """Declare ``state_count`` states, every transition emitting ``emit`` events."""

        self.state_count = state_count
        self.emit = emit

    def transition(self, state: int, element: int) -> ContextualEventTransition:
        """Stay in the incoming state, emitting the currently configured count."""

        del element
        return ContextualEventTransition(state, self.emit)


def _scan(
    machine: ContextualEventMachine[T], model: list[T], initial_state: int
) -> tuple[list[ContextualPrefixSummary], list[ContextualEventLocation]]:
    """Run the machine over ``model`` the naive streaming way, recording every boundary and event.

    This is the independent reference the measured structure is checked against: it replays the
    whole prefix for each answer, which is exactly what the cached effect tables exist to avoid.
    """

    state = initial_state
    events = 0
    prefixes = [ContextualPrefixSummary(state, events)]
    locations: list[ContextualEventLocation] = []
    for index, element in enumerate(model):
        step = machine.transition(state, element)
        for ordinal in range(step.event_count):
            locations.append(
                ContextualEventLocation(index, ordinal, state, step.next_state, step.event_count)
            )
        state = step.next_state
        events += step.event_count
        prefixes.append(ContextualPrefixSummary(state, events))
    return prefixes, locations


def _assert_matches_model(
    sequence: ContextualRankSequence[T], model: list[T], machine: ContextualEventMachine[T]
) -> None:
    """Check a sequence against a list model plus a direct machine scan, for every state."""

    assert sequence.to_list() == model
    assert list(sequence) == model
    assert len(sequence) == len(model)
    assert sequence.is_empty == (not model)

    statistics = sequence.validate_structure()
    assert statistics.count == len(model)
    assert statistics.state_count == machine.state_count

    for index, element in enumerate(model):
        assert sequence.get_at(index) == element
        assert sequence[index] == element

    maximum = 0
    for initial_state in range(machine.state_count):
        prefixes, locations = _scan(machine, model, initial_state)
        for boundary, expected in enumerate(prefixes):
            assert sequence.evaluate_prefix(boundary, initial_state) == expected
            assert sequence.event_rank(boundary, initial_state) == expected.event_count
        assert sequence.evaluate(initial_state) == prefixes[-1]
        for rank, expected_location in enumerate(locations):
            assert sequence.try_select_event(rank, initial_state) == expected_location
        assert sequence.try_select_event(len(locations), initial_state) is None
        assert sequence.try_select_event(-1, initial_state) is None
        maximum = max(maximum, prefixes[-1].event_count)
    assert statistics.maximum_total_event_count == maximum


def test_contextual_quoted_delimiter_ranks_and_selects_by_left_context() -> None:
    machine = _QuotedCommaMachine()
    text = ContextualRankSequence.from_iterable('"a,b",c,d', machine)

    assert text.evaluate(0) == ContextualPrefixSummary(0, 2)
    assert text.evaluate(1) == ContextualPrefixSummary(1, 1)
    assert text.event_rank(5, 0) == 0
    assert text.event_rank(6, 0) == 1

    assert text.try_select_event(0, 0) == ContextualEventLocation(5, 0, 0, 0, 1)
    located = text.try_select_event(1, 0)
    assert located is not None and located.element_index == 7
    assert text.try_select_event(2, 0) is None

    # Starting inside a quoted region makes the comma at index 2 an event instead.
    started_inside = text.try_select_event(0, 1)
    assert started_inside is not None and started_inside.element_index == 2

    # Editing publishes a new version and leaves the retained one exactly as it was.
    edited = text.insert(0, '"')
    assert text.evaluate(0) == ContextualPrefixSummary(0, 2)
    assert edited.evaluate(0) == ContextualPrefixSummary(1, 1)
    assert text.to_list() == list('"a,b",c,d')
    _assert_matches_model(text, list('"a,b",c,d'), machine)
    _assert_matches_model(edited, list('""a,b",c,d'), machine)


def test_contextual_composition_is_ordered_and_not_commutative() -> None:
    machine = _ResetShiftMachine()
    reset = ContextualRankSequence.from_iterable("r", machine)
    shift = ContextualRankSequence.from_iterable("s", machine)

    # Both components of the lifted measure depend on which operand runs first.
    assert reset.concat(shift).evaluate(1) == ContextualPrefixSummary(1, 0)
    assert shift.concat(reset).evaluate(1) == ContextualPrefixSummary(0, 1)
    assert reset.concat(shift).to_list() == ["r", "s"]
    assert shift.concat(reset).to_list() == ["s", "r"]

    # Composition is still associative, so grouping the same order cannot change the answer.
    middle = ContextualRankSequence.from_iterable("srs", machine)
    left_grouped = reset.concat(middle).concat(shift)
    right_grouped = reset.concat(middle.concat(shift))
    for state in range(machine.state_count):
        assert left_grouped.evaluate(state) == right_grouped.evaluate(state)
    _assert_matches_model(left_grouped, list("rsrss"), machine)


def test_contextual_multi_event_transitions_and_boundary_splits() -> None:
    machine = _TernaryMachine()
    sequence = ContextualRankSequence.from_iterable((2, -3, 4, 0), machine)
    _assert_matches_model(sequence, [2, -3, 4, 0], machine)

    # The ternary machine must actually exercise a transition emitting more than one event.
    assert any(
        (sequence.event_rank(index + 1, 0) or 0) - (sequence.event_rank(index, 0) or 0) > 1
        for index in range(len(sequence))
    )
    located = sequence.try_select_event(0, 0)
    assert located is not None
    assert located.event_index_in_element < located.element_event_count

    for index in range(len(sequence) + 1):
        split = sequence.split_at(index)
        assert split.left.concat(split.right).to_list() == sequence.to_list()
        assert len(split.left) + len(split.right) == len(sequence)
        _assert_matches_model(split.left, sequence.to_list()[:index], machine)
        _assert_matches_model(split.right, sequence.to_list()[index:], machine)

    empty = ContextualRankSequence.empty(machine)
    assert sequence.shares_storage_with(sequence.concat(empty))
    assert sequence.shares_storage_with(empty.concat(sequence))
    assert sequence.shares_storage_with(sequence.get_range(0, len(sequence)))
    assert sequence.shares_storage_with(sequence.split_at(0).right)
    assert sequence.shares_storage_with(sequence.split_at(len(sequence)).left)


def test_contextual_absent_results_and_exception_contract() -> None:
    machine = _TernaryMachine()
    sequence = ContextualRankSequence.from_iterable((2, -3, 4, 0), machine)

    # An out-of-range state or event rank is an ordinary miss, reported as absence.
    assert sequence.evaluate(3) is None
    assert sequence.evaluate(-1) is None
    assert sequence.evaluate_prefix(0, 3) is None
    assert sequence.event_rank(2, 3) is None
    assert sequence.try_select_event(0, 3) is None
    assert sequence.try_select_event(-1, 0) is None
    assert sequence.try_select_event(1 << 40, 0) is None

    # An out-of-range index is a caller error, not a miss.
    for index in (-1, len(sequence) + 1):
        with pytest.raises(IndexError):
            sequence.evaluate_prefix(index, 0)
        with pytest.raises(IndexError):
            sequence.split_at(index)
        with pytest.raises(IndexError):
            sequence.insert(index, 1)
    with pytest.raises(IndexError):
        sequence.get_at(len(sequence))
    with pytest.raises(IndexError):
        _ = sequence[-1]
    with pytest.raises(IndexError):
        sequence.set_item(len(sequence), 1)
    with pytest.raises(IndexError):
        sequence.remove_at(len(sequence))
    with pytest.raises(IndexError):
        sequence.get_range(-1, 1)
    with pytest.raises(TypeError):
        _ = sequence["0"]  # type: ignore[index]

    # A malformed count is an invalid argument rather than an index.
    with pytest.raises(ValueError, match="nonnegative"):
        sequence.get_range(0, -1)
    with pytest.raises(ValueError, match="past the end"):
        sequence.get_range(1, len(sequence))

    empty = ContextualRankSequence.empty(machine)
    assert empty.is_empty
    assert empty.evaluate(2) == ContextualPrefixSummary(2, 0)
    assert empty.evaluate_prefix(0, 1) == ContextualPrefixSummary(1, 0)
    assert empty.try_select_event(0, 0) is None
    assert empty.get_range(0, 0).is_empty
    assert empty.validate_structure().count == 0

    singleton = empty.append(5)
    assert len(singleton) == 1 and singleton.get_at(0) == 5
    assert singleton.remove_at(0).is_empty
    _assert_matches_model(singleton, [5], machine)
    _assert_matches_model(empty.prepend(7), [7], machine)


def test_contextual_concatenation_gates_on_machine_identity() -> None:
    first = _QuotedCommaMachine()
    second = _QuotedCommaMachine()
    left = ContextualRankSequence.from_iterable("a,b", first)
    right = ContextualRankSequence.from_iterable(",c", first)
    stranger = ContextualRankSequence.from_iterable(",c", second)

    assert left.concat(right).to_list() == list("a,b,c")
    assert left.machine is first
    with pytest.raises(TypeError, match="same contextual event machine"):
        left.concat(stranger)
    with pytest.raises(TypeError, match="ContextualRankSequence"):
        left.concat("nope")  # type: ignore[arg-type]

    # A sequence already built on this exact machine is returned unchanged.
    assert ContextualRankSequence.from_iterable(left, first) is left
    assert ContextualRankSequence.from_iterable(left, second).to_list() == left.to_list()
    assert ContextualRankSequence.from_iterable(left, second) is not left


def test_contextual_retained_versions_stay_independent_after_branching() -> None:
    machine = _QuotedCommaMachine()
    source = ContextualRankSequence.from_iterable("a,b,c", machine)
    quoted = source.insert(0, '"')
    trimmed = source.remove_at(1)
    replaced = source.set_item(1, '"')

    assert source.evaluate(0) == ContextualPrefixSummary(0, 2)
    assert quoted.evaluate(0) == ContextualPrefixSummary(1, 0)
    assert trimmed.evaluate(0) == ContextualPrefixSummary(0, 1)
    assert replaced.evaluate(0) == ContextualPrefixSummary(1, 0)

    _assert_matches_model(source, list("a,b,c"), machine)
    _assert_matches_model(quoted, list('"a,b,c'), machine)
    _assert_matches_model(trimmed, list("ab,c"), machine)
    _assert_matches_model(replaced, list('a"b,c'), machine)


def test_contextual_machine_contract_violations_are_failure_atomic() -> None:
    with pytest.raises(ValueError, match="at least one state"):
        ContextualRankSequence.empty(_StatelessMachine())
    with pytest.raises(ValueError, match="at least one state"):
        ContextualRankSequence.from_iterable((1, 2), _StatelessMachine())

    invalid = ContextualRankSequence.empty(_InvalidStateMachine())
    with pytest.raises(ValueError, match="invalid next state"):
        invalid.append(1)
    assert invalid.is_empty

    negative = ContextualRankSequence.empty(_NegativeEventMachine())
    with pytest.raises(ValueError, match="negative event count"):
        negative.append(1)
    assert negative.is_empty

    source = ContextualRankSequence.empty(_HugeEventMachine()).append(1)
    assert source.evaluate(0) == ContextualPrefixSummary(0, _INT64_MAX)
    # The substrate defers summary combination, matching the reference's lazy core, so the
    # overflow surfaces on the first summary read of a successor rather than inside the edit
    # itself. Failure atomicity is preserved in the form the laziness contract gives it: a
    # raising force publishes nothing, every retained version stays valid, and the read is
    # retryable - both reads below raise, proving the first failure memoized no poison value.
    for overflowing in (
        source.append(2),
        source.prepend(2),
        source.concat(source),
        source.insert(0, 2),
        source.insert(1, 2),
    ):
        with pytest.raises(OverflowError):
            overflowing.evaluate(0)
        with pytest.raises(OverflowError):
            overflowing.evaluate(0)

    # The retained version is untouched and fully usable after every failed force.
    assert source.to_list() == [1]
    assert source.evaluate(0) == ContextualPrefixSummary(0, _INT64_MAX)
    assert source.validate_structure().maximum_total_event_count == _INT64_MAX

    def unrepresentable(state: int, element: int) -> ContextualEventTransition:
        """Emit one more event than a signed 64-bit count can hold."""

        del state, element
        return ContextualEventTransition(0, 1 << 63)

    over = create_contextual_event_machine(1, unrepresentable)
    with pytest.raises(OverflowError, match="signed 64-bit"):
        ContextualRankSequence.from_iterable((1,), over)


def test_contextual_cached_queries_never_reinvoke_the_machine() -> None:
    machine = _CountingMachine()
    sequence = ContextualRankSequence.from_iterable(range(2_048), machine)
    assert machine.calls == 2 * 2_048

    machine.calls = 0
    for boundary in range(0, len(sequence) + 1, 17):
        assert sequence.event_rank(boundary, boundary & 1) is not None
        assert sequence.evaluate_prefix(boundary, 0) is not None
    for index in range(0, len(sequence), 71):
        sequence.get_at(index)
    total = sequence.evaluate(0)
    assert total is not None and total.event_count > 0
    rank = 0
    while rank < total.event_count:
        assert sequence.try_select_event(rank, 0) is not None
        rank += 31
    split = sequence.split_at(1_000)
    split.left.concat(split.right)
    sequence.get_range(100, 500)
    assert machine.calls == 0, "cached queries must not invoke the machine"

    # Only a newly stored element costs transitions, and exactly one row of them.
    machine.calls = 0
    sequence.append(1)
    assert machine.calls == machine.state_count
    machine.calls = 0
    sequence.set_item(5, 3)
    assert machine.calls == machine.state_count


def test_contextual_validate_structure_reports_and_detects() -> None:
    machine = _QuotedCommaMachine()
    sequence = ContextualRankSequence.from_iterable('"a,b",c,d', machine)
    statistics = sequence.validate_structure()
    assert statistics.count == 9
    assert statistics.state_count == 2
    assert statistics.maximum_total_event_count == 2
    assert sequence.state_count == 2

    drifting = _MutableMachine(1, 1)
    audited = ContextualRankSequence.from_iterable((1, 2, 3), drifting)
    assert audited.validate_structure().maximum_total_event_count == 3
    drifting.emit = 2
    with pytest.raises(ValueError, match="cached element summary"):
        audited.validate_structure()

    widening = _MutableMachine(1, 0)
    widened = ContextualRankSequence.from_iterable((1, 2), widening)
    widening.state_count = 2
    with pytest.raises(ValueError, match="state count changed"):
        widened.validate_structure()

    emptying = _MutableMachine(1, 0)
    emptied = ContextualRankSequence.from_iterable((1, 2), emptying)
    emptying.state_count = 0
    with pytest.raises(ValueError, match="at least one state"):
        emptied.validate_structure()


def test_contextual_functional_machine_adapter_matches_a_class_machine() -> None:
    reference = _QuotedCommaMachine()
    adapted = create_contextual_event_machine(2, reference.transition)
    model = list('x,"y,z",w')
    sequence = ContextualRankSequence.from_iterable(model, adapted)
    _assert_matches_model(sequence, model, adapted)
    assert sequence.evaluate(0) == ContextualRankSequence.from_iterable(model, reference).evaluate(
        0
    )


def test_contextual_stored_none_elements_stay_unambiguous() -> None:
    def gap_machine(state: int, element: int | None) -> ContextualEventTransition:
        """Toggle on a present element; emit one event for a gap seen in state 1."""

        if element is None:
            return ContextualEventTransition(state, 1 if state == 1 else 0)
        return ContextualEventTransition(1 - state, 0)

    machine = create_contextual_event_machine(2, gap_machine)
    model: list[int | None] = [1, None, 2, None, None]
    sequence = ContextualRankSequence.from_iterable(model, machine)
    _assert_matches_model(sequence, model, machine)

    assert sequence.get_at(1) is None
    assert sequence.evaluate(0) == ContextualPrefixSummary(0, 1)
    assert sequence.evaluate(1) == ContextualPrefixSummary(1, 2)
    located = sequence.try_select_event(0, 0)
    assert located == ContextualEventLocation(1, 0, 1, 1, 1)
    assert sequence.try_select_event(1, 0) is None
    assert sequence.remove_at(1).to_list() == [1, 2, None, None]


def test_contextual_exhaustive_small_words_match_independent_scanner() -> None:
    machine = _QuotedCommaMachine()
    for length in range(6):
        for word in product('",x', repeat=length):
            model = list(word)
            _assert_matches_model(
                ContextualRankSequence.from_iterable(model, machine), model, machine
            )


@settings(max_examples=40, deadline=None)
@given(
    st.lists(
        st.tuples(
            st.sampled_from(("prepend", "append", "insert", "set", "remove", "range", "concat")),
            st.integers(min_value=0, max_value=64),
            st.integers(min_value=-9, max_value=9),
        ),
        max_size=60,
    )
)
def test_contextual_random_edits_match_scanner_model(
    operations: list[tuple[str, int, int]],
) -> None:
    machine = _TernaryMachine()
    sequence = ContextualRankSequence.empty(machine)
    model: list[int] = []
    retained: list[tuple[ContextualRankSequence[int], list[int]]] = []
    for kind, raw, item in operations:
        if kind == "prepend":
            sequence = sequence.prepend(item)
            model.insert(0, item)
        elif kind == "append":
            sequence = sequence.append(item)
            model.append(item)
        elif kind == "insert":
            index = raw % (len(model) + 1)
            sequence = sequence.insert(index, item)
            model.insert(index, item)
        elif kind == "set" and model:
            index = raw % len(model)
            sequence = sequence.set_item(index, item)
            model[index] = item
        elif kind == "remove" and model:
            index = raw % len(model)
            sequence = sequence.remove_at(index)
            model.pop(index)
        elif kind == "range":
            index = raw % (len(model) + 1)
            count = (item + 9) % (len(model) - index + 1)
            sequence = sequence.get_range(index, count)
            model = model[index : index + count]
        elif kind == "concat" and len(model) <= 48:
            sequence = sequence.concat(sequence)
            model = model + model
        if len(model) <= 24:
            retained.append((sequence, model.copy()))
    _assert_matches_model(sequence, model, machine)
    for snapshot, expected in retained[-6:]:
        _assert_matches_model(snapshot, expected, machine)
